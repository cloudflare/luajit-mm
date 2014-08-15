#ifndef _PAGE_ALLOC_H_
#define _PAGE_ALLOC_H_

#include "rbtree.h"
#include "util.h"
#include "trunk.h" /* for lm_trunk_t */
#include "lj_mm.h"

/**************************************************************************
 *
 *              Depicting page
 *
 **************************************************************************
 */
typedef struct {
    short order; /* the order in the buddy allocation */
    short flags;
} lm_page_t;

typedef enum {
    PF_LEADER    = (1 << 0), /* set if it's the first page of a block */
    PF_ALLOCATED = (1 << 1), /* set if it's "leader" of a allocated block */
    PF_LAST      = PF_ALLOCATED,
} page_flag_t;

static inline int
is_page_leader(lm_page_t * p) {
    return p->flags & PF_LEADER;
}

static inline void
set_page_leader(lm_page_t* p) {
    p->flags |= PF_LEADER;
}

static inline void
reset_page_leader(lm_page_t* p) {
    p->flags &= ~PF_LEADER;
}

static inline int
is_allocated_blk(lm_page_t* p) {
    return is_page_leader(p) && (p->flags & PF_ALLOCATED);
}

static inline void
set_allocated_blk(lm_page_t* p) {
    ASSERT(is_page_leader(p));
    p->flags |= PF_ALLOCATED;
}

static inline void
reset_allocated_blk(lm_page_t* p) {
    ASSERT(is_page_leader(p));
    p->flags &= ~PF_ALLOCATED;
}

/* We could have up to 1M pages (4G/4k). Hence 20 */ #define MAX_ORDER 20
#define INVALID_ORDER (-1)

/**************************************************************************
 *
 *              Buddy Allocation
 *
 **************************************************************************
 */
typedef struct {
    char* first_page;   /* The starting address of the first page */
    lm_page_t* page_info;
    /* Free blocks of the same order are ordered by a RB tree. */
    rb_tree_t free_blks[MAX_ORDER];
    rb_tree_t alloc_blks;
    int max_order;
    int page_num;       /* This many pages in total */
    int page_size;      /* The size of page in byte, normally 4k*/
    int page_size_log2; /* log2(page_size)*/
    int idx_2_id_adj;
} lm_alloc_t;

extern lm_alloc_t* alloc_info;

/* Page index to ID conversion */
typedef int page_id_t;
typedef int page_idx_t;

static inline page_id_t
page_idx_to_id(page_idx_t idx) {
    ASSERT(idx >= 0 && idx < alloc_info->page_num);
    return idx + alloc_info->idx_2_id_adj;
}

static inline page_idx_t
page_id_to_idx(page_id_t id) {
    int idx = id - alloc_info->idx_2_id_adj;
    ASSERT(idx >= 0 && idx < alloc_info->page_num);
    return idx;
}

static inline int
verify_order(page_idx_t blk_leader, int order) {
    return 0 == (page_idx_to_id(blk_leader) & ((1<<order) - 1));
}

static int
find_block(page_idx_t block, int order, intptr_t* value) {
    ASSERT(order >= 0 && order <= alloc_info->max_order &&
           verify_order(block, order));

    return rbt_search(&alloc_info->free_blks[order], block, value);
}

static inline int
remove_block(page_idx_t block, int order) {
    lm_page_t* page = alloc_info->page_info + block;

    ASSERT(page->order == order && find_block(block, order, NULL));
    ASSERT(!is_allocated_blk(page) && verify_order(block, order));
    set_allocated_blk(page);

    return rbt_delete(&alloc_info->free_blks[order], block);
}

/* Add the free block of the given "order" to the buddy system */
static inline int
add_block(page_idx_t block, int order) {
    lm_page_t* page = alloc_info->page_info + block;

    ASSERT(order >= 0 && order <= alloc_info->max_order &&
           verify_order(block, order));

    page->order = order;
    set_page_leader(page);
    reset_allocated_blk(page);

    return rbt_insert(&alloc_info->free_blks[order], block, 0);
}

static inline void
migrade_alloc_block(page_idx_t block, int ord_was, int ord_is, size_t new_map_sz) {
    rb_tree_t* rbt = &alloc_info->alloc_blks;
    int res = rbt_delete(rbt, block);
    ASSERT(res != 0);

    rbt_insert(rbt, block, new_map_sz);

    ASSERT(alloc_info->page_info[block].order == ord_was);
    alloc_info->page_info[block].order = ord_is;
}

int free_block(page_idx_t page_idx);

/* Init & Fini */
int lm_init_page_alloc(lm_trunk_t* trunk, lj_mm_opt_t* mm_opt);
void lm_fini_page_alloc(void);

/* Misc */
#ifdef DEBUG
void dump_page_alloc(FILE* f);
#endif

#endif /*_PAGE_ALLOC_H_*/
