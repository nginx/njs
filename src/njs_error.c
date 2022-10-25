
/*
 * Copyright (C) Dmitry Volyntsev
 * Copyright (C) NGINX, Inc.
 */


#include <njs_main.h>


typedef struct {
    njs_str_t                         name;
    njs_str_t                         file;
    uint32_t                          line;
} njs_backtrace_entry_t;


static njs_int_t njs_add_backtrace_entry(njs_vm_t *vm, njs_arr_t *stack,
    njs_native_frame_t *native_frame);
static njs_int_t njs_backtrace_to_string(njs_vm_t *vm, njs_arr_t *backtrace,
    njs_str_t *dst);


static const njs_value_t  njs_error_message_string = njs_string("message");
static const njs_value_t  njs_error_name_string = njs_string("name");
static const njs_value_t  njs_error_stack_string = njs_string("stack");
static const njs_value_t  njs_error_errors_string = njs_string("errors");


void
njs_error_new(njs_vm_t *vm, njs_value_t *dst, njs_object_type_t type,
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

    error = njs_error_alloc(vm, type, NULL, &string, NULL);
    if (njs_slow_path(error == NULL)) {
        return;
    }

    njs_set_object(dst, error);
}


void
njs_error_fmt_new(njs_vm_t *vm, njs_value_t *dst, njs_object_type_t type,
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


static njs_int_t
njs_error_stack_new(njs_vm_t *vm, njs_object_t *error, njs_value_t *retval)
{
    njs_int_t           ret;
    njs_str_t           string;
    njs_arr_t           *stack;
    njs_value_t         value;
    njs_native_frame_t  *frame;

    njs_set_object(&value, error);

    ret = njs_error_to_string(vm, retval, &value);
    if (njs_slow_path(ret != NJS_OK)) {
        return ret;
    }

    stack = njs_arr_create(vm->mem_pool, 4, sizeof(njs_backtrace_entry_t));
    if (njs_slow_path(stack == NULL)) {
        return NJS_ERROR;
    }

    frame = vm->top_frame;

    for ( ;; ) {
        if ((frame->native || frame->pc != NULL)
            && njs_add_backtrace_entry(vm, stack, frame) != NJS_OK)
        {
            break;
        }

        frame = frame->previous;

        if (frame == NULL) {
            break;
        }
    }

    njs_string_get(retval, &string);

    ret = njs_backtrace_to_string(vm, stack, &string);

    njs_arr_destroy(stack);

    if (njs_slow_path(ret != NJS_OK)) {
        return ret;
    }

    return njs_string_set(vm, retval, string.start, string.length);
}


njs_int_t
njs_error_stack_attach(njs_vm_t *vm, njs_value_t *value)
{
    njs_int_t    ret;
    njs_value_t  stack;

    if (njs_slow_path(!njs_is_error(value))
        || njs_object(value)->stack_attached)
    {
        return NJS_DECLINED;
    }

    if (njs_slow_path(!vm->options.backtrace || vm->start == NULL)) {
        return NJS_OK;
    }

    ret = njs_error_stack_new(vm, njs_object(value), &stack);
    if (njs_slow_path(ret != NJS_OK)) {
        njs_internal_error(vm, "njs_error_stack_new() failed");
        return NJS_ERROR;
    }

    njs_object(value)->stack_attached = 1;

    return njs_object_prop_define(vm, value,
                                  njs_value_arg(&njs_error_stack_string),
                                  &stack, NJS_OBJECT_PROP_VALUE_CW,
                                  NJS_STACK_HASH);
}


njs_int_t
njs_error_stack(njs_vm_t *vm, njs_value_t *value, njs_value_t *stack)
{
    njs_int_t  ret;

    ret = njs_value_property(vm, value, njs_value_arg(&njs_error_stack_string),
                             stack);
    if (njs_slow_path(ret != NJS_OK)) {
        return ret;
    }

    if (!njs_is_string(stack)) {
        return NJS_DECLINED;
    }

    return NJS_OK;
}


njs_object_t *
njs_error_alloc(njs_vm_t *vm, njs_object_type_t type, const njs_value_t *name,
    const njs_value_t *message, const njs_value_t *errors)
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
    error->type = NJS_OBJECT;
    error->shared = 0;
    error->extensible = 1;
    error->fast_array = 0;
    error->error_data = 1;
    error->stack_attached = 0;
    error->__proto__ = &vm->prototypes[type].object;
    error->slots = NULL;

    lhq.replace = 0;
    lhq.pool = vm->mem_pool;
    lhq.proto = &njs_object_hash_proto;

    if (name != NULL) {
        lhq.key = njs_str_value("name");
        lhq.key_hash = NJS_NAME_HASH;

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

    if (errors != NULL) {
        lhq.key = njs_str_value("errors");
        lhq.key_hash = NJS_ERRORS_HASH;

        prop = njs_object_prop_alloc(vm, &njs_error_errors_string, errors, 1);
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
njs_error_constructor(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t type)
{
    njs_int_t     ret;
    njs_value_t   *iterator, *value, list;
    njs_array_t   *array;
    njs_object_t  *error;

    if (type != NJS_OBJ_TYPE_AGGREGATE_ERROR) {
        iterator = NULL;
        value = njs_arg(args, nargs, 1);

        njs_set_undefined(&list);

    } else {
        iterator = njs_arg(args, nargs, 1);
        value = njs_arg(args, nargs, 2);

        if (njs_slow_path(iterator->type < NJS_STRING)) {
            njs_type_error(vm, "first argument is not iterable");
            return NJS_ERROR;
        }

        array = njs_iterator_to_array(vm, iterator);
        if (njs_slow_path(array == NULL)) {
            return NJS_ERROR;
        }

        njs_set_array(&list, array);
    }

    if (njs_slow_path(!njs_is_string(value))) {
        if (!njs_is_undefined(value)) {
            ret = njs_value_to_string(vm, value, value);
            if (njs_slow_path(ret != NJS_OK)) {
                return ret;
            }
        }
    }

    error = njs_error_alloc(vm, type, NULL,
                            njs_is_defined(value) ? value : NULL,
                            njs_is_defined(&list) ? &list : NULL);
    if (njs_slow_path(error == NULL)) {
        return NJS_ERROR;
    }

    njs_set_object(&vm->retval, error);

    return NJS_OK;
}


static const njs_object_prop_t  njs_error_constructor_properties[] =
{
    NJS_DECLARE_PROP_NAME("Error"),

    NJS_DECLARE_PROP_LENGTH(1),

    NJS_DECLARE_PROP_HANDLER("prototype", njs_object_prototype_create, 0, 0, 0),
};


const njs_object_init_t  njs_error_constructor_init = {
    njs_error_constructor_properties,
    njs_nitems(njs_error_constructor_properties),
};


static const njs_object_prop_t  njs_eval_error_constructor_properties[] =
{
    NJS_DECLARE_PROP_NAME("EvalError"),

    NJS_DECLARE_PROP_LENGTH(1),

    NJS_DECLARE_PROP_HANDLER("prototype", njs_object_prototype_create, 0, 0, 0),
};


const njs_object_init_t  njs_eval_error_constructor_init = {
    njs_eval_error_constructor_properties,
    njs_nitems(njs_eval_error_constructor_properties),
};


static const njs_object_prop_t  njs_internal_error_constructor_properties[] =
{
    NJS_DECLARE_PROP_NAME("InternalError"),

    NJS_DECLARE_PROP_LENGTH(1),

    NJS_DECLARE_PROP_HANDLER("prototype", njs_object_prototype_create, 0, 0, 0),
};


const njs_object_init_t  njs_internal_error_constructor_init = {
    njs_internal_error_constructor_properties,
    njs_nitems(njs_internal_error_constructor_properties),
};


static const njs_object_prop_t  njs_range_error_constructor_properties[] =
{
    NJS_DECLARE_PROP_NAME("RangeError"),

    NJS_DECLARE_PROP_LENGTH(1),

    NJS_DECLARE_PROP_HANDLER("prototype", njs_object_prototype_create, 0, 0, 0),
};


const njs_object_init_t  njs_range_error_constructor_init = {
    njs_range_error_constructor_properties,
    njs_nitems(njs_range_error_constructor_properties),
};


static const njs_object_prop_t  njs_reference_error_constructor_properties[] =
{
    NJS_DECLARE_PROP_NAME("ReferenceError"),

    NJS_DECLARE_PROP_LENGTH(1),

    NJS_DECLARE_PROP_HANDLER("prototype", njs_object_prototype_create, 0, 0, 0),
};


const njs_object_init_t  njs_reference_error_constructor_init = {
    njs_reference_error_constructor_properties,
    njs_nitems(njs_reference_error_constructor_properties),
};


static const njs_object_prop_t  njs_syntax_error_constructor_properties[] =
{
    NJS_DECLARE_PROP_NAME("SyntaxError"),

    NJS_DECLARE_PROP_LENGTH(1),

    NJS_DECLARE_PROP_HANDLER("prototype", njs_object_prototype_create, 0, 0, 0),
};


const njs_object_init_t  njs_syntax_error_constructor_init = {
    njs_syntax_error_constructor_properties,
    njs_nitems(njs_syntax_error_constructor_properties),
};


static const njs_object_prop_t  njs_type_error_constructor_properties[] =
{
    NJS_DECLARE_PROP_NAME("TypeError"),

    NJS_DECLARE_PROP_LENGTH(1),

    NJS_DECLARE_PROP_HANDLER("prototype", njs_object_prototype_create, 0, 0, 0),
};


const njs_object_init_t  njs_type_error_constructor_init = {
    njs_type_error_constructor_properties,
    njs_nitems(njs_type_error_constructor_properties),
};


static const njs_object_prop_t  njs_uri_error_constructor_properties[] =
{
    NJS_DECLARE_PROP_NAME("URIError"),

    NJS_DECLARE_PROP_LENGTH(1),

    NJS_DECLARE_PROP_HANDLER("prototype", njs_object_prototype_create, 0, 0, 0),
};


const njs_object_init_t  njs_uri_error_constructor_init = {
    njs_uri_error_constructor_properties,
    njs_nitems(njs_uri_error_constructor_properties),
};


static const njs_object_prop_t  njs_aggregate_error_constructor_properties[] =
{
    NJS_DECLARE_PROP_NAME("AggregateError"),

    NJS_DECLARE_PROP_LENGTH(1),

    NJS_DECLARE_PROP_HANDLER("prototype", njs_object_prototype_create, 0, 0, 0),
};


const njs_object_init_t  njs_aggregate_error_constructor_init = {
    njs_aggregate_error_constructor_properties,
    njs_nitems(njs_aggregate_error_constructor_properties),
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
    object->__proto__ = &prototypes[NJS_OBJ_TYPE_INTERNAL_ERROR].object;
    object->slots = NULL;
    object->type = NJS_OBJECT;
    object->shared = 1;

    /*
     * Marking it nonextensible to differentiate
     * it from ordinary internal errors.
     */
    object->extensible = 0;
    object->fast_array = 0;
    object->error_data = 1;

    njs_set_object(value, object);
}


void
njs_memory_error(njs_vm_t *vm)
{
    njs_memory_error_set(vm, &vm->retval);
}


static njs_int_t
njs_memory_error_constructor(njs_vm_t *vm, njs_value_t *args,
    njs_uint_t nargs, njs_index_t unused)
{
    njs_memory_error_set(vm, &vm->retval);

    return NJS_OK;
}


static njs_int_t
njs_memory_error_prototype_create(njs_vm_t *vm, njs_object_prop_t *prop,
    njs_value_t *value, njs_value_t *setval, njs_value_t *retval)
{
    int32_t            index;
    njs_function_t     *function;
    const njs_value_t  *proto;

    /* MemoryError has no its own prototype. */

    index = NJS_OBJ_TYPE_INTERNAL_ERROR;

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
    NJS_DECLARE_PROP_NAME("MemoryError"),

    NJS_DECLARE_PROP_LENGTH(1),

    NJS_DECLARE_PROP_HANDLER("prototype", njs_memory_error_prototype_create,
                             0, 0, 0),
};


const njs_object_init_t  njs_memory_error_constructor_init = {
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
njs_error_to_string2(njs_vm_t *vm, njs_value_t *retval,
    const njs_value_t *error, njs_bool_t want_stack)
{
    size_t              length;
    u_char              *p;
    njs_int_t           ret;
    njs_value_t         value1, value2;
    njs_value_t         *name_value, *message_value;
    njs_string_prop_t   name, message;
    njs_lvlhsh_query_t  lhq;

    static const njs_value_t  default_name = njs_string("Error");

    if (want_stack) {
        ret = njs_error_stack(vm, njs_value_arg(error), retval);
        if (njs_slow_path(ret == NJS_ERROR)) {
            return ret;
        }

        if (ret == NJS_OK) {
            return NJS_OK;
        }
    }

    njs_object_property_init(&lhq, &njs_string_name, NJS_NAME_HASH);

    ret = njs_object_property(vm, error, &lhq, &value1);

    if (njs_slow_path(ret == NJS_ERROR)) {
        return ret;
    }

    name_value = (ret == NJS_OK) ? &value1 : njs_value_arg(&default_name);

    if (njs_slow_path(!njs_is_string(name_value))) {
        ret = njs_value_to_string(vm, &value1, name_value);
        if (njs_slow_path(ret != NJS_OK)) {
            return ret;
        }

        name_value = &value1;
    }

    (void) njs_string_prop(&name, name_value);

    lhq.key_hash = NJS_MESSAGE_HASH;
    lhq.key = njs_str_value("message");

    ret = njs_object_property(vm, error, &lhq, &value2);

    if (njs_slow_path(ret == NJS_ERROR)) {
        return ret;
    }

    message_value = (ret == NJS_OK) ? &value2
                                    : njs_value_arg(&njs_string_empty);

    if (njs_slow_path(!njs_is_string(message_value))) {
        ret = njs_value_to_string(vm, &value2, message_value);
        if (njs_slow_path(ret != NJS_OK)) {
            return ret;
        }

        message_value = &value2;
    }

    (void) njs_string_prop(&message, message_value);

    if (name.size == 0) {
        *retval = *message_value;
        return NJS_OK;
    }

    if (message.size == 0) {
        *retval = *name_value;
        return NJS_OK;
    }

    if (name.length != 0 && message.length != 0) {
        length = name.length + message.length + 2;

    } else {
        length = 0;
    }

    p = njs_string_alloc(vm, retval, name.size + message.size + 2, length);

    if (njs_fast_path(p != NULL)) {
        p = njs_cpymem(p, name.start, name.size);
        *p++ = ':';
        *p++ = ' ';
        memcpy(p, message.start, message.size);

        return NJS_OK;
    }

    njs_memory_error(vm);

    return NJS_ERROR;
}


static njs_int_t
njs_error_prototype_to_string(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t unused)
{
    if (nargs < 1 || !njs_is_object(&args[0])) {
        njs_type_error(vm, "\"this\" argument is not an object");
        return NJS_ERROR;
    }

    return njs_error_to_string2(vm, &vm->retval, &args[0], 0);
}


njs_int_t
njs_error_to_string(njs_vm_t *vm, njs_value_t *retval, const njs_value_t *error)
{
    return njs_error_to_string2(vm, retval, error, 1);
}


static const njs_object_prop_t  njs_error_prototype_properties[] =
{
    NJS_DECLARE_PROP_HANDLER("constructor",
                             njs_object_prototype_create_constructor,
                             0, 0, NJS_OBJECT_PROP_VALUE_CW),

    NJS_DECLARE_PROP_VALUE("name", njs_string("Error"),
                           NJS_OBJECT_PROP_VALUE_CW),

    NJS_DECLARE_PROP_VALUE("message", njs_string(""), NJS_OBJECT_PROP_VALUE_CW),

    NJS_DECLARE_PROP_NATIVE("valueOf", njs_error_prototype_value_of, 0, 0),

    NJS_DECLARE_PROP_NATIVE("toString", njs_error_prototype_to_string, 0, 0),
};


const njs_object_init_t  njs_error_prototype_init = {
    njs_error_prototype_properties,
    njs_nitems(njs_error_prototype_properties),
};


const njs_object_type_init_t  njs_error_type_init = {
    .constructor = njs_native_ctor(njs_error_constructor, 1,
                                   NJS_OBJ_TYPE_ERROR),
    .constructor_props = &njs_error_constructor_init,
    .prototype_props = &njs_error_prototype_init,
    .prototype_value = { .object = { .type = NJS_OBJECT } },
};


static const njs_object_prop_t  njs_eval_error_prototype_properties[] =
{
    NJS_DECLARE_PROP_HANDLER("constructor",
                             njs_object_prototype_create_constructor,
                             0, 0, NJS_OBJECT_PROP_VALUE_CW),

    NJS_DECLARE_PROP_VALUE("name", njs_string("EvalError"),
                           NJS_OBJECT_PROP_VALUE_CW),

    NJS_DECLARE_PROP_VALUE("message", njs_string(""), NJS_OBJECT_PROP_VALUE_CW),
};


const njs_object_init_t  njs_eval_error_prototype_init = {
    njs_eval_error_prototype_properties,
    njs_nitems(njs_eval_error_prototype_properties),
};


const njs_object_type_init_t  njs_eval_error_type_init = {
    .constructor = njs_native_ctor(njs_error_constructor, 1,
                                   NJS_OBJ_TYPE_EVAL_ERROR),
    .constructor_props = &njs_eval_error_constructor_init,
    .prototype_props = &njs_eval_error_prototype_init,
    .prototype_value = { .object = { .type = NJS_OBJECT } },
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
    NJS_DECLARE_PROP_VALUE("name", njs_string("InternalError"),
                           NJS_OBJECT_PROP_VALUE_CW),

    NJS_DECLARE_PROP_VALUE("message", njs_string(""), NJS_OBJECT_PROP_VALUE_CW),

    NJS_DECLARE_PROP_NATIVE("toString", njs_internal_error_prototype_to_string,
                            0, 0),
};


const njs_object_init_t  njs_internal_error_prototype_init = {
    njs_internal_error_prototype_properties,
    njs_nitems(njs_internal_error_prototype_properties),
};


const njs_object_type_init_t  njs_internal_error_type_init = {
    .constructor = njs_native_ctor(njs_error_constructor, 1,
                                   NJS_OBJ_TYPE_INTERNAL_ERROR),
    .constructor_props = &njs_internal_error_constructor_init,
    .prototype_props = &njs_internal_error_prototype_init,
    .prototype_value = { .object = { .type = NJS_OBJECT } },
};


const njs_object_type_init_t  njs_memory_error_type_init = {
    .constructor = njs_native_ctor(njs_memory_error_constructor, 1, 0),
    .constructor_props = &njs_memory_error_constructor_init,
    .prototype_props = &njs_internal_error_prototype_init,
    .prototype_value = { .object = { .type = NJS_OBJECT } },
};


static const njs_object_prop_t  njs_range_error_prototype_properties[] =
{
    NJS_DECLARE_PROP_HANDLER("constructor",
                             njs_object_prototype_create_constructor,
                             0, 0, NJS_OBJECT_PROP_VALUE_CW),

    NJS_DECLARE_PROP_VALUE("name", njs_string("RangeError"),
                           NJS_OBJECT_PROP_VALUE_CW),

    NJS_DECLARE_PROP_VALUE("message", njs_string(""), NJS_OBJECT_PROP_VALUE_CW),
};


const njs_object_init_t  njs_range_error_prototype_init = {
    njs_range_error_prototype_properties,
    njs_nitems(njs_range_error_prototype_properties),
};


const njs_object_type_init_t  njs_range_error_type_init = {
    .constructor = njs_native_ctor(njs_error_constructor, 1,
                                   NJS_OBJ_TYPE_RANGE_ERROR),
    .constructor_props = &njs_range_error_constructor_init,
    .prototype_props = &njs_range_error_prototype_init,
    .prototype_value = { .object = { .type = NJS_OBJECT } },
};


static const njs_object_prop_t  njs_reference_error_prototype_properties[] =
{
    NJS_DECLARE_PROP_HANDLER("constructor",
                             njs_object_prototype_create_constructor,
                             0, 0, NJS_OBJECT_PROP_VALUE_CW),

    NJS_DECLARE_PROP_VALUE("name", njs_string("ReferenceError"),
                           NJS_OBJECT_PROP_VALUE_CW),

    NJS_DECLARE_PROP_VALUE("message", njs_string(""), NJS_OBJECT_PROP_VALUE_CW),
};


const njs_object_init_t  njs_reference_error_prototype_init = {
    njs_reference_error_prototype_properties,
    njs_nitems(njs_reference_error_prototype_properties),
};


const njs_object_type_init_t  njs_reference_error_type_init = {
    .constructor = njs_native_ctor(njs_error_constructor, 1,
                                   NJS_OBJ_TYPE_REF_ERROR),
    .constructor_props = &njs_reference_error_constructor_init,
    .prototype_props = &njs_reference_error_prototype_init,
    .prototype_value = { .object = { .type = NJS_OBJECT } },
};


static const njs_object_prop_t  njs_syntax_error_prototype_properties[] =
{
    NJS_DECLARE_PROP_HANDLER("constructor",
                             njs_object_prototype_create_constructor,
                             0, 0, NJS_OBJECT_PROP_VALUE_CW),

    NJS_DECLARE_PROP_VALUE("name", njs_string("SyntaxError"),
                           NJS_OBJECT_PROP_VALUE_CW),

    NJS_DECLARE_PROP_VALUE("message", njs_string(""), NJS_OBJECT_PROP_VALUE_CW),
};


const njs_object_init_t  njs_syntax_error_prototype_init = {
    njs_syntax_error_prototype_properties,
    njs_nitems(njs_syntax_error_prototype_properties),
};


const njs_object_type_init_t  njs_syntax_error_type_init = {
    .constructor = njs_native_ctor(njs_error_constructor, 1,
                                   NJS_OBJ_TYPE_SYNTAX_ERROR),
    .constructor_props = &njs_syntax_error_constructor_init,
    .prototype_props = &njs_syntax_error_prototype_init,
    .prototype_value = { .object = { .type = NJS_OBJECT } },
};


static const njs_object_prop_t  njs_type_error_prototype_properties[] =
{
    NJS_DECLARE_PROP_HANDLER("constructor",
                             njs_object_prototype_create_constructor,
                             0, 0, NJS_OBJECT_PROP_VALUE_CW),

    NJS_DECLARE_PROP_VALUE("name", njs_string("TypeError"),
                           NJS_OBJECT_PROP_VALUE_CW),

    NJS_DECLARE_PROP_VALUE("message", njs_string(""), NJS_OBJECT_PROP_VALUE_CW),
};


const njs_object_init_t  njs_type_error_prototype_init = {
    njs_type_error_prototype_properties,
    njs_nitems(njs_type_error_prototype_properties),
};


const njs_object_type_init_t  njs_type_error_type_init = {
    .constructor = njs_native_ctor(njs_error_constructor, 1,
                                   NJS_OBJ_TYPE_TYPE_ERROR),
    .constructor_props = &njs_type_error_constructor_init,
    .prototype_props = &njs_type_error_prototype_init,
    .prototype_value = { .object = { .type = NJS_OBJECT } },
};


static const njs_object_prop_t  njs_uri_error_prototype_properties[] =
{
    NJS_DECLARE_PROP_HANDLER("constructor",
                             njs_object_prototype_create_constructor,
                             0, 0, NJS_OBJECT_PROP_VALUE_CW),

    NJS_DECLARE_PROP_VALUE("name", njs_string("URIError"),
                           NJS_OBJECT_PROP_VALUE_CW),

    NJS_DECLARE_PROP_VALUE("message", njs_string(""), NJS_OBJECT_PROP_VALUE_CW),
};


const njs_object_init_t  njs_uri_error_prototype_init = {
    njs_uri_error_prototype_properties,
    njs_nitems(njs_uri_error_prototype_properties),
};


const njs_object_type_init_t  njs_uri_error_type_init = {
    .constructor = njs_native_ctor(njs_error_constructor, 1,
                                   NJS_OBJ_TYPE_URI_ERROR),
    .constructor_props = &njs_uri_error_constructor_init,
    .prototype_props = &njs_uri_error_prototype_init,
    .prototype_value = { .object = { .type = NJS_OBJECT } },
};


static const njs_object_prop_t  njs_aggregate_error_prototype_properties[] =
{
    NJS_DECLARE_PROP_HANDLER("constructor",
                             njs_object_prototype_create_constructor,
                             0, 0, NJS_OBJECT_PROP_VALUE_CW),

    NJS_DECLARE_PROP_VALUE("name", njs_string("AggregateError"),
                           NJS_OBJECT_PROP_VALUE_CW),

    NJS_DECLARE_PROP_VALUE("message", njs_string(""), NJS_OBJECT_PROP_VALUE_CW),
};


const njs_object_init_t  njs_aggregate_error_prototype_init = {
    njs_aggregate_error_prototype_properties,
    njs_nitems(njs_aggregate_error_prototype_properties),
};


const njs_object_type_init_t  njs_aggregate_error_type_init = {
    .constructor = njs_native_ctor(njs_error_constructor, 1,
                                   NJS_OBJ_TYPE_AGGREGATE_ERROR),
    .constructor_props = &njs_aggregate_error_constructor_init,
    .prototype_props = &njs_aggregate_error_prototype_init,
    .prototype_value = { .object = { .type = NJS_OBJECT } },
};


static njs_int_t
njs_add_backtrace_entry(njs_vm_t *vm, njs_arr_t *stack,
    njs_native_frame_t *native_frame)
{
    njs_int_t              ret;
    njs_vm_code_t          *code;
    njs_function_t         *function;
    njs_backtrace_entry_t  *be;

    function = native_frame->function;

    if (function != NULL && function->bound != NULL) {
        /* Skip. */
        return NJS_OK;
    }

    be = njs_arr_add(stack);
    if (njs_slow_path(be == NULL)) {
        return NJS_ERROR;
    }

    be->line = 0;
    be->file = njs_str_value("");

    if (function != NULL && function->native) {
        ret = njs_builtin_match_native_function(vm, function, &be->name);
        if (ret == NJS_OK) {
            return NJS_OK;
        }

        be->name = njs_entry_native;

        return NJS_OK;
    }

    code = njs_lookup_code(vm, native_frame->pc);

    if (code != NULL) {
        be->name = code->name;

        if (be->name.length == 0) {
            be->name = njs_entry_anonymous;
        }

        be->line = njs_lookup_line(code->lines, native_frame->pc - code->start);
        if (!vm->options.quiet) {
            be->file = code->file;
        }

        return NJS_OK;
    }

    be->name = njs_entry_unknown;

    return NJS_OK;
}


static njs_int_t
njs_backtrace_to_string(njs_vm_t *vm, njs_arr_t *backtrace, njs_str_t *dst)
{
    size_t                 count;
    njs_chb_t              chain;
    njs_int_t              ret;
    njs_uint_t             i;
    njs_backtrace_entry_t  *be, *prev;

    if (backtrace->items == 0) {
        return NJS_OK;
    }

    njs_chb_init(&chain, vm->mem_pool);

    njs_chb_append_str(&chain, dst);
    njs_chb_append(&chain, "\n", 1);

    count = 0;
    prev = NULL;

    be = backtrace->start;

    for (i = 0; i < backtrace->items; i++) {
        if (i != 0 && prev->name.start == be->name.start
            && prev->line == be->line)
        {
            count++;

        } else {
            if (count != 0) {
                njs_chb_sprintf(&chain, 64, "      repeats %uz times\n", count);
                count = 0;
            }

            njs_chb_sprintf(&chain, 10 + be->name.length, "    at %V ",
                            &be->name);

            if (be->line != 0) {
                njs_chb_sprintf(&chain, 12 + be->file.length,
                                "(%V:%uD)\n", &be->file, be->line);

            } else {
                njs_chb_append(&chain, "(native)\n", 9);
            }
        }

        prev = be;
        be++;
    }

    ret = njs_chb_join(&chain, dst);
    njs_chb_destroy(&chain);

    return ret;
}
