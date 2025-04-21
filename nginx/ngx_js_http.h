
/*
 * Copyright (C) Dmitry Volyntsev
 * Copyright (C) hongzhidao
 * Copyright (C) Antoine Bonavita
 * Copyright (C) NGINX, Inc.
 */


#ifndef _NGX_JS_HTTP_H_INCLUDED_
#define _NGX_JS_HTTP_H_INCLUDED_


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
    ngx_str_t                      url;
    ngx_str_t                      method;
    u_char                         m[8];
    uint8_t                        body_used;
    ngx_str_t                      body;
    ngx_js_headers_t               headers;
    njs_opaque_value_t             header_value;
} ngx_js_request_t;


typedef struct {
    ngx_str_t                      url;
    ngx_int_t                      code;
    ngx_str_t                      status_text;
    uint8_t                        body_used;
    njs_chb_t                      chain;
    ngx_js_headers_t               headers;
    njs_opaque_value_t             header_value;
} ngx_js_response_t;


struct ngx_js_http_s {
    ngx_log_t                     *log;
    ngx_pool_t                    *pool;

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

    uint8_t                        done;
    ngx_js_http_parse_t            http_parse;
    ngx_js_http_chunk_parse_t      http_chunk_parse;
    ngx_int_t                    (*process)(ngx_js_http_t *http);
    ngx_int_t                    (*append_headers)(ngx_js_http_t *http,
                                                   ngx_js_headers_t *headers,
                                                   u_char *name, size_t len,
                                                   u_char *value, size_t vlen);
    void                         (*ready_handler)(ngx_js_http_t *http);
    void                         (*error_handler)(ngx_js_http_t *http,
                                                  const char *err);
};


ngx_resolver_ctx_t *ngx_js_http_resolve(ngx_js_http_t *http, ngx_resolver_t *r,
    ngx_str_t *host, in_port_t port, ngx_msec_t timeout);
void ngx_js_http_connect(ngx_js_http_t *http);
void ngx_js_http_resolve_done(ngx_js_http_t *http);
void ngx_js_http_close_peer(ngx_js_http_t *http);
void ngx_js_http_trim(u_char **value, size_t *len,
    int trim_c0_control_or_space);
ngx_int_t ngx_js_check_header_name(u_char *name, size_t len);


#endif /* _NGX_JS_HTTP_H_INCLUDED_ */
