
/*
 * Copyright (C) Roman Arutyunyan
 * Copyright (C) Dmitry Volyntsev
 * Copyright (C) NGINX, Inc.
 */


#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_stream.h>
#include "ngx_js.h"


typedef struct {
    ngx_str_t              name;
    ngx_str_t              path;
    u_char                *file;
    ngx_uint_t             line;
} ngx_stream_js_import_t;


typedef struct {
    njs_vm_t              *vm;
    ngx_array_t           *imports;
    ngx_array_t           *paths;

    ngx_str_t              access;
    ngx_str_t              preread;
    ngx_str_t              filter;

    size_t                 buffer_size;
    size_t                 max_response_body_size;
    ngx_msec_t             timeout;

#if (NGX_STREAM_SSL)
    ngx_ssl_t             *ssl;
    ngx_str_t              ssl_ciphers;
    ngx_flag_t             ssl_verify;
    ngx_uint_t             ssl_protocols;
    ngx_int_t              ssl_verify_depth;
    ngx_str_t              ssl_trusted_certificate;
#endif
} ngx_stream_js_srv_conf_t;


typedef struct {
    njs_vm_event_t          ev;
    ngx_uint_t              data_type;
} ngx_stream_js_ev_t;


typedef struct {
    njs_vm_t               *vm;
    njs_opaque_value_t      retval;
    njs_opaque_value_t      args[3];
    ngx_buf_t              *buf;
    ngx_chain_t           **last_out;
    ngx_chain_t            *free;
    ngx_chain_t            *upstream_busy;
    ngx_chain_t            *downstream_busy;
    ngx_int_t               status;
#define NGX_JS_EVENT_UPLOAD   0
#define NGX_JS_EVENT_DOWNLOAD 1
#define NGX_JS_EVENT_MAX      2
    ngx_stream_js_ev_t      events[2];
    unsigned                filter:1;
    unsigned                in_progress:1;
} ngx_stream_js_ctx_t;


typedef struct {
    ngx_stream_session_t  *session;
    njs_vm_event_t         vm_event;
    void                  *unused;
    ngx_int_t              ident;
} ngx_stream_js_event_t;


static ngx_int_t ngx_stream_js_access_handler(ngx_stream_session_t *s);
static ngx_int_t ngx_stream_js_preread_handler(ngx_stream_session_t *s);
static ngx_int_t ngx_stream_js_phase_handler(ngx_stream_session_t *s,
    ngx_str_t *name);
static ngx_int_t ngx_stream_js_body_filter(ngx_stream_session_t *s,
    ngx_chain_t *in, ngx_uint_t from_upstream);
static ngx_int_t ngx_stream_js_next_filter(ngx_stream_session_t *s,
    ngx_stream_js_ctx_t *ctx, ngx_chain_t *out, ngx_uint_t from_upstream);
static ngx_int_t ngx_stream_js_variable_set(ngx_stream_session_t *s,
    ngx_stream_variable_value_t *v, uintptr_t data);
static ngx_int_t ngx_stream_js_variable_var(ngx_stream_session_t *s,
    ngx_stream_variable_value_t *v, uintptr_t data);
static ngx_int_t ngx_stream_js_init_vm(ngx_stream_session_t *s);
static void ngx_stream_js_drop_events(ngx_stream_js_ctx_t *ctx);
static void ngx_stream_js_cleanup(void *data);
static void ngx_stream_js_cleanup_vm(void *data);
static njs_int_t ngx_stream_js_run_event(ngx_stream_session_t *s,
    ngx_stream_js_ctx_t *ctx, ngx_stream_js_ev_t *event,
    ngx_uint_t from_upstream);
static njs_vm_event_t *ngx_stream_js_event(ngx_stream_session_t *s,
    njs_str_t *event);

static njs_int_t ngx_stream_js_ext_get_remote_address(njs_vm_t *vm,
    njs_object_prop_t *prop, njs_value_t *value, njs_value_t *setval,
    njs_value_t *retval);

static njs_int_t ngx_stream_js_ext_done(njs_vm_t *vm, njs_value_t *args,
     njs_uint_t nargs, njs_index_t unused);

static njs_int_t ngx_stream_js_ext_on(njs_vm_t *vm, njs_value_t *args,
     njs_uint_t nargs, njs_index_t unused);
static njs_int_t ngx_stream_js_ext_off(njs_vm_t *vm, njs_value_t *args,
     njs_uint_t nargs, njs_index_t unused);
static njs_int_t ngx_stream_js_ext_send(njs_vm_t *vm, njs_value_t *args,
     njs_uint_t nargs, njs_index_t unused);
static njs_int_t ngx_stream_js_ext_set_return_value(njs_vm_t *vm,
    njs_value_t *args, njs_uint_t nargs, njs_index_t unused);

static njs_int_t ngx_stream_js_ext_variables(njs_vm_t *vm,
    njs_object_prop_t *prop, njs_value_t *value, njs_value_t *setval,
    njs_value_t *retval);

static njs_host_event_t ngx_stream_js_set_timer(njs_external_ptr_t external,
    uint64_t delay, njs_vm_event_t vm_event);
static void ngx_stream_js_clear_timer(njs_external_ptr_t external,
    njs_host_event_t event);
static void ngx_stream_js_timer_handler(ngx_event_t *ev);
static ngx_pool_t *ngx_stream_js_pool(njs_vm_t *vm, ngx_stream_session_t *s);
static ngx_resolver_t *ngx_stream_js_resolver(njs_vm_t *vm,
    ngx_stream_session_t *s);
