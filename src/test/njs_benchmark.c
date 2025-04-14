
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) NGINX, Inc.
 */

#include <njs.h>
#include <njs_queue.h>

#include "njs_externals_test.h"

#include <string.h>
#include <stdlib.h>
#include <sys/resource.h>
#include <time.h>


typedef struct {
    const char  *name;
    njs_str_t   script;
    njs_str_t   result;
    njs_uint_t  repeat;
} njs_benchmark_test_t;


typedef struct {
    uint8_t     dump_report;
    const char  *prefix;
    const char  *previous;
} njs_opts_t;


static njs_int_t njs_benchmark_preinit(njs_vm_t *vm);
static njs_int_t njs_benchmark_init(njs_vm_t *vm);
static njs_int_t njs_benchmark_string(njs_vm_t *vm, njs_value_t *args,
    njs_uint_t nargs, njs_index_t unused, njs_value_t *retval);


static njs_external_t  njs_benchmark_external[] = {

    {
        .flags = NJS_EXTERN_PROPERTY | NJS_EXTERN_SYMBOL,
        .name.symbol = NJS_SYMBOL_TO_STRING_TAG,
        .u.property = {
            .value = "Benchmark",
        }
    },

    {
        .flags = NJS_EXTERN_METHOD,
        .name.string = njs_str("string"),
        .writable = 1,
        .configurable = 1,
        .enumerable = 1,
        .u.method = {
            .native = njs_benchmark_string,
        }
    },
};


njs_module_t  njs_benchmark_module = {
    .name = njs_str("benchmark"),
    .preinit = njs_benchmark_preinit,
    .init = njs_benchmark_init,
};


njs_module_t *njs_benchmark_addon_external_modules[] = {
    &njs_unit_test_external_module,
    &njs_benchmark_module,
    NULL
};


static njs_int_t    njs_benchmark_proto_id;


static uint64_t
njs_time(void)
{
#if (NJS_HAVE_CLOCK_MONOTONIC)
    struct timespec ts;

    clock_gettime(CLOCK_MONOTONIC, &ts);

    return (uint64_t) ts.tv_sec * 1000000000 + ts.tv_nsec;
#else
    struct timeval tv;

    gettimeofday(&tv, NULL);

    return (uint64_t) tv.tv_sec * 1000000000 + tv.tv_usec * 1000;
#endif
}


