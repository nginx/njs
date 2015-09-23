
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) NGINX, Inc.
 */

#ifndef _NXT_CLANG_H_INCLUDED_
#define _NXT_CLANG_H_INCLUDED_


#include <stddef.h>       /* offsetof(). */
#include <unistd.h>       /* NULL. */


#define nxt_inline         static inline __attribute__((always_inline))
#define nxt_noinline       __attribute__((noinline))
#define nxt_cdecl


#define nxt_container_of(p, type, field)                                      \
    (type *) ((u_char *) (p) - offsetof(type, field))

#define nxt_nitems(x)                                                         \
    (sizeof(x) / sizeof((x)[0]))



#if (NXT_HAVE_BUILTIN_EXPECT)
#define nxt_fast_path(x)   __builtin_expect((long) (x), 1)
#define nxt_slow_path(x)   __builtin_expect((long) (x), 0)

#else
#define nxt_fast_path(x)   (x)
#define nxt_slow_path(x)   (x)
#endif


#if (NXT_HAVE_BUILTIN_UNREACHABLE)
#define nxt_unreachable()  __builtin_unreachable()

#else
#define nxt_unreachable()
#endif


#if (NXT_HAVE_BUILTIN_PREFETCH)
#define nxt_prefetch(a)    __builtin_prefetch(a)

#else
#define nxt_prefetch(a)
#endif


#if (NXT_HAVE_GCC_ATTRIBUTE_VISIBILITY)
#define NXT_EXPORT         __attribute__((visibility("default")))

#else
#define NXT_EXPORT
#endif


#if (NXT_HAVE_GCC_ATTRIBUTE_MALLOC)
#define NXT_MALLOC_LIKE    __attribute__((__malloc__))

#else
#define NXT_MALLOC_LIKE
#endif


#if (NXT_HAVE_GCC_ATTRIBUTE_ALIGNED)
#define nxt_aligned(x)     __attribute__((aligned(x)))

#else
#define nxt_aligned(x)
#endif


#if (NXT_CLANG)
/* Any __asm__ directive disables loop vectorization in GCC and Clang. */
#define nxt_pragma_loop_disable_vectorization  __asm__("")

#else
#define nxt_pragma_loop_disable_vectorization
#endif


#endif /* _NXT_CLANG_H_INCLUDED_ */
