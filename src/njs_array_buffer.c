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

    proto = njs_vm_proto(vm, NJS_OBJ_TYPE_ARRAY_BUFFER);

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
    njs_index_t unused, njs_value_t *retval)
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

    njs_set_array_buffer(retval, array);

    return NJS_OK;
}


static njs_int_t
njs_array_buffer_get_this(njs_vm_t *vm, njs_value_t *args,
    njs_uint_t nargs, njs_index_t unused, njs_value_t *retval)
{
    njs_value_assign(retval, njs_argument(args, 0));

    return NJS_OK;
}


static njs_int_t
njs_array_buffer_is_view(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t unused, njs_value_t *retval)
{
    njs_set_boolean(retval, njs_is_typed_array(njs_arg(args, nargs, 1)));

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


static njs_object_propi_t  njs_array_buffer_constructor_properties[] =
{
    NJS_DECLARE_PROP_LENGTH(1),

    NJS_DECLARE_PROP_NAME(vs_ArrayBuffer),

    NJS_DECLARE_PROP_HANDLER(vs_prototype, njs_object_prototype_create,
                             0, 0),

    NJS_DECLARE_PROP_GETTER(vw_species,
                            njs_array_buffer_get_this,
                            0),

    NJS_DECLARE_PROP_NATIVE(vs_isView, njs_array_buffer_is_view, 1, 0),
};


static const njs_object_init_t  njs_array_buffer_constructor_init = {
    njs_array_buffer_constructor_properties,
    njs_nitems(njs_array_buffer_constructor_properties),
};


static njs_int_t
njs_array_buffer_prototype_byte_length(njs_vm_t *vm, njs_value_t *args,
    njs_uint_t nargs, njs_index_t unused, njs_value_t *retval)
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

    njs_set_number(retval, array->size);

    return NJS_OK;
}


static njs_int_t
njs_array_buffer_prototype_slice(njs_vm_t *vm, njs_value_t *args,
    njs_uint_t nargs, njs_index_t unused, njs_value_t *retval)
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

    njs_set_array_buffer(retval, buffer);

    return NJS_OK;
}


njs_int_t
njs_array_buffer_detach(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t unused, njs_value_t *retval)
{
    njs_value_t         *value;
    njs_array_buffer_t  *buffer;

    value = njs_arg(args, nargs, 1);
    if (njs_slow_path(!njs_is_array_buffer(value))) {
        njs_type_error(vm, "\"this\" is not an ArrayBuffer");
        return NJS_ERROR;
    }

    buffer = njs_array_buffer(value);
    buffer->u.data = NULL;
    buffer->size = 0;

    njs_set_null(retval);

    return NJS_OK;
}




static njs_object_propi_t  njs_array_buffer_prototype_properties[] =
{
    NJS_DECLARE_PROP_HANDLER(vs_constructor,
                             njs_object_prototype_create_constructor, 0,
                             NJS_OBJECT_PROP_VALUE_CW),

    NJS_DECLARE_PROP_GETTER(vs_byteLength,
                            njs_array_buffer_prototype_byte_length, 0),

    NJS_DECLARE_PROP_NATIVE(vs_slice, njs_array_buffer_prototype_slice,
                            2, 0),

    NJS_DECLARE_PROP_VALUE(vw_toStringTag, njs_atom.vs_ArrayBuffer,
                           NJS_OBJECT_PROP_VALUE_C),
};


static const njs_object_init_t  njs_array_buffer_prototype_init = {
    njs_array_buffer_prototype_properties,
    njs_nitems(njs_array_buffer_prototype_properties),
};


const njs_object_type_init_t  njs_array_buffer_type_init = {
    .constructor = njs_native_ctor(njs_array_buffer_constructor, 1, 0),
    .prototype_props = &njs_array_buffer_prototype_init,
    .constructor_props = &njs_array_buffer_constructor_init,
    .prototype_value = { .object = { .type = NJS_OBJECT } },
};
