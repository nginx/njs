
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) NGINX, Inc.
 */

#include <njs_main.h>
#include <string.h>


static njs_int_t njs_object_hash_test(njs_lvlhsh_query_t *lhq, void *data);
static njs_object_prop_t *njs_object_exist_in_proto(const njs_object_t *begin,
    const njs_object_t *end, njs_lvlhsh_query_t *lhq);
static uint32_t njs_object_enumerate_array_length(const njs_object_t *object);
static uint32_t njs_object_enumerate_string_length(const njs_object_t *object);
static uint32_t njs_object_enumerate_object_length(const njs_object_t *object,
    njs_bool_t all);
static uint32_t njs_object_own_enumerate_object_length(
    const njs_object_t *object, const njs_object_t *parent, njs_bool_t all);
static njs_int_t njs_object_enumerate_array(njs_vm_t *vm,
    const njs_array_t *array, njs_array_t *items, njs_object_enum_t kind);
static njs_int_t njs_object_enumerate_string(njs_vm_t *vm,
    const njs_value_t *value, njs_array_t *items, njs_object_enum_t kind);
static njs_int_t njs_object_enumerate_object(njs_vm_t *vm,
    const njs_object_t *object, njs_array_t *items, njs_object_enum_t kind,
    njs_bool_t all);
static njs_int_t njs_object_own_enumerate_object(njs_vm_t *vm,
    const njs_object_t *object, const njs_object_t *parent, njs_array_t *items,
    njs_object_enum_t kind, njs_bool_t all);


njs_object_t *
njs_object_alloc(njs_vm_t *vm)
{
    njs_object_t  *object;

    object = njs_mp_alloc(vm->mem_pool, sizeof(njs_object_t));

    if (njs_fast_path(object != NULL)) {
        njs_lvlhsh_init(&object->hash);
        njs_lvlhsh_init(&object->shared_hash);
        object->__proto__ = &vm->prototypes[NJS_PROTOTYPE_OBJECT].object;
        object->type = NJS_OBJECT;
        object->shared = 0;
        object->extensible = 1;
        return object;
    }

    njs_memory_error(vm);

    return NULL;
}


njs_object_t *
njs_object_value_copy(njs_vm_t *vm, njs_value_t *value)
{
    njs_object_t  *object;

    object = njs_object(value);

    if (!object->shared) {
        return object;
    }

    object = njs_mp_alloc(vm->mem_pool, sizeof(njs_object_t));

    if (njs_fast_path(object != NULL)) {
        *object = *njs_object(value);
        object->__proto__ = &vm->prototypes[NJS_PROTOTYPE_OBJECT].object;
        object->shared = 0;
        value->data.u.object = object;
        return object;
    }

    njs_memory_error(vm);

    return NULL;
}


njs_object_t *
njs_object_value_alloc(njs_vm_t *vm, const njs_value_t *value, njs_uint_t type)
{
    njs_uint_t          index;
    njs_object_value_t  *ov;

    ov = njs_mp_alloc(vm->mem_pool, sizeof(njs_object_value_t));

    if (njs_fast_path(ov != NULL)) {
        njs_lvlhsh_init(&ov->object.hash);

        if (type == NJS_STRING) {
            ov->object.shared_hash = vm->shared->string_instance_hash;

        } else {
            njs_lvlhsh_init(&ov->object.shared_hash);
        }

        ov->object.type = njs_object_value_type(type);
        ov->object.shared = 0;
        ov->object.extensible = 1;

        index = njs_primitive_prototype_index(type);
        ov->object.__proto__ = &vm->prototypes[index].object;

        ov->value = *value;

        return &ov->object;
    }

    njs_memory_error(vm);

    return NULL;
}


njs_int_t
njs_object_hash_create(njs_vm_t *vm, njs_lvlhsh_t *hash,
    const njs_object_prop_t *prop, njs_uint_t n)
{
    njs_int_t           ret;
    njs_lvlhsh_query_t  lhq;

    lhq.replace = 0;
    lhq.proto = &njs_object_hash_proto;
    lhq.pool = vm->mem_pool;

    while (n != 0) {
        njs_string_get(&prop->name, &lhq.key);
        lhq.key_hash = njs_djb_hash(lhq.key.start, lhq.key.length);
        lhq.value = (void *) prop;

        ret = njs_lvlhsh_insert(hash, &lhq);
        if (njs_slow_path(ret != NJS_OK)) {
            njs_internal_error(vm, "lvlhsh insert failed");
            return NJS_ERROR;
        }

        prop++;
        n--;
    }

    return NJS_OK;
}


const njs_lvlhsh_proto_t  njs_object_hash_proto
    njs_aligned(64) =
{
    NJS_LVLHSH_DEFAULT,
    njs_object_hash_test,
    njs_lvlhsh_alloc,
    njs_lvlhsh_free,
};


static njs_int_t
njs_object_hash_test(njs_lvlhsh_query_t *lhq, void *data)
{
    size_t             size;
    u_char             *start;
    njs_object_prop_t  *prop;

    prop = data;

    size = prop->name.short_string.size;

    if (size != NJS_STRING_LONG) {
        if (lhq->key.length != size) {
            return NJS_DECLINED;
        }

        start = prop->name.short_string.start;

    } else {
        if (lhq->key.length != prop->name.long_string.size) {
            return NJS_DECLINED;
        }

        start = prop->name.long_string.data->start;
    }

    if (memcmp(start, lhq->key.start, lhq->key.length) == 0) {
        return NJS_OK;
    }

    return NJS_DECLINED;
}


njs_int_t
njs_object_constructor(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t unused)
{
    njs_uint_t         type;
    njs_object_t       *object;
    const njs_value_t  *value;

    value = njs_arg(args, nargs, 1);
    type = value->type;

    if (njs_is_null_or_undefined(value)) {

        object = njs_object_alloc(vm);
        if (njs_slow_path(object == NULL)) {
            return NJS_ERROR;
        }

        type = NJS_OBJECT;

    } else {

        if (njs_is_object(value)) {
            object = njs_object(value);

        } else if (njs_is_primitive(value)) {

            /* value->type is the same as prototype offset. */
            object = njs_object_value_alloc(vm, value, type);
            if (njs_slow_path(object == NULL)) {
                return NJS_ERROR;
            }

            type = njs_object_value_type(type);

        } else {
            njs_type_error(vm, "unexpected constructor argument:%s",
                           njs_type_string(type));

            return NJS_ERROR;
        }
    }

    njs_set_type_object(&vm->retval, object, type);

    return NJS_OK;
}


/* TODO: properties with attributes. */

static njs_int_t
njs_object_create(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t unused)
{
    njs_object_t       *object;
    const njs_value_t  *value;

    value = njs_arg(args, nargs, 1);

    if (njs_is_object(value) || njs_is_null(value)) {

        object = njs_object_alloc(vm);
        if (njs_slow_path(object == NULL)) {
            return NJS_ERROR;
        }

        if (!njs_is_null(value)) {
            /* GC */
            object->__proto__ = njs_object(value);

        } else {
            object->__proto__ = NULL;
        }

        njs_set_object(&vm->retval, object);

        return NJS_OK;
    }

    njs_type_error(vm, "prototype may only be an object or null: %s",
                   njs_type_string(value->type));

    return NJS_ERROR;
}


