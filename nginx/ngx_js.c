
/*
 * Copyright (C) Roman Arutyunyan
 * Copyright (C) Dmitry Volyntsev
 * Copyright (C) NGINX, Inc.
 */


#include <ngx_config.h>
#include <ngx_core.h>
#include "ngx_js.h"


typedef struct {
    ngx_queue_t          labels;
} ngx_js_console_t;


typedef struct {
    njs_str_t            name;
    uint64_t             time;
    ngx_queue_t          queue;
} ngx_js_timelabel_t;


typedef struct {
    void                *promise;
    njs_opaque_value_t   message;
} ngx_js_rejected_promise_t;


#if defined(PATH_MAX)
#define NGX_MAX_PATH             PATH_MAX
#else
#define NGX_MAX_PATH             4096
#endif

typedef struct {
    int                 fd;
    njs_str_t           name;
    njs_str_t           file;
    char                path[NGX_MAX_PATH + 1];
} njs_module_info_t;


static njs_int_t ngx_js_ext_build(njs_vm_t *vm, njs_object_prop_t *prop,
    njs_value_t *value, njs_value_t *setval, njs_value_t *retval);
static njs_int_t ngx_js_ext_conf_file_path(njs_vm_t *vm,
    njs_object_prop_t *prop, njs_value_t *value, njs_value_t *setval,
    njs_value_t *retval);
static njs_int_t ngx_js_ext_conf_prefix(njs_vm_t *vm, njs_object_prop_t *prop,
    njs_value_t *value, njs_value_t *setval, njs_value_t *retval);
static njs_int_t ngx_js_ext_error_log_path(njs_vm_t *vm,
    njs_object_prop_t *prop, njs_value_t *value, njs_value_t *setval,
    njs_value_t *retval);
static njs_int_t ngx_js_ext_prefix(njs_vm_t *vm, njs_object_prop_t *prop,
    njs_value_t *value, njs_value_t *setval, njs_value_t *retval);
static njs_int_t ngx_js_ext_version(njs_vm_t *vm, njs_object_prop_t *prop,
    njs_value_t *value, njs_value_t *setval, njs_value_t *retval);
static njs_int_t ngx_js_ext_worker_id(njs_vm_t *vm, njs_object_prop_t *prop,
    njs_value_t *value, njs_value_t *setval, njs_value_t *retval);
static njs_int_t ngx_js_ext_console_time(njs_vm_t *vm, njs_value_t *args,
    njs_uint_t nargs, njs_index_t unused, njs_value_t *retval);
static njs_int_t ngx_js_ext_console_time_end(njs_vm_t *vm, njs_value_t *args,
    njs_uint_t nargs, njs_index_t unused, njs_value_t *retval);
static njs_int_t njs_set_timeout(njs_vm_t *vm, njs_value_t *args,
    njs_uint_t nargs, njs_index_t unused, njs_value_t *retval);
static njs_int_t njs_set_immediate(njs_vm_t *vm, njs_value_t *args,
    njs_uint_t nargs, njs_index_t unused, njs_value_t *retval);
static njs_int_t njs_clear_timeout(njs_vm_t *vm, njs_value_t *args,
    njs_uint_t nargs, njs_index_t unused, njs_value_t *retval);
static njs_int_t ngx_js_unhandled_rejection(ngx_js_ctx_t *ctx);
static void ngx_js_cleanup_vm(void *data);

static njs_int_t ngx_js_core_init(njs_vm_t *vm);
static uint64_t ngx_js_monotonic_time(void);


static njs_external_t  ngx_js_ext_global_shared[] = {

    {
        .flags = NJS_EXTERN_PROPERTY | NJS_EXTERN_SYMBOL,
        .name.symbol = NJS_SYMBOL_TO_STRING_TAG,
        .u.property = {
            .value = "GlobalShared",
        }
    },

    {
        .flags = NJS_EXTERN_SELF,
        .u.object = {
            .enumerable = 1,
            .prop_handler = njs_js_ext_global_shared_prop,
            .keys = njs_js_ext_global_shared_keys,
        }
    },

};


static njs_external_t  ngx_js_ext_core[] = {

    {
        .flags = NJS_EXTERN_PROPERTY,
        .name.string = njs_str("build"),
        .enumerable = 1,
        .u.property = {
            .handler = ngx_js_ext_build,
        }
    },

    {
        .flags = NJS_EXTERN_PROPERTY,
        .name.string = njs_str("conf_file_path"),
        .enumerable = 1,
        .u.property = {
            .handler = ngx_js_ext_conf_file_path,
        }
    },

    {
        .flags = NJS_EXTERN_PROPERTY,
        .name.string = njs_str("conf_prefix"),
        .enumerable = 1,
        .u.property = {
            .handler = ngx_js_ext_conf_prefix,
        }
    },

    {
        .flags = NJS_EXTERN_PROPERTY,
        .name.string = njs_str("ERR"),
        .u.property = {
            .handler = ngx_js_ext_constant,
            .magic32 = NGX_LOG_ERR,
            .magic16 = NGX_JS_NUMBER,
        }
    },

    {
        .flags = NJS_EXTERN_PROPERTY,
        .name.string = njs_str("error_log_path"),
        .enumerable = 1,
        .u.property = {
            .handler = ngx_js_ext_error_log_path,
        }
    },

    {
        .flags = NJS_EXTERN_METHOD,
        .name.string = njs_str("fetch"),
        .writable = 1,
        .configurable = 1,
        .enumerable = 1,
        .u.method = {
            .native = ngx_js_ext_fetch,
        }
    },

    {
        .flags = NJS_EXTERN_PROPERTY,
        .name.string = njs_str("INFO"),
        .u.property = {
            .handler = ngx_js_ext_constant,
            .magic32 = NGX_LOG_INFO,
            .magic16 = NGX_JS_NUMBER,
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
        }
    },

    {
        .flags = NJS_EXTERN_OBJECT,
        .name.string = njs_str("shared"),
        .enumerable = 1,
        .writable = 1,
        .u.object = {
            .enumerable = 1,
            .properties = ngx_js_ext_global_shared,
            .nproperties = njs_nitems(ngx_js_ext_global_shared),
        }
    },

    {
        .flags = NJS_EXTERN_PROPERTY,
        .name.string = njs_str("prefix"),
        .enumerable = 1,
        .u.property = {
            .handler = ngx_js_ext_prefix,
        }
    },

    {
        .flags = NJS_EXTERN_PROPERTY,
        .name.string = njs_str("version"),
        .enumerable = 1,
        .u.property = {
            .handler = ngx_js_ext_version,
        }
    },

    {
        .flags = NJS_EXTERN_PROPERTY,
        .name.string = njs_str("version_number"),
        .enumerable = 1,
        .u.property = {
            .handler = ngx_js_ext_constant,
            .magic32 = nginx_version,
            .magic16 = NGX_JS_NUMBER,
        }
    },

    {
        .flags = NJS_EXTERN_PROPERTY,
        .name.string = njs_str("WARN"),
        .u.property = {
            .handler = ngx_js_ext_constant,
            .magic32 = NGX_LOG_WARN,
            .magic16 = NGX_JS_NUMBER,
        }
    },

    {
        .flags = NJS_EXTERN_PROPERTY,
        .name.string = njs_str("worker_id"),
        .enumerable = 1,
        .u.property = {
            .handler = ngx_js_ext_worker_id,
        }
    },

};


