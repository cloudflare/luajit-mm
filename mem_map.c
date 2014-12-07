/* This file contains the implementation to following exported functions:
 *   lm_mmap(), lm_munmap(), lm_mremap(), lm_malloc(), lm_free().
 */
#ifndef _GNU_SOURCE
    #define _GNU_SOURCE
#endif
#include <sys/mman.h>
#include <errno.h>
#include <string.h> /* for memcpy() */
#include "page_alloc.h"
#include "rbtree.h"
#include "lj_mm.h"

/* Forward Decl */
static int lm_unmap_helper(void* addr, size_t um_size);

static ljmm_mode_t ljmm_mode = lm_default;

void
lm_init_mm_opt(ljmm_opt_t* opt) {
    opt->mode = lm_default;
    opt->dbg_alloc_page_num = -1;
    opt->enable_block_cache = 0;
    opt->blk_cache_in_page = 0;
}

/* For allocating "big" blocks (about one page in size, or across multiple
 * pages). The return value is page-aligned.
 */
void*
lm_malloc(size_t sz) {
    errno = 0;
    if (!alloc_info) {
        lm_init();
        if (!alloc_info)
            return NULL;
    }

    /* Determine the order of allocation request */
    int req_order = ceil_log2_int32(sz);
    req_order -= alloc_info->page_size_log2;
    if (req_order < 0)
        req_order = 0;

    int max_order = alloc_info->max_order;
    if (req_order > max_order) {
        errno = ENOMEM;
        return 0;
    }

    rb_tree_t* free_blks = alloc_info->free_blks;

    int i, blk_order = -1;
    page_idx_t blk_idx = -1;

    /* Find the smallest available block big enough to accommodate the
     * allocation request.
     */
    for (i = req_order; i <= max_order; i++) {
        rb_tree_t* rbt = free_blks + i;
        if (!rbt_is_empty(rbt)) {
            blk_idx = rbt_get_min(rbt);
            blk_order = i;
            break;
        }
    }

    if (blk_idx == -1)
        return NULL;

    remove_free_block(blk_idx, blk_order, 0);

    /* The free block may be too big. If this is the case, keep splitting
     * the block until it tightly fit the allocation request.
     */
    int bo = blk_order;
    while (bo > req_order) {
        bo --;
        int split_block = blk_idx + (1 << bo);
        add_free_block(split_block, bo);
    }

    (void)add_alloc_block(blk_idx, sz, bo);
    return alloc_info->first_page + (blk_idx << alloc_info->page_size_log2);
}

int
lm_free(void* mem) {
    if (unlikely (!alloc_info))
        return 0;

    long ofst = ((char*)mem) - ((char*)alloc_info->first_page);
    if (unlikely (ofst < 0))
        return 0;

    long page_sz = alloc_info->page_size;
    if (unlikely ((ofst & (page_sz - 1)))) {
        /* the lm_malloc()/lm_mmap() return page-aligned block */
        return 0;
    }

    long page_idx = ofst >> log2_int32(page_sz);
    int page_num = alloc_info->page_num;
    if (unlikely(page_idx >= page_num))
        return 0;

    lm_page_t* pi = alloc_info->page_info;
    lm_page_t* page = pi + page_idx;

    /* Check to see if it's a previously allocated block */
    if (unlikely(!is_page_leader(page)))
        return 0;

    if (unlikely(!is_allocated_blk(page)))
        return 0;

    return free_block(page_idx);
}

/*****************************************************************************
 *
 *      Implementation of lm_mremap()
 *
 *****************************************************************************
 */

/* lm_mremap() herlper. Return NULL instead of MAP_FAILED in case it was not
 * successful. It also tries to set errno if fails.
 */
