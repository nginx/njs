
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) NGINX, Inc.
 */

#include <njs.h>

#include <string.h>
#include <stdlib.h>
#include <sys/resource.h>
#include <time.h>


static njs_int_t
njs_unit_test_benchmark(njs_str_t *script, njs_str_t *result, const char *msg,
    njs_uint_t n)
{
    u_char         *start;
    njs_vm_t       *vm, *nvm;
    uint64_t       us;
    njs_int_t      ret;
    njs_str_t      s;
    njs_uint_t     i;
    njs_bool_t     success;
    njs_vm_opt_t   options;
    struct rusage  usage;

    njs_memzero(&options, sizeof(njs_vm_opt_t));

    vm = NULL;
    nvm = NULL;
    ret = NJS_ERROR;

    vm = njs_vm_create(&options);
    if (vm == NULL) {
        njs_printf("njs_vm_create() failed\n");
        goto done;
    }

    start = script->start;

    ret = njs_vm_compile(vm, &start, start + script->length);
    if (ret != NJS_OK) {
        njs_printf("njs_vm_compile() failed\n");
        goto done;
    }

    for (i = 0; i < n; i++) {

        nvm = njs_vm_clone(vm, NULL);
        if (nvm == NULL) {
            njs_printf("njs_vm_clone() failed\n");
            goto done;
        }

        (void) njs_vm_start(nvm);

        if (njs_vm_retval_string(nvm, &s) != NJS_OK) {
            njs_printf("njs_vm_retval_string() failed\n");
            goto done;
        }

        success = njs_strstr_eq(result, &s);

        if (!success) {
            njs_printf("failed: \"%V\" vs \"%V\"\n", result, &s);
            goto done;
        }

        njs_vm_destroy(nvm);
        nvm = NULL;
    }

    getrusage(RUSAGE_SELF, &usage);

    us = usage.ru_utime.tv_sec * 1000000 + usage.ru_utime.tv_usec
         + usage.ru_stime.tv_sec * 1000000 + usage.ru_stime.tv_usec;

    if (n == 1) {
        njs_printf("%s: %.3fs\n", msg, (double) us / 1000000);

    } else {
        njs_printf("%s: %.3fµs, %d times/s\n",
                   msg, (double) us / n, (int) ((uint64_t) n * 1000000 / us));
    }

    ret = NJS_OK;

done:

    if (nvm != NULL) {
        njs_vm_destroy(nvm);
    }

    if (vm != NULL) {
        njs_vm_destroy(vm);
    }

    return ret;
}


int njs_cdecl
main(int argc, char **argv)
{
    static njs_str_t  script = njs_str("null");
    static njs_str_t  result = njs_str("null");

    static njs_str_t  fibo_number = njs_str(
        "function fibo(n) {"
        "    if (n > 1)"
        "        return fibo(n - 1) + fibo(n - 2);"
        "    return 1"
        "}"
        "fibo(32)");

    static njs_str_t  fibo_ascii = njs_str(
        "function fibo(n) {"
        "    if (n > 1)"
        "        return fibo(n - 1) + fibo(n - 2);"
        "    return '.'"
        "}"
        "fibo(32).length");

    static njs_str_t  fibo_bytes = njs_str(
        "var a = '\\x80'.toBytes();"
        "function fibo(n) {"
        "    if (n > 1)"
        "        return fibo(n - 1) + fibo(n - 2);"
        "    return a"
        "}"
        "fibo(32).length");

    static njs_str_t  fibo_utf8 = njs_str(
        "function fibo(n) {"
        "    if (n > 1)"
        "        return fibo(n - 1) + fibo(n - 2);"
        "    return 'α'"
        "}"
        "fibo(32).length");

    static njs_str_t  json = njs_str(
        "JSON.parse('{\"a\":123, \"XXX\":[3,4,null]}').a");

    static njs_str_t  for_loop = njs_str(
        "var i; for (i = 0; i < 100000000; i++); i");

    static njs_str_t while_loop = njs_str(
        "var i = 0; while (i < 100000000) { i++ }; i");

    static njs_str_t  fibo_result = njs_str("3524578");
    static njs_str_t  json_result = njs_str("123");
    static njs_str_t  loop_result = njs_str("100000000");


    if (argc > 1) {
        switch (argv[1][0]) {

        case 'v':
            return njs_unit_test_benchmark(&script, &result,
                                           "nJSVM clone/destroy", 1000000);

        case 'j':
            return njs_unit_test_benchmark(&json, &json_result,
                                           "JSON.parse", 1000000);

        case 'f':
            return njs_unit_test_benchmark(&for_loop, &loop_result,
                                           "for loop 100M", 1);

        case 'w':
            return njs_unit_test_benchmark(&while_loop, &loop_result,
                                           "while loop 100M", 1);

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

    njs_printf("unknown agrument\n");
    return EXIT_FAILURE;
}
