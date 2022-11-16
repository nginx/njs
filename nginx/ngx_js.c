
/*
 * Copyright (C) Roman Arutyunyan
 * Copyright (C) Dmitry Volyntsev
 * Copyright (C) NGINX, Inc.
 */


#include <ngx_config.h>
#include <ngx_core.h>
#include "ngx_js.h"
#include "ngx_js_fetch.h"


static njs_int_t ngx_js_ext_conf_prefix(njs_vm_t *vm, njs_object_prop_t *prop,
    njs_value_t *value, njs_value_t *setval, njs_value_t *retval);
static void ngx_js_cleanup_vm(void *data);


extern njs_module_t  njs_webcrypto_module;


static njs_external_t  ngx_js_ext_core[] = {

    {
        .flags = NJS_EXTERN_PROPERTY,
        .name.string = njs_str("conf_prefix"),
        .u.property = {
            .handler = ngx_js_ext_conf_prefix,
        }
    },

    {
        .flags = NJS_EXTERN_PROPERTY,
        .name.string = njs_str("ERR"),
        .u.property = {
            .handler = ngx_js_ext_constant,
            .magic32 = NGX_LOG_ERR,
            .magic16 = NGX_JS_NUMBER,
        }
    },

    {
        .flags = NJS_EXTERN_METHOD,
        .name.string = njs_str("fetch"),
        .writable = 1,
        .configurable = 1,
        .enumerable = 1,
        .u.method = {
            .native = ngx_js_ext_fetch,
        }
    },

    {
        .flags = NJS_EXTERN_PROPERTY,
        .name.string = njs_str("INFO"),
        .u.property = {
            .handler = ngx_js_ext_constant,
            .magic32 = NGX_LOG_INFO,
            .magic16 = NGX_JS_NUMBER,
        }
    },

    {
        .flags = NJS_EXTERN_METHOD,
        .name.string = njs_str("log"),
        .writable = 1,
        .configurable = 1,
        .enumerable = 1,
        .u.method = {
            .native = ngx_js_ext_log,
        }
    },

    {
        .flags = NJS_EXTERN_PROPERTY,
        .name.string = njs_str("WARN"),
        .u.property = {
            .handler = ngx_js_ext_constant,
            .magic32 = NGX_LOG_WARN,
            .magic16 = NGX_JS_NUMBER,
        }
    },

};


njs_module_t *njs_js_addon_modules[] = {
    &njs_webcrypto_module,
    NULL,
};


ngx_int_t
ngx_js_call(njs_vm_t *vm, ngx_str_t *fname, ngx_log_t *log,
    njs_opaque_value_t *args, njs_uint_t nargs)
{
    njs_int_t        ret;
    njs_str_t        name;
    ngx_str_t        exception;
    njs_function_t  *func;

    name.start = fname->data;
    name.length = fname->len;

    func = njs_vm_function(vm, &name);
    if (func == NULL) {
        ngx_log_error(NGX_LOG_ERR, log, 0,
                      "js function \"%V\" not found", fname);
        return NGX_ERROR;
    }

    ret = njs_vm_call(vm, func, njs_value_arg(args), nargs);
    if (ret == NJS_ERROR) {
        ngx_js_retval(vm, NULL, &exception);

        ngx_log_error(NGX_LOG_ERR, log, 0,
                      "js exception: %V", &exception);

        return NGX_ERROR;
    }

    ret = njs_vm_run(vm);
    if (ret == NJS_ERROR) {
        ngx_js_retval(vm, NULL, &exception);

        ngx_log_error(NGX_LOG_ERR, log, 0,
                      "js exception: %V", &exception);

        return NGX_ERROR;
    }

    return (ret == NJS_AGAIN) ? NGX_AGAIN : NGX_OK;
}


ngx_int_t
ngx_js_retval(njs_vm_t *vm, njs_opaque_value_t *retval, ngx_str_t *s)
{
    njs_int_t  ret;
    njs_str_t  str;

    if (retval != NULL && njs_value_is_valid(njs_value_arg(retval))) {
        ret = njs_vm_value_string(vm, &str, njs_value_arg(retval));

    } else {
        ret = njs_vm_retval_string(vm, &str);
    }

    if (ret != NJS_OK) {
        return NGX_ERROR;
    }

    s->data = str.start;
    s->len = str.length;

    return NGX_OK;
}


