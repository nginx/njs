
/*
 * Copyright (C) Roman Arutyunyan
 * Copyright (C) NGINX, Inc.
 */


#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_stream.h>

#include <nxt_auto_config.h>
#include <nxt_types.h>
#include <nxt_clang.h>
#include <nxt_string.h>
#include <nxt_stub.h>
#include <nxt_array.h>
#include <nxt_random.h>
#include <nxt_lvlhsh.h>
#include <nxt_mem_cache_pool.h>

#include <njscript.h>
#include <njs_vm.h>
#include <njs_string.h>


#define NGX_STREAM_JS_MCP_CLUSTER_SIZE    (2 * ngx_pagesize)
#define NGX_STREAM_JS_MCP_PAGE_ALIGNMENT  128
#define NGX_STREAM_JS_MCP_PAGE_SIZE       512
#define NGX_STREAM_JS_MCP_MIN_CHUNK_SIZE  16


#define ngx_stream_js_create_mem_cache_pool()                                 \
    nxt_mem_cache_pool_create(&ngx_stream_js_mem_cache_pool_proto, NULL, NULL,\
                              NGX_STREAM_JS_MCP_CLUSTER_SIZE,                 \
                              NGX_STREAM_JS_MCP_PAGE_ALIGNMENT,               \
                              NGX_STREAM_JS_MCP_PAGE_SIZE,                    \
                              NGX_STREAM_JS_MCP_MIN_CHUNK_SIZE)


typedef struct {
    njs_vm_t              *vm;
    njs_opaque_value_t     arg;
    ngx_str_t              access;
    ngx_str_t              preread;
    ngx_str_t              filter;
} ngx_stream_js_srv_conf_t;


typedef struct {
    njs_vm_t              *vm;
    njs_opaque_value_t    *arg;
    ngx_buf_t             *buf;
    ngx_chain_t           *free;
    ngx_chain_t           *busy;
    ngx_stream_session_t  *session;
    unsigned               from_upstream:1;
    unsigned               filter:1;
} ngx_stream_js_ctx_t;


static ngx_int_t ngx_stream_js_access_handler(ngx_stream_session_t *s);
static ngx_int_t ngx_stream_js_preread_handler(ngx_stream_session_t *s);
static ngx_int_t ngx_stream_js_phase_handler(ngx_stream_session_t *s,
    ngx_str_t *name);
static ngx_int_t ngx_stream_js_body_filter(ngx_stream_session_t *s,
    ngx_chain_t *in, ngx_uint_t from_upstream);
static ngx_int_t ngx_stream_js_variable(ngx_stream_session_t *s,
    ngx_stream_variable_value_t *v, uintptr_t data);
static void ngx_stream_js_cleanup_mem_cache_pool(void *data);
static ngx_int_t ngx_stream_js_init_vm(ngx_stream_session_t *s);

static void *ngx_stream_js_alloc(void *mem, size_t size);
static void *ngx_stream_js_calloc(void *mem, size_t size);
static void *ngx_stream_js_memalign(void *mem, size_t alignment, size_t size);
static void ngx_stream_js_free(void *mem, void *p);

static njs_ret_t ngx_stream_js_ext_get_remote_address(njs_vm_t *vm,
    njs_value_t *value, void *obj, uintptr_t data);
static njs_ret_t ngx_stream_js_ext_get_eof(njs_vm_t *vm, njs_value_t *value,
    void *obj, uintptr_t data);
static njs_ret_t ngx_stream_js_ext_get_from_upstream(njs_vm_t *vm,
    njs_value_t *value, void *obj, uintptr_t data);
static njs_ret_t ngx_stream_js_ext_get_buffer(njs_vm_t *vm, njs_value_t *value,
    void *obj, uintptr_t data);
