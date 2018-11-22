
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) Dmitry Volyntsev
 * Copyright (C) NGINX, Inc.
 */

#include <njs_core.h>
#include <njs_regexp.h>
#include <njs_date.h>
#include <njs_time.h>
#include <njs_math.h>
#include <njs_json.h>
#include <njs_module.h>
#include <njs_fs.h>
#include <njs_crypto.h>
#include <string.h>
#include <stdio.h>


typedef struct {
    njs_function_native_t  native;
    uint8_t                args_types[NJS_ARGS_TYPES_MAX];
} njs_function_init_t;


static njs_ret_t njs_prototype_function(njs_vm_t *vm, njs_value_t *args,
    nxt_uint_t nargs, njs_index_t unused);
static nxt_array_t *njs_vm_expression_completions(njs_vm_t *vm,
    nxt_str_t *expression);
static nxt_array_t *njs_object_completions(njs_vm_t *vm, njs_object_t *object);


const njs_object_init_t     njs_njs_object_init;
const njs_object_init_t     njs_global_this_init;


const njs_object_init_t    *njs_object_init[] = {
    &njs_global_this_init,        /* global this        */
    &njs_njs_object_init,         /* global njs object  */
    &njs_math_object_init,        /* Math               */
    &njs_json_object_init,        /* JSON               */
    NULL
};


const njs_object_init_t    *njs_module_init[] = {
    &njs_fs_object_init,         /* fs                 */
    &njs_crypto_object_init,     /* crypto             */
    NULL
};


const njs_object_init_t  *njs_prototype_init[] = {
    &njs_object_prototype_init,
    &njs_array_prototype_init,
    &njs_boolean_prototype_init,
    &njs_number_prototype_init,
    &njs_string_prototype_init,
    &njs_function_prototype_init,
    &njs_regexp_prototype_init,
    &njs_date_prototype_init,
    &njs_hash_prototype_init,
    &njs_hmac_prototype_init,
    &njs_error_prototype_init,
    &njs_eval_error_prototype_init,
    &njs_internal_error_prototype_init,
    &njs_range_error_prototype_init,
    &njs_reference_error_prototype_init,
    &njs_syntax_error_prototype_init,
    &njs_type_error_prototype_init,
    &njs_uri_error_prototype_init,
    NULL
};


const njs_object_init_t    *njs_constructor_init[] = {
    &njs_object_constructor_init,
    &njs_array_constructor_init,
    &njs_boolean_constructor_init,
    &njs_number_constructor_init,
    &njs_string_constructor_init,
    &njs_function_constructor_init,
    &njs_regexp_constructor_init,
    &njs_date_constructor_init,
    &njs_hash_constructor_init,
    &njs_hmac_constructor_init,
    &njs_error_constructor_init,
    &njs_eval_error_constructor_init,
    &njs_internal_error_constructor_init,
    &njs_range_error_constructor_init,
    &njs_reference_error_constructor_init,
    &njs_syntax_error_constructor_init,
    &njs_type_error_constructor_init,
    &njs_uri_error_constructor_init,
    &njs_memory_error_constructor_init,
    NULL
};


const njs_object_init_t    *njs_function_init[] = {
    &njs_eval_function_init,
    &njs_to_string_function_init,
    &njs_is_nan_function_init,
    &njs_is_finite_function_init,
    &njs_parse_int_function_init,
    &njs_parse_float_function_init,
    &njs_encode_uri_function_init,
    &njs_encode_uri_component_function_init,
    &njs_decode_uri_function_init,
    &njs_decode_uri_component_function_init,
    &njs_require_function_init,
    &njs_set_timeout_function_init,
    &njs_clear_timeout_function_init,
    NULL
};


