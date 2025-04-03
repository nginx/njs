
/*
 * Copyright (C) Roman Arutyunyan
 * Copyright (C) Dmitry Volyntsev
 * Copyright (C) NGINX, Inc.
 */


#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_stream.h>
#include "ngx_js.h"


typedef struct ngx_stream_js_ctx_s  ngx_stream_js_ctx_t;

typedef struct {
    NGX_JS_COMMON_LOC_CONF;

    ngx_str_t              access;
    ngx_str_t              preread;
    ngx_str_t              filter;
} ngx_stream_js_srv_conf_t;


typedef struct {
    njs_opaque_value_t      function;
    ngx_uint_t              data_type;
} ngx_stream_js_ev_t;


typedef struct {
    ngx_stream_conf_ctx_t  *conf_ctx;
    ngx_connection_t       *connection;
    uint8_t                *worker_affinity;

    /**
     * fd is used for event debug and should be at the same position
     * as in ngx_connection_t: after a 3rd pointer.
     */
    ngx_socket_t            fd;

    ngx_str_t               method;
    ngx_msec_t              interval;
    ngx_msec_t              jitter;

    ngx_log_t               log;
    ngx_event_t             event;
} ngx_js_periodic_t;


struct ngx_stream_js_ctx_s {
    NGX_JS_COMMON_CTX;
    ngx_buf_t              *buf;
    ngx_chain_t           **last_out;
    ngx_chain_t            *free;
    ngx_chain_t            *upstream_busy;
    ngx_chain_t            *downstream_busy;
    ngx_int_t               status;
    ngx_int_t             (*run_event)(ngx_stream_session_t *s,
                                       ngx_stream_js_ctx_t *ctx,
                                       ngx_stream_js_ev_t *event,
                                       ngx_uint_t from_upstream);
    ngx_int_t             (*body_filter)(ngx_stream_session_t *s,
                                         ngx_stream_js_ctx_t *ctx,
                                         ngx_chain_t *in,
                                         ngx_uint_t from_upstream);
#define NGX_JS_EVENT_UPLOAD   0
#define NGX_JS_EVENT_DOWNLOAD 1
#define NGX_JS_EVENT_MAX      2
    ngx_stream_js_ev_t      events[NGX_JS_EVENT_MAX];
    unsigned                filter:1;
    unsigned                in_progress:1;
    ngx_js_periodic_t      *periodic;
};


#if (NJS_HAVE_QUICKJS)

typedef struct {
    ngx_str_t               name;
    ngx_uint_t              data_type;
    ngx_uint_t              id;
} ngx_stream_qjs_event_t;

typedef struct {
    ngx_stream_session_t   *session;
    JSValue                 callbacks[NGX_JS_EVENT_MAX];
} ngx_stream_qjs_session_t;

#endif

#define ngx_stream_pending(ctx)                                               \
    (ngx_js_ctx_pending(ctx) || ngx_stream_js_pending_events(ctx))


static ngx_int_t ngx_stream_js_access_handler(ngx_stream_session_t *s);
static ngx_int_t ngx_stream_js_preread_handler(ngx_stream_session_t *s);
static ngx_int_t ngx_stream_js_phase_handler(ngx_stream_session_t *s,
    ngx_str_t *name);
static ngx_int_t ngx_stream_js_body_filter(ngx_stream_session_t *s,
    ngx_chain_t *in, ngx_uint_t from_upstream);
static ngx_int_t ngx_stream_njs_body_filter(ngx_stream_session_t *s,
    ngx_stream_js_ctx_t *ctx, ngx_chain_t *in, ngx_uint_t from_upstream);
static ngx_int_t ngx_stream_js_next_filter(ngx_stream_session_t *s,
    ngx_stream_js_ctx_t *ctx, ngx_chain_t *out, ngx_uint_t from_upstream);
static ngx_int_t ngx_stream_js_variable_set(ngx_stream_session_t *s,
    ngx_stream_variable_value_t *v, uintptr_t data);
static ngx_int_t ngx_stream_js_variable_var(ngx_stream_session_t *s,
    ngx_stream_variable_value_t *v, uintptr_t data);
static ngx_int_t ngx_stream_js_init_vm(ngx_stream_session_t *s,
    njs_int_t proto_id);
static ngx_int_t ngx_stream_js_pending_events(ngx_stream_js_ctx_t *ctx);
static void ngx_stream_js_drop_events(ngx_stream_js_ctx_t *ctx);
static void ngx_stream_js_cleanup(void *data);
static ngx_int_t ngx_stream_js_run_event(ngx_stream_session_t *s,
    ngx_stream_js_ctx_t *ctx, ngx_stream_js_ev_t *event,
    ngx_uint_t from_upstream);
static ngx_stream_js_ev_t *ngx_stream_js_event(ngx_stream_session_t *s,
    njs_str_t *event);

static njs_int_t ngx_stream_js_ext_get_remote_address(njs_vm_t *vm,
    njs_object_prop_t *prop, njs_value_t *value, njs_value_t *setval,
    njs_value_t *retval);

static njs_int_t ngx_stream_js_ext_done(njs_vm_t *vm, njs_value_t *args,
     njs_uint_t nargs, njs_index_t unused, njs_value_t *retval);

static njs_int_t ngx_stream_js_ext_on(njs_vm_t *vm, njs_value_t *args,
     njs_uint_t nargs, njs_index_t unused, njs_value_t *retval);
static njs_int_t ngx_stream_js_ext_off(njs_vm_t *vm, njs_value_t *args,
     njs_uint_t nargs, njs_index_t unused, njs_value_t *retval);
static njs_int_t ngx_stream_js_ext_send(njs_vm_t *vm, njs_value_t *args,
     njs_uint_t nargs, njs_index_t from_upstream, njs_value_t *retval);
static njs_int_t ngx_stream_js_ext_set_return_value(njs_vm_t *vm,
    njs_value_t *args, njs_uint_t nargs, njs_index_t unused,
    njs_value_t *retval);

static njs_int_t ngx_stream_js_ext_variables(njs_vm_t *vm,
    njs_object_prop_t *prop, njs_value_t *value, njs_value_t *setval,
    njs_value_t *retval);
static njs_int_t ngx_stream_js_periodic_variables(njs_vm_t *vm,
    njs_object_prop_t *prop, njs_value_t *value, njs_value_t *setval,
    njs_value_t *retval);

#if (NJS_HAVE_QUICKJS)

static JSValue ngx_stream_qjs_ext_done(JSContext *cx, JSValueConst this_val,
    int argc, JSValueConst *argv, int magic);
static JSValue ngx_stream_qjs_ext_log(JSContext *cx, JSValueConst this_val,
    int argc, JSValueConst *argv, int level);
static JSValue ngx_stream_qjs_ext_on(JSContext *cx, JSValueConst this_val,
    int argc, JSValueConst *argv);
static JSValue ngx_stream_qjs_ext_off(JSContext *cx, JSValueConst this_val,
    int argc, JSValueConst *argv);
static JSValue ngx_stream_qjs_ext_periodic_variables(JSContext *cx,
    JSValueConst this_val, int type);
static JSValue ngx_stream_qjs_ext_remote_address(JSContext *cx,
    JSValueConst this_val);
static JSValue ngx_stream_qjs_ext_send(JSContext *cx, JSValueConst this_val,
    int argc, JSValueConst *argv, int from_upstream);
static JSValue ngx_stream_qjs_ext_set_return_value(JSContext *cx,
    JSValueConst this_val, int argc, JSValueConst *argv);
static JSValue ngx_stream_qjs_ext_variables(JSContext *cx,
    JSValueConst this_val, int type);
static JSValue ngx_stream_qjs_ext_uint(JSContext *cx, JSValueConst this_val,
    int offset);
static JSValue ngx_stream_qjs_ext_flag(JSContext *cx, JSValueConst this_val,
    int mask);

static int ngx_stream_qjs_variables_own_property(JSContext *cx,
    JSPropertyDescriptor *pdesc, JSValueConst obj, JSAtom prop);
static int ngx_stream_qjs_variables_set_property(JSContext *cx,
    JSValueConst obj, JSAtom atom, JSValueConst value, JSValueConst receiver,
    int flags);
static int ngx_stream_qjs_variables_define_own_property(JSContext *cx,
    JSValueConst obj, JSAtom prop, JSValueConst value, JSValueConst getter,
    JSValueConst setter, int flags);

static ngx_int_t ngx_stream_qjs_run_event(ngx_stream_session_t *s,
    ngx_stream_js_ctx_t *ctx, ngx_stream_js_ev_t *event,
    ngx_uint_t from_upstream);
static ngx_int_t ngx_stream_qjs_body_filter(ngx_stream_session_t *s,
    ngx_stream_js_ctx_t *ctx, ngx_chain_t *in, ngx_uint_t from_upstream);

static ngx_stream_session_t *ngx_stream_qjs_session(JSValueConst val);
static JSValue ngx_stream_qjs_session_make(JSContext *cx, ngx_int_t proto_id,
    ngx_stream_session_t *s);
static void ngx_stream_qjs_session_finalizer(JSRuntime *rt, JSValue val);
static void ngx_stream_qjs_periodic_finalizer(JSRuntime *rt, JSValue val);

#endif

static ngx_pool_t *ngx_stream_js_pool(ngx_stream_session_t *s);
static ngx_resolver_t *ngx_stream_js_resolver(ngx_stream_session_t *s);
static ngx_msec_t ngx_stream_js_resolver_timeout(ngx_stream_session_t *s);
static ngx_msec_t ngx_stream_js_fetch_timeout(ngx_stream_session_t *s);
static size_t ngx_stream_js_buffer_size(ngx_stream_session_t *s);
static size_t ngx_stream_js_max_response_buffer_size(ngx_stream_session_t *s);
static void ngx_stream_js_event_finalize(ngx_stream_session_t *s, ngx_int_t rc);
static ngx_js_ctx_t *ngx_stream_js_ctx(ngx_stream_session_t *s);

static void ngx_stream_js_periodic_handler(ngx_event_t *ev);
static void ngx_stream_js_periodic_event_handler(ngx_event_t *ev);
static void ngx_stream_js_periodic_finalize(ngx_stream_session_t *s,
    ngx_int_t rc);
static void ngx_stream_js_periodic_destroy(ngx_stream_session_t *s,
    ngx_js_periodic_t *periodic);

static njs_int_t ngx_js_stream_init(njs_vm_t *vm);
static ngx_int_t ngx_stream_js_init(ngx_conf_t *cf);
static ngx_int_t ngx_stream_js_init_worker(ngx_cycle_t *cycle);
static char *ngx_stream_js_periodic(ngx_conf_t *cf, ngx_command_t *cmd,
    void *conf);
static char *ngx_stream_js_set(ngx_conf_t *cf, ngx_command_t *cmd,
    void *conf);
static char *ngx_stream_js_var(ngx_conf_t *cf, ngx_command_t *cmd,
    void *conf);
static ngx_int_t ngx_stream_js_init_conf_vm(ngx_conf_t *cf,
    ngx_js_loc_conf_t *conf);
static void *ngx_stream_js_create_main_conf(ngx_conf_t *cf);
static void *ngx_stream_js_create_srv_conf(ngx_conf_t *cf);
static char *ngx_stream_js_merge_srv_conf(ngx_conf_t *cf, void *parent,
    void *child);
static char *ngx_stream_js_shared_dict_zone(ngx_conf_t *cf, ngx_command_t *cmd,
    void *conf);

static ngx_ssl_t *ngx_stream_js_ssl(ngx_stream_session_t *s);
static ngx_flag_t ngx_stream_js_ssl_verify(ngx_stream_session_t *s);

static ngx_conf_bitmask_t  ngx_stream_js_engines[] = {
    { ngx_string("njs"), NGX_ENGINE_NJS },
#if (NJS_HAVE_QUICKJS)
    { ngx_string("qjs"), NGX_ENGINE_QJS },
#endif
    { ngx_null_string, 0 }
};

#if (NGX_STREAM_SSL)

static ngx_conf_bitmask_t  ngx_stream_js_ssl_protocols[] = {
    { ngx_string("TLSv1"), NGX_SSL_TLSv1 },
    { ngx_string("TLSv1.1"), NGX_SSL_TLSv1_1 },
    { ngx_string("TLSv1.2"), NGX_SSL_TLSv1_2 },
    { ngx_string("TLSv1.3"), NGX_SSL_TLSv1_3 },
    { ngx_null_string, 0 }
};

#endif

