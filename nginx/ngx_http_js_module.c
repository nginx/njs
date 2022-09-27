
/*
 * Copyright (C) Roman Arutyunyan
 * Copyright (C) Dmitry Volyntsev
 * Copyright (C) NGINX, Inc.
 */


#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>
#include "ngx_js.h"


typedef struct {
    NGX_JS_COMMON_CONF;

    ngx_str_t              content;
    ngx_str_t              header_filter;
    ngx_str_t              body_filter;
    ngx_uint_t             buffer_type;
} ngx_http_js_loc_conf_t;


#define NJS_HEADER_SEMICOLON   0x1
#define NJS_HEADER_SINGLE      0x2
#define NJS_HEADER_ARRAY       0x4


typedef struct {
    njs_vm_t              *vm;
    ngx_log_t             *log;
    ngx_uint_t             done;
    ngx_int_t              status;
    njs_opaque_value_t     retval;
    njs_opaque_value_t     request;
    njs_opaque_value_t     args;
    njs_opaque_value_t     request_body;
    njs_opaque_value_t     response_body;
    ngx_str_t              redirect_uri;
    ngx_array_t            promise_callbacks;

    ngx_int_t              filter;
    ngx_buf_t             *buf;
    ngx_chain_t          **last_out;
    ngx_chain_t           *free;
    ngx_chain_t           *busy;
} ngx_http_js_ctx_t;


typedef struct {
    ngx_http_request_t    *request;
    njs_opaque_value_t     callbacks[2];
} ngx_http_js_cb_t;


typedef struct {
    ngx_http_request_t    *request;
    njs_vm_event_t         vm_event;
    void                  *unused;
    ngx_int_t              ident;
} ngx_http_js_event_t;


typedef struct {
    njs_str_t              name;
#if defined(nginx_version) && (nginx_version >= 1023000)
    unsigned               flags;
    njs_int_t            (*handler)(njs_vm_t *vm, ngx_http_request_t *r,
                                    unsigned flags, njs_str_t *name,
                                    njs_value_t *setval, njs_value_t *retval);
#else
    njs_int_t            (*handler)(njs_vm_t *vm, ngx_http_request_t *r,
                                    ngx_list_t *headers, njs_str_t *name,
                                    njs_value_t *setval, njs_value_t *retval);

#endif
}  ngx_http_js_header_t;


static ngx_int_t ngx_http_js_content_handler(ngx_http_request_t *r);
static void ngx_http_js_content_event_handler(ngx_http_request_t *r);
static void ngx_http_js_content_write_event_handler(ngx_http_request_t *r);
static void ngx_http_js_content_finalize(ngx_http_request_t *r,
    ngx_http_js_ctx_t *ctx);
static ngx_int_t ngx_http_js_header_filter(ngx_http_request_t *r);
static ngx_int_t ngx_http_js_variable_set(ngx_http_request_t *r,
    ngx_http_variable_value_t *v, uintptr_t data);
static ngx_int_t ngx_http_js_variable_var(ngx_http_request_t *r,
    ngx_http_variable_value_t *v, uintptr_t data);
static ngx_int_t ngx_http_js_init_vm(ngx_http_request_t *r);
static void ngx_http_js_cleanup_ctx(void *data);

static njs_int_t ngx_http_js_ext_keys_header(njs_vm_t *vm, njs_value_t *value,
    njs_value_t *keys, ngx_list_t *headers);
#if defined(nginx_version) && (nginx_version < 1023000)
static ngx_table_elt_t *ngx_http_js_get_header(ngx_list_part_t *part,
    u_char *data, size_t len);
#endif
static njs_int_t ngx_http_js_ext_raw_header(njs_vm_t *vm,
    njs_object_prop_t *prop, njs_value_t *value, njs_value_t *setval,
    njs_value_t *retval);
static njs_int_t ngx_http_js_ext_header_out(njs_vm_t *vm,
    njs_object_prop_t *prop, njs_value_t *value, njs_value_t *setval,
    njs_value_t *retval);
#if defined(nginx_version) && (nginx_version < 1023000)
static njs_int_t ngx_http_js_header_single(njs_vm_t *vm,
    ngx_http_request_t *r, ngx_list_t *headers, njs_str_t *name,
    njs_value_t *setval, njs_value_t *retval);
static njs_int_t ngx_http_js_header_out_special(njs_vm_t *vm,
    ngx_http_request_t *r, njs_str_t *v, njs_value_t *setval,
    njs_value_t *retval, ngx_table_elt_t **hh);
static njs_int_t ngx_http_js_header_array(njs_vm_t *vm,
    ngx_http_request_t *r, ngx_list_t *headers, njs_str_t *name,
    njs_value_t *setval, njs_value_t *retval);
static njs_int_t ngx_http_js_header_generic(njs_vm_t *vm,
    ngx_http_request_t *r, ngx_list_t *headers, njs_str_t *name,
    njs_value_t *setval, njs_value_t *retval);
static njs_int_t ngx_http_js_content_length(njs_vm_t *vm, ngx_http_request_t *r,
    ngx_list_t *headers, njs_str_t *name, njs_value_t *setval,
    njs_value_t *retval);
static njs_int_t ngx_http_js_content_encoding(njs_vm_t *vm,
    ngx_http_request_t *r, ngx_list_t *headers, njs_str_t *name,
    njs_value_t *setval, njs_value_t *retval);
static njs_int_t ngx_http_js_content_type(njs_vm_t *vm, ngx_http_request_t *r,
    ngx_list_t *headers, njs_str_t *name, njs_value_t *setval,
    njs_value_t *retval);
#endif
static njs_int_t ngx_http_js_ext_keys_header_out(njs_vm_t *vm,
    njs_value_t *value, njs_value_t *keys);
static njs_int_t ngx_http_js_ext_status(njs_vm_t *vm, njs_object_prop_t *prop,
    njs_value_t *value, njs_value_t *setval, njs_value_t *retval);
static njs_int_t ngx_http_js_ext_send_header(njs_vm_t *vm, njs_value_t *args,
    njs_uint_t nargs, njs_index_t unused);
static njs_int_t ngx_http_js_ext_send(njs_vm_t *vm, njs_value_t *args,
    njs_uint_t nargs, njs_index_t unused);
static njs_int_t ngx_http_js_ext_send_buffer(njs_vm_t *vm, njs_value_t *args,
    njs_uint_t nargs, njs_index_t unused);
static njs_int_t ngx_http_js_ext_set_return_value(njs_vm_t *vm,
    njs_value_t *args, njs_uint_t nargs, njs_index_t unused);
static njs_int_t ngx_http_js_ext_done(njs_vm_t *vm, njs_value_t *args,
    njs_uint_t nargs, njs_index_t unused);
static njs_int_t ngx_http_js_ext_finish(njs_vm_t *vm, njs_value_t *args,
    njs_uint_t nargs, njs_index_t unused);
static njs_int_t ngx_http_js_ext_return(njs_vm_t *vm, njs_value_t *args,
    njs_uint_t nargs, njs_index_t unused);
static njs_int_t ngx_http_js_ext_internal_redirect(njs_vm_t *vm,
    njs_value_t *args, njs_uint_t nargs, njs_index_t unused);

static njs_int_t ngx_http_js_ext_get_http_version(njs_vm_t *vm,
    njs_object_prop_t *prop, njs_value_t *value, njs_value_t *setval,
    njs_value_t *retval);
static njs_int_t ngx_http_js_ext_internal(njs_vm_t *vm,
    njs_object_prop_t *prop, njs_value_t *value, njs_value_t *setval,
    njs_value_t *retval);
static njs_int_t ngx_http_js_ext_get_remote_address(njs_vm_t *vm,
    njs_object_prop_t *prop, njs_value_t *value, njs_value_t *setval,
    njs_value_t *retval);
static njs_int_t ngx_http_js_ext_get_args(njs_vm_t *vm,
    njs_object_prop_t *prop, njs_value_t *value, njs_value_t *setval,
    njs_value_t *retval);
static njs_int_t ngx_http_js_ext_get_request_body(njs_vm_t *vm,
    njs_object_prop_t *prop, njs_value_t *value, njs_value_t *setval,
    njs_value_t *retval);
static njs_int_t ngx_http_js_ext_header_in(njs_vm_t *vm,
    njs_object_prop_t *prop, njs_value_t *value, njs_value_t *setval,
    njs_value_t *retval);
#if defined(nginx_version) && (nginx_version < 1023000)
static njs_int_t ngx_http_js_header_cookie(njs_vm_t *vm,
    ngx_http_request_t *r, ngx_list_t *headers, njs_str_t *name,
    njs_value_t *setval, njs_value_t *retval);
#if (NGX_HTTP_X_FORWARDED_FOR)
static njs_int_t ngx_http_js_header_x_forwarded_for(njs_vm_t *vm,
    ngx_http_request_t *r, ngx_list_t *headers, njs_str_t *name,
    njs_value_t *setval, njs_value_t *retval);
#endif
static njs_int_t ngx_http_js_header_in_array(njs_vm_t *vm,
    ngx_http_request_t *r, ngx_array_t *array, u_char sep, njs_value_t *retval);
#endif
static njs_int_t ngx_http_js_ext_keys_header_in(njs_vm_t *vm,
    njs_value_t *value, njs_value_t *keys);
static njs_int_t ngx_http_js_ext_variables(njs_vm_t *vm,
    njs_object_prop_t *prop, njs_value_t *value, njs_value_t *setval,
    njs_value_t *retval);
static njs_int_t ngx_http_js_ext_subrequest(njs_vm_t *vm, njs_value_t *args,
    njs_uint_t nargs, njs_index_t unused);
static ngx_int_t ngx_http_js_subrequest(ngx_http_request_t *r,
    njs_str_t *uri_arg, njs_str_t *args_arg, njs_function_t *callback,
    ngx_http_request_t **sr);
static ngx_int_t ngx_http_js_subrequest_done(ngx_http_request_t *r,
    void *data, ngx_int_t rc);
static njs_int_t ngx_http_js_ext_get_parent(njs_vm_t *vm,
    njs_object_prop_t *prop, njs_value_t *value, njs_value_t *setval,
    njs_value_t *retval);
static njs_int_t ngx_http_js_ext_get_response_body(njs_vm_t *vm,
    njs_object_prop_t *prop, njs_value_t *value, njs_value_t *setval,
    njs_value_t *retval);

#if defined(nginx_version) && (nginx_version >= 1023000)
static njs_int_t ngx_http_js_header_in(njs_vm_t *vm, ngx_http_request_t *r,
    unsigned flags, njs_str_t *name, njs_value_t *retval);
static njs_int_t ngx_http_js_header_out(njs_vm_t *vm, ngx_http_request_t *r,
    unsigned flags, njs_str_t *name, njs_value_t *setval, njs_value_t *retval);
static njs_int_t ngx_http_js_content_encoding(njs_vm_t *vm,
    ngx_http_request_t *r, unsigned flags, njs_str_t *name,
    njs_value_t *setval, njs_value_t *retval);
static njs_int_t ngx_http_js_content_length(njs_vm_t *vm, ngx_http_request_t *r,
    unsigned flags, njs_str_t *name, njs_value_t *setval,
    njs_value_t *retval);
static njs_int_t ngx_http_js_content_type(njs_vm_t *vm, ngx_http_request_t *r,
    unsigned flags, njs_str_t *name, njs_value_t *setval,
    njs_value_t *retval);
static njs_int_t ngx_http_js_header_out_special(njs_vm_t *vm,
    ngx_http_request_t *r, njs_str_t *v, njs_value_t *setval,
    njs_value_t *retval, ngx_table_elt_t **hh);
static njs_int_t ngx_http_js_header_generic(njs_vm_t *vm,
    ngx_http_request_t *r, ngx_list_t *headers, ngx_table_elt_t **ph,
    unsigned flags, njs_str_t *name, njs_value_t *retval);
#endif

static njs_host_event_t ngx_http_js_set_timer(njs_external_ptr_t external,
    uint64_t delay, njs_vm_event_t vm_event);
static void ngx_http_js_clear_timer(njs_external_ptr_t external,
    njs_host_event_t event);
static void ngx_http_js_timer_handler(ngx_event_t *ev);
static ngx_pool_t *ngx_http_js_pool(njs_vm_t *vm, ngx_http_request_t *r);
static ngx_resolver_t *ngx_http_js_resolver(njs_vm_t *vm,
    ngx_http_request_t *r);
static ngx_msec_t ngx_http_js_resolver_timeout(njs_vm_t *vm,
    ngx_http_request_t *r);
static ngx_msec_t ngx_http_js_fetch_timeout(njs_vm_t *vm,
    ngx_http_request_t *r);
static size_t ngx_http_js_buffer_size(njs_vm_t *vm, ngx_http_request_t *r);
static size_t ngx_http_js_max_response_buffer_size(njs_vm_t *vm,
    ngx_http_request_t *r);
static void ngx_http_js_handle_vm_event(ngx_http_request_t *r,
    njs_vm_event_t vm_event, njs_value_t *args, njs_uint_t nargs);
