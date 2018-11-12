
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) NGINX, Inc.
 */

#ifndef _NJS_FUNCTION_H_INCLUDED_
#define _NJS_FUNCTION_H_INCLUDED_


#define NJS_SKIP_ARG               1
#define NJS_NUMBER_ARG             2
#define NJS_INTEGER_ARG            3
#define NJS_STRING_ARG             4
#define NJS_OBJECT_ARG             5
#define NJS_STRING_OBJECT_ARG      6
#define NJS_FUNCTION_ARG           7
#define NJS_REGEXP_ARG             8
#define NJS_DATE_ARG               9


struct njs_function_lambda_s {
    uint32_t                       nargs;
    uint32_t                       local_size;
    uint32_t                       closure_size;

    /* Function nesting level. */
    uint8_t                        nesting;           /* 4 bits */

    /* Function internal block closures levels. */
    uint8_t                        block_closures;    /* 4 bits */

    uint8_t                        arguments_object;  /* 1 bit */

    /* Initial values of local scope. */
    njs_value_t                    *local_scope;
    njs_value_t                    *closure_scope;

    u_char                         *start;
    njs_parser_t                   *parser;
};


/* The frame size must be aligned to njs_value_t. */
#define NJS_NATIVE_FRAME_SIZE                                                 \
    nxt_align_size(sizeof(njs_native_frame_t), sizeof(njs_value_t))

/* The frame size must be aligned to njs_value_t. */
#define njs_frame_size(closures)                                              \
    nxt_align_size(sizeof(njs_frame_t) + closures * sizeof(njs_closure_t *),  \
                   sizeof(njs_value_t))

/* The retval field is not used in the global frame. */
#define NJS_GLOBAL_FRAME_SIZE                                                 \
    nxt_align_size(offsetof(njs_frame_t, retval), sizeof(njs_value_t))

#define NJS_FRAME_SPARE_SIZE       512


typedef struct {
    njs_function_native_t          function;
    uint8_t                        *args_types;
    u_char                         *return_address;
    njs_index_t                    retval;
} njs_continuation_t;


#define njs_vm_continuation(vm)                                               \
    (void *) njs_continuation((vm)->top_frame)

#define njs_continuation(frame)                                               \
    ((u_char *) frame + NJS_NATIVE_FRAME_SIZE)

#define njs_continuation_size(size)                                           \
    nxt_align_size(sizeof(size), sizeof(njs_value_t))

#define NJS_CONTINUATION_SIZE      njs_continuation_size(njs_continuation_t)


#define njs_vm_trap_value(vm, val)                                            \
    (vm)->top_frame->trap_scratch.data.u.value = val



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
    njs_value_t                    trap_scratch;
    njs_value_t                    trap_values[2];
    u_char                         *trap_restart;

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

    /* A number of trap tries, it can be no more than three. */
    uint8_t                        trap_tries;        /* 2 bits */

    /*
     * The first operand in trap is reference to original value,
     * it is used to increment or decrement this value.
     */
    uint8_t                        trap_reference;   /* 1 bit */
};


struct njs_frame_s {
    njs_native_frame_t             native;

    njs_index_t                    retval;

    u_char                         *return_address;
    njs_frame_t                    *previous_active_frame;

    njs_value_t                    *local;
#if (NXT_SUNC)
    njs_closure_t                  *closures[1];
#else
    njs_closure_t                  *closures[];
#endif
};


njs_function_t *njs_function_alloc(njs_vm_t *vm);
njs_function_t *njs_function_value_copy(njs_vm_t *vm, njs_value_t *value);
njs_native_frame_t *njs_function_frame_alloc(njs_vm_t *vm, size_t size);
njs_ret_t njs_function_arguments_object_init(njs_vm_t *vm,
    njs_native_frame_t *frame);
njs_ret_t njs_function_arguments_thrower(njs_vm_t *vm, njs_value_t *value,
    njs_value_t *setval, njs_value_t *retval);
njs_ret_t njs_function_prototype_create(njs_vm_t *vm, njs_value_t *value,
    njs_value_t *setval, njs_value_t *retval);
njs_value_t *njs_function_property_prototype_create(njs_vm_t *vm,
    njs_value_t *value);
njs_ret_t njs_function_constructor(njs_vm_t *vm, njs_value_t *args,
    nxt_uint_t nargs, njs_index_t unused);
njs_ret_t njs_function_apply(njs_vm_t *vm, njs_function_t *function,
    njs_value_t *args, nxt_uint_t nargs, njs_index_t retval);
njs_ret_t njs_function_native_frame(njs_vm_t *vm, njs_function_t *function,
    const njs_value_t *this, njs_value_t *args, nxt_uint_t nargs,
    size_t reserve, nxt_bool_t ctor);
njs_ret_t njs_function_frame(njs_vm_t *vm, njs_function_t *function,
    const njs_value_t *this, const njs_value_t *args, nxt_uint_t nargs,
    nxt_bool_t ctor);
njs_ret_t njs_function_call(njs_vm_t *vm, njs_index_t retval, size_t advance);

extern const njs_object_init_t  njs_function_constructor_init;
extern const njs_object_init_t  njs_function_prototype_init;

njs_ret_t njs_eval_function(njs_vm_t *vm, njs_value_t *args, nxt_uint_t nargs,
    njs_index_t unused);

extern const njs_object_init_t  njs_eval_function_init;


#endif /* _NJS_FUNCTION_H_INCLUDED_ */
