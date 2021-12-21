
/*
 * Copyright (C) Dmitry Volyntsev
 * Copyright (C) NGINX, Inc.
 */

#include <njs_main.h>

#include "njs_externals_test.h"


typedef struct {
    njs_lvlhsh_t          hash;

    uint32_t              a;
    uint32_t              d;
    njs_str_t             uri;

    njs_opaque_value_t    value;
} njs_unit_test_req_t;


typedef struct {
    njs_value_t           name;
    njs_value_t           value;
} njs_unit_test_prop_t;


static njs_int_t    njs_external_r_proto_id;


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
lvlhsh_unit_test_add(njs_mp_t *pool, njs_unit_test_req_t *r,
    njs_unit_test_prop_t *prop)
{
    njs_lvlhsh_query_t  lhq;

    njs_string_get(&prop->name, &lhq.key);
    lhq.key_hash = njs_djb_hash(lhq.key.start, lhq.key.length);

    lhq.replace = 1;
    lhq.value = (void *) prop;
    lhq.proto = &lvlhsh_proto;
    lhq.pool = pool;

    switch (njs_lvlhsh_insert(&r->hash, &lhq)) {

    case NJS_OK:
        return NJS_OK;

    case NJS_DECLINED:
    default:
        return NJS_ERROR;
    }
}


static njs_int_t
njs_unit_test_r_uri(njs_vm_t *vm, njs_object_prop_t *prop,
    njs_value_t *value, njs_value_t *setval, njs_value_t *retval)
{
    char       *p;
    njs_str_t  *field;

    p = njs_vm_external(vm, njs_external_r_proto_id, value);
    if (p == NULL) {
        njs_value_undefined_set(retval);
        return NJS_DECLINED;
    }

    field = (njs_str_t *) (p + njs_vm_prop_magic32(prop));

    if (setval != NULL) {
        return njs_vm_value_to_bytes(vm, field, setval);
    }

    return njs_vm_value_string_set(vm, retval, field->start, field->length);
}


static njs_int_t
njs_unit_test_r_a(njs_vm_t *vm, njs_object_prop_t *prop,
    njs_value_t *value, njs_value_t *unused, njs_value_t *retval)
{
    u_char               *p;
    njs_unit_test_req_t  *r;
    u_char               buf[16];

    r = njs_vm_external(vm, njs_external_r_proto_id, value);
    if (r == NULL) {
        njs_value_undefined_set(retval);
        return NJS_DECLINED;
    }

    p = njs_sprintf(buf, buf + njs_length(buf), "%uD", r->a);

    return njs_vm_value_string_set(vm, retval, buf, p - buf);
}


static njs_int_t
njs_unit_test_r_b(njs_vm_t *vm, njs_object_prop_t *prop,
    njs_value_t *value, njs_value_t *unused, njs_value_t *retval)
{
    njs_value_number_set(retval, njs_vm_prop_magic32(prop));

    return NJS_OK;
}


static njs_int_t
njs_unit_test_r_d(njs_vm_t *vm, njs_object_prop_t *prop,
    njs_value_t *value, njs_value_t *unused, njs_value_t *retval)
{
    njs_unit_test_req_t  *r;

    r = njs_vm_external(vm, njs_external_r_proto_id, value);
    if (r == NULL) {
        njs_value_undefined_set(retval);
        return NJS_DECLINED;
    }

    njs_value_number_set(retval, r->d);

    return NJS_OK;
}


static njs_int_t
njs_unit_test_r_host(njs_vm_t *vm, njs_object_prop_t *prop,
    njs_value_t *value, njs_value_t *setval, njs_value_t *retval)
{
    return njs_vm_value_string_set(vm, retval, (u_char *) "АБВГДЕЁЖЗИЙ", 22);
}


static njs_int_t
njs_unit_test_r_buffer(njs_vm_t *vm, njs_object_prop_t *prop,
    njs_value_t *value, njs_value_t *setval, njs_value_t *retval)
{
    return njs_vm_value_buffer_set(vm, retval, (u_char *) "АБВГДЕЁЖЗИЙ", 22);
}


