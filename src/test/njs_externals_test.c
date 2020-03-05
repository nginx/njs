
/*
 * Copyright (C) Dmitry Volyntsev
 * Copyright (C) NGINX, Inc.
 */

#include <njs_main.h>

#include "njs_externals_test.h"


typedef struct {
    njs_lvlhsh_t          hash;
    const njs_extern_t    *proto;
    njs_mp_t              *pool;

    uint32_t              a;
    njs_str_t             uri;

    njs_opaque_value_t    value;
} njs_unit_test_req_t;


typedef struct {
    njs_value_t           name;
    njs_value_t           value;
} njs_unit_test_prop_t;


static njs_int_t
lvlhsh_unit_test_key_test(njs_lvlhsh_query_t *lhq, void *data)
{
    njs_str_t             name;
    njs_unit_test_prop_t  *prop;

    prop = data;
    njs_string_get(&prop->name, &name);

    if (name.length != lhq->key.length) {
        return NJS_DECLINED;
    }

    if (memcmp(name.start, lhq->key.start, lhq->key.length) == 0) {
        return NJS_OK;
    }

    return NJS_DECLINED;
}


static void *
lvlhsh_unit_test_pool_alloc(void *pool, size_t size)
{
    return njs_mp_align(pool, size, size);
}


static void
lvlhsh_unit_test_pool_free(void *pool, void *p, size_t size)
{
    njs_mp_free(pool, p);
}


static const njs_lvlhsh_proto_t  lvlhsh_proto  njs_aligned(64) = {
    NJS_LVLHSH_LARGE_SLAB,
    lvlhsh_unit_test_key_test,
    lvlhsh_unit_test_pool_alloc,
    lvlhsh_unit_test_pool_free,
};


static njs_unit_test_prop_t *
lvlhsh_unit_test_alloc(njs_mp_t *pool, const njs_value_t *name,
    const njs_value_t *value)
{
    njs_unit_test_prop_t *prop;

    prop = njs_mp_alloc(pool, sizeof(njs_unit_test_prop_t));
    if (prop == NULL) {
        return NULL;
    }

    prop->name = *name;
    prop->value = *value;

    return prop;
}


static njs_int_t
lvlhsh_unit_test_add(njs_unit_test_req_t *r, njs_unit_test_prop_t *prop)
{
    njs_lvlhsh_query_t  lhq;

    njs_string_get(&prop->name, &lhq.key);
    lhq.key_hash = njs_djb_hash(lhq.key.start, lhq.key.length);

    lhq.replace = 1;
    lhq.value = (void *) prop;
    lhq.proto = &lvlhsh_proto;
    lhq.pool = r->pool;

    switch (njs_lvlhsh_insert(&r->hash, &lhq)) {

    case NJS_OK:
        return NJS_OK;

    case NJS_DECLINED:
    default:
        return NJS_ERROR;
    }
}


static njs_int_t
njs_unit_test_r_get_uri_external(njs_vm_t *vm, njs_value_t *value, void *obj,
    uintptr_t data)
{
    char       *p;
    njs_str_t  *field;

    p = obj;
    field = (njs_str_t *) (p + data);

    return njs_vm_value_string_set(vm, value, field->start, field->length);
}


static njs_int_t
njs_unit_test_r_set_uri_external(njs_vm_t *vm, void *obj, uintptr_t data,
    njs_str_t *value)
{
    char       *p;
    njs_str_t  *field;

    p = obj;
    field = (njs_str_t *) (p + data);

    *field = *value;

    return NJS_OK;
}


static njs_int_t
njs_unit_test_r_get_a_external(njs_vm_t *vm, njs_value_t *value, void *obj,
    uintptr_t data)
{
    u_char               *p;
    njs_unit_test_req_t  *r;
    u_char               buf[16];

    r = (njs_unit_test_req_t *) obj;

    p = njs_sprintf(buf, buf + njs_length(buf), "%uD", r->a);

    return njs_vm_value_string_set(vm, value, buf, p - buf);
}


static njs_int_t
njs_unit_test_r_get_b_external(njs_vm_t *vm, njs_value_t *value, void *obj,
    uintptr_t data)
{
    njs_value_number_set(value, data);

    return NJS_OK;
}


static njs_int_t
njs_unit_test_host_external(njs_vm_t *vm, njs_value_t *value, void *obj,
    uintptr_t data)
{
    return njs_vm_value_string_set(vm, value, (u_char *) "АБВГДЕЁЖЗИЙ", 22);
}


