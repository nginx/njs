
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
    NGX_JS_COMMON_LOC_CONF;

    ngx_http_complex_value_t  fetch_proxy_cv;

    ngx_str_t              content;
    ngx_str_t              header_filter;
    ngx_str_t              body_filter;
    ngx_uint_t             buffer_type;
} ngx_http_js_loc_conf_t;


typedef struct {
    ngx_http_conf_ctx_t   *conf_ctx;
    ngx_connection_t      *connection;
    uint8_t               *worker_affinity;

    /**
     * fd is used for event debug and should be at the same position
     * as in ngx_connection_t: after a 3rd pointer.
     */
    ngx_socket_t           fd;

    ngx_str_t              method;
    ngx_msec_t             interval;
    ngx_msec_t             jitter;

    ngx_log_t              log;
    ngx_http_log_ctx_t     log_ctx;
    ngx_event_t            event;
} ngx_js_periodic_t;


#define NJS_HEADER_SEMICOLON   0x1
#define NJS_HEADER_SINGLE      0x2
#define NJS_HEADER_ARRAY       0x4
#define NJS_HEADER_GET         0x8


typedef struct ngx_http_js_ctx_s  ngx_http_js_ctx_t;

struct ngx_http_js_ctx_s {
    NGX_JS_COMMON_CTX;
    ngx_uint_t             done;
    ngx_int_t              status;
    njs_opaque_value_t     rargs;
    njs_opaque_value_t     request_body;
    njs_opaque_value_t     response_body;
    ngx_str_t              redirect_uri;

    ngx_int_t              filter;
    ngx_buf_t             *buf;
    ngx_chain_t          **last_out;
    ngx_chain_t           *free;
    ngx_chain_t           *busy;
    ngx_int_t            (*body_filter)(ngx_http_request_t *r,
                                        ngx_http_js_loc_conf_t *jlcf,
                                        ngx_http_js_ctx_t *ctx,
                                        ngx_chain_t *in);

    ngx_js_periodic_t     *periodic;
};


typedef struct {
    ngx_str_t              name;
    unsigned               flags;
    uintptr_t              handler;
}  ngx_http_js_header_t;


typedef njs_int_t (*njs_http_js_header_handler_t)(njs_vm_t *vm,
    ngx_http_request_t *r, unsigned flags, njs_str_t *name, njs_value_t *setval,
    njs_value_t *retval);
typedef njs_int_t (*njs_http_js_header_handler122_t)(njs_vm_t *vm,
    ngx_http_request_t *r, ngx_list_t *headers, njs_str_t *name,
    njs_value_t *setval, njs_value_t *retval);
#if (NJS_HAVE_QUICKJS)
typedef int (*njs_http_qjs_header_handler_t)(JSContext *cx,
    ngx_http_request_t *r, ngx_str_t *name, JSPropertyDescriptor *pdesc,
    JSValue *value, unsigned flags);


typedef struct {
    ngx_http_request_t  *request;
    JSValue              args;
    JSValue              request_body;
    JSValue              response_body;
} ngx_http_qjs_request_t;

#endif


typedef struct {
    ngx_str_t   name;
    ngx_uint_t  value;
} ngx_http_js_entry_t;


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
static ngx_int_t ngx_http_js_init_vm(ngx_http_request_t *r, njs_int_t proto_id);
static void ngx_http_js_cleanup_ctx(void *data);

static njs_int_t ngx_http_js_ext_keys_header(njs_vm_t *vm, njs_value_t *value,
    njs_value_t *keys, ngx_list_t *headers);
#if defined(nginx_version) && (nginx_version < 1023000)
static ngx_table_elt_t *ngx_http_js_get_header(ngx_list_part_t *part,
    u_char *data, size_t len);
#endif
static njs_int_t ngx_http_js_ext_raw_header(njs_vm_t *vm,
    njs_object_prop_t *prop, uint32_t unused, njs_value_t *value,
    njs_value_t *setval, njs_value_t *retval);
static njs_int_t ngx_http_js_ext_header_out(njs_vm_t *vm,
    njs_object_prop_t *prop, uint32_t atom_id, njs_value_t *value,
    njs_value_t *setval, njs_value_t *retval);
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
static njs_int_t ngx_http_js_content_encoding122(njs_vm_t *vm,
    ngx_http_request_t *r, ngx_list_t *headers, njs_str_t *name,
    njs_value_t *setval, njs_value_t *retval);
static njs_int_t ngx_http_js_content_length122(njs_vm_t *vm,
    ngx_http_request_t *r, ngx_list_t *headers, njs_str_t *name,
    njs_value_t *setval, njs_value_t *retval);
static njs_int_t ngx_http_js_content_type122(njs_vm_t *vm,
    ngx_http_request_t *r, ngx_list_t *headers, njs_str_t *name,
    njs_value_t *setval, njs_value_t *retval);
static njs_int_t ngx_http_js_date122(njs_vm_t *vm, ngx_http_request_t *r,
    ngx_list_t *headers, njs_str_t *name, njs_value_t *setval,
    njs_value_t *retval);
static njs_int_t ngx_http_js_last_modified122(njs_vm_t *vm,
    ngx_http_request_t *r, ngx_list_t *headers, njs_str_t *name,
    njs_value_t *setval, njs_value_t *retval);
static njs_int_t ngx_http_js_location122(njs_vm_t *vm, ngx_http_request_t *r,
    ngx_list_t *headers, njs_str_t *name, njs_value_t *setval,
    njs_value_t *retval);
static njs_int_t ngx_http_js_server122(njs_vm_t *vm, ngx_http_request_t *r,
    ngx_list_t *headers, njs_str_t *name, njs_value_t *setval,
    njs_value_t *retval);
#endif
static njs_int_t ngx_http_js_ext_keys_header_out(njs_vm_t *vm,
    njs_value_t *value, njs_value_t *keys);
static njs_int_t ngx_http_js_ext_status(njs_vm_t *vm, njs_object_prop_t *prop,
    uint32_t unused, njs_value_t *value, njs_value_t *setval,
    njs_value_t *retval);
static njs_int_t ngx_http_js_ext_send_header(njs_vm_t *vm, njs_value_t *args,
    njs_uint_t nargs, njs_index_t unused, njs_value_t *retval);
static njs_int_t ngx_http_js_ext_send(njs_vm_t *vm, njs_value_t *args,
    njs_uint_t nargs, njs_index_t unused, njs_value_t *retval);
static njs_int_t ngx_http_js_ext_send_buffer(njs_vm_t *vm, njs_value_t *args,
    njs_uint_t nargs, njs_index_t unused, njs_value_t *retval);
static njs_int_t ngx_http_js_ext_set_return_value(njs_vm_t *vm,
    njs_value_t *args, njs_uint_t nargs, njs_index_t unused,
    njs_value_t *retval);
static njs_int_t ngx_http_js_ext_done(njs_vm_t *vm, njs_value_t *args,
    njs_uint_t nargs, njs_index_t unused, njs_value_t *retval);
static njs_int_t ngx_http_js_ext_finish(njs_vm_t *vm, njs_value_t *args,
    njs_uint_t nargs, njs_index_t unused, njs_value_t *retval);
static njs_int_t ngx_http_js_ext_return(njs_vm_t *vm, njs_value_t *args,
    njs_uint_t nargs, njs_index_t unused, njs_value_t *retval);
static njs_int_t ngx_http_js_ext_internal_redirect(njs_vm_t *vm,
    njs_value_t *args, njs_uint_t nargs, njs_index_t unused,
    njs_value_t *retval);

static njs_int_t ngx_http_js_ext_get_http_version(njs_vm_t *vm,
    njs_object_prop_t *prop, uint32_t unused, njs_value_t *value,
    njs_value_t *setval, njs_value_t *retval);
static njs_int_t ngx_http_js_ext_internal(njs_vm_t *vm,
    njs_object_prop_t *prop, uint32_t unused, njs_value_t *value,
    njs_value_t *setval, njs_value_t *retval);
static njs_int_t ngx_http_js_ext_get_remote_address(njs_vm_t *vm,
    njs_object_prop_t *prop, uint32_t unused, njs_value_t *value,
    njs_value_t *setval, njs_value_t *retval);
static njs_int_t ngx_http_js_ext_get_args(njs_vm_t *vm,
    njs_object_prop_t *prop, uint32_t unused, njs_value_t *value,
    njs_value_t *setval, njs_value_t *retval);
static njs_int_t ngx_http_js_ext_get_request_body(njs_vm_t *vm,
    njs_object_prop_t *prop, uint32_t unused, njs_value_t *value,
    njs_value_t *setval, njs_value_t *retval);
static njs_int_t ngx_http_js_ext_header_in(njs_vm_t *vm,
    njs_object_prop_t *prop, uint32_t atom_id, njs_value_t *value,
    njs_value_t *setval, njs_value_t *retval);
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
    njs_object_prop_t *prop, uint32_t atom_id, njs_value_t *value,
    njs_value_t *setval, njs_value_t *retval);
static njs_int_t ngx_http_js_periodic_session_variables(njs_vm_t *vm,
    njs_object_prop_t *prop, uint32_t atom_id, njs_value_t *value,
    njs_value_t *setval, njs_value_t *retval);
static njs_int_t ngx_http_js_ext_subrequest(njs_vm_t *vm, njs_value_t *args,
    njs_uint_t nargs, njs_index_t unused, njs_value_t *retval);
static ngx_int_t ngx_http_js_subrequest_done(ngx_http_request_t *r,
    void *data, ngx_int_t rc);
static njs_int_t ngx_http_js_ext_get_parent(njs_vm_t *vm,
    njs_object_prop_t *prop, uint32_t unused, njs_value_t *value,
    njs_value_t *setval, njs_value_t *retval);
static njs_int_t ngx_http_js_ext_get_response_body(njs_vm_t *vm,
    njs_object_prop_t *prop, uint32_t unused, njs_value_t *value,
    njs_value_t *setval, njs_value_t *retval);

#if defined(nginx_version) && (nginx_version >= 1023000)
static njs_int_t ngx_http_js_header_in(njs_vm_t *vm, ngx_http_request_t *r,
    unsigned flags, njs_str_t *name, njs_value_t *retval);
static njs_int_t ngx_http_js_header_out(njs_vm_t *vm, ngx_http_request_t *r,
    unsigned flags, njs_str_t *name, njs_value_t *setval, njs_value_t *retval);
static njs_int_t ngx_http_js_header_out_special(njs_vm_t *vm,
    ngx_http_request_t *r, njs_str_t *v, njs_value_t *setval,
    njs_value_t *retval, ngx_table_elt_t **hh);
static njs_int_t ngx_http_js_header_generic(njs_vm_t *vm,
    ngx_http_request_t *r, ngx_list_t *headers, ngx_table_elt_t **ph,
    unsigned flags, njs_str_t *name, njs_value_t *retval);
#endif
static njs_int_t ngx_http_js_content_encoding(njs_vm_t *vm,
    ngx_http_request_t *r, unsigned flags, njs_str_t *name,
    njs_value_t *setval, njs_value_t *retval);
static njs_int_t ngx_http_js_content_length(njs_vm_t *vm, ngx_http_request_t *r,
    unsigned flags, njs_str_t *name, njs_value_t *setval,
    njs_value_t *retval);
static njs_int_t ngx_http_js_content_type(njs_vm_t *vm, ngx_http_request_t *r,
    unsigned flags, njs_str_t *name, njs_value_t *setval,
    njs_value_t *retval);
static njs_int_t ngx_http_js_date(njs_vm_t *vm, ngx_http_request_t *r,
    unsigned flags, njs_str_t *name, njs_value_t *setval,
    njs_value_t *retval);
static njs_int_t ngx_http_js_last_modified(njs_vm_t *vm, ngx_http_request_t *r,
    unsigned flags, njs_str_t *name, njs_value_t *setval,
    njs_value_t *retval);
static njs_int_t ngx_http_js_location(njs_vm_t *vm, ngx_http_request_t *r,
    unsigned flags, njs_str_t *name, njs_value_t *setval,
    njs_value_t *retval);
static njs_int_t ngx_http_js_server(njs_vm_t *vm, ngx_http_request_t *r,
    unsigned flags, njs_str_t *name, njs_value_t *setval,
    njs_value_t *retval);

#if (NJS_HAVE_QUICKJS)
static JSValue ngx_http_qjs_ext_args(JSContext *cx, JSValueConst this_val);
static JSValue ngx_http_qjs_ext_done(JSContext *cx, JSValueConst this_val,
    int argc, JSValueConst *argv);
static JSValue ngx_http_qjs_ext_finish(JSContext *cx, JSValueConst this_val,
    int argc, JSValueConst *argv);
static JSValue ngx_http_qjs_ext_headers_in(JSContext *cx,
    JSValueConst this_val);
static JSValue ngx_http_qjs_ext_headers_out(JSContext *cx,
    JSValueConst this_val);
static JSValue ngx_http_qjs_ext_http_version(JSContext *cx,
    JSValueConst this_val);
static JSValue ngx_http_qjs_ext_internal(JSContext *cx, JSValueConst this_val);
static JSValue ngx_http_qjs_ext_internal_redirect(JSContext *cx,
    JSValueConst this_val, int argc, JSValueConst *argv);
static JSValue ngx_http_qjs_ext_log(JSContext *cx, JSValueConst this_val,
    int argc, JSValueConst *argv, int level);
static JSValue ngx_http_qjs_ext_periodic_variables(JSContext *cx,
    JSValueConst this_val, int type);
static JSValue ngx_http_qjs_ext_parent(JSContext *cx, JSValueConst this_val);
static JSValue ngx_http_qjs_ext_remote_address(JSContext *cx,
    JSValueConst this_val);
static JSValue ngx_http_qjs_ext_request_body(JSContext *cx,
    JSValueConst this_val, int type);
static JSValue ngx_http_qjs_ext_response_body(JSContext *cx,
    JSValueConst this_val, int type);
static JSValue ngx_http_qjs_ext_return(JSContext *cx, JSValueConst this_val,
    int argc, JSValueConst *argv);
static JSValue ngx_http_qjs_ext_send(JSContext *cx, JSValueConst this_val,
    int argc, JSValueConst *argv);
static JSValue ngx_http_qjs_ext_send_buffer(JSContext *cx,
    JSValueConst this_val, int argc, JSValueConst *argv);
static JSValue ngx_http_qjs_ext_send_header(JSContext *cx,
    JSValueConst this_val, int argc, JSValueConst *argv);
static JSValue ngx_http_qjs_ext_set_return_value(JSContext *cx,
    JSValueConst this_val, int argc, JSValueConst *argv);
static JSValue ngx_http_qjs_ext_status_get(JSContext *cx,
    JSValueConst this_val);
static JSValue ngx_http_qjs_ext_status_set(JSContext *cx, JSValueConst this_val,
    JSValueConst value);
static JSValue ngx_http_qjs_ext_string(JSContext *cx, JSValueConst this_val,
    int offset);
static JSValue ngx_http_qjs_ext_subrequest(JSContext *cx, JSValueConst this_val,
    int argc, JSValueConst *argv);
static JSValue ngx_http_qjs_ext_raw_headers(JSContext *cx,
    JSValueConst this_val, int out);
static JSValue ngx_http_qjs_ext_variables(JSContext *cx,
    JSValueConst this_val, int type);

static int ngx_http_qjs_variables_own_property(JSContext *cx,
    JSPropertyDescriptor *pdesc, JSValueConst obj, JSAtom prop);
static int ngx_http_qjs_variables_set_property(JSContext *cx, JSValueConst obj,
    JSAtom atom, JSValueConst value, JSValueConst receiver, int flags);

static int ngx_http_qjs_headers_in_own_property(JSContext *cx,
    JSPropertyDescriptor *pdesc, JSValueConst obj, JSAtom prop);
static int ngx_http_qjs_headers_in_own_property_names(JSContext *ctx,
    JSPropertyEnum **ptab, uint32_t *plen, JSValueConst obj);

static int ngx_http_qjs_headers_out_own_property(JSContext *cx,
    JSPropertyDescriptor *pdesc, JSValueConst obj, JSAtom prop);
static int ngx_http_qjs_headers_out_own_property_names(JSContext *cx,
    JSPropertyEnum **ptab, uint32_t *plen, JSValueConst obj);
static int ngx_http_qjs_headers_out_set_property(JSContext *cx,
    JSValueConst obj, JSAtom atom, JSValueConst value, JSValueConst receiver,
    int flags);
static int ngx_http_qjs_headers_out_define_own_property(JSContext *cx,
    JSValueConst this_obj, JSAtom prop, JSValueConst val, JSValueConst getter,
    JSValueConst setter, int flags);
static int ngx_http_qjs_headers_out_delete_property(JSContext *cx,
    JSValueConst obj, JSAtom prop);

static ngx_http_request_t *ngx_http_qjs_request(JSValueConst val);
static JSValue ngx_http_qjs_request_make(JSContext *cx, ngx_int_t proto_id,
    ngx_http_request_t *r);
static void ngx_http_qjs_request_finalizer(JSRuntime *rt, JSValue val);
static void ngx_http_qjs_periodic_finalizer(JSRuntime *rt, JSValue val);
#endif

static ngx_pool_t *ngx_http_js_pool(ngx_http_request_t *r);
static ngx_resolver_t *ngx_http_js_resolver(ngx_http_request_t *r);
static ngx_msec_t ngx_http_js_resolver_timeout(ngx_http_request_t *r);
static void ngx_http_js_event_finalize(ngx_http_request_t *r, ngx_int_t rc);
static ngx_js_loc_conf_t *ngx_http_js_loc_conf(ngx_http_request_t *r);
static ngx_js_ctx_t *ngx_http_js_ctx(ngx_http_request_t *r);

static void ngx_http_js_periodic_handler(ngx_event_t *ev);
static void ngx_http_js_periodic_write_event_handler(ngx_http_request_t *r);
static void ngx_http_js_periodic_shutdown_handler(ngx_event_t *ev);
static void ngx_http_js_periodic_finalize(ngx_http_request_t *r, ngx_int_t rc);
static void ngx_http_js_periodic_destroy(ngx_http_request_t *r,
    ngx_js_periodic_t *periodic);

static njs_int_t ngx_js_http_init(njs_vm_t *vm);
static ngx_int_t ngx_http_js_init(ngx_conf_t *cf);
static ngx_int_t ngx_http_js_init_worker(ngx_cycle_t *cycle);
static char *ngx_http_js_periodic(ngx_conf_t *cf, ngx_command_t *cmd,
    void *conf);
static char *ngx_http_js_set(ngx_conf_t *cf, ngx_command_t *cmd, void *conf);
static char *ngx_http_js_var(ngx_conf_t *cf, ngx_command_t *cmd, void *conf);
static char *ngx_http_js_content(ngx_conf_t *cf, ngx_command_t *cmd,
    void *conf);
static char *ngx_http_js_shared_dict_zone(ngx_conf_t *cf, ngx_command_t *cmd,
    void *conf);
static char *ngx_http_js_shared_array_zone(ngx_conf_t *cf, ngx_command_t *cmd,
    void *conf);
static char *ngx_http_js_fetch_proxy(ngx_conf_t *cf, ngx_command_t *cmd,
    void *conf);
static char *ngx_http_js_body_filter_set(ngx_conf_t *cf, ngx_command_t *cmd,
    void *conf);
static ngx_int_t ngx_http_js_init_conf_vm(ngx_conf_t *cf,
    ngx_js_loc_conf_t *conf);
static void *ngx_http_js_create_main_conf(ngx_conf_t *cf);
static void *ngx_http_js_create_loc_conf(ngx_conf_t *cf);
static char *ngx_http_js_merge_loc_conf(ngx_conf_t *cf, void *parent,
    void *child);

static ngx_int_t ngx_http_js_parse_unsafe_uri(ngx_http_request_t *r,
    njs_str_t *uri, njs_str_t *args);

static ngx_conf_bitmask_t  ngx_http_js_engines[] = {
    { ngx_string("njs"), NGX_ENGINE_NJS },
#if (NJS_HAVE_QUICKJS)
    { ngx_string("qjs"), NGX_ENGINE_QJS },
#endif
    { ngx_null_string, 0 }
};

#if (NGX_SSL)

static ngx_conf_bitmask_t  ngx_http_js_ssl_protocols[] = {
    { ngx_string("TLSv1"), NGX_SSL_TLSv1 },
    { ngx_string("TLSv1.1"), NGX_SSL_TLSv1_1 },
    { ngx_string("TLSv1.2"), NGX_SSL_TLSv1_2 },
    { ngx_string("TLSv1.3"), NGX_SSL_TLSv1_3 },
    { ngx_null_string, 0 }
};

#endif

