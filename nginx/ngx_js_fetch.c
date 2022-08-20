
/*
 * Copyright (C) Dmitry Volyntsev
 * Copyright (C) hongzhidao
 * Copyright (C) Antoine Bonavita
 * Copyright (C) NGINX, Inc.
 */


#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_event.h>
#include <ngx_event_connect.h>
#include "ngx_js.h"


typedef struct ngx_js_http_s  ngx_js_http_t;


typedef struct {
    ngx_uint_t                     state;
    ngx_uint_t                     code;
    u_char                        *status_text;
    u_char                        *status_text_end;
    ngx_uint_t                     count;
    ngx_flag_t                     chunked;
    off_t                          content_length_n;

    u_char                        *header_name_start;
    u_char                        *header_name_end;
    u_char                        *header_start;
    u_char                        *header_end;
} ngx_js_http_parse_t;


typedef struct {
    u_char                        *pos;
    uint64_t                       chunk_size;
    uint8_t                        state;
    uint8_t                        last;
} ngx_js_http_chunk_parse_t;


struct ngx_js_http_s {
    ngx_log_t                     *log;
    ngx_pool_t                    *pool;

    njs_vm_t                      *vm;
    njs_external_ptr_t             external;
    njs_vm_event_t                 vm_event;
    ngx_js_event_handler_pt        event_handler;

    ngx_resolver_ctx_t            *ctx;
    ngx_addr_t                     addr;
    ngx_addr_t                    *addrs;
    ngx_uint_t                     naddrs;
    ngx_uint_t                     naddr;
    in_port_t                      port;

    ngx_peer_connection_t          peer;
    ngx_msec_t                     timeout;

    ngx_int_t                      buffer_size;
    ngx_int_t                      max_response_body_size;

    njs_str_t                      url;
    ngx_array_t                    headers;

#if (NGX_SSL)
    ngx_str_t                      tls_name;
    ngx_ssl_t                     *ssl;
    njs_bool_t                     ssl_verify;
#endif

    ngx_buf_t                     *buffer;
    ngx_buf_t                     *chunk;
    njs_chb_t                      chain;

    njs_opaque_value_t             reply;
    njs_opaque_value_t             promise;
    njs_opaque_value_t             promise_callbacks[2];

    uint8_t                        done;
    uint8_t                        body_used;
    ngx_js_http_parse_t            http_parse;
    ngx_js_http_chunk_parse_t      http_chunk_parse;
    ngx_int_t                    (*process)(ngx_js_http_t *http);
};


