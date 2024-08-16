
/*
 * Copyright (C) Roman Arutyunyan
 * Copyright (C) Dmitry Volyntsev
 * Copyright (C) NGINX, Inc.
 */


#ifndef _NGX_JS_H_INCLUDED_
#define _NGX_JS_H_INCLUDED_


#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_event.h>
#include <njs.h>
#include <njs_rbtree.h>
#include <njs_arr.h>
#include "ngx_js_fetch.h"
#include "ngx_js_shared_dict.h"


#define NGX_JS_UNSET        0
#define NGX_JS_DEPRECATED   1
#define NGX_JS_STRING       2
#define NGX_JS_BUFFER       4
#define NGX_JS_BOOLEAN      8
#define NGX_JS_NUMBER       16

#define NGX_JS_BOOL_FALSE   0
#define NGX_JS_BOOL_TRUE    1
#define NGX_JS_BOOL_UNSET   2

#define ngx_js_buffer_type(btype) ((btype) & ~NGX_JS_DEPRECATED)


typedef struct ngx_js_event_s ngx_js_event_t;
typedef struct ngx_js_dict_s  ngx_js_dict_t;
typedef struct ngx_js_ctx_s  ngx_js_ctx_t;


typedef ngx_pool_t *(*ngx_external_pool_pt)(njs_external_ptr_t e);
typedef void (*ngx_js_event_finalize_pt)(njs_external_ptr_t e, ngx_int_t rc);
typedef ngx_resolver_t *(*ngx_external_resolver_pt)(njs_external_ptr_t e);
typedef ngx_msec_t (*ngx_external_timeout_pt)(njs_external_ptr_t e);
typedef ngx_flag_t (*ngx_external_flag_pt)(njs_external_ptr_t e);
typedef ngx_flag_t (*ngx_external_size_pt)(njs_external_ptr_t e);
typedef ngx_ssl_t *(*ngx_external_ssl_pt)(njs_external_ptr_t e);
typedef ngx_js_ctx_t *(*ngx_js_external_ctx_pt)(njs_external_ptr_t e);


typedef struct {
    ngx_str_t              name;
    ngx_str_t              path;
    u_char                *file;
    ngx_uint_t             line;
} ngx_js_named_path_t;


struct ngx_js_event_s {
    njs_vm_t            *vm;
    njs_function_t      *function;
    njs_value_t         *args;
    ngx_socket_t         fd;
    NJS_RBTREE_NODE     (node);
    njs_uint_t           nargs;
    void               (*destructor)(njs_external_ptr_t external,
                                     ngx_js_event_t *event);
    ngx_event_t          ev;
    void                *data;
};


#define NGX_JS_COMMON_MAIN_CONF                                               \
    ngx_js_dict_t         *dicts;                                             \
    ngx_array_t           *periodics                                          \


#define _NGX_JS_COMMON_LOC_CONF                                               \
    njs_vm_t              *vm;                                                \
    ngx_str_t              cwd;                                               \
    ngx_array_t           *imports;                                           \
    ngx_array_t           *paths;                                             \
                                                                              \
    njs_vm_t              *preload_vm;                                        \
    ngx_array_t           *preload_objects;                                   \
                                                                              \
    size_t                 buffer_size;                                       \
    size_t                 max_response_body_size;                            \
    ngx_msec_t             timeout


#if defined(NGX_HTTP_SSL) || defined(NGX_STREAM_SSL)
#define NGX_JS_COMMON_LOC_CONF                                                \
    _NGX_JS_COMMON_LOC_CONF;                                                  \
                                                                              \
    ngx_ssl_t             *ssl;                                               \
    ngx_str_t              ssl_ciphers;                                       \
    ngx_uint_t             ssl_protocols;                                     \
    ngx_flag_t             ssl_verify;                                        \
    ngx_int_t              ssl_verify_depth;                                  \
    ngx_str_t              ssl_trusted_certificate

#else
#define NGX_JS_COMMON_LOC_CONF _NGX_JS_COMMON_LOC_CONF
#endif


#define NGX_JS_COMMON_CTX                                                     \
    njs_vm_t              *vm;                                                \
    njs_arr_t             *rejected_promises;                                 \
    njs_rbtree_t           waiting_events;                                    \
    ngx_socket_t           event_id


#define ngx_js_add_event(ctx, event)                                          \
    njs_rbtree_insert(&(ctx)->waiting_events, &(event)->node)


#define ngx_js_del_event(ctx, event)                                          \
    do {                                                                      \
        if ((event)->destructor) {                                            \
            (event)->destructor(njs_vm_external_ptr((event)->vm), event);     \
        }                                                                     \
                                                                              \
        njs_rbtree_delete(&(ctx)->waiting_events, &(event)->node);            \
    } while (0)


typedef struct {
    NGX_JS_COMMON_MAIN_CONF;
} ngx_js_main_conf_t;


typedef struct {
    NGX_JS_COMMON_LOC_CONF;
} ngx_js_loc_conf_t;


struct ngx_js_ctx_s {
    NGX_JS_COMMON_CTX;
};


#define ngx_external_connection(vm, e)                                        \
    (*((ngx_connection_t **) ((u_char *) (e) + njs_vm_meta(vm, 0))))
#define ngx_external_pool(vm, e)                                              \
    ((ngx_external_pool_pt) njs_vm_meta(vm, 1))(e)
#define ngx_external_resolver(vm, e)                                          \
    ((ngx_external_resolver_pt) njs_vm_meta(vm, 2))(e)
