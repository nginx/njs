
/*
 * Copyright (C) Dmitry Volyntsev
 * Copyright (C) NGINX, Inc.
 */


#include <njs_main.h>


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


void
njs_error_stack_attach(njs_vm_t *vm, njs_value_t value)
{
    size_t              count;
    uint32_t            line, prev_line;
    njs_int_t           ret;
    njs_str_t           name, file, prev_name;
    njs_chb_t           chain;
    njs_value_t         *stackval;
    njs_vm_code_t       *code;
    njs_function_t      *function;
    njs_native_frame_t  *frame;

    if (njs_slow_path(!vm->options.backtrace
                      || !njs_is_error(&value))
                      || njs_object(&value)->stack_attached)
    {
        return;
    }

    NJS_CHB_MP_INIT(&chain, vm->mem_pool);

    count = 0;
    prev_line = 0;
    prev_name = njs_str_value("");

    for (frame = vm->top_frame; frame != NULL; frame = frame->previous) {
        function = frame->native ? frame->function : NULL;

        if (function != NULL && function->bound != NULL) {
            continue;
        }

        line = 0;
        file = njs_str_value("");

        if (!frame->native) {
            if (frame->pc == NULL) {
                continue;
            }

            code = njs_lookup_code(vm, frame->pc);

            if (code != NULL) {
                name = code->name;

                if (name.length == 0) {
                    name = njs_entry_anonymous;
                }

                line = njs_lookup_line(code->lines, frame->pc - code->start);

                if (!vm->options.quiet) {
                    file = code->file;
                }

            } else {
                name = njs_entry_unknown;
            }

        } else {
            ret = njs_builtin_match_native_function(vm, function, &name);
            if (ret != NJS_OK) {
                name = njs_entry_unknown;
            }
        }

        if (count != 0 && name.start == prev_name.start
            && line == prev_line)
        {
            count++;
            continue;
        }

        if (count > 1) {
            njs_chb_sprintf(&chain, 64, "      repeats %uz times\n", count);
        }

        count = 1;
        prev_name = name;
        prev_line = line;

        njs_chb_sprintf(&chain, 10 + name.length, "    at %V ", &name);

        if (line != 0) {
            njs_chb_sprintf(&chain, 12 + file.length, "(%V:%uD)\n",
                            &file, line);
        } else {
            njs_chb_append_literal(&chain, "(native)\n");
        }
    }

    if (count > 1) {
        njs_chb_sprintf(&chain, 64, "      repeats %uz times\n", count);
    }

    if (njs_chb_size(&chain) == 0) {
        return;
    }

    stackval = njs_object_value(&value);

    ret = njs_string_create_chb(vm, stackval, &chain);

    njs_chb_destroy(&chain);

    if (njs_fast_path(ret == NJS_OK)) {
        njs_object(&value)->stack_attached = 1;
    }
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
    njs_flathsh_query_t  fhq;

    ov = njs_mp_alloc(vm->mem_pool, sizeof(njs_object_value_t));
    if (njs_slow_path(ov == NULL)) {
        goto memory_error;
    }

    njs_set_undefined(&ov->value);

    error = &ov->object;
    njs_flathsh_init(&error->hash);
    njs_flathsh_init(&error->shared_hash);
    error->type = NJS_OBJECT_VALUE;
    error->shared = 0;
    error->extensible = 1;
    error->fast_array = 0;
    error->error_data = 1;
    error->stack_attached = 0;
    error->__proto__ = proto;
    error->slots = NULL;

    fhq.replace = 0;
    fhq.pool = vm->mem_pool;
    fhq.proto = &njs_object_hash_proto;

    if (name != NULL) {
        fhq.key_hash = NJS_ATOM_STRING_name;

        ret = njs_flathsh_unique_insert(&error->hash, &fhq);
        if (njs_slow_path(ret != NJS_OK)) {
            njs_internal_error(vm, "flathsh insert failed");
            return NULL;
        }

        prop = fhq.value;

        prop->type = NJS_PROPERTY;
        prop->enumerable = 1;
        prop->configurable = 1;
        prop->writable = 1;

        prop->u.value = *name;
    }

    if (message!= NULL) {
        fhq.key_hash = NJS_ATOM_STRING_message;

        ret = njs_flathsh_unique_insert(&error->hash, &fhq);
        if (njs_slow_path(ret != NJS_OK)) {
            njs_internal_error(vm, "flathsh insert failed");
            return NULL;
        }

        prop = fhq.value;

        prop->type = NJS_PROPERTY;
        prop->enumerable = 0;
        prop->configurable = 1;
        prop->writable = 1;

        prop->u.value = *message;
    }

    if (errors != NULL) {
        fhq.key_hash = NJS_ATOM_STRING_errors;

        ret = njs_flathsh_unique_insert(&error->hash, &fhq);
        if (njs_slow_path(ret != NJS_OK)) {
            njs_internal_error(vm, "flathsh insert failed");
            return NULL;
        }

        prop = fhq.value;

        prop->type = NJS_PROPERTY;
        prop->enumerable = 0;
        prop->configurable = 1;
        prop->writable = 1;

        prop->u.value = *errors;
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
    njs_flathsh_init(&object->hash);
    njs_flathsh_init(&object->shared_hash);
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
    u_char       *p;
    size_t       length;
    njs_int_t    ret;
    njs_str_t    msg, trace;
    njs_value_t  msg_val, *stackval;

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

        if (!njs_is_string(stackval)) {
            njs_value_assign(retval, stackval);
            return NJS_OK;
        }

        ret = njs_error_to_string2(vm, &msg_val, value, 0);
        if (njs_slow_path(ret != NJS_OK)) {
            return ret;
        }

        njs_string_get(vm, &msg_val, &msg);
        njs_string_get(vm, stackval, &trace);

        length = msg.length + 1 + trace.length;

        p = njs_string_alloc(vm, retval, msg.length + 1 + trace.length, length);
        if (njs_slow_path(p == NULL)) {
            return NJS_ERROR;
        }

        p = njs_cpymem(p, msg.start, msg.length);
        *p++ = '\n';
        memcpy(p, trace.start, trace.length);

        return NJS_OK;
    }

    /* Delete. */

    if (njs_is_error(value)) {
        stackval = njs_object_value(value);
        njs_set_undefined(stackval);
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
