
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) NGINX, Inc.
 */


#include <njs_main.h>


static njs_object_prop_t *njs_descriptor_prop(njs_vm_t *vm,
    const njs_value_t *name, const njs_object_t *descriptor);


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

        prop->getter = njs_value_invalid;
        prop->setter = njs_value_invalid;

        return prop;
    }

    njs_memory_error(vm);

    return NULL;
}


njs_object_prop_t *
njs_object_property(njs_vm_t *vm, const njs_object_t *object,
    njs_lvlhsh_query_t *lhq)
{
    njs_int_t  ret;

    lhq->proto = &njs_object_hash_proto;

    do {
        ret = njs_lvlhsh_find(&object->hash, lhq);

        if (njs_fast_path(ret == NJS_OK)) {
            return lhq->value;
        }

        ret = njs_lvlhsh_find(&object->shared_hash, lhq);

        if (njs_fast_path(ret == NJS_OK)) {
            return lhq->value;
        }

        object = object->__proto__;

    } while (object != NULL);

    return NULL;
}


/*
 * ES5.1, 8.12.9: [[DefineOwnProperty]]
 *   Limited support of special descriptors like length and array index
 *   (values can be set, but without property flags support).
 */
njs_int_t
njs_object_prop_define(njs_vm_t *vm, njs_value_t *object,
    njs_value_t *name, njs_value_t *value)
{
    njs_int_t             ret;
    njs_object_prop_t     *prop, *prev;
    njs_property_query_t  pq;

    njs_string_get(name, &pq.lhq.key);
    pq.lhq.key_hash = njs_djb_hash(pq.lhq.key.start, pq.lhq.key.length);
    pq.lhq.proto = &njs_object_hash_proto;

    njs_property_query_init(&pq, NJS_PROPERTY_QUERY_SET, 1);

    ret = njs_property_query(vm, &pq, object, name);

    if (ret != NJS_OK && ret != NJS_DECLINED) {
        return ret;
    }

    prop = njs_descriptor_prop(vm, name, njs_object(value));
    if (njs_slow_path(prop == NULL)) {
        return NJS_ERROR;
    }

    if (njs_fast_path(ret == NJS_DECLINED)) {

        /* 6.2.5.6 CompletePropertypropriptor */

        if (njs_is_accessor_descriptor(prop)) {
            if (!njs_is_valid(&prop->getter)) {
                prop->getter = njs_value_undefined;
            }

            if (!njs_is_valid(&prop->setter)) {
                prop->setter = njs_value_undefined;
            }

        } else {
            if (prop->writable == NJS_ATTRIBUTE_UNSET) {
                prop->writable = 0;
            }

            if (!njs_is_valid(&prop->value)) {
                prop->value = njs_value_undefined;
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

    if (njs_slow_path(pq.shared)) {
        ret = njs_prop_private_copy(vm, &pq);

        if (njs_slow_path(ret != NJS_OK)) {
            return ret;
        }
    }

    prev = pq.lhq.value;

    switch (prev->type) {
    case NJS_METHOD:
    case NJS_PROPERTY:
    case NJS_PROPERTY_HANDLER:
        break;

    case NJS_PROPERTY_REF:
        if (njs_is_valid(&prop->value)) {
            *prev->value.data.u.value = prop->value;
        } else {
            *prev->value.data.u.value = njs_value_undefined;
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

        if (njs_is_data_descriptor(prev)) {
            prev->getter = njs_value_undefined;
            prev->setter = njs_value_undefined;

            prev->value = njs_value_invalid;
            prev->writable = NJS_ATTRIBUTE_UNSET;

        } else {
            prev->value = njs_value_undefined;
            prev->writable = NJS_ATTRIBUTE_FALSE;

            prev->getter = njs_value_invalid;
            prev->setter = njs_value_invalid;
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
                && !njs_values_strict_equal(&prop->value, &prev->value))
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

    if (njs_is_valid(&prop->value)) {
        if (prev->type == NJS_PROPERTY_HANDLER) {
            if (njs_is_data_descriptor(prev) && prev->writable) {
                ret = prev->value.data.u.prop_handler(vm, object,
                                                         &prop->value,
                                                         &vm->retval);
                if (njs_slow_path(ret != NJS_OK)) {
                    return ret;
                }
            }

        } else {
            prev->value = prop->value;
        }
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

    njs_type_error(vm, "Cannot redefine property: \"%V\"", &pq.lhq.key);

    return NJS_ERROR;
}


static njs_object_prop_t *
njs_descriptor_prop(njs_vm_t *vm, const njs_value_t *name,
    const njs_object_t *desc)
{
    njs_bool_t          data, accessor;
    njs_object_prop_t   *prop, *pr;
    const njs_value_t   *setter, *getter;
    njs_lvlhsh_query_t  pq;

    data = 0;
    accessor = 0;

    prop = njs_object_prop_alloc(vm, name, &njs_value_invalid,
                                 NJS_ATTRIBUTE_UNSET);
    if (njs_slow_path(prop == NULL)) {
        return NULL;
    }

    getter = &njs_value_invalid;
    pq.key = njs_str_value("get");
    pq.key_hash = NJS_GET_HASH;

    pr = njs_object_property(vm, desc, &pq);
    if (pr != NULL) {
        if (njs_is_defined(&pr->value) && !njs_is_function(&pr->value)) {
            njs_type_error(vm, "Getter must be a function");
            return NULL;
        }

        accessor = 1;
        getter = &pr->value;
    }

    prop->getter = *getter;

    setter = &njs_value_invalid;
    pq.key = njs_str_value("set");
    pq.key_hash = NJS_SET_HASH;

    pr = njs_object_property(vm, desc, &pq);
    if (pr != NULL) {
        if (njs_is_defined(&pr->value) && !njs_is_function(&pr->value)) {
            njs_type_error(vm, "Setter must be a function");
            return NULL;
        }

        accessor = 1;
        setter = &pr->value;
    }

    prop->setter = *setter;

    pq.key = njs_str_value("value");
    pq.key_hash = NJS_VALUE_HASH;

    pr = njs_object_property(vm, desc, &pq);
    if (pr != NULL) {
        data = 1;
        prop->value = pr->value;
    }

    pq.key = njs_str_value("writable");
    pq.key_hash = NJS_WRITABABLE_HASH;

    pr = njs_object_property(vm, desc, &pq);
    if (pr != NULL) {
        data = 1;
        prop->writable = pr->value.data.truth;
    }

    pq.key = njs_str_value("enumerable");
    pq.key_hash = NJS_ENUMERABLE_HASH;

    pr = njs_object_property(vm, desc, &pq);
    if (pr != NULL) {
        prop->enumerable = pr->value.data.truth;
    }

    pq.key = njs_str_value("configurable");
    pq.key_hash = NJS_CONFIGURABLE_HASH;

    pr = njs_object_property(vm, desc, &pq);
    if (pr != NULL) {
        prop->configurable = pr->value.data.truth;
    }

    if (accessor && data) {
        njs_type_error(vm, "Cannot both specify accessors "
                           "and a value or writable attribute");
        return NULL;
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

    njs_property_query_init(&pq, NJS_PROPERTY_QUERY_GET, 1);

    ret = njs_property_query(vm, &pq, value, key);

    switch (ret) {
    case NJS_OK:
        break;

    case NJS_DECLINED:
        *dest = njs_value_undefined;
        return NJS_OK;

    case NJS_ERROR:
    default:
        return ret;
    }

    prop = pq.lhq.value;

    switch (prop->type) {
    case NJS_PROPERTY:
        break;

    case NJS_PROPERTY_HANDLER:
        pq.scratch = *prop;
        prop = &pq.scratch;
        ret = prop->value.data.u.prop_handler(vm, value, NULL, &prop->value);
        if (njs_slow_path(ret != NJS_OK)) {
            return ret;
        }

        break;

    case NJS_METHOD:
        if (pq.shared) {
            ret = njs_prop_private_copy(vm, &pq);

            if (njs_slow_path(ret != NJS_OK)) {
                return ret;
            }

            prop = pq.lhq.value;
        }

        break;

    default:
        njs_type_error(vm, "unexpected property type: %s",
                       njs_prop_type_string(prop->type));
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


njs_int_t
njs_prop_private_copy(njs_vm_t *vm, njs_property_query_t *pq)
{
    njs_int_t           ret;
    njs_function_t      *function;
    njs_object_prop_t   *prop, *shared, *name;
    njs_lvlhsh_query_t  lhq;

    static const njs_value_t  name_string = njs_string("name");

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

    if (!njs_is_function(&prop->value)) {
        return NJS_OK;
    }

    function = njs_function_value_copy(vm, &prop->value);
    if (njs_slow_path(function == NULL)) {
        return NJS_ERROR;
    }

    if (function->ctor) {
        function->object.shared_hash = vm->shared->function_instance_hash;

    } else {
        function->object.shared_hash = vm->shared->arrow_instance_hash;
    }

    name = njs_object_prop_alloc(vm, &name_string, &prop->name, 0);
    if (njs_slow_path(name == NULL)) {
        return NJS_ERROR;
    }

    name->configurable = 1;

    lhq.key_hash = NJS_NAME_HASH;
    lhq.key = njs_str_value("name");
    lhq.replace = 0;
    lhq.value = name;
    lhq.proto = &njs_object_hash_proto;
    lhq.pool = vm->mem_pool;

    ret = njs_lvlhsh_insert(&function->object.hash, &lhq);
    if (njs_slow_path(ret != NJS_OK)) {
        njs_internal_error(vm, "lvlhsh insert failed");
        return NJS_ERROR;
    }

    return NJS_OK;
}


const char *
njs_prop_type_string(njs_object_prop_type_t type)
{
    switch (type) {
    case NJS_PROPERTY_REF:
        return "property_ref";

    case NJS_METHOD:
        return "method";

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
