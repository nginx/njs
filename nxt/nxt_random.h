
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) NGINX, Inc.
 */

#ifndef _NXT_RANDOM_H_INCLUDED_
#define _NXT_RANDOM_H_INCLUDED_


typedef struct {
    int32_t    count;
    nxt_pid_t  pid;
    uint8_t    i;
    uint8_t    j;
    uint8_t    s[256];
} nxt_random_t;


/*
 * The nxt_random_t structure must be either initialized with zeros
 * or initialized by nxt_random_init() function.  The later is intended
 * mainly for unit test.  nxt_random() automatically stirs itself if
 * process pid changed after fork().  This pid testing can be disabled by
 * passing -1 as the pid argument to nxt_random_init() or nxt_random_stir()
 * functions.  The testing can be later enabled by passing any positive
 * number, for example, a real pid number.
 */

NXT_EXPORT void nxt_random_init(nxt_random_t *r, nxt_pid_t pid);
NXT_EXPORT void nxt_random_stir(nxt_random_t *r, nxt_pid_t pid);
NXT_EXPORT void nxt_random_add(nxt_random_t *r, const u_char *key,
    uint32_t len);
NXT_EXPORT uint32_t nxt_random(nxt_random_t *r);


#endif /* _NXT_RANDOM_H_INCLUDED_ */
