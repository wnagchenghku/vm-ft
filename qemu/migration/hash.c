#include <stdlib.h>
#include <stdint.h>
#include <pthread.h>
#include <stdio.h>
#include <unistd.h>
#include <inttypes.h>
#include "migration/vmft/hash.h"


#define nthread 20

#define ALGO_TEST

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

    sum = (sum >> HASH_SIZE) + (sum & HASH_MASK);	/* add hi 32 to low 32 */
    sum += (sum >> HASH_SIZE);			/* add carry */
    answer = ~sum;				/* truncate to 16 bits */
    return(answer);
} 


static hash_t hashofhash(hash_t *a, hash_t *b){
	hash_t ret;
	hash_sum_t sum = 0;
	hash_t answer = 0;
	sum = (*a + *b);
	sum = (sum >> HASH_SIZE) + (sum & HASH_MASK);
	answer = ~sum;
	return answer;

}




struct merkle_tree_t {
	hash_t *tree;
	uint64_t tree_size;
	uint8_t full;
	uint64_t full_part_last_index; 
	uint64_t first_leaf_index; 
	uint64_t last_level_leaf_count;
};

typedef struct merkle_tree_t merkle_tree_t; 

struct hash_list {
	hash_t *hashes;
	uint64_t len; 
};

typedef struct hash_list hash_list;


/*******
Global Variables. 
********/

//Currently using multiple pthread_variable to do 

static pthread_t *ts; 
static int indices[nthread];
static pthread_mutex_t *compute_locks; 
static pthread_cond_t *compute_conds;

//static unsigned long *bitmap;



// For count all the "both dirtied" pages
static unsigned long *dirty_indices; 
static unsigned long dirty_count; 


static merkle_tree_t *mtree; 
static hash_list *hlist;  


#ifdef ALGO_TEST
static uint8_t *fake_page; 
#endif

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


static inline uint8_t* get_page_addr(unsigned long index){
	return fake_page;
}

static inline void compute_hash(unsigned long i){
	#ifdef USE_MERKLE_TREE
	mtree->tree[index_to_node(i)] = hashofpage(get_page_addr(i), 4096);
	#else
	hlist->hashes[i] = hashofpage(get_page_addr(i), 4096);
	#endif
}



void *compute_thread_func(void *arg){
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

		pthread_spin_lock(&finished_lock);
		finished_thread++;
		pthread_spin_unlock(&finished_lock);
	}

}



void hash_init(unsigned long len){

	int i; 

	ts = (pthread_t *) malloc (nthread * sizeof(pthread_t));
	compute_locks = (pthread_mutex_t *) malloc(nthread * sizeof(pthread_mutex_t));
	compute_conds = (pthread_cond_t *)malloc (nthread * sizeof(pthread_cond_t));
	pthread_spin_init (&finished_lock, 0);

	for (i = 0; i < nthread; i++){
		pthread_mutex_init(&compute_locks[i], NULL);
		pthread_cond_init(&compute_conds[i], NULL);
		indices[i] = i;
		pthread_create(&ts[i], NULL, compute_thread_func, (void *)&indices[i]);
	}
	dirty_indices = (unsigned long *) malloc(len * sizeof(unsigned long));

	#ifdef ALGO_TEST
	fake_page = (uint8_t *) malloc (4096);
	#endif
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


	mtree-> tree_size = 1 << (tree_height) - 1 + 2 * (len - 1 << (tree_height-1));
	mtree-> tree = (hash_t *) malloc (mtree->tree_size * sizeof(hash_t));
	if (len - 1 << (tree_height-1) == 0) {
		mtree -> full = 1; 
	}
	else {
		mtree -> full = 0;
	}
	mtree -> first_leaf_index = (mtree-> tree_size - 1) / 2 ;
	mtree -> full_part_last_index = 1 << (tree_height) - 2; 
	mtree -> last_level_leaf_count =  2 * (len - 1 << (tree_height-1));


	int i ;
	for (i = 0; i<nthread; i++){
		pthread_mutex_lock(&compute_locks[i]);
		pthread_cond_broadcast(&compute_conds[i]);
		pthread_mutex_unlock(&compute_locks[i]);
	}
	while(finished_thread < nthread){
	}
	unsigned long j;
	for ( j= mtree->first_leaf_index-1 ; j >= 0; j--){
		mtree->tree[j]=hashofhash(&(mtree->tree[2*j +1]), &(mtree->tree[2*j + 2]));
	}
}

void compute_hash_list(unsigned long *bmap, unsigned long len){
	dirty_count = 0;
	update_dirty_indices(bmap, len);

	if (hlist != NULL){
		free(hlist);
	}

	hlist = (hash_list*) malloc( sizeof (hash_list));
	hlist -> hashes = (hash_t *) malloc(dirty_count * sizeof(hash_t));
	hlist -> len =  dirty_count;

	
	

	int i; 
	for (i = 0; i<nthread; i++){
		pthread_mutex_lock(&compute_locks[i]);
		pthread_cond_broadcast(&compute_conds[i]);
		pthread_mutex_unlock(&compute_locks[i]);
	}
	while(finished_thread < nthread){
	}
}

unsigned long *remote_hlist; 
static pthread_mutex_t *compare_locks; 
static pthread_cond_t *compare_conds;


void *compare_thread_func(void *arg){
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
			if (mem)
		}

	}
}

void compare_hash_list(unsigned long )

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


