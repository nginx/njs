
/*
 * Copyright (C) Alexander Borisov
 * Copyright (C) NGINX, Inc.
 */

#ifndef _NJS_BUFFER_H_INCLUDED_
#define _NJS_BUFFER_H_INCLUDED_


typedef njs_int_t (*njs_buffer_encode_t)(njs_vm_t *vm, njs_value_t *value,
    const njs_str_t *src);
typedef size_t (*njs_buffer_encode_length_t)(const njs_str_t *src,
                                             size_t *out_size);

typedef struct {
    njs_str_t                   name;
    njs_buffer_encode_t         encode;
    njs_buffer_encode_t         decode;
    njs_buffer_encode_length_t  decode_length;
} njs_buffer_encoding_t;


njs_typed_array_t *njs_buffer_slot(njs_vm_t *vm, njs_value_t *value,
    const char *name);
njs_int_t njs_buffer_set(njs_vm_t *vm, njs_value_t *value, const u_char *start,
    uint32_t size);
njs_int_t njs_buffer_new(njs_vm_t *vm, njs_value_t *value, const u_char *start,
    uint32_t size);
njs_typed_array_t *njs_buffer_alloc(njs_vm_t *vm, size_t size,
    njs_bool_t zeroing);

const njs_buffer_encoding_t *njs_buffer_encoding(njs_vm_t *vm,
    const njs_value_t *value);
njs_int_t njs_buffer_decode_string(njs_vm_t *vm, const njs_value_t *value,
    njs_value_t *dst, const njs_buffer_encoding_t *encoding);


extern const njs_object_type_init_t  njs_buffer_type_init;


#endif /* _NJS_BUFFER_H_INCLUDED_ */
