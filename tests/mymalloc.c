#include <sys/mman.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <strings.h> /* for bzero() */
#include <string.h>  /* for memcpy() */

#ifdef DEBUG
    // Usage examples: ASSERT(a > b),  ASSERT(foo() && "Opps, foo() reutrn 0");
    #define ASSERT(c) if (!(c))\
        { fprintf(stderr, "%s:%d Assert: %s\n", __FILE__, __LINE__, #c); abort(); }
#else
    #define ASSERT(c) ((void)0)
#endif

#ifndef FOR_ADAPTOR
    #define MYMALLOC    __wrap_malloc
    #define MYFREE      __wrap_free
    #define MYCALLOC    __wrap_calloc
    #define MYREALLOC   __wrap_realloc
#else
    #define MYMALLOC    __adaptor_malloc
    #define MYFREE      __adaptor_free
    #define MYCALLOC    __adaptor_calloc
    #define MYREALLOC   __adaptor_realloc
#endif

#define MYMALLOC_EXPORT __attribute__ ((visibility ("default")))
void* MYMALLOC(size_t) MYMALLOC_EXPORT;
void  MYFREE(void*) MYMALLOC_EXPORT;
void* MYCALLOC(size_t, size_t) MYMALLOC_EXPORT;
void* MYREALLOC(void*, size_t) MYMALLOC_EXPORT;

typedef int v4si __attribute__ ((vector_size (16)));

static inline int
log2_int32(unsigned num) {
    return 31 - __builtin_clz(num);
}

static inline int
ceil_log2_int32 (unsigned num) {
    int res = 31 - __builtin_clz(num);
    res += (num & (num - 1)) ? 1 : 0;
    return res;
}

#define ENABLE_TRACE    0

#define EXT_SZ          (4096 * 2)
#define MIN_ORDER       5
#define MAX_ORDER       31
#define BIN_NUM         (MAX_ORDER - MIN_ORDER + 1)
#define CHUNK_ALIGN     (__alignof__(v4si))

typedef struct my_malloc_chunk my_chunk_t;
struct my_malloc_chunk {
    unsigned prev_size;
    unsigned this_size;
#ifdef DEBUG
    int magic_number;
#endif
    union {
        struct {
            my_chunk_t* prev_free;
            my_chunk_t* next_free;
        };
        v4si align_data;
    };
};

#ifdef DEBUG
#define MAGIC_NUM  0x5a5a5a
    #define SET_MAGCI_NUM(c)    {(c)->magic_number = MAGIC_NUM; }
    #define VERIFY_MAGIC_NUM(c) ASSERT((c)->magic_number == MAGIC_NUM)
#else
    #define SET_MAGCI_NUM(c)    ((void)0)
    #define VERIFY_MAGIC_NUM(c) ((void)0)
#endif

#define IS_CHUNK_FREE(c)        ((c)->this_size & 1)
#define SET_CHUNK_FREE(c)       {(c)->this_size |= 1;}
#define RESET_CHUNK_FREE(c)     {(c)->this_size &= ~1;}

#define IS_CHUNK_MMAP(c)        ((c)->this_size & 2)
#define SET_CHUNK_MMAP(c)       {(c)->this_size |= 2;}
#define RESET_CHUNK_MMAP(c)     {(c)->this_size &= ~2;}

#define IS_LAST_CHUNK(c)        ((c)->this_size & 4)
#define SET_LAST_CHUNK(c)       {(c)->this_size |= 4; }
#define RESET_LAST_CHUNK(c)     {(c)->this_size &= ~4; }

#define CHUNK_SIZE(c)           ((c)->this_size & ~7)
#define SET_CHUNK_SIZE(c, s)    { typeof(c) t = (c);\
                                  t->this_size = (t->this_size & 7) + s; }

#define offsetof(st, m)     ((size_t)(&((st *)0)->m))
#define CHUNK_OVERHEAD      offsetof(my_chunk_t, align_data)

typedef struct {
    my_chunk_t list;
    int min_size;
} bin_t;

typedef struct my_malloc_info my_malloc_info_t;
struct my_malloc_info {
    int initialized;
    bin_t bins[BIN_NUM];
};

static my_malloc_info_t malloc_info;

#define MMAP_THRESHOLD  (EXT_SZ - sizeof(malloc_info) - CHUNK_ALIGN)

/* Return cur's previous adjacent chunk. If the chunk dose not have previous
 * adjacent chunk, chunk itself is returned.
 */
static inline my_chunk_t*
get_prev_adj_chunk(my_chunk_t* cur) {
    if (cur->prev_size != 0) {
        char* p = ((char*)(void*)cur) - cur->prev_size;
        return (my_chunk_t*)(void*)p;
    }
    return NULL;
}

static inline my_chunk_t*
get_next_adj_chunk(my_chunk_t* chunk) {
    if (!IS_LAST_CHUNK(chunk))
        return (my_chunk_t*)(void*)(CHUNK_SIZE(chunk) + (void*)chunk);
    return NULL;
}

static inline int
is_bin_empty(bin_t* bin) {
    my_chunk_t* list = &bin->list;
    return list->next_free == list && list->prev_free == list;
}

/* Return the min bin index, such that all chunk c in that bin have
 * CHUNK_SIZE(c) > bin->min_size
 */
static inline int
get_bin_idx_for_chunk(int chunk_size) {
    int idx = log2_int32(chunk_size) - MIN_ORDER;
    ASSERT(idx >= 0);
    if (idx >= BIN_NUM)
        idx = BIN_NUM - 1;

    return idx;
}

/* Return the min bin index, such that all chunk c in the bin have
 * CHUNK_SIZE(c) > alloc_sz
 */
static inline int
get_bin_idx_for_alloc(int alloc_sz) {
    int idx = ceil_log2_int32(alloc_sz) - MIN_ORDER;
    ASSERT(idx >= 0);
    if (idx >= BIN_NUM)
        idx = BIN_NUM - 1;

    return idx;
}

static inline void
append_to_bin(bin_t* bin, my_chunk_t* chunk) {
    ASSERT((CHUNK_SIZE(chunk) & (CHUNK_ALIGN - 1)) == 0 &&
           (CHUNK_SIZE(chunk) >= bin->min_size));

    my_chunk_t* insert_after  = bin->list.prev_free;
    my_chunk_t* insert_before = &bin->list;

    chunk->prev_free = insert_after;
    chunk->next_free = insert_before;

    insert_before->prev_free = chunk;
    insert_after->next_free = chunk;
}

static inline my_chunk_t*
pop_from_bin(bin_t* bin) {
    my_chunk_t* first = bin->list.next_free;

    if (first != &bin->list) {
        my_chunk_t* before_1st = first->prev_free;
        my_chunk_t* after_1st = first->next_free;
        before_1st->next_free = after_1st;
        after_1st->prev_free = before_1st;
        first->prev_free = first->next_free = NULL;
        return first;
    }

    return NULL;
}

static inline void
append_free_chunk(my_chunk_t* chunk) {
    ASSERT(IS_CHUNK_FREE(chunk));
    /* look for the right bin for this chunk */
    int chunk_size = CHUNK_SIZE(chunk);
    int bin_idx = get_bin_idx_for_chunk(chunk_size);
    append_to_bin(malloc_info.bins + bin_idx, chunk);
}

/* Remove the free chunk from bin */
static inline void
remove_free_chunk(my_chunk_t* chunk) {
    ASSERT(IS_CHUNK_FREE(chunk));
#ifdef DEBUG
    {
        int chunk_size = CHUNK_SIZE(chunk);
        bin_t* bin = malloc_info.bins + get_bin_idx_for_chunk(chunk_size);
        int found = 0;
        my_chunk_t* iter, *iter_e = &bin->list;
        for (iter = bin->list.next_free;
             iter != iter_e; iter = iter->next_free) {
            if (iter == chunk) {
                found = 1; break;
            }
        }
        ASSERT(found);
    }
#endif

    my_chunk_t* prev = chunk->prev_free;
    my_chunk_t* next = chunk->next_free;
    prev->next_free = next;
    next->prev_free = prev;

    chunk->prev_free = chunk->next_free = NULL;
}

static void
malloc_init(void) {
    int i;
    for (i = 0; i < BIN_NUM; i++) {
        bin_t* bin = malloc_info.bins + i;
        my_chunk_t* list = &bin->list;
        list->prev_free = list->next_free = list;
        bin->min_size = 1 << (i + MIN_ORDER);
    }
    malloc_info.initialized = 1;
}

/* Split the given chunk into two at the specified splitting point, return
 * the second one.
 */
static my_chunk_t*
split_chunk(my_chunk_t* chunk, int split_point) {
    ASSERT((split_point & (CHUNK_ALIGN - 1)) == 0);
    ASSERT(split_point + CHUNK_OVERHEAD <= CHUNK_SIZE(chunk));

    int chunk_sz = CHUNK_SIZE(chunk);
    int chunk2_sz = chunk_sz - split_point;
    ASSERT(chunk2_sz >= sizeof(my_chunk_t));

    my_chunk_t* chunk2;
    chunk2 = (my_chunk_t*)(void*)(split_point + (char*)(void*)chunk);
    chunk2->prev_size = chunk_sz - chunk2_sz;
    SET_CHUNK_SIZE(chunk2, chunk2_sz);
    SET_CHUNK_SIZE(chunk, split_point);

    /* Only the 1st chunk is marked with mapped*/
    RESET_CHUNK_MMAP(chunk2);
    SET_MAGCI_NUM(chunk2);

    if (!IS_LAST_CHUNK(chunk)) {
        my_chunk_t* follow;
        follow = (my_chunk_t*)(void*)(chunk_sz + (char*)(void*)chunk);
        follow->prev_size = chunk2_sz;
        RESET_LAST_CHUNK(chunk2);
    } else {
        RESET_LAST_CHUNK(chunk);
        SET_LAST_CHUNK(chunk2);
    }

    if (IS_CHUNK_FREE(chunk))
        SET_CHUNK_FREE(chunk2);

    return chunk2;
}

static my_chunk_t*
find_big_enough_chunk(size_t alloc_size, int* bin_idx) {
    int bin_idx_tmp = get_bin_idx_for_alloc(alloc_size);
    int i;
    for (i = bin_idx_tmp; i < BIN_NUM; i++) {
        bin_t* bin = malloc_info.bins + i;
        if (is_bin_empty(bin))
            continue;
        break;
    }

    *bin_idx = i;
    if (i < BIN_NUM - 1)
        return pop_from_bin(malloc_info.bins + i);

    if (i == BIN_NUM -1) {
        my_chunk_t* iter, *iter_e;
        for (iter = malloc_info.bins[i].list.next_free,
                iter_e = malloc_info.bins[i].list.prev_free;
            iter != iter_e; iter = iter->next_free) {
            if (CHUNK_SIZE(iter) >= alloc_size) {
                remove_free_chunk(iter);
                return iter;
            }
        }
    }

    return NULL;
}

/* The alloc_sz already take into account the chunk-overhead, and is
 * properly aligned.
 *
 * NOTE: before calling this function, chunk should already be removed from bin.
 */
static void*
malloc_helper(my_chunk_t* chunk, size_t alloc_sz) {
    RESET_CHUNK_FREE(chunk);

    /* Try to split the chunk. */
    unsigned chunk_size = CHUNK_SIZE(chunk);
    ASSERT(((alloc_sz & (CHUNK_ALIGN - 1)) == 0) && chunk_size >= alloc_sz);

    unsigned remain_sz = chunk_size - alloc_sz;
    if (remain_sz > sizeof(my_chunk_t)) {
        my_chunk_t* split = split_chunk(chunk, alloc_sz);
        SET_CHUNK_FREE(split);
        append_free_chunk(split);
    }

    return CHUNK_OVERHEAD + ((char*)(void*)chunk);
}

void*
MYMALLOC(size_t size) {
    if (ENABLE_TRACE)
        fprintf(stderr, "\nmalloc(%lu)\n", size);

    if (!malloc_info.initialized)
        malloc_init();

    size_t norm_size =
        (size + CHUNK_OVERHEAD + CHUNK_ALIGN - 1) & ~(CHUNK_ALIGN - 1);

    void* result = NULL;

    int bin_idx;
    my_chunk_t* chunk = find_big_enough_chunk(norm_size, &bin_idx);
    if (chunk) {
        result = malloc_helper(chunk, norm_size);
        goto malloc_exit;
    }

    /* case 2: no free chunk big enough. Create one via mmap() */
    size_t mmap_sz = EXT_SZ;
    if (mmap_sz < norm_size)
        mmap_sz = norm_size;

    long page_sz = sysconf(_SC_PAGESIZE);
    mmap_sz = (mmap_sz + page_sz - 1) & ~(page_sz - 1);
    result = mmap(NULL, mmap_sz, PROT_READ|PROT_WRITE,
                  MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);
    if (ENABLE_TRACE)
        fprintf(stderr, "  > %p = mmap(%ld)\n", result, mmap_sz);

    if (result == MAP_FAILED)
        goto malloc_exit;

    chunk = (my_chunk_t*)result;
    chunk->prev_size = 0;
    chunk->this_size = mmap_sz;
    SET_LAST_CHUNK(chunk);
    SET_CHUNK_MMAP(chunk);
    SET_CHUNK_FREE(chunk);
    SET_MAGCI_NUM(chunk);

    result = malloc_helper(chunk, norm_size);

malloc_exit:
    if (ENABLE_TRACE)
        fprintf(stderr, "%p = malloc(%ld)\n", result, size);

    return result;
}

void
MYFREE(void* ptr) {
    if (ENABLE_TRACE)
        fprintf(stderr, "\nfree(%p)\n", ptr);

    my_chunk_t* chunk = (my_chunk_t*)(void*)(((char*)ptr) - CHUNK_OVERHEAD);
    ASSERT(!IS_CHUNK_FREE(chunk));
    VERIFY_MAGIC_NUM(chunk);

    while (1) {
        my_chunk_t* prev_adj = get_prev_adj_chunk(chunk);
        my_chunk_t* next_adj = get_next_adj_chunk(chunk);
        int change = 0;

        /* Consolidate with the adjacent following chunk */
        if (next_adj && IS_CHUNK_FREE(next_adj)) {
            remove_free_chunk(next_adj);
            if (IS_LAST_CHUNK(next_adj))
                SET_LAST_CHUNK(chunk);

            int new_sz = CHUNK_SIZE(chunk) + CHUNK_SIZE(next_adj);
            SET_CHUNK_SIZE(chunk, new_sz);
            change = 1;
        }

        /* Consolidate with the previous adjacent chunk */
        if (prev_adj && IS_CHUNK_FREE(prev_adj)) {
            remove_free_chunk(prev_adj);

            if (IS_LAST_CHUNK(chunk))
                SET_LAST_CHUNK(prev_adj);

            int new_sz = CHUNK_SIZE(chunk) + CHUNK_SIZE(prev_adj);
            SET_CHUNK_SIZE(prev_adj, new_sz);
            chunk = prev_adj;
            change = 1;
        }

        if (!change)
            break;
    }

    if (IS_CHUNK_MMAP(chunk) && IS_LAST_CHUNK(chunk)) {
        if (ENABLE_TRACE)
            fprintf(stderr, " > munmap(%p, %u)\n", chunk, CHUNK_SIZE(chunk));
        munmap((void*)chunk, CHUNK_SIZE(chunk));
        return;
    }
    SET_CHUNK_FREE(chunk);
    if (!IS_LAST_CHUNK(chunk)) {
        my_chunk_t* next_adj = get_next_adj_chunk(chunk);
        next_adj->prev_size = CHUNK_SIZE(chunk);
    }

    append_free_chunk(chunk);
}

void*
MYREALLOC(void* ptr, size_t size) {
    if (ENABLE_TRACE)
        fprintf(stderr, "\nrealloc(%p, %lu)\n", ptr, size);

    void* result = ptr;

    /* normalize the size */
    size_t norm_size = (size + CHUNK_ALIGN - 1) & ~(CHUNK_ALIGN - 1);
    norm_size += CHUNK_OVERHEAD;

    my_chunk_t* chunk = (my_chunk_t*)(void*)(((char*)ptr) - CHUNK_OVERHEAD);
    size_t chunk_sz = CHUNK_SIZE(chunk);
    if (norm_size > chunk_sz) {
        result = (my_chunk_t*)MYMALLOC(norm_size);
        if (result) {
            memcpy(result, (void*)&chunk->align_data,
                   chunk_sz - CHUNK_OVERHEAD);
            SET_CHUNK_FREE(chunk);
            append_free_chunk(chunk);
        }
        goto realloc_exit;
    }

    if (chunk_sz - norm_size >= sizeof(my_chunk_t)) {
        /* shrink the allocated block */
        my_chunk_t* another = split_chunk(chunk, norm_size);
        SET_CHUNK_FREE(another);
        append_free_chunk(another);
    }

realloc_exit:
    if (ENABLE_TRACE)
        fprintf(stderr, "%p = realloc(%p, %lu)\n", result, ptr, size);
    return result;
}

void*
MYCALLOC(size_t nmemb, size_t size) {
    size_t t = nmemb * size;
    void* p = MYMALLOC(t);
    if (p)
        bzero(p, t);

    return p;
}

#if 0
static void
my_malloc_verify(void) {
    if (!malloc_info.initialized)
        return;

    int i;
    for (i = 0; i < BIN_NUM; i++) {
        bin_t* bin = malloc_info.bins + i;
        if (is_bin_empty(bin))
            continue;

        my_chunk_t* iter, *iter_e;
        for (iter = bin->list.next_free, iter_e = &bin->list;
             iter != iter_e; iter = iter->next_free)  {
            ASSERT(IS_CHUNK_FREE(iter));
            ASSERT(iter->next_free && iter->prev_free);
        }
    }
}
#endif

void
my_malloc_dump(FILE* f) {
    if (!malloc_info.initialized) {
        return;
    }

    int i;
    for (i = 0; i < BIN_NUM; i++) {
        bin_t* bin = malloc_info.bins + i;
        if (is_bin_empty(bin))
            continue;

        fprintf(f, "BIN:%3d, min_size:%d :", i, bin->min_size);
        my_chunk_t* iter, *iter_e;
        for (iter = bin->list.next_free, iter_e = &bin->list;
             iter != iter_e; iter = iter->next_free)  {
            fprintf(f, "\n\t[chunk %p, size:%d, prev_size:%d, ",
                    iter, CHUNK_SIZE(iter), iter->prev_size);

            fprintf(f, "prev_free:%p, next_free:%p",
                    iter->prev_free, iter->next_free);

            if (IS_CHUNK_FREE(iter))
                fprintf(f, ", free");

            if (IS_CHUNK_MMAP(iter))
                fprintf(f, ", mmap");

            if (IS_LAST_CHUNK(iter))
                fprintf(f, ", last");
            fprintf(f, "] ");
        }

        fprintf(f, "\n");
    }
}