static njs_external_t  ngx_js_ext_console[] = {

    {
        .flags = NJS_EXTERN_PROPERTY | NJS_EXTERN_SYMBOL,
        .name.symbol = NJS_SYMBOL_TO_STRING_TAG,
        .u.property = {
            .value = "Console",
        }
    },

    {
        .flags = NJS_EXTERN_METHOD,
        .name.string = njs_str("dump"),
        .writable = 1,
        .configurable = 1,
        .enumerable = 1,
        .u.method = {
            .native = ngx_js_ext_log,
#define NGX_JS_LOG_DUMP  16
#define NGX_JS_LOG_MASK  15
            .magic8 = NGX_LOG_INFO | NGX_JS_LOG_DUMP,
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
        .name.string = njs_str("info"),
        .writable = 1,
        .configurable = 1,
        .enumerable = 1,
        .u.method = {
            .native = ngx_js_ext_log,
            .magic8 = NGX_LOG_INFO,
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
        .flags = NJS_EXTERN_METHOD,
        .name.string = njs_str("time"),
        .writable = 1,
        .configurable = 1,
        .enumerable = 1,
        .u.method = {
            .native = ngx_js_ext_console_time,
        }
    },

    {
        .flags = NJS_EXTERN_METHOD,
        .name.string = njs_str("timeEnd"),
        .writable = 1,
        .configurable = 1,
        .enumerable = 1,
        .u.method = {
            .native = ngx_js_ext_console_time_end,
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


njs_module_t  ngx_js_ngx_module = {
    .name = njs_str("ngx"),
    .preinit = NULL,
    .init = ngx_js_core_init,
};


njs_module_t *njs_js_addon_modules_shared[] = {
    &ngx_js_ngx_module,
    NULL,
};


static njs_int_t      ngx_js_console_proto_id;


ngx_int_t
ngx_js_call(njs_vm_t *vm, njs_function_t *func, njs_value_t *args,
    njs_uint_t nargs)
{
    njs_int_t          ret;
    ngx_str_t          exception;
    ngx_connection_t  *c;

    ret = njs_vm_call(vm, func, args, nargs);
    if (ret == NJS_ERROR) {
        ngx_js_exception(vm, &exception);

        c = ngx_external_connection(vm, njs_vm_external_ptr(vm));

        ngx_log_error(NGX_LOG_ERR, c->log, 0,
                      "js exception: %V", &exception);
        return NGX_ERROR;
    }

    for ( ;; ) {
        ret = njs_vm_execute_pending_job(vm);
        if (ret <= NJS_OK) {
            c = ngx_external_connection(vm, njs_vm_external_ptr(vm));

            if (ret == NJS_ERROR) {
                ngx_js_exception(vm, &exception);

                ngx_log_error(NGX_LOG_ERR, c->log, 0,
                              "js job exception: %V", &exception);
                return NGX_ERROR;
            }

            break;
        }
    }

    return NGX_OK;
}


ngx_int_t
ngx_js_name_call(njs_vm_t *vm, ngx_str_t *fname, ngx_log_t *log,
    njs_opaque_value_t *args, njs_uint_t nargs)
{
    njs_opaque_value_t  unused;

    return ngx_js_name_invoke(vm, fname, log, args, nargs, &unused);
}


ngx_int_t
ngx_js_name_invoke(njs_vm_t *vm, ngx_str_t *fname, ngx_log_t *log,
    njs_opaque_value_t *args, njs_uint_t nargs, njs_opaque_value_t *retval)
{
    njs_int_t        ret;
    njs_str_t        name;
    ngx_str_t        exception;
    ngx_js_ctx_t    *ctx;
    njs_function_t  *func;

    name.start = fname->data;
    name.length = fname->len;

    func = njs_vm_function(vm, &name);
    if (func == NULL) {
        ngx_log_error(NGX_LOG_ERR, log, 0,
                      "js function \"%V\" not found", fname);
        return NGX_ERROR;
    }

    ret = njs_vm_invoke(vm, func, njs_value_arg(args), nargs,
                        njs_value_arg(retval));
    if (ret == NJS_ERROR) {
        ngx_js_exception(vm, &exception);

        ngx_log_error(NGX_LOG_ERR, log, 0,
                      "js exception: %V", &exception);

        return NGX_ERROR;
    }

    for ( ;; ) {
        ret = njs_vm_execute_pending_job(vm);
        if (ret <= NJS_OK) {
            if (ret == NJS_ERROR) {
                ngx_js_exception(vm, &exception);

                ngx_log_error(NGX_LOG_ERR, log, 0,
                              "js job exception: %V", &exception);
                return NGX_ERROR;
            }

            break;
        }
    }

    ctx = ngx_external_ctx(vm, njs_vm_external_ptr(vm));

    if (ngx_js_unhandled_rejection(ctx)) {
        ngx_js_exception(vm, &exception);

        ngx_log_error(NGX_LOG_ERR, log, 0, "js exception: %V", &exception);
        return NGX_ERROR;
    }

    return njs_rbtree_is_empty(&ctx->waiting_events) ? NGX_OK : NGX_AGAIN;
}


ngx_int_t
ngx_js_exception(njs_vm_t *vm, ngx_str_t *s)
{
    njs_int_t  ret;
    njs_str_t  str;

    ret = njs_vm_exception_string(vm, &str);
    if (ret != NJS_OK) {
        return NGX_ERROR;
    }

    s->data = str.start;
    s->len = str.length;

    return NGX_OK;
}


ngx_int_t
ngx_js_integer(njs_vm_t *vm, njs_value_t *value, ngx_int_t *n)
{
    if (!njs_value_is_valid_number(value)) {
        njs_vm_error(vm, "is not a number");
        return NGX_ERROR;
    }

    *n = njs_value_number(value);

    return NGX_OK;
}


ngx_int_t
ngx_js_string(njs_vm_t *vm, njs_value_t *value, njs_str_t *str)
{
    if (value != NULL && !njs_value_is_null_or_undefined(value)) {
        if (njs_vm_value_to_bytes(vm, str, value) == NJS_ERROR) {
            return NGX_ERROR;
        }

    } else {
        str->start = NULL;
        str->length = 0;
    }

    return NGX_OK;
}


static njs_int_t
njs_function_bind(njs_vm_t *vm, const njs_str_t *name,
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



static intptr_t
ngx_js_event_rbtree_compare(njs_rbtree_node_t *node1, njs_rbtree_node_t *node2)
{
    ngx_js_event_t  *ev1, *ev2;

    ev1 = (ngx_js_event_t *) ((u_char *) node1
                              - offsetof(ngx_js_event_t, node));
    ev2 = (ngx_js_event_t *) ((u_char *) node2
                              - offsetof(ngx_js_event_t, node));

    if (ev1->fd < ev2->fd) {
        return -1;
    }

    if (ev1->fd > ev2->fd) {
        return 1;
    }

    return 0;
}


void
ngx_js_ctx_init(ngx_js_ctx_t *ctx)
{
    ctx->event_id = 0;
    njs_rbtree_init(&ctx->waiting_events, ngx_js_event_rbtree_compare);
}


void
ngx_js_ctx_destroy(ngx_js_ctx_t *ctx)
{
    ngx_js_event_t     *event;
    njs_rbtree_node_t  *node;

    node = njs_rbtree_min(&ctx->waiting_events);

    while (njs_rbtree_is_there_successor(&ctx->waiting_events, node)) {
        event = (ngx_js_event_t *) ((u_char *) node
                                    - offsetof(ngx_js_event_t, node));

        if (event->destructor != NULL) {
            event->destructor(njs_vm_external_ptr(event->vm), event);
        }

        node = njs_rbtree_node_successor(&ctx->waiting_events, node);
    }

    njs_vm_destroy(ctx->vm);
}


static njs_int_t
ngx_js_core_init(njs_vm_t *vm)
{
    njs_int_t           ret, proto_id;
    njs_str_t           name;
    njs_opaque_value_t  value;

    static const njs_str_t  set_timeout = njs_str("setTimeout");
    static const njs_str_t  set_immediate = njs_str("setImmediate");
    static const njs_str_t  clear_timeout = njs_str("clearTimeout");

    proto_id = njs_vm_external_prototype(vm, ngx_js_ext_core,
                                         njs_nitems(ngx_js_ext_core));
    if (proto_id < 0) {
        return NJS_ERROR;
    }

    ret = njs_vm_external_create(vm, njs_value_arg(&value), proto_id, NULL, 1);
    if (njs_slow_path(ret != NJS_OK)) {
        return NJS_ERROR;
    }

    name.length = 3;
    name.start = (u_char *) "ngx";

    ret = njs_vm_bind(vm, &name, njs_value_arg(&value), 1);
    if (njs_slow_path(ret != NJS_OK)) {
        return NJS_ERROR;
    }

    ngx_js_console_proto_id = njs_vm_external_prototype(vm, ngx_js_ext_console,
                                               njs_nitems(ngx_js_ext_console));
    if (ngx_js_console_proto_id < 0) {
        return NJS_ERROR;
    }

    ret = njs_vm_external_create(vm, njs_value_arg(&value),
                                 ngx_js_console_proto_id, NULL, 1);
    if (njs_slow_path(ret != NJS_OK)) {
        return NJS_ERROR;
    }

    name.length = 7;
    name.start = (u_char *) "console";

    ret = njs_vm_bind(vm, &name, njs_value_arg(&value), 1);
    if (njs_slow_path(ret != NJS_OK)) {
        return NJS_ERROR;
    }

    ret = njs_function_bind(vm, &set_timeout, njs_set_timeout, 1);
    if (ret != NJS_OK) {
        return NJS_ERROR;
    }

    ret = njs_function_bind(vm, &set_immediate, njs_set_immediate, 1);
    if (ret != NJS_OK) {
        return NJS_ERROR;
    }

    ret = njs_function_bind(vm, &clear_timeout, njs_clear_timeout, 1);
    if (ret != NJS_OK) {
        return NJS_ERROR;
    }

    return NJS_OK;
}


njs_int_t
ngx_js_ext_string(njs_vm_t *vm, njs_object_prop_t *prop, njs_value_t *value,
    njs_value_t *setval, njs_value_t *retval)
{
    char       *p;
    ngx_str_t  *field;

    p = njs_vm_external(vm, NJS_PROTO_ID_ANY, value);
    if (p == NULL) {
        njs_value_undefined_set(retval);
        return NJS_DECLINED;
    }

    field = (ngx_str_t *) (p + njs_vm_prop_magic32(prop));

    return njs_vm_value_string_create(vm, retval, field->data, field->len);
}


njs_int_t
ngx_js_ext_uint(njs_vm_t *vm, njs_object_prop_t *prop, njs_value_t *value,
    njs_value_t *setval, njs_value_t *retval)
{
    char        *p;
    ngx_uint_t   field;

    p = njs_vm_external(vm, NJS_PROTO_ID_ANY, value);
    if (p == NULL) {
        njs_value_undefined_set(retval);
        return NJS_DECLINED;
    }

    field = *(ngx_uint_t *) (p + njs_vm_prop_magic32(prop));

    njs_value_number_set(retval, field);

    return NJS_OK;
}


njs_int_t
ngx_js_ext_constant(njs_vm_t *vm, njs_object_prop_t *prop,
    njs_value_t *value, njs_value_t *setval, njs_value_t *retval)
{
    uint32_t  magic32;

    magic32 = njs_vm_prop_magic32(prop);

    switch (njs_vm_prop_magic16(prop)) {
    case NGX_JS_NUMBER:
        njs_value_number_set(retval, magic32);
        break;

    case NGX_JS_BOOLEAN:
    default:
        njs_value_boolean_set(retval, magic32);
        break;
    }

    return NJS_OK;
}


njs_int_t
ngx_js_ext_flags(njs_vm_t *vm, njs_object_prop_t *prop,
    njs_value_t *value, njs_value_t *setval, njs_value_t *retval)
{
    uintptr_t  data;

    data = (uintptr_t) njs_vm_external(vm, NJS_PROTO_ID_ANY, value);
    if (data == 0) {
        njs_value_undefined_set(retval);
        return NJS_DECLINED;
    }

    data = data & (uintptr_t) njs_vm_prop_magic32(prop);

    switch (njs_vm_prop_magic16(prop)) {
    case NGX_JS_BOOLEAN:
    default:
        njs_value_boolean_set(retval, data);
        break;
    }

    return NJS_OK;
}


njs_int_t
ngx_js_ext_build(njs_vm_t *vm, njs_object_prop_t *prop, njs_value_t *value,
    njs_value_t *setval, njs_value_t *retval)
{
    return njs_vm_value_string_create(vm, retval,
#ifdef NGX_BUILD
                                      (u_char *) NGX_BUILD,
                                      njs_strlen(NGX_BUILD)
#else
                                      (u_char *) "",
                                      0
#endif
                                     );
}


njs_int_t
ngx_js_ext_conf_file_path(njs_vm_t *vm, njs_object_prop_t *prop,
    njs_value_t *value, njs_value_t *setval, njs_value_t *retval)
{
    return njs_vm_value_string_create(vm, retval, ngx_cycle->conf_file.data,
                                      ngx_cycle->conf_file.len);
}


njs_int_t
ngx_js_ext_conf_prefix(njs_vm_t *vm, njs_object_prop_t *prop,
    njs_value_t *value, njs_value_t *setval, njs_value_t *retval)
{
    return njs_vm_value_string_create(vm, retval, ngx_cycle->conf_prefix.data,
                                      ngx_cycle->conf_prefix.len);
}


njs_int_t
ngx_js_ext_error_log_path(njs_vm_t *vm, njs_object_prop_t *prop,
    njs_value_t *value, njs_value_t *setval, njs_value_t *retval)
{
    return njs_vm_value_string_create(vm, retval, ngx_cycle->error_log.data,
                                      ngx_cycle->error_log.len);
}


njs_int_t
ngx_js_ext_prefix(njs_vm_t *vm, njs_object_prop_t *prop, njs_value_t *value,
    njs_value_t *setval, njs_value_t *retval)
{
    return njs_vm_value_string_create(vm, retval, ngx_cycle->prefix.data,
                                      ngx_cycle->prefix.len);
}


njs_int_t
ngx_js_ext_version(njs_vm_t *vm, njs_object_prop_t *prop, njs_value_t *value,
    njs_value_t *setval, njs_value_t *retval)
{
    return njs_vm_value_string_create(vm, retval, (u_char *) NGINX_VERSION,
                                      njs_strlen(NGINX_VERSION));
}


njs_int_t
ngx_js_ext_worker_id(njs_vm_t *vm, njs_object_prop_t *prop, njs_value_t *value,
    njs_value_t *setval, njs_value_t *retval)
{
    njs_value_number_set(retval, ngx_worker);
    return NJS_OK;
}


njs_int_t
ngx_js_ext_log(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t magic, njs_value_t *retval)
{
    char        *p;
    ngx_int_t   lvl;
    njs_str_t   msg;
    njs_uint_t  n, level;

    p = njs_vm_external(vm, NJS_PROTO_ID_ANY, njs_argument(args, 0));
    if (p == NULL) {
        njs_vm_error(vm, "\"this\" is not an external");
        return NJS_ERROR;
    }

    level = magic & NGX_JS_LOG_MASK;

    if (level == 0) {
        if (ngx_js_integer(vm, njs_arg(args, nargs, 1), &lvl) != NGX_OK) {
            return NJS_ERROR;
        }

        level = lvl;
        n = 2;

    } else {
        n = 1;
    }

    for (; n < nargs; n++) {
        if (njs_vm_value_dump(vm, &msg, njs_argument(args, n), 1,
                              !!(magic & NGX_JS_LOG_DUMP))
            == NJS_ERROR)
        {
            return NJS_ERROR;
        }

        ngx_js_logger(vm, p, level, msg.start, msg.length);
    }

    njs_value_undefined_set(retval);

    return NJS_OK;
}


static njs_int_t
ngx_js_ext_console_time(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t unused, njs_value_t *retval)
{
    njs_int_t           ret;
    njs_str_t           name;
    ngx_queue_t         *labels, *q;
    njs_value_t         *value, *this;
    ngx_js_console_t    *console;
    ngx_js_timelabel_t  *label;

    static const njs_str_t  default_label = njs_str("default");

    this = njs_argument(args, 0);

    if (njs_slow_path(!njs_value_is_external(this, ngx_js_console_proto_id))) {
        njs_vm_type_error(vm, "\"this\" is not a console external");
        return NJS_ERROR;
    }

    name = default_label;

    value = njs_arg(args, nargs, 1);

    if (njs_slow_path(!njs_value_is_string(value))) {
        if (!njs_value_is_undefined(value)) {
            ret = njs_value_to_string(vm, value, value);
            if (njs_slow_path(ret != NJS_OK)) {
                return ret;
            }

            njs_value_string_get(value, &name);
        }

    } else {
        njs_value_string_get(value, &name);
    }

    console = njs_value_external(this);

    if (console == NULL) {
        console = njs_mp_alloc(njs_vm_memory_pool(vm),
                               sizeof(ngx_js_console_t));
        if (console == NULL) {
            njs_vm_memory_error(vm);
            return NJS_ERROR;
        }

        ngx_queue_init(&console->labels);

        njs_value_external_set(this, console);
    }

    labels = &console->labels;

    for (q = ngx_queue_head(labels);
         q != ngx_queue_sentinel(labels);
         q = ngx_queue_next(q))
    {
        label = ngx_queue_data(q, ngx_js_timelabel_t, queue);

        if (njs_strstr_eq(&name, &label->name)) {
            ngx_js_log(vm, njs_vm_external_ptr(vm), NGX_LOG_INFO,
                       "Timer \"%V\" already exists.", &name);
            njs_value_undefined_set(retval);
            return NJS_OK;
        }
    }

    label = njs_mp_alloc(njs_vm_memory_pool(vm),
                         sizeof(ngx_js_timelabel_t) + name.length);
    if (njs_slow_path(label == NULL)) {
        njs_vm_memory_error(vm);
        return NJS_ERROR;
    }

    label->name.length = name.length;
    label->name.start = (u_char *) label + sizeof(ngx_js_timelabel_t);
    memcpy(label->name.start, name.start, name.length);

    label->time = ngx_js_monotonic_time();

    ngx_queue_insert_tail(&console->labels, &label->queue);

    njs_value_undefined_set(retval);

    return NJS_OK;
}


static njs_int_t
ngx_js_ext_console_time_end(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t unused, njs_value_t *retval)
{
    uint64_t            ns, ms;
    njs_int_t           ret;
    njs_str_t           name;
    ngx_queue_t         *labels, *q;
    njs_value_t         *value, *this;
    ngx_js_console_t    *console;
    ngx_js_timelabel_t  *label;

    static const njs_str_t  default_label = njs_str("default");

    ns = ngx_js_monotonic_time();

    this = njs_argument(args, 0);

    if (njs_slow_path(!njs_value_is_external(this, ngx_js_console_proto_id))) {
        njs_vm_type_error(vm, "\"this\" is not a console external");
        return NJS_ERROR;
    }

    name = default_label;

    value = njs_arg(args, nargs, 1);

    if (njs_slow_path(!njs_value_is_string(value))) {
        if (!njs_value_is_undefined(value)) {
            ret = njs_value_to_string(vm, value, value);
            if (njs_slow_path(ret != NJS_OK)) {
                return ret;
            }

            njs_value_string_get(value, &name);
        }

    } else {
        njs_value_string_get(value, &name);
    }

    console = njs_value_external(this);
    if (njs_slow_path(console == NULL)) {
        goto not_found;
    }

    labels = &console->labels;
    q = ngx_queue_head(labels);

    for ( ;; ) {
        if (q == ngx_queue_sentinel(labels)) {
            goto not_found;
        }

        label = ngx_queue_data(q, ngx_js_timelabel_t, queue);

        if (njs_strstr_eq(&name, &label->name)) {
            ngx_queue_remove(&label->queue);
            break;
        }

        q = ngx_queue_next(q);
    }

    ns = ns - label->time;

    ms = ns / 1000000;
    ns = ns % 1000000;

    ngx_js_log(vm, njs_vm_external_ptr(vm), NGX_LOG_INFO, "%V: %uL.%06uLms",
               &name, ms, ns);

    njs_value_undefined_set(retval);

    return NJS_OK;

not_found:

    ngx_js_log(vm, njs_vm_external_ptr(vm), NGX_LOG_INFO,
               "Timer \"%V\" doesn't exist.", &name);

    njs_value_undefined_set(retval);

    return NJS_OK;
}


static void
ngx_js_timer_handler(ngx_event_t *ev)
{
    njs_vm_t        *vm;
    ngx_int_t        rc;
    ngx_js_ctx_t    *ctx;
    ngx_js_event_t  *event;

    event = (ngx_js_event_t *) ((u_char *) ev - offsetof(ngx_js_event_t, ev));

    vm = event->vm;

    rc = ngx_js_call(vm, event->function, event->args, event->nargs);

    ctx = ngx_external_ctx(vm, njs_vm_external_ptr(vm));
    ngx_js_del_event(ctx, event);

    ngx_external_event_finalize(vm)(njs_vm_external_ptr(vm), rc);
}


static void
ngx_js_clear_timer(njs_external_ptr_t external, ngx_js_event_t *event)
{
    if (event->ev.timer_set) {
        ngx_del_timer(&event->ev);
    }
}


static njs_int_t
njs_set_timer(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t unused, njs_bool_t immediate, njs_value_t *retval)
{
    uint64_t           delay;
    njs_uint_t         n;
    ngx_js_ctx_t      *ctx;
    ngx_js_event_t    *event;
    ngx_connection_t  *c;

    if (njs_slow_path(nargs < 2)) {
        njs_vm_type_error(vm, "too few arguments");
        return NJS_ERROR;
    }

    if (njs_slow_path(!njs_value_is_function(njs_argument(args, 1)))) {
        njs_vm_type_error(vm, "first arg must be a function");
        return NJS_ERROR;
    }

    delay = 0;

    if (!immediate && nargs >= 3
        && njs_value_is_number(njs_argument(args, 2)))
    {
        delay = njs_value_number(njs_argument(args, 2));
    }

    n = immediate ? 2 : 3;
    nargs = (nargs >= n) ? nargs - n : 0;

    event = njs_mp_zalloc(njs_vm_memory_pool(vm),
                          sizeof(ngx_js_event_t)
                          + sizeof(njs_opaque_value_t) * nargs);
    if (njs_slow_path(event == NULL)) {
        njs_vm_memory_error(vm);
        return NJS_ERROR;
    }

    event->vm = vm;
    event->function = njs_value_function(njs_argument(args, 1));
    event->nargs = nargs;
    event->args = (njs_value_t *) ((u_char *) event + sizeof(ngx_js_event_t));
    event->destructor = ngx_js_clear_timer;

    ctx = ngx_external_ctx(vm, njs_vm_external_ptr(vm));
    event->fd = ctx->event_id++;

    c = ngx_external_connection(vm, njs_vm_external_ptr(vm));

    event->ev.log = c->log;
    event->ev.data = event;
    event->ev.handler = ngx_js_timer_handler;

    if (event->nargs != 0) {
        memcpy(event->args, njs_argument(args, n),
               sizeof(njs_opaque_value_t) * event->nargs);
    }

    ngx_js_add_event(ctx, event);

    ngx_add_timer(&event->ev, delay);

    njs_value_number_set(retval, event->fd);

    return NJS_OK;
}


static njs_int_t
njs_set_timeout(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t unused, njs_value_t *retval)
{
    return njs_set_timer(vm, args, nargs, unused, 0, retval);
}


static njs_int_t
njs_set_immediate(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t unused, njs_value_t *retval)
{
    return njs_set_timer(vm, args, nargs, unused, 1, retval);
}


static njs_int_t
njs_clear_timeout(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t unused, njs_value_t *retval)
{
    ngx_js_ctx_t       *ctx;
    ngx_js_event_t      event_lookup, *event;
    njs_rbtree_node_t  *rb;

    if (nargs < 2 || !njs_value_is_number(njs_argument(args, 1))) {
        njs_value_undefined_set(retval);
        return NJS_OK;
    }

    ctx = ngx_external_ctx(vm, njs_vm_external_ptr(vm));
    event_lookup.fd = njs_value_number(njs_argument(args, 1));

    rb = njs_rbtree_find(&ctx->waiting_events, &event_lookup.node);
    if (njs_slow_path(rb == NULL)) {
        njs_vm_internal_error(vm, "failed to find timer");
        return NJS_ERROR;
    }

    event = (ngx_js_event_t *) ((u_char *) rb - offsetof(ngx_js_event_t, node));

    ngx_js_del_event(ctx, event);

    njs_value_undefined_set(retval);

    return NJS_OK;
}


void
ngx_js_log(njs_vm_t *vm, njs_external_ptr_t external, ngx_uint_t level,
    const char *fmt, ...)
{
    u_char   *p;
    va_list   args;
    u_char    buf[NGX_MAX_ERROR_STR];

    va_start(args, fmt);
    p = njs_vsprintf(buf, buf + sizeof(buf), fmt, args);
    va_end(args);

    ngx_js_logger(vm, external, level, buf, p - buf);
}


void
ngx_js_logger(njs_vm_t *vm, njs_external_ptr_t external, ngx_uint_t level,
    const u_char *start, size_t length)
{
    ngx_log_t           *log;
    ngx_connection_t    *c;
    ngx_log_handler_pt   handler;

    handler = NULL;

    if (external != NULL) {
        c = ngx_external_connection(vm, external);
        log =  c->log;
        handler = log->handler;
        log->handler = NULL;

    } else {

        /* Logger was called during init phase. */

        log = ngx_cycle->log;
    }

    ngx_log_error(level, log, 0, "js: %*s", length, start);

    if (external != NULL) {
        log->handler = handler;
    }
}


char *
ngx_js_import(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    ngx_js_loc_conf_t *jscf = conf;

    u_char               *p, *end, c;
    ngx_int_t             from;
    ngx_str_t            *value, name, path;
    ngx_js_named_path_t  *import;

    value = cf->args->elts;
    from = (cf->args->nelts == 4);

    if (from) {
        if (ngx_strcmp(value[2].data, "from") != 0) {
            ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                               "invalid parameter \"%V\"", &value[2]);
            return NGX_CONF_ERROR;
        }
    }

    name = value[1];
    path = (from ? value[3] : value[1]);

    if (!from) {
        end = name.data + name.len;

        for (p = end - 1; p >= name.data; p--) {
            if (*p == '/') {
                break;
            }
        }

        name.data = p + 1;
        name.len = end - p - 1;

        if (name.len < 3
            || ngx_memcmp(&name.data[name.len - 3], ".js", 3) != 0)
        {
            ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                               "cannot extract export name from file path "
                               "\"%V\", use extended \"from\" syntax", &path);
            return NGX_CONF_ERROR;
        }

        name.len -= 3;
    }

    if (name.len == 0) {
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0, "empty export name");
        return NGX_CONF_ERROR;
    }

    p = name.data;
    end = name.data + name.len;

    while (p < end) {
        c = ngx_tolower(*p);

        if (*p != '_' && (c < 'a' || c > 'z')) {
            if (p == name.data) {
                ngx_conf_log_error(NGX_LOG_EMERG, cf, 0, "cannot start "
                                   "with \"%c\" in export name \"%V\"", *p,
                                   &name);
                return NGX_CONF_ERROR;
            }

            if (*p < '0' || *p > '9') {
                ngx_conf_log_error(NGX_LOG_EMERG, cf, 0, "invalid character "
                                   "\"%c\" in export name \"%V\"", *p,
                                   &name);
                return NGX_CONF_ERROR;
            }
        }

        p++;
    }

    if (ngx_strchr(path.data, '\'') != NULL) {
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0, "invalid character \"'\" "
                           "in file path \"%V\"", &path);
        return NGX_CONF_ERROR;
    }

    if (jscf->imports == NGX_CONF_UNSET_PTR) {
        jscf->imports = ngx_array_create(cf->pool, 4,
                                         sizeof(ngx_js_named_path_t));
        if (jscf->imports == NULL) {
            return NGX_CONF_ERROR;
        }
    }

    import = ngx_array_push(jscf->imports);
    if (import == NULL) {
        return NGX_CONF_ERROR;
    }

    import->name = name;
    import->path = path;
    import->file = cf->conf_file->file.name.data;
    import->line = cf->conf_file->line;

    return NGX_CONF_OK;
}


char *
ngx_js_preload_object(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    ngx_js_loc_conf_t    *jscf = conf;

    u_char               *p, *end, c;
    ngx_int_t             from;
    ngx_str_t            *value, name, path;
    ngx_js_named_path_t  *preload;

    value = cf->args->elts;
    from = (cf->args->nelts == 4);

    if (from) {
        if (ngx_strcmp(value[2].data, "from") != 0) {
            ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                               "invalid parameter \"%V\"", &value[2]);
            return NGX_CONF_ERROR;
        }
    }

    name = value[1];
    path = (from ? value[3] : value[1]);

    if (!from) {
        end = name.data + name.len;

        for (p = end - 1; p >= name.data; p--) {
            if (*p == '/') {
                break;
            }
        }

        name.data = p + 1;
        name.len = end - p - 1;

        if (name.len < 5
            || ngx_memcmp(&name.data[name.len - 5], ".json", 5) != 0)
        {
            ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                               "cannot extract export name from file path "
                               "\"%V\", use extended \"from\" syntax", &path);
            return NGX_CONF_ERROR;
        }

        name.len -= 5;
    }

    if (name.len == 0) {
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0, "empty global name");
        return NGX_CONF_ERROR;
    }

    p = name.data;
    end = name.data + name.len;

    while (p < end) {
        c = ngx_tolower(*p);

        if (*p != '_' && (c < 'a' || c > 'z')) {
            if (p == name.data) {
                ngx_conf_log_error(NGX_LOG_EMERG, cf, 0, "cannot start "
                                   "with \"%c\" in global name \"%V\"", *p,
                                   &name);
                return NGX_CONF_ERROR;
            }

            if (*p < '0' || *p > '9' || *p == '.') {
                ngx_conf_log_error(NGX_LOG_EMERG, cf, 0, "invalid character "
                                   "\"%c\" in global name \"%V\"", *p,
                                   &name);
                return NGX_CONF_ERROR;
            }
        }

        p++;
    }

    if (ngx_strchr(path.data, '\'') != NULL) {
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0, "invalid character \"'\" "
                           "in file path \"%V\"", &path);
        return NGX_CONF_ERROR;
    }

    if (jscf->preload_objects == NGX_CONF_UNSET_PTR) {
        jscf->preload_objects = ngx_array_create(cf->pool, 4,
                                         sizeof(ngx_js_named_path_t));
        if (jscf->preload_objects == NULL) {
            return NGX_CONF_ERROR;
        }
    }

    preload = ngx_array_push(jscf->preload_objects);
    if (preload == NULL) {
        return NGX_CONF_ERROR;
    }

    preload->name = name;
    preload->path = path;
    preload->file = cf->conf_file->file.name.data;
    preload->line = cf->conf_file->line;

    return NGX_CONF_OK;
}


