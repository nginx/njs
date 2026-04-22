#!/usr/bin/perl

# (C) Dmitry Volyntsev
# (C) F5, Inc.

# Tests for http njs module, crypto module import.
# Regression test for Github issue #1049.

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

        location /hash {
            js_content test.hash;
        }

        location /hmac {
            js_content test.hmac;
        }
    }
}

EOF

$t->write_file('test.js', <<EOF);
    import cr from 'crypto';

    function hash(r) {
        r.return(200,
            cr.createHash('sha256').update('abc').digest('hex'));
    }

    function hmac(r) {
        r.return(200,
            cr.createHmac('sha256', 'key').update('message').digest('hex'));
    }

    export default {hash, hmac};
EOF

$t->try_run('no njs crypto module')->plan(2);

###############################################################################

like(http_get('/hash'),
	qr/ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad/,
	'crypto sha256 digest');

like(http_get('/hmac'),
	qr/6e9ef29b75fffc5b7abae527d58fdadb2fe42e7219011976917343065f58ed4a/,
	'crypto hmac-sha256 digest');

###############################################################################
