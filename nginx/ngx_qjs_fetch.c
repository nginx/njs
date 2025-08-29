
/*
 * Copyright (C) hongzhidao
 * Copyright (C) F5, Inc.
 */


#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_event.h>
#include <ngx_event_connect.h>
#include "ngx_js.h"
#include "ngx_js_http.h"


typedef struct {
    ngx_str_t        name;
    ngx_int_t        value;
} ngx_qjs_entry_t;


typedef struct {
    ngx_js_http_t    http;

    JSContext        *cx;
    ngx_qjs_event_t  *event;

    JSValue           response_value;

    JSValue           promise;
    JSValue           promise_callbacks[2];
} ngx_qjs_fetch_t;


static ngx_int_t ngx_qjs_method_process(JSContext *cx,
    ngx_js_request_t *request);
static ngx_int_t ngx_qjs_headers_inherit(JSContext *cx,
    ngx_js_headers_t *headers, ngx_js_headers_t *orig);
static ngx_int_t ngx_qjs_headers_fill(JSContext *cx, ngx_js_headers_t *headers,
    JSValue init);
static ngx_qjs_fetch_t *ngx_qjs_fetch_alloc(JSContext *cx, ngx_pool_t *pool,
    ngx_log_t *log);
static void ngx_qjs_fetch_error(ngx_js_http_t *http, const char *err);
static void ngx_qjs_fetch_destructor(ngx_qjs_event_t *event);
static void ngx_qjs_fetch_done(ngx_qjs_fetch_t *fetch, JSValue retval,
    ngx_int_t rc);

static ngx_int_t ngx_qjs_request_ctor(JSContext *cx, ngx_js_request_t *request,
    ngx_url_t *u, int argc, JSValueConst *argv);

static ngx_int_t ngx_qjs_fetch_append_headers(ngx_js_http_t *http,
    ngx_js_headers_t *headers, u_char *name, size_t len, u_char *value,
    size_t vlen);
static void ngx_qjs_fetch_process_done(ngx_js_http_t *http);
static ngx_int_t ngx_qjs_headers_append(JSContext *cx,
    ngx_js_headers_t *headers, u_char *name, size_t len, u_char *value,
    size_t vlen);

static JSValue ngx_qjs_fetch_headers_ctor(JSContext *cx,
    JSValueConst new_target, int argc, JSValueConst *argv);
static int ngx_qjs_fetch_headers_own_property(JSContext *cx,
    JSPropertyDescriptor *desc, JSValueConst obj, JSAtom prop);
static int ngx_qjs_fetch_headers_own_property_names(JSContext *cx,
    JSPropertyEnum **ptab, uint32_t *plen, JSValueConst obj);
static JSValue ngx_qjs_ext_fetch_headers_append(JSContext *cx,
    JSValueConst this_val, int argc, JSValueConst *argv);
static JSValue ngx_qjs_ext_fetch_headers_delete(JSContext *cx,
    JSValueConst this_val, int argc, JSValueConst *argv);
static JSValue ngx_qjs_ext_fetch_headers_foreach(JSContext *cx,
    JSValueConst this_val, int argc, JSValueConst *argv);
static JSValue ngx_qjs_ext_fetch_headers_get(JSContext *cx,
    JSValueConst this_val, int argc, JSValueConst *argv, int magic);
static JSValue ngx_qjs_ext_fetch_headers_has(JSContext *cx,
    JSValueConst this_val, int argc, JSValueConst *argv);
static JSValue ngx_qjs_ext_fetch_headers_set(JSContext *cx,
    JSValueConst this_val, int argc, JSValueConst *argv);

static JSValue ngx_qjs_fetch_request_ctor(JSContext *cx,
    JSValueConst new_target, int argc, JSValueConst *argv);
static JSValue ngx_qjs_ext_fetch_request_body(JSContext *cx,
    JSValueConst this_val, int argc, JSValueConst *argv, int magic);
static JSValue ngx_qjs_ext_fetch_request_body_used(JSContext *cx,
    JSValueConst this_val);
static JSValue ngx_qjs_ext_fetch_request_cache(JSContext *cx,
    JSValueConst this_val);
static JSValue ngx_qjs_ext_fetch_request_credentials(JSContext *cx,
    JSValueConst this_val);
static JSValue ngx_qjs_ext_fetch_request_headers(JSContext *cx,
    JSValueConst this_val);
static JSValue ngx_qjs_ext_fetch_request_field(JSContext *cx,
    JSValueConst this_val, int magic);
static JSValue ngx_qjs_ext_fetch_request_mode(JSContext *cx,
    JSValueConst this_val);
static void ngx_qjs_fetch_request_finalizer(JSRuntime *rt, JSValue val);

static JSValue ngx_qjs_fetch_response_ctor(JSContext *cx,
    JSValueConst new_target, int argc, JSValueConst *argv);
static JSValue ngx_qjs_ext_fetch_response_status(JSContext *cx,
    JSValueConst this_val);
static JSValue ngx_qjs_ext_fetch_response_status_text(JSContext *cx,
    JSValueConst this_val);
static JSValue ngx_qjs_ext_fetch_response_ok(JSContext *cx,
    JSValueConst this_val);
static JSValue ngx_qjs_ext_fetch_response_body_used(JSContext *cx,
    JSValueConst this_val);
static JSValue ngx_qjs_ext_fetch_response_headers(JSContext *cx,
    JSValueConst this_val);
static JSValue ngx_qjs_ext_fetch_response_type(JSContext *cx,
    JSValueConst this_val);
static JSValue ngx_qjs_ext_fetch_response_body(JSContext *cx,
    JSValueConst this_val, int argc, JSValueConst *argv, int magic);
static JSValue ngx_qjs_ext_fetch_response_redirected(JSContext *cx,
    JSValueConst this_val);
static JSValue ngx_qjs_ext_fetch_response_field(JSContext *cx,
    JSValueConst this_val, int magic);
static void ngx_qjs_fetch_response_finalizer(JSRuntime *rt, JSValue val);

static JSValue ngx_qjs_fetch_flag(JSContext *cx, const ngx_qjs_entry_t *entries,
    ngx_int_t value);
static ngx_int_t ngx_qjs_fetch_flag_set(JSContext *cx,
    const ngx_qjs_entry_t *entries, JSValue object, const char *prop);

static JSModuleDef *ngx_qjs_fetch_init(JSContext *cx, const char *name);


static const JSCFunctionListEntry  ngx_qjs_ext_fetch_headers_proto[] = {
    JS_CFUNC_DEF("append", 2, ngx_qjs_ext_fetch_headers_append),
    JS_CFUNC_DEF("delete", 1, ngx_qjs_ext_fetch_headers_delete),
    JS_CFUNC_DEF("forEach", 1, ngx_qjs_ext_fetch_headers_foreach),
    JS_CFUNC_MAGIC_DEF("get", 1, ngx_qjs_ext_fetch_headers_get, 0),
    JS_CFUNC_MAGIC_DEF("getAll", 1, ngx_qjs_ext_fetch_headers_get, 1),
    JS_CFUNC_DEF("has", 1, ngx_qjs_ext_fetch_headers_has),
    JS_CFUNC_DEF("set", 2, ngx_qjs_ext_fetch_headers_set),
};


static const JSCFunctionListEntry  ngx_qjs_ext_fetch_request_proto[] = {
#define NGX_QJS_BODY_ARRAY_BUFFER   0
#define NGX_QJS_BODY_JSON           1
#define NGX_QJS_BODY_TEXT           2
    JS_CFUNC_MAGIC_DEF("arrayBuffer", 0, ngx_qjs_ext_fetch_request_body,
                       NGX_QJS_BODY_ARRAY_BUFFER),
    JS_CGETSET_DEF("bodyUsed", ngx_qjs_ext_fetch_request_body_used, NULL),
    JS_CGETSET_DEF("cache", ngx_qjs_ext_fetch_request_cache, NULL),
    JS_CGETSET_DEF("credentials", ngx_qjs_ext_fetch_request_credentials, NULL),
    JS_CFUNC_MAGIC_DEF("json", 0, ngx_qjs_ext_fetch_request_body,
                       NGX_QJS_BODY_JSON),
    JS_CGETSET_DEF("headers", ngx_qjs_ext_fetch_request_headers, NULL ),
    JS_CGETSET_MAGIC_DEF("method", ngx_qjs_ext_fetch_request_field, NULL,
                         offsetof(ngx_js_request_t, method) ),
    JS_CGETSET_DEF("mode", ngx_qjs_ext_fetch_request_mode, NULL),
    JS_CFUNC_MAGIC_DEF("text", 0, ngx_qjs_ext_fetch_request_body,
                       NGX_QJS_BODY_TEXT),
    JS_CGETSET_MAGIC_DEF("url", ngx_qjs_ext_fetch_request_field, NULL,
                         offsetof(ngx_js_request_t, url) ),
};


