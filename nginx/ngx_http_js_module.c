
/*
 * Copyright (C) Roman Arutyunyan
 * Copyright (C) NGINX, Inc.
 */


#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>

#include <njs.h>


typedef struct {
    njs_vm_t            *vm;
    ngx_array_t         *paths;
    const njs_extern_t  *req_proto;
} ngx_http_js_main_conf_t;


typedef struct {
    ngx_str_t            content;
} ngx_http_js_loc_conf_t;


typedef struct {
    njs_vm_t            *vm;
    ngx_log_t           *log;
    ngx_uint_t           done;
    ngx_int_t            status;
    njs_opaque_value_t   request;
    njs_opaque_value_t   request_body;
    ngx_str_t            redirect_uri;
    njs_opaque_value_t   promise_callbacks[2];
} ngx_http_js_ctx_t;


typedef struct {
    ngx_http_request_t  *request;
    njs_vm_event_t       vm_event;
    void                *unused;
    ngx_int_t            ident;
} ngx_http_js_event_t;


static ngx_int_t ngx_http_js_content_handler(ngx_http_request_t *r);
static void ngx_http_js_content_event_handler(ngx_http_request_t *r);
static void ngx_http_js_content_write_event_handler(ngx_http_request_t *r);
static void ngx_http_js_content_finalize(ngx_http_request_t *r,
    ngx_http_js_ctx_t *ctx);
static ngx_int_t ngx_http_js_variable(ngx_http_request_t *r,
    ngx_http_variable_value_t *v, uintptr_t data);
static ngx_int_t ngx_http_js_init_vm(ngx_http_request_t *r);
static void ngx_http_js_cleanup_ctx(void *data);
static void ngx_http_js_cleanup_vm(void *data);

static njs_int_t ngx_http_js_ext_get_string(njs_vm_t *vm, njs_value_t *value,
    void *obj, uintptr_t data);
static njs_int_t ngx_http_js_ext_keys_header(njs_vm_t *vm, void *obj,
    njs_value_t *keys, uintptr_t data);
static ngx_table_elt_t *ngx_http_js_get_header(ngx_list_part_t *part,
    u_char *data, size_t len);
static njs_int_t ngx_http_js_ext_get_header_out(njs_vm_t *vm,
    njs_value_t *value, void *obj, uintptr_t data);
static njs_int_t ngx_http_js_ext_set_header_out(njs_vm_t *vm, void *obj,
    uintptr_t data, njs_str_t *value);
static njs_int_t ngx_http_js_ext_delete_header_out(njs_vm_t *vm, void *obj,
    uintptr_t data, njs_bool_t delete);
static njs_int_t ngx_http_js_ext_keys_header_out(njs_vm_t *vm, void *obj,
    njs_value_t *keys); /*FIXME*/
static njs_int_t ngx_http_js_ext_get_status(njs_vm_t *vm, njs_value_t *value,
    void *obj, uintptr_t data);
static njs_int_t ngx_http_js_ext_set_status(njs_vm_t *vm, void *obj,
    uintptr_t data, njs_str_t *value);
static njs_int_t ngx_http_js_ext_send_header(njs_vm_t *vm, njs_value_t *args,
    njs_uint_t nargs, njs_index_t unused);
static njs_int_t ngx_http_js_ext_send(njs_vm_t *vm, njs_value_t *args,
    njs_uint_t nargs, njs_index_t unused);
static njs_int_t ngx_http_js_ext_finish(njs_vm_t *vm, njs_value_t *args,
    njs_uint_t nargs, njs_index_t unused);
static njs_int_t ngx_http_js_ext_return(njs_vm_t *vm, njs_value_t *args,
    njs_uint_t nargs, njs_index_t unused);
static njs_int_t ngx_http_js_ext_internal_redirect(njs_vm_t *vm,
    njs_value_t *args, njs_uint_t nargs, njs_index_t unused);

static njs_int_t ngx_http_js_ext_log(njs_vm_t *vm, njs_value_t *args,
    njs_uint_t nargs, njs_index_t unused);
static njs_int_t ngx_http_js_ext_warn(njs_vm_t *vm, njs_value_t *args,
    njs_uint_t nargs, njs_index_t unused);
static njs_int_t ngx_http_js_ext_error(njs_vm_t *vm, njs_value_t *args,
    njs_uint_t nargs, njs_index_t unused);
static njs_int_t ngx_http_js_ext_log_core(njs_vm_t *vm, njs_value_t *args,
    njs_uint_t nargs, ngx_uint_t level);

static njs_int_t ngx_http_js_ext_get_http_version(njs_vm_t *vm,
    njs_value_t *value, void *obj, uintptr_t data);
static njs_int_t ngx_http_js_ext_get_remote_address(njs_vm_t *vm,
    njs_value_t *value, void *obj, uintptr_t data);
static njs_int_t ngx_http_js_ext_get_request_body(njs_vm_t *vm,
    njs_value_t *value, void *obj, uintptr_t data);
static njs_int_t ngx_http_js_ext_get_header_in(njs_vm_t *vm, njs_value_t *value,
    void *obj, uintptr_t data);
static njs_int_t ngx_http_js_ext_keys_header_in(njs_vm_t *vm, void *obj,
    njs_value_t *keys); /*FIXME*/
static njs_int_t ngx_http_js_ext_get_arg(njs_vm_t *vm, njs_value_t *value,
    void *obj, uintptr_t data);
static njs_int_t ngx_http_js_ext_keys_arg(njs_vm_t *vm, void *obj,
    njs_value_t *keys);
static njs_int_t ngx_http_js_ext_get_variable(njs_vm_t *vm, njs_value_t *value,
    void *obj, uintptr_t data);
static njs_int_t ngx_http_js_ext_set_variable(njs_vm_t *vm, void *obj,
       uintptr_t data, njs_str_t *value);
static njs_int_t ngx_http_js_ext_subrequest(njs_vm_t *vm, njs_value_t *args,
    njs_uint_t nargs, njs_index_t unused);
static ngx_int_t ngx_http_js_subrequest(ngx_http_request_t *r,
    njs_str_t *uri_arg, njs_str_t *args_arg, njs_function_t *callback,
    ngx_http_request_t **sr);
static ngx_int_t ngx_http_js_subrequest_done(ngx_http_request_t *r,
    void *data, ngx_int_t rc);
static njs_int_t ngx_http_js_ext_get_parent(njs_vm_t *vm, njs_value_t *value,
    void *obj, uintptr_t data);
static njs_int_t ngx_http_js_ext_get_reply_body(njs_vm_t *vm,
    njs_value_t *value, void *obj, uintptr_t data);

static njs_host_event_t ngx_http_js_set_timer(njs_external_ptr_t external,
    uint64_t delay, njs_vm_event_t vm_event);
static void ngx_http_js_clear_timer(njs_external_ptr_t external,
    njs_host_event_t event);
static void ngx_http_js_timer_handler(ngx_event_t *ev);
static void ngx_http_js_handle_event(ngx_http_request_t *r,
    njs_vm_event_t vm_event, njs_value_t *args, njs_uint_t nargs);
static njs_int_t ngx_http_js_string(njs_vm_t *vm, njs_value_t *value,
    njs_str_t *str);

static char *ngx_http_js_include(ngx_conf_t *cf, ngx_command_t *cmd,
    void *conf);
static char *ngx_http_js_set(ngx_conf_t *cf, ngx_command_t *cmd, void *conf);
static char *ngx_http_js_content(ngx_conf_t *cf, ngx_command_t *cmd,
    void *conf);
static void *ngx_http_js_create_main_conf(ngx_conf_t *cf);
static void *ngx_http_js_create_loc_conf(ngx_conf_t *cf);
static char *ngx_http_js_merge_loc_conf(ngx_conf_t *cf, void *parent,
    void *child);


static ngx_command_t  ngx_http_js_commands[] = {

    { ngx_string("js_include"),
      NGX_HTTP_MAIN_CONF|NGX_CONF_TAKE1,
      ngx_http_js_include,
      NGX_HTTP_MAIN_CONF_OFFSET,
      0,
      NULL },

    { ngx_string("js_path"),
      NGX_HTTP_MAIN_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_str_array_slot,
      NGX_HTTP_MAIN_CONF_OFFSET,
      offsetof(ngx_http_js_main_conf_t, paths),
      NULL },

    { ngx_string("js_set"),
      NGX_HTTP_MAIN_CONF|NGX_CONF_TAKE2,
      ngx_http_js_set,
      0,
      0,
      NULL },

    { ngx_string("js_content"),
      NGX_HTTP_LOC_CONF|NGX_HTTP_LMT_CONF|NGX_CONF_TAKE1,
      ngx_http_js_content,
      NGX_HTTP_LOC_CONF_OFFSET,
      0,
      NULL },

      ngx_null_command
};