ngx_int_t
ngx_js_integer(njs_vm_t *vm, njs_value_t *value, ngx_int_t *n)
{
    if (!njs_value_is_valid_number(value)) {
        njs_vm_error(vm, "is not a number");
        return NGX_ERROR;
    }

    *n = njs_value_number(value);

    return NGX_OK;
}


ngx_int_t
ngx_js_string(njs_vm_t *vm, njs_value_t *value, njs_str_t *str)
{
    if (value != NULL && !njs_value_is_null_or_undefined(value)) {
        if (njs_vm_value_to_bytes(vm, str, value) == NJS_ERROR) {
            return NGX_ERROR;
        }

    } else {
        str->start = NULL;
        str->length = 0;
    }

    return NGX_OK;
}


ngx_int_t
ngx_js_core_init(njs_vm_t *vm, ngx_log_t *log)
{
    ngx_int_t           rc;
    njs_int_t           ret, proto_id;
    njs_str_t           name;
    njs_opaque_value_t  value;

    rc = ngx_js_fetch_init(vm, log);
    if (rc != NGX_OK) {
        return NGX_ERROR;
    }

    proto_id = njs_vm_external_prototype(vm, ngx_js_ext_core,
                                         njs_nitems(ngx_js_ext_core));
    if (proto_id < 0) {
        ngx_log_error(NGX_LOG_EMERG, log, 0, "failed to add js core proto");
        return NGX_ERROR;
    }

    ret = njs_vm_external_create(vm, njs_value_arg(&value), proto_id, NULL, 1);
    if (njs_slow_path(ret != NJS_OK)) {
        ngx_log_error(NGX_LOG_EMERG, log, 0,
                      "njs_vm_external_create() failed\n");
        return NGX_ERROR;
    }

    name.length = 3;
    name.start = (u_char *) "ngx";

    ret = njs_vm_bind(vm, &name, njs_value_arg(&value), 1);
    if (njs_slow_path(ret != NJS_OK)) {
        ngx_log_error(NGX_LOG_EMERG, log, 0, "njs_vm_bind() failed\n");
        return NGX_ERROR;
    }

    return NGX_OK;
}


njs_int_t
ngx_js_ext_string(njs_vm_t *vm, njs_object_prop_t *prop, njs_value_t *value,
    njs_value_t *setval, njs_value_t *retval)
{
    char       *p;
    ngx_str_t  *field;

    p = njs_vm_external(vm, NJS_PROTO_ID_ANY, value);
    if (p == NULL) {
        njs_value_undefined_set(retval);
        return NJS_DECLINED;
    }

    field = (ngx_str_t *) (p + njs_vm_prop_magic32(prop));

    return njs_vm_value_string_set(vm, retval, field->data, field->len);
}


njs_int_t
ngx_js_ext_uint(njs_vm_t *vm, njs_object_prop_t *prop, njs_value_t *value,
    njs_value_t *setval, njs_value_t *retval)
{
    char        *p;
    ngx_uint_t   field;

    p = njs_vm_external(vm, NJS_PROTO_ID_ANY, value);
    if (p == NULL) {
        njs_value_undefined_set(retval);
        return NJS_DECLINED;
    }

    field = *(ngx_uint_t *) (p + njs_vm_prop_magic32(prop));

    njs_value_number_set(retval, field);

    return NJS_OK;
}


njs_int_t
ngx_js_ext_constant(njs_vm_t *vm, njs_object_prop_t *prop,
    njs_value_t *value, njs_value_t *setval, njs_value_t *retval)
{
    uint32_t  magic32;

    magic32 = njs_vm_prop_magic32(prop);

    switch (njs_vm_prop_magic16(prop)) {
    case NGX_JS_NUMBER:
        njs_value_number_set(retval, magic32);
        break;

    case NGX_JS_BOOLEAN:
    default:
        njs_value_boolean_set(retval, magic32);
        break;
    }

    return NJS_OK;
}


