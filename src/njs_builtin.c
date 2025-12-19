
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) Dmitry Volyntsev
 * Copyright (C) NGINX, Inc.
 */


#include <njs_main.h>
#include <signal.h>


typedef struct {
    njs_str_t       name;
    int             value;
} njs_signal_entry_t;


static njs_int_t njs_global_this_prop_handler(njs_vm_t *vm,
    njs_object_prop_t *self, uint32_t atom_id, njs_value_t *global,
    njs_value_t *setval, njs_value_t *retval);

static njs_int_t njs_env_hash_init(njs_vm_t *vm, njs_flathsh_t *hash,
    char **environment);


static const njs_object_init_t  njs_global_this_init;
static const njs_object_init_t  njs_njs_object_init;
static const njs_object_init_t  njs_process_object_init;


static const njs_object_init_t  *njs_object_init[] = {
    &njs_global_this_init,
    &njs_njs_object_init,
    &njs_process_object_init,
    &njs_math_object_init,
    &njs_atomics_object_init,
    &njs_json_object_init,
    NULL
};


static const njs_object_type_init_t *const
    njs_object_type_init[NJS_OBJ_TYPE_MAX] =
{
    /* Global types. */

    &njs_obj_type_init,
    &njs_array_type_init,
    &njs_boolean_type_init,
    &njs_number_type_init,
    &njs_symbol_type_init,
    &njs_string_type_init,
    &njs_function_type_init,
    &njs_async_function_type_init,
    &njs_regexp_type_init,
    &njs_date_type_init,
    &njs_promise_type_init,
    &njs_array_buffer_type_init,
    &njs_shared_array_buffer_type_init,
    &njs_data_view_type_init,
    &njs_text_decoder_type_init,
    &njs_text_encoder_type_init,
    &njs_buffer_type_init,

    /* Hidden types. */

    &njs_iterator_type_init,
    &njs_array_iterator_type_init,
    &njs_typed_array_type_init,

    /* TypedArray types. */

    &njs_typed_array_u8_type_init,
    &njs_typed_array_u8clamped_type_init,
    &njs_typed_array_i8_type_init,
    &njs_typed_array_u16_type_init,
    &njs_typed_array_i16_type_init,
    &njs_typed_array_u32_type_init,
    &njs_typed_array_i32_type_init,
    &njs_typed_array_f32_type_init,
    &njs_typed_array_f64_type_init,

    /* Error types. */
    &njs_error_type_init,
    &njs_eval_error_type_init,
    &njs_internal_error_type_init,
    &njs_range_error_type_init,
    &njs_reference_error_type_init,
    &njs_syntax_error_type_init,
    &njs_type_error_type_init,
    &njs_uri_error_type_init,
    &njs_memory_error_type_init,
    &njs_aggregate_error_type_init,
};


/* P1990 signals from `man 7 signal` are supported */
static njs_signal_entry_t njs_signals_table[] = {
    { njs_str("ABRT"), SIGABRT },
    { njs_str("ALRM"), SIGALRM },
    { njs_str("CHLD"), SIGCHLD },
    { njs_str("CONT"), SIGCONT },
    { njs_str("FPE"),  SIGFPE  },
    { njs_str("HUP"),  SIGHUP  },
    { njs_str("ILL"),  SIGILL  },
    { njs_str("INT"),  SIGINT  },
    { njs_str("KILL"), SIGKILL },
    { njs_str("PIPE"), SIGPIPE },
    { njs_str("QUIT"), SIGQUIT },
    { njs_str("SEGV"), SIGSEGV },
    { njs_str("STOP"), SIGSTOP },
    { njs_str("TSTP"), SIGTSTP },
    { njs_str("TERM"), SIGTERM },
    { njs_str("TTIN"), SIGTTIN },
    { njs_str("TTOU"), SIGTTOU },
    { njs_str("USR1"), SIGUSR1 },
    { njs_str("USR2"), SIGUSR2 },
    { njs_null_str, 0 }
};


njs_inline njs_int_t
njs_object_hash_init(njs_vm_t *vm, njs_flathsh_t *hash,
    const njs_object_init_t *init)
{
    return njs_object_hash_create(vm, hash, init->properties, init->items);
}


