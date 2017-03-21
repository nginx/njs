
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) NGINX, Inc.
 */


#include <nxt_auto_config.h>
#include <nxt_types.h>
#include <nxt_clang.h>
#include <nxt_random.h>
#include <fcntl.h>
#include <sys/time.h>
#include <unistd.h>
#if (NXT_HAVE_GETRANDOM)
#include <sys/syscall.h>
#include <linux/random.h>
#endif


/*
 * The pseudorandom generator based on OpenBSD arc4random.  Although
 * it is usually stated that arc4random uses RC4 pseudorandom generation
 * algorithm they are actually different in nxt_random_add().
 */


#define NXT_RANDOM_KEY_SIZE  128


nxt_inline uint8_t nxt_random_byte(nxt_random_t *r);


void
nxt_random_init(nxt_random_t *r, nxt_pid_t pid)
{
    nxt_uint_t  i;

    r->count = 0;
    r->pid = pid;
    r->i = 0;
    r->j = 0;

    for (i = 0; i < 256; i++) {
        r->s[i] = i;
    }
}


void
nxt_random_stir(nxt_random_t *r, nxt_pid_t pid)
{
    int             fd;
    ssize_t         n;
    struct timeval  tv;
    union {
        uint32_t    value[3];
        u_char      bytes[NXT_RANDOM_KEY_SIZE];
    } key;

    if (r->pid == 0) {
        nxt_random_init(r, pid);
    }

    r->pid = pid;

    n = 0;

#if (NXT_HAVE_GETRANDOM)

    /* Linux 3.17 getrandom(), it is not available in Glibc. */

    n = syscall(SYS_getrandom, &key, NXT_RANDOM_KEY_SIZE, 0);

#endif

    if (n != NXT_RANDOM_KEY_SIZE) {
        fd = open("/dev/urandom", O_RDONLY);

        if (fd >= 0) {
            n = read(fd, &key, NXT_RANDOM_KEY_SIZE);
            (void) close(fd);
        }
    }

    if (n != NXT_RANDOM_KEY_SIZE) {
        (void) gettimeofday(&tv, NULL);

        /* XOR with stack garbage. */

        key.value[0] ^= tv.tv_usec;
        key.value[1] ^= tv.tv_sec;
        key.value[2] ^= getpid();
    }

    nxt_random_add(r, key.bytes, NXT_RANDOM_KEY_SIZE);

    /* Drop the first 3072 bytes. */
    for (n = 3072; n != 0; n--) {
        (void) nxt_random_byte(r);
    }

    /* Stir again after 1,600,000 bytes. */
    r->count = 400000;
}


void
nxt_random_add(nxt_random_t *r, const u_char *key, uint32_t len)
{
    uint8_t   val;
    uint32_t  n;

    for (n = 0; n < 256; n++) {
        val = r->s[r->i];
        r->j += val + key[n % len];

        r->s[r->i] = r->s[r->j];
        r->s[r->j] = val;

        r->i++;
    }

    /* This index is not decremented in RC4 algorithm. */
    r->i--;

    r->j = r->i;
}


uint32_t
nxt_random(nxt_random_t *r)
{
    uint32_t    val;
    nxt_pid_t   pid;
    nxt_bool_t  new_pid;

    new_pid = 0;
    pid = r->pid;

    if (pid != -1) {
        pid = getpid();

        if (pid != r->pid) {
            new_pid = 1;
        }
    }

    r->count--;

    if (r->count <= 0 || new_pid) {
        nxt_random_stir(r, pid);
    }

    val  = nxt_random_byte(r) << 24;
    val |= nxt_random_byte(r) << 16;
    val |= nxt_random_byte(r) << 8;
    val |= nxt_random_byte(r);

    return val;
}


nxt_inline uint8_t
nxt_random_byte(nxt_random_t *r)
{
    uint8_t  si, sj;

    r->i++;
    si = r->s[r->i];
    r->j += si;

    sj = r->s[r->j];
    r->s[r->i] = sj;
    r->s[r->j] = si;

    si += sj;

    return r->s[si];
}
