
/*
 * Copyright (C) Roman Arutyunyan
 * Copyright (C) Dmitry Volyntsev
 * Copyright (C) NGINX, Inc.
 */


#include <ngx_config.h>
#include <ngx_core.h>
#include "ngx_js.h"
#include "ngx_js_fetch.h"


njs_int_t ngx_js_ext_conf_prefix(njs_vm_t *vm, njs_object_prop_t *prop,
    njs_value_t *value, njs_value_t *setval, njs_value_t *retval);


extern njs_module_t  njs_webcrypto_module;


static njs_external_t  ngx_js_ext_core[] = {

    {
        .flags = NJS_EXTERN_PROPERTY,
        .name.string = njs_str("conf_prefix"),
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
        .flags = NJS_EXTERN_PROPERTY,
        .name.string = njs_str("WARN"),
        .u.property = {
            .handler = ngx_js_ext_constant,
            .magic32 = NGX_LOG_WARN,
            .magic16 = NGX_JS_NUMBER,
        }
    },

};


njs_module_t *njs_js_addon_modules[] = {
    &njs_webcrypto_module,
    NULL,
};


ngx_int_t
ngx_js_call(njs_vm_t *vm, ngx_str_t *fname, ngx_log_t *log,
    njs_opaque_value_t *args, njs_uint_t nargs)
{
    njs_int_t        ret;
    njs_str_t        name;
    ngx_str_t        exception;
    njs_function_t  *func;

    name.start = fname->data;
    name.length = fname->len;

    func = njs_vm_function(vm, &name);
    if (func == NULL) {
        ngx_log_error(NGX_LOG_ERR, log, 0,
                      "js function \"%V\" not found", fname);
        return NGX_ERROR;
    }

    ret = njs_vm_call(vm, func, njs_value_arg(args), nargs);
    if (ret == NJS_ERROR) {
        ngx_js_retval(vm, NULL, &exception);

        ngx_log_error(NGX_LOG_ERR, log, 0,
                      "js exception: %V", &exception);

        return NGX_ERROR;
    }

    ret = njs_vm_run(vm);
    if (ret == NJS_ERROR) {
        ngx_js_retval(vm, NULL, &exception);

        ngx_log_error(NGX_LOG_ERR, log, 0,
                      "js exception: %V", &exception);

        return NGX_ERROR;
    }

    return (ret == NJS_AGAIN) ? NGX_AGAIN : NGX_OK;
}


ngx_int_t
ngx_js_retval(njs_vm_t *vm, njs_opaque_value_t *retval, ngx_str_t *s)
{
    njs_int_t  ret;
    njs_str_t  str;

    if (retval != NULL && njs_value_is_valid(njs_value_arg(retval))) {
        ret = njs_vm_value_string(vm, &str, njs_value_arg(retval));

    } else {
        ret = njs_vm_retval_string(vm, &str);
    }

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


ngx_int_t
ngx_js_core_init(njs_vm_t *vm, ngx_log_t *log)
{
    ngx_int_t           rc;
    njs_int_t           ret, proto_id;
    njs_str_t           name;
    njs_opaque_value_t  value;

    rc = ngx_js_fetch_init(vm, log);
    if (rc != NGX_OK) {
        return NGX_ERROR;
    }

    proto_id = njs_vm_external_prototype(vm, ngx_js_ext_core,
                                         njs_nitems(ngx_js_ext_core));
    if (proto_id < 0) {
        ngx_log_error(NGX_LOG_EMERG, log, 0, "failed to add js core proto");
        return NGX_ERROR;
    }

    ret = njs_vm_external_create(vm, njs_value_arg(&value), proto_id, NULL, 1);
    if (njs_slow_path(ret != NJS_OK)) {
        ngx_log_error(NGX_LOG_EMERG, log, 0,
                      "njs_vm_external_create() failed\n");
        return NGX_ERROR;
    }

    name.length = 3;
    name.start = (u_char *) "ngx";

    ret = njs_vm_bind(vm, &name, njs_value_arg(&value), 1);
    if (njs_slow_path(ret != NJS_OK)) {
        ngx_log_error(NGX_LOG_EMERG, log, 0, "njs_vm_bind() failed\n");
        return NGX_ERROR;
    }

    return NGX_OK;
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

    return njs_vm_value_string_set(vm, retval, field->data, field->len);
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
ngx_js_ext_conf_prefix(njs_vm_t *vm, njs_object_prop_t *prop,
    njs_value_t *value, njs_value_t *setval, njs_value_t *retval)
{
    return njs_vm_value_string_set(vm, retval, ngx_cycle->conf_prefix.data,
                                   ngx_cycle->conf_prefix.len);
}


njs_int_t
ngx_js_ext_log(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t level)
{
    char                *p;
    ngx_int_t            lvl;
    njs_str_t            msg;
    njs_value_t         *value;
    ngx_connection_t    *c;
    ngx_log_handler_pt   handler;

    p = njs_vm_external(vm, NJS_PROTO_ID_ANY, njs_argument(args, 0));
    if (p == NULL) {
        njs_vm_error(vm, "\"this\" is not an external");
        return NJS_ERROR;
    }

    value =  njs_arg(args, nargs, (level != 0) ? 1 : 2);

    if (level == 0) {
        if (ngx_js_integer(vm, njs_arg(args, nargs, 1), &lvl) != NGX_OK) {
            return NJS_ERROR;
        }

        level = lvl;
    }

    if (ngx_js_string(vm, value, &msg) != NGX_OK) {
        return NJS_ERROR;
    }

    c = ngx_external_connection(vm, p);
    handler = c->log->handler;
    c->log->handler = NULL;

    ngx_log_error((ngx_uint_t) level, c->log, 0, "js: %*s",
                  msg.length, msg.start);

    c->log->handler = handler;

    njs_value_undefined_set(njs_vm_retval(vm));

    return NJS_OK;
}


void
ngx_js_logger(njs_vm_t *vm, njs_external_ptr_t external, njs_log_level_t level,
    const u_char *start, size_t length)
{
    ngx_connection_t    *c;
    ngx_log_handler_pt   handler;

    c = ngx_external_connection(vm, external);
    handler = c->log->handler;
    c->log->handler = NULL;

    ngx_log_error((ngx_uint_t) level, c->log, 0, "js: %*s", length, start);

    c->log->handler = handler;
}
