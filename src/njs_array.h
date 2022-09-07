
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) NGINX, Inc.
 */

#ifndef _NJS_ARRAY_H_INCLUDED_
#define _NJS_ARRAY_H_INCLUDED_


#define NJS_ARRAY_MAX_INDEX            0xffffffff
#define NJS_ARRAY_INVALID_INDEX        NJS_ARRAY_MAX_INDEX

#define NJS_ARRAY_SPARE                8
#define NJS_ARRAY_FAST_OBJECT_LENGTH   (1024)
#define NJS_ARRAY_LARGE_OBJECT_LENGTH  (32768)
#define NJS_ARRAY_FLAT_MAX_LENGTH      (1048576)

#define njs_fast_object(_sz)           ((_sz) <= NJS_ARRAY_FAST_OBJECT_LENGTH)


njs_array_t *njs_array_alloc(njs_vm_t *vm, njs_bool_t flat, uint64_t length,
    uint32_t spare);
void njs_array_destroy(njs_vm_t *vm, njs_array_t *array);
njs_int_t njs_array_add(njs_vm_t *vm, njs_array_t *array, njs_value_t *value);
njs_int_t njs_array_convert_to_slow_array(njs_vm_t *vm, njs_array_t *array);
njs_int_t njs_array_length_redefine(njs_vm_t *vm, njs_value_t *value,
    uint32_t length, int writable);
njs_int_t njs_array_length_set(njs_vm_t *vm, njs_value_t *value,
    njs_object_prop_t *prev, njs_value_t *setval);
njs_array_t *njs_array_keys(njs_vm_t *vm, njs_value_t *array, njs_bool_t all);
njs_array_t *njs_array_indices(njs_vm_t *vm, njs_value_t *object);
njs_int_t njs_array_string_add(njs_vm_t *vm, njs_array_t *array,
    const u_char *start, size_t size, size_t length);
njs_int_t njs_array_expand(njs_vm_t *vm, njs_array_t *array, uint32_t prepend,
    uint32_t append);
njs_int_t njs_array_prototype_to_string(njs_vm_t *vm, njs_value_t *args,
    njs_uint_t nargs, njs_index_t unused);


njs_inline njs_value_t *
njs_array_push(njs_vm_t *vm, njs_array_t *array)
{
    njs_int_t  ret;

    ret = njs_array_expand(vm, array, 0, 1);
    if (njs_slow_path(ret != NJS_OK)) {
        return NULL;
    }

    return &array->start[array->length++];
}


extern const njs_object_init_t  njs_array_instance_init;
extern const njs_object_type_init_t  njs_array_type_init;


#endif /* _NJS_ARRAY_H_INCLUDED_ */