njs_int_t
ngx_js_ext_flags(njs_vm_t *vm, njs_object_prop_t *prop,
    njs_value_t *value, njs_value_t *setval, njs_value_t *retval)
{
    uintptr_t  data;

    data = (uintptr_t) njs_vm_external(vm, NJS_PROTO_ID_ANY, value);
    if (data == 0) {
        njs_value_undefined_set(retval);
        return NJS_DECLINED;
    }

    data = data & (uintptr_t) njs_vm_prop_magic32(prop);

    switch (njs_vm_prop_magic16(prop)) {
    case NGX_JS_BOOLEAN:
    default:
        njs_value_boolean_set(retval, data);
        break;
    }

    return NJS_OK;
}


njs_int_t
ngx_js_ext_conf_prefix(njs_vm_t *vm, njs_object_prop_t *prop,
    njs_value_t *value, njs_value_t *setval, njs_value_t *retval)
{
    return njs_vm_value_string_set(vm, retval, ngx_cycle->conf_prefix.data,
                                   ngx_cycle->conf_prefix.len);
}


njs_int_t
ngx_js_ext_log(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t level)
{
    char                *p;
    ngx_int_t            lvl;
    njs_str_t            msg;
    njs_value_t         *value;
    ngx_connection_t    *c;
    ngx_log_handler_pt   handler;

    p = njs_vm_external(vm, NJS_PROTO_ID_ANY, njs_argument(args, 0));
    if (p == NULL) {
        njs_vm_error(vm, "\"this\" is not an external");
        return NJS_ERROR;
    }

    value =  njs_arg(args, nargs, (level != 0) ? 1 : 2);

    if (level == 0) {
        if (ngx_js_integer(vm, njs_arg(args, nargs, 1), &lvl) != NGX_OK) {
            return NJS_ERROR;
        }

        level = lvl;
    }

    if (ngx_js_string(vm, value, &msg) != NGX_OK) {
        return NJS_ERROR;
    }

    c = ngx_external_connection(vm, p);
    handler = c->log->handler;
    c->log->handler = NULL;

    ngx_log_error((ngx_uint_t) level, c->log, 0, "js: %*s",
                  msg.length, msg.start);

    c->log->handler = handler;

    njs_value_undefined_set(njs_vm_retval(vm));

    return NJS_OK;
}


void
ngx_js_logger(njs_vm_t *vm, njs_external_ptr_t external, njs_log_level_t level,
    const u_char *start, size_t length)
{
    ngx_connection_t    *c;
    ngx_log_handler_pt   handler;

    c = ngx_external_connection(vm, external);
    handler = c->log->handler;
    c->log->handler = NULL;

    ngx_log_error((ngx_uint_t) level, c->log, 0, "js: %*s", length, start);

    c->log->handler = handler;
}


char *
ngx_js_import(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    ngx_js_conf_t *jscf = conf;

    u_char               *p, *end, c;
    ngx_int_t             from;
    ngx_str_t            *value, name, path;
    ngx_js_named_path_t  *import;

    value = cf->args->elts;
    from = (cf->args->nelts == 4);

    if (from) {
        if (ngx_strcmp(value[2].data, "from") != 0) {
            ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                               "invalid parameter \"%V\"", &value[2]);
            return NGX_CONF_ERROR;
        }
    }

    name = value[1];
    path = (from ? value[3] : value[1]);

    if (!from) {
        end = name.data + name.len;

        for (p = end - 1; p >= name.data; p--) {
            if (*p == '/') {
                break;
            }
        }

        name.data = p + 1;
        name.len = end - p - 1;

        if (name.len < 3
            || ngx_memcmp(&name.data[name.len - 3], ".js", 3) != 0)
        {
            ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                               "cannot extract export name from file path "
                               "\"%V\", use extended \"from\" syntax", &path);
            return NGX_CONF_ERROR;
        }

        name.len -= 3;
    }

    if (name.len == 0) {
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0, "empty export name");
        return NGX_CONF_ERROR;
    }

    p = name.data;
    end = name.data + name.len;

    while (p < end) {
        c = ngx_tolower(*p);

        if (*p != '_' && (c < 'a' || c > 'z')) {
            if (p == name.data) {
                ngx_conf_log_error(NGX_LOG_EMERG, cf, 0, "cannot start "
                                   "with \"%c\" in export name \"%V\"", *p,
                                   &name);
                return NGX_CONF_ERROR;
            }

            if (*p < '0' || *p > '9') {
                ngx_conf_log_error(NGX_LOG_EMERG, cf, 0, "invalid character "
                                   "\"%c\" in export name \"%V\"", *p,
                                   &name);
                return NGX_CONF_ERROR;
            }
        }

        p++;
    }

    if (ngx_strchr(path.data, '\'') != NULL) {
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0, "invalid character \"'\" "
                           "in file path \"%V\"", &path);
        return NGX_CONF_ERROR;
    }

    if (jscf->imports == NGX_CONF_UNSET_PTR) {
        jscf->imports = ngx_array_create(cf->pool, 4,
                                         sizeof(ngx_js_named_path_t));
        if (jscf->imports == NULL) {
            return NGX_CONF_ERROR;
        }
    }

    import = ngx_array_push(jscf->imports);
    if (import == NULL) {
        return NGX_CONF_ERROR;
    }

    import->name = name;
    import->path = path;
    import->file = cf->conf_file->file.name.data;
    import->line = cf->conf_file->line;

    return NGX_CONF_OK;
}


