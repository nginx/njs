
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) NGINX, Inc.
 */

#include <nxt_auto_config.h>
#include <nxt_types.h>
#include <nxt_clang.h>
#include <nxt_string.h>
#include <nxt_file.h>

#include <string.h>


void
nxt_file_basename(const nxt_str_t *path, nxt_str_t *name)
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
nxt_file_dirname(const nxt_str_t *path, nxt_str_t *name)
{
    const u_char  *p, *end;

    if (path->length == 0) {
        *name = nxt_string_value("");
        return;
    }

    p = path->start + path->length - 1;

    /* Stripping basename. */

    while (p >= path->start && *p != '/') { p--; }

    end = p + 1;

    if (end == path->start) {
        *name = nxt_string_value("");
        return;
    }

    /* Stripping trailing slashes. */

    while (p >= path->start && *p == '/') { p--; }

    p++;

    if (p == path->start) {
        p = end;
    }

    name->start = path->start;
    name->length = p - path->start;
}
