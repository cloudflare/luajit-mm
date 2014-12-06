#ifndef _BLOCK_CACHE_H_
#define _BLOCK_CACHE_H_

struct blk_lru;
typedef struct blk_lru blk_lru_t;

int bc_set_parameter(int enable_bc, int cache_sz_in_page);

int bc_init(void);
int bc_fini(void);
int bc_add_blk(page_idx_t start_page, int order);
int bc_evict_oldest(void);
int bc_remove_block(page_idx_t start_page, int order, int zap_page);

#endif /* _BLOCK_CACHE_H_ */
