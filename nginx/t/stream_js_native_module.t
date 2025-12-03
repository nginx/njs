#!/usr/bin/perl

# (C) Dmitry Volyntsev
# (C) F5, Inc.

# Tests for QuickJS native module support in stream.

###############################################################################

use warnings;
use strict;

use Test::More;

BEGIN { use FindBin; chdir($FindBin::Bin); }

use lib 'lib';
use Test::Nginx;
use Test::Nginx::Stream qw/ stream /;

###############################################################################

select STDERR; $| = 1;
select STDOUT; $| = 1;

my $cc;
for my $c ('gcc', 'clang') {
    if (system("which $c >/dev/null 2>&1") == 0) {
        $cc = $c;
        last;
    }
}

plan(skip_all => "gcc or clang not found") unless defined $cc;

my $configure_args = `$Test::Nginx::NGINX -V 2>&1`;
my $m32 = $configure_args =~ /-m32/ ? '-m32' : '';
my $quickjs_inc = $configure_args =~ /(-I\S*quickjs(?:-ng)?[^\s'"]*)/
	? $1 : undef;

plan(skip_all => "QuickJS development files not found") unless $quickjs_inc;

my $t = Test::Nginx->new()->has(qw/stream stream_return/)
	->write_file_expand('nginx.conf', <<'EOF');

%%TEST_GLOBALS%%

js_load_stream_native_module %%TESTDIR%%/test.so;
js_load_stream_native_module %%TESTDIR%%/test.so as test;

daemon off;

events {
}

stream {
    %%TEST_GLOBALS_STREAM%%

    js_set $reverse    test.reverse;
    js_set $duplicate  test.duplicate;

    js_import test.js;

    server {
        listen  127.0.0.1:8081;
        return  $reverse;
    }

    server {
        listen  127.0.0.1:8082;
        return  $duplicate;
    }
}

EOF

my $d = $t->testdir();

$t->write_file('test.js', <<EOF);
    import * as native from 'test.so';
    import * as native2 from 'test';

    function reverse(s) {
        return native.reverseString(s.remoteAddress);
    }

    function duplicate(s) {
        return native2.duplicate(s.remoteAddress);
    }

    export default { reverse, duplicate };

EOF

$t->write_file('test.c', <<EOF);
#include <quickjs.h>
#include <string.h>

#define countof(x) (sizeof(x) / sizeof((x)[0]))

static JSValue
js_reverse_string(JSContext *ctx, JSValueConst this_val, int argc,
    JSValueConst *argv)
{
    char        *reversed;
    size_t       i, len;
    JSValue      result;
    const char  *str;

    if (argc < 1) {
        return JS_ThrowTypeError(ctx, "expected 1 argument");
    }

    str = JS_ToCStringLen(ctx, &len, argv[0]);
    if (!str) {
        return JS_EXCEPTION;
    }

    reversed = js_malloc(ctx, len + 1);
    if (!reversed) {
        JS_FreeCString(ctx, str);
        return JS_EXCEPTION;
    }

    for (i = 0; i < len; i++) {
        reversed[i] = str[len - 1 - i];
    }

    reversed[len] = 0;

    result = JS_NewString(ctx, reversed);

    js_free(ctx, reversed);
    JS_FreeCString(ctx, str);

    return result;
}


static JSValue
js_duplicate(JSContext *ctx, JSValueConst this_val, int argc,
    JSValueConst *argv)
{
    char        *dup;
    size_t       len;
    JSValue      result;
    const char  *str;

    if (argc < 1) {
        return JS_ThrowTypeError(ctx, "expected 1 argument");
    }

    str = JS_ToCStringLen(ctx, &len, argv[0]);
    if (!str) {
        return JS_EXCEPTION;
    }

    dup = js_malloc(ctx, len * 2 + 1);
    if (!dup) {
        JS_FreeCString(ctx, str);
        return JS_EXCEPTION;
    }

    memcpy(dup, str, len);
    memcpy(dup + len, str, len);
    dup[len * 2] = 0;

    result = JS_NewString(ctx, dup);

    js_free(ctx, dup);
    JS_FreeCString(ctx, str);

    return result;
}


static const JSCFunctionListEntry js_test_native_funcs[] = {
    JS_CFUNC_DEF("reverseString", 1, js_reverse_string),
    JS_CFUNC_DEF("duplicate", 1, js_duplicate),
};


static int
js_test_native_init(JSContext *ctx, JSModuleDef *m)
{
    return JS_SetModuleExportList(ctx, m, js_test_native_funcs,
                                  countof(js_test_native_funcs));
}


JSModuleDef *
js_init_module(JSContext *ctx, const char *module_name)
{
    int          rc;
    JSModuleDef  *m;

    m = JS_NewCModule(ctx, module_name, js_test_native_init);
    if (!m) {
        return NULL;
    }

    rc = JS_AddModuleExportList(ctx, m, js_test_native_funcs,
                                countof(js_test_native_funcs));
    if (rc < 0) {
        return NULL;
    }

    rc = JS_AddModuleExport(ctx, m, "default");
    if (rc < 0) {
        return NULL;
    }

    return m;
}
EOF

system("$cc -fPIC $m32 -O $quickjs_inc -shared -o $d/test.so $d/test.c") == 0
	or die "failed to build QuickJS native module: $!\n";

$t->try_run('no QuickJS native module support')->plan(2);

###############################################################################

like(stream('127.0.0.1:' . port(8081))->read(), qr/1\.0\.0\.721$/,
    'native module reverseString');
like(stream('127.0.0.1:' . port(8082))->read(), qr/127\.0\.0\.1127\.0\.0\.1$/,
    'native module duplicate');

###############################################################################
