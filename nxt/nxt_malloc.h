
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) NGINX, Inc.
 */

#ifndef _NXT_MALLOC_H_INCLUDED_
#define _NXT_MALLOC_H_INCLUDED_

#include <stdlib.h>

/*
 * alloca() is defined in stdlib.h in Linux, FreeBSD and MacOSX
 * and in alloca.h in Linux, Solaris and MacOSX.
 */
#if (NXT_SOLARIS)
#include <alloca.h>
#endif


#define nxt_malloc(size)   malloc(size)
#define nxt_free(p)        free(p)


NXT_EXPORT void *nxt_memalign(size_t alignment, size_t size);


#endif /* _NXT_MALLOC_H_INCLUDED_ */
