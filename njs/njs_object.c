
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) NGINX, Inc.
 */

#include <nxt_auto_config.h>
#include <nxt_types.h>
#include <nxt_clang.h>
#include <nxt_string.h>
#include <nxt_stub.h>
#include <nxt_djb_hash.h>
#include <nxt_array.h>
#include <nxt_lvlhsh.h>
#include <nxt_random.h>
#include <nxt_mem_cache_pool.h>
#include <njscript.h>
#include <njs_vm.h>
#include <njs_string.h>
#include <njs_object.h>
#include <njs_object_hash.h>
#include <njs_array.h>
#include <njs_function.h>
#include <njs_error.h>
#include <stdio.h>
#include <string.h>


static nxt_int_t njs_object_hash_test(nxt_lvlhsh_query_t *lhq, void *data);
static njs_ret_t njs_define_property(njs_vm_t *vm, njs_object_t *object,
    njs_value_t *name, njs_object_t *descriptor);


nxt_noinline njs_object_t *
njs_object_alloc(njs_vm_t *vm)
{
    njs_object_t  *object;

    object = nxt_mem_cache_alloc(vm->mem_cache_pool, sizeof(njs_object_t));

    if (nxt_fast_path(object != NULL)) {
        nxt_lvlhsh_init(&object->hash);
        nxt_lvlhsh_init(&object->shared_hash);
        object->__proto__ = &vm->prototypes[NJS_PROTOTYPE_OBJECT].object;
        object->type = NJS_OBJECT;
        object->shared = 0;
        object->extensible = 1;
    }

    return object;
}


njs_object_t *
njs_object_value_copy(njs_vm_t *vm, njs_value_t *value)
{
    njs_object_t  *object;

    object = value->data.u.object;

    if (!object->shared) {
        return object;
    }

    object = nxt_mem_cache_alloc(vm->mem_cache_pool, sizeof(njs_object_t));

    if (nxt_fast_path(object != NULL)) {
        *object = *value->data.u.object;
        object->__proto__ = &vm->prototypes[NJS_PROTOTYPE_OBJECT].object;
        object->shared = 0;
        value->data.u.object = object;
    }

    return object;
}


nxt_noinline njs_object_t *
njs_object_value_alloc(njs_vm_t *vm, const njs_value_t *value, nxt_uint_t type)
{
    nxt_uint_t          index;
    njs_object_value_t  *ov;

    ov = nxt_mem_cache_alloc(vm->mem_cache_pool, sizeof(njs_object_value_t));

    if (nxt_fast_path(ov != NULL)) {
        nxt_lvlhsh_init(&ov->object.hash);
        nxt_lvlhsh_init(&ov->object.shared_hash);
        ov->object.type = njs_object_value_type(type);
        ov->object.shared = 0;
        ov->object.extensible = 1;

        index = njs_primitive_prototype_index(type);
        ov->object.__proto__ = &vm->prototypes[index].object;

        ov->value = *value;
    }

    return &ov->object;
}


nxt_int_t
njs_object_hash_create(njs_vm_t *vm, nxt_lvlhsh_t *hash,
    const njs_object_prop_t *prop, nxt_uint_t n)
{
    nxt_int_t           ret;
    nxt_lvlhsh_query_t  lhq;

    lhq.replace = 0;
    lhq.proto = &njs_object_hash_proto;
    lhq.pool = vm->mem_cache_pool;

    do {
        njs_string_get(&prop->name, &lhq.key);
        lhq.key_hash = nxt_djb_hash(lhq.key.start, lhq.key.length);
        lhq.value = (void *) prop;

        ret = nxt_lvlhsh_insert(hash, &lhq);
        if (nxt_slow_path(ret != NXT_OK)) {
            return NXT_ERROR;
        }

        prop++;
        n--;
    } while (n != 0);

    return NXT_OK;
}


const nxt_lvlhsh_proto_t  njs_object_hash_proto
    nxt_aligned(64) =
{
    NXT_LVLHSH_DEFAULT,
    0,
    njs_object_hash_test,
    njs_lvlhsh_alloc,
    njs_lvlhsh_free,
};


static nxt_int_t
njs_object_hash_test(nxt_lvlhsh_query_t *lhq, void *data)
{
    size_t             size;
    u_char             *start;
    njs_object_prop_t  *prop;

    prop = data;

    size = prop->name.short_string.size;

    if (size != NJS_STRING_LONG) {
        if (lhq->key.length != size) {
            return NXT_DECLINED;
        }

        start = prop->name.short_string.start;

    } else {
        if (lhq->key.length != prop->name.data.string_size) {
            return NXT_DECLINED;
        }

        start = prop->name.data.u.string->start;
    }

    if (memcmp(start, lhq->key.start, lhq->key.length) == 0) {
        return NXT_OK;
    }

    return NXT_DECLINED;
}


nxt_noinline njs_object_prop_t *
njs_object_prop_alloc(njs_vm_t *vm, const njs_value_t *name,
    const njs_value_t *value, uint8_t attributes)
{
    njs_object_prop_t  *prop;

    prop = nxt_mem_cache_align(vm->mem_cache_pool, sizeof(njs_value_t),
                               sizeof(njs_object_prop_t));

    if (nxt_fast_path(prop != NULL)) {
        /* GC: retain. */
        prop->value = *value;

        /* GC: retain. */
        prop->name = *name;

        prop->type = NJS_PROPERTY;
        prop->enumerable = attributes;
        prop->writable = attributes;
        prop->configurable = attributes;
    }

    return prop;
}