static const JSCFunctionListEntry  ngx_qjs_ext_fetch_response_proto[] = {
    JS_CFUNC_MAGIC_DEF("arrayBuffer", 0, ngx_qjs_ext_fetch_response_body,
                       NGX_QJS_BODY_ARRAY_BUFFER),
    JS_CGETSET_DEF("bodyUsed", ngx_qjs_ext_fetch_response_body_used, NULL),
    JS_CGETSET_DEF("headers", ngx_qjs_ext_fetch_response_headers, NULL ),
    JS_CFUNC_MAGIC_DEF("json", 0, ngx_qjs_ext_fetch_response_body,
                       NGX_QJS_BODY_JSON),
    JS_CGETSET_DEF("ok", ngx_qjs_ext_fetch_response_ok, NULL),
    JS_CGETSET_DEF("redirected", ngx_qjs_ext_fetch_response_redirected, NULL),
    JS_CGETSET_DEF("status", ngx_qjs_ext_fetch_response_status, NULL),
    JS_CGETSET_DEF("statusText", ngx_qjs_ext_fetch_response_status_text, NULL),
    JS_CFUNC_MAGIC_DEF("text", 0, ngx_qjs_ext_fetch_response_body,
                       NGX_QJS_BODY_TEXT),
    JS_CGETSET_DEF("type", ngx_qjs_ext_fetch_response_type, NULL),
    JS_CGETSET_MAGIC_DEF("url", ngx_qjs_ext_fetch_response_field, NULL,
                         offsetof(ngx_js_response_t, url) ),
};


static const JSClassDef  ngx_qjs_fetch_headers_class = {
    "Headers",
    .finalizer = NULL,
    .exotic = & (JSClassExoticMethods) {
        .get_own_property = ngx_qjs_fetch_headers_own_property,
        .get_own_property_names = ngx_qjs_fetch_headers_own_property_names,
    },
};


static const JSClassDef  ngx_qjs_fetch_request_class = {
    "Request",
    .finalizer = ngx_qjs_fetch_request_finalizer,
};


static const JSClassDef  ngx_qjs_fetch_response_class = {
    "Response",
    .finalizer = ngx_qjs_fetch_response_finalizer,
};


static const ngx_qjs_entry_t  ngx_qjs_fetch_cache_modes[] = {
    { ngx_string("default"), CACHE_MODE_DEFAULT },
    { ngx_string("no-store"), CACHE_MODE_NO_STORE },
    { ngx_string("reload"), CACHE_MODE_RELOAD },
    { ngx_string("no-cache"), CACHE_MODE_NO_CACHE },
    { ngx_string("force-cache"), CACHE_MODE_FORCE_CACHE },
    { ngx_string("only-if-cached"), CACHE_MODE_ONLY_IF_CACHED },
    { ngx_null_string, 0 },
};


static const ngx_qjs_entry_t  ngx_qjs_fetch_credentials[] = {
    { ngx_string("same-origin"), CREDENTIALS_SAME_ORIGIN },
    { ngx_string("omit"), CREDENTIALS_OMIT },
    { ngx_string("include"), CREDENTIALS_INCLUDE },
    { ngx_null_string, 0 },
};


static const ngx_qjs_entry_t  ngx_qjs_fetch_modes[] = {
    { ngx_string("no-cors"), MODE_NO_CORS },
    { ngx_string("cors"), MODE_CORS },
    { ngx_string("same-origin"), MODE_SAME_ORIGIN },
    { ngx_string("navigate"), MODE_NAVIGATE },
    { ngx_string("websocket"), MODE_WEBSOCKET },
    { ngx_null_string, 0 },
};


qjs_module_t  ngx_qjs_ngx_fetch_module = {
    .name = "fetch",
    .init = ngx_qjs_fetch_init,
};


JSValue
ngx_qjs_ext_fetch(JSContext *cx, JSValueConst this_val, int argc,
    JSValueConst *argv)
{
    int                  has_host;
    void                *external;
    JSValue              init, value, promise;
    ngx_int_t            rc;
    ngx_url_t            u;
    ngx_str_t            method;
    ngx_uint_t           i;
    ngx_pool_t          *pool;
    ngx_js_ctx_t        *ctx;
    ngx_js_http_t       *http;
    ngx_qjs_fetch_t     *fetch;
    ngx_list_part_t     *part;
    ngx_js_tb_elt_t     *h;
    ngx_connection_t    *c;
    ngx_js_request_t     request;
    ngx_resolver_ctx_t  *rs;

    external = JS_GetContextOpaque(cx);
    c = ngx_qjs_external_connection(cx, external);
    pool = ngx_qjs_external_pool(cx, external);

    fetch = ngx_qjs_fetch_alloc(cx, pool, c->log);
    if (fetch == NULL) {
        return JS_ThrowOutOfMemory(cx);
    }

    promise = JS_DupValue(cx, fetch->promise);

    rc = ngx_qjs_request_ctor(cx, &request, &u, argc, argv);
    if (rc != NGX_OK) {
        goto fail;
    }

    http = &fetch->http;
    http->response.url = request.url;
    http->timeout = ngx_qjs_external_fetch_timeout(cx, external);
    http->buffer_size = ngx_qjs_external_buffer_size(cx, external);
    http->max_response_body_size =
                        ngx_qjs_external_max_response_buffer_size(cx, external);

#if (NGX_SSL)
    if (u.default_port == 443) {
        http->ssl = ngx_qjs_external_ssl(cx, external);
        http->ssl_verify = ngx_qjs_external_ssl_verify(cx, external);
    }
#endif

    if (JS_IsObject(argv[1])) {
        init = argv[1];
        value = JS_GetPropertyStr(cx, init, "buffer_size");
        if (JS_IsException(value)) {
            goto fail;
        }

        if (!JS_IsUndefined(value)) {
            if (JS_ToInt64(cx, (int64_t *) &http->buffer_size, value) < 0) {
                JS_FreeValue(cx, value);
                goto fail;
            }
        }

        value = JS_GetPropertyStr(cx, init, "max_response_body_size");
        if (JS_IsException(value)) {
            goto fail;
        }

        if (!JS_IsUndefined(value)) {
            if (JS_ToInt64(cx, (int64_t *) &http->max_response_body_size,
                           value) < 0)
            {
                JS_FreeValue(cx, value);
                goto fail;
            }
        }

#if (NGX_SSL)
        value = JS_GetPropertyStr(cx, init, "verify");
        if (JS_IsException(value)) {
            goto fail;
        }

        if (!JS_IsUndefined(value)) {
            http->ssl_verify = JS_ToBool(cx, value);
        }
#endif
    }

    if (request.method.len == 4
        && ngx_strncasecmp(request.method.data, (u_char *) "HEAD", 4) == 0)
    {
        http->header_only = 1;
    }

    ctx = ngx_qjs_external_ctx(cx, JS_GetContextOpaque(cx));

    NJS_CHB_MP_INIT(&http->chain, ctx->engine->pool);
    NJS_CHB_MP_INIT(&http->response.chain, ctx->engine->pool);

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

        if (h[i].key.len == 14
            && ngx_strncasecmp(h[i].key.data, (u_char *) "Content-Length", 14)
            == 0)
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
        method = request.method;

        if ((method.len == 4
            && (ngx_strncasecmp(method.data, (u_char *) "POST", 4) == 0))
            || (method.len == 3
                && ngx_strncasecmp(method.data, (u_char *) "PUT", 3) == 0))
        {
            njs_chb_append_literal(&http->chain, "Content-Length: 0" CRLF CRLF);

        } else {
            njs_chb_append_literal(&http->chain, CRLF);
        }
    }

    if (u.addrs == NULL) {
        rs = ngx_js_http_resolve(http, ngx_qjs_external_resolver(cx, external),
                                 &u.host, u.port,
                               ngx_qjs_external_resolver_timeout(cx, external));
        if (rs == NULL) {
            JS_FreeValue(cx, promise);
            return JS_ThrowOutOfMemory(cx);
        }

        if (rs == NGX_NO_RESOLVER) {
            JS_ThrowInternalError(cx, "no resolver defined");
            goto fail;
        }

        return promise;
    }

    http->naddrs = 1;
    ngx_memcpy(&http->addr, &u.addrs[0], sizeof(ngx_addr_t));
    http->addrs = &http->addr;

    ngx_js_http_connect(http);

    return promise;

fail:

    fetch->response_value = JS_GetException(cx);

    ngx_qjs_fetch_done(fetch, fetch->response_value, NGX_ERROR);

    return promise;
}


static JSValue
ngx_qjs_fetch_headers_ctor(JSContext *cx, JSValueConst new_target, int argc,
    JSValueConst *argv)
{
    JSValue            init, proto, obj;
    ngx_int_t          rc;
    ngx_pool_t        *pool;
    ngx_js_headers_t  *headers;

    pool = ngx_qjs_external_pool(cx, JS_GetContextOpaque(cx));

    headers = ngx_pcalloc(pool, sizeof(ngx_js_headers_t));
    if (headers == NULL) {
        return JS_ThrowOutOfMemory(cx);
    }

    headers->guard = GUARD_NONE;

    rc = ngx_list_init(&headers->header_list, pool, 4,
                       sizeof(ngx_js_tb_elt_t));
    if (rc != NGX_OK) {
        return JS_ThrowOutOfMemory(cx);
    }

    init = argv[0];

    if (JS_IsObject(init)) {
        rc = ngx_qjs_headers_fill(cx, headers, init);
        if (rc != NGX_OK) {
            return JS_EXCEPTION;
        }
    }

    proto = JS_GetPropertyStr(cx, new_target, "prototype");
    if (JS_IsException(proto)) {
        return JS_EXCEPTION;
    }

    obj = JS_NewObjectProtoClass(cx, proto, NGX_QJS_CLASS_ID_FETCH_HEADERS);
    JS_FreeValue(cx, proto);

    if (JS_IsException(obj)) {
        return JS_EXCEPTION;
    }

    JS_SetOpaque(obj, headers);

    return obj;
}


