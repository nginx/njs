
/*
 * Copyright (C) Dmitry Volyntsev
 * Copyright (C) F5, Inc.
 */


#include <ngx_config.h>
#include <ngx_core.h>
#include "ngx_js.h"


typedef struct {
    ngx_atomic_t           rwlock;
} ngx_js_shared_array_sh_t;


struct ngx_js_shared_array_s {
    ngx_shm_zone_t            *shm_zone;
    ngx_js_shared_array_sh_t  *sh;
    void                      *data;
    size_t                     size;

    ngx_js_shared_array_t     *next;
};


njs_int_t njs_js_ext_global_shared_array_prop(njs_vm_t *vm,
    njs_object_prop_t *prop, uint32_t atom_id, njs_value_t *value,
    njs_value_t *setval, njs_value_t *retval);
njs_int_t njs_js_ext_global_shared_array_keys(njs_vm_t *vm,
    njs_value_t *unused, njs_value_t *keys);

static njs_int_t njs_js_ext_shared_array_buffer(njs_vm_t *vm,
    njs_object_prop_t *prop, uint32_t unused, njs_value_t *value,
    njs_value_t *setval, njs_value_t *retval);
static njs_int_t njs_js_ext_shared_array_rw_lock(njs_vm_t *vm,
    njs_value_t *args, njs_uint_t nargs, njs_index_t write,
    njs_value_t *retval);

static ngx_int_t ngx_js_shared_array_init_zone(ngx_shm_zone_t *shm_zone,
    void *data);

#if (NJS_HAVE_QUICKJS)
static int ngx_qjs_shared_array_own_property(JSContext *cx,
    JSPropertyDescriptor *pdesc, JSValueConst obj, JSAtom prop);
static int ngx_qjs_shared_array_own_property_names(JSContext *cx,
    JSPropertyEnum **ptab, uint32_t *plen, JSValueConst obj);
static JSValue ngx_qjs_ext_ngx_shared_array(JSContext *cx,
    JSValueConst this_val);
static JSValue ngx_qjs_ext_shared_array_buffer(JSContext *cx,
    JSValueConst this_val);
static JSValue ngx_qjs_ext_shared_array_rw_lock(JSContext *cx,
    JSValueConst this_val, int argc, JSValueConst *argv, int magic);
static JSModuleDef *ngx_qjs_ngx_shared_array_init(JSContext *cx,
    const char *name);


static const JSCFunctionListEntry ngx_qjs_ext_shared_array[] = {
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "SharedArray",
                       JS_PROP_CONFIGURABLE),
    JS_CGETSET_DEF("buffer", ngx_qjs_ext_shared_array_buffer, NULL),
    JS_CFUNC_MAGIC_DEF("readLock", 1, ngx_qjs_ext_shared_array_rw_lock, 0),
    JS_CFUNC_MAGIC_DEF("writeLock", 1, ngx_qjs_ext_shared_array_rw_lock, 1),
};

static JSClassDef ngx_qjs_shared_array_instance_class = {
    "SharedArray",
    .finalizer = NULL,
};

static JSClassDef ngx_qjs_shared_array_exotic_class = {
    "SharedArrayExotic",
    .finalizer = NULL,
    .exotic = &(JSClassExoticMethods) {
        .get_own_property = ngx_qjs_shared_array_own_property,
        .get_own_property_names = ngx_qjs_shared_array_own_property_names,
    },
};


static const JSCFunctionListEntry ngx_qjs_ext_ngx_shared_array_props[] = {
    JS_CGETSET_DEF("sharedArray", ngx_qjs_ext_ngx_shared_array, NULL),
};


qjs_module_t  ngx_qjs_ngx_shared_array_module = {
    .name = "shared_array",
    .init = ngx_qjs_ngx_shared_array_init,
};

#endif /* NJS_HAVE_QUICKJS */


static njs_int_t    ngx_js_shared_array_proto_id;