static ngx_http_module_t  ngx_http_js_module_ctx = {
    NULL,                          /* preconfiguration */
    NULL,                          /* postconfiguration */

    ngx_http_js_create_main_conf,  /* create main configuration */
    NULL,                          /* init main configuration */

    NULL,                          /* create server configuration */
    NULL,                          /* merge server configuration */

    ngx_http_js_create_loc_conf,   /* create location configuration */
    ngx_http_js_merge_loc_conf     /* merge location configuration */
};


ngx_module_t  ngx_http_js_module = {
    NGX_MODULE_V1,
    &ngx_http_js_module_ctx,       /* module context */
    ngx_http_js_commands,          /* module directives */
    NGX_HTTP_MODULE,               /* module type */
    NULL,                          /* init master */
    NULL,                          /* init module */
    NULL,                          /* init process */
    NULL,                          /* init thread */
    NULL,                          /* exit thread */
    NULL,                          /* exit process */
    NULL,                          /* exit master */
    NGX_MODULE_V1_PADDING
};


static njs_external_t  ngx_http_js_ext_request[] = {

    { njs_str("uri"),
      NJS_EXTERN_PROPERTY,
      NULL,
      0,
      ngx_http_js_ext_get_string,
      NULL,
      NULL,
      NULL,
      NULL,
      offsetof(ngx_http_request_t, uri) },

    { njs_str("method"),
      NJS_EXTERN_PROPERTY,
      NULL,
      0,
      ngx_http_js_ext_get_string,
      NULL,
      NULL,
      NULL,
      NULL,
      offsetof(ngx_http_request_t, method_name) },

    { njs_str("httpVersion"),
      NJS_EXTERN_PROPERTY,
      NULL,
      0,
      ngx_http_js_ext_get_http_version,
      NULL,
      NULL,
      NULL,
      NULL,
      0 },

    { njs_str("remoteAddress"),
      NJS_EXTERN_PROPERTY,
      NULL,
      0,
      ngx_http_js_ext_get_remote_address,
      NULL,
      NULL,
      NULL,
      NULL,
      0 },

    { njs_str("parent"),
      NJS_EXTERN_PROPERTY,
      NULL,
      0,
      ngx_http_js_ext_get_parent,
      NULL,
      NULL,
      NULL,
      NULL,
      0 },

    { njs_str("requestBody"),
      NJS_EXTERN_PROPERTY,
      NULL,
      0,
      ngx_http_js_ext_get_request_body,
      NULL,
      NULL,
      NULL,
      NULL,
      0 },

    { njs_str("responseBody"),
      NJS_EXTERN_PROPERTY,
      NULL,
      0,
      ngx_http_js_ext_get_reply_body,
      NULL,
      NULL,
      NULL,
      NULL,
      0 },

    { njs_str("headersIn"),
      NJS_EXTERN_OBJECT,
      NULL,
      0,
      ngx_http_js_ext_get_header_in,
      NULL,
      NULL,
      ngx_http_js_ext_keys_header_in,
      NULL,
      0 },

    { njs_str("args"),
      NJS_EXTERN_OBJECT,
      NULL,
      0,
      ngx_http_js_ext_get_arg,
      NULL,
      NULL,
      ngx_http_js_ext_keys_arg,
      NULL,
      0 },

    { njs_str("variables"),
      NJS_EXTERN_OBJECT,
      NULL,
      0,
      ngx_http_js_ext_get_variable,
      ngx_http_js_ext_set_variable,
      NULL,
      NULL,
      NULL,
      0 },

    { njs_str("status"),
      NJS_EXTERN_PROPERTY,
      NULL,
      0,
      ngx_http_js_ext_get_status,
      ngx_http_js_ext_set_status,
      NULL,
      NULL,
      NULL,
      0 },

    { njs_str("headersOut"),
      NJS_EXTERN_OBJECT,
      NULL,
      0,
      ngx_http_js_ext_get_header_out,
      ngx_http_js_ext_set_header_out,
      ngx_http_js_ext_delete_header_out,
      ngx_http_js_ext_keys_header_out,
      NULL,
      0 },

    { njs_str("subrequest"),
      NJS_EXTERN_METHOD,
      NULL,
      0,
      NULL,
      NULL,
      NULL,
      NULL,
      ngx_http_js_ext_subrequest,
      0 },

    { njs_str("log"),
      NJS_EXTERN_METHOD,
      NULL,
      0,
      NULL,
      NULL,
      NULL,
      NULL,
      ngx_http_js_ext_log,
      0 },

    { njs_str("warn"),
      NJS_EXTERN_METHOD,
      NULL,
      0,
      NULL,
      NULL,
      NULL,
      NULL,
      ngx_http_js_ext_warn,
      0 },

    { njs_str("error"),
      NJS_EXTERN_METHOD,
      NULL,
      0,
      NULL,
      NULL,
      NULL,
      NULL,
      ngx_http_js_ext_error,
      0 },

    { njs_str("sendHeader"),
      NJS_EXTERN_METHOD,
      NULL,
      0,
      NULL,
      NULL,
      NULL,
      NULL,
      ngx_http_js_ext_send_header,
      0 },

    { njs_str("send"),
      NJS_EXTERN_METHOD,
      NULL,
      0,
      NULL,
      NULL,
      NULL,
      NULL,
      ngx_http_js_ext_send,
      0 },

    { njs_str("finish"),
      NJS_EXTERN_METHOD,
      NULL,
      0,
      NULL,
      NULL,
      NULL,
      NULL,
      ngx_http_js_ext_finish,
      0 },

    { njs_str("return"),
      NJS_EXTERN_METHOD,
      NULL,
      0,
      NULL,
      NULL,
      NULL,
      NULL,
      ngx_http_js_ext_return,
      0 },

    { njs_str("internalRedirect"),
      NJS_EXTERN_METHOD,
      NULL,
      0,
      NULL,
      NULL,
      NULL,
      NULL,
      ngx_http_js_ext_internal_redirect,
      0 },
};


static njs_external_t  ngx_http_js_externals[] = {

    { njs_str("request"),
      NJS_EXTERN_OBJECT,
      ngx_http_js_ext_request,
      njs_nitems(ngx_http_js_ext_request),
      NULL,
      NULL,
      NULL,
      NULL,
      NULL,
      0 },
};


static njs_vm_ops_t ngx_http_js_ops = {
    ngx_http_js_set_timer,
    ngx_http_js_clear_timer
};


static ngx_int_t
ngx_http_js_content_handler(ngx_http_request_t *r)
{
    ngx_int_t  rc;

    ngx_log_debug0(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
                   "http js content handler");

    rc = ngx_http_read_client_request_body(r,
                                           ngx_http_js_content_event_handler);

    if (rc >= NGX_HTTP_SPECIAL_RESPONSE) {
        return rc;
    }

    return NGX_DONE;
}


