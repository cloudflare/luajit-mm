#ifndef _LJ_MM_H_
#define _LJ_MM_H_

#include <stdlib.h> /* for size_t */
#include <stdio.h>  /* for FILE* */

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    /* lm_mmap() is done exclusively by the user mode mmap. */
    lm_user_mode = 0,

    /* lm_mmap() is done exclusively by the mmap(2). */
    lm_sys_mode = 1,

    /* lm_mmap() attempts user-mode mmap first, then mmap(2). */
    lm_prefer_user = 2,

    /* lm_mmap attemps mmap(2) first. If it was not succesful,
     * attempt user-mode mmap
     */
    lm_prefer_sys = 3,

    lm_default = lm_user_mode
} ljmm_mode_t;

/* Additional options, primiarilly for debugging purpose */
typedef struct {
    ljmm_mode_t mode;

    /* For debugging/testing purpose only. It is ask page-allocator to
     * allocate as many pages as the dbg_alloc_page_num specfies. The
     * nubmer is usually very small, say 8, 12, which make it very easy
     * to check the status after each opeator.
     *
     * Negative value means default value.
     */
    int dbg_alloc_page_num;

    /* Tweak block-cache, currently not enabled */
    int enable_block_cache;
    int blk_cache_in_page;
} ljmm_opt_t;

/* All exported symbols are prefixed with ljmm_ to reduce the chance of
 * conflicting with applications being benchmarked.
 */
#define lm_init_mm_opt  ljmm_init_mm_opt
#define lm_init         ljmm_init
#define lm_init2        ljmm_init2
#define lm_fini         ljmm_fini
#define lm_mmap         ljmm_mmap
#define lm_munmap       ljmm_munmap
#define lm_mremap       ljmm_mremap
#define lm_malloc       ljmm_malloc
#define lm_free         ljmm_free
#define lm_get_status   ljmm_get_status
#define lm_free_status  ljmm_free_status

#ifdef BUILDING_LIB
    #define LJMM_EXPORT __attribute__ ((visibility ("protected")))
#else
    #define LJMM_EXPORT __attribute__ ((visibility ("default")))
#endif

/* Populate ljmm_opt_t with default value */
void lm_init_mm_opt(ljmm_opt_t* opt) LJMM_EXPORT;

/* Inititalize the memory-management system. */
int lm_init(void) LJMM_EXPORT;
int lm_init2(ljmm_opt_t*) LJMM_EXPORT;
void lm_fini(void) LJMM_EXPORT;

/* Same prototype as mmap(2), and munmap(2) */
void *lm_mmap(void *addr, size_t length, int prot, int flags,
              int fd, off_t offset) LJMM_EXPORT;

int lm_munmap(void *addr, size_t length) LJMM_EXPORT;
void* lm_mremap(void* old_addr, size_t old_size, size_t new_size, int flags) LJMM_EXPORT;

/* Some "bonus" interface functions */
void* lm_malloc(size_t sz) LJMM_EXPORT;
int lm_free(void* mem) LJMM_EXPORT;

/* Testing/Debugging Support */
typedef struct {
    int page_idx;
    int order;
    int size;
} block_info_t;

typedef struct {
    char* first_page;
    int page_num;
    int free_blk_num;
    int alloc_blk_num;
    int idx_to_id;
    block_info_t* free_blk_info;
    block_info_t* alloc_blk_info;
} lm_status_t;

const lm_status_t* lm_get_status(void) LJMM_EXPORT;
void lm_free_status(lm_status_t*) LJMM_EXPORT;

#ifdef DEBUG
void dump_page_alloc(FILE*) LJMM_EXPORT;
#endif

#ifdef __cplusplus
}
#endif

#endif