ngx_int_t
ngx_js_init_preload_vm(ngx_conf_t *cf, ngx_js_loc_conf_t *conf)
{
    u_char               *p, *start;
    size_t                size;
    njs_vm_t             *vm;
    njs_int_t             ret;
    ngx_uint_t            i;
    njs_vm_opt_t          options;
    njs_opaque_value_t    retval;
    ngx_js_named_path_t  *preload;

    njs_vm_opt_init(&options);

    options.init = 1;
    options.addons = njs_js_addon_modules_shared;

    vm = njs_vm_create(&options);
    if (vm == NULL) {
        goto error;
    }

    njs_str_t str = njs_str(
        "import fs from 'fs';"

        "let g = (function (np, no, nf, nsp, r) {"
            "return function (n, p) {"
                "p = (p[0] == '/') ? p : ngx.conf_prefix + p;"
                "let o = r(p);"
                "globalThis[n] = np("
                    "o,"
                    "function (k, v)  {"
                        "if (v instanceof no) {"
                            "nf(nsp(v, null));"
                        "}"
                        "return v;"
                    "}"
                ");"
                "return;"
            "}"
        "})(JSON.parse,Object,Object.freeze,"
           "Object.setPrototypeOf,fs.readFileSync);\n"
    );

    size = str.length;

    preload = conf->preload_objects->elts;
    for (i = 0; i < conf->preload_objects->nelts; i++) {
        size += sizeof("g('','');\n") - 1 + preload[i].name.len
                + preload[i].path.len;
    }

    start = ngx_pnalloc(cf->pool, size);
    if (start == NULL) {
        return NGX_ERROR;
    }

    p = ngx_cpymem(start, str.start, str.length);

    preload = conf->preload_objects->elts;
    for (i = 0; i < conf->preload_objects->nelts; i++) {
       p = ngx_cpymem(p, "g('", sizeof("g('") - 1);
       p = ngx_cpymem(p, preload[i].name.data, preload[i].name.len);
       p = ngx_cpymem(p, "','", sizeof("','") - 1);
       p = ngx_cpymem(p, preload[i].path.data, preload[i].path.len);
       p = ngx_cpymem(p, "');\n", sizeof("');\n") - 1);
    }

    ret = njs_vm_compile(vm, &start,  start + size);
    if (ret != NJS_OK) {
        goto error;
    }

    ret = njs_vm_start(vm, njs_value_arg(&retval));
    if (ret != NJS_OK) {
        goto error;
    }

    conf->preload_vm = vm;

    return NGX_OK;

error:

    if (vm != NULL) {
        njs_vm_destroy(vm);
    }

    return NGX_ERROR;
}