static void
ngx_http_js_content_event_handler(ngx_http_request_t *r)
{
    ngx_int_t                rc;
    njs_str_t                name, exception;
    njs_function_t          *func;
    ngx_http_js_ctx_t       *ctx;
    ngx_http_js_loc_conf_t  *jlcf;

    ngx_log_debug0(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
                   "http js content event handler");

    rc = ngx_http_js_init_vm(r);

    if (rc == NGX_ERROR || rc == NGX_DECLINED) {
        ngx_http_finalize_request(r, NGX_HTTP_INTERNAL_SERVER_ERROR);
        return;
    }

    jlcf = ngx_http_get_module_loc_conf(r, ngx_http_js_module);

    ngx_log_debug1(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
                   "http js content call \"%V\"" , &jlcf->content);

    ctx = ngx_http_get_module_ctx(r, ngx_http_js_module);

    name.start = jlcf->content.data;
    name.length = jlcf->content.len;

    func = njs_vm_function(ctx->vm, &name);
    if (func == NULL) {
        ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                      "js function \"%V\" not found", &jlcf->content);
        ngx_http_finalize_request(r, NGX_HTTP_INTERNAL_SERVER_ERROR);
        return;
    }

    /*
     * status is expected to be overriden by finish(), return() or
     * internalRedirect() methods, otherwise the content handler is
     * considered invalid.
     */

    ctx->status = NGX_HTTP_INTERNAL_SERVER_ERROR;

    if (njs_vm_call(ctx->vm, func, njs_value_arg(&ctx->request), 1) != NJS_OK) {
        njs_vm_retval_string(ctx->vm, &exception);

        ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                      "js exception: %*s", exception.length, exception.start);

        ngx_http_finalize_request(r, NGX_HTTP_INTERNAL_SERVER_ERROR);
        return;
    }

    if (njs_vm_pending(ctx->vm)) {
        r->write_event_handler = ngx_http_js_content_write_event_handler;
        return;
    }

    ngx_http_js_content_finalize(r, ctx);
}


static void
ngx_http_js_content_write_event_handler(ngx_http_request_t *r)
{
    ngx_event_t               *wev;
    ngx_connection_t          *c;
    ngx_http_js_ctx_t         *ctx;
    ngx_http_core_loc_conf_t  *clcf;

    ngx_log_debug0(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
                   "http js content write event handler");

    ctx = ngx_http_get_module_ctx(r, ngx_http_js_module);

    if (!njs_vm_pending(ctx->vm)) {
        ngx_http_js_content_finalize(r, ctx);
        return;
    }

    c = r->connection;
    wev = c->write;

    if (wev->timedout) {
        ngx_connection_error(c, NGX_ETIMEDOUT, "client timed out");
        ngx_http_finalize_request(r, NGX_HTTP_REQUEST_TIME_OUT);
        return;
    }

    if (ngx_http_output_filter(r, NULL) == NGX_ERROR) {
        ngx_http_finalize_request(r, NGX_ERROR);
        return;
    }

    clcf = ngx_http_get_module_loc_conf(r->main, ngx_http_core_module);

    if (ngx_handle_write_event(wev, clcf->send_lowat) != NGX_OK) {
        ngx_http_finalize_request(r, NGX_ERROR);
        return;
    }

    if (!wev->delayed) {
        if (wev->active && !wev->ready) {
            ngx_add_timer(wev, clcf->send_timeout);

        } else if (wev->timer_set) {
            ngx_del_timer(wev);
        }
    }
}


static void
ngx_http_js_content_finalize(ngx_http_request_t *r, ngx_http_js_ctx_t *ctx)
{
    ngx_str_t  args;

    ngx_log_debug1(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
                   "http js content rc: %i", ctx->status);

    if (ctx->redirect_uri.len) {
        if (ctx->redirect_uri.data[0] == '@') {
            ngx_http_named_location(r, &ctx->redirect_uri);

        } else {
            ngx_http_split_args(r, &ctx->redirect_uri, &args);
            ngx_http_internal_redirect(r, &ctx->redirect_uri, &args);
        }
    }

    ngx_http_finalize_request(r, ctx->status);
}


static ngx_int_t
ngx_http_js_variable(ngx_http_request_t *r, ngx_http_variable_value_t *v,
    uintptr_t data)
{
    ngx_str_t *fname = (ngx_str_t *) data;

    ngx_int_t           rc;
    njs_int_t           pending;
    njs_str_t           name, value, exception;
    njs_function_t     *func;
    ngx_http_js_ctx_t  *ctx;

    rc = ngx_http_js_init_vm(r);

    if (rc == NGX_ERROR) {
        return NGX_ERROR;
    }

    if (rc == NGX_DECLINED) {
        v->not_found = 1;
        return NGX_OK;
    }

    ngx_log_debug1(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
                   "http js variable call \"%V\"", fname);

    ctx = ngx_http_get_module_ctx(r, ngx_http_js_module);

    name.start = fname->data;
    name.length = fname->len;

    func = njs_vm_function(ctx->vm, &name);
    if (func == NULL) {
        ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                      "js function \"%V\" not found", fname);
        v->not_found = 1;
        return NGX_OK;
    }

    pending = njs_vm_pending(ctx->vm);

    if (njs_vm_call(ctx->vm, func, njs_value_arg(&ctx->request), 1) != NJS_OK) {
        njs_vm_retval_string(ctx->vm, &exception);

        ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                      "js exception: %*s", exception.length, exception.start);

        v->not_found = 1;
        return NGX_OK;
    }

    if (njs_vm_retval_string(ctx->vm, &value) != NJS_OK) {
        return NGX_ERROR;
    }

    if (!pending && njs_vm_pending(ctx->vm)) {
        ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
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
ngx_http_js_init_vm(ngx_http_request_t *r)
{
    njs_int_t                 rc;
    njs_str_t                 exception;
    ngx_http_js_ctx_t        *ctx;
    ngx_pool_cleanup_t       *cln;
    ngx_http_js_main_conf_t  *jmcf;

    jmcf = ngx_http_get_module_main_conf(r, ngx_http_js_module);
    if (jmcf->vm == NULL) {
        return NGX_DECLINED;
    }

    ctx = ngx_http_get_module_ctx(r, ngx_http_js_module);

    if (ctx == NULL) {
        ctx = ngx_pcalloc(r->pool, sizeof(ngx_http_js_ctx_t));
        if (ctx == NULL) {
            return NGX_ERROR;
        }

        ngx_http_set_ctx(r, ctx, ngx_http_js_module);
    }

    if (ctx->vm) {
        return NGX_OK;
    }

    ctx->vm = njs_vm_clone(jmcf->vm, r);
    if (ctx->vm == NULL) {
        return NGX_ERROR;
    }

    cln = ngx_pool_cleanup_add(r->pool, 0);
    if (cln == NULL) {
        return NGX_ERROR;
    }

    ctx->log = r->connection->log;

    cln->handler = ngx_http_js_cleanup_ctx;
    cln->data = ctx;

    if (njs_vm_start(ctx->vm) == NJS_ERROR) {
        njs_vm_retval_string(ctx->vm, &exception);

        ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                      "js exception: %*s", exception.length, exception.start);

        return NGX_ERROR;
    }

    rc = njs_vm_external_create(ctx->vm, njs_value_arg(&ctx->request),
                                jmcf->req_proto, r);
    if (rc != NJS_OK) {
        return NGX_ERROR;
    }

    return NGX_OK;
}


static void
ngx_http_js_cleanup_ctx(void *data)
{
    ngx_http_js_ctx_t *ctx = data;

    if (njs_vm_pending(ctx->vm)) {
        ngx_log_error(NGX_LOG_ERR, ctx->log, 0, "pending events");
    }

    njs_vm_destroy(ctx->vm);
}


static void
ngx_http_js_cleanup_vm(void *data)
{
    njs_vm_t *vm = data;

    njs_vm_destroy(vm);
}


static njs_int_t
ngx_http_js_ext_get_string(njs_vm_t *vm, njs_value_t *value, void *obj,
    uintptr_t data)
{
    char *p = obj;

    ngx_str_t  *field;

    field = (ngx_str_t *) (p + data);

    return njs_vm_value_string_set(vm, value, field->data, field->len);
}


static njs_int_t
ngx_http_js_ext_keys_header(njs_vm_t *vm, void *obj, njs_value_t *keys,
    uintptr_t data)
{
    char *p = obj;

    njs_int_t         rc, cookie, x_for;
    ngx_uint_t        item;
    ngx_list_t       *headers;
    njs_value_t      *value;
    ngx_list_part_t  *part;
    ngx_table_elt_t  *header, *h;

    rc = njs_vm_array_alloc(vm, keys, 8);
    if (rc != NJS_OK) {
        return NJS_ERROR;
    }

    headers = (ngx_list_t *) (p + data);
    part = &headers->part;
    item = 0;

    cookie = 0;
    x_for = 0;

    while (part) {

        if (item >= part->nelts) {
            part = part->next;
            item = 0;
            continue;
        }

        header = part->elts;
        h = &header[item++];

        if (h->hash == 0) {
            continue;
        }

        if (h->key.len == njs_length("Cookie")
            && ngx_strncasecmp(h->key.data, (u_char *) "Cookie",
                               h->key.len) == 0)
        {
            if (cookie) {
                continue;
            }

            cookie = 1;
        }

        if (h->key.len == njs_length("X-Forwarded-For")
            && ngx_strncasecmp(h->key.data, (u_char *) "X-Forwarded-For",
                               h->key.len) == 0)
        {
            if (x_for) {
                continue;
            }

            x_for = 1;
        }

        value = njs_vm_array_push(vm, keys);
        if (value == NULL) {
            return NJS_ERROR;
        }

        rc = njs_vm_value_string_set(vm, value, h->key.data, h->key.len);
        if (rc != NJS_OK) {
            return NJS_ERROR;
        }
    }

    return NJS_OK;
}


