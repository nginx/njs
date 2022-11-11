
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) NGINX, Inc.
 */


#include <njs_main.h>


static njs_object_prop_t *njs_object_prop_alloc2(njs_vm_t *vm,
    const njs_value_t *name, njs_object_prop_type_t type, unsigned flags);
static njs_object_prop_t *njs_descriptor_prop(njs_vm_t *vm,
    const njs_value_t *name, const njs_value_t *desc);


njs_object_prop_t *
njs_object_prop_alloc(njs_vm_t *vm, const njs_value_t *name,
    const njs_value_t *value, uint8_t attributes)
{
    unsigned           flags;
    njs_object_prop_t  *prop;

    switch (attributes) {
    case NJS_ATTRIBUTE_FALSE:
    case NJS_ATTRIBUTE_TRUE:
        flags = attributes ? NJS_OBJECT_PROP_VALUE_ECW : 0;
        break;

    case NJS_ATTRIBUTE_UNSET:
    default:
        flags = NJS_OBJECT_PROP_UNSET;
        break;
    }

    prop = njs_object_prop_alloc2(vm, name, NJS_PROPERTY, flags);
    if (njs_slow_path(prop == NULL)) {
        return NULL;
    }

    njs_value_assign(njs_prop_value(prop), value);

    return prop;
}


static njs_object_prop_t *
njs_object_prop_alloc2(njs_vm_t *vm, const njs_value_t *name,
    njs_object_prop_type_t type, unsigned flags)
{
    njs_int_t          ret;
    njs_object_prop_t  *prop;

    prop = njs_mp_align(vm->mem_pool, sizeof(njs_value_t),
                        sizeof(njs_object_prop_t));
    if (njs_slow_path(prop == NULL)) {
        njs_memory_error(vm);
        return NULL;
    }

    njs_value_assign(&prop->name, name);

    if (njs_slow_path(!njs_is_key(&prop->name))) {
        ret = njs_value_to_key(vm, &prop->name, &prop->name);
        if (njs_slow_path(ret != NJS_OK)) {
            return NULL;
        }
    }

    prop->type = type;

    if (flags != NJS_OBJECT_PROP_UNSET) {
        prop->enumerable = !!(flags & NJS_OBJECT_PROP_ENUMERABLE);
        prop->configurable = !!(flags & NJS_OBJECT_PROP_CONFIGURABLE);

        if (type == NJS_PROPERTY) {
            prop->writable = !!(flags & NJS_OBJECT_PROP_WRITABLE);

        } else {
            prop->writable = NJS_ATTRIBUTE_UNSET;
        }

    } else {
        prop->enumerable = NJS_ATTRIBUTE_UNSET;
        prop->configurable = NJS_ATTRIBUTE_UNSET;
        prop->writable = NJS_ATTRIBUTE_UNSET;
    }

    return prop;
}


njs_int_t
njs_object_property(njs_vm_t *vm, const njs_value_t *value,
    njs_lvlhsh_query_t *lhq, njs_value_t *retval)
{
    njs_int_t          ret;
    njs_object_t       *object;
    njs_object_prop_t  *prop;

    object = njs_object(value);

    do {
        ret = njs_lvlhsh_find(&object->hash, lhq);

        if (njs_fast_path(ret == NJS_OK)) {
            goto found;
        }

        ret = njs_lvlhsh_find(&object->shared_hash, lhq);

        if (njs_fast_path(ret == NJS_OK)) {
            goto found;
        }

        object = object->__proto__;

    } while (object != NULL);

    njs_set_undefined(retval);

    return NJS_DECLINED;

found:

    prop = lhq->value;

    if (njs_is_data_descriptor(prop)) {
        njs_value_assign(retval, njs_prop_value(prop));
        return NJS_OK;
    }

    if (njs_prop_getter(prop) == NULL) {
        njs_set_undefined(retval);
        return NJS_OK;
    }

    return njs_function_apply(vm, njs_prop_getter(prop), value, 1, retval);
}


