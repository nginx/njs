
/*
 * Copyright (C) Dmitry Volyntsev
 * Copyright (C) NGINX, Inc.
 */


#include <njs_main.h>


static njs_int_t
njs_module_hash_test(njs_flathsh_query_t *fhq, void *data)
{
    njs_mod_t  *module;

    module = *(njs_mod_t **) data;

    if (njs_strstr_eq(&fhq->key, &module->name)) {
        return NJS_OK;
    }

    return NJS_DECLINED;
}


const njs_flathsh_proto_t  njs_modules_hash_proto
    njs_aligned(64) =
{
    njs_module_hash_test,
    njs_flathsh_proto_alloc,
    njs_flathsh_proto_free,
};


njs_mod_t *
njs_module_find(njs_vm_t *vm, njs_str_t *name, njs_bool_t shared)
{
    njs_int_t            ret;
    njs_mod_t            *shrd, *module;
    njs_object_t         *object;
    njs_object_prop_t    *prop;
    njs_flathsh_query_t  fhq;

    fhq.key = *name;
    fhq.key_hash = njs_djb_hash(name->start, name->length);
    fhq.proto = &njs_modules_hash_proto;

    ret = njs_flathsh_find(&vm->modules_hash, &fhq);
    if (ret == NJS_OK) {
        return njs_prop_module(fhq.value);
    }

    ret = njs_flathsh_find(&vm->shared->modules_hash, &fhq);
    if (ret == NJS_OK) {
        shrd = ((njs_object_prop_t *)fhq.value)->u.mod;

        if (shared) {
            return shrd;
        }

        module = njs_mp_alloc(vm->mem_pool, sizeof(njs_mod_t));
        if (njs_slow_path(module == NULL)) {
            njs_memory_error(vm);
            return NULL;
        }

        memcpy(module, shrd, sizeof(njs_mod_t));

        object = njs_object_value_copy(vm, &module->value);
        if (njs_slow_path(object == NULL)) {
            return NULL;
        }

        fhq.replace = 0;
        fhq.pool = vm->mem_pool;

        ret = njs_flathsh_insert(&vm->modules_hash, &fhq);
        if (njs_slow_path(ret != NJS_OK)) {
            return NULL;
        }

        prop = fhq.value;

        prop->u.mod = module;

        return module;

    }

    return NULL;
}


njs_mod_t *
njs_module_add(njs_vm_t *vm, njs_str_t *name, njs_value_t *value)
{
    njs_int_t            ret;
    njs_mod_t            *module;
    njs_object_prop_t    *prop;
    njs_flathsh_query_t  fhq;

    module = njs_mp_zalloc(vm->mem_pool, sizeof(njs_mod_t));
    if (njs_slow_path(module == NULL)) {
        njs_memory_error(vm);
        return NULL;
    }

    ret = njs_name_copy(vm, &module->name, name);
    if (njs_slow_path(ret != NJS_OK)) {
        njs_memory_error(vm);
        return NULL;
    }

    fhq.replace = 0;
    fhq.key = *name;
    fhq.key_hash = njs_djb_hash(name->start, name->length);
    fhq.pool = vm->mem_pool;
    fhq.proto = &njs_modules_hash_proto;

    ret = njs_flathsh_insert(&vm->shared->modules_hash, &fhq);
    if (njs_slow_path(ret != NJS_OK)) {
        njs_internal_error(vm, "flathsh insert failed");
        return NULL;
    }

    prop = fhq.value;

    prop->u.mod = module;

    if (value != NULL) {
        njs_value_assign(&module->value, value);
        module->function.native = 1;
    }

    return module;
}


njs_int_t
njs_module_require(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t unused, njs_value_t *retval)
{
    njs_int_t    ret;
    njs_str_t    name;
    njs_mod_t    *module;
    njs_value_t  *path;

    if (nargs < 2) {
        njs_type_error(vm, "missing path");
        return NJS_ERROR;
    }

    path = njs_argument(args, 1);
    ret = njs_value_to_string(vm, path, path);
    if (njs_slow_path(ret != NJS_OK)) {
        return ret;
    }

    njs_string_get(vm, path, &name);

    module = njs_module_find(vm, &name, 0);
    if (njs_slow_path(module == NULL)) {
        njs_error(vm, "Cannot load module \"%V\"", &name);

        return NJS_ERROR;
    }

    njs_value_assign(retval, &module->value);

    return NJS_OK;
}
