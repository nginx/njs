
/*
 * Copyright (C) Dmitry Volyntsev
 * Copyright (C) NGINX, Inc.
 */


#include <ngx_config.h>
#include <ngx_core.h>
#include "ngx_js.h"
#include "ngx_js_shared_dict.h"


typedef struct {
    ngx_rbtree_t           rbtree;
    ngx_rbtree_node_t      sentinel;
    ngx_atomic_t           rwlock;

    ngx_rbtree_t           rbtree_expire;
    ngx_rbtree_node_t      sentinel_expire;
} ngx_js_dict_sh_t;


typedef struct {
    ngx_str_node_t         sn;
    ngx_rbtree_node_t      expire;
    union {
        ngx_str_t          value;
        double             number;
    } u;
} ngx_js_dict_node_t;


struct ngx_js_dict_s {
    ngx_shm_zone_t        *shm_zone;
    ngx_js_dict_sh_t      *sh;
    ngx_slab_pool_t       *shpool;

    ngx_msec_t             timeout;
    ngx_flag_t             evict;
#define NGX_JS_DICT_TYPE_STRING  0
#define NGX_JS_DICT_TYPE_NUMBER  1
    ngx_uint_t             type;

    ngx_js_dict_t         *next;
};


static njs_int_t njs_js_ext_shared_dict_capacity(njs_vm_t *vm,
    njs_object_prop_t *prop, njs_value_t *value, njs_value_t *setval,
    njs_value_t *retval);
static njs_int_t njs_js_ext_shared_dict_clear(njs_vm_t *vm, njs_value_t *args,
    njs_uint_t nargs, njs_index_t flags, njs_value_t *retval);
static njs_int_t njs_js_ext_shared_dict_delete(njs_vm_t *vm, njs_value_t *args,
    njs_uint_t nargs, njs_index_t unused, njs_value_t *retval);
static njs_int_t njs_js_ext_shared_dict_free_space(njs_vm_t *vm,
    njs_value_t *args, njs_uint_t nargs, njs_index_t unused,
    njs_value_t *retval);
static njs_int_t njs_js_ext_shared_dict_get(njs_vm_t *vm, njs_value_t *args,
    njs_uint_t nargs, njs_index_t unused, njs_value_t *retval);
static njs_int_t njs_js_ext_shared_dict_has(njs_vm_t *vm, njs_value_t *args,
    njs_uint_t nargs, njs_index_t unused, njs_value_t *retval);
static njs_int_t njs_js_ext_shared_dict_keys(njs_vm_t *vm, njs_value_t *args,
    njs_uint_t nargs, njs_index_t unused, njs_value_t *retval);
static njs_int_t njs_js_ext_shared_dict_incr(njs_vm_t *vm, njs_value_t *args,
    njs_uint_t nargs, njs_index_t unused, njs_value_t *retval);
static njs_int_t njs_js_ext_shared_dict_items(njs_vm_t *vm, njs_value_t *args,
    njs_uint_t nargs, njs_index_t unused, njs_value_t *retval);
static njs_int_t njs_js_ext_shared_dict_name(njs_vm_t *vm,
    njs_object_prop_t *prop, njs_value_t *value, njs_value_t *setval,
    njs_value_t *retval);
static njs_int_t njs_js_ext_shared_dict_pop(njs_vm_t *vm, njs_value_t *args,
    njs_uint_t nargs, njs_index_t unused, njs_value_t *retval);
static njs_int_t njs_js_ext_shared_dict_set(njs_vm_t *vm, njs_value_t *args,
    njs_uint_t nargs, njs_index_t flags, njs_value_t *retval);
static njs_int_t njs_js_ext_shared_dict_size(njs_vm_t *vm, njs_value_t *args,
    njs_uint_t nargs, njs_index_t unused, njs_value_t *retval);
static njs_int_t njs_js_ext_shared_dict_type(njs_vm_t *vm,
    njs_object_prop_t *prop, njs_value_t *value, njs_value_t *setval,
    njs_value_t *retval);
static ngx_js_dict_node_t *ngx_js_dict_lookup(ngx_js_dict_t *dict,
    njs_str_t *key);

#define NGX_JS_DICT_FLAG_MUST_EXIST       1
#define NGX_JS_DICT_FLAG_MUST_NOT_EXIST   2

static ngx_int_t ngx_js_dict_set(njs_vm_t *vm, ngx_js_dict_t *dict,
    njs_str_t *key, njs_value_t *value, unsigned flags);
static ngx_int_t ngx_js_dict_add(ngx_js_dict_t *dict, njs_str_t *key,
    njs_value_t *value, ngx_msec_t now);
static ngx_int_t ngx_js_dict_update(ngx_js_dict_t *dict,
    ngx_js_dict_node_t *node, njs_value_t *value, ngx_msec_t now);
static ngx_int_t ngx_js_dict_get(njs_vm_t *vm, ngx_js_dict_t *dict,
    njs_str_t *key, njs_value_t *retval);
static ngx_int_t ngx_js_dict_incr(ngx_js_dict_t *dict, njs_str_t *key,
    njs_value_t *delta, njs_value_t *init, double *value);
static ngx_int_t ngx_js_dict_delete(njs_vm_t *vm, ngx_js_dict_t *dict,
    njs_str_t *key, njs_value_t *retval);
static ngx_int_t ngx_js_dict_copy_value_locked(njs_vm_t *vm,
    ngx_js_dict_t *dict, ngx_js_dict_node_t *node, njs_value_t *retval);

static void ngx_js_dict_expire(ngx_js_dict_t *dict, ngx_msec_t now);
static void ngx_js_dict_evict(ngx_js_dict_t *dict, ngx_int_t count);

static njs_int_t ngx_js_dict_shared_error_name(njs_vm_t *vm,
    njs_object_prop_t *prop, njs_value_t *value, njs_value_t *setval,
    njs_value_t *retval);

static ngx_int_t ngx_js_dict_init_zone(ngx_shm_zone_t *shm_zone, void *data);
static njs_int_t ngx_js_shared_dict_preinit(njs_vm_t *vm);
static njs_int_t ngx_js_shared_dict_init(njs_vm_t *vm);
static void ngx_js_dict_node_free(ngx_js_dict_t *dict,
    ngx_js_dict_node_t *node);


