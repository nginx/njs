
/*
 * Copyright (C) Dmitry Volyntsev
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


typedef struct {
    nxt_str_t  script;
    nxt_str_t  ret;
} njs_interactive_test_t;


#define ENTER "\n"


static njs_interactive_test_t  njs_test[] =
{
    { nxt_string("var a = 3" ENTER
                 "a * 2" ENTER),
      nxt_string("6") },

    { nxt_string("var a = \"aa\\naa\"" ENTER
                 "a" ENTER),
      nxt_string("aa\naa") },

    { nxt_string("var a = 3" ENTER
                 "var a = 'str'" ENTER
                 "a" ENTER),
      nxt_string("str") },

    { nxt_string("var a = 2" ENTER
                 "a *= 2" ENTER
                 "a *= 2" ENTER
                 "a *= 2" ENTER),
      nxt_string("16") },

    { nxt_string("var a = 2" ENTER
                 "var b = 3" ENTER
                 "a * b" ENTER),
      nxt_string("6") },

    { nxt_string("var a = 2; var b = 3;" ENTER
                 "a * b" ENTER),
      nxt_string("6") },

    { nxt_string("function sq(f) { return f() * f() }" ENTER
                 "sq(function () { return 3 })" ENTER),
      nxt_string("9") },

    /* Temporary indexes */

    { nxt_string("var a = [1,2,3], i; for (i in a) {Object.seal({});}" ENTER),
      nxt_string("undefined") },

    { nxt_string("var i; for (i in [1,2,3]) {Object.seal({});}" ENTER),
      nxt_string("undefined") },

    { nxt_string("var a = 'A'; switch (a) {"
                 "case 0: a += '0';"
                 "case 1: a += '1';"
                 "}; a" ENTER),
      nxt_string("A") },

    { nxt_string("var a = 0; try { a = 5 }"
                 "catch (e) { a = 9 } finally { a++ } a" ENTER),
      nxt_string("6") },

    { nxt_string("/abc/i.test('ABC')" ENTER),
      nxt_string("true") },

    /* Error handling */

    { nxt_string("var a = ;" ENTER
                 "2 + 2" ENTER),
      nxt_string("4") },

    { nxt_string("function f() { return b;" ENTER),
      nxt_string("SyntaxError: Unexpected end of input in 1") },

    { nxt_string("function f() { return b;" ENTER
                 "2 + 2" ENTER),
      nxt_string("4") },

    { nxt_string("function f() { return function() { return 1" ENTER
                 "2 + 2" ENTER),
      nxt_string("4") },

    { nxt_string("function f() { return b;}" ENTER
                 "2 + 2" ENTER),
      nxt_string("4") },

    { nxt_string("function f(o) { return o.a.a;}; f{{}}" ENTER
                 "2 + 2" ENTER),
      nxt_string("4") },

    { nxt_string("function ff(o) {return o.a.a}" ENTER
                 "function f(o) {try {return ff(o)} "
                                 "finally {return 1}}" ENTER
                 "f({})" ENTER),
      nxt_string("1") },

    /* Backtraces */

    { nxt_string("function ff(o) {return o.a.a}" ENTER
                 "function f(o) {return ff(o)}" ENTER
                 "f({})" ENTER),
      nxt_string("TypeError\n"
                 "    at ff (:1)\n"
                 "    at f (:1)\n"
                 "    at main (native)\n") },

    { nxt_string("function ff(o) {return o.a.a}" ENTER
                 "function f(o) {try {return ff(o)} "
                                 "finally {return o.a.a}}" ENTER
                 "f({})" ENTER),
      nxt_string("TypeError\n"
                 "    at f (:1)\n"
                 "    at main (native)\n") },

    { nxt_string("function f(ff, o) {return ff(o)}" ENTER
                 "f(function (o) {return o.a.a}, {})" ENTER),
      nxt_string("TypeError\n"
                 "    at anonymous (:1)\n"
                 "    at f (:1)\n"
                 "    at main (native)\n") },

    { nxt_string("'str'.replace(/t/g,"
                 "              function(m) {return m.a.a})" ENTER),
      nxt_string("TypeError\n"
                 "    at anonymous (:1)\n"
                 "    at String.prototype.replace (native)\n"
                 "    at main (native)\n") },

    { nxt_string("function f(o) {return Object.keys(o)}" ENTER
                 "f()" ENTER),
      nxt_string("TypeError\n"
                 "    at Object.keys (native)\n"
                 "    at f (:1)\n"
                 "    at main (native)\n") },

    { nxt_string("String.fromCharCode(3.14)" ENTER),
      nxt_string("RangeError\n"
                 "    at String.fromCharCode (native)\n"
                 "    at main (native)\n") },

    { nxt_string("Math.log({}.a.a)" ENTER),
      nxt_string("TypeError\n"
                 "    at Math.log (native)\n"
                 "    at main (native)\n") },

    { nxt_string("function f(o) {function f_in(o) {return o.a.a};"
                 "               return f_in(o)}; f({})" ENTER),
      nxt_string("TypeError\n"
                 "    at f_in (:1)\n"
                 "    at f (:1)\n"
                 "    at main (native)\n") },

    { nxt_string("function f(o) {var ff = function (o) {return o.a.a};"
                 "               return ff(o)}; f({})" ENTER),
      nxt_string("TypeError\n"
                 "    at anonymous (:1)\n"
                 "    at f (:1)\n"
                 "    at main (native)\n") },

    { nxt_string("var fs = require('fs'); fs.readFile()" ENTER),
      nxt_string("TypeError: too few arguments\n"
                 "    at fs.readFile (native)\n"
                 "    at main (native)\n") },

    /* Exception in njs_vm_retval() */

    { nxt_string("var o = { toString: function() { return [1] } }" ENTER
                 "o" ENTER),
      nxt_string("TypeError\n"
                 "    at main (native)\n") },

};


