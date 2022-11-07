
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) NGINX, Inc.
 */


#include <njs_main.h>


typedef enum {
    NJS_OBJECT_INTEGRITY_SEALED,
    NJS_OBJECT_INTEGRITY_FROZEN,
} njs_object_integrity_level_t;


static njs_int_t njs_object_hash_test(njs_lvlhsh_query_t *lhq, void *data);
static njs_object_prop_t *njs_object_exist_in_proto(const njs_object_t *begin,
    const njs_object_t *end, njs_lvlhsh_query_t *lhq);
static njs_int_t njs_object_enumerate_array(njs_vm_t *vm,
    const njs_array_t *array, njs_array_t *items, njs_object_enum_t kind);
static njs_int_t njs_object_enumerate_typed_array(njs_vm_t *vm,
    const njs_typed_array_t *array, njs_array_t *items, njs_object_enum_t kind);
static njs_int_t njs_object_enumerate_string(njs_vm_t *vm,
    const njs_value_t *value, njs_array_t *items, njs_object_enum_t kind);
static njs_int_t njs_object_enumerate_object(njs_vm_t *vm,
    const njs_object_t *object, njs_array_t *items, njs_object_enum_t kind,
    njs_object_enum_type_t type, njs_bool_t all);
static njs_int_t njs_object_own_enumerate_object(njs_vm_t *vm,
    const njs_object_t *object, const njs_object_t *parent, njs_array_t *items,
    njs_object_enum_t kind, njs_object_enum_type_t type, njs_bool_t all);
static njs_int_t njs_object_define_properties(njs_vm_t *vm, njs_value_t *args,
    njs_uint_t nargs, njs_index_t unused);
static njs_int_t njs_object_set_prototype(njs_vm_t *vm, njs_object_t *object,
    const njs_value_t *value);


njs_object_t *
njs_object_alloc(njs_vm_t *vm)
{
    njs_object_t  *object;

    object = njs_mp_alloc(vm->mem_pool, sizeof(njs_object_t));

    if (njs_fast_path(object != NULL)) {
        njs_lvlhsh_init(&object->hash);
        njs_lvlhsh_init(&object->shared_hash);
        object->__proto__ = &vm->prototypes[NJS_OBJ_TYPE_OBJECT].object;
        object->slots = NULL;
        object->type = NJS_OBJECT;
        object->shared = 0;
        object->extensible = 1;
        object->error_data = 0;
        object->fast_array = 0;

        return object;
    }

    njs_memory_error(vm);

    return NULL;
}


njs_object_t *
njs_object_value_copy(njs_vm_t *vm, njs_value_t *value)
{
    size_t        size;
    njs_object_t  *object;

    object = njs_object(value);

    if (!object->shared) {
        return object;
    }

    size = njs_is_object_value(value) ? sizeof(njs_object_value_t)
                                      : sizeof(njs_object_t);
    object = njs_mp_alloc(vm->mem_pool, size);

    if (njs_fast_path(object != NULL)) {
        memcpy(object, njs_object(value), size);
        object->__proto__ = &vm->prototypes[NJS_OBJ_TYPE_OBJECT].object;
        object->shared = 0;
        value->data.u.object = object;
        return object;
    }

    njs_memory_error(vm);

    return NULL;
}


njs_object_value_t *
njs_object_value_alloc(njs_vm_t *vm, njs_uint_t prototype_index, size_t extra,
    const njs_value_t *value)
{
    njs_object_value_t  *ov;

    ov = njs_mp_alloc(vm->mem_pool, sizeof(njs_object_value_t) + extra);
    if (njs_slow_path(ov == NULL)) {
        njs_memory_error(vm);
        return NULL;
    }

    njs_lvlhsh_init(&ov->object.hash);

    if (prototype_index == NJS_OBJ_TYPE_STRING) {
        ov->object.shared_hash = vm->shared->string_instance_hash;

    } else {
        njs_lvlhsh_init(&ov->object.shared_hash);
    }

    ov->object.type = NJS_OBJECT_VALUE;
    ov->object.shared = 0;
    ov->object.extensible = 1;
    ov->object.error_data = 0;
    ov->object.fast_array = 0;

    ov->object.__proto__ = &vm->prototypes[prototype_index].object;
    ov->object.slots = NULL;

    if (value != NULL) {
        ov->value = *value;
    }

    return ov;
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

        njs_object_property_key_set(&lhq, &prop->name, 0);

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
    njs_value_t        *name;
    njs_object_prop_t  *prop;

    prop = data;
    name = &prop->name;

    if (njs_slow_path(njs_is_symbol(name))) {
        return ((njs_symbol_key(name) == lhq->key_hash)
                && lhq->key.start == NULL) ? NJS_OK : NJS_DECLINED;
    }

    /* string. */

    size = name->short_string.size;

    if (size != NJS_STRING_LONG) {
        if (lhq->key.length != size) {
            return NJS_DECLINED;
        }

        start = name->short_string.start;

    } else {
        if (lhq->key.length != name->long_string.size) {
            return NJS_DECLINED;
        }

        start = name->long_string.data->start;
    }

    if (memcmp(start, lhq->key.start, lhq->key.length) == 0) {
        return NJS_OK;
    }

    return NJS_DECLINED;
}


static njs_int_t
njs_object_constructor(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t unused)
{
    njs_uint_t          type, index;
    njs_value_t         *value;
    njs_object_t        *object;
    njs_object_value_t  *obj_val;

    value = njs_arg(args, nargs, 1);
    type = value->type;

    if (njs_is_null_or_undefined(value)) {
        object = njs_object_alloc(vm);
        if (njs_slow_path(object == NULL)) {
            return NJS_ERROR;
        }

        njs_set_object(&vm->retval, object);

        return NJS_OK;
    }

    if (njs_is_primitive(value)) {
        index = njs_primitive_prototype_index(type);
        obj_val = njs_object_value_alloc(vm, index, 0, value);
        if (njs_slow_path(obj_val == NULL)) {
            return NJS_ERROR;
        }

        njs_set_object_value(&vm->retval, obj_val);

        return NJS_OK;
    }

    if (njs_slow_path(!njs_is_object(value))) {
        njs_type_error(vm, "unexpected constructor argument:%s",
                       njs_type_string(type));

        return NJS_ERROR;
    }

    njs_value_assign(&vm->retval, value);

    return NJS_OK;
}


