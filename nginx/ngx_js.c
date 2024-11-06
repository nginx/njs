
/*
 * Copyright (C) Roman Arutyunyan
 * Copyright (C) Dmitry Volyntsev
 * Copyright (C) NGINX, Inc.
 */


#include <ngx_config.h>
#include <ngx_core.h>
#include <math.h>
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
    void                *promise_obj;
    njs_opaque_value_t   promise;
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


static ngx_int_t ngx_engine_njs_init(ngx_engine_t *engine,
    ngx_engine_opts_t *opts);
static ngx_int_t ngx_engine_njs_compile(ngx_js_loc_conf_t *conf, ngx_log_t *log,
    u_char *start, size_t size);
static ngx_int_t ngx_engine_njs_call(ngx_js_ctx_t *ctx, ngx_str_t *fname,
    njs_opaque_value_t *args, njs_uint_t nargs);
static void *ngx_engine_njs_external(ngx_engine_t *engine);
static ngx_int_t ngx_engine_njs_pending(ngx_engine_t *engine);
static ngx_int_t ngx_engine_njs_string(ngx_engine_t *e,
    njs_opaque_value_t *value, ngx_str_t *str);
static void ngx_engine_njs_destroy(ngx_engine_t *e, ngx_js_ctx_t *ctx,
    ngx_js_loc_conf_t *conf);

#if (NJS_HAVE_QUICKJS)
static ngx_int_t ngx_engine_qjs_init(ngx_engine_t *engine,
    ngx_engine_opts_t *opts);
static ngx_int_t ngx_engine_qjs_compile(ngx_js_loc_conf_t *conf, ngx_log_t *log,
    u_char *start, size_t size);
static ngx_int_t ngx_engine_qjs_call(ngx_js_ctx_t *ctx, ngx_str_t *fname,
    njs_opaque_value_t *args, njs_uint_t nargs);
static void *ngx_engine_qjs_external(ngx_engine_t *engine);
static ngx_int_t ngx_engine_qjs_pending(ngx_engine_t *engine);
static ngx_int_t ngx_engine_qjs_string(ngx_engine_t *e,
    njs_opaque_value_t *value, ngx_str_t *str);

static JSValue ngx_qjs_process_getter(JSContext *ctx, JSValueConst this_val);
static JSValue ngx_qjs_ext_set_timeout(JSContext *cx, JSValueConst this_val,
    int argc, JSValueConst *argv, int immediate);
static JSValue ngx_qjs_ext_clear_timeout(JSContext *cx, JSValueConst this_val,
    int argc, JSValueConst *argv);

static JSValue ngx_qjs_ext_build(JSContext *cx, JSValueConst this_val);
static JSValue ngx_qjs_ext_conf_file_path(JSContext *cx, JSValueConst this_val);
static JSValue ngx_qjs_ext_conf_prefix(JSContext *cx, JSValueConst this_val);
static JSValue ngx_qjs_ext_constant_integer(JSContext *cx,
    JSValueConst this_val, int magic);
static JSValue ngx_qjs_ext_error_log_path(JSContext *cx, JSValueConst this_val);
static JSValue ngx_qjs_ext_log(JSContext *cx, JSValueConst this_val,
    int argc, JSValueConst *argv, int level);
static JSValue ngx_qjs_ext_console_time(JSContext *cx, JSValueConst this_val,
    int argc, JSValueConst *argv);
static JSValue ngx_qjs_ext_console_time_end(JSContext *cx,
    JSValueConst this_val, int argc, JSValueConst *argv);
static JSValue ngx_qjs_ext_prefix(JSContext *cx, JSValueConst this_val);
static JSValue ngx_qjs_ext_worker_id(JSContext *cx, JSValueConst this_val);

static void ngx_qjs_console_finalizer(JSRuntime *rt, JSValue val);

static JSModuleDef *ngx_qjs_module_loader(JSContext *ctx,
    const char *module_name, void *opaque);
static int ngx_qjs_unhandled_rejection(ngx_js_ctx_t *ctx);
static void ngx_qjs_rejection_tracker(JSContext *ctx, JSValueConst promise,
    JSValueConst reason, JS_BOOL is_handled, void *opaque);

static JSValue ngx_qjs_value(JSContext *cx, const ngx_str_t *path);
static ngx_int_t ngx_qjs_dump_obj(ngx_engine_t *e, JSValueConst val,
    ngx_str_t *dst);

static JSModuleDef *ngx_qjs_core_init(JSContext *cx, const char *name);
#endif

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
static void ngx_js_rejection_tracker(njs_vm_t *vm, njs_external_ptr_t unused,
    njs_bool_t is_handled, njs_value_t *promise, njs_value_t *reason);
static njs_mod_t *ngx_js_module_loader(njs_vm_t *vm,
    njs_external_ptr_t external, njs_str_t *name);
static njs_int_t ngx_js_module_lookup(ngx_js_loc_conf_t *conf,
    njs_module_info_t *info);
static njs_int_t ngx_js_module_read(njs_mp_t *mp, int fd, njs_str_t *text);
static njs_int_t ngx_js_set_cwd(njs_mp_t *mp, ngx_js_loc_conf_t *conf,
    njs_str_t *path);
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


#if (NJS_HAVE_QUICKJS)

static const JSCFunctionListEntry ngx_qjs_ext_ngx[] = {
    JS_CGETSET_DEF("build", ngx_qjs_ext_build, NULL),
    JS_CGETSET_DEF("conf_prefix", ngx_qjs_ext_conf_prefix, NULL),
    JS_CGETSET_DEF("conf_file_path", ngx_qjs_ext_conf_file_path, NULL),
    JS_CGETSET_MAGIC_DEF("ERR", ngx_qjs_ext_constant_integer, NULL,
                         NGX_LOG_ERR),
    JS_CGETSET_DEF("error_log_path", ngx_qjs_ext_error_log_path, NULL),
    JS_CGETSET_MAGIC_DEF("INFO", ngx_qjs_ext_constant_integer, NULL,
                         NGX_LOG_INFO),
    JS_CFUNC_MAGIC_DEF("log", 1, ngx_qjs_ext_log, 0),
    JS_CGETSET_DEF("prefix", ngx_qjs_ext_prefix, NULL),
    JS_PROP_STRING_DEF("version", NGINX_VERSION, JS_PROP_C_W_E),
    JS_PROP_INT32_DEF("version_number", nginx_version, JS_PROP_C_W_E),
    JS_CGETSET_MAGIC_DEF("WARN", ngx_qjs_ext_constant_integer, NULL,
                         NGX_LOG_WARN),
    JS_CGETSET_DEF("worker_id", ngx_qjs_ext_worker_id, NULL),
};


static const JSCFunctionListEntry ngx_qjs_ext_console[] = {
    JS_CFUNC_MAGIC_DEF("error", 1, ngx_qjs_ext_log, NGX_LOG_ERR),
    JS_CFUNC_MAGIC_DEF("info", 1, ngx_qjs_ext_log, NGX_LOG_INFO),
    JS_CFUNC_MAGIC_DEF("log", 1, ngx_qjs_ext_log, NGX_LOG_INFO),
    JS_CFUNC_DEF("time", 1, ngx_qjs_ext_console_time),
    JS_CFUNC_DEF("timeEnd", 1, ngx_qjs_ext_console_time_end),
    JS_CFUNC_MAGIC_DEF("warn", 1, ngx_qjs_ext_log, NGX_LOG_WARN),
};


static const JSCFunctionListEntry ngx_qjs_ext_global[] = {
    JS_CGETSET_DEF("process", ngx_qjs_process_getter, NULL),
    JS_CFUNC_MAGIC_DEF("setTimeout", 1, ngx_qjs_ext_set_timeout, 0),
    JS_CFUNC_MAGIC_DEF("setImmediate", 1, ngx_qjs_ext_set_timeout, 1),
    JS_CFUNC_DEF("clearTimeout", 1, ngx_qjs_ext_clear_timeout),
};


static JSClassDef ngx_qjs_console_class = {
    "Console",
    .finalizer = ngx_qjs_console_finalizer,
};


qjs_module_t  ngx_qjs_ngx_module = {
    .name = "ngx",
    .init = ngx_qjs_core_init,
};

#endif

static ngx_engine_t *
ngx_create_engine(ngx_engine_opts_t *opts)
{
    njs_mp_t      *mp;
    ngx_int_t      rc;
    ngx_engine_t  *engine;

    mp = njs_mp_fast_create(2 * getpagesize(), 128, 512, 16);
    if (mp == NULL) {
        return NULL;
    }

    engine = njs_mp_zalloc(mp, sizeof(ngx_engine_t));
    if (engine == NULL) {
        return NULL;
    }

    engine->pool = mp;
    engine->clone = opts->clone;

    switch (opts->engine) {
    case NGX_ENGINE_NJS:
        rc = ngx_engine_njs_init(engine, opts);
        if (rc != NGX_OK) {
            return NULL;
        }

        engine->name = "njs";
        engine->type = NGX_ENGINE_NJS;
        engine->compile = ngx_engine_njs_compile;
        engine->call = ngx_engine_njs_call;
        engine->external = ngx_engine_njs_external;
        engine->pending = ngx_engine_njs_pending;
        engine->string = ngx_engine_njs_string;
        engine->destroy = opts->destroy ? opts->destroy
                                        : ngx_engine_njs_destroy;
        break;

#if (NJS_HAVE_QUICKJS)
    case NGX_ENGINE_QJS:
        rc = ngx_engine_qjs_init(engine, opts);
        if (rc != NGX_OK) {
            return NULL;
        }

        engine->name = "QuickJS";
        engine->type = NGX_ENGINE_QJS;
        engine->compile = ngx_engine_qjs_compile;
        engine->call = ngx_engine_qjs_call;
        engine->external = ngx_engine_qjs_external;
        engine->pending = ngx_engine_qjs_pending;
        engine->string = ngx_engine_qjs_string;
        engine->destroy = opts->destroy ? opts->destroy
                                        : ngx_engine_qjs_destroy;
        break;
#endif

    default:
        return NULL;
    }

    return engine;
}