static ngx_msec_t ngx_stream_js_resolver_timeout(njs_vm_t *vm,
    ngx_stream_session_t *s);
static ngx_msec_t ngx_stream_js_fetch_timeout(njs_vm_t *vm,
    ngx_stream_session_t *s);
static size_t ngx_stream_js_buffer_size(njs_vm_t *vm, ngx_stream_session_t *s);
static size_t ngx_stream_js_max_response_buffer_size(njs_vm_t *vm,
    ngx_stream_session_t *s);
static void ngx_stream_js_handle_event(ngx_stream_session_t *s,
    njs_vm_event_t vm_event, njs_value_t *args, njs_uint_t nargs);

static char *ngx_stream_js_import(ngx_conf_t *cf, ngx_command_t *cmd,
    void *conf);
static char *ngx_stream_js_set(ngx_conf_t *cf, ngx_command_t *cmd,
    void *conf);
static char *ngx_stream_js_var(ngx_conf_t *cf, ngx_command_t *cmd,
    void *conf);
static ngx_int_t ngx_stream_js_merge_vm(ngx_conf_t *cf,
    ngx_stream_js_srv_conf_t *conf, ngx_stream_js_srv_conf_t *prev);
static ngx_int_t ngx_stream_js_init_conf_vm(ngx_conf_t *cf,
    ngx_stream_js_srv_conf_t *conf);
static void *ngx_stream_js_create_srv_conf(ngx_conf_t *cf);
static char *ngx_stream_js_merge_srv_conf(ngx_conf_t *cf, void *parent,
    void *child);
static ngx_int_t ngx_stream_js_init(ngx_conf_t *cf);

#if (NGX_STREAM_SSL)
static char * ngx_stream_js_set_ssl(ngx_conf_t *cf,
    ngx_stream_js_srv_conf_t *jscf);
#endif
static ngx_ssl_t *ngx_stream_js_ssl(njs_vm_t *vm, ngx_stream_session_t *s);
static ngx_flag_t ngx_stream_js_ssl_verify(njs_vm_t *vm,
    ngx_stream_session_t *s);

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

    { ngx_string("js_import"),
      NGX_STREAM_MAIN_CONF|NGX_STREAM_SRV_CONF|NGX_CONF_TAKE13,
      ngx_stream_js_import,
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
      NGX_STREAM_MAIN_CONF|NGX_STREAM_SRV_CONF|NGX_CONF_TAKE2,
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

      ngx_null_command
};


