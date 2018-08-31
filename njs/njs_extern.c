
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) NGINX, Inc.
 */

#include <njs_core.h>
#include <string.h>
#include <stdio.h>


typedef struct njs_extern_part_s  njs_extern_part_t;

struct njs_extern_part_s {
    njs_extern_part_t  *next;
    nxt_str_t          str;
};


static nxt_int_t
njs_extern_hash_test(nxt_lvlhsh_query_t *lhq, void *data)
{
    njs_extern_t  *ext;

    ext = (njs_extern_t *) data;

    if (nxt_strstr_eq(&lhq->key, &ext->name)) {
        return NXT_OK;
    }

    return NXT_DECLINED;
}


static nxt_int_t
njs_extern_value_hash_test(nxt_lvlhsh_query_t *lhq, void *data)
{
    njs_extern_value_t  *ev;

    ev = (njs_extern_value_t *) data;

    if (nxt_strstr_eq(&lhq->key, &ev->name)) {
        return NXT_OK;
    }

    return NXT_DECLINED;
}


const nxt_lvlhsh_proto_t  njs_extern_hash_proto
    nxt_aligned(64) =
{
    NXT_LVLHSH_DEFAULT,
    NXT_LVLHSH_BATCH_ALLOC,
    njs_extern_hash_test,
    njs_lvlhsh_alloc,
    njs_lvlhsh_free,
};


const nxt_lvlhsh_proto_t  njs_extern_value_hash_proto
    nxt_aligned(64) =
{
    NXT_LVLHSH_DEFAULT,
    NXT_LVLHSH_BATCH_ALLOC,
    njs_extern_value_hash_test,
    njs_lvlhsh_alloc,
    njs_lvlhsh_free,
};


