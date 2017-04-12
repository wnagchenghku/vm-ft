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
#include "net/net.h"



#define nthread 16

//#define ALGO_TEST

//#define USE_MERKLE_TREE 


#define HASH_CKSUM 0
#define HASH_CRC 1
#define HASH_FNV1 2
#define HASH_FNV1a 3
#define HASH_CITY64 4


#define HASH_FUNC HASH_CITY64


static hash_t cksum(uint8_t *data, int len){
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


//A very simple CRC implementation from redis
static const uint64_t crc64_tab[256] = {
    UINT64_C(0x0000000000000000), UINT64_C(0x7ad870c830358979),
    UINT64_C(0xf5b0e190606b12f2), UINT64_C(0x8f689158505e9b8b),
    UINT64_C(0xc038e5739841b68f), UINT64_C(0xbae095bba8743ff6),
    UINT64_C(0x358804e3f82aa47d), UINT64_C(0x4f50742bc81f2d04),
    UINT64_C(0xab28ecb46814fe75), UINT64_C(0xd1f09c7c5821770c),
    UINT64_C(0x5e980d24087fec87), UINT64_C(0x24407dec384a65fe),
    UINT64_C(0x6b1009c7f05548fa), UINT64_C(0x11c8790fc060c183),
    UINT64_C(0x9ea0e857903e5a08), UINT64_C(0xe478989fa00bd371),
    UINT64_C(0x7d08ff3b88be6f81), UINT64_C(0x07d08ff3b88be6f8),
    UINT64_C(0x88b81eabe8d57d73), UINT64_C(0xf2606e63d8e0f40a),
    UINT64_C(0xbd301a4810ffd90e), UINT64_C(0xc7e86a8020ca5077),
    UINT64_C(0x4880fbd87094cbfc), UINT64_C(0x32588b1040a14285),
    UINT64_C(0xd620138fe0aa91f4), UINT64_C(0xacf86347d09f188d),
    UINT64_C(0x2390f21f80c18306), UINT64_C(0x594882d7b0f40a7f),
    UINT64_C(0x1618f6fc78eb277b), UINT64_C(0x6cc0863448deae02),
    UINT64_C(0xe3a8176c18803589), UINT64_C(0x997067a428b5bcf0),
    UINT64_C(0xfa11fe77117cdf02), UINT64_C(0x80c98ebf2149567b),
    UINT64_C(0x0fa11fe77117cdf0), UINT64_C(0x75796f2f41224489),
    UINT64_C(0x3a291b04893d698d), UINT64_C(0x40f16bccb908e0f4),
    UINT64_C(0xcf99fa94e9567b7f), UINT64_C(0xb5418a5cd963f206),
    UINT64_C(0x513912c379682177), UINT64_C(0x2be1620b495da80e),
    UINT64_C(0xa489f35319033385), UINT64_C(0xde51839b2936bafc),
    UINT64_C(0x9101f7b0e12997f8), UINT64_C(0xebd98778d11c1e81),
    UINT64_C(0x64b116208142850a), UINT64_C(0x1e6966e8b1770c73),
    UINT64_C(0x8719014c99c2b083), UINT64_C(0xfdc17184a9f739fa),
    UINT64_C(0x72a9e0dcf9a9a271), UINT64_C(0x08719014c99c2b08),
    UINT64_C(0x4721e43f0183060c), UINT64_C(0x3df994f731b68f75),
    UINT64_C(0xb29105af61e814fe), UINT64_C(0xc849756751dd9d87),
    UINT64_C(0x2c31edf8f1d64ef6), UINT64_C(0x56e99d30c1e3c78f),
    UINT64_C(0xd9810c6891bd5c04), UINT64_C(0xa3597ca0a188d57d),
    UINT64_C(0xec09088b6997f879), UINT64_C(0x96d1784359a27100),
    UINT64_C(0x19b9e91b09fcea8b), UINT64_C(0x636199d339c963f2),
    UINT64_C(0xdf7adabd7a6e2d6f), UINT64_C(0xa5a2aa754a5ba416),
    UINT64_C(0x2aca3b2d1a053f9d), UINT64_C(0x50124be52a30b6e4),
    UINT64_C(0x1f423fcee22f9be0), UINT64_C(0x659a4f06d21a1299),
    UINT64_C(0xeaf2de5e82448912), UINT64_C(0x902aae96b271006b),
    UINT64_C(0x74523609127ad31a), UINT64_C(0x0e8a46c1224f5a63),
    UINT64_C(0x81e2d7997211c1e8), UINT64_C(0xfb3aa75142244891),
    UINT64_C(0xb46ad37a8a3b6595), UINT64_C(0xceb2a3b2ba0eecec),
    UINT64_C(0x41da32eaea507767), UINT64_C(0x3b024222da65fe1e),
    UINT64_C(0xa2722586f2d042ee), UINT64_C(0xd8aa554ec2e5cb97),
    UINT64_C(0x57c2c41692bb501c), UINT64_C(0x2d1ab4dea28ed965),
    UINT64_C(0x624ac0f56a91f461), UINT64_C(0x1892b03d5aa47d18),
    UINT64_C(0x97fa21650afae693), UINT64_C(0xed2251ad3acf6fea),
    UINT64_C(0x095ac9329ac4bc9b), UINT64_C(0x7382b9faaaf135e2),
    UINT64_C(0xfcea28a2faafae69), UINT64_C(0x8632586aca9a2710),
    UINT64_C(0xc9622c4102850a14), UINT64_C(0xb3ba5c8932b0836d),
    UINT64_C(0x3cd2cdd162ee18e6), UINT64_C(0x460abd1952db919f),
    UINT64_C(0x256b24ca6b12f26d), UINT64_C(0x5fb354025b277b14),
    UINT64_C(0xd0dbc55a0b79e09f), UINT64_C(0xaa03b5923b4c69e6),
    UINT64_C(0xe553c1b9f35344e2), UINT64_C(0x9f8bb171c366cd9b),
    UINT64_C(0x10e3202993385610), UINT64_C(0x6a3b50e1a30ddf69),
    UINT64_C(0x8e43c87e03060c18), UINT64_C(0xf49bb8b633338561),
    UINT64_C(0x7bf329ee636d1eea), UINT64_C(0x012b592653589793),
    UINT64_C(0x4e7b2d0d9b47ba97), UINT64_C(0x34a35dc5ab7233ee),
    UINT64_C(0xbbcbcc9dfb2ca865), UINT64_C(0xc113bc55cb19211c),
    UINT64_C(0x5863dbf1e3ac9dec), UINT64_C(0x22bbab39d3991495),
    UINT64_C(0xadd33a6183c78f1e), UINT64_C(0xd70b4aa9b3f20667),
    UINT64_C(0x985b3e827bed2b63), UINT64_C(0xe2834e4a4bd8a21a),
    UINT64_C(0x6debdf121b863991), UINT64_C(0x1733afda2bb3b0e8),
    UINT64_C(0xf34b37458bb86399), UINT64_C(0x8993478dbb8deae0),
    UINT64_C(0x06fbd6d5ebd3716b), UINT64_C(0x7c23a61ddbe6f812),
    UINT64_C(0x3373d23613f9d516), UINT64_C(0x49aba2fe23cc5c6f),
    UINT64_C(0xc6c333a67392c7e4), UINT64_C(0xbc1b436e43a74e9d),
    UINT64_C(0x95ac9329ac4bc9b5), UINT64_C(0xef74e3e19c7e40cc),
    UINT64_C(0x601c72b9cc20db47), UINT64_C(0x1ac40271fc15523e),
    UINT64_C(0x5594765a340a7f3a), UINT64_C(0x2f4c0692043ff643),
    UINT64_C(0xa02497ca54616dc8), UINT64_C(0xdafce7026454e4b1),
    UINT64_C(0x3e847f9dc45f37c0), UINT64_C(0x445c0f55f46abeb9),
    UINT64_C(0xcb349e0da4342532), UINT64_C(0xb1eceec59401ac4b),
    UINT64_C(0xfebc9aee5c1e814f), UINT64_C(0x8464ea266c2b0836),
    UINT64_C(0x0b0c7b7e3c7593bd), UINT64_C(0x71d40bb60c401ac4),
    UINT64_C(0xe8a46c1224f5a634), UINT64_C(0x927c1cda14c02f4d),
    UINT64_C(0x1d148d82449eb4c6), UINT64_C(0x67ccfd4a74ab3dbf),
    UINT64_C(0x289c8961bcb410bb), UINT64_C(0x5244f9a98c8199c2),
    UINT64_C(0xdd2c68f1dcdf0249), UINT64_C(0xa7f41839ecea8b30),
    UINT64_C(0x438c80a64ce15841), UINT64_C(0x3954f06e7cd4d138),
    UINT64_C(0xb63c61362c8a4ab3), UINT64_C(0xcce411fe1cbfc3ca),
    UINT64_C(0x83b465d5d4a0eece), UINT64_C(0xf96c151de49567b7),
    UINT64_C(0x76048445b4cbfc3c), UINT64_C(0x0cdcf48d84fe7545),
    UINT64_C(0x6fbd6d5ebd3716b7), UINT64_C(0x15651d968d029fce),
    UINT64_C(0x9a0d8ccedd5c0445), UINT64_C(0xe0d5fc06ed698d3c),
    UINT64_C(0xaf85882d2576a038), UINT64_C(0xd55df8e515432941),
    UINT64_C(0x5a3569bd451db2ca), UINT64_C(0x20ed197575283bb3),
    UINT64_C(0xc49581ead523e8c2), UINT64_C(0xbe4df122e51661bb),
    UINT64_C(0x3125607ab548fa30), UINT64_C(0x4bfd10b2857d7349),
    UINT64_C(0x04ad64994d625e4d), UINT64_C(0x7e7514517d57d734),
    UINT64_C(0xf11d85092d094cbf), UINT64_C(0x8bc5f5c11d3cc5c6),
    UINT64_C(0x12b5926535897936), UINT64_C(0x686de2ad05bcf04f),
    UINT64_C(0xe70573f555e26bc4), UINT64_C(0x9ddd033d65d7e2bd),
    UINT64_C(0xd28d7716adc8cfb9), UINT64_C(0xa85507de9dfd46c0),
    UINT64_C(0x273d9686cda3dd4b), UINT64_C(0x5de5e64efd965432),
    UINT64_C(0xb99d7ed15d9d8743), UINT64_C(0xc3450e196da80e3a),
    UINT64_C(0x4c2d9f413df695b1), UINT64_C(0x36f5ef890dc31cc8),
    UINT64_C(0x79a59ba2c5dc31cc), UINT64_C(0x037deb6af5e9b8b5),
    UINT64_C(0x8c157a32a5b7233e), UINT64_C(0xf6cd0afa9582aa47),
    UINT64_C(0x4ad64994d625e4da), UINT64_C(0x300e395ce6106da3),
    UINT64_C(0xbf66a804b64ef628), UINT64_C(0xc5bed8cc867b7f51),
    UINT64_C(0x8aeeace74e645255), UINT64_C(0xf036dc2f7e51db2c),
    UINT64_C(0x7f5e4d772e0f40a7), UINT64_C(0x05863dbf1e3ac9de),
    UINT64_C(0xe1fea520be311aaf), UINT64_C(0x9b26d5e88e0493d6),
    UINT64_C(0x144e44b0de5a085d), UINT64_C(0x6e963478ee6f8124),
    UINT64_C(0x21c640532670ac20), UINT64_C(0x5b1e309b16452559),
    UINT64_C(0xd476a1c3461bbed2), UINT64_C(0xaeaed10b762e37ab),
    UINT64_C(0x37deb6af5e9b8b5b), UINT64_C(0x4d06c6676eae0222),
    UINT64_C(0xc26e573f3ef099a9), UINT64_C(0xb8b627f70ec510d0),
    UINT64_C(0xf7e653dcc6da3dd4), UINT64_C(0x8d3e2314f6efb4ad),
    UINT64_C(0x0256b24ca6b12f26), UINT64_C(0x788ec2849684a65f),
    UINT64_C(0x9cf65a1b368f752e), UINT64_C(0xe62e2ad306bafc57),
    UINT64_C(0x6946bb8b56e467dc), UINT64_C(0x139ecb4366d1eea5),
    UINT64_C(0x5ccebf68aecec3a1), UINT64_C(0x2616cfa09efb4ad8),
    UINT64_C(0xa97e5ef8cea5d153), UINT64_C(0xd3a62e30fe90582a),
    UINT64_C(0xb0c7b7e3c7593bd8), UINT64_C(0xca1fc72bf76cb2a1),
    UINT64_C(0x45775673a732292a), UINT64_C(0x3faf26bb9707a053),
    UINT64_C(0x70ff52905f188d57), UINT64_C(0x0a2722586f2d042e),
    UINT64_C(0x854fb3003f739fa5), UINT64_C(0xff97c3c80f4616dc),
    UINT64_C(0x1bef5b57af4dc5ad), UINT64_C(0x61372b9f9f784cd4),
    UINT64_C(0xee5fbac7cf26d75f), UINT64_C(0x9487ca0fff135e26),
    UINT64_C(0xdbd7be24370c7322), UINT64_C(0xa10fceec0739fa5b),
    UINT64_C(0x2e675fb4576761d0), UINT64_C(0x54bf2f7c6752e8a9),
    UINT64_C(0xcdcf48d84fe75459), UINT64_C(0xb71738107fd2dd20),
    UINT64_C(0x387fa9482f8c46ab), UINT64_C(0x42a7d9801fb9cfd2),
    UINT64_C(0x0df7adabd7a6e2d6), UINT64_C(0x772fdd63e7936baf),
    UINT64_C(0xf8474c3bb7cdf024), UINT64_C(0x829f3cf387f8795d),
    UINT64_C(0x66e7a46c27f3aa2c), UINT64_C(0x1c3fd4a417c62355),
    UINT64_C(0x935745fc4798b8de), UINT64_C(0xe98f353477ad31a7),
    UINT64_C(0xa6df411fbfb21ca3), UINT64_C(0xdc0731d78f8795da),
    UINT64_C(0x536fa08fdfd90e51), UINT64_C(0x29b7d047efec8728),
};

static inline uint64_t crc64(uint64_t crc, const uint8_t *s, uint64_t l) {
    uint64_t j;

    for (j = 0; j < l; j++) {
        uint8_t byte = s[j];
        crc = crc64_tab[(uint8_t)crc ^ byte] ^ (crc >> 8);
    }
    return crc;
}


static inline uint64_t FNV1(const uint8_t *s, uint64_t l){
    uint64_t ret = 0xcbf29ce484222325; 
    uint64_t prime = 0x100000001b3; 
    int i ; 
    for (i = 0; i< l; i++){
        ret *= prime; 
        ret = ret ^ s[i]; 
    }
    return ret; 
}




static inline uint64_t FNV1a(const uint8_t *s, uint64_t l){
    uint64_t ret = 0xcbf29ce484222325; 
    uint64_t prime = 0x100000001b3; 
    int i ; 
    for (i = 0; i< l; i++){
        ret = ret ^ s[i]; 
        ret *= prime; 
    }
    return ret; 
}


static const uint64_t k1 = 0xb492b66fbe98f273ULL;

static inline uint64_t ShiftMix(uint64_t val) {
  return val ^ (val >> 47);
}

static inline uint64_t Fetch64(const uint8_t *p) {
    uint64_t ret; 
    memcpy(&ret, p, sizeof(ret));
    return ret; 
}

static inline uint64_t Rotate(uint64_t val, int shift) {
  // Avoid shifting by 64: doing so yields an undefined result.
  return shift == 0 ? val : ((val >> shift) | (val << (64 - shift)));
}




static uint64_t HashLen16(uint64_t u, uint64_t v) {
// Murmur-inspired hashing.
  const uint64_t kMul = 0x9ddfea08eb382d69ULL;
  uint64_t a = (u ^ v) * kMul;
  a ^= (a >> 47);
  uint64_t b = (v ^ a) * kMul;
  b ^= (b >> 47);
  b *= kMul;
  return b;
}


static void weak(uint64_t w, uint64_t x, uint64_t y, uint64_t z, uint64_t a, uint64_t b, uint64_t *ret1, uint64_t *ret2) {
  a += w;
  b = Rotate(b + a + z, 21);
  uint64_t c = a;
  a += x;
  a += y;
  b += Rotate(a, 44);
  *ret1 = a + z;
  *ret2 = b + c; 
  return;
}



static inline void WeakHashLen32WithSeeds(const uint8_t* s, uint64_t a, uint64_t b, uint64_t *ret1, uint64_t *ret2) {
  return weak(Fetch64(s),Fetch64(s + 8),Fetch64(s + 16),Fetch64(s + 24), a, b, ret1, ret2);
}


static uint64_t city_hash_64(const uint8_t * s, size_t len){
    uint64_t x = Fetch64(s + len -40);
    uint64_t y = Fetch64(s + len -16) + Fetch64(s + len -56);
    uint64_t z = HashLen16(Fetch64(s + len - 48) + len, Fetch64(s + len - 24));
    uint64_t v1, v2, w1, w2; 

    WeakHashLen32WithSeeds(s + len - 64, len, z, &v1, &v2);
    WeakHashLen32WithSeeds(s + len - 32, y + k1, x, &w1, &w2);
    x = x * k1 + Fetch64(s);
    uint64_t tmp; 


    len -= 64; 
    do {
        x = Rotate(x + y + v1 + Fetch64(s + 8), 37) * k1;
        y = Rotate(y + v2 + Fetch64(s + 48), 42) * k1;
        x ^= w2;
        y += v1 + Fetch64(s + 40);
        z = Rotate(z + w1, 33) * k1;
        WeakHashLen32WithSeeds(s, v2 * k1, x + w1, &v1, &v2);
        WeakHashLen32WithSeeds(s + 32, z + w2, y + Fetch64(s + 16), &w1, &w2);
        tmp = z; 
        z = x; 
        x =tmp; 
        s += 64;
        len -= 64;
    } while (len != 0);
    return HashLen16(HashLen16(v1, w1) + ShiftMix(y) * k1 + z,
                   HashLen16(v2, w2) + x);

}


static hash_t hashofpage(uint8_t *page , int len){
#if HASH_FUNC == HASH_CKSUM
    return cksum(page, len);

#elif HASH_FUNC == HASH_CRC
    return crc64(0, page, len);

#elif HASH_FUNC == HASH_FNV1
    return FNV1(page, len);

#elif HASH_FUNC == HASH_FNV1a
    return FNV1a(page, len);

#elif HASH_FUNC == HASH_CITY64
    return city_hash_64(page, len);

#endif


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
// static unsigned long *dirty_indices; 
// static unsigned long dirty_count; 


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

hash_list* get_remote_hash_list_pointer(void){
    return remote_hlist;
}

// void print_hash_list(hash_list *list){
// 	uint64_t i; 
// 	for (i = 0; i<list->len; i++){
// 		printf("%"PRIu64"\n", list->hashes[i]);
// 	}
// 	printf("*******end of hash list\n");
// }




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
		unsigned long first_page = block->offset >> TARGET_PAGE_BITS;
		//XS: TODO used length or max length. 
		unsigned long last_page = ((block->offset + block->max_length) >> TARGET_PAGE_BITS)-1;
		if (page_index >= first_page && page_index <= last_page ){
			return block->host + ((page_index-first_page) << TARGET_PAGE_BITS)  ;
		} 
	}
	printf("\n\n***!!!overflow, check your logic !!***\n\n");
	fflush(stdout);
	return fake_page;

}

