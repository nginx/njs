
/*
 * Copyright (C) Dmitry Volyntsev
 * Copyright (C) NGINX, Inc.
 */

#include <njs.h>
#include <njs_flathsh.h>
#include <njs_queue.h>
#include <njs_djb_hash.h>

#include "njs_externals_test.h"


typedef struct {
    njs_flathsh_t         hash;

    uint32_t              a;
    uint32_t              d;
    njs_str_t             uri;

    njs_opaque_value_t    value;
} njs_unit_test_req_t;


typedef struct {
    njs_str_t             name;
    njs_opaque_value_t    value;
} njs_unit_test_prop_t;


static njs_int_t njs_externals_262_init(njs_vm_t *vm);
static njs_int_t njs_externals_shared_preinit(njs_vm_t *vm);
static njs_int_t njs_externals_shared_init(njs_vm_t *vm);
njs_int_t njs_array_buffer_detach(njs_vm_t *vm, njs_value_t *args,
    njs_uint_t nargs, njs_index_t unused, njs_value_t *retval);


static njs_int_t    njs_external_r_proto_id;
static njs_int_t    njs_external_null_proto_id;
static njs_int_t    njs_external_error_ctor_id;


njs_module_t  njs_unit_test_262_module = {
    .name = njs_str("$262"),
    .preinit = NULL,
    .init = njs_externals_262_init,
};


njs_module_t  njs_unit_test_external_module = {
    .name = njs_str("external"),
    .preinit = njs_externals_shared_preinit,
    .init = njs_externals_shared_init,
};


static njs_int_t
flathsh_unit_test_key_test(njs_flathsh_query_t *fhq, void *data)
{
    njs_str_t             name;
    njs_unit_test_prop_t  *prop;

    prop = *(njs_unit_test_prop_t **) data;
    name = prop->name;

    if (name.length != fhq->key.length) {
        return NJS_DECLINED;
    }

    if (memcmp(name.start, fhq->key.start, fhq->key.length) == 0) {
        return NJS_OK;
    }

    return NJS_DECLINED;
}


static void *
flathsh_unit_test_pool_alloc(void *pool, size_t size)
{
    return njs_mp_align(pool, NJS_MAX_ALIGNMENT, size);
}


static void
flathsh_unit_test_pool_free(void *pool, void *p, size_t size)
{
    njs_mp_free(pool, p);
}


static const njs_flathsh_proto_t  flathsh_proto  njs_aligned(64) = {
    flathsh_unit_test_key_test,
    flathsh_unit_test_pool_alloc,
    flathsh_unit_test_pool_free,
};


static njs_unit_test_prop_t *
flathsh_unit_test_alloc(njs_mp_t *pool, const njs_str_t *name,
    const njs_value_t *value)
{
    njs_unit_test_prop_t *prop;

    prop = njs_mp_alloc(pool, sizeof(njs_unit_test_prop_t) + name->length);
    if (prop == NULL) {
        return NULL;
    }

    prop->name.length = name->length;
    prop->name.start = (u_char *) prop + sizeof(njs_unit_test_prop_t);
    memcpy(prop->name.start, name->start, name->length);

    njs_value_assign(&prop->value, value);

    return prop;
}


static njs_int_t
flathsh_unit_test_add(njs_mp_t *pool, njs_unit_test_req_t *r,
    njs_unit_test_prop_t *prop)
{
    njs_flathsh_query_t  fhq;

    fhq.key = prop->name;
    fhq.key_hash = njs_djb_hash(fhq.key.start, fhq.key.length);

    fhq.replace = 1;
    fhq.proto = &flathsh_proto;
    fhq.pool = pool;

    switch (njs_flathsh_insert(&r->hash, &fhq)) {
    case NJS_OK:
        ((njs_flathsh_elt_t *) fhq.value)->value[0] = (void *) prop;
        return NJS_OK;

    case NJS_DECLINED:
    default:
        return NJS_ERROR;
    }
}


static njs_int_t
njs_unit_test_r_uri(njs_vm_t *vm, njs_object_prop_t *prop, uint32_t unused,
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

    return njs_vm_value_string_create(vm, retval, field->start, field->length);
}