static ngx_stream_module_t  ngx_stream_js_module_ctx = {
    NULL,                           /* preconfiguration */
    ngx_stream_js_init,             /* postconfiguration */

    NULL,                           /* create main configuration */
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
    NULL,                           /* init process */
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


static njs_vm_ops_t ngx_stream_js_ops = {
    ngx_stream_js_set_timer,
    ngx_stream_js_clear_timer,
    NULL,
    ngx_js_logger,
};


static uintptr_t ngx_stream_js_uptr[] = {
    offsetof(ngx_stream_session_t, connection),
    (uintptr_t) ngx_stream_js_pool,
    (uintptr_t) ngx_stream_js_resolver,
    (uintptr_t) ngx_stream_js_resolver_timeout,
    (uintptr_t) ngx_stream_js_handle_event,
    (uintptr_t) ngx_stream_js_ssl,
    (uintptr_t) ngx_stream_js_ssl_verify,
    (uintptr_t) ngx_stream_js_fetch_timeout,
    (uintptr_t) ngx_stream_js_buffer_size,
    (uintptr_t) ngx_stream_js_max_response_buffer_size,
};


static njs_vm_meta_t ngx_stream_js_metas = {
    .size = njs_nitems(ngx_stream_js_uptr),
    .values = ngx_stream_js_uptr
};


static ngx_stream_filter_pt  ngx_stream_next_filter;


static njs_int_t    ngx_stream_js_session_proto_id;
static njs_int_t    ngx_stream_js_session_flags_proto_id;


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
    ngx_str_t             exception;
    njs_int_t             ret;
    ngx_int_t             rc;
    ngx_connection_t     *c;
    ngx_stream_js_ctx_t  *ctx;

    if (name->len == 0) {
        return NGX_DECLINED;
    }

    rc = ngx_stream_js_init_vm(s);
    if (rc != NGX_OK) {
        return rc;
    }

    c = s->connection;

    ngx_log_debug1(NGX_LOG_DEBUG_STREAM, c->log, 0,
                   "stream js phase call \"%V\"", name);

    ctx = ngx_stream_get_module_ctx(s, ngx_stream_js_module);

    if (!ctx->in_progress) {
        /*
         * status is expected to be overriden by allow(), deny(), decline() or
         * done() methods.
         */

        ctx->status = NGX_ERROR;

        rc = ngx_js_call(ctx->vm, name, c->log, &ctx->args[0], 1);

        if (rc == NGX_ERROR) {
            return rc;
        }
    }

    ret = ngx_stream_js_run_event(s, ctx, &ctx->events[NGX_JS_EVENT_UPLOAD], 0);
    if (ret != NJS_OK) {
        ngx_js_retval(ctx->vm, NULL, &exception);

        ngx_log_error(NGX_LOG_ERR, c->log, 0, "js exception: %V",
                      &exception);

        return NGX_ERROR;
    }

    if (njs_vm_pending(ctx->vm)) {
        ctx->in_progress = 1;
        rc = ctx->events[NGX_JS_EVENT_UPLOAD].ev ? NGX_AGAIN : NGX_DONE;

    } else {
        ctx->in_progress = 0;
        rc = ctx->status;
    }

    ngx_log_debug1(NGX_LOG_DEBUG_STREAM, c->log, 0, "stream js phase rc: %i",
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
    ngx_str_t                  exception;
    njs_int_t                  ret;
    ngx_chain_t               *out, *cl;
    ngx_connection_t          *c;
    ngx_stream_js_ev_t        *event;
    ngx_stream_js_ctx_t       *ctx;
    ngx_stream_js_srv_conf_t  *jscf;

    jscf = ngx_stream_get_module_srv_conf(s, ngx_stream_js_module);
    if (jscf->filter.len == 0) {
        return ngx_stream_next_filter(s, in, from_upstream);
    }

    c = s->connection;

    ngx_log_debug1(NGX_LOG_DEBUG_STREAM, c->log, 0, "stream js filter u:%ui",
                   from_upstream);

    rc = ngx_stream_js_init_vm(s);

    if (rc == NGX_ERROR) {
        return NGX_ERROR;
    }

    if (rc == NGX_DECLINED) {
        return ngx_stream_next_filter(s, in, from_upstream);
    }

    ctx = ngx_stream_get_module_ctx(s, ngx_stream_js_module);

    if (!ctx->filter) {
        rc = ngx_js_call(ctx->vm, &jscf->filter, c->log, &ctx->args[0], 1);

        if (rc == NGX_ERROR) {
            return rc;
        }
    }

    ctx->filter = 1;

    ctx->last_out = &out;

    while (in) {
        ctx->buf = in->buf;

        event = ngx_stream_event(from_upstream);

        if (event->ev != NULL) {
            ret = ngx_stream_js_run_event(s, ctx, event, from_upstream);
            if (ret != NJS_OK) {
                ngx_js_retval(ctx->vm, NULL, &exception);

                ngx_log_error(NGX_LOG_ERR, c->log, 0, "js exception: %V",
                              &exception);

                return NGX_ERROR;
            }

            ctx->buf->pos = ctx->buf->last;

        } else {
            cl = ngx_alloc_chain_link(c->pool);
            if (cl == NULL) {
                return NGX_ERROR;
            }

            cl->buf = ctx->buf;

            *ctx->last_out = cl;
            ctx->last_out = &cl->next;
        }

        in = in->next;
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
    ngx_str_t *fname = (ngx_str_t *) data;

    ngx_int_t             rc;
    njs_int_t             pending;
    ngx_str_t             value;
    ngx_stream_js_ctx_t  *ctx;

    rc = ngx_stream_js_init_vm(s);

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

    pending = njs_vm_pending(ctx->vm);

    rc = ngx_js_call(ctx->vm, fname, s->connection->log, &ctx->args[0], 1);

    if (rc == NGX_ERROR) {
        v->not_found = 1;
        return NGX_OK;
    }

    if (!pending && rc == NGX_AGAIN) {
        ngx_log_error(NGX_LOG_ERR, s->connection->log, 0,
                      "async operation inside \"%V\" variable handler", fname);
        return NGX_ERROR;
    }

    if (ngx_js_retval(ctx->vm, &ctx->retval, &value) != NGX_OK) {
        return NGX_ERROR;
    }

    v->len = value.len;
    v->valid = 1;
    v->no_cacheable = 0;
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
ngx_stream_js_init_vm(ngx_stream_session_t *s)
{
    njs_int_t                  rc;
    ngx_str_t                  exception;
    ngx_pool_cleanup_t        *cln;
    ngx_stream_js_ctx_t       *ctx;
    ngx_stream_js_srv_conf_t  *jscf;

    jscf = ngx_stream_get_module_srv_conf(s, ngx_stream_js_module);
    if (jscf->vm == NULL) {
        return NGX_DECLINED;
    }

    ctx = ngx_stream_get_module_ctx(s, ngx_stream_js_module);

    if (ctx == NULL) {
        ctx = ngx_pcalloc(s->connection->pool, sizeof(ngx_stream_js_ctx_t));
        if (ctx == NULL) {
            return NGX_ERROR;
        }

        njs_value_invalid_set(njs_value_arg(&ctx->retval));

        ngx_stream_set_ctx(s, ctx, ngx_stream_js_module);
    }

    if (ctx->vm) {
        return NGX_OK;
    }

    ctx->vm = njs_vm_clone(jscf->vm, s);
    if (ctx->vm == NULL) {
        return NGX_ERROR;
    }

    cln = ngx_pool_cleanup_add(s->connection->pool, 0);
    if (cln == NULL) {
        return NGX_ERROR;
    }

    cln->handler = ngx_stream_js_cleanup;
    cln->data = s;

    if (njs_vm_start(ctx->vm) == NJS_ERROR) {
        ngx_js_retval(ctx->vm, NULL, &exception);

        ngx_log_error(NGX_LOG_ERR, s->connection->log, 0,
                      "js exception: %V", &exception);

        return NGX_ERROR;
    }

    rc = njs_vm_external_create(ctx->vm, njs_value_arg(&ctx->args[0]),
                                ngx_stream_js_session_proto_id, s, 0);
    if (rc != NJS_OK) {
        return NGX_ERROR;
    }

    return NGX_OK;
}


static void
ngx_stream_js_drop_events(ngx_stream_js_ctx_t *ctx)
{
    ngx_uint_t  i;

    for (i = 0; i < NGX_JS_EVENT_MAX; i++) {
        if (ctx->events[i].ev != NULL) {
            njs_vm_del_event(ctx->vm, ctx->events[i].ev);
            ctx->events[i].ev = NULL;
        }
    }
}


static void
ngx_stream_js_cleanup(void *data)
{
    ngx_stream_js_ctx_t  *ctx;

    ngx_stream_session_t *s = data;

    ctx = ngx_stream_get_module_ctx(s, ngx_stream_js_module);

    ngx_stream_js_drop_events(ctx);

    if (njs_vm_pending(ctx->vm)) {
        ngx_log_error(NGX_LOG_ERR, s->connection->log, 0, "pending events");
    }

    njs_vm_destroy(ctx->vm);
}


static void
ngx_stream_js_cleanup_vm(void *data)
{
    njs_vm_t *vm = data;

    njs_vm_destroy(vm);
}


static njs_int_t
ngx_stream_js_run_event(ngx_stream_session_t *s, ngx_stream_js_ctx_t *ctx,
    ngx_stream_js_ev_t *event, ngx_uint_t from_upstream)
{
    size_t             len;
    u_char            *p;
    njs_int_t          ret;
    ngx_buf_t         *b;
    uintptr_t          flags;
    ngx_connection_t  *c;

    if (event->ev == NULL) {
        return NJS_OK;
    }

    c = s->connection;
    b = ctx->filter ? ctx->buf : c->buffer;

    len = b ? b->last - b->pos : 0;

    p = ngx_pnalloc(c->pool, len);
    if (p == NULL) {
        njs_vm_memory_error(ctx->vm);
        return NJS_ERROR;
    }

    if (len) {
        ngx_memcpy(p, b->pos, len);
    }

    ret = ngx_js_prop(ctx->vm, event->data_type, njs_value_arg(&ctx->args[1]),
                      p, len);
    if (ret != NJS_OK) {
        return ret;
    }

    flags = from_upstream << 1 | (uintptr_t) (b && b->last_buf);

    ret = njs_vm_external_create(ctx->vm, njs_value_arg(&ctx->args[2]),
                       ngx_stream_js_session_flags_proto_id, (void *) flags, 0);
    if (ret != NJS_OK) {
        return NGX_ERROR;
    }

    njs_vm_post_event(ctx->vm, event->ev, njs_value_arg(&ctx->args[1]), 2);

    ret = njs_vm_run(ctx->vm);
    if (ret == NJS_ERROR) {
        return ret;
    }

    return NJS_OK;
}


static njs_vm_event_t *
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
        njs_vm_error(ctx->vm, "unknown event \"%V\"", event);
        return NULL;
    }

    ctx->events[events[i].id].data_type = events[i].data_type;

    for (n = 0; n < NGX_JS_EVENT_MAX; n++) {
        type = ctx->events[n].data_type;
        if (type != NGX_JS_UNSET && type != events[i].data_type) {
            njs_vm_error(ctx->vm, "mixing string and buffer events"
                         " is not allowed");
            return NULL;
        }
    }

    return &ctx->events[events[i].id].ev;
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

    return njs_vm_value_string_set(vm, retval, c->addr_text.data,
                                   c->addr_text.len);
}


static njs_int_t
ngx_stream_js_ext_done(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t magic)
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

    njs_value_undefined_set(njs_vm_retval(vm));

    return NJS_OK;
}