static njs_int_t
njs_benchmark_test(njs_vm_t *parent, njs_opts_t *opts, njs_value_t *report,
    njs_benchmark_test_t *test)
{
    u_char              *start;
    njs_vm_t            *vm, *nvm;
    uint64_t            ns;
    njs_int_t           ret;
    njs_str_t           s, *expected;
    njs_uint_t          i, n;
    njs_bool_t          success;
    njs_value_t         *result;
    njs_vm_opt_t        options;
    njs_opaque_value_t  retval, name, usec, times;

    static const njs_str_t  name_key = njs_str("name");
    static const njs_str_t  usec_key = njs_str("usec");
    static const njs_str_t  times_key = njs_str("times");

    njs_vm_opt_init(&options);

    options.backtrace = 1;
    options.addons = njs_benchmark_addon_external_modules;

    vm = NULL;
    nvm = NULL;
    ret = NJS_ERROR;

    vm = njs_vm_create(&options);
    if (vm == NULL) {
        njs_printf("njs_vm_create() failed\n");
        goto done;
    }

    start = test->script.start;

    ret = njs_vm_compile(vm, &start, start + test->script.length);
    if (ret != NJS_OK) {
        njs_printf("njs_vm_compile() failed\n");
        goto done;
    }

    n = test->repeat;
    expected = &test->result;

    ret = NJS_ERROR;
    ns = njs_time();

    for (i = 0; i < n; i++) {

        nvm = njs_vm_clone(vm, NULL);
        if (nvm == NULL) {
            njs_printf("njs_vm_clone() failed\n");
            goto done;
        }

        (void) njs_vm_start(nvm, njs_value_arg(&retval));

        if (njs_vm_value_string(nvm, &s, njs_value_arg(&retval)) != NJS_OK) {
            njs_printf("njs_vm_value_string() failed\n");
            goto done;
        }

        success = njs_strstr_eq(expected, &s);

        if (!success) {
            njs_printf("%s failed: \"%V\" vs \"%V\"\n", test->name, expected,
                       &s);
            goto done;
        }

        njs_vm_destroy(nvm);
        nvm = NULL;
    }

    ns = njs_time() - ns;

    if (!opts->dump_report) {
        if (n == 1) {
            njs_printf("%s%s: %.3fs\n", opts->previous ? "    " : "",
                       test->name, (double) ns / 1000000000);

        } else {
            njs_printf("%s%s: %.3fµs, %d times/s\n",
                       opts->previous ? "    " : "",
                       test->name, (double) ns / n / 1000,
                       (int) ((uint64_t) n * 1000000000 / ns));
        }
    }

    result = njs_vm_array_push(parent, report);
    if (result == NULL) {
        njs_printf("njs_vm_array_push() failed\n");
        goto done;
    }

    ret = njs_vm_value_string_create(parent, njs_value_arg(&name),
                                     (u_char *) test->name,
                                     njs_strlen(test->name));
    if (ret != NJS_OK) {
        njs_printf("njs_vm_value_string_create() failed\n");
        goto done;
    }

    njs_value_number_set(njs_value_arg(&usec), 1000 * ns);
    njs_value_number_set(njs_value_arg(&times), n);

    ret = njs_vm_object_alloc(parent, result, NULL);
    if (ret != NJS_OK) {
        njs_printf("njs_vm_object_alloc() failed\n");
        goto done;
    }

    ret = njs_vm_object_prop_set(parent, result, &name_key, &name);
    if (ret != NJS_OK) {
        njs_printf("njs_vm_object_prop_set() failed\n");
        goto done;
    }

    ret = njs_vm_object_prop_set(parent, result, &usec_key, &usec);
    if (ret != NJS_OK) {
        njs_printf("njs_vm_object_prop_set() failed\n");
        goto done;
    }

    ret = njs_vm_object_prop_set(parent, result, &times_key, &times);
    if (ret != NJS_OK) {
        njs_printf("njs_vm_object_prop_set() failed\n");
        goto done;
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

static njs_benchmark_test_t  njs_test[] =
{
    { "nJSVM clone/destroy",
      njs_str("null"),
      njs_str("null"),
      1000000 },

    { "func call",
      njs_str("function test(a) { return 1 }"
              ""
              "test(1);"
              "test(1);"
              "test(1);"
              "test(1);"),
      njs_str("1"),
      100000 },

    { "func call (3 local functions)",
      njs_str("function test(a) { "
              "    function g(x) {}"
              "    function h(x) {}"
              "    function f(x) {}"
              "    return 1;"
              "}"
              ""
              "test(1);"
              "test(1);"
              "test(1);"
              "test(1);"),
      njs_str("1"),
      100000 },

    { "closure var global",
      njs_str("function test(a) { sum++ }"
              ""
              "var sum = 0;"
              ""
              "test(1);"
              "test(1);"
              "test(1);"
              "test(1);"
              "sum"),
      njs_str("4"),
      100000 },

    { "string create 'abcdefABCDEF'",
      njs_str("benchmark.string('create', 'abcdef', 1000000)"),
      njs_str("undefined"),
      1 },

    { "string create 'АБВГДЕ'",
      njs_str("benchmark.string('create', 'АБВГДЕ', 1000000)"),
      njs_str("undefined"),
      1 },

    { "string create 'x'.repeat(24)",
      njs_str("benchmark.string('create', 'x'.repeat(32), 1000000)"),
      njs_str("undefined"),
      1 },

    { "string create 'Д'.repeat(12)",
      njs_str("benchmark.string('create', 'А'.repeat(16), 1000000)"),
      njs_str("undefined"),
      1 },

    { "string create chb 'x'.repeat(256)",
      njs_str("benchmark.string('chb', 'x'.repeat(256), 10000)"),
      njs_str("undefined"),
      1 },

    { "string create chb 'Д'.repeat(128)",
      njs_str("benchmark.string('chb', 'Д'.repeat(128), 10000)"),
      njs_str("undefined"),
      1 },

    { "string create chb 'x'.repeat(128) + 'Д'.repeat(64)",
      njs_str("benchmark.string('chb', 'x'.repeat(128) + 'Д'.repeat(64), 10000)"),
      njs_str("undefined"),
      1 },

    { "JSON.parse",
      njs_str("JSON.parse('{\"a\":123, \"XXX\":[3,4,null]}').a"),
      njs_str("123"),
      1000000 },

    { "JSON.parse large",
      njs_str("JSON.parse(JSON.stringify([Array(2**16)]))[0].length"),
      njs_str("65536"),
      10 },

    { "JSON.parse reviver large",
      njs_str("JSON.parse(JSON.stringify([Array(2**16)]), v=>v)"),
      njs_str(""),
      10 },

    { "for loop 100M",
      njs_str("var i; for (i = 0; i < 100000000; i++); i"),
      njs_str("100000000"),
      1 },

    { "for let loop 100M",
      njs_str("let i; for (i = 0; i < 100000000; i++); i"),
      njs_str("100000000"),
      1 },

    { "for let closures 1M",
      njs_str("let a = []; for (let i = 0; i < 1000000; i++) { a.push(() => i); }"
              "a[5]()"),
      njs_str("5"),
      1 },

    { "while loop 100M",
      njs_str("var i = 0; while (i < 100000000) { i++ }; i"),
      njs_str("100000000"),
      1 },

    { "fibobench numbers",
      njs_str("function fibo(n) {"
              "    if (n > 1)"
              "        return fibo(n - 1) + fibo(n - 2);"
              "    return 1"
              "}"
              "fibo(32)"),
      njs_str("3524578"),
      1 },

    { "fibobench ascii strings",
      njs_str("function fibo(n) {"
              "    if (n > 1)"
              "        return fibo(n - 1) + fibo(n - 2);"
              "    return '.'"
              "}"
              "fibo(32).length"),
      njs_str("3524578"),
      1 },

    { "fibobench utf8 strings",
      njs_str("function fibo(n) {"
              "    if (n > 1)"
              "        return fibo(n - 1) + fibo(n - 2);"
              "    return 'α'"
              "}"
              "fibo(32).length"),
      njs_str("3524578"),
      1 },

    { "array 64k keys",
      njs_str("var arr = new Array(2**16);"
              "arr.fill(1);"
              "Object.keys(arr)[0]"),
      njs_str("0"),
      10 },

    { "array 64k values",
      njs_str("var arr = new Array(2**16);"
              "arr.fill(1);"
              "Object.values(arr)[0]"),
      njs_str("1"),
      10 },

    { "array 64k entries",
      njs_str("var arr = new Array(2**16);"
              "arr.fill(1);"
              "Object.entries(arr)[0][0]"),
      njs_str("0"),
      10 },

    { "array 1M",
      njs_str("var arr = new Array(1000000);"
              "var count = 0, length = arr.length;"
              "arr.fill(2);"
              "for (var i = 0; i < length; i++) { count += arr[i]; }"
              "count"),
      njs_str("2000000"),
      1 },

    { "typed array 10M",
      njs_str("var arr = new Uint8Array(10**7);"
              "var count = 0, length = arr.length;"
              "arr.fill(2);"
              "for (var i = 0; i < length; i++) { count += arr[i]; }"
              "count"),
      njs_str("20000000"),
      1 },

    { "typed array 10M set",
      njs_str("var arr = new Uint32Array(10**7);"
              "var length = arr.length;"
              "for (var i = 0; i < length; i++) { arr[i] = i; }"),
      njs_str("undefined"),
      1 },

    { "regexp split",
      njs_str("var s = Array(26).fill(0).map((v,i)=> {"
              "    var u = String.fromCodePoint(65+i), l = u.toLowerCase(); return u+l+l;}).join('');"
              "s.split(/(?=[A-Z])/).length"),
      njs_str("26"),
      100 },

    { "regexp 10K split",
      njs_str("'a '.repeat(10000).split(/ /).length"),
      njs_str("10001"),
      1 },

    { "simple 100K split",
      njs_str("'a '.repeat(100000).split(' ').length"),
      njs_str("100001"),
      1 },

    { "external property ($shared.uri)",
      njs_str("$shared.uri"),
      njs_str("shared"),
      1000 },

    { "external object property ($shared.props.a)",
      njs_str("$shared.props.a"),
      njs_str("11"),
      1000 },

    { "external dump (JSON.stringify($shared.header))",
      njs_str("JSON.stringify($shared.header)"),
      njs_str("{\"01\":\"01|АБВ\",\"02\":\"02|АБВ\",\"03\":\"03|АБВ\"}"),
      1000 },

    { "external method ($shared.method('YES'))",
      njs_str("$shared.method('YES')"),
      njs_str("shared"),
      1000 },

    { "exception",
      njs_str("function f() { try { throw new Error('test') } catch (e) { return e.message } } [f].map(v=>v())[0]"),
      njs_str("test"),
      10000 },

    { "exception.stack",
      njs_str("function f() { try { throw new Error('test') } catch (e) { return e.stack } } [f].map(v=>v())[0]"),
      njs_str("Error: test\n    at f (:1)\n    at anonymous (:1)\n    at Array.prototype.map (native)\n    at main (:1)\n"),
      100 },
};


static njs_str_t  code = njs_str(
    "import fs from 'fs';"
    ""
    "function compare(prev_fn, current) {"
    "  var prev_report = JSON.parse(fs.readFileSync(prev_fn));"
    "  var test, prev, diff, result = [`Diff with ${prev_fn}:`];"
    "  for (var t in current) {"
    "    test = current[t];"
    "    prev = find(prev_report, test.name);"
    "    diff = (test.usec - prev.usec) / prev.usec * 100;"
    "    result.push(`    ${test.name}: ${diff.toFixed(2)}%`);"
    "  }"
    "  return result.join('\\n') + '\\n';"
    "}"
    ""
    "function find(report, name) {"
    "  for (var t in report) {"
    "     if (report[t].name == name) { return report[t];}"
    "  }"
    "}");


static const njs_str_t  compare = njs_str("compare");


int njs_cdecl
main(int argc, char **argv)
{
    char                  *p;
    u_char                *start;
    njs_vm_t              *vm;
    njs_int_t             ret, k;
    njs_str_t             out;
    njs_uint_t            i;
    njs_opts_t            opts;
    njs_vm_opt_t          options;
    njs_opaque_value_t    args[2], report, retval;
    njs_benchmark_test_t  *test;

    static const char  help[] =
        "njs benchmark.\n"
        "\n"
        "njs_benchmark [OPTIONS]"
        "\n"
        "Options:\n"
        "  -b <name_prefix>  specify the benchmarks to execute.\n"
        "  -d                dump report as a JSON file.\n"
        "  -c <report file>  compare with previous report.\n"
        "  -h                this help.\n";

    njs_memzero(&opts, sizeof(njs_opts_t));
    opts.prefix = "";

    for (k = 1; k < argc; k++) {
        p = argv[k];

        if (p[0] != '-') {
            goto invalid_options;
        }

        p++;

        switch (*p) {
        case '?':
        case 'h':
            njs_print(help, njs_length(help));
            return EXIT_SUCCESS;

        case 'b':
            if (++k < argc) {
                opts.prefix = argv[k];
                break;
            }

            njs_stderror("option \"-b\" requires argument\n");
            return EXIT_FAILURE;

        case 'c':
            if (++k < argc) {
                opts.previous = argv[k];
                break;
            }

            njs_stderror("option \"-c\" requires argument\n");
            return EXIT_FAILURE;

        case 'd':
            opts.dump_report = 1;
            break;

        default:
            goto invalid_options;
        }
    }

    njs_vm_opt_init(&options);
    options.init = 1;
    options.argv = argv;
    options.argc = argc;

    vm = njs_vm_create(&options);
    if (vm == NULL) {
        njs_printf("njs_vm_create() failed\n");
        return EXIT_FAILURE;
    }

    start = code.start;
    ret = njs_vm_compile(vm, &start, start + code.length);
    if (ret != NJS_OK) {
        njs_printf("njs_vm_compile() failed\n");
        goto done;
    }

    njs_vm_start(vm, njs_value_arg(&retval));

    ret = njs_vm_array_alloc(vm, njs_value_arg(&report), 8);
    if (ret != NJS_OK) {
        njs_printf("njs_vm_array_alloc() failed\n");
        goto done;
    }

    if (opts.previous) {
        njs_printf("Current:\n");
    }

    for (i = 0; i < njs_nitems(njs_test); i++) {
        test = &njs_test[i];

        if (strncmp(test->name, opts.prefix,
                    njs_min(strlen(test->name), strlen(opts.prefix))) == 0)
        {
            ret = njs_benchmark_test(vm, &opts, njs_value_arg(&report), test);

            if (ret != NJS_OK) {
                goto done;
            }
        }
    }

    if (opts.previous) {
        ret = njs_vm_value_string_create(vm, njs_value_arg(&args[0]),
                                         (u_char *) opts.previous,
                                         njs_strlen(opts.previous));
        if (ret != NJS_OK) {
            njs_printf("njs_vm_value_string_create() failed\n");
            goto done;
        }

        njs_value_assign(&args[1], &report);

        njs_vm_invoke(vm, njs_vm_function(vm, &compare), njs_value_arg(&args),
                      2, njs_value_arg(&retval));

        ret = njs_vm_value_dump(vm, &out, njs_value_arg(&retval), 1, 1);
        if (ret != NJS_OK) {
            njs_printf("njs_vm_value_dump() failed\n");
            goto done;
        }

        njs_print(out.start, out.length);

        return EXIT_SUCCESS;
    }

    if (opts.dump_report) {
        ret = njs_vm_json_stringify(vm, njs_value_arg(&report), 1,
                                    njs_value_arg(&retval));
        if (ret != NJS_OK) {
            njs_printf("njs_vm_json_stringify() failed\n");
            goto done;
        }

        ret = njs_vm_value_dump(vm, &out, njs_value_arg(&retval), 1, 1);
        if (ret != NJS_OK) {
            njs_printf("njs_vm_value_dump() failed\n");
            goto done;
        }

        njs_print(out.start, out.length);
    }

    ret = EXIT_SUCCESS;

done:

    njs_vm_destroy(vm);

    return ret;

invalid_options:

    njs_stderror("Unknown argument: \"%s\" "
                 "try \"%s -h\" for available options\n", argv[k],
                 argv[0]);

    return EXIT_FAILURE;
}


static njs_int_t
njs_benchmark_preinit(njs_vm_t *vm)
{
    njs_benchmark_proto_id = njs_vm_external_prototype(vm,
                                           njs_benchmark_external,
                                           njs_nitems(njs_benchmark_external));
    if (njs_slow_path(njs_benchmark_proto_id < 0)) {
        njs_printf("njs_vm_external_prototype() failed\n");
        return NJS_ERROR;
    }

    return NJS_OK;
}


static const njs_str_t  benchmark = njs_str("benchmark");


static njs_int_t
njs_benchmark_init(njs_vm_t *vm)
{
    njs_int_t           ret;
    njs_opaque_value_t  value;

    ret = njs_vm_external_create(vm, njs_value_arg(&value),
                                 njs_benchmark_proto_id, NULL, 1);
    if (njs_slow_path(ret != NJS_OK)) {
        return NJS_ERROR;
    }

    ret = njs_vm_bind(vm, &benchmark, njs_value_arg(&value), 1);
    if (njs_slow_path(ret != NJS_OK)) {
        njs_printf("njs_vm_bind() failed\n");
        return NJS_ERROR;
    }

    return NJS_OK;
}


static njs_int_t
njs_benchmark_string(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t unused, njs_value_t *retval)
{
    int64_t             i, n;
    njs_chb_t           chain;
    njs_str_t           s, mode;
    njs_opaque_value_t  value;

    njs_value_string_get(vm, njs_arg(args, nargs, 1), &mode);

    njs_value_string_get(vm, njs_arg(args, nargs, 2), &s);

    if (njs_value_to_integer(vm, njs_arg(args, nargs, 3), &n) != NJS_OK) {
        return NJS_ERROR;
    }

    if (memcmp(mode.start, "create", 6) == 0) {

        for (i = 0; i < n; i++) {
            njs_vm_value_string_create(vm, njs_value_arg(&value), s.start,
                                       s.length);
        }

    } else if (memcmp(mode.start, "chb", 3) == 0) {

        NJS_CHB_MP_INIT(&chain, vm);

        njs_chb_append_literal(&chain, "abc");
        njs_chb_append(&chain, s.start, s.length);
        njs_chb_append_literal(&chain, "abc");
        njs_chb_append(&chain, s.start, s.length);
        njs_chb_append_literal(&chain, "abc");
        njs_chb_append(&chain, s.start, s.length);

        for (i = 0; i < n; i++) {
            njs_vm_value_string_create_chb(vm, njs_value_arg(&value), &chain);
        }

        njs_chb_destroy(&chain);

    } else {
        njs_vm_type_error(vm, "unknown mode \"%V\"", &mode);
        return NJS_ERROR;
    }

    njs_value_undefined_set(retval);

    return NJS_OK;
}
