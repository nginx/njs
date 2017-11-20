
/*
 * Copyright (C) Dmitry Volyntsev
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
#include <njs_error.h>
#include <njs_object.h>
#include <njs_object_hash.h>
#include <njs_string.h>
#include <njs_function.h>
#include <stdio.h>
#include <string.h>
#include <math.h>


static const njs_value_t  njs_error_message_string = njs_string("message");
static const njs_value_t  njs_error_name_string = njs_string("name");


void
njs_exception_error_create(njs_vm_t *vm, njs_value_type_t type,
    const char* fmt, ...)
{
    size_t        size;
    va_list       args;
    nxt_int_t     ret;
    njs_value_t   string;
    njs_object_t  *error;

    static char  buf[256];

    if (fmt != NULL) {
        va_start(args, fmt);
        size = vsnprintf(buf, sizeof(buf), fmt, args);
        va_end(args);

    } else {
        size = 0;
    }

    ret = njs_string_new(vm, &string, (const u_char *) buf, size, size);
    if (nxt_slow_path(ret != NXT_OK)) {
        goto memory_error;
    }

    error = njs_error_alloc(vm, type, NULL, &string);
    if (nxt_slow_path(error == NULL)) {
        goto memory_error;
    }

    vm->retval.data.u.object = error;
    vm->retval.type = type;
    vm->retval.data.truth = 1;

    return;

memory_error:

    njs_exception_memory_error(vm);
}


nxt_noinline njs_object_t *
njs_error_alloc(njs_vm_t *vm, njs_value_type_t type, const njs_value_t *name,
    const njs_value_t *message)
{
    nxt_int_t           ret;
    njs_object_t        *error;
    njs_object_prop_t   *prop;
    nxt_lvlhsh_query_t  lhq;

    error = nxt_mem_cache_alloc(vm->mem_cache_pool, sizeof(njs_object_t));
    if (nxt_slow_path(error == NULL)) {
        return NULL;
    }

    nxt_lvlhsh_init(&error->hash);
    nxt_lvlhsh_init(&error->shared_hash);
    error->type = type;
    error->shared = 0;
    error->extensible = 1;
    error->__proto__ = &vm->prototypes[njs_error_prototype_index(type)].object;

    lhq.replace = 0;
    lhq.pool = vm->mem_cache_pool;

    if (name != NULL) {
        lhq.key = nxt_string_value("name");
        lhq.key_hash = NJS_NAME_HASH;
        lhq.proto = &njs_object_hash_proto;

        prop = njs_object_prop_alloc(vm, &njs_error_name_string, name, 1);
        if (nxt_slow_path(prop == NULL)) {
            return NULL;
        }

        lhq.value = prop;

        ret = nxt_lvlhsh_insert(&error->hash, &lhq);
        if (nxt_slow_path(ret != NXT_OK)) {
            return NULL;
        }
    }

    if (message!= NULL) {
        lhq.key = nxt_string_value("message");
        lhq.key_hash = NJS_MESSAGE_HASH;
        lhq.proto = &njs_object_hash_proto;

        prop = njs_object_prop_alloc(vm, &njs_error_message_string, message, 1);
        if (nxt_slow_path(prop == NULL)) {
            return NULL;
        }

        prop->enumerable = 0;

        lhq.value = prop;

        ret = nxt_lvlhsh_insert(&error->hash, &lhq);
        if (nxt_slow_path(ret != NXT_OK)) {
            return NULL;
        }
    }

    return error;
}


static njs_ret_t
njs_error_create(njs_vm_t *vm, njs_value_t *args, nxt_uint_t nargs,
    njs_value_type_t type)
{
    njs_object_t       *error;
    const njs_value_t  *value;

    if (nargs == 1) {
        value = &njs_string_empty;

    } else {
        value = &args[1];
    }

    error = njs_error_alloc(vm, type, NULL, value);
    if (nxt_slow_path(error == NULL)) {
        njs_exception_memory_error(vm);
        return NXT_ERROR;
    }

    vm->retval.data.u.object = error;
    vm->retval.type = type;
    vm->retval.data.truth = 1;

    return NXT_OK;
}


njs_ret_t
njs_error_constructor(njs_vm_t *vm, njs_value_t *args, nxt_uint_t nargs,
    njs_index_t unused)
{
    return njs_error_create(vm, args, nargs, NJS_OBJECT_ERROR);
}


static const njs_object_prop_t  njs_error_constructor_properties[] =
{
    /* Error.name == "Error". */
    {
        .type = NJS_PROPERTY,
        .name = njs_string("name"),
        .value = njs_string("Error"),
    },

    /* Error.length == 1. */
    {
        .type = NJS_PROPERTY,
        .name = njs_string("length"),
        .value = njs_value(NJS_NUMBER, 1, 1.0),
    },

    /* Error.prototype. */
    {
        .type = NJS_NATIVE_GETTER,
        .name = njs_string("prototype"),
        .value = njs_native_getter(njs_object_prototype_create),
    },
};


