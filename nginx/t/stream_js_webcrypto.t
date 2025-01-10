#!/usr/bin/perl

# (C) Dmitry Volyntsev
# (C) Nginx, Inc.

# Tests for stream njs module, WebCrypto module.

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

    js_set $test test.random_values_test;

    server {
        listen  127.0.0.1:8081;
        return  $test;
    }
}

EOF

$t->write_file('test.js', <<EOF);
    function count1(v) {
        return v.toString(2).match(/1/g).length;
    }

    /*
     * Statistic test
     * bits1 is a random variable with Binomial distribution
     * Expected value is N / 2
     * Standard deviation is sqrt(N / 4)
     */

    function random_values_test(s) {
        let buf = new Uint32Array(32);
        crypto.getRandomValues(buf);
        let bits1 = buf.reduce((a, v)=> a + count1(v), 0);
        let nbits = buf.length * 32;
        let mean = nbits / 2;
        let stdd = Math.sqrt(nbits / 4);

        return bits1 > (mean - 10 * stdd) && bits1 < (mean + 10 * stdd);
    }

    export default {random_values_test};
EOF

$t->try_run('no stream js_var')->plan(1);

###############################################################################

is(stream('127.0.0.1:' . port(8081))->io('###'), 'true', 'random values test');

###############################################################################
