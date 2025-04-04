
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
    njs_object_prop_t *prop, uint32_t unused, njs_value_t *value,
    njs_value_t *setval, njs_value_t *retval);
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
    njs_object_prop_t *prop, uint32_t unused, njs_value_t *value,
    njs_value_t *setval, njs_value_t *retval);
static njs_int_t njs_js_ext_shared_dict_pop(njs_vm_t *vm, njs_value_t *args,
    njs_uint_t nargs, njs_index_t unused, njs_value_t *retval);
static njs_int_t njs_js_ext_shared_dict_set(njs_vm_t *vm, njs_value_t *args,
    njs_uint_t nargs, njs_index_t flags, njs_value_t *retval);
static njs_int_t njs_js_ext_shared_dict_size(njs_vm_t *vm, njs_value_t *args,
    njs_uint_t nargs, njs_index_t unused, njs_value_t *retval);
static njs_int_t njs_js_ext_shared_dict_type(njs_vm_t *vm,
    njs_object_prop_t *prop, uint32_t unused, njs_value_t *value,
    njs_value_t *setval, njs_value_t *retval);
static ngx_js_dict_node_t *ngx_js_dict_lookup(ngx_js_dict_t *dict,
    njs_str_t *key);

#define NGX_JS_DICT_FLAG_MUST_EXIST       1
#define NGX_JS_DICT_FLAG_MUST_NOT_EXIST   2

static ngx_int_t ngx_js_dict_set(njs_vm_t *vm, ngx_js_dict_t *dict,
    njs_str_t *key, njs_value_t *value, ngx_msec_t timeout, unsigned flags);
static ngx_int_t ngx_js_dict_add(ngx_js_dict_t *dict, njs_str_t *key,
    njs_value_t *value, ngx_msec_t timeout, ngx_msec_t now);
static ngx_int_t ngx_js_dict_update(ngx_js_dict_t *dict,
    ngx_js_dict_node_t *node, njs_value_t *value, ngx_msec_t timeout,
    ngx_msec_t now);
static ngx_int_t ngx_js_dict_get(njs_vm_t *vm, ngx_js_dict_t *dict,
    njs_str_t *key, njs_value_t *retval);
static ngx_int_t ngx_js_dict_incr(ngx_js_dict_t *dict, njs_str_t *key,
    njs_value_t *delta, njs_value_t *init, double *value, ngx_msec_t timeout);
static ngx_int_t ngx_js_dict_delete(njs_vm_t *vm, ngx_js_dict_t *dict,
    njs_str_t *key, njs_value_t *retval);
static ngx_int_t ngx_js_dict_copy_value_locked(njs_vm_t *vm,
    ngx_js_dict_t *dict, ngx_js_dict_node_t *node, njs_value_t *retval);

static void ngx_js_dict_expire(ngx_js_dict_t *dict, ngx_msec_t now);
static void ngx_js_dict_evict(ngx_js_dict_t *dict, ngx_int_t count);

static njs_int_t ngx_js_dict_shared_error_name(njs_vm_t *vm,
    njs_object_prop_t *prop, uint32_t unused, njs_value_t *value,
    njs_value_t *setval, njs_value_t *retval);

static ngx_int_t ngx_js_dict_init_zone(ngx_shm_zone_t *shm_zone, void *data);
static njs_int_t ngx_js_shared_dict_preinit(njs_vm_t *vm);
static njs_int_t ngx_js_shared_dict_init(njs_vm_t *vm);
static void ngx_js_dict_node_free(ngx_js_dict_t *dict,
    ngx_js_dict_node_t *node);

#if (NJS_HAVE_QUICKJS)
static int ngx_qjs_shared_own_property(JSContext *cx,
    JSPropertyDescriptor *pdesc, JSValueConst obj, JSAtom prop);
static int ngx_qjs_shared_own_property_names(JSContext *ctx,
    JSPropertyEnum **ptab, uint32_t *plen, JSValueConst obj);

static JSValue ngx_qjs_ext_ngx_shared(JSContext *cx, JSValueConst this_val);
static JSValue ngx_qjs_ext_shared_dict_capacity(JSContext *cx,
    JSValueConst this_val);
static JSValue ngx_qjs_ext_shared_dict_clear(JSContext *cx,
    JSValueConst this_val, int argc, JSValueConst *argv);
static JSValue ngx_qjs_ext_shared_dict_delete(JSContext *cx,
    JSValueConst this_val, int argc, JSValueConst *argv);
static JSValue ngx_qjs_ext_shared_dict_free_space(JSContext *cx,
    JSValueConst this_val, int argc, JSValueConst *argv);
static JSValue ngx_qjs_ext_shared_dict_get(JSContext *cx, JSValueConst this_val,
    int argc, JSValueConst *argv);
static JSValue ngx_qjs_ext_shared_dict_has(JSContext *cx, JSValueConst this_val,
    int argc, JSValueConst *argv);
static JSValue ngx_qjs_ext_shared_dict_incr(JSContext *cx,
    JSValueConst this_val, int argc, JSValueConst *argv);
static JSValue ngx_qjs_ext_shared_dict_items(JSContext *cx,
    JSValueConst this_val, int argc, JSValueConst *argv);
static JSValue ngx_qjs_ext_shared_dict_keys(JSContext *cx,
    JSValueConst this_val, int argc, JSValueConst *argv);
static JSValue ngx_qjs_ext_shared_dict_name(JSContext *cx,
    JSValueConst this_val);
static JSValue ngx_qjs_ext_shared_dict_pop(JSContext *cx, JSValueConst this_val,
    int argc, JSValueConst *argv);
static JSValue ngx_qjs_ext_shared_dict_set(JSContext *cx, JSValueConst this_val,
    int argc, JSValueConst *argv, int flags);
static JSValue ngx_qjs_ext_shared_dict_size(JSContext *cx,
    JSValueConst this_val, int argc, JSValueConst *argv);
    static JSValue ngx_qjs_ext_shared_dict_type(JSContext *cx,
    JSValueConst this_val);

static JSValue ngx_qjs_dict_copy_value_locked(JSContext *cx,
    ngx_js_dict_t *dict, ngx_js_dict_node_t *node);
static ngx_js_dict_node_t *ngx_qjs_dict_lookup(ngx_js_dict_t *dict,
    ngx_str_t *key);