const njs_object_init_t  njs_error_constructor_init = {
    nxt_string("Error"),
    njs_error_constructor_properties,
    nxt_nitems(njs_error_constructor_properties),
};


njs_ret_t
njs_eval_error_constructor(njs_vm_t *vm, njs_value_t *args, nxt_uint_t nargs,
    njs_index_t unused)
{
    return njs_error_create(vm, args, nargs, NJS_OBJECT_EVAL_ERROR);
}


static const njs_object_prop_t  njs_eval_error_constructor_properties[] =
{
    /* EvalError.name == "EvalError". */
    {
        .type = NJS_PROPERTY,
        .name = njs_string("name"),
        .value = njs_string("EvalError"),
    },

    /* EvalError.length == 1. */
    {
        .type = NJS_PROPERTY,
        .name = njs_string("length"),
        .value = njs_value(NJS_NUMBER, 1, 1.0),
    },

    /* EvalError.prototype. */
    {
        .type = NJS_NATIVE_GETTER,
        .name = njs_string("prototype"),
        .value = njs_native_getter(njs_object_prototype_create),
    },
};


const njs_object_init_t  njs_eval_error_constructor_init = {
    nxt_string("EvalError"),
    njs_eval_error_constructor_properties,
    nxt_nitems(njs_eval_error_constructor_properties),
};


njs_ret_t
njs_internal_error_constructor(njs_vm_t *vm, njs_value_t *args,
    nxt_uint_t nargs, njs_index_t unused)
{
    return njs_error_create(vm, args, nargs, NJS_OBJECT_INTERNAL_ERROR);
}


static const njs_object_prop_t  njs_internal_error_constructor_properties[] =
{
    /* InternalError.name == "InternalError". */
    {
        .type = NJS_PROPERTY,
        .name = njs_string("name"),
        .value = njs_string("InternalError"),
    },

    /* InternalError.length == 1. */
    {
        .type = NJS_PROPERTY,
        .name = njs_string("length"),
        .value = njs_value(NJS_NUMBER, 1, 1.0),
    },

    /* InternalError.prototype. */
    {
        .type = NJS_NATIVE_GETTER,
        .name = njs_string("prototype"),
        .value = njs_native_getter(njs_object_prototype_create),
    },
};


const njs_object_init_t  njs_internal_error_constructor_init = {
    nxt_string("InternalError"),
    njs_internal_error_constructor_properties,
    nxt_nitems(njs_internal_error_constructor_properties),
};


njs_ret_t
njs_range_error_constructor(njs_vm_t *vm, njs_value_t *args,
    nxt_uint_t nargs, njs_index_t unused)
{
    return njs_error_create(vm, args, nargs, NJS_OBJECT_RANGE_ERROR);
}


static const njs_object_prop_t  njs_range_error_constructor_properties[] =
{
    /* RangeError.name == "RangeError". */
    {
        .type = NJS_PROPERTY,
        .name = njs_string("name"),
        .value = njs_string("RangeError"),
    },

    /* RangeError.length == 1. */
    {
        .type = NJS_PROPERTY,
        .name = njs_string("length"),
        .value = njs_value(NJS_NUMBER, 1, 1.0),
    },

    /* RangeError.prototype. */
    {
        .type = NJS_NATIVE_GETTER,
        .name = njs_string("prototype"),
        .value = njs_native_getter(njs_object_prototype_create),
    },
};


const njs_object_init_t  njs_range_error_constructor_init = {
    nxt_string("RangeError"),
    njs_range_error_constructor_properties,
    nxt_nitems(njs_range_error_constructor_properties),
};


njs_ret_t
njs_ref_error_constructor(njs_vm_t *vm, njs_value_t *args,
    nxt_uint_t nargs, njs_index_t unused)
{
    return njs_error_create(vm, args, nargs, NJS_OBJECT_REF_ERROR);
}


