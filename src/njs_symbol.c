
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) NGINX, Inc.
 */


#include <njs_main.h>


static const njs_value_t  njs_symbol_async_iterator_name =
                            njs_long_string("Symbol.asyncIterator");
static const njs_value_t  njs_symbol_has_instance_name =
                            njs_long_string("Symbol.hasInstance");
static const njs_value_t  njs_symbol_is_concat_spreadable_name =
                            njs_long_string("Symbol.isConcatSpreadable");
static const njs_value_t  njs_symbol_iterator_name =
                            njs_long_string("Symbol.iterator");
static const njs_value_t  njs_symbol_match_name =
                            njs_string("Symbol.match");
static const njs_value_t  njs_symbol_match_all_name =
                            njs_long_string("Symbol.matchAll");
static const njs_value_t  njs_symbol_replace_name =
                            njs_string("Symbol.replace");
static const njs_value_t  njs_symbol_search_name =
                            njs_string("Symbol.search");
static const njs_value_t  njs_symbol_species_name =
                            njs_string("Symbol.species");
static const njs_value_t  njs_symbol_split_name =
                            njs_string("Symbol.split");
static const njs_value_t  njs_symbol_to_primitive_name =
                            njs_long_string("Symbol.toPrimitive");
static const njs_value_t  njs_symbol_to_string_tag_name =
                            njs_long_string("Symbol.toStringTag");
static const njs_value_t  njs_symbol_unscopables_name =
                            njs_long_string("Symbol.unscopables");


static const njs_value_t  *njs_symbol_names[NJS_SYMBOL_KNOWN_MAX] = {
    &njs_string_invalid,
    &njs_symbol_async_iterator_name,
    &njs_symbol_has_instance_name,
    &njs_symbol_is_concat_spreadable_name,
    &njs_symbol_iterator_name,
    &njs_symbol_match_name,
    &njs_symbol_match_all_name,
    &njs_symbol_replace_name,
    &njs_symbol_search_name,
    &njs_symbol_species_name,
    &njs_symbol_split_name,
    &njs_symbol_to_primitive_name,
    &njs_symbol_to_string_tag_name,
    &njs_symbol_unscopables_name,
};


const njs_value_t *
njs_symbol_description(const njs_value_t *value)
{
    uint32_t  key;

    key = njs_symbol_key(value);

    if (key < NJS_SYMBOL_KNOWN_MAX) {
        return njs_symbol_names[key];
    }

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
        description = &njs_string_empty;
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
    njs_index_t unused)
{
    uint64_t     key;
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

    key = ++vm->symbol_generator;

    if (njs_slow_path(key >= UINT32_MAX)) {
        njs_internal_error(vm, "Symbol generator overflow");
        return NJS_ERROR;
    }

    name = njs_mp_alloc(vm->mem_pool, sizeof(njs_value_t));
    if (njs_slow_path(name == NULL)) {
        njs_memory_error(vm);
        return NJS_ERROR;
    }

    njs_value_assign(name, value);
    njs_set_symbol(&vm->retval, key, name);

    return NJS_OK;
}


static njs_int_t
njs_symbol_for(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t unused)
{
    uint64_t              key;
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
            njs_set_symbol(&vm->retval, node->key, &node->name);
            return NJS_OK;
        }

        rb_node = njs_rbtree_node_successor(&vm->global_symbols, rb_node);
    }

    key = ++vm->symbol_generator;

    if (njs_slow_path(key >= UINT32_MAX)) {
        njs_internal_error(vm, "Symbol generator overflow");
        return NJS_ERROR;
    }

    node = njs_mp_alloc(vm->mem_pool, sizeof(njs_rb_symbol_node_t));
    if (njs_slow_path(node == NULL)) {
        njs_memory_error(vm);
        return NJS_ERROR;
    }

    node->key = key;
    njs_value_assign(&node->name, value);

    njs_rbtree_insert(&vm->global_symbols, &node->node);

    njs_set_symbol(&vm->retval, key, &node->name);

    return NJS_OK;
}


static njs_int_t
njs_symbol_key_for(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t unused)
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

    njs_value_assign(&vm->retval,
                     node != NULL ? &node->name : &njs_value_undefined);

    return NJS_OK;
}