static ngx_command_t  ngx_stream_js_commands[] = {

    { ngx_string("js_engine"),
      NGX_STREAM_MAIN_CONF|NGX_STREAM_SRV_CONF|NGX_CONF_1MORE,
      ngx_js_engine,
      NGX_STREAM_SRV_CONF_OFFSET,
      offsetof(ngx_stream_js_srv_conf_t, type),
      &ngx_stream_js_engines },

    { ngx_string("js_context_reuse"),
      NGX_STREAM_MAIN_CONF|NGX_STREAM_SRV_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_size_slot,
      NGX_STREAM_SRV_CONF_OFFSET,
      offsetof(ngx_stream_js_srv_conf_t, reuse),
      NULL },

    { ngx_string("js_import"),
      NGX_STREAM_MAIN_CONF|NGX_STREAM_SRV_CONF|NGX_CONF_TAKE13,
      ngx_js_import,
      NGX_STREAM_SRV_CONF_OFFSET,
      0,
      NULL },

    { ngx_string("js_periodic"),
      NGX_STREAM_SRV_CONF|NGX_CONF_ANY,
      ngx_stream_js_periodic,
      NGX_STREAM_SRV_CONF_OFFSET,
      0,
      NULL },

    { ngx_string("js_preload_object"),
      NGX_STREAM_MAIN_CONF|NGX_STREAM_SRV_CONF|NGX_CONF_TAKE13,
      ngx_js_preload_object,
      NGX_STREAM_SRV_CONF_OFFSET,
      0,
      NULL },

    { ngx_string("js_path"),
      NGX_STREAM_MAIN_CONF|NGX_STREAM_SRV_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_str_array_slot,
      NGX_STREAM_SRV_CONF_OFFSET,
      offsetof(ngx_stream_js_srv_conf_t, paths),
      NULL },

    { ngx_string("js_set"),
      NGX_STREAM_MAIN_CONF|NGX_STREAM_SRV_CONF|NGX_CONF_TAKE23,
      ngx_stream_js_set,
      0,
      0,
      NULL },

    { ngx_string("js_var"),
      NGX_STREAM_MAIN_CONF|NGX_STREAM_SRV_CONF|NGX_CONF_TAKE12,
      ngx_stream_js_var,
      0,
      0,
      NULL },

    { ngx_string("js_access"),
      NGX_STREAM_MAIN_CONF|NGX_STREAM_SRV_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_str_slot,
      NGX_STREAM_SRV_CONF_OFFSET,
      offsetof(ngx_stream_js_srv_conf_t, access),
      NULL },

    { ngx_string("js_preread"),
      NGX_STREAM_MAIN_CONF|NGX_STREAM_SRV_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_str_slot,
      NGX_STREAM_SRV_CONF_OFFSET,
      offsetof(ngx_stream_js_srv_conf_t, preread),
      NULL },

    { ngx_string("js_filter"),
      NGX_STREAM_MAIN_CONF|NGX_STREAM_SRV_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_str_slot,
      NGX_STREAM_SRV_CONF_OFFSET,
      offsetof(ngx_stream_js_srv_conf_t, filter),
      NULL },

    { ngx_string("js_fetch_buffer_size"),
      NGX_STREAM_MAIN_CONF|NGX_STREAM_SRV_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_size_slot,
      NGX_STREAM_SRV_CONF_OFFSET,
      offsetof(ngx_stream_js_srv_conf_t, buffer_size),
      NULL },

    { ngx_string("js_fetch_max_response_buffer_size"),
      NGX_STREAM_MAIN_CONF|NGX_STREAM_SRV_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_size_slot,
      NGX_STREAM_SRV_CONF_OFFSET,
      offsetof(ngx_stream_js_srv_conf_t, max_response_body_size),
      NULL },

    { ngx_string("js_fetch_timeout"),
      NGX_STREAM_MAIN_CONF|NGX_STREAM_SRV_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_msec_slot,
      NGX_STREAM_SRV_CONF_OFFSET,
      offsetof(ngx_stream_js_srv_conf_t, timeout),
      NULL },

#if (NGX_STREAM_SSL)

    { ngx_string("js_fetch_ciphers"),
      NGX_STREAM_MAIN_CONF|NGX_STREAM_SRV_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_str_slot,
      NGX_STREAM_SRV_CONF_OFFSET,
      offsetof(ngx_stream_js_srv_conf_t, ssl_ciphers),
      NULL },

    { ngx_string("js_fetch_protocols"),
      NGX_STREAM_MAIN_CONF|NGX_STREAM_SRV_CONF|NGX_CONF_1MORE,
      ngx_conf_set_bitmask_slot,
      NGX_STREAM_SRV_CONF_OFFSET,
      offsetof(ngx_stream_js_srv_conf_t, ssl_protocols),
      &ngx_stream_js_ssl_protocols },

    { ngx_string("js_fetch_verify"),
      NGX_STREAM_MAIN_CONF|NGX_STREAM_SRV_CONF|NGX_CONF_FLAG,
      ngx_conf_set_flag_slot,
      NGX_STREAM_SRV_CONF_OFFSET,
      offsetof(ngx_stream_js_srv_conf_t, ssl_verify),
      NULL },

    { ngx_string("js_fetch_verify_depth"),
      NGX_STREAM_MAIN_CONF|NGX_STREAM_SRV_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_num_slot,
      NGX_STREAM_SRV_CONF_OFFSET,
      offsetof(ngx_stream_js_srv_conf_t, ssl_verify_depth),
      NULL },

    { ngx_string("js_fetch_trusted_certificate"),
      NGX_STREAM_MAIN_CONF|NGX_STREAM_SRV_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_str_slot,
      NGX_STREAM_SRV_CONF_OFFSET,
      offsetof(ngx_stream_js_srv_conf_t, ssl_trusted_certificate),
      NULL },

#endif

    { ngx_string("js_shared_dict_zone"),
      NGX_STREAM_MAIN_CONF|NGX_CONF_1MORE,
      ngx_stream_js_shared_dict_zone,
      0,
      0,
      NULL },

      ngx_null_command
};


static ngx_stream_module_t  ngx_stream_js_module_ctx = {
    NULL,                           /* preconfiguration */
    ngx_stream_js_init,             /* postconfiguration */

    ngx_stream_js_create_main_conf, /* create main configuration */
    NULL,                           /* init main configuration */

    ngx_stream_js_create_srv_conf,  /* create server configuration */
    ngx_stream_js_merge_srv_conf,   /* merge server configuration */
};


ngx_module_t  ngx_stream_js_module = {
    NGX_MODULE_V1,
    &ngx_stream_js_module_ctx,      /* module context */
    ngx_stream_js_commands,         /* module directives */
    NGX_STREAM_MODULE,              /* module type */
    NULL,                           /* init master */
    NULL,                           /* init module */
    ngx_stream_js_init_worker,      /* init process */
    NULL,                           /* init thread */
    NULL,                           /* exit thread */
    NULL,                           /* exit process */
    NULL,                           /* exit master */
    NGX_MODULE_V1_PADDING
};


static njs_external_t  ngx_stream_js_ext_session[] = {

    {
        .flags = NJS_EXTERN_PROPERTY | NJS_EXTERN_SYMBOL,
        .name.symbol = NJS_SYMBOL_TO_STRING_TAG,
        .u.property = {
            .value = "Stream Session",
        }
    },

    {
        .flags = NJS_EXTERN_METHOD,
        .name.string = njs_str("allow"),
        .writable = 1,
        .configurable = 1,
        .enumerable = 1,
        .u.method = {
            .native = ngx_stream_js_ext_done,
            .magic8 = NGX_OK,
        }
    },

    {
        .flags = NJS_EXTERN_METHOD,
        .name.string = njs_str("decline"),
        .writable = 1,
        .configurable = 1,
        .enumerable = 1,
        .u.method = {
            .native = ngx_stream_js_ext_done,
            .magic8 = -NGX_DECLINED,
        }
    },

    {
        .flags = NJS_EXTERN_METHOD,
        .name.string = njs_str("deny"),
        .writable = 1,
        .configurable = 1,
        .enumerable = 1,
        .u.method = {
            .native = ngx_stream_js_ext_done,
            .magic8 = -NGX_DONE,
        }
    },

    {
        .flags = NJS_EXTERN_METHOD,
        .name.string = njs_str("done"),
        .writable = 1,
        .configurable = 1,
        .enumerable = 1,
        .u.method = {
            .native = ngx_stream_js_ext_done,
            .magic8 = NGX_OK,

        }
    },

    {
        .flags = NJS_EXTERN_METHOD,
        .name.string = njs_str("error"),
        .writable = 1,
        .configurable = 1,
        .enumerable = 1,
        .u.method = {
            .native = ngx_js_ext_log,
            .magic8 = NGX_LOG_ERR,
        }
    },

    {
        .flags = NJS_EXTERN_METHOD,
        .name.string = njs_str("log"),
        .writable = 1,
        .configurable = 1,
        .enumerable = 1,
        .u.method = {
            .native = ngx_js_ext_log,
            .magic8 = NGX_LOG_INFO,
        }
    },

    {
        .flags = NJS_EXTERN_METHOD,
        .name.string = njs_str("off"),
        .writable = 1,
        .configurable = 1,
        .enumerable = 1,
        .u.method = {
            .native = ngx_stream_js_ext_off,
        }
    },

    {
        .flags = NJS_EXTERN_METHOD,
        .name.string = njs_str("on"),
        .writable = 1,
        .configurable = 1,
        .enumerable = 1,
        .u.method = {
            .native = ngx_stream_js_ext_on,
        }
    },

    {
        .flags = NJS_EXTERN_OBJECT,
        .name.string = njs_str("rawVariables"),
        .u.object = {
            .writable = 1,
            .prop_handler = ngx_stream_js_ext_variables,
            .magic32 = NGX_JS_BUFFER,
        }
    },

    {
        .flags = NJS_EXTERN_PROPERTY,
        .name.string = njs_str("remoteAddress"),
        .enumerable = 1,
        .u.property = {
            .handler = ngx_stream_js_ext_get_remote_address,
        }
    },

    {
        .flags = NJS_EXTERN_METHOD,
        .name.string = njs_str("send"),
        .writable = 1,
        .configurable = 1,
        .enumerable = 1,
        .u.method = {
            .native = ngx_stream_js_ext_send,
            .magic8 = NGX_JS_BOOL_UNSET,
        }
    },

    {
        .flags = NJS_EXTERN_METHOD,
        .name.string = njs_str("sendDownstream"),
        .writable = 1,
        .configurable = 1,
        .enumerable = 1,
        .u.method = {
            .native = ngx_stream_js_ext_send,
            .magic8 = NGX_JS_BOOL_TRUE,
        }
    },

    {
        .flags = NJS_EXTERN_METHOD,
        .name.string = njs_str("sendUpstream"),
        .writable = 1,
        .configurable = 1,
        .enumerable = 1,
        .u.method = {
            .native = ngx_stream_js_ext_send,
            .magic8 = NGX_JS_BOOL_FALSE,
        }
    },

    {
        .flags = NJS_EXTERN_METHOD,
        .name.string = njs_str("setReturnValue"),
        .writable = 1,
        .configurable = 1,
        .enumerable = 1,
        .u.method = {
            .native = ngx_stream_js_ext_set_return_value,
        }
    },

    {
        .flags = NJS_EXTERN_PROPERTY,
        .name.string = njs_str("status"),
        .enumerable = 1,
        .u.property = {
            .handler = ngx_js_ext_uint,
            .magic32 = offsetof(ngx_stream_session_t, status),
        }
    },

    {
        .flags = NJS_EXTERN_OBJECT,
        .name.string = njs_str("variables"),
        .u.object = {
            .writable = 1,
            .prop_handler = ngx_stream_js_ext_variables,
            .magic32 = NGX_JS_STRING,
        }
    },

    {
        .flags = NJS_EXTERN_METHOD,
        .name.string = njs_str("warn"),
        .writable = 1,
        .configurable = 1,
        .enumerable = 1,
        .u.method = {
            .native = ngx_js_ext_log,
            .magic8 = NGX_LOG_WARN,
        }
    },
};


static njs_external_t  ngx_stream_js_ext_periodic_session[] = {

    {
        .flags = NJS_EXTERN_PROPERTY | NJS_EXTERN_SYMBOL,
        .name.symbol = NJS_SYMBOL_TO_STRING_TAG,
        .u.property = {
            .value = "PeriodicSession",
        }
    },

    {
        .flags = NJS_EXTERN_OBJECT,
        .name.string = njs_str("rawVariables"),
        .u.object = {
            .writable = 1,
            .prop_handler = ngx_stream_js_periodic_variables,
            .magic32 = NGX_JS_BUFFER,
        }
    },

    {
        .flags = NJS_EXTERN_OBJECT,
        .name.string = njs_str("variables"),
        .u.object = {
            .writable = 1,
            .prop_handler = ngx_stream_js_periodic_variables,
            .magic32 = NGX_JS_STRING,
        }
    },
};


static njs_external_t  ngx_stream_js_ext_session_flags[] = {

    {
        .flags = NJS_EXTERN_PROPERTY | NJS_EXTERN_SYMBOL,
        .name.symbol = NJS_SYMBOL_TO_STRING_TAG,
        .u.property = {
            .value = "Stream Flags",
        }
    },

    {
        .flags = NJS_EXTERN_PROPERTY,
        .name.string = njs_str("from_upstream"),
        .enumerable = 1,
        .u.property = {
            .handler = ngx_js_ext_flags,
            .magic16 = NGX_JS_BOOLEAN,
            .magic32 = 0x00000002,
        }
    },

    {
        .flags = NJS_EXTERN_PROPERTY,
        .name.string = njs_str("last"),
        .enumerable = 1,
        .u.property = {
            .handler = ngx_js_ext_flags,
            .magic16 = NGX_JS_BOOLEAN,
            .magic32 = 0x00000001,
        }
    },
};


static uintptr_t ngx_stream_js_uptr[] = {
    offsetof(ngx_stream_session_t, connection),
    (uintptr_t) ngx_stream_js_pool,
    (uintptr_t) ngx_stream_js_resolver,
    (uintptr_t) ngx_stream_js_resolver_timeout,
    (uintptr_t) ngx_stream_js_event_finalize,
    (uintptr_t) ngx_stream_js_ssl,
    (uintptr_t) ngx_stream_js_ssl_verify,
    (uintptr_t) ngx_stream_js_fetch_timeout,
    (uintptr_t) ngx_stream_js_buffer_size,
    (uintptr_t) ngx_stream_js_max_response_buffer_size,
    (uintptr_t) 0 /* main_conf ptr */,
    (uintptr_t) ngx_stream_js_ctx,
};


static njs_vm_meta_t ngx_stream_js_metas = {
    .size = njs_nitems(ngx_stream_js_uptr),
    .values = ngx_stream_js_uptr
};


static ngx_stream_filter_pt  ngx_stream_next_filter;


static njs_int_t    ngx_stream_js_session_proto_id = 1;
static njs_int_t    ngx_stream_js_periodic_session_proto_id  = 2;
static njs_int_t    ngx_stream_js_session_flags_proto_id = 3;


njs_module_t  ngx_js_stream_module = {
    .name = njs_str("stream"),
    .preinit = NULL,
    .init = ngx_js_stream_init,
};


njs_module_t *njs_stream_js_addon_modules[] = {
    /*
     * Shared addons should be in the same order and the same positions
     * in all nginx modules.
     */
    &ngx_js_ngx_module,
    &ngx_js_fetch_module,
    &ngx_js_shared_dict_module,
#ifdef NJS_HAVE_OPENSSL
    &njs_webcrypto_module,
#endif
#ifdef NJS_HAVE_XML
    &njs_xml_module,
#endif
#ifdef NJS_HAVE_ZLIB
    &njs_zlib_module,
#endif
    &ngx_js_stream_module,
    NULL,
};

