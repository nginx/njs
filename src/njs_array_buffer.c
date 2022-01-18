/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) NGINX, Inc.
 */


#include <njs_main.h>


njs_array_buffer_t *
njs_array_buffer_alloc(njs_vm_t *vm, uint64_t size, njs_bool_t zeroing)
{
    njs_object_t        *proto;
    njs_array_buffer_t  *array;

    if (njs_slow_path(size > UINT32_MAX)) {
        goto overflow;
    }

    array = njs_mp_alloc(vm->mem_pool, sizeof(njs_array_buffer_t));
    if (njs_slow_path(array == NULL)) {
        goto memory_error;
    }

    if (zeroing) {
        array->u.data = njs_mp_zalloc(vm->mem_pool, size);

    } else {
        array->u.data = njs_mp_alloc(vm->mem_pool, size);
    }

    if (njs_slow_path(array->u.data == NULL)) {
        goto memory_error;
    }

    proto = &vm->prototypes[NJS_OBJ_TYPE_ARRAY_BUFFER].object;

    njs_lvlhsh_init(&array->object.hash);
    njs_lvlhsh_init(&array->object.shared_hash);
    array->object.__proto__ = proto;
    array->object.slots = NULL;
    array->object.type = NJS_ARRAY_BUFFER;
    array->object.shared = 0;
    array->object.extensible = 1;
    array->object.error_data = 0;
    array->object.fast_array = 0;
    array->size = size;

    return array;

memory_error:

    njs_memory_error(vm);

    return NULL;

overflow:

    njs_range_error(vm, "Invalid array length");

    return NULL;
}


static njs_int_t
njs_array_buffer_constructor(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t unused)
{
    uint64_t            size;
    njs_int_t           ret;
    njs_value_t         *value;
    njs_array_buffer_t  *array;

    if (!vm->top_frame->ctor) {
        njs_type_error(vm, "Constructor ArrayBuffer requires 'new'");
        return NJS_ERROR;
    }

    size = 0;
    value = njs_arg(args, nargs, 1);

    ret = njs_value_to_index(vm, value, &size);
    if (njs_slow_path(ret != NJS_OK)) {
        return NJS_ERROR;
    }

    array = njs_array_buffer_alloc(vm, size, 1);
    if (njs_slow_path(array == NULL)) {
        return NJS_ERROR;
    }

    njs_set_array_buffer(&vm->retval, array);

    return NJS_OK;
}


static njs_int_t
njs_array_buffer_get_this(njs_vm_t *vm, njs_value_t *args,
    njs_uint_t nargs, njs_index_t unused)
{
    vm->retval = args[0];

    return NJS_OK;
}


static njs_int_t
njs_array_buffer_is_view(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t unused)
{
    njs_set_boolean(&vm->retval, njs_is_typed_array(njs_arg(args, nargs, 1)));

    return NJS_OK;
}


njs_int_t
njs_array_buffer_writable(njs_vm_t *vm, njs_array_buffer_t *buffer)
{
    void  *dst;

    if (!buffer->object.shared) {
        return NJS_OK;
    }

    dst = njs_mp_alloc(vm->mem_pool, buffer->size);
    if (njs_slow_path(dst == NULL)) {
        njs_memory_error(vm);
        return NJS_ERROR;
    }

    memcpy(dst, buffer->u.data, buffer->size);

    buffer->object.shared = 0;
    buffer->u.data = dst;

    return NJS_OK;
}


static const njs_object_prop_t  njs_array_buffer_constructor_properties[] =
{
    {
        .type = NJS_PROPERTY,
        .name = njs_string("name"),
        .value = njs_string("ArrayBuffer"),
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("length"),
        .value = njs_value(NJS_NUMBER, 1, 1.0),
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY_HANDLER,
        .name = njs_string("prototype"),
        .value = njs_prop_handler(njs_object_prototype_create),
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_wellknown_symbol(NJS_SYMBOL_SPECIES),
        .value = njs_value(NJS_INVALID, 1, NAN),
        .getter = njs_native_function(njs_array_buffer_get_this, 0),
        .setter = njs_value(NJS_UNDEFINED, 0, NAN),
        .writable = NJS_ATTRIBUTE_UNSET,
        .configurable = 1,
        .enumerable = 0,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("isView"),
        .value = njs_native_function(njs_array_buffer_is_view, 1),
        .writable = 1,
        .configurable = 1,
    },
};