static njs_external_t  ngx_js_ext_shared_dict[] = {

    {
        .flags = NJS_EXTERN_PROPERTY | NJS_EXTERN_SYMBOL,
        .name.symbol = NJS_SYMBOL_TO_STRING_TAG,
        .u.property = {
            .value = "SharedDict",
        }
    },

    {
        .flags = NJS_EXTERN_METHOD,
        .name.string = njs_str("add"),
        .writable = 1,
        .configurable = 1,
        .enumerable = 1,
        .u.method = {
            .native = njs_js_ext_shared_dict_set,
            .magic8 = NGX_JS_DICT_FLAG_MUST_NOT_EXIST,
        }
    },

    {
        .flags = NJS_EXTERN_PROPERTY,
        .name.string = njs_str("capacity"),
        .enumerable = 1,
        .u.property = {
            .handler = njs_js_ext_shared_dict_capacity,
        }
    },

    {
        .flags = NJS_EXTERN_METHOD,
        .name.string = njs_str("clear"),
        .writable = 1,
        .configurable = 1,
        .enumerable = 1,
        .u.method = {
            .native = njs_js_ext_shared_dict_clear,
        }
    },

    {
        .flags = NJS_EXTERN_METHOD,
        .name.string = njs_str("freeSpace"),
        .writable = 1,
        .configurable = 1,
        .enumerable = 1,
        .u.method = {
            .native = njs_js_ext_shared_dict_free_space,
        }
    },

    {
        .flags = NJS_EXTERN_METHOD,
        .name.string = njs_str("delete"),
        .writable = 1,
        .configurable = 1,
        .enumerable = 1,
        .u.method = {
            .native = njs_js_ext_shared_dict_delete,
        }
    },

    {
        .flags = NJS_EXTERN_METHOD,
        .name.string = njs_str("incr"),
        .writable = 1,
        .configurable = 1,
        .enumerable = 1,
        .u.method = {
            .native = njs_js_ext_shared_dict_incr,
        }
    },

    {
        .flags = NJS_EXTERN_METHOD,
        .name.string = njs_str("items"),
        .writable = 1,
        .configurable = 1,
        .enumerable = 1,
        .u.method = {
            .native = njs_js_ext_shared_dict_items,
        }
    },

    {
        .flags = NJS_EXTERN_METHOD,
        .name.string = njs_str("get"),
        .writable = 1,
        .configurable = 1,
        .enumerable = 1,
        .u.method = {
            .native = njs_js_ext_shared_dict_get,
        }
    },

    {
        .flags = NJS_EXTERN_METHOD,
        .name.string = njs_str("has"),
        .writable = 1,
        .configurable = 1,
        .enumerable = 1,
        .u.method = {
            .native = njs_js_ext_shared_dict_has,
        }
    },

    {
        .flags = NJS_EXTERN_METHOD,
        .name.string = njs_str("keys"),
        .writable = 1,
        .configurable = 1,
        .enumerable = 1,
        .u.method = {
            .native = njs_js_ext_shared_dict_keys,
        }
    },

    {
        .flags = NJS_EXTERN_PROPERTY,
        .name.string = njs_str("name"),
        .enumerable = 1,
        .u.property = {
            .handler = njs_js_ext_shared_dict_name,
        }
    },

    {
        .flags = NJS_EXTERN_METHOD,
        .name.string = njs_str("pop"),
        .writable = 1,
        .configurable = 1,
        .enumerable = 1,
        .u.method = {
            .native = njs_js_ext_shared_dict_pop,
        }
    },

    {
        .flags = NJS_EXTERN_METHOD,
        .name.string = njs_str("replace"),
        .writable = 1,
        .configurable = 1,
        .enumerable = 1,
        .u.method = {
            .native = njs_js_ext_shared_dict_set,
            .magic8 = NGX_JS_DICT_FLAG_MUST_EXIST,
        }
    },

    {
        .flags = NJS_EXTERN_METHOD,
        .name.string = njs_str("set"),
        .writable = 1,
        .configurable = 1,
        .enumerable = 1,
        .u.method = {
            .native = njs_js_ext_shared_dict_set,
        }
    },

    {
        .flags = NJS_EXTERN_METHOD,
        .name.string = njs_str("size"),
        .writable = 1,
        .configurable = 1,
        .enumerable = 1,
        .u.method = {
            .native = njs_js_ext_shared_dict_size,
        }
    },

    {
        .flags = NJS_EXTERN_PROPERTY,
        .name.string = njs_str("type"),
        .enumerable = 1,
        .u.property = {
            .handler = njs_js_ext_shared_dict_type,
        }
    },

};


