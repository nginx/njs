
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) NGINX, Inc.
 */

#include <nxt_auto_config.h>
#include <nxt_types.h>
#include <nxt_clang.h>
#include <nxt_string.h>
#include <nxt_stub.h>
#include <nxt_array.h>
#include <nxt_lvlhsh.h>
#include <nxt_random.h>
#include <nxt_mem_cache_pool.h>
#include <njscript.h>
#include <njs_vm.h>
#include <njs_boolean.h>
#include <njs_object.h>
#include <njs_function.h>
#include <njs_error.h>


njs_ret_t
njs_boolean_constructor(njs_vm_t *vm, njs_value_t *args, nxt_uint_t nargs,
    njs_index_t unused)
{
    njs_object_t       *object;
    const njs_value_t  *value;

    if (nargs == 1) {
        value = &njs_value_false;

    } else {
        value = njs_is_true(&args[1]) ? &njs_value_true : &njs_value_false;
    }

    if (vm->top_frame->ctor) {
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
    {
        .type = NJS_PROPERTY,
        .name = njs_string("name"),
        .value = njs_string("Boolean"),
    },

    /* Boolean.length == 1. */
    {
        .type = NJS_PROPERTY,
        .name = njs_string("length"),
        .value = njs_value(NJS_NUMBER, 1, 1.0),
    },

    /* Boolean.prototype. */
    {
        .type = NJS_NATIVE_GETTER,
        .name = njs_string("prototype"),
        .value = njs_native_getter(njs_object_prototype_create),
    },
};


const njs_object_init_t  njs_boolean_constructor_init = {
    nxt_string("Boolean"),
    njs_boolean_constructor_properties,
    nxt_nitems(njs_boolean_constructor_properties),
};


static njs_ret_t
njs_boolean_prototype_value_of(njs_vm_t *vm, njs_value_t *args,
    nxt_uint_t nargs, njs_index_t unused)
{
    njs_value_t  *value;

    value = &args[0];

    if (value->type != NJS_BOOLEAN) {

        if (value->type == NJS_OBJECT_BOOLEAN) {
            value = &value->data.u.object_value->value;

        } else {
            njs_exception_type_error(vm, NULL, NULL);
            return NXT_ERROR;
        }
    }

    vm->retval = *value;

    return NXT_OK;
}


static njs_ret_t
njs_boolean_prototype_to_string(njs_vm_t *vm, njs_value_t *args,
    nxt_uint_t nargs, njs_index_t unused)
{
    njs_value_t  *value;

    value = &args[0];

    if (value->type != NJS_BOOLEAN) {

        if (value->type == NJS_OBJECT_BOOLEAN) {
            value = &value->data.u.object_value->value;

        } else {
            njs_exception_type_error(vm, NULL, NULL);
            return NXT_ERROR;
        }
    }

    vm->retval = njs_is_true(value) ? njs_string_true : njs_string_false;

    return NXT_OK;
}


static const njs_object_prop_t  njs_boolean_prototype_properties[] =
{
    {
        .type = NJS_NATIVE_GETTER,
        .name = njs_string("__proto__"),
        .value = njs_native_getter(njs_primitive_prototype_get_proto),
    },

    {
        .type = NJS_METHOD,
        .name = njs_string("valueOf"),
        .value = njs_native_function(njs_boolean_prototype_value_of, 0, 0),
    },

    {
        .type = NJS_METHOD,
        .name = njs_string("toString"),
        .value = njs_native_function(njs_boolean_prototype_to_string, 0, 0),
    },
};


const njs_object_init_t  njs_boolean_prototype_init = {
    nxt_string("Boolean"),
    njs_boolean_prototype_properties,
    nxt_nitems(njs_boolean_prototype_properties),
};