const njs_object_init_t  njs_array_buffer_constructor_init = {
    njs_array_buffer_constructor_properties,
    njs_nitems(njs_array_buffer_constructor_properties),
};


static njs_int_t
njs_array_buffer_prototype_byte_length(njs_vm_t *vm, njs_value_t *args,
    njs_uint_t nargs, njs_index_t unused)
{
    njs_value_t         *value;
    njs_array_buffer_t  *array;

    value = njs_argument(args, 0);

    if (!njs_is_array_buffer(value)) {
        njs_type_error(vm, "Method ArrayBuffer.prototype.byteLength called "
                       "on incompatible receiver");
        return NJS_ERROR;
    }

    array = njs_array_buffer(value);
    if (njs_slow_path(njs_is_detached_buffer(array))) {
        njs_type_error(vm, "detached buffer");
        return NJS_ERROR;
    }

    njs_set_number(&vm->retval, array->size);

    return NJS_OK;
}


static njs_int_t
njs_array_buffer_prototype_slice(njs_vm_t *vm, njs_value_t *args,
    njs_uint_t nargs, njs_index_t unused)
{
    int64_t             len, start, end;
    njs_int_t           ret;
    njs_value_t         *value;
    njs_array_buffer_t  *this, *buffer;

    value = njs_argument(args, 0);

    if (!njs_is_array_buffer(value)) {
        njs_type_error(vm, "Method ArrayBuffer.prototype.slice called "
                       "on incompatible receiver");
        return NJS_ERROR;
    }

    this = njs_array_buffer(value);
    len  = njs_array_buffer_size(this);
    end  = len;

    value = njs_arg(args, nargs, 1);

    ret = njs_value_to_integer(vm, value, &start);
    if (njs_slow_path(ret != NJS_OK)) {
        return ret;
    }

    value = njs_arg(args, nargs, 2);

    if (!njs_is_undefined(value)) {
        ret = njs_value_to_integer(vm, value, &end);
        if (njs_slow_path(ret != NJS_OK)) {
            return ret;
        }
    }

    buffer = njs_array_buffer_slice(vm, this, start, end);
    if (njs_slow_path(buffer == NULL)) {
        return NJS_ERROR;
    }

    njs_set_array_buffer(&vm->retval, buffer);

    return NJS_OK;
}


static const njs_object_prop_t  njs_array_buffer_prototype_properties[] =
{
    {
        .type = NJS_PROPERTY_HANDLER,
        .name = njs_string("constructor"),
        .value = njs_prop_handler(njs_object_prototype_create_constructor),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("byteLength"),
        .value = njs_value(NJS_INVALID, 1, NAN),
        .getter = njs_native_function(njs_array_buffer_prototype_byte_length,
                                      0),
        .setter = njs_value(NJS_UNDEFINED, 0, NAN),
        .writable = NJS_ATTRIBUTE_UNSET,
        .configurable = 1,
        .enumerable = 0,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("slice"),
        .value = njs_native_function(njs_array_buffer_prototype_slice, 2),
        .writable = 1,
        .configurable = 1,
        .enumerable = 0,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_wellknown_symbol(NJS_SYMBOL_TO_STRING_TAG),
        .value = njs_string("ArrayBuffer"),
        .configurable = 1,
    },
};


const njs_object_init_t  njs_array_buffer_prototype_init = {
    njs_array_buffer_prototype_properties,
    njs_nitems(njs_array_buffer_prototype_properties),
};


const njs_object_type_init_t  njs_array_buffer_type_init = {
    .constructor = njs_native_ctor(njs_array_buffer_constructor, 1, 0),
    .prototype_props = &njs_array_buffer_prototype_init,
    .constructor_props = &njs_array_buffer_constructor_init,
    .prototype_value = { .object = { .type = NJS_OBJECT } },
};
