
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) NGINX, Inc.
 */

#ifndef _NJSCRIPT_H_INCLUDED_
#define _NJSCRIPT_H_INCLUDED_


typedef intptr_t                    njs_ret_t;
typedef uintptr_t                   njs_index_t;
typedef struct njs_vm_s             njs_vm_t;
typedef union  njs_value_s          njs_value_t;
typedef struct njs_function_s       njs_function_t;
typedef struct njs_vm_shared_s      njs_vm_shared_t;

typedef struct {
    uint64_t                        filler[2];
} njs_opaque_value_t;


/* sizeof(njs_value_t) is 16 bytes. */
#define njs_argument(args, n)                                                 \
    (njs_value_t *) ((u_char *) args + n * 16)


typedef njs_ret_t (*njs_extern_get_t)(njs_vm_t *vm, njs_value_t *value,
    void *obj, uintptr_t data);
typedef njs_ret_t (*njs_extern_set_t)(njs_vm_t *vm, void *obj, uintptr_t data,
    nxt_str_t *value);
typedef njs_ret_t (*njs_extern_find_t)(njs_vm_t *vm, void *obj, uintptr_t data,
    nxt_bool_t delete);
typedef njs_ret_t (*njs_extern_foreach_t)(njs_vm_t *vm, void *obj, void *next);
typedef njs_ret_t (*njs_extern_next_t)(njs_vm_t *vm, njs_value_t *value,
    void *obj, void *next);
typedef njs_ret_t (*njs_extern_method_t)(njs_vm_t *vm, njs_value_t *args,
    nxt_uint_t nargs, njs_index_t unused);


typedef struct njs_external_s       njs_external_t;

struct njs_external_s {
    nxt_str_t                       name;

#define NJS_EXTERN_PROPERTY         0x00
#define NJS_EXTERN_METHOD           0x01
#define NJS_EXTERN_OBJECT           0x80
#define NJS_EXTERN_CASELESS_OBJECT  0x81

    uintptr_t                       type;

    njs_external_t                  *properties;
    nxt_uint_t                      nproperties;

    njs_extern_get_t                get;
    njs_extern_set_t                set;
    njs_extern_find_t               find;

    njs_extern_foreach_t            foreach;
    njs_extern_next_t               next;

    njs_extern_method_t             method;

    uintptr_t                       data;
};

typedef struct {
    void                            **external;
    nxt_lvlhsh_t                    *externals_hash;
    njs_vm_shared_t                 *shared;
    nxt_mem_cache_pool_t            *mcp;

    uint8_t                         trailer;         /* 1 bit */
    uint8_t                         accumulative;    /* 1 bit */
    uint8_t                         backtrace;       /* 1 bit */
} njs_vm_opt_t;


typedef struct {
    nxt_str_t                       name;
    uint32_t                        line;
} njs_backtrace_entry_t;


#define NJS_OK                      NXT_OK
#define NJS_ERROR                   NXT_ERROR
#define NJS_AGAIN                   NXT_AGAIN
#define NJS_DECLINED                NXT_DECLINED
#define NJS_DONE                    NXT_DONE


NXT_EXPORT nxt_int_t njs_vm_external_add(nxt_lvlhsh_t *hash,
    nxt_mem_cache_pool_t *mcp, uintptr_t object, njs_external_t *external,
    nxt_uint_t n);
NXT_EXPORT nxt_int_t njs_vm_external(njs_vm_t *vm, njs_opaque_value_t *object,
    nxt_str_t *property, njs_opaque_value_t *value);

NXT_EXPORT njs_vm_t *njs_vm_create(njs_vm_opt_t *options);
NXT_EXPORT void njs_vm_destroy(njs_vm_t *vm);

NXT_EXPORT nxt_int_t njs_vm_compile(njs_vm_t *vm, u_char **start, u_char *end);
NXT_EXPORT njs_vm_t *njs_vm_clone(njs_vm_t *vm, nxt_mem_cache_pool_t *mcp,
    void **external);
NXT_EXPORT nxt_int_t njs_vm_call(njs_vm_t *vm, njs_function_t *function,
    njs_opaque_value_t *args, nxt_uint_t nargs);
NXT_EXPORT nxt_int_t njs_vm_run(njs_vm_t *vm);

NXT_EXPORT njs_function_t *njs_vm_function(njs_vm_t *vm, nxt_str_t *name);
NXT_EXPORT njs_ret_t njs_vm_return_string(njs_vm_t *vm, u_char *start,
    size_t size);
NXT_EXPORT nxt_int_t njs_vm_retval(njs_vm_t *vm, nxt_str_t *retval);
NXT_EXPORT nxt_array_t *njs_vm_backtrace(njs_vm_t *vm);

NXT_EXPORT void njs_disassembler(njs_vm_t *vm);

NXT_EXPORT njs_ret_t njs_string_create(njs_vm_t *vm, njs_value_t *value,
    u_char *start, size_t size, size_t length);
NXT_EXPORT njs_ret_t njs_void_set(njs_value_t *value);

NXT_EXPORT void *njs_value_data(njs_value_t *value);
NXT_EXPORT nxt_int_t njs_value_string_copy(njs_vm_t *vm, nxt_str_t *retval,
    njs_value_t *value, uintptr_t *next);

NXT_EXPORT nxt_array_t *njs_vm_completions(njs_vm_t *vm, nxt_str_t *expression);


extern const nxt_mem_proto_t  njs_vm_mem_cache_pool_proto;

#endif /* _NJSCRIPT_H_INCLUDED_ */
