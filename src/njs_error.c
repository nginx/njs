
/*
 * Copyright (C) Dmitry Volyntsev
 * Copyright (C) NGINX, Inc.
 */


#include <njs_main.h>


static const njs_value_t  njs_error_message_string = njs_string("message");
static const njs_value_t  njs_error_name_string = njs_string("name");


void
njs_error_new(njs_vm_t *vm, njs_value_t *dst, njs_value_type_t type,
    u_char *start, size_t size)
{
    ssize_t        length;
    njs_int_t     ret;
    njs_value_t   string;
    njs_object_t  *error;

    length = njs_utf8_length(start, size);
    if (njs_slow_path(length < 0)) {
        length = 0;
    }

    ret = njs_string_new(vm, &string, start, size, length);
    if (njs_slow_path(ret != NJS_OK)) {
        return;
    }

    error = njs_error_alloc(vm, type, NULL, &string);
    if (njs_slow_path(error == NULL)) {
        return;
    }

    njs_set_type_object(dst, error, type);
}


void
njs_error_fmt_new(njs_vm_t *vm, njs_value_t *dst, njs_value_type_t type,
    const char *fmt, ...)
{
    va_list  args;
    u_char   buf[NJS_MAX_ERROR_STR], *p;

    p = buf;

    if (fmt != NULL) {
        va_start(args, fmt);
        p = njs_vsprintf(buf, buf + sizeof(buf), fmt, args);
        va_end(args);
    }

    njs_error_new(vm, dst, type, buf, p - buf);
}


njs_object_t *
njs_error_alloc(njs_vm_t *vm, njs_value_type_t type, const njs_value_t *name,
    const njs_value_t *message)
{
    njs_int_t           ret;
    njs_object_t        *error;
    njs_object_prop_t   *prop;
    njs_lvlhsh_query_t  lhq;

    error = njs_mp_alloc(vm->mem_pool, sizeof(njs_object_t));
    if (njs_slow_path(error == NULL)) {
        goto memory_error;
    }

    njs_lvlhsh_init(&error->hash);
    njs_lvlhsh_init(&error->shared_hash);
    error->type = type;
    error->shared = 0;
    error->extensible = 1;
    error->__proto__ = &vm->prototypes[njs_error_prototype_index(type)].object;

    lhq.replace = 0;
    lhq.pool = vm->mem_pool;

    if (name != NULL) {
        lhq.key = njs_str_value("name");
        lhq.key_hash = NJS_NAME_HASH;
        lhq.proto = &njs_object_hash_proto;

        prop = njs_object_prop_alloc(vm, &njs_error_name_string, name, 1);
        if (njs_slow_path(prop == NULL)) {
            goto memory_error;
        }

        lhq.value = prop;

        ret = njs_lvlhsh_insert(&error->hash, &lhq);
        if (njs_slow_path(ret != NJS_OK)) {
            njs_internal_error(vm, "lvlhsh insert failed");
            return NULL;
        }
    }

    if (message!= NULL) {
        lhq.key = njs_str_value("message");
        lhq.key_hash = NJS_MESSAGE_HASH;
        lhq.proto = &njs_object_hash_proto;

        prop = njs_object_prop_alloc(vm, &njs_error_message_string, message, 1);
        if (njs_slow_path(prop == NULL)) {
            goto memory_error;
        }

        prop->enumerable = 0;

        lhq.value = prop;

        ret = njs_lvlhsh_insert(&error->hash, &lhq);
        if (njs_slow_path(ret != NJS_OK)) {
            njs_internal_error(vm, "lvlhsh insert failed");
            return NULL;
        }
    }

    return error;

memory_error:

    njs_memory_error(vm);

    return NULL;
}


static njs_int_t
njs_error_create(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_value_type_t type)
{
    njs_object_t       *error;
    const njs_value_t  *value;

    value = njs_arg(args, nargs, 1);

    error = njs_error_alloc(vm, type, NULL,
                            njs_is_defined(value) ? value : NULL);
    if (njs_slow_path(error == NULL)) {
        return NJS_ERROR;
    }

    njs_set_type_object(&vm->retval, error, type);

    return NJS_OK;
}