ngx_int_t
ngx_js_merge_vm(ngx_conf_t *cf, ngx_js_loc_conf_t *conf,
    ngx_js_loc_conf_t *prev,
    ngx_int_t (*init_vm) (ngx_conf_t *cf, ngx_js_loc_conf_t *conf))
{
    ngx_str_t            *path, *s;
    ngx_uint_t            i;
    ngx_array_t          *imports, *preload_objects, *paths;
    ngx_js_named_path_t  *import, *pi, *pij, *preload;

    if (conf->imports == NGX_CONF_UNSET_PTR
        && conf->paths == NGX_CONF_UNSET_PTR
        && conf->preload_objects == NGX_CONF_UNSET_PTR)
    {
        if (prev->vm != NULL) {
            conf->preload_objects = prev->preload_objects;
            conf->imports = prev->imports;
            conf->paths = prev->paths;
            conf->vm = prev->vm;

            conf->preload_vm = prev->preload_vm;

            return NGX_OK;
        }
    }

    if (prev->preload_objects != NGX_CONF_UNSET_PTR) {
        if (conf->preload_objects == NGX_CONF_UNSET_PTR) {
            conf->preload_objects = prev->preload_objects;

        } else {
            preload_objects = ngx_array_create(cf->pool, 4,
                                       sizeof(ngx_js_named_path_t));
            if (preload_objects == NULL) {
                return NGX_ERROR;
            }

            pij = prev->preload_objects->elts;

            for (i = 0; i < prev->preload_objects->nelts; i++) {
                preload = ngx_array_push(preload_objects);
                if (preload == NULL) {
                    return NGX_ERROR;
                }

                *preload = pij[i];
            }

            pij = conf->preload_objects->elts;

            for (i = 0; i < conf->preload_objects->nelts; i++) {
                preload = ngx_array_push(preload_objects);
                if (preload == NULL) {
                    return NGX_ERROR;
                }

                *preload = pij[i];
            }

            conf->preload_objects = preload_objects;
        }
    }

    if (prev->imports != NGX_CONF_UNSET_PTR) {
        if (conf->imports == NGX_CONF_UNSET_PTR) {
            conf->imports = prev->imports;

        } else {
            imports = ngx_array_create(cf->pool, 4,
                                       sizeof(ngx_js_named_path_t));
            if (imports == NULL) {
                return NGX_ERROR;
            }

            pi = prev->imports->elts;

            for (i = 0; i < prev->imports->nelts; i++) {
                import = ngx_array_push(imports);
                if (import == NULL) {
                    return NGX_ERROR;
                }

                *import = pi[i];
            }

            pi = conf->imports->elts;

            for (i = 0; i < conf->imports->nelts; i++) {
                import = ngx_array_push(imports);
                if (import == NULL) {
                    return NGX_ERROR;
                }

                *import = pi[i];
            }

            conf->imports = imports;
        }
    }

    if (prev->paths != NGX_CONF_UNSET_PTR) {
        if (conf->paths == NGX_CONF_UNSET_PTR) {
            conf->paths = prev->paths;

        } else {
            paths = ngx_array_create(cf->pool, 4, sizeof(ngx_str_t));
            if (paths == NULL) {
                return NGX_ERROR;
            }

            s = prev->imports->elts;

            for (i = 0; i < prev->paths->nelts; i++) {
                path = ngx_array_push(paths);
                if (path == NULL) {
                    return NGX_ERROR;
                }

                *path = s[i];
            }

            s = conf->imports->elts;

            for (i = 0; i < conf->paths->nelts; i++) {
                path = ngx_array_push(paths);
                if (path == NULL) {
                    return NGX_ERROR;
                }

                *path = s[i];
            }

            conf->paths = paths;
        }
    }

    if (conf->imports == NGX_CONF_UNSET_PTR) {
        return NGX_OK;
    }

    return init_vm(cf, (ngx_js_loc_conf_t *) conf);
}