njs_int_t
njs_builtin_objects_create(njs_vm_t *vm)
{
    njs_int_t                  ret, index;
    njs_uint_t                 i;
    njs_object_t               *object, *string_object;
    njs_function_t             *constructor;
    njs_vm_shared_t            *shared;
    njs_regexp_pattern_t       *pattern;
    njs_object_prototype_t     *prototype;
    const njs_object_init_t    *obj, **p;

    shared = njs_mp_zalloc(vm->mem_pool, sizeof(njs_vm_shared_t));
    if (njs_slow_path(shared == NULL)) {
        return NJS_ERROR;
    }

    vm->shared = shared;

    njs_flathsh_init(&shared->values_hash);

    vm->atom_id_generator = njs_atom_hash_init(vm);
    if (njs_slow_path(vm->atom_id_generator == 0xffffffff)) {
        return NJS_ERROR;
    }

    pattern = njs_regexp_pattern_create(vm, (u_char *) "(?:)",
                                        njs_length("(?:)"), 0);
    if (njs_slow_path(pattern == NULL)) {
        return NJS_ERROR;
    }

    shared->empty_regexp_pattern = pattern;

    ret = njs_object_hash_init(vm, &shared->array_instance_hash,
                               &njs_array_instance_init);
    if (njs_slow_path(ret != NJS_OK)) {
        return NJS_ERROR;
    }

    ret = njs_object_hash_init(vm, &shared->string_instance_hash,
                               &njs_string_instance_init);
    if (njs_slow_path(ret != NJS_OK)) {
        return NJS_ERROR;
    }

    ret = njs_object_hash_init(vm, &shared->function_instance_hash,
                               &njs_function_instance_init);
    if (njs_slow_path(ret != NJS_OK)) {
        return NJS_ERROR;
    }

    ret = njs_object_hash_init(vm, &shared->async_function_instance_hash,
                               &njs_async_function_instance_init);
    if (njs_slow_path(ret != NJS_OK)) {
        return NJS_ERROR;
    }

    ret = njs_object_hash_init(vm, &shared->arrow_instance_hash,
                               &njs_arrow_instance_init);
    if (njs_slow_path(ret != NJS_OK)) {
        return NJS_ERROR;
    }

    ret = njs_object_hash_init(vm, &shared->arguments_object_instance_hash,
                               &njs_arguments_object_instance_init);
    if (njs_slow_path(ret != NJS_OK)) {
        return NJS_ERROR;
    }

    ret = njs_object_hash_init(vm, &shared->regexp_instance_hash,
                               &njs_regexp_instance_init);
    if (njs_slow_path(ret != NJS_OK)) {
        return NJS_ERROR;
    }

    object = shared->objects;

    for (p = njs_object_init; *p != NULL; p++) {
        obj = *p;

        ret = njs_object_hash_init(vm, &object->shared_hash, obj);
        if (njs_slow_path(ret != NJS_OK)) {
            return NJS_ERROR;
        }

        object->type = NJS_OBJECT;
        object->shared = 1;
        object->extensible = 1;

        object++;
    }

    ret = njs_env_hash_init(vm, &shared->env_hash, environ);
    if (njs_slow_path(ret != NJS_OK)) {
        return NJS_ERROR;
    }

    for (i = NJS_OBJ_TYPE_OBJECT; i < NJS_OBJ_TYPE_MAX; i++) {
        index = njs_vm_ctor_push(vm);
        if (njs_slow_path(index < 0)) {
            return NJS_ERROR;
        }

        njs_assert_msg((njs_uint_t) index == i,
                       "ctor index should match object type");

        prototype = njs_shared_prototype(shared, i);
        *prototype = njs_object_type_init[i]->prototype_value;

        if (njs_object_type_init[i] == &njs_boolean_type_init) {
            prototype->object_value.value = njs_value(NJS_BOOLEAN, 0, 0.0);

        } else if (njs_object_type_init[i] == &njs_number_type_init) {
            prototype->object_value.value = njs_value(NJS_NUMBER, 0, 0.0);

        } else if (njs_object_type_init[i] == &njs_string_type_init) {
            njs_set_empty_string(vm, &prototype->object_value.value);
        }

        ret = njs_object_hash_init(vm, &prototype->object.shared_hash,
                                   njs_object_type_init[i]->prototype_props);
        if (njs_slow_path(ret != NJS_OK)) {
            return NJS_ERROR;
        }

        prototype->object.extensible = 1;
    }

    prototype = njs_shared_prototype(shared, NJS_OBJ_TYPE_REGEXP);
    prototype->regexp.pattern = shared->empty_regexp_pattern;

    for (i = NJS_OBJ_TYPE_OBJECT; i < NJS_OBJ_TYPE_MAX; i++) {
        constructor = njs_shared_ctor(shared, i);

        if (njs_object_type_init[i]->constructor_props == NULL) {
            njs_memzero(constructor, sizeof(njs_function_t));
            continue;
        }

        *constructor = njs_object_type_init[i]->constructor;
        constructor->object.shared = 0;

        ret = njs_object_hash_init(vm, &constructor->object.shared_hash,
                                   njs_object_type_init[i]->constructor_props);
        if (njs_slow_path(ret != NJS_OK)) {
            return NJS_ERROR;
        }
    }

    shared->global_slots.prop_handler = njs_global_this_prop_handler;
    shared->global_slots.writable = 1;
    shared->global_slots.configurable = 1;
    shared->global_slots.enumerable = 1;

    shared->objects[0].slots = &shared->global_slots;

    vm->global_object = shared->objects[0];
    vm->global_object.shared = 0;

    string_object = &shared->string_object;
    njs_flathsh_init(&string_object->hash);
    string_object->shared_hash = shared->string_instance_hash;
    string_object->type = NJS_OBJECT_VALUE;
    string_object->shared = 1;
    string_object->extensible = 0;

    njs_flathsh_init(&shared->modules_hash);

    return NJS_OK;
}


