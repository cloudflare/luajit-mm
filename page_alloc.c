#include <sys/mman.h>
#include <stdint.h> /* for intptr_t */
#include <unistd.h>
#include <errno.h>
#include <string.h> /* for memcpy() */
#include "rbtree.h"
#include "util.h"
#include "lj_mm.h"
#include "chunk.h"
#include "page_alloc.h"

/* Forward Decl */
lm_alloc_t* alloc_info = NULL;

/* Initialize the page allocator, return 0 on success, 1 otherwise. */
int
lm_init_page_alloc(lm_chunk_t* chunk, lj_mm_opt_t* mm_opt) {
    if (!chunk) {
        /* Trunk is not yet allocated */
        return 0;
    }

    if (alloc_info) {
        /* This function was succesfully invoked before */
        return 1;
    }

    int page_num = chunk->page_num;
    if (unlikely(mm_opt != NULL)) {
        int pn = mm_opt->page_num;
        if (((pn > 0) && (pn > page_num)) || !pn)
            return 0;
        page_num = pn;
    }

    int alloc_sz = sizeof(lm_alloc_t) +
                   sizeof(lm_page_t) * (page_num + 1);

    alloc_info = (lm_alloc_t*) malloc(alloc_sz);
    if (!alloc_info) {
        errno = ENOMEM;
        return 0;
    }

    alloc_info->first_page = chunk->start;
    alloc_info->page_num   = page_num;
    alloc_info->page_size  = chunk->page_size;
    alloc_info->page_size_log2 = log2_int32(chunk->page_size);

    /* Init the page-info */
    char* p =  (char*)(alloc_info + 1);
    int align = __alignof__(lm_page_t);
    p = (char*)((((intptr_t)p) + align - 1) & ~align);
    alloc_info->page_info = (lm_page_t*)p;

    int i;
    lm_page_t* pi = alloc_info->page_info;
    for (i = 0; i < page_num; i++) {
        pi[i].order = INVALID_ORDER;
        pi[i].flags = 0;
    }

    /* Init the buddy allocator */
    int e;
    rb_tree_t* free_blks = &alloc_info->free_blks[0];
    for (i = 0, e = MAX_ORDER; i < e; i++)
        rbt_init(&free_blks[i]);
    rbt_init(&alloc_info->alloc_blks);

    /* Determine the max order */
    int max_order = 0;
    unsigned int bitmask;
    for (bitmask = 0x80000000, max_order = 31;
         bitmask;
         bitmask >>= 1, max_order --) {
        if (bitmask & page_num)
            break;
    }
    alloc_info->max_order = max_order;

    /* So, the ID of biggest block's first page is "2 * (1<<order)". e.g.
     * Suppose the chunk contains 11 pages, which will be divided into 3
     * blocks, eaching containing 1, 2 and 8 pages. The indices of these
     * blocks are 0, 1, 3 respectively, and their IDs are 5, 6, and 8
     * respectively. In this case:
     *    alloc_info->idx_2_id_adj == 5 == page_id(*) - page_idx(*)
     */
    int idx_2_id_adj = (1 << max_order) - (page_num & ((1 << max_order) - 1));
    alloc_info->idx_2_id_adj = idx_2_id_adj;

    /* Divide the chunk into blocks, smaller block first. Smaller blocks
     * are likely allocated and deallocated frequently. Therefore, they are
     * better off residing closer to data segment.
     */
    int page_idx = 0;
    int order = 0;
    for (bitmask = 1, order = 0;
         bitmask != 0;
         bitmask = bitmask << 1, order++) {
        if (page_num & bitmask) {
            add_free_block(page_idx, order);
            page_idx += (1 << order);
        }
    }

    return 1;
}

void
lm_fini_page_alloc(void) {
    if (alloc_info) {
        rb_tree_t* free_blks = &alloc_info->free_blks[0];
        int i, e;
        for (i = 0, e = MAX_ORDER; i < e; i++)
            rbt_fini(free_blks + i);

        rbt_fini(&alloc_info->alloc_blks);

        free(alloc_info);
        alloc_info = 0;
    }
}

/* The extend given the exiting allocated block such that it could accommodate
 * at least new_sz bytes.
 */