static njs_external_t  ngx_js_ext_shared_array[] = {

    {
        .flags = NJS_EXTERN_PROPERTY | NJS_EXTERN_SYMBOL,
        .name.symbol = NJS_SYMBOL_TO_STRING_TAG,
        .u.property = {
            .value = "SharedArray",
        }
    },

    {
        .flags = NJS_EXTERN_PROPERTY,
        .name.string = njs_str("buffer"),
        .enumerable = 1,
        .u.property = {
            .handler = njs_js_ext_shared_array_buffer,
        }
    },

    {
        .flags = NJS_EXTERN_METHOD,
        .name.string = njs_str("readLock"),
        .writable = 1,
        .configurable = 1,
        .enumerable = 1,
        .u.method = {
            .native = njs_js_ext_shared_array_rw_lock,
        }
    },

    {
        .flags = NJS_EXTERN_METHOD,
        .name.string = njs_str("writeLock"),
        .writable = 1,
        .configurable = 1,
        .enumerable = 1,
        .u.method = {
            .native = njs_js_ext_shared_array_rw_lock,
            .magic8 = 1,
        }
    },

};


njs_int_t
njs_js_ext_global_shared_array_prop(njs_vm_t *vm, njs_object_prop_t *prop,
    uint32_t atom_id, njs_value_t *value, njs_value_t *setval,
    njs_value_t *retval)
{
    njs_int_t               ret;
    njs_str_t               name;
    ngx_shm_zone_t         *shm_zone;
    ngx_js_main_conf_t     *conf;
    ngx_js_shared_array_t  *array;

    ret = njs_vm_prop_name(vm, atom_id, &name);
    if (ret != NJS_OK) {
        return NJS_ERROR;
    }

    conf = ngx_main_conf(vm);

    for (array = conf->shared_arrays; array != NULL; array = array->next) {
        shm_zone = array->shm_zone;

        if (shm_zone->shm.name.len == name.length
            && ngx_strncmp(shm_zone->shm.name.data, name.start, name.length)
               == 0)
        {
            ret = njs_vm_external_create(vm, retval,
                                         ngx_js_shared_array_proto_id,
                                         array, 0);
            if (ret != NJS_OK) {
                njs_vm_internal_error(vm, "SharedArray creation failed");
                return NJS_ERROR;
            }

            return NJS_OK;
        }
    }

    njs_value_null_set(retval);

    return NJS_DECLINED;
}


