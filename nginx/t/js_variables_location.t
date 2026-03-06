#!/usr/bin/perl

# (C) Dmitry Volyntsev
# (C) Nginx, Inc.

# Tests for http njs module, setting nginx variables, location js_import.

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

    server {
        listen       127.0.0.1:8080;
        server_name  localhost;

        location /foo {
            js_import main from foo.js;
            js_set $test_var   main.variable;

            return 200 $test_var;
        }

        location /bar {
            js_import main from bar.js;
            js_set $test_var   main.variable;

            return 200 $test_var;
        }

        location /not_found {
            return 200 "NOT_FOUND:$test_var";
        }
    }
}

EOF

$t->write_file('foo.js', <<EOF);
    function variable(r) {
        return 'foo_var';
    }

    export default {variable};

EOF

$t->write_file('bar.js', <<EOF);
    function variable(r) {
        return 'bar_var';
    }

    export default {variable};

EOF

$t->try_run('no njs')->plan(4);

###############################################################################

like(http_get('/foo'), qr/foo_var/, 'foo var');
like(http_get('/bar'), qr/bar_var/, 'bar var');
like(http_get('/not_found'), qr/NOT_FOUND:$/, 'not found is empty');

$t->stop();

ok(index($t->read_file('error.log'),
	'no "js_import" or inline expression found for "js_set" handler '
	. '"main.variable"') > 0,
	'log error for js_set without js_import');

###############################################################################