const njs_function_init_t  njs_native_functions[] = {
    /* SunC does not allow empty array initialization. */
    { njs_eval_function,               { 0 } },
    { njs_object_prototype_to_string,  { 0 } },
    { njs_number_global_is_nan,        { NJS_SKIP_ARG, NJS_NUMBER_ARG } },
    { njs_number_is_finite,            { NJS_SKIP_ARG, NJS_NUMBER_ARG } },
    { njs_number_parse_int,
      { NJS_SKIP_ARG, NJS_STRING_ARG, NJS_INTEGER_ARG } },
    { njs_number_parse_float,          { NJS_SKIP_ARG, NJS_STRING_ARG } },
    { njs_string_encode_uri,           { NJS_SKIP_ARG, NJS_STRING_ARG } },
    { njs_string_encode_uri_component, { NJS_SKIP_ARG, NJS_STRING_ARG } },
    { njs_string_decode_uri,           { NJS_SKIP_ARG, NJS_STRING_ARG } },
    { njs_string_decode_uri_component, { NJS_SKIP_ARG, NJS_STRING_ARG } },
    { njs_module_require,              { NJS_SKIP_ARG, NJS_STRING_ARG } },
    { njs_set_timeout,
      { NJS_SKIP_ARG, NJS_FUNCTION_ARG, NJS_NUMBER_ARG } },
    { njs_clear_timeout,               { NJS_SKIP_ARG, NJS_NUMBER_ARG } },
};


const njs_object_prop_t  njs_arguments_object_properties[] =
{
    {
        .type = NJS_PROPERTY_HANDLER,
        .name = njs_string("caller"),
        .value = njs_prop_handler(njs_function_arguments_thrower),
    },

    {
        .type = NJS_PROPERTY_HANDLER,
        .name = njs_string("callee"),
        .value = njs_prop_handler(njs_function_arguments_thrower),
    },
};


const njs_function_init_t  njs_native_constructors[] = {
    /* SunC does not allow empty array initialization. */
    { njs_object_constructor,     { 0 } },
    { njs_array_constructor,      { 0 } },
    { njs_boolean_constructor,    { 0 } },
    { njs_number_constructor,     { NJS_SKIP_ARG, NJS_NUMBER_ARG } },
    { njs_string_constructor,     { NJS_SKIP_ARG, NJS_STRING_ARG } },
    { njs_function_constructor,   { 0 } },
    { njs_regexp_constructor,
      { NJS_SKIP_ARG, NJS_STRING_ARG, NJS_STRING_ARG } },
    { njs_date_constructor,       { 0 } },
    { njs_hash_constructor,       { NJS_SKIP_ARG, NJS_STRING_ARG } },
    { njs_hmac_constructor,       { NJS_SKIP_ARG, NJS_STRING_ARG,
                                    NJS_STRING_ARG } },
    { njs_error_constructor,      { NJS_SKIP_ARG, NJS_STRING_ARG } },
    { njs_eval_error_constructor, { NJS_SKIP_ARG, NJS_STRING_ARG } },
    { njs_internal_error_constructor,
      { NJS_SKIP_ARG, NJS_STRING_ARG } },
    { njs_range_error_constructor,
      { NJS_SKIP_ARG, NJS_STRING_ARG } },
    { njs_reference_error_constructor,  { NJS_SKIP_ARG, NJS_STRING_ARG } },
    { njs_syntax_error_constructor,
      { NJS_SKIP_ARG, NJS_STRING_ARG } },
    { njs_type_error_constructor, { NJS_SKIP_ARG, NJS_STRING_ARG } },
    { njs_uri_error_constructor,  { NJS_SKIP_ARG, NJS_STRING_ARG } },
    { njs_memory_error_constructor,  { NJS_SKIP_ARG, NJS_STRING_ARG } },
};

const njs_object_prototype_t  njs_prototype_values[] = {
    /*
     * GCC 4 complains about uninitialized .shared field,
     * if the .type field is initialized as .object.type.
     */
    { .object =       { .type = NJS_OBJECT } },
    { .object =       { .type = NJS_ARRAY } },

    /*
     * The .object.type field must be initialzed after the .value field,
     * otherwise SunC 5.9 treats the .value as .object.value or so.
     */
    { .object_value = { .value = njs_value(NJS_BOOLEAN, 0, 0.0),
                        .object = { .type = NJS_OBJECT_BOOLEAN } } },

    { .object_value = { .value = njs_value(NJS_NUMBER, 0, 0.0),
                        .object = { .type = NJS_OBJECT_NUMBER } } },

    { .object_value = { .value = njs_string(""),
                        .object = { .type = NJS_OBJECT_STRING } } },

    { .function =     { .native = 1,
                        .args_offset = 1,
                        .u.native = njs_prototype_function,
                        .object = { .type = NJS_FUNCTION } } },

    { .object =       { .type = NJS_REGEXP } },

    { .date =         { .time = NAN,
                        .object = { .type = NJS_DATE } } },

    { .object_value = { .value = njs_value(NJS_DATA, 0, 0.0),
                        .object = { .type = NJS_OBJECT } } },

    { .object_value = { .value = njs_value(NJS_DATA, 0, 0.0),
                        .object = { .type = NJS_OBJECT } } },

    { .object =       { .type = NJS_OBJECT_ERROR } },
    { .object =       { .type = NJS_OBJECT_EVAL_ERROR } },
    { .object =       { .type = NJS_OBJECT_INTERNAL_ERROR } },
    { .object =       { .type = NJS_OBJECT_RANGE_ERROR } },
    { .object =       { .type = NJS_OBJECT_REF_ERROR } },
    { .object =       { .type = NJS_OBJECT_SYNTAX_ERROR } },
    { .object =       { .type = NJS_OBJECT_TYPE_ERROR } },
    { .object =       { .type = NJS_OBJECT_URI_ERROR } },
};