static ngx_int_t
ngx_engine_njs_init(ngx_engine_t *engine, ngx_engine_opts_t *opts)
{
    njs_vm_t      *vm;
    ngx_int_t      rc;
    njs_vm_opt_t   vm_options;

    njs_vm_opt_init(&vm_options);

    vm_options.backtrace = 1;
    vm_options.addons = opts->u.njs.addons;
    vm_options.metas = opts->u.njs.metas;
    vm_options.file = opts->file;
    vm_options.argv = ngx_argv;
    vm_options.argc = ngx_argc;

    vm = njs_vm_create(&vm_options);
    if (vm == NULL) {
        return NGX_ERROR;
    }

    njs_vm_set_rejection_tracker(vm, ngx_js_rejection_tracker, NULL);

    rc = ngx_js_set_cwd(njs_vm_memory_pool(vm), opts->conf, &vm_options.file);
    if (rc != NGX_OK) {
        return NGX_ERROR;
    }

    njs_vm_set_module_loader(vm, ngx_js_module_loader, opts->conf);

    engine->u.njs.vm = vm;

    return NJS_OK;
}


static ngx_int_t
ngx_engine_njs_compile(ngx_js_loc_conf_t *conf, ngx_log_t *log, u_char *start,
    size_t size)
{
    u_char               *end;
    njs_vm_t             *vm;
    njs_int_t             rc;
    njs_str_t             text;
    ngx_uint_t            i;
    njs_value_t          *value;
    njs_opaque_value_t    exception, lvalue;
    ngx_js_named_path_t  *import;

    static const njs_str_t line_number_key = njs_str("lineNumber");
    static const njs_str_t file_name_key = njs_str("fileName");

    vm = conf->engine->u.njs.vm;
    end = start + size;

    rc = njs_vm_compile(vm, &start, end);

    if (rc != NJS_OK) {
        njs_vm_exception_get(vm, njs_value_arg(&exception));
        njs_vm_value_string(vm, &text, njs_value_arg(&exception));

        value = njs_vm_object_prop(vm, njs_value_arg(&exception),
                                   &file_name_key, &lvalue);
        if (value == NULL) {
            value = njs_vm_object_prop(vm, njs_value_arg(&exception),
                                       &line_number_key, &lvalue);

            if (value != NULL) {
                i = njs_value_number(value) - 1;

                if (i < conf->imports->nelts) {
                    import = conf->imports->elts;
                    ngx_log_error(NGX_LOG_EMERG, log, 0,
                                  "%*s, included in %s:%ui", text.length,
                                  text.start, import[i].file, import[i].line);
                    return NGX_ERROR;
                }
            }
        }

        ngx_log_error(NGX_LOG_EMERG, log, 0, "%*s", text.length, text.start);
        return NGX_ERROR;
    }

    if (start != end) {
        ngx_log_error(NGX_LOG_EMERG, log, 0,
                      "extra characters in js script: \"%*s\"",
                      end - start, start);
        return NGX_ERROR;
    }

    return NGX_OK;
}


ngx_engine_t *
ngx_njs_clone(ngx_js_ctx_t *ctx, ngx_js_loc_conf_t *cf, void *external)
{
    njs_vm_t             *vm;
    njs_int_t             rc;
    njs_str_t             key;
    ngx_str_t             exception;
    ngx_uint_t            i;
    ngx_engine_t         *engine;
    njs_opaque_value_t    retval;
    ngx_js_named_path_t  *preload;

    vm = njs_vm_clone(cf->engine->u.njs.vm, external);
    if (vm == NULL) {
        return NULL;
    }

    engine = njs_mp_alloc(njs_vm_memory_pool(vm), sizeof(ngx_engine_t));
    if (engine == NULL) {
        return NULL;
    }

    memcpy(engine, cf->engine, sizeof(ngx_engine_t));
    engine->pool = njs_vm_memory_pool(vm);
    engine->u.njs.vm = vm;

    /* bind objects from preload vm */

    if (cf->preload_objects != NGX_CONF_UNSET_PTR) {
        preload = cf->preload_objects->elts;

        for (i = 0; i < cf->preload_objects->nelts; i++) {
            key.start = preload[i].name.data;
            key.length = preload[i].name.len;

            rc = njs_vm_value(cf->preload_vm, &key, njs_value_arg(&retval));
            if (rc != NJS_OK) {
                return NULL;
            }

            rc = njs_vm_bind(vm, &key, njs_value_arg(&retval), 0);
            if (rc != NJS_OK) {
                return NULL;
            }
        }
    }

    if (njs_vm_start(vm, njs_value_arg(&retval)) == NJS_ERROR) {
        ngx_js_exception(vm, &exception);

        ngx_log_error(NGX_LOG_ERR, ctx->log, 0, "js exception: %V", &exception);

        return NULL;
    }

    return engine;
}


static ngx_int_t
ngx_engine_njs_call(ngx_js_ctx_t *ctx, ngx_str_t *fname,
    njs_opaque_value_t *args, njs_uint_t nargs)
{
    njs_vm_t        *vm;
    njs_int_t        ret;
    njs_str_t        name;
    ngx_str_t        exception;
    njs_function_t  *func;

    name.start = fname->data;
    name.length = fname->len;

    vm = ctx->engine->u.njs.vm;

    func = njs_vm_function(vm, &name);
    if (func == NULL) {
        ngx_log_error(NGX_LOG_ERR, ctx->log, 0,
                      "js function \"%V\" not found", fname);
        return NGX_ERROR;
    }

    ret = njs_vm_invoke(vm, func, njs_value_arg(args), nargs,
                        njs_value_arg(&ctx->retval));
    if (ret == NJS_ERROR) {
        ngx_js_exception(vm, &exception);

        ngx_log_error(NGX_LOG_ERR, ctx->log, 0,
                      "js exception: %V", &exception);

        return NGX_ERROR;
    }

    for ( ;; ) {
        ret = njs_vm_execute_pending_job(vm);
        if (ret <= NJS_OK) {
            if (ret == NJS_ERROR) {
                ngx_js_exception(vm, &exception);

                ngx_log_error(NGX_LOG_ERR, ctx->log, 0,
                              "js job exception: %V", &exception);
                return NGX_ERROR;
            }

            break;
        }
    }

    if (ngx_js_unhandled_rejection(ctx)) {
        ngx_js_exception(vm, &exception);

        ngx_log_error(NGX_LOG_ERR, ctx->log, 0, "js exception: %V", &exception);
        return NGX_ERROR;
    }

    return njs_rbtree_is_empty(&ctx->waiting_events) ? NGX_OK : NGX_AGAIN;
}


static void *
ngx_engine_njs_external(ngx_engine_t *engine)
{
    return njs_vm_external_ptr(engine->u.njs.vm);
}

static ngx_int_t
ngx_engine_njs_pending(ngx_engine_t *e)
{
    return njs_vm_pending(e->u.njs.vm);
}


static ngx_int_t
ngx_engine_njs_string(ngx_engine_t *e, njs_opaque_value_t *value,
    ngx_str_t *str)
{
    ngx_int_t  rc;
    njs_str_t  s;

    rc = ngx_js_string(e->u.njs.vm, njs_value_arg(value), &s);

    str->data = s.start;
    str->len = s.length;

    return rc;
}


static void
ngx_engine_njs_destroy(ngx_engine_t *e, ngx_js_ctx_t *ctx,
    ngx_js_loc_conf_t *conf)
{
    ngx_js_event_t     *event;
    njs_rbtree_node_t  *node;

    if (ctx != NULL) {
        node = njs_rbtree_min(&ctx->waiting_events);

        while (njs_rbtree_is_there_successor(&ctx->waiting_events, node)) {
            event = (ngx_js_event_t *) ((u_char *) node
                                        - offsetof(ngx_js_event_t, node));

            if (event->destructor != NULL) {
                event->destructor(event);
            }

            node = njs_rbtree_node_successor(&ctx->waiting_events, node);
        }
    }

    njs_vm_destroy(e->u.njs.vm);

    /*
     * when ctx !=NULL e->pool is vm pool, in such case it is destroyed
     * by njs_vm_destroy().
     */

    if (ctx == NULL) {
        njs_mp_destroy(e->pool);
    }
}


#if (NJS_HAVE_QUICKJS)

static ngx_int_t
ngx_engine_qjs_init(ngx_engine_t *engine, ngx_engine_opts_t *opts)
{
    JSRuntime  *rt;

    rt = JS_NewRuntime();
    if (rt == NULL) {
        return NGX_ERROR;
    }

    engine->u.qjs.ctx = qjs_new_context(rt, opts->u.qjs.addons);
    if (engine->u.qjs.ctx == NULL) {
        return NGX_ERROR;
    }

    JS_SetRuntimeOpaque(rt, opts->u.qjs.metas);
    JS_SetContextOpaque(engine->u.qjs.ctx, opts->u.qjs.addons);

    JS_SetModuleLoaderFunc(rt, NULL, ngx_qjs_module_loader, opts->conf);

    return NGX_OK;
}