static inline hash_t compute_hash(unsigned long page_index){
	// #ifdef USE_MERKLE_TREE
	// mtree->tree[index_to_node(i)] = hashofpage(get_page_addr(i), 4096);
	// #else



	//hlist->hashes[i] = 

    return hashofpage(get_page_addr(page_index), 4096);
	// #endif
}

static unsigned long *compute_bitmap; 


static void *compute_thread_func(void *arg){
 int t = *(int *)arg; //The thread index of the thread
 while(1){
     pthread_mutex_lock(&compute_locks[t]);
     pthread_cond_wait(&compute_conds[t], &compute_locks[t]);
     pthread_mutex_unlock(&compute_locks[t]);
    
     // printf("thread %d stated \n", t);
     // fflush(stdout);
     
     int64_t ram_bitmap_pages = last_ram_offset() >> TARGET_PAGE_BITS;

     int i, offset; 


     int count = 0; 

     unsigned long mask;

     for(i = 0; i * 64 < ram_bitmap_pages; i++){
        mask = 1 << t;

        for (offset = t; offset <64 && i*64 +offset < ram_bitmap_pages; offset+=nthread){
            if (mask & compute_bitmap[i]){  
                hlist->hashes[t][count] = compute_hash(i*64 + offset); 
                hlist->page_indices[t][count++] = i*64 + offset;
            }
            mask <<= nthread; 
        } 
     }

     hlist->len[t] = count; 

     // unsigned long workload = dirty_count / nthread;
     // unsigned long job_start = t * workload; 
     // unsigned long job_end;
        

     // if ( t == (nthread -1)) {
     //     job_end = dirty_count -1; 
     // }
     // else {
     //     job_end  = (t+1) * workload -1;  
     // }
     // //printf("[compute] Thread %d started, workload from:  %lu to :%lu\n", t, job_start, job_end);

     // unsigned long i; 
     // for (i = job_start; i <= job_end; i++){
     //     compute_hash(i);
     // }
     // //printf("[compute] Thread %d finished, workload from:  %lu to :%lu\n", t, job_start, job_end);
     // //printf("[compute] Thread %d finished\n", t);
    
     // printf("** thread %d finished, count=%d \n", t, count);
     // fflush(stdout);

     pthread_spin_lock(&finished_lock);
     finished_thread++;
     pthread_spin_unlock(&finished_lock);
 }
 return NULL; 

}