static ngx_command_t  ngx_http_js_commands[] = {

    { ngx_string("js_engine"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_TAKE1,
      ngx_js_engine,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_js_loc_conf_t, type),
      &ngx_http_js_engines },

    { ngx_string("js_context_reuse"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_size_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_js_loc_conf_t, reuse),
      NULL },

    { ngx_string("js_context_reuse_max_size"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_size_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_js_loc_conf_t, reuse_max_size),
      NULL },

    { ngx_string("js_import"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_TAKE13,
      ngx_js_import,
      NGX_HTTP_LOC_CONF_OFFSET,
      0,
      NULL },

    { ngx_string("js_periodic"),
      NGX_HTTP_LOC_CONF|NGX_CONF_ANY,
      ngx_http_js_periodic,
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
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_TAKE23,
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

#if (NGX_SSL)

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

    { ngx_string("js_shared_dict_zone"),
      NGX_HTTP_MAIN_CONF|NGX_CONF_1MORE,
      ngx_http_js_shared_dict_zone,
      0,
      0,
      NULL },

    { ngx_string("js_shared_array_zone"),
      NGX_HTTP_MAIN_CONF|NGX_CONF_TAKE1,
      ngx_http_js_shared_array_zone,
      0,
      0,
      NULL },

    { ngx_string("js_fetch_keepalive"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_num_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_js_loc_conf_t, fetch_keepalive),
      NULL },

    { ngx_string("js_fetch_keepalive_requests"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_num_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_js_loc_conf_t, fetch_keepalive_requests),
      NULL },

    { ngx_string("js_fetch_keepalive_time"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_msec_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_js_loc_conf_t, fetch_keepalive_time),
      NULL },

    { ngx_string("js_fetch_keepalive_timeout"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_msec_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_js_loc_conf_t, fetch_keepalive_timeout),
      NULL },

    { ngx_string("js_fetch_proxy"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_TAKE1,
      ngx_http_js_fetch_proxy,
      NGX_HTTP_LOC_CONF_OFFSET,
      0,
      NULL },

      ngx_null_command
};


static ngx_command_t  ngx_js_core_commands[] = {

    { ngx_string("js_load_http_native_module"),
      NGX_MAIN_CONF|NGX_DIRECT_CONF|NGX_CONF_TAKE13,
      ngx_js_core_load_native_module,
      0,
      0,
      NULL },

      ngx_null_command
};


static ngx_core_module_t  ngx_js_core_module_ctx = {
    ngx_string("ngx_http_js_core"),
    ngx_js_core_create_conf,
    NULL
};


ngx_module_t  ngx_http_js_core_module = {
    NGX_MODULE_V1,
    &ngx_js_core_module_ctx,           /* module context */
    ngx_js_core_commands,              /* module directives */
    NGX_CORE_MODULE,                   /* module type */
    NULL,                              /* init master */
    NULL,                              /* init module */
    NULL,                              /* init process */
    NULL,                              /* init thread */
    NULL,                              /* exit thread */
    NULL,                              /* exit process */
    NULL,                              /* exit master */
    NGX_MODULE_V1_PADDING
};


static ngx_http_module_t  ngx_http_js_module_ctx = {
    NULL,                          /* preconfiguration */
    ngx_http_js_init,              /* postconfiguration */

    ngx_http_js_create_main_conf,  /* create main configuration */
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
    ngx_http_js_init_worker,       /* init process */
    NULL,                          /* init thread */
    NULL,                          /* exit thread */
    NULL,                          /* exit process */
    NULL,                          /* exit master */
    NGX_MODULE_V1_PADDING
};


static ngx_http_output_header_filter_pt  ngx_http_next_header_filter;
static ngx_http_output_body_filter_pt    ngx_http_next_body_filter;


static njs_int_t    ngx_http_js_request_proto_id = 1;
static njs_int_t    ngx_http_js_periodic_session_proto_id = 2;


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
        .name.string = njs_str("requestBuffer"),
        .u.property = {
            .handler = ngx_http_js_ext_get_request_body,
            .magic32 = NGX_JS_BUFFER,
        }
    },

    {
        .flags = NJS_EXTERN_PROPERTY,
        .name.string = njs_str("requestLine"),
        .enumerable = 1,
        .u.property = {
            .handler = ngx_js_ext_string,
            .magic32 = offsetof(ngx_http_request_t, request_line),
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


static njs_external_t  ngx_http_js_ext_periodic_session[] = {

    {
        .flags = NJS_EXTERN_PROPERTY | NJS_EXTERN_SYMBOL,
        .name.symbol = NJS_SYMBOL_TO_STRING_TAG,
        .u.property = {
            .value = "PeriodicSession",
        }
    },

    {
        .flags = NJS_EXTERN_OBJECT,
        .name.string = njs_str("rawVariables"),
        .u.object = {
            .writable = 1,
            .prop_handler = ngx_http_js_periodic_session_variables,
            .magic32 = NGX_JS_BUFFER,
        }
    },

    {
        .flags = NJS_EXTERN_OBJECT,
        .name.string = njs_str("variables"),
        .u.object = {
            .writable = 1,
            .prop_handler = ngx_http_js_periodic_session_variables,
            .magic32 = NGX_JS_STRING,
        }
    },
};


static uintptr_t ngx_http_js_uptr[] = {
    offsetof(ngx_http_request_t, connection),
    (uintptr_t) ngx_http_js_pool,
    (uintptr_t) ngx_http_js_resolver,
    (uintptr_t) ngx_http_js_resolver_timeout,
    (uintptr_t) ngx_http_js_event_finalize,
    (uintptr_t) ngx_http_js_loc_conf,
    (uintptr_t) ngx_http_js_ctx,
    (uintptr_t) 0 /* main_conf ptr */,
};


static njs_vm_meta_t ngx_http_js_metas = {
    .size = njs_nitems(ngx_http_js_uptr),
    .values = ngx_http_js_uptr
};


njs_module_t  ngx_js_http_module = {
    .name = njs_str("http"),
    .preinit = NULL,
    .init = ngx_js_http_init,
};


njs_module_t *njs_http_js_addon_modules[] = {
    /*
     * Shared addons should be in the same order and the same positions
     * in all nginx modules.
     */
    &ngx_js_ngx_module,
    &ngx_js_fetch_module,
    &ngx_js_shared_dict_module,
    &ngx_js_shared_array_module,
#ifdef NJS_HAVE_OPENSSL
    &njs_webcrypto_module,
#endif
#ifdef NJS_HAVE_XML
    &njs_xml_module,
#endif
#ifdef NJS_HAVE_ZLIB
    &njs_zlib_module,
#endif
    &ngx_js_http_module,
    NULL,
};


static ngx_http_js_entry_t ngx_http_methods[] = {
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


#if (NJS_HAVE_QUICKJS)

static const JSCFunctionListEntry ngx_http_qjs_ext_request[] = {
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "Request", JS_PROP_CONFIGURABLE),
    JS_CGETSET_DEF("args", ngx_http_qjs_ext_args, NULL),
    JS_CFUNC_DEF("done", 0, ngx_http_qjs_ext_done),
    JS_CFUNC_MAGIC_DEF("error", 1, ngx_http_qjs_ext_log, NGX_LOG_ERR),
    JS_CFUNC_DEF("finish", 0, ngx_http_qjs_ext_finish),
    JS_CGETSET_DEF("headersIn", ngx_http_qjs_ext_headers_in, NULL),
    JS_CGETSET_DEF("headersOut", ngx_http_qjs_ext_headers_out, NULL),
    JS_CGETSET_DEF("httpVersion", ngx_http_qjs_ext_http_version, NULL),
    JS_CGETSET_DEF("internal", ngx_http_qjs_ext_internal, NULL),
    JS_CFUNC_DEF("internalRedirect", 1, ngx_http_qjs_ext_internal_redirect),
    JS_CFUNC_MAGIC_DEF("log", 1, ngx_http_qjs_ext_log, NGX_LOG_INFO),
    JS_CGETSET_MAGIC_DEF("method", ngx_http_qjs_ext_string, NULL,
                         offsetof(ngx_http_request_t, method_name)),
    JS_CGETSET_DEF("parent", ngx_http_qjs_ext_parent, NULL),
    JS_CGETSET_MAGIC_DEF("rawHeadersIn", ngx_http_qjs_ext_raw_headers, NULL, 0),
    JS_CGETSET_MAGIC_DEF("rawHeadersOut", ngx_http_qjs_ext_raw_headers, NULL,
                         1),
    JS_CGETSET_MAGIC_DEF("rawVariables", ngx_http_qjs_ext_variables,
                   NULL, NGX_JS_BUFFER),
    JS_CGETSET_DEF("remoteAddress", ngx_http_qjs_ext_remote_address, NULL),
    JS_CGETSET_MAGIC_DEF("requestBuffer", ngx_http_qjs_ext_request_body, NULL,
                         NGX_JS_BUFFER),
    JS_CGETSET_MAGIC_DEF("requestLine", ngx_http_qjs_ext_string, NULL,
                         offsetof(ngx_http_request_t, request_line)),
    JS_CGETSET_MAGIC_DEF("requestText", ngx_http_qjs_ext_request_body, NULL,
                         NGX_JS_STRING),
    JS_CGETSET_MAGIC_DEF("responseBuffer", ngx_http_qjs_ext_response_body, NULL,
                         NGX_JS_BUFFER),
    JS_CGETSET_MAGIC_DEF("responseText", ngx_http_qjs_ext_response_body, NULL,
                         NGX_JS_STRING),
    JS_CFUNC_DEF("return", 2, ngx_http_qjs_ext_return),
    JS_CFUNC_DEF("send", 1, ngx_http_qjs_ext_send),
    JS_CFUNC_DEF("sendBuffer", 2, ngx_http_qjs_ext_send_buffer),
    JS_CFUNC_DEF("sendHeader", 0, ngx_http_qjs_ext_send_header),
    JS_CFUNC_DEF("setReturnValue", 1, ngx_http_qjs_ext_set_return_value),
    JS_CGETSET_DEF("status", ngx_http_qjs_ext_status_get,
                   ngx_http_qjs_ext_status_set),
    JS_CFUNC_DEF("subrequest", 3, ngx_http_qjs_ext_subrequest),
    JS_CGETSET_MAGIC_DEF("uri", ngx_http_qjs_ext_string, NULL,
                         offsetof(ngx_http_request_t, uri)),
    JS_CGETSET_MAGIC_DEF("variables", ngx_http_qjs_ext_variables,
                         NULL, NGX_JS_STRING),
    JS_CFUNC_MAGIC_DEF("warn", 1, ngx_http_qjs_ext_log, NGX_LOG_WARN),
};


static const JSCFunctionListEntry ngx_http_qjs_ext_periodic[] = {
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "PeriodicSession",
                       JS_PROP_CONFIGURABLE),
    JS_CGETSET_MAGIC_DEF("rawVariables", ngx_http_qjs_ext_periodic_variables,
                   NULL, NGX_JS_BUFFER),
    JS_CGETSET_MAGIC_DEF("variables", ngx_http_qjs_ext_periodic_variables,
                         NULL, NGX_JS_STRING),
};


static JSClassDef ngx_http_qjs_request_class = {
    "Request",
    .finalizer = ngx_http_qjs_request_finalizer,
};


static JSClassDef ngx_http_qjs_periodic_class = {
    "PeriodicSession",
    .finalizer = ngx_http_qjs_periodic_finalizer,
};


static JSClassDef ngx_http_qjs_variables_class = {
    "Variables",
    .finalizer = NULL,
    .exotic = & (JSClassExoticMethods) {
        .get_own_property = ngx_http_qjs_variables_own_property,
        .set_property = ngx_http_qjs_variables_set_property,
    },
};


static JSClassDef ngx_http_qjs_headers_in_class = {
    "headersIn",
    .finalizer = NULL,
    .exotic = & (JSClassExoticMethods) {
        .get_own_property = ngx_http_qjs_headers_in_own_property,
        .get_own_property_names = ngx_http_qjs_headers_in_own_property_names,
    },
};


static JSClassDef ngx_http_qjs_headers_out_class = {
    "headersOut",
    .finalizer = NULL,
    .exotic = & (JSClassExoticMethods) {
        .get_own_property = ngx_http_qjs_headers_out_own_property,
        .get_own_property_names = ngx_http_qjs_headers_out_own_property_names,
        .set_property = ngx_http_qjs_headers_out_set_property,
        .define_own_property = ngx_http_qjs_headers_out_define_own_property,
        .delete_property = ngx_http_qjs_headers_out_delete_property,
    },
};


qjs_module_t *njs_http_qjs_addon_modules[] = {
    &ngx_qjs_ngx_module,
    &ngx_qjs_ngx_shared_dict_module,
    &ngx_qjs_ngx_shared_array_module,
#ifdef NJS_HAVE_QUICKJS
    &ngx_qjs_ngx_fetch_module,
#endif
    /*
     * Shared addons should be in the same order and the same positions
     * in all nginx modules.
     */
#ifdef NJS_HAVE_OPENSSL
    &qjs_webcrypto_module,
#endif
#ifdef NJS_HAVE_XML
    &qjs_xml_module,
#endif
#ifdef NJS_HAVE_ZLIB
    &qjs_zlib_module,
#endif
    NULL,
};


#endif


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

    rc = ngx_http_js_init_vm(r, ngx_http_js_request_proto_id);

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

    rc = ctx->engine->call((ngx_js_ctx_t *) ctx, &jlcf->content, &ctx->args[0],
                           1);

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

    c = r->connection;
    ctx = ngx_http_get_module_ctx(r, ngx_http_js_module);

    if (!ngx_js_ctx_pending(ctx)) {
        ngx_http_js_content_finalize(r, ctx);

        if (!c->buffered) {
            return;
        }
    }

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

    if (jlcf->body_filter.len != 0) {
        r->filter_need_in_memory = 1;
    }

    if (jlcf->header_filter.len == 0) {
        return ngx_http_next_header_filter(r);
    }

    ngx_log_debug0(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
                   "http js header filter");

    rc = ngx_http_js_init_vm(r, ngx_http_js_request_proto_id);

    if (rc == NGX_ERROR || rc == NGX_DECLINED) {
        return NGX_ERROR;
    }

    ctx = ngx_http_get_module_ctx(r, ngx_http_js_module);

    ctx->filter = 1;
    pending = ngx_js_ctx_pending(ctx);

    ngx_log_debug1(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
                   "http js header call \"%V\"", &jlcf->header_filter);

    rc = ctx->engine->call((ngx_js_ctx_t *) ctx, &jlcf->header_filter,
                           &ctx->args[0], 1);

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
ngx_http_njs_body_filter(ngx_http_request_t *r, ngx_http_js_loc_conf_t *jlcf,
    ngx_http_js_ctx_t *ctx, ngx_chain_t *in)
{
    size_t               len;
    u_char              *p;
    njs_vm_t            *vm;
    ngx_int_t            rc;
    njs_int_t            ret, pending;
    ngx_buf_t           *b;
    ngx_chain_t         *cl;
    ngx_connection_t    *c;
    njs_opaque_value_t   last_key, last;
    njs_opaque_value_t   arguments[3];

    static const njs_str_t last_str = njs_str("last");

    c = r->connection;
    vm = ctx->engine->u.njs.vm;

    njs_value_assign(&arguments[0], &ctx->args[0]);

    njs_vm_value_string_create(vm, njs_value_arg(&last_key),
                               last_str.start, last_str.length);

    while (in != NULL) {
        ctx->buf = in->buf;
        b = ctx->buf;

        if (!ctx->done) {
            len = b->last - b->pos;

            p = ngx_pnalloc(r->pool, len);
            if (p == NULL) {
                njs_vm_memory_error(vm);
                return NJS_ERROR;
            }

            if (len) {
                ngx_memcpy(p, b->pos, len);
            }

            ret = ngx_js_prop(vm, jlcf->buffer_type,
                              njs_value_arg(&arguments[1]), p, len);
            if (ret != NJS_OK) {
                return ret;
            }

            njs_value_boolean_set(njs_value_arg(&last), b->last_buf);

            ret = njs_vm_object_alloc(vm, njs_value_arg(&arguments[2]),
                                       njs_value_arg(&last_key),
                                       njs_value_arg(&last), NULL);
            if (ret != NJS_OK) {
                return ret;
            }

            pending = ngx_js_ctx_pending(ctx);

            ngx_log_debug1(NGX_LOG_DEBUG_HTTP, c->log, 0,
                           "http js body call \"%V\"", &jlcf->body_filter);

            rc = ctx->engine->call((ngx_js_ctx_t *) ctx, &jlcf->body_filter,
                                   &arguments[0], 3);

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

    return NGX_OK;
}


static ngx_int_t
ngx_http_js_body_filter(ngx_http_request_t *r, ngx_chain_t *in)
{
    ngx_int_t                rc;
    ngx_chain_t             *out;
    ngx_http_js_ctx_t       *ctx;
    ngx_http_js_loc_conf_t  *jlcf;

    jlcf = ngx_http_get_module_loc_conf(r, ngx_http_js_module);

    if (jlcf->body_filter.len == 0 || in == NULL) {
        return ngx_http_next_body_filter(r, in);
    }

    ngx_log_debug0(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
                   "http js body filter");

    rc = ngx_http_js_init_vm(r, ngx_http_js_request_proto_id);

    if (rc == NGX_ERROR || rc == NGX_DECLINED) {
        return NGX_ERROR;
    }

    ctx = ngx_http_get_module_ctx(r, ngx_http_js_module);

    if (ctx->done) {
        return ngx_http_next_body_filter(r, in);
    }

    ctx->filter = 1;
    ctx->last_out = &out;

    rc = ctx->body_filter(r, jlcf, ctx, in);
    if (rc != NGX_OK) {
        return NGX_ERROR;
    }

    *ctx->last_out = NULL;

    if (out != NULL || r->connection->buffered) {
        rc = ngx_http_next_body_filter(r, out);

        ngx_chain_update_chains(r->connection->pool, &ctx->free, &ctx->busy,
                                &out, (ngx_buf_tag_t) &ngx_http_js_module);

    } else {
        rc = NGX_OK;
    }

    return rc;
}


static ngx_int_t
ngx_http_js_variable_set(ngx_http_request_t *r, ngx_http_variable_value_t *v,
    uintptr_t data)
{
    ngx_js_set_t *vdata = (ngx_js_set_t *) data;

    ngx_int_t           rc;
    njs_int_t           pending;
    ngx_str_t          *fname, value;
    ngx_http_js_ctx_t  *ctx;

    fname = &vdata->fname;

    rc = ngx_http_js_init_vm(r, ngx_http_js_request_proto_id);

    if (rc == NGX_ERROR) {
        return NGX_ERROR;
    }

    if (rc == NGX_DECLINED) {
        ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                      "no \"js_import\" directives found for \"js_set\" handler"
                      " \"%V\" in the current scope", fname);
        v->not_found = 1;
        return NGX_OK;
    }

    ngx_log_debug1(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
                   "http js variable call \"%V\"", fname);

    ctx = ngx_http_get_module_ctx(r, ngx_http_js_module);

    pending = ngx_js_ctx_pending(ctx);

    rc = ctx->engine->call((ngx_js_ctx_t *) ctx, fname, &ctx->args[0], 1);

    if (rc == NGX_ERROR) {
        v->not_found = 1;
        return NGX_OK;
    }

    if (!pending && rc == NGX_AGAIN) {
        ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                      "async operation inside \"%V\" variable handler", fname);
        return NGX_ERROR;
    }

    if (ctx->engine->string(ctx->engine, &ctx->retval, &value) != NGX_OK) {
        return NGX_ERROR;
    }

    v->len = value.len;
    v->valid = 1;
    v->no_cacheable = vdata->flags & NGX_NJS_VAR_NOCACHE;
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
ngx_http_js_init_vm(ngx_http_request_t *r, njs_int_t proto_id)
{
    ngx_http_js_ctx_t       *ctx;
    ngx_pool_cleanup_t      *cln;
    ngx_http_js_loc_conf_t  *jlcf;

    jlcf = ngx_http_get_module_loc_conf(r, ngx_http_js_module);
    if (jlcf->engine == NULL) {
        return NGX_DECLINED;
    }

    ctx = ngx_http_get_module_ctx(r, ngx_http_js_module);

    if (ctx == NULL) {
        ctx = ngx_pcalloc(r->pool, sizeof(ngx_http_js_ctx_t));
        if (ctx == NULL) {
            return NGX_ERROR;
        }

        ngx_js_ctx_init((ngx_js_ctx_t *) ctx, r->connection->log);

        ngx_http_set_ctx(r, ctx, ngx_http_js_module);
    }

    if (ctx->engine) {
        return NGX_OK;
    }

    ctx->engine = jlcf->engine->clone((ngx_js_ctx_t *) ctx,
                                      (ngx_js_loc_conf_t *) jlcf, proto_id, r);
    if (ctx->engine == NULL) {
        return NGX_ERROR;
    }

    ngx_log_debug3(NGX_LOG_DEBUG_HTTP, ctx->log, 0,
                   "http js vm clone %s: %p from: %p", jlcf->engine->name,
                   ctx->engine, jlcf->engine);

    cln = ngx_pool_cleanup_add(r->pool, 0);
    if (cln == NULL) {
        return NGX_ERROR;
    }

    cln->handler = ngx_http_js_cleanup_ctx;
    cln->data = ctx;

    return NGX_OK;
}


static void
ngx_http_js_cleanup_ctx(void *data)
{
    ngx_http_request_t      *r;
    ngx_http_js_loc_conf_t  *jlcf;

    ngx_http_js_ctx_t        *ctx = data;

    if (ngx_js_ctx_pending(ctx)) {
        ngx_log_error(NGX_LOG_ERR, ctx->log, 0, "pending events");
    }

    ngx_log_debug1(NGX_LOG_DEBUG_HTTP, ctx->log, 0, "http js vm destroy: %p",
                   ctx->engine);

    r = ngx_js_ctx_external(ctx);

    /*
     * Restoring the original module context, because it can be reset
     * by internalRedirect() method. Proper ctx is required for
     * ngx_http_qjs_request_finalizer() to work correctly.
     */
    ngx_http_set_ctx(r, ctx, ngx_http_js_module);

    jlcf = ngx_http_get_module_loc_conf(r, ngx_http_js_module);

    /*
     * r->pool set to NULL by ngx_http_free_request().
     * Creating a temporary pool for potential use in njs.on('exit', ...)
     * handler.
     */
    r->pool = ngx_create_pool(128, ctx->log);

    ngx_js_ctx_destroy((ngx_js_ctx_t *) ctx, (ngx_js_loc_conf_t *) jlcf);

    ngx_destroy_pool(r->pool);
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
            njs_value_string_get(vm, njs_argument(start, i), &hdr);

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

            rc = njs_vm_value_string_create(vm, value, h->key.data, h->key.len);
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
    uint32_t unused, njs_value_t *value, njs_value_t *setval,
    njs_value_t *retval)
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

        rc = njs_vm_value_string_create(vm, elem, h->key.data, h->key.len);
        if (rc != NJS_OK) {
            return NJS_ERROR;
        }

        elem = njs_vm_array_push(vm, array);
        if (elem == NULL) {
            return NJS_ERROR;
        }

        rc = njs_vm_value_string_create(vm, elem, h->value.data, h->value.len);
        if (rc != NJS_OK) {
            return NJS_ERROR;
        }
    }

    return NJS_OK;
}


static njs_int_t
ngx_http_js_ext_header_out(njs_vm_t *vm, njs_object_prop_t *prop,
    uint32_t atom_id, njs_value_t *value, njs_value_t *setval,
    njs_value_t *retval)
{
    njs_int_t              rc;
    njs_str_t              name;
    ngx_http_request_t    *r;
    ngx_http_js_header_t  *h;

    static ngx_http_js_header_t headers_out[] = {
#if defined(nginx_version) && (nginx_version < 1023000)

#define header(name, h) { njs_str(name), 0, (uintptr_t) h }
        header("Age", ngx_http_js_header_single),
        header("Content-Type", ngx_http_js_content_type122),
        header("Content-Length", ngx_http_js_content_length122),
        header("Content-Encoding", ngx_http_js_content_encoding122),
        header("Date", ngx_http_js_date122),
        header("Etag", ngx_http_js_header_single),
        header("Expires", ngx_http_js_header_single),
        header("Last-Modified", ngx_http_js_last_modified122),
        header("Location", ngx_http_js_location122),
        header("Server", ngx_http_js_server122),
        header("Set-Cookie", ngx_http_js_header_array),
        header("Retry-After", ngx_http_js_header_single),
        header("", ngx_http_js_header_generic),
#undef header

#else

#define header(name, fl, h) { njs_str(name), fl, (uintptr_t) h }
        header("Age", NJS_HEADER_SINGLE, ngx_http_js_header_out),
        header("Content-Encoding", 0, ngx_http_js_content_encoding),
        header("Content-Length", 0, ngx_http_js_content_length),
        header("Content-Type", 0, ngx_http_js_content_type),
        header("Date", 0, ngx_http_js_date),
        header("Etag", NJS_HEADER_SINGLE, ngx_http_js_header_out),
        header("Expires", NJS_HEADER_SINGLE, ngx_http_js_header_out),
        header("Last-Modified", 0, ngx_http_js_last_modified),
        header("Location", 0, ngx_http_js_location),
        header("Server", 0, ngx_http_js_server),
        header("Set-Cookie", NJS_HEADER_ARRAY, ngx_http_js_header_out),
        header("Retry-After", NJS_HEADER_SINGLE, ngx_http_js_header_out),
        header("", 0, ngx_http_js_header_out),
#undef header

#endif
    };

    r = njs_vm_external(vm, ngx_http_js_request_proto_id, value);
    if (r == NULL) {
        if (retval != NULL) {
            njs_value_undefined_set(retval);
        }

        return NJS_DECLINED;
    }

    rc = njs_vm_prop_name(vm, atom_id, &name);
    if (rc != NJS_OK) {
        if (retval != NULL) {
            njs_value_undefined_set(retval);
        }

        return NJS_DECLINED;
    }

    if (r->header_sent && setval != NULL) {
        ngx_log_error(NGX_LOG_WARN, r->connection->log, 0,
                      "ignored setting of response header \"%V\" because"
                      " headers were already sent", &name);
    }

    for (h = headers_out; h->name.len > 0; h++) {
        if (h->name.len == name.length
            && ngx_strncasecmp(h->name.data, name.start, name.length) == 0)
        {
            break;
        }
    }

#if defined(nginx_version) && (nginx_version < 1023000)
    return ((njs_http_js_header_handler122_t) h->handler)(vm, r,
                                &r->headers_out.headers, &name, setval, retval);
#else
    return ((njs_http_js_header_handler_t) h->handler)(vm, r, h->flags, &name,
                                                       setval, retval);
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

        rc = njs_vm_value_string_create(vm, retval, h->value.data,
                                        h->value.len);
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

            rc = njs_vm_value_string_create(vm, value, h->value.data,
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

        return njs_vm_value_string_create(vm, retval, start, p - start);
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
ngx_http_js_content_encoding122(njs_vm_t *vm, ngx_http_request_t *r,
    ngx_list_t *headers, njs_str_t *v, njs_value_t *setval, njs_value_t *retval)
{
    return ngx_http_js_content_encoding(vm, r, 0, v, setval, retval);
}


static njs_int_t
ngx_http_js_content_length122(njs_vm_t *vm, ngx_http_request_t *r,
    ngx_list_t *headers, njs_str_t *v, njs_value_t *setval, njs_value_t *retval)
{
    return ngx_http_js_content_length(vm, r, 0, v, setval, retval);
}


static njs_int_t
ngx_http_js_content_type122(njs_vm_t *vm, ngx_http_request_t *r,
    ngx_list_t *headers, njs_str_t *v, njs_value_t *setval, njs_value_t *retval)
{
    return ngx_http_js_content_type(vm, r, 0, v, setval, retval);
}


static njs_int_t
ngx_http_js_date122(njs_vm_t *vm, ngx_http_request_t *r,
    ngx_list_t *headers, njs_str_t *v, njs_value_t *setval, njs_value_t *retval)
{
    return ngx_http_js_date(vm, r, 0, v, setval, retval);
}


static njs_int_t
ngx_http_js_last_modified122(njs_vm_t *vm, ngx_http_request_t *r,
    ngx_list_t *headers, njs_str_t *v, njs_value_t *setval, njs_value_t *retval)
{
    return ngx_http_js_last_modified(vm, r, 0, v, setval, retval);
}


static njs_int_t
ngx_http_js_location122(njs_vm_t *vm, ngx_http_request_t *r,
    ngx_list_t *headers, njs_str_t *v, njs_value_t *setval, njs_value_t *retval)
{
    return ngx_http_js_location(vm, r, 0, v, setval, retval);
}


static njs_int_t
ngx_http_js_server122(njs_vm_t *vm, ngx_http_request_t *r,
    ngx_list_t *headers, njs_str_t *v, njs_value_t *setval, njs_value_t *retval)
{
    return ngx_http_js_server(vm, r, 0, v, setval, retval);
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

        rc = njs_vm_value_string_create(vm, value, (u_char *) "Content-Type",
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

        rc = njs_vm_value_string_create(vm, value, (u_char *) "Content-Length",
                                        njs_length("Content-Length"));
        if (rc != NJS_OK) {
            return NJS_ERROR;
        }
    }

    return ngx_http_js_ext_keys_header(vm, value, keys,
                                       &r->headers_out.headers);
}


static njs_int_t
ngx_http_js_ext_status(njs_vm_t *vm, njs_object_prop_t *prop, uint32_t unused,
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
    r->headers_out.status_line.len = 0;

    njs_value_undefined_set(retval);

    return NJS_OK;
}


static njs_int_t
ngx_http_js_ext_send_header(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t unused, njs_value_t *retval)
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

    r->disable_not_modified = 1;

    if (ngx_http_send_header(r) == NGX_ERROR) {
        return NJS_ERROR;
    }

    njs_value_undefined_set(retval);

    return NJS_OK;
}


static njs_int_t
ngx_http_js_ext_send(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t unused, njs_value_t *retval)
{
    njs_str_t            s;
    ngx_buf_t           *b;
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
        if (ngx_js_string(vm, njs_argument(args, n), &s) != NGX_OK) {
            return NJS_ERROR;
        }

        if (s.length == 0) {
            continue;
        }

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

    *ll = NULL;

    if (ngx_http_output_filter(r, out) == NGX_ERROR) {
        return NJS_ERROR;
    }

    njs_value_undefined_set(retval);

    return NJS_OK;
}


static njs_int_t
ngx_http_js_ext_send_buffer(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t unused, njs_value_t *retval)
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

    njs_value_undefined_set(retval);

    return NJS_OK;
}


static njs_int_t
ngx_http_js_ext_set_return_value(njs_vm_t *vm, njs_value_t *args,
    njs_uint_t nargs, njs_index_t unused, njs_value_t *retval)
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
    njs_value_undefined_set(retval);

    return NJS_OK;
}


static njs_int_t
ngx_http_js_ext_done(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t unused, njs_value_t *retval)
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

    njs_value_undefined_set(retval);

    return NJS_OK;
}