static njs_int_t
njs_ext_dump(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t unused, njs_value_t *retval)
{
    uint32_t     n;
    njs_int_t    ret;
    njs_str_t    str;
    njs_value_t  *value, *indent;

    value = njs_arg(args, nargs, 1);
    indent = njs_arg(args, nargs, 2);

    ret = njs_value_to_uint32(vm, indent, &n);
    if (njs_slow_path(ret != NJS_OK)) {
        return ret;
    }

    n = njs_min(n, 5);

    if (njs_vm_value_dump(vm, &str, value, 1, n) != NJS_OK) {
        return NJS_ERROR;
    }

    return njs_string_create(vm, retval, str.start, str.length);
}


static njs_int_t
njs_ext_on(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t unused, njs_value_t *retval)
{
    njs_str_t    type;
    njs_uint_t   i, n;
    njs_value_t  *value;

    static const struct {
        njs_str_t   name;
        njs_uint_t  id;
    } hooks[] = {
        {
            njs_str("exit"),
            NJS_HOOK_EXIT
        },
    };

    value = njs_arg(args, nargs, 1);

    if (njs_slow_path(!njs_is_string(value))) {
        njs_type_error(vm, "hook type is not a string");
        return NJS_ERROR;
    }

    njs_string_get(vm, value, &type);

    i = 0;
    n = sizeof(hooks) / sizeof(hooks[0]);

    while (i < n) {
        if (njs_strstr_eq(&type, &hooks[i].name)) {
            break;
        }

        i++;
    }

    if (i == n) {
        njs_type_error(vm, "unknown hook type \"%V\"", &type);
        return NJS_ERROR;
    }

    value = njs_arg(args, nargs, 2);

    if (njs_slow_path(!njs_is_function(value) && !njs_is_null(value))) {
        njs_type_error(vm, "callback is not a function or null");
        return NJS_ERROR;
    }

    vm->hooks[i] = njs_is_function(value) ? njs_function(value) : NULL;

    return NJS_OK;
}


static njs_int_t
njs_ext_memory_stats(njs_vm_t *vm, njs_object_prop_t *prop, uint32_t unused,
    njs_value_t *unused2, njs_value_t *unused3, njs_value_t *retval)
{
    njs_int_t      ret;
    njs_value_t    object, value;
    njs_object_t   *stat;
    njs_mp_stat_t  mp_stat;

    stat = njs_object_alloc(vm);
    if (njs_slow_path(stat == NULL)) {
        return NJS_ERROR;
    }

    njs_set_object(&object, stat);

    njs_mp_stat(vm->mem_pool, &mp_stat);

    njs_set_number(&value, mp_stat.size);

    ret = njs_value_property_set(vm, &object, NJS_ATOM_STRING_size, &value);
    if (njs_slow_path(ret != NJS_OK)) {
        return NJS_ERROR;
    }

    njs_set_number(&value, mp_stat.nblocks);

    ret = njs_value_property_set(vm, &object, NJS_ATOM_STRING_nblocks, &value);
    if (njs_slow_path(ret != NJS_OK)) {
        return NJS_ERROR;
    }

    njs_set_number(&value, mp_stat.cluster_size);

    ret = njs_value_property_set(vm, &object, NJS_ATOM_STRING_cluster_size,
                                 &value);
    if (njs_slow_path(ret != NJS_OK)) {
        return NJS_ERROR;
    }

    njs_set_number(&value, mp_stat.page_size);

    ret = njs_value_property_set(vm, &object, NJS_ATOM_STRING_page_size, &value);
    if (njs_slow_path(ret != NJS_OK)) {
        return NJS_ERROR;
    }

    njs_set_object(retval, stat);

    return NJS_OK;
}




static njs_int_t
njs_global_this_prop_handler(njs_vm_t *vm, njs_object_prop_t *prop,
    uint32_t atom_id, njs_value_t *global, njs_value_t *setval,
    njs_value_t *retval)
{
    njs_value_t          *value;
    njs_variable_t       *var;
    njs_rbtree_node_t    *rb_node;
    njs_variable_node_t  *node, var_node;

    if (retval == NULL) {
        return NJS_DECLINED;
    }

    var_node.key = atom_id;

    if (njs_slow_path(vm->global_scope == NULL)) {
        return NJS_DECLINED;
    }

    rb_node = njs_rbtree_find(&vm->global_scope->variables, &var_node.node);
    if (rb_node == NULL) {
        return NJS_DECLINED;
    }

    node = (njs_variable_node_t *) rb_node;

    var = node->variable;

    if (var->type == NJS_VARIABLE_LET || var->type == NJS_VARIABLE_CONST) {
        return NJS_DECLINED;
    }

    value = njs_scope_valid_value(vm, var->index);

    if (setval != NULL) {
        njs_value_assign(value, setval);
    }

    njs_value_assign(retval, value);

    return NJS_OK;
}