static njs_int_t
njs_unit_test_r_vars(njs_vm_t *vm, njs_object_prop_t *self,
    njs_value_t *value, njs_value_t *setval, njs_value_t *retval)
{
    njs_int_t             ret;
    njs_value_t           name;
    njs_lvlhsh_query_t    lhq;
    njs_unit_test_req_t   *r;
    njs_unit_test_prop_t  *prop;

    r = njs_vm_external(vm, njs_external_r_proto_id, value);
    if (r == NULL) {
        njs_value_undefined_set(retval);
        return NJS_DECLINED;
    }

    ret = njs_vm_prop_name(vm, self, &lhq.key);
    if (ret != NJS_OK) {
        if (setval == NULL && retval != NULL) {
            /* Get. */
            njs_value_undefined_set(retval);
            return NJS_DECLINED;
        }

        return NJS_ERROR;
    }

    if (setval != NULL || retval == NULL) {
        /* Set or Delete. */
        if (lhq.key.length == 5 && memcmp(lhq.key.start, "error", 5) == 0) {
            njs_vm_error(vm, "cannot %s \"error\" prop",
                         retval != NULL ? "set" : "delete");
            return NJS_ERROR;
        }
    }

    if (setval != NULL) {
        /* Set. */
        njs_vm_value_string_set(vm, &name, lhq.key.start, lhq.key.length);
        prop = lvlhsh_unit_test_alloc(vm->mem_pool, &name, setval);
        if (prop == NULL) {
            njs_memory_error(vm);
            return NJS_ERROR;
        }

        ret = lvlhsh_unit_test_add(vm->mem_pool, r, prop);
        if (ret != NJS_OK) {
            njs_vm_error(vm, "lvlhsh_unit_test_add() failed");
            return NJS_ERROR;
        }

        return NJS_OK;
    }

    /* Get or Delete. */

    lhq.key_hash = njs_djb_hash(lhq.key.start, lhq.key.length);
    lhq.proto = &lvlhsh_proto;

    ret = njs_lvlhsh_find(&r->hash, &lhq);

    prop = lhq.value;

    if (ret == NJS_OK) {
        if (retval == NULL) {
            njs_set_invalid(&prop->value);
            return NJS_OK;
        }

        if (njs_is_valid(&prop->value)) {
            *retval = prop->value;
            return NJS_OK;
        }
    }

    if (retval != NULL) {
        njs_value_undefined_set(retval);
    }

    return NJS_DECLINED;
}


static njs_int_t
njs_unit_test_r_header(njs_vm_t *vm, njs_object_prop_t *prop,
    njs_value_t *value, njs_value_t *unused, njs_value_t *retval)
{
    u_char     *p;
    uint32_t   size;
    njs_int_t  ret;
    njs_str_t  h;

    ret = njs_vm_prop_name(vm, prop, &h);
    if (ret == NJS_OK) {
        size = 7 + h.length;

        p = njs_vm_value_string_alloc(vm, retval, size);
        if (p == NULL) {
            return NJS_ERROR;
        }

        p = njs_cpymem(p, h.start, h.length);
        *p++ = '|';
        memcpy(p, "АБВ", njs_length("АБВ"));
        return NJS_OK;
    }

    njs_value_undefined_set(retval);

    return NJS_DECLINED;
}


static njs_int_t
njs_unit_test_r_header_keys(njs_vm_t *vm, njs_value_t *value, njs_value_t *keys)
{
    njs_int_t    ret, i;
    njs_value_t  *push;
    u_char       k[2];

    ret = njs_vm_array_alloc(vm, keys, 4);
    if (ret != NJS_OK) {
        return NJS_ERROR;
    }

    k[0] = '0';
    k[1] = '1';

    for (i = 0; i < 3; i++) {
        push = njs_vm_array_push(vm, keys);
        if (push == NULL) {
            return NJS_ERROR;
        }

        (void) njs_vm_value_string_set(vm, push, k, 2);

        k[1]++;
    }

    return NJS_OK;
}