static ngx_int_t
ngx_engine_qjs_compile(ngx_js_loc_conf_t *conf, ngx_log_t *log, u_char *start,
    size_t size)
{
    JSValue               code;
    ngx_str_t             text;
    JSContext            *cx;
    ngx_engine_t         *engine;
    ngx_js_code_entry_t  *pc;

    engine = conf->engine;
    cx = engine->u.qjs.ctx;

    code = JS_Eval(cx, (char *) start, size, "<main>",
                   JS_EVAL_TYPE_MODULE | JS_EVAL_FLAG_COMPILE_ONLY);

    if (JS_IsException(code)) {
        ngx_qjs_exception(engine, &text);
        ngx_log_error(NGX_LOG_EMERG, log, 0, "js compile %V", &text);
        return NGX_ERROR;
    }

    pc = njs_arr_add(engine->precompiled);
    if (pc == NULL) {
        JS_FreeValue(cx, code);
        ngx_log_error(NGX_LOG_EMERG, log, 0, "njs_arr_add() failed");
        return NGX_ERROR;
    }

    pc->code = JS_WriteObject(cx, &pc->code_size, code, JS_WRITE_OBJ_BYTECODE);
    if (pc->code == NULL) {
        JS_FreeValue(cx, code);
        ngx_log_error(NGX_LOG_EMERG, log, 0, "JS_WriteObject() failed");
        return NGX_ERROR;
    }

    JS_FreeValue(cx, code);

    return NGX_OK;
}


static JSValue
js_std_await(JSContext *ctx, JSValue obj)
{
    int         state, err;
    JSValue     ret;
    JSContext  *ctx1;

    for (;;) {
        state = JS_PromiseState(ctx, obj);
        if (state == JS_PROMISE_FULFILLED) {
            ret = JS_PromiseResult(ctx, obj);
            JS_FreeValue(ctx, obj);
            break;

        } else if (state == JS_PROMISE_REJECTED) {
            ret = JS_Throw(ctx, JS_PromiseResult(ctx, obj));
            JS_FreeValue(ctx, obj);
            break;

        } else if (state == JS_PROMISE_PENDING) {
            err = JS_ExecutePendingJob(JS_GetRuntime(ctx), &ctx1);
            if (err < 0) {
               /* js_std_dump_error(ctx1); */
            }

        } else {
            /* not a promise */
            ret = obj;
            break;
        }
    }

    return ret;
}


ngx_engine_t *
ngx_qjs_clone(ngx_js_ctx_t *ctx, ngx_js_loc_conf_t *cf, void *external)
{
    JSValue               rv;
    njs_mp_t             *mp;
    uint32_t              i, length;
    JSRuntime            *rt;
    ngx_str_t             exception;
    JSContext            *cx;
    ngx_engine_t         *engine;
    ngx_js_code_entry_t  *pc;

    mp = njs_mp_fast_create(2 * getpagesize(), 128, 512, 16);
    if (mp == NULL) {
        return NULL;
    }

    engine = njs_mp_alloc(mp, sizeof(ngx_engine_t));
    if (engine == NULL) {
        return NULL;
    }

    memcpy(engine, cf->engine, sizeof(ngx_engine_t));
    engine->pool = mp;

    if (cf->reuse_queue != NULL) {
        engine->u.qjs.ctx = ngx_js_queue_pop(cf->reuse_queue);
        if (engine->u.qjs.ctx != NULL) {
            ngx_log_debug1(NGX_LOG_DEBUG_HTTP, ctx->log, 0,
                           "js reused context: %p", engine->u.qjs.ctx);
            JS_SetContextOpaque(engine->u.qjs.ctx, external);
            return engine;
        }
    }

    rt = JS_NewRuntime();
    if (rt == NULL) {
        return NULL;
    }

    JS_SetRuntimeOpaque(rt, JS_GetRuntimeOpaque(
                                        JS_GetRuntime(cf->engine->u.qjs.ctx)));

    cx = qjs_new_context(rt, JS_GetContextOpaque(cf->engine->u.qjs.ctx));
    if (cx == NULL) {
        JS_FreeRuntime(rt);
        return NULL;
    }

    engine->u.qjs.ctx = cx;
    JS_SetContextOpaque(cx, external);

    JS_SetHostPromiseRejectionTracker(rt, ngx_qjs_rejection_tracker, ctx);


    /* TODO: bind objects from preload vm */

    rv = JS_UNDEFINED;
    pc = engine->precompiled->start;
    length = engine->precompiled->items;

    for (i = 0; i < length; i++) {
        rv = JS_ReadObject(cx, pc[i].code, pc[i].code_size,
                           JS_READ_OBJ_BYTECODE);
        if (JS_IsException(rv)) {
            ngx_qjs_exception(engine, &exception);

            ngx_log_error(NGX_LOG_ERR, ctx->log, 0,
                          "js load module exception: %V", &exception);
            goto destroy;
        }
    }

    if (JS_ResolveModule(cx, rv) < 0) {
        ngx_log_error(NGX_LOG_ERR, ctx->log, 0, "js resolve module failed");
        goto destroy;
    }

    rv = JS_EvalFunction(cx, rv);

    if (JS_IsException(rv)) {
        ngx_qjs_exception(engine, &exception);

        ngx_log_error(NGX_LOG_ERR, ctx->log, 0, "js eval exception: %V",
                      &exception);
        goto destroy;
    }

    rv = js_std_await(cx, rv);
    if (JS_IsException(rv)) {
        ngx_qjs_exception(engine, &exception);

        ngx_log_error(NGX_LOG_ERR, ctx->log, 0, "js eval exception: %V",
                      &exception);
        goto destroy;
    }

    JS_FreeValue(cx, rv);

    return engine;

destroy:

    JS_FreeContext(cx);
    JS_FreeRuntime(rt);
    njs_mp_destroy(mp);

    return NULL;
}


static ngx_int_t
ngx_engine_qjs_call(ngx_js_ctx_t *ctx, ngx_str_t *fname,
    njs_opaque_value_t *args, njs_uint_t nargs)
{
    int         rc;
    JSValue     fn, val;
    ngx_str_t   exception;
    JSRuntime  *rt;
    JSContext  *cx, *cx1;

    cx = ctx->engine->u.qjs.ctx;

    fn = ngx_qjs_value(cx, fname);
    if (!JS_IsFunction(cx, fn)) {
        JS_FreeValue(cx, fn);
        ngx_log_error(NGX_LOG_ERR, ctx->log, 0, "js function \"%V\" not found",
                      fname);

        return NGX_ERROR;
    }

    val = JS_Call(cx, fn, JS_UNDEFINED, nargs, &ngx_qjs_arg(args[0]));
    JS_FreeValue(cx, fn);
    if (JS_IsException(val)) {
        ngx_qjs_exception(ctx->engine, &exception);

        ngx_log_error(NGX_LOG_ERR, ctx->log, 0,
                      "js call exception: %V", &exception);

        return NGX_ERROR;
    }

    JS_FreeValue(cx, ngx_qjs_arg(ctx->retval));
    ngx_qjs_arg(ctx->retval) = val;

    rt = JS_GetRuntime(cx);

    for ( ;; ) {
        rc = JS_ExecutePendingJob(rt, &cx1);
        if (rc <= 0) {
            if (rc == -1) {
                ngx_qjs_exception(ctx->engine, &exception);

                ngx_log_error(NGX_LOG_ERR, ctx->log, 0,
                              "js job exception: %V", &exception);

                return NGX_ERROR;
            }

            break;
        }
    }

    if (ngx_qjs_unhandled_rejection(ctx)) {
        ngx_qjs_exception(ctx->engine, &exception);

        ngx_log_error(NGX_LOG_ERR, ctx->log, 0, "js exception: %V", &exception);
        return NGX_ERROR;
    }

    return njs_rbtree_is_empty(&ctx->waiting_events) ? NGX_OK : NGX_AGAIN;
}


static void *
ngx_engine_qjs_external(ngx_engine_t *e)
{
    return JS_GetContextOpaque(e->u.qjs.ctx);
}


static ngx_int_t
ngx_engine_qjs_pending(ngx_engine_t *e)
{
    return JS_IsJobPending(JS_GetRuntime(e->u.qjs.ctx));
}


static ngx_int_t
ngx_engine_qjs_string(ngx_engine_t *e, njs_opaque_value_t *value,
    ngx_str_t *str)
{
    return ngx_qjs_dump_obj(e, ngx_qjs_arg(*value), str);
}


static void
ngx_js_cleanup_reuse_ctx(void *data)
{
    JSRuntime  *rt;
    JSContext  *cx;

    ngx_js_queue_t  *reuse = data;

    for ( ;; ) {
        cx = ngx_js_queue_pop(reuse);
        if (cx == NULL) {
            break;
        }

        rt = JS_GetRuntime(cx);
        JS_FreeContext(cx);
        JS_FreeRuntime(rt);
    }
}