static const njs_object_prop_t  njs_ref_error_constructor_properties[] =
{
    /* ReferenceError.name == "ReferenceError". */
    {
        .type = NJS_PROPERTY,
        .name = njs_string("name"),
        .value = njs_string("ReferenceError"),
    },

    /* ReferenceError.length == 1. */
    {
        .type = NJS_PROPERTY,
        .name = njs_string("length"),
        .value = njs_value(NJS_NUMBER, 1, 1.0),
    },

    /* ReferenceError.prototype. */
    {
        .type = NJS_NATIVE_GETTER,
        .name = njs_string("prototype"),
        .value = njs_native_getter(njs_object_prototype_create),
    },
};


const njs_object_init_t  njs_ref_error_constructor_init = {
    nxt_string("ReferenceError"),
    njs_ref_error_constructor_properties,
    nxt_nitems(njs_ref_error_constructor_properties),
};


njs_ret_t
njs_syntax_error_constructor(njs_vm_t *vm, njs_value_t *args,
    nxt_uint_t nargs, njs_index_t unused)
{
    return njs_error_create(vm, args, nargs, NJS_OBJECT_SYNTAX_ERROR);
}


static const njs_object_prop_t  njs_syntax_error_constructor_properties[] =
{
    /* SyntaxError.name == "SyntaxError". */
    {
        .type = NJS_PROPERTY,
        .name = njs_string("name"),
        .value = njs_string("SyntaxError"),
    },

    /* SyntaxError.length == 1. */
    {
        .type = NJS_PROPERTY,
        .name = njs_string("length"),
        .value = njs_value(NJS_NUMBER, 1, 1.0),
    },

    /* SyntaxError.prototype. */
    {
        .type = NJS_NATIVE_GETTER,
        .name = njs_string("prototype"),
        .value = njs_native_getter(njs_object_prototype_create),
    },
};


const njs_object_init_t  njs_syntax_error_constructor_init = {
    nxt_string("SyntaxError"),
    njs_syntax_error_constructor_properties,
    nxt_nitems(njs_syntax_error_constructor_properties),
};


njs_ret_t
njs_type_error_constructor(njs_vm_t *vm, njs_value_t *args,
    nxt_uint_t nargs, njs_index_t unused)
{
    return njs_error_create(vm, args, nargs, NJS_OBJECT_TYPE_ERROR);
}


static const njs_object_prop_t  njs_type_error_constructor_properties[] =
{
    /* TypeError.name == "TypeError". */
    {
        .type = NJS_PROPERTY,
        .name = njs_string("name"),
        .value = njs_string("TypeError"),
    },

    /* TypeError.length == 1. */
    {
        .type = NJS_PROPERTY,
        .name = njs_string("length"),
        .value = njs_value(NJS_NUMBER, 1, 1.0),
    },

    /* TypeError.prototype. */
    {
        .type = NJS_NATIVE_GETTER,
        .name = njs_string("prototype"),
        .value = njs_native_getter(njs_object_prototype_create),
    },
};


const njs_object_init_t  njs_type_error_constructor_init = {
    nxt_string("TypeError"),
    njs_type_error_constructor_properties,
    nxt_nitems(njs_type_error_constructor_properties),
};


njs_ret_t
njs_uri_error_constructor(njs_vm_t *vm, njs_value_t *args,
    nxt_uint_t nargs, njs_index_t unused)
{
    return njs_error_create(vm, args, nargs, NJS_OBJECT_URI_ERROR);
}


static const njs_object_prop_t  njs_uri_error_constructor_properties[] =
{
    /* URIError.name == "URIError". */
    {
        .type = NJS_PROPERTY,
        .name = njs_string("name"),
        .value = njs_string("URIError"),
    },

    /* URIError.length == 1. */
    {
        .type = NJS_PROPERTY,
        .name = njs_string("length"),
        .value = njs_value(NJS_NUMBER, 1, 1.0),
    },

    /* URIError.prototype. */
    {
        .type = NJS_NATIVE_GETTER,
        .name = njs_string("prototype"),
        .value = njs_native_getter(njs_object_prototype_create),
    },
};


const njs_object_init_t  njs_uri_error_constructor_init = {
    nxt_string("URIError"),
    njs_uri_error_constructor_properties,
    nxt_nitems(njs_uri_error_constructor_properties),
};