static njs_ret_t ngx_stream_js_ext_set_buffer(njs_vm_t *vm, void *obj,
    uintptr_t data, nxt_str_t *value);
 static njs_ret_t ngx_stream_js_ext_log(njs_vm_t *vm, njs_value_t *args,
     nxt_uint_t nargs, njs_index_t unused);
static njs_ret_t ngx_stream_js_ext_get_variable(njs_vm_t *vm,
    njs_value_t *value, void *obj, uintptr_t data);
static njs_ret_t ngx_stream_js_ext_get_code(njs_vm_t *vm,
    njs_value_t *value, void *obj, uintptr_t data);

static char *ngx_stream_js_include(ngx_conf_t *cf, ngx_command_t *cmd,
    void *conf);
static char *ngx_stream_js_set(ngx_conf_t *cf, ngx_command_t *cmd,
    void *conf);
static void *ngx_stream_js_create_srv_conf(ngx_conf_t *cf);
static char *ngx_stream_js_merge_srv_conf(ngx_conf_t *cf, void *parent,
    void *child);
static ngx_int_t ngx_stream_js_init(ngx_conf_t *cf);


static ngx_command_t  ngx_stream_js_commands[] = {

    { ngx_string("js_include"),
      NGX_STREAM_MAIN_CONF|NGX_CONF_TAKE1,
      ngx_stream_js_include,
      NGX_STREAM_SRV_CONF_OFFSET,
      0,
      NULL },

    { ngx_string("js_set"),
      NGX_STREAM_MAIN_CONF|NGX_CONF_TAKE2,
      ngx_stream_js_set,
      NGX_STREAM_SRV_CONF_OFFSET,
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
    NULL,                          /* preconfiguration */
    ngx_stream_js_init,            /* postconfiguration */

    NULL,                          /* create main configuration */
    NULL,                          /* init main configuration */

    ngx_stream_js_create_srv_conf, /* create server configuration */
    ngx_stream_js_merge_srv_conf,  /* merge server configuration */
};


ngx_module_t  ngx_stream_js_module = {
    NGX_MODULE_V1,
    &ngx_stream_js_module_ctx,     /* module context */
    ngx_stream_js_commands,        /* module directives */
    NGX_STREAM_MODULE,             /* module type */
    NULL,                          /* init master */
    NULL,                          /* init module */
    NULL,                          /* init process */
    NULL,                          /* init thread */
    NULL,                          /* exit thread */
    NULL,                          /* exit process */
    NULL,                          /* exit master */
    NGX_MODULE_V1_PADDING
};


static const nxt_mem_proto_t  ngx_stream_js_mem_cache_pool_proto = {
    ngx_stream_js_alloc,
    ngx_stream_js_calloc,
    ngx_stream_js_memalign,
    NULL,
    ngx_stream_js_free,
    NULL,
    NULL,
};


static njs_external_t  ngx_stream_js_ext_session[] = {

    { nxt_string("remoteAddress"),
      NJS_EXTERN_PROPERTY,
      NULL,
      0,
      ngx_stream_js_ext_get_remote_address,
      NULL,
      NULL,
      NULL,
      NULL,
      NULL,
      0 },

    { nxt_string("eof"),
      NJS_EXTERN_PROPERTY,
      NULL,
      0,
      ngx_stream_js_ext_get_eof,
      NULL,
      NULL,
      NULL,
      NULL,
      NULL,
      0 },

    { nxt_string("fromUpstream"),
      NJS_EXTERN_PROPERTY,
      NULL,
      0,
      ngx_stream_js_ext_get_from_upstream,
      NULL,
      NULL,
      NULL,
      NULL,
      NULL,
      0 },

    { nxt_string("buffer"),
      NJS_EXTERN_PROPERTY,
      NULL,
      0,
      ngx_stream_js_ext_get_buffer,
      ngx_stream_js_ext_set_buffer,
      NULL,
      NULL,
      NULL,
      NULL,
      0 },

    { nxt_string("log"),
      NJS_EXTERN_METHOD,
      NULL,
      0,
      NULL,
      NULL,
      NULL,
      NULL,
      NULL,
      ngx_stream_js_ext_log,
      0 },

    { nxt_string("variables"),
      NJS_EXTERN_OBJECT,
      NULL,
      0,
      ngx_stream_js_ext_get_variable,
      NULL,
      NULL,
      NULL,
      NULL,
      NULL,
      0 },

    { nxt_string("OK"),
      NJS_EXTERN_PROPERTY,
      NULL,
      0,
      ngx_stream_js_ext_get_code,
      NULL,
      NULL,
      NULL,
      NULL,
      NULL,
      -NGX_OK },

    { nxt_string("DECLINED"),
      NJS_EXTERN_PROPERTY,
      NULL,
      0,
      ngx_stream_js_ext_get_code,
      NULL,
      NULL,
      NULL,
      NULL,
      NULL,
      -NGX_DECLINED },

    { nxt_string("AGAIN"),
      NJS_EXTERN_PROPERTY,
      NULL,
      0,
      ngx_stream_js_ext_get_code,
      NULL,
      NULL,
      NULL,
      NULL,
      NULL,
      -NGX_AGAIN },

    { nxt_string("ERROR"),
      NJS_EXTERN_PROPERTY,
      NULL,
      0,
      ngx_stream_js_ext_get_code,
      NULL,
      NULL,
      NULL,
      NULL,
      NULL,
      -NGX_ERROR },

    { nxt_string("ABORT"),
      NJS_EXTERN_PROPERTY,
      NULL,
      0,
      ngx_stream_js_ext_get_code,
      NULL,
      NULL,
      NULL,
      NULL,
      NULL,
      -NGX_ABORT },
};


static njs_external_t  ngx_stream_js_externals[] = {

    { nxt_string("$s"),
      NJS_EXTERN_OBJECT,
      ngx_stream_js_ext_session,
      nxt_nitems(ngx_stream_js_ext_session),
      NULL,
      NULL,
      NULL,
      NULL,
      NULL,
      NULL,
      0 },
};


static ngx_stream_filter_pt  ngx_stream_next_filter;


static ngx_int_t
ngx_stream_js_access_handler(ngx_stream_session_t *s)
{
    ngx_int_t                  rc;
    ngx_stream_js_srv_conf_t  *jscf;

    ngx_log_debug0(NGX_LOG_DEBUG_STREAM, s->connection->log, 0,
                   "js access handler");

    jscf = ngx_stream_get_module_srv_conf(s, ngx_stream_js_module);

    rc = ngx_stream_js_phase_handler(s, &jscf->access);

    if (rc == NGX_ABORT) {
        ngx_log_error(NGX_LOG_ERR, s->connection->log, 0,
                      "access forbidden by js");
        rc = NGX_STREAM_FORBIDDEN;
    }

    return rc;
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
    nxt_str_t                  fname, value, exception;
    ngx_int_t                  rc;
    njs_function_t            *func;
    ngx_connection_t          *c;
    ngx_stream_js_ctx_t       *ctx;

    if (name->len == 0) {
        return NGX_DECLINED;
    }

    c = s->connection;

    rc = ngx_stream_js_init_vm(s);
    if (rc != NGX_OK) {
        return rc;
    }

    ctx = ngx_stream_get_module_ctx(s, ngx_stream_js_module);

    fname.start = name->data;
    fname.length = name->len;

    func = njs_vm_function(ctx->vm, &fname);

    if (func == NULL) {
        ngx_log_error(NGX_LOG_ERR, c->log, 0, "js function \"%V\" not found",
                      name);
        return NGX_ERROR;
    }

    if (njs_vm_call(ctx->vm, func, ctx->arg, 1) != NJS_OK) {
        njs_vm_retval(ctx->vm, &exception);

        ngx_log_error(NGX_LOG_ERR, c->log, 0, "js exception: %*s",
                      exception.length, exception.start);

        return NGX_ERROR;
    }

    if (ctx->vm->retval.type == NJS_VOID) {
        return NGX_OK;
    }

    if (njs_vm_retval(ctx->vm, &value) != NJS_OK) {
        return NGX_ERROR;
    }

    ngx_log_debug2(NGX_LOG_DEBUG_STREAM, c->log, 0, "js return value: \"%*s\"",
                   value.length, value.start);

    rc = ngx_atoi(value.start, value.length);

    if (rc == NGX_ERROR) {
        ngx_log_error(NGX_LOG_ERR, c->log, 0,
                      "unexpected js return code: \"%*s\"",
                      value.length, value.start);
        return NGX_ERROR;
    }

    return -rc;
}


static ngx_int_t
ngx_stream_js_body_filter(ngx_stream_session_t *s, ngx_chain_t *in,
    ngx_uint_t from_upstream)
{
    nxt_str_t                  name, value, exception;
    ngx_int_t                  rc;
    ngx_chain_t               *out, *cl, **ll;
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

    ctx->filter = 1;

    name.start = jscf->filter.data;
    name.length = jscf->filter.len;

    func = njs_vm_function(ctx->vm, &name);

    if (func == NULL) {
        ngx_log_error(NGX_LOG_ERR, c->log, 0, "js function \"%V\" not found",
                      &jscf->filter);
        return NGX_ERROR;
    }

    ctx->from_upstream = from_upstream;

    ll = &out;

    while (in) {
        ctx->buf = in->buf;

        if (njs_vm_call(ctx->vm, func, ctx->arg, 1) != NJS_OK) {
            njs_vm_retval(ctx->vm, &exception);

            ngx_log_error(NGX_LOG_ERR, c->log, 0, "js exception: %*s",
                          exception.length, exception.start);

            return NGX_ERROR;
        }

        if (ctx->vm->retval.type != NJS_VOID) {
            if (njs_vm_retval(ctx->vm, &value) != NJS_OK) {
                return NGX_ERROR;
            }

            ngx_log_debug2(NGX_LOG_DEBUG_STREAM, c->log, 0,
                           "js return value: \"%*s\"",
                           value.length, value.start);

            if (value.length) {
                rc = ngx_atoi(value.start, value.length);

                if (rc != NGX_OK && rc != -NGX_ERROR) {
                    ngx_log_error(NGX_LOG_ERR, c->log, 0,
                                  "unexpected js return code: \"%*s\"",
                                  value.length, value.start);
                    return NGX_ERROR;
                }

                rc = -rc;

                if (rc == NGX_ERROR) {
                    return NGX_ERROR;
                }
            }
        }

        cl = ngx_alloc_chain_link(c->pool);
        if (cl == NULL) {
            return NGX_ERROR;
        }

        cl->buf = ctx->buf;

        *ll = cl;
        ll = &cl->next;

        in = in->next;
    }

    *ll = NULL;

    rc = ngx_stream_next_filter(s, out, from_upstream);

    ngx_chain_update_chains(c->pool, &ctx->free, &ctx->busy, &out,
                            (ngx_buf_tag_t) &ngx_stream_js_module);

    return rc;
}


static ngx_int_t
ngx_stream_js_variable(ngx_stream_session_t *s, ngx_stream_variable_value_t *v,
    uintptr_t data)
{
    ngx_str_t *fname = (ngx_str_t *) data;

    ngx_int_t             rc;
    nxt_str_t             name, value, exception;
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
        ngx_log_debug1(NGX_LOG_DEBUG_STREAM, s->connection->log, 0,
                       "js function \"%V\" not found", fname);
        v->not_found = 1;
        return NGX_OK;
    }

    if (njs_vm_call(ctx->vm, func, ctx->arg, 1) != NJS_OK) {
        njs_vm_retval(ctx->vm, &exception);

        ngx_log_error(NGX_LOG_ERR, s->connection->log, 0,
                      "js exception: %*s", exception.length, exception.start);

        v->not_found = 1;
        return NGX_OK;
    }

    if (njs_vm_retval(ctx->vm, &value) != NJS_OK) {
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
    void                      **ext;
    ngx_pool_cleanup_t         *cln;
    nxt_mem_cache_pool_t       *mcp;
    ngx_stream_js_ctx_t        *ctx;
    ngx_stream_js_srv_conf_t   *jscf;

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

        ngx_stream_set_ctx(s, ctx, ngx_stream_js_module);
    }

    if (ctx->vm) {
        return NGX_OK;
    }

    mcp = ngx_stream_js_create_mem_cache_pool();
    if (mcp == NULL) {
        return NGX_ERROR;
    }

    cln = ngx_pool_cleanup_add(s->connection->pool, 0);
    if (cln == NULL) {
        return NGX_ERROR;
    }

    cln->handler = ngx_stream_js_cleanup_mem_cache_pool;
    cln->data = mcp;

    ext = ngx_palloc(s->connection->pool, sizeof(void *));
    if (ext == NULL) {
        return NGX_ERROR;
    }

    *ext = s;

    ctx->vm = njs_vm_clone(jscf->vm, mcp, ext);
    if (ctx->vm == NULL) {
        return NGX_ERROR;
    }

    if (njs_vm_run(ctx->vm) != NJS_OK) {
        return NGX_ERROR;
    }

    ctx->arg = &jscf->arg;

    return NGX_OK;
}