static void
ngx_js_rejection_tracker(njs_vm_t *vm, njs_external_ptr_t unused,
    njs_bool_t is_handled, njs_value_t *promise, njs_value_t *reason)
{
    void                       *promise_obj;
    uint32_t                    i, length;
    ngx_js_ctx_t               *ctx;
    ngx_js_rejected_promise_t  *rejected_promise;

    ctx = ngx_external_ctx(vm, njs_vm_external_ptr(vm));

    if (is_handled && ctx->rejected_promises != NULL) {
        rejected_promise = ctx->rejected_promises->start;
        length = ctx->rejected_promises->items;

        promise_obj = njs_value_ptr(promise);

        for (i = 0; i < length; i++) {
            if (rejected_promise[i].promise == promise_obj) {
                njs_arr_remove(ctx->rejected_promises,
                               &rejected_promise[i]);

                break;
            }
        }

        return;
    }

    if (ctx->rejected_promises == NULL) {
        ctx->rejected_promises = njs_arr_create(njs_vm_memory_pool(vm), 4,
                                             sizeof(ngx_js_rejected_promise_t));
        if (njs_slow_path(ctx->rejected_promises == NULL)) {
            return;
        }
    }

    rejected_promise = njs_arr_add(ctx->rejected_promises);
    if (njs_slow_path(rejected_promise == NULL)) {
        return;
    }

    rejected_promise->promise = njs_value_ptr(promise);
    njs_value_assign(&rejected_promise->message, reason);
}


