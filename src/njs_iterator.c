
/*
 * Copyright (C) Artem S. Povalyukhin
 * Copyright (C) NGINX, Inc.
 */


#include <njs_main.h>


struct njs_value_iterator_s {
    njs_value_t        target;
    int64_t            next;
    njs_object_enum_t  kind;
};


typedef struct njs_value_iterator_s  njs_array_iterator_t;


static const njs_value_t  string_done = njs_string("done");
static const njs_value_t  string_value = njs_string("value");


njs_int_t
njs_array_iterator_create(njs_vm_t *vm, const njs_value_t *target,
    njs_value_t *retval, njs_object_enum_t kind)
{
    njs_object_value_t    *ov;
    njs_array_iterator_t  *it;

    ov = njs_mp_alloc(vm->mem_pool, sizeof(njs_object_value_t));
    if (njs_slow_path(ov == NULL)) {
        njs_memory_error(vm);
        return NJS_ERROR;
    }

    njs_lvlhsh_init(&ov->object.hash);
    njs_lvlhsh_init(&ov->object.shared_hash);
    ov->object.type = NJS_OBJECT_VALUE;
    ov->object.shared = 0;
    ov->object.extensible = 1;
    ov->object.error_data = 0;
    ov->object.fast_array = 0;

    ov->object.__proto__ =
        &vm->prototypes[NJS_OBJ_TYPE_ARRAY_ITERATOR].object;
    ov->object.slots = NULL;

    it = njs_mp_alloc(vm->mem_pool, sizeof(njs_array_iterator_t));
    if (njs_slow_path(it == NULL)) {
        njs_memory_error(vm);
        return NJS_ERROR;
    }

    /* GC retain it->target */
    it->target = *target;
    it->next = 0;
    it->kind = kind;

    njs_set_data(&ov->value, it, NJS_DATA_TAG_ARRAY_ITERATOR);
    njs_set_object_value(retval, ov);

    return NJS_OK;
}


njs_int_t
njs_array_iterator_next(njs_vm_t *vm, njs_value_t *iterator,
    njs_value_t *retval)
{
    int64_t               length;
    njs_int_t             ret;
    njs_array_t           *array, *entry;
    njs_typed_array_t     *tarray;
    const njs_value_t     *value;
    njs_array_iterator_t  *it;

    if (njs_slow_path(!njs_is_valid(njs_object_value(iterator)))) {
        return NJS_DECLINED;
    }

    it = njs_object_data(iterator);
    value = &njs_value_undefined;

    if (njs_is_fast_array(&it->target)) {
        array = njs_array(&it->target);
        length = array->length;

        if (it->next >= length) {
            goto release;
        }

        if (it->kind > NJS_ENUM_KEYS && njs_is_valid(&array->start[it->next])) {
            value = &array->start[it->next];
        }

    } else if (njs_is_typed_array(&it->target)) {
        tarray = njs_typed_array(&it->target);

        if (njs_slow_path(njs_is_detached_buffer(tarray->buffer))) {
            njs_type_error(vm, "detached buffer");
            return NJS_ERROR;
        }

        length = njs_typed_array_length(tarray);

        if (it->next >= length) {
            goto release;
        }

        if (it->kind > NJS_ENUM_KEYS) {
            njs_set_number(retval, njs_typed_array_prop(tarray, it->next));
            value = retval;
        }

    } else {
        ret = njs_object_length(vm, &it->target, &length);
        if (njs_slow_path(ret == NJS_ERROR)) {
            return ret;
        }

        if (it->next >= length) {
            goto release;
        }

        if (it->kind > NJS_ENUM_KEYS) {
            ret = njs_value_property_i64(vm, &it->target, it->next, retval);
            if (njs_slow_path(ret == NJS_ERROR)) {
                return ret;
            }

            value = njs_is_valid(retval) ? retval
                                         : &njs_value_undefined;
        }
    }

    switch (it->kind) {
    case NJS_ENUM_KEYS:
        njs_set_number(retval, it->next++);
        break;

    case NJS_ENUM_VALUES:
        it->next++;
        *retval = *value;
        break;

    case NJS_ENUM_BOTH:
        entry = njs_array_alloc(vm, 0, 2, 0);
        if (njs_slow_path(entry == NULL)) {
            return NJS_ERROR;
        }

        njs_set_number(&entry->start[0], it->next++);
        entry->start[1] = *value;

        njs_set_array(retval, entry);
        break;

    default:
        njs_internal_error(vm, "invalid enum kind");
        return NJS_ERROR;
    }

    return NJS_OK;

release:

    /* GC release it->target */
    njs_mp_free(vm->mem_pool, it);
    njs_set_invalid(njs_object_value(iterator));

    return NJS_DECLINED;
}