void
ngx_engine_qjs_destroy(ngx_engine_t *e, ngx_js_ctx_t *ctx,
    ngx_js_loc_conf_t *conf)
{
    uint32_t                    i, length;
    JSRuntime                  *rt;
    JSContext                  *cx;
    JSClassID                   class_id;
    ngx_qjs_event_t            *event;
    ngx_js_opaque_t            *opaque;
    njs_rbtree_node_t          *node;
    ngx_pool_cleanup_t         *cln;
    ngx_js_code_entry_t        *pc;
    ngx_js_rejected_promise_t  *rejected_promise;

    cx = e->u.qjs.ctx;

    if (ctx != NULL) {
        node = njs_rbtree_min(&ctx->waiting_events);

        while (njs_rbtree_is_there_successor(&ctx->waiting_events, node)) {
            event = (ngx_qjs_event_t *) ((u_char *) node
                                         - offsetof(ngx_qjs_event_t, node));

            if (event->destructor != NULL) {
                event->destructor(event);
            }

            node = njs_rbtree_node_successor(&ctx->waiting_events, node);
        }

        if (ctx->rejected_promises != NULL) {
            rejected_promise = ctx->rejected_promises->start;

            for (i = 0; i < ctx->rejected_promises->items; i++) {
                JS_FreeValue(cx, ngx_qjs_arg(rejected_promise[i].promise));
                JS_FreeValue(cx, ngx_qjs_arg(rejected_promise[i].message));
            }
        }

        class_id = JS_GetClassID(ngx_qjs_arg(ctx->args[0]));
        opaque = JS_GetOpaque(ngx_qjs_arg(ctx->args[0]), class_id);
        opaque->external = NULL;

        JS_FreeValue(cx, ngx_qjs_arg(ctx->args[0]));
        JS_FreeValue(cx, ngx_qjs_arg(ctx->retval));

    } else if (e->precompiled != NULL) {
        pc = e->precompiled->start;
        length = e->precompiled->items;

        for (i = 0; i < length; i++) {
            js_free(cx, pc[i].code);
        }
    }

    njs_mp_destroy(e->pool);

    if (conf != NULL && conf->reuse != 0) {
        if (conf->reuse_queue == NULL) {
            conf->reuse_queue = ngx_js_queue_create(ngx_cycle->pool,
                                                    conf->reuse);
            if (conf->reuse_queue == NULL) {
                goto free_ctx;
            }

            cln = ngx_pool_cleanup_add(ngx_cycle->pool, 0);
            if (cln == NULL) {
                goto free_ctx;
            }

            cln->handler = ngx_js_cleanup_reuse_ctx;
            cln->data = conf->reuse_queue;
        }

        if (ngx_js_queue_push(conf->reuse_queue, cx) != NGX_OK) {
            goto free_ctx;
        }

        return;
    }

free_ctx:

    rt = JS_GetRuntime(cx);
    JS_FreeContext(cx);
    JS_FreeRuntime(rt);
}


static JSValue
ngx_qjs_value(JSContext *cx, const ngx_str_t *path)
{
    u_char   *start, *p, *end;
    JSAtom    key;
    size_t    size;
    JSValue   value, rv;

    start = path->data;
    end = start + path->len;

    value = JS_GetGlobalObject(cx);

    for ( ;; ) {
        p = njs_strlchr(start, end, '.');

        size = ((p != NULL) ? p : end) - start;
        if (size == 0) {
            JS_FreeValue(cx, value);
            return JS_ThrowTypeError(cx, "empty path element");
        }

        key = JS_NewAtomLen(cx, (char *) start, size);
        if (key == JS_ATOM_NULL) {
            JS_FreeValue(cx, value);
            return JS_ThrowInternalError(cx, "could not create atom");
        }

        rv = JS_GetProperty(cx, value, key);
        JS_FreeAtom(cx, key);
        if (JS_IsException(rv)) {
            JS_FreeValue(cx, value);
            return JS_EXCEPTION;
        }

        JS_FreeValue(cx, value);

        if (p == NULL) {
            break;
        }

        start = p + 1;
        value = rv;
    }

    return rv;
}


static ngx_int_t
ngx_qjs_dump_obj(ngx_engine_t *e, JSValueConst val, ngx_str_t *dst)
{
    size_t       len, byte_offset, byte_length;
    u_char      *start, *p;
    JSValue      buffer, stack;
    ngx_str_t    str, stack_str;
    JSContext    *cx;

    if (JS_IsNullOrUndefined(val)) {
        dst->data = NULL;
        dst->len = 0;
        return NGX_OK;
    }

    cx = e->u.qjs.ctx;

    buffer = JS_GetTypedArrayBuffer(cx, val, &byte_offset, &byte_length, NULL);
    if (!JS_IsException(buffer)) {
        start = JS_GetArrayBuffer(cx, &dst->len, buffer);

        JS_FreeValue(cx, buffer);

        if (start != NULL) {
            start += byte_offset;
            dst->len = byte_length;

            dst->data = njs_mp_alloc(e->pool, dst->len);
            if (dst->data == NULL) {
                return NGX_ERROR;
            }

            memcpy(dst->data, start, dst->len);
            return NGX_OK;
        }
    }

    str.data = (u_char *) JS_ToCString(cx, val);
    if (str.data != NULL) {
        str.len = ngx_strlen(str.data);

        stack = JS_GetPropertyStr(cx, val, "stack");

        stack_str.len = 0;
        stack_str.data = NULL;

        if (!JS_IsException(stack) && !JS_IsUndefined(stack)) {
            stack_str.data = (u_char *) JS_ToCString(cx, stack);
            if (stack_str.data != NULL) {
                stack_str.len = ngx_strlen(stack_str.data);
            }
        }

        len = str.len;

        if (stack_str.len != 0) {
            len += stack_str.len + njs_length("\n");
        }

        start = njs_mp_alloc(e->pool, len);
        if (start == NULL) {
            JS_FreeCString(cx, (char *) str.data);
            JS_FreeValue(cx, stack);
            return NGX_ERROR;
        }

        p = ngx_cpymem(start, str.data, str.len);

        if (stack_str.len != 0) {
            *p++ = '\n';
            (void) ngx_cpymem(p, stack_str.data, stack_str.len);
            JS_FreeCString(cx, (char *) stack_str.data);
        }

        JS_FreeCString(cx, (char *) str.data);
        JS_FreeValue(cx, stack);

    } else {
        len = njs_length("[exception]");

        start = njs_mp_alloc(e->pool, len);
        if (start == NULL) {
            return NGX_ERROR;
        }

        memcpy(start, "[exception]", len);
    }

    dst->data = start;
    dst->len = len;

    return NGX_OK;
}


ngx_int_t
ngx_qjs_call(ngx_js_ctx_t *ctx, JSValue fn, JSValue *argv, int argc)
{
    int         rc;
    JSValue     ret;
    ngx_str_t   exception;
    JSRuntime  *rt;
    JSContext  *cx, *cx1;

    cx = ctx->engine->u.qjs.ctx;

    ret = JS_Call(cx, fn, JS_UNDEFINED, argc, argv);
    if (JS_IsException(ret)) {
        ngx_qjs_exception(ctx->engine, &exception);

        ngx_log_error(NGX_LOG_ERR, ctx->log, 0,
                      "js call exception: %V", &exception);

        return NGX_ERROR;
    }

    JS_FreeValue(cx, ret);

    rt = JS_GetRuntime(cx);

    for ( ;; ) {
        rc = JS_ExecutePendingJob(rt, &cx1);
        if (rc <= 0) {
            if (rc == -1) {
                ngx_qjs_exception(ctx->engine, &exception);

                ngx_log_error(NGX_LOG_ERR, ctx->log, 0,
                              "js job exception: %V", &exception);

                return NGX_ERROR;
            }

            break;
        }
    }

    return NGX_OK;
}


ngx_int_t
ngx_qjs_exception(ngx_engine_t *e, ngx_str_t *s)
{
    JSValue  exception;

    exception = JS_GetException(e->u.qjs.ctx);
    if (ngx_qjs_dump_obj(e, exception, s) != NGX_OK) {
        return NGX_ERROR;
    }

    JS_FreeValue(e->u.qjs.ctx, exception);

    return NGX_OK;
}


ngx_int_t
ngx_qjs_integer(JSContext *cx, JSValueConst val, ngx_int_t *n)
{
    double  num;

    if (JS_ToFloat64(cx, &num, val)) {
        return NGX_ERROR;
    }

    if (isinf(num) || isnan(num)) {
        (void) JS_ThrowTypeError(cx, "invalid number");
        return NGX_ERROR;
    }

    *n = num;

    return NGX_OK;
}


ngx_int_t
ngx_qjs_string(ngx_engine_t *e, JSValueConst val, ngx_str_t *dst)
{
    size_t       len, byte_offset, byte_length;
    u_char      *start;
    JSValue      buffer;
    JSContext   *cx;
    const char  *str;

    if (JS_IsNullOrUndefined(val)) {
        dst->data = NULL;
        dst->len = 0;
        return NGX_OK;
    }

    cx = e->u.qjs.ctx;

    if (JS_IsString(val)) {
        goto string;
    }

    buffer = JS_GetTypedArrayBuffer(cx, val, &byte_offset, &byte_length, NULL);
    if (!JS_IsException(buffer)) {
        start = JS_GetArrayBuffer(cx, &dst->len, buffer);

        JS_FreeValue(cx, buffer);

        if (start != NULL) {
            start += byte_offset;
            dst->len = byte_length;

            dst->data = njs_mp_alloc(e->pool, dst->len);
            if (dst->data == NULL) {
                return NGX_ERROR;
            }

            memcpy(dst->data, start, dst->len);
            return NGX_OK;
        }
    }

string:

    str = JS_ToCString(cx, val);
    if (str == NULL) {
        return NGX_ERROR;
    }

    len = strlen(str);

    start = njs_mp_alloc(e->pool, len);
    if (start == NULL) {
        JS_FreeCString(cx, str);
        return NGX_ERROR;
    }

    memcpy(start, str, len);

    JS_FreeCString(cx, str);

    dst->data = start;
    dst->len = len;

    return NGX_OK;
}