char *
ngx_js_preload_object(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    ngx_js_conf_t    *jscf = conf;

    u_char               *p, *end, c;
    ngx_int_t             from;
    ngx_str_t            *value, name, path;
    ngx_js_named_path_t  *preload;

    value = cf->args->elts;
    from = (cf->args->nelts == 4);

    if (from) {
        if (ngx_strcmp(value[2].data, "from") != 0) {
            ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                               "invalid parameter \"%V\"", &value[2]);
            return NGX_CONF_ERROR;
        }
    }

    name = value[1];
    path = (from ? value[3] : value[1]);

    if (!from) {
        end = name.data + name.len;

        for (p = end - 1; p >= name.data; p--) {
            if (*p == '/') {
                break;
            }
        }

        name.data = p + 1;
        name.len = end - p - 1;

        if (name.len < 5
            || ngx_memcmp(&name.data[name.len - 5], ".json", 5) != 0)
        {
            ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                               "cannot extract export name from file path "
                               "\"%V\", use extended \"from\" syntax", &path);
            return NGX_CONF_ERROR;
        }

        name.len -= 5;
    }

    if (name.len == 0) {
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0, "empty global name");
        return NGX_CONF_ERROR;
    }

    p = name.data;
    end = name.data + name.len;

    while (p < end) {
        c = ngx_tolower(*p);

        if (*p != '_' && (c < 'a' || c > 'z')) {
            if (p == name.data) {
                ngx_conf_log_error(NGX_LOG_EMERG, cf, 0, "cannot start "
                                   "with \"%c\" in global name \"%V\"", *p,
                                   &name);
                return NGX_CONF_ERROR;
            }

            if (*p < '0' || *p > '9' || *p == '.') {
                ngx_conf_log_error(NGX_LOG_EMERG, cf, 0, "invalid character "
                                   "\"%c\" in global name \"%V\"", *p,
                                   &name);
                return NGX_CONF_ERROR;
            }
        }

        p++;
    }

    if (ngx_strchr(path.data, '\'') != NULL) {
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0, "invalid character \"'\" "
                           "in file path \"%V\"", &path);
        return NGX_CONF_ERROR;
    }

    if (jscf->preload_objects == NGX_CONF_UNSET_PTR) {
        jscf->preload_objects = ngx_array_create(cf->pool, 4,
                                         sizeof(ngx_js_named_path_t));
        if (jscf->preload_objects == NULL) {
            return NGX_CONF_ERROR;
        }
    }

    preload = ngx_array_push(jscf->preload_objects);
    if (preload == NULL) {
        return NGX_CONF_ERROR;
    }

    preload->name = name;
    preload->path = path;
    preload->file = cf->conf_file->file.name.data;
    preload->line = cf->conf_file->line;

    return NGX_CONF_OK;
}


