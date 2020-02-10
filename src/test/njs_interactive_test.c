
/*
 * Copyright (C) Dmitry Volyntsev
 * Copyright (C) NGINX, Inc.
 */

#include <njs.h>

#include <string.h>
#include <sys/resource.h>
#include <time.h>


typedef struct {
    njs_str_t  script;
    njs_str_t  ret;
} njs_interactive_test_t;


#define ENTER "\n\3"


static njs_interactive_test_t  njs_test[] =
{
    { njs_str("var a = 3" ENTER
                 "a * 2" ENTER),
      njs_str("6") },

    { njs_str("var a = \"aa\\naa\"" ENTER
                 "a" ENTER),
      njs_str("aa\naa") },

    { njs_str("var a = 3" ENTER
                 "var a = 'str'" ENTER
                 "a" ENTER),
      njs_str("str") },

    { njs_str("var a = 2" ENTER
                 "a *= 2" ENTER
                 "a *= 2" ENTER
                 "a *= 2" ENTER),
      njs_str("16") },

    { njs_str("var a = 2" ENTER
                 "var b = 3" ENTER
                 "a * b" ENTER),
      njs_str("6") },

    { njs_str("var a = 2; var b = 3;" ENTER
                 "a * b" ENTER),
      njs_str("6") },

    { njs_str("function sq(f) { return f() * f() }" ENTER
                 "sq(function () { return 3 })" ENTER),
      njs_str("9") },

    /* Temporary indexes */

    { njs_str("var a = [1,2,3], i; for (i in a) {Object.seal({});}" ENTER),
      njs_str("undefined") },

    { njs_str("var i; for (i in [1,2,3]) {Object.seal({});}" ENTER),
      njs_str("undefined") },

    { njs_str("var a = 'A'; switch (a) {"
                 "case 0: a += '0';"
                 "case 1: a += '1';"
                 "}; a" ENTER),
      njs_str("A") },

    { njs_str("var a = 0; try { a = 5 }"
                 "catch (e) { a = 9 } finally { a++ } a" ENTER),
      njs_str("6") },

    { njs_str("/abc/i.test('ABC')" ENTER),
      njs_str("true") },

    /* Accumulative mode. */

    { njs_str("var a = 1" ENTER
              "a" ENTER),
      njs_str("1") },

    { njs_str("Number.prototype.test = 'test'" ENTER
              "Number.prototype.test" ENTER),
      njs_str("test") },

    /* Error handling */

    { njs_str("var a = ;" ENTER
                 "2 + 2" ENTER),
      njs_str("4") },

    { njs_str("function f() { return b;" ENTER),
      njs_str("SyntaxError: Unexpected end of input in 1") },

    { njs_str("function f() { return b;" ENTER
                 "2 + 2" ENTER),
      njs_str("4") },

    { njs_str("function f() { return function() { return 1" ENTER
                 "2 + 2" ENTER),
      njs_str("4") },

    { njs_str("function f() { return b;}" ENTER
                 "2 + 2" ENTER),
      njs_str("4") },

    { njs_str("function f(o) { return o.a.a;}; f{{}}" ENTER
                 "2 + 2" ENTER),
      njs_str("4") },

    { njs_str("function ff(o) {return o.a.a}" ENTER
                 "function f(o) {try {return ff(o)} "
                                 "finally {return 1}}" ENTER
                 "f({})" ENTER),
      njs_str("1") },

    { njs_str("arguments" ENTER
                 "function(){}()" ENTER),
      njs_str("SyntaxError: Unexpected token \"(\" in 1") },

    /* Backtraces */

    { njs_str("function ff(o) {return o.a.a}" ENTER
                 "function f(o) {return ff(o)}" ENTER
                 "f({})" ENTER),
      njs_str("TypeError: cannot get property \"a\" of undefined\n"
                 "    at ff (:1)\n"
                 "    at f (:1)\n"
                 "    at main (native)\n") },

    { njs_str("function ff(o) {return o.a.a}" ENTER
                 "function f(o) {try {return ff(o)} "
                                 "finally {return o.a.a}}" ENTER
                 "f({})" ENTER),
      njs_str("TypeError: cannot get property \"a\" of undefined\n"
                 "    at f (:1)\n"
                 "    at main (native)\n") },

    { njs_str("function f(ff, o) {return ff(o)}" ENTER
                 "f(function (o) {return o.a.a}, {})" ENTER),
      njs_str("TypeError: cannot get property \"a\" of undefined\n"
                 "    at anonymous (:1)\n"
                 "    at f (:1)\n"
                 "    at main (native)\n") },

    { njs_str("'str'.replace(/t/g,"
                 "              function(m) {return m.a.a})" ENTER),
      njs_str("TypeError: cannot get property \"a\" of undefined\n"
                 "    at anonymous (:1)\n"
                 "    at String.prototype.replace (native)\n"
                 "    at main (native)\n") },

    { njs_str("function f(o) {return Object.keys(o)}" ENTER
                 "f()" ENTER),
      njs_str("TypeError: cannot convert undefined argument to object\n"
                 "    at Object.keys (native)\n"
                 "    at f (:1)\n"
                 "    at main (native)\n") },

    { njs_str("''.repeat(-1)" ENTER),
      njs_str("RangeError\n"
                 "    at String.prototype.repeat (native)\n"
                 "    at main (native)\n") },

    { njs_str("Math.log({}.a.a)" ENTER),
      njs_str("TypeError: cannot get property \"a\" of undefined\n"
                 "    at Math.log (native)\n"
                 "    at main (native)\n") },

    { njs_str("var bound = Math.max.bind(null, {toString(){return {}}}); bound(1)" ENTER),
      njs_str("TypeError: Cannot convert object to primitive value\n"
                 "    at Math.max (native)\n"
                 "    at main (native)\n") },

    { njs_str("Object.prototype()" ENTER),
      njs_str("TypeError: (intermediate value)[\"prototype\"] is not a function\n"
                 "    at main (native)\n") },

    { njs_str("eval()" ENTER),
      njs_str("InternalError: Not implemented\n"
                 "    at eval (native)\n"
                 "    at main (native)\n") },

    { njs_str("new Function(\n\n@)" ENTER),
      njs_str("SyntaxError: Unexpected token \"@\" in 3") },

    { njs_str("require()" ENTER),
      njs_str("TypeError: missing path\n"
                 "    at require (native)\n"
                 "    at main (native)\n") },

    { njs_str("setTimeout()" ENTER),
      njs_str("TypeError: too few arguments\n"
                 "    at setTimeout (native)\n"
                 "    at main (native)\n") },

    { njs_str("require('crypto').createHash('sha')" ENTER),
      njs_str("TypeError: not supported algorithm: \"sha\"\n"
                 "    at crypto.createHash (native)\n"
                 "    at main (native)\n") },

    { njs_str("var h = require('crypto').createHash('sha1')" ENTER
                 "h.update([])" ENTER),
      njs_str("TypeError: data must be a string\n"
                 "    at Hash.prototype.update (native)\n"
                 "    at main (native)\n") },

    { njs_str("require('crypto').createHmac('sha1', [])" ENTER),
      njs_str("TypeError: key must be a string\n"
                 "    at crypto.createHmac (native)\n"
                 "    at main (native)\n") },

    { njs_str("var h = require('crypto').createHmac('sha1', 'secret')" ENTER
                 "h.update([])" ENTER),
      njs_str("TypeError: data must be a string\n"
                 "    at Hmac.prototype.update (native)\n"
                 "    at main (native)\n") },

    { njs_str("function f(o) {function f_in(o) {return o.a.a};"
                 "               return f_in(o)}; f({})" ENTER),
      njs_str("TypeError: cannot get property \"a\" of undefined\n"
                 "    at f_in (:1)\n"
                 "    at f (:1)\n"
                 "    at main (native)\n") },

    { njs_str("function f(o) {var ff = function (o) {return o.a.a};"
                 "               return ff(o)}; f({})" ENTER),
      njs_str("TypeError: cannot get property \"a\" of undefined\n"
                 "    at anonymous (:1)\n"
                 "    at f (:1)\n"
                 "    at main (native)\n") },

    { njs_str("var fs = require('fs');"
              "["
              " 'access',"
              " 'accessSync',"
              " 'readFile',"
              " 'readFileSync',"
              " 'writeFile',"
              " 'writeFileSync',"
              " 'appendFile',"
              " 'appendFileSync',"
              " 'symlink',"
              " 'symlinkSync',"
              " 'unlink',"
              " 'unlinkSync',"
              " 'realpath',"
              " 'realpathSync',"
              "]"
              ".every(v=>{ try {fs[v]();} catch (e) { return e.stack.search(`fs.${v} `) >= 0}})" ENTER),
      njs_str("true") },

    { njs_str("parseInt({ toString: function() { return [1] } })" ENTER),
      njs_str("TypeError: Cannot convert object to primitive value\n"
                 "    at parseInt (native)\n"
                 "    at main (native)\n") },

    { njs_str("function f(n) { if (n == 0) { throw 'a'; } return f(n-1); }; f(2)" ENTER),
      njs_str("a") },

    /* Exception in njs_vm_retval_string() */

    { njs_str("var o = { toString: function() { return [1] } }" ENTER
                 "o" ENTER),
      njs_str("TypeError: Cannot convert object to primitive value") },

    /* line numbers */

    { njs_str("/**/(function(){throw Error();})()" ENTER),
      njs_str("Error\n"
                 "    at anonymous (:1)\n"
                 "    at main (native)\n") },

    { njs_str("/***/(function(){throw Error();})()" ENTER),
      njs_str("Error\n"
                 "    at anonymous (:1)\n"
                 "    at main (native)\n") },

    { njs_str("/*\n**/(function(){throw Error();})()" ENTER),
      njs_str("Error\n"
                 "    at anonymous (:2)\n"
                 "    at main (native)\n") },

};