static njs_int_t
njs_object_keys(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t unused)
{
    njs_array_t        *keys;
    const njs_value_t  *value;

    value = njs_arg(args, nargs, 1);

    if (njs_is_null_or_undefined(value)) {
        njs_type_error(vm, "cannot convert %s argument to object",
                       njs_type_string(value->type));

        return NJS_ERROR;
    }

    keys = njs_value_own_enumerate(vm, value, NJS_ENUM_KEYS, 0);
    if (keys == NULL) {
        return NJS_ERROR;
    }

    njs_set_array(&vm->retval, keys);

    return NJS_OK;
}


static njs_int_t
njs_object_values(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t unused)
{
    njs_array_t        *array;
    const njs_value_t  *value;

    value = njs_arg(args, nargs, 1);

    if (njs_is_null_or_undefined(value)) {
        njs_type_error(vm, "cannot convert %s argument to object",
                       njs_type_string(value->type));

        return NJS_ERROR;
    }

    array = njs_value_own_enumerate(vm, value, NJS_ENUM_VALUES, 0);
    if (array == NULL) {
        return NJS_ERROR;
    }

    njs_set_array(&vm->retval, array);

    return NJS_OK;
}


static njs_int_t
njs_object_entries(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t unused)
{
    njs_array_t        *array;
    const njs_value_t  *value;

    value = njs_arg(args, nargs, 1);

    if (njs_is_null_or_undefined(value)) {
        njs_type_error(vm, "cannot convert %s argument to object",
                       njs_type_string(value->type));

        return NJS_ERROR;
    }

    array = njs_value_own_enumerate(vm, value, NJS_ENUM_BOTH, 0);
    if (array == NULL) {
        return NJS_ERROR;
    }

    njs_set_array(&vm->retval, array);

    return NJS_OK;
}


static njs_object_prop_t *
njs_object_exist_in_proto(const njs_object_t *object, const njs_object_t *end,
    njs_lvlhsh_query_t *lhq)
{
    njs_int_t          ret;
    njs_object_prop_t  *prop;

    lhq->proto = &njs_object_hash_proto;

    while (object != end) {
        ret = njs_lvlhsh_find(&object->hash, lhq);

        if (njs_fast_path(ret == NJS_OK)) {
            prop = lhq->value;

            if (prop->type == NJS_WHITEOUT) {
                goto next;
            }

            return lhq->value;
        }

        ret = njs_lvlhsh_find(&object->shared_hash, lhq);

        if (njs_fast_path(ret == NJS_OK)) {
            return lhq->value;
        }

next:

        object = object->__proto__;
    }

    return NULL;
}


njs_inline uint32_t
njs_object_enumerate_length(const njs_object_t *object, njs_bool_t all)
{
    uint32_t  length;

    length = njs_object_enumerate_object_length(object, all);

    switch (object->type) {
    case NJS_ARRAY:
        length += njs_object_enumerate_array_length(object);
        break;

    case NJS_OBJECT_STRING:
        length += njs_object_enumerate_string_length(object);
        break;

    default:
        break;
    }

    return length;
}


njs_inline uint32_t
njs_object_own_enumerate_length(const njs_object_t *object,
    const njs_object_t *parent, njs_bool_t all)
{
    uint32_t  length;

    length = njs_object_own_enumerate_object_length(object, parent, all);

    switch (object->type) {
    case NJS_ARRAY:
        length += njs_object_enumerate_array_length(object);
        break;

    case NJS_OBJECT_STRING:
        length += njs_object_enumerate_string_length(object);
        break;

    default:
        break;
    }

    return length;
}


njs_inline njs_int_t
njs_object_enumerate_value(njs_vm_t *vm, const njs_object_t *object,
    njs_array_t *items, njs_object_enum_t kind, njs_bool_t all)
{
    njs_int_t           ret;
    njs_object_value_t  *obj_val;

    switch (object->type) {
    case NJS_ARRAY:
        ret = njs_object_enumerate_array(vm, (njs_array_t *) object, items,
                                         kind);
        break;

    case NJS_OBJECT_STRING:
        obj_val = (njs_object_value_t *) object;

        ret = njs_object_enumerate_string(vm, &obj_val->value, items, kind);
        break;

    default:
        goto object;
    }

    if (njs_slow_path(ret != NJS_OK)) {
        return NJS_ERROR;
    }

object:

    ret = njs_object_enumerate_object(vm, object, items, kind, all);
    if (njs_slow_path(ret != NJS_OK)) {
        return NJS_ERROR;
    }

    return NJS_OK;
}


njs_inline njs_int_t
njs_object_own_enumerate_value(njs_vm_t *vm, const njs_object_t *object,
    const njs_object_t *parent, njs_array_t *items, njs_object_enum_t kind,
    njs_bool_t all)
{
    njs_int_t           ret;
    njs_object_value_t  *obj_val;

    switch (object->type) {
    case NJS_ARRAY:
        ret = njs_object_enumerate_array(vm, (njs_array_t *) object, items,
                                         kind);
        break;

    case NJS_OBJECT_STRING:
        obj_val = (njs_object_value_t *) object;

        ret = njs_object_enumerate_string(vm, &obj_val->value, items, kind);
        break;

    default:
        goto object;
    }

    if (njs_slow_path(ret != NJS_OK)) {
        return NJS_ERROR;
    }

object:

    ret = njs_object_own_enumerate_object(vm, object, parent, items, kind, all);
    if (njs_slow_path(ret != NJS_OK)) {
        return NJS_ERROR;
    }

    return NJS_OK;
}


njs_array_t *
njs_object_enumerate(njs_vm_t *vm, const njs_object_t *object,
    njs_object_enum_t kind, njs_bool_t all)
{
    uint32_t     length;
    njs_int_t    ret;
    njs_array_t  *items;

    length = njs_object_enumerate_length(object, all);

    items = njs_array_alloc(vm, length, NJS_ARRAY_SPARE);
    if (njs_slow_path(items == NULL)) {
        return NULL;
    }

    ret = njs_object_enumerate_value(vm, object, items, kind, all);
    if (njs_slow_path(ret != NJS_OK)) {
        return NULL;
    }

    items->start -= items->length;

    return items;
}


njs_array_t *
njs_object_own_enumerate(njs_vm_t *vm, const njs_object_t *object,
    njs_object_enum_t kind, njs_bool_t all)
{
    uint32_t     length;
    njs_int_t    ret;
    njs_array_t  *items;

    length = njs_object_own_enumerate_length(object, object, all);

    items = njs_array_alloc(vm, length, NJS_ARRAY_SPARE);
    if (njs_slow_path(items == NULL)) {
        return NULL;
    }

    ret = njs_object_own_enumerate_value(vm, object, object, items, kind, all);
    if (njs_slow_path(ret != NJS_OK)) {
        return NULL;
    }

    items->start -= items->length;

    return items;
}


