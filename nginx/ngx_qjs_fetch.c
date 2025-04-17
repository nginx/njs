
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
} ngx_qjs_entry_t;


typedef struct {
    ngx_js_http_t                 http;

    JSContext                     *cx;
    ngx_qjs_event_t               *event;

    JSValue                        response_value;

    JSValue                        promise;
    JSValue                        promise_callbacks[2];
} ngx_qjs_fetch_t;


static njs_int_t ngx_qjs_headers_fill_header_free(JSContext *cx,
    ngx_js_headers_t *headers, JSValue prop_name, JSValue prop_value);
static njs_int_t ngx_qjs_headers_append(JSContext *cx,
    ngx_js_headers_t *headers, u_char *name, size_t len, u_char *value,
    size_t vlen);
static JSValue ngx_qjs_headers_get(JSContext *cx, JSValue this_val,
    ngx_str_t *name, njs_bool_t as_array);
static JSValue ngx_qjs_fetch_flag(JSContext *cx, const ngx_qjs_entry_t *entries,
    njs_int_t value);
static njs_int_t ngx_qjs_request_constructor(JSContext *cx,
    ngx_js_request_t *request, ngx_url_t *u, int argc, JSValueConst *argv);
static void ngx_qjs_fetch_done(ngx_qjs_fetch_t *fetch, JSValue retval,
    njs_int_t rc);

static ngx_int_t ngx_qjs_fetch_append_headers(ngx_js_http_t *http,
    ngx_js_headers_t *headers, u_char *name, size_t len, u_char *value,
    size_t vlen);
static void ngx_qjs_fetch_process_done(ngx_js_http_t *http);


static const ngx_qjs_entry_t  ngx_qjs_fetch_cache_modes[] = {
    { njs_str("default"), CACHE_MODE_DEFAULT },
    { njs_str("no-store"), CACHE_MODE_NO_STORE },
    { njs_str("reload"), CACHE_MODE_RELOAD },
    { njs_str("no-cache"), CACHE_MODE_NO_CACHE },
    { njs_str("force-cache"), CACHE_MODE_FORCE_CACHE },
    { njs_str("only-if-cached"), CACHE_MODE_ONLY_IF_CACHED },
    { njs_null_str, 0 },
};

static const ngx_qjs_entry_t  ngx_qjs_fetch_credentials[] = {
    { njs_str("same-origin"), CREDENTIALS_SAME_ORIGIN },
    { njs_str("omit"), CREDENTIALS_OMIT },
    { njs_str("include"), CREDENTIALS_INCLUDE },
    { njs_null_str, 0 },
};

static const ngx_qjs_entry_t  ngx_qjs_fetch_modes[] = {
    { njs_str("no-cors"), MODE_NO_CORS },
    { njs_str("cors"), MODE_CORS },
    { njs_str("same-origin"), MODE_SAME_ORIGIN },
    { njs_str("navigate"), MODE_NAVIGATE },
    { njs_str("websocket"), MODE_WEBSOCKET },
    { njs_null_str, 0 },
};


#define NGX_QJS_BODY_ARRAY_BUFFER   0
#define NGX_QJS_BODY_JSON           1
#define NGX_QJS_BODY_TEXT           2


static void
njs_qjs_fetch_destructor(ngx_qjs_event_t *event)
{
    JSContext        *cx;
    ngx_js_http_t    *http;
    ngx_qjs_fetch_t  *fetch;

    cx = event->ctx;
    fetch = event->data;
    http = &fetch->http;

    ngx_log_debug1(NGX_LOG_DEBUG_EVENT, http->log, 0, "qjs fetch destructor:%p",
                   fetch);

    ngx_js_http_resolve_done(http);
    ngx_js_http_close_peer(http);

    /* TODO */
    //njs_chb_destroy(&http->chain);

    JS_FreeValue(cx, fetch->promise_callbacks[0]);
    JS_FreeValue(cx, fetch->promise_callbacks[1]);
    JS_FreeValue(cx, fetch->promise);
    JS_FreeValue(cx, fetch->response_value);
}


