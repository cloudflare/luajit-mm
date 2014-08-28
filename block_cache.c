/* Suppose a allocated block B1, whose virtual address is [ad1, ad2], is going
 * to deallocated. One Linux, it seems the only way to deallocate the pages
 * associated with the block is to call madvise(..MADV_DONTNEED...) (
 * hereinafter, call it madvise() for short unless otherwise noted).
 *
 * madvise() *immediately* remove all the pages involved, and invalidate the
 * related TLB entries. So, if later on we allocate a block overlapping with
 * B1 in virtual address; accessing to the overlapping space will result in
 * re-establishing TLB entries, and zero-fill-pages, which is bit expensive.
 *
 * This cost can be reduced by keeping few blocks in memory, and re-use the
 * memory resident pages over and over again. This is the rationale behind the
 * "block cache". The "cache" here may be a misnomer; it dosen't cache any data,
 * it just provide a way to keep small sum of idle pages in memory to avoid
 * cost of TLB manipulation and page initialization via zero-filling.
 */
#include <sys/mman.h>
#include <stdlib.h>
#include "util.h"
#include "page_alloc.h"
#include "block_cache.h"

#define LRU_MAX_ENTRY 64
#define INVALID_LRU_IDX (-1)

typedef struct blk_lru {
    page_idx_t start_page;
    short order;
    short next;
    short prev;
} blk_lru_t;

typedef struct {
    /* Free blocks in the ascending order of their starting page.*/
    rb_tree_t* blks;
    blk_lru_t lru_v[LRU_MAX_ENTRY];
    short lru_hdr;
    short lru_tail;
    short lru_free_list;
    int total_page_num;
} block_cache_t;

/* Block-cache paprameters */
static int MAX_CACHE_PAGE_NUM = 512;
static block_cache_t* blk_cache;
static char enable_blk_cache = 0;
static char blk_cache_init = 0;

/***************************************************************************
 *
 *                      LRU related functions
 *
 ***************************************************************************
 */
static void
lru_init() {
    int i;
    blk_lru_t* lru = blk_cache->lru_v;
    for (i = 0; i < LRU_MAX_ENTRY; i++) {
        lru[i].next = i + 1;
        lru[i].prev = i - 1;
    }
    lru[0].prev = INVALID_LRU_IDX;
    lru[LRU_MAX_ENTRY-1].next = INVALID_LRU_IDX;

    blk_cache->lru_hdr = blk_cache->lru_tail = INVALID_LRU_IDX;
    blk_cache->lru_free_list = 0;
}

static int
lru_is_full() {
    return blk_cache->lru_free_list == INVALID_LRU_IDX;
}

static int
lru_is_empty() {
    return blk_cache->lru_hdr == INVALID_LRU_IDX;
}

static int
lru_append(page_idx_t start_page, int order) {
    if (unlikely(lru_is_full())) {
        ASSERT(0);
        return INVALID_LRU_IDX;
    }

    blk_lru_t *lru = blk_cache->lru_v;
    int new_item = blk_cache->lru_free_list;
    blk_cache->lru_free_list = lru[new_item].next;

    int lru_tail = blk_cache->lru_tail;
    if (lru_tail != INVALID_LRU_IDX)
        lru[lru_tail].next = new_item;
    else {
        ASSERT(blk_cache->lru_hdr == INVALID_LRU_IDX);
        blk_cache->lru_hdr = new_item;
    }

    lru[new_item].prev = lru_tail;
    lru[new_item].next = INVALID_LRU_IDX;
    blk_cache->lru_tail = new_item;

    lru[new_item].start_page = start_page;
    lru[new_item].order = order;

    return new_item;
}

