#!/usr/bin/perl

# (C) Dmitry Volyntsev
# (C) Nginx, Inc.

# Tests for stream njs module, setting nginx variables, server js_import.

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

    server {
        listen  127.0.0.1:8081;

        js_import main from foo.js;
        js_set $test_var main.variable;

        return  $test_var;
    }

    server {
        listen  127.0.0.1:8082;

        js_import main from bar.js;
        js_set $test_var main.variable;

        return  $test_var;
    }

    server {
        listen  127.0.0.1:8083;

        return  "NOT_FOUND:$test_var";
    }
}

EOF

$t->write_file('foo.js', <<EOF);
    function variable(s) {
        return 'foo_var';
    }

    export default {variable};

EOF

$t->write_file('bar.js', <<EOF);
    function variable(s) {
        return 'bar_var';
    }

    export default {variable};

EOF

$t->try_run('no stream njs available')->plan(4);

###############################################################################

is(stream('127.0.0.1:' . port(8081))->read(), 'foo_var', 'foo var');
is(stream('127.0.0.1:' . port(8082))->read(), 'bar_var', 'bar var');
is(stream('127.0.0.1:' . port(8083))->read(), 'NOT_FOUND:', 'not found var');

$t->stop();

ok(index($t->read_file('error.log'),
	'no "js_import" or inline expression found for "js_set" handler '
	. '"main.variable"') > 0,
	'log error for js_set without js_import');

###############################################################################