static njs_int_t
njs_object_create(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t unused)
{
    njs_value_t   *value, *descs, arguments[3];
    njs_object_t  *object;

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

        descs = njs_arg(args, nargs, 2);

        if (njs_slow_path(!njs_is_undefined(descs))) {
            arguments[0] = args[0];
            arguments[1] = vm->retval;
            arguments[2] = *descs;

            return njs_object_define_properties(vm, arguments, 3, unused);
        }

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
    njs_value_t  *value;
    njs_array_t  *keys;

    value = njs_arg(args, nargs, 1);

    if (njs_is_null_or_undefined(value)) {
        njs_type_error(vm, "cannot convert %s argument to object",
                       njs_type_string(value->type));

        return NJS_ERROR;
    }

    keys = njs_value_own_enumerate(vm, value, NJS_ENUM_KEYS,
                                   NJS_ENUM_STRING, 0);
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
    njs_array_t  *array;
    njs_value_t  *value;

    value = njs_arg(args, nargs, 1);

    if (njs_is_null_or_undefined(value)) {
        njs_type_error(vm, "cannot convert %s argument to object",
                       njs_type_string(value->type));

        return NJS_ERROR;
    }

    array = njs_value_own_enumerate(vm, value, NJS_ENUM_VALUES,
                                    NJS_ENUM_STRING, 0);
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
    njs_array_t  *array;
    njs_value_t  *value;

    value = njs_arg(args, nargs, 1);

    if (njs_is_null_or_undefined(value)) {
        njs_type_error(vm, "cannot convert %s argument to object",
                       njs_type_string(value->type));

        return NJS_ERROR;
    }

    array = njs_value_own_enumerate(vm, value, NJS_ENUM_BOTH,
                                    NJS_ENUM_STRING, 0);
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


njs_inline njs_int_t
njs_object_enumerate_value(njs_vm_t *vm, const njs_object_t *object,
    njs_array_t *items, njs_object_enum_t kind, njs_object_enum_type_t type,
    njs_bool_t all)
{
    njs_int_t           ret;
    njs_object_value_t  *obj_val;

    if (type & NJS_ENUM_STRING) {
        switch (object->type) {
        case NJS_ARRAY:
            ret = njs_object_enumerate_array(vm, (njs_array_t *) object, items,
                                             kind);
            break;

        case NJS_TYPED_ARRAY:
            ret = njs_object_enumerate_typed_array(vm,
                                                  (njs_typed_array_t *) object,
                                                  items, kind);
            break;

        case NJS_OBJECT_VALUE:
            obj_val = (njs_object_value_t *) object;

            if (njs_is_string(&obj_val->value)) {
                ret = njs_object_enumerate_string(vm, &obj_val->value, items,
                                                  kind);
                break;
            }

        /* Fall through. */

        default:
            goto object;
        }

        if (njs_slow_path(ret != NJS_OK)) {
            return NJS_ERROR;
        }
    }

object:

    ret = njs_object_enumerate_object(vm, object, items, kind, type, all);
    if (njs_slow_path(ret != NJS_OK)) {
        return NJS_ERROR;
    }

    return NJS_OK;
}


njs_inline njs_int_t
njs_object_own_enumerate_value(njs_vm_t *vm, const njs_object_t *object,
    const njs_object_t *parent, njs_array_t *items, njs_object_enum_t kind,
    njs_object_enum_type_t type, njs_bool_t all)
{
    njs_int_t           ret;
    njs_object_value_t  *obj_val;

    if (type & NJS_ENUM_STRING) {
        switch (object->type) {
        case NJS_ARRAY:
            ret = njs_object_enumerate_array(vm, (njs_array_t *) object, items,
                                             kind);
            break;

        case NJS_TYPED_ARRAY:
            ret = njs_object_enumerate_typed_array(vm,
                                                   (njs_typed_array_t *) object,
                                                   items, kind);
            break;

        case NJS_OBJECT_VALUE:
            obj_val = (njs_object_value_t *) object;

            if (njs_is_string(&obj_val->value)) {
                ret = njs_object_enumerate_string(vm, &obj_val->value, items,
                                                  kind);
                break;
            }

            /* Fall through. */

        default:
            goto object;
        }

        if (njs_slow_path(ret != NJS_OK)) {
            return NJS_ERROR;
        }
    }

object:

    ret = njs_object_own_enumerate_object(vm, object, parent, items, kind,
                                          type, all);
    if (njs_slow_path(ret != NJS_OK)) {
        return NJS_ERROR;
    }

    return NJS_OK;
}


njs_array_t *
njs_object_enumerate(njs_vm_t *vm, const njs_object_t *object,
    njs_object_enum_t kind, njs_object_enum_type_t type, njs_bool_t all)
{
    njs_int_t    ret;
    njs_array_t  *items;

    items = njs_array_alloc(vm, 1, 0, NJS_ARRAY_SPARE);
    if (njs_slow_path(items == NULL)) {
        return NULL;
    }

    ret = njs_object_enumerate_value(vm, object, items, kind, type, all);
    if (njs_slow_path(ret != NJS_OK)) {
        return NULL;
    }

    return items;
}


njs_array_t *
njs_object_own_enumerate(njs_vm_t *vm, const njs_object_t *object,
    njs_object_enum_t kind, njs_object_enum_type_t type, njs_bool_t all)
{
    njs_int_t    ret;
    njs_array_t  *items;

    items = njs_array_alloc(vm, 1, 0, NJS_ARRAY_SPARE);
    if (njs_slow_path(items == NULL)) {
        return NULL;
    }

    ret = njs_object_own_enumerate_value(vm, object, object, items, kind, type,
                                         all);
    if (njs_slow_path(ret != NJS_OK)) {
        return NULL;
    }

    return items;
}


njs_inline njs_bool_t
njs_is_enumerable(const njs_value_t *value, njs_object_enum_type_t type)
{
    return (njs_is_string(value) && (type & NJS_ENUM_STRING))
           || (njs_is_symbol(value) && (type & NJS_ENUM_SYMBOL));
}


static njs_int_t
njs_object_enumerate_array(njs_vm_t *vm, const njs_array_t *array,
    njs_array_t *items, njs_object_enum_t kind)
{
    njs_int_t    ret;
    njs_value_t  *p, *start, *end;
    njs_array_t  *entry;

    if (!array->object.fast_array) {
        return NJS_OK;
    }

    start = array->start;

    p = start;
    end = p + array->length;

    switch (kind) {
    case NJS_ENUM_KEYS:
        while (p < end) {
            if (njs_is_valid(p)) {
                ret = njs_array_expand(vm, items, 0, 1);
                if (njs_slow_path(ret != NJS_OK)) {
                    return NJS_ERROR;
                }

                njs_uint32_to_string(&items->start[items->length++], p - start);
            }

            p++;
        }

        break;

    case NJS_ENUM_VALUES:
        while (p < end) {
            if (njs_is_valid(p)) {
                ret = njs_array_add(vm, items, p);
                if (njs_slow_path(ret != NJS_OK)) {
                    return NJS_ERROR;
                }
            }

            p++;
        }

        break;

    case NJS_ENUM_BOTH:
        while (p < end) {
            if (njs_is_valid(p)) {
                entry = njs_array_alloc(vm, 0, 2, 0);
                if (njs_slow_path(entry == NULL)) {
                    return NJS_ERROR;
                }

                njs_uint32_to_string(&entry->start[0], p - start);
                entry->start[1] = *p;

                ret = njs_array_expand(vm, items, 0, 1);
                if (njs_slow_path(ret != NJS_OK)) {
                    return NJS_ERROR;
                }

                njs_set_array(&items->start[items->length++], entry);
            }

            p++;
        }

        break;
    }

    return NJS_OK;
}


static njs_int_t
njs_object_enumerate_typed_array(njs_vm_t *vm, const njs_typed_array_t *array,
    njs_array_t *items, njs_object_enum_t kind)
{
    uint32_t     i, length;
    njs_int_t    ret;
    njs_value_t  *item;
    njs_array_t  *entry;

    length = njs_typed_array_length(array);

    ret = njs_array_expand(vm, items, 0, length);
    if (njs_slow_path(ret != NJS_OK)) {
        return NJS_ERROR;
    }

    item = &items->start[items->length];

    switch (kind) {
    case NJS_ENUM_KEYS:
        for (i = 0; i < length; i++) {
            njs_uint32_to_string(item++, i);
        }

        break;

    case NJS_ENUM_VALUES:
        for (i = 0; i < length; i++) {
            njs_set_number(item++, njs_typed_array_prop(array, i));
        }

        break;

    case NJS_ENUM_BOTH:
        for (i = 0; i < length; i++) {
            entry = njs_array_alloc(vm, 0, 2, 0);
            if (njs_slow_path(entry == NULL)) {
                return NJS_ERROR;
            }

            njs_uint32_to_string(&entry->start[0], i);
            njs_set_number(&entry->start[1], njs_typed_array_prop(array, i));

            njs_set_array(item++, entry);
        }

        break;
    }

    items->length += length;

    return NJS_OK;
}


static njs_int_t
njs_object_enumerate_string(njs_vm_t *vm, const njs_value_t *value,
    njs_array_t *items, njs_object_enum_t kind)
{
    u_char             *begin;
    uint32_t           i, len, size;
    njs_int_t          ret;
    njs_value_t        *item, *string;
    njs_array_t        *entry;
    const u_char       *src, *end;
    njs_string_prop_t  str_prop;

    len = (uint32_t) njs_string_prop(&str_prop, value);

    ret = njs_array_expand(vm, items, 0, len);
    if (njs_slow_path(ret != NJS_OK)) {
        return NJS_ERROR;
    }

    item = &items->start[items->length];

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

                entry = njs_array_alloc(vm, 0, 2, 0);
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
                entry = njs_array_alloc(vm, 0, 2, 0);
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

    items->length += len;

    return NJS_OK;
}


static njs_int_t
njs_object_enumerate_object(njs_vm_t *vm, const njs_object_t *object,
    njs_array_t *items, njs_object_enum_t kind, njs_object_enum_type_t type,
    njs_bool_t all)
{
    njs_int_t           ret;
    const njs_object_t  *proto;

    ret = njs_object_own_enumerate_object(vm, object, object, items, kind,
                                          type, all);
    if (njs_slow_path(ret != NJS_OK)) {
        return NJS_ERROR;
    }

    proto = object->__proto__;

    while (proto != NULL) {
        ret = njs_object_own_enumerate_value(vm, proto, object, items, kind,
                                             type, all);
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
    njs_object_enum_type_t type, njs_bool_t all)
{
    njs_int_t           ret;
    njs_value_t         value, *v;
    njs_array_t         *entry;
    njs_lvlhsh_each_t   lhe;
    njs_object_prop_t   *prop, *ext_prop;
    njs_lvlhsh_query_t  lhq;
    const njs_lvlhsh_t  *hash;

    lhq.proto = &njs_object_hash_proto;

    njs_lvlhsh_each_init(&lhe, &njs_object_hash_proto);
    hash = &object->hash;

    switch (kind) {
    case NJS_ENUM_KEYS:
        for ( ;; ) {
            prop = njs_lvlhsh_each(hash, &lhe);

            if (prop == NULL) {
                break;
            }

            if (!njs_is_enumerable(&prop->name, type)) {
                continue;
            }

            njs_object_property_key_set(&lhq, &prop->name, lhe.key_hash);

            ext_prop = njs_object_exist_in_proto(parent, object, &lhq);

            if (ext_prop == NULL && prop->type != NJS_WHITEOUT
                && (prop->enumerable || all))
            {
                ret = njs_array_add(vm, items, &prop->name);
                if (njs_slow_path(ret != NJS_OK)) {
                    return NJS_ERROR;
                }
            }
        }

        njs_lvlhsh_each_init(&lhe, &njs_object_hash_proto);
        hash = &object->shared_hash;

        for ( ;; ) {
            prop = njs_lvlhsh_each(hash, &lhe);

            if (prop == NULL) {
                break;
            }

            if (!njs_is_enumerable(&prop->name, type)) {
                continue;
            }

            njs_object_property_key_set(&lhq, &prop->name, lhe.key_hash);

            ret = njs_lvlhsh_find(&object->hash, &lhq);

            if (ret != NJS_OK) {
                ext_prop = njs_object_exist_in_proto(parent, object, &lhq);

                if (ext_prop == NULL && (prop->enumerable || all)) {
                    ret = njs_array_add(vm, items, &prop->name);
                    if (njs_slow_path(ret != NJS_OK)) {
                        return NJS_ERROR;
                    }
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

            if (!njs_is_enumerable(&prop->name, type)) {
                continue;
            }

            njs_object_property_key_set(&lhq, &prop->name, lhe.key_hash);

            ext_prop = njs_object_exist_in_proto(parent, object, &lhq);

            if (ext_prop == NULL && prop->type != NJS_WHITEOUT
                && (prop->enumerable || all))
            {
                v = (prop->type != NJS_ACCESSOR)
                            ? njs_prop_value(prop)
                            : njs_value_arg(&njs_value_undefined);
                ret = njs_array_add(vm, items, v);
                if (njs_slow_path(ret != NJS_OK)) {
                    return NJS_ERROR;
                }
            }
        }

        njs_lvlhsh_each_init(&lhe, &njs_object_hash_proto);
        hash = &object->shared_hash;

        for ( ;; ) {
            prop = njs_lvlhsh_each(hash, &lhe);

            if (prop == NULL) {
                break;
            }

            if (!njs_is_enumerable(&prop->name, type)) {
                continue;
            }

            njs_object_property_key_set(&lhq, &prop->name, lhe.key_hash);

            ret = njs_lvlhsh_find(&object->hash, &lhq);

            if (ret != NJS_OK) {
                ext_prop = njs_object_exist_in_proto(parent, object, &lhq);

                if (ext_prop == NULL && (prop->enumerable || all)) {
                    v = (prop->type != NJS_ACCESSOR)
                                ? njs_prop_value(prop)
                                : njs_value_arg(&njs_value_undefined);
                    ret = njs_array_add(vm, items, v);
                    if (njs_slow_path(ret != NJS_OK)) {
                        return NJS_ERROR;
                    }
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

            if (!njs_is_enumerable(&prop->name, type)) {
                continue;
            }

            njs_object_property_key_set(&lhq, &prop->name, lhe.key_hash);

            ext_prop = njs_object_exist_in_proto(parent, object, &lhq);

            if (ext_prop == NULL && prop->type != NJS_WHITEOUT
                && (prop->enumerable || all))
            {
                entry = njs_array_alloc(vm, 0, 2, 0);
                if (njs_slow_path(entry == NULL)) {
                    return NJS_ERROR;
                }

                njs_string_copy(&entry->start[0], &prop->name);

                v = (prop->type != NJS_ACCESSOR)
                            ? njs_prop_value(prop)
                            : njs_value_arg(&njs_value_undefined);

                njs_value_assign(&entry->start[1], v);

                njs_set_array(&value, entry);

                ret = njs_array_add(vm, items, &value);
                if (njs_slow_path(ret != NJS_OK)) {
                    return NJS_ERROR;
                }
            }
        }

        njs_lvlhsh_each_init(&lhe, &njs_object_hash_proto);
        hash = &object->shared_hash;

        for ( ;; ) {
            prop = njs_lvlhsh_each(hash, &lhe);

            if (prop == NULL) {
                break;
            }

            if (!njs_is_enumerable(&prop->name, type)) {
                continue;
            }

            njs_object_property_key_set(&lhq, &prop->name, lhe.key_hash);

            ret = njs_lvlhsh_find(&object->hash, &lhq);

            if (ret != NJS_OK && (prop->enumerable || all)) {
                ext_prop = njs_object_exist_in_proto(parent, object, &lhq);

                if (ext_prop == NULL) {
                    entry = njs_array_alloc(vm, 0, 2, 0);
                    if (njs_slow_path(entry == NULL)) {
                        return NJS_ERROR;
                    }

                    njs_string_copy(&entry->start[0], &prop->name);
                    njs_value_assign(&entry->start[1], njs_prop_value(prop));

                    njs_set_array(&value, entry);

                    ret = njs_array_add(vm, items, &value);
                    if (njs_slow_path(ret != NJS_OK)) {
                        return NJS_ERROR;
                    }
                }
            }
        }

        break;
    }

    return NJS_OK;
}


njs_inline njs_int_t
njs_traverse_visit(njs_arr_t *list, const njs_value_t *value)
{
    njs_object_t  **p;

    if (njs_is_object(value)) {
        p = njs_arr_add(list);
        if (njs_slow_path(p == NULL)) {
            return NJS_ERROR;
        }

        *p = njs_object(value);
    }

    return NJS_OK;
}


njs_inline njs_int_t
njs_traverse_visited(njs_arr_t *list, const njs_value_t *value)
{
    njs_uint_t    items, n;
    njs_object_t  **start, *obj;

    if (!njs_is_object(value)) {
        /* External. */
        return 0;
    }

    start = list->start;
    items = list->items;
    obj = njs_object(value);

    for (n = 0; n < items; n++) {
        if (start[n] == obj) {
            return 1;
        }
    }

    return 0;
}


njs_int_t
njs_object_traverse(njs_vm_t *vm, njs_object_t *object, void *ctx,
    njs_object_traverse_cb_t cb)
{
    njs_int_t             ret;
    njs_arr_t             visited;
    njs_object_t          **start;
    njs_value_t           value, *key;
    njs_traverse_t        *s;
    njs_object_prop_t     *prop;
    njs_property_query_t  pq;
    njs_traverse_t        state[NJS_TRAVERSE_MAX_DEPTH];

    s = &state[0];
    s->prop = NULL;
    s->parent = NULL;
    s->index = 0;
    njs_set_object(&s->value, object);
    s->keys = njs_value_own_enumerate(vm, &s->value, NJS_ENUM_KEYS,
                                      NJS_ENUM_STRING | NJS_ENUM_SYMBOL, 1);
    if (njs_slow_path(s->keys == NULL)) {
        return NJS_ERROR;
    }

    start = njs_arr_init(vm->mem_pool, &visited, NULL, 8, sizeof(void *));
    if (njs_slow_path(start == NULL)) {
        return NJS_ERROR;
    }

    (void) njs_traverse_visit(&visited, &s->value);

    for ( ;; ) {

        if (s->index >= s->keys->length) {
            njs_array_destroy(vm, s->keys);
            s->keys = NULL;

            if (s == &state[0]) {
                goto done;
            }

            s--;
            continue;
        }

        njs_property_query_init(&pq, NJS_PROPERTY_QUERY_GET, 0, 0);
        key = &s->keys->start[s->index++];

        ret = njs_property_query(vm, &pq, &s->value, key);
        if (njs_slow_path(ret != NJS_OK)) {
            if (ret == NJS_DECLINED) {
                continue;
            }

            return NJS_ERROR;
        }

        prop = pq.lhq.value;
        s->prop = prop;

        ret = cb(vm, s, ctx);
        if (njs_slow_path(ret != NJS_OK)) {
            return ret;
        }

        if (njs_is_accessor_descriptor(prop)) {
            continue;
        }

        njs_value_assign(&value, njs_prop_value(prop));

        if (prop->type == NJS_PROPERTY_HANDLER) {
            ret = njs_prop_handler(prop)(vm, prop, &s->value, NULL, &value);
            if (njs_slow_path(ret == NJS_ERROR)) {
                return ret;

            }
        }

        if (njs_is_object(&value)
            && !njs_traverse_visited(&visited, &value))
        {
            ret = njs_traverse_visit(&visited, &value);
            if (njs_slow_path(ret != NJS_OK)) {
                return NJS_ERROR;
            }

            if (s == &state[NJS_TRAVERSE_MAX_DEPTH - 1]) {
                njs_type_error(vm, "njs_object_traverse() recursion limit:%d",
                               NJS_TRAVERSE_MAX_DEPTH);
                return NJS_ERROR;
            }

            s++;
            s->prop = NULL;
            s->parent = &s[-1];
            s->index = 0;
            njs_value_assign(&s->value, &value);
            s->keys = njs_value_own_enumerate(vm, &s->value, NJS_ENUM_KEYS,
                                          NJS_ENUM_STRING | NJS_ENUM_SYMBOL, 1);
            if (njs_slow_path(s->keys == NULL)) {
                return NJS_ERROR;
            }
        }
    }

done:

    njs_arr_destroy(&visited);

    return NJS_OK;
}


static njs_int_t
njs_object_define_property(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t unused)
{
    njs_int_t    ret;
    njs_value_t  *value, *name, *desc, lvalue;

    if (!njs_is_object(njs_arg(args, nargs, 1))) {
        njs_type_error(vm, "Object.defineProperty is called on non-object");
        return NJS_ERROR;
    }

    desc = njs_arg(args, nargs, 3);

    if (!njs_is_object(desc)) {
        njs_type_error(vm, "descriptor is not an object");
        return NJS_ERROR;
    }

    value = njs_argument(args, 1);
    name = njs_lvalue_arg(&lvalue, args, nargs, 2);

    ret = njs_object_prop_define(vm, value, name, desc,
                                 NJS_OBJECT_PROP_DESCRIPTOR, 0);
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
    uint32_t              i, length;
    njs_int_t             ret;
    njs_array_t           *keys;
    njs_value_t           desc, *value, *descs;
    njs_object_prop_t     *prop;
    njs_property_query_t  pq;

    if (!njs_is_object(njs_arg(args, nargs, 1))) {
        njs_type_error(vm, "Object.defineProperties is called on non-object");
        return NJS_ERROR;
    }

    descs = njs_arg(args, nargs, 2);
    ret = njs_value_to_object(vm, descs);
    if (njs_slow_path(ret != NJS_OK)) {
        return ret;
    }

    keys = njs_value_own_enumerate(vm, descs, NJS_ENUM_KEYS,
                                   NJS_ENUM_STRING | NJS_ENUM_SYMBOL, 0);
    if (njs_slow_path(keys == NULL)) {
        return NJS_ERROR;
    }

    length = keys->length;
    value = njs_argument(args, 1);
    njs_property_query_init(&pq, NJS_PROPERTY_QUERY_GET, 0, 0);

    for (i = 0; i < length; i++) {
        pq.lhq.key_hash = 0;

        ret = njs_property_query(vm, &pq, descs, &keys->start[i]);
        if (njs_slow_path(ret == NJS_ERROR)) {
            goto done;
        }

        prop = pq.lhq.value;

        if (ret == NJS_DECLINED || !prop->enumerable) {
            continue;
        }

        ret = njs_value_property(vm, descs, &keys->start[i], &desc);
        if (njs_slow_path(ret == NJS_ERROR)) {
            goto done;
        }

        ret = njs_object_prop_define(vm, value, &keys->start[i], &desc,
                                     NJS_OBJECT_PROP_DESCRIPTOR, 0);
        if (njs_slow_path(ret != NJS_OK)) {
            goto done;
        }
    }

    ret = NJS_OK;
    vm->retval = *value;

done:

    njs_array_destroy(vm, keys);

    return ret;
}


static njs_int_t
njs_object_get_own_property_descriptor(njs_vm_t *vm, njs_value_t *args,
    njs_uint_t nargs, njs_index_t unused)
{
    njs_value_t  lvalue, *value, *property;

    value = njs_arg(args, nargs, 1);

    if (njs_is_null_or_undefined(value)) {
        njs_type_error(vm, "cannot convert %s argument to object",
                       njs_type_string(value->type));
        return NJS_ERROR;
    }

    property = njs_lvalue_arg(&lvalue, args, nargs, 2);

    return njs_object_prop_descriptor(vm, &vm->retval, value, property);
}


static njs_int_t
njs_object_get_own_property_descriptors(njs_vm_t *vm, njs_value_t *args,
    njs_uint_t nargs, njs_index_t unused)
{
    njs_int_t           ret;
    uint32_t            i, length;
    njs_array_t         *names;
    njs_value_t         descriptor, *value, *key;
    njs_object_t        *descriptors;
    njs_object_prop_t   *pr;
    njs_lvlhsh_query_t  lhq;

    value = njs_arg(args, nargs, 1);

    if (njs_is_null_or_undefined(value)) {
        njs_type_error(vm, "cannot convert %s argument to object",
                       njs_type_string(value->type));

        return NJS_ERROR;
    }

    names = njs_value_own_enumerate(vm, value, NJS_ENUM_KEYS,
                                    NJS_ENUM_STRING | NJS_ENUM_SYMBOL, 1);
    if (njs_slow_path(names == NULL)) {
        return NJS_ERROR;
    }

    length = names->length;

    descriptors = njs_object_alloc(vm);
    if (njs_slow_path(descriptors == NULL)) {
        ret = NJS_ERROR;
        goto done;
    }

    lhq.replace = 0;
    lhq.pool = vm->mem_pool;
    lhq.proto = &njs_object_hash_proto;

    for (i = 0; i < length; i++) {
        key = &names->start[i];
        ret = njs_object_prop_descriptor(vm, &descriptor, value, key);
        if (njs_slow_path(ret != NJS_OK)) {
            ret = NJS_ERROR;
            goto done;
        }

        pr = njs_object_prop_alloc(vm, key, &descriptor, 1);
        if (njs_slow_path(pr == NULL)) {
            ret = NJS_ERROR;
            goto done;
        }

        njs_object_property_key_set(&lhq, key, 0);
        lhq.value = pr;

        ret = njs_lvlhsh_insert(&descriptors->hash, &lhq);
        if (njs_slow_path(ret != NJS_OK)) {
            njs_internal_error(vm, "lvlhsh insert failed");
            goto done;
        }
    }

    ret = NJS_OK;
    njs_set_object(&vm->retval, descriptors);

done:

    njs_array_destroy(vm, names);

    return ret;
}


static njs_int_t
njs_object_get_own_property(njs_vm_t *vm, njs_value_t *args,
    njs_uint_t nargs, njs_index_t type)
{
    njs_array_t  *names;
    njs_value_t  *value;

    value = njs_arg(args, nargs, 1);

    if (njs_is_null_or_undefined(value)) {
        njs_type_error(vm, "cannot convert %s argument to object",
                       njs_type_string(value->type));

        return NJS_ERROR;
    }

    names = njs_value_own_enumerate(vm, value, NJS_ENUM_KEYS,
                                    type, 1);
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
    uint32_t     index;
    njs_value_t  *value;

    value = njs_arg(args, nargs, 1);

    if (njs_is_object(value)) {
        njs_object_prototype_proto(vm, NULL, value, NULL, &vm->retval);
        return NJS_OK;
    }

    if (!njs_is_null_or_undefined(value)) {
        index = njs_primitive_prototype_index(value->type);

        if (njs_is_symbol(value)) {
            njs_set_object(&vm->retval, &vm->prototypes[index].object);

        } else {
            njs_set_object_value(&vm->retval,
                                 &vm->prototypes[index].object_value);
        }

        return NJS_OK;
    }

    njs_type_error(vm, "cannot convert %s argument to object",
                   njs_type_string(value->type));

    return NJS_ERROR;
}


static njs_int_t
njs_object_set_prototype_of(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t unused)
{
    njs_int_t    ret;
    njs_value_t  *value, *proto;

    value = njs_arg(args, nargs, 1);
    if (njs_slow_path(njs_is_null_or_undefined(value))) {
        njs_type_error(vm, "cannot convert %s argument to object",
                       njs_type_string(value->type));
        return NJS_ERROR;
    }

    proto = njs_arg(args, nargs, 2);
    if (njs_slow_path(!njs_is_object(proto) && !njs_is_null(proto))) {
        njs_type_error(vm, "prototype may only be an object or null: %s",
                       njs_type_string(proto->type));
        return NJS_ERROR;
    }

    if (njs_slow_path(!njs_is_object(value))) {
        vm->retval = *value;

        return NJS_OK;
    }

    ret = njs_object_set_prototype(vm, njs_object(value), proto);
    if (njs_fast_path(ret == NJS_OK)) {
        vm->retval = *value;

        return NJS_OK;
    }

    if (ret == NJS_DECLINED) {
        njs_type_error(vm, "Cannot set property \"prototype\", "
                       "object is not extensible");

    } else {
        njs_type_error(vm, "Cyclic __proto__ value");
    }

    return NJS_ERROR;
}


/* 7.3.15 SetIntegrityLevel */

static njs_int_t
njs_object_set_integrity_level(njs_vm_t *vm, njs_value_t *args,
    njs_uint_t nargs, njs_index_t level)
{
    uint32_t           length;
    njs_int_t          ret;
    njs_array_t        *array;
    njs_value_t        *value;
    njs_lvlhsh_t       *hash;
    njs_object_t       *object;
    njs_object_prop_t  *prop;
    njs_lvlhsh_each_t  lhe;

    value = njs_arg(args, nargs, 1);

    if (njs_slow_path(!njs_is_object(value))) {
        vm->retval = *value;
        return NJS_OK;
    }

    if (njs_slow_path(level == NJS_OBJECT_INTEGRITY_FROZEN
                      && njs_is_typed_array(value)
                      && njs_typed_array_length(njs_typed_array(value)) != 0))
    {
        njs_type_error(vm, "Cannot freeze array buffer views with elements");
        return NJS_ERROR;
    }

    if (njs_is_fast_array(value)) {
        array = njs_array(value);
        length = array->length;

        ret = njs_array_convert_to_slow_array(vm, array);
        if (ret != NJS_OK) {
            return ret;
        }

        ret = njs_array_length_redefine(vm, value, length, 1);
        if (njs_slow_path(ret != NJS_OK)) {
            return ret;
        }
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

        if (level == NJS_OBJECT_INTEGRITY_FROZEN
            && !njs_is_accessor_descriptor(prop))
        {
            prop->writable = 0;
        }

        prop->configurable = 0;
    }

    vm->retval = *value;

    return NJS_OK;
}


static njs_int_t
njs_object_test_integrity_level(njs_vm_t *vm, njs_value_t *args,
    njs_uint_t nargs, njs_index_t level)
{
    njs_value_t        *value;
    njs_lvlhsh_t       *hash;
    njs_object_t       *object;
    njs_object_prop_t  *prop;
    njs_lvlhsh_each_t  lhe;
    const njs_value_t  *retval;

    value = njs_arg(args, nargs, 1);

    if (njs_slow_path(!njs_is_object(value))) {
        vm->retval = njs_value_true;
        return NJS_OK;
    }

    retval = &njs_value_false;

    object = njs_object(value);

    if (object->extensible) {
        goto done;
    }

    if (njs_slow_path(level == NJS_OBJECT_INTEGRITY_FROZEN)
                      && njs_is_typed_array(value)
                      && njs_typed_array_length(njs_typed_array(value)) != 0)
    {
        goto done;
    }

    njs_lvlhsh_each_init(&lhe, &njs_object_hash_proto);

    hash = &object->hash;

    for ( ;; ) {
        prop = njs_lvlhsh_each(hash, &lhe);

        if (prop == NULL) {
            break;
        }

        if (prop->configurable) {
            goto done;
        }

        if (level == NJS_OBJECT_INTEGRITY_FROZEN
            && njs_is_data_descriptor(prop) && prop->writable)
        {
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
    njs_value_t  *value;

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
    njs_value_t        *value;
    const njs_value_t  *retval;

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


static njs_int_t
njs_object_assign(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t unused)
{
    uint32_t              i, j, length;
    njs_int_t             ret;
    njs_array_t           *names;
    njs_value_t           *key, *source, *value, setval;
    njs_object_prop_t     *prop;
    njs_property_query_t  pq;

    value = njs_arg(args, nargs, 1);

    ret = njs_value_to_object(vm, value);
    if (njs_slow_path(ret != NJS_OK)) {
        return ret;
    }

    names = NULL;

    for (i = 2; i < nargs; i++) {
        source = &args[i];

        names = njs_value_own_enumerate(vm, source, NJS_ENUM_KEYS,
                                        NJS_ENUM_STRING | NJS_ENUM_SYMBOL, 1);
        if (njs_slow_path(names == NULL)) {
            return NJS_ERROR;
        }

        length = names->length;

        for (j = 0; j < length; j++) {
            key = &names->start[j];

            njs_property_query_init(&pq, NJS_PROPERTY_QUERY_GET, 0, 1);

            ret = njs_property_query(vm, &pq, source, key);
            if (njs_slow_path(ret != NJS_OK)) {
                goto exception;
            }

            prop = pq.lhq.value;
            if (!prop->enumerable) {
                continue;
            }

            ret = njs_value_property(vm, source, key, &setval);
            if (njs_slow_path(ret != NJS_OK)) {
                goto exception;
            }

            ret = njs_value_property_set(vm, value, key, &setval);
            if (njs_slow_path(ret != NJS_OK)) {
                goto exception;
            }
        }

        njs_array_destroy(vm, names);
    }

    vm->retval = *value;

    return NJS_OK;

exception:

    njs_array_destroy(vm, names);

    return NJS_ERROR;
}


static njs_int_t
njs_object_is(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t unused)
{
    njs_set_boolean(&vm->retval, njs_values_same(njs_arg(args, nargs, 1),
                                                 njs_arg(args, nargs, 2)));

    return NJS_OK;
}


/*
 * The __proto__ property of booleans, numbers and strings primitives,
 * of objects created by Boolean(), Number(), and String() constructors,
 * and of Boolean.prototype, Number.prototype, and String.prototype objects.
 */

njs_int_t
njs_primitive_prototype_get_proto(njs_vm_t *vm, njs_object_prop_t *prop,
    njs_value_t *value, njs_value_t *setval, njs_value_t *retval)
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

    if (proto != NULL) {
        njs_set_type_object(retval, proto, proto->type);

    } else {
        njs_set_undefined(retval);
    }

    return NJS_OK;
}


/*
 * The "prototype" property of Object(), Array() and other functions is
 * created on demand in the functions' private hash by the "prototype"
 * getter.  The properties are set to appropriate prototype.
 */

njs_int_t
njs_object_prototype_create(njs_vm_t *vm, njs_object_prop_t *prop,
    njs_value_t *value, njs_value_t *setval, njs_value_t *retval)
{
    int64_t            index;
    njs_function_t     *function;
    const njs_value_t  *proto;

    proto = NULL;
    function = njs_function(value);
    index = function - vm->constructors;

    if (index >= 0 && index < NJS_OBJ_TYPE_MAX) {
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
    njs_int_t           ret;
    njs_object_prop_t   *prop;
    njs_lvlhsh_query_t  lhq;

    static const njs_value_t  proto_string = njs_string("prototype");

    prop = njs_object_prop_alloc(vm, &proto_string, &njs_value_undefined, 0);
    if (njs_slow_path(prop == NULL)) {
        return NULL;
    }

    /* GC */

    njs_set_type_object(njs_prop_value(prop), prototype, prototype->type);

    lhq.value = prop;
    lhq.key_hash = NJS_PROTOTYPE_HASH;
    lhq.key = njs_str_value("prototype");
    lhq.replace = 1;
    lhq.pool = vm->mem_pool;
    lhq.proto = &njs_object_hash_proto;

    ret = njs_lvlhsh_insert(hash, &lhq);

    if (njs_fast_path(ret == NJS_OK)) {
        return njs_prop_value(prop);
    }

    njs_internal_error(vm, "lvlhsh insert failed");

    return NULL;
}


static const njs_object_prop_t  njs_object_constructor_properties[] =
{
    NJS_DECLARE_PROP_NAME("Object"),

    NJS_DECLARE_PROP_LENGTH(1),

    NJS_DECLARE_PROP_HANDLER("prototype", njs_object_prototype_create, 0, 0, 0),

    NJS_DECLARE_PROP_NATIVE("create", njs_object_create, 2, 0),

    NJS_DECLARE_PROP_NATIVE("keys", njs_object_keys, 1, 0),

    NJS_DECLARE_PROP_NATIVE("values", njs_object_values, 1, 0),

    NJS_DECLARE_PROP_NATIVE("entries", njs_object_entries, 1, 0),

    NJS_DECLARE_PROP_NATIVE("defineProperty", njs_object_define_property, 3, 0),

    NJS_DECLARE_PROP_LNATIVE("defineProperties",
                             njs_object_define_properties, 2, 0),

    NJS_DECLARE_PROP_LNATIVE("getOwnPropertyDescriptor",
                             njs_object_get_own_property_descriptor, 2, 0),

    NJS_DECLARE_PROP_LNATIVE("getOwnPropertyDescriptors",
                             njs_object_get_own_property_descriptors, 1, 0),

    NJS_DECLARE_PROP_LNATIVE("getOwnPropertyNames",
                             njs_object_get_own_property, 1, NJS_ENUM_STRING),

    NJS_DECLARE_PROP_LNATIVE("getOwnPropertySymbols",
                             njs_object_get_own_property, 1, NJS_ENUM_SYMBOL),

    NJS_DECLARE_PROP_NATIVE("getPrototypeOf", njs_object_get_prototype_of, 1,
                            0),

    NJS_DECLARE_PROP_NATIVE("setPrototypeOf", njs_object_set_prototype_of, 2,
                            0),

    NJS_DECLARE_PROP_NATIVE("freeze", njs_object_set_integrity_level, 1,
                            NJS_OBJECT_INTEGRITY_FROZEN),

    NJS_DECLARE_PROP_NATIVE("isFrozen", njs_object_test_integrity_level, 1,
                            NJS_OBJECT_INTEGRITY_FROZEN),

    NJS_DECLARE_PROP_NATIVE("seal", njs_object_set_integrity_level, 1,
                            NJS_OBJECT_INTEGRITY_SEALED),

    NJS_DECLARE_PROP_NATIVE("isSealed", njs_object_test_integrity_level, 1,
                            NJS_OBJECT_INTEGRITY_SEALED),

    NJS_DECLARE_PROP_LNATIVE("preventExtensions", njs_object_prevent_extensions,
                             1, 0),

    NJS_DECLARE_PROP_NATIVE("isExtensible", njs_object_is_extensible, 1, 0),

    NJS_DECLARE_PROP_NATIVE("assign", njs_object_assign, 2, 0),

    NJS_DECLARE_PROP_NATIVE("is", njs_object_is, 2, 0),
};


const njs_object_init_t  njs_object_constructor_init = {
    njs_object_constructor_properties,
    njs_nitems(njs_object_constructor_properties),
};


static njs_int_t
njs_object_set_prototype(njs_vm_t *vm, njs_object_t *object,
    const njs_value_t *value)
{
    const njs_object_t *proto;

    proto = njs_object(value);

    if (njs_slow_path(object->__proto__ == proto)) {
        return NJS_OK;
    }

    if (!object->extensible) {
        return NJS_DECLINED;
    }

    if (njs_slow_path(proto == NULL)) {
        object->__proto__ = NULL;
        return NJS_OK;
    }

    do {
        if (proto == object) {
            return NJS_ERROR;
        }

        proto = proto->__proto__;

    } while (proto != NULL);

    object->__proto__ = njs_object(value);

    return NJS_OK;
}


njs_int_t
njs_object_prototype_proto(njs_vm_t *vm, njs_object_prop_t *prop,
    njs_value_t *value, njs_value_t *setval, njs_value_t *retval)
{
    njs_int_t     ret;
    njs_object_t  *proto, *object;

    if (!njs_is_object(value)) {
        *retval = *value;
        return NJS_OK;
    }

    object = njs_object(value);

    if (setval != NULL) {
        if (njs_is_object(setval) || njs_is_null(setval)) {
            ret = njs_object_set_prototype(vm, object, setval);
            if (njs_slow_path(ret == NJS_ERROR)) {
                njs_type_error(vm, "Cyclic __proto__ value");
                return NJS_ERROR;
            }
        }

        njs_set_undefined(retval);

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
njs_object_prototype_create_constructor(njs_vm_t *vm, njs_object_prop_t *prop,
    njs_value_t *value, njs_value_t *setval, njs_value_t *retval)
{
    int64_t                 index;
    njs_value_t             *cons, constructor;
    njs_object_t            *object;
    njs_object_prototype_t  *prototype;

    if (setval != NULL) {
        if (!njs_is_object(value)) {
            njs_type_error(vm, "Cannot create propery \"constructor\" on %s",
                           njs_type_string(value->type));
            return NJS_ERROR;
        }

        cons = njs_property_constructor_set(vm, njs_object_hash(value), setval);
        if (njs_slow_path(cons == NULL)) {
            return NJS_ERROR;
        }

        *retval = *cons;
        return NJS_OK;
    }

    if (njs_is_object(value)) {
        object = njs_object(value);

        do {
            prototype = (njs_object_prototype_t *) object;
            index = prototype - vm->prototypes;

            if (index >= 0 && index < NJS_OBJ_TYPE_MAX) {
                goto found;
            }

            object = object->__proto__;

        } while (object != NULL);

        return NJS_ERROR;

    } else {
        index = njs_primitive_prototype_index(value->type);
        prototype = &vm->prototypes[index];
    }

found:

    njs_set_function(&constructor, &vm->constructors[index]);
    setval = &constructor;

    cons = njs_property_constructor_set(vm, &prototype->object.hash, setval);
    if (njs_slow_path(cons == NULL)) {
        return NJS_ERROR;
    }

    *retval = *cons;
    return NJS_OK;
}


njs_value_t *
njs_property_constructor_set(njs_vm_t *vm, njs_lvlhsh_t *hash,
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

    njs_value_assign(njs_prop_value(prop), constructor);
    prop->enumerable = 0;

    lhq.value = prop;
    lhq.key_hash = NJS_CONSTRUCTOR_HASH;
    lhq.key = njs_str_value("constructor");
    lhq.replace = 1;
    lhq.pool = vm->mem_pool;
    lhq.proto = &njs_object_hash_proto;

    ret = njs_lvlhsh_insert(hash, &lhq);

    if (njs_fast_path(ret == NJS_OK)) {
        return njs_prop_value(prop);
    }

    njs_internal_error(vm, "lvlhsh insert/replace failed");

    return NULL;
}


static njs_int_t
njs_object_prototype_value_of(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t unused)
{
    vm->retval = *njs_argument(args, 0);

    if (!njs_is_object(&vm->retval)) {
        return njs_value_to_object(vm, &vm->retval);
    }

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
static const njs_value_t  njs_object_arguments_string =
                                     njs_long_string("[object Arguments]");


njs_int_t
njs_object_prototype_to_string(njs_vm_t *vm, njs_value_t *args,
    njs_uint_t nargs, njs_index_t unused)
{
    return njs_object_to_string(vm, &args[0], &vm->retval);
}


njs_int_t
njs_object_to_string(njs_vm_t *vm, njs_value_t *this, njs_value_t *retval)
{
    u_char             *p;
    njs_int_t          ret;
    njs_value_t        tag;
    njs_string_prop_t  string;
    const njs_value_t  *name;

    if (njs_is_null_or_undefined(this)) {
        njs_value_assign(retval,
                         njs_is_null(this) ? &njs_object_null_string
                                           : &njs_object_undefined_string);

        return NJS_OK;
    }

    ret = njs_value_to_object(vm, this);
    if (njs_slow_path(ret != NJS_OK)) {
        return ret;
    }

    name = &njs_object_object_string;

    if (njs_is_array(this)) {
        name = &njs_object_array_string;

    } else if (njs_is_object(this)
        && njs_lvlhsh_eq(&njs_object(this)->shared_hash,
                         &vm->shared->arguments_object_instance_hash))
    {
        name = &njs_object_arguments_string;

    } else if (njs_is_function(this)) {
        name = &njs_object_function_string;

    } else if (njs_is_error(this)) {
        name = &njs_object_error_string;

    } else if (njs_is_object_value(this)) {

        switch (njs_object_value(this)->type) {
        case NJS_BOOLEAN:
            name = &njs_object_boolean_string;
            break;

        case NJS_NUMBER:
            name = &njs_object_number_string;
            break;

        case NJS_STRING:
            name = &njs_object_string_string;
            break;

        default:
            break;
        }

    } else if (njs_is_date(this)) {
        name = &njs_object_date_string;

    } else if (njs_is_regexp(this)) {
        name = &njs_object_regexp_string;
    }

    ret = njs_object_string_tag(vm, this, &tag);
    if (njs_slow_path(ret == NJS_ERROR)) {
        return ret;
    }

    if (ret == NJS_DECLINED) {
        if (njs_slow_path(name == NULL)) {
            njs_internal_error(vm, "Unknown value type");

            return NJS_ERROR;
        }

        njs_value_assign(retval, name);

        return NJS_OK;
    }

    (void) njs_string_prop(&string, &tag);

    p = njs_string_alloc(vm, retval, string.size + njs_length("[object ]"),
                         string.length + njs_length("[object ]"));
    if (njs_slow_path(p == NULL)) {
        return NJS_ERROR;
    }

    p = njs_cpymem(p, "[object ", 8);
    p = njs_cpymem(p, string.start, string.size);
    *p = ']';

    return NJS_OK;
}


static njs_int_t
njs_object_prototype_has_own_property(njs_vm_t *vm, njs_value_t *args,
    njs_uint_t nargs, njs_index_t unused)
{
    njs_int_t             ret;
    njs_value_t           *value, *property, lvalue;
    njs_property_query_t  pq;

    value = njs_argument(args, 0);

    if (njs_is_null_or_undefined(value)) {
        njs_type_error(vm, "cannot convert %s argument to object",
                       njs_type_string(value->type));
        return NJS_ERROR;
    }

    property = njs_lvalue_arg(&lvalue, args, nargs, 1);

    if (njs_slow_path(!njs_is_key(property))) {
        ret = njs_value_to_key(vm, property, property);
        if (njs_slow_path(ret != NJS_OK)) {
            return NJS_ERROR;
        }
    }

    njs_property_query_init(&pq, NJS_PROPERTY_QUERY_GET, 0, 1);

    ret = njs_property_query(vm, &pq, value, property);

    switch (ret) {
    case NJS_OK:
        vm->retval = njs_value_true;
        return NJS_OK;

    case NJS_DECLINED:
        vm->retval = njs_value_false;
        return NJS_OK;

    case NJS_ERROR:
    default:
        return NJS_ERROR;
    }
}


static njs_int_t
njs_object_prototype_prop_is_enumerable(njs_vm_t *vm, njs_value_t *args,
    njs_uint_t nargs, njs_index_t unused)
{
    njs_int_t             ret;
    njs_value_t           *value, *property, lvalue;
    const njs_value_t     *retval;
    njs_object_prop_t     *prop;
    njs_property_query_t  pq;

    value = njs_argument(args, 0);

    if (njs_is_null_or_undefined(value)) {
        njs_type_error(vm, "cannot convert %s argument to object",
                       njs_type_string(value->type));
        return NJS_ERROR;
    }

    property = njs_lvalue_arg(&lvalue, args, nargs, 1);

    if (njs_slow_path(!njs_is_key(property))) {
        ret = njs_value_to_key(vm, property, property);
        if (njs_slow_path(ret != NJS_OK)) {
            return NJS_ERROR;
        }
    }

    njs_property_query_init(&pq, NJS_PROPERTY_QUERY_GET, 0, 1);

    ret = njs_property_query(vm, &pq, value, property);

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
        return NJS_ERROR;
    }

    vm->retval = *retval;

    return NJS_OK;
}


static njs_int_t
njs_object_prototype_is_prototype_of(njs_vm_t *vm, njs_value_t *args,
    njs_uint_t nargs, njs_index_t unused)
{
    njs_value_t        *prototype, *value;
    njs_object_t       *object, *proto;
    const njs_value_t  *retval;

    if (njs_slow_path(njs_is_null_or_undefined(njs_argument(args, 0)))) {
        njs_type_error(vm, "cannot convert undefined to object");
        return NJS_ERROR;
    }

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
    NJS_DECLARE_PROP_HANDLER("__proto__", njs_object_prototype_proto,
                             0, 0, NJS_OBJECT_PROP_VALUE_CW),

    NJS_DECLARE_PROP_HANDLER("constructor",
                             njs_object_prototype_create_constructor,
                             0, 0, NJS_OBJECT_PROP_VALUE_CW),

    NJS_DECLARE_PROP_NATIVE("valueOf", njs_object_prototype_value_of, 0, 0),

    NJS_DECLARE_PROP_NATIVE("toString", njs_object_prototype_to_string, 0, 0),

    NJS_DECLARE_PROP_NATIVE("hasOwnProperty",
                            njs_object_prototype_has_own_property, 1, 0),

    NJS_DECLARE_PROP_LNATIVE("propertyIsEnumerable",
                             njs_object_prototype_prop_is_enumerable, 1, 0),

    NJS_DECLARE_PROP_NATIVE("isPrototypeOf",
                            njs_object_prototype_is_prototype_of, 1, 0),
};


const njs_object_init_t  njs_object_prototype_init = {
    njs_object_prototype_properties,
    njs_nitems(njs_object_prototype_properties),
};


njs_int_t
njs_object_length(njs_vm_t *vm, njs_value_t *value, int64_t *length)
{
    njs_int_t    ret;
    njs_value_t  value_length;

    const njs_value_t  string_length = njs_string("length");

    if (njs_is_fast_array(value)) {
        *length = njs_array(value)->length;
        return NJS_OK;
    }

    ret = njs_value_property(vm, value, njs_value_arg(&string_length),
                             &value_length);
    if (njs_slow_path(ret == NJS_ERROR)) {
        return ret;
    }

    return njs_value_to_length(vm, &value_length, length);
}


const njs_object_type_init_t  njs_obj_type_init = {
    .constructor = njs_native_ctor(njs_object_constructor, 1, 0),
    .constructor_props = &njs_object_constructor_init,
    .prototype_props = &njs_object_prototype_init,
    .prototype_value = { .object = { .type = NJS_OBJECT } },
};
