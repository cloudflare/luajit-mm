/* This file is to allocate a big chunk of memory right after .bss. Subsequent
 * lm_mmap() is to serve the allocation request by carving smaller blocker out
 * of this big chunk of memory.
 */
#include <sys/mman.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdint.h>
#include <strings.h> /* for bzero() */
#include "util.h"
#include "chunk.h"
#include "lj_mm.h"

#define SIZE_1MB ((uint)0x100000)
#define SIZE_1GB ((uint)0x40000000)
#define SIZE_2GB ((uint)0x40000000)

/* If we end up allocating a chunk no larger than this size, we might as well
 * pull the plug, and give up for good.
 */
#define MEM_TOO_SMALL (SIZE_1MB * 8)

lm_chunk_t lm_big_chunk;

lm_chunk_t*
lm_alloc_chunk (ljmm_mode_t mode) {
    if (lm_big_chunk.base)
        return &lm_big_chunk;

    uintptr_t cur_brk = (uintptr_t)sbrk(0);
    uintptr_t page_sz = sysconf(_SC_PAGESIZE);

    /* The chunk must be page-aligned, and are multiple pages in size. */
    cur_brk = (page_sz - 1 + cur_brk) & ~(page_sz - 1);
    uintptr_t upbound = (mode == lm_user_mode) ? SIZE_2GB : SIZE_1GB;
    if (cur_brk >= upbound)
        return NULL;

    uintptr_t avail = upbound - cur_brk;
    avail = avail & ~(page_sz - 1);
    if (avail < MEM_TOO_SMALL) {
        /* Bail out as we can achieve almost nothing with 1MB.*/
        return NULL;
    }

    uintptr_t chunk = (uintptr_t)
        mmap((void*)cur_brk, avail, PROT_READ|PROT_WRITE,
             MAP_PRIVATE | MAP_32BIT | MAP_ANONYMOUS, -1, 0);

    if (chunk == (uintptr_t)MAP_FAILED)
        return NULL;

    /* If the program linked to this lib generates core-dump, do not dump those
     * portions which are not allocated at all.
     */
    madvise((void*)chunk, avail, MADV_DONTNEED | MADV_DONTDUMP);

    lm_big_chunk.base = (char*)chunk;
    lm_big_chunk.size = avail;
    lm_big_chunk.page_size = page_sz;
    lm_big_chunk.page_num = avail / page_sz;

    return &lm_big_chunk;
}

void
lm_free_chunk(void) {
    if (lm_big_chunk.base) {
        munmap(lm_big_chunk.base, lm_big_chunk.size);
        bzero(&lm_big_chunk, sizeof(lm_big_chunk));
    }
}
