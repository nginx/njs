
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) NGINX, Inc.
 */

#ifndef _NXT_ALIGNMENT_H_INCLUDED_
#define _NXT_ALIGNMENT_H_INCLUDED_


#ifndef NXT_MAX_ALIGNMENT

#if (NXT_SOLARIS)
/* x86_64: 16, i386: 4, sparcv9: 16, sparcv8: 8. */
#define NXT_MAX_ALIGNMENT  _MAX_ALIGNMENT

#elif (NXT_WINDOWS)
/* Win64: 16, Win32: 8. */
#define NXT_MAX_ALIGNMENT  MEMORY_ALLOCATION_ALIGNMENT

#elif (__amd64__)
#define NXT_MAX_ALIGNMENT  16

#elif (__i386__ || __i386)
#define NXT_MAX_ALIGNMENT  4

#elif (__arm__)
#define NXT_MAX_ALIGNMENT  16

#else
#define NXT_MAX_ALIGNMENT  16
#endif

#endif


#define nxt_align_size(size, a)                                               \
    (((size) + ((size_t) (a) - 1)) & ~((size_t) (a) - 1))


#define nxt_align_ptr(p, a)                                                   \
    (u_char *) (((uintptr_t) (p) + ((uintptr_t) (a) - 1))                     \
                 & ~((uintptr_t) (a) - 1))

#define nxt_trunc_ptr(p, a)                                                   \
    (u_char *) ((uintptr_t) (p) & ~((uintptr_t) (a) - 1))


#endif /* _NXT_ALIGNMENT_H_INCLUDED_ */
