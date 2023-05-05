
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
#endif


#define njs_bio_new_mem_buf(b, len) BIO_new_mem_buf((void *) b, len)


#if (OPENSSL_VERSION_NUMBER < 0x30000000L && !defined ERR_peek_error_data)
#define ERR_peek_error_data(d, f)    ERR_peek_error_line_data(NULL, NULL, d, f)
#endif


njs_inline int
njs_bn_bn2binpad(const BIGNUM *bn, unsigned char *to, int tolen)
{
#if (OPENSSL_VERSION_NUMBER >= 0x10100000L)
    return BN_bn2binpad(bn, to, tolen);
#else
    int  len;

    len = BN_num_bytes(bn);

    if (tolen > len) {
        memset(to, 0, tolen - len);

    } else if (tolen < len) {
        return -1;
    }

    return BN_bn2bin(bn, &to[tolen - len]);
#endif
}


njs_inline int
njs_pkey_up_ref(EVP_PKEY *pkey)
{
#if (OPENSSL_VERSION_NUMBER >= 0x10100000L)
    return EVP_PKEY_up_ref(pkey);
#else
    CRYPTO_add(&pkey->references, 1, CRYPTO_LOCK_EVP_PKEY);
    return 1;
#endif
}


njs_inline const RSA *
njs_pkey_get_rsa_key(EVP_PKEY *pkey)
{
#if (OPENSSL_VERSION_NUMBER >= 0x10100000L)
    return EVP_PKEY_get0_RSA(pkey);
#else
    return EVP_PKEY_get0(pkey);
#endif
}


njs_inline void
njs_rsa_get0_key(const RSA *rsa, const BIGNUM **n, const BIGNUM **e,
    const BIGNUM **d)
{
#if (OPENSSL_VERSION_NUMBER >= 0x10100000L)
    RSA_get0_key(rsa, n, e, d);
#else
    if (n != NULL) {
        *n = rsa->n;
    }

    if (e != NULL) {
        *e = rsa->e;
    }

    if (d != NULL) {
        *d = rsa->d;
    }
#endif
}


njs_inline void
njs_rsa_get0_factors(const RSA *rsa, const BIGNUM **p, const BIGNUM **q)
{
#if (OPENSSL_VERSION_NUMBER >= 0x10100000L)
    RSA_get0_factors(rsa, p, q);
#else
    if (p != NULL) {
        *p = rsa->p;
    }

    if (q != NULL) {
        *q = rsa->q;
    }
#endif
}



njs_inline void
njs_rsa_get0_ctr_params(const RSA *rsa, const BIGNUM **dp, const BIGNUM **dq,
    const BIGNUM **qi)
{
#if (OPENSSL_VERSION_NUMBER >= 0x10100000L)
    RSA_get0_crt_params(rsa, dp, dq, qi);
#else
    if (dp != NULL) {
        *dp = rsa->dmp1;
    }

    if (dq != NULL) {
        *dq = rsa->dmq1;
    }

    if (qi != NULL) {
        *qi = rsa->iqmp;
    }
#endif
}


njs_inline int
njs_rsa_set0_key(RSA *rsa, BIGNUM *n, BIGNUM *e, BIGNUM *d)
{
#if (OPENSSL_VERSION_NUMBER >= 0x10100000L)
    return RSA_set0_key(rsa, n, e, d);
#else
    if ((rsa->n == NULL && n == NULL) || (rsa->e == NULL && e == NULL)) {
        return 0;
    }

    if (n != NULL) {
        BN_free(rsa->n);
        rsa->n = n;
    }

    if (e != NULL) {
        BN_free(rsa->e);
        rsa->e = e;
    }

    if (d != NULL) {
        BN_clear_free(rsa->d);
        rsa->d = d;
        BN_set_flags(rsa->d, BN_FLG_CONSTTIME);
    }

    return 1;
#endif
}