njs_object_prop_t *
njs_object_property_add(njs_vm_t *vm, njs_value_t *object, njs_value_t *key,
    njs_bool_t replace)
{
    njs_int_t           ret;
    njs_value_t         key_value;
    njs_object_prop_t   *prop;
    njs_lvlhsh_query_t  lhq;

    prop = njs_object_prop_alloc(vm, key, &njs_value_invalid, 1);
    if (njs_slow_path(prop == NULL)) {
        return NULL;
    }

    ret = njs_value_to_key(vm, &key_value, key);
    if (njs_slow_path(ret != NJS_OK)) {
        return NULL;
    }

    lhq.proto = &njs_object_hash_proto;
    njs_string_get(&key_value, &lhq.key);
    lhq.key_hash = njs_djb_hash(lhq.key.start, lhq.key.length);
    lhq.value = prop;
    lhq.replace = replace;
    lhq.pool = vm->mem_pool;

    ret = njs_lvlhsh_insert(njs_object_hash(object), &lhq);
    if (njs_slow_path(ret != NJS_OK)) {
        njs_internal_error(vm, "lvlhsh insert failed");
        return NULL;
    }

    return prop;
}


/*
 * ES5.1, 8.12.9: [[DefineOwnProperty]]
 */
njs_int_t
njs_object_prop_define(njs_vm_t *vm, njs_value_t *object,
    njs_value_t *name, njs_value_t *value, unsigned flags, uint32_t hash)
{
    uint32_t              length, index;
    njs_int_t             ret;
    njs_array_t           *array;
    njs_object_prop_t     *prop, *prev;
    njs_property_query_t  pq;

    static const njs_str_t  length_key = njs_str("length");

    if (njs_slow_path(!njs_is_index_or_key(name))) {
        ret = njs_value_to_key(vm, name, name);
        if (njs_slow_path(ret != NJS_OK)) {
            return ret;
        }
    }

again:

    njs_property_query_init(&pq, NJS_PROPERTY_QUERY_SET, hash, 1);

    ret = (flags & NJS_OBJECT_PROP_CREATE)
                ? NJS_DECLINED
                : njs_property_query(vm, &pq, object, name);

    if (njs_slow_path(ret == NJS_ERROR)) {
        return ret;
    }

    switch (njs_prop_type(flags)) {
    case NJS_OBJECT_PROP_DESCRIPTOR:
        prop = njs_descriptor_prop(vm, name, value);
        if (njs_slow_path(prop == NULL)) {
            return NJS_ERROR;
        }

        break;

    case NJS_OBJECT_PROP_VALUE:
        if (((flags & NJS_OBJECT_PROP_VALUE_ECW) == NJS_OBJECT_PROP_VALUE_ECW)
            && njs_is_fast_array(object)
            && njs_is_number(name))
        {
            if (njs_number_is_integer_index(njs_number(name))) {
                array = njs_array(object);
                index = (uint32_t) njs_number(name);

                if (index < array->length) {
                    njs_value_assign(&array->start[index], value);
                    return NJS_OK;
                }
            }
        }

        prop = njs_object_prop_alloc2(vm, name, NJS_PROPERTY,
                                      flags & NJS_OBJECT_PROP_VALUE_ECW);
        if (njs_slow_path(prop == NULL)) {
            return NJS_ERROR;
        }

        njs_value_assign(njs_prop_value(prop), value);
        break;

    case NJS_OBJECT_PROP_GETTER:
    case NJS_OBJECT_PROP_SETTER:
    default:
        njs_assert(njs_is_function(value));

        prop = njs_object_prop_alloc2(vm, name, NJS_ACCESSOR,
                                      NJS_OBJECT_PROP_VALUE_EC);
        if (njs_slow_path(prop == NULL)) {
            return NJS_ERROR;
        }

        if (njs_prop_type(flags) == NJS_OBJECT_PROP_GETTER) {
            njs_prop_getter(prop) = njs_function(value);
            njs_prop_setter(prop) = NJS_PROP_PTR_UNSET;

        } else {
            njs_prop_getter(prop) = NJS_PROP_PTR_UNSET;
            njs_prop_setter(prop) = njs_function(value);
        }

        break;
    }

    if (njs_fast_path(ret == NJS_DECLINED)) {

set_prop:

        if (!njs_object(object)->extensible) {
            njs_key_string_get(vm, name,  &pq.lhq.key);
            njs_type_error(vm, "Cannot add property \"%V\", "
                           "object is not extensible", &pq.lhq.key);
            return NJS_ERROR;
        }

        if (njs_slow_path(njs_is_typed_array(object)
                          && njs_is_string(name)))
        {
            /* Integer-Indexed Exotic Objects [[DefineOwnProperty]]. */
            if (!isnan(njs_string_to_index(name))) {
                njs_type_error(vm, "Invalid typed array index");
                return NJS_ERROR;
            }
        }

        /* 6.2.5.6 CompletePropertyDescriptor */

        if (njs_is_accessor_descriptor(prop)) {
            if (njs_prop_getter(prop) == NJS_PROP_PTR_UNSET) {
                njs_prop_getter(prop) = NULL;
            }

            if (njs_prop_setter(prop) == NJS_PROP_PTR_UNSET) {
                njs_prop_setter(prop) = NULL;
            }

        } else {
            if (prop->writable == NJS_ATTRIBUTE_UNSET) {
                prop->writable = 0;
            }

            if (!njs_is_valid(njs_prop_value(prop))) {
                njs_set_undefined(njs_prop_value(prop));
            }
        }

        if (prop->enumerable == NJS_ATTRIBUTE_UNSET) {
            prop->enumerable = 0;
        }

        if (prop->configurable == NJS_ATTRIBUTE_UNSET) {
            prop->configurable = 0;
        }

        if (njs_slow_path(pq.lhq.value != NULL)) {
            prev = pq.lhq.value;

            if (njs_slow_path(prev->type == NJS_WHITEOUT)) {
                /* Previously deleted property.  */
                *prev = *prop;
            }

        } else {

            if ((flags & NJS_OBJECT_PROP_CREATE)) {
                ret = njs_primitive_value_to_key(vm, &pq.key, name);
                if (njs_slow_path(ret != NJS_OK)) {
                    return NJS_ERROR;
                }

                if (njs_is_symbol(name)) {
                    pq.lhq.key_hash = njs_symbol_key(name);
                    pq.lhq.key.start = NULL;

                } else {
                    njs_string_get(&pq.key, &pq.lhq.key);
                    pq.lhq.key_hash = (hash == 0)
                                           ? njs_djb_hash(pq.lhq.key.start,
                                                          pq.lhq.key.length)
                                           : hash;
                }

                pq.lhq.proto = &njs_object_hash_proto;
            }

            pq.lhq.value = prop;
            pq.lhq.replace = 0;
            pq.lhq.pool = vm->mem_pool;

            ret = njs_lvlhsh_insert(njs_object_hash(object), &pq.lhq);
            if (njs_slow_path(ret != NJS_OK)) {
                njs_internal_error(vm, "lvlhsh insert failed");
                return NJS_ERROR;
            }
        }

        return NJS_OK;
    }

    /* Updating existing prop. */

    prev = pq.lhq.value;

    switch (prev->type) {
    case NJS_PROPERTY:
    case NJS_ACCESSOR:
    case NJS_PROPERTY_HANDLER:
        break;

    case NJS_PROPERTY_REF:
    case NJS_PROPERTY_PLACE_REF:
        if (prev->type == NJS_PROPERTY_REF
            && !njs_is_accessor_descriptor(prop)
            && prop->configurable != NJS_ATTRIBUTE_FALSE
            && prop->enumerable != NJS_ATTRIBUTE_FALSE
            && prop->writable != NJS_ATTRIBUTE_FALSE)
        {
            if (njs_is_valid(njs_prop_value(prop))) {
                njs_value_assign(njs_prop_ref(prev), njs_prop_value(prop));
            }

            return NJS_OK;
        }

        array = njs_array(object);
        length = array->length;

        ret = njs_array_convert_to_slow_array(vm, array);
        if (njs_slow_path(ret != NJS_OK)) {
            return ret;
        }

        ret = njs_array_length_redefine(vm, object, length, 1);
        if (njs_slow_path(ret != NJS_OK)) {
            return ret;
        }

        flags &= ~NJS_OBJECT_PROP_CREATE;

        goto again;

    case NJS_PROPERTY_TYPED_ARRAY_REF:
        if (njs_is_accessor_descriptor(prop)) {
            goto exception;
        }

        if (prop->configurable == NJS_ATTRIBUTE_TRUE ||
            prop->enumerable == NJS_ATTRIBUTE_FALSE ||
            prop->writable == NJS_ATTRIBUTE_FALSE)
        {
            goto exception;
        }

        if (njs_is_valid(njs_prop_value(prop))) {
            return njs_typed_array_set_value(vm,
                                         njs_typed_array(njs_prop_value(prev)),
                                         njs_prop_magic32(prev),
                                         njs_prop_value(prop));
        }

        return NJS_OK;

    default:
        njs_internal_error(vm, "unexpected property type \"%s\" "
                           "while defining property",
                           njs_prop_type_string(prev->type));

        return NJS_ERROR;
    }

    /* 9.1.6.3 ValidateAndApplyPropertyDescriptor */

    if (!prev->configurable) {

        if (prop->configurable == NJS_ATTRIBUTE_TRUE) {
            goto exception;
        }

        if (prop->enumerable != NJS_ATTRIBUTE_UNSET
            && prev->enumerable != prop->enumerable)
        {
            goto exception;
        }
    }

    if (njs_is_generic_descriptor(prop)) {
        goto done;
    }

    if (njs_is_data_descriptor(prev) != njs_is_data_descriptor(prop)) {
        if (!prev->configurable) {
            goto exception;
        }

        /*
         * 6.b-c Preserve the existing values of the converted property's
         * [[Configurable]] and [[Enumerable]] attributes and set the rest of
         * the property's attributes to their default values.
         */

        if (pq.temp) {
            pq.lhq.value = NULL;
            prop->configurable = prev->configurable;
            prop->enumerable = prev->enumerable;
            goto set_prop;
        }

        if (njs_is_data_descriptor(prev)) {
            prev->writable = NJS_ATTRIBUTE_UNSET;
            njs_prop_getter(prev) = NULL;
            njs_prop_setter(prev) = NULL;

        } else {
            prev->writable = NJS_ATTRIBUTE_FALSE;

            njs_set_undefined(njs_prop_value(prev));
        }

        prev->type = prop->type;

    } else if (njs_is_data_descriptor(prev)
               && njs_is_data_descriptor(prop))
    {
        if (!prev->configurable && !prev->writable) {
            if (prop->writable == NJS_ATTRIBUTE_TRUE) {
                goto exception;
            }

            if (njs_is_valid(njs_prop_value(prop))
                && prev->type != NJS_PROPERTY_HANDLER
                && !njs_values_same(njs_prop_value(prop), njs_prop_value(prev)))
            {
                goto exception;
            }
        }

    } else {
        if (!prev->configurable) {
            if (njs_prop_getter(prop) != NJS_PROP_PTR_UNSET
                && njs_prop_getter(prop) != njs_prop_getter(prev))
            {
                goto exception;
            }

            if (njs_prop_setter(prop) != NJS_PROP_PTR_UNSET
                && njs_prop_setter(prop) != njs_prop_setter(prev))
            {
                goto exception;
            }
        }
    }

done:

    if (njs_slow_path(njs_is_fast_array(object)
                      && pq.lhq.key_hash == NJS_LENGTH_HASH)
                      && njs_strstr_eq(&pq.lhq.key, &length_key)
                      && prop->writable == NJS_ATTRIBUTE_FALSE)
    {
        array = njs_array(object);
        length = array->length;

        ret = njs_array_convert_to_slow_array(vm, array);
        if (njs_slow_path(ret != NJS_OK)) {
            return ret;
        }

        ret = njs_array_length_redefine(vm, object, length, 1);
        if (njs_slow_path(ret != NJS_OK)) {
            return ret;
        }

        goto again;
    }

    if (njs_is_accessor_descriptor(prop)) {
        prev->type = prop->type;

        if (njs_prop_getter(prop) != NJS_PROP_PTR_UNSET) {
            njs_prop_getter(prev) = njs_prop_getter(prop);
        }

        if (njs_prop_setter(prop) != NJS_PROP_PTR_UNSET) {
            njs_prop_setter(prev) = njs_prop_setter(prop);
        }

    } else if (njs_is_valid(njs_prop_value(prop))) {

        if (prev->type == NJS_PROPERTY_HANDLER) {
            if (prev->writable) {
                ret = njs_prop_handler(prev)(vm, prev, object,
                                             njs_prop_value(prop), &vm->retval);
                if (njs_slow_path(ret == NJS_ERROR)) {
                    return ret;
                }

                if (ret == NJS_DECLINED) {
                    pq.lhq.value = NULL;
                    goto set_prop;
                }

            } else {

                prev->type = prop->type;
                njs_value_assign(njs_prop_value(prev), njs_prop_value(prop));
            }

        } else {

            if (njs_slow_path(njs_is_array(object)
                              && pq.lhq.key_hash == NJS_LENGTH_HASH)
                              && njs_strstr_eq(&pq.lhq.key, &length_key))
            {
                if (prev->configurable != NJS_ATTRIBUTE_TRUE
                    && prev->writable != NJS_ATTRIBUTE_TRUE
                    && !njs_values_strict_equal(njs_prop_value(prev),
                                                 njs_prop_value(prop)))
                {
                    njs_type_error(vm, "Cannot redefine property: \"length\"");
                    return NJS_ERROR;
                }

                if (prop->writable != NJS_ATTRIBUTE_UNSET) {
                    prev->writable = prop->writable;
                }

                return njs_array_length_set(vm, object, prev,
                                            njs_prop_value(prop));
            }

            njs_value_assign(njs_prop_value(prev), njs_prop_value(prop));
        }
    }

    /*
     * 9. For each field of Desc that is present, set the corresponding
     * attribute of the property named P of object O to the value of the field.
     */

    if (prop->writable != NJS_ATTRIBUTE_UNSET) {
        prev->writable = prop->writable;
    }

    if (prop->enumerable != NJS_ATTRIBUTE_UNSET) {
        prev->enumerable = prop->enumerable;
    }

    if (prop->configurable != NJS_ATTRIBUTE_UNSET) {
        prev->configurable = prop->configurable;
    }

    return NJS_OK;

exception:

    njs_key_string_get(vm, &pq.key,  &pq.lhq.key);
    njs_type_error(vm, "Cannot redefine property: \"%V\"", &pq.lhq.key);

    return NJS_ERROR;
}


