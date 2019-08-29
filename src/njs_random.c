
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) NGINX, Inc.
 */


#include <njs_main.h>
#if (NJS_HAVE_GETRANDOM)
#include <sys/random.h>
#elif (NJS_HAVE_LINUX_SYS_GETRANDOM)
#include <sys/syscall.h>
#include <linux/random.h>
#elif (NJS_HAVE_GETENTROPY_SYS_RANDOM)
#include <sys/random.h>
#endif


/*
 * The pseudorandom generator based on OpenBSD arc4random.  Although
 * it is usually stated that arc4random uses RC4 pseudorandom generation
 * algorithm they are actually different in njs_random_add().
 */


#define NJS_RANDOM_KEY_SIZE  128


njs_inline uint8_t njs_random_byte(njs_random_t *r);


void
njs_random_init(njs_random_t *r, njs_pid_t pid)
{
    njs_uint_t  i;

    r->count = 0;
    r->pid = pid;
    r->i = 0;
    r->j = 0;

    for (i = 0; i < 256; i++) {
        r->s[i] = i;
    }
}


void
njs_random_stir(njs_random_t *r, njs_pid_t pid)
{
    int             fd;
    ssize_t         n;
    struct timeval  tv;
    union {
        uint32_t    value[3];
        u_char      bytes[NJS_RANDOM_KEY_SIZE];
    } key;

    if (r->pid == 0) {
        njs_random_init(r, pid);
    }

    r->pid = pid;

#if (NJS_HAVE_GETRANDOM)

    n = getrandom(&key, NJS_RANDOM_KEY_SIZE, 0);

#elif (NJS_HAVE_LINUX_SYS_GETRANDOM)

    /* Linux 3.17 SYS_getrandom, not available in Glibc prior to 2.25. */

    n = syscall(SYS_getrandom, &key, NJS_RANDOM_KEY_SIZE, 0);

#elif (NJS_HAVE_GETENTROPY || NJS_HAVE_GETENTROPY_SYS_RANDOM)

    n = 0;

    if (getentropy(&key, NJS_RANDOM_KEY_SIZE) == 0) {
        n = NJS_RANDOM_KEY_SIZE;
    }

#else

    n = 0;

#endif

    if (n != NJS_RANDOM_KEY_SIZE) {
        fd = open("/dev/urandom", O_RDONLY);

        if (fd >= 0) {
            n = read(fd, &key, NJS_RANDOM_KEY_SIZE);
            (void) close(fd);
        }
    }

    if (n != NJS_RANDOM_KEY_SIZE) {
        (void) gettimeofday(&tv, NULL);

        /* XOR with stack garbage. */

        key.value[0] ^= tv.tv_usec;
        key.value[1] ^= tv.tv_sec;
        key.value[2] ^= getpid();
    }

    njs_msan_unpoison(&key, NJS_RANDOM_KEY_SIZE);

    njs_random_add(r, key.bytes, NJS_RANDOM_KEY_SIZE);

    /* Drop the first 3072 bytes. */
    for (n = 3072; n != 0; n--) {
        (void) njs_random_byte(r);
    }

    /* Stir again after 1,600,000 bytes. */
    r->count = 400000;
}


void
njs_random_add(njs_random_t *r, const u_char *key, uint32_t len)
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
njs_random(njs_random_t *r)
{
    uint32_t    val;
    njs_pid_t   pid;
    njs_bool_t  new_pid;

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
        njs_random_stir(r, pid);
    }

    val  = (uint32_t) njs_random_byte(r) << 24;
    val |= (uint32_t) njs_random_byte(r) << 16;
    val |= (uint32_t) njs_random_byte(r) << 8;
    val |= (uint32_t) njs_random_byte(r);

    return val;
}


njs_inline uint8_t
njs_random_byte(njs_random_t *r)
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
