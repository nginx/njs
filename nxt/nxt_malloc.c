
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) NGINX, Inc.
 */

#include <nxt_auto_config.h>
#include <nxt_types.h>
#include <nxt_clang.h>
#include <nxt_malloc.h>


#if (NXT_HAVE_POSIX_MEMALIGN)

/*
 * posix_memalign() presents in Linux glibc 2.1.91, FreeBSD 7.0,
 * Solaris 11, MacOSX 10.6 (Snow Leopard), NetBSD 5.0, OpenBSD 4.8.
 */

void *
nxt_memalign(size_t alignment, size_t size)
{
    int   err;
    void  *p;

    err = posix_memalign(&p, alignment, size);

    if (nxt_fast_path(err == 0)) {
        return p;
    }

    // STUB
    //nxt_errno_set(err);

    return NULL;
}

#elif (NXT_HAVE_MEMALIGN)

/* memalign() presents in Solaris, HP-UX. */

void *
nxt_memalign(size_t alignment, size_t size)
{
    return memalign(alignment, size);
}

#else

#error no memalign() implementation.

#endif