static njs_int_t
njs_unit_test_r_method(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t unused)
{
    njs_int_t            ret;
    njs_str_t            s;
    njs_unit_test_req_t  *r;

    r = njs_vm_external(vm, njs_external_r_proto_id, njs_argument(args, 0));
    if (r == NULL) {
        njs_type_error(vm, "\"this\" is not an external");
        return NJS_ERROR;
    }

    ret = njs_vm_value_to_bytes(vm, &s, njs_arg(args, nargs, 1));
    if (ret == NJS_OK && s.length == 3 && memcmp(s.start, "YES", 3) == 0) {
        return njs_vm_value_string_set(vm, njs_vm_retval(vm), r->uri.start,
                                       r->uri.length);
    }

    njs_set_undefined(&vm->retval);

    return NJS_OK;
}


static njs_int_t
njs_unit_test_promise_trampoline(njs_vm_t *vm, njs_value_t *args,
    njs_uint_t nargs, njs_index_t unused)
{
    njs_function_t  *callback;

    callback = njs_value_function(njs_argument(args, 1));

    if (callback != NULL) {
        return njs_vm_call(vm, callback, njs_argument(args, 2), 1);
    }

    return NJS_OK;
}


static njs_int_t
njs_unit_test_r_subrequest(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t unused)
{
    njs_int_t            ret;
    njs_value_t          retval, *argument, *select;
    njs_vm_event_t       vm_event;
    njs_function_t       *callback;
    njs_external_ev_t    *ev;
    njs_external_env_t   *env;
    njs_unit_test_req_t  *r;

    r = njs_vm_external(vm, njs_external_r_proto_id, njs_argument(args, 0));
    if (r == NULL) {
        njs_type_error(vm, "\"this\" is not an external");
        return NJS_ERROR;
    }

    ev = njs_mp_alloc(vm->mem_pool, sizeof(njs_external_ev_t));
    if (ev == NULL) {
        njs_memory_error(vm);
        return NJS_ERROR;
    }

    ret = njs_vm_promise_create(vm, &retval, &ev->callbacks[0]);
    if (ret != NJS_OK) {
        return NJS_ERROR;
    }

    callback = njs_vm_function_alloc(vm, njs_unit_test_promise_trampoline);
    if (callback == NULL) {
        return NJS_ERROR;
    }

    vm_event = njs_vm_add_event(vm, callback, 1, NULL, NULL);
    if (vm_event == NULL) {
        njs_internal_error(vm, "njs_vm_add_event() failed");
        return NJS_ERROR;
    }

    argument = njs_arg(args, nargs, 1);
    select = njs_arg(args, nargs, 2);

    ev->vm_event = vm_event;
    ev->data = r;
    ev->nargs = 2;
    njs_value_assign(&ev->args[0], &ev->callbacks[!!njs_bool(select)]);
    njs_value_assign(&ev->args[1], argument);

    env = vm->external;

    njs_queue_insert_tail(&env->events, &ev->link);

    njs_vm_retval_set(vm, njs_value_arg(&retval));

    return NJS_OK;
}


static njs_int_t
njs_unit_test_r_retval(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t unused)
{
    njs_external_env_t  *env;

    env = vm->external;

    njs_value_assign(&env->retval, njs_arg(args, nargs, 1));

    njs_set_undefined(&vm->retval);

    return NJS_OK;
}


