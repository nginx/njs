
/*
 * Copyright (C) Dmitry Volyntsev
 * Copyright (C) NGINX, Inc.
 */

#ifndef _NJS_CRYPTO_H_INCLUDED_
#define _NJS_CRYPTO_H_INCLUDED_

njs_ret_t njs_hash_constructor(njs_vm_t *vm, njs_value_t *args,
    nxt_uint_t nargs, njs_index_t unused);
njs_ret_t njs_hmac_constructor(njs_vm_t *vm, njs_value_t *args,
    nxt_uint_t nargs, njs_index_t unused);

extern const njs_object_init_t  njs_crypto_object_init;

extern const njs_object_init_t  njs_hash_prototype_init;
extern const njs_object_init_t  njs_hmac_prototype_init;

extern const njs_object_init_t  njs_hash_constructor_init;
extern const njs_object_init_t  njs_hmac_constructor_init;


#endif /* _NJS_CRYPTO_H_INCLUDED_ */