static njs_int_t
njs_unit_test_r_a(njs_vm_t *vm, njs_object_prop_t *prop, uint32_t unused,
    njs_value_t *value, njs_value_t *unused2, njs_value_t *retval)
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

    return njs_vm_value_string_create(vm, retval, buf, p - buf);
}


static njs_int_t
njs_unit_test_r_b(njs_vm_t *vm, njs_object_prop_t *prop, uint32_t unused,
    njs_value_t *value, njs_value_t *unused2, njs_value_t *retval)
{
    njs_value_number_set(retval, njs_vm_prop_magic32(prop));

    return NJS_OK;
}


static njs_int_t
njs_unit_test_r_d(njs_vm_t *vm, njs_object_prop_t *prop, uint32_t unused,
    njs_value_t *value, njs_value_t *unused2, njs_value_t *retval)
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
njs_unit_test_r_host(njs_vm_t *vm, njs_object_prop_t *prop, uint32_t unused,
    njs_value_t *value, njs_value_t *setval, njs_value_t *retval)
{
    return njs_vm_value_string_create(vm, retval, (u_char *) "АБВГДЕЁЖЗИЙ", 22);
}


static njs_int_t
njs_unit_test_r_buffer(njs_vm_t *vm, njs_object_prop_t *prop, uint32_t unused,
    njs_value_t *value, njs_value_t *setval, njs_value_t *retval)
{
    return njs_vm_value_buffer_set(vm, retval, (u_char *) "АБВГДЕЁЖЗИЙ", 22);
}


static njs_int_t
njs_unit_test_r_vars(njs_vm_t *vm, njs_object_prop_t *self, uint32_t atom_id,
    njs_value_t *value, njs_value_t *setval, njs_value_t *retval)
{
    njs_int_t             ret;
    njs_flathsh_query_t   fhq;
    njs_unit_test_req_t   *r;
    njs_unit_test_prop_t  *prop;

    r = njs_vm_external(vm, njs_external_r_proto_id, value);
    if (r == NULL) {
        njs_value_undefined_set(retval);
        return NJS_DECLINED;
    }

    ret = njs_vm_prop_name(vm, atom_id, &fhq.key);
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
        if (fhq.key.length == 5 && memcmp(fhq.key.start, "error", 5) == 0) {
            njs_vm_error(vm, "cannot %s \"error\" prop",
                         retval != NULL ? "set" : "delete");
            return NJS_ERROR;
        }
    }

    if (setval != NULL) {
        /* Set. */
        prop = flathsh_unit_test_alloc(njs_vm_memory_pool(vm), &fhq.key, setval);
        if (prop == NULL) {
            njs_vm_memory_error(vm);
            return NJS_ERROR;
        }

        ret = flathsh_unit_test_add(njs_vm_memory_pool(vm), r, prop);
        if (ret != NJS_OK) {
            njs_vm_error(vm, "flathsh_unit_test_add() failed");
            return NJS_ERROR;
        }

        return NJS_OK;
    }

    /* Get or Delete. */

    fhq.key_hash = njs_djb_hash(fhq.key.start, fhq.key.length);
    fhq.proto = &flathsh_proto;

    ret = njs_flathsh_find(&r->hash, &fhq);

    if (ret == NJS_OK) {
        prop = ((njs_flathsh_elt_t *) fhq.value)->value[0];

        if (retval == NULL) {
            njs_value_invalid_set(njs_value_arg(&prop->value));
            return NJS_OK;
        }

        if (njs_value_is_valid(njs_value_arg(&prop->value))) {
            njs_value_assign(retval, njs_value_arg(&prop->value));
            return NJS_OK;
        }
    }

    if (retval != NULL) {
        njs_value_undefined_set(retval);
    }

    return NJS_DECLINED;
}


