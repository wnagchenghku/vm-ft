// #include <stdlib.h>
// #include <stdint.h>
// #include <pthread.h>
// #include <stdio.h>
// #include <unistd.h>
// #include <inttypes.h>
// #include <string.h>
// #include <stdint.h>
// #include <inttypes.h>





#include "qemu/osdep.h"
#include <zlib.h>
#include "qapi-event.h"
#include "qemu/cutils.h"
#include "qemu/bitops.h"
#include "qemu/bitmap.h"
#include "qemu/timer.h"
#include "qemu/main-loop.h"
#include "migration/migration.h"
#include "migration/postcopy-ram.h"
#include "exec/address-spaces.h"
#include "migration/page_cache.h"
#include "qemu/error-report.h"
#include "trace.h"
#include "exec/ram_addr.h"
#include "qemu/rcu_queue.h"
#include "migration/colo.h"
#include "mc-rdma.h"

#include "migration/hash.h"

#define nthread 20

//#define ALGO_TEST

//#define USE_MERKLE_TREE 



static hash_t hashofpage(uint8_t *data, int len){
	int	nleft = len;
    hash_sum_t sum = 0;
    hash_t *w = (uint64_t *) data;
    hash_t answer = 0;

    while (nleft > 1)  {
        sum += *w++;
        nleft -= HASH_SHIFT;
    }

    sum = (sum >> HASH_BITS) + (sum & HASH_MASK);	/* add hi 32 to low 32 */
    sum += (sum >> HASH_BITS);			/* add carry */
    answer = ~sum;				/* truncate to 16 bits */
    return(answer);
} 


static hash_t hashofhash(hash_t *a, hash_t *b){
	//hash_t ret;
	hash_sum_t sum = 0;
	hash_t answer = 0;
	sum = (*a + *b);
	sum = (sum >> HASH_BITS) + (sum & HASH_MASK);
	answer = ~sum;
	return answer;

}





/*******
Global Variables. 
********/

//Currently using multiple pthread_variable to do 
/*
Variable used by the computer threads
*/
static pthread_t *compute_ts; 
static int indices[nthread];
static pthread_mutex_t *compute_locks; 
static pthread_cond_t *compute_conds;

//static unsigned long *bitmap;



// For count all the "both dirtied" pages
static unsigned long *dirty_indices; 
static unsigned long dirty_count; 


static merkle_tree_t *mtree; 
static hash_list *hlist;  

/* For compare threads 

*/
static pthread_t *compare_ts; 
static hash_list *remote_hlist; 
static pthread_mutex_t *compare_locks; 
static pthread_cond_t *compare_conds;
static pthread_spinlock_t compare_spin_lock;
static uint64_t diverse_count; 
static int compare_complete_thread; 





hash_list* get_hash_list_pointer(void){
	return hlist;
}






static uint8_t *fake_page; 


static int finished_thread; 
static pthread_spinlock_t finished_lock;


static inline unsigned long index_to_node (unsigned long i){
	if ( i >= mtree->last_level_leaf_count ){
		return (i - mtree->last_level_leaf_count) + mtree->first_leaf_index; 
	}
	else{
		return (mtree->last_level_leaf_count + i + 1);
	}
}


static uint8_t* get_page_addr(uint64_t page_index){
	//return fake_page;
	RAMBlock *block; 
	//uint64_t offset; 


	QLIST_FOREACH_RCU(block, &ram_list.blocks, next){
		unsigned long base = block->offset >> TARGET_PAGE_BITS;
		//XS: TODO used length or max length. 
		unsigned long max = base + (block -> used_length >> TARGET_PAGE_BITS);
		if (page_index >= base && page_index <= max){
			return block->host + (page_index << TARGET_PAGE_BITS) - base ;
		} 
	}
	printf("\n\n***!!!overflow, check your logic !!***\n\n");
	return fake_page;

}

static inline void compute_hash(unsigned long i){
	#ifdef USE_MERKLE_TREE
	mtree->tree[index_to_node(i)] = hashofpage(get_page_addr(i), 4096);
	#else
	hlist->hashes[i] = hashofpage(get_page_addr(i), 4096);
	#endif
}