njs_int_t
njs_error_constructor(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
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
        .configurable = 1,
    },

    /* Error.length == 1. */
    {
        .type = NJS_PROPERTY,
        .name = njs_string("length"),
        .value = njs_value(NJS_NUMBER, 1, 1.0),
        .configurable = 1,
    },

    /* Error.prototype. */
    {
        .type = NJS_PROPERTY_HANDLER,
        .name = njs_string("prototype"),
        .value = njs_prop_handler(njs_object_prototype_create),
    },
};


const njs_object_init_t  njs_error_constructor_init = {
    njs_str("Error"),
    njs_error_constructor_properties,
    njs_nitems(njs_error_constructor_properties),
};


njs_int_t
njs_eval_error_constructor(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
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
        .configurable = 1,
    },

    /* EvalError.length == 1. */
    {
        .type = NJS_PROPERTY,
        .name = njs_string("length"),
        .value = njs_value(NJS_NUMBER, 1, 1.0),
        .configurable = 1,
    },

    /* EvalError.prototype. */
    {
        .type = NJS_PROPERTY_HANDLER,
        .name = njs_string("prototype"),
        .value = njs_prop_handler(njs_object_prototype_create),
    },
};


const njs_object_init_t  njs_eval_error_constructor_init = {
    njs_str("EvalError"),
    njs_eval_error_constructor_properties,
    njs_nitems(njs_eval_error_constructor_properties),
};


njs_int_t
njs_internal_error_constructor(njs_vm_t *vm, njs_value_t *args,
    njs_uint_t nargs, njs_index_t unused)
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
        .configurable = 1,
    },

    /* InternalError.length == 1. */
    {
        .type = NJS_PROPERTY,
        .name = njs_string("length"),
        .value = njs_value(NJS_NUMBER, 1, 1.0),
        .configurable = 1,
    },

    /* InternalError.prototype. */
    {
        .type = NJS_PROPERTY_HANDLER,
        .name = njs_string("prototype"),
        .value = njs_prop_handler(njs_object_prototype_create),
    },
};


const njs_object_init_t  njs_internal_error_constructor_init = {
    njs_str("InternalError"),
    njs_internal_error_constructor_properties,
    njs_nitems(njs_internal_error_constructor_properties),
};


njs_int_t
njs_range_error_constructor(njs_vm_t *vm, njs_value_t *args,
    njs_uint_t nargs, njs_index_t unused)
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
        .configurable = 1,
    },

    /* RangeError.length == 1. */
    {
        .type = NJS_PROPERTY,
        .name = njs_string("length"),
        .value = njs_value(NJS_NUMBER, 1, 1.0),
        .configurable = 1,
    },

    /* RangeError.prototype. */
    {
        .type = NJS_PROPERTY_HANDLER,
        .name = njs_string("prototype"),
        .value = njs_prop_handler(njs_object_prototype_create),
    },
};


const njs_object_init_t  njs_range_error_constructor_init = {
    njs_str("RangeError"),
    njs_range_error_constructor_properties,
    njs_nitems(njs_range_error_constructor_properties),
};


njs_int_t
njs_reference_error_constructor(njs_vm_t *vm, njs_value_t *args,
    njs_uint_t nargs, njs_index_t unused)
{
    return njs_error_create(vm, args, nargs, NJS_OBJECT_REF_ERROR);
}


static const njs_object_prop_t  njs_reference_error_constructor_properties[] =
{
    /* ReferenceError.name == "ReferenceError". */
    {
        .type = NJS_PROPERTY,
        .name = njs_string("name"),
        .value = njs_string("ReferenceError"),
        .configurable = 1,
    },

    /* ReferenceError.length == 1. */
    {
        .type = NJS_PROPERTY,
        .name = njs_string("length"),
        .value = njs_value(NJS_NUMBER, 1, 1.0),
        .configurable = 1,
    },

    /* ReferenceError.prototype. */
    {
        .type = NJS_PROPERTY_HANDLER,
        .name = njs_string("prototype"),
        .value = njs_prop_handler(njs_object_prototype_create),
    },
};