static void ngx_http_js_handle_event(ngx_http_request_t *r,
    njs_vm_event_t vm_event, njs_value_t *args, njs_uint_t nargs);

static ngx_int_t ngx_http_js_init(ngx_conf_t *cf);
static char *ngx_http_js_set(ngx_conf_t *cf, ngx_command_t *cmd, void *conf);
static char *ngx_http_js_var(ngx_conf_t *cf, ngx_command_t *cmd, void *conf);
static char *ngx_http_js_content(ngx_conf_t *cf, ngx_command_t *cmd,
    void *conf);
static char *ngx_http_js_body_filter_set(ngx_conf_t *cf, ngx_command_t *cmd,
    void *conf);
static ngx_int_t ngx_http_js_init_conf_vm(ngx_conf_t *cf,
    ngx_js_conf_t *conf);
static void *ngx_http_js_create_loc_conf(ngx_conf_t *cf);
static char *ngx_http_js_merge_loc_conf(ngx_conf_t *cf, void *parent,
    void *child);

static ngx_ssl_t *ngx_http_js_ssl(njs_vm_t *vm, ngx_http_request_t *r);
static ngx_flag_t ngx_http_js_ssl_verify(njs_vm_t *vm, ngx_http_request_t *r);

#if (NGX_HTTP_SSL)

static ngx_conf_bitmask_t  ngx_http_js_ssl_protocols[] = {
    { ngx_string("TLSv1"), NGX_SSL_TLSv1 },
    { ngx_string("TLSv1.1"), NGX_SSL_TLSv1_1 },
    { ngx_string("TLSv1.2"), NGX_SSL_TLSv1_2 },
    { ngx_string("TLSv1.3"), NGX_SSL_TLSv1_3 },
    { ngx_null_string, 0 }
};

#endif