static njs_int_t
ngx_http_js_ext_finish(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t unused, njs_value_t *retval)
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

    njs_value_undefined_set(retval);

    return NJS_OK;
}


static njs_int_t
ngx_http_js_ext_return(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t unused, njs_value_t *retval)
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

    ctx = ngx_http_get_module_ctx(r, ngx_http_js_module);

    if (status < NGX_HTTP_BAD_REQUEST
        || !njs_value_is_null_or_undefined(njs_arg(args, nargs, 2)))
    {
        if (ngx_js_string(vm, njs_arg(args, nargs, 2), &text) != NGX_OK) {
            njs_vm_error(vm, "failed to convert text");
            return NJS_ERROR;
        }

        ngx_memzero(&cv, sizeof(ngx_http_complex_value_t));

        cv.value.data = text.start;
        cv.value.len = text.length;

        r->disable_not_modified = 1;

        ctx->status = ngx_http_send_response(r, status, NULL, &cv);

        if (ctx->status == NGX_ERROR) {
            njs_vm_error(vm, "failed to send response");
            return NJS_ERROR;
        }

    } else {
        ctx->status = status;
    }

    njs_value_undefined_set(retval);

    return NJS_OK;
}


static njs_int_t
ngx_http_js_ext_internal_redirect(njs_vm_t *vm, njs_value_t *args,
    njs_uint_t nargs, njs_index_t unused, njs_value_t *retval)
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

    if (ctx->filter) {
        njs_vm_error(vm, "internalRedirect cannot be called while filtering");
        return NJS_ERROR;
    }

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

    njs_value_undefined_set(retval);

    return NJS_OK;
}


static njs_int_t
ngx_http_js_ext_get_http_version(njs_vm_t *vm, njs_object_prop_t *prop,
    uint32_t unused, njs_value_t *value, njs_value_t *setval,
    njs_value_t *retval)
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

    return njs_vm_value_string_create(vm, retval, v.data, v.len);
}


static njs_int_t
ngx_http_js_ext_internal(njs_vm_t *vm, njs_object_prop_t *prop, uint32_t unused,
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
    uint32_t unused, njs_value_t *value, njs_value_t *setval,
    njs_value_t *retval)
{
    ngx_connection_t    *c;
    ngx_http_request_t  *r;

    r = njs_vm_external(vm, ngx_http_js_request_proto_id, value);
    if (r == NULL) {
        njs_value_undefined_set(retval);
        return NJS_DECLINED;
    }

    c = r->connection;

    return njs_vm_value_string_create(vm, retval, c->addr_text.data,
                                      c->addr_text.len);
}


static njs_int_t
ngx_http_js_ext_get_args(njs_vm_t *vm, njs_object_prop_t *prop, uint32_t unused,
    njs_value_t *value, njs_value_t *setval, njs_value_t *retval)
{
    u_char              *data;
    njs_int_t            ret;
    njs_value_t         *args;
    ngx_http_js_ctx_t   *ctx;
    ngx_http_request_t  *r;

    r = njs_vm_external(vm, ngx_http_js_request_proto_id, value);
    if (r == NULL) {
        njs_value_undefined_set(retval);
        return NJS_DECLINED;
    }

    ctx = ngx_http_get_module_ctx(r, ngx_http_js_module);

    args = njs_value_arg(&ctx->rargs);

    if (njs_value_is_null(args)) {
        data = (r->args.len != 0) ? r->args.data : (u_char *) "";
        ret = njs_vm_query_string_parse(vm, data, data + r->args.len, args);

        if (ret == NJS_ERROR) {
            return NJS_ERROR;
        }
    }

    njs_value_assign(retval, args);

    return NJS_OK;
}