#define ngx_js_http_error(http, err, fmt, ...)                                \
    do {                                                                      \
        njs_vm_value_error_set((http)->vm, njs_value_arg(&(http)->reply),     \
                               fmt, ##__VA_ARGS__);                           \
        ngx_js_http_fetch_done(http, &(http)->reply, NJS_ERROR);              \
    } while (0)


static ngx_js_http_t *ngx_js_http_alloc(njs_vm_t *vm, ngx_pool_t *pool,
    ngx_log_t *log);
static void njs_js_http_destructor(njs_external_ptr_t external,
    njs_host_event_t host);
static void ngx_js_resolve_handler(ngx_resolver_ctx_t *ctx);
static njs_int_t ngx_js_fetch_promissified_result(njs_vm_t *vm,
    njs_value_t *result, njs_int_t rc);
static void ngx_js_http_fetch_done(ngx_js_http_t *http,
    njs_opaque_value_t *retval, njs_int_t rc);
static njs_int_t ngx_js_http_promise_trampoline(njs_vm_t *vm,
    njs_value_t *args, njs_uint_t nargs, njs_index_t unused);
static void ngx_js_http_connect(ngx_js_http_t *http);
static void ngx_js_http_next(ngx_js_http_t *http);
static void ngx_js_http_write_handler(ngx_event_t *wev);
static void ngx_js_http_read_handler(ngx_event_t *rev);
static ngx_int_t ngx_js_http_process_status_line(ngx_js_http_t *http);
static ngx_int_t ngx_js_http_process_headers(ngx_js_http_t *http);
static ngx_int_t ngx_js_http_process_body(ngx_js_http_t *http);
static ngx_int_t ngx_js_http_parse_status_line(ngx_js_http_parse_t *hp,
    ngx_buf_t *b);
static ngx_int_t ngx_js_http_parse_header_line(ngx_js_http_parse_t *hp,
    ngx_buf_t *b);
static ngx_int_t ngx_js_http_parse_chunked(ngx_js_http_chunk_parse_t *hcp,
    ngx_buf_t *b, njs_chb_t *chain);
static void ngx_js_http_dummy_handler(ngx_event_t *ev);

static njs_int_t ngx_response_js_ext_headers_get(njs_vm_t *vm,
    njs_value_t *args,  njs_uint_t nargs, njs_index_t as_array);
static njs_int_t ngx_response_js_ext_headers_has(njs_vm_t *vm,
    njs_value_t *args,  njs_uint_t nargs, njs_index_t unused);
static njs_int_t ngx_response_js_ext_header(njs_vm_t *vm,
    njs_object_prop_t *prop, njs_value_t *value, njs_value_t *setval,
    njs_value_t *retval);
static njs_int_t ngx_response_js_ext_keys(njs_vm_t *vm, njs_value_t *value,
    njs_value_t *keys);
static njs_int_t ngx_response_js_ext_status(njs_vm_t *vm,
    njs_object_prop_t *prop, njs_value_t *value, njs_value_t *setval,
    njs_value_t *retval);
static njs_int_t ngx_response_js_ext_status_text(njs_vm_t *vm,
    njs_object_prop_t *prop, njs_value_t *value, njs_value_t *setval,
    njs_value_t *retval);
static njs_int_t ngx_response_js_ext_ok(njs_vm_t *vm,
    njs_object_prop_t *prop, njs_value_t *value, njs_value_t *setval,
    njs_value_t *retval);
static njs_int_t ngx_response_js_ext_body_used(njs_vm_t *vm,
    njs_object_prop_t *prop, njs_value_t *value, njs_value_t *setval,
    njs_value_t *retval);
static njs_int_t ngx_response_js_ext_type(njs_vm_t *vm,
    njs_object_prop_t *prop, njs_value_t *value, njs_value_t *setval,
    njs_value_t *retval);
static njs_int_t ngx_response_js_ext_body(njs_vm_t *vm, njs_value_t *args,
     njs_uint_t nargs, njs_index_t unused);

#if (NGX_SSL)
static void ngx_js_http_ssl_init_connection(ngx_js_http_t *http);
static void ngx_js_http_ssl_handshake_handler(ngx_connection_t *c);
static void ngx_js_http_ssl_handshake(ngx_js_http_t *http);
static njs_int_t ngx_js_http_ssl_name(ngx_js_http_t *http);
#endif

static njs_external_t  ngx_js_ext_http_response_headers[] = {

    {
        .flags = NJS_EXTERN_PROPERTY | NJS_EXTERN_SYMBOL,
        .name.symbol = NJS_SYMBOL_TO_STRING_TAG,
        .u.property = {
            .value = "Headers",
        }
    },

    {
        .flags = NJS_EXTERN_METHOD,
        .name.string = njs_str("get"),
        .writable = 1,
        .configurable = 1,
        .enumerable = 1,
        .u.method = {
            .native = ngx_response_js_ext_headers_get,
        }
    },

    {
        .flags = NJS_EXTERN_METHOD,
        .name.string = njs_str("getAll"),
        .writable = 1,
        .configurable = 1,
        .enumerable = 1,
        .u.method = {
            .native = ngx_response_js_ext_headers_get,
            .magic8 = 1
        }
    },

    {
        .flags = NJS_EXTERN_METHOD,
        .name.string = njs_str("has"),
        .writable = 1,
        .configurable = 1,
        .enumerable = 1,
        .u.method = {
            .native = ngx_response_js_ext_headers_has,
        }
    },

};


static njs_external_t  ngx_js_ext_http_response[] = {

    {
        .flags = NJS_EXTERN_PROPERTY | NJS_EXTERN_SYMBOL,
        .name.symbol = NJS_SYMBOL_TO_STRING_TAG,
        .u.property = {
            .value = "Response",
        }
    },

    {
        .flags = NJS_EXTERN_METHOD,
        .name.string = njs_str("arrayBuffer"),
        .writable = 1,
        .configurable = 1,
        .enumerable = 1,
        .u.method = {
            .native = ngx_response_js_ext_body,
#define NGX_JS_BODY_ARRAY_BUFFER   0
#define NGX_JS_BODY_JSON           1
#define NGX_JS_BODY_TEXT           2
            .magic8 = NGX_JS_BODY_ARRAY_BUFFER
        }
    },

    {
        .flags = NJS_EXTERN_PROPERTY,
        .name.string = njs_str("bodyUsed"),
        .enumerable = 1,
        .u.property = {
            .handler = ngx_response_js_ext_body_used,
        }
    },

    {
        .flags = NJS_EXTERN_OBJECT,
        .name.string = njs_str("headers"),
        .enumerable = 1,
        .u.object = {
            .enumerable = 1,
            .properties = ngx_js_ext_http_response_headers,
            .nproperties = njs_nitems(ngx_js_ext_http_response_headers),
            .prop_handler = ngx_response_js_ext_header,
            .keys = ngx_response_js_ext_keys,
        }
    },

    {
        .flags = NJS_EXTERN_METHOD,
        .name.string = njs_str("json"),
        .writable = 1,
        .configurable = 1,
        .enumerable = 1,
        .u.method = {
            .native = ngx_response_js_ext_body,
            .magic8 = NGX_JS_BODY_JSON
        }
    },

    {
        .flags = NJS_EXTERN_PROPERTY,
        .name.string = njs_str("ok"),
        .enumerable = 1,
        .u.property = {
            .handler = ngx_response_js_ext_ok,
        }
    },

    {
        .flags = NJS_EXTERN_PROPERTY,
        .name.string = njs_str("redirected"),
        .enumerable = 1,
        .u.property = {
            .handler = ngx_js_ext_constant,
            .magic32 = 0,
            .magic16 = NGX_JS_BOOLEAN,
        }
    },

    {
        .flags = NJS_EXTERN_PROPERTY,
        .name.string = njs_str("status"),
        .enumerable = 1,
        .u.property = {
            .handler = ngx_response_js_ext_status,
        }
    },

    {
        .flags = NJS_EXTERN_PROPERTY,
        .name.string = njs_str("statusText"),
        .enumerable = 1,
        .u.property = {
            .handler = ngx_response_js_ext_status_text,
        }
    },

    {
        .flags = NJS_EXTERN_METHOD,
        .name.string = njs_str("text"),
        .writable = 1,
        .configurable = 1,
        .enumerable = 1,
        .u.method = {
            .native = ngx_response_js_ext_body,
            .magic8 = NGX_JS_BODY_TEXT
        }
    },

    {
        .flags = NJS_EXTERN_PROPERTY,
        .name.string = njs_str("type"),
        .enumerable = 1,
        .u.property = {
            .handler = ngx_response_js_ext_type,
        }
    },

    {
        .flags = NJS_EXTERN_PROPERTY,
        .name.string = njs_str("url"),
        .enumerable = 1,
        .u.property = {
            .handler = ngx_js_ext_string,
            .magic32 = offsetof(ngx_js_http_t, url),
        }
    },
};


static njs_int_t    ngx_http_js_fetch_proto_id;


njs_int_t
ngx_js_ext_fetch(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t unused)
{
    int64_t              i, length;
    njs_int_t            ret;
    njs_str_t            method, body, name, header;
    ngx_url_t            u;
    njs_bool_t           has_host;
    ngx_pool_t          *pool;
    njs_value_t         *init, *value, *headers, *keys;
    ngx_js_http_t       *http;
    ngx_connection_t    *c;
    ngx_resolver_ctx_t  *ctx;
    njs_external_ptr_t   external;
    njs_opaque_value_t  *start, lvalue, headers_value;

    static const njs_str_t body_key = njs_str("body");
    static const njs_str_t headers_key = njs_str("headers");
    static const njs_str_t buffer_size_key = njs_str("buffer_size");
    static const njs_str_t body_size_key = njs_str("max_response_body_size");
    static const njs_str_t method_key = njs_str("method");
#if (NGX_SSL)
    static const njs_str_t verify_key = njs_str("verify");
#endif

    external = njs_vm_external(vm, NJS_PROTO_ID_ANY, njs_argument(args, 0));
    if (external == NULL) {
        njs_vm_error(vm, "\"this\" is not an external");
        return NJS_ERROR;
    }

    c = ngx_external_connection(vm, external);
    pool = ngx_external_pool(vm, external);

    http = ngx_js_http_alloc(vm, pool, c->log);
    if (http == NULL) {
        return NJS_ERROR;
    }

    http->external = external;
    http->timeout = ngx_external_fetch_timeout(vm, external);
    http->event_handler = ngx_external_event_handler(vm, external);
    http->buffer_size = ngx_external_buffer_size(vm, external);
    http->max_response_body_size =
                           ngx_external_max_response_buffer_size(vm, external);

    ret = ngx_js_string(vm, njs_arg(args, nargs, 1), &http->url);
    if (ret != NJS_OK) {
        njs_vm_error(vm, "failed to convert url arg");
        goto fail;
    }

    ngx_memzero(&u, sizeof(ngx_url_t));

    u.url.len = http->url.length;
    u.url.data = http->url.start;
    u.default_port = 80;
    u.uri_part = 1;
    u.no_resolve = 1;

    if (u.url.len > 7
        && ngx_strncasecmp(u.url.data, (u_char *) "http://", 7) == 0)
    {
        u.url.len -= 7;
        u.url.data += 7;

#if (NGX_SSL)
    } else if (u.url.len > 8
        && ngx_strncasecmp(u.url.data, (u_char *) "https://", 8) == 0)
    {
        u.url.len -= 8;
        u.url.data += 8;
        u.default_port = 443;
        http->ssl = ngx_external_ssl(vm, external);
        http->ssl_verify = ngx_external_ssl_verify(vm, external);
#endif

    } else {
        njs_vm_error(vm, "unsupported URL prefix");
        goto fail;
    }

    if (ngx_parse_url(pool, &u) != NGX_OK) {
        njs_vm_error(vm, "invalid url");
        goto fail;
    }

    init = njs_arg(args, nargs, 2);

    method = njs_str_value("GET");
    body = njs_str_value("");
    headers = NULL;

    if (njs_value_is_object(init)) {
        value = njs_vm_object_prop(vm, init, &method_key, &lvalue);
        if (value != NULL && ngx_js_string(vm, value, &method) != NGX_OK) {
            goto fail;
        }

        headers = njs_vm_object_prop(vm, init, &headers_key, &headers_value);
        if (headers != NULL && !njs_value_is_object(headers)) {
            njs_vm_error(vm, "headers is not an object");
            goto fail;
        }

        value = njs_vm_object_prop(vm, init, &body_key, &lvalue);
        if (value != NULL && ngx_js_string(vm, value, &body) != NGX_OK) {
            goto fail;
        }

        value = njs_vm_object_prop(vm, init, &buffer_size_key, &lvalue);
        if (value != NULL
            && ngx_js_integer(vm, value, &http->buffer_size)
               != NGX_OK)
        {
            goto fail;
        }

        value = njs_vm_object_prop(vm, init, &body_size_key, &lvalue);
        if (value != NULL
            && ngx_js_integer(vm, value, &http->max_response_body_size)
               != NGX_OK)
        {
            goto fail;
        }

#if (NGX_SSL)
        value = njs_vm_object_prop(vm, init, &verify_key, &lvalue);
        if (value != NULL) {
            http->ssl_verify = njs_value_bool(value);
        }
#endif
    }

    njs_chb_init(&http->chain, njs_vm_memory_pool(vm));

    njs_chb_append(&http->chain, method.start, method.length);
    njs_chb_append_literal(&http->chain, " ");

    if (u.uri.len == 0 || u.uri.data[0] != '/') {
        njs_chb_append_literal(&http->chain, "/");
    }

    njs_chb_append(&http->chain, u.uri.data, u.uri.len);
    njs_chb_append_literal(&http->chain, " HTTP/1.1" CRLF);
    njs_chb_append_literal(&http->chain, "Connection: close" CRLF);

    has_host = 0;

    if (headers != NULL) {
        keys = njs_vm_object_keys(vm, headers, njs_value_arg(&lvalue));
        if (keys == NULL) {
            goto fail;
        }

        start = (njs_opaque_value_t *) njs_vm_array_start(vm, keys);
        if (start == NULL) {
            goto fail;
        }

        (void) njs_vm_array_length(vm, keys, &length);

        for (i = 0; i < length; i++) {
            if (ngx_js_string(vm, njs_value_arg(start), &name) != NGX_OK) {
                goto fail;
            }

            start++;

            value = njs_vm_object_prop(vm, headers, &name, &lvalue);
            if (value == NULL) {
                goto fail;
            }

            if (njs_value_is_null_or_undefined(value)) {
                continue;
            }

            if (ngx_js_string(vm, value, &header) != NGX_OK) {
                goto fail;
            }

            if (name.length == 4
                && ngx_strncasecmp(name.start, (u_char *) "Host", 4) == 0)
            {
                has_host = 1;
            }

            njs_chb_append(&http->chain, name.start, name.length);
            njs_chb_append_literal(&http->chain, ": ");
            njs_chb_append(&http->chain, header.start, header.length);
            njs_chb_append_literal(&http->chain, CRLF);
        }
    }

    if (!has_host) {
        njs_chb_append_literal(&http->chain, "Host: ");
        njs_chb_append(&http->chain, u.host.data, u.host.len);
        njs_chb_append_literal(&http->chain, CRLF);
    }

#if (NGX_SSL)
    http->tls_name.data = u.host.data;
    http->tls_name.len = u.host.len;
#endif

    if (body.length != 0) {
        njs_chb_sprintf(&http->chain, 32, "Content-Length: %uz" CRLF CRLF,
                        body.length);
        njs_chb_append(&http->chain, body.start, body.length);

    } else {
        njs_chb_append_literal(&http->chain, CRLF);
    }

    if (u.addrs == NULL) {
        ctx = ngx_resolve_start(ngx_external_resolver(vm, external), NULL);
        if (ctx == NULL) {
            njs_vm_memory_error(vm);
            return NJS_ERROR;
        }

        if (ctx == NGX_NO_RESOLVER) {
            njs_vm_error(vm, "no resolver defined");
            goto fail;
        }

        http->ctx = ctx;
        http->port = u.port;

        ctx->name = u.host;
        ctx->handler = ngx_js_resolve_handler;
        ctx->data = http;
        ctx->timeout = ngx_external_resolver_timeout(vm, external);

        ret = ngx_resolve_name(http->ctx);
        if (ret != NGX_OK) {
            http->ctx = NULL;
            njs_vm_memory_error(vm);
            return NJS_ERROR;
        }

        njs_vm_retval_set(vm, njs_value_arg(&http->promise));

        return NJS_OK;
    }

    http->naddrs = 1;
    ngx_memcpy(&http->addr, &u.addrs[0], sizeof(ngx_addr_t));
    http->addrs = &http->addr;

    ngx_js_http_connect(http);

    njs_vm_retval_set(vm, njs_value_arg(&http->promise));

    return NJS_OK;

fail:

    ngx_js_http_fetch_done(http, (njs_opaque_value_t *) njs_vm_retval(vm),
                           NJS_ERROR);

    njs_vm_retval_set(vm, njs_value_arg(&http->promise));

    return NJS_OK;
}


static ngx_js_http_t *
ngx_js_http_alloc(njs_vm_t *vm, ngx_pool_t *pool, ngx_log_t *log)
{
    njs_int_t        ret;
    ngx_js_http_t   *http;
    njs_vm_event_t   vm_event;
    njs_function_t  *callback;

    http = ngx_pcalloc(pool, sizeof(ngx_js_http_t));
    if (http == NULL) {
        goto failed;
    }

    http->pool = pool;
    http->log = log;
    http->vm = vm;

    http->timeout = 10000;

    http->http_parse.content_length_n = -1;

    ret = njs_vm_promise_create(vm, njs_value_arg(&http->promise),
                                njs_value_arg(&http->promise_callbacks));
    if (ret != NJS_OK) {
        goto failed;
    }

    callback = njs_vm_function_alloc(vm, ngx_js_http_promise_trampoline);
    if (callback == NULL) {
        goto failed;
    }

    vm_event = njs_vm_add_event(vm, callback, 1, http, njs_js_http_destructor);
    if (vm_event == NULL) {
        goto failed;
    }

    http->vm_event = vm_event;

    ngx_log_debug1(NGX_LOG_DEBUG_EVENT, log, 0, "js http alloc:%p", http);

    return http;

failed:

    njs_vm_error(vm, "internal error");

    return NULL;
}


static void
ngx_js_resolve_handler(ngx_resolver_ctx_t *ctx)
{
    u_char           *p;
    size_t            len;
    socklen_t         socklen;
    ngx_uint_t        i;
    ngx_js_http_t    *http;
    struct sockaddr  *sockaddr;

    http = ctx->data;

    if (ctx->state) {
        ngx_js_http_error(http, 0, "\"%V\" could not be resolved (%i: %s)",
                          &ctx->name, ctx->state,
                          ngx_resolver_strerror(ctx->state));
        return;
    }

    ngx_log_debug1(NGX_LOG_DEBUG_EVENT, http->log, 0,
                   "http fetch resolved: \"%V\"", &ctx->name);

#if (NGX_DEBUG)
    {
    u_char      text[NGX_SOCKADDR_STRLEN];
    ngx_str_t   addr;
    ngx_uint_t  i;

    addr.data = text;

    for (i = 0; i < ctx->naddrs; i++) {
        addr.len = ngx_sock_ntop(ctx->addrs[i].sockaddr, ctx->addrs[i].socklen,
                                 text, NGX_SOCKADDR_STRLEN, 0);

        ngx_log_debug1(NGX_LOG_DEBUG_EVENT, http->log, 0,
                       "name was resolved to \"%V\"", &addr);
    }
    }
#endif

    http->naddrs = ctx->naddrs;
    http->addrs = ngx_pcalloc(http->pool, http->naddrs * sizeof(ngx_addr_t));

    if (http->addrs == NULL) {
        goto failed;
    }

    for (i = 0; i < ctx->naddrs; i++) {
        socklen = ctx->addrs[i].socklen;

        sockaddr = ngx_palloc(http->pool, socklen);
        if (sockaddr == NULL) {
            goto failed;
        }

        ngx_memcpy(sockaddr, ctx->addrs[i].sockaddr, socklen);
        ngx_inet_set_port(sockaddr, http->port);

        http->addrs[i].sockaddr = sockaddr;
        http->addrs[i].socklen = socklen;

        p = ngx_pnalloc(http->pool, NGX_SOCKADDR_STRLEN);
        if (p == NULL) {
            goto failed;
        }

        len = ngx_sock_ntop(sockaddr, socklen, p, NGX_SOCKADDR_STRLEN, 1);
        http->addrs[i].name.len = len;
        http->addrs[i].name.data = p;
    }

    ngx_resolve_name_done(ctx);
    http->ctx = NULL;

    ngx_js_http_connect(http);

    return;

failed:

    ngx_js_http_error(http, 0, "memory error");
}


static void
ngx_js_http_close_connection(ngx_connection_t *c)
{
    ngx_log_debug1(NGX_LOG_DEBUG_HTTP, c->log, 0,
                   "close js http connection: %d", c->fd);

#if (NGX_SSL)
    if (c->ssl) {
        c->ssl->no_wait_shutdown = 1;

        if (ngx_ssl_shutdown(c) == NGX_AGAIN) {
            c->ssl->handler = ngx_js_http_close_connection;
            return;
        }
    }
#endif

    c->destroyed = 1;

    ngx_close_connection(c);
}


static void
njs_js_http_destructor(njs_external_ptr_t external, njs_host_event_t host)
{
    ngx_js_http_t  *http;

    http = host;

    ngx_log_debug1(NGX_LOG_DEBUG_EVENT, http->log, 0, "js http destructor:%p",
                   http);

    if (http->ctx != NULL) {
        ngx_resolve_name_done(http->ctx);
        http->ctx = NULL;
    }

    if (http->peer.connection != NULL) {
        ngx_js_http_close_connection(http->peer.connection);
        http->peer.connection = NULL;
    }
}


static njs_int_t
ngx_js_fetch_promissified_result(njs_vm_t *vm, njs_value_t *result,
    njs_int_t rc)
{
    njs_int_t            ret;
    njs_function_t      *callback;
    njs_vm_event_t       vm_event;
    njs_opaque_value_t   retval, arguments[2];

    ret = njs_vm_promise_create(vm, njs_value_arg(&retval),
                                njs_value_arg(&arguments));
    if (ret != NJS_OK) {
        goto error;
    }

    callback = njs_vm_function_alloc(vm, ngx_js_http_promise_trampoline);
    if (callback == NULL) {
        goto error;
    }

    vm_event = njs_vm_add_event(vm, callback, 1, NULL, NULL);
    if (vm_event == NULL) {
        goto error;
    }

    njs_value_assign(&arguments[0], &arguments[(rc != NJS_OK)]);
    njs_value_assign(&arguments[1], result);

    ret = njs_vm_post_event(vm, vm_event, njs_value_arg(&arguments), 2);
    if (ret == NJS_ERROR) {
        goto error;
    }

    njs_vm_retval_set(vm, njs_value_arg(&retval));

    return NJS_OK;

error:

    njs_vm_error(vm, "internal error");

    return NJS_ERROR;
}


static void
ngx_js_http_fetch_done(ngx_js_http_t *http, njs_opaque_value_t *retval,
    njs_int_t rc)
{
    njs_opaque_value_t  arguments[2], *action;

    ngx_log_debug2(NGX_LOG_DEBUG_EVENT, http->log, 0,
                   "js fetch done http:%p rc:%i", http, (ngx_int_t) rc);

    if (http->peer.connection != NULL) {
        ngx_js_http_close_connection(http->peer.connection);
        http->peer.connection = NULL;
    }

    if (http->vm_event != NULL) {
        action = &http->promise_callbacks[(rc != NJS_OK)];
        njs_value_assign(&arguments[0], action);
        njs_value_assign(&arguments[1], retval);
        http->event_handler(http->external, http->vm_event,
                            njs_value_arg(&arguments), 2);
    }
}


static njs_int_t
ngx_js_http_promise_trampoline(njs_vm_t *vm, njs_value_t *args,
    njs_uint_t nargs, njs_index_t unused)
{
    njs_function_t  *callback;

    callback = njs_value_function(njs_argument(args, 1));

    if (callback != NULL) {
        return njs_vm_call(vm, callback, njs_argument(args, 2), 1);
    }

    return NJS_OK;
}


static void
ngx_js_http_connect(ngx_js_http_t *http)
{
    ngx_int_t    rc;
    ngx_addr_t  *addr;

    addr = &http->addrs[http->naddr];

    ngx_log_debug2(NGX_LOG_DEBUG_EVENT, http->log, 0,
                   "js http connect %ui/%ui", http->naddr, http->naddrs);

    http->peer.sockaddr = addr->sockaddr;
    http->peer.socklen = addr->socklen;
    http->peer.name = &addr->name;
    http->peer.get = ngx_event_get_peer;
    http->peer.log = http->log;
    http->peer.log_error = NGX_ERROR_ERR;

    rc = ngx_event_connect_peer(&http->peer);

    if (rc == NGX_ERROR) {
        ngx_js_http_error(http, 0, "connect failed");
        return;
    }

    if (rc == NGX_BUSY || rc == NGX_DECLINED) {
        ngx_js_http_next(http);
        return;
    }

    http->peer.connection->data = http;
    http->peer.connection->pool = http->pool;

    http->peer.connection->write->handler = ngx_js_http_write_handler;
    http->peer.connection->read->handler = ngx_js_http_read_handler;

    http->process = ngx_js_http_process_status_line;

    ngx_add_timer(http->peer.connection->read, http->timeout);
    ngx_add_timer(http->peer.connection->write, http->timeout);

#if (NGX_SSL)
    if (http->ssl != NULL && http->peer.connection->ssl == NULL) {
        ngx_js_http_ssl_init_connection(http);
        return;
    }
#endif

    if (rc == NGX_OK) {
        ngx_js_http_write_handler(http->peer.connection->write);
    }
}


#if (NGX_SSL)

static void
ngx_js_http_ssl_init_connection(ngx_js_http_t *http)
{
    ngx_int_t          rc;
    ngx_connection_t  *c;

    c = http->peer.connection;

    ngx_log_debug2(NGX_LOG_DEBUG_EVENT, http->log, 0,
                   "js http secure connect %ui/%ui", http->naddr, http->naddrs);

    if (ngx_ssl_create_connection(http->ssl, c, NGX_SSL_BUFFER|NGX_SSL_CLIENT)
        != NGX_OK)
    {
        ngx_js_http_error(http, 0, "failed to create ssl connection");
        return;
    }

    c->sendfile = 0;

    if (ngx_js_http_ssl_name(http) != NGX_OK) {
        ngx_js_http_error(http, 0, "failed to create ssl connection");
        return;
    }

    c->log->action = "SSL handshaking to fetch target";

    rc = ngx_ssl_handshake(c);

    if (rc == NGX_AGAIN) {
        c->data = http;
        c->ssl->handler = ngx_js_http_ssl_handshake_handler;
        return;
    }

    ngx_js_http_ssl_handshake(http);
}


static void
ngx_js_http_ssl_handshake_handler(ngx_connection_t *c)
{
    ngx_js_http_t  *http;

    http = c->data;

    http->peer.connection->write->handler = ngx_js_http_write_handler;
    http->peer.connection->read->handler = ngx_js_http_read_handler;

    ngx_js_http_ssl_handshake(http);
}


static void
ngx_js_http_ssl_handshake(ngx_js_http_t *http)
{
    long               rc;
    ngx_connection_t  *c;

    c = http->peer.connection;

    if (c->ssl->handshaked) {
        if (http->ssl_verify) {
            rc = SSL_get_verify_result(c->ssl->connection);

            if (rc != X509_V_OK) {
                ngx_log_error(NGX_LOG_ERR, c->log, 0,
                              "js http fetch SSL certificate verify "
                              "error: (%l:%s)", rc,
                              X509_verify_cert_error_string(rc));
                goto failed;
            }

            if (ngx_ssl_check_host(c, &http->tls_name) != NGX_OK) {
                ngx_log_error(NGX_LOG_ERR, c->log, 0,
                              "js http SSL certificate does not match \"%V\"",
                              &http->tls_name);
                goto failed;
            }
        }

        c->write->handler = ngx_js_http_write_handler;
        c->read->handler = ngx_js_http_read_handler;

        if (c->read->ready) {
            ngx_post_event(c->read, &ngx_posted_events);
        }

        http->process = ngx_js_http_process_status_line;
        ngx_js_http_write_handler(c->write);

        return;
    }

failed:

    ngx_js_http_next(http);
 }


static njs_int_t
ngx_js_http_ssl_name(ngx_js_http_t *http)
{
#ifdef SSL_CTRL_SET_TLSEXT_HOSTNAME
    u_char  *p;

    /* as per RFC 6066, literal IPv4 and IPv6 addresses are not permitted */
    ngx_str_t  *name = &http->tls_name;

    if (name->len == 0 || *name->data == '[') {
        goto done;
    }

    if (ngx_inet_addr(name->data, name->len) != INADDR_NONE) {
        goto done;
    }

    /*
     * SSL_set_tlsext_host_name() needs a null-terminated string,
     * hence we explicitly null-terminate name here
     */

    p = ngx_pnalloc(http->pool, name->len + 1);
    if (p == NULL) {
        return NGX_ERROR;
    }

    (void) ngx_cpystrn(p, name->data, name->len + 1);

    name->data = p;

    ngx_log_debug1(NGX_LOG_DEBUG_HTTP, http->log, 0,
                   "js http SSL server name: \"%s\"", name->data);

    if (SSL_set_tlsext_host_name(http->peer.connection->ssl->connection,
                                 (char *) name->data)
        == 0)
    {
        ngx_ssl_error(NGX_LOG_ERR, http->log, 0,
                      "SSL_set_tlsext_host_name(\"%s\") failed", name->data);
        return NGX_ERROR;
    }

#endif
done:

    return NJS_OK;
}

#endif


static void
ngx_js_http_next(ngx_js_http_t *http)
{
    ngx_log_debug0(NGX_LOG_DEBUG_EVENT, http->log, 0, "js http next");

    if (++http->naddr >= http->naddrs) {
        ngx_js_http_error(http, 0, "connect failed");
        return;
    }

    if (http->peer.connection != NULL) {
        ngx_js_http_close_connection(http->peer.connection);
        http->peer.connection = NULL;
    }

    http->buffer = NULL;

    ngx_js_http_connect(http);
}


static void
ngx_js_http_write_handler(ngx_event_t *wev)
{
    ssize_t            n, size;
    ngx_buf_t         *b;
    ngx_js_http_t     *http;
    ngx_connection_t  *c;

    c = wev->data;
    http = c->data;

    ngx_log_debug0(NGX_LOG_DEBUG_EVENT, wev->log, 0, "js http write handler");

    if (wev->timedout) {
        ngx_js_http_error(http, NGX_ETIMEDOUT, "write timed out");
        return;
    }

#if (NGX_SSL)
    if (http->ssl != NULL && http->peer.connection->ssl == NULL) {
        ngx_js_http_ssl_init_connection(http);
        return;
    }
#endif

    b = http->buffer;

    if (b == NULL) {
        size = njs_chb_size(&http->chain);
        if (size < 0) {
            ngx_js_http_error(http, 0, "memory error");
            return;
        }

        b = ngx_create_temp_buf(http->pool, size);
        if (b == NULL) {
            ngx_js_http_error(http, 0, "memory error");
            return;
        }

        njs_chb_join_to(&http->chain, b->last);
        b->last += size;

        http->buffer = b;
    }

    size = b->last - b->pos;

    n = c->send(c, b->pos, size);

    if (n == NGX_ERROR) {
        ngx_js_http_next(http);
        return;
    }

    if (n > 0) {
        b->pos += n;

        if (n == size) {
            wev->handler = ngx_js_http_dummy_handler;

            http->buffer = NULL;

            if (wev->timer_set) {
                ngx_del_timer(wev);
            }

            if (ngx_handle_write_event(wev, 0) != NGX_OK) {
                ngx_js_http_error(http, 0, "write failed");
            }

            return;
        }
    }

    if (!wev->timer_set) {
        ngx_add_timer(wev, http->timeout);
    }
}


static void
ngx_js_http_read_handler(ngx_event_t *rev)
{
    ssize_t            n, size;
    ngx_int_t          rc;
    ngx_buf_t         *b;
    ngx_js_http_t     *http;
    ngx_connection_t  *c;

    c = rev->data;
    http = c->data;

    ngx_log_debug0(NGX_LOG_DEBUG_EVENT, rev->log, 0, "js http read handler");

    if (rev->timedout) {
        ngx_js_http_error(http, NGX_ETIMEDOUT, "read timed out");
        return;
    }

    if (http->buffer == NULL) {
        b = ngx_create_temp_buf(http->pool, http->buffer_size);
        if (b == NULL) {
            ngx_js_http_error(http, 0, "memory error");
            return;
        }

        http->buffer = b;
    }

    for ( ;; ) {
        b = http->buffer;
        size = b->end - b->last;

        n = c->recv(c, b->last, size);

        if (n > 0) {
            b->last += n;

            rc = http->process(http);

            if (rc == NGX_ERROR) {
                return;
            }

            continue;
        }

        if (n == NGX_AGAIN) {
            if (ngx_handle_read_event(rev, 0) != NGX_OK) {
                ngx_js_http_error(http, 0, "read failed");
            }

            return;
        }

        if (n == NGX_ERROR) {
            ngx_js_http_next(http);
            return;
        }

        break;
    }

    http->done = 1;

    rc = http->process(http);

    if (rc == NGX_DONE) {
        /* handler was called */
        return;
    }

    if (rc == NGX_AGAIN) {
        ngx_js_http_error(http, 0, "prematurely closed connection");
    }
}


static ngx_int_t
ngx_js_http_process_status_line(ngx_js_http_t *http)
{
    ngx_int_t             rc;
    ngx_js_http_parse_t  *hp;

    hp = &http->http_parse;

    rc = ngx_js_http_parse_status_line(hp, http->buffer);

    if (rc == NGX_OK) {
        ngx_log_debug1(NGX_LOG_DEBUG_EVENT, http->log, 0, "js http status %ui",
                       hp->code);

        http->process = ngx_js_http_process_headers;

        return http->process(http);
    }

    if (rc == NGX_AGAIN) {
        return NGX_AGAIN;
    }

    /* rc == NGX_ERROR */

    ngx_js_http_error(http, 0, "invalid fetch status line");

    return NGX_ERROR;
}


static ngx_int_t
ngx_js_http_process_headers(ngx_js_http_t *http)
{
    ngx_int_t             rc;
    ngx_table_elt_t      *h;
    ngx_js_http_parse_t  *hp;

    ngx_log_debug0(NGX_LOG_DEBUG_EVENT, http->log, 0,
                   "js http process headers");

    hp = &http->http_parse;

    if (http->headers.size == 0) {
        rc = ngx_array_init(&http->headers, http->pool, 4,
                            sizeof(ngx_table_elt_t));
        if (rc != NGX_OK) {
            return NGX_ERROR;
        }
    }

    for ( ;; ) {
        rc = ngx_js_http_parse_header_line(hp, http->buffer);

        if (rc == NGX_OK) {
            h = ngx_array_push(&http->headers);
            if (h == NULL) {
                return NGX_ERROR;
            }

            h->hash = 1;
            h->key.data = hp->header_name_start;
            h->key.len = hp->header_name_end - hp->header_name_start;

            h->value.data = hp->header_start;
            h->value.len = hp->header_end - hp->header_start;

            ngx_log_debug4(NGX_LOG_DEBUG_EVENT, http->log, 0,
                           "js http header \"%*s: %*s\"",
                           h->key.len, h->key.data, h->value.len,
                           h->value.data);

            if (h->key.len == njs_strlen("Transfer-Encoding")
                && h->value.len == njs_strlen("chunked")
                && ngx_strncasecmp(h->key.data, (u_char *) "Transfer-Encoding",
                                   h->key.len) == 0
                && ngx_strncasecmp(h->value.data, (u_char *) "chunked",
                                   h->value.len) == 0)
            {
                hp->chunked = 1;
            }

            if (h->key.len == njs_strlen("Content-Length")
                && ngx_strncasecmp(h->key.data, (u_char *) "Content-Length",
                                   h->key.len) == 0)
            {
                hp->content_length_n = ngx_atoof(h->value.data, h->value.len);
                if (hp->content_length_n == NGX_ERROR) {
                    ngx_js_http_error(http, 0, "invalid fetch content length");
                    return NGX_ERROR;
                }

                if (hp->content_length_n
                    > (off_t) http->max_response_body_size)
                {
                    ngx_js_http_error(http, 0,
                                      "fetch content length is too large");
                    return NGX_ERROR;
                }
            }

            continue;
        }

        if (rc == NGX_DONE) {
            break;
        }

        if (rc == NGX_AGAIN) {
            return NGX_AGAIN;
        }

        /* rc == NGX_ERROR */

        ngx_js_http_error(http, 0, "invalid fetch header");

        return NGX_ERROR;
    }

    njs_chb_destroy(&http->chain);
    njs_chb_init(&http->chain, njs_vm_memory_pool(http->vm));

    http->process = ngx_js_http_process_body;

    return http->process(http);
}


static ngx_int_t
ngx_js_http_process_body(ngx_js_http_t *http)
{
    ssize_t     size, chsize, need;
    ngx_int_t   rc;
    njs_int_t   ret;
    ngx_buf_t  *b;

    ngx_log_debug1(NGX_LOG_DEBUG_EVENT, http->log, 0,
                   "js http process body done:%ui", (ngx_uint_t) http->done);

    if (http->done) {
        size = njs_chb_size(&http->chain);
        if (size < 0) {
            ngx_js_http_error(http, 0, "memory error");
            return NGX_ERROR;
        }

        if (http->http_parse.chunked
            && http->http_parse.content_length_n == -1)
        {
            ngx_js_http_error(http, 0, "invalid fetch chunked response");
            return NGX_ERROR;
        }

        if (http->http_parse.content_length_n == -1
            || size == http->http_parse.content_length_n)
        {
            ret = njs_vm_external_create(http->vm, njs_value_arg(&http->reply),
                                         ngx_http_js_fetch_proto_id, http, 0);
            if (ret != NJS_OK) {
                ngx_js_http_error(http, 0, "fetch object creation failed");
                return NGX_ERROR;
            }

            ngx_js_http_fetch_done(http, &http->reply, NJS_OK);
            return NGX_DONE;
        }

        if (size < http->http_parse.content_length_n) {
            return NGX_AGAIN;
        }

        ngx_js_http_error(http, 0, "fetch trailing data");
        return NGX_ERROR;
    }

    b = http->buffer;

    if (http->http_parse.chunked) {
        rc = ngx_js_http_parse_chunked(&http->http_chunk_parse, b,
                                       &http->chain);
        if (rc == NGX_ERROR) {
            ngx_js_http_error(http, 0, "invalid fetch chunked response");
            return NGX_ERROR;
        }

        size = njs_chb_size(&http->chain);

        if (rc == NGX_OK) {
            http->http_parse.content_length_n = size;
        }

        if (size > http->max_response_body_size * 10) {
            ngx_js_http_error(http, 0, "very large fetch chunked response");
            return NGX_ERROR;
        }

        b->pos = http->http_chunk_parse.pos;

    } else {
        size = njs_chb_size(&http->chain);

        if (http->http_parse.content_length_n == -1) {
            need = http->max_response_body_size - size;

        } else {
            need = http->http_parse.content_length_n - size;
        }

        chsize = ngx_min(need, b->last - b->pos);

        if (size + chsize > http->max_response_body_size) {
            ngx_js_http_error(http, 0, "fetch response body is too large");
            return NGX_ERROR;
        }

        if (chsize > 0) {
            njs_chb_append(&http->chain, b->pos, chsize);
            b->pos += chsize;
        }

        rc = (need > chsize) ? NGX_AGAIN : NGX_DONE;
    }

    if (b->pos == b->end) {
        if (http->chunk == NULL) {
            b = ngx_create_temp_buf(http->pool, http->buffer_size);
            if (b == NULL) {
                ngx_js_http_error(http, 0, "memory error");
                return NGX_ERROR;
            }

            http->buffer = b;
            http->chunk = b;

        } else {
            b->last = b->start;
            b->pos = b->start;
        }
    }

    return rc;
}


static ngx_int_t
ngx_js_http_parse_status_line(ngx_js_http_parse_t *hp, ngx_buf_t *b)
{
    u_char   ch;
    u_char  *p;
    enum {
        sw_start = 0,
        sw_H,
        sw_HT,
        sw_HTT,
        sw_HTTP,
        sw_first_major_digit,
        sw_major_digit,
        sw_first_minor_digit,
        sw_minor_digit,
        sw_status,
        sw_space_after_status,
        sw_status_text,
        sw_almost_done
    } state;

    state = hp->state;

    for (p = b->pos; p < b->last; p++) {
        ch = *p;

        switch (state) {

        /* "HTTP/" */
        case sw_start:
            switch (ch) {
            case 'H':
                state = sw_H;
                break;
            default:
                return NGX_ERROR;
            }
            break;

        case sw_H:
            switch (ch) {
            case 'T':
                state = sw_HT;
                break;
            default:
                return NGX_ERROR;
            }
            break;

        case sw_HT:
            switch (ch) {
            case 'T':
                state = sw_HTT;
                break;
            default:
                return NGX_ERROR;
            }
            break;

        case sw_HTT:
            switch (ch) {
            case 'P':
                state = sw_HTTP;
                break;
            default:
                return NGX_ERROR;
            }
            break;

        case sw_HTTP:
            switch (ch) {
            case '/':
                state = sw_first_major_digit;
                break;
            default:
                return NGX_ERROR;
            }
            break;

        /* the first digit of major HTTP version */
        case sw_first_major_digit:
            if (ch < '1' || ch > '9') {
                return NGX_ERROR;
            }

            state = sw_major_digit;
            break;

        /* the major HTTP version or dot */
        case sw_major_digit:
            if (ch == '.') {
                state = sw_first_minor_digit;
                break;
            }

            if (ch < '0' || ch > '9') {
                return NGX_ERROR;
            }

            break;

        /* the first digit of minor HTTP version */
        case sw_first_minor_digit:
            if (ch < '0' || ch > '9') {
                return NGX_ERROR;
            }

            state = sw_minor_digit;
            break;

        /* the minor HTTP version or the end of the request line */
        case sw_minor_digit:
            if (ch == ' ') {
                state = sw_status;
                break;
            }

            if (ch < '0' || ch > '9') {
                return NGX_ERROR;
            }

            break;

        /* HTTP status code */
        case sw_status:
            if (ch == ' ') {
                break;
            }

            if (ch < '0' || ch > '9') {
                return NGX_ERROR;
            }

            hp->code = hp->code * 10 + (ch - '0');

            if (++hp->count == 3) {
                state = sw_space_after_status;
            }

            break;

        /* space or end of line */
        case sw_space_after_status:
            switch (ch) {
            case ' ':
                state = sw_status_text;
                break;
            case '.':                    /* IIS may send 403.1, 403.2, etc */
                state = sw_status_text;
                break;
            case CR:
                break;
            case LF:
                goto done;
            default:
                return NGX_ERROR;
            }
            break;

        /* any text until end of line */
        case sw_status_text:
            switch (ch) {
            case CR:
                hp->status_text_end = p;
                state = sw_almost_done;
                break;
            case LF:
                hp->status_text_end = p;
                goto done;
            }

            if (hp->status_text == NULL) {
                hp->status_text = p;
            }

            break;

        /* end of status line */
        case sw_almost_done:
            switch (ch) {
            case LF:
                goto done;
            default:
                return NGX_ERROR;
            }
        }
    }

    b->pos = p;
    hp->state = state;

    return NGX_AGAIN;

done:

    b->pos = p + 1;
    hp->state = sw_start;

    return NGX_OK;
}


static ngx_int_t
ngx_js_http_parse_header_line(ngx_js_http_parse_t *hp, ngx_buf_t *b)
{
    u_char  c, ch, *p;
    enum {
        sw_start = 0,
        sw_name,
        sw_space_before_value,
        sw_value,
        sw_space_after_value,
        sw_almost_done,
        sw_header_almost_done
    } state;

    state = hp->state;

    for (p = b->pos; p < b->last; p++) {
        ch = *p;

        switch (state) {

        /* first char */
        case sw_start:

            switch (ch) {
            case CR:
                hp->header_end = p;
                state = sw_header_almost_done;
                break;
            case LF:
                hp->header_end = p;
                goto header_done;
            default:
                state = sw_name;
                hp->header_name_start = p;

                c = (u_char) (ch | 0x20);
                if (c >= 'a' && c <= 'z') {
                    break;
                }

                if (ch >= '0' && ch <= '9') {
                    break;
                }

                return NGX_ERROR;
            }
            break;

        /* header name */
        case sw_name:
            c = (u_char) (ch | 0x20);
            if (c >= 'a' && c <= 'z') {
                break;
            }

            if (ch == ':') {
                hp->header_name_end = p;
                state = sw_space_before_value;
                break;
            }

            if (ch == '-') {
                break;
            }

            if (ch >= '0' && ch <= '9') {
                break;
            }

            if (ch == CR) {
                hp->header_name_end = p;
                hp->header_start = p;
                hp->header_end = p;
                state = sw_almost_done;
                break;
            }

            if (ch == LF) {
                hp->header_name_end = p;
                hp->header_start = p;
                hp->header_end = p;
                goto done;
            }

            return NGX_ERROR;

        /* space* before header value */
        case sw_space_before_value:
            switch (ch) {
            case ' ':
                break;
            case CR:
                hp->header_start = p;
                hp->header_end = p;
                state = sw_almost_done;
                break;
            case LF:
                hp->header_start = p;
                hp->header_end = p;
                goto done;
            default:
                hp->header_start = p;
                state = sw_value;
                break;
            }
            break;

        /* header value */
        case sw_value:
            switch (ch) {
            case ' ':
                hp->header_end = p;
                state = sw_space_after_value;
                break;
            case CR:
                hp->header_end = p;
                state = sw_almost_done;
                break;
            case LF:
                hp->header_end = p;
                goto done;
            }
            break;

        /* space* before end of header line */
        case sw_space_after_value:
            switch (ch) {
            case ' ':
                break;
            case CR:
                state = sw_almost_done;
                break;
            case LF:
                goto done;
            default:
                state = sw_value;
                break;
            }
            break;

        /* end of header line */
        case sw_almost_done:
            switch (ch) {
            case LF:
                goto done;
            default:
                return NGX_ERROR;
            }

        /* end of header */
        case sw_header_almost_done:
            switch (ch) {
            case LF:
                goto header_done;
            default:
                return NGX_ERROR;
            }
        }
    }

    b->pos = p;
    hp->state = state;

    return NGX_AGAIN;

done:

    b->pos = p + 1;
    hp->state = sw_start;

    return NGX_OK;

header_done:

    b->pos = p + 1;
    hp->state = sw_start;

    return NGX_DONE;
}


#define                                                                       \
ngx_size_is_sufficient(cs)                                                    \
    (cs < ((__typeof__(cs)) 1 << (sizeof(cs) * 8 - 4)))


#define NGX_JS_HTTP_CHUNK_MIDDLE     0
#define NGX_JS_HTTP_CHUNK_ON_BORDER  1
#define NGX_JS_HTTP_CHUNK_END        2


static ngx_int_t
ngx_js_http_chunk_buffer(ngx_js_http_chunk_parse_t *hcp, ngx_buf_t *b,
    njs_chb_t *chain)
{
    size_t  size;

    size = b->last - hcp->pos;

    if (hcp->chunk_size < size) {
        njs_chb_append(chain, hcp->pos, hcp->chunk_size);
        hcp->pos += hcp->chunk_size;

        return NGX_JS_HTTP_CHUNK_END;
    }

    njs_chb_append(chain, hcp->pos, size);
    hcp->pos += size;

    hcp->chunk_size -= size;

    if (hcp->chunk_size == 0) {
        return NGX_JS_HTTP_CHUNK_ON_BORDER;
    }

    return NGX_JS_HTTP_CHUNK_MIDDLE;
}


static ngx_int_t
ngx_js_http_parse_chunked(ngx_js_http_chunk_parse_t *hcp,
    ngx_buf_t *b, njs_chb_t *chain)
{
    u_char     c, ch;
    ngx_int_t  rc;

    enum {
        sw_start = 0,
        sw_chunk_size,
        sw_chunk_size_linefeed,
        sw_chunk_end_newline,
        sw_chunk_end_linefeed,
        sw_chunk,
    } state;

    state = hcp->state;

    hcp->pos = b->pos;

    while (hcp->pos < b->last) {
        /*
         * The sw_chunk state is tested outside the switch
         * to preserve hcp->pos and to not touch memory.
         */
        if (state == sw_chunk) {
            rc = ngx_js_http_chunk_buffer(hcp, b, chain);
            if (rc == NGX_ERROR) {
                return rc;
            }

            if (rc == NGX_JS_HTTP_CHUNK_MIDDLE) {
                break;
            }

            state = sw_chunk_end_newline;

            if (rc == NGX_JS_HTTP_CHUNK_ON_BORDER) {
                break;
            }

            /* rc == NGX_JS_HTTP_CHUNK_END */
        }

        ch = *hcp->pos++;

        switch (state) {

        case sw_start:
            state = sw_chunk_size;

            c = ch - '0';

            if (c <= 9) {
                hcp->chunk_size = c;
                continue;
            }

            c = (ch | 0x20) - 'a';

            if (c <= 5) {
                hcp->chunk_size = 0x0A + c;
                continue;
            }

            return NGX_ERROR;

        case sw_chunk_size:

            c = ch - '0';

            if (c > 9) {
                c = (ch | 0x20) - 'a';

                if (c <= 5) {
                    c += 0x0A;

                } else if (ch == '\r') {
                    state = sw_chunk_size_linefeed;
                    continue;

                } else {
                    return NGX_ERROR;
                }
            }

            if (ngx_size_is_sufficient(hcp->chunk_size)) {
                hcp->chunk_size = (hcp->chunk_size << 4) + c;
                continue;
            }

            return NGX_ERROR;

        case sw_chunk_size_linefeed:
            if (ch == '\n') {

                if (hcp->chunk_size != 0) {
                    state = sw_chunk;
                    continue;
                }

                hcp->last = 1;
                state = sw_chunk_end_newline;
                continue;
            }

            return NGX_ERROR;

        case sw_chunk_end_newline:
            if (ch == '\r') {
                state = sw_chunk_end_linefeed;
                continue;
            }

            return NGX_ERROR;

        case sw_chunk_end_linefeed:
            if (ch == '\n') {

                if (!hcp->last) {
                    state = sw_start;
                    continue;
                }

                return NGX_OK;
            }

            return NGX_ERROR;

        case sw_chunk:
            /*
             * This state is processed before the switch.
             * It added here just to suppress a warning.
             */
            continue;
        }
    }

    hcp->state = state;

    return NGX_AGAIN;
}


static void
ngx_js_http_dummy_handler(ngx_event_t *ev)
{
    ngx_log_debug0(NGX_LOG_DEBUG_EVENT, ev->log, 0, "js http dummy handler");
}


static njs_int_t
ngx_response_js_ext_header_get(njs_vm_t *vm, njs_value_t *value,
    njs_str_t *name, njs_value_t *retval, njs_bool_t as_array)
{
    u_char           *data, *p, *start, *end;
    size_t            len;
    njs_int_t         rc;
    ngx_uint_t        i;
    ngx_js_http_t    *http;
    ngx_table_elt_t  *header, *h;

    http = njs_vm_external(vm, ngx_http_js_fetch_proto_id, value);
    if (http == NULL) {
        njs_value_null_set(retval);
        return NJS_DECLINED;
    }

    p = NULL;
    start = NULL;
    end = NULL;

    if (as_array) {
        rc = njs_vm_array_alloc(vm, retval, 2);
        if (rc != NJS_OK) {
            return NJS_ERROR;
        }
    }

    header = http->headers.elts;

    for (i = 0; i < http->headers.nelts; i++) {
        h = &header[i];

        if (h->hash == 0
            || h->key.len != name->length
            || ngx_strncasecmp(h->key.data, name->start, name->length) != 0)
        {
            continue;
        }

        if (as_array) {
            value = njs_vm_array_push(vm, retval);
            if (value == NULL) {
                return NJS_ERROR;
            }

            rc = njs_vm_value_string_set(vm, value, h->value.data,
                                         h->value.len);
            if (rc != NJS_OK) {
                return NJS_ERROR;
            }

            continue;
        }

        if (p == NULL) {
            start = h->value.data;
            end = h->value.data + h->value.len;
            p = end;
            continue;
        }

        if (p + h->value.len + 1 > end) {
            len = njs_max(p + h->value.len + 1 - start, 2 * (end - start));

            data = ngx_pnalloc(http->pool, len);
            if (data == NULL) {
                njs_vm_memory_error(vm);
                return NJS_ERROR;
            }

            p = ngx_cpymem(data, start, p - start);
            start = data;
            end = data + len;
        }

        *p++ = ',';
        p = ngx_cpymem(p, h->value.data, h->value.len);
    }

    if (as_array) {
        return NJS_OK;
    }

    if (p == NULL) {
        njs_value_null_set(retval);
        return NJS_DECLINED;
    }

    return njs_vm_value_string_set(vm, retval, start, p - start);
}


static njs_int_t
ngx_response_js_ext_headers_get(njs_vm_t *vm, njs_value_t *args,
     njs_uint_t nargs, njs_index_t as_array)
{
    njs_int_t  ret;
    njs_str_t  name;

    ret = ngx_js_string(vm, njs_arg(args, nargs, 1), &name);
    if (ret != NJS_OK) {
        return NJS_ERROR;
    }

    ret = ngx_response_js_ext_header_get(vm, njs_argument(args, 0),
                                         &name, njs_vm_retval(vm), as_array);

    return (ret != NJS_ERROR) ? NJS_OK : NJS_ERROR;
}


static njs_int_t
ngx_response_js_ext_headers_has(njs_vm_t *vm, njs_value_t *args,
     njs_uint_t nargs, njs_index_t unused)
{
    njs_int_t  ret;
    njs_str_t  name;

    ret = ngx_js_string(vm, njs_arg(args, nargs, 1), &name);
    if (ret != NJS_OK) {
        return NJS_ERROR;
    }

    ret = ngx_response_js_ext_header_get(vm, njs_argument(args, 0),
                                         &name, njs_vm_retval(vm), 0);
    if (ret == NJS_ERROR) {
        return NJS_ERROR;
    }

    njs_value_boolean_set(njs_vm_retval(vm), ret == NJS_OK);

    return NJS_OK;
}


static njs_int_t
ngx_response_js_ext_header(njs_vm_t *vm, njs_object_prop_t *prop,
    njs_value_t *value, njs_value_t *setval, njs_value_t *retval)
{
    njs_int_t  ret;
    njs_str_t  name;

    ret = njs_vm_prop_name(vm, prop, &name);
    if (ret != NJS_OK) {
        return NJS_ERROR;
    }

    return ngx_response_js_ext_header_get(vm, value, &name, retval, 0);
}


static njs_int_t
ngx_response_js_ext_keys(njs_vm_t *vm, njs_value_t *value, njs_value_t *keys)
{
    njs_int_t         rc;
    njs_str_t         hdr;
    ngx_uint_t        i, k, length;
    njs_value_t      *start;
    ngx_js_http_t    *http;
    ngx_table_elt_t  *h, *headers;

    http = njs_vm_external(vm, ngx_http_js_fetch_proto_id, value);
    if (http == NULL) {
        njs_value_undefined_set(keys);
        return NJS_DECLINED;
    }

    rc = njs_vm_array_alloc(vm, keys, 8);
    if (rc != NJS_OK) {
        return NJS_ERROR;
    }

    length = 0;
    headers = http->headers.elts;

    for (i = 0; i < http->headers.nelts; i++) {
        h = &headers[i];
        start = njs_vm_array_start(vm, keys);

        for (k = 0; k < length; k++) {
            njs_value_string_get(njs_argument(start, k), &hdr);

            if (h->key.len == hdr.length
                && ngx_strncasecmp(h->key.data, hdr.start, hdr.length) == 0)
            {
                break;
            }
        }

        if (k == length) {
            value = njs_vm_array_push(vm, keys);
            if (value == NULL) {
                return NJS_ERROR;
            }

            rc = njs_vm_value_string_set(vm, value, h->key.data, h->key.len);
            if (rc != NJS_OK) {
                return NJS_ERROR;
            }

            length++;
        }
    }

    return NJS_OK;
}


static njs_int_t
ngx_response_js_ext_body(njs_vm_t *vm, njs_value_t *args,
     njs_uint_t nargs, njs_index_t type)
{
    njs_int_t            ret;
    njs_str_t            string;
    ngx_js_http_t       *http;
    njs_opaque_value_t   retval;

    http = njs_vm_external(vm, ngx_http_js_fetch_proto_id,
                           njs_argument(args, 0));
    if (http == NULL) {
        njs_value_undefined_set(njs_vm_retval(vm));
        return NJS_DECLINED;
    }

    if (http->body_used) {
        njs_vm_error(vm, "body stream already read");
        return NJS_ERROR;
    }

    http->body_used = 1;

    ret = njs_chb_join(&http->chain, &string);
    if (ret != NJS_OK) {
        njs_vm_memory_error(http->vm);
        return NJS_ERROR;
    }

    switch (type) {
    case NGX_JS_BODY_ARRAY_BUFFER:
        ret = njs_vm_value_array_buffer_set(http->vm, njs_value_arg(&retval),
                                            string.start, string.length);
        if (ret != NJS_OK) {
            njs_vm_memory_error(http->vm);
            return NJS_ERROR;
        }

        break;

    case NGX_JS_BODY_JSON:
    case NGX_JS_BODY_TEXT:
    default:
        ret = njs_vm_value_string_set(http->vm, njs_value_arg(&retval),
                                      string.start, string.length);
        if (ret != NJS_OK) {
            njs_vm_memory_error(http->vm);
            return NJS_ERROR;
        }

        if (type == NGX_JS_BODY_JSON) {
            ret = njs_vm_json_parse(vm, njs_value_arg(&retval), 1);
            njs_value_assign(&retval, njs_vm_retval(vm));
        }
    }

    return ngx_js_fetch_promissified_result(http->vm, njs_value_arg(&retval),
                                            ret);
}


static njs_int_t
ngx_response_js_ext_body_used(njs_vm_t *vm, njs_object_prop_t *prop,
    njs_value_t *value, njs_value_t *setval, njs_value_t *retval)
{
    ngx_js_http_t  *http;

    http = njs_vm_external(vm, ngx_http_js_fetch_proto_id, value);
    if (http == NULL) {
        njs_value_undefined_set(retval);
        return NJS_DECLINED;
    }

    njs_value_boolean_set(retval, http->body_used);

    return NJS_OK;
}


static njs_int_t
ngx_response_js_ext_ok(njs_vm_t *vm, njs_object_prop_t *prop,
    njs_value_t *value, njs_value_t *setval, njs_value_t *retval)
{
    ngx_uint_t      code;
    ngx_js_http_t  *http;

    http = njs_vm_external(vm, ngx_http_js_fetch_proto_id, value);
    if (http == NULL) {
        njs_value_undefined_set(retval);
        return NJS_DECLINED;
    }

    code = http->http_parse.code;

    njs_value_boolean_set(retval, code >= 200 && code < 300);

    return NJS_OK;
}


static njs_int_t
ngx_response_js_ext_status(njs_vm_t *vm, njs_object_prop_t *prop,
    njs_value_t *value, njs_value_t *setval, njs_value_t *retval)
{
    ngx_js_http_t  *http;

    http = njs_vm_external(vm, ngx_http_js_fetch_proto_id, value);
    if (http == NULL) {
        njs_value_undefined_set(retval);
        return NJS_DECLINED;
    }

    njs_value_number_set(retval, http->http_parse.code);

    return NJS_OK;
}


static njs_int_t
ngx_response_js_ext_status_text(njs_vm_t *vm, njs_object_prop_t *prop,
    njs_value_t *value, njs_value_t *setval, njs_value_t *retval)
{
    ngx_js_http_t  *http;

    http = njs_vm_external(vm, ngx_http_js_fetch_proto_id, value);
    if (http == NULL) {
        njs_value_undefined_set(retval);
        return NJS_DECLINED;
    }

    njs_vm_value_string_set(vm, retval, http->http_parse.status_text,
                            http->http_parse.status_text_end
                            - http->http_parse.status_text);

    return NJS_OK;
}


static njs_int_t
ngx_response_js_ext_type(njs_vm_t *vm, njs_object_prop_t *prop,
    njs_value_t *value, njs_value_t *setval, njs_value_t *retval)
{
    ngx_js_http_t  *http;

    http = njs_vm_external(vm, ngx_http_js_fetch_proto_id, value);
    if (http == NULL) {
        njs_value_undefined_set(retval);
        return NJS_DECLINED;
    }

    return njs_vm_value_string_set(vm, retval, (u_char *) "basic",
                                   njs_length("basic"));
}


ngx_int_t
ngx_js_fetch_init(njs_vm_t *vm, ngx_log_t *log)
{
    ngx_http_js_fetch_proto_id = njs_vm_external_prototype(vm,
                                        ngx_js_ext_http_response,
                                        njs_nitems(ngx_js_ext_http_response));
    if (ngx_http_js_fetch_proto_id < 0) {
        ngx_log_error(NGX_LOG_EMERG, log, 0,
                      "failed to add js http.response proto");
        return NGX_ERROR;
    }

    return NGX_OK;
}
