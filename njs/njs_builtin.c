
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) NGINX, Inc.
 */

#include <nxt_auto_config.h>
#include <nxt_types.h>
#include <nxt_clang.h>
#include <nxt_string.h>
#include <nxt_djb_hash.h>
#include <nxt_stub.h>
#include <nxt_array.h>
#include <nxt_lvlhsh.h>
#include <nxt_random.h>
#include <nxt_mem_cache_pool.h>
#include <njscript.h>
#include <njs_vm.h>
#include <njs_boolean.h>
#include <njs_number.h>
#include <njs_string.h>
#include <njs_object.h>
#include <njs_array.h>
#include <njs_json.h>
#include <njs_function.h>
#include <njs_variable.h>
#include <njs_extern.h>
#include <njs_parser.h>
#include <njs_regexp.h>
#include <njs_date.h>
#include <njs_error.h>
#include <njs_math.h>
#include <njs_module.h>
#include <njs_fs.h>
#include <string.h>
#include <stdio.h>


typedef struct {
    njs_function_native_t  native;
    uint8_t                args_types[NJS_ARGS_TYPES_MAX];
} njs_function_init_t;


static nxt_int_t njs_builtin_completions(njs_vm_t *vm, size_t *size,
    nxt_str_t *completions);
static nxt_array_t *njs_vm_expression_completions(njs_vm_t *vm,
    nxt_str_t *expression);
static nxt_array_t *njs_object_completions(njs_vm_t *vm, njs_object_t *object);


const njs_object_init_t    *njs_object_init[] = {
    NULL,                         /* global this        */
    &njs_math_object_init,        /* Math               */
    &njs_json_object_init,        /* JSON               */
};


const njs_object_init_t    *njs_module_init[] = {
    &njs_fs_object_init          /* fs                 */
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
    &njs_error_prototype_init,
    &njs_eval_error_prototype_init,
    &njs_internal_error_prototype_init,
    &njs_range_error_prototype_init,
    &njs_ref_error_prototype_init,
    &njs_syntax_error_prototype_init,
    &njs_type_error_prototype_init,
    &njs_uri_error_prototype_init,
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
    &njs_error_constructor_init,
    &njs_eval_error_constructor_init,
    &njs_internal_error_constructor_init,
    &njs_range_error_constructor_init,
    &njs_ref_error_constructor_init,
    &njs_syntax_error_constructor_init,
    &njs_type_error_constructor_init,
    &njs_uri_error_constructor_init,
    &njs_memory_error_constructor_init,
};


static njs_ret_t
njs_prototype_function(njs_vm_t *vm, njs_value_t *args, nxt_uint_t nargs,
    njs_index_t unused)
{
    vm->retval = njs_value_void;

    return NXT_OK;
}


