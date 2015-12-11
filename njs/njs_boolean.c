
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) NGINX, Inc.
 */

#include <nxt_types.h>
#include <nxt_clang.h>
#include <nxt_stub.h>
#include <nxt_array.h>
#include <nxt_lvlhsh.h>
#include <nxt_mem_cache_pool.h>
#include <njscript.h>
#include <njs_vm.h>
#include <njs_boolean.h>
#include <njs_object.h>
#include <njs_function.h>


njs_ret_t
njs_boolean_constructor(njs_vm_t *vm, njs_param_t *param)
{
    njs_object_t       *object;
    const njs_value_t  *value;

    if (param->nargs == 0) {
        value = &njs_value_false;

    } else {
        value = njs_is_true(&param->args[0]) ? &njs_value_true:
                                               &njs_value_false;
    }

    if (vm->frame->ctor) {
        object = njs_object_value_alloc(vm, value, value->type);
        if (nxt_slow_path(object == NULL)) {
            return NXT_ERROR;
        }

        vm->retval.data.u.object = object;
        vm->retval.type = NJS_OBJECT_BOOLEAN;
        vm->retval.data.truth = 1;

    } else {
        vm->retval = *value;
    }

    return NXT_OK;
}


static const njs_object_prop_t  njs_boolean_constructor_properties[] =
{
    /* Boolean.name == "Boolean". */
    { njs_string("Boolean"),
      njs_string("name"),
      NJS_PROPERTY, 0, 0, 0, },

    /* Boolean.length == 1. */
    { njs_value(NJS_NUMBER, 1, 1.0),
      njs_string("length"),
      NJS_PROPERTY, 0, 0, 0, },

    /* Boolean.prototype. */
    { njs_native_getter(njs_object_prototype_create),
      njs_string("prototype"),
      NJS_NATIVE_GETTER, 0, 0, 0, },
};


const njs_object_init_t  njs_boolean_constructor_init = {
     njs_boolean_constructor_properties,
     nxt_nitems(njs_boolean_constructor_properties),
};


static njs_ret_t
njs_boolean_prototype_value_of(njs_vm_t *vm, njs_param_t *param)
{
    njs_value_t  *value;

    value = param->object;

    if (value->type != NJS_BOOLEAN) {

        if (value->type == NJS_OBJECT_BOOLEAN) {
            value = &value->data.u.object_value->value;

        } else {
            vm->exception = &njs_exception_type_error;
            return NXT_ERROR;
        }
    }

    vm->retval = *value;

    return NXT_OK;
}


static njs_ret_t
njs_boolean_prototype_to_string(njs_vm_t *vm, njs_param_t *param)
{
    njs_value_t  *value;

    value = param->object;

    if (value->type != NJS_BOOLEAN) {

        if (value->type == NJS_OBJECT_BOOLEAN) {
            value = &value->data.u.object_value->value;

        } else {
            vm->exception = &njs_exception_type_error;
            return NXT_ERROR;
        }
    }

    vm->retval = njs_is_true(value) ? njs_string_true : njs_string_false;

    return NXT_OK;
}


static const njs_object_prop_t  njs_boolean_prototype_properties[] =
{
    { njs_native_getter(njs_primitive_prototype_get_proto),
      njs_string("__proto__"),
      NJS_NATIVE_GETTER, 0, 0, 0, },

    { njs_native_function(njs_boolean_prototype_value_of, 0),
      njs_string("valueOf"),
      NJS_METHOD, 0, 0, 0, },

    { njs_native_function(njs_boolean_prototype_to_string, 0),
      njs_string("toString"),
      NJS_METHOD, 0, 0, 0, },
};


const njs_object_init_t  njs_boolean_prototype_init = {
     njs_boolean_prototype_properties,
     nxt_nitems(njs_boolean_prototype_properties),
};