static ngx_table_elt_t *
ngx_http_js_get_header(ngx_list_part_t *part, u_char *data, size_t len)
{
    ngx_uint_t        i;
    ngx_table_elt_t  *header, *h;

    header = part->elts;

    for (i = 0; /* void */ ; i++) {

        if (i >= part->nelts) {
            if (part->next == NULL) {
                break;
            }

            part = part->next;
            header = part->elts;
            i = 0;
        }

        h = &header[i];

        if (h->hash == 0) {
            continue;
        }

        if (h->key.len == len && ngx_strncasecmp(h->key.data, data, len) == 0) {
            return h;
        }
    }

    return NULL;
}


static njs_int_t
ngx_http_js_ext_get_header_out(njs_vm_t *vm, njs_value_t *value, void *obj,
    uintptr_t data)
{
    u_char              *p, *start;
    njs_str_t           *v;
    ngx_str_t           *hdr;
    ngx_table_elt_t     *h;
    ngx_http_request_t  *r;
    u_char               content_len[NGX_OFF_T_LEN];

    r = (ngx_http_request_t *) obj;
    v = (njs_str_t *) data;

    if (v->length == njs_length("Content-Type")
        && ngx_strncasecmp(v->start, (u_char *) "Content-Type",
                           v->length) == 0)
    {
        hdr = &r->headers_out.content_type;
        return njs_vm_value_string_set(vm, value, hdr->data, hdr->len);
    }

    if (v->length == njs_length("Content-Length")
        && ngx_strncasecmp(v->start, (u_char *) "Content-Length",
                           v->length) == 0)
    {
        if (r->headers_out.content_length == NULL
            && r->headers_out.content_length_n >= 0)
        {
            p = ngx_sprintf(content_len, "%O", r->headers_out.content_length_n);

            start = njs_vm_value_string_alloc(vm, value, p - content_len);
            if (start == NULL) {
                return NJS_ERROR;
            }

            ngx_memcpy(start, content_len, p - content_len);

            return NJS_OK;
        }
    }

    h = ngx_http_js_get_header(&r->headers_out.headers.part, v->start,
                               v->length);
    if (h == NULL) {
        njs_value_undefined_set(value);
        return NJS_OK;
    }

    return njs_vm_value_string_set(vm, value, h->value.data, h->value.len);
}


static njs_int_t
ngx_http_js_ext_set_header_out(njs_vm_t *vm, void *obj, uintptr_t data,
    njs_str_t *value)
{
    u_char              *p;
    ngx_int_t            n;
    njs_str_t           *v;
    ngx_table_elt_t     *h;
    ngx_http_request_t  *r;

    r = (ngx_http_request_t *) obj;
    v = (njs_str_t *) data;

    if (v->length == njs_length("Content-Type")
        && ngx_strncasecmp(v->start, (u_char *) "Content-Type",
                           v->length) == 0)
    {
        r->headers_out.content_type.len = value->length;
        r->headers_out.content_type_len = r->headers_out.content_type.len;
        r->headers_out.content_type.data = value->start;
        r->headers_out.content_type_lowcase = NULL;

        return NJS_OK;
    }

    h = ngx_http_js_get_header(&r->headers_out.headers.part, v->start,
                               v->length);

    if (h != NULL && value->length == 0) {
        h->hash = 0;
        h = NULL;
    }

    if (h == NULL && value->length != 0) {
        h = ngx_list_push(&r->headers_out.headers);
        if (h == NULL) {
            return NJS_ERROR;
        }

        p = ngx_pnalloc(r->pool, v->length);
        if (p == NULL) {
            return NJS_ERROR;
        }

        ngx_memcpy(p, v->start, v->length);

        h->key.data = p;
        h->key.len = v->length;
    }

    if (h != NULL) {
        p = ngx_pnalloc(r->pool, value->length);
        if (p == NULL) {
            return NJS_ERROR;
        }

        ngx_memcpy(p, value->start, value->length);

        h->value.data = p;
        h->value.len = value->length;
        h->hash = 1;
    }

    if (v->length == njs_length("Content-Encoding")
        && ngx_strncasecmp(v->start, (u_char *) "Content-Encoding",
                           v->length) == 0)
    {
        r->headers_out.content_encoding = h;
    }

    if (v->length == njs_length("Content-Length")
        && ngx_strncasecmp(v->start, (u_char *) "Content-Length",
                           v->length) == 0)
    {
        if (h != NULL) {
            n = ngx_atoi(value->start, value->length);
            if (n == NGX_ERROR) {
                h->hash = 0;
                njs_vm_error(vm, "failed converting argument to integer");
                return NJS_ERROR;
            }

            r->headers_out.content_length = h;
            r->headers_out.content_length_n = n;

        } else {
            ngx_http_clear_content_length(r);
        }
    }

    return NJS_OK;
}


static njs_int_t
ngx_http_js_ext_delete_header_out(njs_vm_t *vm, void *obj, uintptr_t data,
    njs_bool_t unused)
{
    njs_str_t  value;

    value = njs_str_value("");

    return ngx_http_js_ext_set_header_out(vm, obj, data, &value);
}


static njs_int_t
ngx_http_js_ext_keys_header_out(njs_vm_t *vm, void *obj, njs_value_t *keys)
{
    return ngx_http_js_ext_keys_header(vm, obj, keys,
                             offsetof(ngx_http_request_t, headers_out.headers));
}


static njs_int_t
ngx_http_js_ext_get_status(njs_vm_t *vm, njs_value_t *value, void *obj,
    uintptr_t data)
{
    ngx_http_request_t  *r;

    r = (ngx_http_request_t *) obj;

    njs_value_number_set(value, r->headers_out.status);

    return NJS_OK;
}


static njs_int_t
ngx_http_js_ext_set_status(njs_vm_t *vm, void *obj, uintptr_t data,
    njs_str_t *value)
{
    ngx_int_t            n;
    ngx_http_request_t  *r;

    n = ngx_atoi(value->start, value->length);
    if (n == NGX_ERROR) {
        return NJS_ERROR;
    }

    r = (ngx_http_request_t *) obj;

    r->headers_out.status = n;

    return NJS_OK;
}


static njs_int_t
ngx_http_js_ext_send_header(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t unused)
{
    ngx_http_request_t  *r;

    r = njs_vm_external(vm, njs_arg(args, nargs, 0));
    if (njs_slow_path(r == NULL)) {
        return NJS_ERROR;
    }

    if (ngx_http_set_content_type(r) != NGX_OK) {
        return NJS_ERROR;
    }

    if (ngx_http_send_header(r) == NGX_ERROR) {
        return NJS_ERROR;
    }

    return NJS_OK;
}


static njs_int_t
ngx_http_js_ext_send(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t unused)
{
    njs_int_t            ret;
    njs_str_t            s;
    ngx_buf_t           *b;
    uintptr_t            next;
    ngx_uint_t           n;
    ngx_chain_t         *out, *cl, **ll;
    ngx_http_request_t  *r;

    r = njs_vm_external(vm, njs_arg(args, nargs, 0));
    if (njs_slow_path(r == NULL)) {
        return NJS_ERROR;
    }

    out = NULL;
    ll = &out;

    for (n = 1; n < nargs; n++) {
        next = 0;

        for ( ;; ) {
            ret = njs_vm_value_string_copy(vm, &s, njs_argument(args, n),
                                           &next);

            if (ret == NJS_DECLINED) {
                break;
            }

            if (ret == NJS_ERROR) {
                return NJS_ERROR;
            }

            if (s.length == 0) {
                continue;
            }

            /* TODO: njs_value_release(vm, value) in buf completion */

            b = ngx_calloc_buf(r->pool);
            if (b == NULL) {
                return NJS_ERROR;
            }

            b->start = s.start;
            b->pos = b->start;
            b->end = s.start + s.length;
            b->last = b->end;
            b->memory = 1;

            cl = ngx_alloc_chain_link(r->pool);
            if (cl == NULL) {
                return NJS_ERROR;
            }

            cl->buf = b;

            *ll = cl;
            ll = &cl->next;
        }
    }

    *ll = NULL;

    if (ngx_http_output_filter(r, out) == NGX_ERROR) {
        return NJS_ERROR;
    }

    return NJS_OK;
}


