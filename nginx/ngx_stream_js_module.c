
/*
 * Copyright (C) Roman Arutyunyan
 * Copyright (C) Dmitry Volyntsev
 * Copyright (C) NGINX, Inc.
 */


#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_stream.h>

#include <njs.h>


typedef struct {
    njs_vm_t              *vm;
    ngx_array_t           *paths;
    const njs_extern_t    *proto;
} ngx_stream_js_main_conf_t;


typedef struct {
    ngx_str_t              access;
    ngx_str_t              preread;
    ngx_str_t              filter;
} ngx_stream_js_srv_conf_t;


typedef struct {
    njs_vm_t               *vm;
    ngx_log_t              *log;
    njs_opaque_value_t      args[3];
    ngx_buf_t              *buf;
    ngx_chain_t           **last_out;
    ngx_chain_t            *free;
    ngx_chain_t            *busy;
    ngx_stream_session_t   *session;
    ngx_int_t               status;
    njs_vm_event_t          upload_event;
    njs_vm_event_t          download_event;
    unsigned                from_upstream:1;
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
static ngx_int_t ngx_stream_js_variable(ngx_stream_session_t *s,
    ngx_stream_variable_value_t *v, uintptr_t data);
static ngx_int_t ngx_stream_js_init_vm(ngx_stream_session_t *s);
static void ngx_stream_js_cleanup_ctx(void *data);
static void ngx_stream_js_cleanup_vm(void *data);
static njs_int_t ngx_stream_js_buffer_arg(ngx_stream_session_t *s,
    njs_value_t *buffer);
static njs_int_t ngx_stream_js_flags_arg(ngx_stream_session_t *s,
    njs_value_t *flags);
static njs_vm_event_t *ngx_stream_js_event(ngx_stream_session_t *s,
    njs_str_t *event);

static njs_int_t ngx_stream_js_ext_get_remote_address(njs_vm_t *vm,
    njs_value_t *value, void *obj, uintptr_t data);

static njs_int_t ngx_stream_js_ext_done(njs_vm_t *vm, njs_value_t *args,
     njs_uint_t nargs, njs_index_t unused);
static njs_int_t ngx_stream_js_ext_deny(njs_vm_t *vm, njs_value_t *args,
     njs_uint_t nargs, njs_index_t unused);
static njs_int_t ngx_stream_js_ext_decline(njs_vm_t *vm, njs_value_t *args,
     njs_uint_t nargs, njs_index_t unused);
static njs_int_t ngx_stream_js_ext_set_status(njs_vm_t *vm, njs_value_t *args,
    njs_uint_t nargs, ngx_int_t status);

static njs_int_t ngx_stream_js_ext_log(njs_vm_t *vm, njs_value_t *args,
     njs_uint_t nargs, njs_index_t unused);
static njs_int_t ngx_stream_js_ext_warn(njs_vm_t *vm, njs_value_t *args,
     njs_uint_t nargs, njs_index_t unused);
static njs_int_t ngx_stream_js_ext_error(njs_vm_t *vm, njs_value_t *args,
     njs_uint_t nargs, njs_index_t unused);
static njs_int_t ngx_stream_js_ext_log_core(njs_vm_t *vm, njs_value_t *args,
    njs_uint_t nargs, ngx_uint_t level);
static njs_int_t ngx_stream_js_ext_on(njs_vm_t *vm, njs_value_t *args,
     njs_uint_t nargs, njs_index_t unused);
static njs_int_t ngx_stream_js_ext_off(njs_vm_t *vm, njs_value_t *args,
     njs_uint_t nargs, njs_index_t unused);
static njs_int_t ngx_stream_js_ext_send(njs_vm_t *vm, njs_value_t *args,
     njs_uint_t nargs, njs_index_t unused);

static njs_int_t ngx_stream_js_ext_get_variable(njs_vm_t *vm,
    njs_value_t *value, void *obj, uintptr_t data);
static njs_int_t ngx_stream_js_ext_set_variable(njs_vm_t *vm, void *obj,
    uintptr_t data, njs_str_t *value);

static njs_host_event_t ngx_stream_js_set_timer(njs_external_ptr_t external,
    uint64_t delay, njs_vm_event_t vm_event);
static void ngx_stream_js_clear_timer(njs_external_ptr_t external,
    njs_host_event_t event);
static void ngx_stream_js_timer_handler(ngx_event_t *ev);
static void ngx_stream_js_handle_event(ngx_stream_session_t *s,
    njs_vm_event_t vm_event, njs_value_t *args, njs_uint_t nargs);
static njs_int_t ngx_stream_js_string(njs_vm_t *vm, njs_value_t *value,
    njs_str_t *str);

static char *ngx_stream_js_include(ngx_conf_t *cf, ngx_command_t *cmd,
    void *conf);
static char *ngx_stream_js_set(ngx_conf_t *cf, ngx_command_t *cmd,
    void *conf);
static void *ngx_stream_js_create_main_conf(ngx_conf_t *cf);
static void *ngx_stream_js_create_srv_conf(ngx_conf_t *cf);
static char *ngx_stream_js_merge_srv_conf(ngx_conf_t *cf, void *parent,
    void *child);
static ngx_int_t ngx_stream_js_init(ngx_conf_t *cf);


static ngx_command_t  ngx_stream_js_commands[] = {

    { ngx_string("js_include"),
      NGX_STREAM_MAIN_CONF|NGX_CONF_TAKE1,
      ngx_stream_js_include,
      NGX_STREAM_MAIN_CONF_OFFSET,
      0,
      NULL },

    { ngx_string("js_path"),
      NGX_STREAM_MAIN_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_str_array_slot,
      NGX_STREAM_MAIN_CONF_OFFSET,
      offsetof(ngx_stream_js_main_conf_t, paths),
      NULL },

    { ngx_string("js_set"),
      NGX_STREAM_MAIN_CONF|NGX_CONF_TAKE2,
      ngx_stream_js_set,
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
    NULL,                           /* init process */
    NULL,                           /* init thread */
    NULL,                           /* exit thread */
    NULL,                           /* exit process */
    NULL,                           /* exit master */
    NGX_MODULE_V1_PADDING
};


static njs_external_t  ngx_stream_js_ext_session[] = {

    { njs_str("remoteAddress"),
      NJS_EXTERN_PROPERTY,
      NULL,
      0,
      ngx_stream_js_ext_get_remote_address,
      NULL,
      NULL,
      NULL,
      NULL,
      0 },

    { njs_str("variables"),
      NJS_EXTERN_OBJECT,
      NULL,
      0,
      ngx_stream_js_ext_get_variable,
      ngx_stream_js_ext_set_variable,
      NULL,
      NULL,
      NULL,
      0 },

    { njs_str("allow"),
      NJS_EXTERN_METHOD,
      NULL,
      0,
      NULL,
      NULL,
      NULL,
      NULL,
      ngx_stream_js_ext_done,
      0 },

    { njs_str("deny"),
      NJS_EXTERN_METHOD,
      NULL,
      0,
      NULL,
      NULL,
      NULL,
      NULL,
      ngx_stream_js_ext_deny,
      0 },

    { njs_str("decline"),
      NJS_EXTERN_METHOD,
      NULL,
      0,
      NULL,
      NULL,
      NULL,
      NULL,
      ngx_stream_js_ext_decline,
      0 },

    { njs_str("done"),
      NJS_EXTERN_METHOD,
      NULL,
      0,
      NULL,
      NULL,
      NULL,
      NULL,
      ngx_stream_js_ext_done,
      0 },

    { njs_str("log"),
      NJS_EXTERN_METHOD,
      NULL,
      0,
      NULL,
      NULL,
      NULL,
      NULL,
      ngx_stream_js_ext_log,
      0 },

    { njs_str("warn"),
      NJS_EXTERN_METHOD,
      NULL,
      0,
      NULL,
      NULL,
      NULL,
      NULL,
      ngx_stream_js_ext_warn,
      0 },

    { njs_str("error"),
      NJS_EXTERN_METHOD,
      NULL,
      0,
      NULL,
      NULL,
      NULL,
      NULL,
      ngx_stream_js_ext_error,
      0 },

    { njs_str("on"),
      NJS_EXTERN_METHOD,
      NULL,
      0,
      NULL,
      NULL,
      NULL,
      NULL,
      ngx_stream_js_ext_on,
      0 },

    { njs_str("off"),
      NJS_EXTERN_METHOD,
      NULL,
      0,
      NULL,
      NULL,
      NULL,
      NULL,
      ngx_stream_js_ext_off,
      0 },

    { njs_str("send"),
      NJS_EXTERN_METHOD,
      NULL,
      0,
      NULL,
      NULL,
      NULL,
      NULL,
      ngx_stream_js_ext_send,
      0 },

};


static njs_external_t  ngx_stream_js_externals[] = {

    { njs_str("stream"),
      NJS_EXTERN_OBJECT,
      ngx_stream_js_ext_session,
      njs_nitems(ngx_stream_js_ext_session),
      NULL,
      NULL,
      NULL,
      NULL,
      NULL,
      0 },
};


static njs_vm_ops_t ngx_stream_js_ops = {
    ngx_stream_js_set_timer,
    ngx_stream_js_clear_timer
};


static ngx_stream_filter_pt  ngx_stream_next_filter;


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
    njs_str_t             fname, exception;
    njs_int_t             ret;
    ngx_int_t             rc;
    njs_function_t       *func;
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
                   "http js phase call \"%V\"", name);

    ctx = ngx_stream_get_module_ctx(s, ngx_stream_js_module);

    if (!ctx->in_progress) {
        fname.start = name->data;
        fname.length = name->len;

        func = njs_vm_function(ctx->vm, &fname);

        if (func == NULL) {
            ngx_log_error(NGX_LOG_ERR, c->log, 0,
                          "js function \"%V\" not found", name);
            return NGX_ERROR;
        }

        /*
         * status is expected to be overriden by allow(), deny(), decline() or
         * done() methods.
         */

        ctx->status = NGX_ERROR;

        ret = njs_vm_call(ctx->vm, func, njs_value_arg(&ctx->args), 1);
        if (ret != NJS_OK) {
            goto exception;
        }
    }

    if (ctx->upload_event != NULL) {
        ret = ngx_stream_js_buffer_arg(s, njs_value_arg(&ctx->args[1]));
        if (ret != NJS_OK) {
            goto exception;
        }

        ret = ngx_stream_js_flags_arg(s, njs_value_arg(&ctx->args[2]));
        if (ret != NJS_OK) {
            goto exception;
        }

        njs_vm_post_event(ctx->vm, ctx->upload_event,
                          njs_value_arg(&ctx->args[1]), 2);

        rc = njs_vm_run(ctx->vm);
        if (rc == NJS_ERROR) {
            goto exception;
        }
    }

    if (njs_vm_pending(ctx->vm)) {
        ctx->in_progress = 1;
        rc = ctx->upload_event ? NGX_AGAIN : NGX_DONE;

    } else {
        ctx->in_progress = 0;
        rc = ctx->status;
    }

    ngx_log_debug1(NGX_LOG_DEBUG_STREAM, c->log, 0, "stream js phase rc: %i",
                   rc);

    return rc;

exception:

    njs_vm_retval_string(ctx->vm, &exception);

    ngx_log_error(NGX_LOG_ERR, c->log, 0, "js exception: %*s",
                  exception.length, exception.start);

    return NGX_ERROR;
}


