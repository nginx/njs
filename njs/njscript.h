
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
typedef struct njs_vm_shared_s      njs_vm_shared_t;

typedef struct {
    njs_value_t                     *object;
    njs_value_t                     *args;
    uintptr_t                       nargs;
    njs_index_t                     retval;
} njs_param_t;


/* sizeof(njs_value_t) is 16 bytes. */
#define njs_argument(args, n)                                                 \
    (njs_value_t *) ((u_char *) args + n * 16)


typedef njs_ret_t (*njs_extern_get_t)(njs_vm_t *vm, njs_value_t *value,
    void *obj, uintptr_t data);
typedef njs_ret_t (*njs_extern_set_t)(njs_vm_t *vm, void *obj, uintptr_t data,
    nxt_str_t *value);
typedef njs_ret_t (*njs_extern_find_t)(njs_vm_t *vm, void *obj, uintptr_t data,
    nxt_bool_t delete);
typedef njs_ret_t (*njs_extern_each_start_t)(njs_vm_t *vm, void *obj,
    void *each);
typedef njs_ret_t (*njs_extern_each_t)(njs_vm_t *vm, njs_value_t *value,
    void *obj, void *each);
typedef njs_ret_t (*njs_extern_method_t)(njs_vm_t *vm, njs_param_t *param);


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

    njs_extern_each_start_t         each_start;
    njs_extern_each_t               each;

    njs_extern_method_t             method;

    uintptr_t                       data;
};


#define NJS_OK                      NXT_OK
#define NJS_ERROR                   NXT_ERROR
#define NJS_AGAIN                   NXT_AGAIN
#define NJS_DECLINED                NXT_DECLINED
#define NJS_DONE                    NXT_DONE


NXT_EXPORT nxt_int_t njs_add_external(nxt_lvlhsh_t *hash,
    nxt_mem_cache_pool_t *mcp, uintptr_t object, njs_external_t *external,
    nxt_uint_t n);

NXT_EXPORT njs_vm_t *njs_vm_create(nxt_mem_cache_pool_t *mcp,
    njs_vm_shared_t **shared, nxt_lvlhsh_t *externals);
NXT_EXPORT void njs_vm_destroy(njs_vm_t *vm);

NXT_EXPORT nxt_int_t njs_vm_compile(njs_vm_t *vm, u_char **start, u_char *end);
NXT_EXPORT njs_vm_t *njs_vm_clone(njs_vm_t *vm, nxt_mem_cache_pool_t *mcp,
    void **external);
NXT_EXPORT nxt_int_t njs_vm_run(njs_vm_t *vm);

NXT_EXPORT njs_ret_t njs_vm_return_string(njs_vm_t *vm, u_char *start,
    size_t size);
NXT_EXPORT nxt_int_t njs_vm_retval(njs_vm_t *vm, nxt_str_t *retval);
NXT_EXPORT nxt_int_t njs_vm_exception(njs_vm_t *vm, nxt_str_t *retval);

NXT_EXPORT void njs_disassembler(njs_vm_t *vm);

NXT_EXPORT njs_ret_t njs_string_create(njs_vm_t *vm, njs_value_t *value,
    u_char *start, size_t size, size_t length);
NXT_EXPORT njs_ret_t njs_void_set(njs_value_t *value);

NXT_EXPORT void *njs_value_data(njs_value_t *value);
NXT_EXPORT nxt_uint_t njs_vm_is_reentrant(njs_vm_t *vm);
NXT_EXPORT nxt_int_t njs_value_string(njs_vm_t *vm, nxt_str_t *retval,
    njs_value_t *value, njs_value_t **tmp);
NXT_EXPORT nxt_int_t njs_value_string_copy(njs_vm_t *vm, nxt_str_t *retval,
    njs_value_t *value, uintptr_t *next);

NXT_EXPORT njs_value_t *njs_array_add(njs_vm_t *vm, njs_value_t *array,
    u_char *start, size_t size);


#endif /* _NJSCRIPT_H_INCLUDED_ */
