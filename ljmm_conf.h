#ifndef _LJMM_CONFIG_H
#define _LJMM_CONFIG_H

/* allocate block from addr space [lowest-available, LJMM_AS_UPBOUND) via
 * lm_mmap().
 */
#define LJMM_AS_UPBOUND ((uintptr_t)0x80000000)

/* Alloc block from [LJMM_AS_UPBOUND, LJMM_AS_LIMIT) via mmap(...MAP_32BIT) */
#define LJMM_AS_LIMIT  ((uintptr_t)0x80000000)

#endif
