#!/usr/bin/perl

# (C) Thomas P.

# Tests for stream njs module, setting non-cacheable nginx variables.

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

    js_set $nocache_var   test.variable nocache;
    js_set $default_var   test.variable;
    js_set $callcount_var test.callCount nocache;

    js_import test.js;

    server {
        listen  127.0.0.1:8081;
        set     $a $default_var;
        set     $b $default_var;
        return  '"$a/$b"';
    }

    server {
        listen  127.0.0.1:8082;
        set     $a $nocache_var;
        set     $b $nocache_var;
        return  '"$a/$b"';
    }

    server {
        listen  127.0.0.1:8083;
        set     $a $callcount_var;
        set     $b $callcount_var;
        return  '"$a/$b"';
    }
}

EOF

$t->write_file('test.js', <<EOF);
    function variable(r) {
        return Math.random().toFixed(16);
    }

    let n = 0;
    function callCount(r) {
        return (n += 1).toString();
    }

    export default {variable, callCount};

EOF

$t->try_run('no nocache stream njs variables')->plan(3);

###############################################################################

# We use backreferences to make sure the same value was returned for the two uses
like(stream('127.0.0.1:' . port(8081))->read(), qr/"(.+)\/\1"/, 'cached variable');
# Negative lookaheads don't capture, hence the .+ after it
like(stream('127.0.0.1:' . port(8082))->read(), qr/"(.+)\/(?!\1).+"/, 'noncacheable variable');
like(stream('127.0.0.1:' . port(8083))->read(), qr/"1\/2"/, 'callcount variable');

$t->stop();

###############################################################################
