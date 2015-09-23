
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) NGINX, Inc.
 */

#ifndef _NJS_REGEXP_H_INCLUDED_
#define _NJS_REGEXP_H_INCLUDED_


typedef enum {
    NJS_REGEXP_IGNORE_CASE = 1,
    NJS_REGEXP_GLOBAL      = 2,
    NJS_REGEXP_MULTILINE   = 4,
} njs_regexp_flags_t;


struct njs_regexp_s {
    /* Must be aligned to njs_value_t. */
    njs_object_t          object;

    uint32_t              last_index;

    njs_regexp_pattern_t  *pattern;

    /*
     * This string value can be not aligned since
     * it never used in nJSVM operations.
     */
    njs_value_t           string;
};


njs_regexp_t *njs_regexp_alloc(njs_vm_t *vm, njs_regexp_pattern_t *pattern);
njs_regexp_pattern_t *njs_regexp_pattern_create(njs_vm_t *vm,
    nxt_str_t *source, njs_regexp_flags_t flags);
njs_ret_t njs_regexp_function(njs_vm_t *vm, njs_param_t *param);
njs_ret_t njs_regexp_prototype_exec(njs_vm_t *vm, njs_param_t *param);
nxt_int_t njs_regexp_function_hash(njs_vm_t *vm, nxt_lvlhsh_t *hash);
nxt_int_t njs_regexp_prototype_hash(njs_vm_t *vm, nxt_lvlhsh_t *hash);


#endif /* _NJS_REGEXP_H_INCLUDED_ */
