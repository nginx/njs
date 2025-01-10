#!/usr/bin/perl

# (C) Dmitry Volyntsev
# (C) Nginx, Inc.

# Tests for http njs module, WebCrypto module.

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

my $t = Test::Nginx->new()->has(qw/http/)
	->write_file_expand('nginx.conf', <<'EOF');

%%TEST_GLOBALS%%

daemon off;

events {
}

http {
    %%TEST_GLOBALS_HTTP%%

    js_import test.js;

    server {
        listen       127.0.0.1:8080;
        server_name  localhost;

        location /random_values_test {
            js_content test.random_values_test;
        }
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

    function random_values_test(r) {
        let buf = new Uint32Array(32);
        crypto.getRandomValues(buf);
        let bits1 = buf.reduce((a, v)=> a + count1(v), 0);
        let nbits = buf.length * 32;
        let mean = nbits / 2;
        let stdd = Math.sqrt(nbits / 4);

        r.return(200, bits1 > (mean - 10 * stdd) && bits1 < (mean + 10 * stdd));
    }

    export default {random_values_test};

EOF

$t->try_run('no njs')->plan(1);

###############################################################################

like(http_get('/random_values_test'), qr/true/, 'random_values_test');

###############################################################################