static njs_int_t
ngx_js_module_path(const ngx_str_t *dir, njs_module_info_t *info)
{
    char        *p;
    size_t      length;
    njs_bool_t  trail;
    char        src[NGX_MAX_PATH + 1];

    trail = 0;
    length = info->name.length;

    if (dir != NULL) {
        length += dir->len;

        if (length == 0 || dir->len == 0) {
            return NJS_DECLINED;
        }

        trail = (dir->data[dir->len - 1] != '/');

        if (trail) {
            length++;
        }
    }

    if (njs_slow_path(length > NGX_MAX_PATH)) {
        return NJS_ERROR;
    }

    p = &src[0];

    if (dir != NULL) {
        p = (char *) njs_cpymem(p, dir->data, dir->len);

        if (trail) {
            *p++ = '/';
        }
    }

    p = (char *) njs_cpymem(p, info->name.start, info->name.length);
    *p = '\0';

    p = realpath(&src[0], &info->path[0]);
    if (p == NULL) {
        return NJS_DECLINED;
    }

    info->fd = open(&info->path[0], O_RDONLY);
    if (info->fd < 0) {
        return NJS_DECLINED;
    }

    info->file.start = (u_char *) &info->path[0];
    info->file.length = njs_strlen(info->file.start);

    return NJS_OK;
}