ngx_int_t
ngx_js_init_preload_vm(ngx_conf_t *cf, ngx_js_conf_t *conf)
{
    u_char               *p, *start;
    size_t                size;
    njs_vm_t             *vm;
    njs_int_t             ret;
    ngx_uint_t            i;
    njs_vm_opt_t          options;
    ngx_js_named_path_t  *preload;

    njs_vm_opt_init(&options);

    options.init = 1;

    vm = njs_vm_create(&options);
    if (vm == NULL) {
        goto error;
    }

    ret = ngx_js_core_init(vm, cf->log);
    if (njs_slow_path(ret != NJS_OK)) {
        goto error;
    }

    njs_str_t str = njs_str(
        "import fs from 'fs';"

        "let g = (function (np, no, nf, nsp, r) {"
            "return function (n, p) {"
                "p = (p[0] == '/') ? p : ngx.conf_prefix + p;"
                "let o = r(p);"
                "globalThis[n] = np("
                    "o,"
                    "function (k, v)  {"
                        "if (v instanceof no) {"
                            "nf(nsp(v, null));"
                        "}"
                        "return v;"
                    "}"
                ");"
                "return;"
            "}"
        "})(JSON.parse,Object,Object.freeze,"
           "Object.setPrototypeOf,fs.readFileSync);\n"
    );

    size = str.length;

    preload = conf->preload_objects->elts;
    for (i = 0; i < conf->preload_objects->nelts; i++) {
        size += sizeof("g('','');\n") - 1 + preload[i].name.len
                + preload[i].path.len;
    }

    start = ngx_pnalloc(cf->pool, size);
    if (start == NULL) {
        return NGX_ERROR;
    }

    p = ngx_cpymem(start, str.start, str.length);

    preload = conf->preload_objects->elts;
    for (i = 0; i < conf->preload_objects->nelts; i++) {
       p = ngx_cpymem(p, "g('", sizeof("g('") - 1);
       p = ngx_cpymem(p, preload[i].name.data, preload[i].name.len);
       p = ngx_cpymem(p, "','", sizeof("','") - 1);
       p = ngx_cpymem(p, preload[i].path.data, preload[i].path.len);
       p = ngx_cpymem(p, "');\n", sizeof("');\n") - 1);
    }

    ret = njs_vm_compile(vm, &start,  start + size);
    if (ret != NJS_OK) {
        goto error;
    }

    ret = njs_vm_start(vm);
    if (ret != NJS_OK) {
        goto error;
    }

    conf->preload_vm = vm;

    return NGX_OK;

error:

    if (vm != NULL) {
        njs_vm_destroy(vm);
    }

    return NGX_ERROR;
}


