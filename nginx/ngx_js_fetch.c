
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
    njs_str_t       name;
    njs_int_t       value;
} ngx_js_entry_t;


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


typedef struct ngx_js_tb_elt_s  ngx_js_tb_elt_t;

struct ngx_js_tb_elt_s {
    ngx_uint_t        hash;
    ngx_str_t         key;
    ngx_str_t         value;
    ngx_js_tb_elt_t  *next;
};


typedef struct {
    enum {
        GUARD_NONE = 0,
        GUARD_REQUEST,
        GUARD_IMMUTABLE,
        GUARD_RESPONSE,
    }                              guard;
    ngx_list_t                     header_list;
    ngx_js_tb_elt_t               *content_type;
} ngx_js_headers_t;


typedef struct {
    enum {
        CACHE_MODE_DEFAULT = 0,
        CACHE_MODE_NO_STORE,
        CACHE_MODE_RELOAD,
        CACHE_MODE_NO_CACHE,
        CACHE_MODE_FORCE_CACHE,
        CACHE_MODE_ONLY_IF_CACHED,
    }                              cache_mode;
    enum {
        CREDENTIALS_SAME_ORIGIN = 0,
        CREDENTIALS_INCLUDE,
        CREDENTIALS_OMIT,
    }                              credentials;
    enum {
        MODE_NO_CORS = 0,
        MODE_SAME_ORIGIN,
        MODE_CORS,
        MODE_NAVIGATE,
        MODE_WEBSOCKET,
    }                              mode;
    njs_str_t                      url;
    njs_str_t                      method;
    u_char                         m[8];
    uint8_t                        body_used;
    njs_str_t                      body;
    ngx_js_headers_t               headers;
    njs_opaque_value_t             header_value;
} ngx_js_request_t;


typedef struct {
    njs_str_t                      url;
    ngx_int_t                      code;
    njs_str_t                      status_text;
    uint8_t                        body_used;
    njs_chb_t                      chain;
    ngx_js_headers_t               headers;
    njs_opaque_value_t             header_value;
} ngx_js_response_t;


struct ngx_js_http_s {
    ngx_log_t                     *log;
    ngx_pool_t                    *pool;

    njs_vm_t                      *vm;
    ngx_js_event_t                *event;

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

    unsigned                       header_only;

#if (NGX_SSL)
    ngx_str_t                      tls_name;
    ngx_ssl_t                     *ssl;
    njs_bool_t                     ssl_verify;
#endif

    ngx_buf_t                     *buffer;
    ngx_buf_t                     *chunk;
    njs_chb_t                      chain;

    ngx_js_response_t              response;
    njs_opaque_value_t             response_value;

    njs_opaque_value_t             promise;
    njs_opaque_value_t             promise_callbacks[2];

    uint8_t                        done;
    ngx_js_http_parse_t            http_parse;
    ngx_js_http_chunk_parse_t      http_chunk_parse;
    ngx_int_t                    (*process)(ngx_js_http_t *http);
};


static njs_int_t ngx_js_method_process(njs_vm_t *vm, ngx_js_request_t *r);
static njs_int_t ngx_js_headers_inherit(njs_vm_t *vm, ngx_js_headers_t *headers,
    ngx_js_headers_t *orig);
static njs_int_t ngx_js_headers_fill(njs_vm_t *vm, ngx_js_headers_t *headers,
    njs_value_t *init);
static ngx_js_http_t *ngx_js_http_alloc(njs_vm_t *vm, ngx_pool_t *pool,
    ngx_log_t *log);
static void ngx_js_http_resolve_done(ngx_js_http_t *http);
static void ngx_js_http_close_peer(ngx_js_http_t *http);
static void ngx_js_http_destructor(ngx_js_event_t *event);
static ngx_resolver_ctx_t *ngx_js_http_resolve(ngx_js_http_t *http,
    ngx_resolver_t *r, ngx_str_t *host, in_port_t port, ngx_msec_t timeout);
static void ngx_js_http_resolve_handler(ngx_resolver_ctx_t *ctx);
static njs_int_t ngx_js_fetch_promissified_result(njs_vm_t *vm,
    njs_value_t *result, njs_int_t rc, njs_value_t *retval);
static void ngx_js_http_fetch_done(ngx_js_http_t *http,
    njs_opaque_value_t *retval, njs_int_t rc);
static njs_int_t ngx_js_http_promise_trampoline(njs_vm_t *vm,
    njs_value_t *args, njs_uint_t nargs, njs_index_t unused,
    njs_value_t *retval);
