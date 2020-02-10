
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) NGINX, Inc.
 */


#include <njs_main.h>


typedef struct njs_extern_part_s  njs_extern_part_t;

struct njs_extern_part_s {
    njs_extern_part_t  *next;
    njs_str_t          str;
};


static njs_int_t
njs_extern_hash_test(njs_lvlhsh_query_t *lhq, void *data)
{
    njs_extern_t  *ext;

    ext = (njs_extern_t *) data;

    if (njs_strstr_eq(&lhq->key, &ext->name)) {
        return NJS_OK;
    }

    return NJS_DECLINED;
}


const njs_lvlhsh_proto_t  njs_extern_hash_proto
    njs_aligned(64) =
{
    NJS_LVLHSH_DEFAULT,
    njs_extern_hash_test,
    njs_lvlhsh_alloc,
    njs_lvlhsh_free,
};


static njs_extern_t *
njs_vm_external_add(njs_vm_t *vm, njs_lvlhsh_t *hash, njs_external_t *external,
    njs_uint_t n)
{
    njs_int_t           ret;
    njs_extern_t        *ext, *child;
    njs_function_t      *function;
    njs_lvlhsh_query_t  lhq;

    do {
        ext = njs_mp_alloc(vm->mem_pool, sizeof(njs_extern_t));
        if (njs_slow_path(ext == NULL)) {
            goto memory_error;
        }

        ext->name = external->name;

        njs_lvlhsh_init(&ext->hash);

        ext->type = external->type;
        ext->get = external->get;
        ext->set = external->set;
        ext->find = external->find;
        ext->keys = external->keys;
        ext->data = external->data;

        if (external->method != NULL) {
            function = njs_mp_zalloc(vm->mem_pool, sizeof(njs_function_t));
            if (njs_slow_path(function == NULL)) {
                goto memory_error;
            }

            /*
             * njs_mp_zalloc() does also:
             *   njs_lvlhsh_init(&function->object.hash);
             *   function->object.__proto__ = NULL;
             *   function->ctor = 0;
             */

            function->object.__proto__ =
                              &vm->prototypes[NJS_OBJ_TYPE_FUNCTION].object;
            function->object.shared_hash = vm->shared->arrow_instance_hash;
            function->object.type = NJS_FUNCTION;
            function->object.shared = 1;
            function->object.extensible = 1;
            function->args_offset = 1;
            function->native = 1;
            function->u.native = external->method;

            ext->function = function;

        } else {
            ext->function = NULL;
        }

        if (external->properties != NULL) {
            child = njs_vm_external_add(vm, &ext->hash, external->properties,
                                        external->nproperties);
            if (njs_slow_path(child == NULL)) {
                goto memory_error;
            }
        }

        if (hash != NULL) {
            lhq.key_hash = njs_djb_hash(external->name.start,
                                        external->name.length);
            lhq.key = ext->name;
            lhq.replace = 0;
            lhq.value = ext;
            lhq.pool = vm->mem_pool;
            lhq.proto = &njs_extern_hash_proto;

            ret = njs_lvlhsh_insert(hash, &lhq);
            if (njs_slow_path(ret != NJS_OK)) {
                njs_internal_error(vm, "lvlhsh insert failed");
                return NULL;
            }
        }

        external++;
        n--;

    } while (n != 0);

    return ext;

memory_error:

    njs_memory_error(vm);

    return NULL;
}


const njs_extern_t *
njs_vm_external_prototype(njs_vm_t *vm, njs_external_t *external)
{
    return njs_vm_external_add(vm, &vm->external_prototypes_hash, external, 1);
}


njs_int_t
njs_vm_external_create(njs_vm_t *vm, njs_value_t *ext_val,
    const njs_extern_t *proto, njs_external_ptr_t object)
{
    void       *obj;
    uint32_t   n;
    njs_arr_t  *externals;

    if (njs_slow_path(proto == NULL)) {
        return NJS_ERROR;
    }

    if (vm->external_objects->mem_pool != vm->mem_pool) {

        /* Making a local copy of externals in shared VM. */

        n = vm->external_objects->items;

        externals = njs_arr_create(vm->mem_pool, n + 4, sizeof(void *));
        if (njs_slow_path(externals == NULL)) {
            return NJS_ERROR;
        }

        if (n > 0) {
            memcpy(externals->start, vm->external_objects->start,
                   n * sizeof(void *));
            externals->items = n;
        }

        vm->external_objects = externals;
    }

    obj = njs_arr_add(vm->external_objects);
    if (njs_slow_path(obj == NULL)) {
        return NJS_ERROR;
    }

    memcpy(obj, &object, sizeof(void *));

    if (proto->type != NJS_EXTERN_METHOD) {
        ext_val->type = NJS_EXTERNAL;
        ext_val->data.truth = 1;
        ext_val->external.proto = proto;
        ext_val->external.index = vm->external_objects->items - 1;

    } else {
        njs_set_function(ext_val, proto->function);
    }

    return NJS_OK;
}


