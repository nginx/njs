
/*
 * Copyright (C) Dmitry Volyntsev
 * Copyright (C) F5, Inc.
 */


#ifndef _NGX_JS_SHARED_ARRAY_H_INCLUDED_
#define _NGX_JS_SHARED_ARRAY_H_INCLUDED_


#include <ngx_config.h>
#include <ngx_core.h>
#include "ngx_js.h"


njs_int_t njs_js_ext_global_shared_array_prop(njs_vm_t *vm,
    njs_object_prop_t *prop, uint32_t atom_id, njs_value_t *value,
    njs_value_t *setval, njs_value_t *retval);
njs_int_t njs_js_ext_global_shared_array_keys(njs_vm_t *vm,
    njs_value_t *unused, njs_value_t *keys);

char *ngx_js_shared_array_zone(ngx_conf_t *cf, ngx_command_t *cmd,
    void *conf, void *tag);

extern njs_module_t  ngx_js_shared_array_module;


#endif /* _NGX_JS_SHARED_ARRAY_H_INCLUDED_ */