static njs_int_t
ngx_js_module_lookup(ngx_js_loc_conf_t *conf, njs_module_info_t *info)
{
    njs_int_t   ret;
    ngx_str_t   *path;
    njs_uint_t  i;

    if (info->name.start[0] == '/') {
        return ngx_js_module_path(NULL, info);
    }

    ret = ngx_js_module_path(&conf->cwd, info);

    if (ret != NJS_DECLINED) {
        return ret;
    }

    ret = ngx_js_module_path((const ngx_str_t *) &ngx_cycle->conf_prefix, info);

    if (ret != NJS_DECLINED) {
        return ret;
    }

    if (conf->paths == NGX_CONF_UNSET_PTR) {
        return NJS_DECLINED;
    }

    path = conf->paths->elts;

    for (i = 0; i < conf->paths->nelts; i++) {
        ret = ngx_js_module_path(&path[i], info);

        if (ret != NJS_DECLINED) {
            return ret;
        }
    }

    return NJS_DECLINED;
}


static njs_int_t
ngx_js_module_read(njs_mp_t *mp, int fd, njs_str_t *text)
{
    ssize_t      n;
    struct stat  sb;

    text->start = NULL;

    if (fstat(fd, &sb) == -1) {
        goto fail;
    }

    if (!S_ISREG(sb.st_mode)) {
        goto fail;
    }

    text->length = sb.st_size;

    text->start = njs_mp_alloc(mp, text->length + 1);
    if (text->start == NULL) {
        goto fail;
    }

    n = read(fd, text->start, sb.st_size);

    if (n < 0 || n != sb.st_size) {
        goto fail;
    }

    text->start[text->length] = '\0';

    return NJS_OK;

fail:

    if (text->start != NULL) {
        njs_mp_free(mp, text->start);
    }

    return NJS_ERROR;
}


static void
ngx_js_file_dirname(const njs_str_t *path, ngx_str_t *name)
{
    const u_char  *p, *end;

    if (path->length == 0) {
        goto current_dir;
    }

    p = path->start + path->length - 1;

    /* Stripping basename. */

    while (p >= path->start && *p != '/') { p--; }

    end = p + 1;

    if (end == path->start) {
        goto current_dir;
    }

    /* Stripping trailing slashes. */

    while (p >= path->start && *p == '/') { p--; }

    p++;

    if (p == path->start) {
        p = end;
    }

    name->data = path->start;
    name->len = p - path->start;

    return;

current_dir:

    ngx_str_set(name, ".");
}


static njs_int_t
ngx_js_set_cwd(njs_vm_t *vm, ngx_js_loc_conf_t *conf, njs_str_t *path)
{
    ngx_str_t  cwd;

    ngx_js_file_dirname(path, &cwd);

    conf->cwd.data = njs_mp_alloc(njs_vm_memory_pool(vm), cwd.len);
    if (conf->cwd.data == NULL) {
        return NJS_ERROR;
    }

    memcpy(conf->cwd.data, cwd.data, cwd.len);
    conf->cwd.len = cwd.len;

    return NJS_OK;
}


static njs_mod_t *
ngx_js_module_loader(njs_vm_t *vm, njs_external_ptr_t external, njs_str_t *name)
{
    u_char             *start;
    njs_int_t           ret;
    njs_str_t           text;
    ngx_str_t           prev_cwd;
    njs_mod_t          *module;
    ngx_js_loc_conf_t  *conf;
    njs_module_info_t   info;

    conf = external;

    njs_memzero(&info, sizeof(njs_module_info_t));

    info.name = *name;

    ret = ngx_js_module_lookup(conf, &info);
    if (njs_slow_path(ret != NJS_OK)) {
        return NULL;
    }

    ret = ngx_js_module_read(njs_vm_memory_pool(vm), info.fd, &text);

    (void) close(info.fd);

    if (ret != NJS_OK) {
        njs_vm_internal_error(vm, "while reading \"%V\" module", &info.file);
        return NULL;
    }

    prev_cwd = conf->cwd;

    ret = ngx_js_set_cwd(vm, conf, &info.file);
    if (ret != NJS_OK) {
        njs_vm_internal_error(vm, "while setting cwd for \"%V\" module",
                              &info.file);
        return NULL;
    }

    start = text.start;

    module = njs_vm_compile_module(vm, &info.file, &start,
                                   &text.start[text.length]);

    njs_mp_free(njs_vm_memory_pool(vm), conf->cwd.data);
    conf->cwd = prev_cwd;

    njs_mp_free(njs_vm_memory_pool(vm), text.start);

    return module;
}


ngx_int_t
ngx_js_init_conf_vm(ngx_conf_t *cf, ngx_js_loc_conf_t *conf,
    njs_vm_opt_t *options)
{
    size_t                size;
    u_char               *start, *end, *p;
    ngx_str_t            *m, file;
    njs_int_t             rc;
    njs_str_t             text;
    ngx_uint_t            i;
    njs_value_t          *value;
    ngx_pool_cleanup_t   *cln;
    njs_opaque_value_t    lvalue, exception;
    ngx_js_named_path_t  *import;

    static const njs_str_t line_number_key = njs_str("lineNumber");
    static const njs_str_t file_name_key = njs_str("fileName");

    if (conf->preload_objects != NGX_CONF_UNSET_PTR) {
       if (ngx_js_init_preload_vm(cf, (ngx_js_loc_conf_t *)conf) != NGX_OK) {
           return NGX_ERROR;
       }
    }

    size = 0;

    import = conf->imports->elts;
    for (i = 0; i < conf->imports->nelts; i++) {

        /* import <name> from '<path>'; globalThis.<name> = <name>; */

        size += sizeof("import  from '';") - 1 + import[i].name.len * 3
                + import[i].path.len
                + sizeof(" globalThis. = ;\n") - 1;
    }

    start = ngx_pnalloc(cf->pool, size + 1);
    if (start == NULL) {
        return NGX_ERROR;
    }

    p = start;
    import = conf->imports->elts;
    for (i = 0; i < conf->imports->nelts; i++) {

        /* import <name> from '<path>'; globalThis.<name> = <name>; */

        p = ngx_cpymem(p, "import ", sizeof("import ") - 1);
        p = ngx_cpymem(p, import[i].name.data, import[i].name.len);
        p = ngx_cpymem(p, " from '", sizeof(" from '") - 1);
        p = ngx_cpymem(p, import[i].path.data, import[i].path.len);
        p = ngx_cpymem(p, "'; globalThis.", sizeof("'; globalThis.") - 1);
        p = ngx_cpymem(p, import[i].name.data, import[i].name.len);
        p = ngx_cpymem(p, " = ", sizeof(" = ") - 1);
        p = ngx_cpymem(p, import[i].name.data, import[i].name.len);
        p = ngx_cpymem(p, ";\n", sizeof(";\n") - 1);
    }

    *p = '\0';

    file = ngx_cycle->conf_prefix;

    options->file.start = file.data;
    options->file.length = file.len;

    conf->vm = njs_vm_create(options);
    if (conf->vm == NULL) {
        ngx_log_error(NGX_LOG_EMERG, cf->log, 0, "failed to create js VM");
        return NGX_ERROR;
    }

    cln = ngx_pool_cleanup_add(cf->pool, 0);
    if (cln == NULL) {
        return NGX_ERROR;
    }

    cln->handler = ngx_js_cleanup_vm;
    cln->data = conf;

    njs_vm_set_rejection_tracker(conf->vm, ngx_js_rejection_tracker,
                                 NULL);

    rc = ngx_js_set_cwd(conf->vm, conf, &options->file);
    if (rc != NJS_OK) {
        ngx_log_error(NGX_LOG_EMERG, cf->log, 0, "failed to set cwd");
        return NGX_ERROR;
    }

    njs_vm_set_module_loader(conf->vm, ngx_js_module_loader, conf);

    if (conf->paths != NGX_CONF_UNSET_PTR) {
        m = conf->paths->elts;

        for (i = 0; i < conf->paths->nelts; i++) {
            if (ngx_conf_full_name(cf->cycle, &m[i], 1) != NGX_OK) {
                return NGX_ERROR;
            }
        }
    }

    end = start + size;

    rc = njs_vm_compile(conf->vm, &start, end);

    if (rc != NJS_OK) {
        njs_vm_exception_get(conf->vm, njs_value_arg(&exception));
        njs_vm_value_string(conf->vm, &text, njs_value_arg(&exception));

        value = njs_vm_object_prop(conf->vm, njs_value_arg(&exception),
                                   &file_name_key, &lvalue);
        if (value == NULL) {
            value = njs_vm_object_prop(conf->vm, njs_value_arg(&exception),
                                       &line_number_key, &lvalue);

            if (value != NULL) {
                i = njs_value_number(value) - 1;

                if (i < conf->imports->nelts) {
                    import = conf->imports->elts;
                    ngx_log_error(NGX_LOG_EMERG, cf->log, 0,
                                  "%*s, included in %s:%ui", text.length,
                                  text.start, import[i].file, import[i].line);
                    return NGX_ERROR;
                }
            }
        }

        ngx_log_error(NGX_LOG_EMERG, cf->log, 0, "%*s", text.length,
                      text.start);
        return NGX_ERROR;
    }

    if (start != end) {
        ngx_log_error(NGX_LOG_EMERG, cf->log, 0,
                      "extra characters in js script: \"%*s\"",
                      end - start, start);
        return NGX_ERROR;
    }

    return NGX_OK;
}