static njs_int_t
ngx_stream_js_ext_on(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t unused)
{
    njs_str_t              name;
    njs_value_t           *callback;
    njs_vm_event_t        *event;
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

    if (*event != NULL) {
        njs_vm_error(vm, "event handler \"%V\" is already set", &name);
        return NJS_ERROR;
    }

    *event = njs_vm_add_event(vm, njs_value_function(callback), 0, NULL, NULL);
    if (*event == NULL) {
        njs_vm_error(vm, "internal error");
        return NJS_ERROR;
    }

    njs_value_undefined_set(njs_vm_retval(vm));

    return NJS_OK;
}


static njs_int_t
ngx_stream_js_ext_off(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t unused)
{
    njs_str_t              name;
    njs_vm_event_t        *event;
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

    njs_vm_del_event(vm, *event);

    *event = NULL;

    njs_value_undefined_set(njs_vm_retval(vm));

    return NJS_OK;
}


static njs_int_t
ngx_stream_js_ext_send(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t unused)
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

    if (ctx->buf != NULL) {
        *ctx->last_out = cl;
        ctx->last_out = &cl->next;

    } else {
        if (!njs_value_is_object(flags)) {
            goto exception;
        }

        value = njs_vm_object_prop(vm, flags, &from_key, &lvalue);
        if (value == NULL) {
            goto exception;
        }

        if (ngx_stream_js_next_filter(s, ctx, cl, njs_value_bool(value))
            == NGX_ERROR)
        {
            njs_vm_error(vm, "ngx_stream_js_next_filter() failed");
            return NJS_ERROR;
        }
    }

    njs_value_undefined_set(njs_vm_retval(vm));

    return NJS_OK;

exception:

    njs_vm_error(vm, "\"from_upstream\" flag is expected when"
                "called asynchronously");

    return NJS_ERROR;
}


static njs_int_t
ngx_stream_js_ext_set_return_value(njs_vm_t *vm, njs_value_t *args,
    njs_uint_t nargs, njs_index_t unused)
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
    njs_value_undefined_set(njs_vm_retval(vm));

    return NJS_OK;
}


