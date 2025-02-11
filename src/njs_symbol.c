
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) NGINX, Inc.
 */


#include <njs_main.h>


const njs_value_t *
njs_symbol_description(const njs_value_t *value)
{
    return value->data.u.value != NULL ? value->data.u.value
                                       : &njs_value_undefined;
}


njs_int_t
njs_symbol_descriptive_string(njs_vm_t *vm, njs_value_t *dst,
    const njs_value_t *value)
{
    u_char             *start;
    const njs_value_t  *description;
    njs_string_prop_t  string;

    description = njs_symbol_description(value);

    if (njs_is_undefined(description)) {
        description = &njs_atom.vs_;
    }

    (void) njs_string_prop(&string, description);

    string.length += njs_length("Symbol()");

    start = njs_string_alloc(vm, dst, string.size + 8, string.length);
    if (njs_slow_path(start == NULL)) {
        return NJS_ERROR;
    }

    start = njs_cpymem(start, "Symbol(", 7);
    start = njs_cpymem(start, string.start, string.size);
    *start = ')';

    return NJS_OK;
}


static njs_int_t
njs_symbol_constructor(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t unused, njs_value_t *retval)
{
    njs_int_t    ret;
    njs_value_t  *value, *name;

    if (njs_slow_path(vm->top_frame->ctor)) {
        njs_type_error(vm, "Symbol is not a constructor");
        return NJS_ERROR;
    }

    value = njs_arg(args, nargs, 1);

    if (njs_is_defined(value)) {
        if (njs_slow_path(!njs_is_string(value))) {
            ret = njs_value_to_string(vm, value, value);
            if (njs_slow_path(ret != NJS_OK)) {
                return ret;
            }
        }
    }

    name = njs_mp_alloc(vm->mem_pool, sizeof(njs_value_t));
    if (njs_slow_path(name == NULL)) {
        njs_memory_error(vm);
        return NJS_ERROR;
    }

    njs_value_assign(name, value);
    njs_set_symbol(retval, 0, name);

    ret = njs_atom_atomize_key_s(vm, retval);
    if (ret != NJS_OK) {
        return NJS_ERROR;
    }

    return NJS_OK;
}


static njs_int_t
njs_symbol_for(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t unused, njs_value_t *retval)
{
    njs_int_t             ret;
    njs_value_t           *value, lvalue;
    njs_rbtree_node_t     *rb_node;
    njs_rb_symbol_node_t  *node;

    value = njs_lvalue_arg(&lvalue, args, nargs, 1);

    if (njs_slow_path(!njs_is_string(value))) {
        ret = njs_value_to_string(vm, value, value);
        if (njs_slow_path(ret != NJS_OK)) {
            return ret;
        }
    }

    rb_node = njs_rbtree_min(&vm->global_symbols);

    while (njs_rbtree_is_there_successor(&vm->global_symbols, rb_node)) {

        node = (njs_rb_symbol_node_t *) rb_node;

        if (njs_is_string(&node->name)
            && njs_string_cmp(value, &node->name) == 0)
        {
            njs_set_symbol(retval, node->key, &node->name);
            return NJS_OK;
        }

        rb_node = njs_rbtree_node_successor(&vm->global_symbols, rb_node);
    }

    node = njs_mp_alloc(vm->mem_pool, sizeof(njs_rb_symbol_node_t));
    if (njs_slow_path(node == NULL)) {
        njs_memory_error(vm);
        return NJS_ERROR;
    }

    njs_value_assign(&node->name, value);

    njs_set_symbol(retval, 0, &node->name);
    ret = njs_atom_atomize_key_s(vm, retval);
    if (ret != NJS_OK) {
        return NJS_ERROR;
    }
    node->key = retval->atom_id;

    njs_rbtree_insert(&vm->global_symbols, &node->node);

    return NJS_OK;
}


static njs_int_t
njs_symbol_key_for(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t unused, njs_value_t *retval)
{
    njs_value_t           *value;
    njs_rb_symbol_node_t  query, *node;

    value = njs_arg(args, nargs, 1);

    if (njs_slow_path(!njs_is_symbol(value))) {
        njs_type_error(vm, "is not a symbol");
        return NJS_ERROR;
    }

    query.key = njs_symbol_key(value);
    node = (njs_rb_symbol_node_t *) njs_rbtree_find(&vm->global_symbols,
                                                    &query.node);

    njs_value_assign(retval,
                     node != NULL ? &node->name : &njs_value_undefined);

    return NJS_OK;
}


