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

void*
lm_malloc(size_t sz) {
    if (!alloc_info) {
        lm_init(1);
        if (!alloc_info)
            return NULL;
    }

    /* Determine the order of allocation request */
    int req_order = ceil_log2_int32(sz);
    req_order -= alloc_info->page_size_log2;
    if (req_order < 0)
        req_order = 0;

    int max_order = alloc_info->max_order;
    if (req_order > max_order)
        return 0;

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

    remove_free_block(blk_idx, blk_order);

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
    if (unlikely ((ofst & (page_sz - 1))))
        return 0;

    long page_id = ofst >> log2_int32(page_sz);
    int page_num = alloc_info->page_num;
    if (unlikely(page_id >= page_num))
        return 0;

    lm_page_t* pi = alloc_info->page_info;
    lm_page_t* page = pi + page_id;
    if (unlikely(!is_page_leader(page)))
        return 0;

    if (unlikely(!is_allocated_blk(page)))
        return 0;

    return free_block(page_id);
}

/*****************************************************************************
 *
 *      Implementation of lm_mremap()
 *
 *****************************************************************************
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
    intptr_t new_size2;
    rb_tree_t* rbt = &alloc_info->alloc_blks;
    if (!rbt_search(rbt, page_idx, &new_size2) || new_size2 != new_size) {
        errno = EINVAL;
        return NULL;
    }

    int old_page_num = (old_size + page_sz - 1) >> page_sz_log2;
    int new_page_num = (new_size + page_sz - 1) >> page_sz_log2;

    if (old_page_num > new_page_num) {
        char* unmap_start = (char*)alloc_info->first_page +
                            (new_page_num << page_sz_log2);
        size_t unmap_len = old_size - (((size_t)new_page_num) << page_sz_log2);
        if (lm_unmap_helper(unmap_start, unmap_len)) {
            rbt_set_value(rbt, page_idx, new_size);
            return old_addr;
        }
        errno = EINVAL;
        return NULL;
    }

    if (old_page_num < new_page_num) {
        int order = alloc_info->page_info[page_idx].order;
        if (new_page_num < (1<<order)) {
            rbt_set_value(rbt, page_idx, new_size);
            return old_addr;
        }

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

    ASSERT(old_page_num == new_page_num);
    rbt_set_value(&alloc_info->alloc_blks, page_idx, new_size);
    return old_addr;
}

void*
lm_mremap(void* old_addr, size_t old_size, size_t new_size, int flags) {
    void* res = NULL;
    ENTER_MUTEX;
    res = lm_mremap_helper(old_addr, old_size, new_size, flags);
    LEAVE_MUTEX;

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
    int um_page_idx;
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
    int page_sz = alloc_info->page_size;
    int retval = 0;

    ENTER_MUTEX;
    {
        /* The <addr> must be aligned at page boundary */
        if (!length || (((uintptr_t)addr) & (page_sz - 1))) {
            errno = EINVAL;
            retval = -1;
        } else {
            retval = lm_unmap_helper(addr, length);
            if (!retval)
                errno = EINVAL;
            /* negate the sense of succ. */
            retval = retval ? 0 : -1;
        }
    }
    LEAVE_MUTEX;

    return retval;
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
    void *p = NULL;

    ENTER_MUTEX;
    {
        if (addr /* we completely ignore hint */ ||
            fd != -1 /* Only support anonymous mapp */ ||
            !(flags & MAP_32BIT) /* Otherwise, directly use mmap(2) */ ||
            !length ||
            (flags & MAP_FIXED) /* not suppoted*/) {
            errno = EINVAL;
        } else {
            p = lm_malloc(length);
        }
    }
    LEAVE_MUTEX;

    return p ? p : MAP_FAILED;
}

/*****************************************************************************
 *
 *      Init and Fini
 *
 *****************************************************************************
 */
void
lm_fini(void) {
    ENTER_MUTEX;

    lm_fini_page_alloc();
    lm_free_trunk();

    LEAVE_MUTEX;
}

/* The purpose of this variable is to workaround a link problem: If we were
 * directly feeding lm_fini to atexit() in function lm_init(), we would be
 * going to see a complaint like this:
 *
 *  "...relocation R_X86_64_PC32 against protected symbol `lm_fini' can not
 *   be used when making a shared object"...
 *
 *   I think it's perfectly fine using R_X86_64_PC32 as a relocation for
 * the protected symbol lm_fini. It seems like it's GNU ld (I'm using 2.24)
 * problem. Actually gold linker is able to link successfully.
 *
 *   NOTE: This variable must be visible to other modules, otherwise, with
 * higher optimization level, compiler can propagate its initial value (i.e.
 * the lm_fini) to where it's referenced.
 */
void (*lm_fini_ptr)() __attribute__((visibility("protected"))) = lm_fini;

static inline int
lm_init_helper(int auto_fini, lj_mm_opt_t* mm_opt) {
    int res = 1;
    if (auto_fini != 0) {
        /* Do not directly feed lm_fini to atexit(), see the comment to
         * variable "lm_fini_ptr" for why.
         */
        res = atexit(lm_fini_ptr);

        /* Negate the sense of 'success' :-) */
        res = (res == 0) ? 1 : 0;
    }

    if (res)
        res = lm_init_page_alloc(lm_alloc_trunk(), mm_opt);

    return res;
}

/* Initialize the allocation, return non-zero on success, 0 otherwise. */
int
lm_init(int auto_fini) {
    ENTER_MUTEX;
    int res = lm_init_helper(auto_fini, NULL);
    LEAVE_MUTEX;

    return res;
}

int
lm_init2(int auto_fini, lj_mm_opt_t* mm_opt) {
    ENTER_MUTEX;
    int res = lm_init_helper(auto_fini, mm_opt);
    LEAVE_MUTEX;
    return res;
}