static uint32_t
njs_object_enumerate_array_length(const njs_object_t *object)
{
    uint32_t     i, length;
    njs_array_t  *array;

    length = 0;
    array = (njs_array_t *) object;

    for (i = 0; i < array->length; i++) {
        if (njs_is_valid(&array->start[i])) {
            length++;
        }
    }

    return length;
}


static uint32_t
njs_object_enumerate_string_length(const njs_object_t *object)
{
    njs_object_value_t  *obj_val;

    obj_val = (njs_object_value_t *) object;

    return njs_string_length(&obj_val->value);
}


static uint32_t
njs_object_enumerate_object_length(const njs_object_t *object, njs_bool_t all)
{
    uint32_t            length;
    const njs_object_t  *proto;

    length = njs_object_own_enumerate_object_length(object, object, all);

    proto = object->__proto__;

    while (proto != NULL) {
        length += njs_object_own_enumerate_length(proto, object, all);
        proto = proto->__proto__;
    }

    return length;
}


static uint32_t
njs_object_own_enumerate_object_length(const njs_object_t *object,
    const njs_object_t *parent, njs_bool_t all)
{
    uint32_t            length;
    njs_int_t           ret;
    njs_lvlhsh_each_t   lhe;
    njs_object_prop_t   *prop, *ext_prop;
    njs_lvlhsh_query_t  lhq;
    const njs_lvlhsh_t  *hash;

    njs_lvlhsh_each_init(&lhe, &njs_object_hash_proto);
    hash = &object->hash;

    length = 0;

    for ( ;; ) {
        prop = njs_lvlhsh_each(hash, &lhe);

        if (prop == NULL) {
            break;
        }

        lhq.key_hash = lhe.key_hash;
        njs_string_get(&prop->name, &lhq.key);

        ext_prop = njs_object_exist_in_proto(parent, object, &lhq);

        if (ext_prop == NULL && prop->type != NJS_WHITEOUT
            && (prop->enumerable || all))
        {
            length++;
        }
    }

    njs_lvlhsh_each_init(&lhe, &njs_object_hash_proto);
    hash = &object->shared_hash;

    for ( ;; ) {
        prop = njs_lvlhsh_each(hash, &lhe);

        if (prop == NULL) {
            break;
        }

        lhq.key_hash = lhe.key_hash;
        njs_string_get(&prop->name, &lhq.key);

        lhq.proto = &njs_object_hash_proto;
        ret = njs_lvlhsh_find(&object->hash, &lhq);

        if (ret != NJS_OK) {
            ext_prop = njs_object_exist_in_proto(parent, object, &lhq);

            if (ext_prop == NULL && (prop->enumerable || all)) {
                length++;
            }
        }
    }

    return length;
}


static njs_int_t
njs_object_enumerate_array(njs_vm_t *vm, const njs_array_t *array,
    njs_array_t *items, njs_object_enum_t kind)
{
    uint32_t     i;
    njs_value_t  *item;
    njs_array_t  *entry;

    item = items->start;

    switch (kind) {
    case NJS_ENUM_KEYS:
        for (i = 0; i < array->length; i++) {
            if (njs_is_valid(&array->start[i])) {
                njs_uint32_to_string(item++, i);
            }
        }

        break;

    case NJS_ENUM_VALUES:
        for (i = 0; i < array->length; i++) {
            if (njs_is_valid(&array->start[i])) {
                /* GC: retain. */
                *item++ = array->start[i];
            }
        }

        break;

    case NJS_ENUM_BOTH:
        for (i = 0; i < array->length; i++) {
            if (njs_is_valid(&array->start[i])) {

                entry = njs_array_alloc(vm, 2, 0);
                if (njs_slow_path(entry == NULL)) {
                    return NJS_ERROR;
                }

                njs_uint32_to_string(&entry->start[0], i);

                /* GC: retain. */
                entry->start[1] = array->start[i];

                njs_set_array(item, entry);

                item++;
            }
        }

        break;
    }

    items->start = item;

    return NJS_OK;
}


static njs_int_t
njs_object_enumerate_string(njs_vm_t *vm, const njs_value_t *value,
    njs_array_t *items, njs_object_enum_t kind)
{
    u_char             *begin;
    uint32_t           i, len, size;
    njs_value_t        *item, *string;
    njs_array_t        *entry;
    const u_char       *src, *end;
    njs_string_prop_t  str_prop;

    item = items->start;
    len = (uint32_t) njs_string_prop(&str_prop, value);

    switch (kind) {
    case NJS_ENUM_KEYS:
        for (i = 0; i < len; i++) {
            njs_uint32_to_string(item++, i);
        }

        break;

    case NJS_ENUM_VALUES:
        if (str_prop.size == (size_t) len) {
            /* Byte or ASCII string. */

            for (i = 0; i < len; i++) {
                begin = njs_string_short_start(item);
                *begin = str_prop.start[i];

                njs_string_short_set(item, 1, 1);

                item++;
            }

        } else {
            /* UTF-8 string. */

            src = str_prop.start;
            end = src + str_prop.size;

            do {
                begin = (u_char *) src;
                njs_utf8_copy(njs_string_short_start(item), &src, end);
                size = (uint32_t) (src - begin);

                njs_string_short_set(item, size, 1);

                item++;

            } while (src != end);
        }

        break;

    case NJS_ENUM_BOTH:
        if (str_prop.size == (size_t) len) {
            /* Byte or ASCII string. */

            for (i = 0; i < len; i++) {

                entry = njs_array_alloc(vm, 2, 0);
                if (njs_slow_path(entry == NULL)) {
                    return NJS_ERROR;
                }

                njs_uint32_to_string(&entry->start[0], i);

                string = &entry->start[1];

                begin = njs_string_short_start(string);
                *begin = str_prop.start[i];

                njs_string_short_set(string, 1, 1);

                njs_set_array(item, entry);

                item++;
            }

        } else {
            /* UTF-8 string. */

            src = str_prop.start;
            end = src + str_prop.size;
            i = 0;

            do {
                entry = njs_array_alloc(vm, 2, 0);
                if (njs_slow_path(entry == NULL)) {
                    return NJS_ERROR;
                }

                njs_uint32_to_string(&entry->start[0], i++);

                string = &entry->start[1];

                begin = (u_char *) src;
                njs_utf8_copy(njs_string_short_start(string), &src, end);
                size = (uint32_t) (src - begin);

                njs_string_short_set(string, size, 1);

                njs_set_array(item, entry);

                item++;

            } while (src != end);
        }

        break;
    }

    items->start = item;

    return NJS_OK;
}