static njs_int_t
ngx_http_js_ext_finish(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t unused)
{
    ngx_http_js_ctx_t   *ctx;
    ngx_http_request_t  *r;

    r = njs_vm_external(vm, njs_arg(args, nargs, 0));
    if (njs_slow_path(r == NULL)) {
        return NJS_ERROR;
    }

    if (ngx_http_send_special(r, NGX_HTTP_LAST) == NGX_ERROR) {
        return NJS_ERROR;
    }

    ctx = ngx_http_get_module_ctx(r, ngx_http_js_module);

    ctx->status = NGX_OK;

    return NJS_OK;
}


static njs_int_t
ngx_http_js_ext_return(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t unused)
{
    njs_str_t                  text;
    ngx_int_t                  status;
    njs_value_t               *value;
    ngx_http_js_ctx_t         *ctx;
    ngx_http_request_t        *r;
    ngx_http_complex_value_t   cv;

    r = njs_vm_external(vm, njs_arg(args, nargs, 0));
    if (njs_slow_path(r == NULL)) {
        return NJS_ERROR;
    }

    value = njs_arg(args, nargs, 1);
    if (!njs_value_is_valid_number(value)) {
        njs_vm_error(vm, "code is not a number");
        return NJS_ERROR;
    }

    status = njs_value_number(value);

    if (status < 0 || status > 999) {
        njs_vm_error(vm, "code is out of range");
        return NJS_ERROR;
    }

    if (ngx_http_js_string(vm, njs_arg(args, nargs, 2), &text) != NJS_OK) {
        njs_vm_error(vm, "failed to convert text");
        return NJS_ERROR;
    }

    ctx = ngx_http_get_module_ctx(r, ngx_http_js_module);

    if (status < NGX_HTTP_BAD_REQUEST || text.length) {
        ngx_memzero(&cv, sizeof(ngx_http_complex_value_t));

        cv.value.data = text.start;
        cv.value.len = text.length;

        ctx->status = ngx_http_send_response(r, status, NULL, &cv);

        if (ctx->status == NGX_ERROR) {
            njs_vm_error(vm, "failed to send response");
            return NJS_ERROR;
        }

    } else {
        ctx->status = status;
    }

    return NJS_OK;
}


static njs_int_t
ngx_http_js_ext_internal_redirect(njs_vm_t *vm, njs_value_t *args,
    njs_uint_t nargs, njs_index_t unused)
{
    njs_str_t            uri;
    ngx_http_js_ctx_t   *ctx;
    ngx_http_request_t  *r;

    r = njs_vm_external(vm, njs_arg(args, nargs, 0));
    if (njs_slow_path(r == NULL)) {
        return NJS_ERROR;
    }

    ctx = ngx_http_get_module_ctx(r, ngx_http_js_module);

    if (ngx_http_js_string(vm, njs_arg(args, nargs, 1), &uri) != NJS_OK) {
        njs_vm_error(vm, "failed to convert uri arg");
        return NJS_ERROR;
    }

    if (uri.length == 0) {
        njs_vm_error(vm, "uri is empty");
        return NJS_ERROR;
    }

    ctx->redirect_uri.data = uri.start;
    ctx->redirect_uri.len = uri.length;

    ctx->status = NGX_DONE;

    return NJS_OK;
}


static njs_int_t
ngx_http_js_ext_log(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t unused)
{
    return ngx_http_js_ext_log_core(vm, args, nargs, NGX_LOG_INFO);
}


static njs_int_t
ngx_http_js_ext_warn(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t unused)
{
    return ngx_http_js_ext_log_core(vm, args, nargs, NGX_LOG_WARN);
}


static njs_int_t
ngx_http_js_ext_error(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t unused)
{
    return ngx_http_js_ext_log_core(vm, args, nargs, NGX_LOG_ERR);
}


