
/*
 * Copyright (C) Roman Arutyunyan
 * Copyright (C) NGINX, Inc.
 */


#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>

#include <nxt_types.h>
#include <nxt_clang.h>
#include <nxt_stub.h>
#include <nxt_lvlhsh.h>
#include <nxt_mem_cache_pool.h>
#include <njscript.h>


#define NGX_HTTP_JS_MCP_CLUSTER_SIZE    (2 * ngx_pagesize)
#define NGX_HTTP_JS_MCP_PAGE_ALIGNMENT  128
#define NGX_HTTP_JS_MCP_PAGE_SIZE       512
#define NGX_HTTP_JS_MCP_MIN_CHUNK_SIZE  16


#define ngx_http_js_create_mem_cache_pool()                                   \
    nxt_mem_cache_pool_create(&ngx_http_js_mem_cache_pool_proto, NULL, NULL,  \
                              NGX_HTTP_JS_MCP_CLUSTER_SIZE,                   \
                              NGX_HTTP_JS_MCP_PAGE_ALIGNMENT,                 \
                              NGX_HTTP_JS_MCP_PAGE_SIZE,                      \
                              NGX_HTTP_JS_MCP_MIN_CHUNK_SIZE)


typedef struct {
    njs_vm_t            *vm;
    njs_function_t      *function;
    njs_opaque_value_t   args[2];
} ngx_http_js_ctx_t;


typedef struct {
    ngx_http_js_ctx_t   js;
} ngx_http_js_loc_conf_t;


typedef struct {
    ngx_list_part_t     *part;
    ngx_uint_t           item;
} ngx_http_js_table_entry_t;


static ngx_int_t ngx_http_js_handler(ngx_http_request_t *r);
static ngx_int_t ngx_http_js_variable(ngx_http_request_t *r,
    ngx_http_variable_value_t *v, uintptr_t data);
static ngx_int_t ngx_http_js_vm_run(ngx_http_request_t *r,
    ngx_http_js_ctx_t *js, nxt_str_t *value);
static void ngx_http_js_cleanup_mem_cache_pool(void *data);

static void *ngx_http_js_alloc(void *mem, size_t size);
static void *ngx_http_js_calloc(void *mem, size_t size);
static void *ngx_http_js_memalign(void *mem, size_t alignment, size_t size);
static void ngx_http_js_free(void *mem, void *p);

static njs_ret_t ngx_http_js_ext_get_string(njs_vm_t *vm, njs_value_t *value,
    void *obj, uintptr_t data);
static njs_ret_t ngx_http_js_ext_set_string(njs_vm_t *vm, void *obj,
    uintptr_t data, nxt_str_t *value);
static njs_ret_t ngx_http_js_ext_foreach_header(njs_vm_t *vm, void *obj,
    void *next, uintptr_t data);
static njs_ret_t ngx_http_js_ext_next_header(njs_vm_t *vm, njs_value_t *value,
    void *obj, void *next);
static ngx_table_elt_t *ngx_http_js_get_header(ngx_list_part_t *part,
    u_char *data, size_t len);
static njs_ret_t ngx_http_js_ext_get_header_out(njs_vm_t *vm,
    njs_value_t *value, void *obj, uintptr_t data);
static njs_ret_t ngx_http_js_ext_set_header_out(njs_vm_t *vm, void *obj,
    uintptr_t data, nxt_str_t *value);
static njs_ret_t ngx_http_js_ext_foreach_header_out(njs_vm_t *vm, void *obj,
    void *next); /*FIXME*/
static njs_ret_t ngx_http_js_ext_get_status(njs_vm_t *vm, njs_value_t *value,
    void *obj, uintptr_t data);
static njs_ret_t ngx_http_js_ext_set_status(njs_vm_t *vm, void *obj,
    uintptr_t data, nxt_str_t *value);
static njs_ret_t ngx_http_js_ext_get_content_length(njs_vm_t *vm,
    njs_value_t *value, void *obj, uintptr_t data);
static njs_ret_t ngx_http_js_ext_set_content_length(njs_vm_t *vm, void *obj,
    uintptr_t data, nxt_str_t *value);
static njs_ret_t ngx_http_js_ext_send_header(njs_vm_t *vm, njs_value_t *args,
    nxt_uint_t nargs, njs_index_t unused);
static njs_ret_t ngx_http_js_ext_send(njs_vm_t *vm, njs_value_t *args,
    nxt_uint_t nargs, njs_index_t unused);