static njs_int_t
njs_unit_test_r_create(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t unused)
{
    njs_int_t            ret;
    njs_unit_test_req_t  *r, *sr;

    r = njs_vm_external(vm, njs_external_r_proto_id, njs_argument(args, 0));
    if (r == NULL) {
        njs_type_error(vm, "\"this\" is not an external");
        return NJS_ERROR;
    }

    sr = njs_mp_zalloc(vm->mem_pool, sizeof(njs_unit_test_req_t));
    if (sr == NULL) {
        goto memory_error;
    }

    if (njs_vm_value_to_bytes(vm, &sr->uri, njs_arg(args, nargs, 1))
        != NJS_OK)
    {
        return NJS_ERROR;
    }

    ret = njs_vm_external_create(vm, &vm->retval, njs_external_r_proto_id,
                                 sr, 0);
    if (ret != NJS_OK) {
        return NJS_ERROR;
    }

    return NJS_OK;

memory_error:

    njs_memory_error(vm);

    return NJS_ERROR;
}


static njs_int_t
njs_unit_test_r_bind(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t unused)
{
    njs_str_t            name;
    njs_unit_test_req_t  *r;

    r = njs_vm_external(vm, njs_external_r_proto_id, njs_argument(args, 0));
    if (r == NULL) {
        njs_type_error(vm, "\"this\" is not an external");
        return NJS_ERROR;
    }

    if (njs_vm_value_to_bytes(vm, &name, njs_arg(args, nargs, 1)) != NJS_OK) {
        return NJS_ERROR;
    }

    return njs_vm_bind(vm, &name, njs_arg(args, nargs, 2), 0);
}


static njs_external_t  njs_unit_test_r_c[] = {

    {
        .flags = NJS_EXTERN_PROPERTY,
        .name.string = njs_str("d"),
        .enumerable = 1,
        .u.property = {
            .handler = njs_unit_test_r_d,
        }
    },

};


static njs_external_t  njs_unit_test_r_props[] = {

    {
        .flags = NJS_EXTERN_PROPERTY,
        .name.string = njs_str("a"),
        .enumerable = 1,
        .u.property = {
            .handler = njs_unit_test_r_a,
        }
    },

    {
        .flags = NJS_EXTERN_PROPERTY,
        .name.string = njs_str("b"),
        .enumerable = 1,
        .u.property = {
            .handler = njs_unit_test_r_b,
            .magic32 = 42,
        }
    },

    {
        .flags = NJS_EXTERN_OBJECT,
        .name.string = njs_str("c"),
        .enumerable = 1,
        .u.object = {
            .properties = njs_unit_test_r_c,
            .nproperties = njs_nitems(njs_unit_test_r_c),
        }
    },

};


static njs_external_t  njs_unit_test_r_header_props[] = {

    {
        .flags = NJS_EXTERN_PROPERTY | NJS_EXTERN_SYMBOL,
        .name.symbol = NJS_SYMBOL_TO_STRING_TAG,
        .u.property = {
            .value = "Header",
        }
    },

};