static njs_int_t
ngx_http_js_ext_get_request_body(njs_vm_t *vm, njs_object_prop_t *prop,
    uint32_t unused, njs_value_t *value, njs_value_t *setval,
    njs_value_t *retval)
{
    u_char              *p, *body;
    size_t               len;
    ssize_t              n;
    uint32_t             buffer_type;
    ngx_buf_t           *buf;
    njs_int_t            ret;
    njs_value_t         *request_body;
    ngx_chain_t         *cl;
    ngx_http_js_ctx_t   *ctx;
    ngx_http_request_t  *r;

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

    cl = r->request_body->bufs;
    buf = cl->buf;

    if (r->request_body->temp_file) {
        ngx_log_error(NGX_LOG_WARN, r->connection->log, 0,
                      "http js reading request body from a temporary file");

        if (buf == NULL || !buf->in_file) {
            njs_vm_internal_error(vm, "cannot find request body");
            return NJS_ERROR;
        }

        len = buf->file_last - buf->file_pos;

        body = ngx_pnalloc(r->pool, len);
        if (body == NULL) {
            njs_vm_memory_error(vm);
            return NJS_ERROR;
        }

        n = ngx_read_file(buf->file, body, len, buf->file_pos);
        if (n != (ssize_t) len) {
            njs_vm_internal_error(vm, "failed to read request body");
            return NJS_ERROR;
        }

        goto done;
    }

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
ngx_http_js_ext_header_in(njs_vm_t *vm, njs_object_prop_t *prop, uint32_t atom_id,
    njs_value_t *value, njs_value_t *setval, njs_value_t *retval)
{
    njs_int_t              rc;
    njs_str_t              name;
    ngx_http_request_t    *r;
    ngx_http_js_header_t  *h;

    static ngx_http_js_header_t headers_in[] = {
#define header(name, h) { njs_str(name), 0, (uintptr_t) h }
        header("Content-Type", ngx_http_js_header_single),
        header("Cookie", ngx_http_js_header_cookie),
        header("ETag", ngx_http_js_header_single),
        header("From", ngx_http_js_header_single),
        header("Max-Forwards", ngx_http_js_header_single),
        header("Referer", ngx_http_js_header_single),
        header("Proxy-Authorization", ngx_http_js_header_single),
        header("User-Agent", ngx_http_js_header_single),
#if (NGX_HTTP_X_FORWARDED_FOR)
        header("X-Forwarded-For", ngx_http_js_header_x_forwarded_for),
#endif
        header("", ngx_http_js_header_generic),
#undef header
    };

    r = njs_vm_external(vm, ngx_http_js_request_proto_id, value);
    if (r == NULL) {
        if (retval != NULL) {
            njs_value_undefined_set(retval);
        }

        return NJS_DECLINED;
    }

    rc = njs_vm_prop_name(vm, atom_id, &name);
    if (rc != NJS_OK) {
        if (retval != NULL) {
            njs_value_undefined_set(retval);
        }

        return NJS_DECLINED;
    }

    for (h = headers_in; h->name.len > 0; h++) {
        if (h->name.len == name.length
            && ngx_strncasecmp(h->name.data, name.start, name.length) == 0)
        {
            break;
        }
    }

    return ((njs_http_js_header_handler122_t) h->handler)(vm, r,
                                &r->headers_in.headers, &name, setval, retval);
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
    njs_chb_t          chain;
    njs_int_t          ret;
    ngx_uint_t         i, n;
    ngx_table_elt_t  **hh;

    n = array->nelts;
    hh = array->elts;

    if (n == 0) {
        njs_value_undefined_set(retval);
        return NJS_DECLINED;
    }

    if (n == 1) {
        return njs_vm_value_string_create(vm, retval, (*hh)->value.data,
                                              (*hh)->value.len);
    }

    NJS_CHB_MP_INIT(&chain, njs_vm_memory_pool(vm));

    for (i = 0; i < n; i++) {
        njs_chb_append(&chain, hh[i]->value.data, hh[i]->value.len);
        njs_chb_append(&chain, &sep, 1);
    }

    ret = njs_vm_value_string_create_chb(vm, retval, &chain);

    njs_chb_destroy(&chain);

    return ret;
}
#else
static njs_int_t
ngx_http_js_ext_header_in(njs_vm_t *vm, njs_object_prop_t *prop, uint32_t atom_id,
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

    rc = njs_vm_prop_name(vm, atom_id, &name);
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
ngx_http_js_request_variables(njs_vm_t *vm, njs_object_prop_t *prop,
    uint32_t atom_id, ngx_http_request_t *r, njs_value_t *setval,
    njs_value_t *retval)
{
    njs_int_t                   rc, is_capture, start, length;
    njs_str_t                   val, s;
    ngx_str_t                   name;
    ngx_uint_t                  i, key;
    ngx_http_variable_t        *v;
    ngx_http_core_main_conf_t  *cmcf;
    ngx_http_variable_value_t  *vv;
    u_char                      storage[64];

    rc = njs_vm_prop_name(vm, atom_id, &val);
    if (rc != NJS_OK) {
        njs_value_undefined_set(retval);
        return NJS_DECLINED;
    }

    if (setval == NULL) {
        is_capture = 1;
        for (i = 0; i < val.length; i++) {
            if (val.start[i] < '0' || val.start[i] > '9') {
                is_capture = 0;
                break;
            }
        }

        if (is_capture) {
            key = ngx_atoi(val.start, val.length) * 2;
            if (r->captures == NULL || r->captures_data == NULL
                || r->ncaptures <= key)
            {
                njs_value_undefined_set(retval);
                return NJS_DECLINED;
            }

            start = r->captures[key];
            length = r->captures[key + 1] - start;

            return ngx_js_prop(vm, njs_vm_prop_magic32(prop), retval,
                               &r->captures_data[start], length);
        }

        /* Lookup the variable in nginx variables */

        if (val.length < sizeof(storage)) {
            name.data = storage;

        } else {
            name.data = ngx_pnalloc(r->pool, val.length);
            if (name.data == NULL) {
                njs_vm_memory_error(vm);
                return NJS_ERROR;
            }
        }

        name.len = val.length;

        key = ngx_hash_strlow(name.data, val.start, val.length);

        vv = ngx_http_get_variable(r, &name, key);
        if (vv == NULL || vv->not_found) {
            njs_value_undefined_set(retval);
            return NJS_DECLINED;
        }

        return ngx_js_prop(vm, njs_vm_prop_magic32(prop), retval, vv->data,
                           vv->len);
    }

    cmcf = ngx_http_get_module_main_conf(r, ngx_http_core_module);

    if (val.length < sizeof(storage)) {
        name.data = storage;

    } else {
        name.data = ngx_pnalloc(r->pool, val.length);
        if (name.data == NULL) {
            njs_vm_memory_error(vm);
            return NJS_ERROR;
        }
    }

    key = ngx_hash_strlow(name.data, val.start, val.length);

    v = ngx_hash_find(&cmcf->variables_hash, key, name.data, val.length);

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
ngx_http_js_ext_variables(njs_vm_t *vm, njs_object_prop_t *prop,
    uint32_t atom_id, njs_value_t *value, njs_value_t *setval,
    njs_value_t *retval)
{
    ngx_http_request_t  *r;

    r = njs_vm_external(vm, ngx_http_js_request_proto_id, value);
    if (r == NULL) {
        njs_value_undefined_set(retval);
        return NJS_DECLINED;
    }

    return ngx_http_js_request_variables(vm, prop, atom_id, r, setval, retval);
}


static njs_int_t
ngx_http_js_periodic_session_variables(njs_vm_t *vm, njs_object_prop_t *prop,
    uint32_t atom_id, njs_value_t *value, njs_value_t *setval,
    njs_value_t *retval)
{
    ngx_http_request_t  *r;

    r = njs_vm_external(vm, ngx_http_js_periodic_session_proto_id, value);
    if (r == NULL) {
        njs_value_undefined_set(retval);
        return NJS_DECLINED;
    }

    return ngx_http_js_request_variables(vm, prop, atom_id, r, setval, retval);
}


static njs_int_t
ngx_http_js_ext_subrequest(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t unused, njs_value_t *retval)
{
    ngx_int_t                    rc, flags;
    njs_str_t                    uri_arg, args_arg, method_name, body_arg;
    ngx_str_t                    uri, rargs;
    ngx_uint_t                   method, methods_max, has_body, detached,
                                 promise;
    njs_value_t                 *value, *arg, *options, *callback;
    ngx_js_event_t              *event;
    ngx_http_js_ctx_t           *ctx;
    njs_opaque_value_t           lvalue;
    ngx_http_request_t          *r, *sr;
    ngx_http_request_body_t     *rb;
    ngx_http_post_subrequest_t  *ps;

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

    if (r->subrequest_in_memory) {
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
    methods_max = sizeof(ngx_http_methods) / sizeof(ngx_http_methods[0]);

    args_arg.length = 0;
    args_arg.start = NULL;
    has_body = 0;
    detached = 0;

    arg = njs_arg(args, nargs, 2);

    if (njs_value_is_string(arg)) {
        if (ngx_js_string(vm, arg, &args_arg) != NJS_OK) {
            njs_vm_error(vm, "failed to convert args");
            return NJS_ERROR;
        }

    } else if (njs_value_is_function(arg)) {
        callback = arg;

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
                if (method_name.length == ngx_http_methods[method].name.len
                    && ngx_memcmp(method_name.start,
                                  ngx_http_methods[method].name.data,
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

    if (ngx_http_js_parse_unsafe_uri(r, &uri_arg, &args_arg) != NGX_OK) {
        njs_vm_error(vm, "unsafe uri");
        return NJS_ERROR;
    }

    arg = njs_arg(args, nargs, 3);

    if (callback == NULL && !njs_value_is_undefined(arg)) {
        if (!njs_value_is_function(arg)) {
            njs_vm_error(vm, "callback is not a function");
            return NJS_ERROR;

        } else {
            callback = arg;
        }
    }

    if (detached && callback != NULL) {
        njs_vm_error(vm, "detached flag and callback are mutually exclusive");
        return NJS_ERROR;
    }

    flags = NGX_HTTP_SUBREQUEST_BACKGROUND;

    njs_value_undefined_set(retval);

    if (!detached) {
        ps = ngx_palloc(r->pool, sizeof(ngx_http_post_subrequest_t));
        if (ps == NULL) {
            njs_vm_memory_error(vm);
            return NJS_ERROR;
        }

        promise = !!(callback == NULL);

        event = njs_mp_zalloc(njs_vm_memory_pool(vm),
                              sizeof(ngx_js_event_t)
                              + promise * (sizeof(njs_opaque_value_t) * 2));
        if (njs_slow_path(event == NULL)) {
            njs_vm_memory_error(vm);
            return NJS_ERROR;
        }

        event->fd = ctx->event_id++;

        if (promise) {
            event->args = (njs_opaque_value_t *) &event[1];
            rc = njs_vm_promise_create(vm, retval, njs_value_arg(event->args));
            if (rc != NJS_OK) {
                return NJS_ERROR;
            }

            callback = njs_value_arg(event->args);
        }

        njs_value_assign(&event->function, callback);

        ps->handler = ngx_http_js_subrequest_done;
        ps->data = event;

        flags |= NGX_HTTP_SUBREQUEST_IN_MEMORY;

    } else {
        ps = NULL;
        event = NULL;
    }

    uri.len = uri_arg.length;
    uri.data = uri_arg.start;

    rargs.len = args_arg.length;
    rargs.data = args_arg.start;

    if (ngx_http_subrequest(r, &uri, rargs.len ? &rargs : NULL, &sr, ps, flags)
        != NGX_OK)
    {
        njs_vm_error(vm, "subrequest creation failed");
        return NJS_ERROR;
    }

    if (event != NULL) {
        ngx_js_add_event(ctx, event);
    }

    if (method != methods_max) {
        sr->method = ngx_http_methods[method].value;
        sr->method_name = ngx_http_methods[method].name;

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

    return NJS_OK;

memory_error:

    njs_vm_error(vm, "internal error");

    return NJS_ERROR;
}


static ngx_int_t
ngx_http_js_subrequest_done(ngx_http_request_t *r, void *data, ngx_int_t rc)
{
    ngx_js_event_t  *event = data;

    njs_int_t            ret;
    njs_vm_t            *vm;
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

    vm = ctx->engine->u.njs.vm;

    ret = njs_vm_external_create(vm, njs_value_arg(&reply),
                                 ngx_http_js_request_proto_id, r, 0);
    if (ret != NJS_OK) {
        ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                      "js subrequest reply creation failed");

        return NGX_ERROR;
    }

    rc = ngx_js_call(vm, njs_value_function(njs_value_arg(&event->function)),
                     &reply, 1);

    ngx_js_del_event(ctx, event);

    ngx_http_js_event_finalize(r->parent, NGX_OK);

    return NGX_OK;
}


static njs_int_t
ngx_http_js_ext_get_parent(njs_vm_t *vm, njs_object_prop_t *prop,
    uint32_t unused, njs_value_t *value, njs_value_t *setval,
    njs_value_t *retval)
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

    if (ctx == NULL) {
        njs_value_undefined_set(retval);
        return NJS_DECLINED;
    }

    njs_value_assign(retval, njs_value_arg(&ctx->args[0]));

    return NJS_OK;
}


static njs_int_t
ngx_http_js_ext_get_response_body(njs_vm_t *vm, njs_object_prop_t *prop,
    uint32_t unused, njs_value_t *value, njs_value_t *setval,
    njs_value_t *retval)
{
    size_t               len;
    u_char              *p;
    uint32_t             buffer_type;
    njs_int_t            ret;
    ngx_buf_t           *b;
    njs_value_t         *response_body;
    ngx_http_js_ctx_t   *ctx;
    ngx_http_request_t  *r;

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
    u_char                       storage[128];

    if (retval == NULL) {
        return NJS_OK;
    }

    /* look up hashed headers */

    if (name->length < sizeof(storage)) {
        lowcase_key = storage;

    } else {
        lowcase_key = ngx_pnalloc(r->pool, name->length);
        if (lowcase_key == NULL) {
            njs_vm_memory_error(vm);
            return NJS_ERROR;
        }
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
    u_char            sep;
    njs_chb_t         chain;
    njs_int_t         rc, ret;
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

            rc = njs_vm_value_string_create(vm, value, h->value.data,
                                            h->value.len);
            if (rc != NJS_OK) {
                return NJS_ERROR;
            }
        }

        return NJS_OK;
    }

    if ((*ph)->next == NULL || flags & NJS_HEADER_SINGLE) {
        return njs_vm_value_string_create(vm, retval, (*ph)->value.data,
                                          (*ph)->value.len);
    }

    NJS_CHB_MP_INIT(&chain, njs_vm_memory_pool(vm));

    sep = flags & NJS_HEADER_SEMICOLON ? ';' : ',';

    for (h = *ph; h; h = h->next) {
        njs_chb_append(&chain, h->value.data, h->value.len);
        njs_chb_append(&chain, &sep, 1);
        njs_chb_append_literal(&chain, " ");
    }

    ret = njs_vm_value_string_create_chb(vm, retval, &chain);

    njs_chb_destroy(&chain);

    return ret;
}
#endif


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
    u_char           *p;
    njs_int_t         rc;
    ngx_int_t         n;
    ngx_table_elt_t  *h;
    u_char            content_len[NGX_OFF_T_LEN];

    if (retval != NULL && setval == NULL) {
        if (r->headers_out.content_length == NULL
            && r->headers_out.content_length_n >= 0)
        {
            p = ngx_sprintf(content_len, "%O", r->headers_out.content_length_n);

            return njs_vm_value_string_create(vm, retval, content_len,
                                              p - content_len);
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

        return njs_vm_value_string_create(vm, retval, hdr->data, hdr->len);
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
ngx_http_js_date(njs_vm_t *vm, ngx_http_request_t *r, unsigned flags,
    njs_str_t *v, njs_value_t *setval, njs_value_t *retval)
{
    njs_int_t         rc;
    ngx_table_elt_t  *h;

    rc = ngx_http_js_header_out_special(vm, r, v, setval, retval, &h);
    if (rc == NJS_ERROR) {
        return NJS_ERROR;
    }

    if (setval != NULL || retval == NULL) {
        r->headers_out.date = h;
    }

    return NJS_OK;
}


static njs_int_t
ngx_http_js_last_modified(njs_vm_t *vm, ngx_http_request_t *r, unsigned flags,
    njs_str_t *v, njs_value_t *setval, njs_value_t *retval)
{
    njs_int_t         rc;
    ngx_table_elt_t  *h;

    rc = ngx_http_js_header_out_special(vm, r, v, setval, retval, &h);
    if (rc == NJS_ERROR) {
        return NJS_ERROR;
    }

    if (setval != NULL || retval == NULL) {
        r->headers_out.last_modified = h;
    }

    return NJS_OK;
}


static njs_int_t
ngx_http_js_location(njs_vm_t *vm, ngx_http_request_t *r, unsigned flags,
    njs_str_t *v, njs_value_t *setval, njs_value_t *retval)
{
    njs_int_t         rc;
    ngx_table_elt_t  *h;

    rc = ngx_http_js_header_out_special(vm, r, v, setval, retval, &h);
    if (rc == NJS_ERROR) {
        return NJS_ERROR;
    }

    if (setval != NULL || retval == NULL) {
        r->headers_out.location = h;
    }

    return NJS_OK;
}


static njs_int_t
ngx_http_js_server(njs_vm_t *vm, ngx_http_request_t *r, unsigned flags,
    njs_str_t *v, njs_value_t *setval, njs_value_t *retval)
{
    njs_int_t         rc;
    ngx_table_elt_t  *h;

    rc = ngx_http_js_header_out_special(vm, r, v, setval, retval, &h);
    if (rc == NJS_ERROR) {
        return NJS_ERROR;
    }

    if (setval != NULL || retval == NULL) {
        r->headers_out.server = h;
    }

    return NJS_OK;
}


static void
ngx_http_js_periodic_handler(ngx_event_t *ev)
{
    ngx_int_t               rc;
    ngx_msec_t              timer;
    ngx_connection_t       *c;
    ngx_js_periodic_t      *periodic;
    ngx_http_js_ctx_t      *ctx;
    ngx_http_request_t     *r;
    ngx_http_connection_t   hc;

    if (ngx_terminate || ngx_exiting) {
        return;
    }

    periodic = ev->data;

    timer = periodic->interval;

    if (periodic->jitter) {
        timer += (ngx_msec_t) ngx_random() % periodic->jitter;
    }

    ngx_add_timer(&periodic->event, timer);

    c = periodic->connection;

    if (c != NULL) {
        ngx_log_error(NGX_LOG_ERR, c->log, 0,
                      "http js periodic \"%V\" is already running, killing "
                      "previous instance", &periodic->method);

        ngx_http_js_periodic_finalize(c->data, NGX_ERROR);
    }

    ngx_log_debug1(NGX_LOG_DEBUG_HTTP, &periodic->log, 0,
                   "http js periodic handler: \"%V\"", &periodic->method);

    c = ngx_get_connection(0, &periodic->log);

    if (c == NULL) {
        return;
    }

    ngx_memzero(&hc, sizeof(ngx_http_connection_t));

    hc.conf_ctx = periodic->conf_ctx;

    c->data = &hc;

    r = ngx_http_create_request(c);

    if (r == NULL) {
        ngx_free_connection(c);
        c->fd = (ngx_socket_t) -1;
        return;
    }

    c->data = r;
    c->destroyed = 0;
    c->pool = r->pool;
    c->read->handler = ngx_http_js_periodic_shutdown_handler;

    periodic->connection = c;
    periodic->log_ctx.request = r;
    periodic->log_ctx.connection = c;

    r->method = NGX_HTTP_GET;
    r->method_name = ngx_http_core_get_method;

    ngx_str_set(&r->uri, "/");
    r->unparsed_uri = r->uri;
    r->valid_unparsed_uri = 1;

    r->health_check = 1;
    r->write_event_handler = ngx_http_js_periodic_write_event_handler;

    rc = ngx_http_js_init_vm(r, ngx_http_js_periodic_session_proto_id);

    if (rc == NGX_DECLINED) {
        ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                      "no \"js_import\" directives found for \"js_periodic\""
                      " handler \"%V\" in the current scope",
                      &periodic->method);
        ngx_http_js_periodic_destroy(r, periodic);
        return;
    }

    if (rc != NGX_OK) {
        ngx_http_js_periodic_destroy(r, periodic);
        return;
    }

    ctx = ngx_http_get_module_ctx(r, ngx_http_js_module);

    ctx->periodic = periodic;

    r->count++;

    rc = ctx->engine->call((ngx_js_ctx_t *) ctx, &periodic->method,
                           &ctx->args[0], 1);

    if (rc == NGX_AGAIN) {
        rc = NGX_OK;
    }

    r->count--;

    ngx_http_js_periodic_finalize(r, rc);
}


static void
ngx_http_js_periodic_write_event_handler(ngx_http_request_t *r)
{
    ngx_http_js_ctx_t  *ctx;

    ngx_log_debug0(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
                   "http js periodic write event handler");

    ctx = ngx_http_get_module_ctx(r, ngx_http_js_module);

    if (!ngx_js_ctx_pending(ctx)) {
        ngx_http_js_periodic_finalize(r, NGX_OK);
        return;
    }
}


static void
ngx_http_js_periodic_shutdown_handler(ngx_event_t *ev)
{
    ngx_connection_t  *c;

    c = ev->data;

    ngx_log_debug0(NGX_LOG_DEBUG_HTTP, c->log, 0,
                   "http js periodic shutdown handler");

    if (c->close) {
        ngx_http_js_periodic_finalize(c->data, NGX_ERROR);
        return;
    }

    ngx_log_error(NGX_LOG_ERR, c->log, 0, "http js periodic shutdown handler "
                  "while not closing");
}


static void
ngx_http_js_periodic_finalize(ngx_http_request_t *r, ngx_int_t rc)
{
    ngx_http_js_ctx_t  *ctx;

    ctx = ngx_http_get_module_ctx(r, ngx_http_js_module);

    ngx_log_debug4(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
                   "http js periodic finalize: \"%V\" rc: %i c: %i pending: %i",
                   &ctx->periodic->method, rc, r->count,
                   ngx_js_ctx_pending(ctx));

    if (r->count > 1 || (rc == NGX_OK && ngx_js_ctx_pending(ctx))) {
        return;
    }

    ngx_http_js_periodic_destroy(r, ctx->periodic);
}


static void
ngx_http_js_periodic_destroy(ngx_http_request_t *r, ngx_js_periodic_t *periodic)
{
    ngx_connection_t  *c;

    c = r->connection;

    ngx_log_debug1(NGX_LOG_DEBUG_HTTP, c->log, 0,
                   "http js periodic destroy: \"%V\"", &periodic->method);

    periodic->connection = NULL;

    r->logged = 1;

    ngx_http_free_request(r, NGX_OK);

    ngx_free_connection(c);

    c->fd = (ngx_socket_t) -1;
    c->pool = NULL;
    c->destroyed = 1;
}


static ngx_int_t
ngx_http_js_periodic_init(ngx_js_periodic_t *periodic)
{
    ngx_log_t                 *log;
    ngx_msec_t                 jitter;
    ngx_http_core_loc_conf_t  *clcf;

    clcf = ngx_http_get_module_loc_conf(periodic->conf_ctx,
                                        ngx_http_core_module);
    log = clcf->error_log;

    ngx_memcpy(&periodic->log, log, sizeof(ngx_log_t));

    periodic->log.data = &periodic->log_ctx;
    periodic->connection = NULL;

    periodic->event.handler = ngx_http_js_periodic_handler;
    periodic->event.data = periodic;
    periodic->event.log = log;
    periodic->event.cancelable = 1;

    jitter = periodic->jitter ? (ngx_msec_t) ngx_random() % periodic->jitter
                              : 0;
    ngx_add_timer(&periodic->event, jitter + 1);

    return NGX_OK;
}


static ngx_pool_t *
ngx_http_js_pool(ngx_http_request_t *r)
{
    return r->pool;
}


static ngx_resolver_t *
ngx_http_js_resolver(ngx_http_request_t *r)
{
    ngx_http_core_loc_conf_t  *clcf;

    clcf = ngx_http_get_module_loc_conf(r, ngx_http_core_module);

    return clcf->resolver;
}


static ngx_msec_t
ngx_http_js_resolver_timeout(ngx_http_request_t *r)
{
    ngx_http_core_loc_conf_t  *clcf;

    clcf = ngx_http_get_module_loc_conf(r, ngx_http_core_module);

    return clcf->resolver_timeout;
}


static void
ngx_http_js_event_finalize(ngx_http_request_t *r, ngx_int_t rc)
{
    ngx_log_debug1(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
                   "http js event finalize rc: %i", rc);

    if (rc == NGX_ERROR) {
        if (r->health_check) {
            ngx_http_js_periodic_finalize(r, NGX_ERROR);
            return;
        }

        ngx_http_finalize_request(r, NGX_ERROR);
        return;
    }

    if (rc == NGX_OK) {
        ngx_http_post_request(r, NULL);
    }

    ngx_http_run_posted_requests(r->connection);
}


static ngx_js_loc_conf_t *
ngx_http_js_loc_conf(ngx_http_request_t *r)
{
    return ngx_http_get_module_loc_conf(r, ngx_http_js_module);
}


static ngx_js_ctx_t *
ngx_http_js_ctx(ngx_http_request_t *r)
{
    return ngx_http_get_module_ctx(r, ngx_http_js_module);
}


static njs_int_t
ngx_js_http_init(njs_vm_t *vm)
{
    ngx_http_js_request_proto_id = njs_vm_external_prototype(vm,
                                           ngx_http_js_ext_request,
                                           njs_nitems(ngx_http_js_ext_request));
    if (ngx_http_js_request_proto_id < 0) {
        return NJS_ERROR;
    }

    ngx_http_js_periodic_session_proto_id = njs_vm_external_prototype(vm,
                                  ngx_http_js_ext_periodic_session,
                                  njs_nitems(ngx_http_js_ext_periodic_session));
    if (ngx_http_js_periodic_session_proto_id < 0) {
        return NJS_ERROR;
    }

    return NJS_OK;
}


static ngx_engine_t *
ngx_engine_njs_clone(ngx_js_ctx_t *ctx, ngx_js_loc_conf_t *cf,
    njs_int_t proto_id, void *external)
{
    njs_int_t           rc;
    ngx_engine_t       *engine;
    ngx_http_js_ctx_t  *hctx;

    engine = ngx_njs_clone(ctx, cf, external);
    if (engine == NULL) {
        return NULL;
    }

    rc = njs_vm_external_create(engine->u.njs.vm, njs_value_arg(&ctx->args[0]),
                                proto_id, njs_vm_external_ptr(engine->u.njs.vm),
                                0);
    if (rc != NJS_OK) {
        return NULL;
    }

    hctx = (ngx_http_js_ctx_t *) ctx;
    hctx->body_filter = ngx_http_njs_body_filter;

    return engine;
}


#if (NJS_HAVE_QUICKJS)

static ngx_int_t
ngx_http_qjs_query_string_decode(njs_chb_t *chain, const u_char *start,
    size_t size)
{
    u_char                *dst;
    uint32_t               cp;
    const u_char          *p, *end;
    njs_unicode_decode_t   ctx;

    static const int8_t  hex[256]
        njs_aligned(32) =
    {
        -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
        -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
        -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
         0,  1,  2,  3,  4,  5,  6,  7,  8,  9, -1, -1, -1, -1, -1, -1,
        -1, 10, 11, 12, 13, 14, 15, -1, -1, -1, -1, -1, -1, -1, -1, -1,
        -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
        -1, 10, 11, 12, 13, 14, 15, -1, -1, -1, -1, -1, -1, -1, -1, -1,
        -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
        -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
        -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
        -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
        -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
        -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
        -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
        -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
        -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    };

    njs_utf8_decode_init(&ctx);

    cp = 0;

    p = start;
    end = p + size;

    while (p < end) {
        if (*p == '%' && end - p > 2 && hex[p[1]] >= 0 && hex[p[2]] >= 0) {
            cp = njs_utf8_consume(&ctx, (hex[p[1]] << 4) | hex[p[2]]);
            p += 3;

        } else {
            if (*p == '+') {
                cp = ' ';
                p++;

            } else {
                cp = njs_utf8_decode(&ctx, &p, end);
            }
        }

        if (cp > NJS_UNICODE_MAX_CODEPOINT) {
            if (cp == NJS_UNICODE_CONTINUE) {
                continue;
            }

            cp = NJS_UNICODE_REPLACEMENT;
        }

        dst = njs_chb_reserve(chain, 4);
        if (dst == NULL) {
            return NGX_ERROR;
        }

        njs_chb_written(chain, njs_utf8_encode(dst, cp) - dst);
    }

    if (cp == NJS_UNICODE_CONTINUE) {
        dst = njs_chb_reserve(chain, 3);
        if (dst == NULL) {
            return NGX_ERROR;
        }

        njs_chb_written(chain,
                        njs_utf8_encode(dst, NJS_UNICODE_REPLACEMENT) - dst);
    }

    return NGX_OK;
}


static JSValue
ngx_http_qjs_ext_args(JSContext *cx, JSValueConst this_val)
{
    u_char                  *start, *end, *p, *v;
    uint32_t                 len;
    JSAtom                   key;
    JSValue                  args, val, prev, length, arr;
    njs_str_t                decoded;
    njs_int_t                ret;
    ngx_int_t                rc;
    njs_chb_t                chain;
    ngx_http_request_t      *r;
    ngx_http_qjs_request_t  *req;

    req = JS_GetOpaque(this_val, NGX_QJS_CLASS_ID_HTTP_REQUEST);
    if (req == NULL) {
        return JS_ThrowInternalError(cx, "\"this\" is not a request object");
    }

    if (!JS_IsUndefined(req->args)) {
        return JS_DupValue(cx, req->args);
    }

    args = JS_NewObject(cx);
    if (JS_IsException(args)) {
        return JS_EXCEPTION;
    }

    NJS_CHB_CTX_INIT(&chain, cx);

    r = req->request;

    rc = ngx_http_qjs_query_string_decode(&chain, r->args.data, r->args.len);
    if (rc != NGX_OK) {
        njs_chb_destroy(&chain);
        return JS_ThrowOutOfMemory(cx);
    }

    ret = njs_chb_join(&chain, &decoded);
    njs_chb_destroy(&chain);

    if (ret != NJS_OK) {
        return JS_ThrowOutOfMemory(cx);
    }

    start = decoded.start;
    end = start + decoded.length;

    while (start < end) {
        p = ngx_strlchr(start, end, '&');
        if (p == NULL) {
            p = end;
        }

        v = ngx_strlchr(start, p, '=');
        if (v == NULL) {
            v = p;
        }

        if (v == start) {
            start = p + 1;
            continue;
        }

        key = JS_NewAtomLen(cx, (const char *) start, v - start);
        if (key == JS_ATOM_NULL) {
            chain.free(cx, decoded.start);
            return JS_EXCEPTION;
        }

        val = qjs_string_create(cx, v + 1, (p == v) ? 0 : p - v - 1);
        if (JS_IsException(val)) {
            chain.free(cx, decoded.start);
            JS_FreeAtom(cx, key);
            return JS_EXCEPTION;
        }

        prev = JS_GetProperty(cx, args, key);
        if (JS_IsException(prev)) {
            chain.free(cx, decoded.start);
            JS_FreeAtom(cx, key);
            JS_FreeValue(cx, val);
            return JS_EXCEPTION;
        }

        if (JS_IsUndefined(prev)) {
            if (JS_SetProperty(cx, args, key, val) < 0) {
                goto exception;
            }

        } else if (qjs_is_array(cx, prev)) {
            length = JS_GetPropertyStr(cx, prev, "length");

            if (JS_ToUint32(cx, &len, length)) {
                goto exception;
            }

            JS_FreeValue(cx, length);

            if (JS_SetPropertyUint32(cx, prev, len, val) < 0) {
                goto exception;
            }

            JS_FreeValue(cx, prev);

        } else {

            arr = JS_NewArray(cx);
            if (JS_IsException(arr)) {
                goto exception;
            }

            if (JS_SetPropertyUint32(cx, arr, 0, prev) < 0) {
                goto exception;
            }

            if (JS_SetPropertyUint32(cx, arr, 1, val) < 0) {
                goto exception;
            }

            if (JS_SetProperty(cx, args, key, arr) < 0) {
                goto exception;
            }
        }

        JS_FreeAtom(cx, key);
        start = p + 1;
    }

    chain.free(cx, decoded.start);
    req->args = args;

    return JS_DupValue(cx, args);

exception:

    chain.free(cx, decoded.start);
    JS_FreeAtom(cx, key);
    JS_FreeValue(cx, val);
    JS_FreeValue(cx, prev);

    return JS_EXCEPTION;
}


static JSValue
ngx_http_qjs_ext_done(JSContext *cx, JSValueConst this_val,
    int argc, JSValueConst *argv)
{
    ngx_http_js_ctx_t   *ctx;
    ngx_http_request_t  *r;

    r = ngx_http_qjs_request(this_val);
    if (r == NULL) {
        return JS_ThrowInternalError(cx, "\"this\" is not a request object");
    }

    ctx = ngx_http_get_module_ctx(r, ngx_http_js_module);

    if (!ctx->filter) {
        return JS_ThrowTypeError(cx, "cannot set done while not filtering");
    }

    ctx->done = 1;

    return JS_UNDEFINED;
}


static JSValue
ngx_http_qjs_ext_finish(JSContext *cx, JSValueConst this_val,
    int argc, JSValueConst *argv)
{
    ngx_http_js_ctx_t   *ctx;
    ngx_http_request_t  *r;

    r = ngx_http_qjs_request(this_val);
    if (r == NULL) {
        return JS_ThrowInternalError(cx, "\"this\" is not a request object");
    }

    if (ngx_http_send_special(r, NGX_HTTP_LAST) == NGX_ERROR) {
        return JS_ThrowInternalError(cx, "failed to send response");
    }

    ctx = ngx_http_get_module_ctx(r, ngx_http_js_module);

    ctx->status = NGX_OK;

    return JS_UNDEFINED;
}


static JSValue
ngx_http_qjs_ext_headers_in(JSContext *cx, JSValueConst this_val)
{
    JSValue              obj;
    ngx_http_request_t  *r;

    r = ngx_http_qjs_request(this_val);
    if (r == NULL) {
        return JS_ThrowInternalError(cx, "\"this\" is not a request object");
    }

    obj = JS_NewObjectProtoClass(cx, JS_NULL, NGX_QJS_CLASS_ID_HTTP_HEADERS_IN);

    JS_SetOpaque(obj, r);

    return obj;
}


static JSValue
ngx_http_qjs_ext_headers_out(JSContext *cx, JSValueConst this_val)
{
    JSValue              obj;
    ngx_http_request_t  *r;

    r = ngx_http_qjs_request(this_val);
    if (r == NULL) {
        return JS_ThrowInternalError(cx, "\"this\" is not a request object");
    }

    obj = JS_NewObjectProtoClass(cx, JS_NULL,
                                 NGX_QJS_CLASS_ID_HTTP_HEADERS_OUT);

    JS_SetOpaque(obj, r);

    return obj;
}


static JSValue
ngx_http_qjs_ext_http_version(JSContext *cx, JSValueConst this_val)
{
    ngx_str_t            v;
    ngx_http_request_t  *r;

    r = ngx_http_qjs_request(this_val);
    if (r == NULL) {
        return JS_ThrowInternalError(cx, "\"this\" is not a request object");
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

    return qjs_string_create(cx, v.data, v.len);
}


static JSValue
ngx_http_qjs_ext_internal(JSContext *cx, JSValueConst this_val)
{
    ngx_http_request_t  *r;

    r = ngx_http_qjs_request(this_val);
    if (r == NULL) {
        return JS_ThrowInternalError(cx, "\"this\" is not a request object");
    }

    return JS_NewBool(cx, r->internal);
}


static JSValue
ngx_http_qjs_ext_internal_redirect(JSContext *cx, JSValueConst this_val,
    int argc, JSValueConst *argv)
{
    ngx_http_js_ctx_t   *ctx;
    ngx_http_request_t  *r;

    r = ngx_http_qjs_request(this_val);
    if (r == NULL) {
        return JS_ThrowInternalError(cx, "\"this\" is not a request object");
    }

    if (r->parent != NULL) {
        return JS_ThrowTypeError(cx,
                         "internalRedirect cannot be called from a subrequest");
    }

    ctx = ngx_http_get_module_ctx(r, ngx_http_js_module);

    if (ctx->filter) {
        return JS_ThrowTypeError(cx,
                         "internalRedirect cannot be called while filtering");
    }

    if (ngx_qjs_string(cx, r->pool, argv[0], &ctx->redirect_uri) != NGX_OK) {
        return JS_EXCEPTION;
    }

    ctx->status = NGX_DONE;

    return JS_UNDEFINED;
}


static JSValue
ngx_http_qjs_ext_log(JSContext *cx, JSValueConst this_val, int argc,
    JSValueConst *argv, int level)
{
    int                  n;
    const char          *msg;
    ngx_http_request_t  *r;

    r = ngx_http_qjs_request(this_val);
    if (r == NULL) {
        return JS_ThrowInternalError(cx, "\"this\" is not a request object");
    }

    for (n = 0; n < argc; n++) {
        msg = JS_ToCString(cx, argv[n]);

        ngx_js_logger(r->connection, level, (u_char *) msg, ngx_strlen(msg));

        JS_FreeCString(cx, msg);
    }

    return JS_UNDEFINED;
}


static JSValue
ngx_http_qjs_ext_periodic_variables(JSContext *cx,
    JSValueConst this_val, int type)
{
    JSValue                  obj;
    ngx_http_qjs_request_t  *req;

    req = JS_GetOpaque(this_val, NGX_QJS_CLASS_ID_HTTP_PERIODIC);
    if (req == NULL) {
        return JS_ThrowInternalError(cx, "\"this\" is not a periodic object");
    }

    obj = JS_NewObjectProtoClass(cx, JS_NULL, NGX_QJS_CLASS_ID_HTTP_VARS);

    /*
     * Using lowest bit of the pointer to store the buffer type.
     */
    type = (type == NGX_JS_BUFFER) ? 1 : 0;
    JS_SetOpaque(obj, (void *) ((uintptr_t) req->request | (uintptr_t) type));

    return obj;
}


static JSValue
ngx_http_qjs_ext_parent(JSContext *cx, JSValueConst this_val)
{
    ngx_http_js_ctx_t   *ctx;
    ngx_http_request_t  *r;

    r = ngx_http_qjs_request(this_val);
    if (r == NULL) {
        return JS_ThrowInternalError(cx, "\"this\" is not a request object");
    }

    ctx = r->parent ? ngx_http_get_module_ctx(r->parent, ngx_http_js_module)
                    : NULL;

    if (ctx == NULL) {
        return JS_UNDEFINED;
    }

    return JS_DupValue(cx, ngx_qjs_arg(ctx->args[0]));
}


static JSValue
ngx_http_qjs_ext_remote_address(JSContext *cx, JSValueConst this_val)
{
    ngx_connection_t    *c;
    ngx_http_request_t  *r;

    r = ngx_http_qjs_request(this_val);
    if (r == NULL) {
        return JS_ThrowInternalError(cx, "\"this\" is not a request object");
    }

    c = r->connection;

    return qjs_string_create(cx, c->addr_text.data, c->addr_text.len);
}


static JSValue
ngx_http_qjs_ext_response_body(JSContext *cx, JSValueConst this_val, int type)
{
    u_char                  *p;
    size_t                   len;
    uint32_t                 buffer_type;
    ngx_buf_t               *b;
    JSValue                  body;
    ngx_http_request_t      *r;
    ngx_http_qjs_request_t  *req;

    req = JS_GetOpaque(this_val, NGX_QJS_CLASS_ID_HTTP_REQUEST);
    if (req == NULL) {
        return JS_ThrowInternalError(cx, "\"this\" is not a request object");
    }

    buffer_type = ngx_js_buffer_type(type);

    if (!JS_IsUndefined(req->response_body)) {
        if ((buffer_type == NGX_JS_STRING) == JS_IsString(req->response_body)) {
            return JS_DupValue(cx, req->response_body);
        }
    }

    r = req->request;

    b = r->out ? r->out->buf : NULL;

    if (b == NULL) {
        return JS_UNDEFINED;
    }

    len = b->last - b->pos;

    p = ngx_pnalloc(r->pool, len);
    if (p == NULL) {
        return JS_ThrowOutOfMemory(cx);
    }

    if (len) {
        ngx_memcpy(p, b->pos, len);
    }

    body = ngx_qjs_prop(cx, buffer_type, p, len);
    if (JS_IsException(body)) {
        return JS_EXCEPTION;
    }

    req->response_body = body;

    return JS_DupValue(cx, req->response_body);
}


static JSValue
ngx_http_qjs_ext_request_body(JSContext *cx, JSValueConst this_val, int type)
{
    u_char                  *p, *data;
    size_t                   len;
    ssize_t                  n;
    JSValue                  body;
    uint32_t                 buffer_type;
    ngx_buf_t               *buf;
    ngx_chain_t             *cl;
    ngx_http_request_t      *r;
    ngx_http_qjs_request_t  *req;

    req = JS_GetOpaque(this_val, NGX_QJS_CLASS_ID_HTTP_REQUEST);
    if (req == NULL) {
        return JS_ThrowInternalError(cx, "\"this\" is not a request object");
    }

    buffer_type = ngx_js_buffer_type(type);

    if (!JS_IsUndefined(req->request_body)) {
        if ((buffer_type == NGX_JS_STRING) == JS_IsString(req->request_body)) {
            return JS_DupValue(cx, req->request_body);
        }

        JS_FreeValue(cx, req->request_body);
    }

    r = req->request;

    if (r->request_body == NULL || r->request_body->bufs == NULL) {
        return JS_UNDEFINED;
    }

    cl = r->request_body->bufs;
    buf = cl->buf;

    if (r->request_body->temp_file) {
        ngx_log_error(NGX_LOG_WARN, r->connection->log, 0,
                      "http js reading request body from a temporary file");

        if (buf == NULL || !buf->in_file) {
            return JS_ThrowInternalError(cx, "cannot find body file");
        }

        len = buf->file_last - buf->file_pos;

        data = ngx_pnalloc(r->pool, len);
        if (data == NULL) {
            return JS_ThrowOutOfMemory(cx);
        }

        n = ngx_read_file(buf->file, data, len, buf->file_pos);
        if (n != (ssize_t) len) {
            return JS_ThrowInternalError(cx, "failed to read request body");
        }

        goto done;
    }

    if (cl->next == NULL) {
        len = buf->last - buf->pos;
        data = buf->pos;

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
        return JS_ThrowOutOfMemory(cx);
    }

    data = p;
    cl = r->request_body->bufs;

    for ( /* void */ ; cl; cl = cl->next) {
        buf = cl->buf;
        p = ngx_cpymem(p, buf->pos, buf->last - buf->pos);
    }

done:

    body = ngx_qjs_prop(cx, buffer_type, data, len);
    if (JS_IsException(body)) {
        return JS_EXCEPTION;
    }

    req->request_body = body;

    return JS_DupValue(cx, req->request_body);
}


static JSValue
ngx_http_qjs_ext_return(JSContext *cx, JSValueConst this_val,
    int argc, JSValueConst *argv)
{
    ngx_str_t                  body;
    ngx_int_t                  status;
    ngx_http_js_ctx_t         *ctx;
    ngx_http_request_t        *r;
    ngx_http_complex_value_t   cv;

    r = ngx_http_qjs_request(this_val);
    if (r == NULL) {
        return JS_ThrowInternalError(cx, "\"this\" is not a request object");
    }

    if (ngx_qjs_integer(cx, argv[0], &status) != NGX_OK) {
        return JS_EXCEPTION;
    }

    if (status < 0 || status > 999) {
        return JS_ThrowRangeError(cx, "code is out of range");
    }

    ctx = ngx_http_get_module_ctx(r, ngx_http_js_module);

    if (status < NGX_HTTP_BAD_REQUEST || !JS_IsNullOrUndefined(argv[1])) {
        if (ngx_qjs_string(cx, r->pool, argv[1], &body) != NGX_OK) {
            return JS_ThrowOutOfMemory(cx);
        }

        ngx_memzero(&cv, sizeof(ngx_http_complex_value_t));

        cv.value.data = body.data;
        cv.value.len = body.len;

        r->disable_not_modified = 1;

        ctx->status = ngx_http_send_response(r, status, NULL, &cv);

        if (ctx->status == NGX_ERROR) {
            return JS_ThrowTypeError(cx, "failed to send response");
        }

    } else {
        ctx->status = status;
    }

    return JS_UNDEFINED;
}


static JSValue
ngx_http_qjs_ext_status_get(JSContext *cx, JSValueConst this_val)
{
    ngx_http_request_t  *r;

    r = ngx_http_qjs_request(this_val);
    if (r == NULL) {
        return JS_ThrowInternalError(cx, "\"this\" is not a request object");
    }

    return JS_NewInt32(cx, r->headers_out.status);
}


static JSValue
ngx_http_qjs_ext_status_set(JSContext *cx, JSValueConst this_val,
    JSValueConst value)
{
    ngx_int_t            n;
    ngx_http_request_t  *r;

    r = ngx_http_qjs_request(this_val);
    if (r == NULL) {
        return JS_ThrowInternalError(cx, "\"this\" is not a request object");
    }

    if (ngx_qjs_integer(cx, value, &n) != NGX_OK) {
        return JS_EXCEPTION;
    }

    r->headers_out.status = n;
    r->headers_out.status_line.len = 0;

    return JS_UNDEFINED;
}


static JSValue
ngx_http_qjs_ext_string(JSContext *cx, JSValueConst this_val, int offset)
{
    ngx_str_t           *field;
    ngx_http_request_t  *r;

    r = ngx_http_qjs_request(this_val);
    if (r == NULL) {
        return JS_ThrowInternalError(cx, "\"this\" is not a request object");
    }

    field = (ngx_str_t *) ((u_char *) r + offset);

    return qjs_string_create(cx, field->data, field->len);
}


static JSValue
ngx_http_qjs_ext_send(JSContext *cx, JSValueConst this_val,
    int argc, JSValueConst *argv)
{
    ngx_str_t            s;
    ngx_buf_t           *b;
    ngx_uint_t           n;
    ngx_chain_t         *out, *cl, **ll;
    ngx_http_js_ctx_t   *ctx;
    ngx_http_request_t  *r;

    r = ngx_http_qjs_request(this_val);
    if (r == NULL) {
        return JS_ThrowInternalError(cx, "\"this\" is not a request object");
    }

    ctx = ngx_http_get_module_ctx(r, ngx_http_js_module);

    if (ctx->filter) {
        return JS_ThrowTypeError(cx, "cannot send while in body filter");
    }

    out = NULL;
    ll = &out;

    for (n = 0; n < (ngx_uint_t) argc; n++) {
        if (ngx_qjs_string(cx, r->pool, argv[n], &s) != NGX_OK) {
            return JS_ThrowTypeError(cx, "failed to convert arg");
        }

        if (s.len == 0) {
            continue;
        }

        b = ngx_calloc_buf(r->pool);
        if (b == NULL) {
            return JS_ThrowInternalError(cx, "failed to allocate buffer");
        }

        b->start = s.data;
        b->pos = b->start;
        b->end = s.data + s.len;
        b->last = b->end;
        b->memory = 1;

        cl = ngx_alloc_chain_link(r->pool);
        if (cl == NULL) {
            return JS_ThrowInternalError(cx, "failed to allocate chain link");
        }

        cl->buf = b;

        *ll = cl;
        ll = &cl->next;
    }

    *ll = NULL;

    if (ngx_http_output_filter(r, out) == NGX_ERROR) {
        return JS_ThrowInternalError(cx, "failed to send response");
    }

    return JS_UNDEFINED;
}


static JSValue
ngx_http_qjs_ext_send_buffer(JSContext *cx, JSValueConst this_val,
    int argc, JSValueConst *argv)
{
    size_t               byte_offset, byte_length, len;
    unsigned             last_buf, flush;
    JSValue              flags, value, val, buf;
    ngx_str_t            buffer;
    ngx_buf_t           *b;
    const char          *str;
    ngx_chain_t         *cl;
    ngx_http_js_ctx_t   *ctx;
    ngx_http_request_t  *r;

    r = ngx_http_qjs_request(this_val);
    if (r == NULL) {
        return JS_ThrowInternalError(cx, "\"this\" is not a request object");
    }

    ctx = ngx_http_get_module_ctx(r, ngx_http_js_module);

    if (!ctx->filter) {
        return JS_ThrowTypeError(cx, "cannot send buffer while not filtering");
    }

    flush = ctx->buf->flush;
    last_buf = ctx->buf->last_buf;

    flags = argv[1];

    if (JS_IsObject(flags)) {
        value = JS_GetPropertyStr(cx, flags, "flush");
        if (JS_IsException(value)) {
            return JS_EXCEPTION;
        }

        flush = JS_ToBool(cx, value);
        JS_FreeValue(cx, value);

        value = JS_GetPropertyStr(cx, flags, "last");
        if (JS_IsException(value)) {
            return JS_EXCEPTION;
        }

        last_buf = JS_ToBool(cx, value);
        JS_FreeValue(cx, value);
    }

    val = argv[0];

    if (JS_IsNullOrUndefined(val)) {
        buffer.len = 0;
        buffer.data = NULL;
    }

    str = NULL;
    buf = JS_UNDEFINED;

    if (JS_IsString(val)) {
        goto string;
    }

    buf = JS_GetTypedArrayBuffer(cx, val, &byte_offset, &byte_length, NULL);
    if (!JS_IsException(buf)) {
        buffer.data = JS_GetArrayBuffer(cx, &buffer.len, buf);
        if (buffer.data == NULL) {
            JS_FreeValue(cx, buf);
            return JS_EXCEPTION;
        }

        buffer.data += byte_offset;
        buffer.len = byte_length;

    } else {
string:

        str = JS_ToCStringLen(cx, &buffer.len, val);
        if (str == NULL) {
            return JS_EXCEPTION;
        }

        buffer.data = (u_char *) str;
    }

    do {
        cl = ngx_chain_get_free_buf(r->pool, &ctx->free);
        if (cl == NULL) {
            goto out_of_memory;
        }

        b = cl->buf;

        if (b->start == NULL) {
            b->start = ngx_pnalloc(r->pool, buffer.len);
            if (b->start == NULL) {
                goto out_of_memory;
            }

            len = buffer.len;
            b->end = b->start + len;

        } else {
            len = ngx_min(buffer.len, (size_t) (b->end - b->start));
        }

        memcpy(b->start, buffer.data, len);

        b->pos = b->start;
        b->last = b->start + len;

        if (buffer.len == len) {
            b->last_buf = last_buf;
            b->flush = flush;

        } else {
            b->last_buf = 0;
            b->flush = 0;
        }

        b->memory = (len ? 1 : 0);
        b->sync = (len ? 0 : 1);
        b->tag = (ngx_buf_tag_t) &ngx_http_js_module;

        buffer.data += len;
        buffer.len -= len;

        *ctx->last_out = cl;
        ctx->last_out = &cl->next;

    } while (buffer.len != 0);

    if (str != NULL) {
        JS_FreeCString(cx, str);
    }

    JS_FreeValue(cx, buf);

    return JS_UNDEFINED;

out_of_memory:

    if (str != NULL) {
        JS_FreeCString(cx, str);
    }

    JS_FreeValue(cx, buf);

    return JS_ThrowOutOfMemory(cx);
}


static JSValue
ngx_http_qjs_ext_send_header(JSContext *cx, JSValueConst this_val,
    int argc, JSValueConst *argv)
{
    ngx_http_request_t  *r;

    r = ngx_http_qjs_request(this_val);
    if (r == NULL) {
        return JS_ThrowInternalError(cx, "\"this\" is not a request object");
    }

    if (ngx_http_set_content_type(r) != NGX_OK) {
        return JS_ThrowInternalError(cx, "failed to set content type");
    }

    r->disable_not_modified = 1;

    if (ngx_http_send_header(r) == NGX_ERROR) {
        return JS_ThrowInternalError(cx, "failed to send header");
    }

    return JS_UNDEFINED;
}


static JSValue
ngx_http_qjs_ext_set_return_value(JSContext *cx, JSValueConst this_val,
    int argc, JSValueConst *argv)
{
    ngx_js_ctx_t        *ctx;
    ngx_http_request_t  *r;

    r = ngx_http_qjs_request(this_val);
    if (r == NULL) {
        return JS_ThrowInternalError(cx, "\"this\" is not a request object");
    }

    ctx = ngx_http_get_module_ctx(r, ngx_http_js_module);

    JS_FreeValue(cx, ngx_qjs_arg(ctx->retval));
    ngx_qjs_arg(ctx->retval) = JS_DupValue(cx, argv[0]);

    return JS_UNDEFINED;
}


static ngx_int_t
ngx_http_qjs_subrequest_done(ngx_http_request_t *r, void *data, ngx_int_t rc)
{
    ngx_qjs_event_t  *event = data;

    JSValue              reply;
    JSContext           *cx;
    ngx_http_js_ctx_t   *ctx, *sctx;

    if (rc != NGX_OK || r->connection->error || r->buffered) {
        return rc;
    }

    sctx = ngx_http_get_module_ctx(r, ngx_http_js_module);

    if (sctx && sctx->done) {
        return NGX_OK;
    }

    if (sctx == NULL) {
        sctx = ngx_pcalloc(r->pool, sizeof(ngx_http_js_ctx_t));
        if (sctx == NULL) {
            return NGX_ERROR;
        }

        ngx_http_set_ctx(r, sctx, ngx_http_js_module);

        ngx_qjs_arg(sctx->response_body) = JS_UNDEFINED;
    }

    sctx->done = 1;

    ctx = ngx_http_get_module_ctx(r->parent, ngx_http_js_module);

    ngx_log_debug2(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
                   "js subrequest done s: %ui parent ctx: %p",
                   r->headers_out.status, ctx);

    if (ctx == NULL) {
        ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                      "js subrequest: failed to get the parent context");

        return NGX_ERROR;
    }

    cx = ctx->engine->u.qjs.ctx;

    reply = ngx_http_qjs_request_make(cx, NGX_QJS_CLASS_ID_HTTP_REQUEST, r);
    if (JS_IsException(reply)) {
        ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                      "js subrequest reply creation failed");
        return NGX_ERROR;
    }

    rc = ngx_qjs_call(cx, event->function, &reply, 1);

    JS_FreeValue(cx, reply);
    ngx_js_del_event(ctx, event);

    ngx_http_js_event_finalize(r->parent, NGX_OK);

    return NGX_OK;
}


static void
ngx_http_js_subrequest_event_destructor(ngx_qjs_event_t *event)
{
    JSContext  *cx;

    cx = event->ctx;

    JS_FreeValue(cx, event->function);
    JS_FreeValue(cx, event->args[0]);
    JS_FreeValue(cx, event->args[1]);
}


static JSValue
ngx_http_qjs_ext_subrequest(JSContext *cx, JSValueConst this_val,
    int argc, JSValueConst *argv)
{
    JSValue                      arg, options, callback, value, retval;
    ngx_int_t                    rc;
    ngx_str_t                    uri, args, method_name, body_arg;
    ngx_uint_t                   method, methods_max, has_body, detached, flags,
                                 promise;
    ngx_qjs_event_t             *event;
    ngx_http_js_ctx_t           *ctx;
    ngx_http_request_t          *r, *sr;
    ngx_http_request_body_t     *rb;
    ngx_http_post_subrequest_t  *ps;

    r = ngx_http_qjs_request(this_val);
    if (r == NULL) {
        return JS_ThrowInternalError(cx, "\"this\" is not a request object");
    }

    ctx = ngx_http_get_module_ctx(r, ngx_http_js_module);

    if (r->subrequest_in_memory) {
        return JS_ThrowTypeError(cx, "subrequest can only be created for "
                                     "the primary request");
    }

    if (ngx_qjs_string(cx, r->pool, argv[0], &uri) != NGX_OK) {
        return JS_ThrowTypeError(cx, "failed to convert uri arg");
    }

    if (uri.len == 0) {
        return JS_ThrowTypeError(cx, "uri is empty");
    }

    options = JS_UNDEFINED;
    callback = JS_UNDEFINED;

    method = 0;
    methods_max = sizeof(ngx_http_methods) / sizeof(ngx_http_methods[0]);

    args.len = 0;
    args.data = NULL;

    method_name.len = 0;
    method_name.data = NULL;

    has_body = 0;
    detached = 0;

    arg = argv[1];

    if (JS_IsString(arg)) {
        if (ngx_qjs_string(cx, r->pool, arg, &args) != NGX_OK) {
            return JS_ThrowTypeError(cx, "failed to convert args");
        }

    } else if (JS_IsFunction(cx, arg)) {
        callback = arg;

    } else if (JS_IsObject(arg)) {
        options = arg;

    } else if (!JS_IsNullOrUndefined(arg)) {
        return JS_ThrowTypeError(cx, "failed to convert args");
    }

    if (!JS_IsUndefined(options)) {
        value = JS_GetPropertyStr(cx, options, "args");
        if (JS_IsException(value)) {
            return JS_EXCEPTION;
        }

        if (!JS_IsUndefined(value)) {
            rc = ngx_qjs_string(cx, r->pool, value, &args);
            JS_FreeValue(cx, value);

            if (rc != NGX_OK) {
                return JS_ThrowTypeError(cx, "failed to convert options.args");
            }
        }

        value = JS_GetPropertyStr(cx, options, "detached");
        if (JS_IsException(value)) {
            return JS_EXCEPTION;
        }

        if (!JS_IsUndefined(value)) {
            detached = JS_ToBool(cx, value);
            JS_FreeValue(cx, value);
        }

        value = JS_GetPropertyStr(cx, options, "method");
        if (JS_IsException(value)) {
            return JS_EXCEPTION;
        }

        if (!JS_IsUndefined(value)) {
            rc = ngx_qjs_string(cx, r->pool, value, &method_name);
            JS_FreeValue(cx, value);

            if (rc != NGX_OK) {
                return JS_ThrowTypeError(cx, "failed to convert option.method");
            }

            while (method < methods_max) {
                if (method_name.len == ngx_http_methods[method].name.len
                    && ngx_memcmp(method_name.data,
                                  ngx_http_methods[method].name.data,
                                  method_name.len)
                       == 0)
                {
                    break;
                }

                method++;
            }
        }

        value = JS_GetPropertyStr(cx, options, "body");
        if (JS_IsException(value)) {
            return JS_EXCEPTION;
        }

        if (!JS_IsUndefined(value)) {
            rc = ngx_qjs_string(cx, r->pool, value, &body_arg);
            JS_FreeValue(cx, value);

            if (rc != NGX_OK) {
                return JS_ThrowTypeError(cx, "failed to convert option.body");
            }

            has_body = 1;
        }
    }

    flags = NGX_HTTP_LOG_UNSAFE;

    if (ngx_http_parse_unsafe_uri(r, &uri, &args, &flags) != NGX_OK) {
        return JS_ThrowTypeError(cx, "unsafe uri");
    }

    arg = argv[2];

    if (JS_IsUndefined(callback) && !JS_IsNullOrUndefined(arg)) {
        if (!JS_IsFunction(cx, arg)) {
            return JS_ThrowTypeError(cx, "callback is not a function");
        }

        callback = arg;
    }

    if (detached && !JS_IsUndefined(callback)) {
        return JS_ThrowTypeError(cx, "detached flag and callback are mutually "
                                     "exclusive");
    }

    retval = JS_UNDEFINED;
    flags = NGX_HTTP_SUBREQUEST_BACKGROUND;

    if (!detached) {
        ps = ngx_palloc(r->pool, sizeof(ngx_http_post_subrequest_t));
        if (ps == NULL) {
            return JS_ThrowOutOfMemory(cx);
        }

        promise = !!JS_IsUndefined(callback);

        event = ngx_pcalloc(r->pool, sizeof(ngx_qjs_event_t)
                                     + sizeof(JSValue) * 2);
        if (event == NULL) {
            return JS_ThrowOutOfMemory(cx);
        }

        event->ctx = cx;
        event->fd = ctx->event_id++;
        event->args = (JSValue *) &event[1];
        event->destructor = ngx_http_js_subrequest_event_destructor;

        if (promise) {
            retval = JS_NewPromiseCapability(cx, &event->args[0]);
            if (JS_IsException(retval)) {
                return JS_EXCEPTION;
            }

            callback = event->args[0];

        } else {
            event->args[0] = JS_UNDEFINED;
            event->args[1] = JS_UNDEFINED;
        }

        event->function = JS_DupValue(cx, callback);

        ps->handler = ngx_http_qjs_subrequest_done;
        ps->data = event;

        flags |= NGX_HTTP_SUBREQUEST_IN_MEMORY;

    } else {
        ps = NULL;
        event = NULL;
    }

    if (ngx_http_subrequest(r, &uri, args.len ? &args : NULL, &sr, ps, flags)
        != NGX_OK)
    {
        return JS_ThrowInternalError(cx, "subrequest creation failed");
    }

    if (event != NULL) {
        ngx_js_add_event(ctx, event);
    }

    if (method != methods_max) {
        sr->method = ngx_http_methods[method].value;
        sr->method_name = ngx_http_methods[method].name;

    } else {
        sr->method = NGX_HTTP_UNKNOWN;
        sr->method_name = method_name;
    }

    sr->header_only = (sr->method == NGX_HTTP_HEAD) || JS_IsUndefined(callback);

    if (has_body) {
        rb = ngx_pcalloc(r->pool, sizeof(ngx_http_request_body_t));
        if (rb == NULL) {
            goto memory_error;
        }

        if (body_arg.len != 0) {
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

            rb->bufs->buf->pos = body_arg.data;
            rb->bufs->buf->last = body_arg.data + body_arg.len;
        }

        sr->request_body = rb;
        sr->headers_in.content_length_n = body_arg.len;
        sr->headers_in.chunked = 0;
    }

    return retval;

memory_error:

    return JS_ThrowOutOfMemory(cx);
}


static JSValue
ngx_http_qjs_ext_raw_headers(JSContext *cx, JSValueConst this_val, int out)
{
    JSValue              array, elem, key, val;
    uint32_t             idx;
    ngx_uint_t           i;
    ngx_list_t          *headers;
    ngx_list_part_t     *part;
    ngx_table_elt_t     *header, *h;
    ngx_http_request_t  *r;

    r = ngx_http_qjs_request(this_val);
    if (r == NULL) {
        return JS_ThrowInternalError(cx, "\"this\" is not a request object");
    }

    headers = (out) ? &r->headers_out.headers : &r->headers_in.headers;

    array = JS_NewArray(cx);
    if (JS_IsException(array)) {
        return JS_EXCEPTION;
    }

    idx = 0;
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

        elem = JS_NewArray(cx);
        if (JS_IsException(elem)) {
            JS_FreeValue(cx, array);
            return JS_EXCEPTION;
        }

        if (JS_DefinePropertyValueUint32(cx, array, idx++, elem,
                                         JS_PROP_C_W_E) < 0)
        {
            JS_FreeValue(cx, elem);
            JS_FreeValue(cx, array);
            return JS_EXCEPTION;
        }

        key = qjs_string_create(cx, h->key.data, h->key.len);
        if (JS_IsException(key)) {
            JS_FreeValue(cx, array);
            return JS_EXCEPTION;
        }

        if (JS_DefinePropertyValueUint32(cx, elem, 0, key, JS_PROP_C_W_E) < 0) {
            JS_FreeValue(cx, key);
            JS_FreeValue(cx, array);
            return JS_EXCEPTION;
        }

        val = qjs_string_create(cx, h->value.data, h->value.len);
        if (JS_IsException(val)) {
            JS_FreeValue(cx, array);
            return JS_EXCEPTION;
        }

        if (JS_DefinePropertyValueUint32(cx, elem, 1, val, JS_PROP_C_W_E) < 0) {
            JS_FreeValue(cx, val);
            JS_FreeValue(cx, array);
            return JS_EXCEPTION;
        }
    }

    return array;
}


static JSValue
ngx_http_qjs_ext_variables(JSContext *cx, JSValueConst this_val, int type)
{
    JSValue              obj;
    ngx_http_request_t  *r;

    r = ngx_http_qjs_request(this_val);
    if (r == NULL) {
        return JS_ThrowInternalError(cx, "\"this\" is not a request object");
    }

    obj = JS_NewObjectProtoClass(cx, JS_NULL, NGX_QJS_CLASS_ID_HTTP_VARS);

    /*
     * Using lowest bit of the pointer to store the buffer type.
     */
    type = (type == NGX_JS_BUFFER) ? 1 : 0;
    JS_SetOpaque(obj, (void *) ((uintptr_t) r | (uintptr_t) type));

    return obj;
}


static int
ngx_http_qjs_variables_own_property(JSContext *cx, JSPropertyDescriptor *pdesc,
    JSValueConst obj, JSAtom prop)
{
    uint32_t                    buffer_type;
    ngx_str_t                   name, name_lc;
    ngx_uint_t                  i, key, start, length, is_capture;
    ngx_http_request_t         *r;
    ngx_http_variable_value_t  *vv;
    u_char                      storage[64];

    r = JS_GetOpaque(obj, NGX_QJS_CLASS_ID_HTTP_VARS);

    buffer_type = ((uintptr_t) r & 1) ? NGX_JS_BUFFER : NGX_JS_STRING;
    r = (ngx_http_request_t *) ((uintptr_t) r & ~(uintptr_t) 1);

    if (r == NULL) {
        (void) JS_ThrowInternalError(cx, "\"this\" is not a request object");
        return -1;
    }

    name.data = (u_char *) JS_AtomToCString(cx, prop);
    if (name.data == NULL) {
        return -1;
    }

    name.len = ngx_strlen(name.data);

    is_capture = 1;
    for (i = 0; i < name.len; i++) {
        if (name.data[i] < '0' || name.data[i] > '9') {
            is_capture = 0;
            break;
        }
    }

    if (is_capture) {
        key = ngx_atoi(name.data, name.len) * 2;
        JS_FreeCString(cx, (char *) name.data);
        if (r->captures == NULL || r->captures_data == NULL
            || r->ncaptures <= key)
        {
            return 0;
        }


        if (pdesc != NULL) {
            pdesc->flags = JS_PROP_WRITABLE | JS_PROP_CONFIGURABLE;
            pdesc->getter = JS_UNDEFINED;
            pdesc->setter = JS_UNDEFINED;

            start = r->captures[key];
            length = r->captures[key + 1] - start;
            pdesc->value = ngx_qjs_prop(cx, buffer_type,
                                        &r->captures_data[start], length);
        }

        return 1;
    }

    if (name.len < sizeof(storage)) {
        name_lc.data = storage;

    } else {
        name_lc.data = ngx_pnalloc(r->pool, name.len);
        if (name_lc.data == NULL) {
            (void) JS_ThrowOutOfMemory(cx);
            return -1;
        }
    }

    name_lc.len = name.len;

    key = ngx_hash_strlow(name_lc.data, name.data, name.len);

    vv = ngx_http_get_variable(r, &name_lc, key);
    JS_FreeCString(cx, (char *) name.data);
    if (vv == NULL || vv->not_found) {
        return 0;
    }

    if (pdesc != NULL) {
        pdesc->flags = JS_PROP_WRITABLE | JS_PROP_CONFIGURABLE;
        pdesc->getter = JS_UNDEFINED;
        pdesc->setter = JS_UNDEFINED;
        pdesc->value = ngx_qjs_prop(cx, buffer_type, vv->data, vv->len);
    }

    return 1;
}


static int
ngx_http_qjs_variables_set_property(JSContext *cx, JSValueConst obj,
    JSAtom prop, JSValueConst value, JSValueConst receiver, int flags)
{
    u_char                     *lowcase_key;
    ngx_str_t                   name, s;
    ngx_uint_t                  key;
    ngx_http_request_t         *r;
    ngx_http_variable_t        *v;
    ngx_http_variable_value_t  *vv;
    ngx_http_core_main_conf_t  *cmcf;
    u_char                      storage[64];

    r = JS_GetOpaque(obj, NGX_QJS_CLASS_ID_HTTP_VARS);

    r = (ngx_http_request_t *) ((uintptr_t) r & ~(uintptr_t) 1);

    if (r == NULL) {
        (void) JS_ThrowInternalError(cx, "\"this\" is not a request object");
        return -1;
    }

    name.data = (u_char *) JS_AtomToCString(cx, prop);
    if (name.data == NULL) {
        return -1;
    }

    name.len = ngx_strlen(name.data);

    if (name.len < sizeof(storage)) {
        lowcase_key = storage;

    } else {
        lowcase_key = ngx_pnalloc(r->pool, name.len);
        if (lowcase_key == NULL) {
            (void) JS_ThrowOutOfMemory(cx);
            return -1;
        }
    }

    key = ngx_hash_strlow(lowcase_key, name.data, name.len);

    cmcf = ngx_http_get_module_main_conf(r, ngx_http_core_module);

    v = ngx_hash_find(&cmcf->variables_hash, key, lowcase_key, name.len);
    JS_FreeCString(cx, (char *) name.data);

    if (v == NULL) {
        (void) JS_ThrowInternalError(cx, "variable not found");
        return -1;
    }

    if (ngx_qjs_string(cx, r->pool, value, &s) != NGX_OK) {
        return -1;
    }

    if (v->set_handler != NULL) {
        vv = ngx_pcalloc(r->pool, sizeof(ngx_http_variable_value_t));
        if (vv == NULL) {
            (void) JS_ThrowOutOfMemory(cx);
            return -1;
        }

        vv->valid = 1;
        vv->not_found = 0;
        vv->data = s.data;
        vv->len = s.len;

        v->set_handler(r, vv, v->data);

        return 1;
    }

    if (!(v->flags & NGX_HTTP_VAR_INDEXED)) {
        (void) JS_ThrowTypeError(cx, "variable is not writable");
        return -1;
    }

    vv = &r->variables[v->index];

    vv->valid = 1;
    vv->not_found = 0;

    vv->data = ngx_pnalloc(r->pool, s.len);
    if (vv->data == NULL) {
        vv->valid = 0;
        (void) JS_ThrowOutOfMemory(cx);
        return -1;
    }

    vv->len = s.len;
    ngx_memcpy(vv->data, s.data, vv->len);

    return 1;
}


static int
ngx_http_qjs_ext_keys_header(JSContext *cx, ngx_list_t *headers, JSValue keys,
    JSPropertyEnum **ptab, uint32_t *plen)
{
    JSAtom            key;
    ngx_uint_t        item;
    ngx_list_part_t  *part;
    ngx_table_elt_t  *header, *h;

    part = &headers->part;
    item = 0;

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

        key = JS_NewAtomLen(cx, (const char *) h->key.data, h->key.len);
        if (key == JS_ATOM_NULL) {
            return -1;
        }

        if (JS_DefinePropertyValue(cx, keys, key, JS_UNDEFINED,
                                   JS_PROP_ENUMERABLE) < 0)
        {
            JS_FreeAtom(cx, key);
            return -1;
        }

        JS_FreeAtom(cx, key);
    }

    return JS_GetOwnPropertyNames(cx, ptab, plen, keys, JS_GPN_STRING_MASK);
}


static int
ngx_http_qjs_headers_in_own_property_names(JSContext *cx,
    JSPropertyEnum **ptab, uint32_t *plen, JSValueConst obj)
{
    int                  ret;
    JSValue              keys;
    ngx_http_request_t  *r;

    r = JS_GetOpaque(obj, NGX_QJS_CLASS_ID_HTTP_HEADERS_IN);
    if (r == NULL) {
        (void) JS_ThrowInternalError(cx, "\"this\" is not a headers_in object");
        return -1;
    }

    keys = JS_NewObject(cx);
    if (JS_IsException(keys)) {
        return -1;
    }

    ret = ngx_http_qjs_ext_keys_header(cx, &r->headers_in.headers, keys, ptab,
                                       plen);
    JS_FreeValue(cx, keys);

    return ret;
}


static njs_int_t
ngx_http_qjs_header_generic(JSContext *cx, ngx_http_request_t *r,
    ngx_list_t *headers, ngx_table_elt_t **ph, ngx_str_t *name,
    JSPropertyDescriptor *pdesc, unsigned flags)
{
    u_char            sep;
    JSValue           val;
    njs_chb_t         chain;
    ngx_uint_t        i;
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
                || name->len != h[i].key.len
                || ngx_strncasecmp(name->data, h[i].key.data, name->len)
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
        return 0;
    }

    if (flags & NJS_HEADER_ARRAY) {
        if (pdesc == NULL) {
            return 1;
        }

        pdesc->flags = JS_PROP_ENUMERABLE;
        pdesc->getter = JS_UNDEFINED;
        pdesc->setter = JS_UNDEFINED;
        pdesc->value = JS_NewArray(cx);
        if (JS_IsException(pdesc->value)) {
            return -1;
        }

        for (h = *ph, i = 0; h; h = h->next, i++) {
            val = qjs_string_create(cx, h->value.data, h->value.len);
            if (JS_IsException(val)) {
                JS_FreeValue(cx, pdesc->value);
                return -1;
            }

            if (JS_DefinePropertyValueUint32(cx, pdesc->value, i, val,
                                             JS_PROP_ENUMERABLE) < 0)
            {
                JS_FreeValue(cx, pdesc->value);
                return -1;
            }
        }

        return 1;
    }

    if ((*ph)->next == NULL || flags & NJS_HEADER_SINGLE) {
        if (pdesc != NULL) {
            pdesc->flags = JS_PROP_ENUMERABLE;
            pdesc->getter = JS_UNDEFINED;
            pdesc->setter = JS_UNDEFINED;
            pdesc->value = qjs_string_create(cx, (*ph)->value.data,
                                             (*ph)->value.len);
            if (JS_IsException(pdesc->value)) {
                return -1;
            }
        }

        return 1;
    }

    if (pdesc == NULL) {
        return 1;
    }

    NJS_CHB_CTX_INIT(&chain, cx);

    sep = flags & NJS_HEADER_SEMICOLON ? ';' : ',';

    for (h = *ph; h; h = h->next) {
        njs_chb_append(&chain, h->value.data, h->value.len);
        njs_chb_append(&chain, &sep, 1);
        njs_chb_append_literal(&chain, " ");
    }

    pdesc->flags = JS_PROP_ENUMERABLE;
    pdesc->getter = JS_UNDEFINED;
    pdesc->setter = JS_UNDEFINED;
    pdesc->value = qjs_string_create_chb(cx, &chain);
    if (JS_IsException(pdesc->value)) {
        return -1;
    }

    return 1;
}


static int
ngx_http_qjs_header_in(JSContext *cx, ngx_http_request_t *r, unsigned flags,
    ngx_str_t *name, JSPropertyDescriptor *pdesc)
{
    u_char                      *lowcase_key;
    ngx_uint_t                   hash;
    ngx_table_elt_t            **ph;
    ngx_http_header_t           *hh;
    ngx_http_core_main_conf_t   *cmcf;
    u_char                       storage[128];

    /* look up hashed headers */

    if (name->len < sizeof(storage)) {
        lowcase_key = storage;

    } else {
        lowcase_key = ngx_pnalloc(r->pool, name->len);
        if (lowcase_key == NULL) {
            (void) JS_ThrowOutOfMemory(cx);
            return -1;
        }
    }

    hash = ngx_hash_strlow(lowcase_key, name->data, name->len);

    cmcf = ngx_http_get_module_main_conf(r, ngx_http_core_module);

    hh = ngx_hash_find(&cmcf->headers_in_hash, hash, lowcase_key,
                       name->len);

    ph = NULL;

    if (hh) {
        if (hh->offset == offsetof(ngx_http_headers_in_t, cookie)) {
            flags |= NJS_HEADER_SEMICOLON;
        }

        ph = (ngx_table_elt_t **) ((char *) &r->headers_in + hh->offset);
    }

    return ngx_http_qjs_header_generic(cx, r, &r->headers_in.headers, ph, name,
                                       pdesc, flags);
}


static int
ngx_http_qjs_headers_in_own_property(JSContext *cx, JSPropertyDescriptor *pdesc,
    JSValueConst obj, JSAtom prop)
{
    int                  ret;
    unsigned             flags;
    ngx_str_t            name, *h;
    ngx_http_request_t  *r;

    static ngx_str_t single_headers_in[] = {
        ngx_string("Content-Type"),
        ngx_string("ETag"),
        ngx_string("From"),
        ngx_string("Max-Forwards"),
        ngx_string("Referer"),
        ngx_string("Proxy-Authorization"),
        ngx_string("User-Agent"),
        ngx_string(""),
    };

    r = JS_GetOpaque(obj, NGX_QJS_CLASS_ID_HTTP_HEADERS_IN);
    if (r == NULL) {
        (void) JS_ThrowInternalError(cx, "\"this\" is not a headers_in object");
        return -1;
    }

    name.data = (u_char *) JS_AtomToCString(cx, prop);
    if (name.data == NULL) {
        return -1;
    }

    name.len = ngx_strlen(name.data);

    flags = 0;

    for (h = single_headers_in; h->len > 0; h++) {
        if (h->len == name.len
            && ngx_strncasecmp(h->data, name.data, name.len) == 0)
        {
            flags |= NJS_HEADER_SINGLE;
            break;
        }
    }

    ret = ngx_http_qjs_header_in(cx, r, flags, &name, pdesc);
    JS_FreeCString(cx, (char *) name.data);

    return ret;
}


static int
ngx_http_qjs_headers_out_own_property_names(JSContext *cx,
    JSPropertyEnum **ptab, uint32_t *plen, JSValueConst obj)
{
    int                  ret;
    JSAtom               key;
    JSValue              keys;
    ngx_http_request_t  *r;

    r = JS_GetOpaque(obj, NGX_QJS_CLASS_ID_HTTP_HEADERS_OUT);
    if (r == NULL) {
        (void) JS_ThrowInternalError(cx, "\"this\" is not a headers_out"
                                     " object");
        return -1;
    }

    keys = JS_NewObject(cx);
    if (JS_IsException(keys)) {
        return -1;
    }

    if (r->headers_out.content_type.len) {
        key = JS_NewAtomLen(cx, "Content-Type", njs_length("Content-Type"));
        if (key == JS_ATOM_NULL) {
            return -1;
        }

        if (JS_DefinePropertyValue(cx, keys, key, JS_UNDEFINED,
                                   JS_PROP_ENUMERABLE) < 0)
        {
            JS_FreeAtom(cx, key);
            return -1;
        }

        JS_FreeAtom(cx, key);
    }

    if (r->headers_out.content_length == NULL
        && r->headers_out.content_length_n >= 0)
    {
        key = JS_NewAtomLen(cx, "Content-Length", njs_length("Content-Length"));
        if (key == JS_ATOM_NULL) {
            return -1;
        }

        if (JS_DefinePropertyValue(cx, keys, key, JS_UNDEFINED,
                                   JS_PROP_ENUMERABLE) < 0)
        {
            JS_FreeAtom(cx, key);
            return -1;
        }

        JS_FreeAtom(cx, key);
    }

    ret = ngx_http_qjs_ext_keys_header(cx, &r->headers_out.headers, keys, ptab,
                                       plen);
    JS_FreeValue(cx, keys);

    return ret;
}


static int
ngx_http_qjs_headers_out_handler(JSContext *cx, ngx_http_request_t *r,
    ngx_str_t *name, JSPropertyDescriptor *pdesc, JSValue *value,
    unsigned flags)
{
    u_char           *p;
    int64_t           length;
    uint32_t          i;
    ngx_int_t         rc;
    ngx_str_t         s;
    JSValue           v;
    ngx_list_part_t  *part;
    ngx_table_elt_t  *header, *h, **ph;

    if (flags & NJS_HEADER_GET) {
        return ngx_http_qjs_header_generic(cx, r, &r->headers_out.headers, NULL,
                                           name, pdesc, flags);
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
            || h->key.len != name->len
            || ngx_strncasecmp(h->key.data, name->data, name->len) != 0)
        {
            continue;
        }

        h->hash = 0;
        h->next = NULL;
    }

    if (value == NULL) {
        return 1;
    }

    if (qjs_is_array(cx, *value)) {
        v = JS_GetPropertyStr(cx, *value, "length");
        if (JS_IsException(v)) {
            return -1;
        }

        if (JS_ToInt64(cx, &length, v) < 0) {
            JS_FreeValue(cx, v);
            return -1;
        }

        JS_FreeValue(cx, v);

    } else {
        v = *value;
        length = 1;
    }

    ph = &header;

    for (i = 0; i < (uint32_t) length; i++) {
        if (qjs_is_array(cx, *value)) {
            v = JS_GetPropertyUint32(cx, *value, i);
            if (JS_IsException(v)) {
                return -1;
            }
        }

        rc = ngx_qjs_string(cx, r->pool, v, &s);

        if (qjs_is_array(cx, *value)) {
            JS_FreeValue(cx, v);
        }

        if (rc != NGX_OK) {
            return -1;
        }

        if (s.len == 0) {
            continue;
        }

        h = ngx_list_push(&r->headers_out.headers);
        if (h == NULL) {
            (void) JS_ThrowOutOfMemory(cx);
            return -1;
        }

        p = ngx_pnalloc(r->pool, name->len);
        if (p == NULL) {
            h->hash = 0;
            (void) JS_ThrowOutOfMemory(cx);
            return -1;
        }

        ngx_memcpy(p, name->data, name->len);

        h->key.data = p;
        h->key.len = name->len;

        h->value.data = s.data;
        h->value.len = s.len;
        h->hash = 1;

        *ph = h;
        ph = &h->next;
    }

    *ph = NULL;

    return NJS_OK;
}


static int
ngx_http_qjs_headers_out_special_handler(JSContext *cx, ngx_http_request_t *r,
    ngx_str_t *name, JSPropertyDescriptor *pdesc, JSValue *value,
    unsigned flags, ngx_table_elt_t **hh)
{
    u_char           *p;
    uint32_t          length;
    JSValue           len, setval;
    ngx_str_t         s;
    ngx_uint_t        i, rc;
    ngx_list_t       *headers;
    ngx_list_part_t  *part;
    ngx_table_elt_t  *header, *h;

    if (flags & NJS_HEADER_GET) {
        return ngx_http_qjs_headers_out_handler(cx, r, name, pdesc, NULL,
                                                flags | NJS_HEADER_SINGLE);
    }

    if (value != NULL) {
        if (qjs_is_array(cx, *value)) {
            len = JS_GetPropertyStr(cx, *value, "length");
            if (JS_IsException(len)) {
                return -1;
            }

            if (JS_ToUint32(cx, &length, len) < 0) {
                JS_FreeValue(cx, len);
                return -1;
            }

            JS_FreeValue(cx, len);

            setval = JS_GetPropertyUint32(cx, *value, length - 1);
            if (JS_IsException(setval)) {
                return -1;
            }

        } else {
            setval = *value;
        }

    } else {
        setval = JS_UNDEFINED;
    }

    rc = ngx_qjs_string(cx, r->pool, setval, &s);

    if (value != NULL && qjs_is_array(cx, *value)) {
        JS_FreeValue(cx, setval);
    }

    if (rc != NGX_OK) {
        return -1;
    }

    headers = &r->headers_out.headers;
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

        if (h->key.len == name->len
            && ngx_strncasecmp(h->key.data, name->data, name->len) == 0)
        {
            goto done;
        }
    }

    h = NULL;

done:

    if (h != NULL && s.len == 0) {
        h->hash = 0;
        h = NULL;
    }

    if (h == NULL && s.len != 0) {
        h = ngx_list_push(headers);
        if (h == NULL) {
            (void) JS_ThrowOutOfMemory(cx);
            return -1;
        }

        p = ngx_pnalloc(r->pool, name->len);
        if (p == NULL) {
            h->hash = 0;
            (void) JS_ThrowOutOfMemory(cx);
            return -1;
        }

        ngx_memcpy(p, name->data, name->len);

        h->key.data = p;
        h->key.len = name->len;
    }

    if (h != NULL) {
        h->value.data = s.data;
        h->value.len = s.len;
        h->hash = 1;
    }

    if (hh != NULL) {
        *hh = h;
    }

    return 1;
}