static void
ngx_stream_js_cleanup_mem_cache_pool(void *data)
{
    nxt_mem_cache_pool_t *mcp = data;

    nxt_mem_cache_pool_destroy(mcp);
}


static void *
ngx_stream_js_alloc(void *mem, size_t size)
{
    return ngx_alloc(size, ngx_cycle->log);
}


static void *
ngx_stream_js_calloc(void *mem, size_t size)
{
    return ngx_calloc(size, ngx_cycle->log);
}


static void *
ngx_stream_js_memalign(void *mem, size_t alignment, size_t size)
{
    return ngx_memalign(alignment, size, ngx_cycle->log);
}


static void
ngx_stream_js_free(void *mem, void *p)
{
    ngx_free(p);
}


static njs_ret_t
ngx_stream_js_ext_get_remote_address(njs_vm_t *vm, njs_value_t *value,
    void *obj, uintptr_t data)
{
    ngx_connection_t      *c;
    ngx_stream_session_t  *s;

    s = (ngx_stream_session_t *) obj;
    c = s->connection;

    return njs_string_create(vm, value, c->addr_text.data, c->addr_text.len, 0);
}


static njs_ret_t
ngx_stream_js_ext_get_eof(njs_vm_t *vm, njs_value_t *value, void *obj,
    uintptr_t data)
{
    ngx_buf_t             *b;
    ngx_connection_t      *c;
    ngx_stream_js_ctx_t   *ctx;
    ngx_stream_session_t  *s;

    s = (ngx_stream_session_t *) obj;
    c = s->connection;
    ctx = ngx_stream_get_module_ctx(s, ngx_stream_js_module);

    b = ctx->filter ? ctx->buf : c->buffer;

    *value = (b && b->last_buf ? njs_value_true : njs_value_false);

    return NJS_OK;
}