nxt_noinline njs_object_prop_t *
njs_object_property(njs_vm_t *vm, njs_object_t *object, nxt_lvlhsh_query_t *lhq)
{
    nxt_int_t  ret;

    lhq->proto = &njs_object_hash_proto;

    do {
        ret = nxt_lvlhsh_find(&object->hash, lhq);

        if (nxt_fast_path(ret == NXT_OK)) {
            return lhq->value;
        }

        ret = nxt_lvlhsh_find(&object->shared_hash, lhq);

        if (nxt_fast_path(ret == NXT_OK)) {
            return lhq->value;
        }

        object = object->__proto__;

    } while (object != NULL);

    njs_exception_type_error(vm, NULL, NULL);

    return NULL;
}


njs_ret_t
njs_object_constructor(njs_vm_t *vm, njs_value_t *args, nxt_uint_t nargs,
    njs_index_t unused)
{
    nxt_uint_t    type;
    njs_value_t   *value;
    njs_object_t  *object;

    type = NJS_OBJECT;

    if (nargs == 1 || njs_is_null_or_void(&args[1])) {

        object = njs_object_alloc(vm);
        if (nxt_slow_path(object == NULL)) {
            return NXT_ERROR;
        }

    } else {
        value = &args[1];

        if (njs_is_object(value)) {
            object = value->data.u.object;

        } else if (njs_is_primitive(value)) {

            /* value->type is the same as prototype offset. */
            object = njs_object_value_alloc(vm, value, value->type);
            if (nxt_slow_path(object == NULL)) {
                return NXT_ERROR;
            }

            type = njs_object_value_type(value->type);

        } else {
            njs_exception_type_error(vm, NULL, NULL);

            return NXT_ERROR;
        }
    }

    vm->retval.data.u.object = object;
    vm->retval.type = type;
    vm->retval.data.truth = 1;

    return NXT_OK;
}


/* TODO: properties with attributes. */

static njs_ret_t
njs_object_create(njs_vm_t *vm, njs_value_t *args, nxt_uint_t nargs,
    njs_index_t unused)
{
    njs_object_t  *object;

    if (nargs > 1) {

        if (njs_is_object(&args[1]) || njs_is_null(&args[1])) {

            object = njs_object_alloc(vm);
            if (nxt_slow_path(object == NULL)) {
                return NXT_ERROR;
            }

            if (!njs_is_null(&args[1])) {
                /* GC */
                object->__proto__ = args[1].data.u.object;

            } else {
                object->shared_hash = vm->shared->null_proto_hash;
                object->__proto__ = NULL;
            }

            vm->retval.data.u.object = object;
            vm->retval.type = NJS_OBJECT;
            vm->retval.data.truth = 1;

            return NXT_OK;
        }
    }

    njs_exception_type_error(vm, NULL, NULL);

    return NXT_ERROR;
}


static njs_ret_t
njs_object_keys(njs_vm_t *vm, njs_value_t *args, nxt_uint_t nargs,
    njs_index_t unused)
{
    njs_array_t  *keys;

    if (nargs < 2 || !njs_is_object(&args[1])) {
        njs_exception_type_error(vm, NULL, NULL);
        return NXT_ERROR;
    }

    keys = njs_object_keys_array(vm, &args[1]);
    if (keys == NULL) {
        njs_exception_memory_error(vm);
        return NXT_ERROR;
    }

    vm->retval.data.u.array = keys;
    vm->retval.type = NJS_ARRAY;
    vm->retval.data.truth = 1;

    return NXT_OK;
}

njs_array_t*
njs_object_keys_array(njs_vm_t *vm, njs_value_t *object)
{
    size_t             size;
    uint32_t           i, n, keys_length, array_length;
    njs_value_t        *value;
    njs_array_t        *keys, *array;
    nxt_lvlhsh_t       *hash;
    njs_object_prop_t  *prop;
    nxt_lvlhsh_each_t  lhe;

    array = NULL;
    keys_length = 0;
    array_length = 0;

    if (njs_is_array(object)) {
        array = object->data.u.array;
        array_length = array->length;

        for (i = 0; i < array_length; i++) {
            if (njs_is_valid(&array->start[i])) {
                keys_length++;
            }
        }
    }

    nxt_lvlhsh_each_init(&lhe, &njs_object_hash_proto);

    hash = &object->data.u.object->hash;

    for ( ;; ) {
        prop = nxt_lvlhsh_each(hash, &lhe);

        if (prop == NULL) {
            break;
        }

        if (prop->enumerable) {
            keys_length++;
        }
    }

    keys = njs_array_alloc(vm, keys_length, NJS_ARRAY_SPARE);
    if (nxt_slow_path(keys == NULL)) {
        return NULL;
    }

    n = 0;

    for (i = 0; i < array_length; i++) {
        if (njs_is_valid(&array->start[i])) {
            value = &keys->start[n++];
            /*
             * The maximum array index is 4294967294, so
             * it can be stored as a short string inside value.
             */
            size = snprintf((char *) njs_string_short_start(value),
                            NJS_STRING_SHORT, "%u", i);
            njs_string_short_set(value, size, size);
        }
    }

    nxt_lvlhsh_each_init(&lhe, &njs_object_hash_proto);

    for ( ;; ) {
        prop = nxt_lvlhsh_each(hash, &lhe);

        if (prop == NULL) {
            break;
        }

        if (prop->enumerable) {
            njs_string_copy(&keys->start[n++], &prop->name);
        }
    }

    return keys;
}