static njs_int_t
njs_unit_test_r_header(njs_vm_t *vm, njs_object_prop_t *prop, uint32_t atom_id,
    njs_value_t *value, njs_value_t *unused, njs_value_t *retval)
{
    njs_int_t  ret;
    njs_str_t  h;
    njs_chb_t  chain;

    ret = njs_vm_prop_name(vm, atom_id, &h);
    if (ret == NJS_OK) {
        NJS_CHB_MP_INIT(&chain, njs_vm_memory_pool(vm));

        njs_chb_append(&chain, h.start, h.length);
        njs_chb_append(&chain, (u_char *) "|АБВ", njs_length("|АБВ"));

        ret = njs_vm_value_string_create_chb(vm, retval, &chain);

        njs_chb_destroy(&chain);

        return ret;
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

        (void) njs_vm_value_string_create(vm, push, k, 2);

        k[1]++;
    }

    return NJS_OK;
}


static njs_int_t
njs_unit_test_r_method(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t unused, njs_value_t *retval)
{
    njs_int_t            ret;
    njs_str_t            s;
    njs_unit_test_req_t  *r;

    r = njs_vm_external(vm, njs_external_r_proto_id, njs_argument(args, 0));
    if (r == NULL) {
        njs_vm_type_error(vm, "\"this\" is not an external");
        return NJS_ERROR;
    }

    ret = njs_vm_value_to_bytes(vm, &s, njs_arg(args, nargs, 1));
    if (ret == NJS_OK && s.length == 3 && memcmp(s.start, "YES", 3) == 0) {
        return njs_vm_value_string_create(vm, retval, r->uri.start,
                                          r->uri.length);
    }

    njs_value_undefined_set(retval);

    return NJS_OK;
}


static njs_int_t
njs_unit_test_promise_trampoline(njs_vm_t *vm, njs_value_t *args,
    njs_uint_t nargs, njs_index_t unused, njs_value_t *retval)
{
    njs_function_t  *callback;

    callback = njs_value_function(njs_argument(args, 1));

    if (callback != NULL) {
        return njs_vm_invoke(vm, callback, njs_argument(args, 2), 1, retval);
    }

    return NJS_OK;
}


static njs_int_t
njs_unit_test_r_subrequest(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t unused, njs_value_t *retval)
{
    njs_int_t            ret;
    njs_value_t          *argument, *select;
    njs_function_t       *callback;
    njs_external_ev_t    *ev;
    njs_external_env_t   *env;
    njs_opaque_value_t   value;
    njs_unit_test_req_t  *r;

    r = njs_vm_external(vm, njs_external_r_proto_id, njs_argument(args, 0));
    if (r == NULL) {
        njs_vm_type_error(vm, "\"this\" is not an external");
        return NJS_ERROR;
    }

    ev = njs_mp_alloc(njs_vm_memory_pool(vm), sizeof(njs_external_ev_t));
    if (ev == NULL) {
        njs_vm_memory_error(vm);
        return NJS_ERROR;
    }

    ret = njs_vm_promise_create(vm, njs_value_arg(&value),
                                njs_value_arg(&ev->callbacks[0]));
    if (ret != NJS_OK) {
        return NJS_ERROR;
    }

    callback = njs_vm_function_alloc(vm, njs_unit_test_promise_trampoline, 0,
                                     0);
    if (callback == NULL) {
        return NJS_ERROR;
    }

    argument = njs_arg(args, nargs, 1);
    select = njs_arg(args, nargs, 2);

    ev->function = callback;
    ev->data = r;
    ev->nargs = 2;
    njs_value_assign(&ev->args[0], &ev->callbacks[!!njs_value_bool(select)]);
    njs_value_assign(&ev->args[1], argument);

    env = njs_vm_external_ptr(vm);

    njs_queue_insert_tail(&env->events, &ev->link);

    njs_value_assign(retval, &value);

    return NJS_OK;
}


static njs_int_t
njs_unit_test_r_retval(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t unused, njs_value_t *retval)
{
    njs_external_env_t  *env;

    env = njs_vm_external_ptr(vm);

    njs_value_assign(&env->retval, njs_arg(args, nargs, 1));

    njs_value_undefined_set(retval);

    return NJS_OK;
}


