
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) NGINX, Inc.
 */

#ifndef _NJS_EXTERN_H_INCLUDED_
#define _NJS_EXTERN_H_INCLUDED_


#define njs_extern_object(vm, ext)                                          \
    (*(void **) nxt_array_item((vm)->external_objects, (ext)->external.index))

#define njs_extern_index(vm, idx)                                          \
    (*(void **) nxt_array_item((vm)->external_objects, idx))


struct njs_extern_s {
    /* A hash of inclusive njs_extern_t. */
    nxt_lvlhsh_t                 hash;

    uintptr_t                    type;
    nxt_str_t                    name;

    njs_extern_get_t             get;
    njs_extern_set_t             set;
    njs_extern_find_t            find;

    njs_extern_foreach_t         foreach;
    njs_extern_next_t            next;

    njs_function_t               *function;

    uintptr_t                    data;
};


typedef struct {
    njs_value_t             value;
    nxt_str_t               name;
} njs_extern_value_t;


njs_array_t *njs_extern_keys_array(njs_vm_t *vm, const njs_extern_t *external);
nxt_int_t njs_external_match_native_function(njs_vm_t *vm,
    njs_function_native_t func, nxt_str_t *name);


extern const nxt_lvlhsh_proto_t  njs_extern_hash_proto;
extern const nxt_lvlhsh_proto_t  njs_extern_value_hash_proto;


#endif /* _NJS_EXTERN_H_INCLUDED_ */
