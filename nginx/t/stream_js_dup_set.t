#!/usr/bin/perl

# (C) Dmitry Volyntsev
# (C) Nginx, Inc.

# Tests for stream njs module, duplicate identical js_set directives.

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

    js_import test.js;

    server {
        listen  127.0.0.1:8081;
        js_set $test test.foo;
        return  8081:$test;
    }

    server {
        listen  127.0.0.1:8082;
        js_set $test test.foo;
        return  8082:$test;
    }
}

EOF

$t->write_file('test.js', <<EOF);
    function foo(r) {
        return 42;
    }

    export default {foo};

EOF

$t->try_run('no njs available')->plan(2);

###############################################################################

is(stream('127.0.0.1:' . port(8081))->read(), '8081:42', '8081 server');
is(stream('127.0.0.1:' . port(8082))->read(), '8082:42', '8082 server');

###############################################################################