// static void *compute_thread_func(void *arg){
// 	int t = *(int *)arg; //The thread index of the thread
// 	while(1){
// 		pthread_mutex_lock(&compute_locks[t]);
// 		pthread_cond_wait(&compute_conds[t], &compute_locks[t]);
// 		pthread_mutex_unlock(&compute_locks[t]);
		
// 		unsigned long workload = dirty_count / nthread;
// 		unsigned long job_start = t * workload; 
// 		unsigned long job_end;
		

// 		if ( t == (nthread -1)) {
// 			job_end = dirty_count -1; 
// 		}
// 		else {
// 			job_end  = (t+1) * workload -1;  
// 		}
// 		//printf("[compute] Thread %d started, workload from:  %lu to :%lu\n", t, job_start, job_end);

// 		unsigned long i; 
// 		for (i = job_start; i <= job_end; i++){
// 			compute_hash(i);
// 		}
// 		//printf("[compute] Thread %d finished, workload from:  %lu to :%lu\n", t, job_start, job_end);
// 		//printf("[compute] Thread %d finished\n", t);
// 		pthread_spin_lock(&finished_lock);
// 		finished_thread++;
// 		pthread_spin_unlock(&finished_lock);
// 	}
// 	return NULL; 

// }

unsigned long *divergent_bitmap; 




