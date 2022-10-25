
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) NGINX, Inc.
 */


#include <njs_main.h>


static njs_int_t
njs_boolean_constructor(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t unused)
{
    const njs_value_t   *value;
    njs_object_value_t  *object;

    if (nargs == 1) {
        value = &njs_value_false;

    } else {
        value = njs_is_true(&args[1]) ? &njs_value_true : &njs_value_false;
    }

    if (vm->top_frame->ctor) {
        object = njs_object_value_alloc(vm, NJS_OBJ_TYPE_BOOLEAN, 0, value);
        if (njs_slow_path(object == NULL)) {
            return NJS_ERROR;
        }

        njs_set_object_value(&vm->retval, object);

    } else {
        vm->retval = *value;
    }

    return NJS_OK;
}


static const njs_object_prop_t  njs_boolean_constructor_properties[] =
{
    {
        .type = NJS_PROPERTY,
        .name = njs_string("name"),
        .u.value = njs_string("Boolean"),
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("length"),
        .u.value = njs_value(NJS_NUMBER, 1, 1.0),
        .configurable = 1,
    },

    NJS_DECLARE_PROP_HANDLER("prototype", njs_object_prototype_create, 0, 0, 0),
};


const njs_object_init_t  njs_boolean_constructor_init = {
    njs_boolean_constructor_properties,
    njs_nitems(njs_boolean_constructor_properties),
};


static njs_int_t
njs_boolean_prototype_value_of(njs_vm_t *vm, njs_value_t *args,
    njs_uint_t nargs, njs_index_t unused)
{
    njs_value_t  *value;

    value = &args[0];

    if (value->type != NJS_BOOLEAN) {

        if (njs_is_object_boolean(value)) {
            value = njs_object_value(value);

        } else {
            njs_type_error(vm, "unexpected value type:%s",
                           njs_type_string(value->type));
            return NJS_ERROR;
        }
    }

    vm->retval = *value;

    return NJS_OK;
}


static njs_int_t
njs_boolean_prototype_to_string(njs_vm_t *vm, njs_value_t *args,
    njs_uint_t nargs, njs_index_t unused)
{
    njs_value_t  *value;

    value = &args[0];

    if (value->type != NJS_BOOLEAN) {

        if (njs_is_object_boolean(value)) {
            value = njs_object_value(value);

        } else {
            njs_type_error(vm, "unexpected value type:%s",
                           njs_type_string(value->type));
            return NJS_ERROR;
        }
    }

    vm->retval = njs_is_true(value) ? njs_string_true : njs_string_false;

    return NJS_OK;
}


static const njs_object_prop_t  njs_boolean_prototype_properties[] =
{
    NJS_DECLARE_PROP_HANDLER("__proto__", njs_primitive_prototype_get_proto,
                             0, 0, NJS_OBJECT_PROP_VALUE_CW),

    NJS_DECLARE_PROP_HANDLER("constructor",
                             njs_object_prototype_create_constructor,
                             0, 0, NJS_OBJECT_PROP_VALUE_CW),

    NJS_DECLARE_PROP_NATIVE("valueOf", njs_boolean_prototype_value_of, 0, 0),

    NJS_DECLARE_PROP_NATIVE("toString", njs_boolean_prototype_to_string, 0, 0),
};


const njs_object_init_t  njs_boolean_prototype_init = {
    njs_boolean_prototype_properties,
    njs_nitems(njs_boolean_prototype_properties),
};


const njs_object_type_init_t  njs_boolean_type_init = {
   .constructor = njs_native_ctor(njs_boolean_constructor, 1, 0),
   .constructor_props = &njs_boolean_constructor_init,
   .prototype_props = &njs_boolean_prototype_init,
   .prototype_value = { .object_value = {
                            .value = njs_value(NJS_BOOLEAN, 0, 0.0),
                            .object = { .type = NJS_OBJECT_VALUE } }
                      },
};