static njs_int_t
njs_global_this_object(njs_vm_t *vm, njs_object_prop_t *self, uint32_t atom_id,
    njs_value_t *global, njs_value_t *setval, njs_value_t *retval)
{
    njs_int_t            ret;
    njs_object_prop_t    *prop;
    njs_flathsh_query_t  fhq;

    if (retval == NULL) {
        return NJS_DECLINED;
    }

    njs_value_assign(retval, global);

    if (njs_slow_path(setval != NULL)) {
        njs_value_assign(retval, setval);
    }

    fhq.key_hash = atom_id;
    fhq.replace = 1;
    fhq.pool = vm->mem_pool;
    fhq.proto = &njs_object_hash_proto;

    ret = njs_flathsh_unique_insert(njs_object_hash(global), &fhq);
    if (njs_slow_path(ret != NJS_OK)) {
        njs_internal_error(vm, "flathsh insert/replace failed");
        return NJS_ERROR;
    }

    prop = fhq.value;

    prop->type = NJS_PROPERTY;
    prop->enumerable = self->enumerable;
    prop->configurable = 1;
    prop->writable = 1;
    prop->u.value = *retval;

    return NJS_OK;
}


static njs_int_t
njs_top_level_object(njs_vm_t *vm, njs_object_prop_t *self, uint32_t atom_id,
    njs_value_t *global, njs_value_t *setval, njs_value_t *retval)
{
    njs_int_t            ret;
    njs_object_t         *object;
    njs_object_prop_t    *prop;
    njs_flathsh_query_t  fhq;

    if (njs_slow_path(setval != NULL)) {
        njs_value_assign(retval, setval);

    } else {
        if (njs_slow_path(retval == NULL)) {
            return NJS_DECLINED;
        }

        njs_set_object(retval, &vm->shared->objects[njs_prop_magic16(self)]);

        object = njs_object_value_copy(vm, retval);
        if (njs_slow_path(object == NULL)) {
            return NJS_ERROR;
        }

        object->__proto__ = njs_vm_proto(vm, NJS_OBJ_TYPE_OBJECT);
    }

    fhq.key_hash = atom_id;
    fhq.replace = 1;
    fhq.pool = vm->mem_pool;
    fhq.proto = &njs_object_hash_proto;

    ret = njs_flathsh_unique_insert(njs_object_hash(global), &fhq);
    if (njs_slow_path(ret != NJS_OK)) {
        njs_internal_error(vm, "flathsh insert/replace failed");
        return NJS_ERROR;
    }

    prop = fhq.value;

    prop->type = NJS_PROPERTY;
    prop->enumerable = self->enumerable;
    prop->configurable = 1;
    prop->writable = 1;
    prop->u.value = *retval;

    return NJS_OK;
}


static njs_int_t
njs_top_level_constructor(njs_vm_t *vm, njs_object_prop_t *self,
    uint32_t atom_id, njs_value_t *global, njs_value_t *setval,
    njs_value_t *retval)
{
    njs_int_t            ret;
    njs_function_t       *ctor;
    njs_object_prop_t    *prop;
    njs_flathsh_query_t  fhq;

    if (njs_slow_path(setval != NULL)) {
        njs_value_assign(retval, setval);

    } else {
        if (njs_slow_path(retval == NULL)) {
            return NJS_DECLINED;
        }

        ctor = &njs_vm_ctor(vm, njs_prop_magic16(self));

        njs_set_function(retval, ctor);

        return NJS_OK;
    }

    fhq.key_hash = atom_id;
    fhq.replace = 1;
    fhq.pool = vm->mem_pool;
    fhq.proto = &njs_object_hash_proto;

    ret = njs_flathsh_unique_insert(njs_object_hash(global), &fhq);
    if (njs_slow_path(ret != NJS_OK)) {
        njs_internal_error(vm, "flathsh insert/replace failed");
        return NJS_ERROR;
    }

    prop = fhq.value;

    prop->type = NJS_PROPERTY;
    prop->enumerable = 0;
    prop->configurable = 1;
    prop->writable = 1;
    prop->u.value = *retval;

    return NJS_OK;
}