static void *compare_thread_func(void *arg){
	int t = *(int *)arg; //The thread index of the thread
	while(1){
		pthread_mutex_lock(&compare_locks[t]);
		pthread_cond_wait(&compare_conds[t], &compare_locks[t]);
		pthread_mutex_unlock(&compare_locks[t]);
		//uint64_t workload = hlist -> len / nthread ;
		//uint64_t job_start = t * workload; 
		// //uint64_t job_end = (t+1) * workload - 1 ; 
		// if (t + 1 == nthread){
		// 	job_end = (hlist->len) -1;
		// }



		uint64_t i; 
		for (i =0; i < hlist->len[t] ; i++){
			if (memcmp(&(remote_hlist->hashes[t][i]), &(hlist->hashes[t][i]), sizeof(hash_t)) != 0){
				pthread_spin_lock(&compare_spin_lock);
				diverse_count++;
				//TOOD: transfer the page
				bitmap_set(divergent_bitmap, hlist->page_indices[t][i], 1);

				pthread_spin_unlock(&compare_spin_lock);
			}
		}
		//printf("[compare] Thread %d finished, workload from:  %lu to :%lu\n", t, job_start, job_end);
		pthread_spin_lock(&compare_spin_lock);
		compare_complete_thread++;
		//TOOD: transfer the page
		pthread_spin_unlock(&compare_spin_lock);

	}
	return NULL;
}