static ngx_command_t  ngx_http_js_commands[] = {

    { ngx_string("js_import"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_TAKE13,
      ngx_js_import,
      NGX_HTTP_LOC_CONF_OFFSET,
      0,
      NULL },

    { ngx_string("js_preload_object"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_TAKE13,
      ngx_js_preload_object,
      NGX_HTTP_LOC_CONF_OFFSET,
      0,
      NULL },

    { ngx_string("js_path"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_str_array_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_js_loc_conf_t, paths),
      NULL },

    { ngx_string("js_set"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_TAKE2,
      ngx_http_js_set,
      0,
      0,
      NULL },

    { ngx_string("js_var"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_TAKE12,
      ngx_http_js_var,
      0,
      0,
      NULL },

    { ngx_string("js_content"),
      NGX_HTTP_LOC_CONF|NGX_HTTP_LIF_CONF|NGX_HTTP_LMT_CONF|NGX_CONF_TAKE1,
      ngx_http_js_content,
      NGX_HTTP_LOC_CONF_OFFSET,
      0,
      NULL },

    { ngx_string("js_header_filter"),
      NGX_HTTP_LOC_CONF|NGX_HTTP_LIF_CONF|NGX_HTTP_LMT_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_str_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_js_loc_conf_t, header_filter),
      NULL },

    { ngx_string("js_body_filter"),
      NGX_HTTP_LOC_CONF|NGX_HTTP_LIF_CONF|NGX_HTTP_LMT_CONF|NGX_CONF_TAKE12,
      ngx_http_js_body_filter_set,
      NGX_HTTP_LOC_CONF_OFFSET,
      0,
      NULL },

    { ngx_string("js_fetch_buffer_size"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_size_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_js_loc_conf_t, buffer_size),
      NULL },

    { ngx_string("js_fetch_max_response_buffer_size"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_size_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_js_loc_conf_t, max_response_body_size),
      NULL },

    { ngx_string("js_fetch_timeout"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_msec_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_js_loc_conf_t, timeout),
      NULL },

#if (NGX_HTTP_SSL)

    { ngx_string("js_fetch_ciphers"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_str_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_js_loc_conf_t, ssl_ciphers),
      NULL },

    { ngx_string("js_fetch_protocols"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_1MORE,
      ngx_conf_set_bitmask_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_js_loc_conf_t, ssl_protocols),
      &ngx_http_js_ssl_protocols },

    { ngx_string("js_fetch_verify"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_FLAG,
      ngx_conf_set_flag_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_js_loc_conf_t, ssl_verify),
      NULL },

    { ngx_string("js_fetch_verify_depth"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_num_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_js_loc_conf_t, ssl_verify_depth),
      NULL },

    { ngx_string("js_fetch_trusted_certificate"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_str_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_js_loc_conf_t, ssl_trusted_certificate),
      NULL },

#endif

      ngx_null_command
};


static ngx_http_module_t  ngx_http_js_module_ctx = {
    NULL,                          /* preconfiguration */
    ngx_http_js_init,              /* postconfiguration */

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


static ngx_http_output_header_filter_pt  ngx_http_next_header_filter;
static ngx_http_output_body_filter_pt    ngx_http_next_body_filter;


static njs_int_t    ngx_http_js_request_proto_id;


static njs_external_t  ngx_http_js_ext_request[] = {

    {
        .flags = NJS_EXTERN_PROPERTY | NJS_EXTERN_SYMBOL,
        .name.symbol = NJS_SYMBOL_TO_STRING_TAG,
        .u.property = {
            .value = "Request",
        }
    },

    {
        .flags = NJS_EXTERN_PROPERTY,
        .name.string = njs_str("args"),
        .enumerable = 1,
        .u.property = {
            .handler = ngx_http_js_ext_get_args,
        }
    },

    {
        .flags = NJS_EXTERN_METHOD,
        .name.string = njs_str("done"),
        .writable = 1,
        .configurable = 1,
        .enumerable = 1,
        .u.method = {
            .native = ngx_http_js_ext_done,
        }
    },

    {
        .flags = NJS_EXTERN_METHOD,
        .name.string = njs_str("error"),
        .writable = 1,
        .configurable = 1,
        .enumerable = 1,
        .u.method = {
            .native = ngx_js_ext_log,
            .magic8 = NGX_LOG_ERR,
        }
    },

    {
        .flags = NJS_EXTERN_METHOD,
        .name.string = njs_str("finish"),
        .writable = 1,
        .configurable = 1,
        .enumerable = 1,
        .u.method = {
            .native = ngx_http_js_ext_finish,
        }
    },

    {
        .flags = NJS_EXTERN_OBJECT,
        .name.string = njs_str("headersIn"),
        .enumerable = 1,
        .u.object = {
            .enumerable = 1,
            .prop_handler = ngx_http_js_ext_header_in,
            .keys = ngx_http_js_ext_keys_header_in,
        }
    },

    {
        .flags = NJS_EXTERN_OBJECT,
        .name.string = njs_str("headersOut"),
        .enumerable = 1,
        .u.object = {
            .writable = 1,
            .configurable = 1,
            .enumerable = 1,
            .prop_handler = ngx_http_js_ext_header_out,
            .keys = ngx_http_js_ext_keys_header_out,
        }
    },

    {
        .flags = NJS_EXTERN_PROPERTY,
        .name.string = njs_str("httpVersion"),
        .enumerable = 1,
        .u.property = {
            .handler = ngx_http_js_ext_get_http_version,
        }
    },

    {
        .flags = NJS_EXTERN_PROPERTY,
        .name.string = njs_str("internal"),
        .enumerable = 1,
        .u.property = {
            .handler = ngx_http_js_ext_internal,
        }
    },

    {
        .flags = NJS_EXTERN_METHOD,
        .name.string = njs_str("internalRedirect"),
        .writable = 1,
        .configurable = 1,
        .enumerable = 1,
        .u.method = {
            .native = ngx_http_js_ext_internal_redirect,
        }
    },

    {
        .flags = NJS_EXTERN_METHOD,
        .name.string = njs_str("log"),
        .writable = 1,
        .configurable = 1,
        .enumerable = 1,
        .u.method = {
            .native = ngx_js_ext_log,
            .magic8 = NGX_LOG_INFO,
        }
    },

    {
        .flags = NJS_EXTERN_PROPERTY,
        .name.string = njs_str("method"),
        .enumerable = 1,
        .u.property = {
            .handler = ngx_js_ext_string,
            .magic32 = offsetof(ngx_http_request_t, method_name),
        }
    },

    {
        .flags = NJS_EXTERN_PROPERTY,
        .name.string = njs_str("parent"),
        .u.property = {
            .handler = ngx_http_js_ext_get_parent,
        }
    },

    {
        .flags = NJS_EXTERN_PROPERTY,
        .name.string = njs_str("rawHeadersIn"),
        .u.property = {
            .handler = ngx_http_js_ext_raw_header,
            .magic32 = 0,
        }
    },

    {
        .flags = NJS_EXTERN_PROPERTY,
        .name.string = njs_str("rawHeadersOut"),
        .u.property = {
            .handler = ngx_http_js_ext_raw_header,
            .magic32 = 1,
        }
    },

    {
        .flags = NJS_EXTERN_OBJECT,
        .name.string = njs_str("rawVariables"),
        .u.object = {
            .writable = 1,
            .prop_handler = ngx_http_js_ext_variables,
            .magic32 = NGX_JS_BUFFER,
        }
    },

    {
        .flags = NJS_EXTERN_PROPERTY,
        .name.string = njs_str("remoteAddress"),
        .enumerable = 1,
        .u.property = {
            .handler = ngx_http_js_ext_get_remote_address,
        }
    },

    {
        .flags = NJS_EXTERN_PROPERTY,
        .name.string = njs_str("requestBody"),
        .u.property = {
            .handler = ngx_http_js_ext_get_request_body,
            .magic32 = NGX_JS_STRING | NGX_JS_DEPRECATED,
        }
    },

    {
        .flags = NJS_EXTERN_PROPERTY,
        .name.string = njs_str("requestBuffer"),
        .u.property = {
            .handler = ngx_http_js_ext_get_request_body,
            .magic32 = NGX_JS_BUFFER,
        }
    },

    {
        .flags = NJS_EXTERN_PROPERTY,
        .name.string = njs_str("requestText"),
        .enumerable = 1,
        .u.property = {
            .handler = ngx_http_js_ext_get_request_body,
            .magic32 = NGX_JS_STRING,
        }
    },

    {
        .flags = NJS_EXTERN_PROPERTY,
        .name.string = njs_str("responseBody"),
        .u.property = {
            .handler = ngx_http_js_ext_get_response_body,
            .magic32 = NGX_JS_STRING | NGX_JS_DEPRECATED,
        }
    },

    {
        .flags = NJS_EXTERN_PROPERTY,
        .name.string = njs_str("responseBuffer"),
        .u.property = {
            .handler = ngx_http_js_ext_get_response_body,
            .magic32 = NGX_JS_BUFFER,
        }
    },

    {
        .flags = NJS_EXTERN_PROPERTY,
        .name.string = njs_str("responseText"),
        .enumerable = 1,
        .u.property = {
            .handler = ngx_http_js_ext_get_response_body,
            .magic32 = NGX_JS_STRING,
        }
    },

    {
        .flags = NJS_EXTERN_METHOD,
        .name.string = njs_str("return"),
        .writable = 1,
        .configurable = 1,
        .enumerable = 1,
        .u.method = {
            .native = ngx_http_js_ext_return,
        }
    },

    {
        .flags = NJS_EXTERN_METHOD,
        .name.string = njs_str("send"),
        .writable = 1,
        .configurable = 1,
        .enumerable = 1,
        .u.method = {
            .native = ngx_http_js_ext_send,
        }
    },

    {
        .flags = NJS_EXTERN_METHOD,
        .name.string = njs_str("sendBuffer"),
        .writable = 1,
        .configurable = 1,
        .enumerable = 1,
        .u.method = {
            .native = ngx_http_js_ext_send_buffer,
        }
    },

    {
        .flags = NJS_EXTERN_METHOD,
        .name.string = njs_str("sendHeader"),
        .writable = 1,
        .configurable = 1,
        .enumerable = 1,
        .u.method = {
            .native = ngx_http_js_ext_send_header,
        }
    },

    {
        .flags = NJS_EXTERN_METHOD,
        .name.string = njs_str("setReturnValue"),
        .writable = 1,
        .configurable = 1,
        .enumerable = 1,
        .u.method = {
            .native = ngx_http_js_ext_set_return_value,
        }
    },

    {
        .flags = NJS_EXTERN_PROPERTY,
        .name.string = njs_str("status"),
        .writable = 1,
        .enumerable = 1,
        .u.property = {
            .handler = ngx_http_js_ext_status,
        }
    },

    {
        .flags = NJS_EXTERN_METHOD,
        .name.string = njs_str("subrequest"),
        .writable = 1,
        .configurable = 1,
        .enumerable = 1,
        .u.method = {
            .native = ngx_http_js_ext_subrequest,
        }
    },

    {
        .flags = NJS_EXTERN_PROPERTY,
        .name.string = njs_str("uri"),
        .enumerable = 1,
        .u.property = {
            .handler = ngx_js_ext_string,
            .magic32 = offsetof(ngx_http_request_t, uri),
        }
    },

    {
        .flags = NJS_EXTERN_OBJECT,
        .name.string = njs_str("variables"),
        .u.object = {
            .writable = 1,
            .prop_handler = ngx_http_js_ext_variables,
            .magic32 = NGX_JS_STRING,
        }
    },

    {
        .flags = NJS_EXTERN_METHOD,
        .name.string = njs_str("warn"),
        .writable = 1,
        .configurable = 1,
        .enumerable = 1,
        .u.method = {
            .native = ngx_js_ext_log,
            .magic8 = NGX_LOG_WARN,
        }
    },
};


static njs_vm_ops_t ngx_http_js_ops = {
    ngx_http_js_set_timer,
    ngx_http_js_clear_timer,
    NULL,
    ngx_js_logger,
};


static uintptr_t ngx_http_js_uptr[] = {
    offsetof(ngx_http_request_t, connection),
    (uintptr_t) ngx_http_js_pool,
    (uintptr_t) ngx_http_js_resolver,
    (uintptr_t) ngx_http_js_resolver_timeout,
    (uintptr_t) ngx_http_js_handle_event,
    (uintptr_t) ngx_http_js_ssl,
    (uintptr_t) ngx_http_js_ssl_verify,
    (uintptr_t) ngx_http_js_fetch_timeout,
    (uintptr_t) ngx_http_js_buffer_size,
    (uintptr_t) ngx_http_js_max_response_buffer_size,
};


static njs_vm_meta_t ngx_http_js_metas = {
    .size = njs_nitems(ngx_http_js_uptr),
    .values = ngx_http_js_uptr
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

    /*
     * status is expected to be overriden by finish(), return() or
     * internalRedirect() methods, otherwise the content handler is
     * considered invalid.
     */

    ctx->status = NGX_HTTP_INTERNAL_SERVER_ERROR;

    rc = ngx_js_call(ctx->vm, &jlcf->content, r->connection->log,
                     &ctx->request, 1);

    if (rc == NGX_ERROR) {
        ngx_http_finalize_request(r, NGX_HTTP_INTERNAL_SERVER_ERROR);
        return;
    }

    if (rc == NGX_AGAIN) {
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
    ngx_str_t   args;
    ngx_int_t   rc;
    ngx_uint_t  flags;

    ngx_log_debug1(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
                   "http js content rc: %i", ctx->status);

    if (ctx->redirect_uri.len) {
        if (ctx->redirect_uri.data[0] == '@') {
            ngx_http_named_location(r, &ctx->redirect_uri);

        } else {
            ngx_str_null(&args);
            flags = NGX_HTTP_LOG_UNSAFE;

            rc = ngx_http_parse_unsafe_uri(r, &ctx->redirect_uri, &args,
                                           &flags);
            if (rc != NGX_OK) {
                ngx_http_finalize_request(r, NGX_HTTP_INTERNAL_SERVER_ERROR);
                return;
            }

            ngx_http_internal_redirect(r, &ctx->redirect_uri, &args);
        }
    }

    ngx_http_finalize_request(r, ctx->status);
}


static ngx_int_t
ngx_http_js_header_filter(ngx_http_request_t *r)
{
    ngx_int_t                rc;
    njs_int_t                pending;
    ngx_http_js_ctx_t       *ctx;
    ngx_http_js_loc_conf_t  *jlcf;

    jlcf = ngx_http_get_module_loc_conf(r, ngx_http_js_module);

    if (jlcf->header_filter.len == 0) {
        return ngx_http_next_header_filter(r);
    }

    rc = ngx_http_js_init_vm(r);

    if (rc == NGX_ERROR || rc == NGX_DECLINED) {
        return NGX_ERROR;
    }

    ctx = ngx_http_get_module_ctx(r, ngx_http_js_module);

    pending = njs_vm_pending(ctx->vm);

    rc = ngx_js_call(ctx->vm, &jlcf->header_filter, r->connection->log,
                     &ctx->request, 1);

    if (rc == NGX_ERROR) {
        return NGX_ERROR;
    }

    if (!pending && rc == NGX_AGAIN) {
        ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                      "async operation inside \"%V\" header filter",
                      &jlcf->header_filter);
        return NGX_ERROR;
    }

    return ngx_http_next_header_filter(r);
}


static ngx_int_t
ngx_http_js_body_filter(ngx_http_request_t *r, ngx_chain_t *in)
{
    size_t                   len;
    u_char                  *p;
    ngx_int_t                rc;
    njs_int_t                ret, pending;
    ngx_buf_t               *b;
    ngx_chain_t             *out, *cl;
    ngx_connection_t        *c;
    ngx_http_js_ctx_t       *ctx;
    njs_opaque_value_t       last_key, last;
    ngx_http_js_loc_conf_t  *jlcf;
    njs_opaque_value_t       arguments[3];

    static const njs_str_t last_str = njs_str("last");

    jlcf = ngx_http_get_module_loc_conf(r, ngx_http_js_module);

    if (jlcf->body_filter.len == 0) {
        return ngx_http_next_body_filter(r, in);
    }

    rc = ngx_http_js_init_vm(r);

    if (rc == NGX_ERROR || rc == NGX_DECLINED) {
        return NGX_ERROR;
    }

    ctx = ngx_http_get_module_ctx(r, ngx_http_js_module);

    if (ctx->done) {
        return ngx_http_next_body_filter(r, in);
    }

    c = r->connection;

    ctx->filter = 1;
    ctx->last_out = &out;

    njs_value_assign(&arguments[0], &ctx->request);

    njs_vm_value_string_set(ctx->vm, njs_value_arg(&last_key),
                            last_str.start, last_str.length);

    while (in != NULL) {
        ctx->buf = in->buf;
        b = ctx->buf;

        if (!ctx->done) {
            len = b->last - b->pos;

            p = ngx_pnalloc(r->pool, len);
            if (p == NULL) {
                njs_vm_memory_error(ctx->vm);
                return NJS_ERROR;
            }

            if (len) {
                ngx_memcpy(p, b->pos, len);
            }

            ret = ngx_js_prop(ctx->vm, jlcf->buffer_type,
                              njs_value_arg(&arguments[1]), p, len);
            if (ret != NJS_OK) {
                return ret;
            }

            njs_value_boolean_set(njs_value_arg(&last), b->last_buf);

            ret = njs_vm_object_alloc(ctx->vm, njs_value_arg(&arguments[2]),
                                       njs_value_arg(&last_key),
                                       njs_value_arg(&last), NULL);
            if (ret != NJS_OK) {
                return ret;
            }

            pending = njs_vm_pending(ctx->vm);

            rc = ngx_js_call(ctx->vm, &jlcf->body_filter, c->log, &arguments[0],
                             3);

            if (rc == NGX_ERROR) {
                return NGX_ERROR;
            }

            if (!pending && rc == NGX_AGAIN) {
                ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                              "async operation inside \"%V\" body filter",
                              &jlcf->body_filter);
                return NGX_ERROR;
            }

            ctx->buf->pos = ctx->buf->last;

        } else {
            cl = ngx_alloc_chain_link(c->pool);
            if (cl == NULL) {
                return NGX_ERROR;
            }

            cl->buf = b;

            *ctx->last_out = cl;
            ctx->last_out = &cl->next;
        }

        in = in->next;
    }

    *ctx->last_out = NULL;

    if (out != NULL || c->buffered) {
        rc = ngx_http_next_body_filter(r, out);

        ngx_chain_update_chains(c->pool, &ctx->free, &ctx->busy, &out,
                                (ngx_buf_tag_t) &ngx_http_js_module);

    } else {
        rc = NGX_OK;
    }

    return rc;
}


static ngx_int_t
ngx_http_js_variable_set(ngx_http_request_t *r, ngx_http_variable_value_t *v,
    uintptr_t data)
{
    ngx_str_t *fname = (ngx_str_t *) data;

    ngx_int_t           rc;
    njs_int_t           pending;
    ngx_str_t           value;
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

    pending = njs_vm_pending(ctx->vm);

    rc = ngx_js_call(ctx->vm, fname, r->connection->log, &ctx->request, 1);

    if (rc == NGX_ERROR) {
        v->not_found = 1;
        return NGX_OK;
    }

    if (!pending && rc == NGX_AGAIN) {
        ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                      "async operation inside \"%V\" variable handler", fname);
        return NGX_ERROR;
    }

    if (ngx_js_retval(ctx->vm, &ctx->retval, &value) != NGX_OK) {
        return NGX_ERROR;
    }

    v->len = value.len;
    v->valid = 1;
    v->no_cacheable = 0;
    v->not_found = 0;
    v->data = value.data;

    return NGX_OK;
}


static ngx_int_t
ngx_http_js_variable_var(ngx_http_request_t *r, ngx_http_variable_value_t *v,
    uintptr_t data)
{
    ngx_http_complex_value_t *cv = (ngx_http_complex_value_t *) data;

    ngx_str_t  value;

    if (cv != NULL) {
        if (ngx_http_complex_value(r, cv, &value) != NGX_OK) {
            return NGX_ERROR;
        }

    } else {
        ngx_str_null(&value);
    }

    v->len = value.len;
    v->valid = 1;
    v->no_cacheable = 0;
    v->not_found = 0;
    v->data = value.data;

    return NGX_OK;
}


static ngx_int_t
ngx_http_js_init_vm(ngx_http_request_t *r)
{
    njs_int_t                rc;
    ngx_str_t                exception;
    njs_str_t                key;
    ngx_uint_t               i;
    ngx_http_js_ctx_t       *ctx;
    njs_opaque_value_t       retval;
    ngx_pool_cleanup_t      *cln;
    ngx_js_named_path_t     *preload;
    ngx_http_js_loc_conf_t  *jlcf;

    jlcf = ngx_http_get_module_loc_conf(r, ngx_http_js_module);
    if (jlcf->vm == NULL) {
        return NGX_DECLINED;
    }

    ctx = ngx_http_get_module_ctx(r, ngx_http_js_module);

    if (ctx == NULL) {
        ctx = ngx_pcalloc(r->pool, sizeof(ngx_http_js_ctx_t));
        if (ctx == NULL) {
            return NGX_ERROR;
        }

        njs_value_invalid_set(njs_value_arg(&ctx->retval));

        ngx_http_set_ctx(r, ctx, ngx_http_js_module);
    }

    if (ctx->vm) {
        return NGX_OK;
    }

    ctx->vm = njs_vm_clone(jlcf->vm, r);
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

    /* bind objects from preload vm */

    if (jlcf->preload_objects != NGX_CONF_UNSET_PTR) {
        preload = jlcf->preload_objects->elts;

        for (i = 0; i < jlcf->preload_objects->nelts; i++) {
            key.start = preload[i].name.data;
            key.length = preload[i].name.len;

            rc = njs_vm_value(jlcf->preload_vm, &key, njs_value_arg(&retval));
            if (rc != NJS_OK) {
                return NGX_ERROR;
            }

            rc = njs_vm_bind(ctx->vm, &key, njs_value_arg(&retval), 0);
            if (rc != NJS_OK) {
                return NGX_ERROR;
            }
        }
    }

    if (njs_vm_start(ctx->vm) == NJS_ERROR) {
        ngx_js_retval(ctx->vm, NULL, &exception);

        ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                      "js exception: %V", &exception);

        return NGX_ERROR;
    }

    rc = njs_vm_external_create(ctx->vm, njs_value_arg(&ctx->request),
                                ngx_http_js_request_proto_id, r, 0);
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


static njs_int_t
ngx_http_js_ext_keys_header(njs_vm_t *vm, njs_value_t *value, njs_value_t *keys,
    ngx_list_t *headers)
{
    int64_t           i, length;
    njs_int_t         rc;
    njs_str_t         hdr;
    ngx_uint_t        item;
    njs_value_t      *start;
    ngx_list_part_t  *part;
    ngx_table_elt_t  *header, *h;

    part = &headers->part;
    item = 0;
    length = 0;

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

        start = njs_vm_array_start(vm, keys);

        for (i = 0; i < length; i++) {
            njs_value_string_get(njs_argument(start, i), &hdr);

            if (h->key.len == hdr.length
                && ngx_strncasecmp(h->key.data, hdr.start, hdr.length) == 0)
            {
                break;
            }
        }

        if (i == length) {
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


#if defined(nginx_version) && (nginx_version < 1023000)
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
#endif


static njs_int_t
ngx_http_js_ext_raw_header(njs_vm_t *vm, njs_object_prop_t *prop,
    njs_value_t *value, njs_value_t *setval, njs_value_t *retval)
{
    njs_int_t            rc;
    ngx_uint_t           i;
    njs_value_t         *array, *elem;
    ngx_list_part_t     *part;
    ngx_list_t          *headers;
    ngx_table_elt_t     *header, *h;
    ngx_http_request_t  *r;

    r = njs_vm_external(vm, ngx_http_js_request_proto_id, value);
    if (r == NULL) {
        njs_value_undefined_set(retval);
        return NJS_DECLINED;
    }

    headers = (njs_vm_prop_magic32(prop) == 1) ? &r->headers_out.headers
                                               : &r->headers_in.headers;

    rc = njs_vm_array_alloc(vm, retval, 8);
    if (rc != NJS_OK) {
        return NJS_ERROR;
    }

    part = &headers->part;
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

        array = njs_vm_array_push(vm, retval);
        if (array == NULL) {
            return NJS_ERROR;
        }

        rc = njs_vm_array_alloc(vm, array, 2);
        if (rc != NJS_OK) {
            return NJS_ERROR;
        }

        elem = njs_vm_array_push(vm, array);
        if (elem == NULL) {
            return NJS_ERROR;
        }

        rc = njs_vm_value_string_set(vm, elem, h->key.data, h->key.len);
        if (rc != NJS_OK) {
            return NJS_ERROR;
        }

        elem = njs_vm_array_push(vm, array);
        if (elem == NULL) {
            return NJS_ERROR;
        }

        rc = njs_vm_value_string_set(vm, elem, h->value.data, h->value.len);
        if (rc != NJS_OK) {
            return NJS_ERROR;
        }
    }

    return NJS_OK;
}


static njs_int_t
ngx_http_js_ext_header_out(njs_vm_t *vm, njs_object_prop_t *prop,
    njs_value_t *value, njs_value_t *setval, njs_value_t *retval)
{
    njs_int_t              rc;
    njs_str_t              name;
    ngx_http_request_t    *r;
    ngx_http_js_header_t  *h;

    static ngx_http_js_header_t headers_out[] = {
#if defined(nginx_version) && (nginx_version < 1023000)
        { njs_str("Age"), ngx_http_js_header_single },
        { njs_str("Content-Type"), ngx_http_js_content_type },
        { njs_str("Content-Length"), ngx_http_js_content_length },
        { njs_str("Content-Encoding"), ngx_http_js_content_encoding },
        { njs_str("Etag"), ngx_http_js_header_single },
        { njs_str("Expires"), ngx_http_js_header_single },
        { njs_str("Last-Modified"), ngx_http_js_header_single },
        { njs_str("Location"), ngx_http_js_header_single },
        { njs_str("Set-Cookie"), ngx_http_js_header_array },
        { njs_str("Retry-After"), ngx_http_js_header_single },
        { njs_str(""), ngx_http_js_header_generic },
#else
        { njs_str("Age"), NJS_HEADER_SINGLE, ngx_http_js_header_out },
        { njs_str("Content-Encoding"), 0, ngx_http_js_content_encoding },
        { njs_str("Content-Length"), 0, ngx_http_js_content_length },
        { njs_str("Content-Type"), 0, ngx_http_js_content_type },
        { njs_str("Etag"), NJS_HEADER_SINGLE, ngx_http_js_header_out },
        { njs_str("Expires"), NJS_HEADER_SINGLE, ngx_http_js_header_out },
        { njs_str("Last-Modified"), NJS_HEADER_SINGLE, ngx_http_js_header_out },
        { njs_str("Location"), NJS_HEADER_SINGLE, ngx_http_js_header_out },
        { njs_str("Set-Cookie"), NJS_HEADER_ARRAY, ngx_http_js_header_out },
        { njs_str("Retry-After"), NJS_HEADER_SINGLE, ngx_http_js_header_out },
        { njs_str(""), 0, ngx_http_js_header_out },
#endif
    };

    r = njs_vm_external(vm, ngx_http_js_request_proto_id, value);
    if (r == NULL) {
        if (retval != NULL) {
            njs_value_undefined_set(retval);
        }

        return NJS_DECLINED;
    }

    rc = njs_vm_prop_name(vm, prop, &name);
    if (rc != NJS_OK) {
        if (retval != NULL) {
            njs_value_undefined_set(retval);
        }

        return NJS_DECLINED;
    }

    if (r->header_sent && setval != NULL) {
        njs_vm_warn(vm, "ignored setting of response header \"%V\" because"
                        " headers were already sent", &name);
    }

    for (h = headers_out; h->name.length > 0; h++) {
        if (h->name.length == name.length
            && ngx_strncasecmp(h->name.start, name.start, name.length) == 0)
        {
            break;
        }
    }

#if defined(nginx_version) && (nginx_version < 1023000)
    return h->handler(vm, r, &r->headers_out.headers, &name, setval, retval);
#else
    return h->handler(vm, r, h->flags, &name, setval, retval);
#endif
}


#if defined(nginx_version) && (nginx_version < 1023000)
static njs_int_t
ngx_http_js_header_single(njs_vm_t *vm, ngx_http_request_t *r,
    ngx_list_t *headers, njs_str_t *name, njs_value_t *setval,
    njs_value_t *retval)
{
    njs_int_t         rc;
    ngx_table_elt_t  *h;

    if (retval != NULL && setval == NULL) {
        h = ngx_http_js_get_header(&headers->part, name->start, name->length);
        if (h == NULL) {
            njs_value_undefined_set(retval);
            return NJS_DECLINED;
        }

        rc = njs_vm_value_string_set(vm, retval, h->value.data, h->value.len);
        if (rc != NJS_OK) {
            return NJS_ERROR;
        }

        return NJS_OK;
    }

    return ngx_http_js_header_generic(vm, r, headers, name, setval, retval);
}


static njs_int_t
ngx_http_js_header_out_special(njs_vm_t *vm, ngx_http_request_t *r,
    njs_str_t *v, njs_value_t *setval, njs_value_t *retval,
    ngx_table_elt_t **hh)
{
    u_char              *p;
    int64_t              length;
    njs_int_t            rc;
    njs_str_t            s;
    ngx_list_t          *headers;
    ngx_table_elt_t     *h;
    njs_opaque_value_t   lvalue;

    headers = &r->headers_out.headers;

    if (retval != NULL && setval == NULL) {
        return ngx_http_js_header_single(vm, r, headers, v, setval, retval);
    }

    if (setval != NULL && njs_value_is_array(setval)) {
        rc = njs_vm_array_length(vm, setval, &length);
        if (rc != NJS_OK) {
            return NJS_ERROR;
        }

        setval = njs_vm_array_prop(vm, setval, length - 1, &lvalue);
    }

    if (ngx_js_string(vm, setval, &s) != NGX_OK) {
        return NJS_ERROR;
    }

    h = ngx_http_js_get_header(&headers->part, v->start, v->length);

    if (h != NULL && s.length == 0) {
        h->hash = 0;
        h = NULL;
    }

    if (h == NULL && s.length != 0) {
        h = ngx_list_push(headers);
        if (h == NULL) {
            return NJS_ERROR;
        }

        p = ngx_pnalloc(r->pool, v->length);
        if (p == NULL) {
            h->hash = 0;
            return NJS_ERROR;
        }

        ngx_memcpy(p, v->start, v->length);

        h->key.data = p;
        h->key.len = v->length;
    }

    if (h != NULL) {
        p = ngx_pnalloc(r->pool, s.length);
        if (p == NULL) {
            h->hash = 0;
            return NJS_ERROR;
        }

        ngx_memcpy(p, s.start, s.length);

        h->value.data = p;
        h->value.len = s.length;
        h->hash = 1;
    }

    if (hh != NULL) {
        *hh = h;
    }

    return NJS_OK;
}


static njs_int_t
ngx_http_js_header_array(njs_vm_t *vm, ngx_http_request_t *r,
    ngx_list_t *headers, njs_str_t *name, njs_value_t *setval,
    njs_value_t *retval)
{
    size_t            len;
    u_char           *data;
    njs_int_t         rc;
    ngx_uint_t        i;
    njs_value_t      *value;
    ngx_list_part_t  *part;
    ngx_table_elt_t  *header, *h;

    if (retval != NULL && setval == NULL) {
        rc = njs_vm_array_alloc(vm, retval, 4);
        if (rc != NJS_OK) {
            return NJS_ERROR;
        }

        len = name->length;
        data = name->start;

        part = &headers->part;
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

            if (h->hash == 0
                || h->key.len != len
                || ngx_strncasecmp(h->key.data, data, len) != 0)
            {
                continue;
            }

            value = njs_vm_array_push(vm, retval);
            if (value == NULL) {
                return NJS_ERROR;
            }

            rc = njs_vm_value_string_set(vm, value, h->value.data,
                                         h->value.len);
            if (rc != NJS_OK) {
                return NJS_ERROR;
            }
        }

        return NJS_OK;
    }

    return ngx_http_js_header_generic(vm, r, headers, name, setval, retval);
}


static njs_int_t
ngx_http_js_header_generic(njs_vm_t *vm, ngx_http_request_t *r,
    ngx_list_t *headers, njs_str_t *name, njs_value_t *setval,
    njs_value_t *retval)
{
    u_char              *data, *p, *start, *end;
    size_t               len;
    int64_t              length;
    njs_value_t         *array;
    njs_int_t            rc;
    njs_str_t            s;
    ngx_uint_t           i;
    ngx_list_part_t     *part;
    ngx_table_elt_t     *header, *h;
    njs_opaque_value_t   lvalue;

    part = &headers->part;

    if (retval != NULL && setval == NULL) {
        header = part->elts;

        p = NULL;
        start = NULL;
        end  = NULL;

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

            if (h->hash == 0
                || h->key.len != name->length
                || ngx_strncasecmp(h->key.data, name->start, name->length) != 0)
            {
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

                data = ngx_pnalloc(r->pool, len);
                if (data == NULL) {
                    return NJS_ERROR;
                }

                p = ngx_cpymem(data, start, p - start);
                start = data;
                end = data + len;
            }

            *p++ = ',';
            p = ngx_cpymem(p, h->value.data, h->value.len);
        }

        if (p == NULL) {
            njs_value_undefined_set(retval);
            return NJS_DECLINED;
        }

        return njs_vm_value_string_set(vm, retval, start, p - start);
    }

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

        if (h->hash == 0
            || h->key.len != name->length
            || ngx_strncasecmp(h->key.data, name->start, name->length) != 0)
        {
            continue;
        }

        h->hash = 0;
    }

    if (retval == NULL) {
        return NJS_OK;
    }

    if (njs_value_is_array(setval)) {
        array = setval;

        rc = njs_vm_array_length(vm, array, &length);
        if (rc != NJS_OK) {
            return NJS_ERROR;
        }

        if (length == 0) {
            return NJS_OK;
        }

    } else {
        array = NULL;
        length = 1;
    }

    for (i = 0; i < (ngx_uint_t) length; i++) {
        if (array != NULL) {
            setval = njs_vm_array_prop(vm, array, i, &lvalue);
        }

        if (ngx_js_string(vm, setval, &s) != NGX_OK) {
            return NJS_ERROR;
        }

        if (s.length == 0) {
            continue;
        }

        h = ngx_list_push(headers);
        if (h == NULL) {
            return NJS_ERROR;
        }

        p = ngx_pnalloc(r->pool, name->length);
        if (p == NULL) {
            h->hash = 0;
            return NJS_ERROR;
        }

        ngx_memcpy(p, name->start, name->length);

        h->key.data = p;
        h->key.len = name->length;

        p = ngx_pnalloc(r->pool, s.length);
        if (p == NULL) {
            h->hash = 0;
            return NJS_ERROR;
        }

        ngx_memcpy(p, s.start, s.length);

        h->value.data = p;
        h->value.len = s.length;
        h->hash = 1;
    }

    return NJS_OK;
}