int
extend_alloc_block(page_idx_t block_idx, size_t new_sz) {
    rb_tree_t* rbt = &alloc_info->alloc_blks;
    intptr_t alloc_sz;
    int res = rbt_search(rbt, block_idx, &alloc_sz);
    ASSERT(res);

    int page_sz = alloc_info->page_size;
    int page_sz_log2 = alloc_info->page_size_log2;
    int min_page_num = (new_sz + page_sz - 1) >> page_sz_log2;

    page_id_t blk_id = page_idx_to_id(block_idx);
    int order = alloc_info->page_info[block_idx].order;

    /* step 1: perfrom try run to see if we have luck. */
    int succ = 0;
    int ord;
    for (ord = order; ord <= alloc_info->max_order; ord++) {
        if (min_page_num <= (1 << ord)) {
            succ = 1;
            break;
        }

        page_id_t buddy_id = blk_id ^ (1 << ord);
        if (buddy_id < blk_id) {
            /* The buddy block must reside at higher address. */
            break;
        }

        int buddy_idx = page_id_to_idx(buddy_id);
        if (!rbt_search(&alloc_info->free_blks[ord], buddy_idx, NULL))
            break;
    }

    /* This function is not supposed to shrink the existing block; therefore,
     * if the existing block is big enough to accommodate allocation request,
     * it need to return 0 to inform the caller something fishy is happening.
     */
    if (!succ || ord == order)
        return 0;

    int t;
    for (t = order; t < ord; t++) {
        page_id_t buddy_id = blk_id ^ (1 << t);
        int buddy_idx = page_id_to_idx(buddy_id);
        remove_free_block(buddy_idx, t);
        reset_page_leader(alloc_info->page_info + buddy_idx);
    }

    migrade_alloc_block(block_idx, order, ord, new_sz);

    return 1;
}

int
free_block(page_idx_t page_idx) {
    (void)remove_alloc_block(page_idx);

    lm_page_t* pi = alloc_info->page_info;
    lm_page_t* page = pi + page_idx;
    int order = page->order;
    ASSERT (find_block(page_idx, order, NULL) == 0);

    char* block_addr = alloc_info->first_page +
                       (page_idx << alloc_info->page_size_log2);
    size_t block_len = (1<<order) << alloc_info->page_size_log2;
    madvise(block_addr, block_len, MADV_DONTNEED);

    /* Consolidate adjacent buddies */
    int page_num = alloc_info->page_num;
    page_id_t page_id = page_idx_to_id(page_idx);
    int min_page_id = alloc_info->idx_2_id_adj;
    while (1) {
        page_id_t buddy_id = page_id ^ (1<<order);
        if (buddy_id < min_page_id)
            break;

        page_idx_t buddy_idx = page_id_to_idx(buddy_id);
        if (buddy_idx >= page_num ||
            pi[buddy_idx].order != order ||
            !is_page_leader(pi + buddy_idx) ||
            is_allocated_blk(pi + buddy_idx)) {
            break;
        }
        remove_free_block(buddy_idx, order);
        reset_page_leader(alloc_info->page_info + buddy_idx);

        page_id = page_id < buddy_id ? page_id : buddy_id;
        order++;
    }

    add_free_block(page_id_to_idx(page_id), order);
    return 1;
}

/**************************************************************************
 *
 *       Debugging Support & Misc "cold" functions
 *
 **************************************************************************
 */
