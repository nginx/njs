
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

#if (NJS_HAVE_QUICKJS)
#include <qjs.h>
#endif

#define NGX_ENGINE_NJS      1
#define NGX_ENGINE_QJS      2

#define NGX_JS_UNSET        0
#define NGX_JS_DEPRECATED   1
#define NGX_JS_STRING       2
#define NGX_JS_BUFFER       4
#define NGX_JS_BOOLEAN      8
#define NGX_JS_NUMBER       16

#define NGX_JS_BOOL_FALSE   0
#define NGX_JS_BOOL_TRUE    1
#define NGX_JS_BOOL_UNSET   2

#define NGX_NJS_VAR_NOCACHE 1

#define ngx_js_buffer_type(btype) ((btype) & ~NGX_JS_DEPRECATED)

/*
 * This static table solves the problem of a native QuickJS approach
 * which uses a static variables of type JSClassID and JS_NewClassID() to
 * allocate class ids for custom classes. The static variables approach
 * causes a problem when two modules linked with -Wl,-Bsymbolic-functions flag
 * are loaded dynamically.
 */

#define NGX_QJS_CLASS_ID_OFFSET (QJS_CORE_CLASS_ID_LAST)
#define NGX_QJS_CLASS_ID_CONSOLE (NGX_QJS_CLASS_ID_OFFSET + 1)
#define NGX_QJS_CLASS_ID_HTTP_REQUEST (NGX_QJS_CLASS_ID_OFFSET + 2)
#define NGX_QJS_CLASS_ID_HTTP_PERIODIC (NGX_QJS_CLASS_ID_OFFSET + 3)
#define NGX_QJS_CLASS_ID_HTTP_VARS (NGX_QJS_CLASS_ID_OFFSET + 4)
#define NGX_QJS_CLASS_ID_HTTP_HEADERS_IN (NGX_QJS_CLASS_ID_OFFSET + 5)
#define NGX_QJS_CLASS_ID_HTTP_HEADERS_OUT (NGX_QJS_CLASS_ID_OFFSET + 6)
#define NGX_QJS_CLASS_ID_STREAM_SESSION (NGX_QJS_CLASS_ID_OFFSET + 7)
#define NGX_QJS_CLASS_ID_STREAM_PERIODIC (NGX_QJS_CLASS_ID_OFFSET + 8)
#define NGX_QJS_CLASS_ID_STREAM_FLAGS (NGX_QJS_CLASS_ID_OFFSET + 9)
#define NGX_QJS_CLASS_ID_STREAM_VARS (NGX_QJS_CLASS_ID_OFFSET + 10)
#define NGX_QJS_CLASS_ID_SHARED (NGX_QJS_CLASS_ID_OFFSET + 11)
#define NGX_QJS_CLASS_ID_SHARED_DICT (NGX_QJS_CLASS_ID_OFFSET + 12)
#define NGX_QJS_CLASS_ID_SHARED_DICT_ERROR (NGX_QJS_CLASS_ID_OFFSET + 13)


typedef struct ngx_js_loc_conf_s ngx_js_loc_conf_t;
typedef struct ngx_js_event_s ngx_js_event_t;
typedef struct ngx_js_dict_s  ngx_js_dict_t;
typedef struct ngx_js_ctx_s  ngx_js_ctx_t;
typedef struct ngx_engine_s  ngx_engine_t;


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
    void                *ctx;
    njs_opaque_value_t   function;
    njs_opaque_value_t  *args;
    ngx_socket_t         fd;
    NJS_RBTREE_NODE     (node);
    njs_uint_t           nargs;
    void               (*destructor)(ngx_js_event_t *event);
    ngx_event_t          ev;
    void                *data;
};


typedef struct {
    void               **data;
    ngx_uint_t           head;
    ngx_uint_t           tail;
    ngx_uint_t           size;
    ngx_uint_t           capacity;
} ngx_js_queue_t;


#define NGX_JS_COMMON_MAIN_CONF                                               \
    ngx_js_dict_t         *dicts;                                             \
    ngx_array_t           *periodics                                          \


