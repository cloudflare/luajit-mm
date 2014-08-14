#ifndef _LJ_MM_H_
#define _LJ_MM_H_

#include <stdlib.h> /* for size_t */
#include <stdio.h>  /* for FILE* */

#ifdef __cplusplus
extern "C" {
#endif

#ifdef BUILDING_LIB
    #define LJMM_EXPORT __attribute__ ((visibility ("protected")))
#else
    #define LJMM_EXPORT __attribute__ ((visibility ("default")))
#endif

/* The options for memory-management. Currently, it's only for debugging
 * and testing purpose.
 */
typedef struct {
    /* < 0       : the initial trunk contains as many pages as possible:
     * otherwise : the init trunk contains *exactly* as many pages as specified.
     */
    int page_num;
} lj_mm_opt_t;

/* Inititalize the memory-management system. If auto_fini is set
 * (i.e. auto_fini != 0), there is no need to call lm_fini() at exit.
 */
int lm_init(int auto_fini) LJMM_EXPORT;
int lm_init2(int auto_fini, lj_mm_opt_t*) LJMM_EXPORT;
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

lm_status_t* lm_alloc_stat(void);
void lm_free_alloc_stat(lm_status_t*);

#ifdef DEBUG
void dump_page_alloc(FILE*) LJMM_EXPORT;
#endif

#ifdef __cplusplus
}
#endif

#endif
