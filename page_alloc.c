#include <stdint.h> /* for intptr_t */
#include <unistd.h>
#include <errno.h>
#include <sys/mman.h>
#include "lm_util.h"
#include "lm_internal.h"
#include "lj_mm.h"
#include "rbtree.h"
#include "lj_mm.h"

typedef int page_id_t;
typedef int page_idx_t;

/* Forward Decl */
static int add_block(page_id_t block, int order);
static int log2_int32(unsigned num);

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

/**************************************************************************
 *
 *              Buddy allocation stuff.
 *
 **************************************************************************
 */

/* Forward Decl */

/* We could have up to 1M pages (4G/4k). Hence 20 */
#define MAX_ORDER 20
#define INVALID_ORDER (-1)

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

static lm_alloc_t* alloc_info;

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

/* Initialize the page allocator, return 0 on success, 1 otherwise. */
int
lm_init_page_alloc(lm_trunk_t* trunk) {
    if (!trunk) {
        /* Trunk is not yet allocated */
        return 0;
    }

    if (alloc_info) {
        /* This function was succesfully invoked before */
        return 1;
    }

    int page_num = trunk->page_num;
    int alloc_sz = sizeof(lm_alloc_t) +
                   sizeof(lm_page_t) * (page_num + 1);

    alloc_info = (lm_alloc_t*) malloc(alloc_sz);
    if (!alloc_info)
        return 0;

    alloc_info->first_page = trunk->start;
    alloc_info->page_num   = page_num;
    alloc_info->page_size  = trunk->page_size;
    alloc_info->page_size_log2 = log2_int32(trunk->page_size);

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
            add_block(page_idx, order);
            page_idx += (1 << order);
        }
    }

    return 1;
}

static int
ceil_log2_int32 (unsigned num) {
    int res = 31 - __builtin_clz(num);
    res += (num & (num - 1)) ? 1 : 0;
    return res;
}

static int
log2_int32(unsigned num) {
    return 31 - __builtin_clz(num);
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

static int
free_block(page_idx_t page_idx) {
    int del_res = rbt_delete(&alloc_info->alloc_blks, page_idx);
#ifdef DEBUG
    ASSERT(del_res);
#else
    (void)del_res;
#endif

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
        remove_block(buddy_idx, order);
        page_id = page_id < buddy_id ? page_id : buddy_id;
        order++;
    }

    add_block(page_id_to_idx(page_id), order);
    return 1;
}


/**************************************************************************
 *
 *       Implementation of this package's interface funcs
 *
 **************************************************************************
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

/* Initialize the allocation, return non-zero on success, 0 otherwise. */
int
lm_init(int auto_fini) {
    int res = 1;

    ENTER_MUTEX;
    if (auto_fini != 0) {
        /* Do not directly feed lm_fini to atexit(), see the comment to
         * variable "lm_fini_ptr" for why.
         */
        res = atexit(lm_fini_ptr);

        /* Negate the sense of 'success' :-) */
        res = (res == 0) ? 1 : 0;
    }

    if (res)
        res = lm_init_page_alloc(lm_alloc_trunk());

    LEAVE_MUTEX;

    return res;
}

void*
lm_mmap(void *addr, size_t length, int prot, int flags,
         int fd, off_t offset) {
    void *p = NULL;

    ENTER_MUTEX;
    {
        if (addr || fd != -1 || !(flags & MAP_32BIT) || !length) {
            errno = EINVAL;
        } else {
            p = lm_malloc(length);
        }
    }
    LEAVE_MUTEX;

    return p ? p : MAP_FAILED;
}

static int
lm_unmap_helper(void* addr, size_t length) {
    long ofst = ((char*)addr) - ((char*)alloc_info->first_page);
    if (unlikely (ofst < 0))
        return 0;

    long page_sz = alloc_info->page_size;
    if (unlikely ((ofst & (page_sz - 1))))
        return 0;

    long page_id = ofst >> log2_int32(page_sz);
    intptr_t map_size;
    if (rbt_search(&alloc_info->alloc_blks, page_id, &map_size) == 0)
        return 0;

    int page_sz_log2 = alloc_info->page_size_log2;
    int map_pages = ((uintptr_t)(page_sz - 1 + map_size)) >> page_sz_log2;
    int rel_pages = ((uintptr_t)(page_sz - 1 + length)) >> page_sz_log2;

    if (map_pages != rel_pages)
        return 0;

    return free_block(page_id);
}

int
lm_munmap(void* addr, size_t length) {
    int page_sz = alloc_info->page_size;
    length = (length + (page_sz - 1)) & ~(page_sz - 1);
    int retval = 0;

    ENTER_MUTEX;
    {
        if (!length || (((uintptr_t)addr) & (page_sz - 1))) {
            errno = EINVAL;
            retval = -1;
        } else {
            retval = lm_unmap_helper(addr, length);
            /* negate the sense of succ. */
            retval = retval ? 0 : -1;
        }
    }
    LEAVE_MUTEX;

    return retval;
}

void*
lm_mremap(void* old_addr, size_t old_size, size_t new_size, int flags) {
    (void)old_addr;
    (void)old_size;
    (void)new_size;
    (void)flags;
    return MAP_FAILED;
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
    for (i = req_order; i < max_order; i++) {
        rb_tree_t* rbt = free_blks + i;
        if (!rbt_is_empty(rbt)) {
            blk_idx = rbt_get_min(rbt);
            blk_order = i;
            break;
        }
    }

    if (blk_idx == -1)
        return NULL;

    remove_block(blk_idx, blk_order);
    alloc_info->page_info[blk_idx].order = req_order;

    /* The free block may be too big. If this is the case, keep splitting
     * the block until it tightly fit the allocation request.
     */
    int bo = blk_order;
    while (bo > req_order) {
        bo --;
        int split_block = blk_idx + (1 << bo);
        add_block(split_block, bo);
    }

    int insert_res = rbt_insert(&alloc_info->alloc_blks, blk_idx, (intptr_t)sz);
    rbt_verify(&alloc_info->alloc_blks);
#ifdef DEBUG
    ASSERT(insert_res);
#else
    (void)insert_res;
#endif
    return alloc_info->first_page + (blk_idx << alloc_info->page_size_log2);
}

/**************************************************************************
 *
 *       Debugging Support & Misc "cold" functions
 *
 **************************************************************************
 */
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
            fprintf(f, "%d (%p, len=%d), ", page_idx_to_id(page_idx),
                    addr, (int)node->value);
            verify_order(page_idx, i);
        }
        fputs("\n", f);
    }
}
#endif
