
/*
 * Copyright (C) Alexander Borisov
 * Copyright (C) NGINX, Inc.
 */

#ifndef _NJS_UNICODE_H_INCLUDED_
#define _NJS_UNICODE_H_INCLUDED_


enum {
    NJS_UNICODE_BOM           = 0xFEFF,
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

#define njs_surrogate_leading(cp)                                             \
    (((unsigned) (cp) - 0xd800) <= 0xdbff - 0xd800)

#define njs_surrogate_trailing(cp)                                            \
    (((unsigned) (cp) - 0xdc00) <= 0xdfff - 0xdc00)

#define njs_surrogate_any(cp)                                                 \
    (((unsigned) (cp) - 0xd800) <= 0xdfff - 0xd800)

#define njs_surrogate_pair(high, low)                                         \
    (0x10000 + (((high) - 0xd800) << 10) + ((low) - 0xdc00))


#endif /* _NJS_UNICODE_H_INCLUDED_ */
