
/*
 * Copyright (C) Dmitry Volyntsev
 * Copyright (C) NGINX, Inc.
 */


#include <njs_main.h>


typedef struct {
    union {
        njs_function_t                *function;
        u_char                        *pc;
    } u;
    uint8_t                           native;
} njs_stack_entry_t;


typedef struct {
    njs_str_t                         name;
    njs_str_t                         file;
    uint32_t                          line;
} njs_backtrace_entry_t;


static njs_int_t njs_add_backtrace_entry(njs_vm_t *vm, njs_arr_t *stack,
    njs_stack_entry_t *se);
static njs_int_t njs_backtrace_to_string(njs_vm_t *vm, njs_arr_t *backtrace,
    njs_str_t *dst);


void
njs_error_new(njs_vm_t *vm, njs_value_t *dst, njs_object_t *proto,
    u_char *start, size_t size)
{
    njs_int_t     ret;
    njs_value_t   string;
    njs_object_t  *error;

    ret = njs_string_create(vm, &string, start, size);
    if (njs_slow_path(ret != NJS_OK)) {
        return;
    }

    error = njs_error_alloc(vm, proto, NULL, &string, NULL);
    if (njs_slow_path(error == NULL)) {
        return;
    }

    njs_set_object(dst, error);
}

void
njs_throw_error_va(njs_vm_t *vm, njs_object_t *proto, const char *fmt,
    va_list args)
{
    u_char   buf[NJS_MAX_ERROR_STR], *p;

    p = njs_vsprintf(buf, buf + sizeof(buf), fmt, args);

    njs_error_new(vm, &vm->exception, proto, buf, p - buf);
}


void
njs_throw_error(njs_vm_t *vm, njs_object_type_t type, const char *fmt, ...)
{
    va_list  args;

    va_start(args, fmt);
    njs_throw_error_va(vm, njs_vm_proto(vm, type), fmt, args);
    va_end(args);
}


void
njs_error_fmt_new(njs_vm_t *vm, njs_value_t *dst, njs_object_type_t type,
    const char *fmt, ...)
{
    va_list  args;
    u_char   buf[NJS_MAX_ERROR_STR], *p;

    va_start(args, fmt);
    p = njs_vsprintf(buf, buf + sizeof(buf), fmt, args);
    va_end(args);

    njs_error_new(vm, dst, njs_vm_proto(vm, type), buf, p - buf);
}


static njs_int_t
njs_error_stack_new(njs_vm_t *vm, njs_object_value_t *error)
{
    njs_arr_t           *stack;
    njs_stack_entry_t   *se;
    njs_native_frame_t  *frame;

    stack = njs_arr_create(vm->mem_pool, 4, sizeof(njs_stack_entry_t));
    if (njs_slow_path(stack == NULL)) {
        return NJS_ERROR;
    }

    frame = vm->top_frame;

    for ( ;; ) {
        if (frame->native || frame->pc != NULL) {
            se = njs_arr_add(stack);
            if (njs_slow_path(se == NULL)) {
                return NJS_ERROR;
            }

            se->native = frame->native;

            if (se->native) {
                se->u.function = frame->function;

            } else {
                se->u.pc = frame->pc;
            }
        }

        frame = frame->previous;

        if (frame == NULL) {
            break;
        }
    }

    njs_data(&error->value) = stack;

    return NJS_OK;
}


njs_int_t
njs_error_stack_attach(njs_vm_t *vm, njs_value_t value)
{
    njs_int_t  ret;

    if (njs_slow_path(!njs_is_error(&value))
        || njs_object(&value)->stack_attached)
    {
        return NJS_DECLINED;
    }

    if (njs_slow_path(!vm->options.backtrace || vm->start == NULL)) {
        return NJS_OK;
    }

    ret = njs_error_stack_new(vm, value.data.u.object_value);
    if (njs_slow_path(ret != NJS_OK)) {
        njs_internal_error(vm, "njs_error_stack_new() failed");
        return NJS_ERROR;
    }

    njs_object(&value)->stack_attached = 1;

    return NJS_OK;
}


njs_int_t
njs_error_stack(njs_vm_t *vm, njs_value_t *value, njs_value_t *stack)
{
    njs_int_t  ret;

    ret = njs_value_property(vm, value, NJS_ATOM_STRING_stack, stack);
    if (njs_slow_path(ret != NJS_OK)) {
        return ret;
    }

    if (!njs_is_string(stack)) {
        return NJS_DECLINED;
    }

    return NJS_OK;
}