//XS: print bitmap encoded in long
static char* long_to_binary(long l){
    static char b[65];
    b[0]='\0';
    unsigned long z; 
    for (z = 1UL << 63; z > 0; z >>= 1 ){
        strcat(b, ((l & z) == z) ? "1" : "0");
    }
    return b;
}


// static void printbitmap(unsigned long *bmap){
//     int64_t ram_bitmap_pages = last_ram_offset() >> TARGET_PAGE_BITS;
//     long len =  BITS_TO_LONGS(ram_bitmap_pages);
//     fprintf(stderr, "len: %ld ", len);
//     int i;
//     for (i=0; i< len; i++){
//         fprintf(stderr, "[%d]: %s\n", i, long_to_binary(bmap[i]));
//     }

// }

int get_n_thread(void){
    return nthread;
}



unsigned long * get_divergent_bitmap(void){
	return divergent_bitmap;
}


// static void update_dirty_indices(unsigned long *bitmap, unsigned long nbits){
// 	unsigned long mask; 
// 	int i, offset; 
// 	int64_t ram_bitmap_pages = last_ram_offset() >> TARGET_PAGE_BITS;
// 	for (i =0; i * 64 < nbits; ++i){
// 		mask = 1;
// 		for (offset = 0; offset <64 && i*64 +offset < ram_bitmap_pages; offset++){

