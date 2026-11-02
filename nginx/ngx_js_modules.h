
/*
 * Copyright (C) Dmitry Volyntsev
 * Copyright (C) F5, Inc.
 */


#ifndef _NGX_JS_MODULES_H_INCLUDED_
#define _NGX_JS_MODULES_H_INCLUDED_


/*
 * Shared addon module list registered by the HTTP and Stream nginx js
 * modules.  When adding a new conditional addon, declare it here under the
 * matching NJS_HAVE_* guard and extend the corresponding NGX_JS_NJS_ADDONS_*
 * or NGX_JS_QJS_ADDONS_* fragment.
 */


#if defined(NJS_HAVE_OPENSSL) && !NJS_HAVE_SHARED
extern njs_module_t  njs_crypto_module;
extern njs_module_t  njs_webcrypto_module;
#define NGX_JS_NJS_ADDONS_OPENSSL   &njs_crypto_module, &njs_webcrypto_module,
#else
#define NGX_JS_NJS_ADDONS_OPENSSL
#endif

#if defined(NJS_HAVE_XML) && !NJS_HAVE_SHARED
extern njs_module_t  njs_xml_module;
#define NGX_JS_NJS_ADDONS_XML       &njs_xml_module,
#else
#define NGX_JS_NJS_ADDONS_XML
#endif

#if defined(NJS_HAVE_ZLIB) && !NJS_HAVE_SHARED
extern njs_module_t  njs_zlib_module;
#define NGX_JS_NJS_ADDONS_ZLIB      &njs_zlib_module,
#else
#define NGX_JS_NJS_ADDONS_ZLIB
#endif


#define NGX_JS_NJS_ADDON_MODULES                                             \
    &ngx_js_ngx_module,                                                      \
    &ngx_js_fetch_module,                                                    \
    &ngx_js_shared_dict_module,                                              \
    NGX_JS_NJS_ADDONS_OPENSSL                                                \
    NGX_JS_NJS_ADDONS_XML                                                    \
    NGX_JS_NJS_ADDONS_ZLIB


#if (NJS_HAVE_QUICKJS)

#if defined(NJS_HAVE_OPENSSL) && !NJS_HAVE_SHARED
extern qjs_module_t  qjs_crypto_module;
extern qjs_module_t  qjs_webcrypto_module;
#define NGX_JS_QJS_ADDONS_OPENSSL   &qjs_crypto_module, &qjs_webcrypto_module,
#else
#define NGX_JS_QJS_ADDONS_OPENSSL
#endif

#if defined(NJS_HAVE_XML) && !NJS_HAVE_SHARED
extern qjs_module_t  qjs_xml_module;
#define NGX_JS_QJS_ADDONS_XML       &qjs_xml_module,
#else
#define NGX_JS_QJS_ADDONS_XML
#endif

#if defined(NJS_HAVE_ZLIB) && !NJS_HAVE_SHARED
extern qjs_module_t  qjs_zlib_module;
#define NGX_JS_QJS_ADDONS_ZLIB      &qjs_zlib_module,
#else
#define NGX_JS_QJS_ADDONS_ZLIB
#endif


#define NGX_JS_QJS_ADDON_MODULES                                             \
    &ngx_qjs_ngx_module,                                                     \
    &ngx_qjs_ngx_shared_dict_module,                                         \
    &ngx_qjs_ngx_fetch_module,                                               \
    NGX_JS_QJS_ADDONS_OPENSSL                                                \
    NGX_JS_QJS_ADDONS_XML                                                    \
    NGX_JS_QJS_ADDONS_ZLIB

#endif /* NJS_HAVE_QUICKJS */


#endif /* _NGX_JS_MODULES_H_INCLUDED_ */