static njs_int_t
ngx_http_js_ext_log_core(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    ngx_uint_t level)
{
    njs_str_t            msg;
    ngx_connection_t    *c;
    ngx_log_handler_pt   handler;
    ngx_http_request_t  *r;

    r = njs_vm_external(vm, njs_arg(args, nargs, 0));
    if (njs_slow_path(r == NULL)) {
        return NJS_ERROR;
    }

    c = r->connection;

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
ngx_http_js_ext_get_http_version(njs_vm_t *vm, njs_value_t *value, void *obj,
    uintptr_t data)
{
    ngx_str_t            v;
    ngx_http_request_t  *r;

    r = (ngx_http_request_t *) obj;

    switch (r->http_version) {

    case NGX_HTTP_VERSION_9:
        ngx_str_set(&v, "0.9");
        break;

    case NGX_HTTP_VERSION_10:
        ngx_str_set(&v, "1.0");
        break;

    default: /* NGX_HTTP_VERSION_11 */
        ngx_str_set(&v, "1.1");
        break;
    }

    return njs_vm_value_string_set(vm, value, v.data, v.len);
}


static njs_int_t
ngx_http_js_ext_get_remote_address(njs_vm_t *vm, njs_value_t *value, void *obj,
    uintptr_t data)
{
    ngx_connection_t    *c;
    ngx_http_request_t  *r;

    r = (ngx_http_request_t *) obj;
    c = r->connection;

    return njs_vm_value_string_set(vm, value, c->addr_text.data,
                                   c->addr_text.len);
}


static njs_int_t
ngx_http_js_ext_get_request_body(njs_vm_t *vm, njs_value_t *value, void *obj,
    uintptr_t data)
{
    u_char              *p, *body;
    size_t               len;
    ngx_buf_t           *buf;
    njs_int_t            ret;
    njs_value_t         *request_body;
    ngx_chain_t         *cl;
    ngx_http_js_ctx_t   *ctx;
    ngx_http_request_t  *r;

    r = (ngx_http_request_t *) obj;

    ctx = ngx_http_get_module_ctx(r, ngx_http_js_module);
    request_body = (njs_value_t *) &ctx->request_body;

    if (!njs_value_is_null(request_body)) {
        njs_value_assign(value, request_body);
        return NJS_OK;
    }

    if (r->request_body == NULL || r->request_body->bufs == NULL) {
        njs_value_undefined_set(value);
        return NJS_OK;
    }

    if (r->request_body->temp_file) {
        njs_vm_error(vm, "request body is in a file");
        return NJS_ERROR;
    }

    cl = r->request_body->bufs;
    buf = cl->buf;

    if (cl->next == NULL) {
        len = buf->last - buf->pos;
        body = buf->pos;

        goto done;
    }

    len = buf->last - buf->pos;
    cl = cl->next;

    for ( /* void */ ; cl; cl = cl->next) {
        buf = cl->buf;
        len += buf->last - buf->pos;
    }

    p = ngx_pnalloc(r->pool, len);
    if (p == NULL) {
        njs_vm_memory_error(vm);
        return NJS_ERROR;
    }

    body = p;
    cl = r->request_body->bufs;

    for ( /* void */ ; cl; cl = cl->next) {
        buf = cl->buf;
        p = ngx_cpymem(p, buf->pos, buf->last - buf->pos);
    }

done:

    ret = njs_vm_value_string_set(vm, request_body, body, len);

    if (ret != NJS_OK) {
        return NJS_ERROR;
    }

    njs_value_assign(value, request_body);

    return NJS_OK;
}


static njs_int_t
ngx_http_js_ext_get_header_in(njs_vm_t *vm, njs_value_t *value, void *obj,
    uintptr_t data)
{
    u_char              *p, *end, sep;
    size_t               len;
    njs_str_t           *v;
    ngx_uint_t           i, n;
    ngx_array_t         *a;
    ngx_table_elt_t     *h, **hh;
    ngx_http_request_t  *r;

    r = (ngx_http_request_t *) obj;
    v = (njs_str_t *) data;

    if (v->length == njs_length("Cookie")
        && ngx_strncasecmp(v->start, (u_char *) "Cookie",
                           v->length) == 0)
    {
        sep = ';';
        a = &r->headers_in.cookies;
        goto multi;
    }

#if (NGX_HTTP_X_FORWARDED_FOR)
    if (v->length == njs_length("X-Forwarded-For")
        && ngx_strncasecmp(v->start, (u_char *) "X-Forwarded-For",
                           v->length) == 0)
    {
        sep = ',';
        a = &r->headers_in.x_forwarded_for;
        goto multi;
    }
#endif

    h = ngx_http_js_get_header(&r->headers_in.headers.part, v->start,
                               v->length);
    if (h == NULL) {
        njs_value_undefined_set(value);
        return NJS_OK;
    }

    return njs_vm_value_string_set(vm, value, h->value.data, h->value.len);

multi:

    /* Cookie, X-Forwarded-For */

    n = a->nelts;
    hh = a->elts;

    len = 0;

    for (i = 0; i < n; i++) {
        len += hh[i]->value.len + 2;
    }

    if (len == 0) {
        njs_value_undefined_set(value);
        return NJS_OK;
    }

    len -= 2;

    if (n == 1) {
        return njs_vm_value_string_set(vm, value, (*hh)->value.data,
                                       (*hh)->value.len);
    }

    p = njs_vm_value_string_alloc(vm, value, len);
    if (p == NULL) {
        return NJS_ERROR;
    }

    end = p + len;


    for (i = 0; /* void */ ; i++) {

        p = ngx_copy(p, hh[i]->value.data, hh[i]->value.len);

        if (p == end) {
            break;
        }

        *p++ = sep;
        *p++ = ' ';
    }

    return NJS_OK;
}


static njs_int_t
ngx_http_js_ext_keys_header_in(njs_vm_t *vm, void *obj, njs_value_t *keys)
{
    return ngx_http_js_ext_keys_header(vm, obj, keys,
                              offsetof(ngx_http_request_t, headers_in.headers));
}

static njs_int_t
ngx_http_js_ext_get_arg(njs_vm_t *vm, njs_value_t *value, void *obj,
    uintptr_t data)
{
    njs_str_t           *v;
    ngx_str_t            arg;
    ngx_http_request_t  *r;

    r = (ngx_http_request_t *) obj;
    v = (njs_str_t *) data;

    if (ngx_http_arg(r, v->start, v->length, &arg) == NGX_OK) {
        return njs_vm_value_string_set(vm, value, arg.data, arg.len);
    }

    njs_value_undefined_set(value);

    return NJS_OK;
}


static njs_int_t
ngx_http_js_ext_keys_arg(njs_vm_t *vm, void *obj, njs_value_t *keys)
{
    u_char              *v, *p, *start, *end;
    njs_int_t            rc;
    njs_value_t         *value;
    ngx_http_request_t  *r;

    r = (ngx_http_request_t *) obj;

    rc = njs_vm_array_alloc(vm, keys, 8);
    if (rc != NJS_OK) {
        return NJS_ERROR;
    }

    start = r->args.data;
    end = start + r->args.len;

    while (start < end) {
        p = ngx_strlchr(start, end, '&');
        if (p == NULL) {
            p = end;
        }

        v = ngx_strlchr(start, p, '=');
        if (v == NULL) {
            v = p;
        }

        if (v != start) {
            value = njs_vm_array_push(vm, keys);
            if (value == NULL) {
                return NJS_ERROR;
            }

            rc = njs_vm_value_string_set(vm, value, start, v - start);
            if (rc != NJS_OK) {
                return NJS_ERROR;
            }
        }

        start = p + 1;
    }

    return NJS_OK;
}


static njs_int_t
ngx_http_js_ext_get_variable(njs_vm_t *vm, njs_value_t *value, void *obj,
    uintptr_t data)
{
    njs_str_t                  *v;
    ngx_str_t                   name;
    ngx_uint_t                  key;
    ngx_http_request_t         *r;
    ngx_http_variable_value_t  *vv;

    r = (ngx_http_request_t *) obj;
    v = (njs_str_t *) data;

    name.data = v->start;
    name.len = v->length;

    key = ngx_hash_strlow(name.data, name.data, name.len);

    vv = ngx_http_get_variable(r, &name, key);
    if (vv == NULL || vv->not_found) {
        njs_value_undefined_set(value);
        return NJS_OK;
    }

    return njs_vm_value_string_set(vm, value, vv->data, vv->len);
}


static njs_int_t
ngx_http_js_ext_set_variable(njs_vm_t *vm, void *obj, uintptr_t data,
    njs_str_t *value)
{
    njs_str_t                  *val;
    ngx_str_t                   name;
    ngx_uint_t                  key;
    ngx_http_request_t         *r;
    ngx_http_variable_t        *v;
    ngx_http_variable_value_t  *vv;
    ngx_http_core_main_conf_t  *cmcf;

    r = (ngx_http_request_t *) obj;
    val = (njs_str_t *) data;

    name.data = val->start;
    name.len = val->length;

    cmcf = ngx_http_get_module_main_conf(r, ngx_http_core_module);

    key = ngx_hash_strlow(name.data, name.data, name.len);

    v = ngx_hash_find(&cmcf->variables_hash, key, name.data, name.len);

    if (v == NULL) {
        njs_vm_error(vm, "variable not found");
        return NJS_ERROR;
    }

    if (v->set_handler != NULL) {
        vv = ngx_pcalloc(r->pool, sizeof(ngx_http_variable_value_t));
        if (vv == NULL) {
            njs_vm_error(vm, "internal error");
            return NJS_ERROR;
        }

        vv->valid = 1;
        vv->not_found = 0;
        vv->data = value->start;
        vv->len = value->length;

        v->set_handler(r, vv, v->data);

        return NJS_OK;
    }

    if (!(v->flags & NGX_HTTP_VAR_INDEXED)) {
        njs_vm_error(vm, "variable is not writable");
        return NJS_ERROR;
    }

    vv = &r->variables[v->index];

    vv->valid = 1;
    vv->not_found = 0;

    vv->data = ngx_pnalloc(r->pool, value->length);
    if (vv->data == NULL) {
        njs_vm_error(vm, "internal error");
        return NJS_ERROR;
    }

    vv->len = value->length;
    ngx_memcpy(vv->data, value->start, vv->len);

    return NJS_OK;
}


static njs_int_t
ngx_http_js_promise_trampoline(njs_vm_t *vm, njs_value_t *args,
    njs_uint_t nargs, njs_index_t unused)
{
    njs_function_t      *callback;
    ngx_http_js_ctx_t   *ctx;
    ngx_http_request_t  *r;

    r = njs_vm_external(vm, njs_argument(args, 1));
    ctx = ngx_http_get_module_ctx(r, ngx_http_js_module);

    ngx_log_debug1(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
                   "js subrequest promise trampoline ctx: %p", ctx);

    callback = njs_value_function(njs_value_arg(&ctx->promise_callbacks[0]));

    return njs_vm_call(vm, callback, njs_argument(args, 1), 1);
}


static njs_int_t
ngx_http_js_ext_subrequest(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t unused)
{
    ngx_int_t                 rc, promise;
    njs_str_t                 uri_arg, args_arg, method_name, body_arg;
    ngx_uint_t                method, methods_max, has_body, detached;
    njs_value_t              *value, *arg, *options;
    njs_function_t           *callback;
    ngx_http_js_ctx_t        *ctx;
    ngx_http_request_t       *r, *sr;
    ngx_http_request_body_t  *rb;

    static const struct {
        ngx_str_t   name;
        ngx_uint_t  value;
    } methods[] = {
        { ngx_string("GET"),       NGX_HTTP_GET },
        { ngx_string("POST"),      NGX_HTTP_POST },
        { ngx_string("HEAD"),      NGX_HTTP_HEAD },
        { ngx_string("OPTIONS"),   NGX_HTTP_OPTIONS },
        { ngx_string("PROPFIND"),  NGX_HTTP_PROPFIND },
        { ngx_string("PUT"),       NGX_HTTP_PUT },
        { ngx_string("MKCOL"),     NGX_HTTP_MKCOL },
        { ngx_string("DELETE"),    NGX_HTTP_DELETE },
        { ngx_string("COPY"),      NGX_HTTP_COPY },
        { ngx_string("MOVE"),      NGX_HTTP_MOVE },
        { ngx_string("PROPPATCH"), NGX_HTTP_PROPPATCH },
        { ngx_string("LOCK"),      NGX_HTTP_LOCK },
        { ngx_string("UNLOCK"),    NGX_HTTP_UNLOCK },
        { ngx_string("PATCH"),     NGX_HTTP_PATCH },
        { ngx_string("TRACE"),     NGX_HTTP_TRACE },
    };

    static const njs_str_t args_key   = njs_str("args");
    static const njs_str_t method_key = njs_str("method");
    static const njs_str_t body_key = njs_str("body");
    static const njs_str_t detached_key = njs_str("detached");

    r = njs_vm_external(vm, njs_arg(args, nargs, 0));
    if (njs_slow_path(r == NULL)) {
        njs_vm_error(vm, "this is not an external");
        return NJS_ERROR;
    }

    ctx = ngx_http_get_module_ctx(r, ngx_http_js_module);

    if (ctx->vm != vm) {
        njs_vm_error(vm, "subrequest can only be created for "
                         "the primary request");
        return NJS_ERROR;
    }

    if (ngx_http_js_string(vm, njs_arg(args, nargs, 1), &uri_arg) != NJS_OK) {
        njs_vm_error(vm, "failed to convert uri arg");
        return NJS_ERROR;
    }

    if (uri_arg.length == 0) {
        njs_vm_error(vm, "uri is empty");
        return NJS_ERROR;
    }

    options = NULL;
    callback = NULL;

    method = 0;
    methods_max = sizeof(methods) / sizeof(methods[0]);

    args_arg.length = 0;
    args_arg.start = NULL;
    has_body = 0;
    promise = 0;
    detached = 0;

    arg = njs_arg(args, nargs, 2);

    if (njs_value_is_string(arg)) {
        if (njs_vm_value_to_string(vm, &args_arg, arg) != NJS_OK) {
            njs_vm_error(vm, "failed to convert args");
            return NJS_ERROR;
        }

    } else if (njs_value_is_function(arg)) {
        callback = njs_value_function(arg);

    } else if (njs_value_is_object(arg)) {
        options = arg;

    } else if (!njs_value_is_null_or_undefined(arg)) {
        njs_vm_error(vm, "failed to convert args");
        return NJS_ERROR;
    }

    if (options != NULL) {
        value = njs_vm_object_prop(vm, options, &args_key);
        if (value != NULL) {
            if (ngx_http_js_string(vm, value, &args_arg) != NJS_OK) {
                njs_vm_error(vm, "failed to convert options.args");
                return NJS_ERROR;
            }
        }

        value = njs_vm_object_prop(vm, options, &detached_key);
        if (value != NULL) {
            detached = njs_value_bool(value);
        }

        value = njs_vm_object_prop(vm, options, &method_key);
        if (value != NULL) {
            if (ngx_http_js_string(vm, value, &method_name) != NJS_OK) {
                njs_vm_error(vm, "failed to convert options.method");
                return NJS_ERROR;
            }

            while (method < methods_max) {
                if (method_name.length == methods[method].name.len
                    && ngx_memcmp(method_name.start, methods[method].name.data,
                                  method_name.length)
                       == 0)
                {
                    break;
                }

                method++;
            }
        }

        value = njs_vm_object_prop(vm, options, &body_key);
        if (value != NULL) {
            if (ngx_http_js_string(vm, value, &body_arg) != NJS_OK) {
                njs_vm_error(vm, "failed to convert options.body");
                return NJS_ERROR;
            }

            has_body = 1;
        }
    }

    arg = njs_arg(args, nargs, 3);

    if (callback == NULL && !njs_value_is_undefined(arg)) {
        if (!njs_value_is_function(arg)) {
            njs_vm_error(vm, "callback is not a function");
            return NJS_ERROR;

        } else {
            callback = njs_value_function(arg);
        }
    }

    if (detached && callback != NULL) {
        njs_vm_error(vm, "detached flag and callback are mutually exclusive");
        return NJS_ERROR;
    }

    if (!detached && callback == NULL) {
        callback = njs_vm_function_alloc(vm, ngx_http_js_promise_trampoline);
        if (callback == NULL) {
            goto memory_error;
        }

        promise = 1;
    }

    rc  = ngx_http_js_subrequest(r, &uri_arg, &args_arg, callback, &sr);
    if (rc != NGX_OK) {
        return NJS_ERROR;
    }

    if (method != methods_max) {
        sr->method = methods[method].value;
        sr->method_name = methods[method].name;

    } else {
        sr->method = NGX_HTTP_UNKNOWN;
        sr->method_name.len = method_name.length;
        sr->method_name.data = method_name.start;
    }

    sr->header_only = (sr->method == NGX_HTTP_HEAD) || (callback == NULL);

    if (has_body) {
        rb = ngx_pcalloc(r->pool, sizeof(ngx_http_request_body_t));
        if (rb == NULL) {
            goto memory_error;
        }

        if (body_arg.length != 0) {
            rb->bufs = ngx_alloc_chain_link(r->pool);
            if (rb->bufs == NULL) {
                goto memory_error;
            }

            rb->bufs->next = NULL;

            rb->bufs->buf = ngx_calloc_buf(r->pool);
            if (rb->bufs->buf == NULL) {
                goto memory_error;
            }

            rb->bufs->buf->memory = 1;
            rb->bufs->buf->last_buf = 1;

            rb->bufs->buf->pos = body_arg.start;
            rb->bufs->buf->last = body_arg.start + body_arg.length;
        }

        sr->request_body = rb;
        sr->headers_in.content_length_n = body_arg.length;
        sr->headers_in.chunked = 0;
    }

    if (promise) {
        ctx = ngx_pcalloc(sr->pool, sizeof(ngx_http_js_ctx_t));
        if (ctx == NULL) {
            return NGX_ERROR;
        }

        ngx_http_set_ctx(sr, ctx, ngx_http_js_module);

        return njs_vm_promise_create(vm, njs_vm_retval(vm),
                                     njs_value_arg(&ctx->promise_callbacks));
    }

    njs_value_undefined_set(njs_vm_retval(vm));

    return NJS_OK;

memory_error:

    njs_vm_error(ctx->vm, "internal error");

    return NJS_ERROR;
}


static ngx_int_t
ngx_http_js_subrequest(ngx_http_request_t *r, njs_str_t *uri_arg,
    njs_str_t *args_arg, njs_function_t *callback, ngx_http_request_t **sr)
{
    ngx_int_t                    flags;
    ngx_str_t                    uri, args;
    njs_vm_event_t               vm_event;
    ngx_http_js_ctx_t           *ctx;
    ngx_http_post_subrequest_t  *ps;

    ctx = ngx_http_get_module_ctx(r, ngx_http_js_module);

    flags = NGX_HTTP_SUBREQUEST_BACKGROUND;

    if (callback != NULL) {
        ps = ngx_palloc(r->pool, sizeof(ngx_http_post_subrequest_t));
        if (ps == NULL) {
            njs_vm_error(ctx->vm, "internal error");
            return NJS_ERROR;
        }

        vm_event = njs_vm_add_event(ctx->vm, callback, 1, NULL, NULL);
        if (vm_event == NULL) {
            njs_vm_error(ctx->vm, "internal error");
            return NJS_ERROR;
        }

        ps->handler = ngx_http_js_subrequest_done;
        ps->data = vm_event;

        flags |= NGX_HTTP_SUBREQUEST_IN_MEMORY;

    } else {
        ps = NULL;
        vm_event = NULL;
    }

    uri.len = uri_arg->length;
    uri.data = uri_arg->start;

    args.len = args_arg->length;
    args.data = args_arg->start;

    if (ngx_http_subrequest(r, &uri, args.len ? &args : NULL, sr, ps, flags)
        != NGX_OK)
    {
        if (vm_event != NULL) {
            njs_vm_del_event(ctx->vm, vm_event);
        }

        njs_vm_error(ctx->vm, "subrequest creation failed");
        return NJS_ERROR;
    }

    return NJS_OK;
}


static ngx_int_t
ngx_http_js_subrequest_done(ngx_http_request_t *r, void *data, ngx_int_t rc)
{
    njs_vm_event_t  vm_event = data;

    njs_int_t                 ret;
    ngx_http_js_ctx_t        *ctx;
    njs_opaque_value_t        reply;
    ngx_http_js_main_conf_t  *jmcf;

    if (rc != NGX_OK || r->connection->error || r->buffered) {
        return rc;
    }

    ctx = ngx_http_get_module_ctx(r, ngx_http_js_module);

    if (ctx && ctx->done) {
        return NGX_OK;
    }

    if (ctx == NULL) {
        ctx = ngx_pcalloc(r->pool, sizeof(ngx_http_js_ctx_t));
        if (ctx == NULL) {
            return NGX_ERROR;
        }

        ngx_http_set_ctx(r, ctx, ngx_http_js_module);
    }

    ctx->done = 1;

    jmcf = ngx_http_get_module_main_conf(r, ngx_http_js_module);

    ctx = ngx_http_get_module_ctx(r->parent, ngx_http_js_module);

    ngx_log_debug2(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
                   "js subrequest done s: %ui parent ctx: %p",
                   r->headers_out.status, ctx);

    if (ctx == NULL) {
        ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                      "js subrequest: failed to get the parent context");

        return NGX_ERROR;
    }

    ret = njs_vm_external_create(ctx->vm, njs_value_arg(&reply),
                                 jmcf->req_proto, r);
    if (ret != NJS_OK) {
        ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                      "js subrequest reply creation failed");

        return NGX_ERROR;
    }

    ngx_http_js_handle_event(r->parent, vm_event, njs_value_arg(&reply), 1);

    return NGX_OK;
}