static njs_int_t
njs_object_enumerate_object(njs_vm_t *vm, const njs_object_t *object,
    njs_array_t *items, njs_object_enum_t kind, njs_bool_t all)
{
    njs_int_t           ret;
    const njs_object_t  *proto;

    ret = njs_object_own_enumerate_object(vm, object, object, items, kind, all);
    if (njs_slow_path(ret != NJS_OK)) {
        return NJS_ERROR;
    }

    proto = object->__proto__;

    while (proto != NULL) {
        ret = njs_object_own_enumerate_value(vm, proto, object, items, kind,
                                             all);
        if (njs_slow_path(ret != NJS_OK)) {
            return NJS_ERROR;
        }

        proto = proto->__proto__;
    }

    return NJS_OK;
}


static njs_int_t
njs_object_own_enumerate_object(njs_vm_t *vm, const njs_object_t *object,
    const njs_object_t *parent, njs_array_t *items, njs_object_enum_t kind,
    njs_bool_t all)
{
    njs_int_t           ret;
    njs_value_t         *item;
    njs_array_t         *entry;
    njs_lvlhsh_each_t   lhe;
    njs_object_prop_t   *prop, *ext_prop;
    njs_lvlhsh_query_t  lhq;
    const njs_lvlhsh_t  *hash;

    njs_lvlhsh_each_init(&lhe, &njs_object_hash_proto);

    item = items->start;
    hash = &object->hash;

    switch (kind) {
    case NJS_ENUM_KEYS:
        for ( ;; ) {
            prop = njs_lvlhsh_each(hash, &lhe);

            if (prop == NULL) {
                break;
            }

            lhq.key_hash = lhe.key_hash;
            njs_string_get(&prop->name, &lhq.key);

            ext_prop = njs_object_exist_in_proto(parent, object, &lhq);

            if (ext_prop == NULL && prop->type != NJS_WHITEOUT
                && (prop->enumerable || all))
            {
                njs_string_copy(item++, &prop->name);
            }
        }

        njs_lvlhsh_each_init(&lhe, &njs_object_hash_proto);
        hash = &object->shared_hash;

        for ( ;; ) {
            prop = njs_lvlhsh_each(hash, &lhe);

            if (prop == NULL) {
                break;
            }

            lhq.key_hash = lhe.key_hash;
            njs_string_get(&prop->name, &lhq.key);

            lhq.proto = &njs_object_hash_proto;
            ret = njs_lvlhsh_find(&object->hash, &lhq);

            if (ret != NJS_OK) {
                ext_prop = njs_object_exist_in_proto(parent, object, &lhq);

                if (ext_prop == NULL && (prop->enumerable || all)) {
                    njs_string_copy(item++, &prop->name);
                }
            }
        }

        break;

    case NJS_ENUM_VALUES:
        for ( ;; ) {
            prop = njs_lvlhsh_each(hash, &lhe);

            if (prop == NULL) {
                break;
            }

            lhq.key_hash = lhe.key_hash;
            njs_string_get(&prop->name, &lhq.key);

            ext_prop = njs_object_exist_in_proto(parent, object, &lhq);

            if (ext_prop == NULL && prop->type != NJS_WHITEOUT
                && (prop->enumerable || all))
            {
                /* GC: retain. */
                *item++ = prop->value;
            }
        }

        njs_lvlhsh_each_init(&lhe, &njs_object_hash_proto);
        hash = &object->shared_hash;

        for ( ;; ) {
            prop = njs_lvlhsh_each(hash, &lhe);

            if (prop == NULL) {
                break;
            }

            lhq.key_hash = lhe.key_hash;
            njs_string_get(&prop->name, &lhq.key);

            lhq.proto = &njs_object_hash_proto;
            ret = njs_lvlhsh_find(&object->hash, &lhq);

            if (ret != NJS_OK) {
                ext_prop = njs_object_exist_in_proto(parent, object, &lhq);

                if (ext_prop == NULL && (prop->enumerable || all)) {
                    *item++ = prop->value;
                }
            }
        }

        break;

    case NJS_ENUM_BOTH:
        for ( ;; ) {
            prop = njs_lvlhsh_each(hash, &lhe);

            if (prop == NULL) {
                break;
            }

            lhq.key_hash = lhe.key_hash;
            njs_string_get(&prop->name, &lhq.key);

            ext_prop = njs_object_exist_in_proto(parent, object, &lhq);

            if (ext_prop == NULL && prop->type != NJS_WHITEOUT
                && (prop->enumerable || all))
            {
                entry = njs_array_alloc(vm, 2, 0);
                if (njs_slow_path(entry == NULL)) {
                    return NJS_ERROR;
                }

                njs_string_copy(&entry->start[0], &prop->name);

                /* GC: retain. */
                entry->start[1] = prop->value;

                njs_set_array(item, entry);

                item++;
            }
        }

        njs_lvlhsh_each_init(&lhe, &njs_object_hash_proto);
        hash = &object->shared_hash;

        for ( ;; ) {
            prop = njs_lvlhsh_each(hash, &lhe);

            if (prop == NULL) {
                break;
            }

            lhq.key_hash = lhe.key_hash;
            njs_string_get(&prop->name, &lhq.key);

            lhq.proto = &njs_object_hash_proto;
            ret = njs_lvlhsh_find(&object->hash, &lhq);

            if (ret != NJS_OK && (prop->enumerable || all)) {
                ext_prop = njs_object_exist_in_proto(parent, object, &lhq);

                if (ext_prop == NULL) {
                    entry = njs_array_alloc(vm, 2, 0);
                    if (njs_slow_path(entry == NULL)) {
                        return NJS_ERROR;
                    }

                    njs_string_copy(&entry->start[0], &prop->name);

                    /* GC: retain. */
                    entry->start[1] = prop->value;

                    njs_set_array(item, entry);

                    item++;
                }
            }
        }

        break;
    }

    items->start = item;

    return NJS_OK;
}


static njs_int_t
njs_object_define_property(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t unused)
{
    njs_int_t          ret;
    njs_value_t        *value;
    const njs_value_t  *name, *desc;

    if (!njs_is_object(njs_arg(args, nargs, 1))) {
        njs_type_error(vm, "cannot convert %s argument to object",
                       njs_type_string(njs_arg(args, nargs, 1)->type));
        return NJS_ERROR;
    }

    value = njs_argument(args, 1);

    if (!njs_object(value)->extensible) {
        njs_type_error(vm, "object is not extensible");
        return NJS_ERROR;
    }

    desc = njs_arg(args, nargs, 3);

    if (!njs_is_object(desc)) {
        njs_type_error(vm, "descriptor is not an object");
        return NJS_ERROR;
    }

    name = njs_arg(args, nargs, 2);

    ret = njs_object_prop_define(vm, value, name, desc);
    if (njs_slow_path(ret != NJS_OK)) {
        return NJS_ERROR;
    }

    vm->retval = *value;

    return NJS_OK;
}


