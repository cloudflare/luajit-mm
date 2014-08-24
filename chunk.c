#include <sys/mman.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdint.h>
#include <strings.h> /* for bzero() */
#include "util.h"
#include "chunk.h"
#include "lj_mm.h"
#include "ljmm_conf.h"

#define SIZE_1MB ((uint)0x100000)
#define SIZE_1GB ((uint)0x40000000)

/* If we end up allocating a chunk no larger than this size, we might as well
 * pull the plug, and give up for good.
 */
#define MEM_TOO_SMALL (SIZE_1MB * 8)

static lm_chunk_t big_chunk;

lm_chunk_t*
lm_alloc_chunk (void) {
    if (big_chunk.base)
        return &big_chunk;

    uintptr_t cur_brk = (uintptr_t)sbrk(0);
    uintptr_t page_sz = sysconf(_SC_PAGESIZE);
    cur_brk = (page_sz - 1 + cur_brk) & ~(page_sz - 1);

    uint avail = LJMM_AS_UPBOUND - ((intptr_t)cur_brk);
    avail = avail & ~(page_sz - 1);
    if (avail < MEM_TOO_SMALL) {
        /* We can achieve almost nothing with 1MB, might as well bail out. */
        return 0;
    }

    uintptr_t chunk = (uintptr_t)
        mmap((void*)cur_brk, avail, PROT_READ|PROT_WRITE,
             MAP_PRIVATE | MAP_32BIT | MAP_ANONYMOUS, -1, 0);

    if (!chunk)
        return NULL;

    big_chunk.base = (char*)chunk;
    big_chunk.start = (char*)chunk;
    big_chunk.alloc_size = avail;
    big_chunk.usable_size = avail;
    big_chunk.page_size = page_sz;
    big_chunk.page_num = big_chunk.usable_size / page_sz;

    return &big_chunk;
}

void
lm_free_chunk(void) {
    ENTER_MUTEX
    if (big_chunk.base) {
        munmap(big_chunk.base, big_chunk.alloc_size);
        bzero(&big_chunk, sizeof(big_chunk));
    }
    LEAVE_MUTEX
}

#ifdef DEBUG
void
lm_dump_chunk (FILE* f) {
    uint num_page = big_chunk.usable_size / sysconf(_SC_PAGESIZE);

    fprintf(f, "Base:%8p, alloc-size :%fG, usage-start:%8p, usage-size %fG (%u pages)\n",
            big_chunk.base, big_chunk.alloc_size / ((float)SIZE_1GB),
            big_chunk.start, big_chunk.usable_size / ((float)SIZE_1GB),
            num_page);
}
#endif