static void
ngx_qjs_timer_handler(ngx_event_t *ev)
{
    void             *external;
    JSContext        *cx;
    ngx_int_t         rc;
    ngx_js_ctx_t     *ctx;
    ngx_qjs_event_t  *event;

    event = (ngx_qjs_event_t *) ((u_char *) ev - offsetof(ngx_qjs_event_t, ev));

    cx = event->ctx;
    external = JS_GetContextOpaque(cx);
    ctx = ngx_qjs_external_ctx(cx, external);

    rc = ngx_qjs_call((ngx_js_ctx_t *) ctx, event->function, event->args,
                      event->nargs);

    ngx_js_del_event(ctx, event);

    ngx_qjs_external_event_finalize(cx)(external, rc);
}


static void
ngx_qjs_clear_timer(ngx_qjs_event_t *event)
{
    int         i;
    JSContext  *cx;

    cx = event->ctx;

    if (event->ev.timer_set) {
        ngx_del_timer(&event->ev);
    }

    JS_FreeValue(cx, event->function);

    for (i = 0; i < (int) event->nargs; i++) {
        JS_FreeValue(cx, event->args[i]);
    }
}


static JSValue
ngx_qjs_process_getter(JSContext *cx, JSValueConst this_val)
{
    return qjs_process_object(cx, ngx_argc, (const char **) ngx_argv);
}


static JSValue
ngx_qjs_ext_set_timeout(JSContext *cx, JSValueConst this_val, int argc,
    JSValueConst *argv, int immediate)
{
    int                i, n;
    void              *external;
    uint32_t           delay;
    ngx_js_ctx_t      *ctx;
    ngx_qjs_event_t   *event;
    ngx_connection_t  *c;

    if (!JS_IsFunction(cx, argv[0])) {
        return JS_ThrowTypeError(cx, "first arg must be a function");
    }

    delay = 0;

    if (!immediate && argc >= 2) {
        if (JS_ToUint32(cx, &delay, argv[1]) < 0) {
            return JS_EXCEPTION;
        }
    }

    n = immediate ? 1 : 2;
    argc = (argc >= n) ? argc - n : 0;
    external = JS_GetContextOpaque(cx);
    ctx = ngx_qjs_external_ctx(cx, external);

    event = ngx_pcalloc(ngx_qjs_external_pool(cx, external),
                        sizeof(ngx_qjs_event_t) + sizeof(JSValue) * argc);
    if (event == NULL) {
        return JS_ThrowOutOfMemory(cx);
    }

    event->ctx = cx;
    event->function = JS_DupValue(cx, argv[0]);
    event->nargs = argc;
    event->args = (JSValue *) &event[1];
    event->destructor = ngx_qjs_clear_timer;
    event->fd = ctx->event_id++;

    c = ngx_qjs_external_connection(cx, external);

    event->ev.log = c->log;
    event->ev.data = event;
    event->ev.handler = ngx_qjs_timer_handler;

    if (event->nargs != 0) {
        for (i = 0; i < argc; i++) {
            event->args[i] = JS_DupValue(cx, argv[n + i]);
        }
    }

    ngx_js_add_event(ctx, event);

    ngx_add_timer(&event->ev, delay);

    return JS_NewInt32(cx, event->fd);
}


static JSValue
ngx_qjs_ext_clear_timeout(JSContext *cx, JSValueConst this_val,
    int argc, JSValueConst *argv)
{
    uint32_t           id;
    ngx_js_ctx_t       *ctx;
    ngx_qjs_event_t     event_lookup, *event;
    njs_rbtree_node_t  *rb;

    if (JS_ToUint32(cx, &id, argv[0]) < 0) {
        return JS_EXCEPTION;
    }

    ctx = ngx_qjs_external_ctx(cx, JS_GetContextOpaque(cx));
    event_lookup.fd = id;

    rb = njs_rbtree_find(&ctx->waiting_events, &event_lookup.node);
    if (rb == NULL) {
        return JS_ThrowReferenceError(cx, "failed to find timer");
    }

    event = (ngx_qjs_event_t *) ((u_char *) rb
                                 - offsetof(ngx_qjs_event_t, node));

    ngx_js_del_event(ctx, event);

    return JS_UNDEFINED;
}


static JSValue
ngx_qjs_ext_build(JSContext *cx, JSValueConst this_val)
{
    return JS_NewStringLen(cx,
#ifdef NGX_BUILD
                           (char *) NGX_BUILD,
                           njs_strlen(NGX_BUILD)
#else
                           (char *) "",
                           0
#endif
                           );
}


static JSValue
ngx_qjs_ext_conf_prefix(JSContext *cx, JSValueConst this_val)
{
    return JS_NewStringLen(cx, (char *) ngx_cycle->prefix.data,
                           ngx_cycle->prefix.len);
}


static JSValue
ngx_qjs_ext_conf_file_path(JSContext *cx, JSValueConst this_val)
{
    return JS_NewStringLen(cx, (char *) ngx_cycle->conf_file.data,
                           ngx_cycle->conf_file.len);
}


static JSValue
ngx_qjs_ext_constant_integer(JSContext *cx, JSValueConst this_val, int magic)
{
    return JS_NewInt32(cx, magic);
}


static JSValue
ngx_qjs_ext_error_log_path(JSContext *cx, JSValueConst this_val)
{
    return JS_NewStringLen(cx, (char *) ngx_cycle->error_log.data,
                           ngx_cycle->error_log.len);
}


static JSValue
ngx_qjs_ext_prefix(JSContext *cx, JSValueConst this_val)
{
    return JS_NewStringLen(cx, (char *) ngx_cycle->prefix.data,
                           ngx_cycle->prefix.len);
}


static JSValue
ngx_qjs_ext_worker_id(JSContext *cx, JSValueConst this_val)
{
    return JS_NewInt32(cx, ngx_worker);
}


static void
ngx_qjs_console_finalizer(JSRuntime *rt, JSValue val)
{
    ngx_queue_t         *labels, *q, *next;
    ngx_js_console_t    *console;
    ngx_js_timelabel_t  *label;

    console = JS_GetOpaque(val, NGX_QJS_CLASS_ID_CONSOLE);
    if (console == (void *) 1) {
        return;
    }

    labels = &console->labels;
    q = ngx_queue_head(labels);

    for ( ;; ) {
        if (q == ngx_queue_sentinel(labels)) {
            break;
        }

        next = ngx_queue_next(q);

        label = ngx_queue_data(q, ngx_js_timelabel_t, queue);
        ngx_queue_remove(&label->queue);
        js_free_rt(rt, label);

        q = next;
    }

    js_free_rt(rt, console);
}


static JSValue
ngx_qjs_ext_log(JSContext *cx, JSValueConst this_val, int argc,
    JSValueConst *argv, int magic)
{
    char              *p;
    uint32_t           level;
    ngx_str_t          msg;
    ngx_js_ctx_t      *ctx;
    ngx_connection_t  *c;

    p = JS_GetContextOpaque(cx);
    if (p == NULL) {
        return JS_ThrowInternalError(cx, "external is not set");
    }

    level = magic & NGX_JS_LOG_MASK;

    if (level == 0) {
        if (JS_ToUint32(cx, &level, argv[0]) < 0) {
            return JS_EXCEPTION;
        }

        argc--;
        argv++;
    }

    ctx = ngx_qjs_external_ctx(cx, p);
    c = ngx_qjs_external_connection(cx, p);

    for ( ; argc > 0; argc--, argv++) {
        if (ngx_qjs_dump_obj(ctx->engine, argv[0], &msg) != NGX_OK) {
            return JS_EXCEPTION;
        }

        ngx_js_logger(c, level, (u_char *) msg.data, msg.len);
    }

    return JS_UNDEFINED;
}


static JSValue
ngx_qjs_ext_console_time(JSContext *cx, JSValueConst this_val, int argc,
    JSValueConst *argv)
{
    ngx_str_t            name;
    ngx_queue_t         *labels, *q;
    ngx_js_console_t    *console;
    ngx_connection_t    *c;
    ngx_js_timelabel_t  *label;

    static const ngx_str_t  default_label = ngx_string("default");

    console = JS_GetOpaque(this_val, NGX_QJS_CLASS_ID_CONSOLE);
    if (console == NULL) {
        return JS_ThrowInternalError(cx, "this is not a console object");
    }

    if (console == (void *) 1) {
        console = js_malloc(cx, sizeof(ngx_js_console_t));
        if (console == NULL) {
            return JS_ThrowOutOfMemory(cx);
        }

        ngx_queue_init(&console->labels);

        JS_SetOpaque(this_val, console);
    }

    if (!JS_IsUndefined(argv[0])) {
        name.data = (u_char *) JS_ToCStringLen(cx, &name.len, argv[0]);
        if (name.data == NULL) {
            return JS_EXCEPTION;
        }

    } else {
        name = default_label;
    }

    labels = &console->labels;

    for (q = ngx_queue_head(labels);
         q != ngx_queue_sentinel(labels);
         q = ngx_queue_next(q))
    {
        label = ngx_queue_data(q, ngx_js_timelabel_t, queue);

        if (name.len == label->name.length
            && ngx_strncmp(name.data, label->name.start, name.len) == 0)
        {
            c = ngx_qjs_external_connection(cx, JS_GetContextOpaque(cx));
            ngx_log_error(NGX_LOG_INFO, c->log, 0, "js: Timer \"%V\" already"
                          " exists", &name);

            goto done;
        }
    }

    label = js_malloc(cx, sizeof(ngx_js_timelabel_t) + name.len);
    if (label == NULL) {
        if (name.data != default_label.data) {
            JS_FreeCString(cx, (char *) name.data);
        }

        return JS_ThrowOutOfMemory(cx);
    }

    label->name.length = name.len;
    label->name.start = (u_char *) label + sizeof(ngx_js_timelabel_t);
    memcpy(label->name.start, name.data, name.len);

    label->time = ngx_js_monotonic_time();

    ngx_queue_insert_tail(&console->labels, &label->queue);

done:

    if (name.data != default_label.data) {
        JS_FreeCString(cx, (char *) name.data);
    }

    return JS_UNDEFINED;
}