static njs_ret_t ngx_http_js_ext_finish(njs_vm_t *vm, njs_value_t *args,
    nxt_uint_t nargs, njs_index_t unused);
static njs_ret_t ngx_http_js_ext_get_http_version(njs_vm_t *vm,
    njs_value_t *value, void *obj, uintptr_t data);
static njs_ret_t ngx_http_js_ext_get_remote_address(njs_vm_t *vm,
    njs_value_t *value, void *obj, uintptr_t data);
static njs_ret_t ngx_http_js_ext_get_header_in(njs_vm_t *vm, njs_value_t *value,
    void *obj, uintptr_t data);
static njs_ret_t ngx_http_js_ext_foreach_header_in(njs_vm_t *vm, void *obj,
    void *next); /*FIXME*/
static njs_ret_t ngx_http_js_ext_get_arg(njs_vm_t *vm, njs_value_t *value,
    void *obj, uintptr_t data);
static njs_ret_t ngx_http_js_ext_foreach_arg(njs_vm_t *vm, void *obj,
    void *next);
static njs_ret_t ngx_http_js_ext_next_arg(njs_vm_t *vm, njs_value_t *value,
    void *obj, void *next);

static char *ngx_http_js_run(ngx_conf_t *cf, ngx_command_t *cmd, void *conf);
static char *ngx_http_js_set(ngx_conf_t *cf, ngx_command_t *cmd, void *conf);
static char *ngx_http_js_compile(ngx_conf_t *cf, ngx_http_js_ctx_t *jctx,
    ngx_str_t *script);
static void *ngx_http_js_create_loc_conf(ngx_conf_t *cf);
static char *ngx_http_js_merge_loc_conf(ngx_conf_t *cf, void *parent,
    void *child);


static ngx_command_t  ngx_http_js_commands[] = {

    { ngx_string("js_run"),
      NGX_HTTP_LOC_CONF|NGX_HTTP_LMT_CONF|NGX_CONF_TAKE1,
      ngx_http_js_run,
      NGX_HTTP_LOC_CONF_OFFSET,
      0,
      NULL },

    { ngx_string("js_set"),
      NGX_HTTP_MAIN_CONF|NGX_CONF_TAKE2,
      ngx_http_js_set,
      NGX_HTTP_LOC_CONF_OFFSET,
      0,
      NULL },

    ngx_null_command
};