#define ngx_external_resolver_timeout(vm, e)                                  \
    ((ngx_external_timeout_pt) njs_vm_meta(vm, 3))(e)
#define ngx_external_event_finalize(vm) \
    ((ngx_js_event_finalize_pt) njs_vm_meta(vm, 4))
#define ngx_external_ssl(vm, e)                                               \
    ((ngx_external_ssl_pt) njs_vm_meta(vm, 5))(e)
#define ngx_external_ssl_verify(vm, e)                                        \
    ((ngx_external_flag_pt) njs_vm_meta(vm, 6))(e)
#define ngx_external_fetch_timeout(vm, e)                                     \
    ((ngx_external_timeout_pt) njs_vm_meta(vm, 7))(e)
#define ngx_external_buffer_size(vm, e)                                       \
    ((ngx_external_size_pt) njs_vm_meta(vm, 8))(e)
#define ngx_external_max_response_buffer_size(vm, e)                          \
    ((ngx_external_size_pt) njs_vm_meta(vm, 9))(e)
#define NGX_JS_MAIN_CONF_INDEX  10
#define ngx_main_conf(vm)                                                     \
	((ngx_js_main_conf_t *) njs_vm_meta(vm, NGX_JS_MAIN_CONF_INDEX))
#define ngx_external_ctx(vm, e) \
    ((ngx_js_external_ctx_pt) njs_vm_meta(vm, 11))(e)


#define ngx_js_prop(vm, type, value, start, len)                              \
    ((type == NGX_JS_STRING) ? njs_vm_value_string_create(vm, value, start, len) \
                             : njs_vm_value_buffer_set(vm, value, start, len))


#define ngx_vm_pending(ctx)                                                   \
    (njs_vm_pending((ctx)->vm) || !njs_rbtree_is_empty(&(ctx)->waiting_events))


void ngx_js_ctx_init(ngx_js_ctx_t *ctx);
void ngx_js_ctx_destroy(ngx_js_ctx_t *ctx);
ngx_int_t ngx_js_call(njs_vm_t *vm, njs_function_t *func, njs_value_t *args,
    njs_uint_t nargs);
ngx_int_t ngx_js_name_call(njs_vm_t *vm, ngx_str_t *fname, ngx_log_t *log,
    njs_opaque_value_t *args, njs_uint_t nargs);
ngx_int_t ngx_js_name_invoke(njs_vm_t *vm, ngx_str_t *fname, ngx_log_t *log,
    njs_opaque_value_t *args, njs_uint_t nargs, njs_opaque_value_t *retval);
ngx_int_t ngx_js_exception(njs_vm_t *vm, ngx_str_t *s);

njs_int_t ngx_js_ext_log(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t level, njs_value_t *retval);
void ngx_js_log(njs_vm_t *vm, njs_external_ptr_t external,
    ngx_uint_t level, const char *fmt, ...);
void ngx_js_logger(ngx_connection_t *c, ngx_uint_t level,
    const u_char *start, size_t length);
char * ngx_js_import(ngx_conf_t *cf, ngx_command_t *cmd, void *conf);
char * ngx_js_preload_object(ngx_conf_t *cf, ngx_command_t *cmd, void *conf);
ngx_int_t ngx_js_init_preload_vm(ngx_conf_t *cf, ngx_js_loc_conf_t *conf);
ngx_int_t ngx_js_merge_vm(ngx_conf_t *cf, ngx_js_loc_conf_t *conf,
    ngx_js_loc_conf_t *prev,
    ngx_int_t (*init_vm)(ngx_conf_t *cf, ngx_js_loc_conf_t *conf));
ngx_int_t ngx_js_init_conf_vm(ngx_conf_t *cf, ngx_js_loc_conf_t *conf,
    njs_vm_opt_t *options);
ngx_js_loc_conf_t *ngx_js_create_conf(ngx_conf_t *cf, size_t size);
char * ngx_js_merge_conf(ngx_conf_t *cf, void *parent, void *child,
   ngx_int_t (*init_vm)(ngx_conf_t *cf, ngx_js_loc_conf_t *conf));
char *ngx_js_shared_dict_zone(ngx_conf_t *cf, ngx_command_t *cmd, void *conf,
    void *tag);

njs_int_t ngx_js_ext_string(njs_vm_t *vm, njs_object_prop_t *prop,
    njs_value_t *value, njs_value_t *setval, njs_value_t *retval);
njs_int_t ngx_js_ext_uint(njs_vm_t *vm, njs_object_prop_t *prop,
    njs_value_t *value, njs_value_t *setval, njs_value_t *retval);
njs_int_t ngx_js_ext_constant(njs_vm_t *vm, njs_object_prop_t *prop,
    njs_value_t *value, njs_value_t *setval, njs_value_t *retval);
njs_int_t ngx_js_ext_flags(njs_vm_t *vm, njs_object_prop_t *prop,
    njs_value_t *value, njs_value_t *setval, njs_value_t *retval);

ngx_int_t ngx_js_string(njs_vm_t *vm, njs_value_t *value, njs_str_t *str);
ngx_int_t ngx_js_integer(njs_vm_t *vm, njs_value_t *value, ngx_int_t *n);


extern njs_module_t  ngx_js_ngx_module;
extern njs_module_t  njs_webcrypto_module;
extern njs_module_t  njs_xml_module;
extern njs_module_t  njs_zlib_module;


#endif /* _NGX_JS_H_INCLUDED_ */
