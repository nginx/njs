
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) NGINX, Inc.
 */


#include <njs_main.h>


static njs_int_t
random_unit_test(void)
{
    njs_uint_t    n;
    njs_random_t  r;

    njs_random_init(&r, -1);

    r.count = 400000;

    njs_random_add(&r, (u_char *) "arc4random", njs_length("arc4random"));

    /*
     * Test arc4random() numbers.
     * RC4 pseudorandom numbers would be 0x4642AFC3 and 0xBAF0FFF0.
     */

    if (njs_random(&r) == 0xD6270B27) {

        for (n = 100000; n != 0; n--) {
            (void) njs_random(&r);
        }

        if (njs_random(&r) == 0x6FCAE186) {
            njs_printf("random unit test passed\n");

            njs_random_stir(&r, getpid());

            njs_printf("random unit test: 0x%08uXD\n", njs_random(&r));

            return NJS_OK;
        }
    }

    njs_printf("random unit test failed\n");

    return NJS_ERROR;
}


int
main(void)
{
    return random_unit_test();
}
