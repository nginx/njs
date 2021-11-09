
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) NGINX, Inc.
 */

#ifndef _NJS_REGEX_H_INCLUDED_
#define _NJS_REGEX_H_INCLUDED_


typedef void *(*njs_pcre_malloc_t)(size_t size, void *memory_data);
typedef void (*njs_pcre_free_t)(void *p, void *memory_data);


typedef struct njs_regex_s             njs_regex_t;
typedef struct njs_regex_match_data_s  njs_regex_match_data_t;


typedef struct {
    njs_pcre_malloc_t  private_malloc;
    njs_pcre_free_t    private_free;
    void               *memory_data;
    njs_trace_t        *trace;
} njs_regex_context_t;


NJS_EXPORT njs_regex_context_t *
    njs_regex_context_create(njs_pcre_malloc_t private_malloc,
    njs_pcre_free_t private_free, void *memory_data);
NJS_EXPORT njs_int_t njs_regex_compile(njs_regex_t *regex, u_char *source,
    size_t len, njs_uint_t options, njs_regex_context_t *ctx);
NJS_EXPORT njs_bool_t njs_regex_is_valid(njs_regex_t *regex);
NJS_EXPORT njs_int_t njs_regex_named_captures(njs_regex_t *regex,
    njs_str_t *name, int n);
NJS_EXPORT njs_regex_match_data_t *njs_regex_match_data(njs_regex_t *regex,
    njs_regex_context_t *ctx);
NJS_EXPORT void njs_regex_match_data_free(njs_regex_match_data_t *match_data,
    njs_regex_context_t *ctx);
NJS_EXPORT njs_int_t njs_regex_match(njs_regex_t *regex, const u_char *subject,
    size_t off, size_t len, njs_regex_match_data_t *match_data,
    njs_regex_context_t *ctx);
NJS_EXPORT int *njs_regex_captures(njs_regex_match_data_t *match_data);


#endif /* _NJS_REGEX_H_INCLUDED_ */