static njs_ret_t
njs_object_define_property(njs_vm_t *vm, njs_value_t *args, nxt_uint_t nargs,
    njs_index_t unused)
{
    nxt_int_t  ret;

    if (nargs < 4 || !njs_is_object(&args[1]) || !njs_is_object(&args[3])) {
        njs_exception_type_error(vm, NULL, NULL);
        return NXT_ERROR;
    }

    if (!args[1].data.u.object->extensible) {
        njs_exception_type_error(vm, NULL, NULL);
        return NXT_ERROR;
    }

    ret = njs_define_property(vm, args[1].data.u.object, &args[2],
                              args[3].data.u.object);

    if (nxt_slow_path(ret != NXT_OK)) {
        return NXT_ERROR;
    }

    vm->retval = args[1];

    return NXT_OK;
}


static njs_ret_t
njs_object_define_properties(njs_vm_t *vm, njs_value_t *args, nxt_uint_t nargs,
    njs_index_t unused)
{
    nxt_int_t          ret;
    nxt_lvlhsh_t       *hash;
    njs_object_t       *object;
    nxt_lvlhsh_each_t  lhe;
    njs_object_prop_t  *prop;

    if (nargs < 3 || !njs_is_object(&args[1]) || !njs_is_object(&args[2])) {
        njs_exception_type_error(vm, NULL, NULL);
        return NXT_ERROR;
    }

    if (!args[1].data.u.object->extensible) {
        njs_exception_type_error(vm, NULL, NULL);
        return NXT_ERROR;
    }

    nxt_lvlhsh_each_init(&lhe, &njs_object_hash_proto);

    object = args[1].data.u.object;
    hash = &args[2].data.u.object->hash;

    for ( ;; ) {
        prop = nxt_lvlhsh_each(hash, &lhe);

        if (prop == NULL) {
            break;
        }

        if (prop->enumerable && njs_is_object(&prop->value)) {
            ret = njs_define_property(vm, object, &prop->name,
                                      prop->value.data.u.object);

            if (nxt_slow_path(ret != NXT_OK)) {
                return NXT_ERROR;
            }
        }
    }

    vm->retval = args[1];

    return NXT_OK;
}


static njs_ret_t
njs_define_property(njs_vm_t *vm, njs_object_t *object, njs_value_t *name,
    njs_object_t *descriptor)
{
    nxt_int_t           ret;
    njs_object_prop_t   *prop, *pr;
    nxt_lvlhsh_query_t  lhq, pq;

    njs_string_get(name, &lhq.key);
    lhq.key_hash = nxt_djb_hash(lhq.key.start, lhq.key.length);
    lhq.proto = &njs_object_hash_proto;

    ret = nxt_lvlhsh_find(&object->hash, &lhq);

    if (ret != NXT_OK) {
        prop = njs_object_prop_alloc(vm, name, &njs_value_void, 0);

        if (nxt_slow_path(prop == NULL)) {
            return NXT_ERROR;
        }

        lhq.value = prop;

    } else {
        prop = lhq.value;
    }

    pq.key = nxt_string_value("value");
    pq.key_hash = NJS_VALUE_HASH;
    pq.proto = &njs_object_hash_proto;

    pr = njs_object_property(vm, descriptor, &pq);

    if (pr != NULL) {
        prop->value = pr->value;
    }

    pq.key = nxt_string_value("configurable");
    pq.key_hash = NJS_CONFIGURABLE_HASH;

    pr = njs_object_property(vm, descriptor, &pq);

    if (pr != NULL) {
        prop->configurable = pr->value.data.truth;
    }

    pq.key = nxt_string_value("enumerable");
    pq.key_hash = NJS_ENUMERABLE_HASH;

    pr = njs_object_property(vm, descriptor, &pq);

    if (pr != NULL) {
        prop->enumerable = pr->value.data.truth;
    }

    pq.key = nxt_string_value("writable");
    pq.key_hash = NJS_WRITABABLE_HASH;

    pr = njs_object_property(vm, descriptor, &pq);

    if (pr != NULL) {
        prop->writable = pr->value.data.truth;
    }

    lhq.replace = 0;
    lhq.pool = vm->mem_cache_pool;

    ret = nxt_lvlhsh_insert(&object->hash, &lhq);
    if (nxt_slow_path(ret != NXT_OK)) {
        return NXT_ERROR;
    }

    return NXT_OK;
}


static const njs_value_t  njs_object_value_string = njs_string("value");
static const njs_value_t  njs_object_configurable_string =
                                                    njs_string("configurable");
static const njs_value_t  njs_object_enumerable_string =
                                                    njs_string("enumerable");
static const njs_value_t  njs_object_writable_string =
                                                    njs_string("writable");


