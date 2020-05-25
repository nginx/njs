
/*
 * Copyright (C) Dmitry Volyntsev
 * Copyright (C) NGINX, Inc.
 */

#ifndef _NJS_UTILS_H_INCLUDED_
#define _NJS_UTILS_H_INCLUDED_


typedef int (*njs_sort_cmp_t)(const void *, const void *, void *ctx);

void njs_qsort(void *base, size_t n, size_t size, njs_sort_cmp_t cmp,
    void *ctx);

#endif /* _NJS_UTILS_H_INCLUDED_ */
