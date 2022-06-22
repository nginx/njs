
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) NGINX, Inc.
 */

#ifndef _NJS_SYMBOL_H_INCLUDED_
#define _NJS_SYMBOL_H_INCLUDED_


typedef struct {
    NJS_RBTREE_NODE  (node);
    uint32_t         key;
    njs_value_t      name;
} njs_rb_symbol_node_t;


const njs_value_t *njs_symbol_description(const njs_value_t *value);
njs_int_t njs_symbol_descriptive_string(njs_vm_t *vm, njs_value_t *dst,
    const njs_value_t *value);
intptr_t njs_symbol_rbtree_cmp(njs_rbtree_node_t *node1,
    njs_rbtree_node_t *node2);


extern const njs_object_type_init_t  njs_symbol_type_init;


#endif /* _NJS_SYMBOL_H_INCLUDED_ */