static const njs_object_prop_init_t  njs_global_this_object_properties[] =
{
    NJS_DECLARE_PROP_VALUE(SYMBOL_toStringTag, njs_ascii_strval("global"),
                           NJS_OBJECT_PROP_VALUE_C),

    /* Global aliases. */

    NJS_DECLARE_PROP_HANDLER(STRING_global, njs_global_this_object, 0,
                             NJS_OBJECT_PROP_VALUE_ECW),

    NJS_DECLARE_PROP_HANDLER(STRING_globalThis, njs_global_this_object, 0,
                             NJS_OBJECT_PROP_VALUE_CW),

    /* Global constants. */

    NJS_DECLARE_PROP_VALUE(STRING_NaN,  njs_value(NJS_NUMBER, 0, NAN), 0),

    NJS_DECLARE_PROP_VALUE(STRING_Infinity,
                           njs_value(NJS_NUMBER, 1, INFINITY), 0),

    NJS_DECLARE_PROP_VALUE(STRING_undefined,
                           njs_value(NJS_UNDEFINED, 0, NAN), 0),

    /* Global functions. */

    NJS_DECLARE_PROP_NATIVE(STRING_isFinite, njs_number_global_is_finite, 1,
                            0),

    NJS_DECLARE_PROP_NATIVE(STRING_isNaN, njs_number_global_is_nan, 1, 0),

    NJS_DECLARE_PROP_NATIVE(STRING_parseFloat, njs_number_parse_float, 1,
                            0),

    NJS_DECLARE_PROP_NATIVE(STRING_parseInt, njs_number_parse_int, 2, 0),

    NJS_DECLARE_PROP_NATIVE(STRING_toString, njs_object_prototype_to_string,
                            0, 0),

    NJS_DECLARE_PROP_NATIVE(STRING_encodeURI, njs_string_encode_uri, 1, 0),

    NJS_DECLARE_PROP_NATIVE(STRING_encodeURIComponent,
                            njs_string_encode_uri, 1, 1),

    NJS_DECLARE_PROP_NATIVE(STRING_decodeURI, njs_string_decode_uri, 1, 0),

    NJS_DECLARE_PROP_NATIVE(STRING_decodeURIComponent,
                            njs_string_decode_uri, 1, 1),

    NJS_DECLARE_PROP_NATIVE(STRING_atob, njs_string_atob, 1, 0),

    NJS_DECLARE_PROP_NATIVE(STRING_btoa, njs_string_btoa, 1, 0),

    NJS_DECLARE_PROP_NATIVE(STRING_eval, njs_eval_function, 1, 0),

    NJS_DECLARE_PROP_NATIVE(STRING_require, njs_module_require, 1, 0),

    /* Global objects. */

    NJS_DECLARE_PROP_HANDLER(STRING_njs, njs_top_level_object,
                             NJS_OBJECT_NJS, NJS_OBJECT_PROP_VALUE_ECW),

    NJS_DECLARE_PROP_HANDLER(STRING_process, njs_top_level_object,
                             NJS_OBJECT_PROCESS, NJS_OBJECT_PROP_VALUE_ECW),

    NJS_DECLARE_PROP_HANDLER(STRING_Math, njs_top_level_object,
                             NJS_OBJECT_MATH, NJS_OBJECT_PROP_VALUE_CW),

    NJS_DECLARE_PROP_HANDLER(STRING_Atomics, njs_top_level_object,
                             NJS_OBJECT_ATOMICS, NJS_OBJECT_PROP_VALUE_CW),

    NJS_DECLARE_PROP_HANDLER(STRING_JSON, njs_top_level_object,
                             NJS_OBJECT_JSON, NJS_OBJECT_PROP_VALUE_CW),


#ifdef NJS_TEST262
    NJS_DECLARE_PROP_HANDLER(STRING__262, njs_top_level_object,
                             NJS_OBJECT_262, NJS_OBJECT_PROP_VALUE_ECW),
#endif

    /* Global constructors. */

    NJS_DECLARE_PROP_HANDLER(STRING_Object, njs_top_level_constructor,
                             NJS_OBJ_TYPE_OBJECT, NJS_OBJECT_PROP_VALUE_CW),

    NJS_DECLARE_PROP_HANDLER(STRING_Array, njs_top_level_constructor,
                             NJS_OBJ_TYPE_ARRAY, NJS_OBJECT_PROP_VALUE_CW),

    NJS_DECLARE_PROP_HANDLER(STRING_ArrayBuffer, njs_top_level_constructor,
                             NJS_OBJ_TYPE_ARRAY_BUFFER,
                             NJS_OBJECT_PROP_VALUE_CW),

    NJS_DECLARE_PROP_HANDLER(STRING_SharedArrayBuffer,
                             njs_top_level_constructor,
                             NJS_OBJ_TYPE_SHARED_ARRAY_BUFFER,
                             NJS_OBJECT_PROP_VALUE_CW),

    NJS_DECLARE_PROP_HANDLER(STRING_DataView, njs_top_level_constructor,
                             NJS_OBJ_TYPE_DATA_VIEW,
                             NJS_OBJECT_PROP_VALUE_CW),

    NJS_DECLARE_PROP_HANDLER(STRING_TextDecoder, njs_top_level_constructor,
                             NJS_OBJ_TYPE_TEXT_DECODER,
                             NJS_OBJECT_PROP_VALUE_CW),

    NJS_DECLARE_PROP_HANDLER(STRING_TextEncoder, njs_top_level_constructor,
                             NJS_OBJ_TYPE_TEXT_ENCODER,
                             NJS_OBJECT_PROP_VALUE_CW),

    NJS_DECLARE_PROP_HANDLER(STRING_Buffer, njs_top_level_constructor,
                             NJS_OBJ_TYPE_BUFFER, NJS_OBJECT_PROP_VALUE_CW),

    NJS_DECLARE_PROP_HANDLER(STRING_Uint8Array, njs_top_level_constructor,
                             NJS_OBJ_TYPE_UINT8_ARRAY,
                             NJS_OBJECT_PROP_VALUE_CW),

    NJS_DECLARE_PROP_HANDLER(STRING_Uint16Array, njs_top_level_constructor,
                             NJS_OBJ_TYPE_UINT16_ARRAY,
                             NJS_OBJECT_PROP_VALUE_CW),

    NJS_DECLARE_PROP_HANDLER(STRING_Uint32Array, njs_top_level_constructor,
                             NJS_OBJ_TYPE_UINT32_ARRAY,
                             NJS_OBJECT_PROP_VALUE_CW),

    NJS_DECLARE_PROP_HANDLER(STRING_Int8Array, njs_top_level_constructor,
                             NJS_OBJ_TYPE_INT8_ARRAY,
                             NJS_OBJECT_PROP_VALUE_CW),

    NJS_DECLARE_PROP_HANDLER(STRING_Int16Array, njs_top_level_constructor,
                             NJS_OBJ_TYPE_INT16_ARRAY,
                             NJS_OBJECT_PROP_VALUE_CW),

    NJS_DECLARE_PROP_HANDLER(STRING_Int32Array, njs_top_level_constructor,
                             NJS_OBJ_TYPE_INT32_ARRAY,
                             NJS_OBJECT_PROP_VALUE_CW),

    NJS_DECLARE_PROP_HANDLER(STRING_Float32Array, njs_top_level_constructor,
                             NJS_OBJ_TYPE_FLOAT32_ARRAY,
                             NJS_OBJECT_PROP_VALUE_CW),

    NJS_DECLARE_PROP_HANDLER(STRING_Float64Array, njs_top_level_constructor,
                             NJS_OBJ_TYPE_FLOAT64_ARRAY,
                             NJS_OBJECT_PROP_VALUE_CW),

    NJS_DECLARE_PROP_HANDLER(STRING_Uint8ClampedArray,
                             njs_top_level_constructor,
                             NJS_OBJ_TYPE_UINT8_CLAMPED_ARRAY,
                             NJS_OBJECT_PROP_VALUE_CW),

    NJS_DECLARE_PROP_HANDLER(STRING_Boolean, njs_top_level_constructor,
                             NJS_OBJ_TYPE_BOOLEAN, NJS_OBJECT_PROP_VALUE_CW),

    NJS_DECLARE_PROP_HANDLER(STRING_Number, njs_top_level_constructor,
                             NJS_OBJ_TYPE_NUMBER, NJS_OBJECT_PROP_VALUE_CW),

    NJS_DECLARE_PROP_HANDLER(STRING_Symbol, njs_top_level_constructor,
                             NJS_OBJ_TYPE_SYMBOL, NJS_OBJECT_PROP_VALUE_CW),

    NJS_DECLARE_PROP_HANDLER(STRING_String, njs_top_level_constructor,
                             NJS_OBJ_TYPE_STRING, NJS_OBJECT_PROP_VALUE_CW),

    NJS_DECLARE_PROP_HANDLER(STRING_Function, njs_top_level_constructor,
                             NJS_OBJ_TYPE_FUNCTION, NJS_OBJECT_PROP_VALUE_CW),

    NJS_DECLARE_PROP_HANDLER(STRING_RegExp, njs_top_level_constructor,
                             NJS_OBJ_TYPE_REGEXP, NJS_OBJECT_PROP_VALUE_CW),

    NJS_DECLARE_PROP_HANDLER(STRING_Date, njs_top_level_constructor,
                             NJS_OBJ_TYPE_DATE, NJS_OBJECT_PROP_VALUE_CW),

    NJS_DECLARE_PROP_HANDLER(STRING_Promise, njs_top_level_constructor,
                             NJS_OBJ_TYPE_PROMISE, NJS_OBJECT_PROP_VALUE_CW),

    NJS_DECLARE_PROP_HANDLER(STRING_Error, njs_top_level_constructor,
                             NJS_OBJ_TYPE_ERROR, NJS_OBJECT_PROP_VALUE_CW),

    NJS_DECLARE_PROP_HANDLER(STRING_EvalError, njs_top_level_constructor,
                             NJS_OBJ_TYPE_EVAL_ERROR, NJS_OBJECT_PROP_VALUE_CW),

    NJS_DECLARE_PROP_HANDLER(STRING_InternalError,
                             njs_top_level_constructor,
                             NJS_OBJ_TYPE_INTERNAL_ERROR,
                             NJS_OBJECT_PROP_VALUE_CW),

    NJS_DECLARE_PROP_HANDLER(STRING_RangeError, njs_top_level_constructor,
                             NJS_OBJ_TYPE_RANGE_ERROR,
                             NJS_OBJECT_PROP_VALUE_CW),

    NJS_DECLARE_PROP_HANDLER(STRING_ReferenceError,
                             njs_top_level_constructor,
                             NJS_OBJ_TYPE_REF_ERROR, NJS_OBJECT_PROP_VALUE_CW),

    NJS_DECLARE_PROP_HANDLER(STRING_SyntaxError, njs_top_level_constructor,
                             NJS_OBJ_TYPE_SYNTAX_ERROR,
                             NJS_OBJECT_PROP_VALUE_CW),

    NJS_DECLARE_PROP_HANDLER(STRING_TypeError, njs_top_level_constructor,
                             NJS_OBJ_TYPE_TYPE_ERROR, NJS_OBJECT_PROP_VALUE_CW),

    NJS_DECLARE_PROP_HANDLER(STRING_URIError, njs_top_level_constructor,
                             NJS_OBJ_TYPE_URI_ERROR, NJS_OBJECT_PROP_VALUE_CW),

    NJS_DECLARE_PROP_HANDLER(STRING_MemoryError, njs_top_level_constructor,
                             NJS_OBJ_TYPE_MEMORY_ERROR,
                             NJS_OBJECT_PROP_VALUE_CW),

    NJS_DECLARE_PROP_HANDLER(STRING_AggregateError, njs_top_level_constructor,
                             NJS_OBJ_TYPE_AGGREGATE_ERROR,
                             NJS_OBJECT_PROP_VALUE_CW),
};


