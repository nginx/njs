
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) NGINX, Inc.
 */

#include <nxt_types.h>
#include <nxt_clang.h>
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
#include <njs_function.h>
#include <string.h>


static nxt_int_t njs_object_hash_test(nxt_lvlhsh_query_t *lhq, void *data);


nxt_noinline njs_object_t *
njs_object_alloc(njs_vm_t *vm)
{
    njs_object_t  *object;

    object = nxt_mem_cache_alloc(vm->mem_cache_pool, sizeof(njs_object_t));

    if (nxt_fast_path(object != NULL)) {
        nxt_lvlhsh_init(&object->hash);
        nxt_lvlhsh_init(&object->shared_hash);
        object->__proto__ = &vm->prototypes[NJS_PROTOTYPE_OBJECT];
        object->shared = 0;
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
        object->__proto__ = &vm->prototypes[NJS_PROTOTYPE_OBJECT];
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
        ov->object.shared = 0;

        index = njs_primitive_prototype_index(type);
        ov->object.__proto__ = &vm->prototypes[index];

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
        lhq.key.len = prop->name.short_string.size;

        if (lhq.key.len != NJS_STRING_LONG) {
            lhq.key.data = (u_char *) prop->name.short_string.start;

        } else {
            lhq.key.len = prop->name.data.string_size;
            lhq.key.data = prop->name.data.u.string->start;
        }

        lhq.key_hash = nxt_djb_hash(lhq.key.data, lhq.key.len);
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
        if (lhq->key.len != size) {
            return NXT_DECLINED;
        }

        start = prop->name.short_string.start;

    } else {
        if (lhq->key.len != prop->name.data.string_size) {
            return NXT_DECLINED;
        }

        start = prop->name.data.u.string->start;
    }

    if (memcmp(start, lhq->key.data, lhq->key.len) == 0) {
        return NXT_OK;
    }

    return NXT_DECLINED;
}


njs_object_prop_t *
njs_object_prop_alloc(njs_vm_t *vm, const njs_value_t *name)
{
    njs_object_prop_t  *prop;

    prop = nxt_mem_cache_align(vm->mem_cache_pool, sizeof(njs_value_t),
                               sizeof(njs_object_prop_t));

    if (nxt_fast_path(prop != NULL)) {
        prop->value = njs_value_void;

        /* GC: retain. */
        prop->name = *name;

        prop->type = NJS_PROPERTY;
        prop->enumerable = 1;
        prop->writable = 1;
        prop->configurable = 1;
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

    vm->exception = &njs_exception_type_error;

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

            type = NJS_OBJECT + value->type;

        } else {
            vm->exception = &njs_exception_type_error;

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

    vm->exception = &njs_exception_type_error;

    return NXT_ERROR;
}


/*
 * The __proto__ property of booleans, numbers and strings primitives,
 * of objects created by Boolean(), Number(), and String() constructors,
 * and of Boolean.prototype, Number.prototype, and String.prototype objects.
 */

njs_ret_t
njs_primitive_prototype_get_proto(njs_vm_t *vm, njs_value_t *value)
{
    njs_object_t  *proto;

    /*
     * The __proto__ getters reside in object prototypes of primitive types
     * and have to return different results for primitive type and for objects.
     */
    if (njs_is_object(value)) {
         proto = value->data.u.object->__proto__;

    } else {
         proto = &vm->prototypes[njs_primitive_prototype_index(value->type)];
    }

    vm->retval.data.u.object = proto;
    vm->retval.type = NJS_OBJECT;
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
    index = function - vm->functions;

    if (index >= 0 && index < NJS_PROTOTYPE_MAX) {
        proto = njs_property_prototype_create(vm, &function->object.hash,
                                              &vm->prototypes[index]);
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

    prop = njs_object_prop_alloc(vm, &prototype_string);
    if (nxt_slow_path(prop == NULL)) {
        return NULL;
    }

    /* GC */

    prop->value.data.u.object = prototype;
    prop->value.type = NJS_OBJECT;
    prop->value.data.truth = 1;

    prop->enumerable = 0;
    prop->writable = 0;
    prop->configurable = 0;

    lhq.value = prop;
    lhq.key_hash = NJS_PROTOTYPE_HASH;
    lhq.key.len = sizeof("prototype") - 1;
    lhq.key.data = (u_char *) "prototype";
    lhq.replace = 0;
    lhq.pool = vm->mem_cache_pool;
    lhq.proto = &njs_object_hash_proto;

    ret = nxt_lvlhsh_insert(hash, &lhq);

    if (nxt_fast_path(ret == NXT_OK)) {
        return &prop->value;
    }

    /* Memory allocation or NXT_DECLINED error. */
    vm->exception = &njs_exception_internal_error;

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
};


const njs_object_init_t  njs_object_constructor_init = {
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
        vm->retval.type = NJS_OBJECT;
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
    int32_t       index;
    njs_value_t   *cons;
    njs_object_t  *prototype;

    if (njs_is_object(value)) {
        prototype = value->data.u.object;

        do {
            index = prototype - vm->prototypes;

            if (index >= 0 && index < NJS_PROTOTYPE_MAX) {
                goto found;
            }

            prototype = prototype->__proto__;

        } while (prototype != NULL);

        nxt_thread_log_alert("prototype not found");

        return NXT_ERROR;

    } else {
        index = njs_primitive_prototype_index(value->type);
        prototype = &vm->prototypes[index];
    }

found:

    cons = njs_property_constructor_create(vm, &prototype->hash,
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

    prop = njs_object_prop_alloc(vm, &constructor_string);
    if (nxt_slow_path(prop == NULL)) {
        return NULL;
    }

    /* GC */

    prop->value = *constructor;
    prop->enumerable = 0;

    lhq.value = prop;
    lhq.key_hash = NJS_CONSTRUCTOR_HASH;
    lhq.key.len = sizeof("constructor") - 1;
    lhq.key.data = (u_char *) "constructor";
    lhq.replace = 0;
    lhq.pool = vm->mem_cache_pool;
    lhq.proto = &njs_object_hash_proto;

    ret = nxt_lvlhsh_insert(hash, &lhq);

    if (nxt_fast_path(ret == NXT_OK)) {
        return &prop->value;
    }

    /* Memory allocation or NXT_DECLINED error. */
    vm->exception = &njs_exception_internal_error;

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
static const njs_value_t  njs_object_date_string =
                                     njs_long_string("[object Date]");


njs_ret_t
njs_object_prototype_to_string(njs_vm_t *vm, njs_value_t *args,
    nxt_uint_t nargs, njs_index_t unused)
{
    int32_t       index;
    njs_object_t  *prototype;

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

        /* Objects. */
        &njs_object_object_string,
        &njs_object_array_string,
        &njs_object_boolean_string,
        &njs_object_number_string,
        &njs_object_string_string,
        &njs_object_function_string,
        &njs_object_regexp_string,
        &njs_object_date_string,
    };

    index = args[0].type;

    if (njs_is_object(&args[0])) {
        prototype = args[0].data.u.object;

        do {
            index = prototype - vm->prototypes;

            if (index >= 0 && index < NJS_PROTOTYPE_MAX) {
                index += NJS_OBJECT;
                goto found;
            }

            prototype = prototype->__proto__;

        } while (prototype != NULL);

        nxt_thread_log_alert("prototype not found");

        return NXT_ERROR;
    }

found:

    vm->retval = *class_name[index];

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
};


const njs_object_init_t  njs_object_prototype_init = {
    njs_object_prototype_properties,
    nxt_nitems(njs_object_prototype_properties),
};