static nxt_int_t
njs_interactive_test(void)
{
    u_char                  *start, *last, *end;
    njs_vm_t                *vm;
    nxt_int_t               ret;
    nxt_str_t               s;
    nxt_uint_t              i;
    nxt_bool_t              success;
    njs_vm_opt_t            options;
    nxt_mem_cache_pool_t    *mcp;
    njs_interactive_test_t  *test;

    mcp = nxt_mem_cache_pool_create(&njs_vm_mem_cache_pool_proto, NULL, NULL,
                                    2 * nxt_pagesize(), 128, 512, 16);
    if (nxt_slow_path(mcp == NULL)) {
        return NXT_ERROR;
    }

    ret = NXT_ERROR;

    for (i = 0; i < nxt_nitems(njs_test); i++) {

        test = &njs_test[i];

        printf("\"%.*s\"\n", (int) test->script.length, test->script.start);
        fflush(stdout);

        memset(&options, 0, sizeof(njs_vm_opt_t));

        options.mcp = mcp;
        options.accumulative = 1;
        options.backtrace = 1;

        vm = njs_vm_create(&options);
        if (vm == NULL) {
            goto fail;
        }

        start = test->script.start;
        last = start + test->script.length;
        end = NULL;

        for ( ;; ) {
            start = (end != NULL) ? end + 1 : start;
            if (start >= last) {
                break;
            }

            end = (u_char *) strchr((char *) start, '\n');

            ret = njs_vm_compile(vm, &start, end);
            if (ret == NXT_OK) {
                ret = njs_vm_run(vm);
            }
        }

        if (njs_vm_retval(vm, &s) != NXT_OK) {
            return NXT_ERROR;
        }

        success = nxt_strstr_eq(&test->ret, &s);
        if (success) {
            continue;
        }

        printf("njs_interactive(\"%.*s\") failed: \"%.*s\" vs \"%.*s\"\n",
               (int) test->script.length, test->script.start,
               (int) test->ret.length, test->ret.start,
               (int) s.length, s.start);

        goto fail;
    }

    ret = NXT_OK;

    printf("njs interactive tests passed\n");

fail:

    nxt_mem_cache_pool_destroy(mcp);

    return ret;
}


int nxt_cdecl
main(int argc, char **argv)
{
    return njs_interactive_test();
}