static void
njs_set_memory_error(njs_vm_t *vm)
{
    njs_object_t            *object;
    njs_object_prototype_t  *prototypes;

    prototypes = vm->prototypes;
    object = &vm->memory_error_object;

    nxt_lvlhsh_init(&object->hash);
    nxt_lvlhsh_init(&object->shared_hash);
    object->__proto__ = &prototypes[NJS_PROTOTYPE_INTERNAL_ERROR].object;
    object->type = NJS_OBJECT_INTERNAL_ERROR;
    object->shared = 1;

    /*
     * Marking it nonextensible to differentiate
     * it from ordinary internal errors.
     */
    object->extensible = 0;

    vm->retval.data.type = NJS_OBJECT_INTERNAL_ERROR;
    vm->retval.data.truth = 1;
    vm->retval.data.u.number = NAN;
    vm->retval.data.u.object = object;
}


void
njs_exception_memory_error(njs_vm_t *vm)
{
    njs_set_memory_error(vm);
}


njs_ret_t
njs_memory_error_constructor(njs_vm_t *vm, njs_value_t *args,
    nxt_uint_t nargs, njs_index_t unused)
{
    njs_set_memory_error(vm);

    return NXT_OK;
}


static njs_ret_t
njs_memory_error_prototype_create(njs_vm_t *vm, njs_value_t *value)
{
    int32_t         index;
    njs_value_t     *proto;
    njs_function_t  *function;

    /* MemoryError has no its own prototype. */

    index = NJS_PROTOTYPE_INTERNAL_ERROR;

    function = value->data.u.function;
    proto = njs_property_prototype_create(vm, &function->object.hash,
                                          &vm->prototypes[index].object);
    if (proto == NULL) {
        proto = (njs_value_t *) &njs_value_void;
    }

    vm->retval = *proto;

    return NXT_OK;
}


static const njs_object_prop_t  njs_memory_error_constructor_properties[] =
{
    /* MemoryError.name == "MemoryError". */
    {
        .type = NJS_PROPERTY,
        .name = njs_string("name"),
        .value = njs_string("MemoryError"),
    },

    /* MemoryError.length == 1. */
    {
        .type = NJS_PROPERTY,
        .name = njs_string("length"),
        .value = njs_value(NJS_NUMBER, 1, 1.0),
    },

    /* MemoryError.prototype. */
    {
        .type = NJS_NATIVE_GETTER,
        .name = njs_string("prototype"),
        .value = njs_native_getter(njs_memory_error_prototype_create),
    },
};


const njs_object_init_t  njs_memory_error_constructor_init = {
    nxt_string("MemoryError"),
    njs_memory_error_constructor_properties,
    nxt_nitems(njs_memory_error_constructor_properties),
};


static njs_ret_t
njs_error_prototype_value_of(njs_vm_t *vm, njs_value_t *args, nxt_uint_t nargs,
    njs_index_t unused)
{
    vm->retval = args[0];

    return NXT_OK;
}


static njs_ret_t
njs_error_prototype_to_string(njs_vm_t *vm, njs_value_t *args, nxt_uint_t nargs,
    njs_index_t unused)
{
    size_t              size;
    u_char              *p;
    nxt_str_t           name, message;
    const njs_value_t   *name_value, *message_value;
    njs_object_prop_t   *prop;
    nxt_lvlhsh_query_t  lhq;

    static const njs_value_t  default_name = njs_string("Error");

    if (nargs < 1 || !njs_is_object(&args[0])) {
        njs_exception_type_error(vm, NULL, NULL);
        return NXT_ERROR;
    }

    lhq.key_hash = NJS_NAME_HASH;
    lhq.key = nxt_string_value("name");
    lhq.proto = &njs_object_hash_proto;

    prop = njs_object_property(vm, args[0].data.u.object, &lhq);

    if (prop != NULL) {
        name_value = &prop->value;

    } else {
        name_value = &default_name;
    }

    njs_string_get(name_value, &name);

    lhq.key_hash = NJS_MESSAGE_HASH;
    lhq.key = nxt_string_value("message");

    prop = njs_object_property(vm, args[0].data.u.object, &lhq);

    if (prop != NULL) {
        message_value = &prop->value;

    } else {
        message_value = &njs_string_empty;
    }

    njs_string_get(message_value, &message);

    if (name.length == 0) {
        vm->retval = *message_value;
        return NJS_OK;
    }

    if (message.length == 0) {
        vm->retval = *name_value;
        return NJS_OK;
    }

    size = name.length + message.length + 2;

    p = njs_string_alloc(vm, &vm->retval, size, size);

    if (nxt_fast_path(p != NULL)) {
        p = nxt_cpymem(p, name.start, name.length);
        *p++ = ':';
        *p++ = ' ';
        memcpy(p, message.start, message.length);

        return NJS_OK;
    }

    njs_exception_memory_error(vm);
    return NJS_ERROR;
}