#define _NGX_JS_COMMON_LOC_CONF                                               \
    ngx_uint_t             type;                                              \
    ngx_engine_t          *engine;                                            \
    ngx_uint_t             reuse;                                             \
    ngx_js_queue_t        *reuse_queue;                                       \
    ngx_str_t              cwd;                                               \
    ngx_array_t           *imports;                                           \
    ngx_array_t           *paths;                                             \
                                                                              \
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
    ngx_engine_t          *engine;                                            \
    ngx_log_t             *log;                                               \
    njs_opaque_value_t     args[3];                                           \
    njs_opaque_value_t     retval;                                            \
    njs_arr_t             *rejected_promises;                                 \
    njs_rbtree_t           waiting_events;                                    \
    ngx_socket_t           event_id


#define ngx_js_add_event(ctx, event)                                          \
    njs_rbtree_insert(&(ctx)->waiting_events, &(event)->node)


#define ngx_js_del_event(ctx, event)                                          \
    do {                                                                      \
        if ((event)->destructor) {                                            \
            (event)->destructor(event);                                       \
        }                                                                     \
                                                                              \
        njs_rbtree_delete(&(ctx)->waiting_events, &(event)->node);            \
    } while (0)


typedef struct {
    NGX_JS_COMMON_MAIN_CONF;
} ngx_js_main_conf_t;


struct ngx_js_loc_conf_s {
    NGX_JS_COMMON_LOC_CONF;
};


typedef struct {
    ngx_str_t fname;
    unsigned  flags;
} ngx_js_set_t;


struct ngx_js_ctx_s {
    NGX_JS_COMMON_CTX;
};


typedef struct {
    void                        *external;
} ngx_js_opaque_t;


typedef struct ngx_engine_opts_s {
    unsigned                    engine;
    union {
        struct {
            njs_vm_meta_t      *metas;
            njs_module_t      **addons;
        } njs;
#if (NJS_HAVE_QUICKJS)
        struct {
            uintptr_t          *metas;
            qjs_module_t      **addons;
        } qjs;
#endif
    } u;

    njs_str_t                   file;
    ngx_js_loc_conf_t          *conf;
    ngx_engine_t             *(*clone)(ngx_js_ctx_t *ctx,
                                        ngx_js_loc_conf_t *cf, njs_int_t pr_id,
                                        void *external);
    void                      (*destroy)(ngx_engine_t *e, ngx_js_ctx_t *ctx,
                                        ngx_js_loc_conf_t *conf);
} ngx_engine_opts_t;


typedef struct {
    u_char                     *code;
    size_t                      code_size;
} ngx_js_code_entry_t;


struct ngx_engine_s {
    union {
        struct {
            njs_vm_t           *vm;
        } njs;
#if (NJS_HAVE_QUICKJS)
        struct {
            JSContext          *ctx;
        } qjs;
#endif
    } u;

    ngx_int_t                 (*compile)(ngx_js_loc_conf_t *conf, ngx_log_t *lg,
                                         u_char *start, size_t size);
    ngx_int_t                 (*call)(ngx_js_ctx_t *ctx, ngx_str_t *fname,
                                      njs_opaque_value_t *args,
                                      njs_uint_t nargs);
    ngx_engine_t             *(*clone)(ngx_js_ctx_t *ctx,
                                       ngx_js_loc_conf_t *cf, njs_int_t pr_id,
                                       void *external);
    void                     *(*external)(ngx_engine_t *e);
    ngx_int_t                 (*pending)(ngx_engine_t *e);
    ngx_int_t                 (*string)(ngx_engine_t *e,
                                        njs_opaque_value_t *value,
                                        ngx_str_t *str);
    void                      (*destroy)(ngx_engine_t *e, ngx_js_ctx_t *ctx,
                                         ngx_js_loc_conf_t *conf);