njs_int_t
njs_js_ext_global_shared_array_keys(njs_vm_t *vm, njs_value_t *unused,
    njs_value_t *keys)
{
    njs_int_t               rc;
    njs_value_t            *value;
    ngx_shm_zone_t         *shm_zone;
    ngx_js_main_conf_t     *conf;
    ngx_js_shared_array_t  *array;

    conf = ngx_main_conf(vm);

    rc = njs_vm_array_alloc(vm, keys, 4);
    if (rc != NJS_OK) {
        return NJS_ERROR;
    }

    for (array = conf->shared_arrays; array != NULL; array = array->next) {
        shm_zone = array->shm_zone;

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
njs_js_ext_shared_array_buffer(njs_vm_t *vm, njs_object_prop_t *prop,
    uint32_t unused, njs_value_t *value, njs_value_t *setval,
    njs_value_t *retval)
{
    njs_int_t               ret;
    ngx_js_shared_array_t  *array;

    array = njs_vm_external(vm, ngx_js_shared_array_proto_id,
                            njs_value_arg(value));
    if (array == NULL) {
        njs_vm_type_error(vm, "\"this\" is not a shared array");
        return NJS_ERROR;
    }

    ret = njs_vm_value_array_buffer_set2(vm, retval, array->data,
                                         array->size, 1);
    if (ret != NJS_OK) {
        njs_vm_internal_error(vm, "SharedArrayBuffer creation failed");
        return NJS_ERROR;
    }

    return NJS_OK;
}


static njs_int_t
njs_js_ext_shared_array_rw_lock(njs_vm_t *vm, njs_value_t *args,
    njs_uint_t nargs, njs_index_t write, njs_value_t *retval)
{
    njs_int_t               ret;
    ngx_js_shared_array_t  *array;
    njs_opaque_value_t      arguments[2];

    array = njs_vm_external(vm, ngx_js_shared_array_proto_id,
                            njs_argument(args, 0));
    if (array == NULL) {
        njs_vm_type_error(vm, "\"this\" is not a shared array");
        return NJS_ERROR;
    }

    if (!njs_value_is_function(njs_arg(args, nargs, 1))) {
        njs_vm_type_error(vm, "callback is not a function");
        return NJS_ERROR;
    }

    ret = njs_vm_value_array_buffer_set2(vm, njs_value_arg(&arguments[0]),
                                         array->data, array->size, 1);
    if (ret != NJS_OK) {
        njs_vm_internal_error(vm, "SharedArrayBuffer creation failed");
        return NJS_ERROR;
    }

    if (write) {
        ngx_rwlock_wlock(&array->sh->rwlock);

    } else {
        ngx_rwlock_rlock(&array->sh->rwlock);
    }

    ret = njs_vm_invoke(vm, njs_value_function(njs_argument(args, 1)),
                        njs_value_arg(arguments), 1, retval);

    ngx_rwlock_unlock(&array->sh->rwlock);

    return ret;
}


char *
ngx_js_shared_array_zone(ngx_conf_t *cf, ngx_command_t *cmd, void *conf,
    void *tag)
{
    ngx_js_main_conf_t     *jmcf = conf;

    ssize_t                 size;
    ngx_str_t              *value, name, s;
    ngx_uint_t              i;
    ngx_shm_zone_t         *shm_zone;
    ngx_js_shared_array_t  *array;

    value = cf->args->elts;

    if (ngx_strncmp(value[1].data, "zone=", 5) != 0) {
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                          "invalid parameter \"%V\"", &value[1]);
        return NGX_CONF_ERROR;
    }

    name.data = value[1].data + 5;
    name.len = 0;

    for (i = 5; i < value[1].len; i++) {
        if (value[1].data[i] == ':') {
            name.len = i - 5;
            break;
        }
    }

    if (name.len == 0) {
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                          "invalid zone name in \"%V\"", &value[1]);
        return NGX_CONF_ERROR;
    }

    s.data = value[1].data + 5 + name.len + 1;
    s.len = value[1].len - (5 + name.len + 1);

    size = ngx_parse_size(&s);
    if (size == NGX_ERROR) {
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                          "invalid zone size in \"%V\"", &value[1]);
        return NGX_CONF_ERROR;
    }

    if (size < (ssize_t) (8 *ngx_pagesize)) {
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                          "zone \"%V\" is too small", &name);
        return NGX_CONF_ERROR;
    }

    array = ngx_pcalloc(cf->pool, sizeof(ngx_js_shared_array_t));
    if (array == NULL) {
        return NGX_CONF_ERROR;
    }

    size = ngx_align(size, sizeof(uint64_t));
    array->size = size;

    size += ngx_align(sizeof(ngx_slab_pool_t), sizeof(uint64_t));
    size += ngx_align(sizeof(ngx_js_shared_array_sh_t), sizeof(uint64_t));

    shm_zone = ngx_shared_memory_add(cf, &name, size, tag);
    if (shm_zone == NULL) {
        return NGX_CONF_ERROR;
    }

    if (shm_zone->data) {
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                          "duplicate zone \"%V\"", &name);
        return NGX_CONF_ERROR;
    }

    array->shm_zone = shm_zone;
    array->next = jmcf->shared_arrays;
    jmcf->shared_arrays = array;

    shm_zone->data = array;
    shm_zone->init = ngx_js_shared_array_init_zone;

    return NGX_CONF_OK;
}