#if (NJS_HAVE_QUICKJS)

static const JSCFunctionListEntry ngx_stream_qjs_ext_session[] = {
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "Stream Session",
                       JS_PROP_CONFIGURABLE),
    JS_CFUNC_MAGIC_DEF("allow", 1, ngx_stream_qjs_ext_done, NGX_OK),
    JS_CFUNC_MAGIC_DEF("decline", 1, ngx_stream_qjs_ext_done, -NGX_DECLINED),
    JS_CFUNC_MAGIC_DEF("deny", 1, ngx_stream_qjs_ext_done, -NGX_DONE),
    JS_CFUNC_MAGIC_DEF("done", 1, ngx_stream_qjs_ext_done, NGX_OK),
    JS_CFUNC_MAGIC_DEF("error", 1, ngx_stream_qjs_ext_log, NGX_LOG_ERR),
    JS_CFUNC_MAGIC_DEF("log", 1, ngx_stream_qjs_ext_log, NGX_LOG_INFO),
    JS_CFUNC_DEF("on", 2, ngx_stream_qjs_ext_on),
    JS_CFUNC_DEF("off", 1, ngx_stream_qjs_ext_off),
    JS_CGETSET_MAGIC_DEF("rawVariables", ngx_stream_qjs_ext_variables,
                   NULL, NGX_JS_BUFFER),
    JS_CGETSET_DEF("remoteAddress", ngx_stream_qjs_ext_remote_address, NULL),
    JS_CFUNC_MAGIC_DEF("send", 2, ngx_stream_qjs_ext_send, NGX_JS_BOOL_UNSET),
    JS_CFUNC_MAGIC_DEF("sendDownstream", 1, ngx_stream_qjs_ext_send,
                       NGX_JS_BOOL_TRUE),
    JS_CFUNC_MAGIC_DEF("sendUpstream", 1, ngx_stream_qjs_ext_send,
                       NGX_JS_BOOL_FALSE),
    JS_CFUNC_DEF("setReturnValue", 1, ngx_stream_qjs_ext_set_return_value),
    JS_CGETSET_MAGIC_DEF("status", ngx_stream_qjs_ext_uint, NULL,
                         offsetof(ngx_stream_session_t, status)),
    JS_CGETSET_MAGIC_DEF("variables", ngx_stream_qjs_ext_variables,
                         NULL, NGX_JS_STRING),
    JS_CFUNC_MAGIC_DEF("warn", 1, ngx_stream_qjs_ext_log, NGX_LOG_WARN),
};


static const JSCFunctionListEntry ngx_stream_qjs_ext_periodic[] = {
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "PeriodicSession",
                       JS_PROP_CONFIGURABLE),
    JS_CGETSET_MAGIC_DEF("rawVariables", ngx_stream_qjs_ext_periodic_variables,
                   NULL, NGX_JS_BUFFER),
    JS_CGETSET_MAGIC_DEF("variables", ngx_stream_qjs_ext_periodic_variables,
                         NULL, NGX_JS_STRING),
};


static const JSCFunctionListEntry ngx_stream_qjs_ext_flags[] = {
    JS_CGETSET_MAGIC_DEF("from_upstream", ngx_stream_qjs_ext_flag, NULL,
                         2),
    JS_CGETSET_MAGIC_DEF("last", ngx_stream_qjs_ext_flag, NULL, 1),
};


static JSClassDef ngx_stream_qjs_session_class = {
    "Session",
    .finalizer = ngx_stream_qjs_session_finalizer,
};


static JSClassDef ngx_stream_qjs_periodic_class = {
    "Periodic",
    .finalizer = ngx_stream_qjs_periodic_finalizer,
};


static JSClassDef ngx_stream_qjs_flags_class = {
    "Stream Flags",
    .finalizer = NULL,
};


static JSClassDef ngx_stream_qjs_variables_class = {
    "Variables",
    .finalizer = NULL,
    .exotic = & (JSClassExoticMethods) {
        .get_own_property = ngx_stream_qjs_variables_own_property,
        .set_property = ngx_stream_qjs_variables_set_property,
        .define_own_property = ngx_stream_qjs_variables_define_own_property,
    },
};


qjs_module_t *njs_stream_qjs_addon_modules[] = {
    &ngx_qjs_ngx_module,
    &ngx_qjs_ngx_shared_dict_module,
    /*
     * Shared addons should be in the same order and the same positions
     * in all nginx modules.
     */
#ifdef NJS_HAVE_OPENSSL
    &qjs_webcrypto_module,
#endif
#ifdef NJS_HAVE_ZLIB
    &qjs_zlib_module,
#endif
    NULL,
};

#endif


static ngx_int_t
ngx_stream_js_access_handler(ngx_stream_session_t *s)
{
    ngx_stream_js_srv_conf_t  *jscf;

    ngx_log_debug0(NGX_LOG_DEBUG_STREAM, s->connection->log, 0,
                   "js access handler");

    jscf = ngx_stream_get_module_srv_conf(s, ngx_stream_js_module);

    return ngx_stream_js_phase_handler(s, &jscf->access);
}


static ngx_int_t
ngx_stream_js_preread_handler(ngx_stream_session_t *s)
{
    ngx_stream_js_srv_conf_t  *jscf;

    ngx_log_debug0(NGX_LOG_DEBUG_STREAM, s->connection->log, 0,
                   "js preread handler");

    jscf = ngx_stream_get_module_srv_conf(s, ngx_stream_js_module);

    return ngx_stream_js_phase_handler(s, &jscf->preread);
}


static ngx_int_t
ngx_stream_js_phase_handler(ngx_stream_session_t *s, ngx_str_t *name)
{
    ngx_int_t             rc;
    ngx_stream_js_ctx_t  *ctx;

    if (name->len == 0) {
        return NGX_DECLINED;
    }

    ngx_log_debug0(NGX_LOG_DEBUG_STREAM, s->connection->log, 0,
                   "stream js phase handler");

    rc = ngx_stream_js_init_vm(s, ngx_stream_js_session_proto_id);
    if (rc != NGX_OK) {
        return rc;
    }

    ctx = ngx_stream_get_module_ctx(s, ngx_stream_js_module);

    if (!ctx->in_progress) {
        /*
         * status is expected to be overriden by allow(), deny(), decline() or
         * done() methods.
         */

        ctx->status = NGX_ERROR;

        ngx_log_debug1(NGX_LOG_DEBUG_STREAM, ctx->log, 0,
                       "stream js phase call \"%V\"", name);

        rc = ctx->engine->call((ngx_js_ctx_t *) ctx, name, &ctx->args[0], 1);

        if (rc == NGX_ERROR) {
            return rc;
        }
    }

    rc = ctx->run_event(s, ctx, &ctx->events[NGX_JS_EVENT_UPLOAD], 0);
    if (rc != NGX_OK) {
        return NGX_ERROR;
    }

    if (ngx_stream_pending(ctx)) {
        ctx->in_progress = 1;
        rc = (ctx->events[NGX_JS_EVENT_UPLOAD].data_type != NGX_JS_UNSET)
                                                        ? NGX_AGAIN
                                                        : NGX_DONE;

    } else {
        ctx->in_progress = 0;
        rc = ctx->status;
    }

    ngx_log_debug1(NGX_LOG_DEBUG_STREAM, ctx->log, 0, "stream js phase rc: %i",
                   rc);

    return rc;
}


#define ngx_stream_event(from_upstream)                                 \
    (from_upstream ? &ctx->events[NGX_JS_EVENT_DOWNLOAD]                \
                   : &ctx->events[NGX_JS_EVENT_UPLOAD])


static ngx_int_t
ngx_stream_js_body_filter(ngx_stream_session_t *s, ngx_chain_t *in,
    ngx_uint_t from_upstream)
{
    ngx_int_t                  rc;
    ngx_chain_t               *out;
    ngx_stream_js_ctx_t       *ctx;
    ngx_stream_js_srv_conf_t  *jscf;

    jscf = ngx_stream_get_module_srv_conf(s, ngx_stream_js_module);
    if (jscf->filter.len == 0) {
        return ngx_stream_next_filter(s, in, from_upstream);
    }

    ngx_log_debug1(NGX_LOG_DEBUG_STREAM, s->connection->log, 0,
                   "stream js filter u:%ui", from_upstream);

    rc = ngx_stream_js_init_vm(s, ngx_stream_js_session_proto_id);

    if (rc == NGX_ERROR) {
        return NGX_ERROR;
    }

    if (rc == NGX_DECLINED) {
        return ngx_stream_next_filter(s, in, from_upstream);
    }

    ctx = ngx_stream_get_module_ctx(s, ngx_stream_js_module);

    if (!ctx->filter) {
        ngx_log_debug1(NGX_LOG_DEBUG_STREAM, s->connection->log, 0,
                       "stream js filter call \"%V\"" , &jscf->filter);

        rc = ctx->engine->call((ngx_js_ctx_t *) ctx, &jscf->filter,
                               &ctx->args[0], 1);

        if (rc == NGX_ERROR) {
            return rc;
        }
    }

    ctx->filter = 1;

    ctx->last_out = &out;

    rc = ctx->body_filter(s, ctx, in, from_upstream);
    if (rc != NGX_OK) {
        return NGX_ERROR;
    }

    ctx->buf = NULL;
    *ctx->last_out = NULL;

    return ngx_stream_js_next_filter(s, ctx, out, from_upstream);
}


static ngx_int_t
ngx_stream_js_next_filter(ngx_stream_session_t *s, ngx_stream_js_ctx_t *ctx,
     ngx_chain_t *out, ngx_uint_t from_upstream)
{
    ngx_int_t           rc;
    ngx_chain_t       **busy;
    ngx_connection_t   *c, *dst;

    c = s->connection;

    if (from_upstream) {
        dst = c;
        busy = &ctx->downstream_busy;

    } else {
        dst = s->upstream ? s->upstream->peer.connection : NULL;
        busy = &ctx->upstream_busy;
    }

    if (out != NULL || dst == NULL || dst->buffered) {
        rc = ngx_stream_next_filter(s, out, from_upstream);

        ngx_chain_update_chains(c->pool, &ctx->free, busy, &out,
                                (ngx_buf_tag_t) &ngx_stream_js_module);

    } else {
        rc = NGX_OK;
    }

    return rc;
}


static ngx_int_t
ngx_stream_js_variable_set(ngx_stream_session_t *s,
    ngx_stream_variable_value_t *v, uintptr_t data)
{
    ngx_js_set_t *vdata = (ngx_js_set_t *) data;

    ngx_int_t             rc;
    njs_int_t             pending;
    ngx_str_t            *fname, value;
    ngx_stream_js_ctx_t  *ctx;

    fname = &vdata->fname;

    rc = ngx_stream_js_init_vm(s, ngx_stream_js_session_proto_id);

    if (rc == NGX_ERROR) {
        return NGX_ERROR;
    }

    if (rc == NGX_DECLINED) {
        v->not_found = 1;
        return NGX_OK;
    }

    ngx_log_debug1(NGX_LOG_DEBUG_STREAM, s->connection->log, 0,
                   "stream js variable call \"%V\"", fname);

    ctx = ngx_stream_get_module_ctx(s, ngx_stream_js_module);

    pending = ngx_stream_pending(ctx);

    rc = ctx->engine->call((ngx_js_ctx_t *) ctx, fname, &ctx->args[0], 1);

    if (rc == NGX_ERROR) {
        v->not_found = 1;
        return NGX_OK;
    }

    if (!pending && rc == NGX_AGAIN) {
        ngx_log_error(NGX_LOG_ERR, s->connection->log, 0,
                      "async operation inside \"%V\" variable handler", fname);
        return NGX_ERROR;
    }

    if (ctx->engine->string(ctx->engine, &ctx->retval, &value) != NGX_OK) {
        return NGX_ERROR;
    }

    v->len = value.len;
    v->valid = 1;
    v->no_cacheable = vdata->flags & NGX_NJS_VAR_NOCACHE;
    v->not_found = 0;
    v->data = value.data;

    return NGX_OK;
}


static ngx_int_t
ngx_stream_js_variable_var(ngx_stream_session_t *s,
    ngx_stream_variable_value_t *v, uintptr_t data)
{
    ngx_stream_complex_value_t *cv = (ngx_stream_complex_value_t *) data;

    ngx_str_t  value;

    if (cv != NULL) {
        if (ngx_stream_complex_value(s, cv, &value) != NGX_OK) {
            return NGX_ERROR;
        }

    } else {
        ngx_str_null(&value);
    }

    v->len = value.len;
    v->valid = 1;
    v->no_cacheable = 0;
    v->not_found = 0;
    v->data = value.data;

    return NGX_OK;
}


static ngx_int_t
ngx_stream_js_init_vm(ngx_stream_session_t *s, njs_int_t proto_id)
{
    ngx_pool_cleanup_t        *cln;
    ngx_stream_js_ctx_t       *ctx;
    ngx_stream_js_srv_conf_t  *jscf;

    jscf = ngx_stream_get_module_srv_conf(s, ngx_stream_js_module);
    if (jscf->engine == NULL) {
        return NGX_DECLINED;
    }

    ctx = ngx_stream_get_module_ctx(s, ngx_stream_js_module);

    if (ctx == NULL) {
        ctx = ngx_pcalloc(s->connection->pool, sizeof(ngx_stream_js_ctx_t));
        if (ctx == NULL) {
            return NGX_ERROR;
        }

        ngx_js_ctx_init((ngx_js_ctx_t *) ctx, s->connection->log);

        ngx_stream_set_ctx(s, ctx, ngx_stream_js_module);
    }

    if (ctx->engine) {
        return NGX_OK;
    }

    ctx->engine = jscf->engine->clone((ngx_js_ctx_t *) ctx,
                                      (ngx_js_loc_conf_t *) jscf, proto_id, s);
    if (ctx->engine == NULL) {
        return NGX_ERROR;
    }

    ngx_log_debug3(NGX_LOG_DEBUG_STREAM, ctx->log, 0,
                   "stream js vm clone %s: %p from: %p", jscf->engine->name,
                   ctx->engine, jscf->engine);

    cln = ngx_pool_cleanup_add(s->connection->pool, 0);
    if (cln == NULL) {
        return NGX_ERROR;
    }

    cln->handler = ngx_stream_js_cleanup;
    cln->data = s;

    return NGX_OK;
}