static njs_int_t
ngx_http_js_content_encoding(njs_vm_t *vm, ngx_http_request_t *r,
    ngx_list_t *headers, njs_str_t *v, njs_value_t *setval, njs_value_t *retval)
{
    njs_int_t         rc;
    ngx_table_elt_t  *h;

    rc = ngx_http_js_header_out_special(vm, r, v, setval, retval, &h);
    if (rc == NJS_ERROR) {
        return NJS_ERROR;
    }

    if (setval != NULL || retval == NULL) {
        r->headers_out.content_encoding = h;
    }

    return NJS_OK;
}


static njs_int_t
ngx_http_js_content_length(njs_vm_t *vm, ngx_http_request_t *r,
    ngx_list_t *headers, njs_str_t *v, njs_value_t *setval, njs_value_t *retval)
{
    u_char           *p, *start;
    njs_int_t         rc;
    ngx_int_t         n;
    ngx_table_elt_t  *h;
    u_char            content_len[NGX_OFF_T_LEN];

    if (retval != NULL && setval == NULL) {
        if (r->headers_out.content_length == NULL
            && r->headers_out.content_length_n >= 0)
        {
            p = ngx_sprintf(content_len, "%O", r->headers_out.content_length_n);

            start = njs_vm_value_string_alloc(vm, retval, p - content_len);
            if (start == NULL) {
                return NJS_ERROR;
            }

            ngx_memcpy(start, content_len, p - content_len);

            return NJS_OK;
        }
    }

    rc = ngx_http_js_header_out_special(vm, r, v, setval, retval, &h);
    if (rc == NJS_ERROR) {
        return NJS_ERROR;
    }

    if (setval != NULL || retval == NULL) {
        if (h != NULL) {
            n = ngx_atoi(h->value.data, h->value.len);
            if (n == NGX_ERROR) {
                h->hash = 0;
                njs_vm_error(vm, "failed converting argument "
                             "to positive integer");
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
ngx_http_js_content_type(njs_vm_t *vm, ngx_http_request_t *r,
    ngx_list_t *headers, njs_str_t *v, njs_value_t *setval, njs_value_t *retval)
{
    int64_t              length;
    njs_int_t            rc;
    njs_str_t            s;
    ngx_str_t           *hdr;
    njs_opaque_value_t   lvalue;

    if (retval != NULL && setval == NULL) {
        hdr = &r->headers_out.content_type;

        if (hdr->len == 0) {
            njs_value_undefined_set(retval);
            return NJS_OK;
        }

        return njs_vm_value_string_set(vm, retval, hdr->data, hdr->len);
    }

    if (setval != NULL && njs_value_is_array(setval)) {
        rc = njs_vm_array_length(vm, setval, &length);
        if (rc != NJS_OK) {
            return NJS_ERROR;
        }

        setval = njs_vm_array_prop(vm, setval, length - 1, &lvalue);
    }

    if (ngx_js_string(vm, setval, &s) != NGX_OK) {
        return NJS_ERROR;
    }

    r->headers_out.content_type.len = s.length;
    r->headers_out.content_type_len = r->headers_out.content_type.len;
    r->headers_out.content_type.data = s.start;
    r->headers_out.content_type_lowcase = NULL;

    return NJS_OK;
}
#endif


static njs_int_t
ngx_http_js_ext_keys_header_out(njs_vm_t *vm, njs_value_t *value,
    njs_value_t *keys)
{
    njs_int_t           rc;
    ngx_http_request_t  *r;

    rc = njs_vm_array_alloc(vm, keys, 8);
    if (rc != NJS_OK) {
        return NJS_ERROR;
    }

    r = njs_vm_external(vm, ngx_http_js_request_proto_id, value);
    if (r == NULL) {
        return NJS_OK;
    }

    if (r->headers_out.content_type.len) {
        value = njs_vm_array_push(vm, keys);
        if (value == NULL) {
            return NJS_ERROR;
        }

        rc = njs_vm_value_string_set(vm, value, (u_char *) "Content-Type",
                                     njs_length("Content-Type"));
        if (rc != NJS_OK) {
            return NJS_ERROR;
        }
    }

    if (r->headers_out.content_length == NULL
        && r->headers_out.content_length_n >= 0)
    {
        value = njs_vm_array_push(vm, keys);
        if (value == NULL) {
            return NJS_ERROR;
        }

        rc = njs_vm_value_string_set(vm, value, (u_char *) "Content-Length",
                                     njs_length("Content-Length"));
        if (rc != NJS_OK) {
            return NJS_ERROR;
        }
    }

    return ngx_http_js_ext_keys_header(vm, value, keys,
                                       &r->headers_out.headers);
}


static njs_int_t
ngx_http_js_ext_status(njs_vm_t *vm, njs_object_prop_t *prop,
    njs_value_t *value, njs_value_t *setval, njs_value_t *retval)
{
    ngx_int_t            n;
    ngx_http_request_t  *r;

    r = njs_vm_external(vm, ngx_http_js_request_proto_id, value);
    if (r == NULL) {
        njs_value_undefined_set(retval);
        return NJS_DECLINED;
    }

    if (setval == NULL) {
        njs_value_number_set(retval, r->headers_out.status);
        return NJS_OK;
    }

    if (ngx_js_integer(vm, setval, &n) != NGX_OK) {
        return NJS_ERROR;
    }

    r->headers_out.status = n;

    return NJS_OK;
}


static njs_int_t
ngx_http_js_ext_send_header(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t unused)
{
    ngx_http_request_t  *r;

    r = njs_vm_external(vm, ngx_http_js_request_proto_id,
                        njs_argument(args, 0));
    if (r == NULL) {
        njs_vm_error(vm, "\"this\" is not an external");
        return NJS_ERROR;
    }

    if (ngx_http_set_content_type(r) != NGX_OK) {
        return NJS_ERROR;
    }

    if (ngx_http_send_header(r) == NGX_ERROR) {
        return NJS_ERROR;
    }

    njs_value_undefined_set(njs_vm_retval(vm));

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
    ngx_http_js_ctx_t   *ctx;
    ngx_http_request_t  *r;

    r = njs_vm_external(vm, ngx_http_js_request_proto_id,
                        njs_argument(args, 0));
    if (r == NULL) {
        njs_vm_error(vm, "\"this\" is not an external");
        return NJS_ERROR;
    }

    ctx = ngx_http_get_module_ctx(r, ngx_http_js_module);

    if (ctx->filter) {
        njs_vm_error(vm, "cannot send while in body filter");
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

    njs_value_undefined_set(njs_vm_retval(vm));

    return NJS_OK;
}


static njs_int_t
ngx_http_js_ext_send_buffer(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t unused)
{
    unsigned             last_buf, flush;
    njs_str_t            buffer;
    ngx_buf_t           *b;
    ngx_chain_t         *cl;
    njs_value_t         *flags, *value;
    ngx_http_js_ctx_t   *ctx;
    ngx_http_request_t  *r;
    njs_opaque_value_t   lvalue;

    static const njs_str_t last_key = njs_str("last");
    static const njs_str_t flush_key = njs_str("flush");

    r = njs_vm_external(vm, ngx_http_js_request_proto_id,
                        njs_argument(args, 0));
    if (r == NULL) {
        njs_vm_error(vm, "\"this\" is not an external");
        return NJS_ERROR;
    }

    ctx = ngx_http_get_module_ctx(r, ngx_http_js_module);

    if (!ctx->filter) {
        njs_vm_error(vm, "cannot send buffer while not filtering");
        return NJS_ERROR;
    }

    if (ngx_js_string(vm, njs_arg(args, nargs, 1), &buffer) != NGX_OK) {
        njs_vm_error(vm, "failed to get buffer arg");
        return NJS_ERROR;
    }

    flush = ctx->buf->flush;
    last_buf = ctx->buf->last_buf;

    flags = njs_arg(args, nargs, 2);

    if (njs_value_is_object(flags)) {
        value = njs_vm_object_prop(vm, flags, &flush_key, &lvalue);
        if (value != NULL) {
            flush = njs_value_bool(value);
        }

        value = njs_vm_object_prop(vm, flags, &last_key, &lvalue);
        if (value != NULL) {
            last_buf = njs_value_bool(value);
        }
    }

    cl = ngx_chain_get_free_buf(r->pool, &ctx->free);
    if (cl == NULL) {
        njs_vm_error(vm, "memory error");
        return NJS_ERROR;
    }

    b = cl->buf;

    b->flush = flush;
    b->last_buf = last_buf;

    b->memory = (buffer.length ? 1 : 0);
    b->sync = (buffer.length ? 0 : 1);
    b->tag = (ngx_buf_tag_t) &ngx_http_js_module;

    b->start = buffer.start;
    b->end = buffer.start + buffer.length;
    b->pos = b->start;
    b->last = b->end;

    *ctx->last_out = cl;
    ctx->last_out = &cl->next;

    njs_value_undefined_set(njs_vm_retval(vm));

    return NJS_OK;
}


static njs_int_t
ngx_http_js_ext_set_return_value(njs_vm_t *vm, njs_value_t *args,
    njs_uint_t nargs, njs_index_t unused)
{
    ngx_http_js_ctx_t   *ctx;
    ngx_http_request_t  *r;

    r = njs_vm_external(vm, ngx_http_js_request_proto_id,
                        njs_argument(args, 0));
    if (r == NULL) {
        njs_vm_error(vm, "\"this\" is not an external");
        return NJS_ERROR;
    }

    ctx = ngx_http_get_module_ctx(r, ngx_http_js_module);

    njs_value_assign(&ctx->retval, njs_arg(args, nargs, 1));
    njs_value_undefined_set(njs_vm_retval(vm));

    return NJS_OK;
}


static njs_int_t
ngx_http_js_ext_done(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t unused)
{
    ngx_http_js_ctx_t   *ctx;
    ngx_http_request_t  *r;

    r = njs_vm_external(vm, ngx_http_js_request_proto_id,
                        njs_argument(args, 0));
    if (r == NULL) {
        njs_vm_error(vm, "\"this\" is not an external");
        return NJS_ERROR;
    }

    ctx = ngx_http_get_module_ctx(r, ngx_http_js_module);

    if (!ctx->filter) {
        njs_vm_error(vm, "cannot set done while not filtering");
        return NJS_ERROR;
    }

    ctx->done = 1;

    njs_value_undefined_set(njs_vm_retval(vm));

    return NJS_OK;
}


static njs_int_t
ngx_http_js_ext_finish(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t unused)
{
    ngx_http_js_ctx_t   *ctx;
    ngx_http_request_t  *r;

    r = njs_vm_external(vm, ngx_http_js_request_proto_id,
                        njs_argument(args, 0));
    if (r == NULL) {
        njs_vm_error(vm, "\"this\" is not an external");
        return NJS_ERROR;
    }

    if (ngx_http_send_special(r, NGX_HTTP_LAST) == NGX_ERROR) {
        return NJS_ERROR;
    }

    ctx = ngx_http_get_module_ctx(r, ngx_http_js_module);

    ctx->status = NGX_OK;

    njs_value_undefined_set(njs_vm_retval(vm));

    return NJS_OK;
}


static njs_int_t
ngx_http_js_ext_return(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t unused)
{
    njs_str_t                  text;
    ngx_int_t                  status;
    ngx_http_js_ctx_t         *ctx;
    ngx_http_request_t        *r;
    ngx_http_complex_value_t   cv;

    r = njs_vm_external(vm, ngx_http_js_request_proto_id,
                        njs_argument(args, 0));
    if (r == NULL) {
        njs_vm_error(vm, "\"this\" is not an external");
        return NJS_ERROR;
    }

    if (ngx_js_integer(vm, njs_arg(args, nargs, 1), &status) != NGX_OK) {
        return NJS_ERROR;
    }

    if (status < 0 || status > 999) {
        njs_vm_error(vm, "code is out of range");
        return NJS_ERROR;
    }

    if (ngx_js_string(vm, njs_arg(args, nargs, 2), &text) != NGX_OK) {
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

    njs_value_undefined_set(njs_vm_retval(vm));

    return NJS_OK;
}


static njs_int_t
ngx_http_js_ext_internal_redirect(njs_vm_t *vm, njs_value_t *args,
    njs_uint_t nargs, njs_index_t unused)
{
    njs_str_t            uri;
    ngx_http_js_ctx_t   *ctx;
    ngx_http_request_t  *r;

    r = njs_vm_external(vm, ngx_http_js_request_proto_id,
                        njs_argument(args, 0));
    if (r == NULL) {
        njs_vm_error(vm, "\"this\" is not an external");
        return NJS_ERROR;
    }

    if (r->parent != NULL) {
        njs_vm_error(vm, "internalRedirect cannot be called from a subrequest");
        return NJS_ERROR;
    }

    ctx = ngx_http_get_module_ctx(r, ngx_http_js_module);

    if (ngx_js_string(vm, njs_arg(args, nargs, 1), &uri) != NGX_OK) {
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

    njs_value_undefined_set(njs_vm_retval(vm));

    return NJS_OK;
}


static njs_int_t
ngx_http_js_ext_get_http_version(njs_vm_t *vm, njs_object_prop_t *prop,
    njs_value_t *value, njs_value_t *setval, njs_value_t *retval)
{
    ngx_str_t            v;
    ngx_http_request_t  *r;

    r = njs_vm_external(vm, ngx_http_js_request_proto_id, value);
    if (r == NULL) {
        njs_value_undefined_set(retval);
        return NJS_DECLINED;
    }

    switch (r->http_version) {

    case NGX_HTTP_VERSION_9:
        ngx_str_set(&v, "0.9");
        break;

    case NGX_HTTP_VERSION_10:
        ngx_str_set(&v, "1.0");
        break;

    case NGX_HTTP_VERSION_11:
        ngx_str_set(&v, "1.1");
        break;

    case NGX_HTTP_VERSION_20:
        ngx_str_set(&v, "2.0");
        break;

#if (NGX_HTTP_VERSION_30)
    case NGX_HTTP_VERSION_30:
        ngx_str_set(&v, "3.0");
        break;
#endif

    default:
        ngx_str_set(&v, "");
        break;
    }

    return njs_vm_value_string_set(vm, retval, v.data, v.len);
}


static njs_int_t
ngx_http_js_ext_internal(njs_vm_t *vm, njs_object_prop_t *prop,
    njs_value_t *value, njs_value_t *setval, njs_value_t *retval)
{
    ngx_http_request_t  *r;

    r = njs_vm_external(vm, ngx_http_js_request_proto_id, value);
    if (r == NULL) {
        njs_value_undefined_set(retval);
        return NJS_DECLINED;
    }

    njs_value_boolean_set(retval, r->internal);

    return NJS_OK;
}


static njs_int_t
ngx_http_js_ext_get_remote_address(njs_vm_t *vm, njs_object_prop_t *prop,
    njs_value_t *value, njs_value_t *setval, njs_value_t *retval)
{
    ngx_connection_t    *c;
    ngx_http_request_t  *r;

    r = njs_vm_external(vm, ngx_http_js_request_proto_id, value);
    if (r == NULL) {
        njs_value_undefined_set(retval);
        return NJS_DECLINED;
    }

    c = r->connection;

    return njs_vm_value_string_set(vm, retval, c->addr_text.data,
                                   c->addr_text.len);
}


static njs_int_t
ngx_http_js_ext_get_args(njs_vm_t *vm, njs_object_prop_t *prop,
    njs_value_t *value, njs_value_t *setval, njs_value_t *retval)
{
    njs_int_t           ret;
    njs_value_t         *args;
    ngx_http_js_ctx_t   *ctx;
    ngx_http_request_t  *r;

    r = njs_vm_external(vm, ngx_http_js_request_proto_id, value);
    if (r == NULL) {
        njs_value_undefined_set(retval);
        return NJS_DECLINED;
    }

    ctx = ngx_http_get_module_ctx(r, ngx_http_js_module);

    args = njs_value_arg(&ctx->args);

    if (njs_value_is_null(args)) {
        ret = njs_vm_query_string_parse(vm, r->args.data,
                                        r->args.data + r->args.len, args);

        if (ret == NJS_ERROR) {
            return NJS_ERROR;
        }
    }

    njs_value_assign(retval, args);

    return NJS_OK;
}


static njs_int_t
ngx_http_js_ext_get_request_body(njs_vm_t *vm, njs_object_prop_t *prop,
    njs_value_t *value, njs_value_t *setval, njs_value_t *retval)
{
    u_char              *p, *body;
    size_t               len;
    uint32_t             buffer_type;
    ngx_buf_t           *buf;
    njs_int_t            ret;
    njs_value_t         *request_body;
    ngx_chain_t         *cl;
    ngx_http_js_ctx_t   *ctx;
    ngx_http_request_t  *r;

    if (njs_vm_prop_magic32(prop) & NGX_JS_DEPRECATED) {
        njs_deprecated(vm, "r.requestBody");
    }

    r = njs_vm_external(vm, ngx_http_js_request_proto_id, value);
    if (r == NULL) {
        njs_value_undefined_set(retval);
        return NJS_DECLINED;
    }

    ctx = ngx_http_get_module_ctx(r, ngx_http_js_module);
    request_body = (njs_value_t *) &ctx->request_body;
    buffer_type = ngx_js_buffer_type(njs_vm_prop_magic32(prop));

    if (!njs_value_is_null(request_body)) {
        if ((buffer_type == NGX_JS_BUFFER)
            == (uint32_t) njs_value_is_buffer(request_body))
        {
            njs_value_assign(retval, request_body);
            return NJS_OK;
        }
    }

    if (r->request_body == NULL || r->request_body->bufs == NULL) {
        njs_value_undefined_set(retval);
        return NJS_DECLINED;
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

    ret = ngx_js_prop(vm, buffer_type, request_body, body, len);
    if (ret != NJS_OK) {
        return NJS_ERROR;
    }

    njs_value_assign(retval, request_body);

    return NJS_OK;
}


#if defined(nginx_version) && (nginx_version < 1023000)
static njs_int_t
ngx_http_js_ext_header_in(njs_vm_t *vm, njs_object_prop_t *prop,
    njs_value_t *value, njs_value_t *setval, njs_value_t *retval)
{
    njs_int_t              rc;
    njs_str_t              name;
    ngx_http_request_t    *r;
    ngx_http_js_header_t  *h;

    static ngx_http_js_header_t headers_in[] = {
        { njs_str("Content-Type"), ngx_http_js_header_single },
        { njs_str("Cookie"), ngx_http_js_header_cookie },
        { njs_str("ETag"), ngx_http_js_header_single },
        { njs_str("From"), ngx_http_js_header_single },
        { njs_str("Max-Forwards"), ngx_http_js_header_single },
        { njs_str("Referer"), ngx_http_js_header_single },
        { njs_str("Proxy-Authorization"), ngx_http_js_header_single },
        { njs_str("User-Agent"), ngx_http_js_header_single },
#if (NGX_HTTP_X_FORWARDED_FOR)
        { njs_str("X-Forwarded-For"), ngx_http_js_header_x_forwarded_for },
#endif
        { njs_str(""), ngx_http_js_header_generic },
    };

    r = njs_vm_external(vm, ngx_http_js_request_proto_id, value);
    if (r == NULL) {
        if (retval != NULL) {
            njs_value_undefined_set(retval);
        }

        return NJS_DECLINED;
    }

    rc = njs_vm_prop_name(vm, prop, &name);
    if (rc != NJS_OK) {
        if (retval != NULL) {
            njs_value_undefined_set(retval);
        }

        return NJS_DECLINED;
    }

    for (h = headers_in; h->name.length > 0; h++) {
        if (h->name.length == name.length
            && ngx_strncasecmp(h->name.start, name.start, name.length) == 0)
        {
            break;
        }
    }

    return h->handler(vm, r, &r->headers_in.headers, &name, setval, retval);
}


static njs_int_t
ngx_http_js_header_cookie(njs_vm_t *vm, ngx_http_request_t *r,
    ngx_list_t *headers, njs_str_t *name, njs_value_t *setval,
    njs_value_t *retval)
{
    return ngx_http_js_header_in_array(vm, r, &r->headers_in.cookies,
                                       ';', retval);
}


#if (NGX_HTTP_X_FORWARDED_FOR)
static njs_int_t
ngx_http_js_header_x_forwarded_for(njs_vm_t *vm, ngx_http_request_t *r,
    ngx_list_t *headers, njs_str_t *name, njs_value_t *setval,
    njs_value_t *retval)
{
    return ngx_http_js_header_in_array(vm, r, &r->headers_in.x_forwarded_for,
                                       ',', retval);
}
#endif


static njs_int_t
ngx_http_js_header_in_array(njs_vm_t *vm, ngx_http_request_t *r,
    ngx_array_t *array, u_char sep, njs_value_t *retval)
{
    u_char            *p, *end;
    size_t             len;
    ngx_uint_t         i, n;
    ngx_table_elt_t  **hh;

    n = array->nelts;
    hh = array->elts;

    len = 0;

    for (i = 0; i < n; i++) {
        len += hh[i]->value.len + 1;
    }

    if (len == 0) {
        njs_value_undefined_set(retval);
        return NJS_DECLINED;
    }

    len -= 1;

    if (n == 1) {
        return njs_vm_value_string_set(vm, retval, (*hh)->value.data,
                                       (*hh)->value.len);
    }

    p = njs_vm_value_string_alloc(vm, retval, len);
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
    }

    return NJS_OK;
}
#else
static njs_int_t
ngx_http_js_ext_header_in(njs_vm_t *vm, njs_object_prop_t *prop,
    njs_value_t *value, njs_value_t *unused, njs_value_t *retval)
{
    unsigned             flags;
    njs_int_t            rc;
    njs_str_t            name, *h;
    ngx_http_request_t  *r;

    static njs_str_t single_headers_in[] = {
        njs_str("Content-Type"),
        njs_str("ETag"),
        njs_str("From"),
        njs_str("Max-Forwards"),
        njs_str("Referer"),
        njs_str("Proxy-Authorization"),
        njs_str("User-Agent"),
        njs_str(""),
    };

    r = njs_vm_external(vm, ngx_http_js_request_proto_id, value);
    if (r == NULL) {
        if (retval != NULL) {
            njs_value_undefined_set(retval);
        }

        return NJS_DECLINED;
    }

    rc = njs_vm_prop_name(vm, prop, &name);
    if (rc != NJS_OK) {
        if (retval != NULL) {
            njs_value_undefined_set(retval);
        }

        return NJS_DECLINED;
    }

    flags = 0;

    for (h = single_headers_in; h->length > 0; h++) {
        if (h->length == name.length
            && ngx_strncasecmp(h->start, name.start, name.length) == 0)
        {
            flags |= NJS_HEADER_SINGLE;
            break;
        }
    }

    return ngx_http_js_header_in(vm, r, flags, &name, retval);
}
#endif


static njs_int_t
ngx_http_js_ext_keys_header_in(njs_vm_t *vm, njs_value_t *value,
    njs_value_t *keys)
{
    njs_int_t           rc;
    ngx_http_request_t  *r;

    rc = njs_vm_array_alloc(vm, keys, 8);
    if (rc != NJS_OK) {
        return NJS_ERROR;
    }

    r = njs_vm_external(vm, ngx_http_js_request_proto_id, value);
    if (r == NULL) {
        return NJS_OK;
    }

    return ngx_http_js_ext_keys_header(vm, value, keys, &r->headers_in.headers);
}


static njs_int_t
ngx_http_js_ext_variables(njs_vm_t *vm, njs_object_prop_t *prop,
    njs_value_t *value, njs_value_t *setval, njs_value_t *retval)
{
    njs_int_t                   rc;
    njs_str_t                   val, s;
    ngx_str_t                   name;
    ngx_uint_t                  key;
    ngx_http_request_t         *r;
    ngx_http_variable_t        *v;
    ngx_http_core_main_conf_t  *cmcf;
    ngx_http_variable_value_t  *vv;

    r = njs_vm_external(vm, ngx_http_js_request_proto_id, value);
    if (r == NULL) {
        njs_value_undefined_set(retval);
        return NJS_DECLINED;
    }

    rc = njs_vm_prop_name(vm, prop, &val);
    if (rc != NJS_OK) {
        njs_value_undefined_set(retval);
        return NJS_DECLINED;
    }

    name.data = val.start;
    name.len = val.length;

    if (setval == NULL) {
        key = ngx_hash_strlow(name.data, name.data, name.len);

        vv = ngx_http_get_variable(r, &name, key);
        if (vv == NULL || vv->not_found) {
            njs_value_undefined_set(retval);
            return NJS_DECLINED;
        }

        return ngx_js_prop(vm, njs_vm_prop_magic32(prop), retval, vv->data,
                           vv->len);
    }

    cmcf = ngx_http_get_module_main_conf(r, ngx_http_core_module);

    key = ngx_hash_strlow(name.data, name.data, name.len);

    v = ngx_hash_find(&cmcf->variables_hash, key, name.data, name.len);

    if (v == NULL) {
        njs_vm_error(vm, "variable not found");
        return NJS_ERROR;
    }

    if (ngx_js_string(vm, setval, &s) != NGX_OK) {
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
        vv->data = s.start;
        vv->len = s.length;

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

    vv->data = ngx_pnalloc(r->pool, s.length);
    if (vv->data == NULL) {
        vv->valid = 0;
        njs_vm_error(vm, "internal error");
        return NJS_ERROR;
    }

    vv->len = s.length;
    ngx_memcpy(vv->data, s.start, vv->len);

    return NJS_OK;
}


static njs_int_t
ngx_http_js_promise_trampoline(njs_vm_t *vm, njs_value_t *args,
    njs_uint_t nargs, njs_index_t unused)
{
    ngx_uint_t           i;
    njs_function_t      *callback;
    ngx_http_js_cb_t    *cb, *cbs;
    ngx_http_js_ctx_t   *ctx;
    ngx_http_request_t  *r;

    r = njs_vm_external(vm, ngx_http_js_request_proto_id,
                        njs_arg(args, nargs, 1));
    ctx = ngx_http_get_module_ctx(r->parent, ngx_http_js_module);

    ngx_log_debug1(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
                   "js subrequest promise trampoline parent ctx: %p", ctx);

    if (ctx == NULL) {
        njs_vm_error(vm, "js subrequest: failed to get the parent context");
        return NJS_ERROR;
    }

    cbs = ctx->promise_callbacks.elts;

    if (cbs == NULL) {
        goto fail;
    }

    cb = NULL;

    for (i = 0; i < ctx->promise_callbacks.nelts; i++) {
        if (cbs[i].request == r) {
            cb = &cbs[i];
            cb->request = NULL;
            break;
        }
    }

    if (cb == NULL) {
        goto fail;
    }

    callback = njs_value_function(njs_value_arg(&cb->callbacks[0]));

    return njs_vm_call(vm, callback, njs_argument(args, 1), 1);

fail:

    njs_vm_error(vm, "js subrequest: promise callback not found");

    return NJS_ERROR;
}


static njs_int_t
ngx_http_js_ext_subrequest(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t unused)
{
    ngx_int_t                 rc, promise;
    njs_str_t                 uri_arg, args_arg, method_name, body_arg;
    ngx_uint_t                i, method, methods_max, has_body, detached;
    njs_value_t              *value, *arg, *options;
    njs_function_t           *callback;
    ngx_http_js_cb_t         *cb, *cbs;
    ngx_http_js_ctx_t        *ctx;
    njs_opaque_value_t        lvalue;
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

    r = njs_vm_external(vm, ngx_http_js_request_proto_id,
                        njs_argument(args, 0));
    if (r == NULL) {
        njs_vm_error(vm, "\"this\" is not an external");
        return NJS_ERROR;
    }

    ctx = ngx_http_get_module_ctx(r, ngx_http_js_module);

    if (ctx->vm != vm) {
        njs_vm_error(vm, "subrequest can only be created for "
                         "the primary request");
        return NJS_ERROR;
    }

    if (ngx_js_string(vm, njs_arg(args, nargs, 1), &uri_arg) != NGX_OK) {
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
        if (ngx_js_string(vm, arg, &args_arg) != NJS_OK) {
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
        value = njs_vm_object_prop(vm, options, &args_key, &lvalue);
        if (value != NULL) {
            if (ngx_js_string(vm, value, &args_arg) != NGX_OK) {
                njs_vm_error(vm, "failed to convert options.args");
                return NJS_ERROR;
            }
        }

        value = njs_vm_object_prop(vm, options, &detached_key, &lvalue);
        if (value != NULL) {
            detached = njs_value_bool(value);
        }

        value = njs_vm_object_prop(vm, options, &method_key, &lvalue);
        if (value != NULL) {
            if (ngx_js_string(vm, value, &method_name) != NGX_OK) {
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

        value = njs_vm_object_prop(vm, options, &body_key, &lvalue);
        if (value != NULL) {
            if (ngx_js_string(vm, value, &body_arg) != NGX_OK) {
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
        cbs = ctx->promise_callbacks.elts;

        if (cbs == NULL) {
            if (ngx_array_init(&ctx->promise_callbacks, r->pool, 4,
                               sizeof(ngx_http_js_cb_t)) != NGX_OK)
            {
                goto memory_error;
            }
        }

        cb = NULL;

        for (i = 0; i < ctx->promise_callbacks.nelts; i++) {
            if (cbs[i].request == NULL) {
                cb = &cbs[i];
                break;
            }
        }

        if (i == ctx->promise_callbacks.nelts) {
            cb = ngx_array_push(&ctx->promise_callbacks);
            if (cb == NULL) {
                goto memory_error;
            }
        }

        cb->request = sr;

        return njs_vm_promise_create(vm, njs_vm_retval(vm),
                                     njs_value_arg(&cb->callbacks));
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

    njs_int_t            ret;
    ngx_http_js_ctx_t   *ctx;
    njs_opaque_value_t   reply;

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
                                 ngx_http_js_request_proto_id, r, 0);
    if (ret != NJS_OK) {
        ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                      "js subrequest reply creation failed");

        return NGX_ERROR;
    }

    ngx_http_js_handle_vm_event(r->parent, vm_event, njs_value_arg(&reply), 1);

    return NGX_OK;
}


static njs_int_t
ngx_http_js_ext_get_parent(njs_vm_t *vm, njs_object_prop_t *prop,
    njs_value_t *value, njs_value_t *setval, njs_value_t *retval)
{
    ngx_http_js_ctx_t   *ctx;
    ngx_http_request_t  *r;

    r = njs_vm_external(vm, ngx_http_js_request_proto_id, value);
    if (r == NULL) {
        njs_value_undefined_set(retval);
        return NJS_DECLINED;
    }

    ctx = r->parent ? ngx_http_get_module_ctx(r->parent, ngx_http_js_module)
                    : NULL;

    if (ctx == NULL || ctx->vm != vm) {
        njs_value_undefined_set(retval);
        return NJS_DECLINED;
    }

    njs_value_assign(retval, njs_value_arg(&ctx->request));

    return NJS_OK;
}


static njs_int_t
ngx_http_js_ext_get_response_body(njs_vm_t *vm, njs_object_prop_t *prop,
    njs_value_t *value, njs_value_t *setval, njs_value_t *retval)
{
    size_t               len;
    u_char              *p;
    uint32_t             buffer_type;
    njs_int_t            ret;
    ngx_buf_t           *b;
    njs_value_t         *response_body;
    ngx_http_js_ctx_t   *ctx;
    ngx_http_request_t  *r;

    if (njs_vm_prop_magic32(prop) & NGX_JS_DEPRECATED) {
        njs_deprecated(vm, "r.responseBody");
    }

    r = njs_vm_external(vm, ngx_http_js_request_proto_id, value);
    if (r == NULL) {
        njs_value_undefined_set(retval);
        return NJS_DECLINED;
    }

    ctx = ngx_http_get_module_ctx(r, ngx_http_js_module);
    response_body = (njs_value_t *) &ctx->response_body;
    buffer_type = ngx_js_buffer_type(njs_vm_prop_magic32(prop));

    if (!njs_value_is_null(response_body)) {
        if ((buffer_type == NGX_JS_BUFFER)
            == (uint32_t) njs_value_is_buffer(response_body))
        {
            njs_value_assign(retval, response_body);
            return NJS_OK;
        }
    }

    b = r->out ? r->out->buf : NULL;

    if (b == NULL) {
        njs_value_undefined_set(retval);
        return NJS_OK;
    }

    len = b->last - b->pos;

    p = ngx_pnalloc(r->pool, len);
    if (p == NULL) {
        njs_vm_memory_error(vm);
        return NJS_ERROR;
    }

    if (len) {
        ngx_memcpy(p, b->pos, len);
    }

    ret = ngx_js_prop(vm, buffer_type, response_body, p, len);
    if (ret != NJS_OK) {
        return NJS_ERROR;
    }

    njs_value_assign(retval, response_body);

    return NJS_OK;
}


#if defined(nginx_version) && (nginx_version >= 1023000)
static njs_int_t
ngx_http_js_header_in(njs_vm_t *vm, ngx_http_request_t *r, unsigned flags,
    njs_str_t *name, njs_value_t *retval)
{
    u_char                      *lowcase_key;
    ngx_uint_t                   hash;
    ngx_table_elt_t            **ph;
    ngx_http_header_t           *hh;
    ngx_http_core_main_conf_t   *cmcf;

    if (retval == NULL) {
        return NJS_OK;
    }

    /* look up hashed headers */

    lowcase_key = ngx_pnalloc(r->pool, name->length);
    if (lowcase_key == NULL) {
        njs_vm_memory_error(vm);
        return NJS_ERROR;
    }

    hash = ngx_hash_strlow(lowcase_key, name->start, name->length);

    cmcf = ngx_http_get_module_main_conf(r, ngx_http_core_module);

    hh = ngx_hash_find(&cmcf->headers_in_hash, hash, lowcase_key,
                       name->length);

    ph = NULL;

    if (hh) {
        if (hh->offset == offsetof(ngx_http_headers_in_t, cookie)) {
            flags |= NJS_HEADER_SEMICOLON;
        }

        ph = (ngx_table_elt_t **) ((char *) &r->headers_in + hh->offset);
    }

    return ngx_http_js_header_generic(vm, r, &r->headers_in.headers, ph, flags,
                                      name, retval);
}


static njs_int_t
ngx_http_js_header_out(njs_vm_t *vm, ngx_http_request_t *r, unsigned flags,
    njs_str_t *name, njs_value_t *setval, njs_value_t *retval)
{
    u_char              *p;
    int64_t              length;
    njs_value_t         *array;
    njs_int_t            rc;
    njs_str_t            s;
    ngx_uint_t           i;
    ngx_list_part_t     *part;
    ngx_table_elt_t     *header, *h, **ph;
    njs_opaque_value_t   lvalue;

    if (retval != NULL && setval == NULL) {
        return ngx_http_js_header_generic(vm, r, &r->headers_out.headers, NULL,
                                          flags, name, retval);

    }

    part = &r->headers_out.headers.part;
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

        if (h->hash == 0
            || h->key.len != name->length
            || ngx_strncasecmp(h->key.data, name->start, name->length) != 0)
        {
            continue;
        }

        h->hash = 0;
        h->next = NULL;
    }

    if (retval == NULL) {
        return NJS_OK;
    }

    if (njs_value_is_array(setval)) {
        array = setval;

        rc = njs_vm_array_length(vm, array, &length);
        if (rc != NJS_OK) {
            return NJS_ERROR;
        }

        if (length == 0) {
            return NJS_OK;
        }

    } else {
        array = NULL;
        length = 1;
    }

    ph = &header;

    for (i = 0; i < (ngx_uint_t) length; i++) {
        if (array != NULL) {
            setval = njs_vm_array_prop(vm, array, i, &lvalue);
        }

        if (ngx_js_string(vm, setval, &s) != NGX_OK) {
            return NJS_ERROR;
        }

        if (s.length == 0) {
            continue;
        }

        h = ngx_list_push(&r->headers_out.headers);
        if (h == NULL) {
            njs_vm_memory_error(vm);
            return NJS_ERROR;
        }

        p = ngx_pnalloc(r->pool, name->length);
        if (p == NULL) {
            h->hash = 0;
            njs_vm_memory_error(vm);
            return NJS_ERROR;
        }

        ngx_memcpy(p, name->start, name->length);

        h->key.data = p;
        h->key.len = name->length;

        p = ngx_pnalloc(r->pool, s.length);
        if (p == NULL) {
            h->hash = 0;
            njs_vm_memory_error(vm);
            return NJS_ERROR;
        }

        ngx_memcpy(p, s.start, s.length);

        h->value.data = p;
        h->value.len = s.length;
        h->hash = 1;

        *ph = h;
        ph = &h->next;
    }

    *ph = NULL;

    return NJS_OK;
}


static njs_int_t
ngx_http_js_content_encoding(njs_vm_t *vm, ngx_http_request_t *r,
    unsigned flags, njs_str_t *v, njs_value_t *setval, njs_value_t *retval)
{
    njs_int_t         rc;
    ngx_table_elt_t  *h;

    rc = ngx_http_js_header_out_special(vm, r, v, setval, retval, &h);
    if (rc == NJS_ERROR) {
        return NJS_ERROR;
    }

    if (setval != NULL || retval == NULL) {
        r->headers_out.content_encoding = h;
    }

    return NJS_OK;
}


static njs_int_t
ngx_http_js_content_length(njs_vm_t *vm, ngx_http_request_t *r,
    unsigned flags, njs_str_t *v, njs_value_t *setval, njs_value_t *retval)
{
    u_char           *p, *start;
    njs_int_t         rc;
    ngx_int_t         n;
    ngx_table_elt_t  *h;
    u_char            content_len[NGX_OFF_T_LEN];

    if (retval != NULL && setval == NULL) {
        if (r->headers_out.content_length == NULL
            && r->headers_out.content_length_n >= 0)
        {
            p = ngx_sprintf(content_len, "%O", r->headers_out.content_length_n);

            start = njs_vm_value_string_alloc(vm, retval, p - content_len);
            if (start == NULL) {
                return NJS_ERROR;
            }

            ngx_memcpy(start, content_len, p - content_len);

            return NJS_OK;
        }
    }

    rc = ngx_http_js_header_out_special(vm, r, v, setval, retval, &h);
    if (rc == NJS_ERROR) {
        return NJS_ERROR;
    }

    if (setval != NULL || retval == NULL) {
        if (h != NULL) {
            n = ngx_atoi(h->value.data, h->value.len);
            if (n == NGX_ERROR) {
                h->hash = 0;
                njs_vm_error(vm, "failed converting argument "
                             "to positive integer");
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
ngx_http_js_content_type(njs_vm_t *vm, ngx_http_request_t *r,
    unsigned flags, njs_str_t *v, njs_value_t *setval, njs_value_t *retval)
{
    int64_t              length;
    njs_int_t            rc;
    njs_str_t            s;
    ngx_str_t           *hdr;
    njs_opaque_value_t   lvalue;

    if (retval != NULL && setval == NULL) {
        hdr = &r->headers_out.content_type;

        if (hdr->len == 0) {
            njs_value_undefined_set(retval);
            return NJS_OK;
        }

        return njs_vm_value_string_set(vm, retval, hdr->data, hdr->len);
    }

    if (setval != NULL && njs_value_is_array(setval)) {
        rc = njs_vm_array_length(vm, setval, &length);
        if (rc != NJS_OK) {
            return NJS_ERROR;
        }

        setval = njs_vm_array_prop(vm, setval, length - 1, &lvalue);
    }

    if (ngx_js_string(vm, setval, &s) != NGX_OK) {
        return NJS_ERROR;
    }

    r->headers_out.content_type.len = s.length;
    r->headers_out.content_type_len = r->headers_out.content_type.len;
    r->headers_out.content_type.data = s.start;
    r->headers_out.content_type_lowcase = NULL;

    return NJS_OK;
}


static njs_int_t
ngx_http_js_header_out_special(njs_vm_t *vm, ngx_http_request_t *r,
    njs_str_t *v, njs_value_t *setval, njs_value_t *retval,
    ngx_table_elt_t **hh)
{
    u_char              *p;
    int64_t              length;
    njs_int_t            rc;
    njs_str_t            s;
    ngx_uint_t           i;
    ngx_list_t          *headers;
    ngx_list_part_t     *part;
    ngx_table_elt_t     *header, *h;
    njs_opaque_value_t   lvalue;

    headers = &r->headers_out.headers;

    if (retval != NULL && setval == NULL) {
        return ngx_http_js_header_out(vm, r, NJS_HEADER_SINGLE, v, setval,
                                      retval);
    }

    if (setval != NULL && njs_value_is_array(setval)) {
        rc = njs_vm_array_length(vm, setval, &length);
        if (rc != NJS_OK) {
            return NJS_ERROR;
        }

        setval = njs_vm_array_prop(vm, setval, length - 1, &lvalue);
    }

    if (ngx_js_string(vm, setval, &s) != NGX_OK) {
        return NJS_ERROR;
    }

    part = &headers->part;
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

        if (h->key.len == v->length
            && ngx_strncasecmp(h->key.data, v->start, v->length) == 0)
        {
            goto done;
        }
    }

    h = NULL;

done:

    if (h != NULL && s.length == 0) {
        h->hash = 0;
        h = NULL;
    }

    if (h == NULL && s.length != 0) {
        h = ngx_list_push(headers);
        if (h == NULL) {
            njs_vm_memory_error(vm);
            return NJS_ERROR;
        }

        p = ngx_pnalloc(r->pool, v->length);
        if (p == NULL) {
            h->hash = 0;
            njs_vm_memory_error(vm);
            return NJS_ERROR;
        }

        ngx_memcpy(p, v->start, v->length);

        h->key.data = p;
        h->key.len = v->length;
    }

    if (h != NULL) {
        p = ngx_pnalloc(r->pool, s.length);
        if (p == NULL) {
            h->hash = 0;
            njs_vm_memory_error(vm);
            return NJS_ERROR;
        }

        ngx_memcpy(p, s.start, s.length);

        h->value.data = p;
        h->value.len = s.length;
        h->hash = 1;
    }

    if (hh != NULL) {
        *hh = h;
    }

    return NJS_OK;
}


static njs_int_t
ngx_http_js_header_generic(njs_vm_t *vm, ngx_http_request_t *r,
    ngx_list_t *headers, ngx_table_elt_t **ph, unsigned flags, njs_str_t *name,
    njs_value_t *retval)
{
    u_char           *p, sep;
    ssize_t           size;
    njs_int_t         rc;
    ngx_uint_t        i;
    njs_value_t      *value;
    ngx_list_part_t  *part;
    ngx_table_elt_t  *header, *h;

    if (ph == NULL) {
        /* iterate over all headers */

        ph = &header;
        part = &headers->part;
        h = part->elts;

        for (i = 0; /* void */ ; i++) {

            if (i >= part->nelts) {
                if (part->next == NULL) {
                    break;
                }

                part = part->next;
                h = part->elts;
                i = 0;
            }

            if (h[i].hash == 0
                || name->length != h[i].key.len
                || ngx_strncasecmp(name->start, h[i].key.data, name->length)
                   != 0)
            {
                continue;
            }

            *ph = &h[i];
            ph = &h[i].next;
        }

        *ph = NULL;
        ph = &header;
    }

    if (*ph == NULL) {
        njs_value_undefined_set(retval);
        return NJS_DECLINED;
    }

    if (flags & NJS_HEADER_ARRAY) {
        rc = njs_vm_array_alloc(vm, retval, 4);
        if (rc != NJS_OK) {
            return NJS_ERROR;
        }

        for (h = *ph; h; h = h->next) {
            value = njs_vm_array_push(vm, retval);
            if (value == NULL) {
                return NJS_ERROR;
            }

            rc = njs_vm_value_string_set(vm, value, h->value.data,
                                         h->value.len);
            if (rc != NJS_OK) {
                return NJS_ERROR;
            }
        }

        return NJS_OK;
    }

    if ((*ph)->next == NULL || flags & NJS_HEADER_SINGLE) {
        return njs_vm_value_string_set(vm, retval, (*ph)->value.data,
                                       (*ph)->value.len);
    }

    size = - (ssize_t) njs_length("; ");

    for (h = *ph; h; h = h->next) {
        size += h->value.len + njs_length("; ");
    }

    p = njs_vm_value_string_alloc(vm, retval, size);
    if (p == NULL) {
        return NJS_ERROR;
    }

    sep = flags & NJS_HEADER_SEMICOLON ? ';' : ',';

    for (h = *ph; h; h = h->next) {
        p = ngx_copy(p, h->value.data, h->value.len);

        if (h->next == NULL) {
            break;
        }

        *p++ = sep; *p++ = ' ';
    }

    return NJS_OK;
}
#endif


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
    ngx_http_request_t   *r;
    ngx_http_js_event_t  *js_event;

    js_event = (ngx_http_js_event_t *) ev->data;

    r = js_event->request;

    ngx_http_js_handle_event(r, js_event->vm_event, NULL, 0);
}


static ngx_pool_t *
ngx_http_js_pool(njs_vm_t *vm, ngx_http_request_t *r)
{
    return r->pool;
}


static ngx_resolver_t *
ngx_http_js_resolver(njs_vm_t *vm, ngx_http_request_t *r)
{
    ngx_http_core_loc_conf_t  *clcf;

    clcf = ngx_http_get_module_loc_conf(r, ngx_http_core_module);

    return clcf->resolver;
}


static ngx_msec_t
ngx_http_js_resolver_timeout(njs_vm_t *vm, ngx_http_request_t *r)
{
    ngx_http_core_loc_conf_t  *clcf;

    clcf = ngx_http_get_module_loc_conf(r, ngx_http_core_module);

    return clcf->resolver_timeout;
}


static ngx_msec_t
ngx_http_js_fetch_timeout(njs_vm_t *vm, ngx_http_request_t *r)
{
    ngx_http_js_loc_conf_t  *jlcf;

    jlcf = ngx_http_get_module_loc_conf(r, ngx_http_js_module);

    return jlcf->timeout;
}


static size_t
ngx_http_js_buffer_size(njs_vm_t *vm, ngx_http_request_t *r)
{
    ngx_http_js_loc_conf_t  *jlcf;

    jlcf = ngx_http_get_module_loc_conf(r, ngx_http_js_module);

    return jlcf->buffer_size;
}


static size_t
ngx_http_js_max_response_buffer_size(njs_vm_t *vm, ngx_http_request_t *r)
{
    ngx_http_js_loc_conf_t  *jlcf;

    jlcf = ngx_http_get_module_loc_conf(r, ngx_http_js_module);

    return jlcf->max_response_body_size;
}


static void
ngx_http_js_handle_vm_event(ngx_http_request_t *r, njs_vm_event_t vm_event,
    njs_value_t *args, njs_uint_t nargs)
{
    njs_int_t           rc;
    ngx_str_t           exception;
    ngx_http_js_ctx_t  *ctx;

    ctx = ngx_http_get_module_ctx(r, ngx_http_js_module);

    njs_vm_post_event(ctx->vm, vm_event, args, nargs);

    rc = njs_vm_run(ctx->vm);

    ngx_log_debug2(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
                   "http js post event handler rc: %i event: %p",
                   (ngx_int_t) rc, vm_event);

    if (rc == NJS_ERROR) {
        ngx_js_retval(ctx->vm, NULL, &exception);

        ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                      "js exception: %V", &exception);

        ngx_http_finalize_request(r, NGX_ERROR);
    }

    if (rc == NJS_OK) {
        ngx_http_post_request(r, NULL);
    }
}


static void
ngx_http_js_handle_event(ngx_http_request_t *r, njs_vm_event_t vm_event,
    njs_value_t *args, njs_uint_t nargs)
{
    ngx_http_js_handle_vm_event(r, vm_event, args, nargs);

    ngx_http_run_posted_requests(r->connection);
}


static ngx_int_t
ngx_http_js_externals_init(ngx_conf_t *cf, ngx_js_conf_t *conf_in)
{
    ngx_http_js_loc_conf_t        *conf = (ngx_http_js_loc_conf_t *) conf_in;

    ngx_http_js_request_proto_id = njs_vm_external_prototype(conf->vm,
                                           ngx_http_js_ext_request,
                                           njs_nitems(ngx_http_js_ext_request));
    if (ngx_http_js_request_proto_id < 0) {
        ngx_log_error(NGX_LOG_EMERG, cf->log, 0,
                      "failed to add js request proto");
        return NGX_ERROR;
    }

    return NGX_OK;
}


static ngx_int_t
ngx_http_js_init_conf_vm(ngx_conf_t *cf, ngx_js_conf_t *conf)
{
    njs_vm_opt_t  options;

    njs_vm_opt_init(&options);

    options.backtrace = 1;
    options.unhandled_rejection = NJS_VM_OPT_UNHANDLED_REJECTION_THROW;
    options.ops = &ngx_http_js_ops;
    options.metas = &ngx_http_js_metas;
    options.addons = njs_js_addon_modules;
    options.argv = ngx_argv;
    options.argc = ngx_argc;

    return ngx_js_init_conf_vm(cf, conf, &options, ngx_http_js_externals_init);
}


static ngx_int_t
ngx_http_js_init(ngx_conf_t *cf)
{
    ngx_http_next_header_filter = ngx_http_top_header_filter;
    ngx_http_top_header_filter = ngx_http_js_header_filter;

    ngx_http_next_body_filter = ngx_http_top_body_filter;
    ngx_http_top_body_filter = ngx_http_js_body_filter;

    return NGX_OK;
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

    v->get_handler = ngx_http_js_variable_set;
    v->data = (uintptr_t) fname;

    return NGX_CONF_OK;
}


static char *
ngx_http_js_var(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    ngx_str_t                         *value;
    ngx_int_t                          index;
    ngx_http_variable_t               *v;
    ngx_http_complex_value_t          *cv;
    ngx_http_compile_complex_value_t   ccv;

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

    index = ngx_http_get_variable_index(cf, &value[1]);
    if (index == NGX_ERROR) {
        return NGX_CONF_ERROR;
    }

    cv = NULL;

    if (cf->args->nelts == 3) {
        cv = ngx_palloc(cf->pool, sizeof(ngx_http_complex_value_t));
        if (cv == NULL) {
            return NGX_CONF_ERROR;
        }

        ngx_memzero(&ccv, sizeof(ngx_http_compile_complex_value_t));

        ccv.cf = cf;
        ccv.value = &value[2];
        ccv.complex_value = cv;

        if (ngx_http_compile_complex_value(&ccv) != NGX_OK) {
            return NGX_CONF_ERROR;
        }
    }

    v->get_handler = ngx_http_js_variable_var;
    v->data = (uintptr_t) cv;

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


static char *
ngx_http_js_body_filter_set(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    ngx_http_js_loc_conf_t *jlcf = conf;

    ngx_str_t  *value;

    if (jlcf->body_filter.data) {
        return "is duplicate";
    }

    value = cf->args->elts;
    jlcf->body_filter = value[1];

    jlcf->buffer_type = NGX_JS_STRING;

    if (cf->args->nelts == 3
         && ngx_strncmp(value[2].data, "buffer_type=", 12) == 0)
    {
        if (ngx_strcmp(&value[2].data[12], "string") == 0) {
            jlcf->buffer_type = NGX_JS_STRING;

        } else if (ngx_strcmp(&value[2].data[12], "buffer") == 0) {
            jlcf->buffer_type = NGX_JS_BUFFER;

        } else {
            ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                               "invalid buffer_type value \"%V\", "
                               "it must be \"string\" or \"buffer\"",
                               &value[2]);
            return NGX_CONF_ERROR;
        }
    }

    return NGX_CONF_OK;
}


static void *
ngx_http_js_create_loc_conf(ngx_conf_t *cf)
{
    ngx_http_js_loc_conf_t  *conf =
        (ngx_http_js_loc_conf_t *) ngx_js_create_conf(
                                            cf, sizeof(ngx_http_js_loc_conf_t));
    if (conf == NULL) {
        return NULL;
    }

#if (NGX_HTTP_SSL)
    conf->ssl_verify = NGX_CONF_UNSET;
    conf->ssl_verify_depth = NGX_CONF_UNSET;
#endif
    return conf;
}


static char *
ngx_http_js_merge_loc_conf(ngx_conf_t *cf, void *parent, void *child)
{
    ngx_http_js_loc_conf_t *prev = parent;
    ngx_http_js_loc_conf_t *conf = child;

    ngx_conf_merge_str_value(conf->content, prev->content, "");
    ngx_conf_merge_str_value(conf->header_filter, prev->header_filter, "");
    ngx_conf_merge_str_value(conf->body_filter, prev->body_filter, "");
    ngx_conf_merge_uint_value(conf->buffer_type, prev->buffer_type,
                              NGX_JS_STRING);

    return ngx_js_merge_conf(cf, parent, child, ngx_http_js_init_conf_vm);
}


static ngx_ssl_t *
ngx_http_js_ssl(njs_vm_t *vm, ngx_http_request_t *r)
{
#if (NGX_HTTP_SSL)
    ngx_http_js_loc_conf_t  *jlcf;

    jlcf = ngx_http_get_module_loc_conf(r, ngx_http_js_module);

    return jlcf->ssl;
#else
    return NULL;
#endif
}


static ngx_flag_t
ngx_http_js_ssl_verify(njs_vm_t *vm, ngx_http_request_t *r)
{
#if (NGX_HTTP_SSL)
    ngx_http_js_loc_conf_t  *jlcf;

    jlcf = ngx_http_get_module_loc_conf(r, ngx_http_js_module);

    return jlcf->ssl_verify;
#else
    return 0;
#endif
}
