#ifndef MEM_COMPARE_HASH_H
#define MEM_COMPARE_HASH_H

#define HASH_SIZE 64 

#if HASH_SIZE == 64
#define hash_t uint64_t
#define hash_sum_t __uint128_t 
#define HASH_MASK 0xffffffffffffffff
#define HASH_SHIFT 8
#elif HASH_SIZE == 32 
#define hash_t uint32_t 
#define hash_sum_t uint64_t
#define HASH_MASK 0xffffffff
#define HASH_SHIFT 4
#endif


//Init global variables (locks, conds), call it once is enought
void hash_init(unsigned long len);

void build_merkle_tree (unsigned long *xor_bitmap, unsigned long len);

void compute_hash_list (unsigned long *xor_bitmap, unsigned long len);

#endif //MEM_COMPARE_HASH_H