const njs_object_init_t  njs_reference_error_constructor_init = {
    njs_str("ReferenceError"),
    njs_reference_error_constructor_properties,
    njs_nitems(njs_reference_error_constructor_properties),
};


njs_int_t
njs_syntax_error_constructor(njs_vm_t *vm, njs_value_t *args,
    njs_uint_t nargs, njs_index_t unused)
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
        .configurable = 1,
    },

    /* SyntaxError.length == 1. */
    {
        .type = NJS_PROPERTY,
        .name = njs_string("length"),
        .value = njs_value(NJS_NUMBER, 1, 1.0),
        .configurable = 1,
    },

    /* SyntaxError.prototype. */
    {
        .type = NJS_PROPERTY_HANDLER,
        .name = njs_string("prototype"),
        .value = njs_prop_handler(njs_object_prototype_create),
    },
};


const njs_object_init_t  njs_syntax_error_constructor_init = {
    njs_str("SyntaxError"),
    njs_syntax_error_constructor_properties,
    njs_nitems(njs_syntax_error_constructor_properties),
};


njs_int_t
njs_type_error_constructor(njs_vm_t *vm, njs_value_t *args,
    njs_uint_t nargs, njs_index_t unused)
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
        .configurable = 1,
    },

    /* TypeError.length == 1. */
    {
        .type = NJS_PROPERTY,
        .name = njs_string("length"),
        .value = njs_value(NJS_NUMBER, 1, 1.0),
        .configurable = 1,
    },

    /* TypeError.prototype. */
    {
        .type = NJS_PROPERTY_HANDLER,
        .name = njs_string("prototype"),
        .value = njs_prop_handler(njs_object_prototype_create),
    },
};


const njs_object_init_t  njs_type_error_constructor_init = {
    njs_str("TypeError"),
    njs_type_error_constructor_properties,
    njs_nitems(njs_type_error_constructor_properties),
};


njs_int_t
njs_uri_error_constructor(njs_vm_t *vm, njs_value_t *args,
    njs_uint_t nargs, njs_index_t unused)
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
        .configurable = 1,
    },

    /* URIError.length == 1. */
    {
        .type = NJS_PROPERTY,
        .name = njs_string("length"),
        .value = njs_value(NJS_NUMBER, 1, 1.0),
        .configurable = 1,
    },

    /* URIError.prototype. */
    {
        .type = NJS_PROPERTY_HANDLER,
        .name = njs_string("prototype"),
        .value = njs_prop_handler(njs_object_prototype_create),
    },
};


const njs_object_init_t  njs_uri_error_constructor_init = {
    njs_str("URIError"),
    njs_uri_error_constructor_properties,
    njs_nitems(njs_uri_error_constructor_properties),
};


void
njs_memory_error_set(njs_vm_t *vm, njs_value_t *value)
{
    njs_object_t            *object;
    njs_object_prototype_t  *prototypes;

    prototypes = vm->prototypes;
    object = &vm->memory_error_object;

    njs_lvlhsh_init(&object->hash);
    njs_lvlhsh_init(&object->shared_hash);
    object->__proto__ = &prototypes[NJS_PROTOTYPE_INTERNAL_ERROR].object;
    object->type = NJS_OBJECT_INTERNAL_ERROR;
    object->shared = 1;

    /*
     * Marking it nonextensible to differentiate
     * it from ordinary internal errors.
     */
    object->extensible = 0;

    njs_set_type_object(value, object, NJS_OBJECT_INTERNAL_ERROR);
}


void
njs_memory_error(njs_vm_t *vm)
{
    njs_memory_error_set(vm, &vm->retval);
}


