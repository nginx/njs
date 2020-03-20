
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) NGINX, Inc.
 */

#ifndef _NJS_FUNCTION_H_INCLUDED_
#define _NJS_FUNCTION_H_INCLUDED_


struct njs_function_lambda_s {
    uint32_t                       nargs;
    uint32_t                       local_size;
    uint32_t                       closure_size;

    /* Function nesting level. */
    uint8_t                        nesting;           /* 4 bits */

    /* Function internal block closures levels. */
    uint8_t                        block_closures;    /* 4 bits */

    uint8_t                        ctor;              /* 1 bit */
    uint8_t                        rest_parameters;   /* 1 bit */

    /* Initial values of local scope. */
    njs_value_t                    *local_scope;
    njs_value_t                    *closure_scope;

    u_char                         *start;
};


/* The frame size must be aligned to njs_value_t. */
#define NJS_NATIVE_FRAME_SIZE                                                 \
    njs_align_size(sizeof(njs_native_frame_t), sizeof(njs_value_t))

/* The frame size must be aligned to njs_value_t. */
#define njs_frame_size(closures)                                              \
    njs_align_size(sizeof(njs_frame_t) + closures * sizeof(njs_closure_t *),  \
                   sizeof(njs_value_t))

/* The retval field is not used in the global frame. */
#define NJS_GLOBAL_FRAME_SIZE                                                 \
    njs_align_size(offsetof(njs_frame_t, retval), sizeof(njs_value_t))

#define NJS_FRAME_SPARE_SIZE       512


typedef struct njs_exception_s     njs_exception_t;

struct njs_exception_s {
    /*
     * The next field must be the first to alias it with restart address
     * because it is not used to detect catch block existance in the frame.
     */
    njs_exception_t                *next;
    u_char                         *catch;
};


struct njs_native_frame_s {
    u_char                         *free;

    njs_function_t                 *function;
    njs_native_frame_t             *previous;

    njs_value_t                    *arguments;
    njs_object_t                   *arguments_object;

    njs_exception_t                exception;

    uint32_t                       size;
    uint32_t                       free_size;
    uint32_t                       nargs;

    /* Function is called as constructor with "new" keyword. */
    uint8_t                        ctor;              /* 1 bit  */

    /* Skip the Function.call() and Function.apply() methods frames. */
    uint8_t                        skip;              /* 1 bit  */
};


struct njs_frame_s {
    njs_native_frame_t             native;

    njs_index_t                    retval;

    u_char                         *return_address;
    njs_frame_t                    *previous_active_frame;

    njs_value_t                    *local;

#define njs_frame_closures(frame)                                             \
    ((njs_closure_t **) ((u_char *) frame + sizeof(njs_frame_t)))
};


njs_function_t *njs_function_alloc(njs_vm_t *vm, njs_function_lambda_t *lambda,
    njs_closure_t *closures[], njs_bool_t shared);
njs_function_t *njs_function_value_copy(njs_vm_t *vm, njs_value_t *value);
njs_int_t njs_function_name_set(njs_vm_t *vm, njs_function_t *function,
    njs_value_t *name, njs_bool_t bound);
njs_int_t njs_function_arguments_object_init(njs_vm_t *vm,
    njs_native_frame_t *frame);
njs_int_t njs_function_rest_parameters_init(njs_vm_t *vm,
    njs_native_frame_t *frame);
njs_int_t njs_function_prototype_create(njs_vm_t *vm, njs_object_prop_t *prop,
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
njs_int_t njs_function_lambda_call(njs_vm_t *vm);
njs_int_t njs_function_native_call(njs_vm_t *vm);
void njs_function_frame_free(njs_vm_t *vm, njs_native_frame_t *frame);


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
njs_function_frame_invoke(njs_vm_t *vm, njs_index_t retval)
{
    njs_frame_t  *frame;

    frame = (njs_frame_t *) vm->top_frame;

    frame->retval = retval;

    if (frame->native.function->native) {
        return njs_function_native_call(vm);

    } else {
        return njs_function_lambda_call(vm);
    }
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


extern const njs_object_type_init_t  njs_function_type_init;
extern const njs_object_init_t  njs_function_instance_init;
extern const njs_object_init_t  njs_arrow_instance_init;
extern const njs_object_init_t  njs_arguments_object_instance_init;

#endif /* _NJS_FUNCTION_H_INCLUDED_ */