static int
ngx_http_qjs_headers_out_content_encoding(JSContext *cx, ngx_http_request_t *r,
    ngx_str_t *name, JSPropertyDescriptor *pdesc, JSValue *value,
    unsigned flags)
{
    int               ret;
    ngx_table_elt_t  *h;

    ret = ngx_http_qjs_headers_out_special_handler(cx, r, name, pdesc, value,
                                                   flags, &h);
    if (ret < 0) {
        return -1;
    }

    if (!(flags & NJS_HEADER_GET)) {
        r->headers_out.content_encoding = h;
    }

    return ret;
}


static int
ngx_http_qjs_headers_out_content_length(JSContext *cx, ngx_http_request_t *r,
    ngx_str_t *name, JSPropertyDescriptor *pdesc, JSValue *value,
    unsigned flags)
{
    int               ret;
    u_char           *p;
    ngx_int_t         n;
    ngx_table_elt_t  *h;
    u_char            content_len[NGX_OFF_T_LEN];

    if (flags & NJS_HEADER_GET) {
        if (r->headers_out.content_length == NULL
            && r->headers_out.content_length_n >= 0)
        {
            p = ngx_sprintf(content_len, "%O", r->headers_out.content_length_n);

            if (pdesc != NULL) {
                pdesc->flags = JS_PROP_C_W_E;
                pdesc->getter = JS_UNDEFINED;
                pdesc->setter = JS_UNDEFINED;
                pdesc->value = qjs_string_create(cx, content_len,
                                                p - content_len);
                if (JS_IsException(pdesc->value)) {
                    return -1;
                }
            }

            return 1;
        }
    }

    ret = ngx_http_qjs_headers_out_special_handler(cx, r, name, pdesc, value,
                                                   flags, &h);
    if (ret < 0) {
        return -1;
    }

    if (!(flags & NJS_HEADER_GET)) {
        if (h != NULL) {
            n = ngx_atoi(h->value.data, h->value.len);
            if (n == NGX_ERROR) {
                h->hash = 0;
                (void) JS_ThrowInternalError(cx, "failed converting argument "
                                             "to positive integer");
                return -1;
            }

            r->headers_out.content_length = h;
            r->headers_out.content_length_n = n;

        } else {
            ngx_http_clear_content_length(r);
        }
    }

    return ret;
}


