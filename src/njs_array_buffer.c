/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) NGINX, Inc.
 */


#include <njs_main.h>


typedef struct {
    njs_vm_t        *vm;
    void            *data;
} njs_sab_cleanup_data_t;


njs_int_t
njs_vm_value_array_buffer_set2(njs_vm_t *vm, njs_value_t *value,
    u_char *start, uint32_t size, njs_bool_t shared)
{
    njs_array_buffer_t  *array;

    array = njs_array_buffer_alloc(vm, 0, 0, shared);
    if (njs_slow_path(array == NULL)) {
        return NJS_ERROR;
    }

    if (shared) {
        if (vm->sab_funcs.sab_dup != NULL) {
            vm->sab_funcs.sab_dup(vm->sab_funcs.sab_opaque, start);
        }

        if (njs_shared_array_buffer_destructor_set(vm, start) != NJS_OK) {
            return NJS_ERROR;
        }
    }

    array->u.data = (u_char *) start;
    array->size = size;

    if (shared) {
        njs_set_shared_array_buffer(value, array);

    } else {
        njs_set_array_buffer(value, array);
    }

    return NJS_OK;
}


njs_array_buffer_t *
njs_array_buffer_alloc(njs_vm_t *vm, uint64_t size, njs_bool_t zeroing,
    njs_bool_t shared)
{
    void                 *data;
    njs_object_t         *proto;
    njs_array_buffer_t   *array;
    njs_sab_functions_t  *sab_funcs;

    if (njs_slow_path(size > UINT32_MAX)) {
        goto overflow;
    }

    array = njs_mp_alloc(vm->mem_pool, sizeof(njs_array_buffer_t));
    if (njs_slow_path(array == NULL)) {
        goto memory_error;
    }

    if (size != 0 && shared && vm->sab_funcs.sab_alloc != NULL) {
        sab_funcs = &vm->sab_funcs;

        data = sab_funcs->sab_alloc(sab_funcs->sab_opaque, size);
        if (njs_slow_path(data == NULL)) {
            njs_internal_error(vm, "SharedArrayBuffer allocation failed");
            return NULL;
        }

        if (njs_shared_array_buffer_destructor_set(vm, data)
            != NJS_OK)
        {
            if (sab_funcs->sab_free != NULL) {
                sab_funcs->sab_free(sab_funcs->sab_opaque, data);
            }

            goto memory_error;
        }

    } else {
        data = njs_mp_alloc(vm->mem_pool, size);
    }

    if (njs_slow_path(data == NULL)) {
        goto memory_error;
    }

    if (zeroing) {
        memset(data, 0, size);
    }

    array->u.data = data;

    proto = njs_vm_proto(vm, shared ? NJS_OBJ_TYPE_SHARED_ARRAY_BUFFER
                                    : NJS_OBJ_TYPE_ARRAY_BUFFER);

    njs_flathsh_init(&array->object.hash);
    njs_flathsh_init(&array->object.shared_hash);
    array->object.__proto__ = proto;
    array->object.slots = NULL;
    array->object.type = shared ? NJS_SHARED_ARRAY_BUFFER : NJS_ARRAY_BUFFER;
    array->object.shared = 0;
    array->object.extensible = 1;
    array->object.error_data = 0;
    array->object.fast_array = 0;
    array->size = size;
    array->shared = shared;

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
    njs_index_t shared, njs_value_t *retval)
{
    uint64_t            size;
    njs_int_t           ret;
    njs_value_t         *value;
    njs_array_buffer_t  *array;

    if (!vm->top_frame->ctor) {
        njs_type_error(vm, "Constructor %s requires 'new'",
                       shared ? "SharedArrayBuffer" : "ArrayBuffer");
        return NJS_ERROR;
    }

    size = 0;
    value = njs_arg(args, nargs, 1);

    ret = njs_value_to_index(vm, value, &size);
    if (njs_slow_path(ret != NJS_OK)) {
        return NJS_ERROR;
    }

    array = njs_array_buffer_alloc(vm, size, 1, shared);

    if (njs_slow_path(array == NULL)) {
        return NJS_ERROR;
    }

    if (shared) {
        njs_set_shared_array_buffer(retval, array);

    } else {
        njs_set_array_buffer(retval, array);
    }

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


static const njs_object_prop_init_t  njs_array_buffer_constructor_properties[] =
{
    NJS_DECLARE_PROP_LENGTH(1),

    NJS_DECLARE_PROP_NAME("ArrayBuffer"),

    NJS_DECLARE_PROP_HANDLER(STRING_prototype, njs_object_prototype_create,
                             0, 0),

    NJS_DECLARE_PROP_GETTER(SYMBOL_species,
                            njs_array_buffer_get_this,
                            0),

    NJS_DECLARE_PROP_NATIVE(STRING_isView, njs_array_buffer_is_view, 1, 0),
};


const njs_object_init_t  njs_array_buffer_constructor_init = {
    njs_array_buffer_constructor_properties,
    njs_nitems(njs_array_buffer_constructor_properties),
};


static njs_int_t
njs_array_buffer_prototype_byte_length(njs_vm_t *vm, njs_value_t *args,
    njs_uint_t nargs, njs_index_t shared, njs_value_t *retval)
{
    njs_value_t         *value;
    njs_array_buffer_t  *array;

    value = njs_argument(args, 0);

    if (shared) {
        if (value->type != NJS_SHARED_ARRAY_BUFFER) {
            njs_type_error(vm,
                           "Method SharedArrayBuffer.prototype.byteLength "
                           "called on incompatible receiver");
            return NJS_ERROR;
        }

    } else {
        if (value->type != NJS_ARRAY_BUFFER) {
            njs_type_error(vm, "Method ArrayBuffer.prototype.byteLength "
                           "called on incompatible receiver");
            return NJS_ERROR;
        }

        array = njs_array_buffer(value);
        if (njs_slow_path(njs_is_detached_buffer(array))) {
            njs_set_number(retval, 0);
            return NJS_OK;
        }
    }

    array = njs_array_buffer(value);
    njs_set_number(retval, array->size);

    return NJS_OK;
}


static njs_int_t
njs_array_buffer_prototype_slice(njs_vm_t *vm, njs_value_t *args,
    njs_uint_t nargs, njs_index_t shared, njs_value_t *retval)
{
    int64_t             len, start, end;
    njs_int_t           ret;
    njs_value_t         *value;
    njs_array_buffer_t  *this, *buffer;

    value = njs_argument(args, 0);

    if (shared) {
        if (value->type != NJS_SHARED_ARRAY_BUFFER) {
            njs_type_error(vm, "Method SharedArrayBuffer.prototype.slice "
                           "called on incompatible receiver");
            return NJS_ERROR;
        }

    } else {
        if (value->type != NJS_ARRAY_BUFFER) {
            njs_type_error(vm, "Method ArrayBuffer.prototype.slice called "
                           "on incompatible receiver");
            return NJS_ERROR;
        }

        this = njs_array_buffer(value);
        if (njs_slow_path(njs_is_detached_buffer(this))) {
            njs_type_error(vm, "detached buffer");
            return NJS_ERROR;
        }
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

    if (shared) {
        njs_set_shared_array_buffer(retval, buffer);

    } else {
        njs_set_array_buffer(retval, buffer);
    }

    return NJS_OK;
}


njs_int_t
njs_array_buffer_detach(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t unused, njs_value_t *retval)
{
    njs_value_t         *value;
    njs_array_buffer_t  *buffer;

    value = njs_arg(args, nargs, 1);

    if (njs_slow_path(value->type != NJS_ARRAY_BUFFER)) {
        njs_type_error(vm, "detaching of %s is not supported",
                       njs_type_string(value->type));
        return NJS_ERROR;
    }

    buffer = njs_array_buffer(value);
    buffer->u.data = NULL;
    buffer->size = 0;

    njs_set_null(retval);

    return NJS_OK;
}


static njs_int_t
njs_array_buffer_resize(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t shared, njs_value_t *retval)
{
    njs_value_t  *value;

    value = njs_argument(args, 0);

    if (shared) {
        if (value->type != NJS_SHARED_ARRAY_BUFFER) {
            njs_type_error(vm, "Method SharedArrayBuffer.prototype.grow "
                           "called on incompatible receiver");
            return NJS_ERROR;
        }

        njs_type_error(vm, "SharedArrayBuffer is not growable");
        return NJS_ERROR;

    } else {
        if (value->type != NJS_ARRAY_BUFFER) {
            njs_type_error(vm, "Method ArrayBuffer.prototype.resize "
                           "called on incompatible receiver");
            return NJS_ERROR;
        }

        njs_type_error(vm, "ArrayBuffer is not resizable");
        return NJS_ERROR;
    }
}


static njs_int_t
njs_array_buffer_get_resizable(njs_vm_t *vm, njs_value_t *args,
    njs_uint_t nargs, njs_index_t shared, njs_value_t *retval)
{
    njs_value_t  *value;

    value = njs_argument(args, 0);

    if (shared) {
        if (value->type != NJS_SHARED_ARRAY_BUFFER) {
            njs_type_error(vm,
                           "Method SharedArrayBuffer.prototype.growable "
                           "called on incompatible receiver");
            return NJS_ERROR;
        }

    } else {
        if (value->type != NJS_ARRAY_BUFFER) {
            njs_type_error(vm, "Method ArrayBuffer.prototype.resizable "
                           "called on incompatible receiver");
            return NJS_ERROR;
        }
    }

    njs_set_boolean(retval, 0);

    return NJS_OK;
}


static void
njs_shared_array_buffer_cleanup(void *data)
{
    njs_vm_t                *vm;
    njs_sab_cleanup_data_t  *cleanup_data;

    cleanup_data = data;
    vm = cleanup_data->vm;

    if (vm->sab_funcs.sab_free != NULL) {
        vm->sab_funcs.sab_free(vm->sab_funcs.sab_opaque, cleanup_data->data);
    }
}


njs_int_t
njs_shared_array_buffer_destructor_set(njs_vm_t *vm, void *data)
{
    njs_mp_cleanup_t        *cleanup;
    njs_sab_cleanup_data_t  *cleanup_data;

    if (vm->sab_funcs.sab_free == NULL) {
        return NJS_OK;
    }

    cleanup = njs_mp_cleanup_add(vm->mem_pool,
                                 sizeof(njs_sab_cleanup_data_t));
    if (njs_slow_path(cleanup == NULL)) {
        return NJS_ERROR;
    }

    cleanup_data = cleanup->data;
    cleanup_data->vm = vm;
    cleanup_data->data = data;
    cleanup->handler = njs_shared_array_buffer_cleanup;

    return NJS_OK;
}


static const njs_object_prop_init_t
njs_shared_array_buffer_constructor_properties[] =
{
    NJS_DECLARE_PROP_LENGTH(1),

    NJS_DECLARE_PROP_NAME("SharedArrayBuffer"),

    NJS_DECLARE_PROP_HANDLER(STRING_prototype, njs_object_prototype_create,
                             0, 0),

    NJS_DECLARE_PROP_GETTER(SYMBOL_species,
                            njs_array_buffer_get_this,
                            0),
};


const njs_object_init_t  njs_shared_array_buffer_constructor_init = {
    njs_shared_array_buffer_constructor_properties,
    njs_nitems(njs_shared_array_buffer_constructor_properties),
};


static const njs_object_prop_init_t
njs_shared_array_buffer_prototype_properties[] =
{
    NJS_DECLARE_PROP_HANDLER(STRING_constructor,
                             njs_object_prototype_create_constructor, 0,
                             NJS_OBJECT_PROP_VALUE_CW),

    NJS_DECLARE_PROP_GETTER(STRING_byteLength,
                            njs_array_buffer_prototype_byte_length, 1),

    NJS_DECLARE_PROP_NATIVE(STRING_grow, njs_array_buffer_resize, 1, 1),

    NJS_DECLARE_PROP_GETTER(STRING_growable,
                            njs_array_buffer_get_resizable, 1),

    NJS_DECLARE_PROP_GETTER(STRING_maxByteLength,
                            njs_array_buffer_prototype_byte_length, 1),

    NJS_DECLARE_PROP_NATIVE(STRING_slice,
                            njs_array_buffer_prototype_slice,
                            2, 1),

    NJS_DECLARE_PROP_VALUE(SYMBOL_toStringTag,
                           njs_ascii_strval("SharedArrayBuffer"),
                           NJS_OBJECT_PROP_VALUE_C),
};


const njs_object_init_t  njs_shared_array_buffer_prototype_init = {
    njs_shared_array_buffer_prototype_properties,
    njs_nitems(njs_shared_array_buffer_prototype_properties),
};


const njs_object_type_init_t  njs_shared_array_buffer_type_init = {
    .constructor = njs_native_ctor(njs_array_buffer_constructor, 1, 1),
    .prototype_props = &njs_shared_array_buffer_prototype_init,
    .constructor_props = &njs_shared_array_buffer_constructor_init,
    .prototype_value = { .object = { .type = NJS_OBJECT } },
};




static const njs_object_prop_init_t  njs_array_buffer_prototype_properties[] =
{
    NJS_DECLARE_PROP_HANDLER(STRING_constructor,
                             njs_object_prototype_create_constructor, 0,
                             NJS_OBJECT_PROP_VALUE_CW),

    NJS_DECLARE_PROP_GETTER(STRING_byteLength,
                            njs_array_buffer_prototype_byte_length, 0),

    NJS_DECLARE_PROP_GETTER(STRING_maxByteLength,
                            njs_array_buffer_prototype_byte_length, 0),

    NJS_DECLARE_PROP_GETTER(STRING_resizable,
                            njs_array_buffer_get_resizable, 0),

    NJS_DECLARE_PROP_NATIVE(STRING_resize, njs_array_buffer_resize, 1, 0),

    NJS_DECLARE_PROP_NATIVE(STRING_slice, njs_array_buffer_prototype_slice,
                            2, 0),

    NJS_DECLARE_PROP_VALUE(SYMBOL_toStringTag, njs_ascii_strval("ArrayBuffer"),
                           NJS_OBJECT_PROP_VALUE_C),
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
