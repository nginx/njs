#!/usr/bin/perl

# (C) Dmitry Volyntsev
# (C) F5, Inc.

# Tests for QuickJS Fetch object finalization after pool destruction.

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
worker_processes 1;

events {
}

http {
    %%TEST_GLOBALS_HTTP%%

    js_engine qjs;
    js_import test.js;

    server {
        listen       127.0.0.1:8080;
        server_name  localhost;

        location /reuse {
            js_context_reuse 1;
            js_content test.reuse;
        }

        location /no_reuse {
            js_context_reuse 0;
            js_content test.no_reuse;
        }

        location /target {
            add_header X-Test preserved;
            return 200 body;
        }
    }
}

EOF

my $p0 = port(8080);

$t->write_file('test.js', <<EOF);
    var retained;

    async function reuse(r) {
        var result = 'ok';

        switch (r.args.op) {
        case 'request':
            retained = new Request('http://127.0.0.1:$p0/', {
                headers: {x: 'request'}
            });
            result = retained.headers.get('x');
            break;

        case 'response':
            retained = new Response('response', {
                headers: {x: 'response'}
            });
            result = retained.headers.get('x');
            break;

        case 'fetch':
            retained = await ngx.fetch('http://127.0.0.1:$p0/target');
            result = retained.headers.get('x-test');
            break;

        case 'release':
            retained = null;
            break;
        }

        r.return(200, result);
    }

    async function no_reuse(r) {
        var object;

        switch (r.args.type) {
        case 'request':
            object = new Request('http://127.0.0.1:$p0/');
            break;

        case 'response':
            object = new Response('response');
            break;

        case 'fetch':
            object = await ngx.fetch('http://127.0.0.1:$p0/target');
            break;

        case 'request_headers':
            object = new Request('http://127.0.0.1:$p0/');
            object.headers.owner = object;
            r.return(200, 'ok');
            return;

        case 'response_headers':
            object = new Response('response');
            object.headers.owner = object;
            r.return(200, 'ok');
            return;
        }

        object.self = object;
        r.return(200, 'ok');
    }

    export default {reuse, no_reuse};
EOF

$t->try_run('no QuickJS support')->plan(11);

###############################################################################

like(http_get('/reuse?op=request'), qr/request$/s, 'retain Request');
like(http_get('/reuse?op=release'), qr/200 OK/, 'release Request');

like(http_get('/reuse?op=response'), qr/response$/s, 'retain Response');
like(http_get('/reuse?op=release'), qr/200 OK/, 'release Response');

like(http_get('/reuse?op=fetch'), qr/preserved$/s, 'retain fetch Response');
like(http_get('/reuse?op=release'), qr/200 OK/, 'release fetch Response');

like(http_get('/no_reuse?type=request'), qr/200 OK/, 'Request cycle');
like(http_get('/no_reuse?type=response'), qr/200 OK/, 'Response cycle');
like(http_get('/no_reuse?type=fetch'), qr/200 OK/, 'fetch Response cycle');
like(http_get('/no_reuse?type=request_headers'), qr/200 OK/,
    'Request Headers cycle');
like(http_get('/no_reuse?type=response_headers'), qr/200 OK/,
    'Response Headers cycle');

###############################################################################