static void
ngx_qjs_fetch_error(ngx_js_http_t *http, const char *err)
{
    ngx_qjs_fetch_t  *fetch;

    fetch = (ngx_qjs_fetch_t *) http;

    JS_ThrowInternalError(fetch->cx, "%s", err);

    fetch->response_value = JS_GetException(fetch->cx);

    ngx_qjs_fetch_done(fetch, fetch->response_value, NJS_ERROR);
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

    http->response.u.qjs_headers = JS_UNDEFINED;

    http->append_headers = ngx_qjs_fetch_append_headers;
    http->ready_handler = ngx_qjs_fetch_process_done;
    http->error_handler = ngx_qjs_fetch_error;

    fetch->promise = JS_NewPromiseCapability(cx, fetch->promise_callbacks);
    if (JS_IsException(fetch->promise)) {
        return NULL;
    }

    event = ngx_palloc(pool, sizeof(ngx_qjs_event_t));
    if (njs_slow_path(event == NULL)) {
        goto fail;
    }

    ctx = ngx_qjs_external_ctx(cx, JS_GetContextOpaque(cx));

    event->ctx = cx;
    event->destructor = njs_qjs_fetch_destructor;
    event->fd = ctx->event_id++;
    event->data = fetch;

    ngx_js_add_event(ctx, event);

    fetch->cx = cx;
    fetch->event = event;

    ngx_log_debug1(NGX_LOG_DEBUG_EVENT, log, 0, "qjs fetch alloc:%p", fetch);

    return fetch;

fail:

    JS_FreeValue(cx, fetch->promise);
    JS_FreeValue(cx, fetch->promise_callbacks[0]);
    JS_FreeValue(cx, fetch->promise_callbacks[1]);

    JS_ThrowInternalError(cx, "internal error");

    return NULL;
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

    ngx_qjs_fetch_done(fetch, fetch->response_value, NJS_OK);
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
ngx_qjs_fetch_done(ngx_qjs_fetch_t *fetch, JSValue retval, njs_int_t rc)
{
    void             *external;
    JSValue           action;
    JSContext        *cx;
    ngx_js_ctx_t     *ctx;
    ngx_js_http_t    *http;
    ngx_qjs_event_t  *event;

    http = &fetch->http;

    ngx_log_debug2(NGX_LOG_DEBUG_EVENT, http->log, 0,
                   "qjs fetch done fetch:%p rc:%i", fetch, (ngx_int_t) rc);

    ngx_js_http_close_peer(http);

    if (fetch->event != NULL) {
        action = fetch->promise_callbacks[(rc != NJS_OK)];

        cx = fetch->cx;
        event = fetch->event;

        rc = ngx_qjs_call(cx, action, &retval, 1);

        external = JS_GetContextOpaque(cx);
        ctx = ngx_qjs_external_ctx(cx, external);
        ngx_js_del_event(ctx, event);

        ngx_qjs_external_event_finalize(cx)(external, rc);
    }
}


JSValue
ngx_qjs_ext_fetch(JSContext *cx, JSValueConst this_val, int argc,
    JSValueConst *argv)
{
    void                *external;
    JSValue              init, value;
    njs_int_t            ret;
    njs_str_t            str;
    ngx_url_t            u;
    ngx_uint_t           i;
    njs_bool_t           has_host;
    ngx_pool_t          *pool;
    ngx_js_http_t       *http;
    ngx_qjs_fetch_t     *fetch;
    ngx_list_part_t     *part;
    ngx_js_tb_elt_t     *h;
    ngx_connection_t    *c;
    ngx_js_request_t     request;
    ngx_resolver_ctx_t  *ctx;

    external = JS_GetContextOpaque(cx);
    c = ngx_qjs_external_connection(cx, external);
    pool = ngx_qjs_external_pool(cx, external);

    fetch = ngx_qjs_fetch_alloc(cx, pool, c->log);
    if (fetch == NULL) {
        return JS_ThrowOutOfMemory(cx);
    }

    http = &fetch->http;

    ret = ngx_qjs_request_constructor(cx, &request, &u, argc, argv);
    if (ret != NJS_OK) {
        goto fail;
    }

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

    if (argc > 1 && JS_IsObject(argv[1])) {
        init = argv[1];
        value = JS_GetPropertyStr(cx, init, "buffer_size");
        if (JS_IsException(value)) {
            goto fail;
        }

        if (!JS_IsUndefined(value)) {
            ret = JS_ToInt64(cx, &http->buffer_size, value);
            JS_FreeValue(cx, value);

            if (ret < 0) {
                goto fail;
            }
        }

        value = JS_GetPropertyStr(cx, init, "max_response_body_size");
        if (JS_IsException(value)) {
            goto fail;
        }

        if (!JS_IsUndefined(value)) {
            ret = JS_ToInt64(cx, &http->max_response_body_size, value);
            JS_FreeValue(cx, value);

            if (ret < 0) {
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

    str.start = request.method.data;
    str.length = request.method.len;

    http->header_only = njs_strstr_eq(&str, &njs_str_value("HEAD"));

    ngx_js_ctx_t  *js_ctx;
    ngx_engine_t  *e;

    js_ctx = ngx_qjs_external_ctx(cx, JS_GetContextOpaque(cx));
    e = js_ctx->engine;

    NJS_CHB_MP_INIT2(&http->chain, e->pool);
    NJS_CHB_MP_INIT2(&http->response.chain, e->pool);

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
        ctx = ngx_js_http_resolve(http, ngx_qjs_external_resolver(cx, external),
                                  &u.host, u.port,
                               ngx_qjs_external_resolver_timeout(cx, external));
        if (ctx == NULL) {
            return JS_ThrowOutOfMemory(cx);
        }

        if (ctx == NGX_NO_RESOLVER) {
            JS_ThrowInternalError(cx, "no resolver defined");
            goto fail;
        }

        return JS_DupValue(cx, fetch->promise);
    }

    http->naddrs = 1;
    ngx_memcpy(&http->addr, &u.addrs[0], sizeof(ngx_addr_t));
    http->addrs = &http->addr;

    ngx_js_http_connect(http);

    return JS_DupValue(cx, fetch->promise);

fail:

    fetch->response_value = JS_GetException(cx);

    ngx_qjs_fetch_done(fetch, fetch->response_value, NJS_ERROR);

    return JS_DupValue(cx, fetch->promise);
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

    ret = JS_GetOwnPropertyNames(cx, ptab, plen, keys, JS_GPN_STRING_MASK | JS_GPN_ENUM_ONLY);

    JS_FreeValue(cx, keys);

    return ret;

fail:

    JS_FreeValue(cx, keys);

    return -1;
}


static void
ngx_qjs_fetch_request_finalizer(JSRuntime *rt, JSValue val)
{
    ngx_js_request_t  *request;

    request = JS_GetOpaque(val, NGX_QJS_CLASS_ID_FETCH_REQUEST);

    JS_FreeValueRT(rt, request->u.qjs_headers);
}


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


static void
ngx_qjs_fetch_response_finalizer(JSRuntime *rt, JSValue val)
{
    ngx_js_response_t  *response;

    response = JS_GetOpaque(val, NGX_QJS_CLASS_ID_FETCH_RESPONSE);

    /* TODO */
    //njs_chb_destroy(&response->chain);

    JS_FreeValueRT(rt, response->u.qjs_headers);
}

static const JSClassDef  ngx_qjs_fetch_response_class = {
    "Response",
    .finalizer = ngx_qjs_fetch_response_finalizer,
};


static JSValue
ngx_qjs_headers_get(JSContext *cx, JSValue this_val, ngx_str_t *name,
    njs_bool_t as_array)
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
            && njs_strncasecmp(h[i].key.data, name->data, name->len) == 0)
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


static JSValue
ngx_qjs_ext_fetch_headers_get(JSContext *cx, JSValueConst this_val,
    int argc, JSValueConst *argv, int magic)
{
    njs_int_t  ret;
    ngx_str_t  name;

    ret = ngx_qjs_string(cx, argv[0], &name);
    if (ret != NJS_OK) {
        return JS_EXCEPTION;
    }

    return ngx_qjs_headers_get(cx, this_val, &name, magic);
}


static JSValue
ngx_qjs_ext_fetch_headers_has(JSContext *cx, JSValueConst this_val,
    int argc, JSValueConst *argv)
{
    JSValue    retval;
    njs_int_t  ret;
    ngx_str_t  name;

    ret = ngx_qjs_string(cx, argv[0], &name);
    if (ret != NJS_OK) {
        return JS_EXCEPTION;
    }

    retval = ngx_qjs_headers_get(cx, this_val, &name, 0);
    if (JS_IsException(retval)) {
        return JS_EXCEPTION;
    }

    ret = !JS_IsNull(retval);
    JS_FreeValue(cx, retval);

    return JS_NewBool(cx, ret);
}


static JSValue
ngx_qjs_ext_fetch_headers_set(JSContext *cx, JSValueConst this_val,
    int argc, JSValueConst *argv)
{
    njs_int_t          ret;
    ngx_str_t          name, value;
    ngx_uint_t         i;
    ngx_list_part_t   *part;
    ngx_js_tb_elt_t   *h, **ph, **pp;
    ngx_js_headers_t  *headers;

    headers = JS_GetOpaque2(cx, this_val, NGX_QJS_CLASS_ID_FETCH_HEADERS);
    if (headers == NULL) {
        return JS_ThrowInternalError(cx,
                                     "\"this\" is not fetch headers object");
    }

    ret = ngx_qjs_string(cx, argv[0], &name);
    if (ret != NJS_OK) {
        return JS_EXCEPTION;
    }

    ret = ngx_qjs_string(cx, argv[1], &value);
    if (ret != NJS_OK) {
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
            && (njs_strncasecmp(name.data, h[i].key.data, name.len) == 0))
        {
            h[i].value.len = value.len;
            h[i].value.data = value.data;

            ph = &h[i].next;

            while (*ph) {
                pp = ph;
                ph = &(*ph)->next;
                *pp = NULL;
            }

            return JS_UNDEFINED;
        }
    }

    ret = ngx_qjs_headers_append(cx, headers, name.data, name.len,
                                 value.data, value.len);
    if (ret != NJS_OK) {
        return JS_EXCEPTION;
    }

    return JS_UNDEFINED;
}


static JSValue
ngx_qjs_ext_fetch_headers_append(JSContext *cx, JSValueConst this_val,
    int argc, JSValueConst *argv)
{
    njs_int_t          ret;
    ngx_js_headers_t  *headers;

    headers = JS_GetOpaque2(cx, this_val, NGX_QJS_CLASS_ID_FETCH_HEADERS);
    if (headers == NULL) {
        return JS_ThrowInternalError(cx,
                                     "\"this\" is not fetch headers object");
    }

    ret = ngx_qjs_headers_fill_header_free(cx, headers,
                                           JS_DupValue(cx, argv[0]),
                                           JS_DupValue(cx, argv[1]));
    if (ret != NJS_OK) {
        return JS_EXCEPTION;
    }

    return JS_UNDEFINED;
}


static JSValue
ngx_qjs_ext_fetch_headers_delete(JSContext *cx, JSValueConst this_val,
    int argc, JSValueConst *argv)
{
    njs_int_t          ret;
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

    ret = ngx_qjs_string(cx, argv[0], &name);
    if (ret != NJS_OK) {
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
            && (njs_strncasecmp(name.data, h[i].key.data, name.len) == 0))
        {
            h[i].hash = 0;
        }
    }

    if (name.len == njs_strlen("Content-Type")
        && ngx_strncasecmp(name.data, (u_char *) "Content-Type", name.len)
           == 0)
    {
        headers->content_type = NULL;
    }

    return JS_UNDEFINED;
}


