
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) NGINX, Inc.
 */

#ifndef _NJS_REGEXP_PATTERN_H_INCLUDED_
#define _NJS_REGEXP_PATTERN_H_INCLUDED_


typedef enum {
    NJS_REGEXP_BYTE = 0,
    NJS_REGEXP_UTF8,
} njs_regexp_utf8_t;


typedef struct njs_regexp_group_s  njs_regexp_group_t;


struct njs_regexp_pattern_s {
    njs_regex_t           regex[2];

    /*
     * A pattern source is used by RegExp.prototype.toString() method and
     * RegExp.prototype.source and RegExp.prototype.flags accessor properties.
     * So it is is stored in form "/pattern/flags"
     * and as zero-terminated C string but not as value, because retrieving
     * it is very seldom operation.  To get just a pattern string for
     * RegExp.source property a length of flags part "/flags" is stored
     * in flags field.
     */
    u_char                *source;

    uint16_t              ncaptures;
    uint16_t              ngroups;

    uint8_t               flags;        /* 2 bits */
    uint8_t               global;       /* 1 bit */
    uint8_t               ignore_case;  /* 1 bit */
    uint8_t               multiline;    /* 1 bit */

    njs_regexp_group_t    *groups;
};


#endif /* _NJS_REGEXP_PATTERN_H_INCLUDED_ */