static njs_int_t
ngx_js_unhandled_rejection(ngx_js_ctx_t *ctx)
{
    njs_int_t                   ret;
    njs_str_t                   message;
    ngx_js_rejected_promise_t  *rejected_promise;

    if (ctx->rejected_promises == NULL
        || ctx->rejected_promises->items == 0)
    {
        return 0;
    }

    rejected_promise = ctx->rejected_promises->start;

    ret = njs_vm_value_to_string(ctx->vm, &message,
                                 njs_value_arg(&rejected_promise->message));
    if (njs_slow_path(ret != NJS_OK)) {
        return -1;
    }

    njs_vm_error(ctx->vm, "unhandled promise rejection: %V", &message);

    njs_arr_destroy(ctx->rejected_promises);
    ctx->rejected_promises = NULL;

    return 1;
}


static void
ngx_js_cleanup_vm(void *data)
{
    ngx_js_loc_conf_t  *jscf = data;

    njs_vm_destroy(jscf->vm);

    if (jscf->preload_objects != NGX_CONF_UNSET_PTR) {
        njs_vm_destroy(jscf->preload_vm);
    }
}


ngx_js_loc_conf_t *
ngx_js_create_conf(ngx_conf_t *cf, size_t size)
{
    ngx_js_loc_conf_t  *conf;

    conf = ngx_pcalloc(cf->pool, size);
    if (conf == NULL) {
        return NULL;
    }

    conf->paths = NGX_CONF_UNSET_PTR;
    conf->imports = NGX_CONF_UNSET_PTR;
    conf->preload_objects = NGX_CONF_UNSET_PTR;

    conf->buffer_size = NGX_CONF_UNSET_SIZE;
    conf->max_response_body_size = NGX_CONF_UNSET_SIZE;
    conf->timeout = NGX_CONF_UNSET_MSEC;

    return conf;
}


#if defined(NGX_HTTP_SSL) || defined(NGX_STREAM_SSL)

static char *
ngx_js_set_ssl(ngx_conf_t *cf, ngx_js_loc_conf_t *conf)
{
    ngx_ssl_t           *ssl;
    ngx_pool_cleanup_t  *cln;

    ssl = ngx_pcalloc(cf->pool, sizeof(ngx_ssl_t));
    if (ssl == NULL) {
        return NGX_CONF_ERROR;
    }

    conf->ssl = ssl;
    ssl->log = cf->log;

    if (ngx_ssl_create(ssl, conf->ssl_protocols, NULL) != NGX_OK) {
        return NGX_CONF_ERROR;
    }

    cln = ngx_pool_cleanup_add(cf->pool, 0);
    if (cln == NULL) {
        ngx_ssl_cleanup_ctx(ssl);
        return NGX_CONF_ERROR;
    }

    cln->handler = ngx_ssl_cleanup_ctx;
    cln->data = ssl;

    if (ngx_ssl_ciphers(NULL, ssl, &conf->ssl_ciphers, 0) != NGX_OK) {
        return NGX_CONF_ERROR;
    }

    if (ngx_ssl_trusted_certificate(cf, ssl, &conf->ssl_trusted_certificate,
                                    conf->ssl_verify_depth)
        != NGX_OK)
    {
        return NGX_CONF_ERROR;
    }

    return NGX_CONF_OK;
}

#endif


char *
ngx_js_merge_conf(ngx_conf_t *cf, void *parent, void *child,
  ngx_int_t (*init_vm)(ngx_conf_t *cf, ngx_js_loc_conf_t *conf))
{
    ngx_js_loc_conf_t *prev = parent;
    ngx_js_loc_conf_t *conf = child;

    ngx_conf_merge_msec_value(conf->timeout, prev->timeout, 60000);
    ngx_conf_merge_size_value(conf->buffer_size, prev->buffer_size, 16384);
    ngx_conf_merge_size_value(conf->max_response_body_size,
                              prev->max_response_body_size, 1048576);

    if (ngx_js_merge_vm(cf, (ngx_js_loc_conf_t *) conf,
                        (ngx_js_loc_conf_t *) prev,
                        init_vm)
        != NGX_OK)
    {
        return NGX_CONF_ERROR;
    }

#if defined(NGX_HTTP_SSL) || defined(NGX_STREAM_SSL)
    ngx_conf_merge_str_value(conf->ssl_ciphers, prev->ssl_ciphers,
                             "DEFAULT");

    ngx_conf_merge_bitmask_value(conf->ssl_protocols, prev->ssl_protocols,
                             (NGX_CONF_BITMASK_SET|NGX_SSL_TLSv1
                              |NGX_SSL_TLSv1_1|NGX_SSL_TLSv1_2));

    ngx_conf_merge_value(conf->ssl_verify, prev->ssl_verify, 1);
    ngx_conf_merge_value(conf->ssl_verify_depth, prev->ssl_verify_depth,
                         100);

    ngx_conf_merge_str_value(conf->ssl_trusted_certificate,
                         prev->ssl_trusted_certificate, "");

    return ngx_js_set_ssl(cf, conf);
#else
    return NGX_CONF_OK;
#endif
}


static uint64_t
ngx_js_monotonic_time(void)
{
#if (NGX_HAVE_CLOCK_MONOTONIC)
    struct timespec  ts;

#if defined(CLOCK_MONOTONIC_FAST)
    clock_gettime(CLOCK_MONOTONIC_FAST, &ts);
#else
    clock_gettime(CLOCK_MONOTONIC, &ts);
#endif

    return (uint64_t) ts.tv_sec * 1000000000 + ts.tv_nsec;
#else
    struct timeval tv;

    gettimeofday(&tv, NULL);

    return (uint64_t) tv.tv_sec * 1000000000 + tv.tv_usec * 1000;
#endif
}
