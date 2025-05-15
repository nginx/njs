
/*
 * Copyright (C) Dmitry Volyntsev
 * Copyright (C) NGINX, Inc.
 */


#ifndef _NGX_JS_SHARED_DICT_H_INCLUDED_
#define _NGX_JS_SHARED_DICT_H_INCLUDED_

njs_int_t njs_js_ext_global_shared_prop(njs_vm_t *vm, njs_object_prop_t *prop,
    uint32_t atom_id, njs_value_t *value, njs_value_t *setval,
    njs_value_t *retval);
njs_int_t njs_js_ext_global_shared_keys(njs_vm_t *vm, njs_value_t *value,
    njs_value_t *keys);
ngx_int_t ngx_js_dict_init_worker(ngx_js_main_conf_t *jmcf);

extern njs_module_t  ngx_js_shared_dict_module;


#endif /* _NGX_JS_SHARED_DICT_H_INCLUDED_ */
