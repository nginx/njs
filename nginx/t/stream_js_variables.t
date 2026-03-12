#!/usr/bin/perl

# (C) Dmitry Volyntsev
# (C) Nginx, Inc.

# Tests for stream njs module, setting nginx variables.

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

my $t = Test::Nginx->new()->has(qw/stream stream_return/)
	->write_file_expand('nginx.conf', <<'EOF');

%%TEST_GLOBALS%%

daemon off;

events {
}

stream {
    %%TEST_GLOBALS_STREAM%%

    js_set $test_var       test.variable;
    js_set $test_short_var test.short_var;
    js_set $test_not_found test.not_found;
    js_set $test_short_not_found test.short_not_found;

    js_import test.js;

    server {
        listen  127.0.0.1:8081;
        return  $test_var$status;
    }

    server {
        listen  127.0.0.1:8082;
        return  $test_not_found;
    }

    server {
        listen  127.0.0.1:8083;
        return  $test_short_var$status;
    }

    server {
        listen  127.0.0.1:8084;
        return  $test_short_not_found;
    }
}

EOF

$t->write_file('test.js', <<EOF);
    function variable(s) {
        s.variables.status = 400;
        return 'test_var';
    }

    function short_var(s) {
        s.var.status = 401;
        return 'short_var';
    }

    function not_found(s) {
        try {
            s.variables.unknown = 1;
        } catch (e) {
            return 'not_found';
        }
    }

    function short_not_found(s) {
        try {
            s.var.unknown = 1;
        } catch (e) {
            return 'short_not_found';
        }
    }

    export default {variable, short_var, not_found, short_not_found};

EOF

$t->try_run('no stream njs available')->plan(4);

###############################################################################

is(stream('127.0.0.1:' . port(8081))->read(), 'test_var400', 'var set');
is(stream('127.0.0.1:' . port(8082))->read(), 'not_found', 'not found set');
is(stream('127.0.0.1:' . port(8083))->read(), 'short_var401',
   'short var set via s.var');
is(stream('127.0.0.1:' . port(8084))->read(), 'short_not_found',
   'short not found set via s.var');

$t->stop();

###############################################################################