static njs_external_t  njs_unit_test_r_external[] = {

    {
        .flags = NJS_EXTERN_PROPERTY | NJS_EXTERN_SYMBOL,
        .name.symbol = NJS_SYMBOL_TO_STRING_TAG,
        .u.property = {
            .value = "External",
        }
    },

    {
        .flags = NJS_EXTERN_METHOD,
        .name.string = njs_str("bind"),
        .writable = 1,
        .configurable = 1,
        .enumerable = 1,
        .u.method = {
            .native = njs_unit_test_r_bind,
        }
    },

    {
        .flags = NJS_EXTERN_OBJECT,
        .name.string = njs_str("consts"),
        .writable = 1,
        .configurable = 1,
        .enumerable = 1,
        .u.object = {
            .enumerable = 1,
            .prop_handler = njs_unit_test_r_vars,
        }
    },

    {
        .flags = NJS_EXTERN_METHOD,
        .name.string = njs_str("create"),
        .writable = 1,
        .configurable = 1,
        .enumerable = 1,
        .u.method = {
            .native = njs_unit_test_r_create,
        }
    },

    {
        .flags = NJS_EXTERN_OBJECT,
        .name.string = njs_str("header"),
        .writable = 1,
        .configurable = 1,
        .enumerable = 1,
        .u.object = {
            .properties = njs_unit_test_r_header_props,
            .nproperties = njs_nitems(njs_unit_test_r_header_props),
            .enumerable = 1,
            .prop_handler = njs_unit_test_r_header,
            .keys = njs_unit_test_r_header_keys,
        }
    },

    {
        .flags = NJS_EXTERN_PROPERTY,
        .name.string = njs_str("host"),
        .enumerable = 1,
        .u.property = {
            .handler = njs_unit_test_r_host,
        }
    },

    {
        .flags = NJS_EXTERN_PROPERTY,
        .name.string = njs_str("buffer"),
        .enumerable = 1,
        .u.property = {
            .handler = njs_unit_test_r_buffer,
        }
    },

    {
        .flags = NJS_EXTERN_METHOD,
        .name.string = njs_str("method"),
        .writable = 1,
        .configurable = 1,
        .enumerable = 1,
        .u.method = {
            .native = njs_unit_test_r_method,
        }
    },

    {
        .flags = NJS_EXTERN_METHOD,
        .name.string = njs_str("subrequest"),
        .writable = 1,
        .configurable = 1,
        .enumerable = 1,
        .u.method = {
            .native = njs_unit_test_r_subrequest,
        }
    },

    {
        .flags = NJS_EXTERN_METHOD,
        .name.string = njs_str("retval"),
        .writable = 1,
        .configurable = 1,
        .enumerable = 1,
        .u.method = {
            .native = njs_unit_test_r_retval,
        }
    },

    {
        .flags = NJS_EXTERN_OBJECT,
        .name.string = njs_str("props"),
        .enumerable = 1,
        .writable = 1,
        .u.object = {
            .enumerable = 1,
            .properties = njs_unit_test_r_props,
            .nproperties = njs_nitems(njs_unit_test_r_props),
        }
    },

    {
        .flags = NJS_EXTERN_PROPERTY,
        .name.string = njs_str("uri"),
        .writable = 1,
        .enumerable = 1,
        .u.property = {
            .handler = njs_unit_test_r_uri,
            .magic32 = offsetof(njs_unit_test_req_t, uri),
        }
    },

    {
        .flags = NJS_EXTERN_OBJECT,
        .name.string = njs_str("vars"),
        .enumerable = 1,
        .u.object = {
            .writable = 1,
            .configurable = 1,
            .enumerable = 1,
            .prop_handler = njs_unit_test_r_vars,
        }
    },

};


typedef struct {
    njs_str_t             name;
    njs_unit_test_req_t   request;
    njs_unit_test_prop_t  props[2];
} njs_unit_test_req_init_t;


static njs_unit_test_req_init_t njs_test_requests[] = {

    { njs_str("$shared"),
        {
            .uri = njs_str("shared"),
            .a = 11,
            .d = 13,
        },
        {
            { njs_string("r"), njs_string("rval") },
            { njs_string("r2"), njs_string("r2val") },
        }
    },

    { njs_str("$r"),
        {
            .uri = njs_str("АБВ"),
            .a = 1,
            .d = 1024,
        },
        {
            { njs_string("p"), njs_string("pval") },
            { njs_string("p2"), njs_string("p2val") },
        }
    },

    { njs_str("$r2"),
        {
            .uri = njs_str("αβγ"),
            .a = 2,
            .d = 1025,
        },
        {
            { njs_string("q"), njs_string("qval") },
            { njs_string("q2"), njs_string("q2val") },
        }
    },

    { njs_str("$r3"),
        {
            .uri = njs_str("abc"),
            .a = 3,
            .d = 1026,
        },
        {
            { njs_string("k"), njs_string("kval") },
            { njs_string("k2"), njs_string("k2val") },
        }
    },
};


