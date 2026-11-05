#!/usr/bin/perl

# (C) Dmitry Volyntsev
# (C) F5, Inc.

# Tests for QuickJS Fetch object ownership independent of request pools.

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

    js_import test.js;

    server {
        listen       127.0.0.1:8080;
        server_name  localhost;

        location /engine {
            js_content test.engine;
        }

        location /retain {
            js_context_reuse 1;
            js_content test.content;
        }

        location /global {
            js_context_reuse 1;
            js_import global.js;
            js_content global.content;
        }

        location /zero {
            js_context_reuse 0;
            js_import global.js;
            js_content global.content;
        }

        location /target {
            add_header X-Test network;
            js_content test.target;
        }
    }
}

EOF

my $port = port(8080);

$t->write_file('test.js', <<EOF);
    let retained;

    function engine(r) {
        r.return(200, njs.engine);
    }

    function target(r) {
        r.return(200, 'x'.repeat(65536));
    }

    async function content(r) {
        switch (r.args.op) {
        case 'request':
            let initial = new Headers({x: 'request'});
            retained = new Request('http://localhost/request', {
                method: 'POST', body: 'abc', headers: initial
            });
            initial.set('x', 'changed');
            initial = null;
            break;

        case 'copy_request':
            let original = new Request('http://localhost/request', {
                method: 'POST', body: 'abc', headers: {x: 'request'}
            });
            retained = new Request(original);
            original.headers.set('x', 'changed');
            original = null;
            break;

        case 'read_request':
            r.return(200, retained.method + ':' + retained.headers.get('x')
                          + ':' + await retained.text() + ':'
                          + retained.url.endsWith('/request'));
            return;

        case 'response':
            retained = new Response('abc', {
                status: 201, statusText: 'ready', headers: {x: 'response'}
            });
            break;

        case 'read_response':
            r.return(200, retained.status + ':' + retained.statusText + ':'
                          + retained.headers.get('x') + ':'
                          + await retained.text());
            return;

        case 'response_headers':
            let response = new Response('unread'.repeat(4096), {
                headers: {x: 'response'}
            });
            retained = response.headers;
            response = null;
            break;

        case 'request_headers':
            let request = new Request('http://localhost/request', {
                method: 'POST', body: 'unread'.repeat(4096),
                headers: {x: 'request'}
            });
            retained = request.headers;
            request = null;
            break;

        case 'read_headers':
            r.return(200, retained.get('x'));
            return;

        case 'change_headers':
            retained.set('x', 'changed');
            r.return(200, retained.get('x'));
            return;

        case 'fetch':
            retained = await ngx.fetch('http://127.0.0.1:$port/target');
            break;

        case 'read_fetch':
            r.return(200, retained.status + ':' + retained.statusText + ':'
                          + retained.headers.get('x-test') + ':'
                          + (await retained.text()).length);
            return;

        case 'fetch_headers':
            let reply = await ngx.fetch('http://127.0.0.1:$port/target');
            retained = reply.headers;
            reply = null;
            break;

        case 'read_fetch_headers':
            let immutable = false;
            try {
                retained.set('x-test', 'changed');
            } catch (e) {
                immutable = true;
            }

            r.return(200, retained.get('x-test') + ':' + immutable);
            return;

        case 'errors':
            for (let i = 0; i < 20; i++) {
                try {
                    new Response('body', {
                        headers: [['a', 'b'], ['invalid\\n', 'value']]
                    });
                } catch (e) {}

                try {
                    new Request('invalid');
                } catch (e) {}
            }
            break;

        case 'release':
            retained = null;
            break;
        }

        r.return(200, 'ok');
    }

    export default {content, target, engine};

EOF

$t->write_file('global.js', <<'EOF');
    const input = new Uint8Array([97, 98, 99]);
    const request = new Request('http://localhost/request', {
        method: 'POST', body: input, headers: {x: 'request'}
    });
    const response = new Response(input, {
        status: 201, statusText: 'ready', headers: {x: 'response'}
    });
    input[0] = 88;

    async function content(r) {
        if (r.args.op == 'body') {
            r.return(200, await request.text() + ':' + await response.text());
            return;
        }

        r.return(200, request.method + ':' + request.headers.get('x') + ':'
                      + response.status + ':' + response.statusText + ':'
                      + response.headers.get('x') + ':'
                      + (request.headers === request.headers) + ':'
                      + (response.headers === response.headers));
    }

    export default {content};

EOF

my @cases = (
    ['/global?op=meta', qr/POST:request:201:ready:response:true:true$/,
        'global constructors and getter identity'],
    ['/global?op=meta', qr/POST:request:201:ready:response:true:true$/,
        'global metadata after request cleanup'],
    ['/global?op=body', qr/abc:abc$/, 'BufferSource snapshots survive cleanup'],
    ['/zero?op=body', qr/abc:abc$/, 'constructor bodies without reuse'],
    ['/retain?op=request', qr/ok$/, 'retain Request'],
    ['/retain?op=read_request', qr/POST:request:abc:true$/,
        'read retained Request'],
    ['/retain?op=copy_request', qr/ok$/, 'copy Request and release source'],
    ['/retain?op=read_request', qr/POST:request:abc:true$/,
        'read copied Request'],
    ['/retain?op=response', qr/ok$/, 'retain Response'],
    ['/retain?op=read_response', qr/201:ready:response:abc$/,
        'read retained Response'],
    ['/retain?op=request_headers', qr/ok$/, 'retain Headers without Request'],
    ['/retain?op=read_headers', qr/request$/,
        'read Headers after Request release'],
    ['/retain?op=response_headers', qr/ok$/, 'retain Headers without Response'],
    ['/retain?op=read_headers', qr/response$/,
        'read Headers after Response release'],
    ['/retain?op=change_headers', qr/changed$/, 'modify surviving Headers'],
    ['/retain?op=fetch', qr/ok$/, 'retain network Response'],
    ['/retain?op=read_fetch', qr/200:OK:network:65536$/,
        'read retained network body'],
    ['/retain?op=fetch_headers', qr/ok$/, 'release Response with unread body'],
    ['/retain?op=read_fetch_headers', qr/network:true$/,
        'network Headers survive with immutable guard'],
    ['/retain?op=errors', qr/ok$/, 'partial constructor cleanup'],
    ['/retain?op=release', qr/ok$/, 'release retained object'],
);

$t->try_run('no njs available');
plan(skip_all => 'QuickJS-specific lifetime tests')
    if http_get('/engine') =~ /njs$/m;
$t->plan(scalar @cases);

###############################################################################

for my $case (@cases) {
    like(http_get($case->[0]), $case->[1], $case->[2]);
}

###############################################################################