// 			if (mask & bitmap[i]){	
// 				dirty_indices[dirty_count]=i*64 + offset; 
// 				dirty_count++;
// 			}
// 			mask <<= 1; 
// 		}
// 	}

// 	// printf("***********\n dirty indices: ");
// 	//  unsigned long j; 
// 	//  for (j =0 ; j <dirty_count ; j++){
// 	//  	printf("%lu ", dirty_indices[j]);
// 	//  }
// 	//  printf("\n");

// 	//  printf("***********\n result from find next bit");
// 	//  fflush(stdout);
// 	//  unsigned long cur =0; 
// 	//  while (cur < ram_bitmap_pages){
// 	//  	cur = find_next_bit(bitmap, ram_bitmap_pages, cur +1);
// 	//  	printf("%lu ", cur);
// 	//  }



// }


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

	//unsigned long *test_bitmap = bitmap_new(ram_bitmap_pages);
	//bitmap_set(test_bitmap, 1, 1);
	//bitmap_set(test_bitmap, 65, 1);
	//printbitmap(test_bitmap);

	//dirty_indices = (unsigned long *) malloc(ram_bitmap_pages * sizeof(unsigned long));


	//update_dirty_indices(test_bitmap, ram_bitmap_pages);

	// printf("\n\n dirty_count = %lu\n", dirty_count);
	// for (i =0 ; i<dirty_count; i++){
	// 	printf("%lu\n", dirty_indices[i]);
	// }
	// printf("******\n");




	for (i = 0; i < nthread; i++){
		pthread_mutex_init(&compute_locks[i], NULL);
		pthread_cond_init(&compute_conds[i], NULL);
		indices[i] = i;
		pthread_create(&compute_ts[i], NULL, compute_thread_func, (void *)&indices[i]);

		pthread_mutex_init(&compare_locks[i], NULL);
		pthread_cond_init(&compare_conds[i], NULL);
		pthread_create(&compare_ts[i], NULL, compare_thread_func, (void*)&indices[i]);


	}

    //Malloc for hlist; 
    hlist = (hash_list*) malloc( sizeof (hash_list));
    hlist -> hashes = (hash_t **) malloc(nthread * sizeof(hash_t *));
    hlist -> page_indices = (uint64_t **) malloc(nthread *sizeof(uint64_t *));

    uint64_t max_len = ram_bitmap_pages / nthread + 1; 

    for (i =0; i<nthread; i++){
        hlist -> hashes[i] = (hash_t *) malloc(max_len * sizeof(hash_t));
        hlist -> page_indices[i] = (uint64_t *) malloc(max_len * sizeof(uint64_t));
    }
    hlist -> len = (uint64_t *) malloc(nthread * sizeof(uint64_t));

	
    //Malloc for remote hlist; 
    remote_hlist = (hash_list*) malloc( sizeof (hash_list));
    remote_hlist -> hashes = (hash_t **) malloc(nthread * sizeof(hash_t *));
    remote_hlist -> page_indices = (uint64_t **) malloc(nthread *sizeof(uint64_t *));

    for (i =0; i<nthread; i++){
        remote_hlist -> hashes[i] = (hash_t *) malloc(max_len * sizeof(hash_t));
        remote_hlist -> page_indices[i] = (uint64_t *) malloc(max_len * sizeof(uint64_t));
    }
    remote_hlist -> len = (uint64_t *) malloc(nthread * sizeof(uint64_t));




	fake_page = (uint8_t *) malloc (4096);

}