static JSValue
ngx_qjs_fetch_request_ctor(JSContext *cx, JSValueConst new_target, int argc,
    JSValueConst *argv)
{
    JSValue            proto, obj;
    ngx_int_t          rc;
    ngx_url_t          u;
    ngx_pool_t        *pool;
    ngx_js_request_t  *request;

    pool = ngx_qjs_external_pool(cx, JS_GetContextOpaque(cx));

    request = ngx_pcalloc(pool, sizeof(ngx_js_request_t));
    if (request == NULL) {
        return JS_ThrowOutOfMemory(cx);
    }

    rc = ngx_qjs_request_ctor(cx, request, &u, argc, argv);
    if (rc != NGX_OK) {
        return JS_EXCEPTION;
    }

    proto = JS_GetPropertyStr(cx, new_target, "prototype");
    if (JS_IsException(proto)) {
        return JS_EXCEPTION;
    }

    obj = JS_NewObjectProtoClass(cx, proto, NGX_QJS_CLASS_ID_FETCH_REQUEST);
    JS_FreeValue(cx, proto);

    if (JS_IsException(obj)) {
        return JS_EXCEPTION;
    }

    JS_SetOpaque(obj, request);

    return obj;
}


static ngx_int_t
ngx_qjs_request_ctor(JSContext *cx, ngx_js_request_t *request,
    ngx_url_t *u, int argc, JSValueConst *argv)
{
    JSValue            input, init, value;
    ngx_int_t          rc;
    ngx_pool_t        *pool;
    ngx_js_request_t  *orig;

    input = argv[0];
    if (JS_IsUndefined(input)) {
        JS_ThrowInternalError(cx, "1st argument is required");
        return NGX_ERROR;
    }

    /*
     * set by ngx_memzero():
     *
     *  request->url.len = 0;
     *  request->body.length = 0;
     *  request->cache_mode = CACHE_MODE_DEFAULT;
     *  request->credentials = CREDENTIALS_SAME_ORIGIN;
     *  request->mode = MODE_NO_CORS;
     *  request->headers.content_type = NULL;
     */

    ngx_memzero(request, sizeof(ngx_js_request_t));

    request->method.data = (u_char *) "GET";
    request->method.len = 3;
    request->body.data = NULL;
    request->body.len = 0;
    request->headers.guard = GUARD_REQUEST;
    ngx_qjs_arg(request->header_value) = JS_UNDEFINED;

    pool = ngx_qjs_external_pool(cx, JS_GetContextOpaque(cx));

    rc = ngx_list_init(&request->headers.header_list, pool, 4,
                       sizeof(ngx_js_tb_elt_t));
    if (rc != NGX_OK) {
        JS_ThrowOutOfMemory(cx);
        return NGX_ERROR;
    }

    if (JS_IsString(input)) {
        rc = ngx_qjs_string(cx, pool, input, &request->url);
        if (rc != NGX_OK) {
            JS_ThrowInternalError(cx, "failed to convert url arg");
            return NGX_ERROR;
        }

    } else {
        orig = JS_GetOpaque2(cx, input, NGX_QJS_CLASS_ID_FETCH_REQUEST);
        if (orig == NULL) {
            JS_ThrowInternalError(cx,
                                  "input is not string or a Request object");
            return NGX_ERROR;
        }

        request->url = orig->url;
        request->method = orig->method;
        request->body = orig->body;
        request->body_used = orig->body_used;
        request->cache_mode = orig->cache_mode;
        request->credentials = orig->credentials;
        request->mode = orig->mode;

        rc = ngx_qjs_headers_inherit(cx, &request->headers, &orig->headers);
        if (rc != NGX_OK) {
            return NGX_ERROR;
        }
    }

    ngx_js_http_trim(&request->url.data, &request->url.len, 1);

    ngx_memzero(u, sizeof(ngx_url_t));

    u->url = request->url;
    u->default_port = 80;
    u->uri_part = 1;
    u->no_resolve = 1;

    if (u->url.len > 7
        && ngx_strncasecmp(u->url.data, (u_char *) "http://", 7) == 0)
    {
        u->url.len -= 7;
        u->url.data += 7;

#if (NGX_SSL)
    } else if (u->url.len > 8
        && ngx_strncasecmp(u->url.data, (u_char *) "https://", 8) == 0)
    {
        u->url.len -= 8;
        u->url.data += 8;
        u->default_port = 443;
#endif

    } else {
        JS_ThrowInternalError(cx, "unsupported URL schema (only http or https"
                                  " are supported)");
        return NGX_ERROR;
    }

    if (ngx_parse_url(pool, u) != NGX_OK) {
        JS_ThrowInternalError(cx, "invalid url");
        return NGX_ERROR;
    }

    if (JS_IsObject(argv[1])) {
        init = argv[1];
        value = JS_GetPropertyStr(cx, init, "method");
        if (JS_IsException(value)) {
            JS_ThrowInternalError(cx, "invalid Request method");
            return NGX_ERROR;
        }

        if (!JS_IsUndefined(value)) {
            rc = ngx_qjs_string(cx, pool, value, &request->method);
            JS_FreeValue(cx, value);

            if (rc != NGX_OK) {
                JS_ThrowInternalError(cx, "invalid Request method");
                return NGX_ERROR;
            }
        }

        rc = ngx_qjs_method_process(cx, request);
        if (rc != NGX_OK) {
            return NGX_ERROR;
        }

        rc = ngx_qjs_fetch_flag_set(cx, ngx_qjs_fetch_cache_modes, init,
                                    "cache");
        if (rc == NGX_ERROR) {
            return NGX_ERROR;
        }

        request->cache_mode = rc;

        rc = ngx_qjs_fetch_flag_set(cx, ngx_qjs_fetch_credentials, init,
                                    "credentials");
        if (rc == NGX_ERROR) {
            return NGX_ERROR;
        }

        request->credentials = rc;

        rc = ngx_qjs_fetch_flag_set(cx, ngx_qjs_fetch_modes, init, "mode");
        if (rc == NGX_ERROR) {
            return NGX_ERROR;
        }

        request->mode = rc;

        value = JS_GetPropertyStr(cx, init, "headers");
        if (JS_IsException(value)) {
            JS_ThrowInternalError(cx, "invalid Request headers");
            return NGX_ERROR;
        }

        if (!JS_IsUndefined(value)) {
            if (!JS_IsObject(value)) {
                JS_ThrowInternalError(cx, "Headers is not an object");
                return NGX_ERROR;
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
                JS_FreeValue(cx, value);
                JS_ThrowOutOfMemory(cx);
                return NGX_ERROR;
            }

            rc = ngx_qjs_headers_fill(cx, &request->headers, value);
            JS_FreeValue(cx, value);

            if (rc != NGX_OK) {
                return NGX_ERROR;
            }
        }

        value = JS_GetPropertyStr(cx, init, "body");
        if (JS_IsException(value)) {
            JS_ThrowInternalError(cx, "invalid Request body");
            return NGX_ERROR;
        }

        if (!JS_IsUndefined(value)) {
            if (ngx_qjs_string(cx, pool, value, &request->body) != NGX_OK) {
                JS_FreeValue(cx, value);
                JS_ThrowInternalError(cx, "invalid Request body");
                return NGX_ERROR;
            }

            if (request->headers.content_type == NULL && JS_IsString(value)) {
                rc = ngx_qjs_headers_append(cx, &request->headers,
                                        (u_char *) "Content-Type",
                                        sizeof("Content-Type") - 1,
                                        (u_char *) "text/plain;charset=UTF-8",
                                        sizeof("text/plain;charset=UTF-8") - 1);
                if (rc != NGX_OK) {
                    JS_FreeValue(cx, value);
                    return NGX_ERROR;
                }
            }

            JS_FreeValue(cx, value);
        }
    }

    return NGX_OK;
}


