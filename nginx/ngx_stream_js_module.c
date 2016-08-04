
/*
 * Copyright (C) Roman Arutyunyan
 * Copyright (C) NGINX, Inc.
 */


#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_stream.h>

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
} ngx_stream_js_srv_conf_t;


typedef struct {
    njs_vm_t              *vm;
    njs_opaque_value_t    *arg;
} ngx_stream_js_ctx_t;


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
static njs_ret_t ngx_stream_js_ext_log(njs_vm_t *vm, njs_value_t *args,
    nxt_uint_t nargs, njs_index_t unused);
static njs_ret_t ngx_stream_js_ext_get_variable(njs_vm_t *vm,
    njs_value_t *value, void *obj, uintptr_t data);

static char *ngx_stream_js_include(ngx_conf_t *cf, ngx_command_t *cmd,
    void *conf);
static char *ngx_stream_js_set(ngx_conf_t *cf, ngx_command_t *cmd,
    void *conf);
static void *ngx_stream_js_create_srv_conf(ngx_conf_t *cf);
static char *ngx_stream_js_merge_srv_conf(ngx_conf_t *cf, void *parent,
    void *child);


static ngx_command_t  ngx_stream_js_commands[] = {

    { ngx_string("js_include"),
      NGX_STREAM_MAIN_CONF|NGX_STREAM_SRV_CONF|NGX_CONF_TAKE1,
      ngx_stream_js_include,
      NGX_STREAM_SRV_CONF_OFFSET,
      0,
      NULL },

    { ngx_string("js_set"),
      NGX_STREAM_MAIN_CONF|NGX_STREAM_SRV_CONF|NGX_CONF_TAKE2,
      ngx_stream_js_set,
      NGX_STREAM_SRV_CONF_OFFSET,
      0,
      NULL },

    ngx_null_command
};


static ngx_stream_module_t  ngx_stream_js_module_ctx = {
    NULL,                          /* preconfiguration */
    NULL,                          /* postconfiguration */

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
        njs_vm_exception(ctx->vm, &exception);

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
ngx_stream_js_ext_log(njs_vm_t *vm, njs_value_t *args, nxt_uint_t nargs,
    njs_index_t unused)
{
    nxt_str_t              msg;
    ngx_connection_t      *c;
    ngx_log_handler_pt     handler;
    ngx_stream_session_t  *s;

    s = njs_value_data(njs_argument(args, 0));
    c = s->connection;

    if (njs_value_to_ext_string(vm, &msg, njs_argument(args, 1)) == NJS_ERROR)
    {
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
    nxt_lvlhsh_t           externals;
    ngx_file_info_t        fi;
    njs_vm_shared_t       *shared;
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
        return NULL;
    }

    cln->handler = ngx_stream_js_cleanup_mem_cache_pool;
    cln->data = mcp;

    shared = NULL;

    nxt_lvlhsh_init(&externals);

    if (njs_vm_external_add(&externals, mcp, 0, ngx_stream_js_externals,
                            nxt_nitems(ngx_stream_js_externals))
        != NJS_OK)
    {
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0, "could not add js externals");
        return NGX_CONF_ERROR;
    }

    jscf->vm = njs_vm_create(mcp, &shared, &externals);
    if (jscf->vm == NULL) {
        return NGX_CONF_ERROR;
    }

    rc = njs_vm_compile(jscf->vm, &start, end, NULL);

    if (rc != NJS_OK) {
        njs_vm_exception(jscf->vm, &text);

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

    return NGX_CONF_OK;
}
