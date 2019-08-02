
/*
 * Copyright (C) Dmitry Volyntsev
 * Copyright (C) NGINX, Inc.
 */


#ifndef _NJS_UNIX_H_INCLUDED_
#define _NJS_UNIX_H_INCLUDED_

#define njs_pagesize()      getpagesize()

#if (NJS_LINUX)

#ifdef _FORTIFY_SOURCE
/*
 * _FORTIFY_SOURCE
 *     does not allow to use "(void) write()";
 */
#undef _FORTIFY_SOURCE
#endif

#endif /* NJS_LINUX */

#include <errno.h>
#include <stdarg.h>
#include <stddef.h>                 /* offsetof() */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <math.h>
#include <float.h>
#include <time.h>
#include <fcntl.h>

#include <sys/time.h>
#include <sys/stat.h>
#include <sys/param.h>

#include <unistd.h>

#endif /* _NJS_UNIX_H_INCLUDED_ */