static JSValue
ngx_qjs_fetch_response_ctor(JSContext *cx, JSValueConst new_target, int argc,
    JSValueConst *argv)
{
    int                 ret;
    size_t              byte_offset, byte_length;
    u_char             *p, *end;
    JSValue             init, value, body, proto, obj, buf;
    ngx_str_t           bd;
    ngx_int_t           rc;
    const char         *str;
    ngx_pool_t         *pool;
    ngx_js_ctx_t       *ctx;
    ngx_js_response_t  *response;

    pool = ngx_qjs_external_pool(cx, JS_GetContextOpaque(cx));

    response = ngx_pcalloc(pool, sizeof(ngx_js_response_t));
    if (response == NULL) {
        return JS_ThrowOutOfMemory(cx);
    }

    /*
     * set by ngx_pcalloc():
     *
     *  response->url.length = 0;
     *  response->status_text.length = 0;
     */

    response->code = 200;
    response->headers.guard = GUARD_RESPONSE;
    ngx_qjs_arg(response->header_value) = JS_UNDEFINED;

    ret = ngx_list_init(&response->headers.header_list, pool, 4,
                        sizeof(ngx_js_tb_elt_t));
    if (ret != NGX_OK) {
        JS_ThrowOutOfMemory(cx);
    }

    init = argv[1];

    if (JS_IsObject(init)) {
        value = JS_GetPropertyStr(cx, init, "status");
        if (JS_IsException(value)) {
            return JS_ThrowInternalError(cx, "invalid Response status");
        }

        if (!JS_IsUndefined(value)) {
            ret = JS_ToInt64(cx, (int64_t *) &response->code, value);
            JS_FreeValue(cx, value);

            if (ret < 0) {
                return JS_EXCEPTION;
            }

            if (response->code < 200 || response->code > 599) {
                return JS_ThrowInternalError(cx, "status provided (%d) is "
                                                 "outside of [200, 599] range",
                                             (int) response->code);
            }
        }

        value = JS_GetPropertyStr(cx, init, "statusText");
        if (JS_IsException(value)) {
            return JS_ThrowInternalError(cx, "invalid Response statusText");
        }

        if (!JS_IsUndefined(value)) {
            ret = ngx_qjs_string(cx, pool, value, &response->status_text);
            JS_FreeValue(cx, value);

            if (ret < 0) {
                return JS_EXCEPTION;
            }

            p = response->status_text.data;
            end = p + response->status_text.len;

            while (p < end) {
                if (*p != '\t' && *p < ' ') {
                    return JS_ThrowInternalError(cx,
                                                 "invalid Response statusText");
                }

                p++;
            }
        }

        value = JS_GetPropertyStr(cx, init, "headers");
        if (JS_IsException(value)) {
            return JS_ThrowInternalError(cx, "invalid Response headers");
        }

        if (!JS_IsUndefined(value)) {
            if (!JS_IsObject(value)) {
                JS_FreeValue(cx, value);
                return JS_ThrowInternalError(cx, "Headers is not an object");
            }

            rc = ngx_qjs_headers_fill(cx, &response->headers, value);
            JS_FreeValue(cx, value);

            if (ret != NGX_OK) {
                return JS_EXCEPTION;
            }
        }
    }

    ctx = ngx_qjs_external_ctx(cx, JS_GetContextOpaque(cx));

    NJS_CHB_MP_INIT(&response->chain, ctx->engine->pool);

    body = argv[0];

    if (!JS_IsNullOrUndefined(body)) {
        str = NULL;
        if (JS_IsString(body)) {
            goto string;
        }

        buf = JS_GetTypedArrayBuffer(cx, body, &byte_offset, &byte_length, NULL);
        if (!JS_IsException(buf)) {
            bd.data = JS_GetArrayBuffer(cx, &bd.len, buf);

            JS_FreeValue(cx, buf);

            if (bd.data != NULL) {
                bd.data += byte_offset;
                bd.len = byte_length;
            }

        } else {

string:
            str = JS_ToCStringLen(cx, &bd.len, body);
            if (str == NULL) {
                return JS_EXCEPTION;
            }

            bd.data = (u_char *) str;
        }

        njs_chb_append(&response->chain, bd.data, bd.len);

        if (str != NULL) {
            JS_FreeCString(cx, str);
        }

        if (JS_IsString(body)) {
            rc = ngx_qjs_headers_append(cx, &response->headers,
                                      (u_char *) "Content-Type",
                                      sizeof("Content-Type") - 1,
                                      (u_char *) "text/plain;charset=UTF-8",
                                      sizeof("text/plain;charset=UTF-8") - 1);
            if (rc != NGX_OK) {
                return JS_EXCEPTION;
            }
        }
    }

    proto = JS_GetPropertyStr(cx, new_target, "prototype");
    if (JS_IsException(proto)) {
        return JS_EXCEPTION;
    }

    obj = JS_NewObjectProtoClass(cx, proto, NGX_QJS_CLASS_ID_FETCH_RESPONSE);
    JS_FreeValue(cx, proto);

    if (JS_IsException(obj)) {
        return JS_EXCEPTION;
    }

    JS_SetOpaque(obj, response);

    return obj;
}


static u_char
ngx_js_upper_case(u_char c)
{
    return (u_char) ((c >= 'a' && c <= 'z') ? c & 0xDF : c);
}


static ngx_int_t
ngx_qjs_method_process(JSContext *cx, ngx_js_request_t *request)
{
    u_char           *s;
    const u_char     *p;
    const ngx_str_t  *m;

    static const ngx_str_t forbidden[] = {
        ngx_string("CONNECT"),
        ngx_string("TRACE"),
        ngx_string("TRACK"),
        ngx_null_string,
    };

    static const ngx_str_t to_normalize[] = {
        ngx_string("DELETE"),
        ngx_string("GET"),
        ngx_string("HEAD"),
        ngx_string("OPTIONS"),
        ngx_string("POST"),
        ngx_string("PUT"),
        ngx_null_string,
    };

    for (m = &forbidden[0]; m->len != 0; m++) {
        if (request->method.len == m->len
            && ngx_strncasecmp(request->method.data, m->data, m->len) == 0)
        {
            JS_ThrowInternalError(cx, "forbidden method: %.*s",
                                  (int) m->len, m->data);
            return NGX_ERROR;
        }
    }

    for (m = &to_normalize[0]; m->len != 0; m++) {
        if (request->method.len == m->len
            && ngx_strncasecmp(request->method.data, m->data, m->len) == 0)
        {
            s = &request->m[0];
            p = m->data;

            while (*p != '\0') {
                *s++ = ngx_js_upper_case(*p++);
            }

            request->method.data = &request->m[0];
            request->method.len = m->len;
            break;
        }
    }

    return NGX_OK;
}


static ngx_int_t
ngx_qjs_headers_inherit(JSContext *cx, ngx_js_headers_t *headers,
    ngx_js_headers_t *orig)
{
    ngx_int_t         rc;
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

        rc = ngx_qjs_headers_append(cx, headers, h[i].key.data, h[i].key.len,
                                    h[i].value.data, h[i].value.len);
        if (rc != NGX_OK) {
            return NGX_ERROR;
        }
    }

    return NGX_OK;
}


static ngx_int_t
ngx_qjs_headers_fill_header_free(JSContext *cx, ngx_js_headers_t *headers,
    JSValue prop_name, JSValue prop_value)
{
    ngx_int_t   rc;
    ngx_str_t   name, value;
    ngx_pool_t  *pool;

    pool = ngx_qjs_external_pool(cx, JS_GetContextOpaque(cx));

    if (ngx_qjs_string(cx, pool, prop_name, &name) != NGX_OK) {
        JS_FreeValue(cx, prop_name);
        JS_FreeValue(cx, prop_value);
        return NGX_ERROR;
    }

    if (ngx_qjs_string(cx, pool, prop_value, &value) != NGX_OK) {
        JS_FreeValue(cx, prop_name);
        JS_FreeValue(cx, prop_value);
        return NGX_ERROR;
    }

    rc = ngx_qjs_headers_append(cx, headers, name.data, name.len,
                                value.data, value.len);

    JS_FreeValue(cx, prop_name);
    JS_FreeValue(cx, prop_value);

    return rc;
}


static ngx_int_t
ngx_qjs_headers_fill(JSContext *cx, ngx_js_headers_t *headers, JSValue init)
{
    JSValue            header, prop_name, prop_value;
    uint32_t           i, len, length;
    ngx_int_t          rc;
    JSPropertyEnum    *tab;
    ngx_js_headers_t  *hh;

    hh = JS_GetOpaque2(cx, init, NGX_QJS_CLASS_ID_FETCH_HEADERS);
    if (hh != NULL) {
        return ngx_qjs_headers_inherit(cx, headers, hh);
    }

    if (JS_GetOwnPropertyNames(cx, &tab, &len, init,
                               JS_GPN_STRING_MASK | JS_GPN_ENUM_ONLY) < 0) {
        return NGX_ERROR;
    }

    if (qjs_is_array(cx, init)) {
        for (i = 0; i < len; i++) {
            header = JS_GetPropertyUint32(cx, init, i);
            if (JS_IsException(header)) {
                goto fail;
            }

            if (qjs_array_length(cx, header, &length)) {
                JS_FreeValue(cx, header);
                goto fail;
            }

            if (length != 2) {
                JS_FreeValue(cx, header);
                JS_ThrowInternalError(cx,
                                   "header does not contain exactly two items");
                goto fail;
            }

            prop_name = JS_GetPropertyUint32(cx, header, 0);
            prop_value = JS_GetPropertyUint32(cx, header, 1);

            JS_FreeValue(cx, header);

            rc = ngx_qjs_headers_fill_header_free(cx, headers, prop_name,
                                                   prop_value);
            if (rc != NGX_OK) {
                goto fail;
            }
        }

    } else {

        for (i = 0; i < len; i++) {
            prop_name = JS_AtomToString(cx, tab[i].atom);

            prop_value = JS_GetProperty(cx, init, tab[i].atom);
            if (JS_IsException(prop_value)) {
                JS_FreeValue(cx, prop_name);
                goto fail;
            }

            rc = ngx_qjs_headers_fill_header_free(cx, headers, prop_name,
                                                   prop_value);
            if (rc != NGX_OK) {
                goto fail;
            }
        }
    }

    qjs_free_prop_enum(cx, tab, len);

    return NGX_OK;

fail:

    qjs_free_prop_enum(cx, tab, len);

    return NGX_ERROR;
}