#define ngx_stream_event(from_upstream)                                 \
    (from_upstream ? ctx->download_event : ctx->upload_event)


static ngx_int_t
ngx_stream_js_body_filter(ngx_stream_session_t *s, ngx_chain_t *in,
    ngx_uint_t from_upstream)
{
    njs_str_t                  name, exception;
    njs_int_t                  ret;
    ngx_int_t                  rc;
    ngx_chain_t               *out, *cl;
    njs_function_t            *func;
    ngx_connection_t          *c;
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
        name.start = jscf->filter.data;
        name.length = jscf->filter.len;

        func = njs_vm_function(ctx->vm, &name);

        if (func == NULL) {
            ngx_log_error(NGX_LOG_ERR, c->log, 0,
                          "js function \"%V\" not found", &jscf->filter);
            return NGX_ERROR;
        }

        ret = njs_vm_call(ctx->vm, func, njs_value_arg(&ctx->args), 1);
        if (ret != NJS_OK) {
            goto exception;
        }
    }

    ctx->filter = 1;
    ctx->from_upstream = from_upstream;

    ctx->last_out = &out;

    while (in) {
        ctx->buf = in->buf;

        if (ngx_stream_event(from_upstream) != NULL) {
            ret = ngx_stream_js_buffer_arg(s, njs_value_arg(&ctx->args[1]));
            if (ret != NJS_OK) {
                goto exception;
            }

            ret = ngx_stream_js_flags_arg(s, njs_value_arg(&ctx->args[2]));
            if (ret != NJS_OK) {
                goto exception;
            }

            njs_vm_post_event(ctx->vm, ngx_stream_event(from_upstream),
                              njs_value_arg(&ctx->args[1]), 2);

            rc = njs_vm_run(ctx->vm);
            if (rc == NJS_ERROR) {
                goto exception;
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

    *ctx->last_out = NULL;

    if (out != NULL || c->buffered) {
        rc = ngx_stream_next_filter(s, out, from_upstream);

        ngx_chain_update_chains(c->pool, &ctx->free, &ctx->busy, &out,
                                (ngx_buf_tag_t) &ngx_stream_js_module);

    } else {
        rc = NGX_OK;
    }

    return rc;

exception:

    njs_vm_retval_string(ctx->vm, &exception);

    ngx_log_error(NGX_LOG_ERR, c->log, 0, "js exception: %*s",
                  exception.length, exception.start);

    return NGX_ERROR;
}


static ngx_int_t
ngx_stream_js_variable(ngx_stream_session_t *s, ngx_stream_variable_value_t *v,
    uintptr_t data)
{
    ngx_str_t *fname = (ngx_str_t *) data;

    ngx_int_t             rc;
    njs_int_t             pending;
    njs_str_t             name, value, exception;
    njs_function_t       *func;
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

    name.start = fname->data;
    name.length = fname->len;

    func = njs_vm_function(ctx->vm, &name);
    if (func == NULL) {
        ngx_log_error(NGX_LOG_ERR, s->connection->log, 0,
                      "js function \"%V\" not found", fname);
        v->not_found = 1;
        return NGX_OK;
    }

    pending = njs_vm_pending(ctx->vm);

    if (njs_vm_call(ctx->vm, func, njs_value_arg(&ctx->args), 1) != NJS_OK) {
        njs_vm_retval_string(ctx->vm, &exception);

        ngx_log_error(NGX_LOG_ERR, s->connection->log, 0,
                      "js exception: %*s", exception.length, exception.start);

        v->not_found = 1;
        return NGX_OK;
    }

    if (njs_vm_retval_string(ctx->vm, &value) != NJS_OK) {
        return NGX_ERROR;
    }

    if (!pending && njs_vm_pending(ctx->vm)) {
        ngx_log_error(NGX_LOG_ERR, s->connection->log, 0,
                      "async operation inside \"%V\" variable handler", fname);
        return NGX_ERROR;
    }

    v->len = value.length;
    v->valid = 1;
    v->no_cacheable = 0;
    v->not_found = 0;
    v->data = value.start;

    return NGX_OK;
}


static ngx_int_t
ngx_stream_js_init_vm(ngx_stream_session_t *s)
{
    njs_int_t                   rc;
    njs_str_t                   exception;
    ngx_pool_cleanup_t         *cln;
    ngx_stream_js_ctx_t        *ctx;
    ngx_stream_js_main_conf_t  *jmcf;

    jmcf = ngx_stream_get_module_main_conf(s, ngx_stream_js_module);
    if (jmcf->vm == NULL) {
        return NGX_DECLINED;
    }

    ctx = ngx_stream_get_module_ctx(s, ngx_stream_js_module);

    if (ctx == NULL) {
        ctx = ngx_pcalloc(s->connection->pool, sizeof(ngx_stream_js_ctx_t));
        if (ctx == NULL) {
            return NGX_ERROR;
        }

        ngx_stream_set_ctx(s, ctx, ngx_stream_js_module);
    }

    if (ctx->vm) {
        return NGX_OK;
    }

    ctx->vm = njs_vm_clone(jmcf->vm, s);
    if (ctx->vm == NULL) {
        return NGX_ERROR;
    }

    cln = ngx_pool_cleanup_add(s->connection->pool, 0);
    if (cln == NULL) {
        return NGX_ERROR;
    }

    ctx->log = s->connection->log;

    cln->handler = ngx_stream_js_cleanup_ctx;
    cln->data = ctx;

    if (njs_vm_start(ctx->vm) == NJS_ERROR) {
        njs_vm_retval_string(ctx->vm, &exception);

        ngx_log_error(NGX_LOG_ERR, s->connection->log, 0,
                      "js exception: %*s", exception.length, exception.start);

        return NGX_ERROR;
    }

    rc = njs_vm_external_create(ctx->vm, njs_value_arg(&ctx->args[0]),
                                jmcf->proto, s);
    if (rc != NJS_OK) {
        return NGX_ERROR;
    }

    return NGX_OK;
}


static void
ngx_stream_js_cleanup_ctx(void *data)
{
    ngx_stream_js_ctx_t *ctx = data;

    if (ctx->upload_event != NULL) {
        njs_vm_del_event(ctx->vm, ctx->upload_event);
        ctx->upload_event = NULL;
    }

    if (ctx->download_event != NULL) {
        njs_vm_del_event(ctx->vm, ctx->download_event);
        ctx->download_event = NULL;
    }

    if (njs_vm_pending(ctx->vm)) {
        ngx_log_error(NGX_LOG_ERR, ctx->log, 0, "pending events");
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
ngx_stream_js_buffer_arg(ngx_stream_session_t *s, njs_value_t *buffer)
{
    size_t                 len;
    u_char                *p;
    ngx_buf_t             *b;
    ngx_connection_t      *c;
    ngx_stream_js_ctx_t   *ctx;

    c = s->connection;
    ctx = ngx_stream_get_module_ctx(s, ngx_stream_js_module);

    b = ctx->filter ? ctx->buf : c->buffer;

    len = b ? b->last - b->pos : 0;

    p = njs_vm_value_string_alloc(ctx->vm, buffer, len);
    if (p == NULL) {
        return NJS_ERROR;
    }

    if (len) {
        ngx_memcpy(p, b->pos, len);
    }

    return NJS_OK;
}



static njs_int_t
ngx_stream_js_flags_arg(ngx_stream_session_t *s, njs_value_t *flags)
{
    ngx_buf_t             *b;
    ngx_connection_t      *c;
    njs_opaque_value_t    last_key;
    njs_opaque_value_t    values[1];
    ngx_stream_js_ctx_t  *ctx;

    static const njs_str_t last_str = njs_str("last");

    ctx = ngx_stream_get_module_ctx(s, ngx_stream_js_module);

    njs_vm_value_string_set(ctx->vm, njs_value_arg(&last_key), last_str.start,
                            last_str.length);

    c = s->connection;

    b = ctx->filter ? ctx->buf : c->buffer;
    njs_value_boolean_set(njs_value_arg(&values[0]), b && b->last_buf);

    return njs_vm_object_alloc(ctx->vm, flags,
                               njs_value_arg(&last_key),
                               njs_value_arg(&values[0]), NULL);
}


static njs_vm_event_t *
ngx_stream_js_event(ngx_stream_session_t *s, njs_str_t *event)
{
    ngx_uint_t             i, n;
    ngx_stream_js_ctx_t  *ctx;

    static const njs_str_t events[] = {
        njs_str("upload"),
        njs_str("download")
    };

    ctx = ngx_stream_get_module_ctx(s, ngx_stream_js_module);

    i = 0;
    n = sizeof(events) / sizeof(events[0]);

    while (i < n) {
        if (event->length == events[i].length
            && ngx_memcmp(event->start, events[i].start, event->length) == 0)
        {
            break;
        }

        i++;
    }

    if (i == n) {
        njs_vm_error(ctx->vm, "unknown event \"%V\"", event);
        return NULL;
    }

    if (i == 0) {
        return &ctx->upload_event;
    }

    return &ctx->download_event;
}


static njs_int_t
ngx_stream_js_ext_get_remote_address(njs_vm_t *vm, njs_value_t *value,
    void *obj, uintptr_t data)
{
    ngx_connection_t      *c;
    ngx_stream_session_t  *s;

    s = (ngx_stream_session_t *) obj;
    c = s->connection;

    return njs_vm_value_string_set(vm, value, c->addr_text.data,
                                   c->addr_text.len);
}


static njs_int_t
ngx_stream_js_ext_done(njs_vm_t *vm, njs_value_t *args,
     njs_uint_t nargs, njs_index_t unused)
{
    return ngx_stream_js_ext_set_status(vm, args, nargs, NGX_OK);
}


static njs_int_t
ngx_stream_js_ext_deny(njs_vm_t *vm, njs_value_t *args,
     njs_uint_t nargs, njs_index_t unused)
{
    return ngx_stream_js_ext_set_status(vm, args, njs_min(nargs, 1),
                                        NGX_STREAM_FORBIDDEN);
}


static njs_int_t
ngx_stream_js_ext_decline(njs_vm_t *vm, njs_value_t *args,
     njs_uint_t nargs, njs_index_t unused)
{
    return ngx_stream_js_ext_set_status(vm, args, njs_min(nargs, 1),
                                        NGX_DECLINED);
}


static njs_int_t
ngx_stream_js_ext_set_status(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    ngx_int_t status)
{
    njs_value_t           *code;
    ngx_stream_js_ctx_t   *ctx;
    ngx_stream_session_t  *s;

    s = njs_vm_external(vm, njs_arg(args, nargs, 0));
    if (njs_slow_path(s == NULL)) {
        return NJS_ERROR;
    }

    code = njs_arg(args, nargs, 1);

    if (!njs_value_is_undefined(code)) {
        if (!njs_value_is_valid_number(code)) {
            njs_vm_error(vm, "code is not a number");
            return NJS_ERROR;
        }

        status = njs_value_number(code);
        if (status < NGX_ABORT || status > NGX_STREAM_SERVICE_UNAVAILABLE) {
            njs_vm_error(vm, "code is out of range");
            return NJS_ERROR;
        }
    }

    ctx = ngx_stream_get_module_ctx(s, ngx_stream_js_module);

    ngx_log_debug1(NGX_LOG_DEBUG_STREAM, s->connection->log, 0,
                   "stream js set status: %i", status);

    ctx->status = status;

    if (ctx->upload_event != NULL) {
        njs_vm_del_event(ctx->vm, ctx->upload_event);
        ctx->upload_event = NULL;
    }

    if (ctx->download_event != NULL) {
        njs_vm_del_event(ctx->vm, ctx->download_event);
        ctx->download_event = NULL;
    }

    return NJS_OK;
}


static njs_int_t
ngx_stream_js_ext_log(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t unused)
{
    return ngx_stream_js_ext_log_core(vm, args, nargs, NGX_LOG_INFO);
}


static njs_int_t
ngx_stream_js_ext_warn(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t unused)
{
    return ngx_stream_js_ext_log_core(vm, args, nargs, NGX_LOG_WARN);
}


static njs_int_t
ngx_stream_js_ext_error(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t unused)
{
    return ngx_stream_js_ext_log_core(vm, args, nargs, NGX_LOG_ERR);
}


static njs_int_t
ngx_stream_js_ext_log_core(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    ngx_uint_t level)
{
    njs_str_t              msg;
    ngx_connection_t      *c;
    ngx_log_handler_pt     handler;
    ngx_stream_session_t  *s;

    s = njs_vm_external(vm, njs_arg(args, nargs, 0));
    if (njs_slow_path(s == NULL)) {
        return NJS_ERROR;
    }

    c = s->connection;

    if (njs_vm_value_to_string(vm, &msg, njs_arg(args, nargs, 1))
        == NJS_ERROR)
    {
        return NJS_ERROR;
    }

    handler = c->log->handler;
    c->log->handler = NULL;

    ngx_log_error(level, c->log, 0, "js: %*s", msg.length, msg.start);

    c->log->handler = handler;

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

    s = njs_vm_external(vm, njs_arg(args, nargs, 0));
    if (njs_slow_path(s == NULL)) {
        return NJS_ERROR;
    }

    if (njs_vm_value_to_string(vm, &name, njs_arg(args, nargs, 1))
        == NJS_ERROR)
    {
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

    return NJS_OK;
}


static njs_int_t
ngx_stream_js_ext_off(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t unused)
{
    njs_str_t              name;
    njs_vm_event_t        *event;
    ngx_stream_session_t  *s;

    s = njs_vm_external(vm, njs_arg(args, nargs, 0));
    if (njs_slow_path(s == NULL)) {
        return NJS_ERROR;
    }

    if (njs_vm_value_to_string(vm, &name, njs_arg(args, nargs, 1))
        == NJS_ERROR)
    {
        njs_vm_error(vm, "failed to convert event arg");
        return NJS_ERROR;
    }

    event = ngx_stream_js_event(s, &name);
    if (event == NULL) {
        return NJS_ERROR;
    }

    njs_vm_del_event(vm, *event);

    *event = NULL;

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
    ngx_stream_js_ctx_t   *ctx;
    ngx_stream_session_t  *s;

    static const njs_str_t last_key = njs_str("last");
    static const njs_str_t flush_key = njs_str("flush");

    s = njs_vm_external(vm, njs_arg(args, nargs, 0));
    if (njs_slow_path(s == NULL)) {
        return NJS_ERROR;
    }

    c = s->connection;

    ctx = ngx_stream_get_module_ctx(s, ngx_stream_js_module);

    if (!ctx->filter) {
        njs_vm_error(vm, "cannot send buffer in this handler");
        return NJS_ERROR;
    }

    if (ngx_stream_js_string(vm, njs_arg(args, nargs, 1), &buffer) != NJS_OK) {
        njs_vm_error(vm, "failed to get buffer arg");
        return NJS_ERROR;
    }

    flush = ctx->buf->flush;
    last_buf = ctx->buf->last_buf;

    flags = njs_arg(args, nargs, 2);

    if (njs_value_is_object(flags)) {
        value = njs_vm_object_prop(vm, flags, &flush_key);
        if (value != NULL) {
            flush = njs_value_bool(value);
        }

        value = njs_vm_object_prop(vm, flags, &last_key);
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

    *ctx->last_out = cl;
    ctx->last_out = &cl->next;

    return NJS_OK;
}


static njs_int_t
ngx_stream_js_ext_get_variable(njs_vm_t *vm, njs_value_t *value, void *obj,
    uintptr_t data)
{
    njs_str_t                    *v;
    ngx_str_t                     name;
    ngx_uint_t                    key;
    ngx_stream_session_t         *s;
    ngx_stream_variable_value_t  *vv;

    s = (ngx_stream_session_t *) obj;
    v = (njs_str_t *) data;

    name.data = v->start;
    name.len = v->length;

    key = ngx_hash_strlow(name.data, name.data, name.len);

    vv = ngx_stream_get_variable(s, &name, key);
    if (vv == NULL || vv->not_found) {
        njs_value_undefined_set(value);
        return NJS_OK;
    }

    return njs_vm_value_string_set(vm, value, vv->data, vv->len);
}


static njs_int_t
ngx_stream_js_ext_set_variable(njs_vm_t *vm, void *obj, uintptr_t data,
    njs_str_t *value)
{
    njs_str_t                    *val;
    ngx_str_t                     name;
    ngx_uint_t                    key;
    ngx_stream_variable_t        *v;
    ngx_stream_session_t         *s;
    ngx_stream_core_main_conf_t  *cmcf;
    ngx_stream_variable_value_t  *vv;

    s = (ngx_stream_session_t *) obj;
    val = (njs_str_t *) data;

    name.data = val->start;
    name.len = val->length;

    cmcf = ngx_stream_get_module_main_conf(s, ngx_stream_core_module);

    key = ngx_hash_strlow(name.data, name.data, name.len);

    v = ngx_hash_find(&cmcf->variables_hash, key, name.data, name.len);

    if (v == NULL) {
        njs_vm_error(vm, "variable not found");
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
        vv->data = value->start;
        vv->len = value->length;

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

    vv->data = ngx_pnalloc(s->connection->pool, value->length);
    if (vv->data == NULL) {
        return NJS_ERROR;
    }

    vv->len = value->length;
    ngx_memcpy(vv->data, value->start, vv->len);

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


static void
ngx_stream_js_handle_event(ngx_stream_session_t *s, njs_vm_event_t vm_event,
    njs_value_t *args, njs_uint_t nargs)
{
    njs_int_t            rc;
    njs_str_t            exception;
    ngx_stream_js_ctx_t  *ctx;

    ctx = ngx_stream_get_module_ctx(s, ngx_stream_js_module);

    njs_vm_post_event(ctx->vm, vm_event, args, nargs);

    rc = njs_vm_run(ctx->vm);

    if (rc == NJS_ERROR) {
        njs_vm_retval_string(ctx->vm, &exception);

        ngx_log_error(NGX_LOG_ERR, s->connection->log, 0,
                      "js exception: %*s", exception.length, exception.start);

        ngx_stream_finalize_session(s, NGX_STREAM_INTERNAL_SERVER_ERROR);
    }

    if (rc == NJS_OK) {
        ngx_post_event(s->connection->read, &ngx_posted_events);
    }
}


static njs_int_t
ngx_stream_js_string(njs_vm_t *vm, njs_value_t *value, njs_str_t *str)
{
    if (!njs_value_is_null_or_undefined(value)) {
        if (njs_vm_value_to_string(vm, str, value) == NJS_ERROR) {
            return NJS_ERROR;
        }

    } else {
        str->start = NULL;
        str->length = 0;
    }

    return NJS_OK;
}


static char *
ngx_stream_js_include(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    ngx_stream_js_main_conf_t *jmcf = conf;

    size_t                 size;
    u_char                *start, *end;
    ssize_t                n;
    ngx_fd_t               fd;
    ngx_str_t             *m, *value, file;
    njs_int_t              rc;
    njs_str_t              text, path;
    ngx_uint_t             i;
    njs_vm_opt_t           options;
    ngx_file_info_t        fi;
    ngx_pool_cleanup_t    *cln;

    if (jmcf->vm) {
        return "is duplicate";
    }

    value = cf->args->elts;
    file = value[1];

    if (ngx_conf_full_name(cf->cycle, &file, 1) != NGX_OK) {
        return NGX_CONF_ERROR;
    }

    fd = ngx_open_file(file.data, NGX_FILE_RDONLY, NGX_FILE_OPEN, 0);
    if (fd == NGX_INVALID_FILE) {
        ngx_conf_log_error(NGX_LOG_EMERG, cf, ngx_errno,
                           ngx_open_file_n " \"%s\" failed", file.data);
        return NGX_CONF_ERROR;
    }

    if (ngx_fd_info(fd, &fi) == NGX_FILE_ERROR) {
        ngx_conf_log_error(NGX_LOG_EMERG, cf, ngx_errno,
                           ngx_fd_info_n " \"%s\" failed", file.data);
        (void) ngx_close_file(fd);
        return NGX_CONF_ERROR;
    }

    size = ngx_file_size(&fi);

    start = ngx_pnalloc(cf->pool, size);
    if (start == NULL) {
        (void) ngx_close_file(fd);
        return NGX_CONF_ERROR;
    }

    n = ngx_read_fd(fd, start,  size);

    if (n == -1) {
        ngx_conf_log_error(NGX_LOG_EMERG, cf, ngx_errno,
                           ngx_read_fd_n " \"%s\" failed", file.data);

        (void) ngx_close_file(fd);
        return NGX_CONF_ERROR;
    }

    if ((size_t) n != size) {
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                           ngx_read_fd_n " has read only %z of %uz from \"%s\"",
                           n, size, file.data);

        (void) ngx_close_file(fd);
        return NGX_CONF_ERROR;
    }

    if (ngx_close_file(fd) == NGX_FILE_ERROR) {
        ngx_conf_log_error(NGX_LOG_EMERG, cf, ngx_errno,
                           ngx_close_file_n " %s failed", file.data);
    }

    end = start + size;

    ngx_memzero(&options, sizeof(njs_vm_opt_t));

    options.backtrace = 1;
    options.ops = &ngx_stream_js_ops;
    options.argv = ngx_argv;
    options.argc = ngx_argc;

    file = value[1];
    options.file.start = file.data;
    options.file.length = file.len;

    jmcf->vm = njs_vm_create(&options);
    if (jmcf->vm == NULL) {
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0, "failed to create JS VM");
        return NGX_CONF_ERROR;
    }

    cln = ngx_pool_cleanup_add(cf->pool, 0);
    if (cln == NULL) {
        return NGX_CONF_ERROR;
    }

    cln->handler = ngx_stream_js_cleanup_vm;
    cln->data = jmcf->vm;

    path.start = ngx_cycle->prefix.data;
    path.length = ngx_cycle->prefix.len;

    rc = njs_vm_add_path(jmcf->vm, &path);
    if (rc != NJS_OK) {
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0, "failed to add path");
        return NGX_CONF_ERROR;
    }

    if (jmcf->paths != NGX_CONF_UNSET_PTR) {
        m = jmcf->paths->elts;

        for (i = 0; i < jmcf->paths->nelts; i++) {
            if (ngx_conf_full_name(cf->cycle, &m[i], 0) != NGX_OK) {
                return NGX_CONF_ERROR;
            }

            path.start = m[i].data;
            path.length = m[i].len;

            rc = njs_vm_add_path(jmcf->vm, &path);
            if (rc != NJS_OK) {
                ngx_conf_log_error(NGX_LOG_EMERG, cf, 0, "failed to add path");
                return NGX_CONF_ERROR;
            }
        }
    }

    jmcf->proto = njs_vm_external_prototype(jmcf->vm,
                                            &ngx_stream_js_externals[0]);

    if (jmcf->proto == NULL) {
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0, "failed to add stream proto");
        return NGX_CONF_ERROR;
    }

    rc = njs_vm_compile(jmcf->vm, &start, end);

    if (rc != NJS_OK) {
        njs_vm_retval_string(jmcf->vm, &text);

        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                           "%*s, included",
                           text.length, text.start);
        return NGX_CONF_ERROR;
    }

    if (start != end) {
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                           "extra characters in js script: \"%*s\", included",
                           end - start, start);
        return NGX_CONF_ERROR;
    }

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

    v->get_handler = ngx_stream_js_variable;
    v->data = (uintptr_t) fname;

    return NGX_CONF_OK;
}


static void *
ngx_stream_js_create_main_conf(ngx_conf_t *cf)
{
    ngx_stream_js_main_conf_t  *conf;

    conf = ngx_pcalloc(cf->pool, sizeof(ngx_stream_js_main_conf_t));
    if (conf == NULL) {
        return NULL;
    }

    /*
     * set by ngx_pcalloc():
     *
     *     conf->vm = NULL;
     *     conf->proto = NULL;
     */

    conf->paths = NGX_CONF_UNSET_PTR;

    return conf;
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
     *     conf->access = { 0, NULL };
     *     conf->preread = { 0, NULL };
     *     conf->filter = { 0, NULL };
     */

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

    return NGX_CONF_OK;
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