static njs_ret_t
njs_object_get_own_property_descriptor(njs_vm_t *vm, njs_value_t *args,
    nxt_uint_t nargs, njs_index_t unused)
{
    uint32_t            index;
    nxt_int_t           ret;
    njs_array_t         *array;
    njs_object_t        *descriptor;
    njs_object_prop_t   *pr, *prop, array_prop;
    const njs_value_t   *value;
    nxt_lvlhsh_query_t  lhq;

    if (nargs < 3 || !njs_is_object(&args[1])) {
        njs_exception_type_error(vm, NULL, NULL);
        return NXT_ERROR;
    }

    prop = NULL;

    if (njs_is_array(&args[1])) {
        array = args[1].data.u.array;
        index = njs_string_to_index(&args[2]);

        if (index < array->length && njs_is_valid(&array->start[index])) {
            prop = &array_prop;

            array_prop.name = args[2];
            array_prop.value = array->start[index];

            array_prop.configurable = 1;
            array_prop.enumerable = 1;
            array_prop.writable = 1;
        }
    }

    lhq.proto = &njs_object_hash_proto;

    if (prop == NULL) {
        njs_string_get(&args[2], &lhq.key);
        lhq.key_hash = nxt_djb_hash(lhq.key.start, lhq.key.length);

        ret = nxt_lvlhsh_find(&args[1].data.u.object->hash, &lhq);

        if (ret != NXT_OK) {
            vm->retval = njs_string_void;
            return NXT_OK;
        }

        prop = lhq.value;
    }

    descriptor = njs_object_alloc(vm);
    if (nxt_slow_path(descriptor == NULL)) {
        return NXT_ERROR;
    }

    lhq.replace = 0;
    lhq.pool = vm->mem_cache_pool;

    lhq.key = nxt_string_value("value");
    lhq.key_hash = NJS_VALUE_HASH;

    pr = njs_object_prop_alloc(vm, &njs_object_value_string, &prop->value, 1);
    if (nxt_slow_path(pr == NULL)) {
        return NXT_ERROR;
    }

    lhq.value = pr;

    ret = nxt_lvlhsh_insert(&descriptor->hash, &lhq);
    if (nxt_slow_path(ret != NXT_OK)) {
        return NXT_ERROR;
    }

    lhq.key = nxt_string_value("configurable");
    lhq.key_hash = NJS_CONFIGURABLE_HASH;

    value = (prop->configurable == 1) ? &njs_string_true : &njs_string_false;

    pr = njs_object_prop_alloc(vm, &njs_object_configurable_string, value, 1);
    if (nxt_slow_path(pr == NULL)) {
        return NXT_ERROR;
    }

    lhq.value = pr;

    ret = nxt_lvlhsh_insert(&descriptor->hash, &lhq);
    if (nxt_slow_path(ret != NXT_OK)) {
        return NXT_ERROR;
    }

    lhq.key = nxt_string_value("enumerable");
    lhq.key_hash = NJS_ENUMERABLE_HASH;

    value = (prop->enumerable == 1) ? &njs_string_true : &njs_string_false;

    pr = njs_object_prop_alloc(vm, &njs_object_enumerable_string, value, 1);
    if (nxt_slow_path(pr == NULL)) {
        return NXT_ERROR;
    }

    lhq.value = pr;

    ret = nxt_lvlhsh_insert(&descriptor->hash, &lhq);
    if (nxt_slow_path(ret != NXT_OK)) {
        return NXT_ERROR;
    }

    lhq.key = nxt_string_value("writable");
    lhq.key_hash = NJS_WRITABABLE_HASH;

    value = (prop->writable == 1) ? &njs_string_true : &njs_string_false;

    pr = njs_object_prop_alloc(vm, &njs_object_writable_string, value, 1);
    if (nxt_slow_path(pr == NULL)) {
        return NXT_ERROR;
    }

    lhq.value = pr;

    ret = nxt_lvlhsh_insert(&descriptor->hash, &lhq);
    if (nxt_slow_path(ret != NXT_OK)) {
        return NXT_ERROR;
    }

    vm->retval.data.u.object = descriptor;
    vm->retval.type = NJS_OBJECT;
    vm->retval.data.truth = 1;

    return NXT_OK;
}


static njs_ret_t
njs_object_get_prototype_of(njs_vm_t *vm, njs_value_t *args, nxt_uint_t nargs,
    njs_index_t unused)
{
    if (nargs > 1 && njs_is_object(&args[1])) {
        njs_object_prototype_get_proto(vm, &args[1]);
        return NXT_OK;
    }

    njs_exception_type_error(vm, NULL, NULL);
    return NXT_ERROR;
}


static njs_ret_t
njs_object_freeze(njs_vm_t *vm, njs_value_t *args, nxt_uint_t nargs,
    njs_index_t unused)
{
    nxt_lvlhsh_t       *hash;
    njs_object_t       *object;
    njs_object_prop_t  *prop;
    nxt_lvlhsh_each_t  lhe;

    if (nargs < 2 || !njs_is_object(&args[1])) {
        njs_exception_type_error(vm, NULL, NULL);
        return NXT_ERROR;
    }

    object = args[1].data.u.object;
    object->extensible = 0;

    nxt_lvlhsh_each_init(&lhe, &njs_object_hash_proto);

    hash = &object->hash;

    for ( ;; ) {
        prop = nxt_lvlhsh_each(hash, &lhe);

        if (prop == NULL) {
            break;
        }

        prop->writable = 0;
        prop->configurable = 0;
    }

    vm->retval = args[1];

    return NXT_OK;
}