static ngx_qjs_fetch_t *
ngx_qjs_fetch_alloc(JSContext *cx, ngx_pool_t *pool, ngx_log_t *log)
{
    ngx_js_ctx_t     *ctx;
    ngx_js_http_t    *http;
    ngx_qjs_fetch_t  *fetch;
    ngx_qjs_event_t  *event;

    fetch = ngx_pcalloc(pool, sizeof(ngx_qjs_fetch_t));
    if (fetch == NULL) {
        return NULL;
    }

    http = &fetch->http;

    http->pool = pool;
    http->log = log;

    http->timeout = 10000;

    http->http_parse.content_length_n = -1;

    ngx_qjs_arg(http->response.header_value) = JS_UNDEFINED;

    http->append_headers = ngx_qjs_fetch_append_headers;
    http->ready_handler = ngx_qjs_fetch_process_done;
    http->error_handler = ngx_qjs_fetch_error;

    fetch->promise = JS_NewPromiseCapability(cx, fetch->promise_callbacks);
    if (JS_IsException(fetch->promise)) {
        return NULL;
    }

    event = ngx_palloc(pool, sizeof(ngx_qjs_event_t));
    if (event == NULL) {
        goto fail;
    }

    ctx = ngx_qjs_external_ctx(cx, JS_GetContextOpaque(cx));

    event->ctx = cx;
    event->destructor = ngx_qjs_fetch_destructor;
    event->fd = ctx->event_id++;
    event->data = fetch;

    ngx_js_add_event(ctx, event);

    fetch->cx = cx;
    fetch->event = event;

    ngx_log_debug1(NGX_LOG_DEBUG_EVENT, log, 0, "js http alloc:%p", fetch);

    return fetch;

fail:

    JS_FreeValue(cx, fetch->promise);
    JS_FreeValue(cx, fetch->promise_callbacks[0]);
    JS_FreeValue(cx, fetch->promise_callbacks[1]);

    JS_ThrowInternalError(cx, "internal error");

    return NULL;
}


static void
ngx_qjs_fetch_error(ngx_js_http_t *http, const char *err)
{
    ngx_qjs_fetch_t  *fetch;

    fetch = (ngx_qjs_fetch_t *) http;

    JS_ThrowInternalError(fetch->cx, "%s", err);

    fetch->response_value = JS_GetException(fetch->cx);

    ngx_qjs_fetch_done(fetch, fetch->response_value, NGX_ERROR);
}


static void
ngx_qjs_fetch_destructor(ngx_qjs_event_t *event)
{
    JSContext        *cx;
    ngx_js_http_t    *http;
    ngx_qjs_fetch_t  *fetch;

    cx = event->ctx;
    fetch = event->data;
    http = &fetch->http;

    ngx_log_debug1(NGX_LOG_DEBUG_EVENT, http->log, 0, "js http destructor:%p",
                   fetch);

    ngx_js_http_resolve_done(http);
    ngx_js_http_close_peer(http);

    JS_FreeValue(cx, fetch->promise_callbacks[0]);
    JS_FreeValue(cx, fetch->promise_callbacks[1]);
    JS_FreeValue(cx, fetch->promise);
    JS_FreeValue(cx, fetch->response_value);
}


static void
ngx_qjs_fetch_done(ngx_qjs_fetch_t *fetch, JSValue retval, ngx_int_t rc)
{
    void             *external;
    JSValue           action;
    JSContext        *cx;
    ngx_js_ctx_t     *ctx;
    ngx_js_http_t    *http;
    ngx_qjs_event_t  *event;

    http = &fetch->http;

    ngx_log_debug2(NGX_LOG_DEBUG_EVENT, http->log, 0,
                   "js http done fetch:%p rc:%i", fetch, rc);

    ngx_js_http_close_peer(http);

    if (fetch->event != NULL) {
        action = fetch->promise_callbacks[(rc != NGX_OK)];

        cx = fetch->cx;
        event = fetch->event;

        rc = ngx_qjs_call(cx, action, &retval, 1);

        external = JS_GetContextOpaque(cx);
        ctx = ngx_qjs_external_ctx(cx, external);
        ngx_js_del_event(ctx, event);

        ngx_qjs_external_event_finalize(cx)(external, rc);
    }
}


static ngx_int_t
ngx_qjs_fetch_append_headers(ngx_js_http_t *http, ngx_js_headers_t *headers,
    u_char *name, size_t len, u_char *value, size_t vlen)
{
    ngx_qjs_fetch_t  *fetch;

    fetch = (ngx_qjs_fetch_t *) http;

    return ngx_qjs_headers_append(fetch->cx, &http->response.headers,
                                  name, len, value, vlen);
}


static void
ngx_qjs_fetch_process_done(ngx_js_http_t *http)
{
    ngx_qjs_fetch_t  *fetch;

    fetch = (ngx_qjs_fetch_t *) http;

    fetch->response_value = JS_NewObjectClass(fetch->cx,
                                              NGX_QJS_CLASS_ID_FETCH_RESPONSE);
    if (JS_IsException(fetch->response_value)) {
        ngx_qjs_fetch_error(http, "fetch response creation failed");
        return;
    }

    JS_SetOpaque(fetch->response_value, &http->response);

    ngx_qjs_fetch_done(fetch, fetch->response_value, NGX_OK);
}


static ngx_int_t
ngx_qjs_headers_append(JSContext *cx, ngx_js_headers_t *headers,
    u_char *name, size_t len, u_char *value, size_t vlen)
{
    u_char           *p, *end;
    ngx_int_t         ret;
    ngx_uint_t        i;
    ngx_list_part_t  *part;
    ngx_js_tb_elt_t  *h, **ph;

    ngx_js_http_trim(&value, &vlen, 0);

    ret = ngx_js_check_header_name(name, len);
    if (ret != NGX_OK) {
        JS_ThrowInternalError(cx, "invalid header name");
        return NGX_ERROR;
    }

    p = value;
    end = p + vlen;

    while (p < end) {
        if (*p == '\0') {
            JS_ThrowInternalError(cx, "invalid header value");
            return NGX_ERROR;
        }

        p++;
    }

    if (headers->guard == GUARD_IMMUTABLE) {
        JS_ThrowInternalError(cx, "cannot append to immutable object");
        return NGX_ERROR;
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
            && (ngx_strncasecmp(name, h[i].key.data, len) == 0))
        {
            ph = &h[i].next;
            while (*ph) { ph = &(*ph)->next; }
            break;
        }
    }

    h = ngx_list_push(&headers->header_list);
    if (h == NULL) {
        JS_ThrowOutOfMemory(cx);
        return NGX_ERROR;
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

    if (len == (sizeof("Content-Type") - 1)
        && ngx_strncasecmp(name, (u_char *) "Content-Type", len) == 0)
    {
        headers->content_type = h;
    }

    return NGX_OK;
}


static JSValue
ngx_qjs_headers_ext_keys(JSContext *cx, JSValue value)
{
    int                ret, found;
    JSValue            keys, key, item, func, retval;
    uint32_t           length;
    ngx_str_t          hdr;
    ngx_uint_t         i, k, n;
    ngx_list_part_t   *part;
    ngx_js_tb_elt_t   *h;
    ngx_js_headers_t  *headers;

    headers = JS_GetOpaque2(cx, value, NGX_QJS_CLASS_ID_FETCH_HEADERS);
    if (headers == NULL) {
        return JS_NULL;
    }

    keys = JS_NewArray(cx);
    if (JS_IsException(keys)) {
        return JS_EXCEPTION;
    }

    n = 0;

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

        if (qjs_array_length(cx, keys, &length)) {
            goto fail;
        }

        for (k = 0; k < length; k++) {
            key = JS_GetPropertyUint32(cx, keys, k);
            if (JS_IsException(key)) {
                goto fail;
            }

            hdr.data = (u_char *) JS_ToCStringLen(cx, &hdr.len, key);
            JS_FreeValue(cx, key);

            found = h[i].key.len == hdr.len
                    && ngx_strncasecmp(h[i].key.data,
                                       hdr.data, hdr.len) == 0;

            JS_FreeCString(cx, (const char *) hdr.data);

            if (found) {
                break;
            }
        }

        if (k == n) {
            item = JS_NewStringLen(cx, (const char *) h[i].key.data,
                                    h[i].key.len);
            if (JS_IsException(value)) {
                goto fail;
            }

            ret = JS_DefinePropertyValueUint32(cx, keys, n, item,
                                               JS_PROP_C_W_E);
            if (ret < 0) {
                JS_FreeValue(cx, item);
                goto fail;
            }

            n++;
        }
    }

    func = JS_GetPropertyStr(cx, keys, "sort");
    if (JS_IsException(func)) {
        JS_ThrowInternalError(cx, "sort function not found");
        goto fail;
    }

    retval = JS_Call(cx, func, keys, 0, NULL);

    JS_FreeValue(cx, func);
    JS_FreeValue(cx, keys);

    return retval;

