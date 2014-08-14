#include <unistd.h>
#include <stdlib.h>
#include <malloc.h>
#include <strings.h> /* for bzero() */
#include "util.h"
#include "trunk.h"
#include "lj_mm.h"

#define LM_ADDR_UPBOUND   ((uint)0x80000000)
#define SIZE_1MB            ((uint)0x100000)
#define SIZE_1GB            ((uint)0x40000000)

/* If we end up allocating a trunk no larger than this size, we might as well
 * pull the plug, and give up for good.
 */
#define MEM_TOO_SMALL   (SIZE_1MB * 8)

static lm_trunk_t big_trunk;

static int
get_malloc_mmap_max (void) {
    /* Per mallopt(3), the default size of max mmap threshold is 65535 */
    size_t mmap_max = 65536;

    const char* envvar = getenv("MALLOC_MMAP_MAX_");
    if (envvar)
        mmap_max = atoi(envvar);

    return mmap_max;
}

lm_trunk_t*
lm_alloc_trunk (void) {
    if (big_trunk.base)
        return &big_trunk;

    void* cur_brk = sbrk(0);
    uint avail = LM_ADDR_UPBOUND - ((intptr_t)cur_brk);
    if (avail < MEM_TOO_SMALL) {
        /* We can achieve almost nothing with 1MB, might as well bail out. */
        return 0;
    }

    /* Disable malloc using mmap */
    int mmap_max = get_malloc_mmap_max ();
    if (!mallopt(M_MMAP_MAX, 0))
        return 0;

    intptr_t trunk = (intptr_t)malloc (avail);

    /* Restore the original M_MMAP_MAX*/
    if (!mallopt (M_MMAP_MAX, mmap_max)) {
        /* Hey, not all gotos are bad. */
        goto fail_ret;
    }

    if (trunk > LM_ADDR_UPBOUND ||
        (LM_ADDR_UPBOUND - (intptr_t)trunk) <= MEM_TOO_SMALL) {
        goto fail_ret;
    }

    long page_sz = sysconf(_SC_PAGESIZE);
    intptr_t usable_start = (trunk + page_sz - 1) & ~(page_sz - 1);
    intptr_t usable_end = (trunk + avail) & ~(page_sz - 1);
    if (usable_end > LM_ADDR_UPBOUND)
        usable_end = LM_ADDR_UPBOUND;

    if (usable_end - usable_start <= MEM_TOO_SMALL)
        goto fail_ret;

    big_trunk.base = (char*)trunk;
    big_trunk.start = (char*)usable_start;
    big_trunk.alloc_size = avail;
    big_trunk.usable_size = usable_end - usable_start;
    big_trunk.page_size = page_sz;
    big_trunk.page_num = big_trunk.usable_size / page_sz;
    ASSERT(big_trunk.page_num * big_trunk.page_size == big_trunk.usable_size);

    return &big_trunk;

fail_ret:
    free ((void*)trunk);
    return 0;
}

void
lm_free_trunk(void) {
    ENTER_MUTEX
    if (big_trunk.base) {
        free(big_trunk.base);
        bzero(&big_trunk, sizeof(big_trunk));
    }

    LEAVE_MUTEX
}

#ifdef DEBUG
void
lm_dump_trunk (FILE* f) {
    uint num_page = big_trunk.usable_size / sysconf(_SC_PAGESIZE);

    fprintf(f, "Base:%8p, alloc-size :%fG, usage-start:%8p, usage-size %fG (%u pages)\n",
            big_trunk.base, big_trunk.alloc_size / ((float)SIZE_1GB),
            big_trunk.start, big_trunk.usable_size / ((float)SIZE_1GB),
            num_page);
}
#endif
