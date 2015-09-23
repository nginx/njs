
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) NGINX, Inc.
 */

#ifndef _NJS_FUNCTION_H_INCLUDED_
#define _NJS_FUNCTION_H_INCLUDED_


typedef struct {
    uint32_t                       nargs;
    uint32_t                       local_size;
    /*
     * Native methods do not allocate frame space so calling function
     * reserves space in its scope for method frame and arguments.
     */
    uint32_t                       spare_size;

    /* Initial values of local scope. */
    njs_value_t                    *local_scope;

    union {
        u_char                     *code;
        njs_parser_t               *parser;
    } u;
} njs_function_script_t;


struct njs_function_s {
    njs_object_t                   object;

#if (NXT_64BIT)
    uint32_t                       native;
    uint32_t                       args_offset;
#else
    uint8_t                        native;
    uint16_t                       args_offset;
#endif

    union {
        njs_function_script_t      *script;
        njs_native_t               native;
    } code;

    njs_value_t                    *args;
};


/* The frame size must be aligned to njs_value_t. */
#define NJS_NATIVE_FRAME_SIZE                                                 \
    nxt_align_size(sizeof(njs_native_frame_t), sizeof(njs_value_t))

/* The frame size must be aligned to njs_value_t. */
#define NJS_FRAME_SIZE                                                        \
    nxt_align_size(sizeof(njs_frame_t), sizeof(njs_value_t))

/* The retval and return_address fields are not used in the global frame. */
#define NJS_GLOBAL_FRAME_SIZE                                                 \
    nxt_align_size(offsetof(njs_frame_t, retval), sizeof(njs_value_t))

#define NJS_FRAME_SPARE_SIZE       512

#define                                                                       \
njs_method_data_size(size)                                                    \
    nxt_align_size(size, sizeof(njs_value_t))

#define                                                                       \
njs_native_data(frame)                                                        \
    (void *) ((u_char *) frame + NJS_NATIVE_FRAME_SIZE)


typedef struct njs_exception_s     njs_exception_t;

struct njs_exception_s {
    /*
     * The next field must be the first to alias it with restart address
     * because it is not used to detect catch block existance in the frame.
     */
    njs_exception_t                *next;
    u_char                         *catch;
};


typedef struct njs_native_frame_s  njs_native_frame_t;

struct njs_native_frame_s {
    u_char                         *last;
    njs_native_frame_t             *previous;
    njs_value_t                    *arguments;

    union {
        u_char                     *restart;
        njs_exception_t            exception;
    } u;

    uint32_t                       size;

    uint8_t                        start;      /* 1 bit */
    uint8_t                        ctor;       /* 1 bit */
    uint8_t                        reentrant;  /* 1 bit */
    uint8_t                        lvalue;     /* 1 bit */
};


typedef struct {
    njs_native_frame_t             native;

    njs_value_t                    *prev_arguments;
    njs_value_t                    *prev_local;
    njs_value_t                    *local;
    njs_value_t                    *closure;

    njs_index_t                    retval;
    u_char                         *return_address;
} njs_frame_t;


njs_function_t *njs_function_alloc(njs_vm_t *vm);
njs_ret_t njs_function_apply(njs_vm_t *vm, njs_value_t *name,
    njs_param_t *param);
njs_value_t *njs_vmcode_native_frame(njs_vm_t *vm, njs_value_t *method,
    uintptr_t nargs, nxt_bool_t ctor);
njs_ret_t njs_vmcode_trap(njs_vm_t *vm, u_char *trap, njs_value_t *value1,
    njs_value_t *value2, nxt_bool_t lvalue);
njs_ret_t njs_vmcode_function_frame(njs_vm_t *vm, njs_value_t *name,
    njs_param_t *param, nxt_bool_t ctor);
njs_ret_t njs_function_call(njs_vm_t *vm, njs_function_t *func,
    njs_index_t retval);
nxt_int_t njs_function_function_hash(njs_vm_t *vm, nxt_lvlhsh_t *hash);
nxt_int_t njs_function_prototype_hash(njs_vm_t *vm, nxt_lvlhsh_t *hash);


#endif /* _NJS_FUNCTION_H_INCLUDED_ */