njs_external_ptr_t
njs_vm_external(njs_vm_t *vm, const njs_value_t *value)
{
    if (njs_fast_path(njs_is_external(value))) {
        return njs_extern_object(vm, value);
    }

    njs_type_error(vm, "external value is expected");

    return NULL;
}


njs_array_t *
njs_extern_keys_array(njs_vm_t *vm, const njs_extern_t *external)
{
    uint32_t            n, keys_length;
    njs_int_t           ret;
    njs_array_t         *keys;
    const njs_lvlhsh_t  *hash;
    njs_lvlhsh_each_t   lhe;
    const njs_extern_t  *ext;

    keys_length = 0;

    njs_lvlhsh_each_init(&lhe, &njs_extern_hash_proto);

    hash = &external->hash;

    for ( ;; ) {
        ext = njs_lvlhsh_each(hash, &lhe);

        if (ext == NULL) {
            break;
        }

        keys_length++;
    }

    keys = njs_array_alloc(vm, 1, keys_length, NJS_ARRAY_SPARE);
    if (njs_slow_path(keys == NULL)) {
        return NULL;
    }

    n = 0;

    njs_lvlhsh_each_init(&lhe, &njs_extern_hash_proto);

    for ( ;; ) {
        ext = njs_lvlhsh_each(hash, &lhe);

        if (ext == NULL) {
            break;
        }

        ret = njs_string_new(vm, &keys->start[n++], ext->name.start,
                             ext->name.length, 0);

        if (ret != NJS_OK) {
            return NULL;
        }
    }

    return keys;
}


static njs_int_t
njs_external_match(njs_vm_t *vm, njs_function_native_t func, njs_extern_t *ext,
    njs_str_t *name, njs_extern_part_t *head, njs_extern_part_t *ppart)
{
    u_char             *buf, *p;
    size_t             len;
    njs_int_t          ret;
    njs_extern_t       *prop;
    njs_extern_part_t  part, *pr;
    njs_lvlhsh_each_t  lhe;

    ppart->next = &part;

    njs_lvlhsh_each_init(&lhe, &njs_extern_hash_proto);

    for ( ;; ) {
        prop = njs_lvlhsh_each(&ext->hash, &lhe);
        if (prop == NULL) {
            break;
        }

        part.next = NULL;
        part.str = prop->name;

        if (prop->function && prop->function->u.native == func) {
            goto found;
        }

        ret = njs_external_match(vm, func, prop, name, head, &part);
        if (ret != NJS_DECLINED) {
            return ret;
        }
    }

    return NJS_DECLINED;

found:

    len = 0;

    for (pr = head; pr != NULL; pr = pr->next) {
        len += pr->str.length + njs_length(".");
    }

    buf = njs_mp_zalloc(vm->mem_pool, len);
    if (buf == NULL) {
        return NJS_ERROR;
    }

    p = buf;

    for (pr = head; pr != NULL; pr = pr->next) {
        p = njs_sprintf(p, buf + len, "%V.", &pr->str);
    }

    name->start = (u_char *) buf;
    name->length = len - 1;

    return NJS_OK;
}


njs_int_t
njs_external_match_native_function(njs_vm_t *vm, njs_function_native_t func,
    njs_str_t *name)
{
    njs_int_t          ret;
    njs_extern_t       *ext;
    njs_extern_part_t  part;
    njs_lvlhsh_each_t  lhe;

    njs_lvlhsh_each_init(&lhe, &njs_extern_hash_proto);

    for ( ;; ) {
        ext = njs_lvlhsh_each(&vm->external_prototypes_hash, &lhe);
        if (ext == NULL) {
            break;
        }

        part.next = NULL;
        part.str = ext->name;

        ret = njs_external_match(vm, func, ext, name, &part, &part);
        if (ret != NJS_DECLINED) {
            return ret;
        }
    }

    return NJS_DECLINED;
}