static njs_int_t
njs_interactive_test(njs_bool_t verbose)
{
    u_char                  *start, *last, *end;
    njs_vm_t                *vm;
    njs_int_t               ret;
    njs_str_t               s;
    njs_uint_t              i;
    njs_bool_t              success;
    njs_vm_opt_t            options;
    njs_interactive_test_t  *test;

    vm = NULL;
    ret = NJS_ERROR;

    for (i = 0; i < njs_nitems(njs_test); i++) {

        test = &njs_test[i];

        if (verbose) {
            njs_printf("\"%V\"\n", &test->script);
        }

        njs_memzero(&options, sizeof(njs_vm_opt_t));

        options.init = 1;
        options.accumulative = 1;
        options.backtrace = 1;

        vm = njs_vm_create(&options);
        if (vm == NULL) {
            njs_printf("njs_vm_create() failed\n");
            goto done;
        }

        start = test->script.start;
        last = start + test->script.length;
        end = NULL;

        for ( ;; ) {
            start = (end != NULL) ? end + njs_length(ENTER) : start;
            if (start >= last) {
                break;
            }

            end = (u_char *) strstr((char *) start, ENTER);

            ret = njs_vm_compile(vm, &start, end);
            if (ret == NJS_OK) {
                ret = njs_vm_start(vm);
            }
        }

        if (njs_vm_retval_string(vm, &s) != NJS_OK) {
            njs_printf("njs_vm_retval_string() failed\n");
            goto done;
        }

        success = njs_strstr_eq(&test->ret, &s);
        if (success) {
            njs_vm_destroy(vm);
            vm = NULL;
            continue;
        }

        njs_printf("njs_interactive(\"%V\") failed: \"%V\" vs \"%V\"\n",
                   &test->script, &test->ret, &s);

        goto done;
    }

    ret = NJS_OK;

    njs_printf("njs interactive tests passed\n");

done:

    if (vm != NULL) {
        njs_vm_destroy(vm);
    }

    return ret;
}


int njs_cdecl
main(int argc, char **argv)
{
    njs_bool_t  verbose;

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
