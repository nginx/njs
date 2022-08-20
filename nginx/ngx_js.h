
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


#define NGX_JS_UNSET        0
#define NGX_JS_DEPRECATED   1
#define NGX_JS_STRING       2
#define NGX_JS_BUFFER       4
#define NGX_JS_BOOLEAN      8
#define NGX_JS_NUMBER       16

#define ngx_js_buffer_type(btype) ((btype) & ~NGX_JS_DEPRECATED)


typedef ngx_pool_t *(*ngx_external_pool_pt)(njs_vm_t *vm, njs_external_ptr_t e);
typedef void (*ngx_js_event_handler_pt)(njs_external_ptr_t e,
    njs_vm_event_t vm_event, njs_value_t *args, njs_uint_t nargs);
typedef ngx_resolver_t *(*ngx_external_resolver_pt)(njs_vm_t *vm,
    njs_external_ptr_t e);
typedef ngx_msec_t (*ngx_external_timeout_pt)(njs_vm_t *vm,
    njs_external_ptr_t e);
typedef ngx_flag_t (*ngx_external_flag_pt)(njs_vm_t *vm,
    njs_external_ptr_t e);
typedef ngx_flag_t (*ngx_external_size_pt)(njs_vm_t *vm,
    njs_external_ptr_t e);
typedef ngx_ssl_t *(*ngx_external_ssl_pt)(njs_vm_t *vm, njs_external_ptr_t e);


#define ngx_external_connection(vm, e)                                        \
    (*((ngx_connection_t **) ((u_char *) (e) + njs_vm_meta(vm, 0))))
#define ngx_external_pool(vm, e)                                              \
    ((ngx_external_pool_pt) njs_vm_meta(vm, 1))(vm, e)
#define ngx_external_resolver(vm, e)                                          \
    ((ngx_external_resolver_pt) njs_vm_meta(vm, 2))(vm, e)
#define ngx_external_resolver_timeout(vm, e)                                  \
    ((ngx_external_timeout_pt) njs_vm_meta(vm, 3))(vm, e)
#define ngx_external_event_handler(vm, e)                                     \
    ((ngx_js_event_handler_pt) njs_vm_meta(vm, 4))
#define ngx_external_ssl(vm, e)                                               \
    ((ngx_external_ssl_pt) njs_vm_meta(vm, 5))(vm, e)
#define ngx_external_ssl_verify(vm, e)                                        \
    ((ngx_external_flag_pt) njs_vm_meta(vm, 6))(vm, e)
#define ngx_external_fetch_timeout(vm, e)                                     \
    ((ngx_external_timeout_pt) njs_vm_meta(vm, 7))(vm, e)
#define ngx_external_buffer_size(vm, e)                                       \
    ((ngx_external_size_pt) njs_vm_meta(vm, 8))(vm, e)
#define ngx_external_max_response_buffer_size(vm, e)                          \
    ((ngx_external_size_pt) njs_vm_meta(vm, 9))(vm, e)


#define ngx_js_prop(vm, type, value, start, len)                              \
    ((type == NGX_JS_STRING) ? njs_vm_value_string_set(vm, value, start, len) \
                             : njs_vm_value_buffer_set(vm, value, start, len))


ngx_int_t ngx_js_call(njs_vm_t *vm, ngx_str_t *fname, ngx_log_t *log,
    njs_opaque_value_t *args, njs_uint_t nargs);
ngx_int_t ngx_js_retval(njs_vm_t *vm, njs_opaque_value_t *retval,
    ngx_str_t *s);

njs_int_t ngx_js_ext_log(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t level);
void ngx_js_logger(njs_vm_t *vm, njs_external_ptr_t external,
    njs_log_level_t level, const u_char *start, size_t length);

njs_int_t ngx_js_ext_string(njs_vm_t *vm, njs_object_prop_t *prop,
    njs_value_t *value, njs_value_t *setval, njs_value_t *retval);
njs_int_t ngx_js_ext_uint(njs_vm_t *vm, njs_object_prop_t *prop,
    njs_value_t *value, njs_value_t *setval, njs_value_t *retval);
njs_int_t ngx_js_ext_constant(njs_vm_t *vm, njs_object_prop_t *prop,
    njs_value_t *value, njs_value_t *setval, njs_value_t *retval);
njs_int_t ngx_js_ext_flags(njs_vm_t *vm, njs_object_prop_t *prop,
    njs_value_t *value, njs_value_t *setval, njs_value_t *retval);

ngx_int_t ngx_js_core_init(njs_vm_t *vm, ngx_log_t *log);

ngx_int_t ngx_js_string(njs_vm_t *vm, njs_value_t *value, njs_str_t *str);
ngx_int_t ngx_js_integer(njs_vm_t *vm, njs_value_t *value, ngx_int_t *n);


extern njs_module_t *njs_js_addon_modules[];


#endif /* _NGX_JS_H_INCLUDED_ */
