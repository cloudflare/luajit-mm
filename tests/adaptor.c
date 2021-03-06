/*
 * The purpose of this program is twofold:
 *   o. provide a way to test and stress test libljmm.{a|so}
 *   o. provide a way to measure the performance impact of these libs
 *      using widely available applications.
 *
 *   The key to achieve above goals is to intercept function calls to
 * mmap/munmap/mremap(), and override them with lm_mmap/lm_unmap/lm_remap()
 * respectively.
 *
 *  Make mmap() hotter
 * --------------------
 *
 *   While most applications don't call mmap() frequently, they usually call
 * malloc() extensively. As far as I can understand, malloc() depends on mmap()
 * in at least two ways:
 *   1. When allocating a big chunk of memory, say >128k, malloc() will
 *      mmap() instead of expanding heap via brk()/sbrk() to serve the request.
 *
 *   2. If malloc() is not able to expand heap by calling brk()/sbrk(), it will
 *      rely on mmap to allocate a huge/big memory block, and carving smaller
 *      block out of the mmapped huge/big block to serve the allocation request.
 *
 *   We can make above senario 2) take place simply by mmapping a block right
 * after sbrk(0). Subsequent calls to brk() would not prevail because the
 * mmapped block is in the way.
 *
 *  Override mmap()
 * -----------------
 *   Making mmap() hotter is relatively easy undertaking, to override mmap() is
 * bit involved. The difficulty stems from the fact that overriding mmap
 *   s1). will call our fake mmap() (i.e. lm_mmap()) for special situations, and
 *   s2). will call default mmap (i.e. the one being overrided) to take care the
 *        remainning sitations.
 *
 *   For situation s2, We call dlsym() get the entry of the default/overrided
 * mmap(), however, dlsym() may call malloc() which in turn may call the
 * new/overriding mmap(). So, there will be a infinite recurisve call to the
 * new/overriding mmap() when the situation s2 is happening.
 *
 *   We break the dependence cycle by letting malloc() call __wrap_mmap()
 * instead of mmap(). When s2 is taking place, __wrap_mmap() calls mmap()
 * directly.
 *
 *  Build and Testing
 * -------------------
 *   Following is the detail about how to build this program and how to use
 * it for testing/benchmark.
 *
 *  1. Invoke "make libadaptor.so" or "make all" to build the libadaptor.so
 *
 *  2. Download Wolfram Gloger's malloc from
 *      http://www.malloc.de/malloc/ptmalloc3-current.tar.gz
 *
 *      Alternatively, you can directly build a modified glibc so long as you
 *    dare. I need to remind you that building glibc entails iron will,
 *    thick face, and other qualities.
 *
 *  3. Build the ptmalloc3 with:
 *      make -C src/dir linux-shared \
 *         OPT_FLAGS='-O3 -march=native -pthread -Wl,--wrap=mmap \
 *         -Wl,--wrap=munmap -Wl,--wrap=mremap'
 *
 *     It will successfuly build libptmalloc3.so, but fail to build t-test1,
 *   which we don't need. The failure is due to undefined symbol of _wrap_xxx()
 *   which are defined in this file.
 *
 *    o. The -pthread is tell compiler to insert -lpthread at right place such
 *       that the dependece to the libpthread.so will be explictly encoded in
 *       the libptmalloc3.so, and will be automatically loaded by the system
 *       dynamic loader.
 *
 *    o. the -Wl,--wrap=xxx is to let linker the replace symbol xxx with
 *       __wrap_xxx.
 *
 *  4. set LD_LIBRARY_PATH properly to include the path to libljmm.so.
 *
 *  5. run application with libptmalloc3.so and libadaptor.so .e.g
 *   LD_PRELOAD="/the/path/to/libptmalloc3.so /the/path/to/libadaptor.so" \
 *      /my/application [arguments]
 *
 *   Alternatively, one can link applications with
 *     -L/path/to/ptmalloc3 -lptmalloc3 -L/path/to/libadaptor.so -ladaptor.
 *   Make sure the `ldd -r applications` commands list libptmalloc3.so and
 *   libadpator.so before glibc. This way, the libptmalloc3.so and
 *   libadaptor.so will be automatically loaded when the applications are
 *   launched, and the malloc() is resolved to the one in libptmalloc3.so
 *   instead of glibc.
 *
 * How the stress-testing works
 * -----------------------------
 *   Suppose executable a.out is used to test this work, and a.out call
 * malloc() at least once.
 *
 *   Since libptmalloc3.so is in LD_PRELOAD (step 5), the malloc()@libptmalloc3
 * instead of malloc@glibc is called.
 *
 *   The malloc()@libptmalloc3 in turn call __wrap_mmap() (NOTE that in step 3
 * libptmalloc3 was linked with -Wl,--wrap=munmap), and the __wrap_mmap() is
 * hooking to our implementation of mmap()
 *
 * Miscellaneous
 * -------------
 *   Some functionalities can be turned on/off via following environment
 *   variables:
 *     - ENABLE_LJMM = {0|1}
 *     - ENABLE_LJMM_TRACE = {0|1}
 */
