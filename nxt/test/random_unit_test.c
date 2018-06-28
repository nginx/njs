
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) NGINX, Inc.
 */

#include <nxt_auto_config.h>
#include <nxt_types.h>
#include <nxt_clang.h>
#include <nxt_string.h>
#include <nxt_stub.h>
#include <nxt_random.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>


static nxt_int_t
random_unit_test(void)
{
    nxt_uint_t    n;
    nxt_random_t  r;

    nxt_random_init(&r, -1);

    r.count = 400000;

    nxt_random_add(&r, (u_char *) "arc4random", nxt_length("arc4random"));

    /*
     * Test arc4random() numbers.
     * RC4 pseudorandom numbers would be 0x4642AFC3 and 0xBAF0FFF0.
     */

    if (nxt_random(&r) == 0xD6270B27) {

        for (n = 100000; n != 0; n--) {
            (void) nxt_random(&r);
        }

        if (nxt_random(&r) == 0x6FCAE186) {
            printf("random unit test passed\n");

            nxt_random_stir(&r, getpid());

            printf("random unit test: 0x%08X\n", nxt_random(&r));

            return NXT_OK;
        }
    }

    printf("random unit test failed\n");

    return NXT_ERROR;
}


int
main(void)
{
    return random_unit_test();
}
