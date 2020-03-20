
/*
 * Copyright (C) Dmitry Volyntsev
 * Copyright (C) NGINX, Inc.
 */

#ifndef _NJS_EXTERNALS_TEST_H_INCLUDED_
#define _NJS_EXTERNALS_TEST_H_INCLUDED_


njs_external_proto_t njs_externals_shared_init(njs_vm_t *vm);
njs_int_t njs_externals_init(njs_vm_t *vm, njs_external_proto_t proto);


#endif /* _NJS_EXTERNALS_TEST_H_INCLUDED_ */
