
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) NGINX, Inc.
 */


#include <njs_main.h>


void
njs_file_basename(const njs_str_t *path, njs_str_t *name)
{
    const u_char  *p, *end;

    end = path->start + path->length;
    p = end - 1;

    /* Stripping dir prefix. */

    while (p >= path->start && *p != '/') { p--; }

    p++;

    name->start = (u_char *) p;
    name->length = end - p;
}


void
njs_file_dirname(const njs_str_t *path, njs_str_t *name)
{
    const u_char  *p, *end;

    if (path->length == 0) {
        goto current_dir;
    }

    p = path->start + path->length - 1;

    /* Stripping basename. */

    while (p >= path->start && *p != '/') { p--; }

    end = p + 1;

    if (end == path->start) {
        goto current_dir;
    }

    /* Stripping trailing slashes. */

    while (p >= path->start && *p == '/') { p--; }

    p++;

    if (p == path->start) {
        p = end;
    }

    name->start = path->start;
    name->length = p - path->start;

    return;

current_dir:

    *name = njs_str_value(".");
}