static njs_int_t
njs_unit_test_r_get_vars(njs_vm_t *vm, njs_value_t *value, void *obj,
    uintptr_t data)
{
    njs_int_t             ret;
    njs_str_t             *key;
    njs_lvlhsh_query_t    lhq;
    njs_unit_test_req_t   *r;
    njs_unit_test_prop_t  *prop;

    r = (njs_unit_test_req_t *) obj;
    key = (njs_str_t *) data;

    lhq.key = *key;
    lhq.key_hash = njs_djb_hash(key->start, key->length);
    lhq.proto = &lvlhsh_proto;

    ret = njs_lvlhsh_find(&r->hash, &lhq);

    prop = lhq.value;

    if (ret == NJS_OK && njs_is_valid(&prop->value)) {
        *value = prop->value;
        return NJS_OK;
    }

    njs_value_undefined_set(value);

    return NJS_OK;
}


static njs_int_t
njs_unit_test_r_set_vars(njs_vm_t *vm, void *obj, uintptr_t data,
    njs_str_t *value)
{
    njs_int_t             ret;
    njs_str_t             *key;
    njs_value_t           name, val;
    njs_unit_test_req_t   *r;
    njs_unit_test_prop_t  *prop;

    r = (njs_unit_test_req_t *) obj;
    key = (njs_str_t *) data;

    if (key->length == 5 && memcmp(key->start, "error", 5) == 0) {
        njs_vm_error(vm, "cannot set \"error\" prop");
        return NJS_ERROR;
    }

    njs_vm_value_string_set(vm, &name, key->start, key->length);
    njs_vm_value_string_set(vm, &val, value->start, value->length);

    prop = lvlhsh_unit_test_alloc(vm->mem_pool, &name, &val);
    if (prop == NULL) {
        njs_memory_error(vm);
        return NJS_ERROR;
    }

    ret = lvlhsh_unit_test_add(r, prop);
    if (ret != NJS_OK) {
        njs_vm_error(vm, "lvlhsh_unit_test_add() failed");
        return NJS_ERROR;
    }

    return NJS_OK;
}


static njs_int_t
njs_unit_test_r_del_vars(njs_vm_t *vm, void *obj, uintptr_t data,
    njs_bool_t delete)
{
    njs_int_t             ret;
    njs_str_t             *key;
    njs_lvlhsh_query_t    lhq;
    njs_unit_test_req_t   *r;
    njs_unit_test_prop_t  *prop;

    r = (njs_unit_test_req_t *) obj;
    key = (njs_str_t *) data;

    if (key->length == 5 && memcmp(key->start, "error", 5) == 0) {
        njs_vm_error(vm, "cannot delete \"error\" prop");
        return NJS_ERROR;
    }

    lhq.key = *key;
    lhq.key_hash = njs_djb_hash(key->start, key->length);
    lhq.proto = &lvlhsh_proto;

    ret = njs_lvlhsh_find(&r->hash, &lhq);

    prop = lhq.value;

    if (ret == NJS_OK) {
        njs_set_invalid(&prop->value);
    }

    return NJS_OK;
}


static njs_int_t
njs_unit_test_header_external(njs_vm_t *vm, njs_value_t *value, void *obj,
    uintptr_t data)
{
    u_char     *p;
    uint32_t   size;
    njs_str_t  *h;

    h = (njs_str_t *) data;

    size = 7 + h->length;

    p = njs_vm_value_string_alloc(vm, value, size);
    if (p == NULL) {
        return NJS_ERROR;
    }

    p = njs_cpymem(p, h->start, h->length);
    *p++ = '|';
    memcpy(p, "АБВ", 6);

    return NJS_OK;
}


static njs_int_t
njs_unit_test_header_keys_external(njs_vm_t *vm, void *obj, njs_value_t *keys)
{
    njs_int_t    rc, i;
    njs_value_t  *value;
    u_char       k[2];

    rc = njs_vm_array_alloc(vm, keys, 4);
    if (rc != NJS_OK) {
        return NJS_ERROR;
    }

    k[0] = '0';
    k[1] = '1';

    for (i = 0; i < 3; i++) {
        value = njs_vm_array_push(vm, keys);
        if (value == NULL) {
            return NJS_ERROR;
        }

        (void) njs_vm_value_string_set(vm, value, k, 2);

        k[1]++;
    }

    return NJS_OK;
}


