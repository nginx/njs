
/*
 * Copyright (C) Roman Arutyunyan
 * Copyright (C) Dmitry Volyntsev
 * Copyright (C) NGINX, Inc.
 */


#ifndef _NGX_JS_H_INCLUDED_
#define _NGX_JS_H_INCLUDED_


#include <ngx_config.h>
#include <ngx_core.h>
#include <njs.h>


ngx_int_t ngx_js_call(njs_vm_t *vm, ngx_str_t *s, njs_opaque_value_t *value,
    ngx_log_t *log);

njs_int_t ngx_js_ext_string(njs_vm_t *vm, njs_object_prop_t *prop,
    njs_value_t *value, njs_value_t *setval, njs_value_t *retval);

ngx_int_t ngx_js_string(njs_vm_t *vm, njs_value_t *value, njs_str_t *str);
ngx_int_t ngx_js_integer(njs_vm_t *vm, njs_value_t *value, ngx_int_t *n);


#endif /* _NGX_JS_H_INCLUDED_ */