static njs_ret_t
njs_object_is_frozen(njs_vm_t *vm, njs_value_t *args, nxt_uint_t nargs,
    njs_index_t unused)
{
    nxt_lvlhsh_t       *hash;
    njs_object_t       *object;
    njs_object_prop_t  *prop;
    nxt_lvlhsh_each_t  lhe;
    const njs_value_t  *retval;

    if (nargs < 2 || !njs_is_object(&args[1])) {
        njs_exception_type_error(vm, NULL, NULL);
        return NXT_ERROR;
    }

    retval = &njs_string_false;

    object = args[1].data.u.object;
    nxt_lvlhsh_each_init(&lhe, &njs_object_hash_proto);

    hash = &object->hash;

    if (object->extensible) {
        goto done;
    }

    for ( ;; ) {
        prop = nxt_lvlhsh_each(hash, &lhe);

        if (prop == NULL) {
            break;
        }

        if (prop->writable || prop->configurable) {
            goto done;
        }
    }

    retval = &njs_string_true;

done:

    vm->retval = *retval;

    return NXT_OK;
}


static njs_ret_t
njs_object_seal(njs_vm_t *vm, njs_value_t *args, nxt_uint_t nargs,
    njs_index_t unused)
{
    nxt_lvlhsh_t       *hash;
    njs_object_t       *object;
    njs_object_prop_t  *prop;
    nxt_lvlhsh_each_t  lhe;

    if (nargs < 2 || !njs_is_object(&args[1])) {
        njs_exception_type_error(vm, NULL, NULL);
        return NXT_ERROR;
    }

    object = args[1].data.u.object;
    object->extensible = 0;

    nxt_lvlhsh_each_init(&lhe, &njs_object_hash_proto);

    hash = &object->hash;

    for ( ;; ) {
        prop = nxt_lvlhsh_each(hash, &lhe);

        if (prop == NULL) {
            break;
        }

        prop->configurable = 0;
    }

    vm->retval = args[1];

    return NXT_OK;
}


static njs_ret_t
njs_object_is_sealed(njs_vm_t *vm, njs_value_t *args, nxt_uint_t nargs,
    njs_index_t unused)
{
    nxt_lvlhsh_t       *hash;
    njs_object_t       *object;
    njs_object_prop_t  *prop;
    nxt_lvlhsh_each_t  lhe;
    const njs_value_t  *retval;

    if (nargs < 2 || !njs_is_object(&args[1])) {
        njs_exception_type_error(vm, NULL, NULL);
        return NXT_ERROR;
    }

    retval = &njs_string_false;

    object = args[1].data.u.object;
    nxt_lvlhsh_each_init(&lhe, &njs_object_hash_proto);

    hash = &object->hash;

    if (object->extensible) {
        goto done;
    }

    for ( ;; ) {
        prop = nxt_lvlhsh_each(hash, &lhe);

        if (prop == NULL) {
            break;
        }

        if (prop->configurable) {
            goto done;
        }
    }

    retval = &njs_string_true;

done:

    vm->retval = *retval;

    return NXT_OK;
}


static njs_ret_t
njs_object_prevent_extensions(njs_vm_t *vm, njs_value_t *args, nxt_uint_t nargs,
    njs_index_t unused)
{
    if (nargs < 2 || !njs_is_object(&args[1])) {
        njs_exception_type_error(vm, NULL, NULL);
        return NXT_ERROR;
    }

    args[1].data.u.object->extensible = 0;

    vm->retval = args[1];

    return NXT_OK;
}


static njs_ret_t
njs_object_is_extensible(njs_vm_t *vm, njs_value_t *args, nxt_uint_t nargs,
    njs_index_t unused)
{
    const njs_value_t  *retval;

    if (nargs < 2 || !njs_is_object(&args[1])) {
        njs_exception_type_error(vm, NULL, NULL);
        return NXT_ERROR;
    }

    retval = args[1].data.u.object->extensible ? &njs_string_true
                                               : &njs_string_false;

    vm->retval = *retval;

    return NXT_OK;
}


/*
 * The __proto__ property of booleans, numbers and strings primitives,
 * of objects created by Boolean(), Number(), and String() constructors,
 * and of Boolean.prototype, Number.prototype, and String.prototype objects.
 */

njs_ret_t
njs_primitive_prototype_get_proto(njs_vm_t *vm, njs_value_t *value)
{
    nxt_uint_t    index;
    njs_object_t  *proto;

    /*
     * The __proto__ getters reside in object prototypes of primitive types
     * and have to return different results for primitive type and for objects.
     */
    if (njs_is_object(value)) {
        proto = value->data.u.object->__proto__;

    } else {
        index = njs_primitive_prototype_index(value->type);
        proto = &vm->prototypes[index].object;
    }

    vm->retval.data.u.object = proto;
    vm->retval.type = proto->type;
    vm->retval.data.truth = 1;

    return NXT_OK;
}


/*
 * The "prototype" property of Object(), Array() and other functions is
 * created on demand in the functions' private hash by the "prototype"
 * getter.  The properties are set to appropriate prototype.
 */