static ngx_int_t
ngx_js_shared_array_init_zone(ngx_shm_zone_t *shm_zone, void *data)
{
    ngx_js_shared_array_t  *prev = data;

    ngx_js_shared_array_t     *array;
    ngx_js_shared_array_sh_t  *sh;

    array = shm_zone->data;

    if (prev) {
        array->sh = prev->sh;
        array->data = prev->data;
        array->size = prev->size;
        return NGX_OK;
    }

    sh = (ngx_js_shared_array_sh_t *)
          ngx_align_ptr((u_char *) shm_zone->shm.addr + sizeof(ngx_slab_pool_t),
                        sizeof(uint64_t));
    array->sh = sh;

    /* array->data aligned to uint64_t is required for atomic operations. */

    array->data =
        ngx_align_ptr((u_char *) sh + sizeof(ngx_js_shared_array_sh_t),
                      sizeof(uint64_t));

    ngx_memzero(array->sh, sizeof(ngx_js_shared_array_sh_t));
    ngx_memzero(array->data, array->size);

    return NGX_OK;
}


#if (NJS_HAVE_QUICKJS)

static int
ngx_qjs_shared_array_own_property(JSContext *cx, JSPropertyDescriptor *pdesc,
    JSValueConst obj, JSAtom prop)
{
    int                     ret;
    ngx_str_t               name;
    ngx_shm_zone_t         *shm_zone;
    ngx_js_main_conf_t     *conf;
    ngx_js_shared_array_t  *array;

    name.data = (u_char *) JS_AtomToCString(cx, prop);
    if (name.data == NULL) {
        return -1;
    }

    name.len = ngx_strlen(name.data);
    ret = 0;
    conf = ngx_qjs_main_conf(cx);

    for (array = conf->shared_arrays; array != NULL; array = array->next) {
        shm_zone = array->shm_zone;

        if (shm_zone->shm.name.len == name.len
            && ngx_strncmp(shm_zone->shm.name.data, name.data, name.len) == 0)
        {
            if (pdesc != NULL) {
                pdesc->flags = JS_PROP_ENUMERABLE;
                pdesc->getter = JS_UNDEFINED;
                pdesc->setter = JS_UNDEFINED;

                pdesc->value = JS_NewObjectClass(cx,
                                        NGX_QJS_CLASS_ID_SHARED_ARRAY_INSTANCE);
                if (JS_IsException(pdesc->value)) {
                    ret = -1;
                    break;
                }

                JS_SetOpaque(pdesc->value, array);
            }

            ret = 1;
            break;
        }
    }

    JS_FreeCString(cx, (char *) name.data);
    return ret;
}


static int
ngx_qjs_shared_array_own_property_names(JSContext *cx, JSPropertyEnum **ptab,
    uint32_t *plen, JSValueConst obj)
{
    int                     ret;
    JSAtom                  key;
    JSValue                 keys;
    ngx_shm_zone_t         *shm_zone;
    ngx_js_main_conf_t     *conf;
    ngx_js_shared_array_t  *array;

    keys = JS_NewObject(cx);
    if (JS_IsException(keys)) {
        return -1;
    }

    conf = ngx_qjs_main_conf(cx);

    for (array = conf->shared_arrays; array != NULL; array = array->next) {
        shm_zone = array->shm_zone;

        key = JS_NewAtomLen(cx, (const char *) shm_zone->shm.name.data,
                            shm_zone->shm.name.len);
        if (key == JS_ATOM_NULL) {
            return -1;
        }

        if (JS_DefinePropertyValue(cx, keys, key, JS_UNDEFINED,
                                   JS_PROP_ENUMERABLE) < 0)
        {
            return -1;
        }
    }

    ret = JS_GetOwnPropertyNames(cx, ptab, plen, keys,
                                 JS_GPN_STRING_MASK | JS_GPN_ENUM_ONLY);

    JS_FreeValue(cx, keys);
    return ret;
}


static JSValue
ngx_qjs_ext_shared_array_buffer(JSContext *cx, JSValueConst this_val)
{
    ngx_js_shared_array_t  *array;

    array = JS_GetOpaque(this_val, NGX_QJS_CLASS_ID_SHARED_ARRAY_INSTANCE);
    if (array == NULL) {
        return JS_ThrowTypeError(cx, "\"this\" is not a shared array");
    }

    return JS_NewArrayBuffer(cx, array->data, array->size, NULL, NULL, 1);
}