static JSValue
ngx_qjs_ext_console_time_end(JSContext *cx, JSValueConst this_val, int argc,
    JSValueConst *argv)
{
    uint64_t             ns, ms;
    ngx_str_t            name;
    ngx_queue_t         *labels, *q;
    ngx_js_console_t    *console;
    ngx_connection_t    *c;
    ngx_js_timelabel_t  *label;

    static const ngx_str_t  default_label = ngx_string("default");

    ns = ngx_js_monotonic_time();

    console = JS_GetOpaque(this_val, NGX_QJS_CLASS_ID_CONSOLE);
    if (console == NULL) {
        return JS_ThrowInternalError(cx, "this is not a console object");
    }

    if (!JS_IsUndefined(argv[0])) {
        name.data = (u_char *) JS_ToCStringLen(cx, &name.len, argv[0]);
        if (name.data == NULL) {
            return JS_EXCEPTION;
        }

    } else {
        name = default_label;
    }

    if (console == (void *) 1) {
        goto not_found;
    }

    labels = &console->labels;
    q = ngx_queue_head(labels);

    for ( ;; ) {
        if (q == ngx_queue_sentinel(labels)) {
            goto not_found;
        }

        label = ngx_queue_data(q, ngx_js_timelabel_t, queue);

        if (name.len == label->name.length
            && ngx_strncmp(name.data, label->name.start, name.len) == 0)
        {
            ngx_queue_remove(&label->queue);
            break;
        }

        q = ngx_queue_next(q);
    }

    ns = ns - label->time;

    js_free(cx, label);

    ms = ns / 1000000;
    ns = ns % 1000000;

    c = ngx_qjs_external_connection(cx, JS_GetContextOpaque(cx));
    ngx_log_error(NGX_LOG_INFO, c->log, 0, "js: %V: %uL.%06uLms",
                  &name, ms, ns);

    if (name.data != default_label.data) {
        JS_FreeCString(cx, (char *) name.data);
    }

    return JS_UNDEFINED;

not_found:

    c = ngx_qjs_external_connection(cx, JS_GetContextOpaque(cx));
    ngx_log_error(NGX_LOG_INFO, c->log, 0, "js: Timer \"%V\" doesn't exist",
                  &name);

    if (name.data != default_label.data) {
        JS_FreeCString(cx, (char *) name.data);
    }

    return JS_UNDEFINED;
}


static JSModuleDef *
ngx_qjs_module_loader(JSContext *cx, const char *module_name, void *opaque)
{
    JSValue               func_val;
    njs_int_t             ret;
    njs_str_t             text;
    JSModuleDef          *m;
    njs_module_info_t     info;
    ngx_js_loc_conf_t    *conf;
    ngx_js_code_entry_t  *pc;

    conf = opaque;

    njs_memzero(&info, sizeof(njs_module_info_t));

    info.name.start = (u_char *) module_name;
    info.name.length = njs_strlen(module_name);

    errno = 0;
    ret = ngx_js_module_lookup(conf, &info);
    if (ret != NJS_OK) {
        if (errno != 0) {
            JS_ThrowReferenceError(cx, "Cannot load module \"%s\" "
                                   "(%s:%s)", module_name,
                                   ngx_js_errno_string(errno), strerror(errno));
        }

        return NULL;
    }

    ret = ngx_js_module_read(conf->engine->pool, info.fd, &text);

    (void) close(info.fd);

    if (ret != NJS_OK) {
        JS_ThrowInternalError(cx, "while reading \"%*s\" module",
                              (int) info.file.length, info.file.start);
        return NULL;
    }

    func_val = JS_Eval(cx, (char *) text.start, text.length, module_name,
                       JS_EVAL_TYPE_MODULE | JS_EVAL_FLAG_COMPILE_ONLY);

    njs_mp_free(conf->engine->pool, text.start);

    if (JS_IsException(func_val)) {
        return NULL;
    }

    if (conf->engine->precompiled == NULL) {
        conf->engine->precompiled = njs_arr_create(conf->engine->pool, 4,
                                                  sizeof(ngx_js_code_entry_t));
        if (conf->engine->precompiled == NULL) {
            JS_FreeValue(cx, func_val);
            JS_ThrowOutOfMemory(cx);
            return NULL;
        }
    }

    pc = njs_arr_add(conf->engine->precompiled);
    if (pc == NULL) {
        JS_FreeValue(cx, func_val);
        JS_ThrowOutOfMemory(cx);
        return NULL;
    }

    pc->code = JS_WriteObject(cx, &pc->code_size, func_val,
                              JS_WRITE_OBJ_BYTECODE);
    if (pc->code == NULL) {
        JS_FreeValue(cx, func_val);
        JS_ThrowInternalError(cx, "could not write module bytecode");
        return NULL;
    }

    m = JS_VALUE_GET_PTR(func_val);
    JS_FreeValue(cx, func_val);

    return m;
}


static int
ngx_qjs_unhandled_rejection(ngx_js_ctx_t *ctx)
{
    size_t                      len;
    uint32_t                    i;
    JSContext                  *cx;
    const char                 *str;
    ngx_js_rejected_promise_t  *rejected_promise;

    if (ctx->rejected_promises == NULL
        || ctx->rejected_promises->items == 0)
    {
        return 0;
    }

    cx = ctx->engine->u.qjs.ctx;
    rejected_promise = ctx->rejected_promises->start;

    str = JS_ToCStringLen(cx, &len, ngx_qjs_arg(rejected_promise->message));
    if (njs_slow_path(str == NULL)) {
        return -1;
    }

    JS_ThrowTypeError(cx, "unhandled promise rejection: %*s", (int) len, str);
    JS_FreeCString(cx, str);

    for (i = 0; i < ctx->rejected_promises->items; i++) {
        JS_FreeValue(cx, ngx_qjs_arg(rejected_promise[i].promise));
        JS_FreeValue(cx, ngx_qjs_arg(rejected_promise[i].message));
    }

    njs_arr_destroy(ctx->rejected_promises);
    ctx->rejected_promises = NULL;

    return 1;
}


static void
ngx_qjs_rejection_tracker(JSContext *cx, JSValueConst promise,
    JSValueConst reason, JS_BOOL is_handled, void *opaque)
{
    void                       *promise_obj;
    uint32_t                    i, length;
    ngx_js_ctx_t               *ctx;
    ngx_js_rejected_promise_t  *rejected_promise;

    ctx = opaque;

    if (is_handled && ctx->rejected_promises != NULL) {
        rejected_promise = ctx->rejected_promises->start;
        length = ctx->rejected_promises->items;

        promise_obj = JS_VALUE_GET_PTR(promise);

        for (i = 0; i < length; i++) {
            if (JS_VALUE_GET_PTR(ngx_qjs_arg(rejected_promise[i].promise))
                == promise_obj)
            {
                JS_FreeValue(cx, ngx_qjs_arg(rejected_promise[i].promise));
                JS_FreeValue(cx, ngx_qjs_arg(rejected_promise[i].message));
                njs_arr_remove(ctx->rejected_promises, &rejected_promise[i]);

                break;
            }
        }

        return;
    }

    if (ctx->rejected_promises == NULL) {
        if (ctx->engine == NULL) {
            /* Do not track rejections during eval stage. The exception
             * is lifted by the ngx_qjs_clone() function manually. */
            return;
        }

        ctx->rejected_promises = njs_arr_create(ctx->engine->pool, 4,
                                            sizeof(ngx_js_rejected_promise_t));
        if (ctx->rejected_promises == NULL) {
            return;
        }
    }

    rejected_promise = njs_arr_add(ctx->rejected_promises);
    if (rejected_promise == NULL) {
        return;
    }

    ngx_qjs_arg(rejected_promise->promise) = JS_DupValue(cx, promise);
    ngx_qjs_arg(rejected_promise->message) = JS_DupValue(cx, reason);
}