static JSValue
ngx_qjs_headers_ext_keys(JSContext *cx, JSValue value)
{
    int                ret;
    uint32_t           length;
    JSValue            keys, key, item, func, retval;
    njs_str_t          hdr;
    njs_bool_t         found;
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

        if (ngx_qjs_array_length(cx, &length, keys)) {
            goto fail;
        }

        for (k = 0; k < length; k++) {
            key = JS_GetPropertyUint32(cx, keys, k);
            if (JS_IsException(key)) {
                goto fail;
            }

            hdr.start = (u_char *) JS_ToCStringLen(cx, &hdr.length, key);
            JS_FreeValue(cx, key);

            found = h[i].key.len == hdr.length
                    && njs_strncasecmp(h[i].key.data,
                                       hdr.start, hdr.length) == 0;

            JS_FreeCString(cx, (const char *) hdr.start);

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

    retval = JS_Call(cx, func, keys, 0, NULL);

    JS_FreeValue(cx, func);
    JS_FreeValue(cx, keys);

    return retval;

fail:

    JS_FreeValue(cx, keys);

    return JS_EXCEPTION;
}


static JSValue
ngx_qjs_ext_fetch_headers_foreach(JSContext *cx, JSValueConst this_val,
    int argc, JSValueConst *argv)
{
    int                ret;
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

    if (ngx_qjs_array_length(cx, &length, keys)) {
        goto fail;
    }

    for (i = 0; i < length; i++) {
        key = JS_GetPropertyUint32(cx, keys, i);
        if (JS_IsException(key)) {
            goto fail;
        }

        ret = ngx_qjs_string(cx, key, &name);
        if (ret != NJS_OK) {
            JS_FreeValue(cx, key);
            goto fail;
        }

        header = ngx_qjs_headers_get(cx, this_val, &name, 0);
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
ngx_qjs_ext_fetch_request_headers(JSContext *cx, JSValueConst this_val)
{
    ngx_js_request_t  *request;

    request = JS_GetOpaque2(cx, this_val, NGX_QJS_CLASS_ID_FETCH_REQUEST);
    if (request == NULL) {
        return JS_UNDEFINED;
    }

    if (JS_IsUndefined(request->u.qjs_headers)) {
        request->u.qjs_headers = JS_NewObjectClass(cx,
                                                NGX_QJS_CLASS_ID_FETCH_HEADERS);
        if (JS_IsException(request->u.qjs_headers)) {
            return JS_ThrowInternalError(cx, "fetch header creation failed");
        }

        JS_SetOpaque(request->u.qjs_headers, &request->headers);
    }

    return JS_DupValue(cx, request->u.qjs_headers);
}


static JSValue
ngx_qjs_ext_fetch_request_bodyused(JSContext *cx, JSValueConst this_val)
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
                              (njs_int_t) request->cache_mode);
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
                              (njs_int_t) request->credentials);
}


