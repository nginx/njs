
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) NGINX, Inc.
 */

#ifndef _NJS_TYPES_H_INCLUDED_
#define _NJS_TYPES_H_INCLUDED_


#define NJS_OK             0
#define NJS_ERROR          (-1)
#define NJS_AGAIN          (-2)
#define NJS_DECLINED       (-3)
#define NJS_DONE           (-4)


/*
 * off_t is 32 bit on Linux, Solaris and HP-UX by default.
 * Must be before <sys/types.h>.
 */
#define _FILE_OFFSET_BITS  64

/* u_char, u_int, int8_t, int32_t, int64_t, size_t, off_t. */
#include <sys/types.h>
#include <stdint.h>


#if (__LP64__)
#define NJS_64BIT       1
#define NJS_PTR_SIZE    8
#else
#define NJS_64BIT       0
#define NJS_PTR_SIZE    4
#endif


/*
 * njs_int_t corresponds to the most efficient integer type, an architecture
 * word.  It is usually the long type, however on Win64 the long is int32_t,
 * so pointer size suits better.  njs_int_t must be no less than int32_t.
 */

#if (__amd64__)
/*
 * AMD64 64-bit multiplication and division operations are slower and 64-bit
 * instructions are longer.
 */
#define NJS_INT_T_SIZE  4
typedef int             njs_int_t;
typedef u_int           njs_uint_t;

#else
#define NJS_INT_T_SIZE  NJS_PTR_SIZE
typedef intptr_t        njs_int_t;
typedef uintptr_t       njs_uint_t;
#endif


#if (NJS_HAVE_UNSIGNED_INT128)
typedef unsigned __int128 njs_uint128_t;
#endif


#if (NJS_INT_T_SIZE == 8)
#define NJS_INT_T_LEN        NJS_INT64_T_LEN
#define NJS_INT_T_HEXLEN     NJS_INT64_T_HEXLEN
#define NJS_INT_T_MAX        NJS_INT64_T_MAX

#else
#define NJS_INT_T_LEN        NJS_INT32_T_LEN
#define NJS_INT_T_HEXLEN     NJS_INT32_T_HEXLEN
#define NJS_INT_T_MAX        NJS_INT32_T_MAX
#endif


typedef njs_uint_t      njs_bool_t;
typedef int             njs_err_t;


/*
 * njs_off_t corresponds to OS's off_t, a file offset type.  Although Linux,
 * Solaris, and HP-UX define both off_t and off64_t, setting _FILE_OFFSET_BITS
 * to 64 defines off_t as off64_t.
 */
#if (NJS_WINDOWS)
/* Windows defines off_t as a 32-bit "long". */
typedef __int64        njs_off_t;

#else
typedef off_t          njs_off_t;
#endif


/*
 * njs_time_t corresponds to OS's time_t, time in seconds.  njs_time_t is
 * a signed integer.  OS's time_t may be an integer or real-floating type,
 * though it is usually a signed 32-bit or 64-bit integer depending on
 * platform bits length.  There are however exceptions, e.g., time_t is:
 *   32-bit on 64-bit NetBSD prior to 6.0 version;
 *   64-bit on 32-bit NetBSD 6.0;
 *   32-bit on 64-bit OpenBSD;
 *   64-bit in Linux x32 ABI;
 *   64-bit in 32-bit Visual Studio C++ 2005.
 *
 * Besides, QNX defines time_t as uint32_t.
 */
#if (NJS_QNX)
/* Y2038 fix: "typedef int64_t  njs_time_t". */
typedef int32_t        njs_time_t;

#else
/* Y2038, if time_t is 32-bit integer. */
typedef time_t         njs_time_t;
#endif


typedef pid_t          njs_pid_t;


#define NJS_INT32_T_LEN      njs_length("-2147483648")
#define NJS_INT64_T_LEN      njs_length("-9223372036854775808")

#define NJS_DOUBLE_LEN       (1 + DBL_MAX_10_EXP)

#define NJS_MAX_ERROR_STR    2048


#endif /* _NJS_TYPES_H_INCLUDED_ */
