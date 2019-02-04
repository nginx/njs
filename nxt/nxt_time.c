
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) NGINX, Inc.
 */

#include <nxt_auto_config.h>
#include <nxt_types.h>
#include <nxt_clang.h>
#include <nxt_time.h>

#include <time.h>
#include <sys/time.h>

uint64_t
nxt_time(void)
{
#if (NXT_HAVE_CLOCK_MONOTONIC)
    struct timespec ts;

    clock_gettime(CLOCK_MONOTONIC, &ts);

    return (uint64_t) ts.tv_sec * 1000000000 + ts.tv_nsec;
#else
    struct timeval tv;

    gettimeofday(&tv, NULL);

    return (uint64_t) tv.tv_sec * 1000000000 + tv.tv_usec * 1000;
#endif
}