static JSValue
ngx_qjs_ext_fetch_request_mode(JSContext *cx, JSValueConst this_val)
{
    ngx_js_request_t  *request;

    request = JS_GetOpaque2(cx, this_val, NGX_QJS_CLASS_ID_FETCH_REQUEST);
    if (request == NULL) {
        return JS_UNDEFINED;
    }

    return ngx_qjs_fetch_flag(cx, ngx_qjs_fetch_modes,
                              (njs_int_t) request->mode);
}


static JSValue
ngx_qjs_ext_fetch_request_body(JSContext *cx, JSValueConst this_val,
    int argc, JSValueConst *argv, int magic)
{
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

    case NGX_QJS_BODY_TEXT:
    default:
        result = qjs_string_create(cx, request->body.data, request->body.len);
        if (JS_IsException(result)) {
            return JS_ThrowOutOfMemory(cx);
        }        
    }

    return qjs_promise_result(cx, result);
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

    ret = njs_chb_join(&response->chain, &string);
    if (ret != NJS_OK) {
        return JS_ThrowOutOfMemory(cx);
    }

    switch (magic) {
    case NGX_QJS_BODY_ARRAY_BUFFER:
        result = qjs_new_array_buffer_copy(cx, string.start, string.length);
        if (JS_IsException(result)) {
            return JS_ThrowOutOfMemory(cx);
        }

        break;

    case NGX_QJS_BODY_JSON:
    case NGX_QJS_BODY_TEXT:
    default:
        result = qjs_string_create(cx, string.start, string.length);
        if (JS_IsException(result)) {
            //response->chain.free(cx, string.start);
            return JS_ThrowOutOfMemory(cx);
        }

        //response->chain.free(cx, string.start);

        if (magic == NGX_QJS_BODY_JSON) {
            string.start = (u_char *) JS_ToCStringLen(cx, &string.length,
                                                      result);

            JS_FreeValue(cx, result);
            result = JS_UNDEFINED;

            if (string.start == NULL) {
                JS_FreeCString(cx, (const char *) string.start);
                ret = NJS_ERROR;
                break;
            }

            result = JS_ParseJSON(cx, (const char *) string.start,
                                  string.length, "<input>");
            JS_FreeCString(cx, (const char *) string.start);
            if (JS_IsException(result)) {
                ret = NJS_ERROR;
                break;
            }
        }
    }

    return qjs_promise_result(cx, result);
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
ngx_qjs_ext_fetch_response_headers(JSContext *cx, JSValueConst this_val)
{
    ngx_js_response_t  *response;

    response = JS_GetOpaque2(cx, this_val, NGX_QJS_CLASS_ID_FETCH_RESPONSE);
    if (response == NULL) {
        return JS_UNDEFINED;
    }

    if (JS_IsUndefined(response->u.qjs_headers)) {
        response->u.qjs_headers = JS_NewObjectClass(cx,
                                                NGX_QJS_CLASS_ID_FETCH_HEADERS);
        if (JS_IsException(response->u.qjs_headers)) {
            return JS_ThrowInternalError(cx, "fetch header creation failed");
        }

        JS_SetOpaque(response->u.qjs_headers, &response->headers);
    }

    return JS_DupValue(cx, response->u.qjs_headers);
}


