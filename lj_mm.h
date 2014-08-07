#ifndef _LJ_MM_H_
#define _LJ_MM_H_

#include <stdlib.h> /* for size_t */

#ifdef __cplusplus
extern "C" {
#endif

#ifdef BUILDING_LIB
    #define LJMM_EXPORT __attribute__ ((visibility ("protected")))
#else
    #define LJMM_EXPORT __attribute__ ((visibility ("default")))
#endif

/* Inititalize the memory-management system. If auto_fini is set
 * (i.e. auto_fini != 0), there is no need to call lm_fini() at exit.
 */
int lm_init(int auto_fini) LJMM_EXPORT;
void lm_fini(void) LJMM_EXPORT;

/* Same prototype as mmap(2), and munmap(2) */
void *lm_mmap(void *addr, size_t length, int prot, int flags,
              int fd, off_t offset) LJMM_EXPORT;

int lm_munmap(void *addr, size_t length) LJMM_EXPORT;
void* lm_mremap(void* old_addr, size_t old_size, size_t new_size, int flags) LJMM_EXPORT;

/* Some "bonus" interface functions */
void* lm_malloc(size_t sz) LJMM_EXPORT;
int lm_free(void* mem) LJMM_EXPORT;

#ifdef DEBUG
void dump_page_alloc(FILE*) LJMM_EXPORT;
#endif

#ifdef __cplusplus
}
#endif

#endif