static ngx_int_t
ngx_stream_js_pending_events(ngx_stream_js_ctx_t *ctx)
{
    ngx_uint_t  i;

    for (i = 0; i < NGX_JS_EVENT_MAX; i++) {
        if (ctx->events[i].data_type != NGX_JS_UNSET) {
            return 1;
        }
    }

    return 0;
}


static void
ngx_stream_js_drop_events(ngx_stream_js_ctx_t *ctx)
{
    ngx_uint_t  i;

    for (i = 0; i < NGX_JS_EVENT_MAX; i++) {
        /*
         * event[i].data_type = NGX_JS_UNSET
         * event[i].function = JS_NULL
         */
        memset(&ctx->events[i], 0, sizeof(ngx_stream_js_ev_t));
    }
}


static void
ngx_stream_js_cleanup(void *data)
{
    ngx_stream_js_ctx_t       *ctx;
    ngx_stream_js_srv_conf_t  *jscf;

    ngx_stream_session_t *s = data;

    ctx = ngx_stream_get_module_ctx(s, ngx_stream_js_module);

    if (ngx_js_ctx_pending(ctx)) {
        ngx_log_error(NGX_LOG_ERR, ctx->log, 0, "pending events");
    }

    ngx_log_debug1(NGX_LOG_DEBUG_STREAM, ctx->log, 0,
                   "stream js vm destroy: %p", ctx->engine);

    jscf = ngx_stream_get_module_srv_conf(s, ngx_stream_js_module);

    ngx_js_ctx_destroy((ngx_js_ctx_t *) ctx, (ngx_js_loc_conf_t *) jscf);
}


static ngx_int_t
ngx_stream_js_run_event(ngx_stream_session_t *s, ngx_stream_js_ctx_t *ctx,
    ngx_stream_js_ev_t *event, ngx_uint_t from_upstream)
{
    size_t             len;
    u_char            *p;
    njs_vm_t          *vm;
    njs_int_t          ret;
    ngx_str_t          exception;
    ngx_buf_t         *b;
    uintptr_t          flags;
    ngx_connection_t  *c;

    if (!njs_value_is_function(njs_value_arg(&event->function))) {
        return NGX_OK;
    }

    c = s->connection;
    b = ctx->filter ? ctx->buf : c->buffer;

    len = b ? b->last - b->pos : 0;

    vm = ctx->engine->u.njs.vm;

    p = ngx_pnalloc(c->pool, len);
    if (p == NULL) {
        njs_vm_memory_error(vm);
        goto error;
    }

    if (len) {
        ngx_memcpy(p, b->pos, len);
    }

    ret = ngx_js_prop(vm, event->data_type, njs_value_arg(&ctx->args[1]),
                      p, len);
    if (ret != NJS_OK) {
        goto error;
    }

    flags = from_upstream << 1 | (uintptr_t) (b && b->last_buf);

    ret = njs_vm_external_create(vm, njs_value_arg(&ctx->args[2]),
                       ngx_stream_js_session_flags_proto_id, (void *) flags, 0);
    if (ret != NJS_OK) {
        goto error;
    }

    ret = ngx_js_call(vm, njs_value_function(njs_value_arg(&event->function)),
                      &ctx->args[1], 2);

    if (ret == NJS_ERROR) {
error:
        ngx_js_exception(vm, &exception);

        ngx_log_error(NGX_LOG_ERR, c->log, 0, "js exception: %V",
                      &exception);

        return NGX_ERROR;
    }

    return NGX_OK;
}


static ngx_int_t
ngx_stream_njs_body_filter(ngx_stream_session_t *s, ngx_stream_js_ctx_t *ctx,
    ngx_chain_t *in, ngx_uint_t from_upstream)
{
    ngx_int_t            rc;
    ngx_chain_t         *cl;
    ngx_stream_js_ev_t  *event;

    while (in) {
        ctx->buf = in->buf;

        event = ngx_stream_event(from_upstream);

        if (njs_value_is_function(njs_value_arg(&event->function))) {
            rc = ngx_stream_js_run_event(s, ctx, event, from_upstream);
            if (rc != NGX_OK) {
                return NGX_ERROR;
            }

            ctx->buf->pos = ctx->buf->last;

        } else {
            cl = ngx_alloc_chain_link(s->connection->pool);
            if (cl == NULL) {
                return NGX_ERROR;
            }

            cl->buf = ctx->buf;

            *ctx->last_out = cl;
            ctx->last_out = &cl->next;
        }

        in = in->next;
    }

    return NGX_OK;
}


static ngx_stream_js_ev_t *
ngx_stream_js_event(ngx_stream_session_t *s, njs_str_t *event)
{
    ngx_uint_t            i, n, type;
    ngx_stream_js_ctx_t  *ctx;

    static const struct {
        ngx_str_t   name;
        ngx_uint_t  data_type;
        ngx_uint_t  id;
    } events[] = {
        {
            ngx_string("upload"),
            NGX_JS_STRING,
            NGX_JS_EVENT_UPLOAD,
        },

        {
            ngx_string("download"),
            NGX_JS_STRING,
            NGX_JS_EVENT_DOWNLOAD,
        },

        {
            ngx_string("upstream"),
            NGX_JS_BUFFER,
            NGX_JS_EVENT_UPLOAD,
        },

        {
            ngx_string("downstream"),
            NGX_JS_BUFFER,
            NGX_JS_EVENT_DOWNLOAD,
        },
    };

    ctx = ngx_stream_get_module_ctx(s, ngx_stream_js_module);

    i = 0;
    n = sizeof(events) / sizeof(events[0]);

    while (i < n) {
        if (event->length == events[i].name.len
            && ngx_memcmp(event->start, events[i].name.data, event->length)
               == 0)
        {
            break;
        }

        i++;
    }

    if (i == n) {
        njs_vm_error(ctx->engine->u.njs.vm, "unknown event \"%V\"", event);
        return NULL;
    }

    ctx->events[events[i].id].data_type = events[i].data_type;

    for (n = 0; n < NGX_JS_EVENT_MAX; n++) {
        type = ctx->events[n].data_type;
        if (type != NGX_JS_UNSET && type != events[i].data_type) {
            njs_vm_error(ctx->engine->u.njs.vm, "mixing string and buffer"
                         " events is not allowed");
            return NULL;
        }
    }

    return &ctx->events[events[i].id];
}


static njs_int_t
ngx_stream_js_ext_get_remote_address(njs_vm_t *vm,
    njs_object_prop_t *prop, njs_value_t *value, njs_value_t *setval,
    njs_value_t *retval)
{
    ngx_connection_t      *c;
    ngx_stream_session_t  *s;

    s = njs_vm_external(vm, ngx_stream_js_session_proto_id, value);
    if (s == NULL) {
        njs_value_undefined_set(retval);
        return NJS_DECLINED;
    }

    c = s->connection;

    return njs_vm_value_string_create(vm, retval, c->addr_text.data,
                                      c->addr_text.len);
}


static njs_int_t
ngx_stream_js_ext_done(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t magic, njs_value_t *retval)
{
    ngx_int_t              status;
    njs_value_t           *code;
    ngx_stream_js_ctx_t   *ctx;
    ngx_stream_session_t  *s;

    s = njs_vm_external(vm, ngx_stream_js_session_proto_id,
                        njs_argument(args, 0));
    if (s == NULL) {
        njs_vm_error(vm, "\"this\" is not an external");
        return NJS_ERROR;
    }

    status = (ngx_int_t) magic;
    status = -status;

    if (status == NGX_DONE) {
        status = NGX_STREAM_FORBIDDEN;
    }

    code = njs_arg(args, nargs, 1);

    if (!njs_value_is_undefined(code)) {
        if (ngx_js_integer(vm, code, &status) != NGX_OK) {
            return NJS_ERROR;
        }

        if (status < NGX_ABORT || status > NGX_STREAM_SERVICE_UNAVAILABLE) {
            njs_vm_error(vm, "code is out of range");
            return NJS_ERROR;
        }
    }


    ctx = ngx_stream_get_module_ctx(s, ngx_stream_js_module);

    if (ctx->filter) {
        njs_vm_error(vm, "should not be called while filtering");
        return NJS_ERROR;
    }

    ngx_log_debug1(NGX_LOG_DEBUG_STREAM, s->connection->log, 0,
                   "stream js set status: %i", status);

    ctx->status = status;

    ngx_stream_js_drop_events(ctx);

    njs_value_undefined_set(retval);

    return NJS_OK;
}


static njs_int_t
ngx_stream_js_ext_on(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t unused, njs_value_t *retval)
{
    njs_str_t              name;
    njs_value_t           *callback;
    ngx_stream_js_ev_t    *event;
    ngx_stream_session_t  *s;

    s = njs_vm_external(vm, ngx_stream_js_session_proto_id,
                        njs_argument(args, 0));
    if (s == NULL) {
        njs_vm_error(vm, "\"this\" is not an external");
        return NJS_ERROR;
    }

    if (ngx_js_string(vm, njs_arg(args, nargs, 1), &name) == NJS_ERROR) {
        njs_vm_error(vm, "failed to convert event arg");
        return NJS_ERROR;
    }

    callback = njs_arg(args, nargs, 2);
    if (!njs_value_is_function(callback)) {
        njs_vm_error(vm, "callback is not a function");
        return NJS_ERROR;
    }

    event = ngx_stream_js_event(s, &name);
    if (event == NULL) {
        return NJS_ERROR;
    }

    if (njs_value_is_function(njs_value_arg(&event->function))) {
        njs_vm_error(vm, "event handler \"%V\" is already set", &name);
        return NJS_ERROR;
    }

    njs_value_assign(&event->function, callback);

    njs_value_undefined_set(retval);

    return NJS_OK;
}


static njs_int_t
ngx_stream_js_ext_off(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t unused, njs_value_t *retval)
{
    njs_str_t              name;
    ngx_stream_js_ev_t    *event;
    ngx_stream_session_t  *s;

    s = njs_vm_external(vm, ngx_stream_js_session_proto_id,
                        njs_argument(args, 0));
    if (s == NULL) {
        njs_vm_error(vm, "\"this\" is not an external");
        return NJS_ERROR;
    }

    if (ngx_js_string(vm, njs_arg(args, nargs, 1), &name) == NJS_ERROR) {
        njs_vm_error(vm, "failed to convert event arg");
        return NJS_ERROR;
    }

    event = ngx_stream_js_event(s, &name);
    if (event == NULL) {
        return NJS_ERROR;
    }

    njs_value_null_set(njs_value_arg(&event->function));
    event->data_type = NGX_JS_UNSET;

    njs_value_undefined_set(retval);

    return NJS_OK;
}


static njs_int_t
ngx_stream_js_ext_send(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t from_upstream, njs_value_t *retval)
{
    unsigned               last_buf, flush;
    njs_str_t              buffer;
    ngx_buf_t             *b;
    njs_value_t           *flags, *value;
    ngx_chain_t           *cl;
    ngx_connection_t      *c;
    njs_opaque_value_t     lvalue;
    ngx_stream_js_ctx_t   *ctx;
    ngx_stream_session_t  *s;

    static const njs_str_t last_key = njs_str("last");
    static const njs_str_t flush_key = njs_str("flush");
    static const njs_str_t from_key = njs_str("from_upstream");

    s = njs_vm_external(vm, ngx_stream_js_session_proto_id,
                        njs_argument(args, 0));
    if (s == NULL) {
        njs_vm_error(vm, "\"this\" is not an external");
        return NJS_ERROR;
    }

    c = s->connection;

    ctx = ngx_stream_get_module_ctx(s, ngx_stream_js_module);

    if (!ctx->filter) {
        njs_vm_error(vm, "cannot send buffer in this handler");
        return NJS_ERROR;
    }

    if (ngx_js_string(vm, njs_arg(args, nargs, 1), &buffer) != NGX_OK) {
        njs_vm_error(vm, "failed to get buffer arg");
        return NJS_ERROR;
    }

    /*
     * ctx->buf != NULL when s.send() is called while processing incoming
     * data chunks, otherwise s.send() is called asynchronously
     */

    if (ctx->buf != NULL) {
        flush = ctx->buf->flush;
        last_buf = ctx->buf->last_buf;

    } else {
        flush = 0;
        last_buf = 0;
    }

    flags = njs_arg(args, nargs, 2);

    if (njs_value_is_object(flags)) {
        value = njs_vm_object_prop(vm, flags, &flush_key, &lvalue);
        if (value != NULL) {
            flush = njs_value_bool(value);
        }

        value = njs_vm_object_prop(vm, flags, &last_key, &lvalue);
        if (value != NULL) {
            last_buf = njs_value_bool(value);
        }

        if (from_upstream == NGX_JS_BOOL_UNSET) {
            value = njs_vm_object_prop(vm, flags, &from_key, &lvalue);
            if (value != NULL) {
                from_upstream = njs_value_bool(value);
            }

            if (value == NULL && ctx->buf == NULL) {
                goto exception;
            }
        }
    }

    cl = ngx_chain_get_free_buf(c->pool, &ctx->free);
    if (cl == NULL) {
        njs_vm_error(vm, "memory error");
        return NJS_ERROR;
    }

    b = cl->buf;

    b->flush = flush;
    b->last_buf = last_buf;

    b->memory = (buffer.length ? 1 : 0);
    b->sync = (buffer.length ? 0 : 1);
    b->tag = (ngx_buf_tag_t) &ngx_stream_js_module;

    b->start = buffer.start;
    b->end = buffer.start + buffer.length;
    b->pos = b->start;
    b->last = b->end;

    if (from_upstream == NGX_JS_BOOL_UNSET) {
        *ctx->last_out = cl;
        ctx->last_out = &cl->next;

    } else {

        if (ngx_stream_js_next_filter(s, ctx, cl, from_upstream) == NGX_ERROR) {
            njs_vm_error(vm, "ngx_stream_js_next_filter() failed");
            return NJS_ERROR;
        }
    }

    njs_value_undefined_set(retval);

    return NJS_OK;

exception:

    njs_vm_error(vm, "\"from_upstream\" flag is expected when"
                "called asynchronously");

    return NJS_ERROR;
}