static njs_ret_t
ngx_stream_js_ext_get_from_upstream(njs_vm_t *vm, njs_value_t *value, void *obj,
    uintptr_t data)
{
    ngx_stream_js_ctx_t   *ctx;
    ngx_stream_session_t  *s;

    s = (ngx_stream_session_t *) obj;
    ctx = ngx_stream_get_module_ctx(s, ngx_stream_js_module);

    *value = (ctx->from_upstream ? njs_value_true : njs_value_false);

    return NJS_OK;
}


static njs_ret_t
ngx_stream_js_ext_get_buffer(njs_vm_t *vm, njs_value_t *value, void *obj,
    uintptr_t data)
{
    size_t                 len;
    u_char                *p;
    ngx_buf_t             *b;
    ngx_connection_t      *c;
    ngx_stream_js_ctx_t   *ctx;
    ngx_stream_session_t  *s;

    s = (ngx_stream_session_t *) obj;
    c = s->connection;
    ctx = ngx_stream_get_module_ctx(s, ngx_stream_js_module);

    b = ctx->filter ? ctx->buf : c->buffer;

    len = b ? b->last - b->pos : 0;

    p = njs_string_alloc(vm, value, len, 0);
    if (p == NULL) {
        return NJS_ERROR;
    }

    if (len) {
        ngx_memcpy(p, b->pos, len);
    }

    return NJS_OK;
}


