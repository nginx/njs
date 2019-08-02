
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) NGINX, Inc.
 */


#include <njs_main.h>


void *
njs_zalloc(size_t size)
{
    void  *p;

    p = njs_malloc(size);

    if (njs_fast_path(p != NULL)) {
        njs_memzero(p, size);
    }

    return p;
}


#if (NJS_HAVE_POSIX_MEMALIGN)

/*
 * posix_memalign() presents in Linux glibc 2.1.91, FreeBSD 7.0,
 * Solaris 11, MacOSX 10.6 (Snow Leopard), NetBSD 5.0, OpenBSD 4.8.
 */

void *
njs_memalign(size_t alignment, size_t size)
{
    int   err;
    void  *p;

    err = posix_memalign(&p, alignment, size);

    if (njs_fast_path(err == 0)) {
        return p;
    }

    return NULL;
}

#elif (NJS_HAVE_MEMALIGN)

/* memalign() presents in Solaris, HP-UX. */

void *
njs_memalign(size_t alignment, size_t size)
{
    return memalign(alignment, size);
}

#else

#error no memalign() implementation.

#endif