njs_int_t
njs_prop_private_copy(njs_vm_t *vm, njs_property_query_t *pq,
    njs_object_t *proto)
{
    njs_int_t          ret;
    njs_value_t        *value;
    njs_object_t       *object;
    njs_function_t     *function;
    njs_object_prop_t  *prop, *shared;

    prop = njs_mp_align(vm->mem_pool, sizeof(njs_value_t),
                        sizeof(njs_object_prop_t));
    if (njs_slow_path(prop == NULL)) {
        njs_memory_error(vm);
        return NJS_ERROR;
    }

    shared = pq->lhq.value;
    *prop = *shared;

    pq->lhq.replace = 0;
    pq->lhq.value = prop;
    pq->lhq.pool = vm->mem_pool;

    ret = njs_lvlhsh_insert(&proto->hash, &pq->lhq);
    if (njs_slow_path(ret != NJS_OK)) {
        njs_internal_error(vm, "lvlhsh insert failed");
        return NJS_ERROR;
    }

    if (njs_is_accessor_descriptor(prop)) {
        if (njs_prop_getter(prop) != NULL) {
            function = njs_function_copy(vm, njs_prop_getter(prop));
            if (njs_slow_path(function == NULL)) {
                return NJS_ERROR;
            }

            njs_prop_getter(prop) = function;

            if (njs_prop_setter(prop) != NULL
                && function->native && njs_prop_setter(prop)->native
                && function->u.native == njs_prop_setter(prop)->u.native)
            {
                njs_prop_setter(prop) = njs_prop_getter(prop);
                return NJS_OK;
            }
        }

        if (njs_prop_setter(prop) != NULL) {
            function = njs_function_copy(vm, njs_prop_setter(prop));
            if (njs_slow_path(function == NULL)) {
                return NJS_ERROR;
            }

            njs_prop_setter(prop) = function;
        }

        return NJS_OK;
    }

    value = njs_prop_value(prop);

    switch (value->type) {
    case NJS_OBJECT:
    case NJS_OBJECT_VALUE:
        object = njs_object_value_copy(vm, value);
        if (njs_slow_path(object == NULL)) {
            return NJS_ERROR;
        }

        value->data.u.object = object;
        return NJS_OK;

    case NJS_FUNCTION:
        function = njs_function_value_copy(vm, value);
        if (njs_slow_path(function == NULL)) {
            return NJS_ERROR;
        }

        return njs_function_name_set(vm, function, &prop->name, NULL);

    default:
        break;
    }

    return NJS_OK;
}