static void*
lm_mremap_helper(void* old_addr, size_t old_size, size_t new_size, int flags) {
    long ofst = ((char*)old_addr) - ((char*)alloc_info->first_page);
    long page_sz = alloc_info->page_size;

    if (unlikely ((ofst & (page_sz - 1)))) {
        errno = EINVAL;
        return NULL;
    }

    /* We are not supporting MREMAP_FIXED */
    if (unlikely(flags & ~MREMAP_MAYMOVE)) {
        errno = EINVAL;
        return NULL;
    }

    int page_sz_log2 = alloc_info->page_size_log2;
    int page_idx = ofst >> page_sz_log2;
    intptr_t size_verify;
    rb_tree_t* rbt = &alloc_info->alloc_blks;
    if (!rbt_search(rbt, page_idx, &size_verify) || size_verify != old_size) {
        errno = EINVAL;
        return NULL;
    }

    int old_page_num = (old_size + page_sz - 1) >> page_sz_log2;
    int new_page_num = (new_size + page_sz - 1) >> page_sz_log2;

    /* case 1: Shrink the existing allocated block by reducing the number of
     *      mapping pages.
     */
    if (old_page_num > new_page_num) {
        char* unmap_start = old_addr + (new_page_num << page_sz_log2);
        size_t unmap_len = old_size - (((size_t)new_page_num) << page_sz_log2);
        if (lm_unmap_helper(unmap_start, unmap_len)) {
            rbt_set_value(rbt, page_idx, new_size);
            return old_addr;
        }
        errno = EINVAL;
        return NULL;
    }

    /* case 2: Expand the existing allocated block by adding more pages. */
    if (old_page_num < new_page_num) {
        int order = alloc_info->page_info[page_idx].order;
        /* Block is big enough to accommodate the old-size byte.*/
        if (new_page_num < (1<<order)) {
            rbt_set_value(rbt, page_idx, new_size);
            return old_addr;
        }

        /* Try to merge with the buddy block */
        if (extend_alloc_block(page_idx, new_size))
            return old_addr;

        if (flags & MREMAP_MAYMOVE) {
            char* p = lm_malloc(new_size);
            if (!p) {
                errno = ENOMEM;
                return NULL;
            }
            memcpy(p, old_addr, old_size);
            lm_free(old_addr);
            return p;
        }

        errno = EINVAL;
        return NULL;
    }

    /* case 3: Change the mapping size, but we don't need to change the number
     *   of mapping pages, as the new- and old-end of mapping area reside in
     *   the same block.
     */
    ASSERT(old_page_num == new_page_num);
    rbt_set_value(&alloc_info->alloc_blks, page_idx, new_size);

    return old_addr;
}

void*
lm_mremap(void* old_addr, size_t old_size, size_t new_size, int flags) {
    if (!lm_in_chunk_range(old_addr)) {
        return mremap(old_addr, old_size, new_size, flags);
    }

    void* res = lm_mremap_helper(old_addr, old_size, new_size, flags);
    return res ? res : MAP_FAILED;
}

/*****************************************************************************
 *
 *      Implementation of lm_munmap()
 *
 *****************************************************************************
 */
typedef struct {
    int order;          /* The order of the mapped block */
    int m_page_idx;     /* The index of the 1st page of the mapped block*/
    int m_end_idx;
    int um_page_idx;    /* The index of the 1st page to be unmapped*/
    int um_end_idx;
    size_t m_size;      /* The mmap size in byte.*/
} unmap_info_t;

