#ifndef _LM_UTIL_H_
#define _LM_UTIL_H_

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
