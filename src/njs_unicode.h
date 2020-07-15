
/*
 * Copyright (C) Alexander Borisov
 * Copyright (C) NGINX, Inc.
 */

#ifndef _NJS_UNICODE_H_INCLUDED_
#define _NJS_UNICODE_H_INCLUDED_


enum {
    NJS_UNICODE_REPLACEMENT   = 0xFFFD,
    NJS_UNICODE_MAX_CODEPOINT = 0x10FFFF,
    NJS_UNICODE_ERROR         = 0x1FFFFF,
    NJS_UNICODE_CONTINUE      = 0x2FFFFF
};

typedef struct {
    uint32_t  codepoint;

    unsigned  need;
    u_char    lower;
    u_char    upper;
} njs_unicode_decode_t;


#endif /* _NJS_UNICODE_H_INCLUDED_ */
