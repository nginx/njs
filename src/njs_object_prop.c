
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) NGINX, Inc.
 */


#include <njs_main.h>


static njs_object_prop_t *njs_descriptor_prop(njs_vm_t *vm,
    const njs_value_t *desc, njs_object_prop_t *prop,
    uint32_t *unset_enumerable, uint32_t *unset_configuarble,
    uint32_t *enum_writable);


void
njs_object_prop_init(njs_object_prop_t *prop, njs_object_prop_type_t type,
    uint8_t flags)
{
    prop->next_elt = 0;
    prop->atom_id = 0;

    prop->type = type;

    if (flags != NJS_OBJECT_PROP_UNSET) {
        prop->enumerable = !!(flags & NJS_OBJECT_PROP_ENUMERABLE);
        prop->configurable = !!(flags & NJS_OBJECT_PROP_CONFIGURABLE);

        if (type == NJS_PROPERTY) {
            prop->writable = !!(flags & NJS_OBJECT_PROP_WRITABLE);

        } else {
            prop->writable = 0;
        }

    } else {
        prop->enumerable = 0;
        prop->configurable = 0;
        prop->writable = 0;
    }
}


njs_int_t
njs_object_property(njs_vm_t *vm, njs_object_t *object,
    njs_flathsh_query_t *fhq, njs_value_t *retval)
{
    njs_int_t          ret;
    njs_value_t        value;
    njs_object_prop_t  *prop;

    do {
        ret = njs_flathsh_unique_find(&object->hash, fhq);

        if (njs_fast_path(ret == NJS_OK)) {
            prop = fhq->value;

            if (prop->type != NJS_WHITEOUT) {
                goto found;
            }
        }

        ret = njs_flathsh_unique_find(&object->shared_hash, fhq);

        if (njs_fast_path(ret == NJS_OK)) {
            goto found;
        }

        object = object->__proto__;

    } while (object != NULL);

    njs_set_undefined(retval);

    return NJS_DECLINED;

found:

    prop = fhq->value;

    if (njs_is_data_descriptor(prop)) {
        njs_value_assign(retval, njs_prop_value(prop));
        return NJS_OK;
    }

    if (njs_prop_getter(prop) == NULL) {
        njs_set_undefined(retval);
        return NJS_OK;
    }

    njs_set_object(&value, object);

    return njs_function_apply(vm, njs_prop_getter(prop), &value, 1, retval);
}


njs_object_prop_t *
njs_object_property_add(njs_vm_t *vm, njs_value_t *object, unsigned atom_id,
    njs_bool_t replace)
{
    njs_int_t            ret;
    njs_object_prop_t    *prop;
    njs_flathsh_query_t  fhq;

    fhq.key_hash = atom_id;
    fhq.replace = replace;
    fhq.pool = vm->mem_pool;
    fhq.proto = &njs_object_hash_proto;

    ret = njs_flathsh_unique_insert(njs_object_hash(object), &fhq);
    if (njs_slow_path(ret != NJS_OK)) {
        njs_internal_error(vm, "flathsh insert failed");
        return NULL;
    }

    prop = fhq.value;

    prop->type = NJS_PROPERTY;
    prop->enumerable = 1;
    prop->configurable = 1;
    prop->writable = 1;
    prop->u.value = njs_value_invalid;

    return prop;
}


/*
 * ES5.1, 8.12.9: [[DefineOwnProperty]]
 */
