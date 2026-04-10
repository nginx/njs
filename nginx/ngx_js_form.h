/*
 * Copyright (C) Dmitry Volyntsev
 * Copyright (C) F5, Inc.
 */


#ifndef _NGX_JS_FORM_H_INCLUDED_
#define _NGX_JS_FORM_H_INCLUDED_


#include <ngx_config.h>
#include <ngx_core.h>

#define NGX_JS_FORM_DEFAULT_MAX_KEYS  128

#define NGX_JS_FORM_OK          NGX_OK
#define NGX_JS_FORM_TYPE_ERROR  NGX_DECLINED
#define NGX_JS_FORM_PARSE_ERROR NGX_DONE


typedef struct {
    ngx_str_t  name;
    ngx_str_t  value;
    ngx_str_t  filename;
    unsigned   is_file:1;
} ngx_js_form_entry_t;


typedef struct {
    ngx_array_t  entries;
    unsigned     has_files:1;
} ngx_js_form_t;


ngx_int_t ngx_js_parse_form(ngx_pool_t *pool, ngx_str_t *content_type,
    u_char *body, size_t len, ngx_uint_t max_keys, ngx_js_form_t **form,
    ngx_str_t *error);


#endif /* _NGX_JS_FORM_H_INCLUDED_ */