static njs_int_t
njs_object_define_properties(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t unused)
{
    njs_int_t          ret;
    njs_value_t        *value;
    njs_lvlhsh_t       *hash;
    njs_lvlhsh_each_t  lhe;
    njs_object_prop_t  *prop;
    const njs_value_t  *desc;

    if (!njs_is_object(njs_arg(args, nargs, 1))) {
        njs_type_error(vm, "cannot convert %s argument to object",
                       njs_type_string(njs_arg(args, nargs, 1)->type));
        return NJS_ERROR;
    }

    value = njs_argument(args, 1);

    if (!njs_object(value)->extensible) {
        njs_type_error(vm, "object is not extensible");
        return NJS_ERROR;
    }

    desc = njs_arg(args, nargs, 2);

    if (!njs_is_object(desc)) {
        njs_type_error(vm, "descriptor is not an object");
        return NJS_ERROR;
    }

    njs_lvlhsh_each_init(&lhe, &njs_object_hash_proto);

    hash = njs_object_hash(desc);

    for ( ;; ) {
        prop = njs_lvlhsh_each(hash, &lhe);

        if (prop == NULL) {
            break;
        }

        if (prop->enumerable && njs_is_object(&prop->value)) {
            ret = njs_object_prop_define(vm, value, &prop->name, &prop->value);

            if (njs_slow_path(ret != NJS_OK)) {
                return NJS_ERROR;
            }
        }
    }

    vm->retval = *value;

    return NJS_OK;
}


static njs_int_t
njs_object_get_own_property_descriptor(njs_vm_t *vm, njs_value_t *args,
    njs_uint_t nargs, njs_index_t unused)
{
    const njs_value_t  *value, *property;

    value = njs_arg(args, nargs, 1);

    if (njs_is_null_or_undefined(value)) {
        njs_type_error(vm, "cannot convert %s argument to object",
                       njs_type_string(value->type));
        return NJS_ERROR;
    }

    property = njs_arg(args, nargs, 2);

    return njs_object_prop_descriptor(vm, &vm->retval, value, property);
}


static njs_int_t
njs_object_get_own_property_descriptors(njs_vm_t *vm, njs_value_t *args,
    njs_uint_t nargs, njs_index_t unused)
{
    njs_int_t           ret;
    uint32_t            i, length;
    njs_array_t         *names;
    njs_value_t         descriptor;
    njs_object_t        *descriptors;
    const njs_value_t   *value, *key;
    njs_object_prop_t   *pr;
    njs_lvlhsh_query_t  lhq;

    value = njs_arg(args, nargs, 1);

    if (njs_is_null_or_undefined(value)) {
        njs_type_error(vm, "cannot convert %s argument to object",
                       njs_type_string(value->type));

        return NJS_ERROR;
    }

    names = njs_value_own_enumerate(vm, value, NJS_ENUM_KEYS, 1);
    if (njs_slow_path(names == NULL)) {
        return NJS_ERROR;
    }

    length = names->length;

    descriptors = njs_object_alloc(vm);
    if (njs_slow_path(descriptors == NULL)) {
        return NJS_ERROR;
    }

    lhq.replace = 0;
    lhq.pool = vm->mem_pool;
    lhq.proto = &njs_object_hash_proto;

    for (i = 0; i < length; i++) {
        key = &names->start[i];
        ret = njs_object_prop_descriptor(vm, &descriptor, value, key);
        if (njs_slow_path(ret != NJS_OK)) {
            return ret;
        }

        pr = njs_object_prop_alloc(vm, key, &descriptor, 1);
        if (njs_slow_path(pr == NULL)) {
            return NJS_ERROR;
        }

        njs_string_get(key, &lhq.key);
        lhq.key_hash = njs_djb_hash(lhq.key.start, lhq.key.length);
        lhq.value = pr;

        ret = njs_lvlhsh_insert(&descriptors->hash, &lhq);
        if (njs_slow_path(ret != NJS_OK)) {
            njs_internal_error(vm, "lvlhsh insert failed");
            return NJS_ERROR;
        }
    }

    njs_set_object(&vm->retval, descriptors);

    return NJS_OK;
}


static njs_int_t
njs_object_get_own_property_names(njs_vm_t *vm, njs_value_t *args,
    njs_uint_t nargs, njs_index_t unused)
{
    njs_array_t        *names;
    const njs_value_t  *value;

    value = njs_arg(args, nargs, 1);

    if (njs_is_null_or_undefined(value)) {
        njs_type_error(vm, "cannot convert %s argument to object",
                       njs_type_string(value->type));

        return NJS_ERROR;
    }

    names = njs_value_own_enumerate(vm, value, NJS_ENUM_KEYS, 1);
    if (names == NULL) {
        return NJS_ERROR;
    }

    njs_set_array(&vm->retval, names);

    return NJS_OK;
}


static njs_int_t
njs_object_get_prototype_of(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t unused)
{
    const njs_value_t  *value;

    value = njs_arg(args, nargs, 1);

    if (njs_is_object(value)) {
        njs_object_prototype_proto(vm, (njs_value_t *) value, NULL,
                                   &vm->retval);
        return NJS_OK;
    }

    njs_type_error(vm, "cannot convert %s argument to object",
                   njs_type_string(value->type));

    return NJS_ERROR;
}


static njs_int_t
njs_object_freeze(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t unused)
{
    njs_lvlhsh_t       *hash;
    njs_object_t       *object;
    njs_object_prop_t  *prop;
    njs_lvlhsh_each_t  lhe;
    const njs_value_t  *value;

    value = njs_arg(args, nargs, 1);

    if (!njs_is_object(value)) {
        vm->retval = njs_value_undefined;
        return NJS_OK;
    }

    object = njs_object(value);
    object->extensible = 0;

    njs_lvlhsh_each_init(&lhe, &njs_object_hash_proto);

    hash = &object->hash;

    for ( ;; ) {
        prop = njs_lvlhsh_each(hash, &lhe);

        if (prop == NULL) {
            break;
        }

        prop->writable = 0;
        prop->configurable = 0;
    }

    vm->retval = *value;

    return NJS_OK;
}


static njs_int_t
njs_object_is_frozen(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t unused)
{
    njs_lvlhsh_t       *hash;
    njs_object_t       *object;
    njs_object_prop_t  *prop;
    njs_lvlhsh_each_t  lhe;
    const njs_value_t  *value, *retval;

    value = njs_arg(args, nargs, 1);

    if (!njs_is_object(value)) {
        vm->retval = njs_value_true;
        return NJS_OK;
    }

    retval = &njs_value_false;

    object = njs_object(value);
    njs_lvlhsh_each_init(&lhe, &njs_object_hash_proto);

    hash = &object->hash;

    if (object->extensible) {
        goto done;
    }

    for ( ;; ) {
        prop = njs_lvlhsh_each(hash, &lhe);

        if (prop == NULL) {
            break;
        }

        if (prop->configurable) {
            goto done;
        }

        if (njs_is_data_descriptor(prop) && prop->writable) {
            goto done;
        }
    }

    retval = &njs_value_true;

done:

    vm->retval = *retval;

    return NJS_OK;
}