    unsigned                    type;
    const char                 *name;
    njs_mp_t                   *pool;
    njs_arr_t                  *precompiled;
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


void ngx_js_ctx_init(ngx_js_ctx_t *ctx, ngx_log_t *log);

#define ngx_js_ctx_pending(ctx)                                               \
    ((ctx)->engine->pending(ctx->engine)                                      \
     || !njs_rbtree_is_empty(&(ctx)->waiting_events))

#define ngx_js_ctx_external(ctx)                                              \
    ((ctx)->engine->external(ctx->engine))


void ngx_js_ctx_destroy(ngx_js_ctx_t *ctx, ngx_js_loc_conf_t *conf);
ngx_int_t ngx_js_call(njs_vm_t *vm, njs_function_t *func,
    njs_opaque_value_t *args, njs_uint_t nargs);
ngx_int_t ngx_js_exception(njs_vm_t *vm, ngx_str_t *s);
ngx_engine_t *ngx_njs_clone(ngx_js_ctx_t *ctx, ngx_js_loc_conf_t *cf,
    void *external);

#if (NJS_HAVE_QUICKJS)

typedef struct ngx_qjs_event_s ngx_qjs_event_t;

typedef union {
    njs_opaque_value_t opaque;
    JSValue            value;
} ngx_qjs_value_t;

struct ngx_qjs_event_s {
    void                *ctx;
    JSValue              function;
    JSValue             *args;
    ngx_socket_t         fd;
    NJS_RBTREE_NODE     (node);
    njs_uint_t           nargs;
    void               (*destructor)(ngx_qjs_event_t *event);
    ngx_event_t          ev;
    void                *data;
};

#define ngx_qjs_arg(val) (((ngx_qjs_value_t *) &(val))->value)
ngx_engine_t *ngx_qjs_clone(ngx_js_ctx_t *ctx, ngx_js_loc_conf_t *cf,
    void *external);
void ngx_engine_qjs_destroy(ngx_engine_t *e, ngx_js_ctx_t *ctx,
    ngx_js_loc_conf_t *conf);
ngx_int_t ngx_qjs_call(JSContext *cx, JSValue function, JSValue *argv,
    int argc);
ngx_int_t ngx_qjs_exception(ngx_engine_t *e, ngx_str_t *s);
ngx_int_t ngx_qjs_integer(JSContext *cx, JSValueConst val, ngx_int_t *n);
ngx_int_t ngx_qjs_string(JSContext *cx, JSValueConst val, ngx_str_t *str);

#define ngx_qjs_prop(cx, type, start, len)                                   \
    ((type == NGX_JS_STRING) ? qjs_string_create(cx, start, len)             \
                             : qjs_buffer_create(cx, (u_char *) start, len))

#define ngx_qjs_meta(cx, i)                                                  \
    ((uintptr_t *) JS_GetRuntimeOpaque(JS_GetRuntime(cx)))[i]
#define ngx_qjs_external_connection(cx, e)                                   \
    (*((ngx_connection_t **) ((u_char *) (e) + ngx_qjs_meta(cx, 0))))
#define ngx_qjs_external_pool(cx, e)                                         \
    ((ngx_external_pool_pt) ngx_qjs_meta(cx, 1))(e)
#define ngx_qjs_external_resolver(cx, e)                                     \
    ((ngx_external_resolver_pt) ngx_qjs_meta(cx, 2))(e)
#define ngx_qjs_external_resolver_timeout(cx, e)                             \
    ((ngx_external_timeout_pt) ngx_qjs_meta(cx, 3))(e)
#define ngx_qjs_external_event_finalize(cx)                                  \
    ((ngx_js_event_finalize_pt) ngx_qjs_meta(cx, 4))
#define ngx_qjs_external_ssl(cx, e)                                          \
    ((ngx_external_ssl_pt) ngx_qjs_meta(cx, 5))(e)
#define ngx_qjs_external_ssl_verify(cx, e)                                   \
    ((ngx_external_flag_pt) ngx_qjs_meta(cx, 6))(e)
#define ngx_qjs_external_fetch_timeout(cx, e)                                \
    ((ngx_external_timeout_pt) ngx_qjs_meta(cx, 7))(e)
#define ngx_qjs_external_buffer_size(cx, e)                                  \
    ((ngx_external_size_pt) ngx_qjs_meta(cx, 8))(e)
#define ngx_qjs_external_max_response_buffer_size(cx, e)                     \
    ((ngx_external_size_pt) ngx_qjs_meta(cx, 9))(e)
#define ngx_qjs_main_conf(cx)                                                \
    ((ngx_js_main_conf_t *) ngx_qjs_meta(cx, NGX_JS_MAIN_CONF_INDEX))
#define ngx_qjs_external_ctx(cx, e)                                          \
    ((ngx_js_external_ctx_pt) ngx_qjs_meta(cx, 11))(e)

extern qjs_module_t  qjs_webcrypto_module;
extern qjs_module_t  qjs_xml_module;
extern qjs_module_t  qjs_zlib_module;
extern qjs_module_t  ngx_qjs_ngx_module;
extern qjs_module_t  ngx_qjs_ngx_shared_dict_module;

#endif

njs_int_t ngx_js_ext_log(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t level, njs_value_t *retval);
void ngx_js_log(njs_vm_t *vm, njs_external_ptr_t external,
    ngx_uint_t level, const char *fmt, ...);
void ngx_js_logger(ngx_connection_t *c, ngx_uint_t level,
    const u_char *start, size_t length);
char * ngx_js_import(ngx_conf_t *cf, ngx_command_t *cmd, void *conf);
char * ngx_js_engine(ngx_conf_t *cf, ngx_command_t *cmd, void *conf);
char * ngx_js_preload_object(ngx_conf_t *cf, ngx_command_t *cmd, void *conf);
ngx_int_t ngx_js_merge_vm(ngx_conf_t *cf, ngx_js_loc_conf_t *conf,
    ngx_js_loc_conf_t *prev,
    ngx_int_t (*init_vm)(ngx_conf_t *cf, ngx_js_loc_conf_t *conf));
ngx_int_t ngx_js_init_conf_vm(ngx_conf_t *cf, ngx_js_loc_conf_t *conf,
    ngx_engine_opts_t *opt);
ngx_js_loc_conf_t *ngx_js_create_conf(ngx_conf_t *cf, size_t size);
char * ngx_js_merge_conf(ngx_conf_t *cf, void *parent, void *child,
   ngx_int_t (*init_vm)(ngx_conf_t *cf, ngx_js_loc_conf_t *conf));
char *ngx_js_shared_dict_zone(ngx_conf_t *cf, ngx_command_t *cmd, void *conf,
    void *tag);

njs_int_t ngx_js_ext_string(njs_vm_t *vm, njs_object_prop_t *prop, uint32_t unused,
    njs_value_t *value, njs_value_t *setval, njs_value_t *retval);
njs_int_t ngx_js_ext_uint(njs_vm_t *vm, njs_object_prop_t *prop, uint32_t unused,
    njs_value_t *value, njs_value_t *setval, njs_value_t *retval);
njs_int_t ngx_js_ext_constant(njs_vm_t *vm, njs_object_prop_t *prop, uint32_t unused,
    njs_value_t *value, njs_value_t *setval, njs_value_t *retval);
njs_int_t ngx_js_ext_flags(njs_vm_t *vm, njs_object_prop_t *prop, uint32_t unused,
    njs_value_t *value, njs_value_t *setval, njs_value_t *retval);

ngx_int_t ngx_js_string(njs_vm_t *vm, njs_value_t *value, njs_str_t *str);
ngx_int_t ngx_js_integer(njs_vm_t *vm, njs_value_t *value, ngx_int_t *n);
const char *ngx_js_errno_string(int errnum);

ngx_js_queue_t *ngx_js_queue_create(ngx_pool_t *pool, ngx_uint_t capacity);
ngx_int_t ngx_js_queue_push(ngx_js_queue_t *queue, void *item);
void *ngx_js_queue_pop(ngx_js_queue_t *queue);


extern njs_module_t  ngx_js_ngx_module;
extern njs_module_t  njs_webcrypto_module;
extern njs_module_t  njs_xml_module;
extern njs_module_t  njs_zlib_module;


#endif /* _NGX_JS_H_INCLUDED_ */
