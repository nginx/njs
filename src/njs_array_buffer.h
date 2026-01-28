
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) NGINX, Inc.
 */

#ifndef _NJS_ARRAY_BUFFER_H_INCLUDED_
#define _NJS_ARRAY_BUFFER_H_INCLUDED_


#define njs_array_buffer_size(buffer)                                        \
    ((buffer)->size)


njs_array_buffer_t *njs_array_buffer_alloc(njs_vm_t *vm, uint64_t size,
    njs_bool_t zeroing, njs_bool_t shared);
njs_int_t njs_shared_array_buffer_destructor_set(njs_vm_t *vm, void *data);
njs_int_t njs_array_buffer_writable(njs_vm_t *vm, njs_array_buffer_t *buffer);
NJS_EXPORT njs_int_t njs_array_buffer_detach(njs_vm_t *vm, njs_value_t *args,
    njs_uint_t nargs, njs_index_t unused, njs_value_t *retval);

njs_inline njs_array_buffer_t *
njs_array_buffer_slice(njs_vm_t *vm, njs_array_buffer_t *this_value,
    int64_t start, int64_t end)
{
    int64_t             len, new_len, first, final;
    njs_array_buffer_t  *new_buffer;

    len = njs_array_buffer_size(this_value);

    first = (start < 0) ? njs_max(len + start, 0) : njs_min(start, len);
    final = (end < 0) ? njs_max(len + end, 0) : njs_min(end, len);

    new_len = njs_max(final - first, 0);

    new_buffer = njs_array_buffer_alloc(vm, new_len, 1, this_value->shared);
    if (new_buffer == NULL) {
        return NULL;
    }

    memcpy(new_buffer->u.u8, &this_value->u.u8[first], new_len);

    return new_buffer;
}


extern const njs_object_type_init_t  njs_array_buffer_type_init;
extern const njs_object_type_init_t  njs_shared_array_buffer_type_init;


#endif /* _NJS_ARRAY_BUFFER_H_INCLUDED_ */