static void *compute_thread_func(void *arg){
	int t = *(int *)arg; //The thread index of the thread
	while(1){
		pthread_mutex_lock(&compute_locks[t]);
		pthread_cond_wait(&compute_conds[t], &compute_locks[t]);
		pthread_mutex_unlock(&compute_locks[t]);
	

		unsigned long workload = dirty_count / nthread + 1;
		unsigned long job_start = t * workload; 
		unsigned long job_end;
		

		if ( t == (nthread -1)) {
			job_end = dirty_count -1; 
		}
		else {
			job_end  = (t+1) * workload -1;  
		}
		unsigned long i; 
		for (i = job_start; i <= job_end; i++){
			compute_hash(i);
		}
		printf("[MT] Thread %d finished, workload = %lu\n", t, workload);
		pthread_spin_lock(&finished_lock);
		finished_thread++;
		pthread_spin_unlock(&finished_lock);
	}
	return NULL; 

}



static void *compare_thread_func(void *arg){
	int t = *(int *)arg; //The thread index of the thread
	while(1){
		pthread_mutex_lock(&compare_locks[t]);
		pthread_cond_wait(&compare_conds[t], &compare_locks[t]);
		pthread_mutex_unlock(&compare_locks[t]);
		uint64_t workload = hlist -> len / nthread + 1;
		uint64_t job_start = t * workload; 
		uint64_t job_end = (t+1) * workload - 1 ; 
		if (t + 1 == nthread){
			job_end = hlist -> len -1;
		}
		uint64_t i; 
		for (i = job_start; i <= job_end; i++){
			if (memcmp(&(remote_hlist->hashes[i]), &(hlist->hashes[i]), sizeof(hash_t)) != 0){
				pthread_spin_lock(&compare_spin_lock);
				diverse_count++;
				//TOOD: transfer the page
				pthread_spin_unlock(&compare_spin_lock);
			}
		}
		pthread_spin_lock(&compare_spin_lock);
		compare_complete_thread++;
		//TOOD: transfer the page
		pthread_spin_unlock(&compare_spin_lock);

	}
	return NULL;
}


void hash_init(void){


	int64_t ram_bitmap_pages = last_ram_offset() >> TARGET_PAGE_BITS;

	int i; 

	compute_ts = (pthread_t *) malloc (nthread * sizeof(pthread_t));
	compute_locks = (pthread_mutex_t *) malloc(nthread * sizeof(pthread_mutex_t));
	compute_conds = (pthread_cond_t *)malloc (nthread * sizeof(pthread_cond_t));
	pthread_spin_init (&finished_lock, 0);


	compare_ts = (pthread_t *) malloc (nthread * sizeof(pthread_t));
	compare_locks = (pthread_mutex_t *) malloc(nthread * sizeof(pthread_mutex_t));
	compare_conds = (pthread_cond_t *)malloc (nthread * sizeof(pthread_cond_t));
	pthread_spin_init (&compare_spin_lock, 0); 



	for (i = 0; i < nthread; i++){
		pthread_mutex_init(&compute_locks[i], NULL);
		pthread_cond_init(&compute_conds[i], NULL);
		indices[i] = i;
		pthread_create(&compute_ts[i], NULL, compute_thread_func, (void *)&indices[i]);

		pthread_mutex_init(&compare_locks[i], NULL);
		pthread_cond_init(&compare_conds[i], NULL);
		pthread_create(&compare_ts[i], NULL, compare_thread_func, (void*)&indices[i]);


	}


	dirty_indices = (unsigned long *) malloc(ram_bitmap_pages * sizeof(unsigned long));


	fake_page = (uint8_t *) malloc (4096);

}

static void update_dirty_indices(unsigned long *bitmap, unsigned long len){
	unsigned long mask; 
	int i, offset; 
	for (i =0; i * 64 < len; ++i){
		mask = 0x8000000000000000;
		for (offset = 0; offset <64; offset++){

			if (mask & bitmap[i]){	
				dirty_indices[dirty_count]=i*64 + offset; 
				dirty_count++;
			}
			mask >>= 1; 
		}
	}
}
//Construct a complete binary tree as http://mathworld.wolfram.com/images/eps-gif/CompleteBinaryTree_1000.gif

