
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


static njs_int_t njs_iterator_object_handler(njs_vm_t *vm,
    njs_iterator_handler_t handler, njs_iterator_args_t *args,
    njs_value_t *key, int64_t i);

static njs_int_t njs_iterator_to_array_handler(njs_vm_t *vm,
    njs_iterator_args_t *args, njs_value_t *value, int64_t index);


njs_int_t
njs_array_iterator_create(njs_vm_t *vm, const njs_value_t *target,
    njs_value_t *retval, njs_object_enum_t kind)
{
    njs_object_value_t    *iterator;
    njs_array_iterator_t  *it;

    iterator = njs_object_value_alloc(vm, NJS_OBJ_TYPE_ARRAY_ITERATOR, 0, NULL);
    if (njs_slow_path(iterator == NULL)) {
        njs_memory_error(vm);
        return NJS_ERROR;
    }

    it = njs_mp_alloc(vm->mem_pool, sizeof(njs_array_iterator_t));
    if (njs_slow_path(it == NULL)) {
        njs_memory_error(vm);
        return NJS_ERROR;
    }

    /* GC retain it->target */
    it->target = *target;
    it->next = 0;
    it->kind = kind;

    njs_set_data(&iterator->value, it, NJS_DATA_TAG_ARRAY_ITERATOR);
    njs_set_object_value(retval, iterator);

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


njs_int_t
njs_object_iterate(njs_vm_t *vm, njs_iterator_args_t *args,
    njs_iterator_handler_t handler)
{
    double              idx;
    int64_t             length, i, from, to;
    njs_int_t           ret;
    njs_array_t         *array, *keys;
    njs_value_t         *value, *entry, prop, character, string_obj;
    const u_char        *p, *end, *pos;
    njs_string_prop_t   string_prop;
    njs_object_value_t  *object;

    value = args->value;
    from = args->from;
    to = args->to;

    if (njs_is_array(value)) {
        array = njs_array(value);

        for (; from < to; from++) {
            if (njs_slow_path(!array->object.fast_array)) {
                goto process_object;
            }

            if (njs_fast_path(from < array->length
                              && njs_is_valid(&array->start[from])))
            {
                ret = handler(vm, args, &array->start[from], from);

            } else {
                entry = njs_value_arg(&njs_value_invalid);
                ret = njs_value_property_i64(vm, value, from, &prop);
                if (njs_slow_path(ret != NJS_DECLINED)) {
                    if (ret == NJS_ERROR) {
                        return NJS_ERROR;
                    }

                    entry = &prop;
                }

                ret = handler(vm, args, entry, from);
            }

            if (njs_slow_path(ret != NJS_OK)) {
                if (ret == NJS_DONE) {
                    return NJS_DONE;
                }

                return NJS_ERROR;
            }
        }

        return NJS_OK;
    }

    if (njs_is_string(value) || njs_is_object_string(value)) {

        if (njs_is_string(value)) {
            object = njs_object_value_alloc(vm, NJS_OBJ_TYPE_STRING, 0, value);
            if (njs_slow_path(object == NULL)) {
                return NJS_ERROR;
            }

            njs_set_object_value(&string_obj, object);

            args->value = &string_obj;
        }
        else {
            value = njs_object_value(value);
        }

        length = njs_string_prop(&string_prop, value);

        p = string_prop.start;
        end = p + string_prop.size;

        if ((size_t) length == string_prop.size) {
            /* Byte or ASCII string. */

            for (i = from; i < to; i++) {
                /* This cannot fail. */
                (void) njs_string_new(vm, &character, p + i, 1, 1);

                ret = handler(vm, args, &character, i);
                if (njs_slow_path(ret != NJS_OK)) {
                    if (ret == NJS_DONE) {
                        return NJS_DONE;
                    }

                    return NJS_ERROR;
                }
            }

        } else {
            /* UTF-8 string. */

            for (i = from; i < to; i++) {
                pos = njs_utf8_next(p, end);

                /* This cannot fail. */
                (void) njs_string_new(vm, &character, p, pos - p, 1);

                ret = handler(vm, args, &character, i);
                if (njs_slow_path(ret != NJS_OK)) {
                    if (ret == NJS_DONE) {
                        return NJS_DONE;
                    }

                    return NJS_ERROR;
                }

                p = pos;
            }
        }

        return NJS_OK;
    }

    if (!njs_is_object(value)) {
        return NJS_OK;
    }

process_object:

    if (!njs_fast_object(to - from)) {
        keys = njs_array_indices(vm, value);
        if (njs_slow_path(keys == NULL)) {
            return NJS_ERROR;
        }

        for (i = 0; i < keys->length; i++) {
            idx = njs_string_to_index(&keys->start[i]);

            if (idx < from || idx >= to) {
                continue;
            }

            ret = njs_iterator_object_handler(vm, handler, args, &keys->start[i],
                                           idx);
            if (njs_slow_path(ret != NJS_OK)) {
                njs_array_destroy(vm, keys);
                return ret;
            }
        }

        njs_array_destroy(vm, keys);

        return NJS_OK;
    }

    for (i = from; i < to; i++) {
        ret = njs_iterator_object_handler(vm, handler, args, NULL, i);
        if (njs_slow_path(ret != NJS_OK)) {
            return ret;
        }
    }

    return NJS_OK;
}


njs_int_t
njs_object_iterate_reverse(njs_vm_t *vm, njs_iterator_args_t *args,
    njs_iterator_handler_t handler)
{
    double              idx;
    int64_t             i, from, to, length;
    njs_int_t           ret;
    njs_array_t         *array, *keys;
    njs_value_t         *entry, *value, prop, character, string_obj;
    const u_char        *p, *end, *pos;
    njs_string_prop_t   string_prop;
    njs_object_value_t  *object;

    value = args->value;
    from = args->from;
    to = args->to;

    if (njs_is_array(value)) {
        array = njs_array(value);

        from += 1;

        while (from-- > to) {
            if (njs_slow_path(!array->object.fast_array)) {
                goto process_object;
            }

            if (njs_fast_path(from < array->length
                              && njs_is_valid(&array->start[from])))
            {
                ret = handler(vm, args, &array->start[from], from);

            } else {
                entry = njs_value_arg(&njs_value_invalid);
                ret = njs_value_property_i64(vm, value, from, &prop);
                if (njs_slow_path(ret != NJS_DECLINED)) {
                    if (ret == NJS_ERROR) {
                        return NJS_ERROR;
                    }

                    entry = &prop;
                }

                ret = handler(vm, args, entry, from);
            }

            if (njs_slow_path(ret != NJS_OK)) {
                if (ret == NJS_DONE) {
                    return NJS_DONE;
                }

                return NJS_ERROR;
            }
        }

        return NJS_OK;
    }

    if (njs_is_string(value) || njs_is_object_string(value)) {

        if (njs_is_string(value)) {
            object = njs_object_value_alloc(vm, NJS_OBJ_TYPE_STRING, 0, value);
            if (njs_slow_path(object == NULL)) {
                return NJS_ERROR;
            }

            njs_set_object_value(&string_obj, object);

            args->value = &string_obj;
        }
        else {
            value = njs_object_value(value);
        }

        length = njs_string_prop(&string_prop, value);
        end = string_prop.start + string_prop.size;

        if ((size_t) length == string_prop.size) {
            /* Byte or ASCII string. */

            p = string_prop.start + from;

            i = from + 1;

            while (i-- > to) {
                /* This cannot fail. */
                (void) njs_string_new(vm, &character, p, 1, 1);

                ret = handler(vm, args, &character, i);
                if (njs_slow_path(ret != NJS_OK)) {
                    if (ret == NJS_DONE) {
                        return NJS_DONE;
                    }

                    return NJS_ERROR;
                }

                p--;
            }

        } else {
            /* UTF-8 string. */

            p = NULL;
            i = from + 1;

            if (i > to) {
                p = njs_string_offset(string_prop.start, end, from);
                p = njs_utf8_next(p, end);
            }

            while (i-- > to) {
                pos = njs_utf8_prev(p);

                /* This cannot fail. */
                (void) njs_string_new(vm, &character, pos, p - pos , 1);

                ret = handler(vm, args, &character, i);
                if (njs_slow_path(ret != NJS_OK)) {
                    if (ret == NJS_DONE) {
                        return NJS_DONE;
                    }

                    return NJS_ERROR;
                }

                p = pos;
            }
        }

        return NJS_OK;
    }

    if (!njs_is_object(value)) {
        return NJS_OK;
    }

process_object:

    if (!njs_fast_object(from - to)) {
        keys = njs_array_indices(vm, value);
        if (njs_slow_path(keys == NULL)) {
            return NJS_ERROR;
        }

        i = keys->length;

        while (i > 0) {
            idx = njs_string_to_index(&keys->start[--i]);

            if (idx < to || idx > from) {
                continue;
            }

            ret = njs_iterator_object_handler(vm, handler, args,
                                              &keys->start[i], idx);
            if (njs_slow_path(ret != NJS_OK)) {
                njs_array_destroy(vm, keys);
                return ret;
            }
        }

        njs_array_destroy(vm, keys);

        return NJS_OK;
    }

    i = from + 1;

    while (i-- > to) {
        ret = njs_iterator_object_handler(vm, handler, args, NULL, i);
        if (njs_slow_path(ret != NJS_OK)) {
            return ret;
        }
    }

    return NJS_OK;
}


static njs_int_t
njs_iterator_object_handler(njs_vm_t *vm, njs_iterator_handler_t handler,
    njs_iterator_args_t *args, njs_value_t *key, int64_t i)
{
    njs_int_t    ret;
    njs_value_t  prop, *entry;

    if (key != NULL) {
        ret = njs_value_property(vm, args->value, key, &prop);
        if (njs_slow_path(ret == NJS_ERROR)) {
            return ret;
        }

    } else {
        ret = njs_value_property_i64(vm, args->value, i, &prop);
        if (njs_slow_path(ret == NJS_ERROR)) {
            return ret;
        }
    }

    entry = (ret == NJS_OK) ? &prop : njs_value_arg(&njs_value_invalid);

    ret = handler(vm, args, entry, i);
    if (njs_slow_path(ret != NJS_OK)) {
        if (ret == NJS_DONE) {
            return NJS_DONE;
        }

        return NJS_ERROR;
    }

    return ret;
}


njs_array_t *
njs_iterator_to_array(njs_vm_t *vm, njs_value_t *iterator)
{
    int64_t              length;
    njs_int_t            ret;
    njs_iterator_args_t  args;

    njs_memzero(&args, sizeof(njs_iterator_args_t));

    ret = njs_object_length(vm, iterator, &length);
    if (njs_slow_path(ret != NJS_OK)) {
        return NULL;
    }

    args.data = njs_array_alloc(vm, 0, 0,
                                njs_min(length, NJS_ARRAY_LARGE_OBJECT_LENGTH));
    if (njs_slow_path(args.data == NULL)) {
        return NULL;
    }

    args.value = iterator;
    args.to = length;

    ret = njs_object_iterate(vm, &args, njs_iterator_to_array_handler);
    if (njs_slow_path(ret == NJS_ERROR)) {
        njs_mp_free(vm->mem_pool, args.data);
        return NULL;
    }

    return args.data;
}


static njs_int_t
njs_iterator_to_array_handler(njs_vm_t *vm, njs_iterator_args_t *args,
    njs_value_t *value, int64_t index)
{
    njs_value_t  array;

    njs_set_array(&array, args->data);

    return njs_value_property_i64_set(vm, &array, index, value);
}
