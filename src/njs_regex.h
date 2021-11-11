
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) NGINX, Inc.
 */

#ifndef _NJS_REGEX_H_INCLUDED_
#define _NJS_REGEX_H_INCLUDED_

#define NJS_REGEX_UNSET      (size_t) (-1)


typedef enum {
    NJS_REGEX_INVALID_FLAG = -1,
    NJS_REGEX_NO_FLAGS     =  0,
    NJS_REGEX_GLOBAL       =  1,
    NJS_REGEX_IGNORE_CASE  =  2,
    NJS_REGEX_MULTILINE    =  4,
    NJS_REGEX_STICKY       =  8,
    NJS_REGEX_UTF8         = 16,
} njs_regex_flags_t;


typedef void *(*njs_pcre_malloc_t)(size_t size, void *memory_data);
typedef void (*njs_pcre_free_t)(void *p, void *memory_data);


typedef struct {
    void        *code;
    void        *extra;
    int         ncaptures;
    int         backrefmax;
    int         nentries;
    int         entry_size;
    char        *entries;
} njs_regex_t;


#ifdef NJS_HAVE_PCRE2

#define njs_regex_generic_ctx_t  void
#define njs_regex_compile_ctx_t  void
#define njs_regex_match_data_t   void

#else

typedef struct {
    njs_pcre_malloc_t  private_malloc;
    njs_pcre_free_t    private_free;
    void               *memory_data;
} njs_regex_generic_ctx_t;

#define njs_regex_compile_ctx_t  void

typedef struct {
    int         ncaptures;
    /*
     * Each capture is stored in 3 "int" vector elements.
     * The N capture positions are stored in [n * 2] and [n * 2 + 1] elements.
     * The 3rd bookkeeping elements are at the end of the vector.
     * The first vector is for the "$0" capture and it is always allocated.
     */
    int         captures[3];
} njs_regex_match_data_t;

#endif


NJS_EXPORT njs_regex_generic_ctx_t *
    njs_regex_generic_ctx_create(njs_pcre_malloc_t private_malloc,
    njs_pcre_free_t private_free, void *memory_data);
NJS_EXPORT njs_regex_compile_ctx_t *njs_regex_compile_ctx_create(
    njs_regex_generic_ctx_t *ctx);
NJS_EXPORT njs_int_t njs_regex_escape(njs_mp_t *mp, njs_str_t *text);
NJS_EXPORT njs_int_t njs_regex_compile(njs_regex_t *regex, u_char *source,
    size_t len, njs_regex_flags_t flags, njs_regex_compile_ctx_t *ctx,
    njs_trace_t *trace);
NJS_EXPORT njs_bool_t njs_regex_is_valid(njs_regex_t *regex);
NJS_EXPORT njs_int_t njs_regex_named_captures(njs_regex_t *regex,
    njs_str_t *name, int n);
NJS_EXPORT njs_regex_match_data_t *njs_regex_match_data(njs_regex_t *regex,
    njs_regex_generic_ctx_t *ctx);
NJS_EXPORT void njs_regex_match_data_free(njs_regex_match_data_t *match_data,
    njs_regex_generic_ctx_t *ctx);
NJS_EXPORT njs_int_t njs_regex_match(njs_regex_t *regex, const u_char *subject,
    size_t off, size_t len, njs_regex_match_data_t *match_data,
    njs_trace_t *trace);
NJS_EXPORT size_t njs_regex_capture(njs_regex_match_data_t *match_data,
    njs_uint_t n);


#endif /* _NJS_REGEX_H_INCLUDED_ */