static njs_object_prop_t *
njs_descriptor_prop(njs_vm_t *vm, const njs_value_t *name,
    const njs_value_t *desc)
{
    njs_int_t           ret;
    njs_bool_t          data, accessor;
    njs_value_t         value;
    njs_function_t      *getter, *setter;
    njs_object_prop_t   *prop;
    njs_lvlhsh_query_t  lhq;

    static const njs_value_t  get_string = njs_string("get");

    if (!njs_is_object(desc)) {
        njs_type_error(vm, "property descriptor must be an object");
        return NULL;
    }

    prop = njs_object_prop_alloc(vm, name, &njs_value_invalid,
                                 NJS_ATTRIBUTE_UNSET);
    if (njs_slow_path(prop == NULL)) {
        return NULL;
    }

    data = 0;
    accessor = 0;
    getter = NJS_PROP_PTR_UNSET;
    setter = NJS_PROP_PTR_UNSET;

    njs_object_property_init(&lhq, &get_string, NJS_GET_HASH);

    ret = njs_object_property(vm, desc, &lhq, &value);
    if (njs_slow_path(ret == NJS_ERROR)) {
        return NULL;
    }

    if (ret == NJS_OK) {
        if (njs_is_defined(&value) && !njs_is_function(&value)) {
            njs_type_error(vm, "Getter must be a function");
            return NULL;
        }

        accessor = 1;
        getter = njs_is_function(&value) ? njs_function(&value) : NULL;
    }

    lhq.key = njs_str_value("set");
    lhq.key_hash = NJS_SET_HASH;

    ret = njs_object_property(vm, desc, &lhq, &value);
    if (njs_slow_path(ret == NJS_ERROR)) {
        return NULL;
    }

    if (ret == NJS_OK) {
        if (njs_is_defined(&value) && !njs_is_function(&value)) {
            njs_type_error(vm, "Setter must be a function");
            return NULL;
        }

        accessor = 1;
        setter = njs_is_function(&value) ? njs_function(&value) : NULL;
    }

    lhq.key = njs_str_value("value");
    lhq.key_hash = NJS_VALUE_HASH;

    ret = njs_object_property(vm, desc, &lhq, &value);
    if (njs_slow_path(ret == NJS_ERROR)) {
        return NULL;
    }

    if (ret == NJS_OK) {
        data = 1;
        njs_value_assign(njs_prop_value(prop), &value);
    }

    lhq.key = njs_str_value("writable");
    lhq.key_hash = NJS_WRITABABLE_HASH;

    ret = njs_object_property(vm, desc, &lhq, &value);
    if (njs_slow_path(ret == NJS_ERROR)) {
        return NULL;
    }

    if (ret == NJS_OK) {
        data = 1;
        prop->writable = njs_is_true(&value);
    }

    if (accessor && data) {
        njs_type_error(vm, "Cannot both specify accessors "
                           "and a value or writable attribute");
        return NULL;
    }

    lhq.key = njs_str_value("enumerable");
    lhq.key_hash = NJS_ENUMERABLE_HASH;

    ret = njs_object_property(vm, desc, &lhq, &value);
    if (njs_slow_path(ret == NJS_ERROR)) {
        return NULL;
    }

    if (ret == NJS_OK) {
        prop->enumerable = njs_is_true(&value);
    }

    lhq.key = njs_str_value("configurable");
    lhq.key_hash = NJS_CONFIGURABLE_HASH;

    ret = njs_object_property(vm, desc, &lhq, &value);
    if (njs_slow_path(ret == NJS_ERROR)) {
        return NULL;
    }

    if (ret == NJS_OK) {
        prop->configurable = njs_is_true(&value);
    }

    if (accessor) {
        prop->type = NJS_ACCESSOR;
        njs_prop_getter(prop) = getter;
        njs_prop_setter(prop) = setter;
    }

    return prop;
}


