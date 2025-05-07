
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
#include "ngx_js_http.h"


typedef struct {
    njs_str_t       name;
    njs_int_t       value;
} ngx_js_entry_t;


typedef struct {
    ngx_js_http_t                  http;

    njs_vm_t                      *vm;
    ngx_js_event_t                *event;

    njs_opaque_value_t             response_value;

    njs_opaque_value_t             promise;
    njs_opaque_value_t             promise_callbacks[2];
} ngx_js_fetch_t;


static njs_int_t ngx_js_method_process(njs_vm_t *vm, ngx_js_request_t *r);
static njs_int_t ngx_js_headers_inherit(njs_vm_t *vm, ngx_js_headers_t *headers,
    ngx_js_headers_t *orig);
static njs_int_t ngx_js_headers_fill(njs_vm_t *vm, ngx_js_headers_t *headers,
    njs_value_t *init);
static ngx_js_fetch_t *ngx_js_fetch_alloc(njs_vm_t *vm, ngx_pool_t *pool,
    ngx_log_t *log);
static void ngx_js_fetch_error(ngx_js_http_t *http, const char *err);
static void ngx_js_fetch_destructor(ngx_js_event_t *event);
static njs_int_t ngx_js_fetch_promissified_result(njs_vm_t *vm,
    njs_value_t *result, njs_int_t rc, njs_value_t *retval);
static void ngx_js_fetch_done(ngx_js_fetch_t *fetch, njs_opaque_value_t *retval,
                              njs_int_t rc);
static njs_int_t ngx_js_http_promise_trampoline(njs_vm_t *vm,
    njs_value_t *args, njs_uint_t nargs, njs_index_t unused,
    njs_value_t *retval);

static njs_int_t ngx_js_request_constructor(njs_vm_t *vm,
    ngx_js_request_t *request, ngx_url_t *u, njs_external_ptr_t external,
    njs_value_t *args, njs_uint_t nargs);

static ngx_int_t ngx_js_fetch_append_headers(ngx_js_http_t *http,
    ngx_js_headers_t *headers, u_char *name, size_t len, u_char *value,
    size_t vlen);
static void ngx_js_fetch_process_done(ngx_js_http_t *http);
static njs_int_t ngx_js_headers_append(njs_vm_t *vm, ngx_js_headers_t *headers,
    u_char *name, size_t len, u_char *value, size_t vlen);

static njs_int_t ngx_headers_js_ext_append(njs_vm_t *vm, njs_value_t *args,
    njs_uint_t nargs, njs_index_t unused, njs_value_t *retval);
static njs_int_t ngx_headers_js_ext_delete(njs_vm_t *vm, njs_value_t *args,
    njs_uint_t nargs, njs_index_t unused, njs_value_t *retval);
static njs_int_t ngx_headers_js_ext_for_each(njs_vm_t *vm, njs_value_t *args,
    njs_uint_t nargs, njs_index_t as_array, njs_value_t *retval);
static njs_int_t ngx_headers_js_ext_get(njs_vm_t *vm, njs_value_t *args,
    njs_uint_t nargs, njs_index_t as_array, njs_value_t *retval);
static njs_int_t ngx_headers_js_ext_has(njs_vm_t *vm, njs_value_t *args,
    njs_uint_t nargs, njs_index_t unused, njs_value_t *retval);
static njs_int_t ngx_headers_js_ext_prop(njs_vm_t *vm,
    njs_object_prop_t *prop, uint32_t atom_id, njs_value_t *value,
    njs_value_t *setval, njs_value_t *retval);
static njs_int_t ngx_headers_js_ext_keys(njs_vm_t *vm, njs_value_t *value,
    njs_value_t *keys);
static njs_int_t ngx_headers_js_ext_set(njs_vm_t *vm, njs_value_t *args,
    njs_uint_t nargs, njs_index_t unused, njs_value_t *retval);
static njs_int_t ngx_request_js_ext_body(njs_vm_t *vm, njs_value_t *args,
     njs_uint_t nargs, njs_index_t unused, njs_value_t *retval);
static njs_int_t ngx_request_js_ext_body_used(njs_vm_t *vm,
    njs_object_prop_t *prop, uint32_t unused, njs_value_t *value,
    njs_value_t *setval, njs_value_t *retval);
static njs_int_t ngx_request_js_ext_cache(njs_vm_t *vm,
    njs_object_prop_t *prop, uint32_t unused, njs_value_t *value,
    njs_value_t *setval, njs_value_t *retval);
static njs_int_t ngx_request_js_ext_credentials(njs_vm_t *vm,
    njs_object_prop_t *prop, uint32_t unused, njs_value_t *value,
    njs_value_t *setval, njs_value_t *retval);
static njs_int_t ngx_request_js_ext_headers(njs_vm_t *vm,
    njs_object_prop_t *prop, uint32_t unused, njs_value_t *value,
    njs_value_t *setval, njs_value_t *retval);
static njs_int_t ngx_request_js_ext_mode(njs_vm_t *vm, njs_object_prop_t *prop,
    uint32_t unused, njs_value_t *value, njs_value_t *setval,
    njs_value_t *retval);
static njs_int_t ngx_response_js_ext_status(njs_vm_t *vm,
    njs_object_prop_t *prop, uint32_t unused, njs_value_t *value,
    njs_value_t *setval, njs_value_t *retval);
static njs_int_t ngx_response_js_ext_status_text(njs_vm_t *vm,
    njs_object_prop_t *prop, uint32_t unused, njs_value_t *value,
    njs_value_t *setval, njs_value_t *retval);
static njs_int_t ngx_response_js_ext_ok(njs_vm_t *vm,
    njs_object_prop_t *prop, uint32_t unused, njs_value_t *value,
    njs_value_t *setval, njs_value_t *retval);
static njs_int_t ngx_response_js_ext_body_used(njs_vm_t *vm,
    njs_object_prop_t *prop, uint32_t unused, njs_value_t *value,
    njs_value_t *setval, njs_value_t *retval);
static njs_int_t ngx_response_js_ext_headers(njs_vm_t *vm,
    njs_object_prop_t *prop, uint32_t unused, njs_value_t *value,
    njs_value_t *setval, njs_value_t *retval);
static njs_int_t ngx_response_js_ext_type(njs_vm_t *vm,
    njs_object_prop_t *prop, uint32_t unused, njs_value_t *value,
    njs_value_t *setval, njs_value_t *retval);
static njs_int_t ngx_response_js_ext_body(njs_vm_t *vm, njs_value_t *args,
     njs_uint_t nargs, njs_index_t unused, njs_value_t *retval);

static njs_int_t ngx_fetch_flag(njs_vm_t *vm, const ngx_js_entry_t *entries,
    njs_int_t value, njs_value_t *retval);
static njs_int_t ngx_fetch_flag_set(njs_vm_t *vm, const ngx_js_entry_t *entries,
     njs_value_t *value, const char *type);

static njs_int_t ngx_js_fetch_init(njs_vm_t *vm);


static const ngx_js_entry_t ngx_js_fetch_credentials[] = {
    { njs_str("same-origin"), CREDENTIALS_SAME_ORIGIN },
    { njs_str("omit"), CREDENTIALS_OMIT },
    { njs_str("include"), CREDENTIALS_INCLUDE },
    { njs_null_str, 0 },
};


static const ngx_js_entry_t ngx_js_fetch_cache_modes[] = {
    { njs_str("default"), CACHE_MODE_DEFAULT },
    { njs_str("no-store"), CACHE_MODE_NO_STORE },
    { njs_str("reload"), CACHE_MODE_RELOAD },
    { njs_str("no-cache"), CACHE_MODE_NO_CACHE },
    { njs_str("force-cache"), CACHE_MODE_FORCE_CACHE },
    { njs_str("only-if-cached"), CACHE_MODE_ONLY_IF_CACHED },
    { njs_null_str, 0 },
};


static const ngx_js_entry_t ngx_js_fetch_modes[] = {
    { njs_str("no-cors"), MODE_NO_CORS },
    { njs_str("cors"), MODE_CORS },
    { njs_str("same-origin"), MODE_SAME_ORIGIN },
    { njs_str("navigate"), MODE_NAVIGATE },
    { njs_str("websocket"), MODE_WEBSOCKET },
    { njs_null_str, 0 },
};