static const njs_object_init_t  njs_global_this_init = {
    njs_global_this_object_properties,
    njs_nitems(njs_global_this_object_properties)
};


static const njs_object_prop_init_t  njs_njs_object_properties[] =
{
    NJS_DECLARE_PROP_VALUE(SYMBOL_toStringTag, njs_ascii_strval("njs"),
                           NJS_OBJECT_PROP_VALUE_C),

    NJS_DECLARE_PROP_VALUE(STRING_engine, njs_ascii_strval("njs"),
                           NJS_OBJECT_PROP_VALUE_EC),

    NJS_DECLARE_PROP_VALUE(STRING_version, njs_ascii_strval(NJS_VERSION),
                           NJS_OBJECT_PROP_VALUE_EC),

    NJS_DECLARE_PROP_VALUE(STRING_version_number,
                           njs_value(NJS_NUMBER, 1, NJS_VERSION_NUMBER),
                           NJS_OBJECT_PROP_VALUE_EC),

    NJS_DECLARE_PROP_NATIVE(STRING_dump, njs_ext_dump, 0, 0),

    NJS_DECLARE_PROP_NATIVE(STRING_on, njs_ext_on, 0, 0),

    NJS_DECLARE_PROP_HANDLER(STRING_memoryStats, njs_ext_memory_stats, 0,
                             NJS_OBJECT_PROP_VALUE_EC),

};