njs_ret_t
njs_object_prototype_create(njs_vm_t *vm, njs_value_t *value)
{
    int32_t         index;
    njs_value_t     *proto;
    njs_function_t  *function;

    proto = NULL;
    function = value->data.u.function;
    index = function - vm->constructors;

    if (index >= 0 && index < NJS_PROTOTYPE_MAX) {
        proto = njs_property_prototype_create(vm, &function->object.hash,
                                              &vm->prototypes[index].object);
    }

    if (proto == NULL) {
        proto = (njs_value_t *) &njs_value_void;
    }

    vm->retval = *proto;

    return NXT_OK;
}


njs_value_t *
njs_property_prototype_create(njs_vm_t *vm, nxt_lvlhsh_t *hash,
    njs_object_t *prototype)
{
    nxt_int_t                  ret;
    njs_object_prop_t          *prop;
    nxt_lvlhsh_query_t         lhq;

    static const njs_value_t   prototype_string = njs_string("prototype");

    prop = njs_object_prop_alloc(vm, &prototype_string, &njs_value_void, 0);
    if (nxt_slow_path(prop == NULL)) {
        return NULL;
    }

    /* GC */

    prop->value.data.u.object = prototype;
    prop->value.type = prototype->type;
    prop->value.data.truth = 1;

    lhq.value = prop;
    lhq.key_hash = NJS_PROTOTYPE_HASH;
    lhq.key = nxt_string_value("prototype");
    lhq.replace = 0;
    lhq.pool = vm->mem_cache_pool;
    lhq.proto = &njs_object_hash_proto;

    ret = nxt_lvlhsh_insert(hash, &lhq);

    if (nxt_fast_path(ret == NXT_OK)) {
        return &prop->value;
    }

    /* Memory allocation or NXT_DECLINED error. */
    njs_exception_internal_error(vm, NULL, NULL);

    return NULL;
}


static const njs_object_prop_t  njs_object_constructor_properties[] =
{
    /* Object.name == "Object". */
    {
        .type = NJS_PROPERTY,
        .name = njs_string("name"),
        .value = njs_string("Object"),
    },

    /* Object.length == 1. */
    {
        .type = NJS_PROPERTY,
        .name = njs_string("length"),
        .value = njs_value(NJS_NUMBER, 1, 1.0),
    },

    /* Object.prototype. */
    {
        .type = NJS_NATIVE_GETTER,
        .name = njs_string("prototype"),
        .value = njs_native_getter(njs_object_prototype_create),
    },

    /* Object.create(). */
    {
        .type = NJS_METHOD,
        .name = njs_string("create"),
        .value = njs_native_function(njs_object_create, 0, 0),
    },

    /* Object.keys(). */
    {
        .type = NJS_METHOD,
        .name = njs_string("keys"),
        .value = njs_native_function(njs_object_keys, 0,
                                     NJS_SKIP_ARG, NJS_OBJECT_ARG),
    },

    /* Object.defineProperty(). */
    {
        .type = NJS_METHOD,
        .name = njs_string("defineProperty"),
        .value = njs_native_function(njs_object_define_property, 0,
                                     NJS_SKIP_ARG, NJS_OBJECT_ARG,
                                     NJS_STRING_ARG, NJS_OBJECT_ARG),
    },

    /* Object.defineProperties(). */
    {
        .type = NJS_METHOD,
        .name = njs_long_string("defineProperties"),
        .value = njs_native_function(njs_object_define_properties, 0,
                                     NJS_SKIP_ARG, NJS_OBJECT_ARG,
                                     NJS_OBJECT_ARG),
    },

    /* Object.getOwnPropertyDescriptor(). */
    {
        .type = NJS_METHOD,
        .name = njs_long_string("getOwnPropertyDescriptor"),
        .value = njs_native_function(njs_object_get_own_property_descriptor, 0,
                                     NJS_SKIP_ARG, NJS_OBJECT_ARG,
                                     NJS_STRING_ARG),
    },

    /* Object.getPrototypeOf(). */
    {
        .type = NJS_METHOD,
        .name = njs_string("getPrototypeOf"),
        .value = njs_native_function(njs_object_get_prototype_of, 0,
                                     NJS_SKIP_ARG, NJS_OBJECT_ARG),
    },

    /* Object.freeze(). */
    {
        .type = NJS_METHOD,
        .name = njs_string("freeze"),
        .value = njs_native_function(njs_object_freeze, 0,
                                     NJS_SKIP_ARG, NJS_OBJECT_ARG),
    },

    /* Object.isFrozen(). */
    {
        .type = NJS_METHOD,
        .name = njs_string("isFrozen"),
        .value = njs_native_function(njs_object_is_frozen, 0,
                                     NJS_SKIP_ARG, NJS_OBJECT_ARG),
    },

    /* Object.seal(). */
    {
        .type = NJS_METHOD,
        .name = njs_string("seal"),
        .value = njs_native_function(njs_object_seal, 0,
                                     NJS_SKIP_ARG, NJS_OBJECT_ARG),
    },

    /* Object.isSealed(). */
    {
        .type = NJS_METHOD,
        .name = njs_string("isSealed"),
        .value = njs_native_function(njs_object_is_sealed, 0,
                                     NJS_SKIP_ARG, NJS_OBJECT_ARG),
    },

    /* Object.preventExtensions(). */
    {
        .type = NJS_METHOD,
        .name = njs_long_string("preventExtensions"),
        .value = njs_native_function(njs_object_prevent_extensions, 0,
                                     NJS_SKIP_ARG, NJS_OBJECT_ARG),
    },

    /* Object.isExtensible(). */
    {
        .type = NJS_METHOD,
        .name = njs_string("isExtensible"),
        .value = njs_native_function(njs_object_is_extensible, 0,
                                     NJS_SKIP_ARG, NJS_OBJECT_ARG),
    },
};