static njs_int_t
njs_object_seal(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t unused)
{
    njs_lvlhsh_t       *hash;
    njs_object_t       *object;
    const njs_value_t  *value;
    njs_object_prop_t  *prop;
    njs_lvlhsh_each_t  lhe;

    value = njs_arg(args, nargs, 1);

    if (!njs_is_object(value)) {
        vm->retval = *value;
        return NJS_OK;
    }

    object = njs_object(value);
    object->extensible = 0;

    njs_lvlhsh_each_init(&lhe, &njs_object_hash_proto);

    hash = &object->hash;

    for ( ;; ) {
        prop = njs_lvlhsh_each(hash, &lhe);

        if (prop == NULL) {
            break;
        }

        prop->configurable = 0;
    }

    vm->retval = *value;

    return NJS_OK;
}


static njs_int_t
njs_object_is_sealed(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t unused)
{
    njs_lvlhsh_t       *hash;
    njs_object_t       *object;
    njs_object_prop_t  *prop;
    njs_lvlhsh_each_t  lhe;
    const njs_value_t  *value, *retval;

    value = njs_arg(args, nargs, 1);

    if (!njs_is_object(value)) {
        vm->retval = njs_value_true;
        return NJS_OK;
    }

    retval = &njs_value_false;

    object = njs_object(value);
    njs_lvlhsh_each_init(&lhe, &njs_object_hash_proto);

    hash = &object->hash;

    if (object->extensible) {
        goto done;
    }

    for ( ;; ) {
        prop = njs_lvlhsh_each(hash, &lhe);

        if (prop == NULL) {
            break;
        }

        if (prop->configurable) {
            goto done;
        }
    }

    retval = &njs_value_true;

done:

    vm->retval = *retval;

    return NJS_OK;
}


static njs_int_t
njs_object_prevent_extensions(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t unused)
{
    const njs_value_t  *value;

    value = njs_arg(args, nargs, 1);

    if (!njs_is_object(value)) {
        vm->retval = *value;
        return NJS_OK;
    }

    njs_object(&args[1])->extensible = 0;

    vm->retval = *value;

    return NJS_OK;
}


static njs_int_t
njs_object_is_extensible(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t unused)
{
    const njs_value_t  *value, *retval;

    value = njs_arg(args, nargs, 1);

    if (!njs_is_object(value)) {
        vm->retval = njs_value_false;
        return NJS_OK;
    }

    retval = njs_object(value)->extensible ? &njs_value_true
                                           : &njs_value_false;

    vm->retval = *retval;

    return NJS_OK;
}


/*
 * The __proto__ property of booleans, numbers and strings primitives,
 * of objects created by Boolean(), Number(), and String() constructors,
 * and of Boolean.prototype, Number.prototype, and String.prototype objects.
 */

njs_int_t
njs_primitive_prototype_get_proto(njs_vm_t *vm, njs_value_t *value,
    njs_value_t *setval, njs_value_t *retval)
{
    njs_uint_t    index;
    njs_object_t  *proto;

    /*
     * The __proto__ getters reside in object prototypes of primitive types
     * and have to return different results for primitive type and for objects.
     */
    if (njs_is_object(value)) {
        proto = njs_object(value)->__proto__;

    } else {
        index = njs_primitive_prototype_index(value->type);
        proto = &vm->prototypes[index].object;
    }

    njs_set_type_object(retval, proto, proto->type);

    return NJS_OK;
}


/*
 * The "prototype" property of Object(), Array() and other functions is
 * created on demand in the functions' private hash by the "prototype"
 * getter.  The properties are set to appropriate prototype.
 */

njs_int_t
njs_object_prototype_create(njs_vm_t *vm, njs_value_t *value,
    njs_value_t *setval, njs_value_t *retval)
{
    int32_t            index;
    njs_function_t     *function;
    const njs_value_t  *proto;

    proto = NULL;
    function = njs_function(value);
    index = function - vm->constructors;

    if (index >= 0 && index < NJS_PROTOTYPE_MAX) {
        proto = njs_property_prototype_create(vm, &function->object.hash,
                                              &vm->prototypes[index].object);
    }

    if (proto == NULL) {
        proto = &njs_value_undefined;
    }

    *retval = *proto;

    return NJS_OK;
}


njs_value_t *
njs_property_prototype_create(njs_vm_t *vm, njs_lvlhsh_t *hash,
    njs_object_t *prototype)
{
    njs_int_t                 ret;
    njs_object_prop_t         *prop;
    njs_lvlhsh_query_t        lhq;

    static const njs_value_t  proto_string = njs_string("prototype");

    prop = njs_object_prop_alloc(vm, &proto_string, &njs_value_undefined, 0);
    if (njs_slow_path(prop == NULL)) {
        return NULL;
    }

    /* GC */

    njs_set_type_object(&prop->value, prototype, prototype->type);

    lhq.value = prop;
    lhq.key_hash = NJS_PROTOTYPE_HASH;
    lhq.key = njs_str_value("prototype");
    lhq.replace = 0;
    lhq.pool = vm->mem_pool;
    lhq.proto = &njs_object_hash_proto;

    ret = njs_lvlhsh_insert(hash, &lhq);

    if (njs_fast_path(ret == NJS_OK)) {
        return &prop->value;
    }

    njs_internal_error(vm, "lvlhsh insert failed");

    return NULL;
}


