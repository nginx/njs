#!/usr/bin/perl

# (C) Dmitry Volyntsev
# (C) F5, Inc.

# Tests for QuickJS native module support.

###############################################################################

use warnings;
use strict;

use Test::More;

BEGIN { use FindBin; chdir($FindBin::Bin); }

use lib 'lib';
use Test::Nginx;

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

my $t = Test::Nginx->new()->has(qw/http/)
	->write_file_expand('nginx.conf', <<'EOF');

%%TEST_GLOBALS%%

js_load_http_native_module %%TESTDIR%%/test.so;
js_load_http_native_module %%TESTDIR%%/test.so as test;

daemon off;

events {
}

http {
    %%TEST_GLOBALS_HTTP%%

    js_import main from test.js;

    server {
        listen       127.0.0.1:8080;
        server_name  localhost;

        location /add {
            js_content main.test_add;
        }

        location /reverse {
            js_content main.test_reverse;
        }
    }
}

EOF

my $d = $t->testdir();

$t->write_file('test.js', <<EOF);
    import * as native from 'test.so';
    import * as native2 from 'test';

    function test_add(r) {
        let a = Number(r.args.a);
        let b = Number(r.args.b);
        r.return(200, native.add(a, b).toString());
    }

    function test_reverse(r) {
        r.return(200, native2.reverseString(r.args.str));
    }

    export default { test_add, test_reverse };

EOF

$t->write_file('test.c', <<EOF);
#include <quickjs.h>

#define countof(x) (sizeof(x) / sizeof((x)[0]))

static JSValue
js_add(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
    int  a, b;

    if (argc < 2) {
        return JS_ThrowTypeError(ctx, "expected 2 arguments");
    }

    if (JS_ToInt32(ctx, &a, argv[0]) < 0) {
        return JS_EXCEPTION;
    }

    if (JS_ToInt32(ctx, &b, argv[1]) < 0) {
        return JS_EXCEPTION;
    }

    return JS_NewInt32(ctx, a + b);
}


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


static const JSCFunctionListEntry js_test_native_funcs[] = {
    JS_CFUNC_DEF("add", 2, js_add),
    JS_CFUNC_DEF("reverseString", 1, js_reverse_string),
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

like(http_get('/add?a=7&b=9'), qr/16$/, 'native module add');
like(http_get('/reverse?str=hello'), qr/olleh$/, 'native module reverseString');

###############################################################################