njs_inline int
njs_rsa_set0_factors(RSA *rsa, BIGNUM *p, BIGNUM *q)
{
#if (OPENSSL_VERSION_NUMBER >= 0x10100000L)
    return RSA_set0_factors(rsa, p, q);
#else
    if ((rsa->p == NULL && p == NULL) || (rsa->q == NULL && q == NULL)) {
        return 0;
    }

    if (p != NULL) {
        BN_clear_free(rsa->p);
        rsa->p = p;
        BN_set_flags(rsa->p, BN_FLG_CONSTTIME);
    }

    if (q != NULL) {
        BN_clear_free(rsa->q);
        rsa->q = q;
        BN_set_flags(rsa->q, BN_FLG_CONSTTIME);
    }

    return 1;
#endif
}


njs_inline int
njs_rsa_set0_ctr_params(RSA *rsa, BIGNUM *dp, BIGNUM *dq, BIGNUM *qi)
{
#if (OPENSSL_VERSION_NUMBER >= 0x10100000L)
    return RSA_set0_crt_params(rsa, dp, dq, qi);
#else
    if ((rsa->dmp1 == NULL && dp == NULL)
        || (rsa->dmq1 == NULL && dq == NULL)
        || (rsa->iqmp == NULL && qi == NULL))
    {
        return 0;
    }

    if (dp != NULL) {
        BN_clear_free(rsa->dmp1);
        rsa->dmp1 = dp;
        BN_set_flags(rsa->dmp1, BN_FLG_CONSTTIME);
    }

    if (dq != NULL) {
        BN_clear_free(rsa->dmq1);
        rsa->dmq1 = dq;
        BN_set_flags(rsa->dmq1, BN_FLG_CONSTTIME);
    }

    if (qi != NULL) {
        BN_clear_free(rsa->iqmp);
        rsa->iqmp = qi;
        BN_set_flags(rsa->iqmp, BN_FLG_CONSTTIME);
    }

    return 1;
#endif
}


njs_inline const EC_KEY *
njs_pkey_get_ec_key(EVP_PKEY *pkey)
{
#if (OPENSSL_VERSION_NUMBER >= 0x10100000L)
    return EVP_PKEY_get0_EC_KEY(pkey);
#else
    if (pkey->type != EVP_PKEY_EC) {
        return NULL;
    }

    return pkey->pkey.ec;
#endif
}


njs_inline int
njs_ec_group_order_bits(const EC_GROUP *group)
{
#if (OPENSSL_VERSION_NUMBER >= 0x10100000L)
    return EC_GROUP_order_bits(group);
#else
    int     bits;
    BIGNUM  *order;

    order = BN_new();
    if (order == NULL) {
        return 0;
    }

    if (EC_GROUP_get_order(group, order, NULL) == 0) {
        return 0;
    }

    bits = BN_num_bits(order);

    BN_free(order);

    return bits;
#endif
}


njs_inline int
njs_ec_point_get_affine_coordinates(const EC_GROUP *group, const EC_POINT *p,
    BIGNUM *x, BIGNUM *y)
{
#if (OPENSSL_VERSION_NUMBER >= 0x10101001L)
    return EC_POINT_get_affine_coordinates(group, p, x, y, NULL);
#else
    return EC_POINT_get_affine_coordinates_GFp(group, p, x, y, NULL);
#endif
}


njs_inline int
njs_ecdsa_sig_set0(ECDSA_SIG *sig, BIGNUM *r, BIGNUM *s)
{
#if (OPENSSL_VERSION_NUMBER >= 0x10100000L)
    return ECDSA_SIG_set0(sig, r, s);
#else
    if (r == NULL || s == NULL) {
        return 0;
    }

    BN_clear_free(sig->r);
    BN_clear_free(sig->s);

    sig->r = r;
    sig->s = s;

    return 1;
#endif
}


#endif /* _NJS_EXTERNAL_OPENSSL_H_INCLUDED_ */
