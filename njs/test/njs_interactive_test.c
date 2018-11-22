
/*
 * Copyright (C) Dmitry Volyntsev
 * Copyright (C) NGINX, Inc.
 */

#include <njs.h>
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

    { nxt_string("arguments" ENTER
                 "function(){}()" ENTER),
      nxt_string("SyntaxError: Unexpected token \"(\" in 1") },

    /* Backtraces */

    { nxt_string("function ff(o) {return o.a.a}" ENTER
                 "function f(o) {return ff(o)}" ENTER
                 "f({})" ENTER),
      nxt_string("TypeError: cannot get property 'a' of undefined\n"
                 "    at ff (:1)\n"
                 "    at f (:1)\n"
                 "    at main (native)\n") },

    { nxt_string("function ff(o) {return o.a.a}" ENTER
                 "function f(o) {try {return ff(o)} "
                                 "finally {return o.a.a}}" ENTER
                 "f({})" ENTER),
      nxt_string("TypeError: cannot get property 'a' of undefined\n"
                 "    at f (:1)\n"
                 "    at main (native)\n") },

    { nxt_string("function f(ff, o) {return ff(o)}" ENTER
                 "f(function (o) {return o.a.a}, {})" ENTER),
      nxt_string("TypeError: cannot get property 'a' of undefined\n"
                 "    at anonymous (:1)\n"
                 "    at f (:1)\n"
                 "    at main (native)\n") },

    { nxt_string("'str'.replace(/t/g,"
                 "              function(m) {return m.a.a})" ENTER),
      nxt_string("TypeError: cannot get property 'a' of undefined\n"
                 "    at anonymous (:1)\n"
                 "    at String.prototype.replace (native)\n"
                 "    at main (native)\n") },

    { nxt_string("function f(o) {return Object.keys(o)}" ENTER
                 "f()" ENTER),
      nxt_string("TypeError: cannot convert undefined to object\n"
                 "    at Object.keys (native)\n"
                 "    at f (:1)\n"
                 "    at main (native)\n") },

    { nxt_string("String.fromCharCode(3.14)" ENTER),
      nxt_string("RangeError\n"
                 "    at String.fromCharCode (native)\n"
                 "    at main (native)\n") },

    { nxt_string("Math.log({}.a.a)" ENTER),
      nxt_string("TypeError: cannot get property 'a' of undefined\n"
                 "    at Math.log (native)\n"
                 "    at main (native)\n") },

    { nxt_string("eval()" ENTER),
      nxt_string("InternalError: Not implemented\n"
                 "    at eval (native)\n"
                 "    at main (native)\n") },

    { nxt_string("require()" ENTER),
      nxt_string("TypeError: missing path\n"
                 "    at require (native)\n"
                 "    at main (native)\n") },

    { nxt_string("setTimeout()" ENTER),
      nxt_string("TypeError: too few arguments\n"
                 "    at setTimeout (native)\n"
                 "    at main (native)\n") },

    { nxt_string("require('crypto').createHash('sha')" ENTER),
      nxt_string("TypeError: not supported algorithm: 'sha'\n"
                 "    at crypto.createHash (native)\n"
                 "    at main (native)\n") },

    { nxt_string("var h = require('crypto').createHash('sha1')" ENTER
                 "h.update([])" ENTER),
      nxt_string("TypeError: data must be a string\n"
                 "    at Hash.prototype.update (native)\n"
                 "    at main (native)\n") },

    { nxt_string("require('crypto').createHmac('sha1', [])" ENTER),
      nxt_string("TypeError: key must be a string\n"
                 "    at crypto.createHmac (native)\n"
                 "    at main (native)\n") },

    { nxt_string("var h = require('crypto').createHmac('sha1', 'secret')" ENTER
                 "h.update([])" ENTER),
      nxt_string("TypeError: data must be a string\n"
                 "    at Hmac.prototype.update (native)\n"
                 "    at main (native)\n") },

    { nxt_string("function f(o) {function f_in(o) {return o.a.a};"
                 "               return f_in(o)}; f({})" ENTER),
      nxt_string("TypeError: cannot get property 'a' of undefined\n"
                 "    at f_in (:1)\n"
                 "    at f (:1)\n"
                 "    at main (native)\n") },

    { nxt_string("function f(o) {var ff = function (o) {return o.a.a};"
                 "               return ff(o)}; f({})" ENTER),
      nxt_string("TypeError: cannot get property 'a' of undefined\n"
                 "    at anonymous (:1)\n"
                 "    at f (:1)\n"
                 "    at main (native)\n") },

    { nxt_string("var fs = require('fs'); fs.readFile()" ENTER),
      nxt_string("TypeError: too few arguments\n"
                 "    at fs.readFile (native)\n"
                 "    at main (native)\n") },

    { nxt_string("parseInt({ toString: function() { return [1] } })" ENTER),
      nxt_string("TypeError: Cannot convert object to primitive value\n"
                 "    at parseInt (native)\n"
                 "    at main (native)\n") },

    { nxt_string("function f(n) { if (n == 0) { throw 'a'; } return f(n-1); }; f(2)" ENTER),
      nxt_string("a\n"
                 "    at f (:1)\n"
                 "      repeats 2 times\n"
                 "    at main (native)\n") },

    /* Exception in njs_vm_retval_to_ext_string() */

    { nxt_string("var o = { toString: function() { return [1] } }" ENTER
                 "o" ENTER),
      nxt_string("TypeError: Cannot convert object to primitive value\n"
                 "    at main (native)\n") },

};


static nxt_int_t
njs_interactive_test(nxt_bool_t verbose)
{
    u_char                  *start, *last, *end;
    njs_vm_t                *vm;
    nxt_int_t               ret;
    nxt_str_t               s;
    nxt_uint_t              i;
    nxt_bool_t              success;
    njs_vm_opt_t            options;
    njs_interactive_test_t  *test;

    vm = NULL;
    ret = NXT_ERROR;

    for (i = 0; i < nxt_nitems(njs_test); i++) {

        test = &njs_test[i];

        if (verbose) {
            printf("\"%.*s\"\n", (int) test->script.length, test->script.start);
            fflush(stdout);
        }

        nxt_memzero(&options, sizeof(njs_vm_opt_t));

        options.accumulative = 1;
        options.backtrace = 1;

        vm = njs_vm_create(&options);
        if (vm == NULL) {
            printf("njs_vm_create() failed\n");
            goto done;
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

        if (njs_vm_retval_to_ext_string(vm, &s) != NXT_OK) {
            printf("njs_vm_retval_to_ext_string() failed\n");
            goto done;
        }

        success = nxt_strstr_eq(&test->ret, &s);
        if (success) {
            njs_vm_destroy(vm);
            vm = NULL;
            continue;
        }

        printf("njs_interactive(\"%.*s\") failed: \"%.*s\" vs \"%.*s\"\n",
               (int) test->script.length, test->script.start,
               (int) test->ret.length, test->ret.start,
               (int) s.length, s.start);

        goto done;
    }

    ret = NXT_OK;

    printf("njs interactive tests passed\n");

done:

    if (vm != NULL) {
        njs_vm_destroy(vm);
    }

    return ret;
}


int nxt_cdecl
main(int argc, char **argv)
{
    nxt_bool_t  verbose;

    verbose = 0;

    if (argc > 1) {
        switch (argv[1][0]) {

        case 'v':
            verbose = 1;
            break;

        default:
            break;
        }
    }

    return njs_interactive_test(verbose);
}