//Construct a complete binary tree as http://mathworld.wolfram.com/images/eps-gif/CompleteBinaryTree_1000.gif

// void build_merkle_tree (unsigned long *bmap, unsigned long len){
// 	dirty_count = 0;
// 	update_dirty_indices(bmap, len);

// 	mtree = (merkle_tree_t *) malloc (sizeof(merkle_tree_t));

		
// 	//get log base 2
// 	unsigned long c = dirty_count; 
// 	int tree_height = 1; 
// 	while ( c >>= 1) { ++tree_height; }
// 	// the tree height 


// 	mtree-> tree_size = (1 << (tree_height)) - 1 + 2 * (len - (1 << (tree_height-1)));
// 	mtree-> tree = (hash_t *) malloc (mtree->tree_size * sizeof(hash_t));
// 	if (len - (1 << (tree_height-1)) == 0) {
// 		mtree -> full = 1; 
// 	}
// 	else {
// 		mtree -> full = 0;
// 	}
// 	mtree -> first_leaf_index = (mtree-> tree_size - 1) / 2 ;
// 	mtree -> full_part_last_index = (1 << (tree_height)) - 2; 
// 	mtree -> last_level_leaf_count =  2 * (len - (1 << (tree_height-1)));


// 	int i ;
// 	for (i = 0; i<nthread; i++){
// 		pthread_mutex_lock(&compute_locks[i]);
// 		pthread_cond_broadcast(&compute_conds[i]);
// 		pthread_mutex_unlock(&compute_locks[i]);
// 	}
// 	while(finished_thread < nthread){
// 	}
// 	long j;
// 	for ( j= mtree->first_leaf_index-1 ; j >= 0; j--){
// 		mtree->tree[j]=hashofhash(&(mtree->tree[2*j +1]), &(mtree->tree[2*j + 2]));
// 	}
// }

