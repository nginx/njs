
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) NGINX, Inc.
 */

#ifndef _NJS_REGEXP_PATTERN_H_INCLUDED_
#define _NJS_REGEXP_PATTERN_H_INCLUDED_

#include <nxt_pcre.h>
#include <nxt_regex.h>


typedef enum {
    NJS_REGEXP_BYTE = 0,
    NJS_REGEXP_UTF8,
} njs_regexp_utf8_t;


struct njs_regexp_pattern_s {
    nxt_regex_t           regex[2];

    /*
     * A pattern source is used by RegExp.toString() method and
     * RegExp.source property.  So it is is stored in form "/pattern/flags"
     * and as zero-terminated C string but not as value, because retrieving
     * it is very seldom operation.  To get just a pattern string for
     * RegExp.source property a length of flags part "/flags" is stored
     * in flags field.
     */
    u_char                *source;

#if (NXT_64BIT)
    uint32_t              ncaptures;
    uint8_t               flags;        /* 2 bits */

    uint8_t               global;       /* 1 bit */
    uint8_t               ignore_case;  /* 1 bit */
    uint8_t               multiline;    /* 1 bit */
#else
    uint16_t              ncaptures;
    uint8_t               flags;        /* 2 bits */
    uint8_t               global:1;
    uint8_t               ignore_case:1;
    uint8_t               multiline:1;
#endif
};


#endif /* _NJS_REGEXP_PATTERN_H_INCLUDED_ */