static int
ngx_http_qjs_headers_out_content_type(JSContext *cx, ngx_http_request_t *r,
    ngx_str_t *name, JSPropertyDescriptor *pdesc, JSValue *value,
    unsigned flags)
{
    uint32_t     length;
    JSValue     len, setval;
    ngx_int_t   rc;
    ngx_str_t  *hdr, s;

    if (flags & NJS_HEADER_GET) {
        hdr = &r->headers_out.content_type;

        if (pdesc != NULL) {
            pdesc->flags = JS_PROP_C_W_E;
            pdesc->getter = JS_UNDEFINED;
            pdesc->setter = JS_UNDEFINED;

            if (hdr->len == 0) {
                pdesc->value = JS_UNDEFINED;
                return 1;
            }

            pdesc->value = qjs_string_create(cx, hdr->data, hdr->len);
            if (JS_IsException(pdesc->value)) {
                return -1;
            }
        }

        return 1;
    }

    if (value == NULL) {
        r->headers_out.content_type.len = 0;
        r->headers_out.content_type_len = 0;
        r->headers_out.content_type.data = NULL;
        r->headers_out.content_type_lowcase = NULL;
        return 1;
    }

    if (qjs_is_array(cx, *value)) {
        len = JS_GetPropertyStr(cx, *value, "length");
        if (JS_IsException(len)) {
            return -1;
        }

        if (JS_ToUint32(cx, &length, len) < 0) {
            JS_FreeValue(cx, len);
            return -1;
        }

        JS_FreeValue(cx, len);

        setval = JS_GetPropertyUint32(cx, *value, length - 1);
        if (JS_IsException(setval)) {
            return -1;
        }

    } else {
        setval = *value;
    }

    rc = ngx_qjs_string(cx, r->pool, setval, &s);

    if (qjs_is_array(cx, *value)) {
        JS_FreeValue(cx, setval);
    }

    if (rc != NGX_OK) {
        return -1;
    }

    r->headers_out.content_type.len = s.len;
    r->headers_out.content_type_len = r->headers_out.content_type.len;
    r->headers_out.content_type.data = s.data;
    r->headers_out.content_type_lowcase = NULL;

    return 1;
}