static const njs_object_init_t  njs_njs_object_init = {
    njs_njs_object_properties,
    njs_nitems(njs_njs_object_properties),
};


static njs_int_t
njs_process_object_argv(njs_vm_t *vm, njs_object_prop_t *pr, uint32_t unused,
    njs_value_t *process, njs_value_t *unused2, njs_value_t *retval)
{
    char                 **arg;
    njs_int_t            ret;
    njs_uint_t           i;
    njs_array_t          *argv;
    njs_object_prop_t    *prop;
    njs_flathsh_query_t  fhq;

    argv = njs_array_alloc(vm, 1, vm->options.argc, 0);
    if (njs_slow_path(argv == NULL)) {
        return NJS_ERROR;
    }

    i = 0;

    for (arg = vm->options.argv; i < vm->options.argc; arg++) {
        ret = njs_string_create(vm, &argv->start[i++], (u_char *) *arg,
                                njs_strlen(*arg));
        if (njs_slow_path(ret != NJS_OK)) {
            return NJS_ERROR;
        }
    }

    fhq.key_hash = NJS_ATOM_STRING_argv;
    fhq.replace = 1;
    fhq.pool = vm->mem_pool;
    fhq.proto = &njs_object_hash_proto;

    ret = njs_flathsh_unique_insert(njs_object_hash(process), &fhq);
    if (njs_slow_path(ret != NJS_OK)) {
        njs_internal_error(vm, "flathsh insert failed");
        return NJS_ERROR;
    }

    prop = fhq.value;

    prop->type = NJS_PROPERTY;
    prop->enumerable = 1;
    prop->configurable = 1;
    prop->writable = 1;

    njs_set_array(njs_prop_value(prop), argv);

    njs_value_assign(retval, njs_prop_value(prop));
    return NJS_OK;

}


static njs_int_t
njs_env_hash_init(njs_vm_t *vm, njs_flathsh_t *hash, char **environment)
{
    char                 **ep;
    u_char               *dst;
    ssize_t              length;
    uint32_t             cp;
    njs_int_t            ret;
    njs_value_t          prop_name;
    const u_char         *val, *entry, *s, *end;
    njs_object_prop_t    *prop;
    njs_string_prop_t    string;
    njs_flathsh_query_t  fhq;

    fhq.replace = 0;
    fhq.pool = vm->mem_pool;
    fhq.proto = &njs_object_hash_proto;

    ep = environment;

    while (*ep != NULL) {

        entry = (u_char *) *ep++;

        val = njs_strchr(entry, '=');
        if (njs_slow_path(val == NULL)) {
            continue;
        }

        ret = njs_string_create(vm, &prop_name, entry, val - entry);
        if (njs_slow_path(ret != NJS_OK)) {
            return NJS_ERROR;
        }

        (void) njs_string_prop(vm, &string, &prop_name);

        length = string.length;
        s = string.start;
        end = s + string.size;
        dst = (u_char *) s;

        while (length != 0) {
            cp = njs_utf8_upper_case(&s, end);
            dst = njs_utf8_encode(dst, cp);
            length--;
        }

        val++;

        ret = njs_atom_atomize_key(vm, &prop_name);
        if (ret != NJS_OK) {
            return ret;
        }

        fhq.key_hash = prop_name.atom_id;

        ret = njs_flathsh_unique_insert(hash, &fhq);
        if (njs_slow_path(ret != NJS_OK)) {
            if (ret == NJS_ERROR) {
                njs_internal_error(vm, "flathsh insert failed");
                return NJS_ERROR;
            }

            /* ret == NJS_DECLINED: entry already exists */

            /*
             * Always using the first element among the duplicates
             * and ignoring the rest.
             */
            continue;
        }

        prop = fhq.value;

        prop->type = NJS_PROPERTY;
        prop->enumerable = 1;
        prop->configurable = 1;
        prop->writable = 1;

        ret = njs_string_create(vm, njs_prop_value(prop), val, njs_strlen(val));
        if (njs_slow_path(ret != NJS_OK)) {
            return NJS_ERROR;
        }
    }

    return NJS_OK;
}