static njs_int_t
njs_unit_test_r_custom_exception(njs_vm_t *vm, njs_value_t *args,
    njs_uint_t nargs, njs_index_t unused, njs_value_t *retval)
{
    njs_vm_error3(vm, njs_external_error_ctor_id, "Oops", NULL);

    return NJS_ERROR;
}


static njs_int_t
njs_unit_test_r_create(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t unused, njs_value_t *retval)
{
    njs_int_t            ret;
    njs_unit_test_req_t  *r, *sr;

    r = njs_vm_external(vm, njs_external_r_proto_id, njs_argument(args, 0));
    if (r == NULL) {
        njs_vm_type_error(vm, "\"this\" is not an external");
        return NJS_ERROR;
    }

    sr = njs_mp_zalloc(njs_vm_memory_pool(vm), sizeof(njs_unit_test_req_t));
    if (sr == NULL) {
        goto memory_error;
    }

    if (njs_vm_value_to_bytes(vm, &sr->uri, njs_arg(args, nargs, 1))
        != NJS_OK)
    {
        return NJS_ERROR;
    }

    ret = njs_vm_external_create(vm, retval, njs_external_r_proto_id,
                                 sr, 0);
    if (ret != NJS_OK) {
        return NJS_ERROR;
    }

    return NJS_OK;

memory_error:

    njs_vm_memory_error(vm);

    return NJS_ERROR;
}


static njs_int_t
njs_unit_test_r_bind(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t unused, njs_value_t *retval)
{
    njs_str_t            name;
    njs_unit_test_req_t  *r;

    r = njs_vm_external(vm, njs_external_r_proto_id, njs_argument(args, 0));
    if (r == NULL) {
        njs_vm_type_error(vm, "\"this\" is not an external");
        return NJS_ERROR;
    }

    if (njs_vm_value_to_bytes(vm, &name, njs_arg(args, nargs, 1)) != NJS_OK) {
        return NJS_ERROR;
    }

    return njs_vm_bind(vm, &name, njs_arg(args, nargs, 2), 0);
}


static njs_int_t
njs_unit_test_null_get(njs_vm_t *vm, njs_value_t *args,
    njs_uint_t nargs, njs_index_t unused, njs_value_t *retval)
{
    double       *d;
    njs_value_t  *this;

    this = njs_argument(args, 0);

    if (!njs_value_is_external(this, njs_external_null_proto_id)) {
        njs_vm_type_error(vm, "\"this\" is not a null external");
        return NJS_ERROR;
    }

    d = njs_value_external(this);

    if (d == NULL) {
        njs_value_undefined_set(retval);

    } else {
        njs_value_number_set(retval, *d);
    }

    return NJS_OK;
}


static njs_int_t
njs_unit_test_null_set(njs_vm_t *vm, njs_value_t *args,
    njs_uint_t nargs, njs_index_t unused, njs_value_t *retval)
{
    double       *d;
    njs_value_t  *this;

    this = njs_argument(args, 0);

    if (!njs_value_is_external(this, njs_external_null_proto_id)) {
        njs_vm_type_error(vm, "\"this\" is not a null external");
        return NJS_ERROR;
    }

    d = njs_value_external(this);

    if (d == NULL) {
        d = njs_mp_alloc(njs_vm_memory_pool(vm), sizeof(double));
        if (d == NULL) {
            njs_vm_memory_error(vm);
            return NJS_ERROR;
        }
    }

    *d = njs_value_number(njs_arg(args, nargs, 1));

    njs_value_external_set(this, d);

    njs_value_undefined_set(retval);

    return NJS_OK;
}


static njs_int_t
njs_unit_test_constructor(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t unused, njs_value_t *retval)
{
    njs_unit_test_req_t  *sr;

    sr = njs_mp_zalloc(njs_vm_memory_pool(vm), sizeof(njs_unit_test_req_t));
    if (sr == NULL) {
        njs_vm_memory_error(vm);
        return NJS_ERROR;
    }

    if (njs_vm_value_to_bytes(vm, &sr->uri, njs_arg(args, nargs, 1))
        != NJS_OK)
    {
        return NJS_ERROR;
    }

    return njs_vm_external_create(vm, retval, njs_external_r_proto_id,
                                  sr, 0);
}


