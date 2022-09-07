
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) NGINX, Inc.
 */


#include <njs_main.h>


static njs_int_t njs_descriptor_prop(njs_vm_t *vm,
    njs_object_prop_t *prop, const njs_value_t *desc);


njs_object_prop_t *
njs_object_prop_alloc(njs_vm_t *vm, const njs_value_t *name,
    const njs_value_t *value, uint8_t attributes)
{
    njs_object_prop_t  *prop;

    prop = njs_mp_align(vm->mem_pool, sizeof(njs_value_t),
                        sizeof(njs_object_prop_t));

    if (njs_fast_path(prop != NULL)) {
        /* GC: retain. */
        prop->value = *value;

        /* GC: retain. */
        prop->name = *name;

        prop->type = NJS_PROPERTY;
        prop->writable = attributes;
        prop->enumerable = attributes;
        prop->configurable = attributes;

        njs_set_invalid(&prop->getter);
        njs_set_invalid(&prop->setter);

        return prop;
    }

    njs_memory_error(vm);

    return NULL;
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
        *retval = prop->value;
        return NJS_OK;
    }

    if (njs_is_undefined(&prop->getter)) {
        njs_set_undefined(retval);
        return NJS_OK;
    }

    return njs_function_apply(vm, njs_function(&prop->getter), value,
                              1, retval);
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
    njs_value_t *name, njs_value_t *value, njs_object_prop_define_t type)
{
    uint32_t              length;
    njs_int_t             ret;
    njs_array_t           *array;
    njs_object_prop_t     *prop, *prev;
    njs_property_query_t  pq;

    static const njs_str_t  length_key = njs_str("length");

    if (njs_slow_path(!njs_is_key(name))) {
        ret = njs_value_to_key(vm, name, name);
        if (njs_slow_path(ret != NJS_OK)) {
            return ret;
        }
    }

    if (njs_slow_path(njs_is_fast_array(object))) {
        array = njs_array(object);
        length = array->length;

        ret = njs_array_convert_to_slow_array(vm, array);
        if (ret != NJS_OK) {
            return NJS_ERROR;
        }

        ret = njs_array_length_redefine(vm, object, length, 1);
        if (njs_slow_path(ret != NJS_OK)) {
            return ret;
        }
    }

again:

    njs_property_query_init(&pq, NJS_PROPERTY_QUERY_SET, 1);

    ret = njs_property_query(vm, &pq, object, name);
    if (njs_slow_path(ret == NJS_ERROR)) {
        return ret;
    }

    prop = njs_object_prop_alloc(vm, name, &njs_value_invalid,
                                 NJS_ATTRIBUTE_UNSET);
    if (njs_slow_path(prop == NULL)) {
        return NJS_ERROR;
    }

    switch (type) {

    case NJS_OBJECT_PROP_DESCRIPTOR:
        if (njs_descriptor_prop(vm, prop, value) != NJS_OK) {
            return NJS_ERROR;
        }

        break;

    case NJS_OBJECT_PROP_GETTER:
        prop->getter = *value;
        njs_set_invalid(&prop->setter);
        prop->enumerable = NJS_ATTRIBUTE_TRUE;
        prop->configurable = NJS_ATTRIBUTE_TRUE;

        break;

    case NJS_OBJECT_PROP_SETTER:
        prop->setter = *value;
        njs_set_invalid(&prop->getter);
        prop->enumerable = NJS_ATTRIBUTE_TRUE;
        prop->configurable = NJS_ATTRIBUTE_TRUE;

        break;
    }

    if (njs_fast_path(ret == NJS_DECLINED)) {

set_prop:

        if (!njs_object(object)->extensible) {
            njs_key_string_get(vm, &pq.key,  &pq.lhq.key);
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
            if (!njs_is_valid(&prop->getter)) {
                njs_set_undefined(&prop->getter);
            }

            if (!njs_is_valid(&prop->setter)) {
                njs_set_undefined(&prop->setter);
            }

        } else {
            if (prop->writable == NJS_ATTRIBUTE_UNSET) {
                prop->writable = 0;
            }

            if (!njs_is_valid(&prop->value)) {
                njs_set_undefined(&prop->value);
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
    case NJS_PROPERTY_HANDLER:
        break;

    case NJS_PROPERTY_REF:
        if (njs_is_accessor_descriptor(prop)
            || prop->configurable == NJS_ATTRIBUTE_FALSE
            || prop->enumerable == NJS_ATTRIBUTE_FALSE
            || prop->writable == NJS_ATTRIBUTE_FALSE)
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

        if (njs_is_valid(&prop->value)) {
            *prev->value.data.u.value = prop->value;
        } else {
            njs_set_undefined(prev->value.data.u.value);
        }

        return NJS_OK;

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

        if (njs_is_valid(&prop->value)) {
            return njs_typed_array_set_value(vm, njs_typed_array(&prev->value),
                                             prev->value.data.magic32,
                                             &prop->value);
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

        prev->type = prop->type;

        if (njs_is_data_descriptor(prev)) {
            njs_set_undefined(&prev->getter);
            njs_set_undefined(&prev->setter);

            njs_set_invalid(&prev->value);
            prev->writable = NJS_ATTRIBUTE_UNSET;

        } else {
            njs_set_undefined(&prev->value);
            prev->writable = NJS_ATTRIBUTE_FALSE;

            njs_set_invalid(&prev->getter);
            njs_set_invalid(&prev->setter);
        }


    } else if (njs_is_data_descriptor(prev)
               && njs_is_data_descriptor(prop))
    {
        if (!prev->configurable && !prev->writable) {
            if (prop->writable == NJS_ATTRIBUTE_TRUE) {
                goto exception;
            }

            if (njs_is_valid(&prop->value)
                && prev->type != NJS_PROPERTY_HANDLER
                && !njs_values_same(&prop->value, &prev->value))
            {
                goto exception;
            }
        }

    } else {
        if (!prev->configurable) {
            if (njs_is_valid(&prop->getter)
                && !njs_values_strict_equal(&prop->getter, &prev->getter))
            {
                goto exception;
            }

            if (njs_is_valid(&prop->setter)
                && !njs_values_strict_equal(&prop->setter, &prev->setter))
            {
                goto exception;
            }
        }
    }

done:

    if (njs_is_valid(&prop->value)) {
        if (prev->type == NJS_PROPERTY_HANDLER) {
            if (prev->writable) {
                ret = prev->value.data.u.prop_handler(vm, prev, object,
                                                      &prop->value,
                                                      &vm->retval);
                if (njs_slow_path(ret == NJS_ERROR)) {
                    return ret;
                }

                if (ret == NJS_DECLINED) {
                    pq.lhq.value = NULL;
                    goto set_prop;
                }
            }

        } else {
            if (njs_is_array(object)) {
                if (njs_slow_path(pq.lhq.key_hash == NJS_LENGTH_HASH)) {

                    if (njs_strstr_eq(&pq.lhq.key, &length_key)) {

                        if (prev->configurable != 1 &&
                            prev->writable != 1 &&
                            !njs_values_strict_equal(&prev->value, &prop->value))
                        {
                            njs_type_error(vm, "Cannot redefine property: \"length\"");
                            return NJS_ERROR;
                        }

                        if (prop->writable != NJS_ATTRIBUTE_UNSET) {
                            prev->writable = prop->writable;
                        }

                        return njs_array_length_set(vm, object, prev, &prop->value);
                    }
                }
            }

            prev->value = prop->value;
        }
    }

    /*
     * 9. For each field of Desc that is present, set the corresponding
     * attribute of the property named P of object O to the value of the field.
     */

    if (njs_is_valid(&prop->getter)) {
        prev->getter = prop->getter;
    }

    if (njs_is_valid(&prop->setter)) {
        prev->setter = prop->setter;
    }

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
njs_prop_private_copy(njs_vm_t *vm, njs_property_query_t *pq)
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

    ret = njs_lvlhsh_insert(&pq->prototype->hash, &pq->lhq);
    if (njs_slow_path(ret != NJS_OK)) {
        njs_internal_error(vm, "lvlhsh insert failed");
        return NJS_ERROR;
    }

    if (njs_is_accessor_descriptor(prop)) {
        if (njs_is_function(&prop->getter)) {
            function = njs_function_value_copy(vm, &prop->getter);
            if (njs_slow_path(function == NULL)) {
                return NJS_ERROR;
            }

            if (njs_is_function(&prop->setter)
                && function->native && njs_function(&prop->setter)->native
                && function->u.native == njs_function(&prop->setter)->u.native)
            {
                prop->setter = prop->getter;
                return NJS_OK;
            }
        }

        if (njs_is_function(&prop->setter)) {
            function = njs_function_value_copy(vm, &prop->setter);
            if (njs_slow_path(function == NULL)) {
                return NJS_ERROR;
            }
        }

        return NJS_OK;
    }

    value = &prop->value;

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
        function = njs_function_value_copy(vm, &prop->value);
        if (njs_slow_path(function == NULL)) {
            return NJS_ERROR;
        }

        return njs_function_name_set(vm, function, &prop->name, NULL);

    default:
        break;
    }

    return NJS_OK;
}


static njs_int_t
njs_descriptor_prop(njs_vm_t *vm, njs_object_prop_t *prop,
    const njs_value_t *desc)
{
    njs_int_t           ret;
    njs_bool_t          data, accessor;
    njs_value_t         value;
    njs_lvlhsh_query_t  lhq;

    static const njs_value_t  get_string = njs_string("get");

    if (!njs_is_object(desc)) {
        njs_type_error(vm, "property descriptor must be an object");
        return NJS_ERROR;
    }

    data = 0;
    accessor = 0;

    njs_object_property_init(&lhq, &get_string, NJS_GET_HASH);

    ret = njs_object_property(vm, desc, &lhq, &value);

    if (njs_slow_path(ret == NJS_ERROR)) {
        return NJS_ERROR;
    }

    if (ret == NJS_OK) {
        if (njs_is_defined(&value) && !njs_is_function(&value)) {
            njs_type_error(vm, "Getter must be a function");
            return NJS_ERROR;
        }

        accessor = 1;
        prop->getter = value;

    } else {
        /* NJS_DECLINED */
        njs_set_invalid(&prop->getter);
    }

    lhq.key = njs_str_value("set");
    lhq.key_hash = NJS_SET_HASH;

    ret = njs_object_property(vm, desc, &lhq, &value);

    if (njs_slow_path(ret == NJS_ERROR)) {
        return ret;
    }

    if (ret == NJS_OK) {
        if (njs_is_defined(&value) && !njs_is_function(&value)) {
            njs_type_error(vm, "Setter must be a function");
            return NJS_ERROR;
        }

        accessor = 1;
        prop->setter = value;

    } else {
        /* NJS_DECLINED */
        njs_set_invalid(&prop->setter);
    }

    lhq.key = njs_str_value("value");
    lhq.key_hash = NJS_VALUE_HASH;

    ret = njs_object_property(vm, desc, &lhq, &value);

    if (njs_slow_path(ret == NJS_ERROR)) {
        return ret;
    }

    if (ret == NJS_OK) {
        data = 1;
        prop->value = value;
    }

    lhq.key = njs_str_value("writable");
    lhq.key_hash = NJS_WRITABABLE_HASH;

    ret = njs_object_property(vm, desc, &lhq, &value);

    if (njs_slow_path(ret == NJS_ERROR)) {
        return ret;
    }

    if (ret == NJS_OK) {
        data = 1;
        prop->writable = njs_is_true(&value);
    }

    lhq.key = njs_str_value("enumerable");
    lhq.key_hash = NJS_ENUMERABLE_HASH;

    ret = njs_object_property(vm, desc, &lhq, &value);

    if (njs_slow_path(ret == NJS_ERROR)) {
        return ret;
    }

    if (ret == NJS_OK) {
        prop->enumerable = njs_is_true(&value);
    }

    lhq.key = njs_str_value("configurable");
    lhq.key_hash = NJS_CONFIGURABLE_HASH;

    ret = njs_object_property(vm, desc, &lhq, &value);

    if (njs_slow_path(ret == NJS_ERROR)) {
        return ret;
    }

    if (ret == NJS_OK) {
        prop->configurable = njs_is_true(&value);
    }

    if (accessor && data) {
        njs_type_error(vm, "Cannot both specify accessors "
                           "and a value or writable attribute");
        return NJS_ERROR;
    }

    return NJS_OK;
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

    njs_property_query_init(&pq, NJS_PROPERTY_QUERY_GET, 1);

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
            break;

        case NJS_PROPERTY_HANDLER:
            pq.scratch = *prop;
            prop = &pq.scratch;
            ret = prop->value.data.u.prop_handler(vm, prop, value, NULL,
                                                  &prop->value);
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
        return ret;
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

        pr = njs_object_prop_alloc(vm, &njs_object_value_string, &prop->value,
                                   1);
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

        pr = njs_object_prop_alloc(vm, &njs_object_get_string, &prop->getter,
                                   1);
        if (njs_slow_path(pr == NULL)) {
            return NJS_ERROR;
        }

        lhq.value = pr;

        ret = njs_lvlhsh_insert(&desc->hash, &lhq);
        if (njs_slow_path(ret != NJS_OK)) {
            njs_internal_error(vm, "lvlhsh insert failed");
            return NJS_ERROR;
        }

        lhq.key = njs_str_value("set");
        lhq.key_hash = NJS_SET_HASH;

        pr = njs_object_prop_alloc(vm, &njs_object_set_string, &prop->setter,
                                   1);
        if (njs_slow_path(pr == NULL)) {
            return NJS_ERROR;
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
    njs_set_object(&prop->value, object);

    lhq.proto = &njs_object_hash_proto;
    njs_string_get(&prop->name, &lhq.key);
    lhq.key_hash = njs_djb_hash(lhq.key.start, lhq.key.length);
    lhq.value = prop;
    lhq.replace = 1;
    lhq.pool = vm->mem_pool;

    ret = njs_lvlhsh_insert(njs_object_hash(value), &lhq);
    if (njs_fast_path(ret == NJS_OK)) {
        *retval = prop->value;
        return NJS_OK;
    }

    njs_internal_error(vm, "lvlhsh insert failed");

    return NJS_ERROR;
}
