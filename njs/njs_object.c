
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


nxt_noinline njs_ret_t
njs_object_method(njs_vm_t *vm, njs_param_t *param, nxt_lvlhsh_query_t *lhq)
{
    njs_object_prop_t  *prop;

    prop = njs_object_property(vm, param->object->data.u.object, lhq);

    if (nxt_fast_path(prop != NULL)) {
        return njs_function_apply(vm, &prop->value, param);
    }

    return NXT_ERROR;
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
njs_object_constructor(njs_vm_t *vm, njs_param_t *param)
{
    nxt_uint_t    type;
    njs_value_t   *value;
    njs_object_t  *object;

    type = NJS_OBJECT;

    if (param->nargs == 0 || njs_is_null_or_void(&param->args[0])) {

        object = njs_object_alloc(vm);
        if (nxt_slow_path(object == NULL)) {
            return NXT_ERROR;
        }

    } else {
        value = &param->args[0];

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
njs_object_create(njs_vm_t *vm, njs_param_t *param)
{
    njs_value_t   *args;
    njs_object_t  *object;

    if (param->nargs != 0) {
        args = param->args;

        if (njs_is_object(&args[0]) || njs_is_null(&args[0])) {

            object = njs_object_alloc(vm);
            if (nxt_slow_path(object == NULL)) {
                return NXT_ERROR;
            }

            if (!njs_is_null(&args[0])) {
                /* GC */
                object->__proto__ = args[0].data.u.object;

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
 * The __proto__ property of booleans, numbers and strings primitives
 * and Boolean.prototype, Number.prototype, and String.prototype objects.
 */

njs_ret_t
njs_primitive_prototype_get_proto(njs_vm_t *vm, njs_value_t *value)
{
    nxt_uint_t  index;

    /*
     * The __proto__ getters reside in object prototypes of primitive types
     * and have to return different results for primitive type and for object
     * prototype.
     */
    index = njs_is_object(value) ? NJS_PROTOTYPE_OBJECT:
                                   njs_primitive_prototype_index(value->type);

    vm->retval.data.u.object = &vm->prototypes[index];
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
    int32_t                    index;
    nxt_int_t                  ret;
    njs_function_t             *function;
    njs_object_prop_t          *prop;
    nxt_lvlhsh_query_t         lhq;

    static const njs_value_t   prototype = njs_string("prototype");

    function = value->data.u.function;
    index = function - vm->functions;

    if (index < 0 && index > NJS_PROTOTYPE_MAX) {
        vm->retval = njs_value_void;
        return NXT_OK;
    }

    prop = njs_object_prop_alloc(vm, &prototype);
    if (nxt_slow_path(prop == NULL)) {
        return NXT_ERROR;
    }

    prop->value.data.u.object = &vm->prototypes[index];
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

    ret = nxt_lvlhsh_insert(&function->object.hash, &lhq);

    if (nxt_fast_path(ret == NXT_OK)) {
        vm->retval = prop->value;
    }

    /* TODO: exception NXT_ERROR. */

    return ret;
}


static const njs_object_prop_t  njs_object_constructor_properties[] =
{
    /* Object.name == "name". */
    { njs_string("Object"),
      njs_string("name"),
      NJS_PROPERTY, 0, 0, 0, },

    /* Object.length == 1. */
    { njs_value(NJS_NUMBER, 1, 1.0),
      njs_string("length"),
      NJS_PROPERTY, 0, 0, 0, },

    /* Object.prototype. */
    { njs_getter(njs_object_prototype_create),
      njs_string("prototype"),
      NJS_NATIVE_GETTER, 0, 0, 0, },

    /* Object.create(). */
    { njs_native_function(njs_object_create, 0),
      njs_string("create"),
      NJS_METHOD, 0, 0, 0, },
};


const njs_object_init_t  njs_object_constructor_init = {
     njs_object_constructor_properties,
     nxt_nitems(njs_object_constructor_properties),
};


static njs_ret_t
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
    int32_t                   index;
    nxt_int_t                 ret;
    njs_value_t               *constructor;
    njs_object_t              *prototype;
    njs_object_prop_t         *prop;
    nxt_lvlhsh_query_t        lhq;

    static const njs_value_t  constructor_string = njs_string("constructor");

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

    prop = njs_object_prop_alloc(vm, &constructor_string);
    if (nxt_slow_path(prop == NULL)) {
        return NXT_ERROR;
    }

    /* GC */

    constructor = &vm->scopes[NJS_SCOPE_GLOBAL][index];
    prop->value = *constructor;

    prop->enumerable = 0;

    lhq.value = prop;
    lhq.key_hash = NJS_CONSTRUCTOR_HASH;
    lhq.key.len = sizeof("constructor") - 1;
    lhq.key.data = (u_char *) "constructor";
    lhq.replace = 0;
    lhq.pool = vm->mem_cache_pool;
    lhq.proto = &njs_object_hash_proto;

    ret = nxt_lvlhsh_insert(&prototype->hash, &lhq);

    if (nxt_fast_path(ret == NXT_OK)) {
        vm->retval = *constructor;
    }

    return ret;
}


static njs_ret_t
njs_object_prototype_value_of(njs_vm_t *vm, njs_param_t *param)
{
    vm->retval = *param->object;

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


static njs_ret_t
njs_object_prototype_to_string(njs_vm_t *vm, njs_param_t *param)
{
    int32_t       index;
    njs_value_t   *value;
    njs_object_t  *prototype;

    static const njs_value_t  *class_name[] = {
        /* Primitives. */
        &njs_object_null_string,
        &njs_object_undefined_string,
        &njs_object_boolean_string,
        &njs_object_number_string,
        &njs_object_string_string,

        &njs_object_function_string,
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
    };

    value = param->object;
    index = value->type;

    if (njs_is_object(value)) {
        prototype = value->data.u.object;

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
    { njs_getter(njs_object_prototype_get_proto),
      njs_string("__proto__"),
      NJS_NATIVE_GETTER, 0, 0, 0, },

    { njs_getter(njs_object_prototype_create_constructor),
      njs_string("constructor"),
      NJS_NATIVE_GETTER, 0, 0, 0, },

    { njs_native_function(njs_object_prototype_value_of, 0),
      njs_string("valueOf"),
      NJS_METHOD, 0, 0, 0, },

    { njs_native_function(njs_object_prototype_to_string, 0),
      njs_string("toString"),
      NJS_METHOD, 0, 0, 0, },
};


const njs_object_init_t  njs_object_prototype_init = {
     njs_object_prototype_properties,
     nxt_nitems(njs_object_prototype_properties),
};