nxt_int_t
njs_builtin_objects_create(njs_vm_t *vm)
{
    nxt_int_t                  ret;
    njs_module_t               *module;
    njs_object_t               *object;
    njs_function_t             *func;
    nxt_lvlhsh_query_t         lhq;
    njs_object_prototype_t     *prototype;
    const njs_object_init_t    *obj, **p;
    const njs_function_init_t  *f;

    static const njs_object_prop_t    function_prototype_property = {
        .type = NJS_PROPERTY_HANDLER,
        .name = njs_string("prototype"),
        .value = njs_prop_handler(njs_function_prototype_create),
    };

    static const nxt_str_t  sandbox_key = nxt_string("sandbox");

    ret = njs_object_hash_create(vm, &vm->shared->function_prototype_hash,
                                 &function_prototype_property, 1);
    if (nxt_slow_path(ret != NXT_OK)) {
        return NXT_ERROR;
    }

    ret = njs_object_hash_create(vm, &vm->shared->arguments_object_hash,
                                 njs_arguments_object_properties,
                                 nxt_nitems(njs_arguments_object_properties));
    if (nxt_slow_path(ret != NXT_OK)) {
        return NXT_ERROR;
    }

    object = vm->shared->objects;

    for (p = njs_object_init; *p != NULL; p++) {
        obj = *p;

        ret = njs_object_hash_create(vm, &object->shared_hash,
                                     obj->properties, obj->items);

        if (nxt_slow_path(ret != NXT_OK)) {
            return NXT_ERROR;
        }

        object->shared = 1;
        object->extensible = 1;

        object++;
    }

    lhq.replace = 0;
    lhq.pool = vm->mem_cache_pool;

    for (p = njs_module_init; *p != NULL; p++) {
        obj = *p;

        module = nxt_mem_cache_zalloc(vm->mem_cache_pool, sizeof(njs_module_t));
        if (nxt_slow_path(module == NULL)) {
            return NJS_ERROR;
        }

        ret = njs_object_hash_create(vm, &module->object.shared_hash,
                                     obj->properties, obj->items);
        if (nxt_slow_path(ret != NXT_OK)) {
            return NXT_ERROR;
        }

        if (vm->options.sandbox) {
            lhq.key = sandbox_key;
            lhq.key_hash = nxt_djb_hash(sandbox_key.start, sandbox_key.length);
            lhq.proto = &njs_object_hash_proto;

            ret = nxt_lvlhsh_find(&module->object.shared_hash, &lhq);
            if (nxt_fast_path(ret != NXT_OK)) {
                continue;
            }
        }

        module->name = obj->name;
        module->object.shared = 1;

        lhq.key = module->name;
        lhq.key_hash = nxt_djb_hash(lhq.key.start, lhq.key.length);
        lhq.proto = &njs_modules_hash_proto;
        lhq.value = module;

        ret = nxt_lvlhsh_insert(&vm->modules_hash, &lhq);
        if (nxt_fast_path(ret != NXT_OK)) {
            return NXT_ERROR;
        }
    }

    f = njs_native_functions;
    func = vm->shared->functions;

    for (p = njs_function_init; *p != NULL; p++) {
        obj = *p;

        ret = njs_object_hash_create(vm, &func->object.shared_hash,
                                     obj->properties, obj->items);
        if (nxt_slow_path(ret != NXT_OK)) {
            return NXT_ERROR;
        }

        func->object.shared = 1;
        func->object.extensible = 1;
        func->native = 1;
        func->args_offset = 1;

        func->u.native = f->native;
        memcpy(func->args_types, f->args_types, NJS_ARGS_TYPES_MAX);

        f++;
        func++;
    }

    prototype = vm->shared->prototypes;
    memcpy(prototype, njs_prototype_values, sizeof(njs_prototype_values));

    for (p = njs_prototype_init; *p != NULL; p++) {
        obj = *p;

        ret = njs_object_hash_create(vm, &prototype->object.shared_hash,
                                     obj->properties, obj->items);
        if (nxt_slow_path(ret != NXT_OK)) {
            return NXT_ERROR;
        }

        prototype->object.extensible = 1;

        prototype++;
    }

    vm->shared->prototypes[NJS_PROTOTYPE_REGEXP].regexp.pattern =
                                              vm->shared->empty_regexp_pattern;

    f = njs_native_constructors;
    func = vm->shared->constructors;

    for (p = njs_constructor_init; *p != NULL; p++) {
        obj = *p;

        func->object.shared = 0;
        func->object.extensible = 1;
        func->native = 1;
        func->ctor = 1;
        func->args_offset = 1;

        func->u.native = f->native;

        memcpy(func->args_types, f->args_types, NJS_ARGS_TYPES_MAX);

        ret = njs_object_hash_create(vm, &func->object.shared_hash,
                                     obj->properties, obj->items);
        if (nxt_slow_path(ret != NXT_OK)) {
            return NXT_ERROR;
        }

        f++;
        func++;
    }

    return NXT_OK;
}