#ifndef _GNU_SOURCE
    #define _GNU_SOURCE
#endif
#include <sys/mman.h>
#include <errno.h>
#include <unistd.h>
#include <dlfcn.h>
#include <stdint.h>
#include <stdio.h>
#include <malloc.h>
#include "lj_mm.h"
#include "util.h"

/* When debugging, turn enable_ljmm off, and manually call init_before_main()
 * right after main() is hit.
 */
static int enable_ljmm = 1;
static int enable_trace = 0;

/* Set to non-zero after initialization is successfully done.*/
static int init_done = 0;

/* The upbound of the address space */
#define LJMM_AS_UPBOUND  ((uintptr_t)0x80000000)

/* "noinline" such that we can manually invoke this function in gdb */
static int __attribute__((noinline))
init_adaptor(void) {
    const char* func_name = __FUNCTION__;
    if (!lm_init()) {
        fprintf(stderr, "%s: fail to call lm_init()\n", func_name);
        return 0;
    }

    return init_done = 1;
}

void __attribute__((constructor))
init_before_main() {
    const char* t = getenv("ENABLE_LJMM");
    if (t) {
        int res = sscanf(t, "%d", &enable_ljmm);
        if (res < 0 || t[res] != '\0' ||
            (enable_ljmm != 0 && enable_ljmm != 1)) {
            fprintf(stderr, "ENABLE_LJMM={0|1}\n");
            enable_ljmm = 0;
        }
    }

    t = getenv("ENABLE_LJMM_TRACE");
    if (t) {
        int res = sscanf(t, "%d", &enable_trace);
        if (res < 0 || t[res] != '\0' ||
            (enable_trace != 0 && enable_trace != 1)) {
            fprintf(stderr, "ENABLE_LJMM_TRACE={0|1}\n");
            enable_trace = 0;
        }
    }

    if (enable_ljmm)
        init_adaptor();
}

void*
__wrap_mmap64(void *addr, size_t length, int prot, int flags,
       int fd, off_t offset) {
    const char* func = __FUNCTION__;
    void* blk = NULL;

    if (init_done && !addr && (flags & (MAP_ANONYMOUS|MAP_ANON))) {
        blk = lm_mmap(addr, length, prot, flags|MAP_32BIT, fd, offset);
        if (unlikely(enable_trace)) {
            fprintf(stderr,
                    "%s: call lm_mmap: %p = (%p, %lu, %d, %d, %d, %lu)\n",
                    func, blk, addr, length, prot, flags, fd, offset);
        }

        if (blk || errno != ENOMEM)
            return blk;

        /* Fall back to mmap() */
        if (unlikely(enable_trace)) {
            fprintf(stderr, "%s: OOM\n", func);
        }
    }

    blk = mmap64(addr, length, prot, flags, fd, offset);
    if (unlikely(enable_trace)) {
        fprintf(stderr, "mmap: %p = (%p, %lu, %d, %d, %d, %lu)\n",
                blk, addr, length, prot, flags, fd, offset);
    }

    return blk;
}

void*
__wrap_mmap(void *addr, size_t length, int prot, int flags,
     int fd, off_t offset) {
    return __wrap_mmap64(addr, length, prot, flags, fd, offset); }

int
__wrap_munmap(void *addr, size_t length) {
    if (!init_done || addr >= (void*)LJMM_AS_UPBOUND) {
        int ret = munmap(addr, length);
        if (unlikely(enable_trace)) {
            fprintf(stderr, "munmap: %d = (%p, %lu)\n", ret, addr, length);
        }
        return ret;
    }

    int ret = lm_munmap(addr, length);
    if (unlikely(enable_trace))
        fprintf(stderr, "lm_munmap: %d = (%p, %lu)\n", ret, addr, length);

    return ret;
}

void*
__wrap_mremap(void *old_addr, size_t old_size, size_t new_size,
              int flags, ...) {
    fprintf(stderr, "WTF!, remap is called\n");
    if (!init_done || old_addr > (void*)LJMM_AS_UPBOUND) {
        void* p = NULL;
        if (!(flags & MREMAP_FIXED)) {
            p = mremap(old_addr, old_size, new_size, flags);
        } else {
            /* Too complicated, give up for now*/
            p = MAP_FAILED;
        }
        if (unlikely(enable_trace))
            fprintf(stderr, "mremap: %p = (%p, %lu, %lu, %d)\n",
                    p, old_addr, old_size, new_size, flags);
        return p;
    }

    void* p = lm_mremap(old_addr, old_size, new_size, flags);
    if (unlikely(enable_trace))
        fprintf(stderr, "lm_mremap: %p = (%p, %lu, %lu, %d)\n",
                p, old_addr, old_size, new_size, flags);
    return p;
}