static njs_int_t
njs_unit_test_error_name(njs_vm_t *vm, njs_object_prop_t *prop, uint32_t unused,
    njs_value_t *value, njs_value_t *setval, njs_value_t *retval)
{
    return njs_vm_value_string_create(vm, retval, (u_char *) "ExternalError",
                                      13);
}


static njs_int_t
njs_unit_test_error_message(njs_vm_t *vm, njs_object_prop_t *prop,
    uint32_t unused, njs_value_t *value, njs_value_t *setval,
    njs_value_t *retval)
{
    return njs_vm_value_string_create(vm, retval, (u_char *) "", 0);
}


static njs_external_t  njs_unit_test_262_external[] = {

    {
        .flags = NJS_EXTERN_PROPERTY | NJS_EXTERN_SYMBOL,
        .name.symbol = NJS_SYMBOL_TO_STRING_TAG,
        .u.property = {
            .value = "$262",
        }
    },

    {
        .flags = NJS_EXTERN_METHOD,
        .name.string = njs_str("detachArrayBuffer"),
        .writable = 1,
        .configurable = 1,
        .enumerable = 1,
        .u.method = {
            .native = njs_array_buffer_detach,
        }
    },

};


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


static njs_external_t  njs_unit_test_r_header_props2[] = {

    {
        .flags = NJS_EXTERN_PROPERTY | NJS_EXTERN_SYMBOL,
        .name.symbol = NJS_SYMBOL_TO_STRING_TAG,
        .u.property = {
            .value = "Header2",
        }
    },

    {
        .flags = NJS_EXTERN_SELF,
        .u.object = {
            .enumerable = 1,
            .prop_handler = njs_unit_test_r_header,
            .keys = njs_unit_test_r_header_keys,
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
        .flags = NJS_EXTERN_OBJECT,
        .name.string = njs_str("header2"),
        .writable = 1,
        .configurable = 1,
        .u.object = {
            .properties = njs_unit_test_r_header_props2,
            .nproperties = njs_nitems(njs_unit_test_r_header_props2),
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
        .flags = NJS_EXTERN_METHOD,
        .name.string = njs_str("customException"),
        .writable = 1,
        .configurable = 1,
        .enumerable = 1,
        .u.method = {
            .native = njs_unit_test_r_custom_exception,
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

static njs_external_t  njs_unit_test_null_external[] = {

    {
        .flags = NJS_EXTERN_PROPERTY | NJS_EXTERN_SYMBOL,
        .name.symbol = NJS_SYMBOL_TO_STRING_TAG,
        .u.property = {
            .value = "Null",
        }
    },

    {
        .flags = NJS_EXTERN_METHOD,
        .name.string = njs_str("get"),
        .writable = 1,
        .configurable = 1,
        .enumerable = 1,
        .u.method = {
            .native = njs_unit_test_null_get,
        }
    },

    {
        .flags = NJS_EXTERN_METHOD,
        .name.string = njs_str("set"),
        .writable = 1,
        .configurable = 1,
        .enumerable = 1,
        .u.method = {
            .native = njs_unit_test_null_set,
        }
    },

};


static njs_external_t  njs_unit_test_ctor_props[] = {

    {
        .flags = NJS_EXTERN_PROPERTY,
        .name.string = njs_str("name"),
        .enumerable = 1,
        .u.property = {
            .handler = njs_unit_test_error_name,
        }
    },

    {
        .flags = NJS_EXTERN_PROPERTY,
        .name.string = njs_str("prototype"),
        .enumerable = 1,
        .u.property = {
            .handler = njs_object_prototype_create,
        }
    },

};


static njs_external_t  njs_unit_test_proto_props[] = {

    {
        .flags = NJS_EXTERN_PROPERTY,
        .name.string = njs_str("name"),
        .enumerable = 1,
        .u.property = {
            .handler = njs_unit_test_error_name,
        }
    },

    {
        .flags = NJS_EXTERN_PROPERTY,
        .name.string = njs_str("message"),
        .enumerable = 1,
        .u.property = {
            .handler = njs_unit_test_error_message,
        }
    },

    {
        .flags = NJS_EXTERN_PROPERTY,
        .name.string = njs_str("constructor"),
        .enumerable = 1,
        .u.property = {
            .handler = njs_object_prototype_create_constructor,
        }
    },

};


typedef struct {
    njs_str_t                  name;
    njs_str_t                  value;
} njs_unit_test_prop_init_t;


typedef struct {
    njs_str_t                  name;
    njs_unit_test_req_t        request;
    njs_unit_test_prop_init_t  props[2];
} njs_unit_test_req_init_t;


static njs_unit_test_req_init_t njs_test_requests[] = {

    { njs_str("$shared"),
        {
            .uri = njs_str("shared"),
            .a = 11,
            .d = 13,
        },
        {
            { njs_str("r"), njs_str("rval") },
            { njs_str("r2"), njs_str("r2val") },
        }
    },

    { njs_str("$r"),
        {
            .uri = njs_str("АБВ"),
            .a = 1,
            .d = 1024,
        },
        {
            { njs_str("p"), njs_str("pval") },
            { njs_str("p2"), njs_str("p2val") },
        }
    },

    { njs_str("$r2"),
        {
            .uri = njs_str("αβγ"),
            .a = 2,
            .d = 1025,
        },
        {
            { njs_str("q"), njs_str("qval") },
            { njs_str("q2"), njs_str("q2val") },
        }
    },

    { njs_str("$r3"),
        {
            .uri = njs_str("abc"),
            .a = 3,
            .d = 1026,
        },
        {
            { njs_str("k"), njs_str("kval") },
            { njs_str("k2"), njs_str("k2val") },
        }
    },
};


static njs_int_t
njs_externals_init_internal(njs_vm_t *vm, njs_unit_test_req_init_t *init,
    njs_uint_t n, njs_bool_t shared)
{
    njs_int_t             ret;
    njs_uint_t            i, j;
    njs_opaque_value_t    value;
    njs_unit_test_req_t   *requests;
    njs_unit_test_prop_t  *prop;

    requests = njs_mp_zalloc(njs_vm_memory_pool(vm),
                             n * sizeof(njs_unit_test_req_t));
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
            ret = njs_vm_value_string_create(vm, njs_value_arg(&value),
                                             init[i].props[j].value.start,
                                             init[i].props[j].value.length);
            if (njs_slow_path(ret != NJS_OK)) {
                return NJS_ERROR;
            }

            prop = flathsh_unit_test_alloc(njs_vm_memory_pool(vm),
                                           &init[i].props[j].name,
                                           njs_value_arg(&value));

            if (njs_slow_path(prop == NULL)) {
                njs_printf("flathsh_unit_test_alloc() failed\n");
                return NJS_ERROR;
            }

            ret = flathsh_unit_test_add(njs_vm_memory_pool(vm), &requests[i],
                                        prop);
            if (njs_slow_path(ret != NJS_OK)) {
                njs_printf("flathsh_unit_test_add() failed\n");
                return NJS_ERROR;
            }
        }
    }

    return NJS_OK;
}


static njs_int_t
njs_externals_262_init(njs_vm_t *vm)
{
    njs_int_t           ret, proto_id;
    njs_opaque_value_t  value;

    static const njs_str_t  dollar_262 = njs_str("$262");

    proto_id = njs_vm_external_prototype(vm, njs_unit_test_262_external,
                                       njs_nitems(njs_unit_test_262_external));
    if (njs_slow_path(proto_id < 0)) {
        njs_printf("njs_vm_external_prototype() failed\n");
        return NJS_ERROR;
    }

    ret = njs_vm_external_create(vm, njs_value_arg(&value), proto_id, NULL, 1);
    if (njs_slow_path(ret != NJS_OK)) {
        njs_printf("njs_vm_external_create() failed\n");
        return NJS_ERROR;
    }

    ret = njs_vm_bind(vm, &dollar_262, njs_value_arg(&value), 1);
    if (njs_slow_path(ret != NJS_OK)) {
        njs_printf("njs_vm_bind() failed\n");
        return NJS_ERROR;
    }

    return NJS_OK;
}


static njs_int_t
njs_externals_shared_preinit(njs_vm_t *vm)
{
    static const njs_str_t  external_error = njs_str("ExternalError");

    njs_external_r_proto_id = njs_vm_external_prototype(vm,
                                     njs_unit_test_r_external,
                                     njs_nitems(njs_unit_test_r_external));
    if (njs_slow_path(njs_external_r_proto_id < 0)) {
        njs_printf("njs_vm_external_prototype() failed\n");
        return NJS_ERROR;
    }

    njs_external_null_proto_id = njs_vm_external_prototype(vm,
                                   njs_unit_test_null_external,
                                   njs_nitems(njs_unit_test_null_external));
    if (njs_slow_path(njs_external_null_proto_id < 0)) {
        njs_printf("njs_vm_external_prototype() failed\n");
        return NJS_ERROR;
    }

    njs_external_error_ctor_id =
        njs_vm_external_constructor(vm, &external_error,
                          njs_error_constructor, njs_unit_test_ctor_props,
                          njs_nitems(njs_unit_test_ctor_props),
                          njs_unit_test_proto_props,
                          njs_nitems(njs_unit_test_proto_props));
    if (njs_slow_path(njs_external_error_ctor_id < 0)) {
        njs_printf("njs_vm_external_constructor() failed\n");
        return NJS_ERROR;
    }

    return NJS_OK;
}


static njs_int_t
njs_externals_shared_init(njs_vm_t *vm)
{
    njs_int_t           ret;
    njs_function_t      *f;
    njs_opaque_value_t  value;

    static const njs_str_t  external_ctor = njs_str("ExternalConstructor");
    static const njs_str_t  external_null = njs_str("ExternalNull");

    f = njs_vm_function_alloc(vm, njs_unit_test_constructor, 1, 1);
    if (f == NULL) {
        njs_printf("njs_vm_function_alloc() failed\n");
        return NJS_ERROR;
    }

    njs_value_function_set(njs_value_arg(&value), f);

    ret = njs_vm_bind(vm, &external_ctor, njs_value_arg(&value), 1);
    if (njs_slow_path(ret != NJS_OK)) {
        njs_printf("njs_vm_bind() failed\n");
        return NJS_ERROR;
    }

    ret = njs_vm_external_create(vm, njs_value_arg(&value),
                                 njs_external_null_proto_id, NULL, 1);
    if (njs_slow_path(ret != NJS_OK)) {
        return NJS_ERROR;
    }

    ret = njs_vm_bind(vm, &external_null, njs_value_arg(&value), 1);
    if (njs_slow_path(ret != NJS_OK)) {
        njs_printf("njs_vm_bind() failed\n");
        return NJS_ERROR;
    }

    return njs_externals_init_internal(vm, &njs_test_requests[0], 1, 1);
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
        njs_value_invalid_set(njs_value_arg(&env->retval));
        njs_queue_init(&env->events);
    }

    return NJS_OK;
}


njs_int_t
njs_external_process_events(njs_vm_t *vm, njs_external_env_t *env)
{
    njs_int_t          ret;
    njs_queue_t        *events;
    njs_queue_link_t   *link;
    njs_external_ev_t  *ev;

    if (env == NULL) {
        return NJS_OK;
    }

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

        ret = njs_vm_call(vm, ev->function, njs_value_arg(ev->args), ev->nargs);
        if (ret == NJS_ERROR) {
            return NJS_ERROR;
        }
    }

    return njs_vm_pending(vm) ? NJS_AGAIN: NJS_OK;
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

    for ( ;; ) {
        ret = njs_vm_execute_pending_job(vm);
        if (ret <= NJS_OK) {
            if (ret == NJS_ERROR) {
                return NJS_ERROR;
            }

            break;
        }
    }

    return NJS_OK;
}