static njs_external_t  ngx_js_ext_http_headers[] = {

    {
        .flags = NJS_EXTERN_PROPERTY | NJS_EXTERN_SYMBOL,
        .name.symbol = NJS_SYMBOL_TO_STRING_TAG,
        .u.property = {
            .value = "Headers",
        }
    },

    {
        .flags = NJS_EXTERN_SELF,
        .u.object = {
            .enumerable = 1,
            .prop_handler = ngx_headers_js_ext_prop,
            .keys = ngx_headers_js_ext_keys,
        }
    },

    {
        .flags = NJS_EXTERN_METHOD,
        .name.string = njs_str("append"),
        .writable = 1,
        .configurable = 1,
        .enumerable = 1,
        .u.method = {
            .native = ngx_headers_js_ext_append,
        }
    },

    {
        .flags = NJS_EXTERN_METHOD,
        .name.string = njs_str("delete"),
        .writable = 1,
        .configurable = 1,
        .enumerable = 1,
        .u.method = {
            .native = ngx_headers_js_ext_delete,
        }
    },

    {
        .flags = NJS_EXTERN_METHOD,
        .name.string = njs_str("forEach"),
        .writable = 1,
        .configurable = 1,
        .enumerable = 1,
        .u.method = {
            .native = ngx_headers_js_ext_for_each,
        }
    },

    {
        .flags = NJS_EXTERN_METHOD,
        .name.string = njs_str("get"),
        .writable = 1,
        .configurable = 1,
        .enumerable = 1,
        .u.method = {
            .native = ngx_headers_js_ext_get,
        }
    },

    {
        .flags = NJS_EXTERN_METHOD,
        .name.string = njs_str("getAll"),
        .writable = 1,
        .configurable = 1,
        .enumerable = 1,
        .u.method = {
            .native = ngx_headers_js_ext_get,
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
            .native = ngx_headers_js_ext_has,
        }
    },

    {
        .flags = NJS_EXTERN_METHOD,
        .name.string = njs_str("set"),
        .writable = 1,
        .configurable = 1,
        .enumerable = 1,
        .u.method = {
            .native = ngx_headers_js_ext_set,
        }
    },

};


