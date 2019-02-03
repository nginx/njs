
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) NGINX, Inc.
 */

#ifndef _NXT_TYPES_H_INCLUDED_
#define _NXT_TYPES_H_INCLUDED_


/*
 * off_t is 32 bit on Linux, Solaris and HP-UX by default.
 * Must be before <sys/types.h>.
 */
#define _FILE_OFFSET_BITS  64

/* u_char, u_int, int8_t, int32_t, int64_t, size_t, off_t. */
#include <sys/types.h>
#include <inttypes.h>


#if (__LP64__)
#define NXT_64BIT       1
#define NXT_PTR_SIZE    8
#else
#define NXT_64BIT       0
#define NXT_PTR_SIZE    4
#endif


/*
 * nxt_int_t corresponds to the most efficient integer type, an architecture
 * word.  It is usually the long type, however on Win64 the long is int32_t,
 * so pointer size suits better.  nxt_int_t must be no less than int32_t.
 */

#if (__amd64__)
/*
 * AMD64 64-bit multiplication and division operations are slower and 64-bit
 * instructions are longer.
 */
#define NXT_INT_T_SIZE  4
typedef int             nxt_int_t;
typedef u_int           nxt_uint_t;

#else
#define NXT_INT_T_SIZE  NXT_PTR_SIZE
typedef intptr_t        nxt_int_t;
typedef uintptr_t       nxt_uint_t;
#endif


#if (NXT_HAVE_UNSIGNED_INT128)
typedef unsigned __int128 nxt_uint128_t;
#endif


#if (NXT_INT_T_SIZE == 8)
#define NXT_INT_T_LEN        NXT_INT64_T_LEN
#define NXT_INT_T_HEXLEN     NXT_INT64_T_HEXLEN
#define NXT_INT_T_MAX        NXT_INT64_T_MAX

#else
#define NXT_INT_T_LEN        NXT_INT32_T_LEN
#define NXT_INT_T_HEXLEN     NXT_INT32_T_HEXLEN
#define NXT_INT_T_MAX        NXT_INT32_T_MAX
#endif


typedef nxt_uint_t      nxt_bool_t;
typedef int             nxt_err_t;


/*
 * nxt_off_t corresponds to OS's off_t, a file offset type.  Although Linux,
 * Solaris, and HP-UX define both off_t and off64_t, setting _FILE_OFFSET_BITS
 * to 64 defines off_t as off64_t.
 */
#if (NXT_WINDOWS)
/* Windows defines off_t as a 32-bit "long". */
typedef __int64        nxt_off_t;

#else
typedef off_t          nxt_off_t;
#endif


/*
 * nxt_time_t corresponds to OS's time_t, time in seconds.  nxt_time_t is
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
#if (NXT_QNX)
/* Y2038 fix: "typedef int64_t  nxt_time_t". */
typedef int32_t        nxt_time_t;

#else
/* Y2038, if time_t is 32-bit integer. */
typedef time_t         nxt_time_t;
#endif


typedef pid_t          nxt_pid_t;


#define NXT_INT32_T_LEN      nxt_length("-2147483648")
#define NXT_INT64_T_LEN      nxt_length("-9223372036854775808")

#define NXT_DOUBLE_LEN       (1 + DBL_MAX_10_EXP)

#define NXT_MAX_ERROR_STR    2048


#endif /* _NXT_TYPES_H_INCLUDED_ */
