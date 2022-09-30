
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
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <float.h>
#include <limits.h>
#include <time.h>
#include <fcntl.h>

/*
 * alloca() is defined in stdlib.h in Linux, FreeBSD and MacOSX
 * and in alloca.h in Linux, Solaris and MacOSX.
 */
#if (NJS_SOLARIS)
#include <alloca.h>
#endif

#include <sys/time.h>
#include <sys/stat.h>
#include <sys/param.h>

#include <unistd.h>

extern char  **environ;

#if defined(PATH_MAX)
#define NJS_MAX_PATH             PATH_MAX
#else
#define NJS_MAX_PATH             4096
#endif

#endif /* _NJS_UNIX_H_INCLUDED_ */