static njs_external_t  ngx_js_ext_http_request[] = {

    {
        .flags = NJS_EXTERN_PROPERTY | NJS_EXTERN_SYMBOL,
        .name.symbol = NJS_SYMBOL_TO_STRING_TAG,
        .u.property = {
            .value = "Request",
        }
    },

    {
        .flags = NJS_EXTERN_METHOD,
        .name.string = njs_str("arrayBuffer"),
        .writable = 1,
        .configurable = 1,
        .enumerable = 1,
        .u.method = {
            .native = ngx_request_js_ext_body,
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
            .handler = ngx_request_js_ext_body_used,
        }
    },

    {
        .flags = NJS_EXTERN_PROPERTY,
        .name.string = njs_str("cache"),
        .enumerable = 1,
        .u.property = {
            .handler = ngx_request_js_ext_cache,
        }
    },

    {
        .flags = NJS_EXTERN_PROPERTY,
        .name.string = njs_str("credentials"),
        .enumerable = 1,
        .u.property = {
            .handler = ngx_request_js_ext_credentials,
        }
    },

    {
        .flags = NJS_EXTERN_METHOD,
        .name.string = njs_str("json"),
        .writable = 1,
        .configurable = 1,
        .enumerable = 1,
        .u.method = {
            .native = ngx_request_js_ext_body,
            .magic8 = NGX_JS_BODY_JSON
        }
    },

    {
        .flags = NJS_EXTERN_PROPERTY,
        .name.string = njs_str("headers"),
        .enumerable = 1,
        .u.property = {
            .handler = ngx_request_js_ext_headers,
        }
    },

    {
        .flags = NJS_EXTERN_PROPERTY,
        .name.string = njs_str("method"),
        .enumerable = 1,
        .u.property = {
            .handler = ngx_js_ext_string,
            .magic32 = offsetof(ngx_js_request_t, method),
        }
    },

    {
        .flags = NJS_EXTERN_PROPERTY,
        .name.string = njs_str("mode"),
        .enumerable = 1,
        .u.property = {
            .handler = ngx_request_js_ext_mode,
        }
    },

    {
        .flags = NJS_EXTERN_METHOD,
        .name.string = njs_str("text"),
        .writable = 1,
        .configurable = 1,
        .enumerable = 1,
        .u.method = {
            .native = ngx_request_js_ext_body,
            .magic8 = NGX_JS_BODY_TEXT
        }
    },

    {
        .flags = NJS_EXTERN_PROPERTY,
        .name.string = njs_str("url"),
        .enumerable = 1,
        .u.property = {
            .handler = ngx_js_ext_string,
            .magic32 = offsetof(ngx_js_request_t, url),
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
        .flags = NJS_EXTERN_PROPERTY,
        .name.string = njs_str("headers"),
        .enumerable = 1,
        .u.property = {
            .handler = ngx_response_js_ext_headers,
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
            .magic32 = offsetof(ngx_js_response_t, url),
        }
    },
};


static njs_int_t    ngx_http_js_fetch_request_proto_id;
static njs_int_t    ngx_http_js_fetch_response_proto_id;
static njs_int_t    ngx_http_js_fetch_headers_proto_id;


njs_module_t  ngx_js_fetch_module = {
    .name = njs_str("fetch"),
    .preinit = NULL,
    .init = ngx_js_fetch_init,
};


njs_int_t
ngx_js_ext_fetch(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t unused, njs_value_t *retval)
{
    njs_int_t            ret;
    njs_str_t            str;
    ngx_url_t            u;
    ngx_uint_t           i;
    njs_bool_t           has_host;
    ngx_pool_t          *pool;
    njs_value_t         *init, *value;
    ngx_js_http_t       *http;
    ngx_js_fetch_t      *fetch;
    ngx_list_part_t     *part;
    ngx_js_tb_elt_t     *h;
    ngx_js_request_t     request;
    ngx_connection_t    *c;
    ngx_resolver_ctx_t  *ctx;
    njs_external_ptr_t   external;
    njs_opaque_value_t   lvalue;

    static const njs_str_t buffer_size_key = njs_str("buffer_size");
    static const njs_str_t body_size_key = njs_str("max_response_body_size");
#if (NGX_SSL)
    static const njs_str_t verify_key = njs_str("verify");
#endif

    external = njs_vm_external_ptr(vm);
    c = ngx_external_connection(vm, external);
    pool = ngx_external_pool(vm, external);

    fetch = ngx_js_fetch_alloc(vm, pool, c->log);
    if (fetch == NULL) {
        return NJS_ERROR;
    }

    http = &fetch->http;

    ret = ngx_js_request_constructor(vm, &request, &u, external, args, nargs);
    if (ret != NJS_OK) {
        goto fail;
    }

    http->response.url = request.url;
    http->timeout = ngx_external_fetch_timeout(vm, external);
    http->buffer_size = ngx_external_buffer_size(vm, external);
    http->max_response_body_size =
                           ngx_external_max_response_buffer_size(vm, external);

#if (NGX_SSL)
    if (u.default_port == 443) {
        http->ssl = ngx_external_ssl(vm, external);
        http->ssl_verify = ngx_external_ssl_verify(vm, external);
    }
#endif

    init = njs_arg(args, nargs, 2);

    if (njs_value_is_object(init)) {
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

    str.start = request.method.data;
    str.length = request.method.len;

    http->header_only = njs_strstr_eq(&str, &njs_str_value("HEAD"));

    NJS_CHB_MP_INIT(&http->chain, njs_vm_memory_pool(vm));
    NJS_CHB_MP_INIT(&http->response.chain, njs_vm_memory_pool(vm));

    njs_chb_append(&http->chain, request.method.data, request.method.len);
    njs_chb_append_literal(&http->chain, " ");

    if (u.uri.len == 0 || u.uri.data[0] != '/') {
        njs_chb_append_literal(&http->chain, "/");
    }

    njs_chb_append(&http->chain, u.uri.data, u.uri.len);
    njs_chb_append_literal(&http->chain, " HTTP/1.1" CRLF);

    has_host = 0;
    part = &request.headers.header_list.part;
    h = part->elts;

    for (i = 0; /* void */; i++) {

        if (i >= part->nelts) {
            if (part->next == NULL) {
                break;
            }

            part = part->next;
            h = part->elts;
            i = 0;
        }

        if (h[i].hash == 0) {
            continue;
        }

        if (h[i].key.len == 4
            && ngx_strncasecmp(h[i].key.data, (u_char *) "Host", 4) == 0)
        {
            has_host = 1;
            njs_chb_append_literal(&http->chain, "Host: ");
            njs_chb_append(&http->chain, h[i].value.data, h[i].value.len);
            njs_chb_append_literal(&http->chain, CRLF);
            break;
        }
    }

    if (!has_host) {
        njs_chb_append_literal(&http->chain, "Host: ");
        njs_chb_append(&http->chain, u.host.data, u.host.len);

        if (!u.no_port) {
            njs_chb_sprintf(&http->chain, 32, ":%d", u.port);
        }

        njs_chb_append_literal(&http->chain, CRLF);
    }

    part = &request.headers.header_list.part;
    h = part->elts;

    for (i = 0; /* void */; i++) {

        if (i >= part->nelts) {
            if (part->next == NULL) {
                break;
            }

            part = part->next;
            h = part->elts;
            i = 0;
        }

        if (h[i].hash == 0) {
            continue;
        }

        if (h[i].key.len == 4
            && ngx_strncasecmp(h[i].key.data, (u_char *) "Host", 4) == 0)
        {
            continue;
        }

        njs_chb_append(&http->chain, h[i].key.data, h[i].key.len);
        njs_chb_append_literal(&http->chain, ": ");
        njs_chb_append(&http->chain, h[i].value.data, h[i].value.len);
        njs_chb_append_literal(&http->chain, CRLF);
    }

    njs_chb_append_literal(&http->chain, "Connection: close" CRLF);

#if (NGX_SSL)
    http->tls_name.data = u.host.data;
    http->tls_name.len = u.host.len;
#endif

    if (request.body.len != 0) {
        njs_chb_sprintf(&http->chain, 32, "Content-Length: %uz" CRLF CRLF,
                        request.body.len);
        njs_chb_append(&http->chain, request.body.data, request.body.len);

    } else {
        njs_chb_append_literal(&http->chain, CRLF);
    }

    if (u.addrs == NULL) {
        ctx = ngx_js_http_resolve(http, ngx_external_resolver(vm, external),
                                  &u.host, u.port,
                                  ngx_external_resolver_timeout(vm, external));
        if (ctx == NULL) {
            njs_vm_memory_error(vm);
            return NJS_ERROR;
        }

        if (ctx == NGX_NO_RESOLVER) {
            njs_vm_error(vm, "no resolver defined");
            goto fail;
        }

        njs_value_assign(retval, njs_value_arg(&fetch->promise));

        return NJS_OK;
    }

    http->naddrs = 1;
    ngx_memcpy(&http->addr, &u.addrs[0], sizeof(ngx_addr_t));
    http->addrs = &http->addr;

    ngx_js_http_connect(http);

    njs_value_assign(retval, njs_value_arg(&fetch->promise));

    return NJS_OK;

fail:

    njs_vm_exception_get(vm, njs_value_arg(&lvalue));

    ngx_js_fetch_done(fetch, &lvalue, NJS_ERROR);

    njs_value_assign(retval, njs_value_arg(&fetch->promise));

    return NJS_OK;
}


static njs_int_t
ngx_js_ext_headers_constructor(njs_vm_t *vm, njs_value_t *args,
    njs_uint_t nargs, njs_index_t unused, njs_value_t *retval)
{
    ngx_int_t          rc;
    njs_int_t          ret;
    njs_value_t       *init;
    ngx_pool_t        *pool;
    ngx_js_headers_t  *headers;

    pool = ngx_external_pool(vm, njs_vm_external_ptr(vm));

    headers = ngx_palloc(pool, sizeof(ngx_js_headers_t));
    if (headers == NULL) {
        njs_vm_memory_error(vm);
        return NJS_ERROR;
    }

    headers->guard = GUARD_NONE;

    rc = ngx_list_init(&headers->header_list, pool, 4, sizeof(ngx_js_tb_elt_t));
    if (rc != NGX_OK) {
        njs_vm_memory_error(vm);
        return NJS_ERROR;
    }

    init = njs_arg(args, nargs, 1);

    if (njs_value_is_object(init)) {
        ret = ngx_js_headers_fill(vm, headers, init);
        if (ret != NJS_OK) {
            return NJS_ERROR;
        }
    }

    return njs_vm_external_create(vm, retval,
                                  ngx_http_js_fetch_headers_proto_id, headers,
                                  0);
}


static njs_int_t
ngx_js_ext_request_constructor(njs_vm_t *vm, njs_value_t *args,
    njs_uint_t nargs, njs_index_t unused, njs_value_t *retval)
{
    njs_int_t          ret;
    ngx_url_t          u;
    ngx_js_request_t  *request;

    request = njs_mp_alloc(njs_vm_memory_pool(vm), sizeof(ngx_js_request_t));
    if (request == NULL) {
        njs_vm_memory_error(vm);
        return NJS_ERROR;
    }

    ret = ngx_js_request_constructor(vm, request, &u, njs_vm_external_ptr(vm),
                                     args, nargs);
    if (ret != NJS_OK) {
        return NJS_ERROR;
    }

    return njs_vm_external_create(vm, retval,
                                  ngx_http_js_fetch_request_proto_id, request,
                                  0);
}


static njs_int_t
ngx_js_ext_response_constructor(njs_vm_t *vm, njs_value_t *args,
    njs_uint_t nargs, njs_index_t unused, njs_value_t *retval)
{
    u_char              *p, *end;
    ngx_int_t            rc;
    njs_int_t            ret;
    njs_str_t            bd;
    ngx_pool_t          *pool;
    njs_value_t         *body, *init, *value;
    ngx_js_response_t   *response;
    njs_opaque_value_t   lvalue;

    static const njs_str_t headers = njs_str("headers");
    static const njs_str_t status = njs_str("status");
    static const njs_str_t status_text = njs_str("statusText");

    response = njs_mp_zalloc(njs_vm_memory_pool(vm), sizeof(ngx_js_response_t));
    if (response == NULL) {
        njs_vm_memory_error(vm);
        return NJS_ERROR;
    }

    /*
     * set by njs_mp_zalloc():
     *
     *  request->url.length = 0;
     *  request->status_text.length = 0;
     */

    response->code = 200;
    response->headers.guard = GUARD_RESPONSE;

    pool = ngx_external_pool(vm, njs_vm_external_ptr(vm));

    rc = ngx_list_init(&response->headers.header_list, pool, 4,
                       sizeof(ngx_js_tb_elt_t));
    if (rc != NGX_OK) {
        njs_vm_memory_error(vm);
        return NJS_ERROR;
    }

    init = njs_arg(args, nargs, 2);

    if (njs_value_is_object(init)) {
        value = njs_vm_object_prop(vm, init, &status, &lvalue);
        if (value != NULL) {
            if (ngx_js_integer(vm, value, &response->code) != NGX_OK) {
                njs_vm_error(vm, "invalid Response status");
                return NJS_ERROR;
            }

            if (response->code < 200 || response->code > 599) {
                njs_vm_error(vm, "status provided (%i) is outside of "
                             "[200, 599] range", response->code);
                return NJS_ERROR;
            }
        }

        value = njs_vm_object_prop(vm, init, &status_text, &lvalue);
        if (value != NULL) {
            if (ngx_js_ngx_string(vm, value, &response->status_text) != NGX_OK) {
                njs_vm_error(vm, "invalid Response statusText");
                return NJS_ERROR;
            }

            p = response->status_text.data;
            end = p + response->status_text.len;

            while (p < end) {
                if (*p != '\t' && *p < ' ') {
                    njs_vm_error(vm, "invalid Response statusText");
                    return NJS_ERROR;
                }

                p++;
            }
        }

        value = njs_vm_object_prop(vm, init, &headers, &lvalue);
        if (value != NULL) {
            if (!njs_value_is_object(value)) {
                njs_vm_error(vm, "Headers is not an object");
                return NJS_ERROR;
            }

            ret = ngx_js_headers_fill(vm, &response->headers, value);
            if (ret != NJS_OK) {
                return NJS_ERROR;
            }
        }
    }

    NJS_CHB_MP_INIT(&response->chain, njs_vm_memory_pool(vm));

    body = njs_arg(args, nargs, 1);

    if (!njs_value_is_null_or_undefined(body)) {
        if (ngx_js_string(vm, body, &bd) != NGX_OK) {
            njs_vm_error(vm, "invalid Response body");
            return NJS_ERROR;
        }

        njs_chb_append(&response->chain, bd.start, bd.length);

        if (njs_value_is_string(body)) {
            ret = ngx_js_headers_append(vm, &response->headers,
                                    (u_char *) "Content-Type",
                                    njs_length("Content-Type"),
                                    (u_char *) "text/plain;charset=UTF-8",
                                    njs_length("text/plain;charset=UTF-8"));
            if (ret != NJS_OK) {
                return NJS_ERROR;
            }
        }
    }

    return njs_vm_external_create(vm, retval,
                                  ngx_http_js_fetch_response_proto_id, response,
                                  0);
}


static njs_int_t
ngx_js_method_process(njs_vm_t *vm, ngx_js_request_t *request)
{
    u_char           *s, *p;
    njs_str_t        str;
    const njs_str_t  *m;

    static const njs_str_t forbidden[] = {
        njs_str("CONNECT"),
        njs_str("TRACE"),
        njs_str("TRACK"),
        njs_null_str,
    };

    static const njs_str_t to_normalize[] = {
        njs_str("DELETE"),
        njs_str("GET"),
        njs_str("HEAD"),
        njs_str("OPTIONS"),
        njs_str("POST"),
        njs_str("PUT"),
        njs_null_str,
    };

    str.start = request->method.data;
    str.length = request->method.len;

    for (m = &forbidden[0]; m->length != 0; m++) {
        if (njs_strstr_case_eq(&str, m)) {
            njs_vm_error(vm, "forbidden method: %V", m);
            return NJS_ERROR;
        }
    }

    for (m = &to_normalize[0]; m->length != 0; m++) {
        if (njs_strstr_case_eq(&str, m)) {
            s = &request->m[0];
            p = m->start;

            while (*p != '\0') {
                *s++ = njs_upper_case(*p++);
            }

            request->method.data = &request->m[0];
            request->method.len = m->length;
            break;
        }
    }

    return NJS_OK;
}


static njs_int_t
ngx_js_headers_inherit(njs_vm_t *vm, ngx_js_headers_t *headers,
    ngx_js_headers_t *orig)
{
    njs_int_t         ret;
    ngx_uint_t        i;
    ngx_list_part_t  *part;
    ngx_js_tb_elt_t  *h;

    part = &orig->header_list.part;
    h = part->elts;

    for (i = 0; /* void */; i++) {

        if (i >= part->nelts) {
            if (part->next == NULL) {
                break;
            }

            part = part->next;
            h = part->elts;
            i = 0;
        }

        if (h[i].hash == 0) {
            continue;
        }

        ret = ngx_js_headers_append(vm, headers, h[i].key.data, h[i].key.len,
                                    h[i].value.data, h[i].value.len);
        if (ret != NJS_OK) {
            return NJS_ERROR;
        }
    }

    return NJS_OK;
}

static njs_int_t
ngx_js_headers_fill(njs_vm_t *vm, ngx_js_headers_t *headers, njs_value_t *init)
{
    int64_t              i, len, length;
    njs_int_t            ret;
    njs_str_t            name, header;
    njs_value_t         *keys, *value;
    ngx_js_headers_t    *hh;
    njs_opaque_value_t  *e, *start, lvalue;

    hh = njs_vm_external(vm, ngx_http_js_fetch_headers_proto_id, init);

    if (hh != NULL) {
        return ngx_js_headers_inherit(vm, headers, hh);
    }

    if (njs_value_is_array(init)) {
        start = (njs_opaque_value_t *) njs_vm_array_start(vm, init);
        if (start == NULL) {
            return NJS_ERROR;
        }

        (void) njs_vm_array_length(vm, init, &length);

        for (i = 0; i < length; i++) {
            e = (njs_opaque_value_t *) njs_vm_array_start(vm,
                                                          njs_value_arg(start));
            if (e == NULL) {
                return NJS_ERROR;
            }

            (void) njs_vm_array_length(vm, njs_value_arg(start), &len);

            start++;

            if (len != 2) {
                njs_vm_error(vm, "header does not contain exactly two items");
                return NJS_ERROR;
            }

            if (ngx_js_string(vm, njs_value_arg(&e[0]), &name) != NGX_OK) {
                return NJS_ERROR;
            }

            if (ngx_js_string(vm, njs_value_arg(&e[1]), &header) != NGX_OK) {
                return NJS_ERROR;
            }

            ret = ngx_js_headers_append(vm, headers, name.start, name.length,
                                        header.start, header.length);
            if (ret != NJS_OK) {
                return NJS_ERROR;
            }
        }

    } else {
        keys = njs_vm_object_keys(vm, init, njs_value_arg(&lvalue));
        if (keys == NULL) {
            return NJS_ERROR;
        }

        start = (njs_opaque_value_t *) njs_vm_array_start(vm, keys);
        if (start == NULL) {
            return NJS_ERROR;
        }

        (void) njs_vm_array_length(vm, keys, &length);

        for (i = 0; i < length; i++) {
            if (ngx_js_string(vm, njs_value_arg(start), &name) != NGX_OK) {
                return NJS_ERROR;
            }

            start++;

            value = njs_vm_object_prop(vm, init, &name, &lvalue);
            if (value == NULL) {
                return NJS_ERROR;
            }

            if (ngx_js_string(vm, value, &header) != NGX_OK) {
                return NJS_ERROR;
            }

            ret = ngx_js_headers_append(vm, headers, name.start, name.length,
                                        header.start, header.length);
            if (ret != NJS_OK) {
                return NJS_ERROR;
            }
        }
    }

    return NJS_OK;
}


static ngx_js_fetch_t *
ngx_js_fetch_alloc(njs_vm_t *vm, ngx_pool_t *pool, ngx_log_t *log)
{
    njs_int_t        ret;
    ngx_js_ctx_t    *ctx;
    ngx_js_http_t   *http;
    ngx_js_fetch_t  *fetch;
    ngx_js_event_t  *event;
    njs_function_t  *callback;

    fetch = ngx_pcalloc(pool, sizeof(ngx_js_fetch_t));
    if (fetch == NULL) {
        goto failed;
    }

    http = &fetch->http;

    http->pool = pool;
    http->log = log;

    http->timeout = 10000;

    http->http_parse.content_length_n = -1;

    http->append_headers = ngx_js_fetch_append_headers;
    http->ready_handler = ngx_js_fetch_process_done;
    http->error_handler = ngx_js_fetch_error;

    ret = njs_vm_promise_create(vm, njs_value_arg(&fetch->promise),
                                njs_value_arg(&fetch->promise_callbacks));
    if (ret != NJS_OK) {
        goto failed;
    }

    callback = njs_vm_function_alloc(vm, ngx_js_http_promise_trampoline, 0, 0);
    if (callback == NULL) {
        goto failed;
    }

    event = njs_mp_zalloc(njs_vm_memory_pool(vm), sizeof(ngx_js_event_t));
    if (njs_slow_path(event == NULL)) {
        goto failed;
    }

    ctx = ngx_external_ctx(vm, njs_vm_external_ptr(vm));

    event->ctx = vm;
    njs_value_function_set(njs_value_arg(&event->function), callback);
    event->destructor = ngx_js_fetch_destructor;
    event->fd = ctx->event_id++;
    event->data = fetch;

    ngx_js_add_event(ctx, event);

    fetch->vm = vm;
    fetch->event = event;

    ngx_log_debug1(NGX_LOG_DEBUG_EVENT, log, 0, "js http alloc:%p", fetch);

    return fetch;

failed:

    njs_vm_error(vm, "internal error");

    return NULL;
}


static void
ngx_js_fetch_error(ngx_js_http_t *http, const char *err)
{
    ngx_js_fetch_t  *fetch;

    fetch = (ngx_js_fetch_t *) http;

    njs_vm_error(fetch->vm, err);

    njs_vm_exception_get(fetch->vm, njs_value_arg(&fetch->response_value));

    ngx_js_fetch_done(fetch, &fetch->response_value, NJS_ERROR);
}


static void
ngx_js_fetch_destructor(ngx_js_event_t *event)
{
    ngx_js_http_t   *http;
    ngx_js_fetch_t  *fetch;

    fetch = event->data;
    http = &fetch->http;

    ngx_log_debug1(NGX_LOG_DEBUG_EVENT, http->log, 0, "js http destructor:%p",
                   fetch);

    ngx_js_http_resolve_done(http);
    ngx_js_http_close_peer(http);
}


static njs_int_t
ngx_js_fetch_promissified_result(njs_vm_t *vm, njs_value_t *result,
    njs_int_t rc, njs_value_t *retval)
{
    njs_int_t            ret;
    njs_function_t      *callback;
    njs_opaque_value_t   promise, arguments[2];

    ret = njs_vm_promise_create(vm, njs_value_arg(&promise),
                                njs_value_arg(&arguments));
    if (ret != NJS_OK) {
        goto error;
    }

    callback = njs_vm_function_alloc(vm, ngx_js_http_promise_trampoline, 0, 0);
    if (callback == NULL) {
        goto error;
    }

    njs_value_assign(&arguments[0], &arguments[(rc != NJS_OK)]);

    if (rc != NJS_OK) {
        njs_vm_exception_get(vm, njs_value_arg(&arguments[1]));

    } else {
        njs_value_assign(&arguments[1], result);
    }

    ret = njs_vm_enqueue_job(vm, callback, njs_value_arg(&arguments), 2);
    if (ret == NJS_ERROR) {
        goto error;
    }

    njs_value_assign(retval, &promise);

    return NJS_OK;

error:

    njs_vm_error(vm, "internal error");

    return NJS_ERROR;
}


static void
ngx_js_fetch_done(ngx_js_fetch_t *fetch, njs_opaque_value_t *retval,
    njs_int_t rc)
{
    njs_vm_t            *vm;
    ngx_js_ctx_t        *ctx;
    ngx_js_http_t       *http;
    ngx_js_event_t      *event;
    njs_opaque_value_t   arguments[2], *action;

    http = &fetch->http;

    ngx_log_debug2(NGX_LOG_DEBUG_EVENT, http->log, 0,
                   "js http done fetch:%p rc:%i", fetch, (ngx_int_t) rc);

    ngx_js_http_close_peer(http);

    if (fetch->event != NULL) {
        action = &fetch->promise_callbacks[(rc != NJS_OK)];
        njs_value_assign(&arguments[0], action);
        njs_value_assign(&arguments[1], retval);

        vm = fetch->vm;
        event = fetch->event;

        rc = ngx_js_call(vm, njs_value_function(njs_value_arg(&event->function)),
                         &arguments[0], 2);

        ctx = ngx_external_ctx(vm,  njs_vm_external_ptr(vm));
        ngx_js_del_event(ctx, event);

        ngx_external_event_finalize(vm)(njs_vm_external_ptr(vm), rc);
    }
}


static njs_int_t
ngx_js_http_promise_trampoline(njs_vm_t *vm, njs_value_t *args,
    njs_uint_t nargs, njs_index_t unused, njs_value_t *retval)
{
    njs_function_t  *callback;

    callback = njs_value_function(njs_argument(args, 1));

    if (callback != NULL) {
        return njs_vm_call(vm, callback, njs_argument(args, 2), 1);
    }

    return NJS_OK;
}


static njs_int_t
ngx_js_request_constructor(njs_vm_t *vm, ngx_js_request_t *request,
    ngx_url_t *u, njs_external_ptr_t external, njs_value_t *args,
    njs_uint_t nargs)
{
    njs_int_t            ret;
    ngx_uint_t           rc;
    ngx_pool_t          *pool;
    njs_value_t         *input, *init, *value, *headers;
    ngx_js_request_t    *orig;
    njs_opaque_value_t   lvalue;

    static const njs_str_t body_key = njs_str("body");
    static const njs_str_t cache_key = njs_str("cache");
    static const njs_str_t cred_key = njs_str("credentials");
    static const njs_str_t headers_key = njs_str("headers");
    static const njs_str_t mode_key = njs_str("mode");
    static const njs_str_t method_key = njs_str("method");

    input = njs_arg(args, nargs, 1);
    if (njs_value_is_undefined(input)) {
        njs_vm_error(vm, "1st argument is required");
        return NJS_ERROR;
    }

    /*
     * set by ngx_memzero():
     *
     *  request->url.length = 0;
     *  request->body.length = 0;
     *  request->cache_mode = CACHE_MODE_DEFAULT;
     *  request->credentials = CREDENTIALS_SAME_ORIGIN;
     *  request->mode = MODE_NO_CORS;
     *  request->headers.content_type = NULL;
     */

    ngx_memzero(request, sizeof(ngx_js_request_t));

    request->method.data = (u_char *) "GET";
    request->method.len = 3;
    request->body.data = (u_char *) "";
    request->body.len = 0;
    request->headers.guard = GUARD_REQUEST;

    pool = ngx_external_pool(vm, external);

    rc = ngx_list_init(&request->headers.header_list, pool, 4,
                       sizeof(ngx_js_tb_elt_t));
    if (rc != NGX_OK) {
        njs_vm_memory_error(vm);
        return NJS_ERROR;
    }

    if (njs_value_is_string(input)) {
        ret = ngx_js_ngx_string(vm, input, &request->url);
        if (ret != NJS_OK) {
            njs_vm_error(vm, "failed to convert url arg");
            return NJS_ERROR;
        }

    } else {
        orig = njs_vm_external(vm, ngx_http_js_fetch_request_proto_id, input);
        if (orig == NULL) {
            njs_vm_error(vm, "input is not string or a Request object");
            return NJS_ERROR;
        }

        request->url = orig->url;
        request->method = orig->method;
        request->body = orig->body;
        request->body_used = orig->body_used;
        request->cache_mode = orig->cache_mode;
        request->credentials = orig->credentials;
        request->mode = orig->mode;

        ret = ngx_js_headers_inherit(vm, &request->headers, &orig->headers);
        if (ret != NJS_OK) {
            return NJS_ERROR;
        }
    }

    ngx_js_http_trim(&request->url.data, &request->url.len, 1);

    ngx_memzero(u, sizeof(ngx_url_t));

    u->url.len = request->url.len;
    u->url.data = request->url.data;
    u->default_port = 80;
    u->uri_part = 1;
    u->no_resolve = 1;

    if (u->url.len > 7
        && njs_strncasecmp(u->url.data, (u_char *) "http://", 7) == 0)
    {
        u->url.len -= 7;
        u->url.data += 7;

#if (NGX_SSL)
    } else if (u->url.len > 8
        && njs_strncasecmp(u->url.data, (u_char *) "https://", 8) == 0)
    {
        u->url.len -= 8;
        u->url.data += 8;
        u->default_port = 443;
#endif

    } else {
        njs_vm_error(vm, "unsupported URL schema (only http or https are"
                     " supported)");
        return NJS_ERROR;
    }

    if (ngx_parse_url(pool, u) != NGX_OK) {
        njs_vm_error(vm, "invalid url");
        return NJS_ERROR;
    }

    init = njs_arg(args, nargs, 2);

    if (njs_value_is_object(init)) {
        value = njs_vm_object_prop(vm, init, &method_key, &lvalue);
        if (value != NULL && ngx_js_ngx_string(vm, value, &request->method)
            != NGX_OK)
        {
            njs_vm_error(vm, "invalid Request method");
            return NJS_ERROR;
        }

        ret = ngx_js_method_process(vm, request);
        if (ret != NJS_OK) {
            return NJS_ERROR;
        }

        value = njs_vm_object_prop(vm, init, &cache_key, &lvalue);
        if (value != NULL) {
            ret = ngx_fetch_flag_set(vm, ngx_js_fetch_cache_modes, value,
                                     "cache");
            if (ret == NJS_ERROR) {
                return NJS_ERROR;
            }

            request->cache_mode = ret;
        }

        value = njs_vm_object_prop(vm, init, &cred_key, &lvalue);
        if (value != NULL) {
            ret = ngx_fetch_flag_set(vm, ngx_js_fetch_credentials, value,
                                     "credentials");
            if (ret == NJS_ERROR) {
                return NJS_ERROR;
            }

            request->credentials = ret;
        }

        value = njs_vm_object_prop(vm, init, &mode_key, &lvalue);
        if (value != NULL) {
            ret = ngx_fetch_flag_set(vm, ngx_js_fetch_modes, value, "mode");
            if (ret == NJS_ERROR) {
                return NJS_ERROR;
            }

            request->mode = ret;
        }

        headers = njs_vm_object_prop(vm, init, &headers_key, &lvalue);
        if (headers != NULL) {
            if (!njs_value_is_object(headers)) {
                njs_vm_error(vm, "Headers is not an object");
                return NJS_ERROR;
            }

            /*
             * There are no API to reset or destroy ngx_list,
             * just allocating a new one.
             */

            ngx_memset(&request->headers, 0, sizeof(ngx_js_headers_t));
            request->headers.guard = GUARD_REQUEST;

            rc = ngx_list_init(&request->headers.header_list, pool, 4,
                               sizeof(ngx_js_tb_elt_t));
            if (rc != NGX_OK) {
                njs_vm_memory_error(vm);
                return NJS_ERROR;
            }

            ret = ngx_js_headers_fill(vm, &request->headers, headers);
            if (ret != NJS_OK) {
                return NJS_ERROR;
            }
        }

        value = njs_vm_object_prop(vm, init, &body_key, &lvalue);
        if (value != NULL) {
            if (ngx_js_ngx_string(vm, value, &request->body) != NGX_OK) {
                njs_vm_error(vm, "invalid Request body");
                return NJS_ERROR;
            }

            if (request->headers.content_type == NULL
                && njs_value_is_string(value))
            {
                ret = ngx_js_headers_append(vm, &request->headers,
                                        (u_char *) "Content-Type",
                                        njs_length("Content-Type"),
                                        (u_char *) "text/plain;charset=UTF-8",
                                        njs_length("text/plain;charset=UTF-8"));
                if (ret != NJS_OK) {
                    return NJS_ERROR;
                }
            }
        }
    }

    return NJS_OK;
}


static ngx_int_t
ngx_js_fetch_append_headers(ngx_js_http_t *http, ngx_js_headers_t *headers,
    u_char *name, size_t len, u_char *value, size_t vlen)
{
    ngx_js_fetch_t  *fetch;

    fetch = (ngx_js_fetch_t *) http;

    return ngx_js_headers_append(fetch->vm, headers, name, len, value, vlen);
}


static void
ngx_js_fetch_process_done(ngx_js_http_t *http)
{
    njs_int_t       ret;
    ngx_js_fetch_t  *fetch;

    fetch = (ngx_js_fetch_t *) http;

    ret = njs_vm_external_create(fetch->vm,
                                 njs_value_arg(&fetch->response_value),
                                 ngx_http_js_fetch_response_proto_id,
                                 &fetch->http.response, 0);
    if (ret != NJS_OK) {
        ngx_js_fetch_error(http, "fetch response creation failed");
        return;
    }

    ngx_js_fetch_done(fetch, &fetch->response_value, NJS_OK);
}


static njs_int_t
ngx_js_headers_append(njs_vm_t *vm, ngx_js_headers_t *headers,
    u_char *name, size_t len, u_char *value, size_t vlen)
{
    u_char           *p, *end;
    ngx_int_t         ret;
    ngx_uint_t        i;
    ngx_js_tb_elt_t  *h, **ph;
    ngx_list_part_t  *part;

    ngx_js_http_trim(&value, &vlen, 0);

    ret = ngx_js_check_header_name(name, len);
    if (ret != NGX_OK) {
        njs_vm_error(vm, "invalid header name");
        return NJS_ERROR;
    }

    p = value;
    end = p + vlen;

    while (p < end) {
        if (*p == '\0') {
            njs_vm_error(vm, "invalid header value");
            return NJS_ERROR;
        }

        p++;
    }

    if (headers->guard == GUARD_IMMUTABLE) {
        njs_vm_error(vm, "cannot append to immutable object");
        return NJS_ERROR;
    }

    ph = NULL;
    part = &headers->header_list.part;
    h = part->elts;

    for (i = 0; /* void */; i++) {

        if (i >= part->nelts) {
            if (part->next == NULL) {
                break;
            }

            part = part->next;
            h = part->elts;
            i = 0;
        }

        if (h[i].hash == 0) {
            continue;
        }

        if (len == h[i].key.len
            && (njs_strncasecmp(name, h[i].key.data, len) == 0))
        {
            ph = &h[i].next;
            while (*ph) { ph = &(*ph)->next; }
            break;
        }
    }

    h = ngx_list_push(&headers->header_list);
    if (h == NULL) {
        njs_vm_memory_error(vm);
        return NJS_ERROR;
    }

    if (ph != NULL) {
        *ph = h;
    }

    h->hash = 1;
    h->key.data = name;
    h->key.len = len;
    h->value.data = value;
    h->value.len = vlen;
    h->next = NULL;

    if (len == njs_strlen("Content-Type")
        && ngx_strncasecmp(name, (u_char *) "Content-Type", len) == 0)
    {
        headers->content_type = h;
    }

    return NJS_OK;
}


static njs_int_t
ngx_headers_js_get(njs_vm_t *vm, njs_value_t *value, njs_str_t *name,
    njs_value_t *retval, njs_bool_t as_array)
{
    njs_int_t          rc;
    njs_chb_t          chain;
    ngx_uint_t         i;
    ngx_js_tb_elt_t   *h, *ph;
    ngx_list_part_t   *part;
    ngx_js_headers_t  *headers;

    headers = njs_vm_external(vm, ngx_http_js_fetch_headers_proto_id, value);
    if (headers == NULL) {
        njs_value_null_set(retval);
        return NJS_DECLINED;
    }

    if (as_array) {
        rc = njs_vm_array_alloc(vm, retval, 2);
        if (rc != NJS_OK) {
            return NJS_ERROR;
        }
    }

    part = &headers->header_list.part;
    h = part->elts;
    ph = NULL;

    for (i = 0; /* void */; i++) {

        if (i >= part->nelts) {
            if (part->next == NULL) {
                break;
            }

            part = part->next;
            h = part->elts;
            i = 0;
        }

        if (h[i].hash == 0) {
            continue;
        }

        if (h[i].key.len == name->length
            && njs_strncasecmp(h[i].key.data, name->start, name->length) == 0)
        {
            ph = &h[i];
            break;
        }
    }

    if (as_array) {
        while (ph != NULL) {
            value = njs_vm_array_push(vm, retval);
            if (value == NULL) {
                return NJS_ERROR;
            }

            rc = njs_vm_value_string_create(vm, value, ph->value.data,
                                            ph->value.len);
            if (rc != NJS_OK) {
                return NJS_ERROR;
            }

            ph = ph->next;
        }

        return NJS_OK;
    }

    if (ph == NULL) {
        njs_value_null_set(retval);
        return NJS_DECLINED;
    }

    NJS_CHB_MP_INIT(&chain, njs_vm_memory_pool(vm));

    h = ph;

    for ( ;; ) {
        njs_chb_append(&chain, h->value.data, h->value.len);

        if (h->next == NULL) {
            break;
        }

        njs_chb_append_literal(&chain, ", ");
        h = h->next;
    }

    rc = njs_vm_value_string_create_chb(vm, retval, &chain);

    njs_chb_destroy(&chain);

    return rc;
}


static njs_int_t
ngx_headers_js_ext_append(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t unused, njs_value_t *retval)
{
    njs_int_t          ret;
    njs_str_t          name, value;
    ngx_js_headers_t  *headers;

    headers = njs_vm_external(vm, ngx_http_js_fetch_headers_proto_id,
                              njs_argument(args, 0));
    if (headers == NULL) {
        njs_vm_error(vm, "\"this\" is not fetch headers object");
        return NJS_ERROR;
    }

    ret = ngx_js_string(vm, njs_arg(args, nargs, 1), &name);
    if (ret != NJS_OK) {
        return NJS_ERROR;
    }

    ret = ngx_js_string(vm, njs_arg(args, nargs, 2), &value);
    if (ret != NJS_OK) {
        return NJS_ERROR;
    }

    ret = ngx_js_headers_append(vm, headers, name.start, name.length,
                                value.start, value.length);
    if (ret != NJS_OK) {
        return NJS_ERROR;
    }

    njs_value_undefined_set(retval);

    return NJS_OK;
}


static njs_int_t
ngx_headers_js_ext_delete(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t unused, njs_value_t *retval)
{
    njs_int_t          ret;
    njs_str_t          name;
    ngx_uint_t         i;
    ngx_list_part_t   *part;
    ngx_js_tb_elt_t   *h;
    ngx_js_headers_t  *headers;

    headers = njs_vm_external(vm, ngx_http_js_fetch_headers_proto_id,
                              njs_argument(args, 0));
    if (headers == NULL) {
        njs_vm_error(vm, "\"this\" is not fetch headers object");
        return NJS_ERROR;
    }

    ret = ngx_js_string(vm, njs_arg(args, nargs, 1), &name);
    if (ret != NJS_OK) {
        return NJS_ERROR;
    }

    part = &headers->header_list.part;
    h = part->elts;

    for (i = 0; /* void */; i++) {

        if (i >= part->nelts) {
            if (part->next == NULL) {
                break;
            }

            part = part->next;
            h = part->elts;
            i = 0;
        }

        if (h[i].hash == 0) {
            continue;
        }

        if (name.length == h[i].key.len
            && (njs_strncasecmp(name.start, h[i].key.data, name.length) == 0))
        {
            h[i].hash = 0;
        }
    }

    if (name.length == njs_strlen("Content-Type")
        && ngx_strncasecmp(name.start, (u_char *) "Content-Type", name.length)
           == 0)
    {
        headers->content_type = NULL;
    }

    njs_value_undefined_set(retval);

    return NJS_OK;
}


static njs_int_t
ngx_headers_js_ext_for_each(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t unused, njs_value_t *retval)
{
    int64_t              length;
    njs_int_t            ret;
    njs_str_t            name;
    njs_value_t         *this, *callback;
    ngx_js_headers_t    *headers;
    njs_opaque_value_t  *k, *end, keys, arguments[2];

    this = njs_argument(args, 0);

    headers = njs_vm_external(vm, ngx_http_js_fetch_headers_proto_id, this);
    if (headers == NULL) {
        njs_vm_error(vm, "\"this\" is not fetch headers object");
        return NJS_ERROR;
    }

    callback = njs_arg(args, nargs, 1);

    if (!njs_value_is_function(callback)) {
        njs_vm_error(vm, "\"callback\" is not a function");
        return NJS_ERROR;
    }

    ret = ngx_headers_js_ext_keys(vm, this, njs_value_arg(&keys));
    if (ret != NJS_OK) {
        return NJS_ERROR;
    }

    (void) njs_vm_array_length(vm, njs_value_arg(&keys), &length);

    k = (njs_opaque_value_t *) njs_vm_array_start(vm, njs_value_arg(&keys));
    end = k + length;

    for (; k < end; k++) {
        ret = ngx_js_string(vm, njs_value_arg(k), &name);
        if (ret != NJS_OK) {
            return NJS_ERROR;
        }

        ret = ngx_headers_js_get(vm, this, &name, njs_value_arg(&arguments[1]),
                                 0);
        if (ret != NJS_OK) {
            return NJS_ERROR;
        }

        njs_value_assign(&arguments[0], k);

        ret = njs_vm_call(vm, njs_value_function(callback),
                          njs_value_arg(&arguments[0]), 2);
        if (ret != NJS_OK) {
            return NJS_ERROR;
        }
    }

    return NJS_OK;
}


static njs_int_t
ngx_headers_js_ext_get(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t as_array, njs_value_t *retval)
{
    njs_int_t  ret;
    njs_str_t  name;

    ret = ngx_js_string(vm, njs_arg(args, nargs, 1), &name);
    if (ret != NJS_OK) {
        return NJS_ERROR;
    }

    ret = ngx_headers_js_get(vm, njs_argument(args, 0), &name,
                             retval, as_array);

    return (ret != NJS_ERROR) ? NJS_OK : NJS_ERROR;
}


static njs_int_t
ngx_headers_js_ext_has(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t unused, njs_value_t *retval)
{
    njs_int_t  ret;
    njs_str_t  name;

    ret = ngx_js_string(vm, njs_arg(args, nargs, 1), &name);
    if (ret != NJS_OK) {
        return NJS_ERROR;
    }

    ret = ngx_headers_js_get(vm, njs_argument(args, 0), &name,
                             retval, 0);
    if (ret == NJS_ERROR) {
        return NJS_ERROR;
    }

    njs_value_boolean_set(retval, ret == NJS_OK);

    return NJS_OK;
}


static njs_int_t
ngx_headers_js_ext_prop(njs_vm_t *vm, njs_object_prop_t *prop, uint32_t atom_id,
    njs_value_t *value, njs_value_t *setval, njs_value_t *retval)
{
    njs_int_t  ret;
    njs_str_t  name;

    ret = njs_vm_prop_name(vm, atom_id, &name);
    if (ret != NJS_OK) {
        return NJS_ERROR;
    }

    return ngx_headers_js_get(vm, value, &name, retval, 0);
}


static ngx_int_t
ngx_string_compare(const void *s1, const void *s2, void *ctx)
{
    return njs_vm_string_compare(ctx, s1, s2);
}


static void
ngx_js_sort(void *base, size_t n, size_t size,
    ngx_int_t (*cmp)(const void *, const void *, void *), void *ctx)
{
    u_char  *p1, *p2, *p;

    p = ngx_alloc(size, ngx_cycle->log);
    if (p == NULL) {
        return;
    }

    for (p1 = (u_char *) base + size;
         p1 < (u_char *) base + n * size;
         p1 += size)
    {
        ngx_memcpy(p, p1, size);

        for (p2 = p1;
             p2 > (u_char *) base && cmp(p2 - size, p, ctx) > 0;
             p2 -= size)
        {
            ngx_memcpy(p2, p2 - size, size);
        }

        ngx_memcpy(p2, p, size);
    }

    ngx_free(p);
}


static njs_int_t
ngx_headers_js_ext_keys(njs_vm_t *vm, njs_value_t *value, njs_value_t *keys)
{
    njs_int_t          rc;
    njs_str_t          hdr;
    ngx_uint_t         i, k, length;
    njs_value_t       *start;
    ngx_list_part_t   *part;
    ngx_js_tb_elt_t   *h;
    ngx_js_headers_t  *headers;

    headers = njs_vm_external(vm, ngx_http_js_fetch_headers_proto_id, value);
    if (headers == NULL) {
        njs_value_null_set(keys);
        return NJS_DECLINED;
    }

    rc = njs_vm_array_alloc(vm, keys, 8);
    if (rc != NJS_OK) {
        return NJS_ERROR;
    }

    length = 0;

    part = &headers->header_list.part;
    h = part->elts;

    for (i = 0; /* void */; i++) {

        if (i >= part->nelts) {
            if (part->next == NULL) {
                break;
            }

            part = part->next;
            h = part->elts;
            i = 0;
        }

        if (h[i].hash == 0) {
            continue;
        }

        start = njs_vm_array_start(vm, keys);

        for (k = 0; k < length; k++) {
            njs_value_string_get(vm, njs_argument(start, k), &hdr);

            if (h[i].key.len == hdr.length
                && njs_strncasecmp(h[i].key.data, hdr.start, hdr.length) == 0)
            {
                break;
            }
        }

        if (k == length) {
            value = njs_vm_array_push(vm, keys);
            if (value == NULL) {
                return NJS_ERROR;
            }

            rc = njs_vm_value_string_create(vm, value, h[i].key.data,
                                            h[i].key.len);
            if (rc != NJS_OK) {
                return NJS_ERROR;
            }

            length++;
        }
    }

    start = njs_vm_array_start(vm, keys);

    ngx_js_sort(start, (size_t) length, sizeof(njs_opaque_value_t),
                ngx_string_compare, vm);

    return NJS_OK;
}


static njs_int_t
ngx_headers_js_ext_set(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t unused, njs_value_t *retval)
{
    njs_int_t          ret;
    njs_str_t          name, value;
    ngx_uint_t         i;
    ngx_list_part_t   *part;
    ngx_js_tb_elt_t   *h, **ph, **pp;
    ngx_js_headers_t  *headers;

    headers = njs_vm_external(vm, ngx_http_js_fetch_headers_proto_id,
                              njs_argument(args, 0));
    if (headers == NULL) {
        njs_vm_error(vm, "\"this\" is not fetch headers object");
        return NJS_ERROR;
    }

    ret = ngx_js_string(vm, njs_arg(args, nargs, 1), &name);
    if (ret != NJS_OK) {
        return NJS_ERROR;
    }

    ret = ngx_js_string(vm, njs_arg(args, nargs, 2), &value);
    if (ret != NJS_OK) {
        return NJS_ERROR;
    }

    part = &headers->header_list.part;
    h = part->elts;

    for (i = 0; /* void */; i++) {

        if (i >= part->nelts) {
            if (part->next == NULL) {
                break;
            }

            part = part->next;
            h = part->elts;
            i = 0;
        }

        if (h[i].hash == 0) {
            continue;
        }

        if (name.length == h[i].key.len
            && (njs_strncasecmp(name.start, h[i].key.data, name.length) == 0))
        {
            h[i].value.len = value.length;
            h[i].value.data = value.start;

            ph = &h[i].next;

            while (*ph) {
                pp = ph;
                ph = &(*ph)->next;
                *pp = NULL;
            }

            goto done;
        }
    }

    ret = ngx_js_headers_append(vm, headers, name.start, name.length,
                                value.start, value.length);
    if (ret != NJS_OK) {
        return NJS_ERROR;
    }

done:

    njs_value_undefined_set(retval);

    return NJS_OK;
}


static njs_int_t
ngx_request_js_ext_body(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t type, njs_value_t *retval)
{
    njs_int_t            ret;
    ngx_js_request_t    *request;
    njs_opaque_value_t   result;

    request = njs_vm_external(vm, ngx_http_js_fetch_request_proto_id,
                              njs_argument(args, 0));
    if (request == NULL) {
        njs_value_undefined_set(retval);
        return NJS_DECLINED;
    }

    if (request->body_used) {
        njs_vm_error(vm, "body stream already read");
        return NJS_ERROR;
    }

    request->body_used = 1;

    switch (type) {
    case NGX_JS_BODY_ARRAY_BUFFER:
        ret = njs_vm_value_array_buffer_set(vm, njs_value_arg(&result),
                                            request->body.data,
                                            request->body.len);
        if (ret != NJS_OK) {
            njs_vm_memory_error(vm);
            return NJS_ERROR;
        }

        break;

    case NGX_JS_BODY_JSON:
    case NGX_JS_BODY_TEXT:
    default:
        ret = njs_vm_value_string_create(vm, njs_value_arg(&result),
                                         request->body.data,
                                         request->body.len);
        if (ret != NJS_OK) {
            njs_vm_memory_error(vm);
            return NJS_ERROR;
        }

        if (type == NGX_JS_BODY_JSON) {
            ret = njs_vm_json_parse(vm, njs_value_arg(&result), 1,
                                    njs_value_arg(&result));
        }
    }

    return ngx_js_fetch_promissified_result(vm, njs_value_arg(&result), ret,
                                            retval);
}


static njs_int_t
ngx_request_js_ext_body_used(njs_vm_t *vm, njs_object_prop_t *prop,
    uint32_t unused, njs_value_t *value, njs_value_t *setval,
    njs_value_t *retval)
{
    ngx_js_request_t  *request;

    request = njs_vm_external(vm, ngx_http_js_fetch_request_proto_id, value);
    if (request == NULL) {
        njs_value_undefined_set(retval);
        return NJS_DECLINED;
    }

    njs_value_boolean_set(retval, request->body_used);

    return NJS_OK;
}


static njs_int_t
ngx_request_js_ext_cache(njs_vm_t *vm, njs_object_prop_t *prop, uint32_t unused,
    njs_value_t *value, njs_value_t *setval, njs_value_t *retval)
{
    ngx_js_request_t  *request;

    request = njs_vm_external(vm, ngx_http_js_fetch_request_proto_id, value);
    if (request == NULL) {
        njs_value_undefined_set(retval);
        return NJS_DECLINED;
    }

    return ngx_fetch_flag(vm, ngx_js_fetch_cache_modes,
                          (njs_int_t) request->cache_mode, retval);
}


static njs_int_t
ngx_request_js_ext_credentials(njs_vm_t *vm, njs_object_prop_t *prop,
    uint32_t unused, njs_value_t *value, njs_value_t *setval,
    njs_value_t *retval)
{
    ngx_js_request_t  *request;

    request = njs_vm_external(vm, ngx_http_js_fetch_request_proto_id, value);
    if (request == NULL) {
        njs_value_undefined_set(retval);
        return NJS_DECLINED;
    }

    return ngx_fetch_flag(vm, ngx_js_fetch_credentials,
                          (njs_int_t) request->credentials, retval);
}


static njs_int_t
ngx_request_js_ext_headers(njs_vm_t *vm, njs_object_prop_t *prop,
    uint32_t unused, njs_value_t *value, njs_value_t *setval,
    njs_value_t *retval)
{
    njs_int_t          ret;
    ngx_js_request_t  *request;

    request = njs_vm_external(vm, ngx_http_js_fetch_request_proto_id, value);
    if (request == NULL) {
        njs_value_undefined_set(retval);
        return NJS_DECLINED;
    }

    if (njs_value_is_null(njs_value_arg(&request->header_value))) {
        ret = njs_vm_external_create(vm, njs_value_arg(&request->header_value),
                                     ngx_http_js_fetch_headers_proto_id,
                                     &request->headers, 0);
        if (ret != NJS_OK) {
            njs_vm_error(vm, "fetch header creation failed");
            return NJS_ERROR;
        }
    }

    njs_value_assign(retval, &request->header_value);

    return NJS_OK;
}


static njs_int_t
ngx_request_js_ext_mode(njs_vm_t *vm, njs_object_prop_t *prop, uint32_t unused,
    njs_value_t *value, njs_value_t *setval, njs_value_t *retval)
{
    ngx_js_request_t  *request;

    request = njs_vm_external(vm, ngx_http_js_fetch_request_proto_id, value);
    if (request == NULL) {
        njs_value_undefined_set(retval);
        return NJS_DECLINED;
    }

    return ngx_fetch_flag(vm, ngx_js_fetch_modes, (njs_int_t) request->mode,
                          retval);
}


static njs_int_t
ngx_response_js_ext_body(njs_vm_t *vm, njs_value_t *args,
     njs_uint_t nargs, njs_index_t type, njs_value_t *retval)
{
    njs_int_t            ret;
    njs_str_t            string;
    ngx_js_response_t   *response;
    njs_opaque_value_t   result;

    response = njs_vm_external(vm, ngx_http_js_fetch_response_proto_id,
                               njs_argument(args, 0));
    if (response == NULL) {
        njs_value_undefined_set(retval);
        return NJS_DECLINED;
    }

    if (response->body_used) {
        njs_vm_error(vm, "body stream already read");
        return NJS_ERROR;
    }

    response->body_used = 1;

    ret = njs_chb_join(&response->chain, &string);
    if (ret != NJS_OK) {
        njs_vm_memory_error(vm);
        return NJS_ERROR;
    }

    switch (type) {
    case NGX_JS_BODY_ARRAY_BUFFER:
        ret = njs_vm_value_array_buffer_set(vm, njs_value_arg(&result),
                                            string.start, string.length);
        if (ret != NJS_OK) {
            njs_vm_memory_error(vm);
            return NJS_ERROR;
        }

        break;

    case NGX_JS_BODY_JSON:
    case NGX_JS_BODY_TEXT:
    default:
        ret = njs_vm_value_string_create(vm, njs_value_arg(&result),
                                         string.start, string.length);
        if (ret != NJS_OK) {
            njs_vm_memory_error(vm);
            return NJS_ERROR;
        }

        if (type == NGX_JS_BODY_JSON) {
            ret = njs_vm_json_parse(vm, njs_value_arg(&result), 1, retval);
            njs_value_assign(&result, retval);
        }
    }

    return ngx_js_fetch_promissified_result(vm, njs_value_arg(&result), ret,
                                            retval);
}


static njs_int_t
ngx_response_js_ext_body_used(njs_vm_t *vm, njs_object_prop_t *prop,
    uint32_t unused, njs_value_t *value, njs_value_t *setval,
    njs_value_t *retval)
{
    ngx_js_response_t  *response;

    response = njs_vm_external(vm, ngx_http_js_fetch_response_proto_id, value);
    if (response == NULL) {
        njs_value_undefined_set(retval);
        return NJS_DECLINED;
    }

    njs_value_boolean_set(retval, response->body_used);

    return NJS_OK;
}


static njs_int_t
ngx_response_js_ext_headers(njs_vm_t *vm, njs_object_prop_t *prop,
    uint32_t unused, njs_value_t *value, njs_value_t *setval,
    njs_value_t *retval)
{
    njs_int_t           ret;
    ngx_js_response_t  *response;

    response = njs_vm_external(vm, ngx_http_js_fetch_response_proto_id, value);
    if (response == NULL) {
        njs_value_undefined_set(retval);
        return NJS_DECLINED;
    }

    if (njs_value_is_null(njs_value_arg(&response->header_value))) {
        ret = njs_vm_external_create(vm, njs_value_arg(&response->header_value),
                                     ngx_http_js_fetch_headers_proto_id,
                                     &response->headers, 0);
        if (ret != NJS_OK) {
            njs_vm_error(vm, "fetch header creation failed");
            return NJS_ERROR;
        }
    }

    njs_value_assign(retval, &response->header_value);

    return NJS_OK;
}


static njs_int_t
ngx_response_js_ext_ok(njs_vm_t *vm, njs_object_prop_t *prop, uint32_t unused,
    njs_value_t *value, njs_value_t *setval, njs_value_t *retval)
{
    ngx_uint_t          code;
    ngx_js_response_t  *response;

    response = njs_vm_external(vm, ngx_http_js_fetch_response_proto_id, value);
    if (response == NULL) {
        njs_value_undefined_set(retval);
        return NJS_DECLINED;
    }

    code = response->code;

    njs_value_boolean_set(retval, code >= 200 && code < 300);

    return NJS_OK;
}


static njs_int_t
ngx_response_js_ext_status(njs_vm_t *vm, njs_object_prop_t *prop,
    uint32_t unused, njs_value_t *value, njs_value_t *setval,
    njs_value_t *retval)
{
    ngx_js_response_t  *response;

    response = njs_vm_external(vm, ngx_http_js_fetch_response_proto_id, value);
    if (response == NULL) {
        njs_value_undefined_set(retval);
        return NJS_DECLINED;
    }

    njs_value_number_set(retval, response->code);

    return NJS_OK;
}


static njs_int_t
ngx_response_js_ext_status_text(njs_vm_t *vm, njs_object_prop_t *prop,
    uint32_t unused, njs_value_t *value, njs_value_t *setval,
    njs_value_t *retval)
{
    ngx_js_response_t  *response;

    response = njs_vm_external(vm, ngx_http_js_fetch_response_proto_id, value);
    if (response == NULL) {
        njs_value_undefined_set(retval);
        return NJS_DECLINED;
    }

    njs_vm_value_string_create(vm, retval, response->status_text.data,
                               response->status_text.len);

    return NJS_OK;
}


static njs_int_t
ngx_response_js_ext_type(njs_vm_t *vm, njs_object_prop_t *prop, uint32_t unused,
    njs_value_t *value, njs_value_t *setval, njs_value_t *retval)
{
    ngx_js_response_t  *response;

    response = njs_vm_external(vm, ngx_http_js_fetch_response_proto_id, value);
    if (response == NULL) {
        njs_value_undefined_set(retval);
        return NJS_DECLINED;
    }

    return njs_vm_value_string_create(vm, retval, (u_char *) "basic",
                                      njs_length("basic"));
}


static njs_int_t
ngx_fetch_flag(njs_vm_t *vm, const ngx_js_entry_t *entries, njs_int_t value,
    njs_value_t *retval)
{
    const ngx_js_entry_t  *e;

    for (e = entries; e->name.length != 0; e++) {
        if (e->value == value) {
            return njs_vm_value_string_create(vm, retval, e->name.start,
                                              e->name.length);
        }
    }

    return NJS_ERROR;
}


static njs_int_t
ngx_fetch_flag_set(njs_vm_t *vm, const ngx_js_entry_t *entries,
     njs_value_t *value, const char *type)
{
    njs_int_t              ret;
    njs_str_t              flag;
    const ngx_js_entry_t  *e;

    ret = ngx_js_string(vm, value, &flag);
    if (ret != NJS_OK) {
        return NJS_ERROR;
    }

    for (e = entries; e->name.length != 0; e++) {
        if (njs_strstr_case_eq(&flag, &e->name)) {
            return e->value;
        }
    }

    njs_vm_error(vm, "unknown %s type: %V", type, &flag);

    return NJS_ERROR;
}


static njs_int_t
ngx_js_fetch_function_bind(njs_vm_t *vm, const njs_str_t *name,
    njs_function_native_t native, njs_bool_t ctor)
{
    njs_function_t      *f;
    njs_opaque_value_t   value;

    f = njs_vm_function_alloc(vm, native, 1, ctor);
    if (f == NULL) {
        return NJS_ERROR;
    }

    njs_value_function_set(njs_value_arg(&value), f);

    return njs_vm_bind(vm, name, njs_value_arg(&value), 1);
}


static njs_int_t
ngx_js_fetch_init(njs_vm_t *vm)
{
    njs_int_t  ret;

    static const njs_str_t  headers = njs_str("Headers");
    static const njs_str_t  request = njs_str("Request");
    static const njs_str_t  response = njs_str("Response");

    ngx_http_js_fetch_headers_proto_id = njs_vm_external_prototype(vm,
                                          ngx_js_ext_http_headers,
                                          njs_nitems(ngx_js_ext_http_headers));
    if (ngx_http_js_fetch_headers_proto_id < 0) {
        return NJS_ERROR;
    }

    ngx_http_js_fetch_request_proto_id = njs_vm_external_prototype(vm,
                                          ngx_js_ext_http_request,
                                          njs_nitems(ngx_js_ext_http_request));
    if (ngx_http_js_fetch_request_proto_id < 0) {
        return NJS_ERROR;
    }

    ngx_http_js_fetch_response_proto_id = njs_vm_external_prototype(vm,
                                          ngx_js_ext_http_response,
                                          njs_nitems(ngx_js_ext_http_response));
    if (ngx_http_js_fetch_response_proto_id < 0) {
        return NJS_ERROR;
    }

    ret = ngx_js_fetch_function_bind(vm, &headers,
                                     ngx_js_ext_headers_constructor, 1);
    if (ret != NJS_OK) {
        return NJS_ERROR;
    }

    ret = ngx_js_fetch_function_bind(vm, &request,
                                     ngx_js_ext_request_constructor, 1);
    if (ret != NJS_OK) {
        return NJS_ERROR;
    }

    ret = ngx_js_fetch_function_bind(vm, &response,
                                     ngx_js_ext_response_constructor, 1);
    if (ret != NJS_OK) {
        return NJS_ERROR;
    }

    return NJS_OK;
}