static const njs_value_t  njs_object_value_string = njs_string("value");
static const njs_value_t  njs_object_get_string = njs_string("get");
static const njs_value_t  njs_object_set_string = njs_string("set");
static const njs_value_t  njs_object_writable_string =
                                                    njs_string("writable");
static const njs_value_t  njs_object_enumerable_string =
                                                    njs_string("enumerable");
static const njs_value_t  njs_object_configurable_string =
                                                    njs_string("configurable");


njs_int_t
njs_object_prop_descriptor(njs_vm_t *vm, njs_value_t *dest,
    njs_value_t *value, njs_value_t *key)
{
    njs_int_t             ret;
    njs_object_t          *desc;
    njs_object_prop_t     *pr, *prop;
    const njs_value_t     *setval;
    njs_lvlhsh_query_t    lhq;
    njs_property_query_t  pq;

    njs_property_query_init(&pq, NJS_PROPERTY_QUERY_GET, 0, 1);

    if (njs_slow_path(!njs_is_key(key))) {
        ret = njs_value_to_key(vm, key, key);
        if (njs_slow_path(ret != NJS_OK)) {
            return ret;
        }
    }

    ret = njs_property_query(vm, &pq, value, key);

    switch (ret) {
    case NJS_OK:
        prop = pq.lhq.value;

        switch (prop->type) {
        case NJS_PROPERTY:
        case NJS_ACCESSOR:
            break;

        case NJS_PROPERTY_HANDLER:
            pq.scratch = *prop;
            prop = &pq.scratch;
            ret = njs_prop_handler(prop)(vm, prop, value, NULL,
                                         njs_prop_value(prop));
            if (njs_slow_path(ret == NJS_ERROR)) {
                return ret;
            }

            break;

        default:
            njs_type_error(vm, "unexpected property type: %s",
                           njs_prop_type_string(prop->type));
            return NJS_ERROR;
        }

        break;

    case NJS_DECLINED:
        njs_set_undefined(dest);
        return NJS_OK;

    case NJS_ERROR:
    default:
        return NJS_ERROR;
    }

    desc = njs_object_alloc(vm);
    if (njs_slow_path(desc == NULL)) {
        return NJS_ERROR;
    }

    lhq.proto = &njs_object_hash_proto;
    lhq.replace = 0;
    lhq.pool = vm->mem_pool;

    if (njs_is_data_descriptor(prop)) {

        lhq.key = njs_str_value("value");
        lhq.key_hash = NJS_VALUE_HASH;

        pr = njs_object_prop_alloc(vm, &njs_object_value_string,
                                   njs_prop_value(prop), 1);
        if (njs_slow_path(pr == NULL)) {
            return NJS_ERROR;
        }

        lhq.value = pr;

        ret = njs_lvlhsh_insert(&desc->hash, &lhq);
        if (njs_slow_path(ret != NJS_OK)) {
            njs_internal_error(vm, "lvlhsh insert failed");
            return NJS_ERROR;
        }

        lhq.key = njs_str_value("writable");
        lhq.key_hash = NJS_WRITABABLE_HASH;

        setval = (prop->writable == 1) ? &njs_value_true : &njs_value_false;

        pr = njs_object_prop_alloc(vm, &njs_object_writable_string, setval, 1);
        if (njs_slow_path(pr == NULL)) {
            return NJS_ERROR;
        }

        lhq.value = pr;

        ret = njs_lvlhsh_insert(&desc->hash, &lhq);
        if (njs_slow_path(ret != NJS_OK)) {
            njs_internal_error(vm, "lvlhsh insert failed");
            return NJS_ERROR;
        }

    } else {

        lhq.key = njs_str_value("get");
        lhq.key_hash = NJS_GET_HASH;

        pr = njs_object_prop_alloc(vm, &njs_object_get_string,
                                   &njs_value_undefined, 1);
        if (njs_slow_path(pr == NULL)) {
            return NJS_ERROR;
        }

        if (njs_prop_getter(prop) != NULL) {
            njs_set_function(njs_prop_value(pr), njs_prop_getter(prop));
        }

        lhq.value = pr;

        ret = njs_lvlhsh_insert(&desc->hash, &lhq);
        if (njs_slow_path(ret != NJS_OK)) {
            njs_internal_error(vm, "lvlhsh insert failed");
            return NJS_ERROR;
        }

        lhq.key = njs_str_value("set");
        lhq.key_hash = NJS_SET_HASH;

        pr = njs_object_prop_alloc(vm, &njs_object_set_string,
                                   &njs_value_undefined, 1);
        if (njs_slow_path(pr == NULL)) {
            return NJS_ERROR;
        }

        if (njs_prop_setter(prop) != NULL) {
            njs_set_function(njs_prop_value(pr), njs_prop_setter(prop));
        }

        lhq.value = pr;

        ret = njs_lvlhsh_insert(&desc->hash, &lhq);
        if (njs_slow_path(ret != NJS_OK)) {
            njs_internal_error(vm, "lvlhsh insert failed");
            return NJS_ERROR;
        }
    }

    lhq.key = njs_str_value("enumerable");
    lhq.key_hash = NJS_ENUMERABLE_HASH;

    setval = (prop->enumerable == 1) ? &njs_value_true : &njs_value_false;

    pr = njs_object_prop_alloc(vm, &njs_object_enumerable_string, setval, 1);
    if (njs_slow_path(pr == NULL)) {
        return NJS_ERROR;
    }

    lhq.value = pr;

    ret = njs_lvlhsh_insert(&desc->hash, &lhq);
    if (njs_slow_path(ret != NJS_OK)) {
        njs_internal_error(vm, "lvlhsh insert failed");
        return NJS_ERROR;
    }

    lhq.key = njs_str_value("configurable");
    lhq.key_hash = NJS_CONFIGURABLE_HASH;

    setval = (prop->configurable == 1) ? &njs_value_true : &njs_value_false;

    pr = njs_object_prop_alloc(vm, &njs_object_configurable_string, setval, 1);
    if (njs_slow_path(pr == NULL)) {
        return NJS_ERROR;
    }

    lhq.value = pr;

    ret = njs_lvlhsh_insert(&desc->hash, &lhq);
    if (njs_slow_path(ret != NJS_OK)) {
        njs_internal_error(vm, "lvlhsh insert failed");
        return NJS_ERROR;
    }

    njs_set_object(dest, desc);

    return NJS_OK;
}