static njs_ret_t
ngx_stream_js_ext_set_buffer(njs_vm_t *vm, void *obj, uintptr_t data,
    nxt_str_t *value)
{
    ngx_buf_t             *b;
    ngx_chain_t           *cl;
    ngx_connection_t      *c;
    ngx_stream_js_ctx_t   *ctx;
    ngx_stream_session_t  *s;

    s = (ngx_stream_session_t *) obj;
    c = s->connection;
    ctx = ngx_stream_get_module_ctx(s, ngx_stream_js_module);

    ngx_log_debug2(NGX_LOG_DEBUG_STREAM, c->log, 0,
                   "stream js set buffer \"%*s\"", value->length, value->start);

    if (!ctx->filter) {
        ngx_log_error(NGX_LOG_WARN, c->log, 0,
                      "cannot set buffer in this handler");
        return NJS_OK;
    }

    cl = ngx_chain_get_free_buf(c->pool, &ctx->free);
    if (cl == NULL) {
        return NJS_ERROR;
    }

    b = cl->buf;

    ngx_free_chain(c->pool, cl);

    b->last_buf = ctx->buf->last_buf;
    b->memory = (value->length ? 1 : 0);
    b->sync = (value->length ? 0 : 1);
    b->tag = (ngx_buf_tag_t) &ngx_stream_js_module;

    b->start = value->start;
    b->end = value->start + value->length;
    b->pos = b->start;
    b->last = b->end;

    if (ctx->buf->tag != (ngx_buf_tag_t) &ngx_stream_js_module) {
        ctx->buf->pos = ctx->buf->last;

    } else {
        cl = ngx_alloc_chain_link(c->pool);
        if (cl == NULL) {
            return NJS_ERROR;
        }

        cl->buf = ctx->buf;
        cl->next = ctx->free;
        ctx->free = cl;
    }

    ctx->buf = b;

    return NJS_OK;
}


