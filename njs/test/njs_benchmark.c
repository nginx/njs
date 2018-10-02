
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) NGINX, Inc.
 */

#include <njs.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/resource.h>
#include <time.h>


static nxt_int_t
njs_unit_test_benchmark(nxt_str_t *script, nxt_str_t *result, const char *msg,
    nxt_uint_t n)
{
    u_char         *start;
    njs_vm_t       *vm, *nvm;
    uint64_t       us;
    nxt_int_t      ret, rc;
    nxt_str_t      s;
    nxt_uint_t     i;
    nxt_bool_t     success;
    njs_vm_opt_t   options;
    struct rusage  usage;

    nxt_memzero(&options, sizeof(njs_vm_opt_t));

    vm = NULL;
    nvm = NULL;
    rc = NXT_ERROR;

    vm = njs_vm_create(&options);
    if (vm == NULL) {
        printf("njs_vm_create() failed\n");
        goto done;
    }

    start = script->start;

    ret = njs_vm_compile(vm, &start, start + script->length);
    if (ret != NXT_OK) {
        printf("njs_vm_compile() failed\n");
        goto done;
    }

    for (i = 0; i < n; i++) {

        nvm = njs_vm_clone(vm, NULL);
        if (nvm == NULL) {
            printf("njs_vm_clone() failed\n");
            goto done;
        }

        (void) njs_vm_run(nvm);

        if (njs_vm_retval_to_ext_string(nvm, &s) != NXT_OK) {
            printf("njs_vm_retval_to_ext_string() failed\n");
            goto done;
        }

        success = nxt_strstr_eq(result, &s);

        if (!success) {
            printf("failed: \"%.*s\" vs \"%.*s\"\n",
                   (int) result->length, result->start, (int) s.length,
                   s.start);
            goto done;
        }

        njs_vm_destroy(nvm);
        nvm = NULL;
    }

    getrusage(RUSAGE_SELF, &usage);

    us = usage.ru_utime.tv_sec * 1000000 + usage.ru_utime.tv_usec
         + usage.ru_stime.tv_sec * 1000000 + usage.ru_stime.tv_usec;

    if (n == 1) {
        printf("%s: %.3fs\n", msg, (double) us / 1000000);

    } else {
        printf("%s: %.3fµs, %d times/s\n",
               msg, (double) us / n, (int) ((uint64_t) n * 1000000 / us));
    }

    rc = NXT_OK;

done:

    if (nvm != NULL) {
        njs_vm_destroy(nvm);
    }

    if (vm != NULL) {
        njs_vm_destroy(vm);
    }

    return rc;
}


int nxt_cdecl
main(int argc, char **argv)
{
    static nxt_str_t  script = nxt_string("null");
    static nxt_str_t  result = nxt_string("null");

    static nxt_str_t  fibo_number = nxt_string(
        "function fibo(n) {"
        "    if (n > 1)"
        "        return fibo(n - 1) + fibo(n - 2)"
        "    return 1"
        "}"
        "fibo(32)");

    static nxt_str_t  fibo_ascii = nxt_string(
        "function fibo(n) {"
        "    if (n > 1)"
        "        return fibo(n - 1) + fibo(n - 2)"
        "    return '.'"
        "}"
        "fibo(32).length");

    static nxt_str_t  fibo_bytes = nxt_string(
        "var a = '\\x80'.toBytes();"
        "function fibo(n) {"
        "    if (n > 1)"
        "        return fibo(n - 1) + fibo(n - 2)"
        "    return a"
        "}"
        "fibo(32).length");

    static nxt_str_t  fibo_utf8 = nxt_string(
        "function fibo(n) {"
        "    if (n > 1)"
        "        return fibo(n - 1) + fibo(n - 2)"
        "    return 'α'"
        "}"
        "fibo(32).length");

    static nxt_str_t  fibo_result = nxt_string("3524578");


    if (argc > 1) {
        switch (argv[1][0]) {

        case 'v':
            return njs_unit_test_benchmark(&script, &result,
                                           "nJSVM clone/destroy", 1000000);

        case 'n':
            return njs_unit_test_benchmark(&fibo_number, &fibo_result,
                                           "fibobench numbers", 1);

        case 'a':
            return njs_unit_test_benchmark(&fibo_ascii, &fibo_result,
                                           "fibobench ascii strings", 1);

        case 'b':
            return njs_unit_test_benchmark(&fibo_bytes, &fibo_result,
                                           "fibobench byte strings", 1);

        case 'u':
            return njs_unit_test_benchmark(&fibo_utf8, &fibo_result,
                                           "fibobench utf8 strings", 1);
        }
    }

    printf("unknown agrument\n");
    return EXIT_FAILURE;
}