static const njs_object_prop_t  njs_error_prototype_properties[] =
{
    {
        .type = NJS_PROPERTY,
        .name = njs_string("name"),
        .value = njs_string("Error"),
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("message"),
        .value = njs_string(""),
    },

    {
        .type = NJS_METHOD,
        .name = njs_string("valueOf"),
        .value = njs_native_function(njs_error_prototype_value_of, 0, 0),
    },

    {
        .type = NJS_METHOD,
        .name = njs_string("toString"),
        .value = njs_native_function(njs_error_prototype_to_string, 0, 0),
    },
};


const njs_object_init_t  njs_error_prototype_init = {
    nxt_string("Error"),
    njs_error_prototype_properties,
    nxt_nitems(njs_error_prototype_properties),
};


static const njs_object_prop_t  njs_eval_error_prototype_properties[] =
{
    {
        .type = NJS_PROPERTY,
        .name = njs_string("name"),
        .value = njs_string("EvalError"),
    },
};


const njs_object_init_t  njs_eval_error_prototype_init = {
    nxt_string("EvalError"),
    njs_eval_error_prototype_properties,
    nxt_nitems(njs_eval_error_prototype_properties),
};


static njs_ret_t
njs_internal_error_prototype_to_string(njs_vm_t *vm, njs_value_t *args,
    nxt_uint_t nargs, njs_index_t unused)
{
    if (nargs >= 1 && njs_is_object(&args[0])) {

        /* MemoryError is a nonextensible internal error. */
        if (!args[0].data.u.object->extensible) {
            static const njs_value_t name = njs_string("MemoryError");

            vm->retval = name;

            return NJS_OK;
        }
    }

    return njs_error_prototype_to_string(vm, args, nargs, unused);
}


static const njs_object_prop_t  njs_internal_error_prototype_properties[] =
{
    {
        .type = NJS_PROPERTY,
        .name = njs_string("name"),
        .value = njs_string("InternalError"),
    },

    {
        .type = NJS_METHOD,
        .name = njs_string("toString"),
        .value = njs_native_function(njs_internal_error_prototype_to_string,
                                     0, 0),
    },
};


const njs_object_init_t  njs_internal_error_prototype_init = {
    nxt_string("InternalError"),
    njs_internal_error_prototype_properties,
    nxt_nitems(njs_internal_error_prototype_properties),
};


static const njs_object_prop_t  njs_range_error_prototype_properties[] =
{
    {
        .type = NJS_PROPERTY,
        .name = njs_string("name"),
        .value = njs_string("RangeError"),
    },
};


const njs_object_init_t  njs_range_error_prototype_init = {
    nxt_string("RangeError"),
    njs_range_error_prototype_properties,
    nxt_nitems(njs_range_error_prototype_properties),
};


static const njs_object_prop_t  njs_ref_error_prototype_properties[] =
{
    {
        .type = NJS_PROPERTY,
        .name = njs_string("name"),
        .value = njs_string("ReferenceError"),
    },
};


const njs_object_init_t  njs_ref_error_prototype_init = {
    nxt_string("ReferenceError"),
    njs_ref_error_prototype_properties,
    nxt_nitems(njs_ref_error_prototype_properties),
};


static const njs_object_prop_t  njs_syntax_error_prototype_properties[] =
{
    {
        .type = NJS_PROPERTY,
        .name = njs_string("name"),
        .value = njs_string("SyntaxError"),
    },
};


const njs_object_init_t  njs_syntax_error_prototype_init = {
    nxt_string("SyntaxError"),
    njs_syntax_error_prototype_properties,
    nxt_nitems(njs_syntax_error_prototype_properties),
};


static const njs_object_prop_t  njs_type_error_prototype_properties[] =
{
    {
        .type = NJS_PROPERTY,
        .name = njs_string("name"),
        .value = njs_string("TypeError"),
    },
};


const njs_object_init_t  njs_type_error_prototype_init = {
    nxt_string("TypeError"),
    njs_type_error_prototype_properties,
    nxt_nitems(njs_type_error_prototype_properties),
};


static const njs_object_prop_t  njs_uri_error_prototype_properties[] =
{
    {
        .type = NJS_PROPERTY,
        .name = njs_string("name"),
        .value = njs_string("URIError"),
    },
};


const njs_object_init_t  njs_uri_error_prototype_init = {
    nxt_string("URIError"),
    njs_uri_error_prototype_properties,
    nxt_nitems(njs_uri_error_prototype_properties),
};