static njs_int_t
ngx_stream_js_ext_set_return_value(njs_vm_t *vm, njs_value_t *args,
    njs_uint_t nargs, njs_index_t unused, njs_value_t *retval)
{
    ngx_stream_js_ctx_t   *ctx;
    ngx_stream_session_t  *s;

    s = njs_vm_external(vm, ngx_stream_js_session_proto_id,
                        njs_argument(args, 0));
    if (s == NULL) {
        njs_vm_error(vm, "\"this\" is not an external");
        return NJS_ERROR;
    }

    ctx = ngx_stream_get_module_ctx(s, ngx_stream_js_module);

    njs_value_assign(&ctx->retval, njs_arg(args, nargs, 1));
    njs_value_undefined_set(retval);

    return NJS_OK;
}


static njs_int_t
ngx_stream_js_session_variables(njs_vm_t *vm, njs_object_prop_t *prop,
    ngx_stream_session_t *s, njs_value_t *setval, njs_value_t *retval)
{
    njs_int_t                     rc;
    njs_str_t                     val;
    ngx_str_t                     name;
    ngx_uint_t                    key;
    ngx_stream_variable_t        *v;
    ngx_stream_core_main_conf_t  *cmcf;
    ngx_stream_variable_value_t  *vv;
    u_char                        storage[64];

    rc = njs_vm_prop_name(vm, prop, &val);
    if (rc != NJS_OK) {
        njs_value_undefined_set(retval);
        return NJS_DECLINED;
    }

    if (setval == NULL) {
        if (val.length < sizeof(storage)) {
            name.data = storage;

        } else {
            name.data = ngx_pnalloc(s->connection->pool, val.length);
            if (name.data == NULL) {
                njs_vm_error(vm, "internal error");
                return NJS_ERROR;
            }
        }

        name.len = val.length;

        key = ngx_hash_strlow(name.data, val.start, val.length);

        vv = ngx_stream_get_variable(s, &name, key);
        if (vv == NULL || vv->not_found) {
            njs_value_undefined_set(retval);
            return NJS_DECLINED;
        }

        return ngx_js_prop(vm, njs_vm_prop_magic32(prop), retval, vv->data,
                           vv->len);
    }

    cmcf = ngx_stream_get_module_main_conf(s, ngx_stream_core_module);

    if (val.length < sizeof(storage)) {
        name.data = storage;

    } else {
        name.data = ngx_pnalloc(s->connection->pool, val.length);
        if (name.data == NULL) {
            njs_vm_error(vm, "internal error");
            return NJS_ERROR;
        }
    }

    key = ngx_hash_strlow(name.data, val.start, val.length);

    v = ngx_hash_find(&cmcf->variables_hash, key, name.data, val.length);

    if (v == NULL) {
        njs_vm_error(vm, "variable not found");
        return NJS_ERROR;
    }

    if (ngx_js_string(vm, setval, &val) != NGX_OK) {
        return NJS_ERROR;
    }

    if (v->set_handler != NULL) {
        vv = ngx_pcalloc(s->connection->pool,
                         sizeof(ngx_stream_variable_value_t));
        if (vv == NULL) {
            return NJS_ERROR;
        }

        vv->valid = 1;
        vv->not_found = 0;
        vv->data = val.start;
        vv->len = val.length;

        v->set_handler(s, vv, v->data);

        return NJS_OK;
    }

    if (!(v->flags & NGX_STREAM_VAR_INDEXED)) {
        njs_vm_error(vm, "variable is not writable");
        return NJS_ERROR;
    }

    vv = &s->variables[v->index];

    vv->valid = 1;
    vv->not_found = 0;

    vv->data = ngx_pnalloc(s->connection->pool, val.length);
    if (vv->data == NULL) {
        return NJS_ERROR;
    }

    vv->len = val.length;
    ngx_memcpy(vv->data, val.start, vv->len);

    return NJS_OK;
}


static njs_int_t
ngx_stream_js_ext_variables(njs_vm_t *vm, njs_object_prop_t *prop,
    njs_value_t *value, njs_value_t *setval, njs_value_t *retval)
{
    ngx_stream_session_t  *s;

    s = njs_vm_external(vm, ngx_stream_js_session_proto_id, value);
    if (s == NULL) {
        njs_value_undefined_set(retval);
        return NJS_DECLINED;
    }

    return ngx_stream_js_session_variables(vm, prop, s, setval, retval);
}


static njs_int_t
ngx_stream_js_periodic_variables(njs_vm_t *vm, njs_object_prop_t *prop,
    njs_value_t *value, njs_value_t *setval, njs_value_t *retval)
{
    ngx_stream_session_t  *s;

    s = njs_vm_external(vm, ngx_stream_js_periodic_session_proto_id, value);
    if (s == NULL) {
        njs_value_undefined_set(retval);
        return NJS_DECLINED;
    }

    return ngx_stream_js_session_variables(vm, prop, s, setval, retval);
}


static ngx_pool_t *
ngx_stream_js_pool(ngx_stream_session_t *s)
{
    return s->connection->pool;
}


static ngx_resolver_t *
ngx_stream_js_resolver(ngx_stream_session_t *s)
{
    ngx_stream_core_srv_conf_t  *cscf;

    cscf = ngx_stream_get_module_srv_conf(s, ngx_stream_core_module);

    return cscf->resolver;
}


static ngx_msec_t
ngx_stream_js_resolver_timeout(ngx_stream_session_t *s)
{
    ngx_stream_core_srv_conf_t  *cscf;

    cscf = ngx_stream_get_module_srv_conf(s, ngx_stream_core_module);

    return cscf->resolver_timeout;
}


static ngx_msec_t
ngx_stream_js_fetch_timeout(ngx_stream_session_t *s)
{
    ngx_stream_js_srv_conf_t  *jscf;

    jscf = ngx_stream_get_module_srv_conf(s, ngx_stream_js_module);

    return jscf->timeout;
}


static size_t
ngx_stream_js_buffer_size(ngx_stream_session_t *s)
{
    ngx_stream_js_srv_conf_t  *jscf;

    jscf = ngx_stream_get_module_srv_conf(s, ngx_stream_js_module);

    return jscf->buffer_size;
}


static size_t
ngx_stream_js_max_response_buffer_size(ngx_stream_session_t *s)
{
    ngx_stream_js_srv_conf_t  *jscf;

    jscf = ngx_stream_get_module_srv_conf(s, ngx_stream_js_module);

    return jscf->max_response_body_size;
}


static void
ngx_stream_js_event_finalize(ngx_stream_session_t *s, ngx_int_t rc)
{
    ngx_log_debug1(NGX_LOG_DEBUG_STREAM, s->connection->log, 0,
                   "http js event finalize rc: %i", rc);

    if (rc == NGX_ERROR) {
        if (s->health_check) {
            ngx_stream_js_periodic_finalize(s, NGX_ERROR);
            return;
        }

        ngx_stream_finalize_session(s, NGX_STREAM_INTERNAL_SERVER_ERROR);
        return;
    }

    if (rc == NGX_OK) {
        ngx_post_event(s->connection->read, &ngx_posted_events);
    }
}


static ngx_js_ctx_t *
ngx_stream_js_ctx(ngx_stream_session_t *s)
{
    return ngx_stream_get_module_ctx(s, ngx_stream_js_module);
}


static njs_int_t
ngx_js_stream_init(njs_vm_t *vm)
{
    ngx_stream_js_session_proto_id = njs_vm_external_prototype(vm,
                                         ngx_stream_js_ext_session,
                                         njs_nitems(ngx_stream_js_ext_session));
    if (ngx_stream_js_session_proto_id < 0) {
        return NJS_ERROR;
    }

    ngx_stream_js_periodic_session_proto_id = njs_vm_external_prototype(vm,
                                ngx_stream_js_ext_periodic_session,
                                njs_nitems(ngx_stream_js_ext_periodic_session));
    if (ngx_stream_js_periodic_session_proto_id < 0) {
        return NJS_ERROR;
    }

    ngx_stream_js_session_flags_proto_id = njs_vm_external_prototype(vm,
                                   ngx_stream_js_ext_session_flags,
                                   njs_nitems(ngx_stream_js_ext_session_flags));
    if (ngx_stream_js_session_flags_proto_id < 0) {
        return NJS_ERROR;
    }

    return NJS_OK;
}


static ngx_engine_t *
ngx_engine_njs_clone(ngx_js_ctx_t *ctx, ngx_js_loc_conf_t *cf,
    njs_int_t proto_id, void *external)
{
    njs_int_t             rc;
    ngx_engine_t         *engine;
    ngx_stream_js_ctx_t  *sctx;

    engine = ngx_njs_clone(ctx, cf, external);
    if (engine == NULL) {
        return NULL;
    }

    sctx = (ngx_stream_js_ctx_t *) ctx;
    sctx->run_event = ngx_stream_js_run_event;
    sctx->body_filter = ngx_stream_njs_body_filter;

    rc = njs_vm_external_create(engine->u.njs.vm, njs_value_arg(&ctx->args[0]),
                                proto_id, njs_vm_external_ptr(engine->u.njs.vm),
                                0);
    if (rc != NJS_OK) {
        return NULL;
    }

    return engine;
}


#if (NJS_HAVE_QUICKJS)

static JSValue
ngx_stream_qjs_ext_done(JSContext *cx, JSValueConst this_val, int argc,
    JSValueConst *argv, int magic)
{
    ngx_int_t              status;
    ngx_stream_js_ctx_t   *ctx;
    ngx_stream_session_t  *s;

    s = ngx_stream_qjs_session(this_val);
    if (s == NULL) {
        return JS_ThrowInternalError(cx, "\"this\" is not a session object");
    }

    status = (ngx_int_t) magic;
    status = -status;

    if (status == NGX_DONE) {
        status = NGX_STREAM_FORBIDDEN;
    }

    if (!JS_IsUndefined(argv[0])) {
        if (ngx_qjs_integer(cx, argv[0], &status) != NGX_OK) {
            return JS_EXCEPTION;
        }

        if (status < NGX_ABORT || status > NGX_STREAM_SERVICE_UNAVAILABLE) {
            return JS_ThrowInternalError(cx, "code is out of range");
        }
    }

    ctx = ngx_stream_get_module_ctx(s, ngx_stream_js_module);

    if (ctx->filter) {
        return JS_ThrowInternalError(cx, "should not be called while "
                                     "filtering");
    }

    ngx_log_debug1(NGX_LOG_DEBUG_STREAM, s->connection->log, 0,
                   "stream js set status: %i", status);

    ctx->status = status;

    ngx_stream_js_drop_events(ctx);

    return JS_UNDEFINED;
}


static JSValue
ngx_stream_qjs_ext_log(JSContext *cx, JSValueConst this_val, int argc,
    JSValueConst *argv, int level)
{
    int                    n;
    const char            *msg;
    ngx_stream_session_t  *s;

    s = ngx_stream_qjs_session(this_val);
    if (s == NULL) {
        return JS_ThrowInternalError(cx, "\"this\" is not a session object");
    }

    for (n = 0; n < argc; n++) {
        msg = JS_ToCString(cx, argv[n]);

        ngx_js_logger(s->connection, level, (u_char *) msg, ngx_strlen(msg));

        JS_FreeCString(cx, msg);
    }

    return JS_UNDEFINED;
}


static const ngx_stream_qjs_event_t *
ngx_stream_qjs_event(ngx_stream_session_t *s, JSContext *cx, ngx_str_t *event)
{
    ngx_uint_t            i, n, type;
    ngx_stream_js_ctx_t  *ctx;

    static const ngx_stream_qjs_event_t events[] = {
        {
            ngx_string("upload"),
            NGX_JS_STRING,
            NGX_JS_EVENT_UPLOAD,
        },

        {
            ngx_string("download"),
            NGX_JS_STRING,
            NGX_JS_EVENT_DOWNLOAD,
        },

        {
            ngx_string("upstream"),
            NGX_JS_BUFFER,
            NGX_JS_EVENT_UPLOAD,
        },

        {
            ngx_string("downstream"),
            NGX_JS_BUFFER,
            NGX_JS_EVENT_DOWNLOAD,
        },
    };

    ctx = ngx_stream_get_module_ctx(s, ngx_stream_js_module);

    i = 0;
    n = sizeof(events) / sizeof(events[0]);

    while (i < n) {
        if (event->len == events[i].name.len
            && ngx_memcmp(event->data, events[i].name.data, event->len)
               == 0)
        {
            break;
        }

        i++;
    }

    if (i == n) {
        (void) JS_ThrowInternalError(cx, "unknown event \"%.*s\"",
                                     (int) event->len, event->data);
        return NULL;
    }

    ctx->events[events[i].id].data_type = events[i].data_type;

    for (n = 0; n < NGX_JS_EVENT_MAX; n++) {
        type = ctx->events[n].data_type;
        if (type != NGX_JS_UNSET && type != events[i].data_type) {
            (void) JS_ThrowInternalError(cx, "mixing string and buffer"
                                         " events is not allowed");
            return NULL;
        }
    }

    return &events[i];
}


