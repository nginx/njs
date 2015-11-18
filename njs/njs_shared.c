
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
#include <njs_regexp.h>
#include <string.h>


typedef nxt_int_t (*njs_shared_hash_t) (njs_vm_t *vm, nxt_lvlhsh_t *hash);


/* STUB */
static nxt_int_t
njs_stub_hash(njs_vm_t *vm, nxt_lvlhsh_t *hash)
{
    return NXT_OK;
}
static njs_ret_t
njs_stub_function(njs_vm_t *vm, njs_param_t *param)
{
    return NXT_ERROR;
}
/**/


nxt_int_t
njs_shared_objects_create(njs_vm_t *vm)
{
    size_t          size;
    nxt_int_t       ret;
    nxt_uint_t      i;
    njs_object_t    *prototypes;
    njs_function_t  *functions;

    static const njs_shared_hash_t  prototype_hash[] = {
        njs_object_prototype_hash,
        njs_array_prototype_hash,
        njs_stub_hash,
        njs_number_prototype_hash,
        njs_string_prototype_hash,
        njs_function_prototype_hash,
        njs_regexp_prototype_hash,
    };

    static const njs_shared_hash_t  function_hash[] = {
        njs_object_function_hash,
        njs_array_function_hash,
        njs_stub_hash,
        njs_number_function_hash,
        njs_string_function_hash,
        njs_function_function_hash,
        njs_regexp_function_hash,
        njs_stub_hash,
    };

    static const njs_native_t  native_functions[] = {
        njs_object_function,
        njs_array_function,
        njs_stub_function,
        njs_number_function,
        njs_string_ctor_function,
        njs_stub_function,
        njs_regexp_function,
        njs_stub_function,
    };

    size = NJS_PROTOTYPE_MAX * sizeof(njs_object_t);

    prototypes = nxt_mem_cache_zalign(vm->mem_cache_pool, sizeof(njs_value_t),
                                      size);
    if (nxt_slow_path(prototypes == NULL)) {
        return NXT_ERROR;
    }

    vm->shared->prototypes = prototypes;

    for (i = NJS_PROTOTYPE_OBJECT; i < NJS_PROTOTYPE_MAX; i++) {
        /* TODO: shared hash: prototype & constructor getters, methods */

        ret = prototype_hash[i](vm, &prototypes[i].shared_hash);
        if (nxt_slow_path(ret != NXT_OK)) {
            return NXT_ERROR;
        }
    }

    size = NJS_FUNCTION_MAX * sizeof(njs_function_t);

    functions = nxt_mem_cache_zalign(vm->mem_cache_pool, sizeof(njs_value_t),
                                     size);
    if (nxt_slow_path(functions == NULL)) {
        return NXT_ERROR;
    }

    vm->shared->functions = functions;

    for (i = NJS_FUNCTION_OBJECT; i < NJS_FUNCTION_MAX; i++) {
        functions[i].native = 1;
        functions[i].args_offset = 1;
        functions[i].code.native = native_functions[i];

        ret = function_hash[i](vm, &functions[i].object.shared_hash);
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
 * Function(),
 * Function.__proto__           -> Function_Prototype,
 * Function_Prototype.__proto__ -> Object_Prototype,
 *
 * [...]
 *
 * eval().
 */

nxt_int_t
njs_shared_objects_clone(njs_vm_t *vm)
{
    size_t          size;
    nxt_uint_t      i;
    njs_value_t     *values;
    njs_object_t    *prototypes;
    njs_function_t  *functions;

    size = NJS_PROTOTYPE_MAX * sizeof(njs_object_t);

    prototypes = nxt_mem_cache_align(vm->mem_cache_pool, sizeof(njs_value_t),
                                     size);
    if (nxt_slow_path(prototypes == NULL)) {
        return NXT_ERROR;
    }

    vm->prototypes = prototypes;

    memcpy(prototypes, vm->shared->prototypes, size);

    for (i = NJS_PROTOTYPE_ARRAY; i < NJS_PROTOTYPE_MAX; i++) {
        prototypes[i].__proto__ = &prototypes[NJS_PROTOTYPE_OBJECT];
    }

    size = NJS_FUNCTION_MAX * sizeof(njs_function_t);

    functions = nxt_mem_cache_align(vm->mem_cache_pool, sizeof(njs_value_t),
                                    size);
    if (nxt_slow_path(functions == NULL)) {
        return NXT_ERROR;
    }

    vm->functions = functions;

    memcpy(functions, vm->shared->functions, size);

    values = vm->scopes[NJS_SCOPE_GLOBAL];

    for (i = NJS_FUNCTION_OBJECT; i < NJS_FUNCTION_MAX; i++) {
        values[i].type = NJS_FUNCTION;
        values[i].data.truth = 1;
        values[i].data.u.function = &functions[i];
        functions[i].object.__proto__ = &prototypes[NJS_FUNCTION_FUNCTION];
    }

    return NXT_OK;
}