static int
unmap_lower_part(const unmap_info_t* ui) {
    int order       = ui->order;
    int m_page_idx  = ui->m_page_idx;
    int um_end_idx = ui->um_end_idx;

    // new_ord is used to keep track of the order the remaining allocated blk.
    int new_ord = order;
    int new_page_idx = m_page_idx;
    int split = 0;

    /* Step 1: Try to deallocate leading free blocks */
    while(1) {
        int first_valid_page = um_end_idx + 1;
        int half_blk_ord = new_ord - 1;

        if (new_page_idx + (1 << half_blk_ord) > first_valid_page) {
            // [first_valid_page, new_page_idx + 1<<new_order] contains valid
            // data, it should not be discarded.
            break;
        }

        split = 1;

        /* De-allocate the first half */
        add_free_block(new_page_idx, half_blk_ord);
        new_page_idx += (1 << half_blk_ord);
        new_ord--;
    }

    if (!split)
        return 0;

    remove_alloc_block(m_page_idx);

    /* Step 2: Try the shrink the trailing block */

    /* As many as "alloc_page_num" pages are allocate to accommodate the data.
     * The data is stored in the leading "data_page_num" pages.
     */
    int alloc_page_num = (1 << order) - (new_page_idx - m_page_idx);
    int data_page_num = ui->m_end_idx - new_page_idx + 1;

    while (alloc_page_num >= 2 * data_page_num) {
        new_ord --;
        add_free_block(new_page_idx + (1 << new_ord), new_ord);
        alloc_page_num >>= 1;
    }

    size_t new_map_sz = ui->m_size;
    new_map_sz -= ((new_page_idx - m_page_idx) << alloc_info->page_size_log2);
    add_alloc_block(new_page_idx, new_map_sz, new_ord);

    return 1;
}

static int
unmap_higher_part(const unmap_info_t* ui) {
    int order       = ui->order;
    int m_page_idx  = ui->m_page_idx;
    int um_page_idx = ui->um_page_idx;

    int split = 0;
    int new_ord = order;
    while (m_page_idx + (1 << (new_ord - 1)) >= um_page_idx) {
        new_ord--;
        add_free_block(m_page_idx + (1 << new_ord), new_ord);
        split = 1;
    }

    if (split) {
        int new_sz;
        new_sz = (um_page_idx - m_page_idx) << alloc_info->page_size_log2;
        migrade_alloc_block(m_page_idx, order, new_ord, new_sz);
    }

    return split;
}

/* Helper function of lm_munmap() */
static int
lm_unmap_helper(void* addr, size_t um_size) {
    /* Step 1: preliminary check if the parameters make sense */
    long ofst = ((char*)addr) - ((char*)alloc_info->first_page);
    if (unlikely (ofst < 0)) {
        return 0;
    }

    long page_sz = alloc_info->page_size;
    /* This was previously checked by lm_munmap() */
    ASSERT((ofst & (page_sz - 1)) == 0);

    /* The index of the first page of the area to be unmapped. */
    long um_page_idx = ofst >> log2_int32(page_sz);

    /* step 2: Find the previously mmapped blk which cover the unmapped area.*/
    intptr_t m_size;
    int m_page_idx;
    RBS_RESULT sr = rbt_search_le(&alloc_info->alloc_blks, um_page_idx,
                                  &m_page_idx, &m_size);
    if (unlikely(sr == RBS_FAIL)) {
        return 0;
    }

    /* Check if previously mapped block completely cover the to-be-unmapped
     * area.
     */
    int page_sz_log2 = alloc_info->page_size_log2;
    intptr_t m_end = (((intptr_t)m_page_idx) << page_sz_log2) + m_size;
    intptr_t um_end = (((intptr_t)um_page_idx) << page_sz_log2) + um_size;
    if ((um_end & ~(page_sz - 1)) == (m_end & ~(page_sz - 1))) {
        /* The the ends of mapped area and unmapped area are in the same
         * page, ignore their difference.
         */
        um_end = m_end;
    } else {
        if (um_end > m_end) {
            return 0;
        }
    }

    int m_end_idx = ((m_end + page_sz - 1) >> page_sz_log2) - 1;
    int um_end_idx = ((um_end + page_sz - 1) >> page_sz_log2) - 1;

    /* step 3: try to split the mapped area */

    /* The most common case, unmap the entire mapped block */
    if (m_page_idx == um_page_idx) {
        if (m_end_idx == um_end_idx)
            return free_block(m_page_idx);
    }

    unmap_info_t ui;
    ui.order = alloc_info->page_info[m_page_idx].order;
    ui.m_page_idx = m_page_idx;
    ui.m_end_idx = m_end_idx;
    ui.um_page_idx = um_page_idx;
    ui.um_end_idx = um_end_idx;
    ui.m_size = m_size;

    /* case 1: unmap lower portion */
    if (m_page_idx == um_page_idx)
        return unmap_lower_part(&ui);

    /* case 2: unmap higher portion */
    if (m_end_idx == um_end_idx)
        return unmap_higher_part(&ui);

    /* TODO: case 3: unmap the middle portion. Bit tricky */
    return 0;
}