static njs_ret_t
njs_prototype_function(njs_vm_t *vm, njs_value_t *args, nxt_uint_t nargs,
    njs_index_t unused)
{
    vm->retval = njs_value_void;

    return NXT_OK;
}


/*
 * Object(),
 * Object.__proto__             -> Function_Prototype,
 * Object_Prototype.__proto__   -> null,
 *   the null value is handled by njs_object_prototype_get_proto(),
 *
 * Array(),
 * Array.__proto__              -> Function_Prototype,
 * Array_Prototype.__proto__    -> Object_Prototype,
 *
 * Boolean(),
 * Boolean.__proto__            -> Function_Prototype,
 * Boolean_Prototype.__proto__  -> Object_Prototype,
 *
 * Number(),
 * Number.__proto__             -> Function_Prototype,
 * Number_Prototype.__proto__   -> Object_Prototype,
 *
 * String(),
 * String.__proto__             -> Function_Prototype,
 * String_Prototype.__proto__   -> Object_Prototype,
 *
 * Function(),
 * Function.__proto__           -> Function_Prototype,
 * Function_Prototype.__proto__ -> Object_Prototype,
 *
 * RegExp(),
 * RegExp.__proto__             -> Function_Prototype,
 * RegExp_Prototype.__proto__   -> Object_Prototype,
 *
 * Date(),
 * Date.__proto__               -> Function_Prototype,
 * Date_Prototype.__proto__     -> Object_Prototype,
 *
 * Error(),
 * Error.__proto__               -> Function_Prototype,
 * Error_Prototype.__proto__     -> Object_Prototype,
 *
 * EvalError(),
 * EvalError.__proto__           -> Function_Prototype,
 * EvalError_Prototype.__proto__ -> Error_Prototype,
 *
 * InternalError(),
 * InternalError.__proto__           -> Function_Prototype,
 * InternalError_Prototype.__proto__ -> Error_Prototype,
 *
 * RangeError(),
 * RangeError.__proto__           -> Function_Prototype,
 * RangeError_Prototype.__proto__ -> Error_Prototype,
 *
 * ReferenceError(),
 * ReferenceError.__proto__           -> Function_Prototype,
 * ReferenceError_Prototype.__proto__ -> Error_Prototype,
 *
 * SyntaxError(),
 * SyntaxError.__proto__           -> Function_Prototype,
 * SyntaxError_Prototype.__proto__ -> Error_Prototype,
 *
 * TypeError(),
 * TypeError.__proto__           -> Function_Prototype,
 * TypeError_Prototype.__proto__ -> Error_Prototype,
 *
 * URIError(),
 * URIError.__proto__           -> Function_Prototype,
 * URIError_Prototype.__proto__ -> Error_Prototype,
 *
 * MemoryError(),
 * MemoryError.__proto__           -> Function_Prototype,
 * MemoryError_Prototype.__proto__ -> Error_Prototype,
 *
 * eval(),
 * eval.__proto__               -> Function_Prototype.
 */