static ngx_http_module_t  ngx_http_js_module_ctx = {
    NULL,                          /* preconfiguration */
    NULL,                          /* postconfiguration */

    NULL,                          /* create main configuration */
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


static const nxt_mem_proto_t  ngx_http_js_mem_cache_pool_proto = {
    ngx_http_js_alloc,
    ngx_http_js_calloc,
    ngx_http_js_memalign,
    NULL,
    ngx_http_js_free,
    NULL,
    NULL,
};


static njs_external_t  ngx_http_js_ext_response[] = {

    { nxt_string("headers"),
      NJS_EXTERN_OBJECT,
      NULL,
      0,
      ngx_http_js_ext_get_header_out,
      ngx_http_js_ext_set_header_out,
      NULL,
      ngx_http_js_ext_foreach_header_out,
      ngx_http_js_ext_next_header,
      NULL,
      0 },

    { nxt_string("status"),
      NJS_EXTERN_PROPERTY,
      NULL,
      0,
      ngx_http_js_ext_get_status,
      ngx_http_js_ext_set_status,
      NULL,
      NULL,
      NULL,
      NULL,
      offsetof(ngx_http_request_t, headers_out.status) },

    { nxt_string("contentType"),
      NJS_EXTERN_PROPERTY,
      NULL,
      0,
      ngx_http_js_ext_get_string,
      ngx_http_js_ext_set_string,
      NULL,
      NULL,
      NULL,
      NULL,
      offsetof(ngx_http_request_t, headers_out.content_type) },

    { nxt_string("contentLength"),
      NJS_EXTERN_PROPERTY,
      NULL,
      0,
      ngx_http_js_ext_get_content_length,
      ngx_http_js_ext_set_content_length,
      NULL,
      NULL,
      NULL,
      NULL,
      0 },

    { nxt_string("sendHeader"),
      NJS_EXTERN_METHOD,
      NULL,
      0,
      NULL,
      NULL,
      NULL,
      NULL,
      NULL,
      ngx_http_js_ext_send_header,
      0 },

    { nxt_string("send"),
      NJS_EXTERN_METHOD,
      NULL,
      0,
      NULL,
      NULL,
      NULL,
      NULL,
      NULL,
      ngx_http_js_ext_send,
      0 },

    { nxt_string("finish"),
      NJS_EXTERN_METHOD,
      NULL,
      0,
      NULL,
      NULL,
      NULL,
      NULL,
      NULL,
      ngx_http_js_ext_finish,
      0 },
};


static njs_external_t  ngx_http_js_ext_request[] = {

    { nxt_string("response"),
      NJS_EXTERN_OBJECT,
      ngx_http_js_ext_response,
      nxt_nitems(ngx_http_js_ext_response),
      NULL,
      NULL,
      NULL,
      NULL,
      NULL,
      NULL,
      0 },

    { nxt_string("uri"),
      NJS_EXTERN_PROPERTY,
      NULL,
      0,
      ngx_http_js_ext_get_string,
      NULL,
      NULL,
      NULL,
      NULL,
      NULL,
      offsetof(ngx_http_request_t, uri) },

    { nxt_string("method"),
      NJS_EXTERN_PROPERTY,
      NULL,
      0,
      ngx_http_js_ext_get_string,
      NULL,
      NULL,
      NULL,
      NULL,
      NULL,
      offsetof(ngx_http_request_t, method_name) },

    { nxt_string("httpVersion"),
      NJS_EXTERN_PROPERTY,
      NULL,
      0,
      ngx_http_js_ext_get_http_version,
      NULL,
      NULL,
      NULL,
      NULL,
      NULL,
      0 },

    { nxt_string("remoteAddress"),
      NJS_EXTERN_PROPERTY,
      NULL,
      0,
      ngx_http_js_ext_get_remote_address,
      NULL,
      NULL,
      NULL,
      NULL,
      NULL,
      0 },

    { nxt_string("headers"),
      NJS_EXTERN_OBJECT,
      NULL,
      0,
      ngx_http_js_ext_get_header_in,
      NULL,
      NULL,
      ngx_http_js_ext_foreach_header_in,
      ngx_http_js_ext_next_header,
      NULL,
      0 },

    { nxt_string("args"),
      NJS_EXTERN_OBJECT,
      NULL,
      0,
      ngx_http_js_ext_get_arg,
      NULL,
      NULL,
      ngx_http_js_ext_foreach_arg,
      ngx_http_js_ext_next_arg,
      NULL,
      0 },
};


static njs_external_t  ngx_http_js_externals[] = {

    { nxt_string("$r"),
      NJS_EXTERN_OBJECT,
      ngx_http_js_ext_request,
      nxt_nitems(ngx_http_js_ext_request),
      NULL,
      NULL,
      NULL,
      NULL,
      NULL,
      NULL,
      0 },
};


static ngx_int_t
ngx_http_js_handler(ngx_http_request_t *r)
{
    ngx_int_t                rc;
    ngx_http_js_loc_conf_t  *jlcf;

    ngx_log_debug0(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
                   "http js handler");

    jlcf = ngx_http_get_module_loc_conf(r, ngx_http_js_module);

    rc = ngx_http_js_vm_run(r, &jlcf->js, NULL);

    if (rc == NGX_OK) {
        return rc;
    }

    return NGX_ERROR;
}


static ngx_int_t
ngx_http_js_variable(ngx_http_request_t *r, ngx_http_variable_value_t *v,
    uintptr_t data)
{
    ngx_http_js_ctx_t *js = (ngx_http_js_ctx_t *) data;

    ngx_int_t  rc;
    nxt_str_t  value;

    ngx_log_debug0(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
                   "http js variable handler");

    rc = ngx_http_js_vm_run(r, js, &value);

    if (rc == NXT_ERROR) {
        return NGX_ERROR;
    }

    if (rc == NGX_OK) {
        v->len = value.len;
        v->valid = 1;
        v->no_cacheable = 0;
        v->not_found = 0;
        v->data = value.data;

    } else {
        v->not_found = 1;
    }

    ngx_log_debug0(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
                   "http js variable done");

    return NGX_OK;
}


static ngx_int_t
ngx_http_js_vm_run(ngx_http_request_t *r, ngx_http_js_ctx_t *js,
    nxt_str_t *value)
{
    njs_vm_t              *nvm;
    nxt_int_t              ret;
    nxt_str_t              exception;
    ngx_pool_cleanup_t    *cln;
    nxt_mem_cache_pool_t  *mcp;

    mcp = ngx_http_js_create_mem_cache_pool();
    if (mcp == NULL) {
        return NGX_ERROR;
    }

    cln = ngx_pool_cleanup_add(r->pool, 0);
    if (cln == NULL) {
        return NGX_ERROR;
    }

    cln->handler = ngx_http_js_cleanup_mem_cache_pool;
    cln->data = mcp;

    /* The double cast is required by GCC 4.1. */
    nvm = njs_vm_clone(js->vm, mcp, (void **) (void *) &r);
    if (nvm == NULL) {
        return NGX_ERROR;
    }

    if (js->function) {
        ret = njs_vm_call(nvm, js->function, js->args, 2);

    } else {
        ret = njs_vm_run(nvm);
    }

    if (ret == NJS_OK) {

        if (value != NULL) {
            if (njs_vm_retval(nvm, value) != NJS_OK) {
                return NGX_ERROR;
            }
        }

    } else {
        njs_vm_exception(nvm, &exception);

        ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                      "js exception: %*s", exception.len, exception.data);

        return NGX_DECLINED;
    }

    return NGX_OK;
}