static JSValue
ngx_stream_qjs_ext_on(JSContext *cx, JSValueConst this_val, int argc,
    JSValueConst *argv)
{
    ngx_str_t                      name;
    ngx_stream_js_ctx_t           *ctx;
    ngx_stream_qjs_session_t      *ses;
    const ngx_stream_qjs_event_t  *e;

    ses = JS_GetOpaque(this_val, NGX_QJS_CLASS_ID_STREAM_SESSION);
    if (ses == NULL) {
        return JS_ThrowInternalError(cx, "\"this\" is not a session object");
    }

    ctx = ngx_stream_get_module_ctx(ses->session, ngx_stream_js_module);

    if (ngx_qjs_string(cx, argv[0], &name) != NGX_OK) {
        return JS_EXCEPTION;
    }

    e = ngx_stream_qjs_event(ses->session, cx, &name);
    if (e == NULL) {
        return JS_EXCEPTION;
    }

    if (JS_IsFunction(cx, ngx_qjs_arg(ctx->events[e->id].function))) {
        return JS_ThrowInternalError(cx, "event handler \"%s\" is already set",
                                     name.data);
    }

    if (!JS_IsFunction(cx, argv[1])) {
        return JS_ThrowTypeError(cx, "callback is not a function");
    }

    ngx_qjs_arg(ctx->events[e->id].function) = argv[1];

    JS_FreeValue(cx, ses->callbacks[e->id]);
    ses->callbacks[e->id] = JS_DupValue(cx, argv[1]);

    return JS_UNDEFINED;
}


static JSValue
ngx_stream_qjs_ext_off(JSContext *cx, JSValueConst this_val, int argc,
    JSValueConst *argv)
{
    ngx_str_t                      name;
    ngx_stream_js_ctx_t           *ctx;
    ngx_stream_session_t          *s;
    const ngx_stream_qjs_event_t  *e;

    s = ngx_stream_qjs_session(this_val);
    if (s == NULL) {
        return JS_ThrowInternalError(cx, "\"this\" is not a session object");
    }

    ctx = ngx_stream_get_module_ctx(s, ngx_stream_js_module);

    if (ngx_qjs_string(cx, argv[0], &name) != NGX_OK) {
        return JS_EXCEPTION;
    }

    e = ngx_stream_qjs_event(s, cx, &name);
    if (e == NULL) {
        return JS_EXCEPTION;
    }

    ngx_qjs_arg(ctx->events[e->id].function) = JS_NULL;
    ctx->events[e->id].data_type = NGX_JS_UNSET;

    return JS_UNDEFINED;
}


static JSValue
ngx_stream_qjs_ext_periodic_variables(JSContext *cx,
    JSValueConst this_val, int type)
{
    JSValue                    obj;
    ngx_stream_qjs_session_t  *ses;

    ses = JS_GetOpaque(this_val, NGX_QJS_CLASS_ID_STREAM_PERIODIC);
    if (ses == NULL) {
        return JS_ThrowInternalError(cx, "\"this\" is not a periodic object");
    }

    obj = JS_NewObjectProtoClass(cx, JS_NULL, NGX_QJS_CLASS_ID_STREAM_VARS);

    /*
     * Using lowest bit of the pointer to store the buffer type.
     */
    type = (type == NGX_JS_BUFFER) ? 1 : 0;
    JS_SetOpaque(obj, (void *) ((uintptr_t) ses->session | (uintptr_t) type));

    return obj;
}


static JSValue
ngx_stream_qjs_ext_remote_address(JSContext *cx, JSValueConst this_val)
{
    ngx_connection_t      *c;
    ngx_stream_session_t  *s;

    s = ngx_stream_qjs_session(this_val);
    if (s == NULL) {
        return JS_ThrowInternalError(cx, "\"this\" is not a session object");
    }

    c = s->connection;

    return qjs_string_create(cx, c->addr_text.data, c->addr_text.len);
}


static JSValue
ngx_stream_qjs_ext_send(JSContext *cx, JSValueConst this_val, int argc,
    JSValueConst *argv, int from_upstream)
{
    JSValue                val;
    unsigned               last_buf, flush;
    ngx_str_t              buffer;
    ngx_buf_t             *b;
    ngx_chain_t           *cl;
    ngx_connection_t      *c;
    ngx_stream_js_ctx_t   *ctx;
    ngx_stream_session_t  *s;

    s = ngx_stream_qjs_session(this_val);
    if (s == NULL) {
        return JS_ThrowInternalError(cx, "\"this\" is not a session object");
    }

    c = s->connection;

    ctx = ngx_stream_get_module_ctx(s, ngx_stream_js_module);

    if (!ctx->filter) {
        return JS_ThrowInternalError(cx, "cannot send buffer in this handler");
    }

    if (ngx_qjs_string(cx, argv[0], &buffer) != NGX_OK) {
        return JS_EXCEPTION;
    }

    /*
     * ctx->buf != NULL when s.send() is called while processing incoming
     * data chunks, otherwise s.send() is called asynchronously
     */

    if (ctx->buf != NULL) {
        flush = ctx->buf->flush;
        last_buf = ctx->buf->last_buf;

    } else {
        flush = 0;
        last_buf = 0;
    }

    if (JS_IsObject(argv[1])) {
        val = JS_GetPropertyStr(cx, argv[1], "flush");
        if (JS_IsException(val)) {
            return JS_EXCEPTION;
        }

        if (!JS_IsUndefined(val)) {
            flush = JS_ToBool(cx, val);
            JS_FreeValue(cx, val);
        }

        val = JS_GetPropertyStr(cx, argv[1], "last");
        if (JS_IsException(val)) {
            return JS_EXCEPTION;
        }

        if (!JS_IsUndefined(val)) {
            last_buf = JS_ToBool(cx, val);
            JS_FreeValue(cx, val);
        }

        if (from_upstream == NGX_JS_BOOL_UNSET) {
            val = JS_GetPropertyStr(cx, argv[1], "from_upstream");
            if (JS_IsException(val)) {
                return JS_EXCEPTION;
            }

            if (!JS_IsUndefined(val)) {
                from_upstream = JS_ToBool(cx, val);
                JS_FreeValue(cx, val);
            }

            if (from_upstream == NGX_JS_BOOL_UNSET && ctx->buf == NULL) {
                return JS_ThrowInternalError(cx, "from_upstream flag is "
                                             "expected when called "
                                             "asynchronously");
            }
        }
    }

    cl = ngx_chain_get_free_buf(c->pool, &ctx->free);
    if (cl == NULL) {
        return JS_ThrowInternalError(cx, "memory error");
    }

    b = cl->buf;

    b->flush = flush;
    b->last_buf = last_buf;

    b->memory = (buffer.len ? 1 : 0);
    b->sync = (buffer.len ? 0 : 1);
    b->tag = (ngx_buf_tag_t) &ngx_stream_js_module;

    b->start = buffer.data;
    b->end = buffer.data + buffer.len;

    b->pos = b->start;
    b->last = b->end;

    if (from_upstream == NGX_JS_BOOL_UNSET) {
        *ctx->last_out = cl;
        ctx->last_out = &cl->next;

    } else {

        if (ngx_stream_js_next_filter(s, ctx, cl, from_upstream) == NGX_ERROR) {
            return JS_ThrowInternalError(cx, "ngx_stream_js_next_filter() "
                                         "failed");
        }
    }

    return JS_UNDEFINED;
}


static JSValue
ngx_stream_qjs_ext_set_return_value(JSContext *cx, JSValueConst this_val,
    int argc, JSValueConst *argv)
{
    ngx_js_ctx_t          *ctx;
    ngx_stream_session_t  *s;

    s = ngx_stream_qjs_session(this_val);
    if (s == NULL) {
        return JS_ThrowInternalError(cx, "\"this\" is not a session object");
    }

    ctx = ngx_stream_get_module_ctx(s, ngx_stream_js_module);

    JS_FreeValue(cx, ngx_qjs_arg(ctx->retval));
    ngx_qjs_arg(ctx->retval) = JS_DupValue(cx, argv[0]);

    return JS_UNDEFINED;
}


static JSValue
ngx_stream_qjs_ext_variables(JSContext *cx, JSValueConst this_val, int type)
{
    JSValue                obj;
    ngx_stream_session_t  *s;

    s = ngx_stream_qjs_session(this_val);
    if (s == NULL) {
        return JS_ThrowInternalError(cx, "\"this\" is not a session object");
    }

    obj = JS_NewObjectProtoClass(cx, JS_NULL, NGX_QJS_CLASS_ID_STREAM_VARS);

    /*
     * Using lowest bit of the pointer to store the buffer type.
     */
    type = (type == NGX_JS_BUFFER) ? 1 : 0;
    JS_SetOpaque(obj, (void *) ((uintptr_t) s | (uintptr_t) type));

    return obj;
}


static JSValue
ngx_stream_qjs_ext_uint(JSContext *cx, JSValueConst this_val, int offset)
{
    ngx_uint_t            *field;
    ngx_stream_session_t  *s;

    s = ngx_stream_qjs_session(this_val);
    if (s == NULL) {
        return JS_ThrowInternalError(cx, "\"this\" is not a session object");
    }

    field = (ngx_uint_t *) ((u_char *) s + offset);

    return JS_NewUint32(cx, *field);
}


static JSValue
ngx_stream_qjs_ext_flag(JSContext *cx, JSValueConst this_val, int mask)
{
    uintptr_t  flags;

    flags = (uintptr_t) JS_GetOpaque(this_val, NGX_QJS_CLASS_ID_STREAM_FLAGS);

    return JS_NewBool(cx, flags & mask);
}


static int
ngx_stream_qjs_variables_own_property(JSContext *cx,
    JSPropertyDescriptor *pdesc, JSValueConst obj, JSAtom prop)
{
    uint32_t                      buffer_type;
    ngx_str_t                     name, name_lc;
    ngx_uint_t                    key;
    ngx_stream_session_t         *s;
    ngx_stream_variable_value_t  *vv;
    u_char                        storage[64];

    s = JS_GetOpaque(obj, NGX_QJS_CLASS_ID_STREAM_VARS);

    buffer_type = ((uintptr_t) s & 1) ? NGX_JS_BUFFER : NGX_JS_STRING;
    s = (ngx_stream_session_t *) ((uintptr_t) s & ~(uintptr_t) 1);

    if (s == NULL) {
        (void) JS_ThrowInternalError(cx, "\"this\" is not a session object");
        return -1;
    }

    name.data = (u_char *) JS_AtomToCString(cx, prop);
    if (name.data == NULL) {
        return -1;
    }

    name.len = ngx_strlen(name.data);

    if (name.len < sizeof(storage)) {
        name_lc.data = storage;

    } else {
        name_lc.data = ngx_pnalloc(s->connection->pool, name.len);
        if (name_lc.data == NULL) {
            (void) JS_ThrowOutOfMemory(cx);
            return -1;
        }
    }

    name_lc.len = name.len;

    key = ngx_hash_strlow(name_lc.data, name.data, name.len);

    vv = ngx_stream_get_variable(s, &name_lc, key);
    JS_FreeCString(cx, (char *) name.data);
    if (vv == NULL || vv->not_found) {
        return 0;
    }

    if (pdesc != NULL) {
        pdesc->flags = JS_PROP_WRITABLE | JS_PROP_CONFIGURABLE;
        pdesc->getter = JS_UNDEFINED;
        pdesc->setter = JS_UNDEFINED;
        pdesc->value = ngx_qjs_prop(cx, buffer_type, vv->data, vv->len);
    }

    return 1;
}


static int
ngx_stream_qjs_variables_set_property(JSContext *cx, JSValueConst obj,
    JSAtom prop, JSValueConst value, JSValueConst receiver, int flags)
{
    ngx_str_t                     name, name_lc, val;
    ngx_uint_t                    key;
    ngx_stream_session_t         *s;
    ngx_stream_variable_t        *v;
    ngx_stream_variable_value_t  *vv;
    ngx_stream_core_main_conf_t  *cmcf;
    u_char                        storage[64];

    s = JS_GetOpaque(obj, NGX_QJS_CLASS_ID_STREAM_VARS);

    s = (ngx_stream_session_t *) ((uintptr_t) s & ~(uintptr_t) 1);

    if (s == NULL) {
        (void) JS_ThrowInternalError(cx, "\"this\" is not a session object");
        return -1;
    }

    name.data = (u_char *) JS_AtomToCString(cx, prop);
    if (name.data == NULL) {
        return -1;
    }

    name.len = ngx_strlen(name.data);

    if (name.len < sizeof(storage)) {
        name_lc.data = storage;

    } else {
        name_lc.data = ngx_pnalloc(s->connection->pool, name.len);
        if (name_lc.data == NULL) {
            (void) JS_ThrowOutOfMemory(cx);
            return -1;
        }
    }

    key = ngx_hash_strlow(name_lc.data, name.data, name.len);

    cmcf = ngx_stream_get_module_main_conf(s, ngx_stream_core_module);

    v = ngx_hash_find(&cmcf->variables_hash, key, name_lc.data, name.len);
    JS_FreeCString(cx, (char *) name.data);

    if (v == NULL) {
        (void) JS_ThrowInternalError(cx, "variable not found");
        return -1;
    }

    if (ngx_qjs_string(cx, value, &val) != NGX_OK) {
        return -1;
    }

    if (v->set_handler != NULL) {
        vv = ngx_pcalloc(s->connection->pool,
                         sizeof(ngx_stream_variable_value_t));
        if (vv == NULL) {
            (void) JS_ThrowOutOfMemory(cx);
            return -1;
        }

        vv->valid = 1;
        vv->not_found = 0;
        vv->data = val.data;
        vv->len = val.len;

        v->set_handler(s, vv, v->data);

        return 1;
    }

    if (!(v->flags & NGX_STREAM_VAR_INDEXED)) {
        (void) JS_ThrowTypeError(cx, "variable is not writable");
        return -1;
    }

    vv = &s->variables[v->index];

    vv->valid = 1;
    vv->not_found = 0;

    vv->data = ngx_pnalloc(s->connection->pool, val.len);
    if (vv->data == NULL) {
        vv->valid = 0;
        (void) JS_ThrowOutOfMemory(cx);
        return -1;
    }

    vv->len = val.len;
    ngx_memcpy(vv->data, val.data, vv->len);

    return 1;
}


static int
ngx_stream_qjs_variables_define_own_property(JSContext *cx,
    JSValueConst obj, JSAtom prop, JSValueConst value, JSValueConst getter,
    JSValueConst setter, int flags)
{
    if (!JS_IsUndefined(setter) || !JS_IsUndefined(getter)) {
        (void) JS_ThrowTypeError(cx, "cannot define getter or setter");
        return -1;
    }

    return ngx_stream_qjs_variables_set_property(cx, obj, prop, value, obj,
                                                 flags);
}