static njs_int_t
ngx_http_js_ext_get_parent(njs_vm_t *vm, njs_value_t *value, void *obj,
    uintptr_t data)
{
    ngx_http_js_ctx_t   *ctx;
    ngx_http_request_t  *r;

    r = (ngx_http_request_t *) obj;

    ctx = r->parent ? ngx_http_get_module_ctx(r->parent, ngx_http_js_module)
                    : NULL;

    if (ctx == NULL || ctx->vm != vm) {
        njs_value_undefined_set(value);
        return NJS_OK;
    }

    njs_value_assign(value, njs_value_arg(&ctx->request));

    return NJS_OK;
}


static njs_int_t
ngx_http_js_ext_get_reply_body(njs_vm_t *vm, njs_value_t *value, void *obj,
    uintptr_t data)
{
    size_t               len;
    u_char              *p;
    ngx_buf_t           *b;
    ngx_http_request_t  *r;

    r = (ngx_http_request_t *) obj;

    b = r->out ? r->out->buf : NULL;

    len = b ? b->last - b->pos : 0;

    p = njs_vm_value_string_alloc(vm, value, len);
    if (p == NULL) {
        return NJS_ERROR;
    }

    if (len) {
        ngx_memcpy(p, b->pos, len);
    }

    return NJS_OK;
}