static void
ngx_http_js_cleanup_mem_cache_pool(void *data)
{
    nxt_mem_cache_pool_t *mcp = data;

    nxt_mem_cache_pool_destroy(mcp);
}


static void *
ngx_http_js_alloc(void *mem, size_t size)
{
    return ngx_alloc(size, ngx_cycle->log);
}


static void *
ngx_http_js_calloc(void *mem, size_t size)
{
    return ngx_calloc(size, ngx_cycle->log);
}


static void *
ngx_http_js_memalign(void *mem, size_t alignment, size_t size)
{
    return ngx_memalign(alignment, size, ngx_cycle->log);
}


static void
ngx_http_js_free(void *mem, void *p)
{
    ngx_free(p);
}


static njs_ret_t
ngx_http_js_ext_get_string(njs_vm_t *vm, njs_value_t *value, void *obj,
    uintptr_t data)
{
    char *p = obj;

    ngx_str_t  *field;

    field = (ngx_str_t *) (p + data);

    return njs_string_create(vm, value, field->data, field->len, 0);
}


static njs_ret_t
ngx_http_js_ext_set_string(njs_vm_t *vm, void *obj, uintptr_t data,
    nxt_str_t *value)
{
    char *p = obj;

    ngx_str_t           *field;
    ngx_http_request_t  *r;

    r = (ngx_http_request_t *) obj;

    field = (ngx_str_t *) (p + data);
    field->len = value->len;

    field->data = ngx_pnalloc(r->pool, value->len);
    if (field->data == NULL) {
        return NJS_ERROR;
    }

    ngx_memcpy(field->data, value->data, value->len);

    return NJS_OK;
}


static njs_ret_t
ngx_http_js_ext_foreach_header(njs_vm_t *vm, void *obj, void *next,
    uintptr_t data)
{
    char *p = obj;

    ngx_list_t                 *headers;
    ngx_http_request_t         *r;
    ngx_http_js_table_entry_t  *entry, **e;

    r = (ngx_http_request_t *) obj;

    entry = ngx_palloc(r->pool, sizeof(ngx_http_js_table_entry_t));
    if (entry == NULL) {
        return NJS_ERROR;
    }

    headers = (ngx_list_t *) (p + data);

    entry->part = &headers->part;
    entry->item = 0;

    e = (ngx_http_js_table_entry_t **) next;
    *e = entry;

    return NJS_OK;
}


