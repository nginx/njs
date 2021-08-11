
/*
 * Copyright (C) Dmitry Volyntsev
 * Copyright (C) NGINX, Inc.
 */

#ifndef _NJS_ERROR_H_INCLUDED_
#define _NJS_ERROR_H_INCLUDED_


#define njs_error(vm, fmt, ...)                                               \
    njs_error_fmt_new(vm, &vm->retval, NJS_OBJ_TYPE_ERROR, fmt, ##__VA_ARGS__)
#define njs_eval_error(vm, fmt, ...)                                          \
    njs_error_fmt_new(vm, &vm->retval, NJS_OBJ_TYPE_EVAL_ERROR, fmt,          \
                      ##__VA_ARGS__)
#define njs_internal_error(vm, fmt, ...)                                      \
    njs_error_fmt_new(vm, &vm->retval, NJS_OBJ_TYPE_INTERNAL_ERROR, fmt,      \
                      ##__VA_ARGS__)
#define njs_range_error(vm, fmt, ...)                                         \
    njs_error_fmt_new(vm, &vm->retval, NJS_OBJ_TYPE_RANGE_ERROR, fmt,         \
                      ##__VA_ARGS__)
#define njs_reference_error(vm, fmt, ...)                                     \
    njs_error_fmt_new(vm, &vm->retval, NJS_OBJ_TYPE_REF_ERROR, fmt,           \
                      ##__VA_ARGS__)
#define njs_syntax_error(vm, fmt, ...)                                        \
    njs_error_fmt_new(vm, &vm->retval, NJS_OBJ_TYPE_SYNTAX_ERROR, fmt,        \
                      ##__VA_ARGS__)
#define njs_type_error(vm, fmt, ...)                                          \
    njs_error_fmt_new(vm, &vm->retval, NJS_OBJ_TYPE_TYPE_ERROR, fmt,          \
                      ##__VA_ARGS__)
#define njs_uri_error(vm, fmt, ...)                                           \
    njs_error_fmt_new(vm, &vm->retval, NJS_OBJ_TYPE_URI_ERROR, fmt,           \
                      ##__VA_ARGS__)

void njs_error_new(njs_vm_t *vm, njs_value_t *dst, njs_object_type_t type,
    u_char *start, size_t size);
void njs_noinline njs_error_fmt_new(njs_vm_t *vm, njs_value_t *dst,
    njs_object_type_t type, const char *fmt, ...);

void njs_memory_error(njs_vm_t *vm);
void njs_memory_error_set(njs_vm_t *vm, njs_value_t *value);

njs_object_t *njs_error_alloc(njs_vm_t *vm, njs_object_type_t type,
    const njs_value_t *name, const njs_value_t *message,
    const njs_value_t *errors);
njs_int_t njs_error_to_string(njs_vm_t *vm, njs_value_t *retval,
    const njs_value_t *error);
njs_int_t njs_error_stack(njs_vm_t *vm, njs_value_t *value, njs_value_t *stack);
njs_int_t njs_error_stack_attach(njs_vm_t *vm, njs_value_t *value);


extern const njs_object_type_init_t  njs_error_type_init;
extern const njs_object_type_init_t  njs_eval_error_type_init;
extern const njs_object_type_init_t  njs_internal_error_type_init;
extern const njs_object_type_init_t  njs_range_error_type_init;
extern const njs_object_type_init_t  njs_reference_error_type_init;
extern const njs_object_type_init_t  njs_syntax_error_type_init;
extern const njs_object_type_init_t  njs_type_error_type_init;
extern const njs_object_type_init_t  njs_uri_error_type_init;
extern const njs_object_type_init_t  njs_memory_error_type_init;
extern const njs_object_type_init_t  njs_aggregate_error_type_init;


njs_inline njs_int_t
njs_is_memory_error(njs_vm_t *vm, njs_value_t *value)
{
    if (njs_is_error(value)
        && njs_has_prototype(vm, value, NJS_OBJ_TYPE_INTERNAL_ERROR)
        && !njs_object(value)->extensible)
    {
        return 1;
    }

    return 0;
}


#endif /* _NJS_BOOLEAN_H_INCLUDED_ */