fail:

    JS_FreeValue(cx, keys);

    return JS_EXCEPTION;
}


static JSValue
ngx_qjs_headers_get(JSContext *cx, JSValue this_val, ngx_str_t *name,
    int as_array)
{
    int                ret;
    JSValue            retval, value;
    njs_chb_t          chain;
    ngx_uint_t         i;
    ngx_list_part_t   *part;
    ngx_js_tb_elt_t   *h, *ph;
    ngx_js_headers_t  *headers;

    headers = JS_GetOpaque2(cx, this_val, NGX_QJS_CLASS_ID_FETCH_HEADERS);
    if (headers == NULL) {
        return JS_NULL;
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

        if (h[i].key.len == name->len
            && ngx_strncasecmp(h[i].key.data, name->data, name->len) == 0)
        {
            ph = &h[i];
            break;
        }
    }

    if (as_array) {
        retval = JS_NewArray(cx);
        if (JS_IsException(retval)) {
            return JS_EXCEPTION;
        }

        i = 0;
        while (ph != NULL) {
            value = JS_NewStringLen(cx, (const char *) ph->value.data,
                                    ph->value.len);
            if (JS_IsException(value)) {
                JS_FreeValue(cx, retval);
                return JS_EXCEPTION;
            }

            ret = JS_DefinePropertyValueUint32(cx, retval, i, value,
                                               JS_PROP_C_W_E);
            if (ret < 0) {
                JS_FreeValue(cx, retval);
                JS_FreeValue(cx, value);
                return JS_EXCEPTION;
            }

            i++;
            ph = ph->next;
        }

        return retval;
    }

    if (ph == NULL) {
        return JS_NULL;
    }

    NJS_CHB_CTX_INIT(&chain, cx);

    h = ph;

    for ( ;; ) {
        njs_chb_append(&chain, h->value.data, h->value.len);

        if (h->next == NULL) {
            break;
        }

        njs_chb_append_literal(&chain, ", ");
        h = h->next;
    }

    retval = qjs_string_create_chb(cx, &chain);

    return retval;
}


static int
ngx_qjs_fetch_headers_own_property(JSContext *cx, JSPropertyDescriptor *desc,
    JSValueConst obj, JSAtom prop)
{
    JSValue    value;
    ngx_str_t  name;

    name.data = (u_char *) JS_AtomToCString(cx, prop);
    if (name.data == NULL) {
        return -1;
    }

    name.len = ngx_strlen(name.data);

    value = ngx_qjs_headers_get(cx, obj, &name, 0);
    JS_FreeCString(cx, (char *) name.data);

    if (JS_IsException(value)) {
        return -1;
    }

    if (JS_IsNull(value)) {
        return 0;
    }

    if (desc == NULL) {
        JS_FreeValue(cx, value);

    } else {
        desc->flags = JS_PROP_ENUMERABLE;
        desc->getter = JS_UNDEFINED;
        desc->setter = JS_UNDEFINED;
        desc->value = value;
    }

    return 1;
}


static int
ngx_qjs_fetch_headers_own_property_names(JSContext *cx, JSPropertyEnum **ptab,
    uint32_t *plen, JSValueConst obj)
{
    int                ret;
    JSAtom             key;
    JSValue            keys;
    ngx_uint_t         i;
    ngx_list_part_t   *part;
    ngx_js_tb_elt_t   *h;
    ngx_js_headers_t  *headers;

    headers = JS_GetOpaque2(cx, obj, NGX_QJS_CLASS_ID_FETCH_HEADERS);
    if (headers == NULL) {
        (void) JS_ThrowInternalError(cx, "\"this\" is not a Headers object");
        return -1;
    }

    keys = JS_NewObject(cx);
    if (JS_IsException(keys)) {
        return -1;
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

        key = JS_NewAtomLen(cx, (const char *) h[i].key.data, h[i].key.len);
        if (key == JS_ATOM_NULL) {
            goto fail;
        }

        if (JS_DefinePropertyValue(cx, keys, key, JS_UNDEFINED,
                                   JS_PROP_ENUMERABLE) < 0)
        {
            JS_FreeAtom(cx, key);
            goto fail;
        }

        JS_FreeAtom(cx, key);
    }

    ret = JS_GetOwnPropertyNames(cx, ptab, plen, keys,
                                 JS_GPN_STRING_MASK | JS_GPN_ENUM_ONLY);

    JS_FreeValue(cx, keys);

    return ret;

fail:

    JS_FreeValue(cx, keys);

    return -1;
}


static JSValue
ngx_qjs_ext_fetch_headers_append(JSContext *cx, JSValueConst this_val,
    int argc, JSValueConst *argv)
{
    ngx_int_t          rc;
    ngx_js_headers_t  *headers;

    headers = JS_GetOpaque2(cx, this_val, NGX_QJS_CLASS_ID_FETCH_HEADERS);
    if (headers == NULL) {
        return JS_ThrowInternalError(cx,
                                     "\"this\" is not fetch headers object");
    }

    rc = ngx_qjs_headers_fill_header_free(cx, headers,
                                          JS_DupValue(cx, argv[0]),
                                          JS_DupValue(cx, argv[1]));
    if (rc != NGX_OK) {
        return JS_EXCEPTION;
    }

    return JS_UNDEFINED;
}


static JSValue
ngx_qjs_ext_fetch_headers_delete(JSContext *cx, JSValueConst this_val,
    int argc, JSValueConst *argv)
{
    ngx_str_t          name;
    ngx_uint_t         i;
    ngx_list_part_t   *part;
    ngx_js_tb_elt_t   *h;
    ngx_js_headers_t  *headers;

    headers = JS_GetOpaque2(cx, this_val, NGX_QJS_CLASS_ID_FETCH_HEADERS);
    if (headers == NULL) {
        return JS_ThrowInternalError(cx,
                                     "\"this\" is not fetch headers object");
    }

    name.data = (u_char *) JS_ToCStringLen(cx, &name.len, argv[0]);
    if (name.data == NULL) {
        return JS_EXCEPTION;
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

        if (name.len == h[i].key.len
            && (ngx_strncasecmp(name.data, h[i].key.data, name.len) == 0))
        {
            h[i].hash = 0;
        }
    }

    if (name.len == (sizeof("Content-Type") - 1)
        && ngx_strncasecmp(name.data, (u_char *) "Content-Type", name.len)
           == 0)
    {
        headers->content_type = NULL;
    }

    JS_FreeCString(cx, (const char *) name.data);

    return JS_UNDEFINED;
}


static JSValue
ngx_qjs_ext_fetch_headers_foreach(JSContext *cx, JSValueConst this_val,
    int argc, JSValueConst *argv)
{
    JSValue            callback, keys, key;
    JSValue            header, retval, arguments[2];
    uint32_t           length;;
    ngx_str_t          name;
    ngx_uint_t         i;
    ngx_js_headers_t  *headers;

    headers = JS_GetOpaque2(cx, this_val, NGX_QJS_CLASS_ID_FETCH_HEADERS);
    if (headers == NULL) {
        return JS_ThrowInternalError(cx,
                                     "\"this\" is not fetch headers object");
    }

    callback = argv[0];

    if (!JS_IsFunction(cx, callback)) {
        return JS_ThrowInternalError(cx, "\"callback\" is not a function");
    }

    keys = ngx_qjs_headers_ext_keys(cx, this_val);
    if (JS_IsException(keys)) {
        return JS_EXCEPTION;
    }

    if (qjs_array_length(cx, keys, &length)) {
        goto fail;
    }

    for (i = 0; i < length; i++) {
        key = JS_GetPropertyUint32(cx, keys, i);
        if (JS_IsException(key)) {
            goto fail;
        }

        name.data = (u_char *) JS_ToCStringLen(cx, &name.len, key);
        if (name.data == NULL) {
            JS_FreeValue(cx, key);
            goto fail;
        }

        header = ngx_qjs_headers_get(cx, this_val, &name, 0);
        JS_FreeCString(cx, (char *) name.data);
        if (JS_IsException(header)) {
            JS_FreeValue(cx, key);
            goto fail;
        }

        arguments[0] = key;
        arguments[1] = header;

        retval = JS_Call(cx, callback, JS_UNDEFINED, 2, arguments);

        JS_FreeValue(cx, key);
        JS_FreeValue(cx, header);
        JS_FreeValue(cx, retval);
    }

    JS_FreeValue(cx, keys);

    return JS_UNDEFINED;

fail:

    JS_FreeValue(cx, keys);

    return JS_EXCEPTION;
}


static JSValue
ngx_qjs_ext_fetch_headers_get(JSContext *cx, JSValueConst this_val,
    int argc, JSValueConst *argv, int magic)
{
    JSValue    ret;
    ngx_str_t  name;

    name.data = (u_char *) JS_ToCStringLen(cx, &name.len, argv[0]);
    if (name.data == NULL) {
        return JS_EXCEPTION;
    }

    ret = ngx_qjs_headers_get(cx, this_val, &name, magic);

    JS_FreeCString(cx, (char *) name.data);

    return ret;
}