int
lm_munmap(void* addr, size_t length) {
    /* Step 1: see if the addr is allocated via mmap(2). If so, we need to
     *  unmap it with munmap(2).
     */
    if (!lm_in_chunk_range(addr)) {
        if (ljmm_mode != lm_user_mode)
            return munmap(addr, length);

        errno = EINVAL;
        return -1;
    }

    /* Step 2: unmap the block with the "user-mode" munmap */

    /* The <addr> must be aligned at page boundary */
    int page_sz = alloc_info->page_size;
    if (!length || (((uintptr_t)addr) & (page_sz - 1))) {
        errno = EINVAL;
        return -1;
    }

    if (lm_unmap_helper(addr, length))
        return 0;

    errno = EINVAL;
    return -1;
}

/*****************************************************************************
 *
 *      Implementation of lm_mmap()
 *
 *****************************************************************************
 */
void*
lm_mmap(void *addr, size_t length, int prot, int flags,
         int fd, off_t offset) {

    if (addr /* we completely ignore hint */ ||
        fd != -1 /* Only support anonymous mapp */ ||
        !(flags & MAP_32BIT) /* Otherwise, directly use mmap(2) */ ||
        !length ||
        (flags & MAP_FIXED) /* not suppoted*/) {
        errno = EINVAL;
        return MAP_FAILED;
    }

    void *p = NULL;
    if (ljmm_mode == lm_prefer_sys || ljmm_mode == lm_sys_mode) {
        p = mmap(addr, length, prot, flags, fd, offset);
        if (p != MAP_FAILED || ljmm_mode == lm_sys_mode)
            return p;
    }

    p = lm_malloc(length);
    return p ? p : MAP_FAILED;
}

/*****************************************************************************
 *
 *      Init and Fini
 *
 *****************************************************************************
 */
static int finalized = 1;

/* "ignore_alloc_blk != 0": to unmap allocated chunk even if there are some
 * allocated blocks not yet released.
 */
static inline void
fini_helper(int ignore_alloc_blk) {
    if (finalized)
        return;

    int no_alloc_blk = no_alloc_blocks();
    lm_fini_page_alloc();

    if (no_alloc_blk || ignore_alloc_blk)
        lm_free_chunk();

    finalized = 1;
}

void
lm_fini(void) {
    fini_helper(1);
}

__attribute__((destructor))
static void
lm_fini2(void) {
    /* It is unsafe to unmap the chunk as we are not sure if they are still alive.
     * We don't need to worry about luajit. However, when we stress-test this lib
     * with real-world applications, we find there are memory leakage, and at the
     * time lm_fini2() is called, these allocated blocks are still alive (will be
     * referenced by exit-handlers.
     */
    fini_helper(0);
}

int
lm_init2(ljmm_opt_t* opt) {
    lm_chunk_t* chunk;
    if ((chunk = lm_alloc_chunk(ljmm_mode))) {
        if (lm_init_page_alloc(chunk, opt)) {
            finalized = 0;
            return 1;
        }
    } else {
        /* Look like we run out of (0, 1 GB] space, we have to resort
         * to mmap(2).
         */
        ljmm_mode = lm_sys_mode;
    }

    return 0;
}

int
lm_init(void) {
    return lm_init2(NULL);
}