static JSValue
ngx_qjs_ext_fetch_response_bodyused(JSContext *cx, JSValueConst this_val)
{
    ngx_js_response_t  *response;

    response = JS_GetOpaque2(cx, this_val, NGX_QJS_CLASS_ID_FETCH_RESPONSE);
    if (response == NULL) {
        return JS_UNDEFINED;
    }

    return JS_NewBool(cx, response->body_used);
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
ngx_qjs_ext_fetch_response_redirected(JSContext *cx, JSValueConst this_val)
{
    ngx_js_response_t  *response;

    response = JS_GetOpaque2(cx, this_val, NGX_QJS_CLASS_ID_FETCH_RESPONSE);
    if (response == NULL) {
        return JS_UNDEFINED;
    }

    return JS_NewBool(cx, 0);
}


static const JSCFunctionListEntry  ngx_qjs_ext_fetch_headers_funcs[] = {
    JS_CFUNC_MAGIC_DEF("get", 1, ngx_qjs_ext_fetch_headers_get, 0),
    JS_CFUNC_MAGIC_DEF("getAll", 1, ngx_qjs_ext_fetch_headers_get, 1),
    JS_CFUNC_DEF("has", 1, ngx_qjs_ext_fetch_headers_has),
    JS_CFUNC_DEF("set", 2, ngx_qjs_ext_fetch_headers_set),
    JS_CFUNC_DEF("append", 2, ngx_qjs_ext_fetch_headers_append),
    JS_CFUNC_DEF("delete", 1, ngx_qjs_ext_fetch_headers_delete),
    JS_CFUNC_DEF("forEach", 1, ngx_qjs_ext_fetch_headers_foreach),
};

static const JSCFunctionListEntry  ngx_qjs_ext_fetch_request_funcs[] = {
    JS_CGETSET_MAGIC_DEF("url", ngx_qjs_ext_fetch_request_field, NULL,
                         offsetof(ngx_js_request_t, url) ),
    JS_CGETSET_MAGIC_DEF("method", ngx_qjs_ext_fetch_request_field, NULL,
                         offsetof(ngx_js_request_t, method) ),
    JS_CGETSET_DEF("headers", ngx_qjs_ext_fetch_request_headers, NULL ),
    JS_CGETSET_DEF("bodyUsed", ngx_qjs_ext_fetch_request_bodyused, NULL),
    JS_CGETSET_DEF("cache", ngx_qjs_ext_fetch_request_cache, NULL),
    JS_CGETSET_DEF("credentials", ngx_qjs_ext_fetch_request_credentials, NULL),
    JS_CGETSET_DEF("mode", ngx_qjs_ext_fetch_request_mode, NULL),
    JS_CFUNC_MAGIC_DEF("text", 1, ngx_qjs_ext_fetch_request_body,
                       NGX_QJS_BODY_TEXT),
};

static const JSCFunctionListEntry  ngx_qjs_ext_fetch_response_funcs[] = {
    JS_CGETSET_MAGIC_DEF("url", ngx_qjs_ext_fetch_response_field, NULL,
                         offsetof(ngx_js_response_t, url) ),
    JS_CFUNC_MAGIC_DEF("arrayBuffer", 1, ngx_qjs_ext_fetch_response_body,
                       NGX_QJS_BODY_ARRAY_BUFFER),
    JS_CFUNC_MAGIC_DEF("text", 1, ngx_qjs_ext_fetch_response_body,
                       NGX_QJS_BODY_TEXT),
    JS_CFUNC_MAGIC_DEF("json", 1, ngx_qjs_ext_fetch_response_body,
                       NGX_QJS_BODY_JSON),
    JS_CGETSET_DEF("status", ngx_qjs_ext_fetch_response_status, NULL),
    JS_CGETSET_DEF("statusText", ngx_qjs_ext_fetch_response_status_text, NULL),
    JS_CGETSET_DEF("headers", ngx_qjs_ext_fetch_response_headers, NULL ),
    JS_CGETSET_DEF("bodyUsed", ngx_qjs_ext_fetch_response_bodyused, NULL),
    JS_CGETSET_DEF("ok", ngx_qjs_ext_fetch_response_ok, NULL),
    JS_CGETSET_DEF("type", ngx_qjs_ext_fetch_response_type, NULL),
    JS_CGETSET_DEF("redirected", ngx_qjs_ext_fetch_response_redirected, NULL),
};


static njs_int_t
ngx_qjs_headers_append(JSContext *cx, ngx_js_headers_t *headers,
    u_char *name, size_t len, u_char *value, size_t vlen)
{
    u_char           *p, *end;
    ngx_uint_t        i;
    ngx_list_part_t  *part;
    ngx_js_tb_elt_t  *h, **ph;

    ngx_js_http_trim(&value, &vlen, 0);

    p = name;
    end = p + len;

    while (p < end) {
        if (!njs_is_token(*p)) {
            JS_ThrowInternalError(cx, "invalid header name");
            return NJS_ERROR;
        }

        p++;
    }

    p = value;
    end = p + vlen;

    while (p < end) {
        if (*p == '\0') {
            JS_ThrowInternalError(cx, "invalid header value");
            return NJS_ERROR;
        }

        p++;
    }

    if (headers->guard == GUARD_IMMUTABLE) {
        JS_ThrowInternalError(cx, "cannot append to immutable object");
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
        JS_ThrowOutOfMemory(cx);
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
ngx_qjs_headers_fill_header_free(JSContext *cx, ngx_js_headers_t *headers,
    JSValue prop_name, JSValue prop_value)
{
    int        ret;
    ngx_str_t  name, value;

    if (ngx_qjs_string(cx, prop_name, &name) != NGX_OK) {
        JS_FreeValue(cx, prop_name);
        JS_FreeValue(cx, prop_value);
        return NJS_ERROR;
    }

    if (ngx_qjs_string(cx, prop_value, &value) != NGX_OK) {
        JS_FreeValue(cx, prop_name);
        JS_FreeValue(cx, prop_value);
        return NJS_ERROR;
    }

    ret = ngx_qjs_headers_append(cx, headers, name.data, name.len,
                                 value.data, value.len);

    JS_FreeValue(cx, prop_name);
    JS_FreeValue(cx, prop_value);

    return ret;
}


static njs_int_t
ngx_qjs_headers_inherit(JSContext *cx, ngx_js_headers_t *headers,
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

        ret = ngx_qjs_headers_append(cx, headers, h[i].key.data, h[i].key.len,
                                     h[i].value.data, h[i].value.len);
        if (ret != NJS_OK) {
            return NJS_ERROR;
        }
    }

    return NJS_OK;
}


static njs_int_t
ngx_qjs_headers_fill(JSContext *cx, ngx_js_headers_t *headers, JSValue init)
{
    int                ret;
    JSValue            header, prop_name, prop_value;
    uint32_t           i, len, length;
    JSPropertyEnum    *tab;
    ngx_js_headers_t  *hh;

    hh = JS_GetOpaque2(cx, init, NGX_QJS_CLASS_ID_FETCH_HEADERS);
    if (hh != NULL) {
        return ngx_qjs_headers_inherit(cx, headers, hh);
    }

    if (JS_GetOwnPropertyNames(cx, &tab, &len, init,
                               JS_GPN_STRING_MASK | JS_GPN_ENUM_ONLY) < 0) {
        return NJS_ERROR;
    }

    if (qjs_is_array(cx, init)) {

        for (i = 0; i < len; i++) {
            header = JS_GetPropertyUint32(cx, init, i);
            if (JS_IsException(header)) {
                goto fail;
            }

            if (ngx_qjs_array_length(cx, &length, header)) {
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

            ret = ngx_qjs_headers_fill_header_free(cx, headers, prop_name,
                                                   prop_value);
            if (ret != NJS_OK) {
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

            ret = ngx_qjs_headers_fill_header_free(cx, headers, prop_name,
                                                   prop_value);
            if (ret != NJS_OK) {
                goto fail;
            }
        }
    }

    qjs_free_prop_enum(cx, tab, len);

    return NJS_OK;

fail:

    qjs_free_prop_enum(cx, tab, len);

    return NJS_ERROR;
}


static JSValue
ngx_qjs_fetch_headers_ctor(JSContext *cx, JSValueConst new_target, int argc,
    JSValueConst *argv)
{
    JSValue            init, proto, obj;
    ngx_int_t          rc;
    njs_int_t          ret;
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
        ret = ngx_qjs_headers_fill(cx, headers, init);
        if (ret != NJS_OK) {
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


static njs_int_t
ngx_qjs_method_process(JSContext *cx, ngx_js_request_t *request)
{
    u_char           *s, *p;
    njs_str_t         method;
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

    method.start = request->method.data;
    method.length = request->method.len;

    for (m = &forbidden[0]; m->length != 0; m++) {
        if (njs_strstr_case_eq(&method, m)) {
            JS_ThrowInternalError(cx, "forbidden method: %.*s",
                                  (int) m->length, m->start);
            return NJS_ERROR;
        }
    }

    for (m = &to_normalize[0]; m->length != 0; m++) {
        if (njs_strstr_case_eq(&method, m)) {
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
ngx_qjs_fetch_flag_set(JSContext *cx, const ngx_qjs_entry_t *entries,
     JSValue value, const char *type)
{
    njs_int_t               ret;
    njs_str_t               str;
    ngx_str_t               flag;
    const ngx_qjs_entry_t  *e;

    ret = ngx_qjs_string(cx, value, &flag);
    if (ret != NJS_OK) {
        return NJS_ERROR;
    }

    str.start = flag.data;
    str.length = flag.len;

    for (e = entries; e->name.length != 0; e++) {
        if (njs_strstr_case_eq(&str, &e->name)) {
            return e->value;
        }
    }

    JS_ThrowInternalError(cx, "unknown %s type: %.*s",
                          type, (int) flag.len, flag.data);

    return NJS_ERROR;
}


static JSValue
ngx_qjs_fetch_flag(JSContext *cx, const ngx_qjs_entry_t *entries,
    njs_int_t value)
{
    const ngx_qjs_entry_t  *e;

    for (e = entries; e->name.length != 0; e++) {
        if (e->value == value) {
            return qjs_string_create(cx, e->name.start, e->name.length);
        }
    }

    return JS_EXCEPTION;
}


static njs_int_t
ngx_qjs_request_constructor(JSContext *cx, ngx_js_request_t *request,
    ngx_url_t *u, int argc, JSValueConst *argv)
{
    JSValue            input, init, value;
    njs_int_t          ret;
    ngx_uint_t         rc;
    ngx_pool_t        *pool;
    ngx_js_request_t  *orig;

    input = argv[0];
    if (JS_IsUndefined(input)) {
        JS_ThrowInternalError(cx, "1st argument is required");
        return NJS_ERROR;
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
    request->u.qjs_headers = JS_UNDEFINED;

    pool = ngx_qjs_external_pool(cx, JS_GetContextOpaque(cx));

    rc = ngx_list_init(&request->headers.header_list, pool, 4,
                       sizeof(ngx_js_tb_elt_t));
    if (rc != NGX_OK) {
        JS_ThrowOutOfMemory(cx);
        return NJS_ERROR;
    }

    if (JS_IsString(input)) {
        ret = ngx_qjs_string(cx, input, &request->url);
        if (ret != NJS_OK) {
            JS_ThrowInternalError(cx, "failed to convert url arg");
            return NJS_ERROR;
        }

    } else {
        orig = JS_GetOpaque2(cx, input, NGX_QJS_CLASS_ID_FETCH_REQUEST);
        if (orig == NULL) {
            JS_ThrowInternalError(cx,
                                  "input is not string or a Request object");
            return NJS_ERROR;
        }

        request->url = orig->url;
        request->method = orig->method;
        request->body = orig->body;
        request->body_used = orig->body_used;
        request->cache_mode = orig->cache_mode;
        request->credentials = orig->credentials;
        request->mode = orig->mode;

        ret = ngx_qjs_headers_inherit(cx, &request->headers, &orig->headers);
        if (ret != NJS_OK) {
            return NJS_ERROR;
        }
    }

    ngx_js_http_trim(&request->url.data, &request->url.len, 1);

    ngx_memzero(u, sizeof(ngx_url_t));

    u->url = request->url;
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
        JS_ThrowInternalError(cx, "unsupported URL schema (only http or https"
                                  " are supported)");
        return NJS_ERROR;
    }

    if (ngx_parse_url(pool, u) != NGX_OK) {
        JS_ThrowInternalError(cx, "invalid url");
        return NJS_ERROR;
    }

    if (JS_IsObject(argv[1])) {
        init = argv[1];
        value = JS_GetPropertyStr(cx, init, "method");
        if (JS_IsException(value)) {
            JS_ThrowInternalError(cx, "invalid Request method");
            return NJS_ERROR;
        }

        if (!JS_IsUndefined(value)) {
            ret = ngx_qjs_string(cx, value, &request->method);
            JS_FreeValue(cx, value);

            if (ret != NJS_OK) {
                JS_ThrowInternalError(cx, "invalid Request method");
                return NJS_ERROR;
            }
        }

        ret = ngx_qjs_method_process(cx, request);
        if (ret != NJS_OK) {
            return NJS_ERROR;
        }

        value = JS_GetPropertyStr(cx, init, "cache");
        if (JS_IsException(value)) {
            JS_ThrowInternalError(cx, "invalid Request cache");
            return NJS_ERROR;
        }

        if (!JS_IsUndefined(value)) {
            ret = ngx_qjs_fetch_flag_set(cx, ngx_qjs_fetch_cache_modes, value,
                                         "cache");
            JS_FreeValue(cx, value);

            if (ret == NJS_ERROR) {
                return NJS_ERROR;
            }

            request->cache_mode = ret;
        }

        value = JS_GetPropertyStr(cx, init, "credentials");
        if (JS_IsException(value)) {
            JS_ThrowInternalError(cx, "invalid Request credentials");
            return NJS_ERROR;
        }

        if (!JS_IsUndefined(value)) {
            ret = ngx_qjs_fetch_flag_set(cx, ngx_qjs_fetch_credentials, value,
                                         "credentials");
            JS_FreeValue(cx, value);

            if (ret == NJS_ERROR) {
                return NJS_ERROR;
            }

            request->credentials = ret;
        }

        value = JS_GetPropertyStr(cx, init, "mode");
        if (JS_IsException(value)) {
            JS_ThrowInternalError(cx, "invalid Request mode");
            return NJS_ERROR;
        }

        if (!JS_IsUndefined(value)) {
            ret = ngx_qjs_fetch_flag_set(cx, ngx_qjs_fetch_modes, value,
                                         "mode");
            JS_FreeValue(cx, value);

            if (ret == NJS_ERROR) {
                return NJS_ERROR;
            }

            request->mode = ret;
        }

        value = JS_GetPropertyStr(cx, init, "headers");
        if (JS_IsException(value)) {
            JS_ThrowInternalError(cx, "invalid Request headers");
            return NJS_ERROR;
        }

        if (!JS_IsUndefined(value)) {

            if (!JS_IsObject(value)) {
                JS_ThrowInternalError(cx, "Headers is not an object");
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
                JS_FreeValue(cx, value);
                JS_ThrowOutOfMemory(cx);
                return NJS_ERROR;
            }

            ret = ngx_qjs_headers_fill(cx, &request->headers, value);
            JS_FreeValue(cx, value);

            if (ret != NJS_OK) {
                return NJS_ERROR;
            }
        }

        value = JS_GetPropertyStr(cx, init, "body");
        if (JS_IsException(value)) {
            JS_ThrowInternalError(cx, "invalid Request body");
            return NJS_ERROR;
        }

        if (!JS_IsUndefined(value)) {

            if (ngx_qjs_string(cx, value, &request->body) != NGX_OK) {
                JS_FreeValue(cx, value);
                JS_ThrowInternalError(cx, "invalid Request body");
                return NJS_ERROR;
            }

            if (request->headers.content_type == NULL && JS_IsString(value)) {
                ret = ngx_qjs_headers_append(cx, &request->headers,
                                        (u_char *) "Content-Type",
                                        njs_length("Content-Type"),
                                        (u_char *) "text/plain;charset=UTF-8",
                                        njs_length("text/plain;charset=UTF-8"));
                if (ret != NJS_OK) {
                    JS_FreeValue(cx, value);
                    return NJS_ERROR;
                }
            }

            JS_FreeValue(cx, value);
        }
    }

    return NJS_OK;    
}


static JSValue
ngx_qjs_fetch_request_ctor(JSContext *cx, JSValueConst new_target, int argc,
    JSValueConst *argv)
{
    JSValue            proto, obj;
    njs_int_t          ret;
    ngx_url_t          u;
    ngx_pool_t        *pool;
    ngx_js_request_t  *request;

    pool = ngx_qjs_external_pool(cx, JS_GetContextOpaque(cx));

    request = ngx_pcalloc(pool, sizeof(ngx_js_request_t));
    if (request == NULL) {
        return JS_ThrowOutOfMemory(cx);
    }

    ret = ngx_qjs_request_constructor(cx, request, &u, argc, argv);
    if (ret != NJS_OK) {
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


static JSValue
ngx_qjs_fetch_response_ctor(JSContext *cx, JSValueConst new_target, int argc,
    JSValueConst *argv)
{
    int                 ret;
    u_char              *p, *end;
    JSValue             init, value, body, proto, obj;
    ngx_str_t           bd;
    ngx_pool_t         *pool;
    ngx_js_response_t  *response;

    pool = ngx_qjs_external_pool(cx, JS_GetContextOpaque(cx));

    response = ngx_pcalloc(pool, sizeof(ngx_js_response_t));
    if (response == NULL) {
        return JS_ThrowOutOfMemory(cx);
    }

    /*
     * set by njs_mp_zalloc():
     *
     *  response->url.length = 0;
     *  response->status_text.length = 0;
     */

    response->code = 200;
    response->headers.guard = GUARD_RESPONSE;
    response->u.qjs_headers = JS_UNDEFINED;

    pool = ngx_qjs_external_pool(cx, JS_GetContextOpaque(cx));

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
            ret = JS_ToInt64(cx, &response->code, value);
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
            ret = ngx_qjs_string(cx, value, &response->status_text);
            JS_FreeValue(cx, value);

            if (ret < 0) {
                return JS_EXCEPTION;
            }

            p = response->status_text.data;
            end = p + response->status_text.len;

            while (p < end) {
                if (*p != '\t' && *p < ' ') {
                    return JS_ThrowInternalError(cx, "invalid Response statusText");
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

            ret = ngx_qjs_headers_fill(cx, &response->headers, value);
            JS_FreeValue(cx, value);

            if (ret != NJS_OK) {
                return JS_EXCEPTION;
            }
        }
    }

    NJS_CHB_CTX_INIT(&response->chain, cx);

    body = argv[0];

    if (!JS_IsNullOrUndefined(body)) {
        if (ngx_qjs_string(cx, body, &bd) != NGX_OK) {
            return JS_ThrowInternalError(cx, "invalid Response body");
        }

        njs_chb_append(&response->chain, bd.data, bd.len);

        if (JS_IsString(body)) {
            ret = ngx_qjs_headers_append(cx, &response->headers,
                                    (u_char *) "Content-Type",
                                    njs_length("Content-Type"),
                                    (u_char *) "text/plain;charset=UTF-8",
                                    njs_length("text/plain;charset=UTF-8"));
            if (ret != NJS_OK) {
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


static const JSClassDef  *const ngx_qjs_fetch_class_ptr[3] = {
    &ngx_qjs_fetch_headers_class,
    &ngx_qjs_fetch_request_class,
    &ngx_qjs_fetch_response_class,
};

static JSCFunction  *ngx_qjs_fetch_class_ctor_ptr[3] = {
    ngx_qjs_fetch_headers_ctor,
    ngx_qjs_fetch_request_ctor,
    ngx_qjs_fetch_response_ctor,
};

static const JSCFunctionListEntry *const  ngx_qjs_fetch_proto_funcs_ptr[6] = {
    ngx_qjs_ext_fetch_headers_funcs,
    ngx_qjs_ext_fetch_request_funcs,
    ngx_qjs_ext_fetch_response_funcs,
};

static const uint8_t  ngx_qjs_fetch_proto_funcs_count[3] = {
    njs_nitems(ngx_qjs_ext_fetch_headers_funcs),
    njs_nitems(ngx_qjs_ext_fetch_request_funcs),
    njs_nitems(ngx_qjs_ext_fetch_response_funcs),
};


static JSModuleDef *
ngx_qjs_fetch_init(JSContext *cx, const char *name)
{
    int      i, class_id;
    JSValue  global_obj, proto, class;

    global_obj = JS_GetGlobalObject(cx);

    for (i = 0; i < 3; i++) {
        class_id = NGX_QJS_CLASS_ID_FETCH_HEADERS + i;
        JS_NewClass(JS_GetRuntime(cx), class_id, ngx_qjs_fetch_class_ptr[i]);

        proto = JS_NewObject(cx);
        if (JS_IsException(proto)) {
            JS_FreeValue(cx, global_obj);
            return NULL;
        }

        JS_SetPropertyFunctionList(cx, proto,
                                   ngx_qjs_fetch_proto_funcs_ptr[i],
                                   ngx_qjs_fetch_proto_funcs_count[i]);

        class = JS_NewCFunction2(cx, ngx_qjs_fetch_class_ctor_ptr[i],
                                 ngx_qjs_fetch_class_ptr[i]->class_name, 2,
                                 JS_CFUNC_constructor, 0);

        JS_SetConstructor(cx, class, proto);
        JS_SetClassProto(cx, class_id, proto);

        JS_SetPropertyStr(cx, global_obj,
                          ngx_qjs_fetch_class_ptr[i]->class_name, class);
    }

    JS_FreeValue(cx, global_obj);

    return JS_NewCModule(cx, name, NULL);
}


qjs_module_t  ngx_qjs_ngx_fetch_module = {
    .name = "fetch",
    .init = ngx_qjs_fetch_init,
};
