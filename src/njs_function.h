
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) NGINX, Inc.
 */

#ifndef _NJS_FUNCTION_H_INCLUDED_
#define _NJS_FUNCTION_H_INCLUDED_


struct njs_function_lambda_s {
    njs_index_t                    *closures;
    uint32_t                       nclosures;
    uint32_t                       nlocal;

    njs_declaration_t              *declarations;
    uint32_t                       ndeclarations;

    njs_index_t                    self;

    uint32_t                       nargs;

    uint8_t                        ctor;              /* 1 bit */
    uint8_t                        rest_parameters;   /* 1 bit */

    njs_value_t                    name;

    u_char                         *start;
};


/* The frame size must be aligned to njs_value_t. */
#define NJS_NATIVE_FRAME_SIZE                                                 \
    njs_align_size(sizeof(njs_native_frame_t), sizeof(njs_value_t))

/* The frame size must be aligned to njs_value_t. */
#define NJS_FRAME_SIZE                                                        \
    njs_align_size(sizeof(njs_frame_t), sizeof(njs_value_t))

#define NJS_FRAME_SPARE_SIZE       (4 * 1024)


struct njs_native_frame_s {
    u_char                         *free;
    u_char                         *pc;

    njs_function_t                 *function;
    njs_native_frame_t             *previous;

    /* Points to the first arg after 'this'. */
    njs_value_t                    *arguments;
    njs_object_t                   *arguments_object;
    njs_value_t                    **local;

    uint32_t                       size;
    uint32_t                       free_size;

    njs_value_t                    *retval;

    /* Number of allocated args on the frame. */
    uint32_t                       nargs;
    /* Number of already put args. */
    uint32_t                       put_args;

    uint8_t                        native;            /* 1 bit  */
    /* Function is called as constructor with "new" keyword. */
    uint8_t                        ctor;              /* 1 bit  */

    /* Skip the Function.call() and Function.apply() methods frames. */
    uint8_t                        skip;              /* 1 bit  */
};


typedef struct njs_exception_s     njs_exception_t;

struct njs_exception_s {
    njs_exception_t                *next;
    u_char                         *catch;
};


struct njs_frame_s {
    njs_native_frame_t             native;

    njs_exception_t                exception;

    njs_frame_t                    *previous_active_frame;
};


njs_function_t *njs_function_alloc(njs_vm_t *vm, njs_function_lambda_t *lambda,
    njs_bool_t async);
njs_function_t *njs_function_value_copy(njs_vm_t *vm, njs_value_t *value);
njs_int_t njs_function_name_set(njs_vm_t *vm, njs_function_t *function,
    njs_value_t *name, const char *prefix);
njs_function_t *njs_function_copy(njs_vm_t *vm, njs_function_t *function);
njs_int_t njs_function_arguments_object_init(njs_vm_t *vm,
    njs_native_frame_t *frame);
njs_int_t njs_function_rest_parameters_init(njs_vm_t *vm,
    njs_native_frame_t *frame);
njs_int_t njs_function_prototype_create(njs_vm_t *vm, njs_object_prop_t *prop,
    njs_value_t *value, njs_value_t *setval, njs_value_t *retval);
njs_int_t njs_function_constructor(njs_vm_t *vm, njs_value_t *args,
    njs_uint_t nargs, njs_index_t unused);
njs_int_t njs_function_instance_length(njs_vm_t *vm, njs_object_prop_t *prop,
    njs_value_t *value, njs_value_t *setval, njs_value_t *retval);
njs_int_t njs_function_instance_name(njs_vm_t *vm, njs_object_prop_t *prop,
    njs_value_t *value, njs_value_t *setval, njs_value_t *retval);
njs_int_t njs_eval_function(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t unused);
njs_int_t njs_function_native_frame(njs_vm_t *vm, njs_function_t *function,
    const njs_value_t *this, const njs_value_t *args, njs_uint_t nargs,
    njs_bool_t ctor);
njs_int_t njs_function_lambda_frame(njs_vm_t *vm, njs_function_t *function,
    const njs_value_t *this, const njs_value_t *args, njs_uint_t nargs,
    njs_bool_t ctor);
njs_int_t njs_function_call2(njs_vm_t *vm, njs_function_t *function,
    const njs_value_t *this, const njs_value_t *args,
    njs_uint_t nargs, njs_value_t *retval, njs_bool_t ctor);