static njs_host_event_t
ngx_http_js_set_timer(njs_external_ptr_t external, uint64_t delay,
    njs_vm_event_t vm_event)
{
    ngx_event_t          *ev;
    ngx_http_request_t   *r;
    ngx_http_js_event_t  *js_event;

    r = (ngx_http_request_t *) external;

    ev = ngx_pcalloc(r->pool, sizeof(ngx_event_t));
    if (ev == NULL) {
        return NULL;
    }

    js_event = ngx_palloc(r->pool, sizeof(ngx_http_js_event_t));
    if (js_event == NULL) {
        return NULL;
    }

    js_event->request = r;
    js_event->vm_event = vm_event;
    js_event->ident = r->connection->fd;

    ev->data = js_event;
    ev->log = r->connection->log;
    ev->handler = ngx_http_js_timer_handler;

    ngx_add_timer(ev, delay);

    return ev;
}


static void
ngx_http_js_clear_timer(njs_external_ptr_t external, njs_host_event_t event)
{
    ngx_event_t  *ev = event;

    if (ev->timer_set) {
        ngx_del_timer(ev);
    }
}


static void
ngx_http_js_timer_handler(ngx_event_t *ev)
{
    ngx_connection_t     *c;
    ngx_http_request_t   *r;
    ngx_http_js_event_t  *js_event;

    js_event = (ngx_http_js_event_t *) ev->data;

    r = js_event->request;

    c = r->connection;

    ngx_http_js_handle_event(r, js_event->vm_event, NULL, 0);

    ngx_http_run_posted_requests(c);
}


static void
ngx_http_js_handle_event(ngx_http_request_t *r, njs_vm_event_t vm_event,
    njs_value_t *args, njs_uint_t nargs)
{
    njs_int_t           rc;
    njs_str_t           exception;
    ngx_http_js_ctx_t  *ctx;

    ctx = ngx_http_get_module_ctx(r, ngx_http_js_module);

    njs_vm_post_event(ctx->vm, vm_event, args, nargs);

    rc = njs_vm_run(ctx->vm);

    if (rc == NJS_ERROR) {
        njs_vm_retval_string(ctx->vm, &exception);

        ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                      "js exception: %*s", exception.length, exception.start);

        ngx_http_finalize_request(r, NGX_ERROR);
    }

    if (rc == NJS_OK) {
        ngx_http_post_request(r, NULL);
    }
}


static njs_int_t
ngx_http_js_string(njs_vm_t *vm, njs_value_t *value, njs_str_t *str)
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
ngx_http_js_include(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    ngx_http_js_main_conf_t *jmcf = conf;

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
                           ngx_read_fd_n " has read only %z of %O from \"%s\"",
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
    options.ops = &ngx_http_js_ops;
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

    cln->handler = ngx_http_js_cleanup_vm;
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

    jmcf->req_proto = njs_vm_external_prototype(jmcf->vm,
                                                &ngx_http_js_externals[0]);
    if (jmcf->req_proto == NULL) {
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0, "failed to add request proto");
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
ngx_http_js_set(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    ngx_str_t            *value, *fname;
    ngx_http_variable_t  *v;

    value = cf->args->elts;

    if (value[1].data[0] != '$') {
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                           "invalid variable name \"%V\"", &value[1]);
        return NGX_CONF_ERROR;
    }

    value[1].len--;
    value[1].data++;

    v = ngx_http_add_variable(cf, &value[1], NGX_HTTP_VAR_CHANGEABLE);
    if (v == NULL) {
        return NGX_CONF_ERROR;
    }

    fname = ngx_palloc(cf->pool, sizeof(ngx_str_t));
    if (fname == NULL) {
        return NGX_CONF_ERROR;
    }

    *fname = value[2];

    v->get_handler = ngx_http_js_variable;
    v->data = (uintptr_t) fname;

    return NGX_CONF_OK;
}


static char *
ngx_http_js_content(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    ngx_http_js_loc_conf_t *jlcf = conf;

    ngx_str_t                 *value;
    ngx_http_core_loc_conf_t  *clcf;

    if (jlcf->content.data) {
        return "is duplicate";
    }

    value = cf->args->elts;
    jlcf->content = value[1];

    clcf = ngx_http_conf_get_module_loc_conf(cf, ngx_http_core_module);
    clcf->handler = ngx_http_js_content_handler;

    return NGX_CONF_OK;
}


static void *
ngx_http_js_create_main_conf(ngx_conf_t *cf)
{
    ngx_http_js_main_conf_t  *conf;

    conf = ngx_pcalloc(cf->pool, sizeof(ngx_http_js_main_conf_t));
    if (conf == NULL) {
        return NULL;
    }

    /*
     * set by ngx_pcalloc():
     *
     *     conf->vm = NULL;
     *     conf->req_proto = NULL;
     */

    conf->paths = NGX_CONF_UNSET_PTR;

    return conf;
}


static void *
ngx_http_js_create_loc_conf(ngx_conf_t *cf)
{
    ngx_http_js_loc_conf_t  *conf;

    conf = ngx_pcalloc(cf->pool, sizeof(ngx_http_js_loc_conf_t));
    if (conf == NULL) {
        return NULL;
    }

    /*
     * set by ngx_pcalloc():
     *
     *     conf->content = { 0, NULL };
     */

    return conf;
}


static char *
ngx_http_js_merge_loc_conf(ngx_conf_t *cf, void *parent, void *child)
{
    return NGX_CONF_OK;
}
