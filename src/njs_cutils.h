/*
 * Selected C utilities adapted from QuickJS cutils.h
 *
 * Copyright (c) 2024 Fabrice Bellard
 */

#ifndef _NJS_CUTILS_H_INCLUDED_
#define _NJS_CUTILS_H_INCLUDED_

#include <stddef.h>
#include <stdint.h>

#ifndef likely
#define likely(x)       __builtin_expect(!!(x), 1)
#endif

#ifndef unlikely
#define unlikely(x)     __builtin_expect(!!(x), 0)
#endif

#ifndef no_inline
#define no_inline __attribute__((noinline))
#endif

/* compatibility attribute used in dtoa implementation */
#ifndef __maybe_unused
#define __maybe_unused __attribute__((unused))
#endif

typedef int BOOL;

#ifndef FALSE
#define FALSE 0
#endif

#ifndef TRUE
#define TRUE 1
#endif

static inline int
max_int(int a, int b)
{
    return (a > b) ? a : b;
}

static inline int
min_int(int a, int b)
{
    return (a < b) ? a : b;
}

static inline uint32_t
max_uint32(uint32_t a, uint32_t b)
{
    return (a > b) ? a : b;
}

static inline uint32_t
min_uint32(uint32_t a, uint32_t b)
{
    return (a < b) ? a : b;
}

static inline int64_t
max_int64(int64_t a, int64_t b)
{
    return (a > b) ? a : b;
}

static inline int64_t
min_int64(int64_t a, int64_t b)
{
    return (a < b) ? a : b;
}

/* WARNING: undefined if a = 0 */
static inline int
clz32(unsigned int a)
{
    return __builtin_clz(a);
}

/* WARNING: undefined if a = 0 */
static inline int
clz64(uint64_t a)
{
    return __builtin_clzll(a);
}

/* WARNING: undefined if a = 0 */
static inline int
ctz32(unsigned int a)
{
    return __builtin_ctz(a);
}

/* WARNING: undefined if a = 0 */
static inline int
ctz64(uint64_t a)
{
    return __builtin_ctzll(a);
}

static inline uint64_t
float64_as_uint64(double d)
{
    union {
        double   d;
        uint64_t u64;
    } u;

    u.d = d;
    return u.u64;
}

static inline double
uint64_as_float64(uint64_t u64)
{
    union {
        double   d;
        uint64_t u64;
    } u;

    u.u64 = u64;
    return u.d;
}

static inline int
strstart(const char *str, const char *val, const char **ptr)
{
    const char *p = str;
    const char *q = val;

    while (*q != '\0') {
        if (*p != *q) {
            return 0;
        }
        p++;
        q++;
    }

    if (ptr != NULL) {
        *ptr = p;
    }

    return 1;
}

#endif /* _NJS_CUTILS_H_INCLUDED_ */
