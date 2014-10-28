#ifndef _LM_UTIL_H_
#define _LM_UTIL_H_

#ifdef DEBUG
    #include <stdlib.h> /* for abort() */
#endif

#ifdef THREAD_SAFE
    void entery_mutex();
    void leave_mutex();
    #define ENTER_MUTEX enter_mutex()
    #define LEAVE_MUTEX leave_mutex()
#else
    #define ENTER_MUTEX
    #define LEAVE_MUTEX
#endif

typedef unsigned int uint;

typedef int page_id_t;
typedef int page_idx_t;

static inline int
ceil_log2_int32 (unsigned num) {
    int res = 31 - __builtin_clz(num);
    res += (num & (num - 1)) ? 1 : 0;
    return res;
}

static inline int
log2_int32(unsigned num) {
    return 31 - __builtin_clz(num);
}

#ifdef FOR_ADAPTOR
    /* The gist of stress testing/benchmarking is to use widely available
     * applications, and bind their malloc()s calls to non-glibc
     * implementation (we use ptmalloc3). The malloc@ptmalloc3 in turn
     * calls functions of the adpator (test/libadaptor.so), which in turn
     * call functions of libljmm.so.
     *
     * If libljmm.so would directly call malloc(), it would form a call cycle,
     * and call cycle would quickly exhaust stack space and making app crash.
     *
     *  So, the libljmm.so should call another malloc implementation to avoid
     * call-cycle. We come up a poor-man's implementation in test/mymalloc.
     *
     *  Please also note, for exactly the same reason, we should avoid calling
     * those libcs function which may potentially call malloc(), for instance
     * dlsym().
     */
    #define MYMALLOC    __adaptor_malloc
    #define MYFREE      __adaptor_free
    #define MYCALLOC    __adaptor_calloc
    #define MYREALLOC   __adaptor_realloc
    void* MYMALLOC(size_t);
    void  MYFREE(void*);
    void* MYCALLOC(size_t, size_t);
    void* MYREALLOC(void*, size_t);
#else
    #define MYMALLOC    malloc
    #define MYFREE      free
    #define MYCALLOC    calloc
    #define MYREALLOC   realloc
#endif

#ifdef DEBUG
    // Usage examples: ASSERT(a > b),  ASSERT(foo() && "Opps, foo() reutrn 0");
    #define ASSERT(c) if (!(c))\
        { fprintf(stderr, "%s:%d Assert: %s\n", __FILE__, __LINE__, #c); abort(); }
#else
    #define ASSERT(c) ((void)0)
#endif

#define likely(x)   __builtin_expect((x),1)
#define unlikely(x) __builtin_expect((x),0)

#endif