njs_int_t
njs_object_prop_define(njs_vm_t *vm, njs_value_t *object, unsigned atom_id,
    njs_value_t *value, unsigned flags)
{
    uint32_t              length, index, set_enumerable, set_configurable,
                          set_writable;
    njs_int_t             ret;
    njs_array_t           *array;
    njs_value_t           key, retval;
    njs_object_prop_t     _prop;
    njs_object_prop_t     *prop = &_prop, *prev, *obj_prop;
    njs_property_query_t  pq;

again:

    set_enumerable = 1;
    set_configurable = 1;
    set_writable = 1;

    njs_property_query_init(&pq, NJS_PROPERTY_QUERY_SET, 1);

    ret = (flags & NJS_OBJECT_PROP_CREATE)
                ? NJS_DECLINED
                : njs_property_query(vm, &pq, object, atom_id);

    if (njs_slow_path(ret == NJS_ERROR)) {
        return ret;
    }

    switch (njs_prop_type(flags)) {
    case NJS_OBJECT_PROP_DESCRIPTOR:
        prop = njs_descriptor_prop(vm, value, prop, &set_enumerable,
                                   &set_configurable, &set_writable);
        if (njs_slow_path(prop == NULL)) {
            return NJS_ERROR;
        }

        break;

    case NJS_OBJECT_PROP_VALUE:
        if (((flags & NJS_OBJECT_PROP_VALUE_ECW) == NJS_OBJECT_PROP_VALUE_ECW)
            && njs_is_fast_array(object)
            && njs_atom_is_number(atom_id))
        {
            array = njs_array(object);
            index = njs_atom_number(atom_id);

            if (index < array->length) {
                njs_value_assign(&array->start[index], value);
                return NJS_OK;
            }
        }

        njs_object_prop_init(prop, NJS_PROPERTY,
                              flags & NJS_OBJECT_PROP_VALUE_ECW);
        njs_value_assign(njs_prop_value(prop), value);
        break;

    case NJS_OBJECT_PROP_GETTER:
    case NJS_OBJECT_PROP_SETTER:
    default:
        njs_assert(njs_is_function(value));

        njs_object_prop_init(prop, NJS_ACCESSOR, NJS_OBJECT_PROP_VALUE_EC);

        set_writable = 0;

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
            njs_atom_string_get(vm, atom_id, &pq.fhq.key);
            njs_type_error(vm, "Cannot add property \"%V\", "
                           "object is not extensible", &pq.fhq.key);
            return NJS_ERROR;
        }

        if (njs_slow_path(njs_is_typed_array(object) &&
           (flags & NJS_OBJECT_PROP_IS_STRING)))
        {
            /* Integer-Indexed Exotic Objects [[DefineOwnProperty]]. */

            ret = njs_atom_to_value(vm, &key, atom_id);
            if (njs_slow_path(ret != NJS_OK)) {
                return ret;
            }

            if (!isnan(njs_string_to_index(&key))) {
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
            if (!set_writable) {
                prop->writable = 0;
            }

            if (!njs_is_valid(njs_prop_value(prop))) {
                njs_set_undefined(njs_prop_value(prop));
            }
        }

        if (!set_enumerable) {
            prop->enumerable = 0;
        }

        if (!set_configurable) {
            prop->configurable = 0;
        }

        if (njs_slow_path(pq.fhq.value != NULL)) {
            prev = pq.fhq.value;

            if (njs_slow_path(prev->type == NJS_WHITEOUT)) {
                /* Previously deleted property.  */
                prop->atom_id = prev->atom_id;
                prop->next_elt = prev->next_elt;
                *prev = *prop;
            }

        } else {

            pq.fhq.key_hash = atom_id;
            pq.fhq.proto = &njs_object_hash_proto;
            pq.fhq.replace = 0;
            pq.fhq.pool = vm->mem_pool;

            ret = njs_flathsh_unique_insert(njs_object_hash(object), &pq.fhq);
            if (njs_slow_path(ret != NJS_OK)) {
                njs_internal_error(vm, "flathsh insert failed");
                return NJS_ERROR;
            }

            obj_prop = pq.fhq.value;
            obj_prop->enumerable = prop->enumerable;
            obj_prop->configurable = prop->configurable;
            obj_prop->writable = prop->writable;
            obj_prop->type = prop->type;
            obj_prop->u.value = prop->u.value;
        }

        return NJS_OK;
    }

    /* Updating existing prop. */

    prev = pq.fhq.value;

    switch (prev->type) {
    case NJS_PROPERTY:
    case NJS_ACCESSOR:
    case NJS_PROPERTY_HANDLER:
        break;

    case NJS_PROPERTY_REF:
    case NJS_PROPERTY_PLACE_REF:
        if (prev->type == NJS_PROPERTY_REF
            && !njs_is_accessor_descriptor(prop)
            && (!set_configurable || prop->configurable)
            && (!set_enumerable || prop->enumerable)
            && (!set_writable || prop->writable))
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

        if ((set_configurable && prop->configurable)
            || (set_enumerable && !prop->enumerable)
            || (set_writable && !prop->writable))
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

        if (prop->configurable) {
            goto exception;
        }

        if (set_enumerable && prev->enumerable != prop->enumerable) {
            goto exception;
        }
    }

    if (!(set_writable || njs_is_data_descriptor(prop))
        && !njs_is_accessor_descriptor(prop))
    {
        goto done;
    }

    if (njs_is_data_descriptor(prev)
        != (set_writable || njs_is_data_descriptor(prop)))
    {
        if (!prev->configurable) {
            goto exception;
        }

        /*
         * 6.b-c Preserve the existing values of the converted property's
         * [[Configurable]] and [[Enumerable]] attributes and set the rest of
         * the property's attributes to their default values.
         */

        if (pq.temp) {
            pq.fhq.value = NULL;
            prop->configurable = prev->configurable;
            prop->enumerable = prev->enumerable;
            goto set_prop;
        }

        if (njs_is_data_descriptor(prev)) {
            set_writable = 0;
            njs_prop_getter(prev) = NULL;
            njs_prop_setter(prev) = NULL;

        } else {
            prev->writable = 0;

            njs_set_undefined(njs_prop_value(prev));
        }

        prev->type = prop->type;

    } else if (njs_is_data_descriptor(prev)
               && (set_writable || njs_is_data_descriptor(prop)))
    {
        if (!prev->configurable && !prev->writable) {
            if (prop->writable) {
                goto exception;
            }

            if (njs_is_valid(njs_prop_value(prop))
                && prev->type != NJS_PROPERTY_HANDLER
                && !njs_values_same(vm, njs_prop_value(prop),
                                    njs_prop_value(prev)))
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
                      && pq.fhq.key_hash == NJS_ATOM_STRING_length)
                      && (set_writable && !prop->writable))
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
                ret = njs_prop_handler(prev)(vm, prev, atom_id, object,
                                             njs_prop_value(prop), &retval);
                if (njs_slow_path(ret == NJS_ERROR)) {
                    return ret;
                }

                if (ret == NJS_DECLINED) {
                    pq.fhq.value = NULL;
                    goto set_prop;
                }

            } else {

                prev->type = prop->type;
                njs_value_assign(njs_prop_value(prev), njs_prop_value(prop));
            }

        } else {

            if (njs_slow_path(njs_is_array(object)
                              && pq.fhq.key_hash == NJS_ATOM_STRING_length))
            {
                if (!prev->configurable
                    && !prev->writable
                    && !njs_values_strict_equal(vm, njs_prop_value(prev),
                                                njs_prop_value(prop)))
                {
                    njs_type_error(vm, "Cannot redefine property: \"length\"");
                    return NJS_ERROR;
                }

                if (set_writable) {
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

    if (set_writable) {
        prev->writable = prop->writable;
    }

    if (set_enumerable) {
        prev->enumerable = prop->enumerable;
    }

    if (set_configurable) {
        prev->configurable = prop->configurable;
    }

    return NJS_OK;

exception:

    njs_atom_string_get(vm, atom_id, &pq.fhq.key);
    njs_type_error(vm, "Cannot redefine property: \"%V\"", &pq.fhq.key);

    return NJS_ERROR;
}


njs_int_t
njs_prop_private_copy(njs_vm_t *vm, njs_property_query_t *pq,
    njs_object_t *proto)
{
    njs_int_t          ret;
    njs_value_t        *value, prop_name;
    njs_object_t       *object;
    njs_function_t     *function;
    njs_object_prop_t  *prop, *shared;

    shared = pq->fhq.value;

    pq->fhq.replace = 0;
    pq->fhq.pool = vm->mem_pool;

    ret = njs_flathsh_unique_insert(&proto->hash, &pq->fhq);
    if (njs_slow_path(ret != NJS_OK)) {
        njs_internal_error(vm, "flathsh insert failed");
        return NJS_ERROR;
    }

    prop = pq->fhq.value;
    prop->enumerable = shared->enumerable;
    prop->configurable = shared->configurable;
    prop->writable = shared->writable;
    prop->type = shared->type;
    prop->u.value = shared->u.value;

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
    case NJS_ARRAY:
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

        ret = njs_atom_to_value(vm, &prop_name, pq->fhq.key_hash);
        if (ret != NJS_OK) {
            return NJS_ERROR;
        }

        return njs_function_name_set(vm, function, &prop_name, NULL);

    default:
        break;
    }

    return NJS_OK;
}


static njs_object_prop_t *
njs_descriptor_prop(njs_vm_t *vm, const njs_value_t *desc,
    njs_object_prop_t *prop, uint32_t *set_enumerable,
    uint32_t *set_configurable, uint32_t *set_writable)
{
    njs_int_t            ret;
    njs_bool_t           data, accessor;
    njs_value_t          value;
    njs_object_t         *desc_object;
    njs_function_t       *getter, *setter;
    njs_flathsh_query_t  fhq;

    if (!njs_is_object(desc)) {
        njs_type_error(vm, "property descriptor must be an object");
        return NULL;
    }

    njs_object_prop_init(prop, NJS_PROPERTY, NJS_OBJECT_PROP_UNSET);
    *njs_prop_value(prop) = njs_value_invalid;

    *set_enumerable = 0;
    *set_configurable = 0;
    *set_writable = 0;

    data = 0;
    accessor = 0;
    getter = NJS_PROP_PTR_UNSET;
    setter = NJS_PROP_PTR_UNSET;
    desc_object = njs_object(desc);

    fhq.proto = &njs_object_hash_proto;
    fhq.key_hash = NJS_ATOM_STRING_get;

    ret = njs_object_property(vm, desc_object, &fhq, &value);
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

    fhq.key_hash = NJS_ATOM_STRING_set;

    ret = njs_object_property(vm, desc_object, &fhq, &value);
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

    fhq.key_hash = NJS_ATOM_STRING_value;

    ret = njs_object_property(vm, desc_object, &fhq, &value);
    if (njs_slow_path(ret == NJS_ERROR)) {
        return NULL;
    }

    if (ret == NJS_OK) {
        data = 1;
        njs_value_assign(njs_prop_value(prop), &value);
    }

    fhq.key_hash = NJS_ATOM_STRING_writable;

    ret = njs_object_property(vm, desc_object, &fhq, &value);
    if (njs_slow_path(ret == NJS_ERROR)) {
        return NULL;
    }

    if (ret == NJS_OK) {
        data = 1;
        prop->writable = njs_is_true(&value);
        *set_writable = 1;
    }

    if (accessor && data) {
        njs_type_error(vm, "Cannot both specify accessors "
                           "and a value or writable attribute");
        return NULL;
    }

    fhq.key_hash = NJS_ATOM_STRING_enumerable;

    ret = njs_object_property(vm, desc_object, &fhq, &value);
    if (njs_slow_path(ret == NJS_ERROR)) {
        return NULL;
    }

    if (ret == NJS_OK) {
        prop->enumerable = njs_is_true(&value);
        *set_enumerable = 1;
    }

    fhq.key_hash = NJS_ATOM_STRING_configurable;

    ret = njs_object_property(vm, desc_object, &fhq, &value);
    if (njs_slow_path(ret == NJS_ERROR)) {
        return NULL;
    }

    if (ret == NJS_OK) {
        prop->configurable = njs_is_true(&value);
        *set_configurable = 1;
    }

    if (accessor) {
        prop->type = NJS_ACCESSOR;
        njs_prop_getter(prop) = getter;
        njs_prop_setter(prop) = setter;
    }

    return prop;
}


njs_int_t
njs_object_prop_descriptor(njs_vm_t *vm, njs_value_t *dest,
    njs_value_t *value, njs_value_t *key)
{
    njs_int_t             ret;
    njs_object_t          *desc;
    njs_object_prop_t     *pr, *prop;
    const njs_value_t     *setval;
    njs_flathsh_query_t   fhq;
    njs_property_query_t  pq;

    njs_property_query_init(&pq, NJS_PROPERTY_QUERY_GET, 1);

    if (njs_slow_path(!njs_is_key(key))) {
        ret = njs_value_to_key(vm, key, key);
        if (njs_slow_path(ret != NJS_OK)) {
            return ret;
        }
    }

    ret = njs_property_query_val(vm, &pq, value, key);

    switch (ret) {
    case NJS_OK:
        prop = pq.fhq.value;

        switch (prop->type) {
        case NJS_PROPERTY:
        case NJS_ACCESSOR:
            break;

        case NJS_PROPERTY_HANDLER:
            pq.scratch = *prop;
            prop = &pq.scratch;
            ret = njs_prop_handler(prop)(vm, prop, key->atom_id, value, NULL,
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

    fhq.proto = &njs_object_hash_proto;
    fhq.replace = 0;
    fhq.pool = vm->mem_pool;

    if (njs_is_data_descriptor(prop)) {
        fhq.key_hash = NJS_ATOM_STRING_value;

        ret = njs_flathsh_unique_insert(&desc->hash, &fhq);
        if (njs_slow_path(ret != NJS_OK)) {
            njs_internal_error(vm, "flathsh insert failed");
            return NJS_ERROR;
        }

        pr = fhq.value;

        pr->type = NJS_PROPERTY;
        pr->enumerable = 1;
        pr->configurable = 1;
        pr->writable = 1;
        pr->u.value = *(njs_prop_value(prop));

        fhq.key_hash = NJS_ATOM_STRING_writable;

        setval = (prop->writable == 1) ? &njs_value_true : &njs_value_false;

        ret = njs_flathsh_unique_insert(&desc->hash, &fhq);
        if (njs_slow_path(ret != NJS_OK)) {
            njs_internal_error(vm, "flathsh insert failed");
            return NJS_ERROR;
        }

        pr = fhq.value;

        pr->type = NJS_PROPERTY;
        pr->enumerable = 1;
        pr->configurable = 1;
        pr->writable = 1;
        pr->u.value = *setval;

    } else {

        fhq.key_hash = NJS_ATOM_STRING_get;

        ret = njs_flathsh_unique_insert(&desc->hash, &fhq);
        if (njs_slow_path(ret != NJS_OK)) {
            njs_internal_error(vm, "flathsh insert failed");
            return NJS_ERROR;
        }

        pr = fhq.value;

        pr->type = NJS_PROPERTY;
        pr->enumerable = 1;
        pr->configurable = 1;
        pr->writable = 1;
        pr->u.value = njs_value_undefined;

        if (njs_prop_getter(prop) != NULL) {
            njs_set_function(njs_prop_value(pr), njs_prop_getter(prop));
        }

        fhq.key_hash = NJS_ATOM_STRING_set;

        ret = njs_flathsh_unique_insert(&desc->hash, &fhq);
        if (njs_slow_path(ret != NJS_OK)) {
            njs_internal_error(vm, "flathsh insert failed");
            return NJS_ERROR;
        }

        pr = fhq.value;

        pr->type = NJS_PROPERTY;
        pr->enumerable = 1;
        pr->configurable = 1;
        pr->writable = 1;
        pr->u.value = njs_value_undefined;

        if (njs_prop_setter(prop) != NULL) {
            njs_set_function(njs_prop_value(pr), njs_prop_setter(prop));
        }
    }

    fhq.key_hash = NJS_ATOM_STRING_enumerable;

    setval = (prop->enumerable == 1) ? &njs_value_true : &njs_value_false;

    ret = njs_flathsh_unique_insert(&desc->hash, &fhq);
    if (njs_slow_path(ret != NJS_OK)) {
        njs_internal_error(vm, "flathsh insert failed");
        return NJS_ERROR;
    }

    pr = fhq.value;

    pr->type = NJS_PROPERTY;
    pr->enumerable = 1;
    pr->configurable = 1;
    pr->writable = 1;
    pr->u.value = *setval;

    fhq.key_hash = NJS_ATOM_STRING_configurable;

    setval = (prop->configurable == 1) ? &njs_value_true : &njs_value_false;

    ret = njs_flathsh_unique_insert(&desc->hash, &fhq);
    if (njs_slow_path(ret != NJS_OK)) {
        njs_internal_error(vm, "flathsh insert failed");
        return NJS_ERROR;
    }

    pr = fhq.value;

    pr->type = NJS_PROPERTY;
    pr->enumerable = 1;
    pr->configurable = 1;
    pr->writable = 1;
    pr->u.value = *setval;

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

    case NJS_FREE_FLATHSH_ELEMENT:
        return "free hash element";

    default:
        return "unknown";
    }
}


njs_int_t
njs_object_props_init(njs_vm_t *vm, const njs_object_init_t* init,
    njs_object_prop_t *base, uint32_t atom_id, njs_value_t *value,
    njs_value_t *retval)
{
    njs_int_t            ret;
    njs_object_t         *object;
    njs_object_prop_t    *prop;
    njs_flathsh_query_t  fhq;

    object = njs_object_alloc(vm);
    if (object == NULL) {
        return NJS_ERROR;
    }

    ret = njs_object_hash_create(vm, &object->hash, init->properties,
                                 init->items);
    if (njs_slow_path(ret != NJS_OK)) {
        return NJS_ERROR;
    }

    fhq.key_hash = atom_id;
    fhq.replace = 1;
    fhq.pool = vm->mem_pool;
    fhq.proto = &njs_object_hash_proto;

    ret = njs_flathsh_unique_insert(njs_object_hash(value), &fhq);
    if (njs_slow_path(ret != NJS_OK)) {
        njs_internal_error(vm, "flathsh insert failed");
        return NJS_ERROR;
    }

    prop = fhq.value;

    prop->enumerable = base->enumerable;
    prop->configurable = base->configurable;
    prop->writable = base->writable;
    prop->type = NJS_PROPERTY;
    njs_set_object(njs_prop_value(prop), object);

    njs_value_assign(retval, njs_prop_value(prop));

    return NJS_OK;
}
