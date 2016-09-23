
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) NGINX, Inc.
 */

#ifndef _NXT_REGEX_H_INCLUDED_
#define _NXT_REGEX_H_INCLUDED_


typedef void *(*nxt_pcre_malloc_t)(size_t size, void *memory_data);
typedef void (*nxt_pcre_free_t)(void *p, void *memory_data);


typedef struct nxt_regex_s             nxt_regex_t;
typedef struct nxt_regex_match_data_s  nxt_regex_match_data_t;


typedef struct {
    nxt_pcre_malloc_t  private_malloc;
    nxt_pcre_free_t    private_free;
    void               *memory_data;
    nxt_trace_t        *trace;
} nxt_regex_context_t;


NXT_EXPORT nxt_regex_context_t *
    nxt_regex_context_create(nxt_pcre_malloc_t private_malloc,
    nxt_pcre_free_t private_free, void *memory_data);
NXT_EXPORT nxt_int_t nxt_regex_compile(nxt_regex_t *regex, u_char *source,
    size_t len, nxt_uint_t options, nxt_regex_context_t *ctx);
NXT_EXPORT nxt_bool_t nxt_regex_is_valid(nxt_regex_t *regex);
NXT_EXPORT nxt_uint_t nxt_regex_ncaptures(nxt_regex_t *regex);
NXT_EXPORT nxt_regex_match_data_t *nxt_regex_match_data(nxt_regex_t *regex,
    nxt_regex_context_t *ctx);
NXT_EXPORT void nxt_regex_match_data_free(nxt_regex_match_data_t *match_data,
    nxt_regex_context_t *ctx);
NXT_EXPORT nxt_int_t nxt_regex_match(nxt_regex_t *regex, u_char *subject,
    size_t len, nxt_regex_match_data_t *match_data, nxt_regex_context_t *ctx);
NXT_EXPORT int *nxt_regex_captures(nxt_regex_match_data_t *match_data);


#endif /* _NXT_REGEX_H_INCLUDED_ */
