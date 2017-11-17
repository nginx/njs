
/*
 * Copyright (C) Dmitry Volyntsev
 * Copyright (C) NGINX, Inc.
 */

#include <nxt_auto_config.h>
#include <nxt_types.h>
#include <nxt_clang.h>
#include <nxt_string.h>
#include <nxt_stub.h>
#include <nxt_djb_hash.h>
#include <nxt_array.h>
#include <nxt_lvlhsh.h>
#include <nxt_random.h>
#include <nxt_mem_cache_pool.h>
#include <njscript.h>
#include <njs_vm.h>
#include <njs_number.h>
#include <njs_string.h>
#include <njs_object.h>
#include <njs_function.h>
#include <njs_error.h>
#include <njs_module.h>
#include <string.h>
#include <stdio.h>


static nxt_int_t
njs_module_hash_test(nxt_lvlhsh_query_t *lhq, void *data)
{
    njs_module_t  *module;

    module = data;

    if (nxt_strstr_eq(&lhq->key, &module->name)) {
        return NXT_OK;
    }

    return NXT_DECLINED;
}


const nxt_lvlhsh_proto_t  njs_modules_hash_proto
    nxt_aligned(64) =
{
    NXT_LVLHSH_DEFAULT,
    0,
    njs_module_hash_test,
    njs_lvlhsh_alloc,
    njs_lvlhsh_free,
};


njs_ret_t njs_module_require(njs_vm_t *vm, njs_value_t *args,
    nxt_uint_t nargs, njs_index_t unused)
{
    njs_module_t        *module;
    nxt_lvlhsh_query_t  lhq;

    if (nargs < 2) {
        njs_exception_type_error(vm, "missing path", NULL);
        return NJS_ERROR;
    }

    njs_string_get(&args[1], &lhq.key);
    lhq.key_hash = nxt_djb_hash(lhq.key.start, lhq.key.length);
    lhq.proto = &njs_modules_hash_proto;

    if (nxt_lvlhsh_find(&vm->modules_hash, &lhq) == NXT_OK) {
        module = lhq.value;
        module->object.__proto__ = &vm->prototypes[NJS_PROTOTYPE_OBJECT].object;

        vm->retval.data.u.object = &module->object;
        vm->retval.type = NJS_OBJECT;
        vm->retval.data.truth = 1;

        return NXT_OK;
    }

    njs_exception_error(vm, "Cannot find module '%.*s'", (int) lhq.key.length,
                        lhq.key.start);

    return NJS_ERROR;
}