static const njs_object_prop_t  njs_symbol_constructor_properties[] =
{
    NJS_DECLARE_PROP_NAME("Symbol"),

    NJS_DECLARE_PROP_LENGTH(0),

    NJS_DECLARE_PROP_HANDLER("prototype", njs_object_prototype_create, 0, 0, 0),

    NJS_DECLARE_PROP_NATIVE("for", njs_symbol_for, 1, 0),

    NJS_DECLARE_PROP_NATIVE("keyFor", njs_symbol_key_for, 1, 0),

    NJS_DECLARE_PROP_VALUE("asyncIterator",
                           njs_wellknown_symbol(NJS_SYMBOL_ASYNC_ITERATOR), 0),

    NJS_DECLARE_PROP_VALUE("hasInstance",
                           njs_wellknown_symbol(NJS_SYMBOL_HAS_INSTANCE), 0),

    NJS_DECLARE_PROP_LVALUE("isConcatSpreadable",
                     njs_wellknown_symbol(NJS_SYMBOL_IS_CONCAT_SPREADABLE), 0),

    NJS_DECLARE_PROP_VALUE("iterator",
                           njs_wellknown_symbol(NJS_SYMBOL_ITERATOR), 0),

    NJS_DECLARE_PROP_VALUE("match",
                           njs_wellknown_symbol(NJS_SYMBOL_MATCH), 0),

    NJS_DECLARE_PROP_VALUE("matchAll",
                           njs_wellknown_symbol(NJS_SYMBOL_MATCH_ALL), 0),

    NJS_DECLARE_PROP_VALUE("replace",
                           njs_wellknown_symbol(NJS_SYMBOL_REPLACE), 0),

    NJS_DECLARE_PROP_VALUE("search",
                           njs_wellknown_symbol(NJS_SYMBOL_SEARCH), 0),

    NJS_DECLARE_PROP_VALUE("species",
                           njs_wellknown_symbol(NJS_SYMBOL_SPECIES), 0),

    NJS_DECLARE_PROP_VALUE("split",
                           njs_wellknown_symbol(NJS_SYMBOL_SPLIT), 0),

    NJS_DECLARE_PROP_VALUE("toPrimitive",
                           njs_wellknown_symbol(NJS_SYMBOL_TO_PRIMITIVE), 0),

    NJS_DECLARE_PROP_VALUE("toStringTag",
                           njs_wellknown_symbol(NJS_SYMBOL_TO_STRING_TAG), 0),

    NJS_DECLARE_PROP_VALUE("unscopables",
                           njs_wellknown_symbol(NJS_SYMBOL_UNSCOPABLES), 0),
};


const njs_object_init_t  njs_symbol_constructor_init = {
    njs_symbol_constructor_properties,
    njs_nitems(njs_symbol_constructor_properties),
};


static njs_int_t
njs_symbol_prototype_value_of(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t unused)
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

    vm->retval = *value;

    return NJS_OK;
}


static njs_int_t
njs_symbol_prototype_to_string(njs_vm_t *vm, njs_value_t *args,
    njs_uint_t nargs, njs_index_t unused)
{
    njs_int_t  ret;

    ret = njs_symbol_prototype_value_of(vm, args, nargs, unused);
    if (njs_slow_path(ret != NJS_OK)) {
        return ret;
    }

    return njs_symbol_descriptive_string(vm, &vm->retval, &vm->retval);
}


static njs_int_t
njs_symbol_prototype_description(njs_vm_t *vm, njs_value_t *args,
    njs_uint_t nargs, njs_index_t unused)
{
    njs_int_t  ret;

    ret = njs_symbol_prototype_value_of(vm, args, nargs, unused);
    if (njs_slow_path(ret != NJS_OK)) {
        return ret;
    }

    njs_value_assign(&vm->retval, njs_symbol_description(&vm->retval));

    return NJS_OK;
}


static const njs_object_prop_t  njs_symbol_prototype_properties[] =
{
    {
        .type = NJS_PROPERTY,
        .name = njs_wellknown_symbol(NJS_SYMBOL_TO_STRING_TAG),
        .u.value = njs_string("Symbol"),
        .configurable = 1,
    },

    NJS_DECLARE_PROP_HANDLER("__proto__", njs_primitive_prototype_get_proto,
                             0, 0, NJS_OBJECT_PROP_VALUE_CW),

    NJS_DECLARE_PROP_HANDLER("constructor",
                             njs_object_prototype_create_constructor,
                             0, 0, NJS_OBJECT_PROP_VALUE_CW),

    NJS_DECLARE_PROP_NATIVE("valueOf", njs_symbol_prototype_value_of, 0, 0),

    NJS_DECLARE_PROP_NATIVE("toString", njs_symbol_prototype_to_string, 0, 0),

    NJS_DECLARE_PROP_GETTER("description", njs_symbol_prototype_description, 0),
};


const njs_object_init_t  njs_symbol_prototype_init = {
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