static njs_int_t
njs_unit_test_method_external(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t unused)
{
    njs_int_t            ret;
    njs_str_t            s;
    njs_unit_test_req_t  *r;

    r = njs_vm_external(vm, njs_arg(args, nargs, 0));
    if (r == NULL) {
        return NJS_ERROR;
    }

    ret = njs_vm_value_to_string(vm, &s, njs_arg(args, nargs, 1));
    if (ret == NJS_OK && s.length == 3 && memcmp(s.start, "YES", 3) == 0) {
        return njs_vm_value_string_set(vm, njs_vm_retval(vm), r->uri.start,
                                       r->uri.length);
    }

    njs_set_undefined(&vm->retval);

    return NJS_OK;
}


static njs_int_t
njs_unit_test_create_external(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t unused)
{
    njs_int_t            ret;
    njs_str_t            uri;
    njs_value_t          *value;
    njs_unit_test_req_t  *r, *sr;

    r = njs_vm_external(vm, njs_arg(args, nargs, 0));
    if (r == NULL) {
        return NJS_ERROR;
    }

    if (njs_vm_value_to_string(vm, &uri, njs_arg(args, nargs, 1)) != NJS_OK) {
        return NJS_ERROR;
    }

    value = njs_mp_zalloc(r->pool, sizeof(njs_opaque_value_t));
    if (value == NULL) {
        goto memory_error;
    }

    sr = njs_mp_zalloc(r->pool, sizeof(njs_unit_test_req_t));
    if (sr == NULL) {
        goto memory_error;
    }

    sr->uri = uri;
    sr->pool = r->pool;
    sr->proto = r->proto;

    ret = njs_vm_external_create(vm, value, sr->proto, sr);
    if (ret != NJS_OK) {
        return NJS_ERROR;
    }

    njs_vm_retval_set(vm, value);

    return NJS_OK;

memory_error:

    njs_memory_error(vm);

    return NJS_ERROR;
}


static njs_int_t
njs_unit_test_bind_external(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t unused)
{
    njs_str_t            name;
    njs_unit_test_req_t  *r;

    r = njs_vm_external(vm, njs_arg(args, nargs, 0));
    if (r == NULL) {
        return NJS_ERROR;
    }

    if (njs_vm_value_to_string(vm, &name, njs_arg(args, nargs, 1)) != NJS_OK) {
        return NJS_ERROR;
    }

    return njs_vm_bind(vm, &name, njs_arg(args, nargs, 2), 0);
}


static njs_external_t  njs_unit_test_r_props[] = {

    { njs_str("a"),
      NJS_EXTERN_PROPERTY,
      NULL,
      0,
      njs_unit_test_r_get_a_external,
      NULL,
      NULL,
      NULL,
      NULL,
      0 },

    { njs_str("b"),
      NJS_EXTERN_PROPERTY,
      NULL,
      0,
      njs_unit_test_r_get_b_external,
      NULL,
      NULL,
      NULL,
      NULL,
      42 },
};


static njs_external_t  njs_unit_test_r_external[] = {

    { njs_str("uri"),
      NJS_EXTERN_PROPERTY,
      NULL,
      0,
      njs_unit_test_r_get_uri_external,
      njs_unit_test_r_set_uri_external,
      NULL,
      NULL,
      NULL,
      offsetof(njs_unit_test_req_t, uri) },

    { njs_str("host"),
      NJS_EXTERN_PROPERTY,
      NULL,
      0,
      njs_unit_test_host_external,
      NULL,
      NULL,
      NULL,
      NULL,
      0 },

    { njs_str("props"),
      NJS_EXTERN_OBJECT,
      njs_unit_test_r_props,
      njs_nitems(njs_unit_test_r_props),
      NULL,
      NULL,
      NULL,
      NULL,
      NULL,
      0 },

    { njs_str("vars"),
      NJS_EXTERN_OBJECT,
      NULL,
      0,
      njs_unit_test_r_get_vars,
      njs_unit_test_r_set_vars,
      njs_unit_test_r_del_vars,
      NULL,
      NULL,
      0 },

    { njs_str("consts"),
      NJS_EXTERN_OBJECT,
      NULL,
      0,
      njs_unit_test_r_get_vars,
      NULL,
      NULL,
      NULL,
      NULL,
      0 },

    { njs_str("header"),
      NJS_EXTERN_OBJECT,
      NULL,
      0,
      njs_unit_test_header_external,
      NULL,
      NULL,
      njs_unit_test_header_keys_external,
      NULL,
      0 },

    { njs_str("some_method"),
      NJS_EXTERN_METHOD,
      NULL,
      0,
      NULL,
      NULL,
      NULL,
      NULL,
      njs_unit_test_method_external,
      0 },

    { njs_str("create"),
      NJS_EXTERN_METHOD,
      NULL,
      0,
      NULL,
      NULL,
      NULL,
      NULL,
      njs_unit_test_create_external,
      0 },

    { njs_str("bind"),
      NJS_EXTERN_METHOD,
      NULL,
      0,
      NULL,
      NULL,
      NULL,
      NULL,
      njs_unit_test_bind_external,
      0 },

};