void build_merkle_tree (unsigned long *bmap, unsigned long len){
	dirty_count = 0;
	update_dirty_indices(bmap, len);

	mtree = (merkle_tree_t *) malloc (sizeof(merkle_tree_t));

		
	//get log base 2
	unsigned long c = dirty_count; 
	int tree_height = 1; 
	while ( c >>= 1) { ++tree_height; }
	// the tree height 


	mtree-> tree_size = (1 << (tree_height)) - 1 + 2 * (len - (1 << (tree_height-1)));
	mtree-> tree = (hash_t *) malloc (mtree->tree_size * sizeof(hash_t));
	if (len - (1 << (tree_height-1)) == 0) {
		mtree -> full = 1; 
	}
	else {
		mtree -> full = 0;
	}
	mtree -> first_leaf_index = (mtree-> tree_size - 1) / 2 ;
	mtree -> full_part_last_index = (1 << (tree_height)) - 2; 
	mtree -> last_level_leaf_count =  2 * (len - (1 << (tree_height-1)));


	int i ;
	for (i = 0; i<nthread; i++){
		pthread_mutex_lock(&compute_locks[i]);
		pthread_cond_broadcast(&compute_conds[i]);
		pthread_mutex_unlock(&compute_locks[i]);
	}
	while(finished_thread < nthread){
	}
	long j;
	for ( j= mtree->first_leaf_index-1 ; j >= 0; j--){
		mtree->tree[j]=hashofhash(&(mtree->tree[2*j +1]), &(mtree->tree[2*j + 2]));
	}
}

void compute_hash_list(unsigned long *bmap, unsigned long len){
	dirty_count = 0;
	finished_thread = 0;
	update_dirty_indices(bmap, len);

	if (hlist != NULL){
		free(hlist);
	}

	hlist = (hash_list*) malloc( sizeof (hash_list));
	hlist -> hashes = (hash_t *) malloc(dirty_count * sizeof(hash_t));
	hlist -> len =  dirty_count;

	printf("\n\nBefore waking up compute threads, dirty_count = %lu, hashlist->hashes addr = %p\n\n", hlist->len, hlist->hashes);
	int i; 
	for (i = 0; i<nthread; i++){
		pthread_mutex_lock(&compute_locks[i]);
		pthread_cond_broadcast(&compute_conds[i]);
		pthread_mutex_unlock(&compute_locks[i]);
	}
	
	while(1){
		pthread_spin_lock(&finished_lock);
		if (finished_thread == nthread){
			pthread_spin_unlock(&finished_lock);
			break;
		}
		pthread_spin_unlock(&finished_lock);
		usleep(100);
	}

	printf("compute hashlist finished, will return \n");


}




void compare_hash_list(hash_list *hlist){
	compare_complete_thread = 0;
	remote_hlist = hlist; 
	int i ; 
	for (i = 0; i<nthread; i++){
		pthread_mutex_lock(&compute_locks[i]);
		pthread_cond_broadcast(&compute_conds[i]);
		pthread_mutex_unlock(&compute_locks[i]);
	}
	while (compare_complete_thread < nthread) {

	}

	printf("Compared %"PRIu64 " pages, same = %" PRIu64" same rate = %"PRIu64"%%\n", hlist->len, hlist->len - diverse_count, (hlist->len - diverse_count) / hlist->len);
}

// int main(char* argv[], int argc){
// 	unsigned long xor_bitmap[2];
// 	xor_bitmap[0] = 1000000;
// 	xor_bitmap[1] = 1234567;
// 	init(128);
// 	sleep(1);
// 	compute_hash_list(xor_bitmap, 128);
// 	int i; 
// 	printf("dirty_count: %lu\n", dirty_count);
// 	for (i = 0; i<hlist->len; i++){
// 		printf("[%d] %"PRIu64"\n", i, hlist->hashes[i]);
// 	}
// }

//****!!!!! Remember to push on macbook. 
// unsigned long compare_merkle_tree(merkle_tree_t *tree_a, merkle_tree_t *tree_b){
// 	unsigned long count = 0;

// 	unsigned long *stk = (unsigned long*) malloc(tree_a->tree_size * sizeof(unsigned long));
// 	unsigned long top = -1;



// 	stk[top+1] = 0;
// 	top++; 
// 	unsigned long cur;
// 	while (top >= 0){
// 		cur = stk[top]; 
// 		top--;
// 		if (tree_a->tree[cur] == tree_b->tree[cur]){
// 			continue;
// 		}
// 		else{
// 			if (cur >= tree_a-> first_leaf_index){
				
// 			}
// 		}


// 	}
// }


