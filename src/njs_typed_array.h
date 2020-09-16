
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) NGINX, Inc.
 */

#ifndef _NJS_TYPED_ARRAY_H_INCLUDED_
#define _NJS_TYPED_ARRAY_H_INCLUDED_


njs_typed_array_t *njs_typed_array_alloc(njs_vm_t *vm, njs_value_t *args,
    njs_uint_t nargs, njs_bool_t zeroing, njs_object_type_t type);
njs_array_buffer_t *njs_typed_array_writable(njs_vm_t *vm,
    njs_typed_array_t *array);
njs_int_t njs_typed_array_set_value(njs_vm_t *vm, njs_typed_array_t *array,
    uint32_t index, njs_value_t *setval);
njs_int_t njs_typed_array_to_chain(njs_vm_t *vm, njs_chb_t *chain,
    njs_typed_array_t *array, njs_value_t *sep);
njs_int_t njs_typed_array_prototype_slice(njs_vm_t *vm, njs_value_t *args,
    njs_uint_t nargs, njs_index_t copy);

njs_inline unsigned
njs_typed_array_element_size(njs_object_type_t type)
{
    switch (type) {
    case NJS_OBJ_TYPE_DATA_VIEW:
    case NJS_OBJ_TYPE_UINT8_ARRAY:
    case NJS_OBJ_TYPE_UINT8_CLAMPED_ARRAY:
    case NJS_OBJ_TYPE_INT8_ARRAY:
        return 1;

    case NJS_OBJ_TYPE_UINT16_ARRAY:
    case NJS_OBJ_TYPE_INT16_ARRAY:
        return 2;

    case NJS_OBJ_TYPE_UINT32_ARRAY:
    case NJS_OBJ_TYPE_INT32_ARRAY:
    case NJS_OBJ_TYPE_FLOAT32_ARRAY:
        return 4;

    default:

        /* NJS_OBJ_TYPE_FLOAT64_ARRAY. */

        return 8;
    }
}


njs_inline uint32_t
njs_typed_array_length(const njs_typed_array_t *array)
{
    return array->byte_length / njs_typed_array_element_size(array->type);
}


njs_inline uint32_t
njs_typed_array_offset(const njs_typed_array_t *array)
{
    return array->offset * njs_typed_array_element_size(array->type);
}


njs_inline u_char *
njs_typed_array_start(const njs_typed_array_t *array)
{
    return &array->buffer->u.u8[njs_typed_array_offset(array)];
}


njs_inline double
njs_typed_array_prop(const njs_typed_array_t *array, uint32_t index)
{
    njs_array_buffer_t  *buffer;

    index += array->offset;

    buffer = array->buffer;

    switch (array->type) {
    case NJS_OBJ_TYPE_UINT8_ARRAY:
    case NJS_OBJ_TYPE_UINT8_CLAMPED_ARRAY:
        return buffer->u.u8[index];

    case NJS_OBJ_TYPE_INT8_ARRAY:
        return buffer->u.i8[index];

    case NJS_OBJ_TYPE_UINT16_ARRAY:
        return buffer->u.u16[index];

    case NJS_OBJ_TYPE_INT16_ARRAY:
        return buffer->u.i16[index];

    case NJS_OBJ_TYPE_UINT32_ARRAY:
        return buffer->u.u32[index];

    case NJS_OBJ_TYPE_INT32_ARRAY:
        return buffer->u.i32[index];

    case NJS_OBJ_TYPE_FLOAT32_ARRAY:
        return buffer->u.f32[index];

    default:

        /* NJS_OBJ_TYPE_FLOAT64_ARRAY. */

        return buffer->u.f64[index];
    }
}


extern const njs_object_type_init_t  njs_typed_array_type_init;
extern const njs_object_type_init_t  njs_data_view_type_init;
extern const njs_object_type_init_t  njs_typed_array_u8_type_init;
extern const njs_object_type_init_t  njs_typed_array_u8clamped_type_init;
extern const njs_object_type_init_t  njs_typed_array_i8_type_init;
extern const njs_object_type_init_t  njs_typed_array_u16_type_init;
extern const njs_object_type_init_t  njs_typed_array_i16_type_init;
extern const njs_object_type_init_t  njs_typed_array_u32_type_init;
extern const njs_object_type_init_t  njs_typed_array_i32_type_init;
extern const njs_object_type_init_t  njs_typed_array_f32_type_init;
extern const njs_object_type_init_t  njs_typed_array_f64_type_init;


#endif /* _NJS_TYPED_ARRAY_H_INCLUDED_ */
