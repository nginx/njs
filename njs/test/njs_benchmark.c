
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) NGINX, Inc.
 */

#include <nxt_auto_config.h>
#include <nxt_types.h>
#include <nxt_clang.h>
#include <nxt_string.h>
#include <nxt_stub.h>
#include <nxt_malloc.h>
#include <nxt_array.h>
#include <nxt_lvlhsh.h>
#include <nxt_mem_cache_pool.h>
#include <njscript.h>
#include <string.h>
#include <stdio.h>
#include <sys/resource.h>
#include <time.h>


static void *
njs_alloc(void *mem, size_t size)
{
    return nxt_malloc(size);
}


static void *
njs_zalloc(void *mem, size_t size)
{
    void  *p;

    p = nxt_malloc(size);

    if (p != NULL) {
        memset(p, 0, size);
    }

    return p;
}


static void *
njs_align(void *mem, size_t alignment, size_t size)
{
    return nxt_memalign(alignment, size);
}


static void
njs_free(void *mem, void *p)
{
    nxt_free(p);
}


static const nxt_mem_proto_t  njs_mem_cache_pool_proto = {
    njs_alloc,
    njs_zalloc,
    njs_align,
    NULL,
    njs_free,
    NULL,
    NULL,
};


static nxt_int_t
njs_unit_test_benchmark(nxt_str_t *script, nxt_str_t *result, const char *msg,
    nxt_uint_t n)
{
    u_char                *start;
    njs_vm_t              *vm, *nvm;
    uint64_t              us;
    nxt_int_t             ret;
    nxt_str_t             s;
    nxt_uint_t            i;
    nxt_bool_t            success;
    njs_vm_opt_t          options;
    struct rusage         usage;
    nxt_mem_cache_pool_t  *mcp;

    mcp = nxt_mem_cache_pool_create(&njs_mem_cache_pool_proto, NULL, NULL,
                                    2 * nxt_pagesize(), 128, 512, 16);
    if (nxt_slow_path(mcp == NULL)) {
        return NXT_ERROR;
    }

    memset(&options, 0, sizeof(njs_vm_opt_t));

    options.mcp = mcp;

    vm = njs_vm_create(&options);
    if (vm == NULL) {
        return NXT_ERROR;
    }

    start = script->start;

    ret = njs_vm_compile(vm, &start, start + script->length);
    if (ret != NXT_OK) {
        return NXT_ERROR;
    }

    for (i = 0; i < n; i++) {

        nvm = njs_vm_clone(vm, NULL, NULL);
        if (nvm == NULL) {
            return NXT_ERROR;
        }

        (void) njs_vm_run(nvm);

        if (njs_vm_retval(nvm, &s) != NXT_OK) {
            return NXT_ERROR;
        }

        success = nxt_strstr_eq(result, &s);

        if (!success) {
            return NXT_ERROR;
        }

        njs_vm_destroy(nvm);
    }

    nxt_mem_cache_pool_destroy(mcp);

    getrusage(RUSAGE_SELF, &usage);

    us = usage.ru_utime.tv_sec * 1000000 + usage.ru_utime.tv_usec
         + usage.ru_stime.tv_sec * 1000000 + usage.ru_stime.tv_usec;

    if (n == 1) {
        printf("%s: %.3fs\n", msg, (double) us / 1000000);

    } else {
        printf("%s: %.3fµs, %d times/s\n",
               msg, (double) us / n, (int) ((uint64_t) n * 1000000 / us));
    }

    return NXT_OK;
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