static njs_int_t
njs_externals_init_internal(njs_vm_t *vm, njs_unit_test_req_init_t *init,
    njs_uint_t n, njs_bool_t shared)
{
    njs_int_t             ret;
    njs_uint_t            i, j;
    njs_unit_test_req_t   *requests;
    njs_unit_test_prop_t  *prop;

    if (shared) {
        njs_external_r_proto_id = njs_vm_external_prototype(vm,
                                         njs_unit_test_r_external,
                                         njs_nitems(njs_unit_test_r_external));
        if (njs_slow_path(njs_external_r_proto_id < 0)) {
            njs_printf("njs_vm_external_prototype() failed\n");
            return NJS_ERROR;
        }
    }

    requests = njs_mp_zalloc(vm->mem_pool, n * sizeof(njs_unit_test_req_t));
    if (njs_slow_path(requests == NULL)) {
        return NJS_ERROR;
    }

    for (i = 0; i < n; i++) {

        requests[i] = init[i].request;

        ret = njs_vm_external_create(vm, njs_value_arg(&requests[i].value),
                                     njs_external_r_proto_id, &requests[i],
                                     shared);
        if (njs_slow_path(ret != NJS_OK)) {
            njs_printf("njs_vm_external_create() failed\n");
            return NJS_ERROR;
        }

        ret = njs_vm_bind(vm, &init[i].name, njs_value_arg(&requests[i].value),
                          shared);
        if (njs_slow_path(ret != NJS_OK)) {
            njs_printf("njs_vm_bind() failed\n");
            return NJS_ERROR;
        }

        for (j = 0; j < njs_nitems(init[i].props); j++) {
            prop = lvlhsh_unit_test_alloc(vm->mem_pool, &init[i].props[j].name,
                                          &init[i].props[j].value);

            if (njs_slow_path(prop == NULL)) {
                njs_printf("lvlhsh_unit_test_alloc() failed\n");
                return NJS_ERROR;
            }

            ret = lvlhsh_unit_test_add(vm->mem_pool, &requests[i], prop);
            if (njs_slow_path(ret != NJS_OK)) {
                njs_printf("lvlhsh_unit_test_add() failed\n");
                return NJS_ERROR;
            }
        }
    }

    return NJS_OK;
}


njs_int_t
njs_externals_shared_init(njs_vm_t *vm)
{
    return njs_externals_init_internal(vm, njs_test_requests, 1, 1);
}


njs_int_t
njs_externals_init(njs_vm_t *vm)
{
    return njs_externals_init_internal(vm, &njs_test_requests[1], 3, 0);
}


njs_int_t
njs_external_env_init(njs_external_env_t *env)
{
    if (env != NULL) {
        njs_value_invalid_set(&env->retval);
        njs_queue_init(&env->events);
    }

    return NJS_OK;
}


njs_int_t
njs_external_process_events(njs_vm_t *vm, njs_external_env_t *env)
{
    njs_queue_t        *events;
    njs_queue_link_t   *link;
    njs_external_ev_t  *ev;

    events = &env->events;

    for ( ;; ) {
        link = njs_queue_first(events);

        if (link == njs_queue_tail(events)) {
            break;
        }

        ev = njs_queue_link_data(link, njs_external_ev_t, link);

        njs_queue_remove(&ev->link);
        ev->link.prev = NULL;
        ev->link.next = NULL;

        njs_vm_post_event(vm, ev->vm_event, &ev->args[0], ev->nargs);
    }

    return NJS_OK;
}


njs_int_t
njs_external_call(njs_vm_t *vm, const njs_str_t *fname, njs_value_t *args,
    njs_uint_t nargs)
{
    njs_int_t       ret;
    njs_function_t  *func;

    func = njs_vm_function(vm, fname);
    if (func == NULL) {
        njs_stderror("njs_external_call(): function \"%V\" not found\n", fname);
        return NJS_ERROR;
    }

    ret = njs_vm_call(vm, func, args, nargs);
    if (ret == NJS_ERROR) {
        return NJS_ERROR;
    }

    return njs_vm_run(vm);
}