ngx_int_t
ngx_js_merge_vm(ngx_conf_t *cf, ngx_js_conf_t *conf,
    ngx_js_conf_t *prev,
    ngx_int_t (*init_vm) (ngx_conf_t *cf, ngx_js_conf_t *conf))
{
    ngx_str_t            *path, *s;
    ngx_uint_t            i;
    ngx_array_t          *imports, *preload_objects, *paths;
    ngx_js_named_path_t  *import, *pi, *pij, *preload;

    if (prev->imports != NGX_CONF_UNSET_PTR && prev->vm == NULL) {
        if (init_vm(cf, (ngx_js_conf_t *) prev) != NGX_OK) {
            return NGX_ERROR;
        }
    }

    if (conf->imports == NGX_CONF_UNSET_PTR
        && conf->paths == NGX_CONF_UNSET_PTR
        && conf->preload_objects == NGX_CONF_UNSET_PTR)
    {
        if (prev->vm != NULL) {
            conf->preload_objects = prev->preload_objects;
            conf->imports = prev->imports;
            conf->paths = prev->paths;
            conf->vm = prev->vm;

            conf->preload_vm = prev->preload_vm;

            return NGX_OK;
        }
    }

    if (prev->preload_objects != NGX_CONF_UNSET_PTR) {
        if (conf->preload_objects == NGX_CONF_UNSET_PTR) {
            conf->preload_objects = prev->preload_objects;

        } else {
            preload_objects = ngx_array_create(cf->pool, 4,
                                       sizeof(ngx_js_named_path_t));
            if (preload_objects == NULL) {
                return NGX_ERROR;
            }

            pij = prev->preload_objects->elts;

            for (i = 0; i < prev->preload_objects->nelts; i++) {
                preload = ngx_array_push(preload_objects);
                if (preload == NULL) {
                    return NGX_ERROR;
                }

                *preload = pij[i];
            }

            pij = conf->preload_objects->elts;

            for (i = 0; i < conf->preload_objects->nelts; i++) {
                preload = ngx_array_push(preload_objects);
                if (preload == NULL) {
                    return NGX_ERROR;
                }

                *preload = pij[i];
            }

            conf->preload_objects = preload_objects;
        }
    }

    if (prev->imports != NGX_CONF_UNSET_PTR) {
        if (conf->imports == NGX_CONF_UNSET_PTR) {
            conf->imports = prev->imports;

        } else {
            imports = ngx_array_create(cf->pool, 4,
                                       sizeof(ngx_js_named_path_t));
            if (imports == NULL) {
                return NGX_ERROR;
            }

            pi = prev->imports->elts;

            for (i = 0; i < prev->imports->nelts; i++) {
                import = ngx_array_push(imports);
                if (import == NULL) {
                    return NGX_ERROR;
                }

                *import = pi[i];
            }

            pi = conf->imports->elts;

            for (i = 0; i < conf->imports->nelts; i++) {
                import = ngx_array_push(imports);
                if (import == NULL) {
                    return NGX_ERROR;
                }

                *import = pi[i];
            }

            conf->imports = imports;
        }
    }

    if (prev->paths != NGX_CONF_UNSET_PTR) {
        if (conf->paths == NGX_CONF_UNSET_PTR) {
            conf->paths = prev->paths;

        } else {
            paths = ngx_array_create(cf->pool, 4, sizeof(ngx_str_t));
            if (paths == NULL) {
                return NGX_ERROR;
            }

            s = prev->imports->elts;

            for (i = 0; i < prev->paths->nelts; i++) {
                path = ngx_array_push(paths);
                if (path == NULL) {
                    return NGX_ERROR;
                }

                *path = s[i];
            }

            s = conf->imports->elts;

            for (i = 0; i < conf->paths->nelts; i++) {
                path = ngx_array_push(paths);
                if (path == NULL) {
                    return NGX_ERROR;
                }

                *path = s[i];
            }

            conf->paths = paths;
        }
    }

    if (conf->imports == NGX_CONF_UNSET_PTR) {
        return NGX_OK;
    }

    return init_vm(cf, (ngx_js_conf_t *) conf);
}