const char *
njs_prop_type_string(njs_object_prop_type_t type)
{
    switch (type) {
    case NJS_PROPERTY_REF:
    case NJS_PROPERTY_PLACE_REF:
        return "property_ref";

    case NJS_PROPERTY_HANDLER:
        return "property handler";

    case NJS_WHITEOUT:
        return "whiteout";

    case NJS_PROPERTY:
        return "property";

    default:
        return "unknown";
    }
}


njs_int_t
njs_object_prop_init(njs_vm_t *vm, const njs_object_init_t* init,
    const njs_object_prop_t *base, njs_value_t *value, njs_value_t *retval)
{
    njs_int_t           ret;
    njs_object_t        *object;
    njs_object_prop_t   *prop;
    njs_lvlhsh_query_t  lhq;

    object = njs_object_alloc(vm);
    if (object == NULL) {
        return NJS_ERROR;
    }

    ret = njs_object_hash_create(vm, &object->hash, init->properties,
                                 init->items);
    if (njs_slow_path(ret != NJS_OK)) {
        return NJS_ERROR;
    }

    prop = njs_mp_align(vm->mem_pool, sizeof(njs_value_t),
                        sizeof(njs_object_prop_t));
    if (njs_slow_path(prop == NULL)) {
        njs_memory_error(vm);
        return NJS_ERROR;
    }

    *prop = *base;

    prop->type = NJS_PROPERTY;
    njs_set_object(njs_prop_value(prop), object);

    lhq.proto = &njs_object_hash_proto;
    njs_string_get(&prop->name, &lhq.key);
    lhq.key_hash = njs_djb_hash(lhq.key.start, lhq.key.length);
    lhq.value = prop;
    lhq.replace = 1;
    lhq.pool = vm->mem_pool;

    ret = njs_lvlhsh_insert(njs_object_hash(value), &lhq);
    if (njs_fast_path(ret == NJS_OK)) {
        njs_value_assign(retval, njs_prop_value(prop));
        return NJS_OK;
    }

    njs_internal_error(vm, "lvlhsh insert failed");

    return NJS_ERROR;
}
