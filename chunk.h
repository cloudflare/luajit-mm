#ifndef _LM_INTERNAL_H_
#define _LM_INTERNAL_H_

#include <stdio.h> /* for FILE */

#define LJMM_ADDR_UPBOUND ((unsigned int)0x80000000)

/* "Huge" chunk of memmory. Memmory allocations are to carve blocks
 *  from the big chunk.
 */
typedef struct {
    char* base;                 /* the starting address of the big chunk */
    char* start;                /* the starting address of the usable portion */
    unsigned alloc_size;        /* the size of the big chunk */
    unsigned usable_size;       /* the size of the usable portion.
                                 * usabe_size = page_num * page_size.
                                 */
    unsigned page_num;           /* number of available pages */
    unsigned page_size;          /* cache of sysconf(_SC_PAGESIZE); */
} lm_chunk_t;

lm_chunk_t* lm_alloc_chunk(void);
void lm_free_chunk(void);
#ifdef DEBUG
void lm_dump_chunk(FILE* f);
#endif

#ifdef DEBUG
void dump_page_alloc(FILE* f);
#endif

#endif