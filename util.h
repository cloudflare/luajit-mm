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