static JSModuleDef *
ngx_qjs_core_init(JSContext *cx, const char *name)
{
    int           ret;
    JSValue       global_obj, proto, obj;
    JSModuleDef  *m;

    if (!JS_IsRegisteredClass(JS_GetRuntime(cx),
                              NGX_QJS_CLASS_ID_CONSOLE))
    {
        if (JS_NewClass(JS_GetRuntime(cx), NGX_QJS_CLASS_ID_CONSOLE,
                        &ngx_qjs_console_class) < 0)
        {
            return NULL;
        }

        proto = JS_NewObject(cx);
        if (JS_IsException(proto)) {
            return NULL;
        }

        JS_SetPropertyFunctionList(cx, proto, ngx_qjs_ext_console,
                                   njs_nitems(ngx_qjs_ext_console));

        JS_SetClassProto(cx, NGX_QJS_CLASS_ID_CONSOLE, proto);
    }

    obj = JS_NewObject(cx);
    if (JS_IsException(obj)) {
        return NULL;
    }

    JS_SetPropertyFunctionList(cx, obj, ngx_qjs_ext_ngx,
                               njs_nitems(ngx_qjs_ext_ngx));

    global_obj = JS_GetGlobalObject(cx);

    JS_SetPropertyFunctionList(cx, global_obj, ngx_qjs_ext_global,
                               njs_nitems(ngx_qjs_ext_global));

    ret = JS_SetPropertyStr(cx, global_obj, "ngx", obj);
    if (ret < 0) {
        JS_FreeValue(cx, global_obj);
        return NULL;
    }

    obj = JS_NewObjectClass(cx, NGX_QJS_CLASS_ID_CONSOLE);
    if (JS_IsException(obj)) {
        JS_FreeValue(cx, global_obj);
        return NULL;
    }

    JS_SetOpaque(obj, (void *) 1);

    ret = JS_SetPropertyStr(cx, global_obj, "console", obj);
    if (ret < 0) {
        JS_FreeValue(cx, global_obj);
        return NULL;
    }

    JS_FreeValue(cx, global_obj);

    m = JS_NewCModule(cx, name, NULL);
    if (m == NULL) {
        return NULL;
    }

    return m;
}

#endif


