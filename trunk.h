#ifndef _LM_INTERNAL_H_
#define _LM_INTERNAL_H_

#include <stdio.h> /* for FILE */

#define LJMM_ADDR_UPBOUND ((unsigned int)0x80000000)

/* "Huge" chunk of memmory. Memmory allocations are to carve blocks
 *  from the big trunk.
 */
typedef struct {
    char* base;                 /* the starting address of the big trunk */
    char* start;                /* the starting address of the usable portion */
    unsigned alloc_size;        /* the size of the big trunk */
    unsigned usable_size;       /* the size of the usable portion.
                                 * usabe_size = page_num * page_size.
                                 */
    unsigned page_num;           /* number of available pages */
    unsigned page_size;          /* cache of sysconf(_SC_PAGESIZE); */
} lm_trunk_t;

lm_trunk_t* lm_alloc_trunk(void);
void lm_free_trunk(void);
#ifdef DEBUG
void lm_dump_trunk(FILE* f);
#endif

int lm_init_page_alloc(lm_trunk_t*);
void lm_fini_page_alloc(void);
#ifdef DEBUG
void dump_page_alloc(FILE* f);
#endif

#endif