nxt_int_t
njs_builtin_objects_clone(njs_vm_t *vm)
{
    size_t        size;
    nxt_uint_t    i;
    njs_value_t   *values;
    njs_object_t  *object_prototype, *function_prototype, *error_prototype;

    /*
     * Copy both prototypes and constructors arrays by one memcpy()
     * because they are stored together.
     */
    size = NJS_PROTOTYPE_MAX * sizeof(njs_object_prototype_t)
           + NJS_CONSTRUCTOR_MAX * sizeof(njs_function_t);

    memcpy(vm->prototypes, vm->shared->prototypes, size);

    object_prototype = &vm->prototypes[NJS_PROTOTYPE_OBJECT].object;

    for (i = NJS_PROTOTYPE_ARRAY; i < NJS_PROTOTYPE_EVAL_ERROR; i++) {
        vm->prototypes[i].object.__proto__ = object_prototype;
    }

    error_prototype = &vm->prototypes[NJS_PROTOTYPE_ERROR].object;

    for (i = NJS_PROTOTYPE_EVAL_ERROR; i < NJS_PROTOTYPE_MAX; i++) {
        vm->prototypes[i].object.__proto__ = error_prototype;
    }

    function_prototype = &vm->prototypes[NJS_CONSTRUCTOR_FUNCTION].object;
    values = vm->scopes[NJS_SCOPE_GLOBAL];

    for (i = NJS_CONSTRUCTOR_OBJECT; i < NJS_CONSTRUCTOR_MAX; i++) {
        values[i].type = NJS_FUNCTION;
        values[i].data.truth = 1;
        values[i].data.u.function = &vm->constructors[i];
        vm->constructors[i].object.__proto__ = function_prototype;
    }

    return NXT_OK;
}


static size_t
njs_builtin_completions_size(njs_vm_t *vm)
{
    nxt_uint_t               n;
    njs_keyword_t            *keyword;
    nxt_lvlhsh_each_t        lhe, lhe_prop;
    njs_extern_value_t       *ev;
    const njs_extern_t       *ext_proto, *ext_prop;
    const njs_object_init_t  **p;

    n = 0;

    nxt_lvlhsh_each_init(&lhe, &njs_keyword_hash_proto);

    for ( ;; ) {
        keyword = nxt_lvlhsh_each(&vm->shared->keywords_hash, &lhe);

        if (keyword == NULL) {
            break;
        }

        n++;
    }

    for (p = njs_object_init; *p != NULL; p++) {
        n += (*p)->items;
    }

    for (p = njs_prototype_init; *p != NULL; p++) {
        n += (*p)->items;
    }

    for (p = njs_constructor_init; *p != NULL; p++) {
        n += (*p)->items;
    }

    nxt_lvlhsh_each_init(&lhe, &njs_extern_value_hash_proto);

    for ( ;; ) {
        ev = nxt_lvlhsh_each(&vm->externals_hash, &lhe);

        if (ev == NULL) {
            break;
        }

        ext_proto = ev->value.external.proto;

        nxt_lvlhsh_each_init(&lhe_prop, &njs_extern_hash_proto);

        n++;

        for ( ;; ) {
            ext_prop = nxt_lvlhsh_each(&ext_proto->hash, &lhe_prop);

            if (ext_prop == NULL) {
                break;
            }

            n++;
        }
    }

    return n;
}