static njs_extern_t *
njs_vm_external_add(njs_vm_t *vm, nxt_lvlhsh_t *hash, njs_external_t *external,
    nxt_uint_t n)
{
    nxt_int_t           ret;
    njs_extern_t        *ext, *child;
    njs_function_t      *function;
    nxt_lvlhsh_query_t  lhq;

    do {
        ext = nxt_mem_cache_alloc(vm->mem_cache_pool, sizeof(njs_extern_t));
        if (nxt_slow_path(ext == NULL)) {
            goto memory_error;
        }

        ext->name = external->name;

        nxt_lvlhsh_init(&ext->hash);

        ext->type = external->type;
        ext->get = external->get;
        ext->set = external->set;
        ext->find = external->find;
        ext->foreach = external->foreach;
        ext->next = external->next;
        ext->data = external->data;

        if (external->method != NULL) {
            function = nxt_mem_cache_zalloc(vm->mem_cache_pool,
                                            sizeof(njs_function_t));
            if (nxt_slow_path(function == NULL)) {
                goto memory_error;
            }

            /*
             * nxt_mem_cache_zalloc() does also:
             *   nxt_lvlhsh_init(&function->object.hash);
             *   function->object.__proto__ = NULL;
             */

            function->object.__proto__ =
                              &vm->prototypes[NJS_CONSTRUCTOR_FUNCTION].object;
            function->object.shared_hash = vm->shared->function_prototype_hash;
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
            if (nxt_slow_path(child == NULL)) {
                goto memory_error;
            }
        }

        if (hash != NULL) {
            lhq.key_hash = nxt_djb_hash(external->name.start,
                                        external->name.length);
            lhq.key = ext->name;
            lhq.replace = 0;
            lhq.value = ext;
            lhq.pool = vm->mem_cache_pool;
            lhq.proto = &njs_extern_hash_proto;

            ret = nxt_lvlhsh_insert(hash, &lhq);
            if (nxt_slow_path(ret != NXT_OK)) {
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


nxt_int_t
njs_vm_external_create(njs_vm_t *vm, njs_value_t *ext_val,
    const njs_extern_t *proto,  njs_external_ptr_t object)
{
    void  *obj;

    if (nxt_slow_path(proto == NULL)) {
        return NXT_ERROR;
    }

    obj = nxt_array_add(vm->external_objects, &njs_array_mem_proto,
                        vm->mem_cache_pool);
    if (nxt_slow_path(obj == NULL)) {
        return NXT_ERROR;
    }

    memcpy(obj, &object, sizeof(void *));

    ext_val->type = NJS_EXTERNAL;
    ext_val->data.truth = 1;
    ext_val->external.proto = proto;
    ext_val->external.index = vm->external_objects->items - 1;

    return NXT_OK;
}


nxt_int_t
njs_vm_external_bind(njs_vm_t *vm, const nxt_str_t *var_name,
    const njs_value_t *value)
{
    nxt_int_t           ret;
    njs_extern_value_t  *ev;
    nxt_lvlhsh_query_t  lhq;

    if (nxt_slow_path(!njs_is_external(value))) {
        return NXT_ERROR;
    }

    ev = nxt_mem_cache_align(vm->mem_cache_pool, sizeof(njs_value_t),
                             sizeof(njs_extern_value_t));
    if (nxt_slow_path(ev == NULL)) {
        njs_memory_error(vm);
        return NXT_ERROR;
    }

    ev->value = *value;
    ev->name = *var_name;

    lhq.key = *var_name;
    lhq.key_hash = nxt_djb_hash(lhq.key.start, lhq.key.length);
    lhq.proto = &njs_extern_value_hash_proto;
    lhq.value = ev;
    lhq.replace = 0;
    lhq.pool = vm->mem_cache_pool;

    ret = nxt_lvlhsh_insert(&vm->externals_hash, &lhq);
    if (nxt_slow_path(ret != NXT_OK)) {
        njs_internal_error(vm, "lvlhsh insert failed");
        return ret;
    }

    return NXT_OK;
}


nxt_noinline njs_external_ptr_t
njs_vm_external(njs_vm_t *vm, const njs_value_t *value)
{
    if (nxt_fast_path(njs_is_external(value))) {
        return njs_extern_object(vm, value);
    }

    njs_type_error(vm, "external value is expected");

    return NULL;
}


njs_array_t *
njs_extern_keys_array(njs_vm_t *vm, const njs_extern_t *external)
{
    uint32_t            n, keys_length;
    njs_ret_t           ret;
    njs_array_t         *keys;
    const nxt_lvlhsh_t  *hash;
    nxt_lvlhsh_each_t   lhe;
    const njs_extern_t  *ext;

    keys_length = 0;

    nxt_lvlhsh_each_init(&lhe, &njs_extern_hash_proto);

    hash = &external->hash;

    for ( ;; ) {
        ext = nxt_lvlhsh_each(hash, &lhe);

        if (ext == NULL) {
            break;
        }

        keys_length++;
    }

    keys = njs_array_alloc(vm, keys_length, NJS_ARRAY_SPARE);
    if (nxt_slow_path(keys == NULL)) {
        return NULL;
    }

    n = 0;

    nxt_lvlhsh_each_init(&lhe, &njs_extern_hash_proto);

    for ( ;; ) {
        ext = nxt_lvlhsh_each(hash, &lhe);

        if (ext == NULL) {
            break;
        }

        ret = njs_string_create(vm, &keys->start[n++], ext->name.start,
                                ext->name.length, 0);

        if (ret != NXT_OK) {
            return NULL;
        }
    }

    return keys;
}


njs_value_t *
njs_parser_external(njs_vm_t *vm, njs_parser_t *parser)
{
    nxt_lvlhsh_query_t  lhq;
    njs_extern_value_t  *ev;

    lhq.key_hash = parser->lexer->key_hash;
    lhq.key = parser->lexer->text;
    lhq.proto = &njs_extern_value_hash_proto;

    if (nxt_lvlhsh_find(&vm->externals_hash, &lhq) == NXT_OK) {
        ev = (njs_extern_value_t *) lhq.value;
        return &ev->value;
    }

    return NULL;
}


static nxt_int_t
njs_external_match(njs_vm_t *vm, njs_function_native_t func, njs_extern_t *ext,
    nxt_str_t *name, njs_extern_part_t *head, njs_extern_part_t *ppart)
{
    char               *buf, *p;
    size_t             len;
    nxt_int_t          ret;
    njs_extern_t       *prop;
    njs_extern_part_t  part, *pr;
    nxt_lvlhsh_each_t  lhe;

    ppart->next = &part;

    nxt_lvlhsh_each_init(&lhe, &njs_extern_hash_proto);

    for ( ;; ) {
        prop = nxt_lvlhsh_each(&ext->hash, &lhe);
        if (prop == NULL) {
            break;
        }

        part.next = NULL;
        part.str = prop->name;

        if (prop->function && prop->function->u.native == func) {
            goto found;
        }

        ret = njs_external_match(vm, func, prop, name, head, &part);
        if (ret != NXT_DECLINED) {
            return ret;
        }
    }

    return NXT_DECLINED;

found:

    len = 0;

    for (pr = head; pr != NULL; pr = pr->next) {
        len += pr->str.length + nxt_length(".");
    }

    buf = nxt_mem_cache_zalloc(vm->mem_cache_pool, len);
    if (buf == NULL) {
        return NXT_ERROR;
    }

    p = buf;

    for (pr = head; pr != NULL; pr = pr->next) {
        p += snprintf(p, buf + len - p, "%.*s.", (int) pr->str.length,
                      pr->str.start);
    }

    name->start = (u_char *) buf;
    name->length = len;

    return NXT_OK;
}


nxt_int_t
njs_external_match_native_function(njs_vm_t *vm, njs_function_native_t func,
    nxt_str_t *name)
{
    nxt_int_t          ret;
    njs_extern_t       *ext;
    njs_extern_part_t  part;
    nxt_lvlhsh_each_t  lhe;

    nxt_lvlhsh_each_init(&lhe, &njs_extern_hash_proto);

    for ( ;; ) {
        ext = nxt_lvlhsh_each(&vm->external_prototypes_hash, &lhe);
        if (ext == NULL) {
            break;
        }

        part.next = NULL;
        part.str = ext->name;

        ret = njs_external_match(vm, func, ext, name, &part, &part);
        if (ret != NXT_DECLINED) {
            return ret;
        }
    }

    return NXT_DECLINED;
}