static int
ngx_http_qjs_headers_out_date(JSContext *cx, ngx_http_request_t *r,
    ngx_str_t *name, JSPropertyDescriptor *pdesc, JSValue *value,
    unsigned flags)
{
    int               ret;
    ngx_table_elt_t  *h;

    ret = ngx_http_qjs_headers_out_special_handler(cx, r, name, pdesc, value,
                                                   flags, &h);
    if (ret < 0) {
        return -1;
    }

    if (!(flags & NJS_HEADER_GET)) {
        r->headers_out.date = h;
    }

    return ret;
}


static int
ngx_http_qjs_headers_out_last_modified(JSContext *cx, ngx_http_request_t *r,
    ngx_str_t *name, JSPropertyDescriptor *pdesc, JSValue *value,
    unsigned flags)
{
    int               ret;
    ngx_table_elt_t  *h;

    ret = ngx_http_qjs_headers_out_special_handler(cx, r, name, pdesc, value,
                                                   flags, &h);
    if (ret < 0) {
        return -1;
    }

    if (!(flags & NJS_HEADER_GET)) {
        r->headers_out.last_modified = h;
    }

    return ret;
}


static int
ngx_http_qjs_headers_out_location(JSContext *cx, ngx_http_request_t *r,
    ngx_str_t *name, JSPropertyDescriptor *pdesc, JSValue *value,
    unsigned flags)
{
    int               ret;
    ngx_table_elt_t  *h;

    ret = ngx_http_qjs_headers_out_special_handler(cx, r, name, pdesc, value,
                                                   flags, &h);
    if (ret < 0) {
        return -1;
    }

    if (!(flags & NJS_HEADER_GET)) {
        r->headers_out.location = h;
    }

    return ret;
}


static int
ngx_http_qjs_headers_out_server(JSContext *cx, ngx_http_request_t *r,
    ngx_str_t *name, JSPropertyDescriptor *pdesc, JSValue *value,
    unsigned flags)
{
    int               ret;
    ngx_table_elt_t  *h;

    ret = ngx_http_qjs_headers_out_special_handler(cx, r, name, pdesc, value,
                                                   flags, &h);
    if (ret < 0) {
        return -1;
    }

    if (!(flags & NJS_HEADER_GET)) {
        r->headers_out.server = h;
    }

    return ret;
}


static int
ngx_http_qjs_headers_out(JSContext *cx, ngx_http_request_t *r,
    ngx_str_t *name, JSPropertyDescriptor *pdesc, JSValue *value,
    unsigned flags)
{
    ngx_http_js_header_t  *h;

    static ngx_http_js_header_t headers_out[] = {
#define header(name, fl, h) { njs_str(name), fl, (uintptr_t) h }
        header("Age", NJS_HEADER_SINGLE, ngx_http_qjs_headers_out_handler),
        header("Content-Encoding", 0, ngx_http_qjs_headers_out_content_encoding),
        header("Content-Length", 0, ngx_http_qjs_headers_out_content_length),
        header("Content-Type", 0, ngx_http_qjs_headers_out_content_type),
        header("Date", 0, ngx_http_qjs_headers_out_date),
        header("Etag", NJS_HEADER_SINGLE, ngx_http_qjs_headers_out_handler),
        header("Expires", NJS_HEADER_SINGLE, ngx_http_qjs_headers_out_handler),
        header("Last-Modified", 0, ngx_http_qjs_headers_out_last_modified),
        header("Location", 0, ngx_http_qjs_headers_out_location),
        header("Server", 0, ngx_http_qjs_headers_out_server),
        header("Set-Cookie", NJS_HEADER_ARRAY,
               ngx_http_qjs_headers_out_handler),
        header("Retry-After", NJS_HEADER_SINGLE,
               ngx_http_qjs_headers_out_handler),
        header("", 0, ngx_http_qjs_headers_out_handler),
#undef header
    };

    for (h = headers_out; h->name.len > 0; h++) {
        if (h->name.len == name->len
            && ngx_strncasecmp(h->name.data, name->data, name->len) == 0)
        {
            break;
        }
    }

    return ((njs_http_qjs_header_handler_t) h->handler)(cx,
                                      r, name, pdesc, value, h->flags | flags);
}


static int
ngx_http_qjs_headers_out_own_property(JSContext *cx,
    JSPropertyDescriptor *pdesc, JSValueConst obj, JSAtom prop)
{
    int                   ret;
    ngx_str_t             name;
    ngx_http_request_t   *r;

    r = JS_GetOpaque(obj, NGX_QJS_CLASS_ID_HTTP_HEADERS_OUT);
    if (r == NULL) {
        (void) JS_ThrowInternalError(cx, "\"this\" is not a headers_out"
                                     " object");
        return -1;
    }

    name.data = (u_char *) JS_AtomToCString(cx, prop);
    if (name.data == NULL) {
        return -1;
    }

    name.len = ngx_strlen(name.data);

    ret = ngx_http_qjs_headers_out(cx, r, &name, pdesc, NULL, NJS_HEADER_GET);
    JS_FreeCString(cx, (char *) name.data);

    return ret;
}


static int
ngx_http_qjs_headers_out_set_property(JSContext *cx,
    JSValueConst obj, JSAtom atom, JSValueConst value, JSValueConst receiver,
    int flags)
{
    return ngx_http_qjs_headers_out_define_own_property(cx, obj, atom, value,
                                JS_UNDEFINED, JS_UNDEFINED, flags);
}


static int
ngx_http_qjs_headers_out_define_own_property(JSContext *cx,
    JSValueConst obj, JSAtom prop, JSValueConst value, JSValueConst getter,
    JSValueConst setter, int flags)
{
    int                   ret;
    ngx_str_t             name;
    ngx_http_request_t   *r;

    r = JS_GetOpaque(obj, NGX_QJS_CLASS_ID_HTTP_HEADERS_OUT);
    if (r == NULL) {
        (void) JS_ThrowInternalError(cx, "\"this\" is not a headers_out"
                                     " object");
        return -1;
    }

    if (!JS_IsUndefined(setter) || !JS_IsUndefined(getter)) {
        (void) JS_ThrowTypeError(cx, "cannot define getter or setter");
        return -1;
    }

    name.data = (u_char *) JS_AtomToCString(cx, prop);
    if (name.data == NULL) {
        return -1;
    }

    name.len = ngx_strlen(name.data);

    if (r->header_sent) {
        ngx_log_error(NGX_LOG_WARN, r->connection->log, 0,
                      "ignored setting of response header \"%V\" because"
                      " headers were already sent", &name);
    }

    ret = ngx_http_qjs_headers_out(cx, r, &name, NULL, &value, 0);
    JS_FreeCString(cx, (char *) name.data);

    return ret;
}