static njs_external_t  njs_test_external[] = {

    { njs_str("request.proto"),
      NJS_EXTERN_OBJECT,
      njs_unit_test_r_external,
      njs_nitems(njs_unit_test_r_external),
      NULL,
      NULL,
      NULL,
      NULL,
      NULL,
      0 },

};


typedef struct {
    njs_str_t             name;
    njs_unit_test_req_t   request;
    njs_unit_test_prop_t  props[2];
} njs_unit_test_req_t_init_t;


static const njs_unit_test_req_t_init_t njs_test_requests[] = {

    { njs_str("$r"),
     {
         .uri = njs_str("АБВ"),
         .a = 1
     },
     {
         { njs_string("p"), njs_string("pval") },
         { njs_string("p2"), njs_string("p2val") },
     }
    },

    { njs_str("$r2"),
     {
         .uri = njs_str("αβγ"),
         .a = 2
     },
     {
         { njs_string("q"), njs_string("qval") },
         { njs_string("q2"), njs_string("q2val") },
     }
    },

    { njs_str("$r3"),
     {
         .uri = njs_str("abc"),
         .a = 3
     },
     {
         { njs_string("k"), njs_string("kval") },
         { njs_string("k2"), njs_string("k2val") },
     }
    },
};


njs_int_t
njs_externals_init(njs_vm_t *vm)
{
    njs_int_t             ret;
    njs_uint_t            i, j;
    const njs_extern_t    *proto;
    njs_unit_test_req_t   *requests;
    njs_unit_test_prop_t  *prop;

    proto = njs_vm_external_prototype(vm, &njs_test_external[0]);
    if (njs_slow_path(proto == NULL)) {
        njs_printf("njs_vm_external_prototype() failed\n");
        return NJS_ERROR;
    }

    requests = njs_mp_zalloc(vm->mem_pool, njs_nitems(njs_test_requests)
                             * sizeof(njs_unit_test_req_t));
    if (njs_slow_path(requests == NULL)) {
        return NJS_ERROR;
    }

    for (i = 0; i < njs_nitems(njs_test_requests); i++) {

        requests[i] = njs_test_requests[i].request;
        requests[i].pool = vm->mem_pool;
        requests[i].proto = proto;

        ret = njs_vm_external_create(vm, njs_value_arg(&requests[i].value),
                                     proto, &requests[i]);
        if (njs_slow_path(ret != NJS_OK)) {
            njs_printf("njs_vm_external_create() failed\n");
            return NJS_ERROR;
        }

        ret = njs_vm_bind(vm, &njs_test_requests[i].name,
                          njs_value_arg(&requests[i].value), 1);
        if (njs_slow_path(ret != NJS_OK)) {
            njs_printf("njs_vm_bind() failed\n");
            return NJS_ERROR;
        }

        for (j = 0; j < njs_nitems(njs_test_requests[i].props); j++) {
            prop = lvlhsh_unit_test_alloc(vm->mem_pool,
                                          &njs_test_requests[i].props[j].name,
                                          &njs_test_requests[i].props[j].value);

            if (njs_slow_path(prop == NULL)) {
                njs_printf("lvlhsh_unit_test_alloc() failed\n");
                return NJS_ERROR;
            }

            ret = lvlhsh_unit_test_add(&requests[i], prop);
            if (njs_slow_path(ret != NJS_OK)) {
                njs_printf("lvlhsh_unit_test_add() failed\n");
                return NJS_ERROR;
            }
        }
    }

    return NJS_OK;
}