static ngx_int_t
ngx_stream_qjs_run_event(ngx_stream_session_t *s, ngx_stream_js_ctx_t *ctx,
    ngx_stream_js_ev_t *event, ngx_uint_t from_upstream)
{
    size_t             len;
    u_char            *p;
    JSContext         *cx;
    ngx_int_t          rc;
    ngx_str_t          exception;
    ngx_buf_t         *b;
    uintptr_t          flags;
    ngx_connection_t  *c;
    JSValue            argv[2];

    cx = ctx->engine->u.qjs.ctx;

    if (!JS_IsFunction(cx, ngx_qjs_arg(event->function))) {
        return NGX_OK;
    }

    c = s->connection;
    b = ctx->filter ? ctx->buf : c->buffer;

    len = b ? b->last - b->pos : 0;

    p = ngx_pnalloc(c->pool, len);
    if (p == NULL) {
        (void) JS_ThrowOutOfMemory(cx);
        goto error;
    }

    if (len) {
        ngx_memcpy(p, b->pos, len);
    }

    argv[0] = ngx_qjs_prop(cx, event->data_type, p, len);
    if (JS_IsException(argv[0])) {
        goto error;
    }

    argv[1] = JS_NewObjectClass(cx, NGX_QJS_CLASS_ID_STREAM_FLAGS);
    if (JS_IsException(argv[1])) {
        JS_FreeValue(cx, argv[0]);
        goto error;
    }

    flags = from_upstream << 1 | (uintptr_t) (b && b->last_buf);

    JS_SetOpaque(argv[1], (void *) flags);

    rc = ngx_qjs_call(cx, ngx_qjs_arg(event->function), &argv[0], 2);
    JS_FreeValue(cx, argv[0]);
    JS_FreeValue(cx, argv[1]);

    if (rc == NGX_ERROR) {
error:
        ngx_qjs_exception(ctx->engine, &exception);

        ngx_log_error(NGX_LOG_ERR, c->log, 0, "js exception: %V",
                      &exception);

        return NGX_ERROR;
    }

    return NGX_OK;
}


static ngx_int_t
ngx_stream_qjs_body_filter(ngx_stream_session_t *s, ngx_stream_js_ctx_t *ctx,
    ngx_chain_t *in, ngx_uint_t from_upstream)
{
    ngx_int_t            rc;
    JSContext           *cx;
    ngx_chain_t         *cl;
    ngx_stream_js_ev_t  *event;

    cx = ctx->engine->u.qjs.ctx;

    while (in != NULL) {
        ctx->buf = in->buf;

        event = ngx_stream_event(from_upstream);

        if (JS_IsFunction(cx, ngx_qjs_arg(event->function))) {
            rc = ngx_stream_qjs_run_event(s, ctx, event, from_upstream);
            if (rc != NGX_OK) {
                return NGX_ERROR;
            }

            ctx->buf->pos = ctx->buf->last;

        } else {
            cl = ngx_alloc_chain_link(s->connection->pool);
            if (cl == NULL) {
                return NGX_ERROR;
            }

            cl->buf = ctx->buf;

            *ctx->last_out = cl;
            ctx->last_out = &cl->next;
        }

        in = in->next;
    }

    return NGX_OK;
}


static ngx_stream_session_t *
ngx_stream_qjs_session(JSValueConst val)
{
    ngx_stream_qjs_session_t  *ses;

    ses = JS_GetOpaque(val, NGX_QJS_CLASS_ID_STREAM_SESSION);
    if (ses == NULL) {
        return NULL;
    }

    return ses->session;
}


static JSValue
ngx_stream_qjs_session_make(JSContext *cx, ngx_int_t proto_id,
    ngx_stream_session_t *s)
{
    JSValue                    session;
    ngx_uint_t                 i;
    ngx_stream_qjs_session_t  *ses;

    session = JS_NewObjectClass(cx, proto_id);
    if (JS_IsException(session)) {
        return JS_EXCEPTION;
    }

    ses = js_malloc(cx, sizeof(ngx_stream_qjs_session_t));
    if (ses == NULL) {
        return JS_ThrowOutOfMemory(cx);
    }

    ses->session = s;

    for (i = 0; i < NGX_JS_EVENT_MAX; i++) {
        ses->callbacks[i] = JS_UNDEFINED;
    }

    JS_SetOpaque(session, ses);

    return session;
}


static void
ngx_stream_qjs_session_finalizer(JSRuntime *rt, JSValue val)
{
    ngx_uint_t                 i;
    ngx_stream_qjs_session_t  *ses;

    ses = JS_GetOpaque(val, NGX_QJS_CLASS_ID_STREAM_SESSION);
    if (ses == NULL) {
        return;
    }

    for (i = 0; i < NGX_JS_EVENT_MAX; i++) {
        JS_FreeValueRT(rt, ses->callbacks[i]);
    }

    js_free_rt(rt, ses);
}


static void
ngx_stream_qjs_periodic_finalizer(JSRuntime *rt, JSValue val)
{
    ngx_stream_qjs_session_t  *ses;

    ses = JS_GetOpaque(val, NGX_QJS_CLASS_ID_STREAM_PERIODIC);
    if (ses == NULL) {
        return;
    }

    js_free_rt(rt, ses);
}


static ngx_engine_t *
ngx_engine_qjs_clone(ngx_js_ctx_t *ctx, ngx_js_loc_conf_t *cf,
    njs_int_t proto_id, void *external)
{
    JSValue               proto;
    JSContext            *cx;
    ngx_engine_t         *engine;
    ngx_stream_js_ctx_t  *sctx;

    engine = ngx_qjs_clone(ctx, cf, external);
    if (engine == NULL) {
        return NULL;
    }

    cx = engine->u.qjs.ctx;

    if (!JS_IsRegisteredClass(JS_GetRuntime(cx),
                              NGX_QJS_CLASS_ID_STREAM_SESSION))
    {
        if (JS_NewClass(JS_GetRuntime(cx), NGX_QJS_CLASS_ID_STREAM_SESSION,
                        &ngx_stream_qjs_session_class) < 0)
        {
            return NULL;
        }

        proto = JS_NewObject(cx);
        if (JS_IsException(proto)) {
            return NULL;
        }

        JS_SetPropertyFunctionList(cx, proto, ngx_stream_qjs_ext_session,
                                   njs_nitems(ngx_stream_qjs_ext_session));

        JS_SetClassProto(cx, NGX_QJS_CLASS_ID_STREAM_SESSION, proto);

        if (JS_NewClass(JS_GetRuntime(cx), NGX_QJS_CLASS_ID_STREAM_PERIODIC,
                        &ngx_stream_qjs_periodic_class) < 0)
        {
            return NULL;
        }

        proto = JS_NewObject(cx);
        if (JS_IsException(proto)) {
            return NULL;
        }

        JS_SetPropertyFunctionList(cx, proto, ngx_stream_qjs_ext_periodic,
                                   njs_nitems(ngx_stream_qjs_ext_periodic));

        JS_SetClassProto(cx, NGX_QJS_CLASS_ID_STREAM_PERIODIC, proto);

        if (JS_NewClass(JS_GetRuntime(cx), NGX_QJS_CLASS_ID_STREAM_FLAGS,
                        &ngx_stream_qjs_flags_class) < 0)
        {
            return NULL;
        }

        proto = JS_NewObject(cx);
        if (JS_IsException(proto)) {
            return NULL;
        }

        JS_SetPropertyFunctionList(cx, proto, ngx_stream_qjs_ext_flags,
                                   njs_nitems(ngx_stream_qjs_ext_flags));

        JS_SetClassProto(cx, NGX_QJS_CLASS_ID_STREAM_FLAGS, proto);

        if (JS_NewClass(JS_GetRuntime(cx), NGX_QJS_CLASS_ID_STREAM_VARS,
                        &ngx_stream_qjs_variables_class) < 0)
        {
            return NULL;
        }
    }

    sctx = (ngx_stream_js_ctx_t *) ctx;
    sctx->run_event = ngx_stream_qjs_run_event;
    sctx->body_filter = ngx_stream_qjs_body_filter;

    if (proto_id == ngx_stream_js_session_proto_id) {
        proto_id = NGX_QJS_CLASS_ID_STREAM_SESSION;

    } else if (proto_id == ngx_stream_js_periodic_session_proto_id) {
        proto_id = NGX_QJS_CLASS_ID_STREAM_PERIODIC;
    }

    ngx_qjs_arg(ctx->args[0]) = ngx_stream_qjs_session_make(cx, proto_id,
                                                            external);
    if (JS_IsException(ngx_qjs_arg(ctx->args[0]))) {
        return NULL;
    }

    return engine;
}


static void
ngx_stream_qjs_destroy(ngx_engine_t *e, ngx_js_ctx_t *ctx,
    ngx_js_loc_conf_t *conf)
{
    ngx_uint_t                 i;
    JSValue                    cb;
    ngx_stream_qjs_session_t  *ses;

    if (ctx != NULL) {
        /*
         * explicitly freeing the callback functions
         * to avoid circular references with the session object.
         */
        ses = JS_GetOpaque(ngx_qjs_arg(ctx->args[0]),
                           NGX_QJS_CLASS_ID_STREAM_SESSION);
        if (ses != NULL) {
            for (i = 0; i < NGX_JS_EVENT_MAX; i++) {
                cb = ses->callbacks[i];
                ses->callbacks[i] = JS_UNDEFINED;
                JS_FreeValue(e->u.qjs.ctx, cb);
            }
        }
    }

    ngx_engine_qjs_destroy(e, ctx, conf);
}

#endif


static ngx_int_t
ngx_stream_js_init_conf_vm(ngx_conf_t *cf, ngx_js_loc_conf_t *conf)
{
    ngx_engine_opts_t    options;
    ngx_js_main_conf_t  *jmcf;

    memset(&options, 0, sizeof(ngx_engine_opts_t));

    options.engine = conf->type;

    jmcf = ngx_stream_conf_get_module_main_conf(cf, ngx_stream_js_module);
    ngx_stream_js_uptr[NGX_JS_MAIN_CONF_INDEX] = (uintptr_t) jmcf;

    if (conf->type == NGX_ENGINE_NJS) {
        options.u.njs.metas = &ngx_stream_js_metas;
        options.u.njs.addons = njs_stream_js_addon_modules;
        options.clone = ngx_engine_njs_clone;
    }

#if (NJS_HAVE_QUICKJS)
    else if (conf->type == NGX_ENGINE_QJS) {
        options.u.qjs.metas = ngx_stream_js_uptr;
        options.u.qjs.addons = njs_stream_qjs_addon_modules;
        options.clone = ngx_engine_qjs_clone;
        options.destroy = ngx_stream_qjs_destroy;
    }
#endif

    return ngx_js_init_conf_vm(cf, conf, &options);
}


static void
ngx_stream_js_periodic_handler(ngx_event_t *ev)
{
    ngx_int_t                     rc;
    ngx_msec_t                    timer;
    ngx_js_periodic_t            *periodic;
    ngx_connection_t             *c;
    ngx_stream_js_ctx_t          *ctx;
    ngx_stream_session_t         *s;
    ngx_stream_core_main_conf_t  *cmcf;

    if (ngx_terminate || ngx_exiting) {
        return;
    }

    periodic = ev->data;

    timer = periodic->interval;

    if (periodic->jitter) {
        timer += (ngx_msec_t) ngx_random() % periodic->jitter;
    }

    ngx_add_timer(&periodic->event, timer);

    c = periodic->connection;

    if (c != NULL) {
        ngx_log_error(NGX_LOG_ERR, c->log, 0,
                      "stream js periodic \"%V\" is already running, killing "
                      "previous instance", &periodic->method);

        ngx_stream_js_periodic_finalize(c->data, NGX_ERROR);
    }

    ngx_log_debug1(NGX_LOG_DEBUG_STREAM, &periodic->log, 0,
                   "stream js periodic handler: \"%V\"", &periodic->method);

    c = ngx_get_connection(0, &periodic->log);

    if (c == NULL) {
        return;
    }

    c->pool = ngx_create_pool(1024, c->log);
    if (c->pool == NULL) {
        goto free_connection;
    }

    s = ngx_pcalloc(c->pool, sizeof(ngx_stream_session_t));
    if (s == NULL) {
        goto free_pool;
    }

    s->main_conf = periodic->conf_ctx->main_conf;
    s->srv_conf = periodic->conf_ctx->srv_conf;

    s->ctx = ngx_pcalloc(c->pool, sizeof(void *) * ngx_stream_max_module);
    if (s->ctx == NULL) {
        goto free_pool;
    }

    cmcf = ngx_stream_get_module_main_conf(s, ngx_stream_core_module);

    s->variables = ngx_pcalloc(c->pool, cmcf->variables.nelts
                                        * sizeof(ngx_stream_variable_value_t));
    if (s->variables == NULL) {
        goto free_pool;
    }

    c->data = s;
    c->destroyed = 0;
    c->read->log = &periodic->log;
    c->read->handler = ngx_stream_js_periodic_event_handler;

    s->received = 1;
    s->connection = c;
    s->signature = NGX_STREAM_MODULE;

    s->health_check = 1;

    rc = ngx_stream_js_init_vm(s, ngx_stream_js_periodic_session_proto_id);

    if (rc != NGX_OK) {
        ngx_stream_js_periodic_destroy(s, periodic);
        return;
    }

    periodic->connection = c;

    ctx = ngx_stream_get_module_ctx(s, ngx_stream_js_module);

    ctx->periodic = periodic;

    s->received++;

    rc = ctx->engine->call((ngx_js_ctx_t *) ctx, &periodic->method,
                           &ctx->args[0], 1);

    if (rc == NGX_AGAIN) {
        rc = NGX_OK;
    }

    s->received--;

    ngx_stream_js_periodic_finalize(s, rc);

    return;

free_pool:

    ngx_destroy_pool(c->pool);

free_connection:

    ngx_close_connection(c);
}