ngx_int_t
ngx_js_init_conf_vm(ngx_conf_t *cf, ngx_js_conf_t *conf,
    njs_vm_opt_t *options,
    ngx_int_t (*externals_init)(ngx_conf_t *cf, ngx_js_conf_t *conf))
{
    size_t                size;
    u_char               *start, *end, *p;
    ngx_str_t            *m, file;
    njs_int_t             rc;
    njs_str_t             text, path;
    ngx_uint_t            i;
    njs_value_t          *value;
    ngx_pool_cleanup_t   *cln;
    njs_opaque_value_t    lvalue, exception;
    ngx_js_named_path_t  *import;

    static const njs_str_t line_number_key = njs_str("lineNumber");
    static const njs_str_t file_name_key = njs_str("fileName");

    if (conf->preload_objects != NGX_CONF_UNSET_PTR) {
       if (ngx_js_init_preload_vm(cf, (ngx_js_conf_t *)conf) != NGX_OK) {
           return NGX_ERROR;
       }
    }

    size = 0;

    import = conf->imports->elts;
    for (i = 0; i < conf->imports->nelts; i++) {

        /* import <name> from '<path>'; globalThis.<name> = <name>; */

        size += sizeof("import  from '';") - 1 + import[i].name.len * 3
                + import[i].path.len
                + sizeof(" globalThis. = ;\n") - 1;
    }

    start = ngx_pnalloc(cf->pool, size);
    if (start == NULL) {
        return NGX_ERROR;
    }

    p = start;
    import = conf->imports->elts;
    for (i = 0; i < conf->imports->nelts; i++) {

        /* import <name> from '<path>'; globalThis.<name> = <name>; */

        p = ngx_cpymem(p, "import ", sizeof("import ") - 1);
        p = ngx_cpymem(p, import[i].name.data, import[i].name.len);
        p = ngx_cpymem(p, " from '", sizeof(" from '") - 1);
        p = ngx_cpymem(p, import[i].path.data, import[i].path.len);
        p = ngx_cpymem(p, "'; globalThis.", sizeof("'; globalThis.") - 1);
        p = ngx_cpymem(p, import[i].name.data, import[i].name.len);
        p = ngx_cpymem(p, " = ", sizeof(" = ") - 1);
        p = ngx_cpymem(p, import[i].name.data, import[i].name.len);
        p = ngx_cpymem(p, ";\n", sizeof(";\n") - 1);
    }

    file = ngx_cycle->conf_prefix;

    options->file.start = file.data;
    options->file.length = file.len;

    conf->vm = njs_vm_create(options);
    if (conf->vm == NULL) {
        ngx_log_error(NGX_LOG_EMERG, cf->log, 0, "failed to create js VM");
        return NGX_ERROR;
    }

    cln = ngx_pool_cleanup_add(cf->pool, 0);
    if (cln == NULL) {
        return NGX_ERROR;
    }

    cln->handler = ngx_js_cleanup_vm;
    cln->data = conf;

    path.start = ngx_cycle->conf_prefix.data;
    path.length = ngx_cycle->conf_prefix.len;

    rc = njs_vm_add_path(conf->vm, &path);
    if (rc != NJS_OK) {
        ngx_log_error(NGX_LOG_EMERG, cf->log, 0, "failed to add \"js_path\"");
        return NGX_ERROR;
    }

    if (conf->paths != NGX_CONF_UNSET_PTR) {
        m = conf->paths->elts;

        for (i = 0; i < conf->paths->nelts; i++) {
            if (ngx_conf_full_name(cf->cycle, &m[i], 1) != NGX_OK) {
                return NGX_ERROR;
            }

            path.start = m[i].data;
            path.length = m[i].len;

            rc = njs_vm_add_path(conf->vm, &path);
            if (rc != NJS_OK) {
                ngx_log_error(NGX_LOG_EMERG, cf->log, 0,
                              "failed to add \"js_path\"");
                return NGX_ERROR;
            }
        }
    }

    /*
     * Core prototypes must be inited before externals_init() because
     * the core prototype ids have to be identical in all the modules.
     */

    rc = ngx_js_core_init(conf->vm, cf->log);
    if (njs_slow_path(rc != NJS_OK)) {
        return NGX_ERROR;
    }

    rc = externals_init(cf, conf);
    if (rc != NGX_OK) {
        return NGX_ERROR;
    }

    end = start + size;

    rc = njs_vm_compile(conf->vm, &start, end);

    if (rc != NJS_OK) {
        njs_value_assign(&exception, njs_vm_retval(conf->vm));
        njs_vm_retval_string(conf->vm, &text);

        value = njs_vm_object_prop(conf->vm, njs_value_arg(&exception),
                                   &file_name_key, &lvalue);
        if (value == NULL) {
            value = njs_vm_object_prop(conf->vm, njs_value_arg(&exception),
                                       &line_number_key, &lvalue);

            if (value != NULL) {
                i = njs_value_number(value) - 1;

                if (i < conf->imports->nelts) {
                    import = conf->imports->elts;
                    ngx_log_error(NGX_LOG_EMERG, cf->log, 0,
                                  "%*s, included in %s:%ui", text.length,
                                  text.start, import[i].file, import[i].line);
                    return NGX_ERROR;
                }
            }
        }

        ngx_log_error(NGX_LOG_EMERG, cf->log, 0, "%*s", text.length,
                      text.start);
        return NGX_ERROR;
    }

    if (start != end) {
        ngx_log_error(NGX_LOG_EMERG, cf->log, 0,
                      "extra characters in js script: \"%*s\"",
                      end - start, start);
        return NGX_ERROR;
    }

    return NGX_OK;
}


