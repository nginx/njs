
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
    const njs_value_t  *name;

    if (njs_symbol_key(value) < NJS_SYMBOL_KNOWN_MAX) {
        name = njs_symbol_names[njs_symbol_key(value)];

    } else {
        name = value->data.u.value;

        if (name == NULL) {
            return  &njs_value_undefined;
        }
    }

    return name;
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
    njs_int_t    ret;
    njs_value_t  *value, *name;
    uint64_t     key;

    if (njs_slow_path(vm->top_frame->ctor)) {
        njs_type_error(vm, "Symbol is not a constructor");
        return NJS_ERROR;
    }

    value = njs_arg(args, nargs, 1);

    if (njs_is_undefined(value)) {
        name = NULL;

    } else {
        if (njs_slow_path(!njs_is_string(value))) {
            ret = njs_value_to_string(vm, value, value);
            if (njs_slow_path(ret != NJS_OK)) {
                return ret;
            }
        }

        name = njs_mp_alloc(vm->mem_pool, sizeof(njs_value_t));
        if (njs_slow_path(name == NULL)) {
            njs_memory_error(vm);
            return NJS_ERROR;
        }

        /* GC: retain */
        *name = *value;
    }

    key = ++vm->symbol_generator;

    if (njs_slow_path(key >= UINT32_MAX)) {
        njs_internal_error(vm, "Symbol generator overflow");
        return NJS_ERROR;
    }

    vm->retval.type = NJS_SYMBOL;
    vm->retval.data.truth = 1;
    vm->retval.data.magic32 = key;
    vm->retval.data.u.value = name;

    return NJS_OK;
}


static njs_int_t
njs_symbol_for(njs_vm_t *vm, njs_value_t *args,
    njs_uint_t nargs, njs_index_t unused)
{
    njs_internal_error(vm, "not implemented");

    return NJS_ERROR;
}


static njs_int_t
njs_symbol_key_for(njs_vm_t *vm, njs_value_t *args,
    njs_uint_t nargs, njs_index_t unused)
{
    njs_internal_error(vm, "not implemented");

    return NJS_ERROR;
}


static const njs_object_prop_t  njs_symbol_constructor_properties[] =
{
    {
        .type = NJS_PROPERTY,
        .name = njs_string("name"),
        .value = njs_string("Symbol"),
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("length"),
        .value = njs_value(NJS_NUMBER, 0, 0.0),
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY_HANDLER,
        .name = njs_string("prototype"),
        .value = njs_prop_handler(njs_object_prototype_create),
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("for"),
        .value = njs_native_function(njs_symbol_for, 1),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("keyFor"),
        .value = njs_native_function(njs_symbol_key_for, 1),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("asyncIterator"),
        .value = njs_wellknown_symbol(NJS_SYMBOL_ASYNC_ITERATOR),
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("hasInstance"),
        .value = njs_wellknown_symbol(NJS_SYMBOL_HAS_INSTANCE),
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_long_string("isConcatSpreadable"),
        .value = njs_wellknown_symbol(NJS_SYMBOL_IS_CONCAT_SPREADABLE),
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("iterator"),
        .value = njs_wellknown_symbol(NJS_SYMBOL_ITERATOR),
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("match"),
        .value = njs_wellknown_symbol(NJS_SYMBOL_MATCH),
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("matchAll"),
        .value = njs_wellknown_symbol(NJS_SYMBOL_MATCH_ALL),
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("replace"),
        .value = njs_wellknown_symbol(NJS_SYMBOL_REPLACE),
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("search"),
        .value = njs_wellknown_symbol(NJS_SYMBOL_SEARCH),
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("species"),
        .value = njs_wellknown_symbol(NJS_SYMBOL_SPECIES),
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("split"),
        .value = njs_wellknown_symbol(NJS_SYMBOL_SPLIT),
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("toPrimitive"),
        .value = njs_wellknown_symbol(NJS_SYMBOL_TO_PRIMITIVE),
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("toStringTag"),
        .value = njs_wellknown_symbol(NJS_SYMBOL_TO_STRING_TAG),
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("unscopables"),
        .value = njs_wellknown_symbol(NJS_SYMBOL_UNSCOPABLES),
    },

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

    vm->retval = *njs_symbol_description(&vm->retval);

    return NJS_OK;
}


static const njs_object_prop_t  njs_symbol_prototype_properties[] =
{
    {
        .type = NJS_PROPERTY,
        .name = njs_wellknown_symbol(NJS_SYMBOL_TO_STRING_TAG),
        .value = njs_string("Symbol"),
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY_HANDLER,
        .name = njs_string("__proto__"),
        .value = njs_prop_handler(njs_primitive_prototype_get_proto),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY_HANDLER,
        .name = njs_string("constructor"),
        .value = njs_prop_handler(njs_object_prototype_create_constructor),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("valueOf"),
        .value = njs_native_function(njs_symbol_prototype_value_of, 0),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("toString"),
        .value = njs_native_function(njs_symbol_prototype_to_string, 0),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("description"),
        .value = njs_value(NJS_INVALID, 1, NAN),
        .getter = njs_native_function(njs_symbol_prototype_description, 0),
        .setter = njs_value(NJS_UNDEFINED, 0, NAN),
        .writable = NJS_ATTRIBUTE_UNSET,
        .configurable = 1,
    },
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