static const njs_object_prop_t  njs_object_constructor_properties[] =
{
    /* Object.name == "Object". */
    {
        .type = NJS_PROPERTY,
        .name = njs_string("name"),
        .value = njs_string("Object"),
        .configurable = 1,
    },

    /* Object.length == 1. */
    {
        .type = NJS_PROPERTY,
        .name = njs_string("length"),
        .value = njs_value(NJS_NUMBER, 1, 1.0),
        .configurable = 1,
    },

    /* Object.prototype. */
    {
        .type = NJS_PROPERTY_HANDLER,
        .name = njs_string("prototype"),
        .value = njs_prop_handler(njs_object_prototype_create),
    },

    /* Object.create(). */
    {
        .type = NJS_METHOD,
        .name = njs_string("create"),
        .value = njs_native_function(njs_object_create, 0),
        .writable = 1,
        .configurable = 1,
    },

    /* Object.keys(). */
    {
        .type = NJS_METHOD,
        .name = njs_string("keys"),
        .value = njs_native_function(njs_object_keys,
                                     NJS_SKIP_ARG, NJS_OBJECT_ARG),
        .writable = 1,
        .configurable = 1,
    },

    /* ES8: Object.values(). */
    {
        .type = NJS_METHOD,
        .name = njs_string("values"),
        .value = njs_native_function(njs_object_values,
                                     NJS_SKIP_ARG, NJS_OBJECT_ARG),
        .writable = 1,
        .configurable = 1,
    },

    /* ES8: Object.entries(). */
    {
        .type = NJS_METHOD,
        .name = njs_string("entries"),
        .value = njs_native_function(njs_object_entries,
                                     NJS_SKIP_ARG, NJS_OBJECT_ARG),
        .writable = 1,
        .configurable = 1,
    },

    /* Object.defineProperty(). */
    {
        .type = NJS_METHOD,
        .name = njs_string("defineProperty"),
        .value = njs_native_function(njs_object_define_property,
                                     NJS_SKIP_ARG, NJS_OBJECT_ARG,
                                     NJS_STRING_ARG, NJS_OBJECT_ARG),
        .writable = 1,
        .configurable = 1,
    },

    /* Object.defineProperties(). */
    {
        .type = NJS_METHOD,
        .name = njs_long_string("defineProperties"),
        .value = njs_native_function(njs_object_define_properties,
                                     NJS_SKIP_ARG, NJS_OBJECT_ARG,
                                     NJS_OBJECT_ARG),
        .writable = 1,
        .configurable = 1,
    },

    /* Object.getOwnPropertyDescriptor(). */
    {
        .type = NJS_METHOD,
        .name = njs_long_string("getOwnPropertyDescriptor"),
        .value = njs_native_function(njs_object_get_own_property_descriptor,
                                     NJS_SKIP_ARG, NJS_SKIP_ARG,
                                     NJS_STRING_ARG),
        .writable = 1,
        .configurable = 1,
    },

    /* Object.getOwnPropertyDescriptors(). */
    {
        .type = NJS_METHOD,
        .name = njs_long_string("getOwnPropertyDescriptors"),
        .value = njs_native_function(njs_object_get_own_property_descriptors,
                                     NJS_SKIP_ARG, NJS_OBJECT_ARG),
        .writable = 1,
        .configurable = 1,
    },

    /* Object.getOwnPropertyNames(). */
    {
        .type = NJS_METHOD,
        .name = njs_long_string("getOwnPropertyNames"),
        .value = njs_native_function(njs_object_get_own_property_names,
                                     NJS_SKIP_ARG, NJS_OBJECT_ARG),
        .writable = 1,
        .configurable = 1,
    },

    /* Object.getPrototypeOf(). */
    {
        .type = NJS_METHOD,
        .name = njs_string("getPrototypeOf"),
        .value = njs_native_function(njs_object_get_prototype_of,
                                     NJS_SKIP_ARG, NJS_OBJECT_ARG),
        .writable = 1,
        .configurable = 1,
    },

    /* Object.freeze(). */
    {
        .type = NJS_METHOD,
        .name = njs_string("freeze"),
        .value = njs_native_function(njs_object_freeze,
                                     NJS_SKIP_ARG, NJS_OBJECT_ARG),
        .writable = 1,
        .configurable = 1,
    },

    /* Object.isFrozen(). */
    {
        .type = NJS_METHOD,
        .name = njs_string("isFrozen"),
        .value = njs_native_function(njs_object_is_frozen,
                                     NJS_SKIP_ARG, NJS_OBJECT_ARG),
        .writable = 1,
        .configurable = 1,
    },

    /* Object.seal(). */
    {
        .type = NJS_METHOD,
        .name = njs_string("seal"),
        .value = njs_native_function(njs_object_seal,
                                     NJS_SKIP_ARG, NJS_OBJECT_ARG),
        .writable = 1,
        .configurable = 1,
    },

    /* Object.isSealed(). */
    {
        .type = NJS_METHOD,
        .name = njs_string("isSealed"),
        .value = njs_native_function(njs_object_is_sealed,
                                     NJS_SKIP_ARG, NJS_OBJECT_ARG),
        .writable = 1,
        .configurable = 1,
    },

    /* Object.preventExtensions(). */
    {
        .type = NJS_METHOD,
        .name = njs_long_string("preventExtensions"),
        .value = njs_native_function(njs_object_prevent_extensions,
                                     NJS_SKIP_ARG, NJS_OBJECT_ARG),
        .writable = 1,
        .configurable = 1,
    },

    /* Object.isExtensible(). */
    {
        .type = NJS_METHOD,
        .name = njs_string("isExtensible"),
        .value = njs_native_function(njs_object_is_extensible,
                                     NJS_SKIP_ARG, NJS_OBJECT_ARG),
        .writable = 1,
        .configurable = 1,
    },
};


const njs_object_init_t  njs_object_constructor_init = {
    njs_str("Object"),
    njs_object_constructor_properties,
    njs_nitems(njs_object_constructor_properties),
};


/*
 * ES6, 9.1.2: [[SetPrototypeOf]].
 */
static njs_bool_t
njs_object_set_prototype_of(njs_vm_t *vm, njs_object_t *object,
    const njs_value_t *value)
{
    const njs_object_t *proto;

    proto = njs_is_object(value) ? njs_object(value)->__proto__
                                 : NULL;

    if (njs_slow_path(object->__proto__ == proto)) {
        return 1;
    }

    if (njs_slow_path(proto == NULL)) {
        object->__proto__ = NULL;
        return 1;
    }

    do {
        if (proto == object) {
            return 0;
        }

        proto = proto->__proto__;

    } while (proto != NULL);

    object->__proto__ = njs_object(value);

    return 1;
}


njs_int_t
njs_object_prototype_proto(njs_vm_t *vm, njs_value_t *value,
    njs_value_t *setval, njs_value_t *retval)
{
    njs_bool_t    ret;
    njs_object_t  *proto, *object;

    if (!njs_is_object(value)) {
        *retval = *value;
        return NJS_OK;
    }

    object = njs_object(value);

    if (setval != NULL) {
        if (njs_is_object(setval) || njs_is_null(setval)) {
            ret = njs_object_set_prototype_of(vm, object, setval);
            if (njs_slow_path(!ret)) {
                njs_type_error(vm, "Cyclic __proto__ value");
                return NJS_ERROR;
            }
        }

        *retval = njs_value_undefined;

        return NJS_OK;
    }

    proto = object->__proto__;

    if (njs_fast_path(proto != NULL)) {
        njs_set_type_object(retval, proto, proto->type);

    } else {
        *retval = njs_value_null;
    }

    return NJS_OK;
}


/*
 * The "constructor" property of Object(), Array() and other functions
 * prototypes is created on demand in the prototypes' private hash by the
 * "constructor" getter.  The properties are set to appropriate function.
 */

njs_int_t
njs_object_prototype_create_constructor(njs_vm_t *vm, njs_value_t *value,
    njs_value_t *setval, njs_value_t *retval)
{
    int32_t                 index;
    njs_value_t             *cons;
    njs_object_t            *object;
    njs_object_prototype_t  *prototype;

    if (njs_is_object(value)) {
        object = njs_object(value);

        do {
            prototype = (njs_object_prototype_t *) object;
            index = prototype - vm->prototypes;

            if (index >= 0 && index < NJS_PROTOTYPE_MAX) {
                goto found;
            }

            object = object->__proto__;

        } while (object != NULL);

        njs_thread_log_alert("prototype not found");

        return NJS_ERROR;

    } else {
        index = njs_primitive_prototype_index(value->type);
        prototype = &vm->prototypes[index];
    }

found:

    if (setval == NULL) {
        setval = &vm->scopes[NJS_SCOPE_GLOBAL][index];
    }

    cons = njs_property_constructor_create(vm, &prototype->object.hash, setval);
    if (njs_fast_path(cons != NULL)) {
        *retval = *cons;
        return NJS_OK;
    }

    return NJS_ERROR;
}


