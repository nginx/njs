
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
nxt_file_name(nxt_str_t *name, char *path)
{
    char  *p;
    size_t  length;

    length = strlen(path);

    for (p = path + length; p >= path; p--) {
        if (*p == '/') {
            p++;
            break;
        }
    }

    name->start = (u_char *) p;
    name->length = length - (p - path);
}