static void
ngx_js_cleanup_vm(void *data)
{
    ngx_js_conf_t  *jscf = data;

    njs_vm_destroy(jscf->vm);

    if (jscf->preload_objects != NGX_CONF_UNSET_PTR) {
        njs_vm_destroy(jscf->preload_vm);
    }
}


ngx_js_conf_t *
ngx_js_create_conf(ngx_conf_t *cf, size_t size)
{
    ngx_js_conf_t  *conf;

    conf = ngx_pcalloc(cf->pool, size);
    if (conf == NULL) {
        return NULL;
    }

    conf->paths = NGX_CONF_UNSET_PTR;
    conf->imports = NGX_CONF_UNSET_PTR;
    conf->preload_objects = NGX_CONF_UNSET_PTR;

    conf->buffer_size = NGX_CONF_UNSET_SIZE;
    conf->max_response_body_size = NGX_CONF_UNSET_SIZE;
    conf->timeout = NGX_CONF_UNSET_MSEC;

    return conf;
}


#if defined(NGX_HTTP_SSL) || defined(NGX_STREAM_SSL)

static char *
ngx_js_set_ssl(ngx_conf_t *cf, ngx_js_conf_t *conf)
{
    ngx_ssl_t           *ssl;
    ngx_pool_cleanup_t  *cln;

    ssl = ngx_pcalloc(cf->pool, sizeof(ngx_ssl_t));
    if (ssl == NULL) {
        return NGX_CONF_ERROR;
    }

    conf->ssl = ssl;
    ssl->log = cf->log;

    if (ngx_ssl_create(ssl, conf->ssl_protocols, NULL) != NGX_OK) {
        return NGX_CONF_ERROR;
    }

    cln = ngx_pool_cleanup_add(cf->pool, 0);
    if (cln == NULL) {
        ngx_ssl_cleanup_ctx(ssl);
        return NGX_CONF_ERROR;
    }

    cln->handler = ngx_ssl_cleanup_ctx;
    cln->data = ssl;

    if (ngx_ssl_ciphers(NULL, ssl, &conf->ssl_ciphers, 0) != NGX_OK) {
        return NGX_CONF_ERROR;
    }

    if (ngx_ssl_trusted_certificate(cf, ssl, &conf->ssl_trusted_certificate,
                                    conf->ssl_verify_depth)
        != NGX_OK)
    {
        return NGX_CONF_ERROR;
    }

    return NGX_CONF_OK;
}

#endif


char *
ngx_js_merge_conf(ngx_conf_t *cf, void *parent, void *child,
  ngx_int_t (*init_vm)(ngx_conf_t *cf, ngx_js_conf_t *conf))
{
    ngx_js_conf_t *prev = parent;
    ngx_js_conf_t *conf = child;

    ngx_conf_merge_msec_value(conf->timeout, prev->timeout, 60000);
    ngx_conf_merge_size_value(conf->buffer_size, prev->buffer_size, 16384);
    ngx_conf_merge_size_value(conf->max_response_body_size,
                              prev->max_response_body_size, 1048576);

    if (ngx_js_merge_vm(cf, (ngx_js_conf_t *) conf, (ngx_js_conf_t *) prev,
                        init_vm)
        != NGX_OK)
    {
        return NGX_CONF_ERROR;
    }

#if defined(NGX_HTTP_SSL) || defined(NGX_STREAM_SSL)
    ngx_conf_merge_str_value(conf->ssl_ciphers, prev->ssl_ciphers,
                             "DEFAULT");

    ngx_conf_merge_bitmask_value(conf->ssl_protocols, prev->ssl_protocols,
                             (NGX_CONF_BITMASK_SET|NGX_SSL_TLSv1
                              |NGX_SSL_TLSv1_1|NGX_SSL_TLSv1_2));

    ngx_conf_merge_value(conf->ssl_verify, prev->ssl_verify, 1);
    ngx_conf_merge_value(conf->ssl_verify_depth, prev->ssl_verify_depth,
                         100);

    ngx_conf_merge_str_value(conf->ssl_trusted_certificate,
                         prev->ssl_trusted_certificate, "");

    return ngx_js_set_ssl(cf, conf);
#else
    return NGX_CONF_OK;
#endif
}
