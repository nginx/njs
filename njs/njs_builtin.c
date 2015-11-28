
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) NGINX, Inc.
 */

#include <nxt_types.h>
#include <nxt_clang.h>
#include <nxt_stub.h>
#include <nxt_array.h>
#include <nxt_lvlhsh.h>
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
#include <string.h>


nxt_int_t
njs_builtin_objects_create(njs_vm_t *vm)
{
    nxt_int_t                       ret;
    nxt_uint_t                      i;
    njs_object_t                    *prototypes;
    njs_function_t                  *functions;

    static const njs_object_init_t  *prototype_init[] = {
        &njs_object_prototype_init,
        &njs_array_prototype_init,
        &njs_boolean_prototype_init,
        &njs_number_prototype_init,
        &njs_string_prototype_init,
        &njs_function_prototype_init,
        &njs_regexp_prototype_init,
    };

    static const njs_object_init_t  *function_init[] = {
        &njs_object_constructor_init,
        &njs_array_constructor_init,
        &njs_boolean_constructor_init,
        &njs_number_constructor_init,
        &njs_string_constructor_init,
        &njs_function_constructor_init,
        &njs_regexp_constructor_init,

        &njs_eval_function_init,
    };

    static const njs_native_t       native_functions[] = {
        njs_object_constructor,
        njs_array_constructor,
        njs_boolean_constructor,
        njs_number_constructor,
        njs_string_constructor,
        njs_function_constructor,
        njs_regexp_constructor,

        njs_eval_function,
    };

    static const njs_object_prop_t  null_proto_property = {
        njs_value(NJS_NULL, 0, 0.0),
        njs_string("__proto__"),
        NJS_WHITEOUT, 0, 0, 0,
    };

    ret = njs_object_hash_create(vm, &vm->shared->null_proto_hash,
                                 &null_proto_property, 1);
    if (nxt_slow_path(ret != NXT_OK)) {
        return NXT_ERROR;
    }

    prototypes = vm->shared->prototypes;

    for (i = NJS_PROTOTYPE_OBJECT; i < NJS_PROTOTYPE_MAX; i++) {
        /* TODO: shared hash: prototype & constructor getters, methods */

        ret = njs_object_hash_create(vm, &prototypes[i].shared_hash,
                                     prototype_init[i]->properties,
                                     prototype_init[i]->items);
        if (nxt_slow_path(ret != NXT_OK)) {
            return NXT_ERROR;
        }
    }

    functions = vm->shared->functions;

    for (i = NJS_FUNCTION_OBJECT; i < NJS_FUNCTION_MAX; i++) {
        functions[i].native = 1;
        functions[i].args_offset = 1;
        functions[i].code.native = native_functions[i];

        ret = njs_object_hash_create(vm, &functions[i].object.shared_hash,
                                     function_init[i]->properties,
                                     function_init[i]->items);
        if (nxt_slow_path(ret != NXT_OK)) {
            return NXT_ERROR;
        }
    }

    /* TODO: create function shared hash: prototype+contructor getter */

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