static void ngx_js_http_connect(ngx_js_http_t *http);
static void ngx_js_http_next(ngx_js_http_t *http);
static void ngx_js_http_write_handler(ngx_event_t *wev);
static void ngx_js_http_read_handler(ngx_event_t *rev);

static njs_int_t ngx_js_request_constructor(njs_vm_t *vm,
    ngx_js_request_t *request, ngx_url_t *u, njs_external_ptr_t external,
    njs_value_t *args, njs_uint_t nargs);

static njs_int_t ngx_js_headers_append(njs_vm_t *vm, ngx_js_headers_t *headers,
    u_char *name, size_t len, u_char *value, size_t vlen);

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

#if (NGX_SSL)
static void ngx_js_http_ssl_init_connection(ngx_js_http_t *http);
static void ngx_js_http_ssl_handshake_handler(ngx_connection_t *c);
static void ngx_js_http_ssl_handshake(ngx_js_http_t *http);
static njs_int_t ngx_js_http_ssl_name(ngx_js_http_t *http);
#endif

static void ngx_js_http_trim(u_char **value, size_t *len,
    njs_bool_t trim_c0_control_or_space);
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
    ngx_url_t            u;
    ngx_uint_t           i;
    njs_bool_t           has_host;
    ngx_pool_t          *pool;
    njs_value_t         *init, *value;
    ngx_js_http_t       *http;
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

    http = ngx_js_http_alloc(vm, pool, c->log);
    if (http == NULL) {
        return NJS_ERROR;
    }

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

    http->header_only = njs_strstr_eq(&request.method, &njs_str_value("HEAD"));

    NJS_CHB_MP_INIT(&http->chain, vm);

    njs_chb_append(&http->chain, request.method.start, request.method.length);
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

    if (request.body.length != 0) {
        njs_chb_sprintf(&http->chain, 32, "Content-Length: %uz" CRLF CRLF,
                        request.body.length);
        njs_chb_append(&http->chain, request.body.start, request.body.length);

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

        njs_value_assign(retval, njs_value_arg(&http->promise));

        return NJS_OK;
    }

    http->naddrs = 1;
    ngx_memcpy(&http->addr, &u.addrs[0], sizeof(ngx_addr_t));
    http->addrs = &http->addr;

    ngx_js_http_connect(http);

    njs_value_assign(retval, njs_value_arg(&http->promise));

    return NJS_OK;

