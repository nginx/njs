
/*
 * Copyright (C) Dmitry Volyntsev
 * Copyright (C) NGINX, Inc.
 */

#ifndef _NJS_ERROR_H_INCLUDED_
#define _NJS_ERROR_H_INCLUDED_


#define njs_error(vm, fmt, ...)                                               \
    njs_exception_error_create(vm, NJS_OBJECT_ERROR, fmt, ##__VA_ARGS__)
#define njs_eval_error(vm, fmt, ...)                                          \
    njs_exception_error_create(vm, NJS_OBJECT_EVAL_ERROR, fmt, ##__VA_ARGS__)
#define njs_internal_error(vm, fmt, ...)                                      \
    njs_exception_error_create(vm, NJS_OBJECT_INTERNAL_ERROR, fmt,            \
                               ##__VA_ARGS__)
#define njs_range_error(vm, fmt, ...)                                         \
    njs_exception_error_create(vm, NJS_OBJECT_RANGE_ERROR, fmt, ##__VA_ARGS__)
#define njs_reference_error(vm, fmt, ...)                                     \
    njs_exception_error_create(vm, NJS_OBJECT_REF_ERROR, fmt, ##__VA_ARGS__)
#define njs_syntax_error(vm, fmt, ...)                                        \
    njs_exception_error_create(vm, NJS_OBJECT_SYNTAX_ERROR, fmt, ##__VA_ARGS__)
#define njs_type_error(vm, fmt, ...)                                          \
    njs_exception_error_create(vm, NJS_OBJECT_TYPE_ERROR, fmt, ##__VA_ARGS__)
#define njs_uri_error(vm, fmt, ...)                                           \
    njs_exception_error_create(vm, NJS_OBJECT_URI_ERROR, fmt, ##__VA_ARGS__)

void njs_exception_error_create(njs_vm_t *vm, njs_value_type_t type,
    const char* fmt, ...);

void njs_memory_error(njs_vm_t *vm);
void njs_set_memory_error(njs_vm_t *vm, njs_value_t *value);

njs_object_t *njs_error_alloc(njs_vm_t *vm, njs_value_type_t type,
    const njs_value_t *name, const njs_value_t *message);
njs_ret_t njs_error_constructor(njs_vm_t *vm, njs_value_t *args,
    nxt_uint_t nargs, njs_index_t unused);
njs_ret_t njs_eval_error_constructor(njs_vm_t *vm, njs_value_t *args,
    nxt_uint_t nargs, njs_index_t unused);
njs_ret_t njs_internal_error_constructor(njs_vm_t *vm, njs_value_t *args,
    nxt_uint_t nargs, njs_index_t unused);
njs_ret_t njs_range_error_constructor(njs_vm_t *vm, njs_value_t *args,
    nxt_uint_t nargs, njs_index_t unused);
njs_ret_t njs_reference_error_constructor(njs_vm_t *vm, njs_value_t *args,
    nxt_uint_t nargs, njs_index_t unused);
njs_ret_t njs_syntax_error_constructor(njs_vm_t *vm, njs_value_t *args,
    nxt_uint_t nargs, njs_index_t unused);
njs_ret_t njs_type_error_constructor(njs_vm_t *vm, njs_value_t *args,
    nxt_uint_t nargs, njs_index_t unused);
njs_ret_t njs_uri_error_constructor(njs_vm_t *vm, njs_value_t *args,
    nxt_uint_t nargs, njs_index_t unused);
njs_ret_t njs_memory_error_constructor(njs_vm_t *vm, njs_value_t *args,
    nxt_uint_t nargs, njs_index_t unused);

njs_ret_t njs_error_to_string(njs_vm_t *vm, njs_value_t *retval,
    const njs_value_t *error);

extern const njs_object_init_t  njs_error_constructor_init;
extern const njs_object_init_t  njs_eval_error_constructor_init;
extern const njs_object_init_t  njs_internal_error_constructor_init;
extern const njs_object_init_t  njs_range_error_constructor_init;
extern const njs_object_init_t  njs_reference_error_constructor_init;
extern const njs_object_init_t  njs_syntax_error_constructor_init;
extern const njs_object_init_t  njs_type_error_constructor_init;
extern const njs_object_init_t  njs_uri_error_constructor_init;
extern const njs_object_init_t  njs_memory_error_constructor_init;


extern const njs_object_init_t  njs_error_prototype_init;
extern const njs_object_init_t  njs_eval_error_prototype_init;
extern const njs_object_init_t  njs_internal_error_prototype_init;
extern const njs_object_init_t  njs_range_error_prototype_init;
extern const njs_object_init_t  njs_reference_error_prototype_init;
extern const njs_object_init_t  njs_syntax_error_prototype_init;
extern const njs_object_init_t  njs_type_error_prototype_init;
extern const njs_object_init_t  njs_uri_error_prototype_init;


#endif /* _NJS_BOOLEAN_H_INCLUDED_ */