void compute_hash_list(unsigned long *bmap, unsigned long len){
	


    compute_bitmap = bmap;

	int64_t ram_bitmap_pages = last_ram_offset() >> TARGET_PAGE_BITS;

	finished_thread = 0;


    //xs fix it: slow
	if (divergent_bitmap == NULL){	
		divergent_bitmap = bitmap_new(ram_bitmap_pages);
	}else{
		//bitmap_zero(divergent_bitmap, ram_bitmap_pages);
		free(divergent_bitmap);
		divergent_bitmap = bitmap_new(ram_bitmap_pages);
	}

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

}



void compare_hash_list(void){
	compare_complete_thread = 0;
	diverse_count = 0;
	int i ; 
	//printf("before waking up compare hashlist\n");
	for (i = 0; i<nthread; i++){
		pthread_mutex_lock(&compare_locks[i]);
		pthread_cond_broadcast(&compare_conds[i]);
		pthread_mutex_unlock(&compare_locks[i]);
	}
	while (1) {
		pthread_spin_lock(&compare_spin_lock);
		if (compare_complete_thread == nthread){
			pthread_spin_unlock(&compare_spin_lock);
			break;
		}
		pthread_spin_unlock(&compare_spin_lock);
		usleep(100);
	}
    uint64_t dirty_count =0; 
    for (i = 0; i<nthread; i++){
        dirty_count += hlist->len[i]; 
    }


    // printf("Compared %"PRIu64 " pages, same = %" PRIu64" same rate = %"PRIu64"%%\n", dirty_count, dirty_count - diverse_count, (dirty_count - diverse_count) * 100 / dirty_count);

	// fprintf(stderr ,"%"PRIu64 ",%" PRIu64", %"PRIu64"%%, %lu\n", hlist->len, hlist->len - diverse_count, (hlist->len - diverse_count) * 100 / hlist->len, get_and_rest_output_counter());
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

#define BILLION 1000000000L

void clock_init(clock_handler *c_k)
{
    c_k->counter = 0;
}
void clock_add(clock_handler *c_k)
{
    struct timespec clock_time;
    clock_gettime(CLOCK_MONOTONIC, &clock_time);
    c_k->clocks[c_k->counter] = clock_time;
    c_k->counter++;
}

void clock_display(clock_handler *c_k)
{
    uint64_t diff;
    struct timespec start_time, end_time;
    char tmp[64], str[256];
    memset(str, 0, sizeof(str));
    int i;
    for (i = 0; i < c_k->counter; i++)
    {
        end_time = c_k->clocks[i];
        if (i != 0)
        {
            diff = BILLION * (end_time.tv_sec - start_time.tv_sec) + end_time.tv_nsec - start_time.tv_nsec;
            double elp = diff / 1000000.0;
            sprintf(tmp, "%f\n", elp);
            strcat(str, tmp);
        }
        start_time = end_time;
    }
    fprintf(stderr, "%s\n", str);
}