static njs_object_prop_t  njs_symbol_constructor_properties[] =
{
    NJS_DECLARE_PROP_LENGTH(0),

    NJS_DECLARE_PROP_NAME(vs_Symbol),

    NJS_DECLARE_PROP_HANDLER(vs_prototype, njs_object_prototype_create,
                             0, 0),

    NJS_DECLARE_PROP_NATIVE(vs_for, njs_symbol_for, 1, 0),

    NJS_DECLARE_PROP_NATIVE(vs_keyFor, njs_symbol_key_for, 1, 0),

    NJS_DECLARE_PROP_VALUE(vs_asyncIterator,
                           njs_atom.vw_asyncIterator, 0),

    NJS_DECLARE_PROP_VALUE(vs_hasInstance, njs_atom.vw_hasInstance, 0),

    NJS_DECLARE_PROP_VALUE(vs_isConcatSpreadable,
                           njs_atom.vw_isConcatSpreadable, 0),

    NJS_DECLARE_PROP_VALUE(vs_iterator, njs_atom.vw_iterator, 0),

    NJS_DECLARE_PROP_VALUE(vs_match, njs_atom.vw_match, 0),

    NJS_DECLARE_PROP_VALUE(vs_matchAll, njs_atom.vw_matchAll, 0),

    NJS_DECLARE_PROP_VALUE(vs_replace, njs_atom.vw_replace, 0),

    NJS_DECLARE_PROP_VALUE(vs_search, njs_atom.vw_search, 0),

    NJS_DECLARE_PROP_VALUE(vs_species, njs_atom.vw_species, 0),

    NJS_DECLARE_PROP_VALUE(vs_split, njs_atom.vw_split, 0),

    NJS_DECLARE_PROP_VALUE(vs_toPrimitive, njs_atom.vw_toPrimitive, 0),

    NJS_DECLARE_PROP_VALUE(vs_toStringTag, njs_atom.vw_toStringTag, 0),

    NJS_DECLARE_PROP_VALUE(vs_unscopables, njs_atom.vw_unscopables, 0),
};


static const njs_object_init_t  njs_symbol_constructor_init = {
    njs_symbol_constructor_properties,
    njs_nitems(njs_symbol_constructor_properties),
};


static njs_int_t
njs_symbol_prototype_value_of(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t unused, njs_value_t *retval)
{
    njs_value_t  *value;

    value = &args[0];

    if (value->type != NJS_SYMBOL) {

        if (njs_is_object_symbol(value)) {
            value = njs_object_value(value);

        } else {
            njs_type_error(vm, "unexpected value type:%s",
                           njs_type_string(value->type));

            return NJS_ERROR;
        }
    }

    njs_value_assign(retval, value);

    return NJS_OK;
}


static njs_int_t
njs_symbol_prototype_to_string(njs_vm_t *vm, njs_value_t *args,
    njs_uint_t nargs, njs_index_t unused, njs_value_t *retval)
{
    njs_int_t  ret;

    ret = njs_symbol_prototype_value_of(vm, args, nargs, unused, retval);
    if (njs_slow_path(ret != NJS_OK)) {
        return ret;
    }

    return njs_symbol_descriptive_string(vm, retval, retval);
}


static njs_int_t
njs_symbol_prototype_description(njs_vm_t *vm, njs_value_t *args,
    njs_uint_t nargs, njs_index_t unused, njs_value_t *retval)
{
    njs_int_t  ret;

    ret = njs_symbol_prototype_value_of(vm, args, nargs, unused, retval);
    if (njs_slow_path(ret != NJS_OK)) {
        return ret;
    }

    njs_value_assign(retval, njs_symbol_description(retval));

    return NJS_OK;
}


static njs_object_prop_t  njs_symbol_prototype_properties[] =
{
    NJS_DECLARE_PROP_VALUE(vw_toStringTag, njs_atom.vs_Symbol,
                           NJS_OBJECT_PROP_VALUE_C),

    NJS_DECLARE_PROP_HANDLER(vs___proto__,
                             njs_primitive_prototype_get_proto, 0,
                             NJS_OBJECT_PROP_VALUE_CW),

    NJS_DECLARE_PROP_HANDLER(vs_constructor,
                             njs_object_prototype_create_constructor, 0,
                             NJS_OBJECT_PROP_VALUE_CW),

    NJS_DECLARE_PROP_NATIVE(vs_valueOf, njs_symbol_prototype_value_of,
                            0, 0),

    NJS_DECLARE_PROP_NATIVE(vs_toString,
                            njs_symbol_prototype_to_string, 0, 0),

    NJS_DECLARE_PROP_GETTER(vs_description,
                            njs_symbol_prototype_description, 0),
};


static const njs_object_init_t  njs_symbol_prototype_init = {
    njs_symbol_prototype_properties,
    njs_nitems(njs_symbol_prototype_properties),
};


const njs_object_type_init_t  njs_symbol_type_init = {
   .constructor = njs_native_ctor(njs_symbol_constructor, 0, 0),
   .constructor_props = &njs_symbol_constructor_init,
   .prototype_props = &njs_symbol_prototype_init,
   .prototype_value = { .object = { .type = NJS_OBJECT } },
};


intptr_t
njs_symbol_rbtree_cmp(njs_rbtree_node_t *node1, njs_rbtree_node_t *node2)
{
    njs_rb_symbol_node_t  *item1, *item2;

    item1 = (njs_rb_symbol_node_t *) node1;
    item2 = (njs_rb_symbol_node_t *) node2;

    if (item1->key < item2->key) {
        return -1;
    }

    if (item1->key == item2->key) {
        return 0;
    }

    return 1;
}