const njs_object_init_t  njs_object_constructor_init = {
    nxt_string("Object"),
    njs_object_constructor_properties,
    nxt_nitems(njs_object_constructor_properties),
};


njs_ret_t
njs_object_prototype_get_proto(njs_vm_t *vm, njs_value_t *value)
{
    njs_object_t  *proto;

    proto = value->data.u.object->__proto__;

    if (nxt_fast_path(proto != NULL)) {
        vm->retval.data.u.object = proto;
        vm->retval.type = proto->type;
        vm->retval.data.truth = 1;

    } else {
        vm->retval = njs_value_null;
    }

    return NXT_OK;
}


/*
 * The "constructor" property of Object(), Array() and other functions
 * prototypes is created on demand in the prototypes' private hash by the
 * "constructor" getter.  The properties are set to appropriate function.
 */

static njs_ret_t
njs_object_prototype_create_constructor(njs_vm_t *vm, njs_value_t *value)
{
    int32_t                 index;
    njs_value_t             *cons;
    njs_object_t            *object;
    njs_object_prototype_t  *prototype;

    if (njs_is_object(value)) {
        object = value->data.u.object;

        do {
            prototype = (njs_object_prototype_t *) object;
            index = prototype - vm->prototypes;

            if (index >= 0 && index < NJS_PROTOTYPE_MAX) {
                goto found;
            }

            object = object->__proto__;

        } while (object != NULL);

        nxt_thread_log_alert("prototype not found");

        return NXT_ERROR;

    } else {
        index = njs_primitive_prototype_index(value->type);
        prototype = &vm->prototypes[index];
    }

found:

    cons = njs_property_constructor_create(vm, &prototype->object.hash,
                                          &vm->scopes[NJS_SCOPE_GLOBAL][index]);
    if (nxt_fast_path(cons != NULL)) {
        vm->retval = *cons;
        return NXT_OK;
    }

    return NXT_ERROR;
}


njs_value_t *
njs_property_constructor_create(njs_vm_t *vm, nxt_lvlhsh_t *hash,
    njs_value_t *constructor)
{
    nxt_int_t                 ret;
    njs_object_prop_t         *prop;
    nxt_lvlhsh_query_t        lhq;

    static const njs_value_t  constructor_string = njs_string("constructor");

    prop = njs_object_prop_alloc(vm, &constructor_string, constructor, 1);
    if (nxt_slow_path(prop == NULL)) {
        return NULL;
    }

    /* GC */

    prop->value = *constructor;
    prop->enumerable = 0;

    lhq.value = prop;
    lhq.key_hash = NJS_CONSTRUCTOR_HASH;
    lhq.key = nxt_string_value("constructor");
    lhq.replace = 0;
    lhq.pool = vm->mem_cache_pool;
    lhq.proto = &njs_object_hash_proto;

    ret = nxt_lvlhsh_insert(hash, &lhq);

    if (nxt_fast_path(ret == NXT_OK)) {
        return &prop->value;
    }

    /* Memory allocation or NXT_DECLINED error. */
    njs_exception_internal_error(vm, NULL, NULL);

    return NULL;
}


static njs_ret_t
njs_object_prototype_value_of(njs_vm_t *vm, njs_value_t *args, nxt_uint_t nargs,
    njs_index_t unused)
{
    vm->retval = args[0];

    return NXT_OK;
}


static const njs_value_t  njs_object_null_string = njs_string("[object Null]");
static const njs_value_t  njs_object_undefined_string =
                                     njs_long_string("[object Undefined]");
static const njs_value_t  njs_object_boolean_string =
                                     njs_long_string("[object Boolean]");
static const njs_value_t  njs_object_number_string =
                                     njs_long_string("[object Number]");
static const njs_value_t  njs_object_string_string =
                                     njs_long_string("[object String]");
static const njs_value_t  njs_object_object_string =
                                     njs_long_string("[object Object]");
static const njs_value_t  njs_object_array_string =
                                     njs_string("[object Array]");
static const njs_value_t  njs_object_function_string =
                                     njs_long_string("[object Function]");
static const njs_value_t  njs_object_regexp_string =
                                     njs_long_string("[object RegExp]");
static const njs_value_t  njs_object_date_string = njs_string("[object Date]");
static const njs_value_t  njs_object_error_string =
                                     njs_string("[object Error]");
static const njs_value_t  njs_object_eval_error_string =
                                     njs_long_string("[object EvalError]");
static const njs_value_t  njs_object_internal_error_string =
                                     njs_long_string("[object InternalError]");
static const njs_value_t  njs_object_range_error_string =
                                     njs_long_string("[object RangeError]");
static const njs_value_t  njs_object_ref_error_string =
                                     njs_long_string("[object RefError]");
static const njs_value_t  njs_object_syntax_error_string =
                                     njs_long_string("[object SyntaxError]");
static const njs_value_t  njs_object_type_error_string =
                                     njs_long_string("[object TypeError]");
