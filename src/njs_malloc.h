
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) NGINX, Inc.
 */

#ifndef _NJS_MALLOC_H_INCLUDED_
#define _NJS_MALLOC_H_INCLUDED_

#include <stdlib.h>

/*
 * alloca() is defined in stdlib.h in Linux, FreeBSD and MacOSX
 * and in alloca.h in Linux, Solaris and MacOSX.
 */
#if (NJS_SOLARIS)
#include <alloca.h>
#endif


#define njs_malloc(size)   malloc(size)
#define njs_free(p)        free(p)


NJS_EXPORT void *njs_memalign(size_t alignment, size_t size);


#endif /* _NJS_MALLOC_H_INCLUDED_ */
