
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) NGINX, Inc.
 */

#ifndef _NJS_EXTERN_H_INCLUDED_
#define _NJS_EXTERN_H_INCLUDED_


struct njs_extern_s {
    njs_value_t                  value;

    /* A hash of inclusive njs_extern_t. */
    nxt_lvlhsh_t                 hash;

    uintptr_t                    type;
    nxt_str_t                    name;

    njs_extern_get_t             get;
    njs_extern_set_t             set;
    njs_extern_find_t            find;

    njs_extern_each_start_t      each_start;
    njs_extern_each_t            each;

    njs_extern_method_t          method;

    uintptr_t                    object;
    uintptr_t                    data;
};


extern const nxt_lvlhsh_proto_t  njs_extern_hash_proto;


#endif /* _NJS_EXTERN_H_INCLUDED_ */