static JSValue
ngx_qjs_ext_fetch_headers_has(JSContext *cx, JSValueConst this_val,
    int argc, JSValueConst *argv)
{
    JSValue    retval;
    ngx_int_t  rc;
    ngx_str_t  name;

    name.data = (u_char *) JS_ToCStringLen(cx, &name.len, argv[0]);
    if (name.data == NULL) {
        return JS_EXCEPTION;
    }

    retval = ngx_qjs_headers_get(cx, this_val, &name, 0);
    JS_FreeCString(cx, (char *) name.data);
    if (JS_IsException(retval)) {
        return JS_EXCEPTION;
    }

    rc = !JS_IsNull(retval);
    JS_FreeValue(cx, retval);

    return JS_NewBool(cx, rc);
}


static JSValue
ngx_qjs_ext_fetch_headers_set(JSContext *cx, JSValueConst this_val,
    int argc, JSValueConst *argv)
{
    ngx_int_t          rc;
    ngx_str_t          name, value;
    ngx_uint_t         i;
    ngx_pool_t        *pool;
    ngx_list_part_t   *part;
    ngx_js_tb_elt_t   *h, **ph, **pp;
    ngx_js_headers_t  *headers;

    headers = JS_GetOpaque2(cx, this_val, NGX_QJS_CLASS_ID_FETCH_HEADERS);
    if (headers == NULL) {
        return JS_ThrowInternalError(cx,
                                     "\"this\" is not fetch headers object");
    }

    name.data = (u_char *) JS_ToCStringLen(cx, &name.len, argv[0]);
    if (name.data == NULL) {
        return JS_EXCEPTION;
    }

    pool = ngx_qjs_external_pool(cx, JS_GetContextOpaque(cx));

    rc = ngx_qjs_string(cx, pool, argv[1], &value);
    if (rc != NGX_OK) {
        JS_FreeCString(cx, (const char *) name.data);
        return JS_EXCEPTION;
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

        if (name.len == h[i].key.len
            && (ngx_strncasecmp(name.data, h[i].key.data, name.len) == 0))
        {
            h[i].value.len = value.len;
            h[i].value.data = value.data;

            ph = &h[i].next;

            while (*ph) {
                pp = ph;
                ph = &(*ph)->next;
                *pp = NULL;
            }

            JS_FreeCString(cx, (const char *) name.data);

            return JS_UNDEFINED;
        }
    }

    rc = ngx_qjs_headers_append(cx, headers, name.data, name.len,
                                 value.data, value.len);
    JS_FreeCString(cx, (const char *) name.data);
    if (rc != NGX_OK) {
        return JS_EXCEPTION;
    }

    return JS_UNDEFINED;
}


static JSValue
ngx_qjs_ext_fetch_request_body(JSContext *cx, JSValueConst this_val,
    int argc, JSValueConst *argv, int magic)
{
    char *             string;
    JSValue            result;
    ngx_js_request_t  *request;

    request = JS_GetOpaque2(cx, this_val, NGX_QJS_CLASS_ID_FETCH_REQUEST);
    if (request == NULL) {
        return JS_UNDEFINED;
    }

    if (request->body_used) {
        return JS_ThrowInternalError(cx, "body stream already read");
    }

    request->body_used = 1;

    switch (magic) {
    case NGX_QJS_BODY_ARRAY_BUFFER:
        /*
         * no free_func for JS_NewArrayBuffer()
         * because request->body is allocated from e->pool
         * and will be freed when context is freed.
         */
        result = JS_NewArrayBuffer(cx, request->body.data, request->body.len,
                                   NULL, NULL, 0);
        if (JS_IsException(result)) {
            return JS_ThrowOutOfMemory(cx);
        }

        break;

    case NGX_QJS_BODY_JSON:
    case NGX_QJS_BODY_TEXT:
    default:
        result = qjs_string_create(cx, request->body.data, request->body.len);
        if (JS_IsException(result)) {
            return JS_ThrowOutOfMemory(cx);
        }

        if (magic == NGX_QJS_BODY_JSON) {
            string = js_malloc(cx, request->body.len + 1);

            JS_FreeValue(cx, result);
            result = JS_UNDEFINED;

            if (string == NULL) {
                return JS_ThrowOutOfMemory(cx);
            }

            ngx_memcpy(string, request->body.data, request->body.len);
            string[request->body.len] = '\0';

            /* 'string' must be zero terminated. */
            result = JS_ParseJSON(cx, string, request->body.len, "<input>");
            js_free(cx, string);
            if (JS_IsException(result)) {
                break;
            }
        }
    }

    return qjs_promise_result(cx, result);
}


static JSValue
ngx_qjs_ext_fetch_request_body_used(JSContext *cx, JSValueConst this_val)
{
    ngx_js_request_t  *request;

    request = JS_GetOpaque2(cx, this_val, NGX_QJS_CLASS_ID_FETCH_REQUEST);
    if (request == NULL) {
        return JS_UNDEFINED;
    }

    return JS_NewBool(cx, request->body_used);
}


static JSValue
ngx_qjs_ext_fetch_request_cache(JSContext *cx, JSValueConst this_val)
{
    ngx_js_request_t  *request;

    request = JS_GetOpaque2(cx, this_val, NGX_QJS_CLASS_ID_FETCH_REQUEST);
    if (request == NULL) {
        return JS_UNDEFINED;
    }

    return ngx_qjs_fetch_flag(cx, ngx_qjs_fetch_cache_modes,
                              request->cache_mode);
}


static JSValue
ngx_qjs_ext_fetch_request_credentials(JSContext *cx, JSValueConst this_val)
{
    ngx_js_request_t  *request;

    request = JS_GetOpaque2(cx, this_val, NGX_QJS_CLASS_ID_FETCH_REQUEST);
    if (request == NULL) {
        return JS_UNDEFINED;
    }

    return ngx_qjs_fetch_flag(cx, ngx_qjs_fetch_credentials,
                              request->credentials);
}


static JSValue
ngx_qjs_ext_fetch_request_headers(JSContext *cx, JSValueConst this_val)
{
    JSValue           header;
    ngx_js_request_t  *request;

    request = JS_GetOpaque2(cx, this_val, NGX_QJS_CLASS_ID_FETCH_REQUEST);
    if (request == NULL) {
        return JS_UNDEFINED;
    }

    header = ngx_qjs_arg(request->header_value);

    if (JS_IsUndefined(header)) {
        header = JS_NewObjectClass(cx, NGX_QJS_CLASS_ID_FETCH_HEADERS);
        if (JS_IsException(header)) {
            return JS_ThrowInternalError(cx, "fetch header creation failed");
        }

        JS_SetOpaque(header, &request->headers);

        ngx_qjs_arg(request->header_value) = header;
    }

    return JS_DupValue(cx, header);
}


static JSValue
ngx_qjs_ext_fetch_request_field(JSContext *cx, JSValueConst this_val, int magic)
{
    ngx_str_t         *field;
    ngx_js_request_t  *request;

    request = JS_GetOpaque2(cx, this_val, NGX_QJS_CLASS_ID_FETCH_REQUEST);
    if (request == NULL) {
        return JS_UNDEFINED;
    }

    field = (ngx_str_t *) ((u_char *) request + magic);

    return qjs_string_create(cx, field->data, field->len);
}


static JSValue
ngx_qjs_ext_fetch_request_mode(JSContext *cx, JSValueConst this_val)
{
    ngx_js_request_t  *request;

    request = JS_GetOpaque2(cx, this_val, NGX_QJS_CLASS_ID_FETCH_REQUEST);
    if (request == NULL) {
        return JS_UNDEFINED;
    }

    return ngx_qjs_fetch_flag(cx, ngx_qjs_fetch_modes, request->mode);
}


static void
ngx_qjs_fetch_request_finalizer(JSRuntime *rt, JSValue val)
{
    ngx_js_request_t  *request;

    request = JS_GetOpaque(val, NGX_QJS_CLASS_ID_FETCH_REQUEST);

    JS_FreeValueRT(rt, ngx_qjs_arg(request->header_value));
}


static JSValue
ngx_qjs_ext_fetch_response_status(JSContext *cx, JSValueConst this_val)
{
    ngx_js_response_t  *response;

    response = JS_GetOpaque2(cx, this_val, NGX_QJS_CLASS_ID_FETCH_RESPONSE);
    if (response == NULL) {
        return JS_UNDEFINED;
    }

    return JS_NewUint32(cx, response->code);
}


static JSValue
ngx_qjs_ext_fetch_response_status_text(JSContext *cx, JSValueConst this_val)
{
    ngx_js_response_t  *response;

    response = JS_GetOpaque2(cx, this_val, NGX_QJS_CLASS_ID_FETCH_RESPONSE);
    if (response == NULL) {
        return JS_UNDEFINED;
    }

    return qjs_string_create(cx, response->status_text.data,
                             response->status_text.len);
}


static JSValue
ngx_qjs_ext_fetch_response_ok(JSContext *cx, JSValueConst this_val)
{
    ngx_js_response_t  *response;

    response = JS_GetOpaque2(cx, this_val, NGX_QJS_CLASS_ID_FETCH_RESPONSE);
    if (response == NULL) {
        return JS_UNDEFINED;
    }

    return JS_NewBool(cx, response->code >= 200 && response->code < 300);
}