static njs_int_t
ngx_stream_js_ext_variables(njs_vm_t *vm, njs_object_prop_t *prop,
    njs_value_t *value, njs_value_t *setval, njs_value_t *retval)
{
    njs_int_t                     rc;
    njs_str_t                     val;
    ngx_str_t                     name;
    ngx_uint_t                    key;
    ngx_stream_variable_t        *v;
    ngx_stream_session_t         *s;
    ngx_stream_core_main_conf_t  *cmcf;
    ngx_stream_variable_value_t  *vv;

    s = njs_vm_external(vm, ngx_stream_js_session_proto_id, value);
    if (s == NULL) {
        njs_value_undefined_set(retval);
        return NJS_DECLINED;
    }

    rc = njs_vm_prop_name(vm, prop, &val);
    if (rc != NJS_OK) {
        njs_value_undefined_set(retval);
        return NJS_DECLINED;
    }

    name.data = val.start;
    name.len = val.length;

    if (setval == NULL) {
        key = ngx_hash_strlow(name.data, name.data, name.len);

        vv = ngx_stream_get_variable(s, &name, key);
        if (vv == NULL || vv->not_found) {
            njs_value_undefined_set(retval);
            return NJS_DECLINED;
        }

        return ngx_js_prop(vm, njs_vm_prop_magic32(prop), retval, vv->data,
                           vv->len);
    }

    cmcf = ngx_stream_get_module_main_conf(s, ngx_stream_core_module);

    key = ngx_hash_strlow(name.data, name.data, name.len);

    v = ngx_hash_find(&cmcf->variables_hash, key, name.data, name.len);

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


static njs_host_event_t
ngx_stream_js_set_timer(njs_external_ptr_t external, uint64_t delay,
    njs_vm_event_t vm_event)
{
    ngx_event_t            *ev;
    ngx_stream_session_t   *s;
    ngx_stream_js_event_t  *js_event;

    s = (ngx_stream_session_t *) external;

    ev = ngx_pcalloc(s->connection->pool, sizeof(ngx_event_t));
    if (ev == NULL) {
        return NULL;
    }

    js_event = ngx_palloc(s->connection->pool, sizeof(ngx_stream_js_event_t));
    if (js_event == NULL) {
        return NULL;
    }

    js_event->session = s;
    js_event->vm_event = vm_event;
    js_event->ident = s->connection->fd;

    ev->data = js_event;
    ev->log = s->connection->log;
    ev->handler = ngx_stream_js_timer_handler;

    ngx_add_timer(ev, delay);

    return ev;
}


static void
ngx_stream_js_clear_timer(njs_external_ptr_t external, njs_host_event_t event)
{
    ngx_event_t  *ev = event;

    if (ev->timer_set) {
        ngx_del_timer(ev);
    }
}


static void
ngx_stream_js_timer_handler(ngx_event_t *ev)
{
    ngx_stream_session_t   *s;
    ngx_stream_js_event_t  *js_event;

    js_event = (ngx_stream_js_event_t *) ev->data;

    s = js_event->session;

    ngx_stream_js_handle_event(s, js_event->vm_event, NULL, 0);
}


static ngx_pool_t *
ngx_stream_js_pool(njs_vm_t *vm, ngx_stream_session_t *s)
{
    return s->connection->pool;
}


static ngx_resolver_t *
ngx_stream_js_resolver(njs_vm_t *vm, ngx_stream_session_t *s)
{
    ngx_stream_core_srv_conf_t  *cscf;

    cscf = ngx_stream_get_module_srv_conf(s, ngx_stream_core_module);

    return cscf->resolver;
}


static ngx_msec_t
ngx_stream_js_resolver_timeout(njs_vm_t *vm, ngx_stream_session_t *s)
{
    ngx_stream_core_srv_conf_t  *cscf;

    cscf = ngx_stream_get_module_srv_conf(s, ngx_stream_core_module);

    return cscf->resolver_timeout;
}


static ngx_msec_t
ngx_stream_js_fetch_timeout(njs_vm_t *vm, ngx_stream_session_t *s)
{
    ngx_stream_js_srv_conf_t  *jscf;

    jscf = ngx_stream_get_module_srv_conf(s, ngx_stream_js_module);

    return jscf->timeout;
}


static size_t
ngx_stream_js_buffer_size(njs_vm_t *vm, ngx_stream_session_t *s)
{
    ngx_stream_js_srv_conf_t  *jscf;

    jscf = ngx_stream_get_module_srv_conf(s, ngx_stream_js_module);

    return jscf->buffer_size;
}


static size_t
ngx_stream_js_max_response_buffer_size(njs_vm_t *vm, ngx_stream_session_t *s)
{
    ngx_stream_js_srv_conf_t  *jscf;

    jscf = ngx_stream_get_module_srv_conf(s, ngx_stream_js_module);

    return jscf->max_response_body_size;
}


static void
ngx_stream_js_handle_event(ngx_stream_session_t *s, njs_vm_event_t vm_event,
    njs_value_t *args, njs_uint_t nargs)
{
    njs_int_t            rc;
    ngx_str_t            exception;
    ngx_stream_js_ctx_t  *ctx;

    ctx = ngx_stream_get_module_ctx(s, ngx_stream_js_module);

    njs_vm_post_event(ctx->vm, vm_event, args, nargs);

    rc = njs_vm_run(ctx->vm);

    if (rc == NJS_ERROR) {
        ngx_js_retval(ctx->vm, NULL, &exception);

        ngx_log_error(NGX_LOG_ERR, s->connection->log, 0,
                      "js exception: %V", &exception);

        ngx_stream_finalize_session(s, NGX_STREAM_INTERNAL_SERVER_ERROR);
    }

    if (rc == NJS_OK) {
        ngx_post_event(s->connection->read, &ngx_posted_events);
    }
}


static ngx_int_t
ngx_stream_js_merge_vm(ngx_conf_t *cf, ngx_stream_js_srv_conf_t *conf,
    ngx_stream_js_srv_conf_t *prev)
{
    ngx_str_t               *path, *s;
    ngx_uint_t               i;
    ngx_array_t             *imports, *paths;
    ngx_stream_js_import_t  *import, *pi;

    if (prev->imports != NGX_CONF_UNSET_PTR && prev->vm == NULL) {
        if (ngx_stream_js_init_conf_vm(cf, prev) != NGX_OK) {
            return NGX_ERROR;
        }
    }

    if (conf->imports == NGX_CONF_UNSET_PTR
        && conf->paths == NGX_CONF_UNSET_PTR)
    {
        if (prev->vm != NULL) {
            conf->imports = prev->imports;
            conf->paths = prev->paths;
            conf->vm = prev->vm;
            return NGX_OK;
        }
    }

    if (prev->imports != NGX_CONF_UNSET_PTR) {
        if (conf->imports == NGX_CONF_UNSET_PTR) {
            conf->imports = prev->imports;

        } else {
            imports = ngx_array_create(cf->pool, 4,
                                       sizeof(ngx_stream_js_import_t));
            if (imports == NULL) {
                return NGX_ERROR;
            }

            pi = prev->imports->elts;

            for (i = 0; i < prev->imports->nelts; i++) {
                import = ngx_array_push(imports);
                if (import == NULL) {
                    return NGX_ERROR;
                }

                *import = pi[i];
            }

            pi = conf->imports->elts;

            for (i = 0; i < conf->imports->nelts; i++) {
                import = ngx_array_push(imports);
                if (import == NULL) {
                    return NGX_ERROR;
                }

                *import = pi[i];
            }

            conf->imports = imports;
        }
    }

    if (prev->paths != NGX_CONF_UNSET_PTR) {
        if (conf->paths == NGX_CONF_UNSET_PTR) {
            conf->paths = prev->paths;

        } else {
            paths = ngx_array_create(cf->pool, 4, sizeof(ngx_str_t));
            if (paths == NULL) {
                return NGX_ERROR;
            }

            s = prev->imports->elts;

            for (i = 0; i < prev->paths->nelts; i++) {
                path = ngx_array_push(paths);
                if (path == NULL) {
                    return NGX_ERROR;
                }

                *path = s[i];
            }

            s = conf->imports->elts;

            for (i = 0; i < conf->paths->nelts; i++) {
                path = ngx_array_push(paths);
                if (path == NULL) {
                    return NGX_ERROR;
                }

                *path = s[i];
            }

            conf->paths = paths;
        }
    }

    if (conf->imports == NGX_CONF_UNSET_PTR) {
        return NGX_OK;
    }

    return ngx_stream_js_init_conf_vm(cf, conf);
}


static ngx_int_t
ngx_stream_js_init_conf_vm(ngx_conf_t *cf, ngx_stream_js_srv_conf_t *conf)
{
    size_t                   size;
    u_char                  *start, *end, *p;
    ngx_str_t               *m, file;
    njs_int_t                rc;
    njs_str_t                text, path;
    ngx_uint_t               i;
    njs_value_t             *value;
    njs_vm_opt_t             options;
    ngx_pool_cleanup_t      *cln;
    njs_opaque_value_t       lvalue, exception;
    ngx_stream_js_import_t  *import;

    static const njs_str_t line_number_key = njs_str("lineNumber");
    static const njs_str_t file_name_key = njs_str("fileName");

    size = 0;

    import = conf->imports->elts;
    for (i = 0; i < conf->imports->nelts; i++) {
        /* import <name> from '<path>'; globalThis.<name> = <name>; */

        size += sizeof("import  from '';") - 1 + import[i].name.len * 3
                + import[i].path.len
                + sizeof(" globalThis. = ;\n") - 1;
    }

    start = ngx_pnalloc(cf->pool, size);
    if (start == NULL) {
        return NGX_ERROR;
    }

    p = start;
    import = conf->imports->elts;
    for (i = 0; i < conf->imports->nelts; i++) {

        /* import <name> from '<path>'; globalThis.<name> = <name>; */

        p = ngx_cpymem(p, "import ", sizeof("import ") - 1);
        p = ngx_cpymem(p, import[i].name.data, import[i].name.len);
        p = ngx_cpymem(p, " from '", sizeof(" from '") - 1);
        p = ngx_cpymem(p, import[i].path.data, import[i].path.len);
        p = ngx_cpymem(p, "'; globalThis.", sizeof("'; globalThis.") - 1);
        p = ngx_cpymem(p, import[i].name.data, import[i].name.len);
        p = ngx_cpymem(p, " = ", sizeof(" = ") - 1);
        p = ngx_cpymem(p, import[i].name.data, import[i].name.len);
        p = ngx_cpymem(p, ";\n", sizeof(";\n") - 1);
    }

    njs_vm_opt_init(&options);

    options.backtrace = 1;
    options.unhandled_rejection = NJS_VM_OPT_UNHANDLED_REJECTION_THROW;
    options.ops = &ngx_stream_js_ops;
    options.metas = &ngx_stream_js_metas;
    options.addons = njs_js_addon_modules;
    options.argv = ngx_argv;
    options.argc = ngx_argc;

    file = ngx_cycle->conf_prefix;

    options.file.start = file.data;
    options.file.length = file.len;

    conf->vm = njs_vm_create(&options);
    if (conf->vm == NULL) {
        ngx_log_error(NGX_LOG_EMERG, cf->log, 0, "failed to create js VM");
        return NGX_ERROR;
    }

    cln = ngx_pool_cleanup_add(cf->pool, 0);
    if (cln == NULL) {
        return NGX_ERROR;
    }

    cln->handler = ngx_stream_js_cleanup_vm;
    cln->data = conf->vm;

    path.start = ngx_cycle->conf_prefix.data;
    path.length = ngx_cycle->conf_prefix.len;

    rc = njs_vm_add_path(conf->vm, &path);
    if (rc != NJS_OK) {
        ngx_log_error(NGX_LOG_EMERG, cf->log, 0, "failed to add \"js_path\"");
        return NGX_ERROR;
    }

    if (conf->paths != NGX_CONF_UNSET_PTR) {
        m = conf->paths->elts;

        for (i = 0; i < conf->paths->nelts; i++) {
            if (ngx_conf_full_name(cf->cycle, &m[i], 1) != NGX_OK) {
                return NGX_ERROR;
            }

            path.start = m[i].data;
            path.length = m[i].len;

            rc = njs_vm_add_path(conf->vm, &path);
            if (rc != NJS_OK) {
                ngx_log_error(NGX_LOG_EMERG, cf->log, 0,
                              "failed to add \"js_path\"");
                return NGX_ERROR;
            }
        }
    }

    ngx_stream_js_session_proto_id = njs_vm_external_prototype(conf->vm,
                                         ngx_stream_js_ext_session,
                                         njs_nitems(ngx_stream_js_ext_session));
    if (ngx_stream_js_session_proto_id < 0) {
        ngx_log_error(NGX_LOG_EMERG, cf->log, 0,
                      "failed to add js session proto");
        return NGX_ERROR;
    }

    ngx_stream_js_session_flags_proto_id = njs_vm_external_prototype(conf->vm,
                                   ngx_stream_js_ext_session_flags,
                                   njs_nitems(ngx_stream_js_ext_session_flags));
    if (ngx_stream_js_session_flags_proto_id < 0) {
        ngx_log_error(NGX_LOG_EMERG, cf->log, 0,
                      "failed to add js session flags proto");
        return NGX_ERROR;
    }

    rc = ngx_js_core_init(conf->vm, cf->log);
    if (njs_slow_path(rc != NJS_OK)) {
        return NGX_ERROR;
    }

    end = start + size;

    rc = njs_vm_compile(conf->vm, &start, end);

    if (rc != NJS_OK) {
        njs_value_assign(&exception, njs_vm_retval(conf->vm));
        njs_vm_retval_string(conf->vm, &text);

        value = njs_vm_object_prop(conf->vm, njs_value_arg(&exception),
                                   &file_name_key, &lvalue);
        if (value == NULL) {
            value = njs_vm_object_prop(conf->vm, njs_value_arg(&exception),
                                       &line_number_key, &lvalue);

            if (value != NULL) {
                i = njs_value_number(value) - 1;

                if (i < conf->imports->nelts) {
                    import = conf->imports->elts;
                    ngx_log_error(NGX_LOG_EMERG, cf->log, 0,
                                  "%*s, included in %s:%ui", text.length,
                                  text.start, import[i].file, import[i].line);
                    return NGX_ERROR;
                }
            }
        }

        ngx_log_error(NGX_LOG_EMERG, cf->log, 0, "%*s", text.length,
                      text.start);
        return NGX_ERROR;
    }

    if (start != end) {
        ngx_log_error(NGX_LOG_EMERG, cf->log, 0,
                      "extra characters in js script: \"%*s\"",
                      end - start, start);
        return NGX_ERROR;
    }

    return NGX_OK;
}


static char *
ngx_stream_js_import(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    ngx_stream_js_srv_conf_t *jscf = conf;

    u_char                  *p, *end, c;
    ngx_int_t               from;
    ngx_str_t               *value, name, path;
    ngx_stream_js_import_t  *import;

    value = cf->args->elts;
    from = (cf->args->nelts == 4);

    if (from) {
        if (ngx_strcmp(value[2].data, "from") != 0) {
            ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                               "invalid parameter \"%V\"", &value[2]);
            return NGX_CONF_ERROR;
        }
    }

    name = value[1];
    path = (from ? value[3] : value[1]);

    if (!from) {
        end = name.data + name.len;

        for (p = end - 1; p >= name.data; p--) {
            if (*p == '/') {
                break;
            }
        }

        name.data = p + 1;
        name.len = end - p - 1;

        if (name.len < 3
            || ngx_memcmp(&name.data[name.len - 3], ".js", 3) != 0)
        {
            ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                               "cannot extract export name from file path "
                               "\"%V\", use extended \"from\" syntax", &path);
            return NGX_CONF_ERROR;
        }

        name.len -= 3;
    }

    if (name.len == 0) {
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0, "empty \"name\" parameter");
        return NGX_CONF_ERROR;
    }

    p = name.data;
    end = name.data + name.len;

    while (p < end) {
        c = ngx_tolower(*p);

        if (*p != '_' && (c < 'a' || c > 'z')) {
            if (p == name.data) {
                ngx_conf_log_error(NGX_LOG_EMERG, cf, 0, "cannot start "
                                   "with \"%c\" in export name \"%V\"", *p,
                                   &name);
                return NGX_CONF_ERROR;
            }

            if (*p < '0' || *p > '9') {
                ngx_conf_log_error(NGX_LOG_EMERG, cf, 0, "invalid character "
                                   "\"%c\" in export name \"%V\"", *p, &name);
                return NGX_CONF_ERROR;
            }
        }

        p++;
    }

    if (ngx_strchr(path.data, '\'') != NULL) {
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0, "invalid character \"'\" "
                           "in file path \"%V\"", &path);
        return NGX_CONF_ERROR;
    }

    if (jscf->imports == NGX_CONF_UNSET_PTR) {
        jscf->imports = ngx_array_create(cf->pool, 4,
                                         sizeof(ngx_stream_js_import_t));
        if (jscf->imports == NULL) {
            return NGX_CONF_ERROR;
        }
    }

    import = ngx_array_push(jscf->imports);
    if (import == NULL) {
        return NGX_CONF_ERROR;
    }

    import->name = name;
    import->path = path;
    import->file = cf->conf_file->file.name.data;
    import->line = cf->conf_file->line;

    return NGX_CONF_OK;
}


