
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) NGINX, Inc.
 */

#ifndef _NJS_PCRE_H_INCLUDED_
#define _NJS_PCRE_H_INCLUDED_


#include <pcre.h>


#define NJS_REGEX_NOMATCH  PCRE_ERROR_NOMATCH


struct njs_regex_s {
    pcre        *code;
    pcre_extra  *extra;
    int         ncaptures;
    int         backrefmax;
    int         nentries;
    int         entry_size;
    char        *entries;
};


struct njs_regex_match_data_s {
    int         ncaptures;
    /*
     * Each capture is stored in 3 "int" vector elements.
     * The N capture positions are stored in [n * 2] and [n * 2 + 1] elements.
     * The 3rd bookkeeping elements are at the end of the vector.
     * The first vector is for the "$0" capture and it is always allocated.
     */
    int         captures[3];
};


#endif /* _NJS_PCRE_H_INCLUDED_ */
