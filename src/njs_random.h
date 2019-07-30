
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) NGINX, Inc.
 */

#ifndef _NJS_RANDOM_H_INCLUDED_
#define _NJS_RANDOM_H_INCLUDED_


typedef struct {
    int32_t    count;
    njs_pid_t  pid;
    uint8_t    i;
    uint8_t    j;
    uint8_t    s[256];
} njs_random_t;


/*
 * The njs_random_t structure must be either initialized with zeros
 * or initialized by njs_random_init() function.  The later is intended
 * mainly for unit test.  njs_random() automatically stirs itself if
 * process pid changed after fork().  This pid testing can be disabled by
 * passing -1 as the pid argument to njs_random_init() or njs_random_stir()
 * functions.  The testing can be later enabled by passing any positive
 * number, for example, a real pid number.
 */

NJS_EXPORT void njs_random_init(njs_random_t *r, njs_pid_t pid);
NJS_EXPORT void njs_random_stir(njs_random_t *r, njs_pid_t pid);
NJS_EXPORT void njs_random_add(njs_random_t *r, const u_char *key,
    uint32_t len);
NJS_EXPORT uint32_t njs_random(njs_random_t *r);


#endif /* _NJS_RANDOM_H_INCLUDED_ */