const lm_status_t*
lm_get_status(void) {
    if (!alloc_info)
        return NULL;

    lm_status_t* s = (lm_status_t *)malloc(sizeof(lm_status_t));
    s->first_page = alloc_info->first_page;
    s->page_num = alloc_info->page_num;
    s->idx_to_id = alloc_info->idx_2_id_adj;
    s->alloc_blk_num = 0;
    s->free_blk_num = 0;
    s->free_blk_info = NULL;
    s->alloc_blk_info = NULL;
    rb_tree_t* rbt = &alloc_info->alloc_blks;
    int alloc_blk_num = rbt_size(rbt);

    /* Populate allocated block info */
    if (alloc_blk_num) {
        block_info_t* ai;
        ai = (block_info_t*)malloc(sizeof(block_info_t) * alloc_blk_num);

        rb_iter_t iter, iter_e;
        int idx = 0;
        for (iter = rbt_iter_begin(rbt), iter_e = rbt_iter_end(rbt);
             iter != iter_e;
             iter = rbt_iter_inc(rbt, iter)) {
            rb_node_t* blk = rbt_iter_deref(iter);
            ai[idx].page_idx = blk->key;
            ai[idx].size = blk->value;
            ai[idx].order = alloc_info->page_info[blk->key].order;
            idx++;
        }

        s->alloc_blk_info = ai;
        s->alloc_blk_num = idx;
    }

    /* Populate free block info */
    int free_blk_num = 0;
    int i, e;
    for (i = 0, e = alloc_info->max_order; i <= e; i++) {
        free_blk_num += rbt_size(alloc_info->free_blks + i);
    }
    if (free_blk_num) {
        block_info_t* fi;
        fi = (block_info_t*)malloc(sizeof(block_info_t) * free_blk_num);

        int idx = 0;
        int page_size_log2 = alloc_info->page_size_log2;
        for (i = 0, e = alloc_info->max_order; i <= e; i++) {
            rb_tree_t* rbt = alloc_info->free_blks + i;

            rb_iter_t iter, iter_e;
            for (iter = rbt_iter_begin(rbt), iter_e = rbt_iter_end(rbt);
                 iter != iter_e;
                 iter = rbt_iter_inc(rbt, iter)) {
                rb_node_t* nd = rbt_iter_deref(iter);
                fi[idx].page_idx = nd->key;
                fi[idx].order = alloc_info->page_info[nd->key].order;
                fi[idx].size = (1 << fi[idx].order) << page_size_log2;
                idx++;
            }
        }
        ASSERT(idx == free_blk_num);

        s->free_blk_info = fi;
        s->free_blk_num = idx;
    }

    return s;
}

void
lm_free_status(lm_status_t* status) {
    if (!status)
        return;

    if (status->free_blk_info)
        free(status->free_blk_info);

    if (status->alloc_blk_info)
        free(status->alloc_blk_info);

    free(status);
}

#ifdef DEBUG
void
dump_page_alloc(FILE* f) {
    if (!alloc_info) {
        fprintf(f, "not initialized yet\n");
        fflush(f);
        return;
    }

    /* dump the buddy system */
    fprintf (f, "Buddy system: max-order=%d, id - idx = %d\n",
             alloc_info->max_order, alloc_info->idx_2_id_adj);

    int i, e;
    char* page_start_addr = alloc_info->first_page;
    int page_sz_log = alloc_info->page_size_log2;

    for (i = 0, e = alloc_info->max_order; i <= e; i++) {
        rb_tree_t* free_blks = &alloc_info->free_blks[i];

        if (rbt_is_empty(free_blks))
            continue;

        fprintf(f, "Order = %3d: ", i);
        rb_iter_t iter, iter_e;
        for (iter = rbt_iter_begin(free_blks),
                iter_e = rbt_iter_end(free_blks);
             iter != iter_e;
             iter = rbt_iter_inc(free_blks, iter)) {
            rb_node_t* node = rbt_iter_deref(iter);
            page_idx_t page_idx = node->key;
            char* addr = page_start_addr + (page_idx << page_sz_log);
            fprintf(f, "pg_idx:%d (%p, len=%d), ", page_idx,
                    addr, (int)node->value);
            verify_order(page_idx, i);
        }
        fputs("\n", f);
    }

    fprintf(f, "\nAllocated blocks:\n");
    {
        rb_tree_t* rbt = &alloc_info->alloc_blks;
        rb_iter_t iter, iter_e;
        int idx = 0;
        for (iter = rbt_iter_begin(rbt), iter_e = rbt_iter_end(rbt);
             iter != iter_e;
             iter = rbt_iter_inc(rbt, iter)) {
            rb_node_t* nd = rbt_iter_deref(iter);
            int blk = nd->key;
            fprintf(f, "%3d: pg_idx:%d, size:%ld, order = %d\n",
                    idx, blk, nd->value, alloc_info->page_info[blk].order);
            idx++;
        }
    }
}
#endif
