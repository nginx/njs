
/*
 * Copyright (C) Roman Arutyunyan
 * Copyright (C) Dmitry Volyntsev
 * Copyright (C) NGINX, Inc.
 */


#include <ngx_config.h>
#include <ngx_core.h>
#include "ngx_js.h"


ngx_int_t
ngx_js_call(njs_vm_t *vm, ngx_str_t *fname, njs_opaque_value_t *value,
    ngx_log_t *log)
{
    njs_str_t        name, exception;
    njs_function_t  *func;

    name.start = fname->data;
    name.length = fname->len;

    func = njs_vm_function(vm, &name);
    if (func == NULL) {
        ngx_log_error(NGX_LOG_ERR, log, 0,
                      "js function \"%V\" not found", fname);
        return NGX_ERROR;
    }

    if (njs_vm_call(vm, func, njs_value_arg(value), 1) != NJS_OK) {
        njs_vm_retval_string(vm, &exception);

        ngx_log_error(NGX_LOG_ERR, log, 0,
                      "js exception: %*s", exception.length, exception.start);

        return NGX_ERROR;
    }

    if (njs_vm_pending(vm)) {
        return NGX_AGAIN;
    }

    return NGX_OK;
}


njs_int_t
ngx_js_ext_string(njs_vm_t *vm, njs_object_prop_t *prop, njs_value_t *value,
    njs_value_t *setval, njs_value_t *retval)
{
    char       *p;
    ngx_str_t  *field;

    p = njs_vm_external(vm, value);
    if (p == NULL) {
        njs_value_undefined_set(retval);
        return NJS_DECLINED;
    }

    field = (ngx_str_t *) (p + njs_vm_prop_magic32(prop));

    return njs_vm_value_string_set(vm, retval, field->data, field->len);
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
        if (njs_vm_value_to_string(vm, str, value) == NJS_ERROR) {
            return NGX_ERROR;
        }

    } else {
        str->start = NULL;
        str->length = 0;
    }

    return NGX_OK;
}