static njs_external_t  ngx_js_ext_error_ctor_props[] = {

    {
        .flags = NJS_EXTERN_PROPERTY,
        .name.string = njs_str("name"),
        .enumerable = 1,
        .u.property = {
            .handler = ngx_js_dict_shared_error_name,
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


static njs_external_t  ngx_js_ext_error_proto_props[] = {

    {
        .flags = NJS_EXTERN_PROPERTY,
        .name.string = njs_str("name"),
        .enumerable = 1,
        .u.property = {
            .handler = ngx_js_dict_shared_error_name,
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


static njs_int_t    ngx_js_shared_dict_proto_id;
static njs_int_t    ngx_js_shared_dict_error_id;


njs_module_t  ngx_js_shared_dict_module = {
    .name = njs_str("shared_dict"),
    .preinit = ngx_js_shared_dict_preinit,
    .init = ngx_js_shared_dict_init,
};


njs_int_t
njs_js_ext_global_shared_prop(njs_vm_t *vm, njs_object_prop_t *prop,
    njs_value_t *value, njs_value_t *setval, njs_value_t *retval)
{
    njs_int_t            ret;
    njs_str_t            name;
    ngx_js_dict_t       *dict;
    ngx_shm_zone_t      *shm_zone;
    ngx_js_main_conf_t  *conf;

    ret = njs_vm_prop_name(vm, prop, &name);
    if (ret != NJS_OK) {
        return NJS_ERROR;
    }

    conf = ngx_main_conf(vm);

    for (dict = conf->dicts; dict != NULL; dict = dict->next) {
        shm_zone = dict->shm_zone;

        if (shm_zone->shm.name.len == name.length
            && ngx_strncmp(shm_zone->shm.name.data, name.start,
                           name.length)
               == 0)
        {
            ret = njs_vm_external_create(vm, retval,
                                         ngx_js_shared_dict_proto_id,
                                         shm_zone, 0);
            if (ret != NJS_OK) {
                njs_vm_internal_error(vm, "sharedDict creation failed");
                return NJS_ERROR;
            }

            return NJS_OK;
        }
    }

    njs_value_null_set(retval);

    return NJS_DECLINED;
}


njs_int_t
njs_js_ext_global_shared_keys(njs_vm_t *vm, njs_value_t *unused,
    njs_value_t *keys)
{
    njs_int_t            rc;
    njs_value_t         *value;
    ngx_js_dict_t       *dict;
    ngx_shm_zone_t      *shm_zone;
    ngx_js_main_conf_t  *conf;

    conf = ngx_main_conf(vm);

    rc = njs_vm_array_alloc(vm, keys, 4);
    if (rc != NJS_OK) {
        return NJS_ERROR;
    }

    for (dict = conf->dicts; dict != NULL; dict = dict->next) {
        shm_zone = dict->shm_zone;

        value = njs_vm_array_push(vm, keys);
        if (value == NULL) {
            return NJS_ERROR;
        }

        rc = njs_vm_value_string_create(vm, value, shm_zone->shm.name.data,
                                        shm_zone->shm.name.len);
        if (rc != NJS_OK) {
            return NJS_ERROR;
        }
    }

    return NJS_OK;
}


static njs_int_t
njs_js_ext_shared_dict_capacity(njs_vm_t *vm, njs_object_prop_t *prop,
    njs_value_t *value, njs_value_t *setval, njs_value_t *retval)
{
    ngx_shm_zone_t  *shm_zone;

    shm_zone = njs_vm_external(vm, ngx_js_shared_dict_proto_id, value);
    if (shm_zone == NULL) {
        njs_value_undefined_set(retval);
        return NJS_DECLINED;
    }

    njs_value_number_set(retval, shm_zone->shm.size);

    return NJS_OK;
}


static njs_int_t
njs_js_ext_shared_dict_clear(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t unused, njs_value_t *retval)
{
    ngx_rbtree_t       *rbtree;
    ngx_js_dict_t      *dict;
    ngx_shm_zone_t     *shm_zone;
    ngx_rbtree_node_t  *rn, *next;

    shm_zone = njs_vm_external(vm, ngx_js_shared_dict_proto_id,
                               njs_argument(args, 0));
    if (shm_zone == NULL) {
        njs_vm_type_error(vm, "\"this\" is not a shared dict");
        return NJS_ERROR;
    }

    dict = shm_zone->data;

    ngx_rwlock_wlock(&dict->sh->rwlock);

    if (dict->timeout) {
        ngx_js_dict_evict(dict, 0x7fffffff /* INT_MAX */);

    } else {
        rbtree = &dict->sh->rbtree;

        if (rbtree->root == rbtree->sentinel) {
            goto done;
        }

        for (rn = ngx_rbtree_min(rbtree->root, rbtree->sentinel);
             rn != NULL;
             rn = next)
        {
            next = ngx_rbtree_next(rbtree, rn);

            ngx_rbtree_delete(rbtree, rn);

            ngx_js_dict_node_free(dict, (ngx_js_dict_node_t *) rn);
        }
    }

done:

    ngx_rwlock_unlock(&dict->sh->rwlock);

    njs_value_undefined_set(retval);

    return NJS_OK;
}


static njs_int_t
njs_js_ext_shared_dict_free_space(njs_vm_t *vm, njs_value_t *args,
    njs_uint_t nargs, njs_index_t unused, njs_value_t *retval)
{
    size_t           bytes;
    ngx_js_dict_t   *dict;
    ngx_shm_zone_t  *shm_zone;

    shm_zone = njs_vm_external(vm, ngx_js_shared_dict_proto_id,
                               njs_argument(args, 0));
    if (shm_zone == NULL) {
        njs_vm_type_error(vm, "\"this\" is not a shared dict");
        return NJS_ERROR;
    }

    dict = shm_zone->data;

    ngx_rwlock_rlock(&dict->sh->rwlock);
    bytes = dict->shpool->pfree * ngx_pagesize;
    ngx_rwlock_unlock(&dict->sh->rwlock);

    njs_value_number_set(retval, bytes);

    return NJS_OK;
}


static njs_int_t
njs_js_ext_shared_dict_delete(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t unused, njs_value_t *retval)
{
    ngx_int_t        rc;
    njs_str_t        key;
    ngx_shm_zone_t  *shm_zone;

    shm_zone = njs_vm_external(vm, ngx_js_shared_dict_proto_id,
                               njs_argument(args, 0));
    if (shm_zone == NULL) {
        njs_vm_type_error(vm, "\"this\" is not a shared dict");
        return NJS_ERROR;
    }

    if (ngx_js_string(vm, njs_arg(args, nargs, 1), &key) != NGX_OK) {
        return NJS_ERROR;
    }

    rc = ngx_js_dict_delete(vm, shm_zone->data, &key, NULL);

    njs_value_boolean_set(retval, rc == NGX_OK);

    return NJS_OK;
}


static njs_int_t
njs_js_ext_shared_dict_get(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t unused, njs_value_t *retval)
{
    ngx_int_t        rc;
    njs_str_t        key;
    ngx_shm_zone_t  *shm_zone;

    shm_zone = njs_vm_external(vm, ngx_js_shared_dict_proto_id,
                               njs_argument(args, 0));
    if (shm_zone == NULL) {
        njs_vm_type_error(vm, "\"this\" is not a shared dict");
        return NJS_ERROR;
    }

    if (ngx_js_string(vm, njs_arg(args, nargs, 1), &key) != NGX_OK) {
        return NJS_ERROR;
    }

    rc = ngx_js_dict_get(vm, shm_zone->data, &key, retval);
    if (njs_slow_path(rc == NGX_ERROR)) {
        njs_vm_error(vm, "failed to get value from shared dict");
        return NJS_ERROR;
    }

    return NJS_OK;
}


static njs_int_t
njs_js_ext_shared_dict_has(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t unused, njs_value_t *retval)
{
    njs_str_t            key;
    ngx_msec_t           now;
    ngx_time_t          *tp;
    ngx_js_dict_t       *dict;
    ngx_shm_zone_t      *shm_zone;
    ngx_js_dict_node_t  *node;

    shm_zone = njs_vm_external(vm, ngx_js_shared_dict_proto_id,
                               njs_argument(args, 0));
    if (shm_zone == NULL) {
        njs_vm_type_error(vm, "\"this\" is not a shared dict");
        return NJS_ERROR;
    }

    if (ngx_js_string(vm, njs_arg(args, nargs, 1), &key) != NGX_OK) {
        return NJS_ERROR;
    }

    dict = shm_zone->data;

    ngx_rwlock_rlock(&dict->sh->rwlock);

    node = ngx_js_dict_lookup(dict, &key);

    if (node != NULL && dict->timeout) {
        tp = ngx_timeofday();
        now = tp->sec * 1000 + tp->msec;

        if (now >= node->expire.key) {
            node = NULL;
        }
    }

    ngx_rwlock_unlock(&dict->sh->rwlock);

    njs_value_boolean_set(retval, node != NULL);

    return NJS_OK;
}


static njs_int_t
njs_js_ext_shared_dict_keys(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t unused, njs_value_t *retval)
{
    njs_int_t            rc;
    ngx_int_t            max_count;
    ngx_msec_t           now;
    ngx_time_t          *tp;
    njs_value_t         *value;
    ngx_rbtree_t        *rbtree;
    ngx_js_dict_t       *dict;
    ngx_shm_zone_t      *shm_zone;
    ngx_rbtree_node_t   *rn;
    ngx_js_dict_node_t  *node;

    shm_zone = njs_vm_external(vm, ngx_js_shared_dict_proto_id,
                               njs_argument(args, 0));
    if (shm_zone == NULL) {
        njs_vm_type_error(vm, "\"this\" is not a shared dict");
        return NJS_ERROR;
    }

    dict = shm_zone->data;

    max_count = 1024;

    if (nargs > 1) {
        if (ngx_js_integer(vm, njs_arg(args, nargs, 1), &max_count) != NGX_OK) {
            return NJS_ERROR;
        }
    }

    rc = njs_vm_array_alloc(vm, retval, 8);
    if (rc != NJS_OK) {
        return NJS_ERROR;
    }

    ngx_rwlock_rlock(&dict->sh->rwlock);

    if (dict->timeout) {
        tp = ngx_timeofday();
        now = tp->sec * 1000 + tp->msec;
        ngx_js_dict_expire(dict, now);
    }

    rbtree = &dict->sh->rbtree;

    if (rbtree->root == rbtree->sentinel) {
        goto done;
    }

    for (rn = ngx_rbtree_min(rbtree->root, rbtree->sentinel);
         rn != NULL;
         rn = ngx_rbtree_next(rbtree, rn))
    {
        if (max_count-- == 0) {
            break;
        }

        node = (ngx_js_dict_node_t *) rn;

        value = njs_vm_array_push(vm, retval);
        if (value == NULL) {
            goto fail;
        }

        rc = njs_vm_value_string_create(vm, value, node->sn.str.data,
                                        node->sn.str.len);
        if (rc != NJS_OK) {
            goto fail;
        }
    }

done:

    ngx_rwlock_unlock(&dict->sh->rwlock);

    return NJS_OK;

fail:

    ngx_rwlock_unlock(&dict->sh->rwlock);

    return NJS_ERROR;
}


static njs_int_t
njs_js_ext_shared_dict_incr(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t unused, njs_value_t *retval)
{
    double               value;
    ngx_int_t            rc;
    njs_str_t            key;
    njs_value_t         *delta, *init;
    ngx_js_dict_t       *dict;
    ngx_shm_zone_t      *shm_zone;
    njs_opaque_value_t   lvalue;

    shm_zone = njs_vm_external(vm, ngx_js_shared_dict_proto_id,
                               njs_argument(args, 0));
    if (shm_zone == NULL) {
        njs_vm_type_error(vm, "\"this\" is not a shared dict");
        return NJS_ERROR;
    }

    dict = shm_zone->data;

    if (dict->type != NGX_JS_DICT_TYPE_NUMBER) {
        njs_vm_type_error(vm, "shared dict is not a number dict");
        return NJS_ERROR;
    }

    if (ngx_js_string(vm, njs_arg(args, nargs, 1), &key) != NGX_OK) {
        return NJS_ERROR;
    }

    delta = njs_arg(args, nargs, 2);
    if (!njs_value_is_number(delta)) {
        njs_vm_type_error(vm, "delta is not a number");
        return NJS_ERROR;
    }

    init = njs_lvalue_arg(njs_value_arg(&lvalue), args, nargs, 3);
    if (!njs_value_is_number(init) && !njs_value_is_undefined(init)) {
        njs_vm_type_error(vm, "init value is not a number");
        return NJS_ERROR;
    }

    if (njs_value_is_undefined(init)) {
        njs_value_number_set(init, 0);
    }

    rc = ngx_js_dict_incr(shm_zone->data, &key, delta, init, &value);
    if (rc == NGX_ERROR) {
        njs_vm_error(vm, "failed to increment value in shared dict");
        return NJS_ERROR;
    }

    njs_value_number_set(retval, value);

    return NJS_OK;
}


static njs_int_t
njs_js_ext_shared_dict_items(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t unused, njs_value_t *retval)
{
    njs_int_t            rc;
    ngx_int_t            max_count;
    ngx_msec_t           now;
    ngx_time_t          *tp;
    njs_value_t         *value, *kv;
    ngx_rbtree_t        *rbtree;
    ngx_js_dict_t       *dict;
    ngx_shm_zone_t      *shm_zone;
    ngx_rbtree_node_t   *rn;
    ngx_js_dict_node_t  *node;

    shm_zone = njs_vm_external(vm, ngx_js_shared_dict_proto_id,
                               njs_argument(args, 0));
    if (shm_zone == NULL) {
        njs_vm_type_error(vm, "\"this\" is not a shared dict");
        return NJS_ERROR;
    }

    dict = shm_zone->data;

    max_count = 1024;

    if (nargs > 1) {
        if (ngx_js_integer(vm, njs_arg(args, nargs, 1), &max_count) != NGX_OK) {
            return NJS_ERROR;
        }
    }

    rc = njs_vm_array_alloc(vm, retval, 8);
    if (rc != NJS_OK) {
        return NJS_ERROR;
    }

    ngx_rwlock_rlock(&dict->sh->rwlock);

    if (dict->timeout) {
        tp = ngx_timeofday();
        now = tp->sec * 1000 + tp->msec;
        ngx_js_dict_expire(dict, now);
    }

    rbtree = &dict->sh->rbtree;

    if (rbtree->root == rbtree->sentinel) {
        goto done;
    }

    for (rn = ngx_rbtree_min(rbtree->root, rbtree->sentinel);
         rn != NULL;
         rn = ngx_rbtree_next(rbtree, rn))
    {
        if (max_count-- == 0) {
            break;
        }

        node = (ngx_js_dict_node_t *) rn;

        kv = njs_vm_array_push(vm, retval);
        if (kv == NULL) {
            goto fail;
        }

        rc = njs_vm_array_alloc(vm, kv, 2);
        if (rc != NJS_OK) {
            goto fail;
        }

        value = njs_vm_array_push(vm, kv);
        if (value == NULL) {
            goto fail;
        }

        rc = njs_vm_value_string_create(vm, value, node->sn.str.data,
                                        node->sn.str.len);
        if (rc != NJS_OK) {
            goto fail;
        }

        value = njs_vm_array_push(vm, kv);
        if (value == NULL) {
            goto fail;
        }

        rc = ngx_js_dict_copy_value_locked(vm, dict, node, value);
        if (rc != NJS_OK) {
            goto fail;
        }
    }

done:

    ngx_rwlock_unlock(&dict->sh->rwlock);

    return NJS_OK;

fail:

    ngx_rwlock_unlock(&dict->sh->rwlock);

    return NJS_ERROR;
}


static njs_int_t
njs_js_ext_shared_dict_name(njs_vm_t *vm, njs_object_prop_t *prop,
    njs_value_t *value, njs_value_t *setval, njs_value_t *retval)
{
    ngx_shm_zone_t  *shm_zone;

    shm_zone = njs_vm_external(vm, ngx_js_shared_dict_proto_id, value);
    if (shm_zone == NULL) {
        njs_value_undefined_set(retval);
        return NJS_DECLINED;
    }

    return njs_vm_value_string_create(vm, retval, shm_zone->shm.name.data,
                                      shm_zone->shm.name.len);
}


static njs_int_t
njs_js_ext_shared_dict_pop(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t unused, njs_value_t *retval)
{
    ngx_int_t        rc;
    njs_str_t        key;
    ngx_shm_zone_t  *shm_zone;

    shm_zone = njs_vm_external(vm, ngx_js_shared_dict_proto_id,
                               njs_argument(args, 0));
    if (shm_zone == NULL) {
        njs_vm_type_error(vm, "\"this\" is not a shared dict");
        return NJS_ERROR;
    }

    if (ngx_js_string(vm, njs_arg(args, nargs, 1), &key) != NGX_OK) {
        return NJS_ERROR;
    }

    rc = ngx_js_dict_delete(vm, shm_zone->data, &key, retval);

    if (rc == NGX_DECLINED) {
        njs_value_undefined_set(retval);
    }

    return (rc != NGX_ERROR) ? NJS_OK : NJS_ERROR;
}


static njs_int_t
njs_js_ext_shared_dict_set(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t flags, njs_value_t *retval)
{
    njs_str_t        key;
    ngx_int_t        rc;
    njs_value_t     *value;
    ngx_js_dict_t   *dict;
    ngx_shm_zone_t  *shm_zone;

    shm_zone = njs_vm_external(vm, ngx_js_shared_dict_proto_id,
                               njs_argument(args, 0));
    if (shm_zone == NULL) {
        njs_vm_type_error(vm, "\"this\" is not a shared dict");
        return NJS_ERROR;
    }

    if (ngx_js_string(vm, njs_arg(args, nargs, 1), &key) != NGX_OK) {
        return NJS_ERROR;
    }

    dict = shm_zone->data;
    value = njs_arg(args, nargs, 2);

    if (dict->type == NGX_JS_DICT_TYPE_STRING) {
        if (!njs_value_is_string(value)) {
            njs_vm_type_error(vm, "string value is expected");
            return NJS_ERROR;
        }

    } else {
        if (!njs_value_is_number(value)) {
            njs_vm_type_error(vm, "number value is expected");
            return NJS_ERROR;
        }
    }

    rc = ngx_js_dict_set(vm, shm_zone->data, &key, value, flags);
    if (rc == NGX_ERROR) {
        return NJS_ERROR;
    }

    if (flags) {
        /* add() or replace(). */
        njs_value_boolean_set(retval, rc == NGX_OK);

    } else {
        njs_value_assign(retval, njs_argument(args, 0));
    }

    return NJS_OK;
}


static njs_int_t
njs_js_ext_shared_dict_size(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t unused, njs_value_t *retval)
{
    njs_int_t           items;
    ngx_msec_t          now;
    ngx_time_t         *tp;
    ngx_rbtree_t       *rbtree;
    ngx_js_dict_t      *dict;
    ngx_shm_zone_t     *shm_zone;
    ngx_rbtree_node_t  *rn;

    shm_zone = njs_vm_external(vm, ngx_js_shared_dict_proto_id,
                               njs_argument(args, 0));
    if (shm_zone == NULL) {
        njs_vm_type_error(vm, "\"this\" is not a shared dict");
        return NJS_ERROR;
    }

    dict = shm_zone->data;

    ngx_rwlock_rlock(&dict->sh->rwlock);

    if (dict->timeout) {
        tp = ngx_timeofday();
        now = tp->sec * 1000 + tp->msec;
        ngx_js_dict_expire(dict, now);
    }

    rbtree = &dict->sh->rbtree;

    if (rbtree->root == rbtree->sentinel) {
        ngx_rwlock_unlock(&dict->sh->rwlock);
        njs_value_number_set(retval, 0);
        return NJS_OK;
    }

    items = 0;

    for (rn = ngx_rbtree_min(rbtree->root, rbtree->sentinel);
         rn != NULL;
         rn = ngx_rbtree_next(rbtree, rn))
    {
        items++;
    }

    ngx_rwlock_unlock(&dict->sh->rwlock);
    njs_value_number_set(retval, items);

    return NJS_OK;
}


static njs_int_t
njs_js_ext_shared_dict_type(njs_vm_t *vm, njs_object_prop_t *prop,
    njs_value_t *value, njs_value_t *setval, njs_value_t *retval)
{
    njs_str_t        type;
    ngx_js_dict_t   *dict;
    ngx_shm_zone_t  *shm_zone;

    shm_zone = njs_vm_external(vm, ngx_js_shared_dict_proto_id, value);
    if (shm_zone == NULL) {
        njs_value_undefined_set(retval);
        return NJS_DECLINED;
    }

    dict = shm_zone->data;

    switch (dict->type) {
    case NGX_JS_DICT_TYPE_STRING:
        type = njs_str_value("string");
        break;

    default:
        type = njs_str_value("number");
        break;
    }

    return njs_vm_value_string_create(vm, retval, type.start, type.length);
}


static ngx_js_dict_node_t *
ngx_js_dict_lookup(ngx_js_dict_t *dict, njs_str_t *key)
{
    uint32_t       hash;
    ngx_str_t      k;
    ngx_rbtree_t  *rbtree;

    rbtree = &dict->sh->rbtree;

    hash = ngx_crc32_long(key->start, key->length);

    k.data = key->start;
    k.len = key->length;

    return (ngx_js_dict_node_t *) ngx_str_rbtree_lookup(rbtree, &k, hash);
}


static void *
ngx_js_dict_alloc(ngx_js_dict_t *dict, size_t n)
{
    void  *p;

    p = ngx_slab_alloc_locked(dict->shpool, n);

    if (p == NULL && dict->evict) {
        ngx_js_dict_evict(dict, 16);
        p = ngx_slab_alloc_locked(dict->shpool, n);
    }

    return p;
}


static void
ngx_js_dict_node_free(ngx_js_dict_t *dict, ngx_js_dict_node_t *node)
{
    ngx_slab_pool_t  *shpool;

    shpool = dict->shpool;

    if (dict->type == NGX_JS_DICT_TYPE_STRING) {
        ngx_slab_free_locked(shpool, node->u.value.data);
    }

    ngx_slab_free_locked(shpool, node);
}


static ngx_int_t
ngx_js_dict_set(njs_vm_t *vm, ngx_js_dict_t *dict, njs_str_t *key,
    njs_value_t *value, unsigned flags)
{
    ngx_msec_t           now;
    ngx_time_t          *tp;
    ngx_js_dict_node_t  *node;

    tp = ngx_timeofday();
    now = tp->sec * 1000 + tp->msec;

    ngx_rwlock_wlock(&dict->sh->rwlock);

    node = ngx_js_dict_lookup(dict, key);

    if (node == NULL) {
        if (flags & NGX_JS_DICT_FLAG_MUST_EXIST) {
            ngx_rwlock_unlock(&dict->sh->rwlock);
            return NGX_DECLINED;
        }

        if (ngx_js_dict_add(dict, key, value, now) != NGX_OK) {
            goto memory_error;
        }

    } else {
        if (flags & NGX_JS_DICT_FLAG_MUST_NOT_EXIST) {
            if (!dict->timeout || now < node->expire.key) {
                ngx_rwlock_unlock(&dict->sh->rwlock);
                return NGX_DECLINED;
            }
        }

        if (ngx_js_dict_update(dict, node, value, now) != NGX_OK) {
            goto memory_error;
        }
    }

    ngx_rwlock_unlock(&dict->sh->rwlock);

    return NGX_OK;

memory_error:

    ngx_rwlock_unlock(&dict->sh->rwlock);

    njs_vm_error3(vm, ngx_js_shared_dict_error_id, "", 0);

    return NGX_ERROR;
}


static ngx_int_t
ngx_js_dict_add(ngx_js_dict_t *dict, njs_str_t *key, njs_value_t *value,
    ngx_msec_t now)
{
    size_t               n;
    uint32_t             hash;
    njs_str_t            string;
    ngx_js_dict_node_t  *node;

    if (dict->timeout) {
        ngx_js_dict_expire(dict, now);
    }

    n = sizeof(ngx_js_dict_node_t) + key->length;
    hash = ngx_crc32_long(key->start, key->length);

    node = ngx_js_dict_alloc(dict, n);
    if (node == NULL) {
        return NGX_ERROR;
    }

    node->sn.str.data = (u_char *) node + sizeof(ngx_js_dict_node_t);

    if (dict->type == NGX_JS_DICT_TYPE_STRING) {
        njs_value_string_get(value, &string);
        node->u.value.data = ngx_js_dict_alloc(dict, string.length);
        if (node->u.value.data == NULL) {
            ngx_slab_free_locked(dict->shpool, node);
            return NGX_ERROR;
        }

        ngx_memcpy(node->u.value.data, string.start, string.length);
        node->u.value.len = string.length;

    } else {
        node->u.number = njs_value_number(value);
    }

    node->sn.node.key = hash;

    ngx_memcpy(node->sn.str.data, key->start, key->length);
    node->sn.str.len = key->length;

    ngx_rbtree_insert(&dict->sh->rbtree, &node->sn.node);

    if (dict->timeout) {
        node->expire.key = now + dict->timeout;
        ngx_rbtree_insert(&dict->sh->rbtree_expire, &node->expire);
    }

    return NGX_OK;
}


static ngx_int_t
ngx_js_dict_update(ngx_js_dict_t *dict, ngx_js_dict_node_t *node,
    njs_value_t *value, ngx_msec_t now)
{
    u_char     *p;
    njs_str_t   string;

    if (dict->type == NGX_JS_DICT_TYPE_STRING) {
        njs_value_string_get(value, &string);

        p = ngx_js_dict_alloc(dict, string.length);
        if (p == NULL) {
            return NGX_ERROR;
        }

        ngx_slab_free_locked(dict->shpool, node->u.value.data);
        ngx_memcpy(p, string.start, string.length);

        node->u.value.data = p;
        node->u.value.len = string.length;

    } else {
        node->u.number = njs_value_number(value);
    }

    if (dict->timeout) {
        ngx_rbtree_delete(&dict->sh->rbtree_expire, &node->expire);
        node->expire.key = now + dict->timeout;
        ngx_rbtree_insert(&dict->sh->rbtree_expire, &node->expire);
    }

    return NGX_OK;
}


static ngx_int_t
ngx_js_dict_delete(njs_vm_t *vm, ngx_js_dict_t *dict, njs_str_t *key,
    njs_value_t *retval)
{
    ngx_int_t            rc;
    ngx_msec_t           now;
    ngx_time_t          *tp;
    ngx_js_dict_node_t  *node;

    ngx_rwlock_wlock(&dict->sh->rwlock);

    node = ngx_js_dict_lookup(dict, key);

    if (node == NULL) {
        ngx_rwlock_unlock(&dict->sh->rwlock);
        return NGX_DECLINED;
    }

    if (dict->timeout) {
        ngx_rbtree_delete(&dict->sh->rbtree_expire, &node->expire);
    }

    ngx_rbtree_delete(&dict->sh->rbtree, (ngx_rbtree_node_t *) node);

    if (retval != NULL) {
        tp = ngx_timeofday();
        now = tp->sec * 1000 + tp->msec;

        if (!dict->timeout || now < node->expire.key) {
            rc = ngx_js_dict_copy_value_locked(vm, dict, node, retval);

        } else {
            rc = NGX_DECLINED;
        }

    } else {
        rc = NGX_OK;
    }

    ngx_js_dict_node_free(dict, node);

    ngx_rwlock_unlock(&dict->sh->rwlock);

    return rc;
}


static ngx_int_t
ngx_js_dict_incr(ngx_js_dict_t *dict, njs_str_t *key, njs_value_t *delta,
    njs_value_t *init, double *value)
{
    ngx_msec_t           now;
    ngx_time_t          *tp;
    ngx_js_dict_node_t  *node;

    tp = ngx_timeofday();
    now = tp->sec * 1000 + tp->msec;

    ngx_rwlock_wlock(&dict->sh->rwlock);

    node = ngx_js_dict_lookup(dict, key);

    if (node == NULL) {
        njs_value_number_set(init, njs_value_number(init)
                                   + njs_value_number(delta));
        if (ngx_js_dict_add(dict, key, init, now) != NGX_OK) {
            ngx_rwlock_unlock(&dict->sh->rwlock);
            return NGX_ERROR;
        }

        *value = njs_value_number(init);

    } else {
        node->u.number += njs_value_number(delta);
        *value = node->u.number;

        if (dict->timeout) {
            ngx_rbtree_delete(&dict->sh->rbtree_expire, &node->expire);
            node->expire.key = now + dict->timeout;
            ngx_rbtree_insert(&dict->sh->rbtree_expire, &node->expire);
        }
    }

    ngx_rwlock_unlock(&dict->sh->rwlock);

    return NGX_OK;
}


static ngx_int_t
ngx_js_dict_get(njs_vm_t *vm, ngx_js_dict_t *dict, njs_str_t *key,
    njs_value_t *retval)
{
    ngx_int_t            rc;
    ngx_msec_t           now;
    ngx_time_t          *tp;
    ngx_js_dict_node_t  *node;

    ngx_rwlock_rlock(&dict->sh->rwlock);

    node = ngx_js_dict_lookup(dict, key);

    if (node == NULL) {
        goto not_found;
    }

    if (dict->timeout) {
        tp = ngx_timeofday();
        now = tp->sec * 1000 + tp->msec;

        if (now >= node->expire.key) {
            goto not_found;
        }
    }

    rc = ngx_js_dict_copy_value_locked(vm, dict, node, retval);
    ngx_rwlock_unlock(&dict->sh->rwlock);

    return rc;

not_found:

    ngx_rwlock_unlock(&dict->sh->rwlock);
    njs_value_undefined_set(retval);

    return NGX_OK;
}


static ngx_int_t
ngx_js_dict_copy_value_locked(njs_vm_t *vm, ngx_js_dict_t *dict,
    ngx_js_dict_node_t *node, njs_value_t *retval)
{
    njs_int_t   ret;
    njs_str_t   string;
    ngx_uint_t  type;
    ngx_pool_t  *pool;

    type = dict->type;

    if (type == NGX_JS_DICT_TYPE_STRING) {
        pool = ngx_external_pool(vm, njs_vm_external_ptr(vm));

        string.length = node->u.value.len;
        string.start = ngx_pstrdup(pool, &node->u.value);
        if (string.start == NULL) {
            return NGX_ERROR;
        }

        ret = njs_vm_value_string_create(vm, retval, string.start,
                                         string.length);
        if (ret != NJS_OK) {
            return NGX_ERROR;
        }

    } else {
        njs_value_number_set(retval, node->u.number);
    }

    return NGX_OK;
}


static void
ngx_js_dict_expire(ngx_js_dict_t *dict, ngx_msec_t now)
{
    ngx_rbtree_t        *rbtree;
    ngx_rbtree_node_t   *rn, *next;
    ngx_js_dict_node_t  *node;

    rbtree = &dict->sh->rbtree_expire;

    if (rbtree->root == rbtree->sentinel) {
        return;
    }

    for (rn = ngx_rbtree_min(rbtree->root, rbtree->sentinel);
         rn != NULL;
         rn = next)
    {
        if (rn->key > now) {
            return;
        }

        node = (ngx_js_dict_node_t *)
                   ((char *) rn - offsetof(ngx_js_dict_node_t, expire));

        next = ngx_rbtree_next(rbtree, rn);

        ngx_rbtree_delete(rbtree, rn);

        ngx_rbtree_delete(&dict->sh->rbtree, (ngx_rbtree_node_t *) node);

        ngx_js_dict_node_free(dict, node);
    }
}


static void
ngx_js_dict_evict(ngx_js_dict_t *dict, ngx_int_t count)
{
    ngx_rbtree_t        *rbtree;
    ngx_rbtree_node_t   *rn, *next;
    ngx_js_dict_node_t  *node;

    rbtree = &dict->sh->rbtree_expire;

    if (rbtree->root == rbtree->sentinel) {
        return;
    }

    for (rn = ngx_rbtree_min(rbtree->root, rbtree->sentinel);
         rn != NULL;
         rn = next)
    {
        if (count-- == 0) {
            return;
        }

        node = (ngx_js_dict_node_t *)
                   ((char *) rn - offsetof(ngx_js_dict_node_t, expire));

        next = ngx_rbtree_next(rbtree, rn);

        ngx_rbtree_delete(rbtree, rn);

        ngx_rbtree_delete(&dict->sh->rbtree, (ngx_rbtree_node_t *) node);

        ngx_js_dict_node_free(dict, node);
    }
}


static njs_int_t
ngx_js_dict_shared_error_name(njs_vm_t *vm, njs_object_prop_t *prop,
    njs_value_t *value, njs_value_t *setval, njs_value_t *retval)
{
    return njs_vm_value_string_create(vm, retval,
                                      (u_char *) "SharedMemoryError", 17);
}


static ngx_int_t
ngx_js_dict_init_zone(ngx_shm_zone_t *shm_zone, void *data)
{
    ngx_js_dict_t  *prev = data;

    size_t          len;
    ngx_js_dict_t  *dict;

    dict = shm_zone->data;

    if (prev) {

        if (dict->timeout && !prev->timeout) {
            ngx_log_error(NGX_LOG_EMERG, shm_zone->shm.log, 0,
                          "js_shared_dict_zone \"%V\" uses timeout %M "
                          "while previously it did not use timeout",
                          &shm_zone->shm.name, dict->timeout);
            return NGX_ERROR;
        }

        if (dict->type != prev->type) {
            ngx_log_error(NGX_LOG_EMERG, shm_zone->shm.log, 0,
                          "js_shared_dict_zone \"%V\" had previously a "
                          "different type", &shm_zone->shm.name, dict->timeout);
            return NGX_ERROR;
        }

        dict->sh = prev->sh;
        dict->shpool = prev->shpool;

        return NGX_OK;
    }

    dict->shpool = (ngx_slab_pool_t *) shm_zone->shm.addr;

    if (shm_zone->shm.exists) {
        dict->sh = dict->shpool->data;
        return NGX_OK;
    }

    dict->sh = ngx_slab_calloc(dict->shpool, sizeof(ngx_js_dict_sh_t));
    if (dict->sh == NULL) {
        return NGX_ERROR;
    }

    dict->shpool->data = dict->sh;

    ngx_rbtree_init(&dict->sh->rbtree, &dict->sh->sentinel,
                    ngx_str_rbtree_insert_value);

    if (dict->timeout) {
        ngx_rbtree_init(&dict->sh->rbtree_expire,
                        &dict->sh->sentinel_expire,
                        ngx_rbtree_insert_timer_value);
    }

    len = sizeof(" in js shared dict zone \"\"") + shm_zone->shm.name.len;

    dict->shpool->log_ctx = ngx_slab_alloc(dict->shpool, len);
    if (dict->shpool->log_ctx == NULL) {
        return NGX_ERROR;
    }

    ngx_sprintf(dict->shpool->log_ctx, " in js shared zone \"%V\"%Z",
                &shm_zone->shm.name);

    return NGX_OK;
}


char *
ngx_js_shared_dict_zone(ngx_conf_t *cf, ngx_command_t *cmd, void *conf,
    void *tag)
{
    ngx_js_main_conf_t  *jmcf = conf;

    u_char          *p;
    ssize_t          size;
    ngx_str_t       *value, name, s;
    ngx_flag_t       evict;
    ngx_msec_t       timeout;
    ngx_uint_t       i, type;
    ngx_js_dict_t   *dict;
    ngx_shm_zone_t  *shm_zone;

    size = 0;
    evict = 0;
    timeout = 0;
    name.len = 0;
    type = NGX_JS_DICT_TYPE_STRING;

    value = cf->args->elts;

    for (i = 1; i < cf->args->nelts; i++) {

        if (ngx_strncmp(value[i].data, "zone=", 5) == 0) {

            name.data = value[i].data + 5;

            p = (u_char *) ngx_strchr(name.data, ':');

            if (p == NULL) {
                ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                                   "invalid zone size \"%V\"", &value[i]);
                return NGX_CONF_ERROR;
            }

            name.len = p - name.data;

            if (name.len == 0) {
                ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                                   "invalid zone name \"%V\"", &value[i]);
                return NGX_CONF_ERROR;
            }

            s.data = p + 1;
            s.len = value[i].data + value[i].len - s.data;

            size = ngx_parse_size(&s);

            if (size == NGX_ERROR) {
                ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                                   "invalid zone size \"%V\"", &value[i]);
                return NGX_CONF_ERROR;
            }

            if (size < (ssize_t) (8 * ngx_pagesize)) {
                ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                                   "zone \"%V\" is too small", &value[i]);
                return NGX_CONF_ERROR;
            }

            continue;
        }

        if (ngx_strncmp(value[i].data, "evict", 5) == 0) {
            evict = 1;
            continue;
        }

        if (ngx_strncmp(value[i].data, "timeout=", 8) == 0) {

            s.data = value[i].data + 8;
            s.len = value[i].len - 8;

            timeout = ngx_parse_time(&s, 0);
            if (timeout == (ngx_msec_t) NGX_ERROR) {
                ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                                   "invalid timeout value \"%V\"", &value[i]);
                return NGX_CONF_ERROR;
            }

            continue;
        }

        if (ngx_strncmp(value[i].data, "type=", 5) == 0) {

            if (ngx_strcmp(&value[i].data[5], "string") == 0) {
                type = NGX_JS_DICT_TYPE_STRING;

            } else if (ngx_strcmp(&value[i].data[5], "number") == 0) {
                type = NGX_JS_DICT_TYPE_NUMBER;

            } else {
                ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                                   "invalid dict type \"%s\"",
                                   &value[i].data[5]);
                return NGX_CONF_ERROR;
            }

            continue;
        }
    }

    if (name.len == 0) {
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                           "\"%V\" must have \"zone\" parameter", &cmd->name);
        return NGX_CONF_ERROR;
    }

    if (evict && timeout == 0) {
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                           "evict requires timeout=");
        return NGX_CONF_ERROR;
    }

    shm_zone = ngx_shared_memory_add(cf, &name, size, tag);
    if (shm_zone == NULL) {
        return NGX_CONF_ERROR;
    }

    if (shm_zone->data) {
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0, "duplicate zone \"%V\"",
                           &name);
        return NGX_CONF_ERROR;
    }

    dict = ngx_pcalloc(cf->pool, sizeof(ngx_js_dict_t));
    if (dict == NULL) {
        return NGX_CONF_ERROR;
    }

    dict->shm_zone = shm_zone;
    dict->next = jmcf->dicts;
    jmcf->dicts = dict;

    shm_zone->data = dict;
    shm_zone->init = ngx_js_dict_init_zone;

    dict->evict = evict;
    dict->timeout = timeout;
    dict->type = type;

    return NGX_CONF_OK;
}


static njs_int_t
ngx_js_shared_dict_preinit(njs_vm_t *vm)
{
    static const njs_str_t  error_name = njs_str("SharedMemoryError");

    ngx_js_shared_dict_error_id =
        njs_vm_external_constructor(vm, &error_name,
                          njs_error_constructor, ngx_js_ext_error_ctor_props,
                          njs_nitems(ngx_js_ext_error_ctor_props),
                          ngx_js_ext_error_proto_props,
                          njs_nitems(ngx_js_ext_error_ctor_props));
    if (njs_slow_path(ngx_js_shared_dict_error_id < 0)) {
        return NJS_ERROR;
    }

    return NJS_OK;
}


static njs_int_t
ngx_js_shared_dict_init(njs_vm_t *vm)
{
    ngx_js_shared_dict_proto_id = njs_vm_external_prototype(vm,
                                          ngx_js_ext_shared_dict,
                                          njs_nitems(ngx_js_ext_shared_dict));
    if (ngx_js_shared_dict_proto_id < 0) {
        return NJS_ERROR;
    }

    return NJS_OK;
}