njs_value_t *
njs_property_constructor_create(njs_vm_t *vm, njs_lvlhsh_t *hash,
    njs_value_t *constructor)
{
    njs_int_t                 ret;
    njs_object_prop_t         *prop;
    njs_lvlhsh_query_t        lhq;

    static const njs_value_t  constructor_string = njs_string("constructor");

    prop = njs_object_prop_alloc(vm, &constructor_string, constructor, 1);
    if (njs_slow_path(prop == NULL)) {
        return NULL;
    }

    /* GC */

    prop->value = *constructor;
    prop->enumerable = 0;

    lhq.value = prop;
    lhq.key_hash = NJS_CONSTRUCTOR_HASH;
    lhq.key = njs_str_value("constructor");
    lhq.replace = 1;
    lhq.pool = vm->mem_pool;
    lhq.proto = &njs_object_hash_proto;

    ret = njs_lvlhsh_insert(hash, &lhq);

    if (njs_fast_path(ret == NJS_OK)) {
        return &prop->value;
    }

    njs_internal_error(vm, "lvlhsh insert/replace failed");

    return NULL;
}


static njs_int_t
njs_object_prototype_value_of(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t unused)
{
    vm->retval = args[0];

    return NJS_OK;
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
static const njs_value_t  njs_object_data_string =
                                     njs_string("[object Data]");
static const njs_value_t  njs_object_exernal_string =
                                     njs_long_string("[object External]");
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


njs_int_t
njs_object_prototype_to_string(njs_vm_t *vm, njs_value_t *args,
    njs_uint_t nargs, njs_index_t unused)
{
    const njs_value_t  *name;

    static const njs_value_t  *class_name[NJS_TYPE_MAX] = {
        /* Primitives. */
        &njs_object_null_string,
        &njs_object_undefined_string,
        &njs_object_boolean_string,
        &njs_object_number_string,
        &njs_object_string_string,

        &njs_object_data_string,
        &njs_object_exernal_string,
        NULL,
        NULL,
        NULL,
        NULL,
        NULL,
        NULL,
        NULL,
        NULL,
        NULL,

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
        &njs_object_error_string,
        &njs_object_error_string,
        &njs_object_error_string,
        &njs_object_error_string,
        &njs_object_error_string,
        &njs_object_error_string,
        &njs_object_error_string,
        &njs_object_object_string,
    };

    name = class_name[args[0].type];

    if (njs_fast_path(name != NULL)) {
        vm->retval = *name;

        return NJS_OK;
    }

    njs_internal_error(vm, "Unknown value type");

    return NJS_ERROR;
}


static njs_int_t
njs_object_prototype_has_own_property(njs_vm_t *vm, njs_value_t *args,
    njs_uint_t nargs, njs_index_t unused)
{
    njs_int_t             ret;
    const njs_value_t     *value, *property;
    njs_property_query_t  pq;

    value = njs_arg(args, nargs, 0);

    if (njs_is_null_or_undefined(value)) {
        njs_type_error(vm, "cannot convert %s argument to object",
                       njs_type_string(value->type));
        return NJS_ERROR;
    }

    property = njs_arg(args, nargs, 1);

    njs_property_query_init(&pq, NJS_PROPERTY_QUERY_GET, 1);

    ret = njs_property_query(vm, &pq, (njs_value_t *) value, property);

    switch (ret) {
    case NJS_OK:
        vm->retval = njs_value_true;
        return NJS_OK;

    case NJS_DECLINED:
        vm->retval = njs_value_false;
        return NJS_OK;

    case NJS_ERROR:
    default:
        return ret;
    }
}


static njs_int_t
njs_object_prototype_prop_is_enumerable(njs_vm_t *vm, njs_value_t *args,
    njs_uint_t nargs, njs_index_t unused)
{
    njs_int_t             ret;
    const njs_value_t     *value, *property, *retval;
    njs_object_prop_t     *prop;
    njs_property_query_t  pq;

    value = njs_arg(args, nargs, 0);

    if (njs_is_null_or_undefined(value)) {
        njs_type_error(vm, "cannot convert %s argument to object",
                       njs_type_string(value->type));
        return NJS_ERROR;
    }

    property = njs_arg(args, nargs, 1);

    njs_property_query_init(&pq, NJS_PROPERTY_QUERY_GET, 1);

    ret = njs_property_query(vm, &pq, (njs_value_t *) value, property);

    switch (ret) {
    case NJS_OK:
        prop = pq.lhq.value;
        retval = prop->enumerable ? &njs_value_true : &njs_value_false;
        break;

    case NJS_DECLINED:
        retval = &njs_value_false;
        break;

    case NJS_ERROR:
    default:
        return ret;
    }

    vm->retval = *retval;

    return NJS_OK;
}


static njs_int_t
njs_object_prototype_is_prototype_of(njs_vm_t *vm, njs_value_t *args,
    njs_uint_t nargs, njs_index_t unused)
{
    njs_object_t       *object, *proto;
    const njs_value_t  *prototype, *value, *retval;

    retval = &njs_value_false;
    prototype = &args[0];
    value = njs_arg(args, nargs, 1);

    if (njs_is_object(prototype) && njs_is_object(value)) {
        proto = njs_object(prototype);
        object = njs_object(value);

        do {
            object = object->__proto__;

            if (object == proto) {
                retval = &njs_value_true;
                break;
            }

        } while (object != NULL);
    }

    vm->retval = *retval;

    return NJS_OK;
}


static const njs_object_prop_t  njs_object_prototype_properties[] =
{
    {
        .type = NJS_PROPERTY_HANDLER,
        .name = njs_string("__proto__"),
        .value = njs_prop_handler(njs_object_prototype_proto),
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
        .type = NJS_METHOD,
        .name = njs_string("valueOf"),
        .value = njs_native_function(njs_object_prototype_value_of, 0),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_METHOD,
        .name = njs_string("toString"),
        .value = njs_native_function(njs_object_prototype_to_string, 0),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_METHOD,
        .name = njs_string("hasOwnProperty"),
        .value = njs_native_function(njs_object_prototype_has_own_property,
                                     NJS_OBJECT_ARG, NJS_STRING_ARG),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_METHOD,
        .name = njs_long_string("propertyIsEnumerable"),
        .value = njs_native_function(njs_object_prototype_prop_is_enumerable,
                                     NJS_OBJECT_ARG, NJS_STRING_ARG),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_METHOD,
        .name = njs_string("isPrototypeOf"),
        .value = njs_native_function(njs_object_prototype_is_prototype_of,
                                     NJS_OBJECT_ARG, NJS_OBJECT_ARG),
        .writable = 1,
        .configurable = 1,
    },
};


const njs_object_init_t  njs_object_prototype_init = {
    njs_str("Object"),
    njs_object_prototype_properties,
    njs_nitems(njs_object_prototype_properties),
};