static njs_int_t
njs_iterator_prototype_get_this(njs_vm_t *vm, njs_value_t *args,
    njs_uint_t nargs, njs_index_t unused)
{
    vm->retval = args[0];

    return NJS_OK;
}


static const njs_object_prop_t  njs_iterator_prototype_properties[] =
{
    {
        .type = NJS_PROPERTY,
        .name = njs_wellknown_symbol(NJS_SYMBOL_ITERATOR),
        .value = njs_native_function(njs_iterator_prototype_get_this, 0),
        .configurable = 1,
        .writable = 1,
    },
};


static const njs_object_init_t  njs_iterator_prototype_init = {
    njs_iterator_prototype_properties,
    njs_nitems(njs_iterator_prototype_properties),
};


const njs_object_type_init_t  njs_iterator_type_init = {
    .prototype_props = &njs_iterator_prototype_init,
    .prototype_value = { .object = { .type = NJS_OBJECT } },
};


static njs_int_t
njs_array_iterator_prototype_next(njs_vm_t *vm, njs_value_t *args,
    njs_uint_t nargs, njs_index_t tag)
{
    njs_int_t          ret;
    njs_bool_t         check;
    njs_value_t        *this;
    njs_object_t       *object;
    njs_object_prop_t  *prop_value, *prop_done;

    this = njs_argument(args, 0);

    check = njs_is_object_value(this)
            && (njs_is_object_data(this, NJS_DATA_TAG_ARRAY_ITERATOR)
                || !njs_is_valid(njs_object_value(this)));

    if (njs_slow_path(!check)) {
        njs_type_error(vm, "Method [Array Iterator].prototype.next"
                           " called on incompatible receiver");
        return NJS_ERROR;
    }

    object = njs_object_alloc(vm);
    if (njs_slow_path(object == NULL)) {
        return NJS_ERROR;
    }

    njs_set_object(&vm->retval, object);

    prop_value = njs_object_property_add(vm, &vm->retval,
                                         njs_value_arg(&string_value), 0);
    if (njs_slow_path(prop_value == NULL)) {
        return NJS_ERROR;
    }

    prop_done = njs_object_property_add(vm, &vm->retval,
                                        njs_value_arg(&string_done), 0);
    if (njs_slow_path(prop_done == NULL)) {
        return NJS_ERROR;
    }

    ret = njs_array_iterator_next(vm, this, &prop_value->value);
    if (njs_slow_path(ret == NJS_ERROR)) {
        return ret;
    }

    if (njs_slow_path(ret == NJS_DECLINED)) {
        njs_set_undefined(&prop_value->value);
        njs_set_boolean(&prop_done->value, 1);

        return NJS_OK;
    }

    njs_set_boolean(&prop_done->value, 0);

    return NJS_OK;
}


static const njs_object_prop_t  njs_array_iterator_prototype_properties[] =
{
    {
        .type = NJS_PROPERTY,
        .name = njs_string("next"),
        .value = njs_native_function2(njs_array_iterator_prototype_next, 0,
                                      NJS_DATA_TAG_ARRAY_ITERATOR),
        .configurable = 1,
        .writable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_wellknown_symbol(NJS_SYMBOL_TO_STRING_TAG),
        .value = njs_string("Array Iterator"),
        .configurable = 1,
    },
};


static const njs_object_init_t  njs_array_iterator_prototype_init = {
    njs_array_iterator_prototype_properties,
    njs_nitems(njs_array_iterator_prototype_properties),
};


const njs_object_type_init_t  njs_array_iterator_type_init = {
    .prototype_props = &njs_array_iterator_prototype_init,
    .prototype_value = { .object = { .type = NJS_OBJECT } },
};
