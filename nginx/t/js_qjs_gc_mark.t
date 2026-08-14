#!/usr/bin/perl

# (C) Dmitry Volyntsev
# (C) F5, Inc.

# Tests for JavaScript references held by QuickJS nginx wrapper classes.

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

my $t = Test::Nginx->new()->has(qw/http stream stream_return/)
    ->write_file_expand('nginx.conf', <<'EOF');

%%TEST_GLOBALS%%

daemon off;

events {
}

http {
    %%TEST_GLOBALS_HTTP%%

    js_engine qjs;
    js_context_reuse 0;
    js_import test.js;

    server {
        listen       127.0.0.1:8080;
        server_name  localhost;

        location /cycle {
            js_content test.http_cycle;
        }

        location /request_body_cycle {
            js_content test.request_body_cycle;
        }

        location /response_body_cycle {
            js_content test.response_body_cycle;
        }

        location /body {
            return 200 body;
        }
    }
}

stream {
    %%TEST_GLOBALS_STREAM%%

    js_engine qjs;
    js_context_reuse 0;
    js_import test.js;

    server {
        listen      127.0.0.1:8081;
        js_preread  test.stream_cycle;
        return      OK;
    }
}

EOF

$t->write_file('test.js', <<'EOF');
    function http_cycle(r) {
        r.args.request = r;
        r.return(200, 'ok');
    }

    function request_body_cycle(r) {
        var body = r.requestBuffer;

        body.request = r;
        r.return(200, body.length);
    }

    async function response_body_cycle(r) {
        var reply = await r.subrequest('/body');
        var body = reply.responseBuffer;

        body.reply = reply;
        r.return(200, body.length);
    }

    function stream_cycle(s) {
        s.on('upload', function(data) {
            if (data.length != 0) {
                s.done();
            }
        });
    }

    export default {http_cycle, request_body_cycle, response_body_cycle,
                    stream_cycle};
EOF

$t->try_run('no QuickJS support')->plan(4);

###############################################################################

# Disabling reuse forces JS_FreeRuntime() after each request or session.
# A missing gc_mark leaves the cycle in the runtime and the harness catches
# the resulting worker abort through its implicit no-alerts check.

like(http_get('/cycle'), qr/200 OK/, 'HTTP request cycle');
like(http_post('/request_body_cycle'), qr/200 OK/, 'request body cycle');
like(http_get('/response_body_cycle'), qr/200 OK/, 'response body cycle');
is(stream('127.0.0.1:' . port(8081))->io('x'), 'OK',
    'stream session callback cycle');

###############################################################################

sub http_post {
    my ($uri) = @_;
    my $crlf = "\x0d\x0a";

    return http("POST $uri HTTP/1.0" . $crlf
        . "Host: localhost" . $crlf
        . "Content-Length: 4" . $crlf . $crlf
        . "body");
}

###############################################################################
