#!/usr/bin/perl

# (C) Dmitry Volyntsev
# (C) F5, Inc.

# Tests for QuickJS context reuse.  An unhandled rejection must keep being
# reported after the per-worker context is reused, not only on its first use.

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

        location /reject {
            js_content test.reject;
        }
    }
}

EOF

$t->write_file('test.js', <<EOF);
    function reject(r) {
        Promise.reject(new Error('unhandled-boom'));
        r.return(200, 'ok');
    }

    export default { reject };

EOF

$t->try_run('no njs available')->plan(4);

###############################################################################

# The first request runs in a fresh context, the following ones reuse it
# (with the QuickJS engine).  Every unhandled rejection must be reported.

like(http_get('/reject'), qr/200 OK/, 'reject 1 (fresh context)');
like(http_get('/reject'), qr/200 OK/, 'reject 2 (reused context)');
like(http_get('/reject'), qr/200 OK/, 'reject 3 (reused context)');

$t->stop();

my $log = $t->read_file('error.log');
my $count = () = $log =~ /js unhandled rejection/g;

is($count, 3, 'unhandled rejection reported for every request');

###############################################################################