ngx_int_t
ngx_js_call(njs_vm_t *vm, njs_function_t *func, njs_opaque_value_t *args,
    njs_uint_t nargs)
{
    njs_int_t          ret;
    ngx_str_t          exception;
    ngx_connection_t  *c;

    ret = njs_vm_call(vm, func, njs_value_arg(args), nargs);
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
ngx_js_ctx_init(ngx_js_ctx_t *ctx, ngx_log_t *log)
{
    ctx->log = log;
    ctx->event_id = 0;
    njs_rbtree_init(&ctx->waiting_events, ngx_js_event_rbtree_compare);
}


void
ngx_js_ctx_destroy(ngx_js_ctx_t *ctx, ngx_js_loc_conf_t *conf)
{
    ctx->engine->destroy(ctx->engine, ctx, conf);
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
    char              *p;
    ngx_int_t          lvl;
    njs_str_t          msg;
    njs_uint_t         n, level;
    ngx_connection_t  *c;

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

    c = ngx_external_connection(vm, p);

    for (; n < nargs; n++) {
        if (njs_vm_value_dump(vm, &msg, njs_argument(args, n), 1,
                              !!(magic & NGX_JS_LOG_DUMP))
            == NJS_ERROR)
        {
            return NJS_ERROR;
        }

        ngx_js_logger(c, level, msg.start, msg.length);
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

    vm = event->ctx;

    rc = ngx_js_call(vm, njs_value_function(njs_value_arg(&event->function)),
                     event->args, event->nargs);

    ctx = ngx_external_ctx(vm, njs_vm_external_ptr(vm));
    ngx_js_del_event(ctx, event);

    ngx_external_event_finalize(vm)(njs_vm_external_ptr(vm), rc);
}


static void
ngx_js_clear_timer(ngx_js_event_t *event)
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

    event->ctx = vm;
    njs_value_assign(&event->function, njs_argument(args, 1));
    event->nargs = nargs;
    event->args = (njs_opaque_value_t *) &event[1];
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
    u_char            *p;
    va_list            args;
    ngx_connection_t  *c;
    u_char             buf[NGX_MAX_ERROR_STR];

    va_start(args, fmt);
    p = njs_vsprintf(buf, buf + sizeof(buf), fmt, args);
    va_end(args);

    if (external != NULL) {
        c = ngx_external_connection(vm, external);

    } else {
        c = NULL;
    }

    ngx_js_logger(c, level, buf, p - buf);
}


void
ngx_js_logger(ngx_connection_t *c, ngx_uint_t level, const u_char *start,
    size_t length)
{
    ngx_log_t           *log;
    ngx_log_handler_pt   handler;

    handler = NULL;

    if (c != NULL) {
        log =  c->log;
        handler = log->handler;
        log->handler = NULL;

    } else {

        /* Logger was called during init phase. */

        log = ngx_cycle->log;
    }

    ngx_log_error(level, log, 0, "js: %*s", length, start);

    if (c != NULL) {
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
ngx_js_engine(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    char  *p = conf;

    ngx_str_t           *value;
    ngx_uint_t          *type, m;
    ngx_conf_bitmask_t  *mask;

    type = (size_t *) (p + cmd->offset);
    if (*type != NGX_CONF_UNSET_UINT) {
        return "is duplicate";
    }

    value = cf->args->elts;
    mask = cmd->post;

    for (m = 0; mask[m].name.len != 0; m++) {

        if (mask[m].name.len != value[1].len
            || ngx_strcasecmp(mask[m].name.data, value[1].data) != 0)
        {
            continue;
        }

        *type = mask[m].mask;

        break;
    }

    if (mask[m].name.len == 0) {
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                           "invalid value \"%s\"", value[1].data);

        return NGX_CONF_ERROR;
    }

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
        && conf->type == prev->type
        && conf->paths == NGX_CONF_UNSET_PTR
        && conf->preload_objects == NGX_CONF_UNSET_PTR)
    {
        if (prev->engine != NULL) {
            conf->preload_objects = prev->preload_objects;
            conf->imports = prev->imports;
            conf->type = prev->type;
            conf->paths = prev->paths;
            conf->engine = prev->engine;

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
            if (njs_value_ptr(njs_value_arg(&rejected_promise[i].promise))
                == promise_obj)
            {
                njs_arr_remove(ctx->rejected_promises, &rejected_promise[i]);

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

    njs_value_assign(&rejected_promise->promise, promise);
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
ngx_js_set_cwd(njs_mp_t *mp, ngx_js_loc_conf_t *conf, njs_str_t *path)
{
    ngx_str_t  cwd;

    ngx_js_file_dirname(path, &cwd);

    conf->cwd.data = njs_mp_alloc(mp, cwd.len);
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

    errno = 0;
    ret = ngx_js_module_lookup(conf, &info);
    if (njs_slow_path(ret != NJS_OK)) {
        if (errno != 0) {
            njs_vm_ref_error(vm, "Cannot load module \"%V\" (%s:%s)", name,
                             ngx_js_errno_string(errno), strerror(errno));
        }

        return NULL;
    }

    ret = ngx_js_module_read(njs_vm_memory_pool(vm), info.fd, &text);

    (void) close(info.fd);

    if (ret != NJS_OK) {
        njs_vm_internal_error(vm, "while reading \"%V\" module", &info.file);
        return NULL;
    }

    prev_cwd = conf->cwd;

    ret = ngx_js_set_cwd(njs_vm_memory_pool(vm), conf, &info.file);
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
    ngx_engine_opts_t *options)
{
    u_char               *start, *p;
    size_t                size;
    ngx_str_t            *m, file;
    ngx_uint_t            i;
    ngx_pool_cleanup_t   *cln;
    ngx_js_named_path_t  *import;

    if (ngx_set_environment(cf->cycle, NULL) == NULL) {
        return NGX_ERROR;
    }

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
    options->conf = conf;

    conf->engine = ngx_create_engine(options);
    if (conf->engine == NULL) {
        ngx_log_error(NGX_LOG_EMERG, cf->log, 0, "failed to create js VM");
        return NGX_ERROR;
    }

    cln = ngx_pool_cleanup_add(cf->pool, 0);
    if (cln == NULL) {
        return NGX_ERROR;
    }

    cln->handler = ngx_js_cleanup_vm;
    cln->data = conf;

    if (conf->paths != NGX_CONF_UNSET_PTR) {
        m = conf->paths->elts;

        for (i = 0; i < conf->paths->nelts; i++) {
            if (ngx_conf_full_name(cf->cycle, &m[i], 1) != NGX_OK) {
                return NGX_ERROR;
            }
        }
    }

    return conf->engine->compile(conf, cf->log, start, size);
}


static njs_int_t
ngx_js_unhandled_rejection(ngx_js_ctx_t *ctx)
{
    njs_vm_t                   *vm;
    njs_int_t                   ret;
    njs_str_t                   message;
    ngx_js_rejected_promise_t  *rejected_promise;

    if (ctx->rejected_promises == NULL
        || ctx->rejected_promises->items == 0)
    {
        return 0;
    }

    vm = ctx->engine->u.njs.vm;
    rejected_promise = ctx->rejected_promises->start;

    ret = njs_vm_value_to_string(vm, &message,
                                 njs_value_arg(&rejected_promise->message));
    if (njs_slow_path(ret != NJS_OK)) {
        return -1;
    }

    njs_vm_error(vm, "unhandled promise rejection: %V", &message);

    njs_arr_destroy(ctx->rejected_promises);
    ctx->rejected_promises = NULL;

    return 1;
}


static void
ngx_js_cleanup_vm(void *data)
{
    ngx_js_loc_conf_t  *jscf = data;

    jscf->engine->destroy(jscf->engine, NULL, NULL);

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

    /*
     * set by ngx_pcalloc():
     *
     *     conf->reuse_queue = NULL;
     */

    conf->paths = NGX_CONF_UNSET_PTR;
    conf->type = NGX_CONF_UNSET_UINT;
    conf->imports = NGX_CONF_UNSET_PTR;
    conf->preload_objects = NGX_CONF_UNSET_PTR;

    conf->reuse = NGX_CONF_UNSET_SIZE;
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

    ngx_conf_merge_uint_value(conf->type, prev->type, NGX_ENGINE_NJS);
    ngx_conf_merge_msec_value(conf->timeout, prev->timeout, 60000);
    ngx_conf_merge_size_value(conf->reuse, prev->reuse, 128);
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


#define ngx_js_errno_case(e)                                                \
    case e:                                                                 \
        return #e;


const char*
ngx_js_errno_string(int errnum)
{
    switch (errnum) {
#ifdef EACCES
    ngx_js_errno_case(EACCES);
#endif

#ifdef EADDRINUSE
    ngx_js_errno_case(EADDRINUSE);
#endif

#ifdef EADDRNOTAVAIL
    ngx_js_errno_case(EADDRNOTAVAIL);
#endif

#ifdef EAFNOSUPPORT
    ngx_js_errno_case(EAFNOSUPPORT);
#endif

#ifdef EAGAIN
    ngx_js_errno_case(EAGAIN);
#endif

#ifdef EWOULDBLOCK
#if EAGAIN != EWOULDBLOCK
    ngx_js_errno_case(EWOULDBLOCK);
#endif
#endif

#ifdef EALREADY
    ngx_js_errno_case(EALREADY);
#endif

#ifdef EBADF
    ngx_js_errno_case(EBADF);
#endif

#ifdef EBADMSG
    ngx_js_errno_case(EBADMSG);
#endif

#ifdef EBUSY
    ngx_js_errno_case(EBUSY);
#endif

#ifdef ECANCELED
    ngx_js_errno_case(ECANCELED);
#endif

#ifdef ECHILD
    ngx_js_errno_case(ECHILD);
#endif

#ifdef ECONNABORTED
    ngx_js_errno_case(ECONNABORTED);
#endif

#ifdef ECONNREFUSED
    ngx_js_errno_case(ECONNREFUSED);
#endif

#ifdef ECONNRESET
    ngx_js_errno_case(ECONNRESET);
#endif

#ifdef EDEADLK
    ngx_js_errno_case(EDEADLK);
#endif

#ifdef EDESTADDRREQ
    ngx_js_errno_case(EDESTADDRREQ);
#endif

#ifdef EDOM
    ngx_js_errno_case(EDOM);
#endif

#ifdef EDQUOT
    ngx_js_errno_case(EDQUOT);
#endif

#ifdef EEXIST
    ngx_js_errno_case(EEXIST);
#endif

#ifdef EFAULT
    ngx_js_errno_case(EFAULT);
#endif

#ifdef EFBIG
    ngx_js_errno_case(EFBIG);
#endif

#ifdef EHOSTUNREACH
    ngx_js_errno_case(EHOSTUNREACH);
#endif

#ifdef EIDRM
    ngx_js_errno_case(EIDRM);
#endif

#ifdef EILSEQ
    ngx_js_errno_case(EILSEQ);
#endif

#ifdef EINPROGRESS
    ngx_js_errno_case(EINPROGRESS);
#endif

#ifdef EINTR
    ngx_js_errno_case(EINTR);
#endif

#ifdef EINVAL
    ngx_js_errno_case(EINVAL);
#endif

#ifdef EIO
    ngx_js_errno_case(EIO);
#endif

#ifdef EISCONN
    ngx_js_errno_case(EISCONN);
#endif

#ifdef EISDIR
    ngx_js_errno_case(EISDIR);
#endif

#ifdef ELOOP
    ngx_js_errno_case(ELOOP);
#endif

#ifdef EMFILE
    ngx_js_errno_case(EMFILE);
#endif

#ifdef EMLINK
    ngx_js_errno_case(EMLINK);
#endif

#ifdef EMSGSIZE
    ngx_js_errno_case(EMSGSIZE);
#endif

#ifdef EMULTIHOP
    ngx_js_errno_case(EMULTIHOP);
#endif

#ifdef ENAMETOOLONG
    ngx_js_errno_case(ENAMETOOLONG);
#endif

#ifdef ENETDOWN
    ngx_js_errno_case(ENETDOWN);
#endif

#ifdef ENETRESET
    ngx_js_errno_case(ENETRESET);
#endif

#ifdef ENETUNREACH
    ngx_js_errno_case(ENETUNREACH);
#endif

#ifdef ENFILE
    ngx_js_errno_case(ENFILE);
#endif

#ifdef ENOBUFS
    ngx_js_errno_case(ENOBUFS);
#endif

#ifdef ENODATA
    ngx_js_errno_case(ENODATA);
#endif

#ifdef ENODEV
    ngx_js_errno_case(ENODEV);
#endif

#ifdef ENOENT
    ngx_js_errno_case(ENOENT);
#endif

#ifdef ENOEXEC
    ngx_js_errno_case(ENOEXEC);
#endif

#ifdef ENOLINK
    ngx_js_errno_case(ENOLINK);
#endif

#ifdef ENOLCK
#if ENOLINK != ENOLCK
    ngx_js_errno_case(ENOLCK);
#endif
#endif

#ifdef ENOMEM
    ngx_js_errno_case(ENOMEM);
#endif

#ifdef ENOMSG
    ngx_js_errno_case(ENOMSG);
#endif

#ifdef ENOPROTOOPT
    ngx_js_errno_case(ENOPROTOOPT);
#endif

#ifdef ENOSPC
    ngx_js_errno_case(ENOSPC);
#endif

#ifdef ENOSR
    ngx_js_errno_case(ENOSR);
#endif

#ifdef ENOSTR
    ngx_js_errno_case(ENOSTR);
#endif

#ifdef ENOSYS
    ngx_js_errno_case(ENOSYS);
#endif

#ifdef ENOTCONN
    ngx_js_errno_case(ENOTCONN);
#endif

#ifdef ENOTDIR
    ngx_js_errno_case(ENOTDIR);
#endif

#ifdef ENOTEMPTY
#if ENOTEMPTY != EEXIST
    ngx_js_errno_case(ENOTEMPTY);
#endif
#endif

#ifdef ENOTSOCK
    ngx_js_errno_case(ENOTSOCK);
#endif

#ifdef ENOTSUP
    ngx_js_errno_case(ENOTSUP);
#else
#ifdef EOPNOTSUPP
    ngx_js_errno_case(EOPNOTSUPP);
#endif
#endif

#ifdef ENOTTY
    ngx_js_errno_case(ENOTTY);
#endif

#ifdef ENXIO
    ngx_js_errno_case(ENXIO);
#endif

#ifdef EOVERFLOW
    ngx_js_errno_case(EOVERFLOW);
#endif

#ifdef EPERM
    ngx_js_errno_case(EPERM);
#endif

#ifdef EPIPE
    ngx_js_errno_case(EPIPE);
#endif

#ifdef EPROTO
    ngx_js_errno_case(EPROTO);
#endif

#ifdef EPROTONOSUPPORT
    ngx_js_errno_case(EPROTONOSUPPORT);
#endif

#ifdef EPROTOTYPE
    ngx_js_errno_case(EPROTOTYPE);
#endif

#ifdef ERANGE
    ngx_js_errno_case(ERANGE);
#endif

#ifdef EROFS
    ngx_js_errno_case(EROFS);
#endif

#ifdef ESPIPE
    ngx_js_errno_case(ESPIPE);
#endif

#ifdef ESRCH
    ngx_js_errno_case(ESRCH);
#endif

#ifdef ESTALE
    ngx_js_errno_case(ESTALE);
#endif

#ifdef ETIME
    ngx_js_errno_case(ETIME);
#endif

#ifdef ETIMEDOUT
    ngx_js_errno_case(ETIMEDOUT);
#endif

#ifdef ETXTBSY
    ngx_js_errno_case(ETXTBSY);
#endif

#ifdef EXDEV
    ngx_js_errno_case(EXDEV);
#endif

    default:
        break;
    }

    return "UNKNOWN CODE";
}


ngx_js_queue_t *
ngx_js_queue_create(ngx_pool_t *pool, ngx_uint_t capacity)
{
    ngx_js_queue_t  *queue;

    queue = ngx_pcalloc(pool, sizeof(ngx_js_queue_t));
    if (queue == NULL) {
        return NULL;
    }

    queue->data = ngx_pcalloc(pool, sizeof(void *) * capacity);
    if (queue->data == NULL) {
        return NULL;
    }

    queue->head = 0;
    queue->tail = 0;
    queue->size = 0;
    queue->capacity = capacity;

    return queue;
}


ngx_int_t
ngx_js_queue_push(ngx_js_queue_t *queue, void *item)
{
    if (queue->size >= queue->capacity) {
        return NGX_ERROR;
    }

    queue->data[queue->tail] = item;
    queue->tail = (queue->tail + 1) % queue->capacity;
    queue->size++;

    return NGX_OK;
}


void *
ngx_js_queue_pop(ngx_js_queue_t *queue)
{
    void *item;

    if (queue->size == 0) {
        return NULL;
    }

    item = queue->data[queue->head];
    queue->head = (queue->head + 1) % queue->capacity;
    queue->size--;

    return item;
}