static JSValue
ngx_qjs_ext_fetch_response_body_used(JSContext *cx, JSValueConst this_val)
{
    ngx_js_response_t  *response;

    response = JS_GetOpaque2(cx, this_val, NGX_QJS_CLASS_ID_FETCH_RESPONSE);
    if (response == NULL) {
        return JS_UNDEFINED;
    }

    return JS_NewBool(cx, response->body_used);
}


static JSValue
ngx_qjs_ext_fetch_response_headers(JSContext *cx, JSValueConst this_val)
{
    JSValue            header;
    ngx_js_response_t  *response;

    response = JS_GetOpaque2(cx, this_val, NGX_QJS_CLASS_ID_FETCH_RESPONSE);
    if (response == NULL) {
        return JS_UNDEFINED;
    }

    header = ngx_qjs_arg(response->header_value);

    if (JS_IsUndefined(header)) {
        header = JS_NewObjectClass(cx, NGX_QJS_CLASS_ID_FETCH_HEADERS);
        if (JS_IsException(header)) {
            return JS_ThrowInternalError(cx, "fetch header creation failed");
        }

        JS_SetOpaque(header, &response->headers);

        ngx_qjs_arg(response->header_value) = header;
    }

    return JS_DupValue(cx, header);
}


static JSValue
ngx_qjs_ext_fetch_response_type(JSContext *cx, JSValueConst this_val)
{
    ngx_js_response_t  *response;

    response = JS_GetOpaque2(cx, this_val, NGX_QJS_CLASS_ID_FETCH_RESPONSE);
    if (response == NULL) {
        return JS_UNDEFINED;
    }

    return JS_NewString(cx, "basic");
}


static JSValue
ngx_qjs_ext_fetch_response_body(JSContext *cx, JSValueConst this_val,
    int argc, JSValueConst *argv, int magic)
{
    JSValue             result;
    njs_int_t           ret;
    njs_str_t           string;
    ngx_js_response_t  *response;

    response = JS_GetOpaque2(cx, this_val, NGX_QJS_CLASS_ID_FETCH_RESPONSE);
    if (response == NULL) {
        return JS_UNDEFINED;
    }

    if (response->body_used) {
        return JS_ThrowInternalError(cx, "body stream already read");
    }

    response->body_used = 1;

    switch (magic) {
    case NGX_QJS_BODY_ARRAY_BUFFER:
    case NGX_QJS_BODY_TEXT:
        ret = njs_chb_join(&response->chain, &string);
        if (ret != NJS_OK) {
            return JS_ThrowOutOfMemory(cx);
        }

        if (magic == NGX_QJS_BODY_TEXT) {
            result = qjs_string_create(cx, string.start, string.length);
            if (JS_IsException(result)) {
                return JS_ThrowOutOfMemory(cx);
            }

            break;
        }

        /*
         * no free_func for JS_NewArrayBuffer()
         * because string.start is allocated from e->pool
         * and will be freed when context is freed.
         */
        result = JS_NewArrayBuffer(cx, string.start, string.length, NULL, NULL,
                                   0);
        if (JS_IsException(result)) {
            return JS_ThrowOutOfMemory(cx);
        }

        break;

    case NGX_QJS_BODY_JSON:
    default:
        /* 'string.start' must be zero terminated. */
        njs_chb_append_literal(&response->chain, "\0");
        ret = njs_chb_join(&response->chain, &string);
        if (ret != NJS_OK) {
            return JS_ThrowOutOfMemory(cx);
        }

        result = JS_ParseJSON(cx, (char *) string.start, string.length - 1,
                              "<input>");
        if (JS_IsException(result)) {
            break;
        }
    }

    return qjs_promise_result(cx, result);
}


static JSValue
ngx_qjs_ext_fetch_response_redirected(JSContext *cx, JSValueConst this_val)
{
    ngx_js_response_t  *response;

    response = JS_GetOpaque2(cx, this_val, NGX_QJS_CLASS_ID_FETCH_RESPONSE);
    if (response == NULL) {
        return JS_UNDEFINED;
    }

    return JS_NewBool(cx, 0);
}


static JSValue
ngx_qjs_ext_fetch_response_field(JSContext *cx, JSValueConst this_val, int magic)
{
    ngx_str_t          *field;
    ngx_js_response_t  *response;

    response = JS_GetOpaque2(cx, this_val, NGX_QJS_CLASS_ID_FETCH_RESPONSE);
    if (response == NULL) {
        return JS_UNDEFINED;
    }

    field = (ngx_str_t *) ((u_char *) response + magic);

    return qjs_string_create(cx, field->data, field->len);
}


static void
ngx_qjs_fetch_response_finalizer(JSRuntime *rt, JSValue val)
{
    ngx_js_response_t  *response;

    response = JS_GetOpaque(val, NGX_QJS_CLASS_ID_FETCH_RESPONSE);

    JS_FreeValueRT(rt, ngx_qjs_arg(response->header_value));
    njs_chb_destroy(&response->chain);
}


static JSValue
ngx_qjs_fetch_flag(JSContext *cx, const ngx_qjs_entry_t *entries,
    ngx_int_t value)
{
    const ngx_qjs_entry_t  *e;

    for (e = entries; e->name.len != 0; e++) {
        if (e->value == value) {
            return qjs_string_create(cx, e->name.data, e->name.len);
        }
    }

    return JS_ThrowInternalError(cx, "unknown fetch flag: %i", (int) value);
}


static ngx_int_t
ngx_qjs_fetch_flag_set(JSContext *cx, const ngx_qjs_entry_t *entries,
     JSValue object, const char *prop)
{
    JSValue                value;
    ngx_str_t              flag;
    const ngx_qjs_entry_t  *e;

    value = JS_GetPropertyStr(cx, object, prop);
    if (JS_IsException(value)) {
        JS_ThrowInternalError(cx, "failed to get %s property", prop);
        return NGX_ERROR;
    }

    if (JS_IsUndefined(value)) {
        return entries[0].value;
    }

    flag.data = (u_char *) JS_ToCStringLen(cx, &flag.len, value);
    JS_FreeValue(cx, value);
    if (flag.data == NULL) {
        return NGX_ERROR;
    }

    for (e = entries; e->name.len != 0; e++) {
        if (flag.len == e->name.len
            && ngx_strncasecmp(e->name.data, flag.data, flag.len) == 0)
        {
            JS_FreeCString(cx, (const char *) flag.data);
            return e->value;
        }
    }

    JS_ThrowInternalError(cx, "unknown %s type: %.*s", prop,
                          (int) flag.len, flag.data);

    JS_FreeCString(cx, (const char *) flag.data);

    return NGX_ERROR;
}


static JSModuleDef *
ngx_qjs_fetch_init(JSContext *cx, const char *name)
{
    int      i, class_id;
    JSValue  global_obj, proto, class;

    static const JSClassDef  *const classes[] = {
        &ngx_qjs_fetch_headers_class,
        &ngx_qjs_fetch_request_class,
        &ngx_qjs_fetch_response_class,
        NULL
    };

    static JSCFunction  *ctors[] = {
        ngx_qjs_fetch_headers_ctor,
        ngx_qjs_fetch_request_ctor,
        ngx_qjs_fetch_response_ctor,
        NULL
    };

    static const JSCFunctionListEntry *const  protos[] = {
        ngx_qjs_ext_fetch_headers_proto,
        ngx_qjs_ext_fetch_request_proto,
        ngx_qjs_ext_fetch_response_proto,
        NULL
    };

    static const uint8_t  proto_sizes[] = {
        njs_nitems(ngx_qjs_ext_fetch_headers_proto),
        njs_nitems(ngx_qjs_ext_fetch_request_proto),
        njs_nitems(ngx_qjs_ext_fetch_response_proto),
        0
    };

    global_obj = JS_GetGlobalObject(cx);

    for (i = 0; classes[i] != NULL; i++) {
        class_id = NGX_QJS_CLASS_ID_FETCH_HEADERS + i;

        if (JS_NewClass(JS_GetRuntime(cx), class_id, classes[i]) < 0) {
            return NULL;
        }

        proto = JS_NewObject(cx);
        if (JS_IsException(proto)) {
            JS_FreeValue(cx, global_obj);
            return NULL;
        }

        JS_SetPropertyFunctionList(cx, proto, protos[i], proto_sizes[i]);

        class = JS_NewCFunction2(cx, ctors[i], classes[i]->class_name, 2,
                                 JS_CFUNC_constructor, 0);
        if (JS_IsException(class)) {
            JS_FreeValue(cx, proto);
            JS_FreeValue(cx, global_obj);
            return NULL;
        }

        JS_SetConstructor(cx, class, proto);
        JS_SetClassProto(cx, class_id, proto);

        if (JS_SetPropertyStr(cx, global_obj, classes[i]->class_name, class)
            < 0)
        {
            JS_FreeValue(cx, class);
            JS_FreeValue(cx, proto);
            JS_FreeValue(cx, global_obj);
            return NULL;
        }
    }

    JS_FreeValue(cx, global_obj);

    return JS_NewCModule(cx, name, NULL);
}
