#!/usr/bin/perl

# (C) Vadim Zhestikov
# (C) F5, Inc.

# Tests for QuickJS ArrayBuffer body accessors with context reuse.
# A buffer retained in module scope must own its bytes: the pools its
# data was read from are destroyed at the end of the request, while the
# context is reused by the next one.

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

        location /test {
            js_context_reuse 1;
            client_body_buffer_size 64k;
            js_content test.run;
        }

        location /a.bin {
        }

        location /b.bin {
        }

        location /empty.bin {
        }
    }
}

EOF

my $p0 = port(8080);

$t->write_file('test.js', <<EOF);
    var retained;

    function verify(view) {
        /* slice() copies the bytes with memcpy(). */
        var copy = view.slice();

        if (copy.length != 16384) {
            return 'length ' + copy.length;
        }

        for (var i = 0; i < copy.length; i++) {
            if (copy[i] != 0x61) {
                return 'corrupted at ' + i;
            }
        }

        return 'intact';
    }

    async function run(r) {
        var ab, reply;

        switch (r.args.op) {
        case 'response':
            reply = await ngx.fetch('http://127.0.0.1:$p0/a.bin');
            ab = await reply.arrayBuffer();
            break;

        case 'request':
            ab = await new Request('http://127.0.0.1:$p0/', {
                method: 'POST',
                body: 'a'.repeat(16384)
            }).arrayBuffer();
            break;

        case 'read_body':
            ab = await r.readRequestArrayBuffer();
            break;

        case 'check':
            /* Reallocates the memory freed by the previous request. */
            reply = await ngx.fetch('http://127.0.0.1:$p0/b.bin');
            await reply.arrayBuffer();

            r.return(200, verify(retained));
            return;

        case 'empty':
            reply = await ngx.fetch('http://127.0.0.1:$p0/empty.bin');

            r.return(200, [
                (await reply.arrayBuffer()).byteLength,
                (await new Request('http://127.0.0.1:$p0/').arrayBuffer())
                    .byteLength,
                (await r.readRequestArrayBuffer()).byteLength
            ].join(','));
            return;
        }

        retained = new Uint8Array(ab);

        r.return(200, verify(retained));
    }

    export default {run};

EOF

$t->write_file('a.bin', 'a' x 16384);
$t->write_file('b.bin', 'b' x 16384);
$t->write_file('empty.bin', '');

$t->try_run('no QuickJS support')->plan(7);

###############################################################################

like(http_get('/test?op=response'), qr/intact$/s, 'response retained');
like(http_get('/test?op=check'), qr/intact$/s, 'response after reuse');

like(http_get('/test?op=request'), qr/intact$/s, 'request retained');
like(http_get('/test?op=check'), qr/intact$/s, 'request after reuse');

like(http_post('/test?op=read_body', 'a' x 16384), qr/intact$/s,
	'request body retained');
like(http_get('/test?op=check'), qr/intact$/s, 'request body after reuse');

like(http_get('/test?op=empty'), qr/0,0,0$/s, 'empty bodies');

###############################################################################

sub http_post {
	my ($uri, $body) = @_;

	return http(<<EOF . $body);
POST $uri HTTP/1.0
Host: localhost
Content-Length: @{[ length $body ]}

EOF
}

###############################################################################