static JSValue
ngx_qjs_ext_shared_array_rw_lock(JSContext *cx, JSValueConst this_val,
    int argc, JSValueConst *argv, int magic)
{
    JSValue                 buffer, result, exception;
    ngx_js_shared_array_t  *array;

    array = JS_GetOpaque(this_val, NGX_QJS_CLASS_ID_SHARED_ARRAY_INSTANCE);
    if (array == NULL) {
        return JS_ThrowTypeError(cx, "\"this\" is not a shared array");
    }

    if (!JS_IsFunction(cx, argv[0])) {
        return JS_ThrowTypeError(cx, "callback is not a function");
    }

    buffer = JS_NewArrayBuffer(cx, array->data, array->size, NULL, NULL, 1);
    if (JS_IsException(buffer)) {
        return JS_EXCEPTION;
    }

    if (magic) {
        ngx_rwlock_wlock(&array->sh->rwlock);

    } else {
        ngx_rwlock_rlock(&array->sh->rwlock);
    }

    result = JS_Call(cx, argv[0], JS_UNDEFINED, 1, &buffer);

    ngx_rwlock_unlock(&array->sh->rwlock);

    JS_FreeValue(cx, buffer);

    if (JS_IsException(result)) {
        exception = JS_GetException(cx);
        JS_Throw(cx, exception);
        return JS_EXCEPTION;
    }

    return result;
}


static JSValue
ngx_qjs_ext_ngx_shared_array(JSContext *cx, JSValueConst this_val)
{
    return JS_NewObjectProtoClass(cx, JS_NULL,
                                  NGX_QJS_CLASS_ID_SHARED_ARRAY_EXOTIC);
}


static JSModuleDef *
ngx_qjs_ngx_shared_array_init(JSContext *cx, const char *name)
{
    JSValue  global_obj, ngx_obj, proto;

    if (JS_NewClass(JS_GetRuntime(cx), NGX_QJS_CLASS_ID_SHARED_ARRAY_INSTANCE,
                    &ngx_qjs_shared_array_instance_class) < 0)
    {
        return NULL;
    }

    proto = JS_NewObject(cx);
    if (JS_IsException(proto)) {
        return NULL;
    }

    JS_SetPropertyFunctionList(cx, proto, ngx_qjs_ext_shared_array,
                               njs_nitems(ngx_qjs_ext_shared_array));

    JS_SetClassProto(cx, NGX_QJS_CLASS_ID_SHARED_ARRAY_INSTANCE, proto);

    if (JS_NewClass(JS_GetRuntime(cx), NGX_QJS_CLASS_ID_SHARED_ARRAY_EXOTIC,
                    &ngx_qjs_shared_array_exotic_class) < 0)
    {
        return NULL;
    }

    global_obj = JS_GetGlobalObject(cx);
    if (JS_IsException(global_obj)) {
        return NULL;
    }

    ngx_obj = JS_GetPropertyStr(cx, global_obj, "ngx");
    JS_FreeValue(cx, global_obj);

    if (JS_IsException(ngx_obj)) {
        return NULL;
    }

    JS_SetPropertyFunctionList(cx, ngx_obj,
                               ngx_qjs_ext_ngx_shared_array_props,
                               njs_nitems(ngx_qjs_ext_ngx_shared_array_props));

    JS_FreeValue(cx, ngx_obj);

    return JS_NewCModule(cx, name, NULL);
}

#endif /* NJS_HAVE_QUICKJS */


static njs_int_t
ngx_js_shared_array_init(njs_vm_t *vm)
{
    ngx_js_shared_array_proto_id = njs_vm_external_prototype(vm,
                                          ngx_js_ext_shared_array,
                                          njs_nitems(ngx_js_ext_shared_array));
    if (ngx_js_shared_array_proto_id < 0) {
        return NJS_ERROR;
    }

    return NJS_OK;
}


njs_module_t  ngx_js_shared_array_module = {
    .name = njs_str("shared_array"),
    .preinit = NULL,
    .init = ngx_js_shared_array_init,
};