static ngx_int_t ngx_qjs_dict_add(JSContext *cx, ngx_js_dict_t *dict,
    ngx_str_t *key, JSValue value, ngx_msec_t timeout, ngx_msec_t now);
static JSValue ngx_qjs_dict_delete(JSContext *cx, ngx_js_dict_t *dict,
    ngx_str_t *key, int retval);
static JSValue ngx_qjs_dict_get(JSContext *cx, ngx_js_dict_t *dict,
    ngx_str_t *key);
static JSValue ngx_qjs_dict_incr(JSContext *cx, ngx_js_dict_t *dict,
    ngx_str_t *key, double delta, double init, ngx_msec_t timeout);
static JSValue ngx_qjs_dict_set(JSContext *cx, ngx_js_dict_t *dict,
    ngx_str_t *key, JSValue value, ngx_msec_t timeout, unsigned flags);
static ngx_int_t ngx_qjs_dict_update(JSContext *cx, ngx_js_dict_t *dict,
    ngx_js_dict_node_t *node, JSValue value, ngx_msec_t timeout,
    ngx_msec_t now);

static JSValue ngx_qjs_throw_shared_memory_error(JSContext *cx);

static JSModuleDef *ngx_qjs_ngx_shared_dict_init(JSContext *cx,
    const char *name);
#endif


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


#if (NJS_HAVE_QUICKJS)

static const JSCFunctionListEntry ngx_qjs_ext_ngx[] = {
    JS_CGETSET_DEF("shared", ngx_qjs_ext_ngx_shared, NULL),
};

static const JSCFunctionListEntry ngx_qjs_ext_shared_dict[] = {
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "SharedDict",
                       JS_PROP_CONFIGURABLE),
    JS_CFUNC_MAGIC_DEF("add", 3, ngx_qjs_ext_shared_dict_set,
                       NGX_JS_DICT_FLAG_MUST_NOT_EXIST),
    JS_CGETSET_DEF("capacity", ngx_qjs_ext_shared_dict_capacity, NULL),
    JS_CFUNC_DEF("clear", 0, ngx_qjs_ext_shared_dict_clear),
    JS_CFUNC_DEF("delete", 1, ngx_qjs_ext_shared_dict_delete),
    JS_CFUNC_DEF("freeSpace", 0, ngx_qjs_ext_shared_dict_free_space),
    JS_CFUNC_DEF("get", 1, ngx_qjs_ext_shared_dict_get),
    JS_CFUNC_DEF("has", 1, ngx_qjs_ext_shared_dict_has),
    JS_CFUNC_DEF("incr", 3, ngx_qjs_ext_shared_dict_incr),
    JS_CFUNC_DEF("items", 0, ngx_qjs_ext_shared_dict_items),
    JS_CFUNC_DEF("keys", 0, ngx_qjs_ext_shared_dict_keys),
    JS_CGETSET_DEF("name", ngx_qjs_ext_shared_dict_name, NULL),
    JS_CFUNC_DEF("pop", 1, ngx_qjs_ext_shared_dict_pop),
    JS_CFUNC_MAGIC_DEF("replace", 3, ngx_qjs_ext_shared_dict_set,
                       NGX_JS_DICT_FLAG_MUST_EXIST),
    JS_CFUNC_MAGIC_DEF("set", 3, ngx_qjs_ext_shared_dict_set, 0),
    JS_CFUNC_DEF("size", 0, ngx_qjs_ext_shared_dict_size),
    JS_CGETSET_DEF("type", ngx_qjs_ext_shared_dict_type, NULL),
};

static const JSCFunctionListEntry ngx_qjs_ext_shared_dict_error[] = {
    JS_PROP_STRING_DEF("name", "SharedMemoryError",
                       JS_PROP_WRITABLE | JS_PROP_CONFIGURABLE),
    JS_PROP_STRING_DEF("message", "", JS_PROP_WRITABLE | JS_PROP_CONFIGURABLE),
};

static JSClassDef ngx_qjs_shared_dict_class = {
    "SharedDict",
    .finalizer = NULL,
};

static JSClassDef ngx_qjs_shared_class = {
    "Shared",
    .finalizer = NULL,
    .exotic = & (JSClassExoticMethods) {
        .get_own_property = ngx_qjs_shared_own_property,
        .get_own_property_names = ngx_qjs_shared_own_property_names,
    },
};

static JSClassDef ngx_qjs_shared_dict_error_class = {
    "SharedDictError",
    .finalizer = NULL,
};

qjs_module_t  ngx_qjs_ngx_shared_dict_module = {
    .name = "shared_dict",
    .init = ngx_qjs_ngx_shared_dict_init,
};

#endif


njs_int_t
njs_js_ext_global_shared_prop(njs_vm_t *vm, njs_object_prop_t *prop,
    uint32_t atom_id, njs_value_t *value, njs_value_t *setval,
    njs_value_t *retval)
{
    njs_int_t            ret;
    njs_str_t            name;
    ngx_js_dict_t       *dict;
    ngx_shm_zone_t      *shm_zone;
    ngx_js_main_conf_t  *conf;

    ret = njs_vm_prop_name(vm, atom_id, &name);
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
    uint32_t unused, njs_value_t *value, njs_value_t *setval,
    njs_value_t *retval)
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
    ngx_msec_t           timeout;
    njs_value_t         *delta, *init, *timeo;
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

    timeo = njs_arg(args, nargs, 4);
    if (!njs_value_is_undefined(timeo)) {
        if (!njs_value_is_number(timeo)) {
            njs_vm_type_error(vm, "timeout is not a number");
            return NJS_ERROR;
        }

        if (!dict->timeout) {
            njs_vm_type_error(vm, "shared dict must be declared with timeout");
            return NJS_ERROR;
        }

        timeout = (ngx_msec_t) njs_value_number(timeo);

        if (timeout < 1) {
            njs_vm_type_error(vm, "timeout must be greater than or equal to 1");
            return NJS_ERROR;
        }

    } else {
        timeout = dict->timeout;
    }

    rc = ngx_js_dict_incr(shm_zone->data, &key, delta, init, &value, timeout);
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
    uint32_t unused, njs_value_t *value, njs_value_t *setval,
    njs_value_t *retval)
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
    ngx_msec_t       timeout;
    njs_value_t     *value, *timeo;
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

    timeo = njs_arg(args, nargs, 3);
    if (!njs_value_is_undefined(timeo)) {
        if (!njs_value_is_number(timeo)) {
            njs_vm_type_error(vm, "timeout is not a number");
            return NJS_ERROR;
        }

        if (!dict->timeout) {
            njs_vm_type_error(vm, "shared dict must be declared with timeout");
            return NJS_ERROR;
        }

        timeout = (ngx_msec_t) njs_value_number(timeo);

        if (timeout < 1) {
            njs_vm_type_error(vm, "timeout must be greater than or equal to 1");
            return NJS_ERROR;
        }

    } else {
        timeout = dict->timeout;
    }

    rc = ngx_js_dict_set(vm, shm_zone->data, &key, value, timeout, flags);
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
    uint32_t unused, njs_value_t *value, njs_value_t *setval,
    njs_value_t *retval)
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
    njs_value_t *value, ngx_msec_t timeout, unsigned flags)
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

        if (ngx_js_dict_add(dict, key, value, timeout, now) != NGX_OK) {
            goto memory_error;
        }

    } else {
        if (flags & NGX_JS_DICT_FLAG_MUST_NOT_EXIST) {
            if (!dict->timeout || now < node->expire.key) {
                ngx_rwlock_unlock(&dict->sh->rwlock);
                return NGX_DECLINED;
            }
        }

        if (ngx_js_dict_update(dict, node, value, timeout, now) != NGX_OK) {
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
    ngx_msec_t timeout, ngx_msec_t now)
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
        node->expire.key = now + timeout;
        ngx_rbtree_insert(&dict->sh->rbtree_expire, &node->expire);
    }

    return NGX_OK;
}