njs_int_t
njs_memory_error_constructor(njs_vm_t *vm, njs_value_t *args,
    njs_uint_t nargs, njs_index_t unused)
{
    njs_memory_error_set(vm, &vm->retval);

    return NJS_OK;
}


static njs_int_t
njs_memory_error_prototype_create(njs_vm_t *vm, njs_value_t *value,
    njs_value_t *setval, njs_value_t *retval)
{
    int32_t            index;
    njs_function_t     *function;
    const njs_value_t  *proto;

    /* MemoryError has no its own prototype. */

    index = NJS_PROTOTYPE_INTERNAL_ERROR;

    function = njs_function(value);
    proto = njs_property_prototype_create(vm, &function->object.hash,
                                          &vm->prototypes[index].object);
    if (proto == NULL) {
        proto = &njs_value_undefined;
    }

    *retval = *proto;

    return NJS_OK;
}


static const njs_object_prop_t  njs_memory_error_constructor_properties[] =
{
    /* MemoryError.name == "MemoryError". */
    {
        .type = NJS_PROPERTY,
        .name = njs_string("name"),
        .value = njs_string("MemoryError"),
        .configurable = 1,
    },

    /* MemoryError.length == 1. */
    {
        .type = NJS_PROPERTY,
        .name = njs_string("length"),
        .value = njs_value(NJS_NUMBER, 1, 1.0),
        .configurable = 1,
    },

    /* MemoryError.prototype. */
    {
        .type = NJS_PROPERTY_HANDLER,
        .name = njs_string("prototype"),
        .value = njs_prop_handler(njs_memory_error_prototype_create),
    },
};


const njs_object_init_t  njs_memory_error_constructor_init = {
    njs_str("MemoryError"),
    njs_memory_error_constructor_properties,
    njs_nitems(njs_memory_error_constructor_properties),
};


static njs_int_t
njs_error_prototype_value_of(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t unused)
{
    vm->retval = args[0];

    return NJS_OK;
}


static njs_int_t
njs_error_prototype_to_string(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t unused)
{
    if (nargs < 1 || !njs_is_object(&args[0])) {
        njs_type_error(vm, "\"this\" argument is not an object");
        return NJS_ERROR;
    }

    return njs_error_to_string(vm, &vm->retval, &args[0]);
}


njs_int_t
njs_error_to_string(njs_vm_t *vm, njs_value_t *retval, const njs_value_t *error)
{
    size_t              size;
    u_char              *p;
    njs_str_t           name, message;
    const njs_value_t   *name_value, *message_value;
    njs_object_prop_t   *prop;
    njs_lvlhsh_query_t  lhq;

    static const njs_value_t  default_name = njs_string("Error");

    lhq.key_hash = NJS_NAME_HASH;
    lhq.key = njs_str_value("name");
    lhq.proto = &njs_object_hash_proto;

    prop = njs_object_property(vm, njs_object(error), &lhq);

    if (prop != NULL) {
        name_value = &prop->value;

    } else {
        name_value = &default_name;
    }

    njs_string_get(name_value, &name);

    lhq.key_hash = NJS_MESSAGE_HASH;
    lhq.key = njs_str_value("message");

    prop = njs_object_property(vm, njs_object(error), &lhq);

    if (prop != NULL) {
        message_value = &prop->value;

    } else {
        message_value = &njs_string_empty;
    }

    njs_string_get(message_value, &message);

    if (name.length == 0) {
        *retval = *message_value;
        return NJS_OK;
    }

    if (message.length == 0) {
        *retval = *name_value;
        return NJS_OK;
    }

    size = name.length + message.length + 2;

    p = njs_string_alloc(vm, retval, size, size);

    if (njs_fast_path(p != NULL)) {
        p = njs_cpymem(p, name.start, name.length);
        *p++ = ':';
        *p++ = ' ';
        memcpy(p, message.start, message.length);

        return NJS_OK;
    }

    njs_memory_error(vm);

    return NJS_ERROR;
}