static nxt_array_t *
njs_builtin_completions(njs_vm_t *vm, nxt_array_t *array)
{
    char                     *compl;
    size_t                   n, len;
    nxt_str_t                string, *completions;
    nxt_uint_t               i, k;
    njs_keyword_t            *keyword;
    nxt_lvlhsh_each_t        lhe, lhe_prop;
    njs_extern_value_t       *ev;
    const njs_extern_t       *ext_proto, *ext_prop;
    const njs_object_prop_t  *prop;
    const njs_object_init_t  *obj, **p;

    n = 0;
    completions = array->start;

    nxt_lvlhsh_each_init(&lhe, &njs_keyword_hash_proto);

    for ( ;; ) {
        keyword = nxt_lvlhsh_each(&vm->shared->keywords_hash, &lhe);

        if (keyword == NULL) {
            break;
        }

        completions[n++] = keyword->name;
    }

    for (p = njs_object_init; *p != NULL; p++) {
        obj = *p;

        for (i = 0; i < obj->items; i++) {
            prop = &obj->properties[i];
            njs_string_get(&prop->name, &string);
            len = obj->name.length + string.length + 2;

            compl = nxt_mem_cache_zalloc(vm->mem_cache_pool, len);
            if (compl == NULL) {
                return NULL;
            }

            snprintf(compl, len, "%s.%s", obj->name.start, string.start);

            completions[n].length = len;
            completions[n++].start = (u_char *) compl;
        }
    }

    for (p = njs_prototype_init; *p != NULL; p++) {
        obj = *p;

        for (i = 0; i < obj->items; i++) {
            prop = &obj->properties[i];
            njs_string_get(&prop->name, &string);
            len = string.length + 2;

            compl = nxt_mem_cache_zalloc(vm->mem_cache_pool, len);
            if (compl == NULL) {
                return NULL;
            }

            snprintf(compl, len, ".%s", string.start);

            for (k = 0; k < n; k++) {
                if (strncmp((char *) completions[k].start, compl, len)
                    == 0)
                {
                    break;
                }
            }

            if (k == n) {
                completions[n].length = len;
                completions[n++].start = (u_char *) compl;
            }
        }
    }

    for (p = njs_constructor_init; *p != NULL; p++) {
        obj = *p;

        for (i = 0; i < obj->items; i++) {
            prop = &obj->properties[i];
            njs_string_get(&prop->name, &string);
            len = obj->name.length + string.length + 2;

            compl = nxt_mem_cache_zalloc(vm->mem_cache_pool, len);
            if (compl == NULL) {
                return NULL;
            }

            snprintf(compl, len, "%s.%s", obj->name.start, string.start);

            completions[n].length = len;
            completions[n++].start = (u_char *) compl;
        }
    }

    nxt_lvlhsh_each_init(&lhe, &njs_extern_value_hash_proto);

    for ( ;; ) {
        ev = nxt_lvlhsh_each(&vm->externals_hash, &lhe);

        if (ev == NULL) {
            break;
        }

        ext_proto = ev->value.external.proto;

        nxt_lvlhsh_each_init(&lhe_prop, &njs_extern_hash_proto);

        len = ev->name.length + 1;
        compl = nxt_mem_cache_zalloc(vm->mem_cache_pool, len);
        if (compl == NULL) {
            return NULL;
        }

        snprintf(compl, len, "%.*s", (int) ev->name.length, ev->name.start);

        completions[n].length = len;
        completions[n++].start = (u_char *) compl;

        for ( ;; ) {
            ext_prop = nxt_lvlhsh_each(&ext_proto->hash, &lhe_prop);

            if (ext_prop == NULL) {
                break;
            }

            len = ev->name.length + ev->name.length + 2;
            compl = nxt_mem_cache_zalloc(vm->mem_cache_pool, len);
            if (compl == NULL) {
                return NULL;
            }

            snprintf(compl, len, "%.*s.%.*s", (int) ev->name.length,
                     ev->name.start, (int) ext_prop->name.length,
                     ext_prop->name.start);

            completions[n].length = len;
            completions[n++].start = (u_char *) compl;
        }
    }

    array->items = n;

    return array;
}


nxt_array_t *
njs_vm_completions(njs_vm_t *vm, nxt_str_t *expression)
{
    size_t       size;
    nxt_array_t  *completions;

    if (expression == NULL) {
        size = njs_builtin_completions_size(vm);

        completions = nxt_array_create(size, sizeof(nxt_str_t),
                                       &njs_array_mem_proto,
                                       vm->mem_cache_pool);

        if (nxt_slow_path(completions == NULL)) {
            return NULL;
        }

        return njs_builtin_completions(vm, completions);
    }

    return njs_vm_expression_completions(vm, expression);
}