static void
lru_remove(int idx) {
    if (!blk_cache_init || !enable_blk_cache)
        return;

    blk_lru_t* lru = blk_cache->lru_v;
    blk_lru_t* lru_entry = lru + idx;
    int prev = lru_entry->prev;
    int next = lru_entry->next;

    if (prev != INVALID_LRU_IDX) {
        lru[prev].next = next;
    } else {
        ASSERT(blk_cache->lru_hdr == idx);
        blk_cache->lru_hdr = next;
    }

    if (next != INVALID_LRU_IDX) {
        lru[next].prev = prev;
    } else {
        ASSERT(blk_cache->lru_tail == idx);
        blk_cache->lru_tail = prev;
    }

    lru_entry->order = -1; /* for debugging purpose */
    lru_entry->next = blk_cache->lru_free_list;
    blk_cache->lru_free_list = idx;
}

static inline int
lru_popback(void) {
    if (likely(blk_cache->lru_tail) != INVALID_LRU_IDX) {
        lru_remove(blk_cache->lru_tail);
        return 1;
    }
    ASSERT(blk_cache->lru_hdr == INVALID_LRU_IDX);
    return 0;
}

/***************************************************************************
 *
 *                   block-cache related functions
 *
 ***************************************************************************
 */
int
bc_init(void) {
    if (unlikely(blk_cache_init))
        return 1;

    if (unlikely(!enable_blk_cache))
        return 0;

    if (!(blk_cache = (block_cache_t*)malloc(sizeof(block_cache_t))))
        return 0;

    blk_cache->blks = rbt_create();
    if (!blk_cache->blks) {
        free(blk_cache);
        return 0;
    }

    lru_init();
    blk_cache_init = 1;

    return 1;
}

int
bc_fini(void) {
    if (unlikely(!enable_blk_cache))
        return 1;

    if (unlikely(!blk_cache_init))
        return 0;

    rbt_destroy(blk_cache->blks);
    free(blk_cache);
    blk_cache_init = 0;

    return 1;
}

int
bc_add_blk(page_idx_t start_page, int order) {
    if (!blk_cache_init || !enable_blk_cache)
        return INVALID_LRU_IDX;

    if (unlikely(lru_is_full())) {
        bc_evict_oldest();
        ASSERT(!lru_is_full());
    }

    int idx = lru_append(start_page, order);
    ASSERT(idx != INVALID_LRU_IDX);

    int rv = rbt_insert(blk_cache->blks, start_page, idx);
    if (likely(rv)) {
        blk_cache->total_page_num += 1 << order;
        if (blk_cache->total_page_num > MAX_CACHE_PAGE_NUM &&
            blk_cache->lru_hdr != blk_cache->lru_tail) {
            bc_evict_oldest();
        }
        return 1;
    }

    ASSERT(0);
    lru_popback();
    return 0;
}

int
bc_remove_block(page_idx_t start_page, int order, int zap_page) {
    if (zap_page) {
        char* p = get_page_addr(start_page);
        size_t len = ((size_t)(1 << order)) << alloc_info->page_size_log2;
        madvise(p, len, MADV_DONTDUMP|MADV_DONTNEED);
    }

    if (!blk_cache_init || !enable_blk_cache)
        return 0;

    intptr_t idx;
    if (!rbt_delete(blk_cache->blks, start_page, &idx))
        return 0;

    ASSERT(blk_cache->lru_v[idx].order == order);
    blk_cache->total_page_num -= (1 << order);
    ASSERT(blk_cache->total_page_num >= 0);

    lru_remove(idx);

    return 1;
}

int
bc_evict_oldest() {
    if (!blk_cache_init || !enable_blk_cache)
        return 0;

    if (!lru_is_empty()) {
        blk_lru_t* lru = blk_cache->lru_v + blk_cache->lru_hdr;
        page_idx_t page = lru->start_page;
        return bc_remove_block(page, lru->order, 1);
    }

    return 1;
}

int
bc_set_parameter(int enable_bc, int cache_sz_in_page) {
    if (cache_sz_in_page > 0)
        MAX_CACHE_PAGE_NUM = cache_sz_in_page;

    enable_blk_cache = enable_bc;
    return 1;
}