static njs_ret_t
ngx_stream_js_ext_log(njs_vm_t *vm, njs_value_t *args, nxt_uint_t nargs,
    njs_index_t unused)
{
    nxt_str_t              msg;
    ngx_connection_t      *c;
    ngx_log_handler_pt     handler;
    ngx_stream_session_t  *s;

    s = njs_value_data(njs_argument(args, 0));
    c = s->connection;

    if (njs_value_to_ext_string(vm, &msg, njs_argument(args, 1)) == NJS_ERROR) {
        return NJS_ERROR;
    }

    handler = c->log->handler;
    c->log->handler = NULL;

    ngx_log_error(NGX_LOG_INFO, c->log, 0, "js: %*s", msg.length, msg.start);

    c->log->handler = handler;

    return NJS_OK;
}


static njs_ret_t
ngx_stream_js_ext_get_variable(njs_vm_t *vm, njs_value_t *value, void *obj,
    uintptr_t data)
{
    nxt_str_t                    *v;
    ngx_str_t                     name;
    ngx_uint_t                    key;
    ngx_stream_session_t         *s;
    ngx_stream_variable_value_t  *vv;

    s = (ngx_stream_session_t *) obj;
    v = (nxt_str_t *) data;

    name.data = v->start;
    name.len = v->length;

    key = ngx_hash_strlow(name.data, name.data, name.len);

    vv = ngx_stream_get_variable(s, &name, key);
    if (vv == NULL || vv->not_found) {
        return njs_string_create(vm, value, NULL, 0, 0);
    }

    return njs_string_create(vm, value, vv->data, vv->len, 0);
}