static const njs_object_prop_t  njs_error_prototype_properties[] =
{
    {
        .type = NJS_PROPERTY,
        .name = njs_string("name"),
        .value = njs_string("Error"),
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
        .type = NJS_PROPERTY,
        .name = njs_string("message"),
        .value = njs_string(""),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("valueOf"),
        .value = njs_native_function(njs_error_prototype_value_of, 0),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("toString"),
        .value = njs_native_function(njs_error_prototype_to_string, 0),
        .writable = 1,
        .configurable = 1,
    },
};


const njs_object_init_t  njs_error_prototype_init = {
    njs_str("Error"),
    njs_error_prototype_properties,
    njs_nitems(njs_error_prototype_properties),
};


static const njs_object_prop_t  njs_eval_error_prototype_properties[] =
{
    {
        .type = NJS_PROPERTY,
        .name = njs_string("name"),
        .value = njs_string("EvalError"),
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
};


const njs_object_init_t  njs_eval_error_prototype_init = {
    njs_str("EvalError"),
    njs_eval_error_prototype_properties,
    njs_nitems(njs_eval_error_prototype_properties),
};


static njs_int_t
njs_internal_error_prototype_to_string(njs_vm_t *vm, njs_value_t *args,
    njs_uint_t nargs, njs_index_t unused)
{
    if (nargs >= 1 && njs_is_object(&args[0])) {

        /* MemoryError is a nonextensible internal error. */
        if (!njs_object(&args[0])->extensible) {
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
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("toString"),
        .value = njs_native_function(njs_internal_error_prototype_to_string, 0),
        .writable = 1,
        .configurable = 1,
    },
};


const njs_object_init_t  njs_internal_error_prototype_init = {
    njs_str("InternalError"),
    njs_internal_error_prototype_properties,
    njs_nitems(njs_internal_error_prototype_properties),
};


static const njs_object_prop_t  njs_range_error_prototype_properties[] =
{
    {
        .type = NJS_PROPERTY,
        .name = njs_string("name"),
        .value = njs_string("RangeError"),
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
};


const njs_object_init_t  njs_range_error_prototype_init = {
    njs_str("RangeError"),
    njs_range_error_prototype_properties,
    njs_nitems(njs_range_error_prototype_properties),
};


static const njs_object_prop_t  njs_reference_error_prototype_properties[] =
{
    {
        .type = NJS_PROPERTY,
        .name = njs_string("name"),
        .value = njs_string("ReferenceError"),
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
};


const njs_object_init_t  njs_reference_error_prototype_init = {
    njs_str("ReferenceError"),
    njs_reference_error_prototype_properties,
    njs_nitems(njs_reference_error_prototype_properties),
};


static const njs_object_prop_t  njs_syntax_error_prototype_properties[] =
{
    {
        .type = NJS_PROPERTY,
        .name = njs_string("name"),
        .value = njs_string("SyntaxError"),
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
};


const njs_object_init_t  njs_syntax_error_prototype_init = {
    njs_str("SyntaxError"),
    njs_syntax_error_prototype_properties,
    njs_nitems(njs_syntax_error_prototype_properties),
};


static const njs_object_prop_t  njs_type_error_prototype_properties[] =
{
    {
        .type = NJS_PROPERTY,
        .name = njs_string("name"),
        .value = njs_string("TypeError"),
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
};


const njs_object_init_t  njs_type_error_prototype_init = {
    njs_str("TypeError"),
    njs_type_error_prototype_properties,
    njs_nitems(njs_type_error_prototype_properties),
};


static const njs_object_prop_t  njs_uri_error_prototype_properties[] =
{
    {
        .type = NJS_PROPERTY_HANDLER,
        .name = njs_string("constructor"),
        .value = njs_prop_handler(njs_object_prototype_create_constructor),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("name"),
        .value = njs_string("URIError"),
        .writable = 1,
        .configurable = 1,
    },
};


const njs_object_init_t  njs_uri_error_prototype_init = {
    njs_str("URIError"),
    njs_uri_error_prototype_properties,
    njs_nitems(njs_uri_error_prototype_properties),
};