static ngx_int_t
ngx_js_dict_update(ngx_js_dict_t *dict, ngx_js_dict_node_t *node,
    njs_value_t *value, ngx_msec_t timeout, ngx_msec_t now)
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
        node->expire.key = now + timeout;
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
    njs_value_t *init, double *value, ngx_msec_t timeout)
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
        if (ngx_js_dict_add(dict, key, init, timeout, now) != NGX_OK) {
            ngx_rwlock_unlock(&dict->sh->rwlock);
            return NGX_ERROR;
        }

        *value = njs_value_number(init);

    } else {
        node->u.number += njs_value_number(delta);
        *value = node->u.number;

        if (dict->timeout) {
            ngx_rbtree_delete(&dict->sh->rbtree_expire, &node->expire);
            node->expire.key = now + timeout;
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
    ngx_uint_t  type;

    type = dict->type;

    if (type == NGX_JS_DICT_TYPE_STRING) {
        ret = njs_vm_value_string_create(vm, retval, node->u.value.data,
                                         node->u.value.len);
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
    uint32_t unused, njs_value_t *value, njs_value_t *setval,
    njs_value_t *retval)
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


#if (NJS_HAVE_QUICKJS)

static int
ngx_qjs_shared_own_property(JSContext *cx, JSPropertyDescriptor *pdesc,
    JSValueConst obj, JSAtom prop)
{
    int                  ret;
    ngx_str_t            name;
    ngx_js_dict_t       *dict;
    ngx_shm_zone_t      *shm_zone;
    ngx_js_main_conf_t  *conf;

    name.data = (u_char *) JS_AtomToCString(cx, prop);
    if (name.data == NULL) {
        return -1;
    }

    name.len = ngx_strlen(name.data);

    ret = 0;
    conf = ngx_qjs_main_conf(cx);

    for (dict = conf->dicts; dict != NULL; dict = dict->next) {
        shm_zone = dict->shm_zone;

        if (shm_zone->shm.name.len == name.len
            && ngx_strncmp(shm_zone->shm.name.data, name.data, name.len)
               == 0)
        {
            if (pdesc != NULL) {
                pdesc->flags = JS_PROP_ENUMERABLE;
                pdesc->getter = JS_UNDEFINED;
                pdesc->setter = JS_UNDEFINED;
                pdesc->value = JS_NewObjectClass(cx,
                                                 NGX_QJS_CLASS_ID_SHARED_DICT);
                if (JS_IsException(pdesc->value)) {
                    ret = -1;
                    break;
                }

                JS_SetOpaque(pdesc->value, shm_zone);
            }

            ret = 1;
            break;
        }
    }

    JS_FreeCString(cx, (char *) name.data);

    return ret;
}


static int
ngx_qjs_shared_own_property_names(JSContext *cx, JSPropertyEnum **ptab,
    uint32_t *plen, JSValueConst obj)
{
    int                 ret;
    JSAtom              key;
    JSValue             keys;
    ngx_js_dict_t       *dict;
    ngx_shm_zone_t      *shm_zone;
    ngx_js_main_conf_t  *conf;

    keys = JS_NewObject(cx);
    if (JS_IsException(keys)) {
        return -1;
    }

    conf = ngx_qjs_main_conf(cx);

    for (dict = conf->dicts; dict != NULL; dict = dict->next) {
        shm_zone = dict->shm_zone;

        key = JS_NewAtomLen(cx, (const char *) shm_zone->shm.name.data,
                            shm_zone->shm.name.len);
        if (key == JS_ATOM_NULL) {
            return -1;
        }

        if (JS_DefinePropertyValue(cx, keys, key, JS_UNDEFINED,
                                   JS_PROP_ENUMERABLE) < 0)
        {
            JS_FreeAtom(cx, key);
            return -1;
        }

        JS_FreeAtom(cx, key);
    }

    ret = JS_GetOwnPropertyNames(cx, ptab, plen, keys, JS_GPN_STRING_MASK);

    JS_FreeValue(cx, keys);

    return ret;
}


static JSValue
ngx_qjs_ext_ngx_shared(JSContext *cx, JSValueConst this_val)
{
    return JS_NewObjectProtoClass(cx, JS_NULL, NGX_QJS_CLASS_ID_SHARED);
}


static JSValue
ngx_qjs_ext_shared_dict_capacity(JSContext *cx, JSValueConst this_val)
{
    ngx_shm_zone_t  *shm_zone;

    shm_zone = JS_GetOpaque(this_val, NGX_QJS_CLASS_ID_SHARED_DICT);
    if (shm_zone == NULL) {
        return JS_UNDEFINED;
    }

    return JS_NewInt32(cx, shm_zone->shm.size);
}


static JSValue
ngx_qjs_ext_shared_dict_clear(JSContext *cx, JSValueConst this_val,
    int argc, JSValueConst *argv)
{
    ngx_rbtree_t       *rbtree;
    ngx_js_dict_t      *dict;
    ngx_shm_zone_t     *shm_zone;
    ngx_rbtree_node_t  *rn, *next;

    shm_zone = JS_GetOpaque(this_val, NGX_QJS_CLASS_ID_SHARED_DICT);
    if (shm_zone == NULL) {
        return JS_ThrowTypeError(cx, "\"this\" is not a shared dict");
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

    return JS_UNDEFINED;
}


static JSValue
ngx_qjs_ext_shared_dict_delete(JSContext *cx, JSValueConst this_val,
    int argc, JSValueConst *argv)
{
    ngx_str_t        key;
    ngx_shm_zone_t  *shm_zone;

    shm_zone = JS_GetOpaque(this_val, NGX_QJS_CLASS_ID_SHARED_DICT);
    if (shm_zone == NULL) {
        return JS_ThrowTypeError(cx, "\"this\" is not a shared dict");
    }

    if (ngx_qjs_string(cx, argv[0], &key) != NGX_OK) {
        return JS_EXCEPTION;
    }

    return ngx_qjs_dict_delete(cx, shm_zone->data, &key, 0);
}


static JSValue
ngx_qjs_ext_shared_dict_free_space(JSContext *cx, JSValueConst this_val,
    int argc, JSValueConst *argv)
{
    size_t           bytes;
    ngx_js_dict_t   *dict;
    ngx_shm_zone_t  *shm_zone;

    shm_zone = JS_GetOpaque(this_val, NGX_QJS_CLASS_ID_SHARED_DICT);
    if (shm_zone == NULL) {
        return JS_ThrowTypeError(cx, "\"this\" is not a shared dict");
    }

    dict = shm_zone->data;

    ngx_rwlock_rlock(&dict->sh->rwlock);
    bytes = dict->shpool->pfree * ngx_pagesize;
    ngx_rwlock_unlock(&dict->sh->rwlock);

    return JS_NewInt32(cx, bytes);
}


static JSValue
ngx_qjs_ext_shared_dict_get(JSContext *cx, JSValueConst this_val,
    int argc, JSValueConst *argv)
{
    ngx_str_t        key;
    ngx_shm_zone_t  *shm_zone;

    shm_zone = JS_GetOpaque(this_val, NGX_QJS_CLASS_ID_SHARED_DICT);
    if (shm_zone == NULL) {
        return JS_ThrowTypeError(cx, "\"this\" is not a shared dict");
    }

    if (ngx_qjs_string(cx, argv[0], &key) != NGX_OK) {
        return JS_EXCEPTION;
    }

    return ngx_qjs_dict_get(cx, shm_zone->data, &key);
}


static JSValue
ngx_qjs_ext_shared_dict_has(JSContext *cx, JSValueConst this_val,
    int argc, JSValueConst *argv)
{
    ngx_str_t            key;
    ngx_msec_t           now;
    ngx_time_t          *tp;
    ngx_js_dict_t       *dict;
    ngx_shm_zone_t      *shm_zone;
    ngx_js_dict_node_t  *node;

    shm_zone = JS_GetOpaque(this_val, NGX_QJS_CLASS_ID_SHARED_DICT);
    if (shm_zone == NULL) {
        return JS_ThrowTypeError(cx, "\"this\" is not a shared dict");
    }

    if (ngx_qjs_string(cx, argv[0], &key) != NGX_OK) {
        return JS_EXCEPTION;
    }

    dict = shm_zone->data;

    ngx_rwlock_rlock(&dict->sh->rwlock);

    node = ngx_qjs_dict_lookup(dict, &key);

    if (node != NULL && dict->timeout) {
        tp = ngx_timeofday();
        now = tp->sec * 1000 + tp->msec;

        if (now >= node->expire.key) {
            node = NULL;
        }
    }

    ngx_rwlock_unlock(&dict->sh->rwlock);

    return JS_NewBool(cx, node != NULL);
}


static JSValue
ngx_qjs_ext_shared_dict_incr(JSContext *cx, JSValueConst this_val,
    int argc, JSValueConst *argv)
{
    double           delta, init;
    uint32_t         timeout;
    ngx_str_t        key;
    ngx_js_dict_t   *dict;
    ngx_shm_zone_t  *shm_zone;

    shm_zone = JS_GetOpaque(this_val, NGX_QJS_CLASS_ID_SHARED_DICT);
    if (shm_zone == NULL) {
        return JS_ThrowTypeError(cx, "\"this\" is not a shared dict");
    }

    dict = shm_zone->data;

    if (dict->type != NGX_JS_DICT_TYPE_NUMBER) {
        return JS_ThrowTypeError(cx, "shared dict is not a number dict");
    }

    if (ngx_qjs_string(cx, argv[0], &key) != NGX_OK) {
        return JS_EXCEPTION;
    }

    if (JS_ToFloat64(cx, &delta, argv[1]) < 0) {
        return JS_EXCEPTION;
    }

    if (JS_IsUndefined(argv[2])) {
        init = 0;

    } else if (JS_ToFloat64(cx, &init, argv[2]) < 0) {
        return JS_EXCEPTION;
    }

    if (argc > 3) {
        if (JS_ToUint32(cx, &timeout, argv[3]) < 0) {
            return JS_EXCEPTION;
        }

        if (!dict->timeout) {
            return JS_ThrowTypeError(cx,
                                  "shared dict must be declared with timeout");
        }

        if (timeout < 1) {
            return JS_ThrowRangeError(cx,
                                 "timeout must be greater than or equal to 1");
        }

    } else {
        timeout = dict->timeout;
    }

    return ngx_qjs_dict_incr(cx, dict, &key, delta, init, timeout);
}


static JSValue
ngx_qjs_ext_shared_dict_items(JSContext *cx, JSValueConst this_val,
    int argc, JSValueConst *argv)
{
    JSValue              arr, kv, v;
    uint32_t             max_count, i;
    ngx_msec_t           now;
    ngx_time_t          *tp;
    ngx_rbtree_t        *rbtree;
    ngx_js_dict_t       *dict;
    ngx_shm_zone_t      *shm_zone;
    ngx_rbtree_node_t   *rn;
    ngx_js_dict_node_t  *node;

    shm_zone = JS_GetOpaque(this_val, NGX_QJS_CLASS_ID_SHARED_DICT);
    if (shm_zone == NULL) {
        return JS_ThrowTypeError(cx, "\"this\" is not a shared dict");
    }

    dict = shm_zone->data;

    max_count = 1024;

    if (argc > 0) {
        if (JS_ToUint32(cx, &max_count, argv[0]) < 0) {
            return JS_EXCEPTION;
        }
    }

    rbtree = &dict->sh->rbtree;

    ngx_rwlock_rlock(&dict->sh->rwlock);

    if (dict->timeout) {
        tp = ngx_timeofday();
        now = tp->sec * 1000 + tp->msec;
        ngx_js_dict_expire(dict, now);
    }

    if (rbtree->root == rbtree->sentinel) {
        ngx_rwlock_unlock(&dict->sh->rwlock);
        return JS_NewArray(cx);
    }

    arr = JS_NewArray(cx);
    if (JS_IsException(arr)) {
        ngx_rwlock_unlock(&dict->sh->rwlock);
        return JS_EXCEPTION;
    }

    i = 0;

    for (rn = ngx_rbtree_min(rbtree->root, rbtree->sentinel);
         rn != NULL;
         rn = ngx_rbtree_next(rbtree, rn))
    {
        if (max_count-- == 0) {
            break;
        }

        node = (ngx_js_dict_node_t *) rn;

        kv = JS_NewArray(cx);
        if (JS_IsException(kv)) {
            ngx_rwlock_unlock(&dict->sh->rwlock);
            JS_FreeValue(cx, arr);
            return JS_EXCEPTION;
        }

        v = JS_NewStringLen(cx, (const char *) node->sn.str.data,
                            node->sn.str.len);
        if (JS_IsException(v)) {
            ngx_rwlock_unlock(&dict->sh->rwlock);
            JS_FreeValue(cx, kv);
            JS_FreeValue(cx, arr);
            return JS_EXCEPTION;
        }

        if (JS_DefinePropertyValueUint32(cx, kv, 0, v, JS_PROP_C_W_E) < 0) {
            ngx_rwlock_unlock(&dict->sh->rwlock);
            JS_FreeValue(cx, v);
            JS_FreeValue(cx, kv);
            JS_FreeValue(cx, arr);
            return JS_EXCEPTION;
        }

        v = ngx_qjs_dict_copy_value_locked(cx, dict, node);

        if (JS_DefinePropertyValueUint32(cx, kv, 1, v, JS_PROP_C_W_E) < 0) {
            ngx_rwlock_unlock(&dict->sh->rwlock);
            JS_FreeValue(cx, v);
            JS_FreeValue(cx, kv);
            JS_FreeValue(cx, arr);
            return JS_EXCEPTION;
        }

        if (JS_DefinePropertyValueUint32(cx, arr, i++, kv, JS_PROP_C_W_E) < 0) {
            ngx_rwlock_unlock(&dict->sh->rwlock);
            JS_FreeValue(cx, kv);
            JS_FreeValue(cx, arr);
            return JS_EXCEPTION;
        }
    }

    ngx_rwlock_unlock(&dict->sh->rwlock);

    return arr;
}


static JSValue
ngx_qjs_ext_shared_dict_keys(JSContext *cx, JSValueConst this_val, int argc,
    JSValueConst *argv)
{
    JSValue              arr, key;
    uint32_t             max_count, i;
    ngx_msec_t           now;
    ngx_time_t          *tp;
    ngx_rbtree_t        *rbtree;
    ngx_js_dict_t       *dict;
    ngx_shm_zone_t      *shm_zone;
    ngx_rbtree_node_t   *rn;
    ngx_js_dict_node_t  *node;

    shm_zone = JS_GetOpaque(this_val, NGX_QJS_CLASS_ID_SHARED_DICT);
    if (shm_zone == NULL) {
        return JS_ThrowTypeError(cx, "\"this\" is not a shared dict");
    }

    dict = shm_zone->data;

    max_count = 1024;

    if (argc > 0) {
        if (JS_ToUint32(cx, &max_count, argv[0]) < 0) {
            return JS_EXCEPTION;
        }
    }

    rbtree = &dict->sh->rbtree;

    ngx_rwlock_rlock(&dict->sh->rwlock);

    if (dict->timeout) {
        tp = ngx_timeofday();
        now = tp->sec * 1000 + tp->msec;
        ngx_js_dict_expire(dict, now);
    }

    if (rbtree->root == rbtree->sentinel) {
        ngx_rwlock_unlock(&dict->sh->rwlock);
        return JS_NewArray(cx);
    }

    arr = JS_NewArray(cx);
    if (JS_IsException(arr)) {
        ngx_rwlock_unlock(&dict->sh->rwlock);
        return JS_EXCEPTION;
    }

    i = 0;

    for (rn = ngx_rbtree_min(rbtree->root, rbtree->sentinel);
         rn != NULL;
         rn = ngx_rbtree_next(rbtree, rn))
    {
        if (max_count-- == 0) {
            break;
        }

        node = (ngx_js_dict_node_t *) rn;

        key = JS_NewStringLen(cx, (const char *) node->sn.str.data,
                              node->sn.str.len);
        if (JS_IsException(key)) {
            ngx_rwlock_unlock(&dict->sh->rwlock);
            JS_FreeValue(cx, arr);
            return JS_EXCEPTION;
        }

        if (JS_DefinePropertyValueUint32(cx, arr, i++, key,
                                         JS_PROP_C_W_E) < 0)
        {
            ngx_rwlock_unlock(&dict->sh->rwlock);
            JS_FreeValue(cx, key);
            JS_FreeValue(cx, arr);
            return JS_EXCEPTION;
        }
    }

    ngx_rwlock_unlock(&dict->sh->rwlock);

    return arr;
}


static JSValue
ngx_qjs_ext_shared_dict_name(JSContext *cx, JSValueConst this_val)
{
    ngx_shm_zone_t  *shm_zone;

    shm_zone = JS_GetOpaque(this_val, NGX_QJS_CLASS_ID_SHARED_DICT);
    if (shm_zone == NULL) {
        return JS_UNDEFINED;
    }

    return JS_NewStringLen(cx, (const char *) shm_zone->shm.name.data,
                           shm_zone->shm.name.len);
}


static JSValue
ngx_qjs_ext_shared_dict_pop(JSContext *cx, JSValueConst this_val,
    int argc, JSValueConst *argv)
{
    ngx_str_t        key;
    ngx_shm_zone_t  *shm_zone;

    shm_zone = JS_GetOpaque(this_val, NGX_QJS_CLASS_ID_SHARED_DICT);
    if (shm_zone == NULL) {
        return JS_ThrowTypeError(cx, "\"this\" is not a shared dict");
    }

    if (ngx_qjs_string(cx, argv[0], &key) != NGX_OK) {
        return JS_EXCEPTION;
    }

    return ngx_qjs_dict_delete(cx, shm_zone->data, &key, 1);
}


static JSValue
ngx_qjs_ext_shared_dict_set(JSContext *cx, JSValueConst this_val,
    int argc, JSValueConst *argv, int flags)
{
    JSValue          ret;
    uint32_t         timeout;
    ngx_str_t        key;
    ngx_js_dict_t   *dict;
    ngx_shm_zone_t  *shm_zone;

    shm_zone = JS_GetOpaque(this_val, NGX_QJS_CLASS_ID_SHARED_DICT);
    if (shm_zone == NULL) {
        return JS_ThrowTypeError(cx, "\"this\" is not a shared dict");
    }

    if (ngx_qjs_string(cx, argv[0], &key) != NGX_OK) {
        return JS_EXCEPTION;
    }

    dict = shm_zone->data;

    if (dict->type == NGX_JS_DICT_TYPE_STRING) {
        if (!JS_IsString(argv[1])) {
            return JS_ThrowTypeError(cx, "string value is expected");
        }

    } else {
        if (!JS_IsNumber(argv[1])) {
            return JS_ThrowTypeError(cx, "number value is expected");
        }
    }

    if (!JS_IsUndefined(argv[2])) {
        if (!JS_IsNumber(argv[2])) {
            return JS_ThrowTypeError(cx, "timeout is not a number");
        }

        if (!dict->timeout) {
            return JS_ThrowTypeError(cx,
                                "shared dict must be declared with timeout");
        }

        if (JS_ToUint32(cx, &timeout, argv[2]) < 0) {
            return JS_EXCEPTION;
        }

        if (timeout < 1) {
            return JS_ThrowTypeError(cx,
                                "timeout must be greater than or equal to 1");
        }

    } else {
        timeout = dict->timeout;
    }

    ret = ngx_qjs_dict_set(cx, shm_zone->data, &key, argv[1], timeout, flags);
    if (JS_IsException(ret)) {
        return JS_EXCEPTION;
    }

    if (flags) {
        /* add() or replace(). */
        return ret;
    }

    return JS_DupValue(cx, this_val);
}


static JSValue
ngx_qjs_ext_shared_dict_size(JSContext *cx, JSValueConst this_val,
    int argc, JSValueConst *argv)
{
    njs_int_t           items;
    ngx_msec_t          now;
    ngx_time_t         *tp;
    ngx_rbtree_t       *rbtree;
    ngx_js_dict_t      *dict;
    ngx_shm_zone_t     *shm_zone;
    ngx_rbtree_node_t  *rn;

    shm_zone = JS_GetOpaque(this_val, NGX_QJS_CLASS_ID_SHARED_DICT);
    if (shm_zone == NULL) {
        return JS_ThrowTypeError(cx, "\"this\" is not a shared dict");
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
        return JS_NewInt32(cx, 0);
    }

    items = 0;

    for (rn = ngx_rbtree_min(rbtree->root, rbtree->sentinel);
         rn != NULL;
         rn = ngx_rbtree_next(rbtree, rn))
    {
        items++;
    }

    ngx_rwlock_unlock(&dict->sh->rwlock);

    return JS_NewInt32(cx, items);
}


static JSValue
ngx_qjs_ext_shared_dict_type(JSContext *cx, JSValueConst this_val)
{
    ngx_str_t        type;
    ngx_js_dict_t   *dict;
    ngx_shm_zone_t  *shm_zone;

    shm_zone = JS_GetOpaque(this_val, NGX_QJS_CLASS_ID_SHARED_DICT);
    if (shm_zone == NULL) {
        return JS_UNDEFINED;
    }

    dict = shm_zone->data;

    switch (dict->type) {
    case NGX_JS_DICT_TYPE_STRING:
        ngx_str_set(&type, "string");
        break;

    default:
        ngx_str_set(&type, "number");
        break;
    }

    return JS_NewStringLen(cx, (const char *) type.data, type.len);
}


static JSValue
ngx_qjs_dict_copy_value_locked(JSContext *cx, ngx_js_dict_t *dict,
    ngx_js_dict_node_t *node)
{
    if (dict->type == NGX_JS_DICT_TYPE_STRING) {
        return JS_NewStringLen(cx, (const char *) node->u.value.data,
                               node->u.value.len);
    }

    /* NGX_JS_DICT_TYPE_NUMBER */

    return JS_NewFloat64(cx, node->u.number);
}


static ngx_js_dict_node_t *
ngx_qjs_dict_lookup(ngx_js_dict_t *dict, ngx_str_t *key)
{
    uint32_t       hash;
    ngx_rbtree_t  *rbtree;

    rbtree = &dict->sh->rbtree;

    hash = ngx_crc32_long(key->data, key->len);

    return (ngx_js_dict_node_t *) ngx_str_rbtree_lookup(rbtree, key, hash);
}


static ngx_int_t
ngx_qjs_dict_add(JSContext *cx, ngx_js_dict_t *dict, ngx_str_t *key,
    JSValue value, ngx_msec_t timeout, ngx_msec_t now)
{
    size_t               n;
    uint32_t             hash;
    ngx_str_t            string;
    ngx_js_dict_node_t  *node;

    if (dict->timeout) {
        ngx_js_dict_expire(dict, now);
    }

    n = sizeof(ngx_js_dict_node_t) + key->len;
    hash = ngx_crc32_long(key->data, key->len);

    node = ngx_js_dict_alloc(dict, n);
    if (node == NULL) {
        return NGX_ERROR;
    }

    node->sn.str.data = (u_char *) node + sizeof(ngx_js_dict_node_t);

    if (dict->type == NGX_JS_DICT_TYPE_STRING) {
        string.data = (u_char *) JS_ToCStringLen(cx, &string.len, value);
        if (string.data == NULL) {
            ngx_slab_free_locked(dict->shpool, node);
            return NGX_ERROR;
        }

        node->u.value.data = ngx_js_dict_alloc(dict, string.len);
        if (node->u.value.data == NULL) {
            ngx_slab_free_locked(dict->shpool, node);
            JS_FreeCString(cx, (char *) string.data);
            return NGX_ERROR;
        }

        ngx_memcpy(node->u.value.data, string.data, string.len);
        node->u.value.len = string.len;

        JS_FreeCString(cx, (char *) string.data);

    } else {
        if (JS_ToFloat64(cx, &node->u.number, value) < 0) {
            ngx_slab_free_locked(dict->shpool, node);
            return NGX_ERROR;
        }
    }

    node->sn.node.key = hash;

    ngx_memcpy(node->sn.str.data, key->data, key->len);
    node->sn.str.len = key->len;

    ngx_rbtree_insert(&dict->sh->rbtree, &node->sn.node);

    if (dict->timeout) {
        node->expire.key = now + timeout;
        ngx_rbtree_insert(&dict->sh->rbtree_expire, &node->expire);
    }

    return NGX_OK;
}


static JSValue
ngx_qjs_dict_delete(JSContext *cx, ngx_js_dict_t *dict, ngx_str_t *key,
    int retval)
{
    JSValue              ret;
    ngx_msec_t           now;
    ngx_time_t          *tp;
    ngx_js_dict_node_t  *node;

    ngx_rwlock_wlock(&dict->sh->rwlock);

    node = ngx_qjs_dict_lookup(dict, key);

    if (node == NULL) {
        ngx_rwlock_unlock(&dict->sh->rwlock);
        return JS_UNDEFINED;
    }

    if (dict->timeout) {
        ngx_rbtree_delete(&dict->sh->rbtree_expire, &node->expire);
    }

    ngx_rbtree_delete(&dict->sh->rbtree, (ngx_rbtree_node_t *) node);

    if (retval) {
        tp = ngx_timeofday();
        now = tp->sec * 1000 + tp->msec;

        if (!dict->timeout || now < node->expire.key) {
            ret = ngx_qjs_dict_copy_value_locked(cx, dict, node);

        } else {
            ret = JS_UNDEFINED;
        }

    } else {
        ret = JS_TRUE;
    }

    ngx_js_dict_node_free(dict, node);

    ngx_rwlock_unlock(&dict->sh->rwlock);

    return ret;
}


static JSValue
ngx_qjs_dict_get(JSContext *cx, ngx_js_dict_t *dict, ngx_str_t *key)
{
    JSValue              ret;
    ngx_msec_t           now;
    ngx_time_t          *tp;
    ngx_js_dict_node_t  *node;

    ngx_rwlock_rlock(&dict->sh->rwlock);

    node = ngx_qjs_dict_lookup(dict, key);

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

    ret = ngx_qjs_dict_copy_value_locked(cx, dict, node);
    ngx_rwlock_unlock(&dict->sh->rwlock);

    return ret;

not_found:

    ngx_rwlock_unlock(&dict->sh->rwlock);

    return JS_UNDEFINED;
}


static JSValue
ngx_qjs_dict_incr(JSContext *cx, ngx_js_dict_t *dict, ngx_str_t *key,
    double delta, double init, ngx_msec_t timeout)
{
    JSValue              value;
    ngx_msec_t           now;
    ngx_time_t          *tp;
    ngx_js_dict_node_t  *node;

    tp = ngx_timeofday();
    now = tp->sec * 1000 + tp->msec;

    ngx_rwlock_wlock(&dict->sh->rwlock);

    node = ngx_qjs_dict_lookup(dict, key);

    if (node == NULL) {
        value = JS_NewFloat64(cx, init + delta);
        if (ngx_qjs_dict_add(cx, dict, key, value, timeout, now) != NGX_OK) {
            ngx_rwlock_unlock(&dict->sh->rwlock);
            JS_FreeValue(cx, value);
            return ngx_qjs_throw_shared_memory_error(cx);
        }

    } else {
        node->u.number += delta;
        value = JS_NewFloat64(cx, node->u.number);

        if (dict->timeout) {
            ngx_rbtree_delete(&dict->sh->rbtree_expire, &node->expire);
            node->expire.key = now + timeout;
            ngx_rbtree_insert(&dict->sh->rbtree_expire, &node->expire);
        }
    }

    ngx_rwlock_unlock(&dict->sh->rwlock);

    return value;
}


static JSValue
ngx_qjs_dict_set(JSContext *cx, ngx_js_dict_t *dict, ngx_str_t *key,
    JSValue value, ngx_msec_t timeout, unsigned flags)
{
    ngx_msec_t           now;
    ngx_time_t          *tp;
    ngx_js_dict_node_t  *node;

    tp = ngx_timeofday();
    now = tp->sec * 1000 + tp->msec;

    ngx_rwlock_wlock(&dict->sh->rwlock);

    node = ngx_qjs_dict_lookup(dict, key);

    if (node == NULL) {
        if (flags & NGX_JS_DICT_FLAG_MUST_EXIST) {
            ngx_rwlock_unlock(&dict->sh->rwlock);
            return JS_FALSE;
        }

        if (ngx_qjs_dict_add(cx, dict, key, value, timeout, now) != NGX_OK) {
            goto memory_error;
        }

        ngx_rwlock_unlock(&dict->sh->rwlock);

        return JS_TRUE;
    }

    if (flags & NGX_JS_DICT_FLAG_MUST_NOT_EXIST) {
        if (!dict->timeout || now < node->expire.key) {
            ngx_rwlock_unlock(&dict->sh->rwlock);
            return JS_FALSE;
        }
    }

    if (ngx_qjs_dict_update(cx, dict, node, value, timeout, now) != NGX_OK) {
        goto memory_error;
    }

    ngx_rwlock_unlock(&dict->sh->rwlock);

    return JS_TRUE;

memory_error:

    ngx_rwlock_unlock(&dict->sh->rwlock);

    return ngx_qjs_throw_shared_memory_error(cx);
}


static ngx_int_t
ngx_qjs_dict_update(JSContext *cx, ngx_js_dict_t *dict,
    ngx_js_dict_node_t *node, JSValue value, ngx_msec_t timeout, ngx_msec_t now)
{
    u_char     *p;
    ngx_str_t   string;

    if (dict->type == NGX_JS_DICT_TYPE_STRING) {
        string.data = (u_char *) JS_ToCStringLen(cx, &string.len, value);
        if (string.data == NULL) {
            return NGX_ERROR;
        }

        p = ngx_js_dict_alloc(dict, string.len);
        if (p == NULL) {
            JS_FreeCString(cx, (char *) string.data);
            return NGX_ERROR;
        }

        ngx_slab_free_locked(dict->shpool, node->u.value.data);
        ngx_memcpy(p, string.data, string.len);

        node->u.value.data = p;
        node->u.value.len = string.len;

        JS_FreeCString(cx, (char *) string.data);

    } else {
        if (JS_ToFloat64(cx, &node->u.number, value) < 0) {
            return NGX_ERROR;
        }
    }

    if (dict->timeout) {
        ngx_rbtree_delete(&dict->sh->rbtree_expire, &node->expire);
        node->expire.key = now + timeout;
        ngx_rbtree_insert(&dict->sh->rbtree_expire, &node->expire);
    }

    return NGX_OK;
}


static JSValue
ngx_qjs_shared_dict_error_constructor(JSContext *cx, JSValueConst this_val,
    int argc, JSValueConst *argv)
{
    JSValue  proto, error_ctor, result, global_obj;

    global_obj = JS_GetGlobalObject(cx);

    error_ctor = JS_GetPropertyStr(cx, global_obj, "Error");
    if (JS_IsException(error_ctor)) {
        JS_FreeValue(cx, global_obj);
        return error_ctor;
    }

    result = JS_CallConstructor(cx, error_ctor, argc, argv);
    JS_FreeValue(cx, error_ctor);
    JS_FreeValue(cx, global_obj);

    if (!JS_IsException(result)) {
        proto = JS_GetClassProto(cx, NGX_QJS_CLASS_ID_SHARED_DICT_ERROR);
        if (JS_SetPrototype(cx, result, proto) < 0) {
            JS_FreeValue(cx, result);
            JS_FreeValue(cx, proto);
            return JS_EXCEPTION;
        }

        JS_FreeValue(cx, proto);
    }

    return result;
}


static JSValue
ngx_qjs_throw_shared_memory_error(JSContext *cx)
{
    JSValue  ctor, global_obj, err;

    global_obj = JS_GetGlobalObject(cx);

    ctor = JS_GetPropertyStr(cx, global_obj, "SharedMemoryError");
    JS_FreeValue(cx, global_obj);

    if (JS_IsException(ctor)) {
        return ctor;
    }

    err = JS_CallConstructor(cx, ctor, 0, NULL);

    JS_FreeValue(cx, ctor);

    return JS_Throw(cx, err);
}


static JSModuleDef *
ngx_qjs_ngx_shared_dict_init(JSContext *cx, const char *name)
{
    JSValue  global_obj, ngx_obj, proto, error_ctor, error_proto, shared_ctor;

    if (JS_NewClass(JS_GetRuntime(cx), NGX_QJS_CLASS_ID_SHARED,
                    &ngx_qjs_shared_class) < 0)
    {
        return NULL;
    }

    if (JS_NewClass(JS_GetRuntime(cx), NGX_QJS_CLASS_ID_SHARED_DICT,
                    &ngx_qjs_shared_dict_class) < 0)
    {
        return NULL;
    }

    if (JS_NewClass(JS_GetRuntime(cx), NGX_QJS_CLASS_ID_SHARED_DICT_ERROR,
                    &ngx_qjs_shared_dict_error_class) < 0)
    {
        return NULL;
    }

    proto = JS_NewObject(cx);
    if (JS_IsException(proto)) {
        return NULL;
    }

    JS_SetPropertyFunctionList(cx, proto, ngx_qjs_ext_shared_dict,
                               njs_nitems(ngx_qjs_ext_shared_dict));

    JS_SetClassProto(cx, NGX_QJS_CLASS_ID_SHARED_DICT, proto);

    global_obj = JS_GetGlobalObject(cx);

    error_ctor = JS_GetPropertyStr(cx, global_obj, "Error");
    if (JS_IsException(error_ctor)) {
        JS_FreeValue(cx, global_obj);
        return NULL;
    }

    error_proto = JS_GetPropertyStr(cx, error_ctor, "prototype");
    if (JS_IsException(error_proto)) {
        JS_FreeValue(cx, error_ctor);
        JS_FreeValue(cx, global_obj);
        return NULL;
    }

    proto = JS_NewObjectProto(cx, error_proto);

    JS_FreeValue(cx, error_ctor);
    JS_FreeValue(cx, error_proto);

    if (JS_IsException(proto)) {
        JS_FreeValue(cx, global_obj);
        return NULL;
    }

    JS_SetPropertyFunctionList(cx, proto, ngx_qjs_ext_shared_dict_error,
                               njs_nitems(ngx_qjs_ext_shared_dict_error));

    JS_SetClassProto(cx, NGX_QJS_CLASS_ID_SHARED_DICT_ERROR, proto);

    shared_ctor = JS_NewCFunction2(cx, ngx_qjs_shared_dict_error_constructor,
                                   "SharedDictError", 1, JS_CFUNC_constructor,
                                   0);
    if (JS_IsException(shared_ctor)) {
        JS_FreeValue(cx, global_obj);
        return NULL;
    }

    JS_SetConstructor(cx, shared_ctor, proto);

    if (JS_SetPropertyStr(cx, global_obj, "SharedMemoryError", shared_ctor)
        < 0)
    {
        JS_FreeValue(cx, shared_ctor);
        JS_FreeValue(cx, global_obj);
        return NULL;
    }

    ngx_obj = JS_GetPropertyStr(cx, global_obj, "ngx");
    if (JS_IsException(ngx_obj)) {
        JS_FreeValue(cx, global_obj);
        return NULL;
    }

    JS_SetPropertyFunctionList(cx, ngx_obj, ngx_qjs_ext_ngx,
                               njs_nitems(ngx_qjs_ext_ngx));

    JS_FreeValue(cx, ngx_obj);
    JS_FreeValue(cx, global_obj);

    return JS_NewCModule(cx, name, NULL);
}

#endif