static njs_ret_t
ngx_http_js_ext_next_header(njs_vm_t *vm, njs_value_t *value, void *obj,
    void *next)
{
    ngx_http_js_table_entry_t **e = next;

    ngx_table_elt_t            *header, *h;
    ngx_http_js_table_entry_t  *entry;

    entry = *e;

    while (entry->part) {

        if (entry->item >= entry->part->nelts) {
            entry->part = entry->part->next;
            entry->item = 0;
            continue;
        }

        header = entry->part->elts;
        h = &header[entry->item++];

        return njs_string_create(vm, value, h->key.data, h->key.len, 0);
    }

    return NJS_DONE;
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


static njs_ret_t
ngx_http_js_ext_get_header_out(njs_vm_t *vm, njs_value_t *value, void *obj,
    uintptr_t data)
{
    nxt_str_t           *v;
    ngx_table_elt_t     *h;
    ngx_http_request_t  *r;

    r = (ngx_http_request_t *) obj;
    v = (nxt_str_t *) data;

    h = ngx_http_js_get_header(&r->headers_out.headers.part, v->data, v->len);
    if (h == NULL) {
        return njs_string_create(vm, value, NULL, 0, 0);
    }

    return njs_string_create(vm, value, h->value.data, h->value.len, 0);
}


static njs_ret_t
ngx_http_js_ext_set_header_out(njs_vm_t *vm, void *obj, uintptr_t data,
    nxt_str_t *value)
{
    u_char              *p;
    nxt_str_t           *v;
    ngx_table_elt_t     *h;
    ngx_http_request_t  *r;

    r = (ngx_http_request_t *) obj;
    v = (nxt_str_t *) data;

    h = ngx_http_js_get_header(&r->headers_out.headers.part, v->data, v->len);

    if (h == NULL || h->hash == 0) {
        h = ngx_list_push(&r->headers_out.headers);
        if (h == NULL) {
            return NJS_ERROR;
        }

        p = ngx_pnalloc(r->pool, v->len);
        if (p == NULL) {
            return NJS_ERROR;
        }

        ngx_memcpy(p, v->data, v->len);

        h->key.data = p;
        h->key.len = v->len;
        h->hash = 1;
    }


    p = ngx_pnalloc(r->pool, value->len);
    if (p == NULL) {
        return NJS_ERROR;
    }

    ngx_memcpy(p, value->data, value->len);

    h->value.data = p;
    h->value.len = value->len;

    return NJS_OK;
}


static njs_ret_t
ngx_http_js_ext_foreach_header_out(njs_vm_t *vm, void *obj, void *next)
{
    return ngx_http_js_ext_foreach_header(vm, obj, next,
                             offsetof(ngx_http_request_t, headers_out.headers));
}


static njs_ret_t
ngx_http_js_ext_get_status(njs_vm_t *vm, njs_value_t *value, void *obj,
    uintptr_t data)
{
    size_t               len;
    u_char              *p;
    ngx_http_request_t  *r;

    r = (ngx_http_request_t *) obj;

    p = ngx_pnalloc(r->pool, 3);
    if (p == NULL) {
        return NJS_ERROR;
    }

    len = ngx_snprintf(p, 3, "%ui", r->headers_out.status) - p;

    return njs_string_create(vm, value, p, len, 0);
}


static njs_ret_t
ngx_http_js_ext_set_status(njs_vm_t *vm, void *obj, uintptr_t data,
    nxt_str_t *value)
{
    ngx_int_t            n;
    ngx_http_request_t  *r;

    n = ngx_atoi(value->data, value->len);
    if (n == NGX_ERROR) {
        return NJS_ERROR;
    }

    r = (ngx_http_request_t *) obj;

    r->headers_out.status = n;

    return NJS_OK;
}


static njs_ret_t
ngx_http_js_ext_get_content_length(njs_vm_t *vm, njs_value_t *value, void *obj,
    uintptr_t data)
{
    size_t               len;
    u_char              *p;
    ngx_http_request_t  *r;

    r = (ngx_http_request_t *) obj;

    p = ngx_pnalloc(r->pool, NGX_OFF_T_LEN);
    if (p == NULL) {
        return NJS_ERROR;
    }

    len = ngx_sprintf(p, "%O", r->headers_out.content_length_n) - p;

    return njs_string_create(vm, value, p, len, 0);
}


static njs_ret_t
ngx_http_js_ext_set_content_length(njs_vm_t *vm, void *obj, uintptr_t data,
    nxt_str_t *value)
{
    ngx_int_t            n;
    ngx_http_request_t  *r;

    n = ngx_atoi(value->data, value->len);
    if (n == NGX_ERROR) {
        return NJS_ERROR;
    }

    r = (ngx_http_request_t *) obj;

    r->headers_out.content_length_n = n;

    return NJS_OK;
}


static njs_ret_t
ngx_http_js_ext_send_header(njs_vm_t *vm, njs_value_t *args, nxt_uint_t nargs,
    njs_index_t unused)
{
    ngx_http_request_t  *r;

    r = njs_value_data(njs_argument(args, 0));

    if (ngx_http_send_header(r) == NGX_ERROR) {
        return NJS_ERROR;
    }

    return NJS_OK;
}


static njs_ret_t
ngx_http_js_ext_send(njs_vm_t *vm, njs_value_t *args, nxt_uint_t nargs,
    njs_index_t unused)
{
    nxt_int_t            ret;
    nxt_str_t            s;
    ngx_buf_t           *b;
    uintptr_t            next;
    ngx_uint_t           n;
    ngx_chain_t         *out, *cl, **ll;
    ngx_http_request_t  *r;

    r = njs_value_data(njs_argument(args, 0));

    out = NULL;
    ll = &out;

    for (n = 1; n < nargs; n++) {
        next = 0;

        for ( ;; ) {
            ret = njs_value_string_copy(vm, &s, njs_argument(args, n), &next);

            if (ret == NJS_DECLINED) {
                break;
            }

            if (ret == NJS_ERROR) {
                return NJS_ERROR;
            }

            /* TODO: njs_value_release(vm, value) in buf completion */

            ngx_log_debug2(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
                           "http js send: \"%*s\"", s.len, s.data);

            b = ngx_calloc_buf(r->pool);
            if (b == NULL) {
                return NJS_ERROR;
            }

            b->start = s.data;
            b->pos = b->start;
            b->end = s.data + s.len;
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


static njs_ret_t
ngx_http_js_ext_finish(njs_vm_t *vm, njs_value_t *args, nxt_uint_t nargs,
    njs_index_t unused)
{
    ngx_http_request_t  *r;

    r = njs_value_data(njs_argument(args, 0));

    if (ngx_http_send_special(r, NGX_HTTP_LAST) == NGX_ERROR) {
        return NJS_ERROR;
    }

    return NJS_OK;
}


static njs_ret_t
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

    return njs_string_create(vm, value, v.data, v.len, 0);
}


static njs_ret_t
ngx_http_js_ext_get_remote_address(njs_vm_t *vm, njs_value_t *value, void *obj,
    uintptr_t data)
{
    ngx_connection_t    *c;
    ngx_http_request_t  *r;

    r = (ngx_http_request_t *) obj;
    c = r->connection;

    return njs_string_create(vm, value, c->addr_text.data, c->addr_text.len, 0);
}


static njs_ret_t
ngx_http_js_ext_get_header_in(njs_vm_t *vm, njs_value_t *value, void *obj,
    uintptr_t data)
{
    nxt_str_t           *v;
    ngx_table_elt_t     *h;
    ngx_http_request_t  *r;

    r = (ngx_http_request_t *) obj;
    v = (nxt_str_t *) data;

    h = ngx_http_js_get_header(&r->headers_in.headers.part, v->data, v->len);
    if (h == NULL) {
        return njs_string_create(vm, value, NULL, 0, 0);
    }

    return njs_string_create(vm, value, h->value.data, h->value.len, 0);
}


static njs_ret_t
ngx_http_js_ext_foreach_header_in(njs_vm_t *vm, void *obj, void *next)
{
    return ngx_http_js_ext_foreach_header(vm, obj, next,
                              offsetof(ngx_http_request_t, headers_in.headers));
}

static njs_ret_t
ngx_http_js_ext_get_arg(njs_vm_t *vm, njs_value_t *value, void *obj,
    uintptr_t data)
{
    nxt_str_t           *v;
    ngx_str_t            arg;
    ngx_http_request_t  *r;

    r = (ngx_http_request_t *) obj;
    v = (nxt_str_t *) data;

    if (ngx_http_arg(r, v->data, v->len, &arg) == NGX_OK) {
        return njs_string_create(vm, value, arg.data, arg.len, 0);
    }

    return njs_string_create(vm, value, NULL, 0, 0);
}


static njs_ret_t
ngx_http_js_ext_foreach_arg(njs_vm_t *vm, void *obj, void *next)
{
    ngx_str_t           *entry, **e;
    ngx_http_request_t  *r;

    r = (ngx_http_request_t *) obj;

    entry = ngx_palloc(r->pool, sizeof(ngx_str_t));
    if (entry == NULL) {
        return NJS_ERROR;
    }

    *entry = r->args;

    e = (ngx_str_t **) next;
    *e = entry;

    return NJS_OK;
}


static njs_ret_t
ngx_http_js_ext_next_arg(njs_vm_t *vm, njs_value_t *value, void *obj,
    void *next)
{
    ngx_str_t **e = next;

    size_t      len;
    u_char     *p, *start, *end;
    ngx_str_t  *entry;

    entry = *e;

    if (entry->len == 0) {
        return NJS_DONE;
    }

    start = entry->data;
    end = start + entry->len;

    p = ngx_strlchr(start, end, '=');
    if (p == NULL) {
        return NJS_ERROR;
    }

    len = p - start;
    p++;

    p = ngx_strlchr(p, end, '&');

    if (p) {
        entry->data = &p[1];
        entry->len = end - entry->data;

    } else {
        entry->len = 0;
    }

    return njs_string_create(vm, value, start, len, 0);
}


static char *
ngx_http_js_run(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    ngx_http_js_loc_conf_t *jlcf = conf;

    char                      *ret;
    ngx_str_t                 *value;
    ngx_http_core_loc_conf_t  *clcf;

    value = cf->args->elts;

    if (jlcf->js.vm) {
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                           "duplicate js handler \"%V\"", &value[1]);
        return NGX_CONF_ERROR;
    }

    ret = ngx_http_js_compile(cf, &jlcf->js, &value[1]);
    if (ret != NGX_CONF_OK) {
        return ret;
    }

    clcf = ngx_http_conf_get_module_loc_conf(cf, ngx_http_core_module);
    clcf->handler = ngx_http_js_handler;

    return NGX_CONF_OK;
}


static char *
ngx_http_js_set(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    char                 *ret;
    ngx_str_t            *value;
    ngx_http_js_ctx_t    *js;
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

    js = ngx_palloc(cf->pool, sizeof(ngx_http_js_ctx_t));
    if (js == NULL) {
        return NGX_CONF_ERROR;
    }

    ret = ngx_http_js_compile(cf, js, &value[2]);
    if (ret != NGX_CONF_OK) {
        return ret;
    }

    v->get_handler = ngx_http_js_variable;
    v->data = (uintptr_t) js;

    return NGX_CONF_OK;
}


static char *
ngx_http_js_compile(ngx_conf_t *cf, ngx_http_js_ctx_t *js, ngx_str_t *script)
{
    u_char                *start, *end;
    nxt_int_t              rc;
    nxt_str_t              s, name;
    njs_vm_t              *vm;
    nxt_lvlhsh_t           externals;
    njs_function_t        *function;
    njs_vm_shared_t       *shared;
    nxt_mem_cache_pool_t  *mcp;

    mcp = ngx_http_js_create_mem_cache_pool();
    if (mcp == NULL) {
        return NGX_CONF_ERROR;
    }

    shared = NULL;

    nxt_lvlhsh_init(&externals);

    if (njs_add_external(&externals, mcp, 0, ngx_http_js_externals,
                         nxt_nitems(ngx_http_js_externals))
        != NJS_OK)
    {
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0, "could not add js externals");
        return NGX_CONF_ERROR;
    }

    vm = njs_vm_create(mcp, &shared, &externals);
    if (vm == NULL) {
        return NGX_CONF_ERROR;
    }

    start = script->data;
    end = start + script->len;

    rc = njs_vm_compile(vm, &start, end, &function);

    if (rc != NJS_OK) {
        njs_vm_exception(vm, &s);

        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                           "js compilation error: \"%*s\"", s.len, s.data);
        return NGX_CONF_ERROR;
    }

    if (start != end) {
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                           "extra characters in js script: \"%*s\"",
                           end - start, start);
        return NGX_CONF_ERROR;
    }

    js->vm = vm;
    js->function = function;

    if (function) {
        ngx_str_set(&name, "$r");

        rc = njs_external_get(vm, NULL, &name, &js->args[0]);
        if (rc != NXT_OK) {
            ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                               "could not get $r external");
            return NGX_CONF_ERROR;
        }

        ngx_str_set(&name, "response");

        rc = njs_external_get(vm, &js->args[0], &name, &js->args[1]);
        if (rc != NXT_OK) {
            ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                               "could not get $r.response external");
            return NGX_CONF_ERROR;
        }
    }

    return NGX_CONF_OK;
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
     *     conf->vm = NULL;
     */

    return conf;
}


static char *
ngx_http_js_merge_loc_conf(ngx_conf_t *cf, void *parent, void *child)
{
    ngx_http_js_loc_conf_t *prev = parent;
    ngx_http_js_loc_conf_t *conf = child;

    if (conf->js.vm == NULL) {
        conf->js = prev->js;
    }

    return NGX_CONF_OK;
}