njs_int_t njs_function_lambda_call(njs_vm_t *vm, void *promise_cap);
njs_int_t njs_function_native_call(njs_vm_t *vm);
njs_native_frame_t *njs_function_frame_alloc(njs_vm_t *vm, size_t size);
void njs_function_frame_free(njs_vm_t *vm, njs_native_frame_t *frame);
njs_int_t njs_function_frame_save(njs_vm_t *vm, njs_frame_t *native,
    u_char *pc);
njs_object_type_t njs_function_object_type(njs_vm_t *vm,
    njs_function_t *function);
njs_int_t njs_function_capture_closure(njs_vm_t *vm, njs_function_t *function,
     njs_function_lambda_t *lambda);
njs_int_t njs_function_capture_global_closures(njs_vm_t *vm,
    njs_function_t *function);
njs_int_t njs_function_frame_invoke(njs_vm_t *vm, njs_value_t *retval);


njs_inline njs_function_lambda_t *
njs_function_lambda_alloc(njs_vm_t *vm, uint8_t ctor)
{
    njs_function_lambda_t  *lambda;

    lambda = njs_mp_zalloc(vm->mem_pool, sizeof(njs_function_lambda_t));

    if (njs_fast_path(lambda != NULL)) {
        lambda->ctor = ctor;
    }

    return lambda;
}


njs_inline njs_int_t
njs_function_frame(njs_vm_t *vm, njs_function_t *function,
    const njs_value_t *this, const njs_value_t *args, njs_uint_t nargs,
    njs_bool_t ctor)
{
    if (function->native) {
        return njs_function_native_frame(vm, function, this, args, nargs, ctor);

    } else {
        return njs_function_lambda_frame(vm, function, this, args, nargs, ctor);
    }
}


njs_inline njs_native_frame_t *
njs_function_previous_frame(njs_native_frame_t *frame)
{
    njs_native_frame_t  *previous;

    do {
        previous = frame->previous;
        frame = previous;

    } while (frame->skip);

    return frame;
}


njs_inline njs_int_t
njs_function_call(njs_vm_t *vm, njs_function_t *function,
    const njs_value_t *this, const njs_value_t *args,
    njs_uint_t nargs, njs_value_t *retval)
{
    return njs_function_call2(vm, function, this, args, nargs, retval, 0);
}


njs_inline njs_int_t
njs_function_apply(njs_vm_t *vm, njs_function_t *function,
    const njs_value_t *args, njs_uint_t nargs, njs_value_t *retval)
{
    return njs_function_call2(vm, function, &args[0], &args[1], nargs - 1,
                              retval, 0);
}


njs_inline njs_bool_t
njs_native_function_same(const njs_function_t *f1, const njs_function_t *f2)
{
    return f1->u.native == f2->u.native && f1->magic8 == f2->magic8;
}


njs_inline njs_value_t **
njs_function_closures(const njs_function_t *func)
{
    return (njs_value_t **) ((u_char *) func + sizeof(njs_function_t));
}


njs_inline size_t
njs_function_frame_size(njs_native_frame_t *frame)
{
    size_t     size;
    uintptr_t  start;

    start = (uintptr_t) ((u_char *) frame + NJS_FRAME_SIZE);
    size = ((uintptr_t) frame->arguments - start) / sizeof(njs_value_t *);

    return NJS_FRAME_SIZE + (size * sizeof(njs_value_t *))
                          + (size * sizeof(njs_value_t));
}


njs_inline size_t
njs_function_frame_args_count(njs_native_frame_t *frame)
{
    uintptr_t  start;

    start = (uintptr_t) ((u_char *) frame + NJS_FRAME_SIZE);

    return ((uintptr_t) frame->local - start) / sizeof(njs_value_t *);
}


njs_inline njs_value_t *
njs_function_frame_values(njs_native_frame_t *frame, njs_value_t **end)
{
    size_t     count;
    uintptr_t  start;

    start = (uintptr_t) ((u_char *) frame + NJS_FRAME_SIZE);
    count = ((uintptr_t) frame->arguments - start) / sizeof(njs_value_t *);

    *end = frame->arguments + count;

    return frame->arguments;
}


extern const njs_object_type_init_t  njs_function_type_init;
extern const njs_object_init_t  njs_function_instance_init;
extern const njs_object_init_t  njs_arrow_instance_init;
extern const njs_object_init_t  njs_arguments_object_instance_init;

#endif /* _NJS_FUNCTION_H_INCLUDED_ */
