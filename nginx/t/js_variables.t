#!/usr/bin/perl

# (C) Dmitry Volyntsev
# (C) Nginx, Inc.

# Tests for http njs module, setting nginx variables.

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

my $t = Test::Nginx->new()->has(qw/http rewrite/)
	->write_file_expand('nginx.conf', <<'EOF');

%%TEST_GLOBALS%%

daemon off;

events {
}

http {
    %%TEST_GLOBALS_HTTP%%

    js_set $test_var        test.variable;
    js_set $test_short_var  test.short_var;

    js_import test.js;

    server {
        listen       127.0.0.1:8080;
        server_name  localhost;

        set $foo       test.foo_orig;
        set $XXXXXXXXXXXXXXXX 1;

        location /var_set {
            return 200 $test_var$foo;
        }

        location /var_set_short {
            return 200 $test_short_var$foo;
        }

        location /content_set {
            js_content test.content_set;
        }

        location /content_set_short {
            js_content test.short_content_set;
        }

        location /not_found_set {
            js_content test.not_found_set;
        }

        location /variable_lowkey {
            js_content test.variable_lowkey;
        }
    }
}

EOF

$t->write_file('test.js', <<EOF);
    function variable(r) {
        r.variables.foo = r.variables.arg_a;
        return 'test_var';
    }

    function short_var(r) {
        r.var.foo = r.var.arg_a;
        return 'short_var';
    }

    function content_set(r) {
        r.variables.foo = r.variables.arg_a;
        r.return(200, r.variables.foo);
    }

    function short_content_set(r) {
        r.var.foo = r.var.arg_a;
        r.return(200, r.var.foo);
    }

    function not_found_set(r) {
        try {
            r.variables.unknown = 1;
        } catch (e) {
            r.return(500, e);
        }
    }

    function variable_lowkey(r) {
        const name = 'X'.repeat(16);

        if (r.args.set) {
            r.variables[name] = "1";

        } else {
            let v = r.variables[name];
        }

        r.return(200, name);
    }

    export default {variable, short_var, content_set, short_content_set,
                    not_found_set, variable_lowkey};

EOF

$t->try_run('no njs')->plan(7);

###############################################################################

like(http_get('/var_set?a=bar'), qr/test_varbar/, 'var set');
like(http_get('/var_set_short?a=bar'), qr/short_varbar/,
    'short var set via r.var');
like(http_get('/content_set?a=bar'), qr/bar/, 'content set');
like(http_get('/content_set_short?a=bar'), qr/bar/,
    'short content set via r.var');
like(http_get('/not_found_set'), qr/variable not found/, 'not found exception');
like(http_get('/variable_lowkey'), qr/X{16}/,
	'variable name is not overwritten while reading');
like(http_get('/variable_lowkey?set=1'), qr/X{16}/,
	'variable name is not overwritten while setting');

###############################################################################
