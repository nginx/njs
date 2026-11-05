#!/usr/bin/perl

# (C) Dmitry Volyntsev
# (C) F5, Inc.

# Tests for QuickJS Headers ownership across requests.

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

    js_context_reuse 1;
    js_import test.js;

    server {
        listen       127.0.0.1:8080;
        server_name  localhost;

        location /engine {
            js_content test.engine;
        }

        location / {
            js_content test.content;
        }
    }
}

EOF

$t->write_file('test.js', <<'EOF');
    let headers = new Headers({value: 'original'});
    for (let i = 0; i < 12; i++) {
        headers.append('header-' + i, String(i));
    }

    function engine(r) {
        r.return(200, njs.engine);
    }

    function content(r) {
        switch (r.args.op) {
        case 'append':
            headers.append('extra', 'one');
            const copy = new Headers(headers);
            copy.set('value', 'copy');
            if (headers.get('value') != 'original') {
                throw new Error('Headers copy aliases its input');
            }
            break;

        case 'set':
            headers.set('value', new Uint8Array([110, 101, 119]));
            break;

        case 'delete':
            headers.delete('value');
            break;

        case 'restore':
            headers.append('value', 'restored');
            for (let i = 0; i < 20; i++) {
                headers.set('extra-' + i, String(i));
                headers.delete('extra-' + i);
            }
            break;

        case 'release':
            headers = null;
            r.return(200, 'released');
            return;

        case 'views':
            let source = new Headers({a: 'copied'});
            const request = new Request('http://localhost/', {
                headers: source
            });
            const response = new Response('body', {headers: source});
            source = null;
            r.return(200, request.headers.get('a') + ':'
                          + response.headers.get('a'));
            return;
        }

        r.return(200, headers.get('value') + ':' + headers.get('extra'));
    }

    export default {content, engine};

EOF

$t->try_run('no njs available');
plan(skip_all => 'QuickJS-specific lifetime tests')
    if http_get('/engine') =~ /njs$/m;
$t->plan(9);

###############################################################################

like(http_get('/?op=read'), qr/original:null$/, 'module-level Headers');
like(http_get('/?op=read'), qr/original:null$/,
    'Headers after request cleanup');
like(http_get('/?op=append'), qr/original:one$/, 'append and independent copy');
like(http_get('/?op=set'), qr/new:one$/, 'set from a TypedArray');
like(http_get('/?op=delete'), qr/null:one$/, 'delete');
like(http_get('/?op=read'), qr/null:one$/, 'modified Headers survive cleanup');
like(http_get('/?op=restore'), qr/restored:one$/, 'reuse deleted entries');
like(http_get('/?op=views'), qr/copied:copied$/,
    'copy Headers to Request/Response');
like(http_get('/?op=release'), qr/released$/, 'release retained Headers');

###############################################################################
