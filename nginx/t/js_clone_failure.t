#!/usr/bin/perl

# (C) Dmitry Volyntsev
# (C) F5, Inc.

# Tests for cleanup after a JavaScript context clone failure.

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

    js_engine njs;
    js_import test.js;

    server {
        listen       127.0.0.1:8080;
        server_name  localhost;

        location /failed {
            js_content test.content;
            js_body_filter test.filter;
        }

        location /alive {
            return 200 alive;
        }
    }
}

EOF

$t->write_file('test.js', <<'EOF');
    Promise.reject(new Error('unhandled'));
    throw new Error('clone failed');

    function content(r) {
        r.return(200, 'unexpected');
    }

    function filter(r, data, flags) {
        r.sendBuffer(data, flags);
    }

    export default { content, filter };

EOF

$t->try_run('no njs available')->plan(2);

###############################################################################

is(http_get('/failed'), '', 'clone failure');
like(http_get('/alive'), qr/200 OK/, 'worker remains alive');

###############################################################################