static const njs_value_t  njs_object_uri_error_string =
                                     njs_long_string("[object URIError]");


njs_ret_t
njs_object_prototype_to_string(njs_vm_t *vm, njs_value_t *args,
    nxt_uint_t nargs, njs_index_t unused)
{
    int32_t                 index;
    njs_object_t            *object;
    njs_object_prototype_t  *prototype;

    static const njs_value_t  *class_name[] = {
        /* Primitives. */
        &njs_object_null_string,
        &njs_object_undefined_string,
        &njs_object_boolean_string,
        &njs_object_number_string,
        &njs_object_string_string,

        &njs_string_empty,
        &njs_object_function_string,
        &njs_string_empty,
        &njs_string_empty,
        &njs_string_empty,
        &njs_string_empty,
        &njs_string_empty,
        &njs_string_empty,
        &njs_string_empty,
        &njs_string_empty,
        &njs_string_empty,

        /* Objects. */
        &njs_object_object_string,
        &njs_object_array_string,
        &njs_object_boolean_string,
        &njs_object_number_string,
        &njs_object_string_string,
        &njs_object_function_string,
        &njs_object_regexp_string,
        &njs_object_date_string,
        &njs_object_error_string,
        &njs_object_eval_error_string,
        &njs_object_internal_error_string,
        &njs_object_range_error_string,
        &njs_object_ref_error_string,
        &njs_object_syntax_error_string,
        &njs_object_type_error_string,
        &njs_object_uri_error_string,
    };

    index = args[0].type;

    if (njs_is_object(&args[0])) {
        object = args[0].data.u.object;

        do {
            prototype = (njs_object_prototype_t *) object;
            index = prototype - vm->prototypes;

            if (index >= 0 && index < NJS_PROTOTYPE_MAX) {
                index += NJS_OBJECT;
                goto found;
            }

            object = object->__proto__;

        } while (object != NULL);

        nxt_thread_log_alert("prototype not found");

        return NXT_ERROR;
    }

found:

    vm->retval = *class_name[index];

    return NXT_OK;
}


static njs_ret_t
njs_object_prototype_has_own_property(njs_vm_t *vm, njs_value_t *args,
    nxt_uint_t nargs, njs_index_t unused)
{
    uint32_t            index;
    nxt_int_t           ret;
    njs_array_t         *array;
    const njs_value_t   *retval;
    nxt_lvlhsh_query_t  lhq;

    retval = &njs_string_false;

    if (nargs > 1 && njs_is_object(&args[0])) {

        if (njs_is_array(&args[0])) {
            array = args[0].data.u.array;
            index = njs_string_to_index(&args[1]);

            if (index < array->length && njs_is_valid(&array->start[index])) {
                retval = &njs_string_true;
                goto done;
            }
        }

        njs_string_get(&args[1], &lhq.key);
        lhq.key_hash = nxt_djb_hash(lhq.key.start, lhq.key.length);
        lhq.proto = &njs_object_hash_proto;

        ret = nxt_lvlhsh_find(&args[0].data.u.object->hash, &lhq);

        if (ret == NXT_OK) {
            retval = &njs_string_true;
        }
    }

done:

    vm->retval = *retval;

    return NXT_OK;
}


static njs_ret_t
njs_object_prototype_is_prototype_of(njs_vm_t *vm, njs_value_t *args,
    nxt_uint_t nargs, njs_index_t unused)
{
    njs_object_t       *object, *proto;
    const njs_value_t  *retval;

    retval = &njs_string_false;

    if (nargs > 1 && njs_is_object(&args[0]) && njs_is_object(&args[1])) {
        proto = args[0].data.u.object;
        object = args[1].data.u.object;

        do {
            object = object->__proto__;

            if (object == proto) {
                retval = &njs_string_true;
                break;
            }

        } while (object != NULL);
    }

    vm->retval = *retval;

    return NXT_OK;
}


static const njs_object_prop_t  njs_object_prototype_properties[] =
{
    {
        .type = NJS_NATIVE_GETTER,
        .name = njs_string("__proto__"),
        .value = njs_native_getter(njs_object_prototype_get_proto),
    },

    {
        .type = NJS_NATIVE_GETTER,
        .name = njs_string("constructor"),
        .value = njs_native_getter(njs_object_prototype_create_constructor),
    },

    {
        .type = NJS_METHOD,
        .name = njs_string("valueOf"),
        .value = njs_native_function(njs_object_prototype_value_of, 0, 0),
    },

    {
        .type = NJS_METHOD,
        .name = njs_string("toString"),
        .value = njs_native_function(njs_object_prototype_to_string, 0, 0),
    },

    {
        .type = NJS_METHOD,
        .name = njs_string("hasOwnProperty"),
        .value = njs_native_function(njs_object_prototype_has_own_property, 0,
                                     NJS_OBJECT_ARG, NJS_STRING_ARG),
    },

    {
        .type = NJS_METHOD,
        .name = njs_string("isPrototypeOf"),
        .value = njs_native_function(njs_object_prototype_is_prototype_of, 0,
                                     NJS_OBJECT_ARG, NJS_OBJECT_ARG),
    },
};


const njs_object_init_t  njs_object_prototype_init = {
    nxt_string("Object"),
    njs_object_prototype_properties,
    nxt_nitems(njs_object_prototype_properties),
};