static nxt_array_t *
njs_vm_expression_completions(njs_vm_t *vm, nxt_str_t *expression)
{
    u_char              *p, *end;
    nxt_int_t           ret;
    njs_value_t         *value;
    njs_variable_t      *var;
    njs_object_prop_t   *prop;
    nxt_lvlhsh_query_t  lhq;

    if (nxt_slow_path(vm->parser == NULL)) {
        return NULL;
    }

    p = expression->start;
    end = p + expression->length;

    lhq.key.start = p;

    while (p < end && *p != '.') { p++; }

    lhq.proto = &njs_variables_hash_proto;
    lhq.key.length = p - lhq.key.start;
    lhq.key_hash = nxt_djb_hash(lhq.key.start, lhq.key.length);

    ret = nxt_lvlhsh_find(&vm->parser->scope->variables, &lhq);
    if (nxt_slow_path(ret != NXT_OK)) {
        return NULL;
    }

    var = lhq.value;
    value = njs_vmcode_operand(vm, var->index);

    if (!njs_is_object(value)) {
        return NULL;
    }

    lhq.proto = &njs_object_hash_proto;

    for ( ;; ) {

        if (p == end) {
            break;
        }

        lhq.key.start = ++p;

        while (p < end && *p != '.') { p++; }

        lhq.key.length = p - lhq.key.start;
        lhq.key_hash = nxt_djb_hash(lhq.key.start, lhq.key.length);

        ret = nxt_lvlhsh_find(&value->data.u.object->hash, &lhq);
        if (nxt_slow_path(ret != NXT_OK)) {
            return NULL;
        }

        prop = lhq.value;

        if (!njs_is_object(&prop->value)) {
            return NULL;
        }

        value = &prop->value;
    }

    return njs_object_completions(vm, value->data.u.object);
}


static nxt_array_t *
njs_object_completions(njs_vm_t *vm, njs_object_t *object)
{
    size_t             size;
    nxt_uint_t         n, k;
    nxt_str_t          *compl;
    nxt_array_t        *completions;
    njs_object_t       *o;
    njs_object_prop_t  *prop;
    nxt_lvlhsh_each_t  lhe;

    size = 0;
    o = object;

    do {
        nxt_lvlhsh_each_init(&lhe, &njs_object_hash_proto);

        for ( ;; ) {
            prop = nxt_lvlhsh_each(&o->hash, &lhe);
            if (prop == NULL) {
                break;
            }

            size++;
        }

        nxt_lvlhsh_each_init(&lhe, &njs_object_hash_proto);

        for ( ;; ) {
            prop = nxt_lvlhsh_each(&o->shared_hash, &lhe);
            if (prop == NULL) {
                break;
            }

            size++;
        }

        o = o->__proto__;

    } while (o != NULL);

    completions = nxt_array_create(size, sizeof(nxt_str_t),
                                   &njs_array_mem_proto, vm->mem_cache_pool);

    if (nxt_slow_path(completions == NULL)) {
        return NULL;
    }

    n = 0;
    o = object;
    compl = completions->start;

    do {
        nxt_lvlhsh_each_init(&lhe, &njs_object_hash_proto);

        for ( ;; ) {
            prop = nxt_lvlhsh_each(&o->hash, &lhe);
            if (prop == NULL) {
                break;
            }

            njs_string_get(&prop->name, &compl[n]);

            for (k = 0; k < n; k++) {
                if (nxt_strstr_eq(&compl[k], &compl[n])) {
                    break;
                }
            }

            if (k == n) {
                n++;
            }
        }

        nxt_lvlhsh_each_init(&lhe, &njs_object_hash_proto);

        for ( ;; ) {
            prop = nxt_lvlhsh_each(&o->shared_hash, &lhe);
            if (prop == NULL) {
                break;
            }

            njs_string_get(&prop->name, &compl[n]);

            for (k = 0; k < n; k++) {
                if (nxt_strstr_eq(&compl[k], &compl[n])) {
                    break;
                }
            }

            if (k == n) {
                n++;
            }
        }

        o = o->__proto__;

    } while (o != NULL);

    completions->items = n;

    return completions;
}


static nxt_int_t
njs_builtin_match(const njs_object_init_t **objects, njs_function_t *function,
    const njs_object_prop_t **prop, const njs_object_init_t **object)
{
    nxt_uint_t               i;
    const njs_object_init_t  *o, **p;
    const njs_object_prop_t  *pr;

    for (p = objects; *p != NULL; p++) {
        o = *p;

        for (i = 0; i < o->items; i++) {
            pr = &o->properties[i];

            if (pr->type != NJS_METHOD) {
                continue;
            }

            if (function != pr->value.data.u.function) {
                continue;
            }

            *prop = pr;
            *object = o;

            return NXT_OK;
        }
    }

    return NXT_DECLINED;
}


