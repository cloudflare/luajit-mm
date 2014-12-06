#ifndef _CHUNK_H_
#define _CHUNK_H_

#include <stdint.h>
#ifdef DEBUG
#include <stdio.h> /* for FILE */
#endif

/* "Huge" chunk of memmory. Memmory allocations are to carve blocks
 *  from the big chunk.
 */
typedef struct {
    char* base;          /* the starting address of the big chunk */
    uint64_t size;       /* page_num * page_size */
    uint32_t page_num;   /* number of pages in the chunk */
    uint32_t page_size;  /* cache of sysconf(_SC_PAGESIZE); */
} lm_chunk_t;

extern lm_chunk_t lm_big_chunk;

lm_chunk_t* lm_alloc_chunk();
void lm_free_chunk(void);

static inline int lm_in_chunk_range(void* ptr) {
    char* t = (char*) ptr;
    return t >= lm_big_chunk.base &&
           (t < lm_big_chunk.base + lm_big_chunk.size);
}

#ifdef DEBUG
void lm_dump_chunk(FILE*);
void dump_page_alloc(FILE*);
#endif

#endif
