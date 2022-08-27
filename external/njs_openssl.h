
/*
 * Copyright (C) Dmitry Volyntsev
 * Copyright (C) NGINX, Inc.
 */


#ifndef _NJS_EXTERNAL_OPENSSL_H_INCLUDED_
#define _NJS_EXTERNAL_OPENSSL_H_INCLUDED_


#define OPENSSL_SUPPRESS_DEPRECATED

#include <openssl/bn.h>
#include <openssl/bio.h>
#include <openssl/x509.h>
#include <openssl/evp.h>
#include <openssl/aes.h>
#include <openssl/rsa.h>
#include <openssl/err.h>
#include <openssl/rand.h>
#include <openssl/hmac.h>
#include <openssl/crypto.h>

#ifdef EVP_PKEY_HKDF
#include <openssl/kdf.h>
#endif


#if (defined LIBRESSL_VERSION_NUMBER && OPENSSL_VERSION_NUMBER == 0x20000000L)
#undef OPENSSL_VERSION_NUMBER
#if (LIBRESSL_VERSION_NUMBER >= 0x2080000fL)
#define OPENSSL_VERSION_NUMBER  0x1010000fL
#else
#define OPENSSL_VERSION_NUMBER  0x1000107fL
#endif
#endif


#if (OPENSSL_VERSION_NUMBER >= 0x10100000L)
#define njs_evp_md_ctx_new()  EVP_MD_CTX_new()
#define njs_evp_md_ctx_free(_ctx)  EVP_MD_CTX_free(_ctx)
#else
#define njs_evp_md_ctx_new()  EVP_MD_CTX_create()
#define njs_evp_md_ctx_free(_ctx)  EVP_MD_CTX_destroy(_ctx)
#define ECDSA_SIG_get0_s(sig) (sig)->s
#define ECDSA_SIG_get0_r(sig) (sig)->r
#endif


#define njs_bio_new_mem_buf(b, len) BIO_new_mem_buf((void *) b, len)


#if (OPENSSL_VERSION_NUMBER < 0x30000000L && !defined ERR_peek_error_data)
#define ERR_peek_error_data(d, f)    ERR_peek_error_line_data(NULL, NULL, d, f)
#endif


#endif /* _NJS_EXTERNAL_OPENSSL_H_INCLUDED_ */