nxt_int_t
njs_builtin_match_native_function(njs_vm_t *vm, njs_function_t *function,
    nxt_str_t *name)
{
    char                       *buf;
    size_t                     len;
    nxt_str_t                  string;
    nxt_int_t                  rc;
    const njs_object_init_t    *obj, **p;
    const njs_object_prop_t    *prop;
    const njs_function_init_t  *fun;

    rc = njs_builtin_match(njs_object_init, function, &prop, &obj);

    if (rc == NXT_OK) {
        njs_string_get(&prop->name, &string);
        len = obj->name.length + string.length + sizeof(".");

        buf = nxt_mem_cache_zalloc(vm->mem_cache_pool, len);
        if (buf == NULL) {
            return NXT_ERROR;
        }

        snprintf(buf, len, "%s.%s", obj->name.start, string.start);

        name->length = len;
        name->start = (u_char *) buf;

        return NXT_OK;
    }

    rc = njs_builtin_match(njs_prototype_init, function, &prop, &obj);

    if (rc == NXT_OK) {
        njs_string_get(&prop->name, &string);
        len = obj->name.length + string.length + sizeof(".prototype.");

        buf = nxt_mem_cache_zalloc(vm->mem_cache_pool, len);
        if (buf == NULL) {
            return NXT_ERROR;
        }

        snprintf(buf, len, "%s.prototype.%s", obj->name.start, string.start);

        name->length = len;
        name->start = (u_char *) buf;

        return NXT_OK;
    }

    rc = njs_builtin_match(njs_constructor_init, function, &prop, &obj);

    if (rc == NXT_OK) {
        njs_string_get(&prop->name, &string);
        len = obj->name.length + string.length + sizeof(".");

        buf = nxt_mem_cache_zalloc(vm->mem_cache_pool, len);
        if (buf == NULL) {
            return NXT_ERROR;
        }

        snprintf(buf, len, "%s.%s", obj->name.start, string.start);

        name->length = len;
        name->start = (u_char *) buf;

        return NXT_OK;
    }

    fun = njs_native_functions;

    for (p = njs_function_init; *p != NULL; p++, fun++) {
        if (function->u.native == fun->native) {
            *name = (*p)->name;

            return NXT_OK;
        }
    }

    rc = njs_builtin_match(njs_module_init, function, &prop, &obj);

    if (rc == NXT_OK) {
        njs_string_get(&prop->name, &string);
        len = obj->name.length + string.length + sizeof(".");

        buf = nxt_mem_cache_zalloc(vm->mem_cache_pool, len);
        if (buf == NULL) {
            return NXT_ERROR;
        }

        snprintf(buf, len, "%s.%s", obj->name.start, string.start);

        name->length = len;
        name->start = (u_char *) buf;

        return NXT_OK;
    }

    return NXT_DECLINED;
}


static njs_ret_t
njs_dump_value(njs_vm_t *vm, njs_value_t *args, nxt_uint_t nargs,
    njs_index_t unused)
{
    nxt_str_t          str;
    nxt_uint_t         n;
    const njs_value_t  *value, *indent;

    value = njs_arg(args, nargs, 1);
    indent = njs_arg(args, nargs, 2);

    n = indent->data.u.number;
    n = nxt_min(n, 5);

    if (njs_vm_value_dump(vm, &str, value, n) != NXT_OK) {
        return NXT_ERROR;
    }

    return njs_string_new(vm, &vm->retval, str.start, str.length, 0);
}


static const njs_object_prop_t  njs_njs_object_properties[] =
{
    {
        .type = NJS_PROPERTY,
        .name = njs_string("version"),
        .value = njs_string(NJS_VERSION),
    },

    {
        .type = NJS_METHOD,
        .name = njs_string("dump"),
        .value = njs_native_function(njs_dump_value, 0,
                                    NJS_SKIP_ARG, NJS_SKIP_ARG, NJS_NUMBER_ARG),
    },
};


const njs_object_init_t  njs_njs_object_init = {
    nxt_string("njs"),
    njs_njs_object_properties,
    nxt_nitems(njs_njs_object_properties),
};


static const njs_object_prop_t  njs_global_this_object_properties[] =
{
    {
        .type = NJS_PROPERTY,
        .name = njs_string("NaN"),
        .value = njs_value(NJS_NUMBER, 0, NAN),
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("Infinity"),
        .value = njs_value(NJS_NUMBER, 0, INFINITY),
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("undefined"),
        .value = njs_value(NJS_VOID, 0, NAN),
    },
};


const njs_object_init_t  njs_global_this_init = {
    nxt_string("this"),
    njs_global_this_object_properties,
    nxt_nitems(njs_global_this_object_properties)
};
