
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) NGINX, Inc.
 */

#include <nxt_types.h>
#include <nxt_clang.h>
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
#include <njs_function.h>
#include <njs_regexp.h>
#include <njs_date.h>
#include <njs_math.h>
#include <string.h>


typedef struct {
    njs_function_native_t  native;
    uint8_t                args_types[NJS_ARGS_TYPES_MAX];
} njs_function_init_t;


nxt_int_t
njs_builtin_objects_create(njs_vm_t *vm)
{
    nxt_int_t                         ret;
    nxt_uint_t                        i;
    njs_object_t                      *objects, *prototypes;
    njs_function_t                    *functions;

    static const njs_object_init_t    *prototype_init[] = {
        &njs_object_prototype_init,
        &njs_array_prototype_init,
        &njs_boolean_prototype_init,
        &njs_number_prototype_init,
        &njs_string_prototype_init,
        &njs_function_prototype_init,
        &njs_regexp_prototype_init,
        &njs_date_prototype_init,
    };

    static const njs_object_init_t    *function_init[] = {
        &njs_object_constructor_init,
        &njs_array_constructor_init,
        &njs_boolean_constructor_init,
        &njs_number_constructor_init,
        &njs_string_constructor_init,
        &njs_function_constructor_init,
        &njs_regexp_constructor_init,
        &njs_date_constructor_init,

        &njs_eval_function_init,
    };

    static const njs_function_init_t  native_functions[] = {
        /* SunC does not allow empty array initialization. */
        { njs_object_constructor,   { 0 } },
        { njs_array_constructor,    { 0 } },
        { njs_boolean_constructor,  { 0 } },
        { njs_number_constructor,   { NJS_SKIP_ARG, NJS_NUMBER_ARG } },
        { njs_string_constructor,   { NJS_SKIP_ARG, NJS_STRING_ARG } },
        { njs_function_constructor, { 0 } },
        { njs_regexp_constructor,
          { NJS_SKIP_ARG, NJS_STRING_ARG, NJS_STRING_ARG } },
        { njs_date_constructor,     { 0 } },

        { njs_eval_function,        { 0 } },
    };

    static const njs_object_init_t    *objects_init[] = {
        &njs_math_object_init,
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

    for (i = NJS_OBJECT_MATH; i < NJS_OBJECT_MAX; i++) {
        ret = njs_object_hash_create(vm, &objects[i].shared_hash,
                                     objects_init[i]->properties,
                                     objects_init[i]->items);
        if (nxt_slow_path(ret != NXT_OK)) {
            return NXT_ERROR;
        }

        objects[i].shared = 1;
    }

    prototypes = vm->shared->prototypes;

    for (i = NJS_PROTOTYPE_OBJECT; i < NJS_PROTOTYPE_MAX; i++) {
        ret = njs_object_hash_create(vm, &prototypes[i].shared_hash,
                                     prototype_init[i]->properties,
                                     prototype_init[i]->items);
        if (nxt_slow_path(ret != NXT_OK)) {
            return NXT_ERROR;
        }
    }

    functions = vm->shared->functions;

    for (i = NJS_FUNCTION_OBJECT; i < NJS_FUNCTION_MAX; i++) {
        functions[i].object.shared = 0;
        functions[i].native = 1;
        functions[i].args_offset = 1;
        functions[i].u.native = native_functions[i].native;
        functions[i].args_types[0] = native_functions[i].args_types[0];
        functions[i].args_types[1] = native_functions[i].args_types[1];
        functions[i].args_types[2] = native_functions[i].args_types[2];

        ret = njs_object_hash_create(vm, &functions[i].object.shared_hash,
                                     function_init[i]->properties,
                                     function_init[i]->items);
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
 * eval(),
 * eval.__proto__               -> Function_Prototype.
 */

nxt_int_t
njs_builtin_objects_clone(njs_vm_t *vm)
{
    size_t        size;
    nxt_uint_t    i;
    njs_value_t   *values;
    njs_object_t  *function_prototype;

    /*
     * Copy both prototypes and functions arrays by one memcpy()
     * because they are stored together.
     */
    size = NJS_PROTOTYPE_MAX * sizeof(njs_object_t)
           + NJS_FUNCTION_MAX * sizeof(njs_function_t);

    memcpy(vm->prototypes, vm->shared->prototypes, size);

    for (i = NJS_PROTOTYPE_ARRAY; i < NJS_PROTOTYPE_MAX; i++) {
        vm->prototypes[i].__proto__ = &vm->prototypes[NJS_PROTOTYPE_OBJECT];
    }

    function_prototype = &vm->prototypes[NJS_FUNCTION_FUNCTION];
    values = vm->scopes[NJS_SCOPE_GLOBAL];

    for (i = NJS_FUNCTION_OBJECT; i < NJS_FUNCTION_MAX; i++) {
        values[i].type = NJS_FUNCTION;
        values[i].data.truth = 1;
        values[i].data.u.function = &vm->functions[i];
        vm->functions[i].object.__proto__ = function_prototype;
    }

    return NXT_OK;
}
