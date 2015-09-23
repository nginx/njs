
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) NGINX, Inc.
 */

#ifndef _NJS_REGEXP_PATTERN_H_INCLUDED_
#define _NJS_REGEXP_PATTERN_H_INCLUDED_

#include <pcre.h>


struct njs_regexp_pattern_s {
    pcre                  *code[2];
    pcre_extra            *extra[2];
    u_char                *source;

#if (NXT_64BIT)
    uint32_t              ncaptures;
    uint8_t               global;       /* 1 bit */
    uint8_t               ignore_case;  /* 1 bit */
    uint8_t               multiline;    /* 1 bit */
#else
    uint16_t              ncaptures;
    uint8_t               global;       /* 1 bit */
    uint8_t               ignore_case:1;
    uint8_t               multiline:1;
#endif
};


#endif /* _NJS_REGEXP_PATTERN_H_INCLUDED_ */