fail:


    njs_vm_exception_get(vm, njs_value_arg(&lvalue));

    ngx_js_http_fetch_done(http, &lvalue, NJS_ERROR);

    njs_value_assign(retval, njs_value_arg(&http->promise));

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
            if (ngx_js_string(vm, value, &response->status_text) != NGX_OK) {
                njs_vm_error(vm, "invalid Response statusText");
                return NJS_ERROR;
            }

            p = response->status_text.start;
            end = p + response->status_text.length;

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

    NJS_CHB_MP_INIT(&response->chain, vm);

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

    for (m = &forbidden[0]; m->length != 0; m++) {
        if (njs_strstr_case_eq(&request->method, m)) {
            njs_vm_error(vm, "forbidden method: %V", m);
            return NJS_ERROR;
        }
    }

    for (m = &to_normalize[0]; m->length != 0; m++) {
        if (njs_strstr_case_eq(&request->method, m)) {
            s = &request->m[0];
            p = m->start;

            while (*p != '\0') {
                *s++ = njs_upper_case(*p++);
            }

            request->method.start = &request->m[0];
            request->method.length = m->length;
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


static ngx_js_http_t *
ngx_js_http_alloc(njs_vm_t *vm, ngx_pool_t *pool, ngx_log_t *log)
{
    njs_int_t        ret;
    ngx_js_ctx_t    *ctx;
    ngx_js_http_t   *http;
    ngx_js_event_t  *event;
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
    event->destructor = ngx_js_http_destructor;
    event->fd = ctx->event_id++;
    event->data = http;

    ngx_js_add_event(ctx, event);

    http->event = event;

    ngx_log_debug1(NGX_LOG_DEBUG_EVENT, log, 0, "js fetch alloc:%p", http);

    return http;

failed:

    njs_vm_error(vm, "internal error");

    return NULL;
}


static void
ngx_js_http_error(ngx_js_http_t *http, const char *fmt, ...)
{
    u_char   *p, *end;
    va_list   args;
    u_char    err[NGX_MAX_ERROR_STR];

    end = err + NGX_MAX_ERROR_STR - 1;

    va_start(args, fmt);
    p = njs_vsprintf(err, end, fmt, args);
    *p = '\0';
    va_end(args);

    njs_vm_error(http->vm, (const char *) err);
    njs_vm_exception_get(http->vm, njs_value_arg(&http->response_value));
    ngx_js_http_fetch_done(http, &http->response_value, NJS_ERROR);
}


static ngx_resolver_ctx_t *
ngx_js_http_resolve(ngx_js_http_t *http, ngx_resolver_t *r, ngx_str_t *host,
    in_port_t port, ngx_msec_t timeout)
{
    ngx_int_t            ret;
    ngx_resolver_ctx_t  *ctx;

    ctx = ngx_resolve_start(r, NULL);
    if (ctx == NULL) {
        return NULL;
    }

    if (ctx == NGX_NO_RESOLVER) {
        return ctx;
    }

    http->ctx = ctx;
    http->port = port;

    ctx->name = *host;
    ctx->handler = ngx_js_http_resolve_handler;
    ctx->data = http;
    ctx->timeout = timeout;

    ret = ngx_resolve_name(ctx);
    if (ret != NGX_OK) {
        http->ctx = NULL;
        return NULL;
    }

    return ctx;
}


static void
ngx_js_http_resolve_handler(ngx_resolver_ctx_t *ctx)
{
    u_char           *p;
    size_t            len;
    socklen_t         socklen;
    ngx_uint_t        i;
    ngx_js_http_t    *http;
    struct sockaddr  *sockaddr;

    http = ctx->data;

    if (ctx->state) {
        ngx_js_http_error(http, "\"%V\" could not be resolved (%i: %s)",
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

    ngx_js_http_resolve_done(http);

    ngx_js_http_connect(http);

    return;

failed:

    ngx_js_http_error(http, "memory error");
}


static void
ngx_js_http_close_connection(ngx_connection_t *c)
{
    ngx_log_debug1(NGX_LOG_DEBUG_EVENT, c->log, 0,
                   "js fetch close connection: %d", c->fd);

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
ngx_js_http_resolve_done(ngx_js_http_t *http)
{
    if (http->ctx != NULL) {
        ngx_resolve_name_done(http->ctx);
        http->ctx = NULL;
    }
}


static void
ngx_js_http_close_peer(ngx_js_http_t *http)
{
    if (http->peer.connection != NULL) {
        ngx_js_http_close_connection(http->peer.connection);
        http->peer.connection = NULL;
    }
}


static void
ngx_js_http_destructor(ngx_js_event_t *event)
{
    ngx_js_http_t  *http;

    http = event->data;

    ngx_log_debug1(NGX_LOG_DEBUG_EVENT, http->log, 0, "js fetch destructor:%p",
                   http);

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
ngx_js_http_fetch_done(ngx_js_http_t *http, njs_opaque_value_t *retval,
    njs_int_t rc)
{
    njs_vm_t            *vm;
    ngx_js_ctx_t        *ctx;
    ngx_js_event_t      *event;
    njs_opaque_value_t   arguments[2], *action;

    ngx_log_debug2(NGX_LOG_DEBUG_EVENT, http->log, 0,
                   "js fetch done http:%p rc:%i", http, (ngx_int_t) rc);

    ngx_js_http_close_peer(http);

    if (http->event != NULL) {
        action = &http->promise_callbacks[(rc != NJS_OK)];
        njs_value_assign(&arguments[0], action);
        njs_value_assign(&arguments[1], retval);

        vm = http->vm;
        event = http->event;

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


static void
ngx_js_http_connect(ngx_js_http_t *http)
{
    ngx_int_t    rc;
    ngx_addr_t  *addr;

    addr = &http->addrs[http->naddr];

    ngx_log_debug2(NGX_LOG_DEBUG_EVENT, http->log, 0,
                   "js fetch connect %ui/%ui", http->naddr, http->naddrs);

    http->peer.sockaddr = addr->sockaddr;
    http->peer.socklen = addr->socklen;
    http->peer.name = &addr->name;
    http->peer.get = ngx_event_get_peer;
    http->peer.log = http->log;
    http->peer.log_error = NGX_ERROR_ERR;

    rc = ngx_event_connect_peer(&http->peer);

    if (rc == NGX_ERROR) {
        ngx_js_http_error(http, "connect failed");
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
                   "js fetch secure connect %ui/%ui", http->naddr,
                   http->naddrs);

    if (ngx_ssl_create_connection(http->ssl, c, NGX_SSL_BUFFER|NGX_SSL_CLIENT)
        != NGX_OK)
    {
        ngx_js_http_error(http, "failed to create ssl connection");
        return;
    }

    c->sendfile = 0;

    if (ngx_js_http_ssl_name(http) != NGX_OK) {
        ngx_js_http_error(http, "failed to create ssl connection");
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
                              "js fetch SSL certificate verify error: (%l:%s)",
                              rc, X509_verify_cert_error_string(rc));
                goto failed;
            }

            if (ngx_ssl_check_host(c, &http->tls_name) != NGX_OK) {
                ngx_log_error(NGX_LOG_ERR, c->log, 0,
                              "js fetch SSL certificate does not match \"%V\"",
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

    ngx_log_debug1(NGX_LOG_DEBUG_EVENT, http->log, 0,
                   "js fetch SSL server name: \"%s\"", name->data);

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
    ngx_log_debug0(NGX_LOG_DEBUG_EVENT, http->log, 0, "js fetch next addr");

    if (++http->naddr >= http->naddrs) {
        ngx_js_http_error(http, "connect failed");
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

    ngx_log_debug0(NGX_LOG_DEBUG_EVENT, wev->log, 0, "js fetch write handler");

    if (wev->timedout) {
        ngx_js_http_error(http, "write timed out");
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
            ngx_js_http_error(http, "memory error");
            return;
        }

        b = ngx_create_temp_buf(http->pool, size);
        if (b == NULL) {
            ngx_js_http_error(http, "memory error");
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
                ngx_js_http_error(http, "write failed");
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

    ngx_log_debug0(NGX_LOG_DEBUG_EVENT, rev->log, 0, "js fetch read handler");

    if (rev->timedout) {
        ngx_js_http_error(http, "read timed out");
        return;
    }

    if (http->buffer == NULL) {
        b = ngx_create_temp_buf(http->pool, http->buffer_size);
        if (b == NULL) {
            ngx_js_http_error(http, "memory error");
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
                ngx_js_http_error(http, "read failed");
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
        ngx_js_http_error(http, "prematurely closed connection");
    }
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

    request->method = njs_str_value("GET");
    request->body = njs_str_value("");
    request->headers.guard = GUARD_REQUEST;

    pool = ngx_external_pool(vm, external);

    rc = ngx_list_init(&request->headers.header_list, pool, 4,
                       sizeof(ngx_js_tb_elt_t));
    if (rc != NGX_OK) {
        njs_vm_memory_error(vm);
        return NJS_ERROR;
    }

    if (njs_value_is_string(input)) {
        ret = ngx_js_string(vm, input, &request->url);
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

    ngx_js_http_trim(&request->url.start, &request->url.length, 1);

    ngx_memzero(u, sizeof(ngx_url_t));

    u->url.len = request->url.length;
    u->url.data = request->url.start;
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
        if (value != NULL && ngx_js_string(vm, value, &request->method)
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
            if (ngx_js_string(vm, value, &request->body) != NGX_OK) {
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


njs_inline njs_int_t
ngx_js_http_whitespace(u_char c)
{
    switch (c) {
    case 0x09:  /* <TAB>  */
    case 0x0A:  /* <LF>   */
    case 0x0D:  /* <CR>   */
    case 0x20:  /* <SP>   */
        return 1;

    default:
        return 0;
    }
}


static void
ngx_js_http_trim(u_char **value, size_t *len,
    njs_bool_t trim_c0_control_or_space)
{
    u_char  *start, *end;

    start = *value;
    end = start + *len;

    for ( ;; ) {
        if (start == end) {
            break;
        }

        if (ngx_js_http_whitespace(*start)
            || (trim_c0_control_or_space && *start <= ' '))
        {
            start++;
            continue;
        }

        break;
    }

    for ( ;; ) {
        if (start == end) {
            break;
        }

        end--;

        if (ngx_js_http_whitespace(*end)
            || (trim_c0_control_or_space && *end <= ' '))
        {
            continue;
        }

        end++;
        break;
    }

    *value = start;
    *len = end - start;
}


static const uint32_t  token_map[] = {
    0x00000000,  /* 0000 0000 0000 0000  0000 0000 0000 0000 */

                 /* ?>=< ;:98 7654 3210  /.-, +*)( '&%$ #"!  */
    0x03ff6cfa,  /* 0000 0011 1111 1111  0110 1100 1111 1010 */

                 /* _^]\ [ZYX WVUT SRQP  ONML KJIH GFED CBA@ */
    0xc7fffffe,  /* 1100 0111 1111 1111  1111 1111 1111 1110 */

                 /*  ~}| {zyx wvut srqp  onml kjih gfed cba` */
    0x57ffffff,  /* 0101 0111 1111 1111  1111 1111 1111 1111 */

    0x00000000,  /* 0000 0000 0000 0000  0000 0000 0000 0000 */
    0x00000000,  /* 0000 0000 0000 0000  0000 0000 0000 0000 */
    0x00000000,  /* 0000 0000 0000 0000  0000 0000 0000 0000 */
    0x00000000,  /* 0000 0000 0000 0000  0000 0000 0000 0000 */
};


njs_inline njs_bool_t
njs_is_token(uint32_t byte)
{
    return ((token_map[byte >> 5] & ((uint32_t) 1 << (byte & 0x1f))) != 0);
}


static njs_int_t
ngx_js_headers_append(njs_vm_t *vm, ngx_js_headers_t *headers,
    u_char *name, size_t len, u_char *value, size_t vlen)
{
    u_char           *p, *end;
    ngx_uint_t        i;
    ngx_js_tb_elt_t  *h, **ph;
    ngx_list_part_t  *part;

    ngx_js_http_trim(&value, &vlen, 0);

    p = name;
    end = p + len;

    while (p < end) {
        if (!njs_is_token(*p)) {
            njs_vm_error(vm, "invalid header name");
            return NJS_ERROR;
        }

        p++;
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


static ngx_int_t
ngx_js_http_process_status_line(ngx_js_http_t *http)
{
    ngx_int_t             rc;
    ngx_js_http_parse_t  *hp;

    hp = &http->http_parse;

    rc = ngx_js_http_parse_status_line(hp, http->buffer);

    if (rc == NGX_OK) {
        ngx_log_debug1(NGX_LOG_DEBUG_EVENT, http->log, 0, "js fetch status %ui",
                       hp->code);

        http->response.code = hp->code;
        http->response.status_text.start = hp->status_text;
        http->response.status_text.length = hp->status_text_end
                                                             - hp->status_text;
        http->process = ngx_js_http_process_headers;

        return http->process(http);
    }

    if (rc == NGX_AGAIN) {
        return NGX_AGAIN;
    }

    /* rc == NGX_ERROR */

    ngx_js_http_error(http, "invalid fetch status line");

    return NGX_ERROR;
}


static ngx_int_t
ngx_js_http_process_headers(ngx_js_http_t *http)
{
    size_t                len, vlen;
    ngx_int_t             rc;
    njs_int_t             ret;
    ngx_js_http_parse_t  *hp;

    ngx_log_debug0(NGX_LOG_DEBUG_EVENT, http->log, 0,
                   "js fetch process headers");

    hp = &http->http_parse;

    if (http->response.headers.header_list.size == 0) {
        rc = ngx_list_init(&http->response.headers.header_list, http->pool, 4,
                           sizeof(ngx_js_tb_elt_t));
        if (rc != NGX_OK) {
            ngx_js_http_error(http, "alloc failed");
            return NGX_ERROR;
        }
    }

    for ( ;; ) {
        rc = ngx_js_http_parse_header_line(hp, http->buffer);

        if (rc == NGX_OK) {
            len = hp->header_name_end - hp->header_name_start;
            vlen = hp->header_end - hp->header_start;

            ret = ngx_js_headers_append(http->vm, &http->response.headers,
                                        hp->header_name_start, len,
                                        hp->header_start, vlen);

            if (ret == NJS_ERROR) {
                ngx_js_http_error(http, "cannot add respose header");
                return NGX_ERROR;
            }

            ngx_log_debug4(NGX_LOG_DEBUG_EVENT, http->log, 0,
                           "js fetch header \"%*s: %*s\"",
                           len, hp->header_name_start, vlen, hp->header_start);

            if (len == njs_strlen("Transfer-Encoding")
                && vlen == njs_strlen("chunked")
                && ngx_strncasecmp(hp->header_name_start,
                                   (u_char *) "Transfer-Encoding", len) == 0
                && ngx_strncasecmp(hp->header_start, (u_char *) "chunked",
                                   vlen) == 0)
            {
                hp->chunked = 1;
            }

            if (len == njs_strlen("Content-Length")
                && ngx_strncasecmp(hp->header_name_start,
                                   (u_char *) "Content-Length", len) == 0)
            {
                hp->content_length_n = ngx_atoof(hp->header_start, vlen);
                if (hp->content_length_n == NGX_ERROR) {
                    ngx_js_http_error(http, "invalid fetch content length");
                    return NGX_ERROR;
                }

                if (!http->header_only
                    && hp->content_length_n
                       > (off_t) http->max_response_body_size)
                {
                    ngx_js_http_error(http,
                                      "fetch content length is too large");
                    return NGX_ERROR;
                }
            }

            continue;
        }

        if (rc == NGX_DONE) {
            http->response.headers.guard = GUARD_IMMUTABLE;
            break;
        }

        if (rc == NGX_AGAIN) {
            return NGX_AGAIN;
        }

        /* rc == NGX_ERROR */

        ngx_js_http_error(http, "invalid fetch header");

        return NGX_ERROR;
    }

    njs_chb_destroy(&http->chain);

    NJS_CHB_MP_INIT(&http->response.chain, http->vm);

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
                   "js fetch process body done:%ui", (ngx_uint_t) http->done);

    if (http->done) {
        size = njs_chb_size(&http->response.chain);
        if (size < 0) {
            ngx_js_http_error(http, "memory error");
            return NGX_ERROR;
        }

        if (!http->header_only
            && http->http_parse.chunked
            && http->http_parse.content_length_n == -1)
        {
            ngx_js_http_error(http, "invalid fetch chunked response");
            return NGX_ERROR;
        }

        if (http->header_only
            || http->http_parse.content_length_n == -1
            || size == http->http_parse.content_length_n)
        {
            ret = njs_vm_external_create(http->vm,
                                         njs_value_arg(&http->response_value),
                                         ngx_http_js_fetch_response_proto_id,
                                         &http->response, 0);
            if (ret != NJS_OK) {
                ngx_js_http_error(http, "fetch response creation failed");
                return NGX_ERROR;
            }

            ngx_js_http_fetch_done(http, &http->response_value, NJS_OK);
            return NGX_DONE;
        }

        if (size < http->http_parse.content_length_n) {
            return NGX_AGAIN;
        }

        ngx_js_http_error(http, "fetch trailing data");
        return NGX_ERROR;
    }

    b = http->buffer;

    if (http->http_parse.chunked) {
        rc = ngx_js_http_parse_chunked(&http->http_chunk_parse, b,
                                       &http->response.chain);
        if (rc == NGX_ERROR) {
            ngx_js_http_error(http, "invalid fetch chunked response");
            return NGX_ERROR;
        }

        size = njs_chb_size(&http->response.chain);

        if (rc == NGX_OK) {
            http->http_parse.content_length_n = size;
        }

        if (size > http->max_response_body_size * 10) {
            ngx_js_http_error(http, "very large fetch chunked response");
            return NGX_ERROR;
        }

        b->pos = http->http_chunk_parse.pos;

    } else {
        size = njs_chb_size(&http->response.chain);

        if (http->header_only) {
            need = 0;

        } else  if (http->http_parse.content_length_n == -1) {
            need = http->max_response_body_size - size;

        } else {
            need = http->http_parse.content_length_n - size;
        }

        chsize = ngx_min(need, b->last - b->pos);

        if (size + chsize > http->max_response_body_size) {
            ngx_js_http_error(http, "fetch response body is too large");
            return NGX_ERROR;
        }

        if (chsize > 0) {
            njs_chb_append(&http->response.chain, b->pos, chsize);
            b->pos += chsize;
        }

        rc = (need > chsize) ? NGX_AGAIN : NGX_DONE;
    }

    if (b->pos == b->end) {
        if (http->chunk == NULL) {
            b = ngx_create_temp_buf(http->pool, http->buffer_size);
            if (b == NULL) {
                ngx_js_http_error(http, "memory error");
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

            if (ch == '-' || ch == '_') {
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
    ngx_log_debug0(NGX_LOG_DEBUG_EVENT, ev->log, 0, "js fetch dummy handler");
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

    NJS_CHB_MP_INIT(&chain, vm);

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
                                            request->body.start,
                                            request->body.length);
        if (ret != NJS_OK) {
            njs_vm_memory_error(vm);
            return NJS_ERROR;
        }

        break;

    case NGX_JS_BODY_JSON:
    case NGX_JS_BODY_TEXT:
    default:
        ret = njs_vm_value_string_create(vm, njs_value_arg(&result),
                                         request->body.start,
                                         request->body.length);
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

    njs_vm_value_string_create(vm, retval, response->status_text.start,
                               response->status_text.length);

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