static int
ngx_http_qjs_headers_out_delete_property(JSContext *cx,
    JSValueConst obj, JSAtom prop)
{
    int                   ret;
    ngx_str_t             name;
    ngx_http_request_t   *r;

    r = JS_GetOpaque(obj, NGX_QJS_CLASS_ID_HTTP_HEADERS_OUT);
    if (r == NULL) {
        (void) JS_ThrowInternalError(cx, "\"this\" is not a headers_out"
                                     " object");
        return -1;
    }

    name.data = (u_char *) JS_AtomToCString(cx, prop);
    if (name.data == NULL) {
        return -1;
    }

    name.len = ngx_strlen(name.data);

    ret = ngx_http_qjs_headers_out(cx, r, &name, NULL, NULL, 0);
    JS_FreeCString(cx, (char *) name.data);

    return ret;
}


static ngx_int_t
ngx_http_qjs_body_filter(ngx_http_request_t *r, ngx_http_js_loc_conf_t *jlcf,
    ngx_http_js_ctx_t *ctx, ngx_chain_t *in)
{
    size_t             len;
    JSAtom             last_key;
    JSValue            arguments[3], last;
    ngx_int_t          rc;
    njs_int_t          pending;
    ngx_buf_t         *b;
    ngx_chain_t       *cl;
    JSContext         *cx;
    ngx_connection_t  *c;

    c = r->connection;
    cx = ctx->engine->u.qjs.ctx;

    arguments[0] = ngx_qjs_arg(ctx->args[0]);

    last_key = JS_NewAtom(cx, "last");
    if (last_key == JS_ATOM_NULL) {
        return NGX_ERROR;
    }

    while (in != NULL) {
        ctx->buf = in->buf;
        b = ctx->buf;

        if (!ctx->done) {
            len = b->last - b->pos;

            arguments[1] = ngx_qjs_prop(cx, jlcf->buffer_type, b->pos, len);
            if (JS_IsException(arguments[1])) {
                JS_FreeAtom(cx, last_key);
                return NGX_ERROR;
            }

            last = JS_NewBool(cx, b->last_buf);

            arguments[2] = JS_NewObject(cx);
            if (JS_IsException(arguments[2])) {
                JS_FreeAtom(cx, last_key);
                JS_FreeValue(cx, arguments[1]);
                return NGX_ERROR;
            }

            if (JS_SetProperty(cx, arguments[2], last_key, last) < 0) {
                JS_FreeAtom(cx, last_key);
                JS_FreeValue(cx, arguments[1]);
                JS_FreeValue(cx, arguments[2]);
                return NGX_ERROR;
            }

            pending = ngx_js_ctx_pending(ctx);

            ngx_log_debug1(NGX_LOG_DEBUG_HTTP, c->log, 0,
                           "http js body call \"%V\"", &jlcf->body_filter);

            rc = ctx->engine->call((ngx_js_ctx_t *) ctx, &jlcf->body_filter,
                                   (njs_opaque_value_t *) &arguments[0], 3);

            JS_FreeValue(cx, arguments[1]);
            JS_FreeValue(cx, arguments[2]);

            if (rc == NGX_ERROR) {
                JS_FreeAtom(cx, last_key);
                return NGX_ERROR;
            }

            if (!pending && rc == NGX_AGAIN) {
                ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                              "async operation inside \"%V\" body filter",
                              &jlcf->body_filter);
                JS_FreeAtom(cx, last_key);
                return NGX_ERROR;
            }

            ctx->buf->pos = ctx->buf->last;

        } else {
            cl = ngx_alloc_chain_link(c->pool);
            if (cl == NULL) {
                JS_FreeAtom(cx, last_key);
                return NGX_ERROR;
            }

            cl->buf = b;

            *ctx->last_out = cl;
            ctx->last_out = &cl->next;
        }

        in = in->next;
    }

    JS_FreeAtom(cx, last_key);

    return NGX_OK;
}


static ngx_http_request_t *
ngx_http_qjs_request(JSValueConst val)
{
    ngx_http_qjs_request_t  *req;

    req = JS_GetOpaque(val, NGX_QJS_CLASS_ID_HTTP_REQUEST);
    if (req == NULL) {
        return NULL;
    }

    return req->request;
}


static JSValue
ngx_http_qjs_request_make(JSContext *cx, ngx_int_t proto_id,
    ngx_http_request_t *r)
{
    JSValue                  request;
    ngx_http_qjs_request_t  *req;

    request = JS_NewObjectClass(cx, proto_id);
    if (JS_IsException(request)) {
        return JS_EXCEPTION;
    }

    req = js_malloc(cx, sizeof(ngx_http_qjs_request_t));
    if (req == NULL) {
        return JS_ThrowOutOfMemory(cx);
    }

    req->request = r;
    req->args = JS_UNDEFINED;
    req->request_body = JS_UNDEFINED;
    req->response_body = JS_UNDEFINED;

    JS_SetOpaque(request, req);

    return request;
}


static void
ngx_http_qjs_request_finalizer(JSRuntime *rt, JSValue val)
{
    ngx_http_qjs_request_t  *req;

    req = JS_GetOpaque(val, NGX_QJS_CLASS_ID_HTTP_REQUEST);
    if (req == NULL) {
        return;
    }

    JS_FreeValueRT(rt, req->args);
    JS_FreeValueRT(rt, req->request_body);
    JS_FreeValueRT(rt, req->response_body);

    js_free_rt(rt, req);
}


static void
ngx_http_qjs_periodic_finalizer(JSRuntime *rt, JSValue val)
{
    ngx_http_qjs_request_t  *req;

    req = JS_GetOpaque(val, NGX_QJS_CLASS_ID_HTTP_PERIODIC);
    if (req == NULL) {
        return;
    }

    js_free_rt(rt, req);
}


static ngx_engine_t *
ngx_engine_qjs_clone(ngx_js_ctx_t *ctx, ngx_js_loc_conf_t *cf,
    njs_int_t proto_id, void *external)
{
    JSValue             proto;
    JSContext          *cx;
    ngx_engine_t       *engine;
    ngx_http_js_ctx_t  *hctx;

    engine = ngx_qjs_clone(ctx, cf, external);
    if (engine == NULL) {
        return NULL;
    }

    cx = engine->u.qjs.ctx;

    if (!JS_IsRegisteredClass(JS_GetRuntime(cx),
                              NGX_QJS_CLASS_ID_HTTP_REQUEST))
    {
        if (JS_NewClass(JS_GetRuntime(cx), NGX_QJS_CLASS_ID_HTTP_REQUEST,
                        &ngx_http_qjs_request_class) < 0)
        {
            return NULL;
        }

        proto = JS_NewObject(cx);
        if (JS_IsException(proto)) {
            return NULL;
        }

        JS_SetPropertyFunctionList(cx, proto, ngx_http_qjs_ext_request,
                                   njs_nitems(ngx_http_qjs_ext_request));

        JS_SetClassProto(cx, NGX_QJS_CLASS_ID_HTTP_REQUEST, proto);

        if (JS_NewClass(JS_GetRuntime(cx), NGX_QJS_CLASS_ID_HTTP_PERIODIC,
                        &ngx_http_qjs_periodic_class) < 0)
        {
            return NULL;
        }

        proto = JS_NewObject(cx);
        if (JS_IsException(proto)) {
            return NULL;
        }

        JS_SetPropertyFunctionList(cx, proto, ngx_http_qjs_ext_periodic,
                                   njs_nitems(ngx_http_qjs_ext_periodic));

        JS_SetClassProto(cx, NGX_QJS_CLASS_ID_HTTP_PERIODIC, proto);

        if (JS_NewClass(JS_GetRuntime(cx), NGX_QJS_CLASS_ID_HTTP_VARS,
                        &ngx_http_qjs_variables_class) < 0)
        {
            return NULL;
        }

        if (JS_NewClass(JS_GetRuntime(cx), NGX_QJS_CLASS_ID_HTTP_HEADERS_IN,
                        &ngx_http_qjs_headers_in_class) < 0)
        {
            return NULL;
        }

        if (JS_NewClass(JS_GetRuntime(cx), NGX_QJS_CLASS_ID_HTTP_HEADERS_OUT,
                        &ngx_http_qjs_headers_out_class) < 0)
        {
            return NULL;
        }
    }

    hctx = (ngx_http_js_ctx_t *) ctx;
    hctx->body_filter = ngx_http_qjs_body_filter;

    if (proto_id == ngx_http_js_request_proto_id) {
        proto_id = NGX_QJS_CLASS_ID_HTTP_REQUEST;

    } else if (proto_id == ngx_http_js_periodic_session_proto_id) {
        proto_id = NGX_QJS_CLASS_ID_HTTP_PERIODIC;
    }

    ngx_qjs_arg(hctx->args[0]) = ngx_http_qjs_request_make(cx, proto_id,
                                                           external);
    if (JS_IsException(ngx_qjs_arg(hctx->args[0]))) {
        return NULL;
    }

    return engine;
}

#endif


static ngx_int_t
ngx_http_js_init_conf_vm(ngx_conf_t *cf, ngx_js_loc_conf_t *conf)
{
    ngx_engine_opts_t    options;
    ngx_js_main_conf_t  *jmcf;

    memset(&options, 0, sizeof(ngx_engine_opts_t));

    options.engine = conf->type;

    jmcf = ngx_http_conf_get_module_main_conf(cf, ngx_http_js_module);
    ngx_http_js_uptr[NGX_JS_EXTERNAL_MAIN_CONF] = (uintptr_t) jmcf;

    if (conf->type == NGX_ENGINE_NJS) {
        options.u.njs.metas = &ngx_http_js_metas;
        options.u.njs.addons = njs_http_js_addon_modules;
        options.clone = ngx_engine_njs_clone;
    }

#if (NJS_HAVE_QUICKJS)
    else if (conf->type == NGX_ENGINE_QJS) {
        options.u.qjs.metas = ngx_http_js_uptr;
        options.u.qjs.addons = njs_http_qjs_addon_modules;
        options.clone = ngx_engine_qjs_clone;

        options.core_conf = (ngx_js_core_conf_t *)
                    ngx_get_conf(cf->cycle->conf_ctx, ngx_http_js_core_module);
    }
#endif

    return ngx_js_init_conf_vm(cf, conf, &options);
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


static ngx_int_t
ngx_http_js_init_worker_periodics(ngx_js_main_conf_t *jmcf)
{
    ngx_uint_t           i;
    ngx_js_periodic_t   *periodics;

    if (jmcf->periodics == NULL) {
        return NGX_OK;
    }

    periodics = jmcf->periodics->elts;

    for (i = 0; i < jmcf->periodics->nelts; i++) {
        if (periodics[i].worker_affinity != NULL
            && !periodics[i].worker_affinity[ngx_worker])
        {
            continue;
        }

        if (periodics[i].worker_affinity == NULL && ngx_worker != 0) {
            continue;
        }

        periodics[i].fd = 1000000 + i;

        if (ngx_http_js_periodic_init(&periodics[i]) != NGX_OK) {
            return NGX_ERROR;
        }
    }

    return NGX_OK;
}


static ngx_int_t
ngx_http_js_init_worker(ngx_cycle_t *cycle)
{
    ngx_js_main_conf_t  *jmcf;

    if ((ngx_process != NGX_PROCESS_WORKER)
        && ngx_process != NGX_PROCESS_SINGLE)
    {
        return NGX_OK;
    }

    jmcf = ngx_http_cycle_get_module_main_conf(cycle, ngx_http_js_module);

    if (jmcf == NULL) {
        return NGX_OK;
    }

    if (ngx_http_js_init_worker_periodics(jmcf) != NGX_OK) {
        return NGX_ERROR;
    }

    if (ngx_js_dict_init_worker(jmcf) != NGX_OK) {
        return NGX_ERROR;
    }

    return NGX_OK;
}


static char *
ngx_http_js_periodic(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    uint8_t             *mask;
    ngx_str_t           *value, s;
    ngx_msec_t           interval, jitter;
    ngx_uint_t           i;
    ngx_core_conf_t     *ccf;
    ngx_js_periodic_t   *periodic;
    ngx_js_main_conf_t  *jmcf;

    if (cf->args->nelts < 2) {
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0, "method name is required");
        return NGX_CONF_ERROR;
    }

    jmcf = ngx_http_conf_get_module_main_conf(cf, ngx_http_js_module);

    if (jmcf->periodics == NULL) {
        jmcf->periodics = ngx_array_create(cf->pool, 1,
                                           sizeof(ngx_js_periodic_t));
        if (jmcf->periodics == NULL) {
            return NGX_CONF_ERROR;
        }
    }

    periodic = ngx_array_push(jmcf->periodics);
    if (periodic == NULL) {
        return NGX_CONF_ERROR;
    }

    ngx_memzero(periodic, sizeof(ngx_js_periodic_t));

    mask = NULL;
    jitter = 0;
    interval = 5000;

    value = cf->args->elts;

    for (i = 2; i < cf->args->nelts; i++) {

        if (ngx_strncmp(value[i].data, "interval=", 9) == 0) {
            s.len = value[i].len - 9;
            s.data = value[i].data + 9;

            interval = ngx_parse_time(&s, 0);

            if (interval == (ngx_msec_t) NGX_ERROR || interval == 0) {
                goto invalid;
            }

            continue;
        }

        if (ngx_strncmp(value[i].data, "jitter=", 7) == 0) {
            s.len = value[i].len - 7;
            s.data = value[i].data + 7;

            jitter = ngx_parse_time(&s, 0);

            if (jitter == (ngx_msec_t) NGX_ERROR) {
                goto invalid;
            }

            continue;
        }

        if (ngx_strncmp(value[i].data, "worker_affinity=", 16) == 0) {
            s.len = value[i].len - 16;
            s.data = value[i].data + 16;

            ccf = (ngx_core_conf_t *) ngx_get_conf(cf->cycle->conf_ctx,
                                                   ngx_core_module);

            if (ccf->worker_processes == NGX_CONF_UNSET) {
                ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                                   "\"worker_affinity\" is not supported "
                                   "with unset \"worker_processes\" directive");
                return NGX_CONF_ERROR;
            }

            mask = ngx_palloc(cf->pool, ccf->worker_processes);
            if (mask == NULL) {
                return NGX_CONF_ERROR;
            }

            if (ngx_strncmp(s.data, "all", 3) == 0) {
                memset(mask, 1, ccf->worker_processes);
                continue;
            }

            if ((size_t) ccf->worker_processes != s.len) {
                ngx_conf_log_error(NGX_LOG_EMERG, cf, 0, "the number of "
                                   "\"worker_processes\" is not equal to the "
                                   "size of \"worker_affinity\" mask");
                return NGX_CONF_ERROR;
            }

            for (i = 0; i < s.len; i++) {
                if (s.data[i] == '0') {
                    mask[i] = 0;
                    continue;
                }

                if (s.data[i] == '1') {
                    mask[i] = 1;
                    continue;
                }

                ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                          "invalid character \"%c\" in \"worker_affinity=\"",
                          s.data[i]);

                return NGX_CONF_ERROR;
            }

            continue;
        }

invalid:

        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                           "invalid parameter \"%V\"", &value[i]);
        return NGX_CONF_ERROR;
    }

    periodic->method = value[1];
    periodic->interval = interval;
    periodic->jitter = jitter;
    periodic->worker_affinity = mask;
    periodic->conf_ctx = cf->ctx;

    return NGX_CONF_OK;
}


static char *
ngx_http_js_set(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    ngx_str_t            *value;
    ngx_js_set_t         *data, *prev;
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

    data = ngx_palloc(cf->pool, sizeof(ngx_js_set_t));
    if (data == NULL) {
        return NGX_CONF_ERROR;
    }

    data->fname = value[2];
    data->flags = 0;
    data->file_name = cf->conf_file->file.name.data;
    data->line = cf->conf_file->line;

    if (v->get_handler == ngx_http_js_variable_set) {
        prev = (ngx_js_set_t *) v->data;

        if (data->fname.len != prev->fname.len
            || ngx_strncmp(data->fname.data, prev->fname.data, data->fname.len) != 0)
        {
            ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                               "variable \"%V\" is redeclared with "
                               "different function name", &value[1]);
            return NGX_CONF_ERROR;
        }
    }

    if (cf->args->nelts == 4) {
        if (ngx_strcmp(value[3].data, "nocache") == 0) {
            data->flags |= NGX_NJS_VAR_NOCACHE;

        } else {
            ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                "unrecognized flag \"%V\"", &value[3]);
            return NGX_CONF_ERROR;
        }
    }

    v->get_handler = ngx_http_js_variable_set;
    v->data = (uintptr_t) data;

    return NGX_CONF_OK;
}


static ngx_int_t
ngx_http_js_eval_proxy_url(ngx_pool_t *pool, void *request,
    void *module_conf, ngx_url_t **url_out, ngx_str_t *auth_out)
{
    ngx_str_t                value;
    ngx_http_request_t      *r;
    ngx_http_js_loc_conf_t  *jlcf;

    r = request;
    jlcf = module_conf;

    if (ngx_http_complex_value(r, &jlcf->fetch_proxy_cv, &value) != NGX_OK) {
        return NGX_ERROR;
    }

    return ngx_js_parse_proxy_url(pool, r->connection->log, &value,
                                  url_out, auth_out);
}


static char *
ngx_http_js_fetch_proxy(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    ngx_str_t                        *value;
    ngx_uint_t                        n;
    ngx_http_js_loc_conf_t           *jlcf;
    ngx_http_compile_complex_value_t  ccv;

    value = cf->args->elts;

    n = ngx_http_script_variables_count(&value[1]);

    if (n) {
        ngx_memzero(&ccv, sizeof(ngx_http_compile_complex_value_t));

        jlcf = conf;

        ccv.cf = cf;
        ccv.value = &value[1];
        ccv.complex_value = &jlcf->fetch_proxy_cv;

        if (ngx_http_compile_complex_value(&ccv) != NGX_OK) {
            return NGX_CONF_ERROR;
        }

        jlcf->eval_proxy_url = ngx_http_js_eval_proxy_url;

        return NGX_CONF_OK;
    }

    return ngx_js_fetch_proxy(cf, cmd, conf);
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
ngx_http_js_shared_dict_zone(ngx_conf_t *cf, ngx_command_t *cmd,
    void *conf)
{
    return ngx_js_shared_dict_zone(cf, cmd, conf, &ngx_http_js_module);
}


static char *
ngx_http_js_shared_array_zone(ngx_conf_t *cf, ngx_command_t *cmd,
    void *conf)
{
    return ngx_js_shared_array_zone(cf, cmd, conf, &ngx_http_js_module);
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

    if (cf->args->nelts == 3) {
        if (ngx_strncmp(value[2].data, "buffer_type=", 12) == 0) {
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
        } else {
            ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                               "invalid parameter \"%V\"", &value[2]);
            return NGX_CONF_ERROR;
        }
    }

    return NGX_CONF_OK;
}


static void *
ngx_http_js_create_main_conf(ngx_conf_t *cf)
{
    ngx_js_main_conf_t  *jmcf;

    jmcf = ngx_pcalloc(cf->pool, sizeof(ngx_js_main_conf_t));
    if (jmcf == NULL) {
        return NULL;
    }

    /*
     * set by ngx_pcalloc():
     *
     *     jmcf->dicts = NULL;
     *     jmcf->periodics = NULL;
     */

    return jmcf;
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

#if (NGX_SSL)
    conf->ssl_verify = NGX_CONF_UNSET;
    conf->ssl_verify_depth = NGX_CONF_UNSET;
#endif
    conf->buffer_type = NGX_CONF_UNSET_UINT;

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

    if (ngx_js_merge_conf(cf, parent, child, ngx_http_js_init_conf_vm)
        != NGX_CONF_OK)
    {
        return NGX_CONF_ERROR;
    }

    if (conf->content.len != 0) {
        if (conf->imports == NGX_CONF_UNSET_PTR) {
            ngx_log_error(NGX_LOG_EMERG, cf->log, 0,
                          "no imports defined for \"js_content\" \"%V\", "
                          "use \"js_import\" directive", &conf->content);
            return NGX_CONF_ERROR;
        }
    }

    if (conf->header_filter.len != 0) {
        if (conf->imports == NGX_CONF_UNSET_PTR) {
            ngx_log_error(NGX_LOG_EMERG, cf->log, 0,
                          "no imports defined for \"js_header_filter\" \"%V\", "
                          "use \"js_import\" directive", &conf->header_filter);
            return NGX_CONF_ERROR;
        }
    }

    if (conf->body_filter.len != 0) {
        if (conf->imports == NGX_CONF_UNSET_PTR) {
            ngx_log_error(NGX_LOG_EMERG, cf->log, 0,
                          "no imports defined for \"js_body_filter\" \"%V\", "
                          "use \"js_import\" directive", &conf->body_filter);
            return NGX_CONF_ERROR;
        }
    }

    return NGX_CONF_OK;
}


static ngx_int_t
ngx_http_js_parse_unsafe_uri(ngx_http_request_t *r, njs_str_t *uri,
    njs_str_t *args)
{
    ngx_str_t   uri_arg, args_arg;
    ngx_uint_t  flags;

    flags = NGX_HTTP_LOG_UNSAFE;

    uri_arg.data = uri->start;
    uri_arg.len = uri->length;

    args_arg.data = args->start;
    args_arg.len = args->length;

    if (ngx_http_parse_unsafe_uri(r, &uri_arg, &args_arg, &flags) != NGX_OK) {
        return NGX_ERROR;
    }

    uri->start = uri_arg.data;
    uri->length = uri_arg.len;

    args->start = args_arg.data;
    args->length = args_arg.len;

    return NGX_OK;
}