njs_object_t *
njs_error_alloc(njs_vm_t *vm, njs_object_t *proto, const njs_value_t *name,
    const njs_value_t *message, const njs_value_t *errors)
{
    njs_int_t            ret;
    njs_object_t         *error;
    njs_object_prop_t    *prop;
    njs_object_value_t   *ov;
    njs_flathsh_query_t  lhq;

    ov = njs_mp_alloc(vm->mem_pool, sizeof(njs_object_value_t));
    if (njs_slow_path(ov == NULL)) {
        goto memory_error;
    }

    njs_set_data(&ov->value, NULL, NJS_DATA_TAG_ANY);

    error = &ov->object;
    njs_lvlhsh_init(&error->hash);
    njs_lvlhsh_init(&error->shared_hash);
    error->type = NJS_OBJECT_VALUE;
    error->shared = 0;
    error->extensible = 1;
    error->fast_array = 0;
    error->error_data = 1;
    error->stack_attached = 0;
    error->__proto__ = proto;
    error->slots = NULL;

    lhq.replace = 0;
    lhq.pool = vm->mem_pool;
    lhq.proto = &njs_object_hash_proto;

    if (name != NULL) {
        prop = njs_object_prop_alloc(vm, name, 1);
        if (njs_slow_path(prop == NULL)) {
            goto memory_error;
        }

        lhq.value = prop;
        lhq.key_hash = NJS_ATOM_STRING_name;

        ret = njs_flathsh_unique_insert(&error->hash, &lhq);
        if (njs_slow_path(ret != NJS_OK)) {
            njs_internal_error(vm, "lvlhsh insert failed");
            return NULL;
        }
    }

    if (message!= NULL) {
        prop = njs_object_prop_alloc(vm, message, 1);
        if (njs_slow_path(prop == NULL)) {
            goto memory_error;
        }

        prop->enumerable = 0;

        lhq.value = prop;
        lhq.key_hash = NJS_ATOM_STRING_message;

        ret = njs_flathsh_unique_insert(&error->hash, &lhq);
        if (njs_slow_path(ret != NJS_OK)) {
            njs_internal_error(vm, "lvlhsh insert failed");
            return NULL;
        }
    }

    if (errors != NULL) {
        prop = njs_object_prop_alloc(vm, errors, 1);
        if (njs_slow_path(prop == NULL)) {
            goto memory_error;
        }

        prop->enumerable = 0;

        lhq.value = prop;
        lhq.key_hash = NJS_ATOM_STRING_errors;

        ret = njs_flathsh_unique_insert(&error->hash, &lhq);
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


njs_int_t
njs_error_constructor(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t type, njs_value_t *retval)
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

        array = njs_iterator_to_array(vm, iterator, retval);
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

    error = njs_error_alloc(vm, njs_vm_proto(vm, type), NULL,
                            njs_is_defined(value) ? value : NULL,
                            njs_is_defined(&list) ? &list : NULL);
    if (njs_slow_path(error == NULL)) {
        return NJS_ERROR;
    }

    njs_set_object(retval, error);

    return NJS_OK;
}


static const njs_object_prop_init_t  njs_error_constructor_properties[] =
{
    NJS_DECLARE_PROP_LENGTH(1),

    NJS_DECLARE_PROP_NAME("Error"),

    NJS_DECLARE_PROP_HANDLER(STRING_prototype, njs_object_prototype_create,
                             0, 0),
};


const njs_object_init_t  njs_error_constructor_init = {
    njs_error_constructor_properties,
    njs_nitems(njs_error_constructor_properties),
};


static const njs_object_prop_init_t  njs_eval_error_constructor_properties[] =
{
    NJS_DECLARE_PROP_LENGTH(1),

    NJS_DECLARE_PROP_NAME("EvalError"),

    NJS_DECLARE_PROP_HANDLER(STRING_prototype, njs_object_prototype_create,
                             0, 0),
};


const njs_object_init_t  njs_eval_error_constructor_init = {
    njs_eval_error_constructor_properties,
    njs_nitems(njs_eval_error_constructor_properties),
};


static const njs_object_prop_init_t  njs_internal_error_constructor_properties[] =
{
    NJS_DECLARE_PROP_LENGTH(1),

    NJS_DECLARE_PROP_NAME("InternalError"),

    NJS_DECLARE_PROP_HANDLER(STRING_prototype, njs_object_prototype_create,
                             0, 0),
};


const njs_object_init_t  njs_internal_error_constructor_init = {
    njs_internal_error_constructor_properties,
    njs_nitems(njs_internal_error_constructor_properties),
};


static const njs_object_prop_init_t  njs_range_error_constructor_properties[] =
{
    NJS_DECLARE_PROP_LENGTH(1),

    NJS_DECLARE_PROP_NAME("RangeError"),

    NJS_DECLARE_PROP_HANDLER(STRING_prototype, njs_object_prototype_create,
                             0, 0),
};


const njs_object_init_t  njs_range_error_constructor_init = {
    njs_range_error_constructor_properties,
    njs_nitems(njs_range_error_constructor_properties),
};


static const njs_object_prop_init_t  njs_reference_error_constructor_properties[] =
{
    NJS_DECLARE_PROP_LENGTH(1),

    NJS_DECLARE_PROP_NAME("ReferenceError"),

    NJS_DECLARE_PROP_HANDLER(STRING_prototype, njs_object_prototype_create,
                             0, 0),
};


const njs_object_init_t  njs_reference_error_constructor_init = {
    njs_reference_error_constructor_properties,
    njs_nitems(njs_reference_error_constructor_properties),
};


static const njs_object_prop_init_t  njs_syntax_error_constructor_properties[] =
{
    NJS_DECLARE_PROP_LENGTH(1),

    NJS_DECLARE_PROP_NAME("SyntaxError"),

    NJS_DECLARE_PROP_HANDLER(STRING_prototype, njs_object_prototype_create,
                             0, 0),
};


const njs_object_init_t  njs_syntax_error_constructor_init = {
    njs_syntax_error_constructor_properties,
    njs_nitems(njs_syntax_error_constructor_properties),
};


static const njs_object_prop_init_t  njs_type_error_constructor_properties[] =
{
    NJS_DECLARE_PROP_LENGTH(1),

    NJS_DECLARE_PROP_NAME("TypeError"),

    NJS_DECLARE_PROP_HANDLER(STRING_prototype, njs_object_prototype_create,
                             0, 0),
};


const njs_object_init_t  njs_type_error_constructor_init = {
    njs_type_error_constructor_properties,
    njs_nitems(njs_type_error_constructor_properties),
};


static const njs_object_prop_init_t  njs_uri_error_constructor_properties[] =
{
    NJS_DECLARE_PROP_LENGTH(1),

    NJS_DECLARE_PROP_NAME("URIError"),

    NJS_DECLARE_PROP_HANDLER(STRING_prototype, njs_object_prototype_create,
                             0, 0),
};


const njs_object_init_t  njs_uri_error_constructor_init = {
    njs_uri_error_constructor_properties,
    njs_nitems(njs_uri_error_constructor_properties),
};


static const njs_object_prop_init_t  njs_aggregate_error_constructor_properties[] =
{
    NJS_DECLARE_PROP_LENGTH(1),

    NJS_DECLARE_PROP_NAME("AggregateError"),

    NJS_DECLARE_PROP_HANDLER(STRING_prototype, njs_object_prototype_create,
                             0, 0),
};


const njs_object_init_t  njs_aggregate_error_constructor_init = {
    njs_aggregate_error_constructor_properties,
    njs_nitems(njs_aggregate_error_constructor_properties),
};


void
njs_memory_error_set(njs_vm_t *vm, njs_value_t *value)
{
    njs_object_t        *object;
    njs_object_value_t  *ov;

    ov = &vm->memory_error_object;
    njs_set_data(&ov->value, NULL, NJS_DATA_TAG_ANY);

    object = &ov->object;
    njs_lvlhsh_init(&object->hash);
    njs_lvlhsh_init(&object->shared_hash);
    object->__proto__ = njs_vm_proto(vm, NJS_OBJ_TYPE_INTERNAL_ERROR);
    object->slots = NULL;
    object->type = NJS_OBJECT_VALUE;
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
    njs_memory_error_set(vm, &vm->exception);
}


static njs_int_t
njs_memory_error_constructor(njs_vm_t *vm, njs_value_t *args,
    njs_uint_t nargs, njs_index_t unused, njs_value_t *retval)
{
    njs_memory_error_set(vm, retval);

    return NJS_OK;
}


static njs_int_t
njs_memory_error_prototype_create(njs_vm_t *vm, njs_object_prop_t *prop,
    uint32_t unused, njs_value_t *value, njs_value_t *setval,
    njs_value_t *retval)
{
    int32_t            index;
    njs_function_t     *function;
    const njs_value_t  *proto;

    /* MemoryError has no its own prototype. */

    index = NJS_OBJ_TYPE_INTERNAL_ERROR;

    function = njs_function(value);
    proto = njs_property_prototype_create(vm, &function->object.hash,
                                          njs_vm_proto(vm, index));
    if (proto == NULL) {
        proto = &njs_value_undefined;
    }

    *retval = *proto;

    return NJS_OK;
}


static const njs_object_prop_init_t  njs_memory_error_constructor_properties[] =
{
    NJS_DECLARE_PROP_LENGTH(1),

    NJS_DECLARE_PROP_NAME("MemoryError"),

    NJS_DECLARE_PROP_HANDLER(STRING_prototype,
                             njs_memory_error_prototype_create, 0, 0),
};


const njs_object_init_t  njs_memory_error_constructor_init = {
    njs_memory_error_constructor_properties,
    njs_nitems(njs_memory_error_constructor_properties),
};


static njs_int_t
njs_error_prototype_value_of(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t unused, njs_value_t *retval)
{
    njs_value_assign(retval, njs_argument(args, 0));

    return NJS_OK;
}


static njs_int_t
njs_error_to_string2(njs_vm_t *vm, njs_value_t *retval,
    const njs_value_t *error, njs_bool_t want_stack)
{
    size_t             length;
    u_char             *p;
    njs_int_t          ret;
    njs_value_t        value1, value2;
    njs_value_t        *name_value, *message_value;
    njs_string_prop_t  name, message;

    njs_assert(njs_is_object(error));

    if (want_stack) {
        ret = njs_error_stack(vm, njs_value_arg(error), retval);
        if (njs_slow_path(ret == NJS_ERROR)) {
            return ret;
        }

        if (ret == NJS_OK) {
            return NJS_OK;
        }
    }

    ret = njs_value_property(vm, (njs_value_t *) error, NJS_ATOM_STRING_name,
                             &value1);
    if (njs_slow_path(ret == NJS_ERROR)) {
        return ret;
    }

    if (ret == NJS_DECLINED) {
        njs_atom_to_value(vm, &value1, NJS_ATOM_STRING_Error);
    }

    name_value = &value1;

    if (njs_slow_path(!njs_is_string(name_value))) {
        ret = njs_value_to_string(vm, &value1, name_value);
        if (njs_slow_path(ret != NJS_OK)) {
            return ret;
        }
    }

    (void) njs_string_prop(vm, &name, name_value);

    ret = njs_value_property(vm,  (njs_value_t *) error, NJS_ATOM_STRING_message,
                             &value2);
    if (njs_slow_path(ret == NJS_ERROR)) {
        return ret;
    }

    if (ret == NJS_DECLINED) {
        njs_set_empty_string(vm, &value2);
    }

    message_value = &value2;

    if (njs_slow_path(!njs_is_string(message_value))) {
        ret = njs_value_to_string(vm, &value2, message_value);
        if (njs_slow_path(ret != NJS_OK)) {
            return ret;
        }

        message_value = &value2;
    }

    (void) njs_string_prop(vm, &message, message_value);

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
    njs_index_t unused, njs_value_t *retval)
{
    if (nargs < 1 || !njs_is_object(&args[0])) {
        njs_type_error(vm, "\"this\" argument is not an object");
        return NJS_ERROR;
    }

    return njs_error_to_string2(vm, retval, &args[0], 0);
}


static njs_int_t
njs_error_prototype_stack(njs_vm_t *vm, njs_object_prop_t *prop, uint32_t unused,
    njs_value_t *value, njs_value_t *setval, njs_value_t *retval)
{
    njs_int_t          ret;
    njs_str_t          string;
    njs_arr_t          *stack, *backtrace;
    njs_uint_t         i;
    njs_value_t        rv, *stackval;
    njs_stack_entry_t  *se;

    if (retval != NULL) {
        if (!njs_is_error(value)) {
            njs_set_undefined(retval);
            return NJS_DECLINED;
        }

        stackval = njs_object_value(value);

        if (setval != NULL) {
            njs_value_assign(stackval, setval);
            return NJS_OK;
        }

        if (!njs_is_data(stackval, NJS_DATA_TAG_ANY)) {
            njs_value_assign(retval, stackval);
            return NJS_OK;
        }

        stack = njs_data(stackval);
        if (stack == NULL) {
            njs_set_undefined(retval);
            return NJS_OK;
        }

        se = stack->start;

        backtrace = njs_arr_create(vm->mem_pool, stack->items,
                                   sizeof(njs_backtrace_entry_t));
        if (njs_slow_path(backtrace == NULL)) {
            return NJS_ERROR;
        }

        for (i = 0; i < stack->items; i++) {
            if (njs_add_backtrace_entry(vm, backtrace, &se[i]) != NJS_OK) {
                return NJS_ERROR;
            }
        }

        ret = njs_error_to_string2(vm, &rv, value, 0);
        if (njs_slow_path(ret != NJS_OK)) {
            return ret;
        }

        njs_string_get(vm, &rv, &string);

        ret = njs_backtrace_to_string(vm, backtrace, &string);

        njs_arr_destroy(backtrace);
        njs_arr_destroy(stack);

        if (njs_slow_path(ret != NJS_OK)) {
            return ret;
        }

        ret = njs_string_create(vm, stackval, string.start, string.length);
        if (njs_slow_path(ret != NJS_OK)) {
            return ret;
        }

        njs_value_assign(retval, stackval);

        return NJS_OK;
    }

    /* Delete. */

    if (njs_is_error(value)) {
        stackval = njs_object_value(value);
        njs_set_data(stackval, NULL, NJS_DATA_TAG_ANY);
    }

    return NJS_OK;
}


njs_int_t
njs_error_to_string(njs_vm_t *vm, njs_value_t *retval, const njs_value_t *error)
{
    if (njs_slow_path(!njs_is_object(error))) {
        njs_type_error(vm, "\"error\" is not an object");
        return NJS_ERROR;
    }

    return njs_error_to_string2(vm, retval, error, 1);
}


static const njs_object_prop_init_t  njs_error_prototype_properties[] =
{
    NJS_DECLARE_PROP_HANDLER(STRING_constructor,
                             njs_object_prototype_create_constructor, 0,
                             NJS_OBJECT_PROP_VALUE_CW),

    NJS_DECLARE_PROP_VALUE(STRING_name, njs_ascii_strval("Error"),
                           NJS_OBJECT_PROP_VALUE_CW),

    NJS_DECLARE_PROP_VALUE(STRING_message, njs_ascii_strval(""),
                           NJS_OBJECT_PROP_VALUE_CW),

    NJS_DECLARE_PROP_NATIVE(STRING_valueOf, njs_error_prototype_value_of,
                            0, 0),

    NJS_DECLARE_PROP_NATIVE(STRING_toString, njs_error_prototype_to_string,
                            0, 0),

    NJS_DECLARE_PROP_HANDLER(STRING_stack, njs_error_prototype_stack,
                             0, NJS_OBJECT_PROP_VALUE_CW),
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


static const njs_object_prop_init_t  njs_eval_error_prototype_properties[] =
{
    NJS_DECLARE_PROP_HANDLER(STRING_constructor,
                             njs_object_prototype_create_constructor, 0,
                             NJS_OBJECT_PROP_VALUE_CW),

    NJS_DECLARE_PROP_VALUE(STRING_name, njs_ascii_strval("EvalError"),
                           NJS_OBJECT_PROP_VALUE_CW),

    NJS_DECLARE_PROP_VALUE(STRING_message, njs_ascii_strval(""),
                           NJS_OBJECT_PROP_VALUE_CW),
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
    njs_uint_t nargs, njs_index_t unused, njs_value_t *retval)
{
    if (nargs >= 1 && njs_is_object(&args[0])) {
        /* MemoryError is a nonextensible internal error. */
        if (!njs_object(&args[0])->extensible) {
            njs_atom_to_value(vm, retval, NJS_ATOM_STRING_MemoryError);
            return NJS_OK;
        }
    }

    return njs_error_prototype_to_string(vm, args, nargs, unused, retval);
}


static const njs_object_prop_init_t  njs_internal_error_prototype_properties[] =
{
    NJS_DECLARE_PROP_VALUE(STRING_name, njs_ascii_strval("InternalError"),
                           NJS_OBJECT_PROP_VALUE_CW),

    NJS_DECLARE_PROP_VALUE(STRING_message, njs_ascii_strval(""),
                           NJS_OBJECT_PROP_VALUE_CW),

    NJS_DECLARE_PROP_NATIVE(STRING_toString,
                            njs_internal_error_prototype_to_string, 0, 0),
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


static const njs_object_prop_init_t  njs_range_error_prototype_properties[] =
{
    NJS_DECLARE_PROP_HANDLER(STRING_constructor,
                             njs_object_prototype_create_constructor, 0,
                             NJS_OBJECT_PROP_VALUE_CW),

    NJS_DECLARE_PROP_VALUE(STRING_name, njs_ascii_strval("RangeError"),
                           NJS_OBJECT_PROP_VALUE_CW),

    NJS_DECLARE_PROP_VALUE(STRING_message, njs_ascii_strval(""),
                           NJS_OBJECT_PROP_VALUE_CW),
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


static const njs_object_prop_init_t  njs_reference_error_prototype_properties[] =
{
    NJS_DECLARE_PROP_HANDLER(STRING_constructor,
                             njs_object_prototype_create_constructor, 0,
                             NJS_OBJECT_PROP_VALUE_CW),

    NJS_DECLARE_PROP_VALUE(STRING_name, njs_ascii_strval("ReferenceError"),
                           NJS_OBJECT_PROP_VALUE_CW),

    NJS_DECLARE_PROP_VALUE(STRING_message, njs_ascii_strval(""),
                           NJS_OBJECT_PROP_VALUE_CW),
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


static const njs_object_prop_init_t  njs_syntax_error_prototype_properties[] =
{
    NJS_DECLARE_PROP_HANDLER(STRING_constructor,
                             njs_object_prototype_create_constructor, 0,
                             NJS_OBJECT_PROP_VALUE_CW),

    NJS_DECLARE_PROP_VALUE(STRING_name, njs_ascii_strval("SyntaxError"),
                           NJS_OBJECT_PROP_VALUE_CW),

    NJS_DECLARE_PROP_VALUE(STRING_message, njs_ascii_strval(""),
                           NJS_OBJECT_PROP_VALUE_CW),
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


static const njs_object_prop_init_t  njs_type_error_prototype_properties[] =
{
    NJS_DECLARE_PROP_HANDLER(STRING_constructor,
                             njs_object_prototype_create_constructor, 0,
                             NJS_OBJECT_PROP_VALUE_CW),

    NJS_DECLARE_PROP_VALUE(STRING_name, njs_ascii_strval("TypeError"),
                           NJS_OBJECT_PROP_VALUE_CW),

    NJS_DECLARE_PROP_VALUE(STRING_message, njs_ascii_strval(""),
                           NJS_OBJECT_PROP_VALUE_CW),
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


static const njs_object_prop_init_t  njs_uri_error_prototype_properties[] =
{
    NJS_DECLARE_PROP_HANDLER(STRING_constructor,
                             njs_object_prototype_create_constructor, 0,
                             NJS_OBJECT_PROP_VALUE_CW),

    NJS_DECLARE_PROP_VALUE(STRING_name, njs_ascii_strval("URIError"),
                           NJS_OBJECT_PROP_VALUE_CW),

    NJS_DECLARE_PROP_VALUE(STRING_message, njs_ascii_strval(""),
                           NJS_OBJECT_PROP_VALUE_CW),
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


static const njs_object_prop_init_t  njs_aggregate_error_prototype_properties[] =
{
    NJS_DECLARE_PROP_HANDLER(STRING_constructor,
                             njs_object_prototype_create_constructor, 0,
                             NJS_OBJECT_PROP_VALUE_CW),

    NJS_DECLARE_PROP_VALUE(STRING_name, njs_ascii_strval("AggregateError"),
                           NJS_OBJECT_PROP_VALUE_CW),

    NJS_DECLARE_PROP_VALUE(STRING_message, njs_ascii_strval(""),
                           NJS_OBJECT_PROP_VALUE_CW),
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
    njs_stack_entry_t *se)
{
    njs_int_t              ret;
    njs_vm_code_t          *code;
    njs_function_t         *function;
    njs_backtrace_entry_t  *be;

    function = se->native ? se->u.function : NULL;

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

    code = njs_lookup_code(vm, se->u.pc);

    if (code != NULL) {
        be->name = code->name;

        if (be->name.length == 0) {
            be->name = njs_entry_anonymous;
        }

        be->line = njs_lookup_line(code->lines, se->u.pc - code->start);
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

    NJS_CHB_MP_INIT(&chain, njs_vm_memory_pool(vm));

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