static njs_int_t
njs_process_object_env(njs_vm_t *vm, njs_object_prop_t *pr, uint32_t unused,
    njs_value_t *process, njs_value_t *unused2, njs_value_t *retval)
{
    njs_int_t            ret;
    njs_object_t         *env;
    njs_object_prop_t    *prop;
    njs_flathsh_query_t  fhq;

    env = njs_object_alloc(vm);
    if (njs_slow_path(env == NULL)) {
        return NJS_ERROR;
    }

    env->shared_hash = vm->shared->env_hash;

    fhq.replace = 1;
    fhq.pool = vm->mem_pool;
    fhq.proto = &njs_object_hash_proto;
    fhq.key_hash = NJS_ATOM_STRING_env;

    ret = njs_flathsh_unique_insert(njs_object_hash(process), &fhq);
    if (njs_slow_path(ret != NJS_OK)) {
        njs_internal_error(vm, "flathsh insert failed");

        return NJS_ERROR;
    }

    prop = fhq.value;

    prop->type = NJS_PROPERTY;
    prop->enumerable = 1;
    prop->configurable = 1;
    prop->writable = 1;

    njs_set_object(njs_prop_value(prop), env);

    njs_value_assign(retval, njs_prop_value(prop));

    return NJS_OK;
}


static njs_int_t
njs_process_object_pid(njs_vm_t *vm, njs_object_prop_t *prop, uint32_t unused,
    njs_value_t *unused2, njs_value_t *unused3, njs_value_t *retval)
{
    njs_set_number(retval, getpid());

    return NJS_OK;
}


static njs_int_t
njs_process_object_ppid(njs_vm_t *vm, njs_object_prop_t *prop, uint32_t unused,
    njs_value_t *unused2, njs_value_t *unused3, njs_value_t *retval)
{
    njs_set_number(retval, getppid());

    return NJS_OK;
}


static njs_int_t
njs_ext_process_kill(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t magic, njs_value_t *retval)
{
    int                       signal;
    njs_str_t                 str;
    njs_uint_t                pid;
    njs_value_t               *arg;
    const njs_signal_entry_t  *s;

    arg = njs_arg(args, nargs, 1);
    if (!njs_value_is_number(arg)) {
        njs_vm_type_error(vm, "\"pid\" is not a number");
        return NJS_ERROR;
    }

    pid = njs_value_number(arg);
    signal = SIGTERM;

    arg = njs_arg(args, nargs, 2);
    if (njs_value_is_number(arg)) {
        signal = njs_value_number(arg);

    } else if (njs_value_is_string(arg)) {
        njs_string_get(vm, arg, &str);

        if (str.length < 3 || memcmp(str.start, "SIG", 3) != 0) {
            njs_vm_type_error(vm, "\"signal\" unknown value: \"%V\"", &str);
            return NJS_ERROR;
        }

        str.start += 3;
        str.length -= 3;

        for (s = &njs_signals_table[0]; s->name.length != 0; s++) {
            if (njs_strstr_eq(&str, &s->name)) {
                signal = s->value;
                break;
            }
        }

        if (s->name.length == 0) {
            njs_vm_type_error(vm, "\"signal\" unknown value");
            return NJS_ERROR;
        }

    } else if (!njs_value_is_undefined(arg)) {
        njs_vm_type_error(vm, "\"signal\" invalid type");
        return NJS_ERROR;
    }

    if (kill(pid, signal) < 0) {
        njs_vm_error(vm, "kill failed with (%d:%s)", errno, strerror(errno));
        return NJS_ERROR;
    }

    njs_set_boolean(retval, 1);
    return NJS_OK;
}


static const njs_object_prop_init_t  njs_process_object_properties[] =
{
    NJS_DECLARE_PROP_VALUE(SYMBOL_toStringTag, njs_ascii_strval("process"),
                           NJS_OBJECT_PROP_VALUE_C),

    NJS_DECLARE_PROP_HANDLER(STRING_argv, njs_process_object_argv, 0, 0),

    NJS_DECLARE_PROP_HANDLER(STRING_env, njs_process_object_env, 0, 0),

    NJS_DECLARE_PROP_HANDLER(STRING_pid, njs_process_object_pid, 0, 0),

    NJS_DECLARE_PROP_HANDLER(STRING_ppid, njs_process_object_ppid, 0, 0),

    NJS_DECLARE_PROP_NATIVE(STRING_kill, njs_ext_process_kill, 2, 0),
};


static const njs_object_init_t  njs_process_object_init = {
    njs_process_object_properties,
    njs_nitems(njs_process_object_properties),
};