static njs_ret_t
ngx_stream_js_ext_get_code(njs_vm_t *vm, njs_value_t *value, void *obj,
    uintptr_t data)
{
    ngx_memzero(value, sizeof(njs_value_t));

    value->data.type = NJS_NUMBER;
    value->data.u.number = data;

    return NJS_OK;
}


static char *
ngx_stream_js_include(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    ngx_stream_js_srv_conf_t *jscf = conf;

    size_t                 size;
    u_char                *start, *end;
    ssize_t                n;
    ngx_fd_t               fd;
    ngx_str_t             *value, file;
    nxt_int_t              rc;
    nxt_str_t              text, ext;
    njs_vm_opt_t           options;
    nxt_lvlhsh_t           externals;
    ngx_file_info_t        fi;
    ngx_pool_cleanup_t    *cln;
    nxt_mem_cache_pool_t  *mcp;

    if (jscf->vm) {
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
        ngx_log_error(NGX_LOG_EMERG, cf->log, ngx_errno,
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
        ngx_log_error(NGX_LOG_ALERT, cf->log, ngx_errno,
                      ngx_read_fd_n " \"%s\" failed", file.data);

        (void) ngx_close_file(fd);
        return NGX_CONF_ERROR;
    }

    if ((size_t) n != size) {
        ngx_log_error(NGX_LOG_ALERT, cf->log, 0,
                      ngx_read_fd_n " has read only %z of %uz from \"%s\"",
                      n, size, file.data);

        (void) ngx_close_file(fd);
        return NGX_CONF_ERROR;
    }

    if (ngx_close_file(fd) == NGX_FILE_ERROR) {
        ngx_log_error(NGX_LOG_ALERT, cf->log, ngx_errno,
                      ngx_close_file_n " %s failed", file.data);
    }

    end = start + size;

    mcp = ngx_stream_js_create_mem_cache_pool();
    if (mcp == NULL) {
        return NGX_CONF_ERROR;
    }

    cln = ngx_pool_cleanup_add(cf->pool, 0);
    if (cln == NULL) {
        return NGX_CONF_ERROR;
    }

    cln->handler = ngx_stream_js_cleanup_mem_cache_pool;
    cln->data = mcp;

    nxt_lvlhsh_init(&externals);

    if (njs_vm_external_add(&externals, mcp, 0, ngx_stream_js_externals,
                            nxt_nitems(ngx_stream_js_externals))
        != NJS_OK)
    {
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0, "could not add js externals");
        return NGX_CONF_ERROR;
    }

    ngx_memzero(&options, sizeof(njs_vm_opt_t));

    options.mcp = mcp;
    options.backtrace = 1;
    options.externals_hash = &externals;

    jscf->vm = njs_vm_create(&options);
    if (jscf->vm == NULL) {
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0, "failed to create JS VM");
        return NGX_CONF_ERROR;
    }

    rc = njs_vm_compile(jscf->vm, &start, end);

    if (rc != NJS_OK) {
        njs_vm_retval(jscf->vm, &text);

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

    ext = nxt_string_value("$s");

    if (njs_vm_external(jscf->vm, NULL, &ext, &jscf->arg) != NJS_OK) {
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                       "js external \"%*s\" not found", ext.length, ext.start);
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
     *     conf->arg = NULL;
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

    if (conf->vm == NULL) {
        conf->vm = prev->vm;
        conf->arg = prev->arg;
    }

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