static char *
ngx_stream_js_set(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    ngx_str_t              *value, *fname;
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

    fname = ngx_palloc(cf->pool, sizeof(ngx_str_t));
    if (fname == NULL) {
        return NGX_CONF_ERROR;
    }

    *fname = value[2];

    v->get_handler = ngx_stream_js_variable_set;
    v->data = (uintptr_t) fname;

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
ngx_stream_js_create_srv_conf(ngx_conf_t *cf)
{
    ngx_stream_js_srv_conf_t  *conf;

    conf = ngx_pcalloc(cf->pool, sizeof(ngx_stream_js_srv_conf_t));
    if (conf == NULL) {
        return NULL;
    }

    /*
     * set by ngx_pcalloc():
     *
     *     conf->vm = NULL;
     *     conf->access = { 0, NULL };
     *     conf->preread = { 0, NULL };
     *     conf->filter = { 0, NULL };
     *     conf->ssl_ciphers = { 0, NULL };
     *     conf->ssl_protocols = 0;
     *     conf->ssl_trusted_certificate = { 0, NULL };
     */

    conf->paths = NGX_CONF_UNSET_PTR;
    conf->imports = NGX_CONF_UNSET_PTR;

    conf->buffer_size = NGX_CONF_UNSET_SIZE;
    conf->max_response_body_size = NGX_CONF_UNSET_SIZE;
    conf->timeout = NGX_CONF_UNSET_MSEC;

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

    ngx_conf_merge_msec_value(conf->timeout, prev->timeout, 60000);
    ngx_conf_merge_size_value(conf->buffer_size, prev->buffer_size, 16384);
    ngx_conf_merge_size_value(conf->max_response_body_size,
                              prev->max_response_body_size, 1048576);

    if (ngx_stream_js_merge_vm(cf, conf, prev) != NGX_OK) {
        return NGX_CONF_ERROR;
    }

#if (NGX_STREAM_SSL)
    ngx_conf_merge_str_value(conf->ssl_ciphers, prev->ssl_ciphers, "DEFAULT");

    ngx_conf_merge_bitmask_value(conf->ssl_protocols, prev->ssl_protocols,
                                 (NGX_CONF_BITMASK_SET|NGX_SSL_TLSv1
                                  |NGX_SSL_TLSv1_1|NGX_SSL_TLSv1_2));

    ngx_conf_merge_value(conf->ssl_verify, prev->ssl_verify, 1);
    ngx_conf_merge_value(conf->ssl_verify_depth, prev->ssl_verify_depth, 100);

    ngx_conf_merge_str_value(conf->ssl_trusted_certificate,
                             prev->ssl_trusted_certificate, "");

    return ngx_stream_js_set_ssl(cf, conf);
#else
    return NGX_CONF_OK;
#endif
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


#if (NGX_STREAM_SSL)

static char *
ngx_stream_js_set_ssl(ngx_conf_t *cf, ngx_stream_js_srv_conf_t *jscf)
{
    ngx_ssl_t           *ssl;
    ngx_pool_cleanup_t  *cln;

    ssl = ngx_pcalloc(cf->pool, sizeof(ngx_ssl_t));
    if (ssl == NULL) {
        return NGX_CONF_ERROR;
    }

    jscf->ssl = ssl;
    ssl->log = cf->log;

    if (ngx_ssl_create(ssl, jscf->ssl_protocols, NULL) != NGX_OK) {
        return NGX_CONF_ERROR;
    }

    cln = ngx_pool_cleanup_add(cf->pool, 0);
    if (cln == NULL) {
        ngx_ssl_cleanup_ctx(ssl);
        return NGX_CONF_ERROR;
    }

    cln->handler = ngx_ssl_cleanup_ctx;
    cln->data = ssl;

    if (ngx_ssl_ciphers(NULL, ssl, &jscf->ssl_ciphers, 0) != NGX_OK) {
        return NGX_CONF_ERROR;
    }

    if (ngx_ssl_trusted_certificate(cf, ssl, &jscf->ssl_trusted_certificate,
                                    jscf->ssl_verify_depth)
        != NGX_OK)
    {
        return NGX_CONF_ERROR;
    }

    return NGX_CONF_OK;
}

#endif


static ngx_ssl_t *
ngx_stream_js_ssl(njs_vm_t *vm, ngx_stream_session_t *s)
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
ngx_stream_js_ssl_verify(njs_vm_t *vm, ngx_stream_session_t *s)
{
#if (NGX_STREAM_SSL)
    ngx_stream_js_srv_conf_t  *jscf;

    jscf = ngx_stream_get_module_srv_conf(s, ngx_stream_js_module);

    return jscf->ssl_verify;
#else
    return 0;
#endif
}
