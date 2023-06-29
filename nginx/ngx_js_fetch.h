
/*
 * Copyright (C) Dmitry Volyntsev
 * Copyright (C) NGINX, Inc.
 */


#ifndef _NGX_JS_FETCH_H_INCLUDED_
#define _NGX_JS_FETCH_H_INCLUDED_


njs_int_t ngx_js_ext_fetch(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t level, njs_value_t *retval);

extern njs_module_t  ngx_js_fetch_module;


#endif /* _NGX_JS_FETCH_H_INCLUDED_ */