nxt_int_t
njs_builtin_objects_create(njs_vm_t *vm)
{
    nxt_int_t               ret;
    nxt_uint_t              i;
    njs_module_t            *module;
    njs_object_t            *objects;
    njs_function_t          *functions, *constructors;
    nxt_lvlhsh_query_t      lhq;
    njs_object_prototype_t  *prototypes;

    static const njs_object_prototype_t  prototype_values[] = {
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

        { .object =       { .type = NJS_OBJECT_ERROR } },
        { .object =       { .type = NJS_OBJECT_EVAL_ERROR } },
        { .object =       { .type = NJS_OBJECT_INTERNAL_ERROR } },
        { .object =       { .type = NJS_OBJECT_RANGE_ERROR } },
        { .object =       { .type = NJS_OBJECT_REF_ERROR } },
        { .object =       { .type = NJS_OBJECT_SYNTAX_ERROR } },
        { .object =       { .type = NJS_OBJECT_TYPE_ERROR } },
        { .object =       { .type = NJS_OBJECT_URI_ERROR } },
        { .object =       { .type = NJS_OBJECT_INTERNAL_ERROR } },
    };

    static const njs_function_init_t  native_constructors[] = {
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
        { njs_error_constructor,      { NJS_SKIP_ARG, NJS_STRING_ARG } },
        { njs_eval_error_constructor, { NJS_SKIP_ARG, NJS_STRING_ARG } },
        { njs_internal_error_constructor,
          { NJS_SKIP_ARG, NJS_STRING_ARG } },
        { njs_range_error_constructor,
          { NJS_SKIP_ARG, NJS_STRING_ARG } },
        { njs_ref_error_constructor,  { NJS_SKIP_ARG, NJS_STRING_ARG } },
        { njs_syntax_error_constructor,
          { NJS_SKIP_ARG, NJS_STRING_ARG } },
        { njs_type_error_constructor, { NJS_SKIP_ARG, NJS_STRING_ARG } },
        { njs_uri_error_constructor,  { NJS_SKIP_ARG, NJS_STRING_ARG } },
        { njs_memory_error_constructor,  { NJS_SKIP_ARG, NJS_STRING_ARG } },
    };

    static const njs_object_init_t    *function_init[] = {
        &njs_eval_function_init,      /* eval               */
        NULL,                         /* toString           */
        NULL,                         /* isNaN              */
        NULL,                         /* isFinite           */
        NULL,                         /* parseInt           */
        NULL,                         /* parseFloat         */
        NULL,                         /* encodeURI          */
        NULL,                         /* encodeURIComponent */
        NULL,                         /* decodeURI          */
        NULL,                         /* decodeURIComponent */
        NULL,                         /* require */
    };

    static const njs_function_init_t  native_functions[] = {
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
    };

    static const njs_object_prop_t    null_proto_property = {
        .type = NJS_WHITEOUT,
        .name = njs_string("__proto__"),
        .value = njs_value(NJS_NULL, 0, 0.0),
    };

    static const njs_object_prop_t    function_prototype_property = {
        .type = NJS_NATIVE_GETTER,
        .name = njs_string("prototype"),
        .value = njs_native_getter(njs_function_prototype_create),
    };

    ret = njs_object_hash_create(vm, &vm->shared->null_proto_hash,
                                 &null_proto_property, 1);
    if (nxt_slow_path(ret != NXT_OK)) {
        return NXT_ERROR;
    }

    ret = njs_object_hash_create(vm, &vm->shared->function_prototype_hash,
                                 &function_prototype_property, 1);
    if (nxt_slow_path(ret != NXT_OK)) {
        return NXT_ERROR;
    }

    objects = vm->shared->objects;

    for (i = NJS_OBJECT_THIS; i < NJS_OBJECT_MAX; i++) {
        if (njs_object_init[i] != NULL) {
            ret = njs_object_hash_create(vm, &objects[i].shared_hash,
                                         njs_object_init[i]->properties,
                                         njs_object_init[i]->items);
            if (nxt_slow_path(ret != NXT_OK)) {
                return NXT_ERROR;
            }
        }

        objects[i].shared = 1;
    }

    lhq.replace = 0;
    lhq.proto = &njs_modules_hash_proto;
    lhq.pool = vm->mem_cache_pool;

    for (i = NJS_MODULE_FS; i < NJS_MODULE_MAX; i++) {
        module = nxt_mem_cache_zalloc(vm->mem_cache_pool, sizeof(njs_module_t));
        if (nxt_slow_path(module == NULL)) {
            return NJS_ERROR;
        }

        module->name = njs_module_init[i]->name;

        ret = njs_object_hash_create(vm, &module->object.shared_hash,
                                     njs_module_init[i]->properties,
                                     njs_module_init[i]->items);
        if (nxt_slow_path(ret != NXT_OK)) {
            return NXT_ERROR;
        }

        module->object.shared = 1;

        lhq.key = module->name;
        lhq.key_hash = nxt_djb_hash(lhq.key.start, lhq.key.length);
        lhq.value = module;

        ret = nxt_lvlhsh_insert(&vm->modules_hash, &lhq);
        if (nxt_fast_path(ret != NXT_OK)) {
            return NXT_ERROR;
        }
    }

    functions = vm->shared->functions;

    for (i = NJS_FUNCTION_EVAL; i < NJS_FUNCTION_MAX; i++) {
        if (function_init[i] != NULL) {
            ret = njs_object_hash_create(vm, &functions[i].object.shared_hash,
                                         function_init[i]->properties,
                                         function_init[i]->items);
            if (nxt_slow_path(ret != NXT_OK)) {
                return NXT_ERROR;
            }
        }

        functions[i].object.shared = 1;
        functions[i].object.extensible = 1;
        functions[i].native = 1;
        functions[i].args_offset = 1;
        functions[i].u.native = native_functions[i].native;
        functions[i].args_types[0] = native_functions[i].args_types[0];
        functions[i].args_types[1] = native_functions[i].args_types[1];
        functions[i].args_types[2] = native_functions[i].args_types[2];
        functions[i].args_types[3] = native_functions[i].args_types[3];
        functions[i].args_types[4] = native_functions[i].args_types[4];
    }

    prototypes = vm->shared->prototypes;

    for (i = NJS_PROTOTYPE_OBJECT; i < NJS_PROTOTYPE_MAX; i++) {
        prototypes[i] = prototype_values[i];

        ret = njs_object_hash_create(vm, &prototypes[i].object.shared_hash,
                                     njs_prototype_init[i]->properties,
                                     njs_prototype_init[i]->items);
        if (nxt_slow_path(ret != NXT_OK)) {
            return NXT_ERROR;
        }
    }

    prototypes[NJS_PROTOTYPE_REGEXP].regexp.pattern =
                                              vm->shared->empty_regexp_pattern;

    constructors = vm->shared->constructors;

    for (i = NJS_CONSTRUCTOR_OBJECT; i < NJS_CONSTRUCTOR_MAX; i++) {
        constructors[i].object.shared = 0;
        constructors[i].object.extensible = 1;
        constructors[i].native = 1;
        constructors[i].ctor = 1;
        constructors[i].args_offset = 1;
        constructors[i].u.native = native_constructors[i].native;
        constructors[i].args_types[0] = native_constructors[i].args_types[0];
        constructors[i].args_types[1] = native_constructors[i].args_types[1];
        constructors[i].args_types[2] = native_constructors[i].args_types[2];
        constructors[i].args_types[3] = native_constructors[i].args_types[3];
        constructors[i].args_types[4] = native_constructors[i].args_types[4];

        ret = njs_object_hash_create(vm, &constructors[i].object.shared_hash,
                                     njs_constructor_init[i]->properties,
                                     njs_constructor_init[i]->items);
        if (nxt_slow_path(ret != NXT_OK)) {
            return NXT_ERROR;
        }
    }

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


nxt_array_t *
njs_vm_completions(njs_vm_t *vm, nxt_str_t *expression)
{
    size_t       size;
    nxt_array_t  *completions;

    if (expression == NULL) {
        if (njs_builtin_completions(vm, &size, NULL) != NXT_OK) {
            return NULL;
        }

        completions = nxt_array_create(size, sizeof(nxt_str_t),
                                       &njs_array_mem_proto,
                                       vm->mem_cache_pool);

        if (nxt_slow_path(completions == NULL)) {
            return NULL;
        }

        if (njs_builtin_completions(vm, &size, completions->start) != NXT_OK) {
            return NULL;
        }

        completions->items = size;

        return completions;
    }

    return njs_vm_expression_completions(vm, expression);
}


static nxt_int_t
njs_builtin_completions(njs_vm_t *vm, size_t *size, nxt_str_t *completions)
{
    char                    *compl;
    size_t                  n, len;
    nxt_str_t               string;
    nxt_uint_t              i, k;
    njs_extern_t            *ext_object, *ext_prop;
    njs_object_t            *objects;
    njs_keyword_t           *keyword;
    njs_function_t          *constructors;
    njs_object_prop_t       *prop;
    nxt_lvlhsh_each_t       lhe, lhe_prop;
    njs_object_prototype_t  *prototypes;

    n = 0;

    nxt_lvlhsh_each_init(&lhe, &njs_keyword_hash_proto);

    for ( ;; ) {
        keyword = nxt_lvlhsh_each(&vm->shared->keywords_hash, &lhe);

        if (keyword == NULL) {
            break;
        }

        if (completions != NULL) {
            completions[n++] = keyword->name;

        } else {
            n++;
        }
    }

    objects = vm->shared->objects;

    for (i = NJS_OBJECT_THIS; i < NJS_OBJECT_MAX; i++) {
        if (njs_object_init[i] == NULL) {
            continue;
        }

        nxt_lvlhsh_each_init(&lhe, &njs_object_hash_proto);

        for ( ;; ) {
            prop = nxt_lvlhsh_each(&objects[i].shared_hash, &lhe);

            if (prop == NULL) {
                break;
            }

            if (completions != NULL) {
                njs_string_get(&prop->name, &string);
                len = njs_object_init[i]->name.length + string.length + 2;

                compl = nxt_mem_cache_zalloc(vm->mem_cache_pool, len);
                if (compl == NULL) {
                    return NXT_ERROR;
                }

                snprintf(compl, len, "%s.%s", njs_object_init[i]->name.start,
                         string.start);

                completions[n].length = len;
                completions[n++].start = (u_char *) compl;

            } else {
                n++;
            }
        }
    }

    prototypes = vm->shared->prototypes;

    for (i = NJS_PROTOTYPE_OBJECT; i < NJS_PROTOTYPE_MAX; i++) {
        nxt_lvlhsh_each_init(&lhe, &njs_object_hash_proto);

        for ( ;; ) {
            prop = nxt_lvlhsh_each(&prototypes[i].object.shared_hash, &lhe);

            if (prop == NULL) {
                break;
            }

            if (completions != NULL) {
                njs_string_get(&prop->name, &string);
                len = string.length + 2;

                compl = nxt_mem_cache_zalloc(vm->mem_cache_pool, len);
                if (compl == NULL) {
                    return NXT_ERROR;
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

            } else {
                n++;
            }
        }
    }

    constructors = vm->shared->constructors;

    for (i = NJS_CONSTRUCTOR_OBJECT; i < NJS_CONSTRUCTOR_MAX; i++) {
        nxt_lvlhsh_each_init(&lhe, &njs_object_hash_proto);

        for ( ;; ) {
            prop = nxt_lvlhsh_each(&constructors[i].object.shared_hash, &lhe);

            if (prop == NULL) {
                break;
            }

            if (completions != NULL) {
                njs_string_get(&prop->name, &string);
                len = njs_constructor_init[i]->name.length + string.length + 2;

                compl = nxt_mem_cache_zalloc(vm->mem_cache_pool, len);
                if (compl == NULL) {
                    return NXT_ERROR;
                }

                snprintf(compl, len, "%s.%s",
                         njs_constructor_init[i]->name.start, string.start);

                completions[n].length = len;
                completions[n++].start = (u_char *) compl;

            } else {
                n++;
            }
        }
    }

    nxt_lvlhsh_each_init(&lhe, &njs_extern_hash_proto);

    for ( ;; ) {
        ext_object = nxt_lvlhsh_each(&vm->externals_hash, &lhe);

        if (ext_object == NULL) {
            break;
        }

        nxt_lvlhsh_each_init(&lhe_prop, &njs_extern_hash_proto);

        if (completions != NULL) {
            len = ext_object->name.length + 1;
            compl = nxt_mem_cache_zalloc(vm->mem_cache_pool, len);
            if (compl == NULL) {
                return NXT_ERROR;
            }

            snprintf(compl, len, "%.*s",
                     (int) ext_object->name.length, ext_object->name.start);

            completions[n].length = len;
            completions[n++].start = (u_char *) compl;

        } else {
            n++;
        }

        for ( ;; ) {
            ext_prop = nxt_lvlhsh_each(&ext_object->hash, &lhe_prop);

            if (ext_prop == NULL) {
                break;
            }

            if (completions != NULL) {
                len = ext_object->name.length + ext_prop->name.length + 2;
                compl = nxt_mem_cache_zalloc(vm->mem_cache_pool, len);
                if (compl == NULL) {
                    return NXT_ERROR;
                }

                snprintf(compl, len, "%.*s.%.*s",
                         (int) ext_object->name.length, ext_object->name.start,
                         (int) ext_prop->name.length, ext_prop->name.start);

                completions[n].length = len;
                completions[n++].start = (u_char *) compl;

            } else {
                n++;
            }
        }
    }

    if (size) {
        *size = n;
    }

    return NXT_OK;
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


nxt_int_t
njs_builtin_match_native_function(njs_vm_t *vm, njs_function_t *function,
    nxt_str_t *name)
{
    char                    *buf;
    size_t                  len;
    nxt_str_t               string;
    nxt_uint_t              i;
    njs_module_t            *module;
    njs_object_t            *objects;
    njs_function_t          *constructors;
    njs_object_prop_t       *prop;
    nxt_lvlhsh_each_t       lhe, lhe_prop;
    njs_object_prototype_t  *prototypes;

    objects = vm->shared->objects;

    for (i = NJS_OBJECT_THIS; i < NJS_OBJECT_MAX; i++) {
        if (njs_object_init[i] == NULL) {
            continue;
        }

        nxt_lvlhsh_each_init(&lhe, &njs_object_hash_proto);

        for ( ;; ) {
            prop = nxt_lvlhsh_each(&objects[i].shared_hash, &lhe);

            if (prop == NULL) {
                break;
            }

            if (!njs_is_function(&prop->value)) {
                continue;
            }

            if (function == prop->value.data.u.function) {
                njs_string_get(&prop->name, &string);
                len = njs_object_init[i]->name.length + string.length
                      + sizeof(".");

                buf = nxt_mem_cache_zalloc(vm->mem_cache_pool, len);
                if (buf == NULL) {
                    return NXT_ERROR;
                }

                snprintf(buf, len, "%s.%s", njs_object_init[i]->name.start,
                         string.start);

                name->length = len;
                name->start = (u_char *) buf;

                return NXT_OK;
            }
        }
    }

    prototypes = vm->shared->prototypes;

    for (i = NJS_PROTOTYPE_OBJECT; i < NJS_PROTOTYPE_MAX; i++) {
        nxt_lvlhsh_each_init(&lhe, &njs_object_hash_proto);

        for ( ;; ) {
            prop = nxt_lvlhsh_each(&prototypes[i].object.shared_hash, &lhe);

            if (prop == NULL) {
                break;
            }

            if (!njs_is_function(&prop->value)) {
                continue;
            }

            if (function == prop->value.data.u.function) {
                njs_string_get(&prop->name, &string);
                len = njs_prototype_init[i]->name.length + string.length
                      + sizeof(".prototype.");

                buf = nxt_mem_cache_zalloc(vm->mem_cache_pool, len);
                if (buf == NULL) {
                    return NXT_ERROR;
                }

                snprintf(buf, len, "%s.prototype.%s",
                         njs_prototype_init[i]->name.start, string.start);

                name->length = len;
                name->start = (u_char *) buf;

                return NXT_OK;
            }
        }
    }

    constructors = vm->shared->constructors;

    for (i = NJS_CONSTRUCTOR_OBJECT; i < NJS_CONSTRUCTOR_MAX; i++) {
        nxt_lvlhsh_each_init(&lhe, &njs_object_hash_proto);

        for ( ;; ) {
            prop = nxt_lvlhsh_each(&constructors[i].object.shared_hash, &lhe);

            if (prop == NULL) {
                break;
            }

            if (!njs_is_function(&prop->value)) {
                continue;
            }

            if (function == prop->value.data.u.function) {
                njs_string_get(&prop->name, &string);
                len = njs_constructor_init[i]->name.length + string.length
                      + sizeof(".");

                buf = nxt_mem_cache_zalloc(vm->mem_cache_pool, len);
                if (buf == NULL) {
                    return NXT_ERROR;
                }

                snprintf(buf, len, "%s.%s", njs_constructor_init[i]->name.start,
                         string.start);

                name->length = len;
                name->start = (u_char *) buf;

                return NXT_OK;
            }
        }
    }

    nxt_lvlhsh_each_init(&lhe, &njs_modules_hash_proto);

    for ( ;; ) {
        module = nxt_lvlhsh_each(&vm->modules_hash, &lhe);
        if (module == NULL) {
            break;
        }

        nxt_lvlhsh_each_init(&lhe_prop, &njs_object_hash_proto);

        for ( ;; ) {
            prop = nxt_lvlhsh_each(&module->object.shared_hash, &lhe_prop);
            if (prop == NULL) {
                break;
            }

            if (!njs_is_function(&prop->value)) {
                continue;
            }

            if (function == prop->value.data.u.function) {
                njs_string_get(&prop->name, &string);
                len = module->name.length + string.length + sizeof(".");

                buf = nxt_mem_cache_zalloc(vm->mem_cache_pool, len);
                if (buf == NULL) {
                    return NXT_ERROR;
                }

                snprintf(buf, len, "%s.%s", module->name.start, string.start);

                name->length = len;
                name->start = (u_char *) buf;

                return NXT_OK;
            }
        }
    }

    return NXT_DECLINED;
}