static void
ngx_stream_js_periodic_event_handler(ngx_event_t *ev)
{
    ngx_connection_t      *c;
    ngx_stream_js_ctx_t   *ctx;
    ngx_stream_session_t  *s;

    c = ev->data;

    ngx_log_debug0(NGX_LOG_DEBUG_STREAM, c->log, 0,
                   "stream js periodic event handler");

    if (c->close) {
        ngx_stream_js_periodic_finalize(c->data, NGX_ERROR);
        return;
    }

    s = c->data;

    ctx = ngx_stream_get_module_ctx(s, ngx_stream_js_module);

    if (!ngx_js_ctx_pending(ctx)) {
        ngx_stream_js_periodic_finalize(s, NGX_OK);
        return;
    }
}


static void
ngx_stream_js_periodic_finalize(ngx_stream_session_t *s, ngx_int_t rc)
{
    ngx_stream_js_ctx_t  *ctx;

    ctx = ngx_stream_get_module_ctx(s, ngx_stream_js_module);

    ngx_log_debug4(NGX_LOG_DEBUG_STREAM, s->connection->log, 0,
                   "stream js periodic finalize: \"%V\" rc: %i c: %i "
                   "pending: %i", &ctx->periodic->method, rc, s->received,
                   ngx_js_ctx_pending(ctx));

    if (s->received > 1 || (rc == NGX_OK && ngx_js_ctx_pending(ctx))) {
        return;
    }

    ngx_stream_js_periodic_destroy(s, ctx->periodic);
}


static void
ngx_stream_js_periodic_destroy(ngx_stream_session_t *s,
    ngx_js_periodic_t *periodic)
{
    ngx_connection_t  *c;

    c = s->connection;

    ngx_log_debug1(NGX_LOG_DEBUG_STREAM, c->log, 0,
                   "stream js periodic request destroy: \"%V\"",
                   &periodic->method);

    periodic->connection = NULL;

    ngx_free_connection(c);

    ngx_destroy_pool(c->pool);

    c->fd = (ngx_socket_t) -1;
    c->pool = NULL;
    c->destroyed = 1;

    if (c->read->posted) {
        ngx_delete_posted_event(c->read);
    }
}


static ngx_int_t
ngx_stream_js_periodic_init(ngx_js_periodic_t *periodic)
{
    ngx_log_t                   *log;
    ngx_msec_t                   jitter;
    ngx_stream_core_srv_conf_t  *cscf;

    cscf = ngx_stream_get_module_srv_conf(periodic->conf_ctx,
                                          ngx_stream_core_module);
    log = cscf->error_log;

    ngx_memcpy(&periodic->log, log, sizeof(ngx_log_t));

    periodic->connection = NULL;

    periodic->event.handler = ngx_stream_js_periodic_handler;
    periodic->event.data = periodic;
    periodic->event.log = log;
    periodic->event.cancelable = 1;

    jitter = periodic->jitter ? (ngx_msec_t) ngx_random() % periodic->jitter
                              : 0;
    ngx_add_timer(&periodic->event, jitter + 1);

    return NGX_OK;
}


static ngx_int_t
ngx_stream_js_init_worker(ngx_cycle_t *cycle)
{
    ngx_uint_t           i;
    ngx_js_periodic_t   *periodics;
    ngx_js_main_conf_t  *jmcf;

    if ((ngx_process != NGX_PROCESS_WORKER)
        && ngx_process != NGX_PROCESS_SINGLE)
    {
        return NGX_OK;
    }

    jmcf = ngx_stream_cycle_get_module_main_conf(cycle, ngx_stream_js_module);

    if (jmcf == NULL || jmcf->periodics == NULL) {
        return NGX_OK;
    }

    periodics = jmcf->periodics->elts;

    for (i = 0; i < jmcf->periodics->nelts; i++) {
        if (periodics[i].worker_affinity != NULL
            && !periodics[i].worker_affinity[ngx_worker])
        {
            continue;
        }

        if (periodics[i].worker_affinity == NULL && ngx_worker != 0) {
            continue;
        }

        periodics[i].fd = 1000000 + i;

        if (ngx_stream_js_periodic_init(&periodics[i]) != NGX_OK) {
            return NGX_ERROR;
        }
    }

    return NGX_OK;
}


static char *
ngx_stream_js_periodic(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    uint8_t             *mask;
    ngx_str_t           *value, s;
    ngx_msec_t           interval, jitter;
    ngx_uint_t           i;
    ngx_core_conf_t     *ccf;
    ngx_js_periodic_t   *periodic;
    ngx_js_main_conf_t  *jmcf;

    if (cf->args->nelts < 2) {
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0, "method name is required");
        return NGX_CONF_ERROR;
    }

    jmcf = ngx_stream_conf_get_module_main_conf(cf, ngx_stream_js_module);

    if (jmcf->periodics == NULL) {
        jmcf->periodics = ngx_array_create(cf->pool, 1,
                                           sizeof(ngx_js_periodic_t));
        if (jmcf->periodics == NULL) {
            return NGX_CONF_ERROR;
        }
    }

    periodic = ngx_array_push(jmcf->periodics);
    if (periodic == NULL) {
        return NGX_CONF_ERROR;
    }

    ngx_memzero(periodic, sizeof(ngx_js_periodic_t));

    mask = NULL;
    jitter = 0;
    interval = 5000;

    value = cf->args->elts;

    for (i = 2; i < cf->args->nelts; i++) {

        if (ngx_strncmp(value[i].data, "interval=", 9) == 0) {
            s.len = value[i].len - 9;
            s.data = value[i].data + 9;

            interval = ngx_parse_time(&s, 0);

            if (interval == (ngx_msec_t) NGX_ERROR || interval == 0) {
                goto invalid;
            }

            continue;
        }

        if (ngx_strncmp(value[i].data, "jitter=", 7) == 0) {
            s.len = value[i].len - 7;
            s.data = value[i].data + 7;

            jitter = ngx_parse_time(&s, 0);

            if (jitter == (ngx_msec_t) NGX_ERROR) {
                goto invalid;
            }

            continue;
        }

        if (ngx_strncmp(value[i].data, "worker_affinity=", 16) == 0) {
            s.len = value[i].len - 16;
            s.data = value[i].data + 16;

            ccf = (ngx_core_conf_t *) ngx_get_conf(cf->cycle->conf_ctx,
                                                   ngx_core_module);

            if (ccf->worker_processes == NGX_CONF_UNSET) {
                ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                                   "\"worker_affinity\" is not supported "
                                   "with unset \"worker_processes\" directive");
                return NGX_CONF_ERROR;
            }

            mask = ngx_palloc(cf->pool, ccf->worker_processes);
            if (mask == NULL) {
                return NGX_CONF_ERROR;
            }

            if (ngx_strncmp(s.data, "all", 3) == 0) {
                memset(mask, 1, ccf->worker_processes);
                continue;
            }

            if ((size_t) ccf->worker_processes != s.len) {
                ngx_conf_log_error(NGX_LOG_EMERG, cf, 0, "the number of "
                                   "\"worker_processes\" is not equal to the "
                                   "size of \"worker_affinity\" mask");
                return NGX_CONF_ERROR;
            }

            for (i = 0; i < s.len; i++) {
                if (s.data[i] == '0') {
                    mask[i] = 0;
                    continue;
                }

                if (s.data[i] == '1') {
                    mask[i] = 1;
                    continue;
                }

                ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                          "invalid character \"%c\" in \"worker_affinity=\"",
                          s.data[i]);

                return NGX_CONF_ERROR;
            }

            continue;
        }

invalid:

        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                           "invalid parameter \"%V\"", &value[i]);
        return NGX_CONF_ERROR;
    }

    periodic->method = value[1];
    periodic->interval = interval;
    periodic->jitter = jitter;
    periodic->worker_affinity = mask;
    periodic->conf_ctx = cf->ctx;

    return NGX_CONF_OK;
}

static char *
ngx_stream_js_set(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    ngx_str_t              *value;
    ngx_js_set_t           *data, *prev;
    ngx_stream_variable_t  *v;

    value = cf->args->elts;

    if (value[1].data[0] != '$') {
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                           "invalid variable name \"%V\"", &value[1]);
        return NGX_CONF_ERROR;
    }

    value[1].len--;
    value[1].data++;

    v = ngx_stream_add_variable(cf, &value[1], NGX_STREAM_VAR_CHANGEABLE);
    if (v == NULL) {
        return NGX_CONF_ERROR;
    }

    data = ngx_palloc(cf->pool, sizeof(ngx_js_set_t));
    if (data == NULL) {
        return NGX_CONF_ERROR;
    }

    data->fname = value[2];

    if (v->get_handler == ngx_stream_js_variable_set) {
        prev = (ngx_js_set_t *) v->data;

        if (data->fname.len != prev->fname.len
            || ngx_strncmp(data->fname.data, prev->fname.data, data->fname.len) != 0)
        {
            ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                               "variable \"%V\" is redeclared with "
                               "different function name", &value[1]);
            return NGX_CONF_ERROR;
        }
    }

    if (cf->args->nelts == 4) {
        if (ngx_strcmp(value[3].data, "nocache") == 0) {
            data->flags |= NGX_NJS_VAR_NOCACHE;

        } else {
            ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                "unrecognized flag \"%V\"", &value[3]);
            return NGX_CONF_ERROR;
        }
    }

    v->get_handler = ngx_stream_js_variable_set;
    v->data = (uintptr_t) data;

    return NGX_CONF_OK;
}


static char *
ngx_stream_js_var(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    ngx_str_t                           *value;
    ngx_int_t                            index;
    ngx_stream_variable_t               *v;
    ngx_stream_complex_value_t          *cv;
    ngx_stream_compile_complex_value_t   ccv;

    value = cf->args->elts;

    if (value[1].data[0] != '$') {
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                           "invalid variable name \"%V\"", &value[1]);
        return NGX_CONF_ERROR;
    }

    value[1].len--;
    value[1].data++;

    v = ngx_stream_add_variable(cf, &value[1], NGX_STREAM_VAR_CHANGEABLE);
    if (v == NULL) {
        return NGX_CONF_ERROR;
    }

    index = ngx_stream_get_variable_index(cf, &value[1]);
    if (index == NGX_ERROR) {
        return NGX_CONF_ERROR;
    }

    cv = NULL;

    if (cf->args->nelts == 3) {
        cv = ngx_palloc(cf->pool, sizeof(ngx_stream_complex_value_t));
        if (cv == NULL) {
            return NGX_CONF_ERROR;
        }

        ngx_memzero(&ccv, sizeof(ngx_stream_compile_complex_value_t));

        ccv.cf = cf;
        ccv.value = &value[2];
        ccv.complex_value = cv;

        if (ngx_stream_compile_complex_value(&ccv) != NGX_OK) {
            return NGX_CONF_ERROR;
        }
    }

    v->get_handler = ngx_stream_js_variable_var;
    v->data = (uintptr_t) cv;

    return NGX_CONF_OK;
}


static void *
ngx_stream_js_create_main_conf(ngx_conf_t *cf)
{
    ngx_js_main_conf_t  *jmcf;

    jmcf = ngx_pcalloc(cf->pool, sizeof(ngx_js_main_conf_t));
    if (jmcf == NULL) {
        return NULL;
    }

    /*
     * set by ngx_pcalloc():
     *
     *     jmcf->dicts = NULL;
     *     jmcf->periodics = NULL;
     */

    return jmcf;
}


static void *
ngx_stream_js_create_srv_conf(ngx_conf_t *cf)
{
    ngx_stream_js_srv_conf_t  *conf =
        (ngx_stream_js_srv_conf_t *) ngx_js_create_conf(
                                          cf, sizeof(ngx_stream_js_srv_conf_t));
    if (conf == NULL) {
        return NULL;
    }

#if (NGX_STREAM_SSL)
    conf->ssl_verify = NGX_CONF_UNSET;
    conf->ssl_verify_depth = NGX_CONF_UNSET;
#endif
    return conf;
}


static char *
ngx_stream_js_merge_srv_conf(ngx_conf_t *cf, void *parent, void *child)
{
    ngx_stream_js_srv_conf_t *prev = parent;
    ngx_stream_js_srv_conf_t *conf = child;

    ngx_conf_merge_str_value(conf->access, prev->access, "");
    ngx_conf_merge_str_value(conf->preread, prev->preread, "");
    ngx_conf_merge_str_value(conf->filter, prev->filter, "");

    return ngx_js_merge_conf(cf, parent, child, ngx_stream_js_init_conf_vm);
}


static char *
ngx_stream_js_shared_dict_zone(ngx_conf_t *cf, ngx_command_t *cmd,
    void *conf)
{
    return ngx_js_shared_dict_zone(cf, cmd, conf, &ngx_stream_js_module);
}


static ngx_int_t
ngx_stream_js_init(ngx_conf_t *cf)
{
    ngx_stream_handler_pt        *h;
    ngx_stream_core_main_conf_t  *cmcf;

    ngx_stream_next_filter = ngx_stream_top_filter;
    ngx_stream_top_filter = ngx_stream_js_body_filter;

    cmcf = ngx_stream_conf_get_module_main_conf(cf, ngx_stream_core_module);

    h = ngx_array_push(&cmcf->phases[NGX_STREAM_ACCESS_PHASE].handlers);
    if (h == NULL) {
        return NGX_ERROR;
    }

    *h = ngx_stream_js_access_handler;

    h = ngx_array_push(&cmcf->phases[NGX_STREAM_PREREAD_PHASE].handlers);
    if (h == NULL) {
        return NGX_ERROR;
    }

    *h = ngx_stream_js_preread_handler;

    return NGX_OK;
}


static ngx_ssl_t *
ngx_stream_js_ssl(ngx_stream_session_t *s)
{
#if (NGX_STREAM_SSL)
    ngx_stream_js_srv_conf_t  *jscf;

    jscf = ngx_stream_get_module_srv_conf(s, ngx_stream_js_module);

    return jscf->ssl;
#else
    return NULL;
#endif
}


static ngx_flag_t
ngx_stream_js_ssl_verify(ngx_stream_session_t *s)
{
#if (NGX_STREAM_SSL)
    ngx_stream_js_srv_conf_t  *jscf;

    jscf = ngx_stream_get_module_srv_conf(s, ngx_stream_js_module);

    return jscf->ssl_verify;
#else
    return 0;
#endif
}
