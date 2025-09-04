#!/usr/bin/perl

# (C) Dmitry Volyntsev
# (C) F5, Inc.

# Tests for stream njs module, fetch method keepalive.

###############################################################################

use warnings;
use strict;

use Test::More;
use IO::Socket::INET;

BEGIN { use FindBin; chdir($FindBin::Bin); }

use lib 'lib';
use Test::Nginx;
use Test::Nginx::Stream qw/ stream /;

###############################################################################

select STDERR; $| = 1;
select STDOUT; $| = 1;

my $t = Test::Nginx->new()->has(qw/http stream/)
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

        location /engine {
            js_content test.engine;
        }
    }

    server {
        listen       127.0.0.1:8081;
        keepalive_requests 100;
        keepalive_timeout 60s;

        location /count {
            add_header Connection-ID $connection_requests;
            return 200 $connection_requests;
        }

        location /headers {
            return 200 "Connection: $http_connection";
        }
    }
}

stream {
    %%TEST_GLOBALS_STREAM%%

    js_import test.js;
    js_var $message;

    server {
        listen      127.0.0.1:8082;
        js_fetch_keepalive 4;
        js_fetch_keepalive_requests 100;
        js_fetch_keepalive_time 60s;
        js_fetch_keepalive_timeout 60s;
        js_preread  test.keepalive;
        return      $message;
    }

    server {
        listen      127.0.0.1:8083;
        js_fetch_keepalive 0;
        js_preread  test.keepalive;
        return      $message;
    }

    server {
        listen      127.0.0.1:8084;
        js_fetch_keepalive 4;
        js_fetch_keepalive_requests 2;
        js_preread  test.keepalive;
        return      $message;
    }

    server {
        listen      127.0.0.1:8085;
        js_fetch_keepalive 4;
        js_fetch_keepalive_time 100ms;
        js_preread  test.keepalive;
        return      $message;
    }

    server {
        listen      127.0.0.1:8086;
        js_fetch_keepalive 4;
        js_fetch_keepalive_timeout 100ms;
        js_preread  test.keepalive;
        return      $message;
    }

    server {
        listen      127.0.0.1:8087;
        js_fetch_keepalive 4;
        js_preread  test.keepalive_simultaneous;
        return      $message;
    }
}

EOF

my $p1 = port(8081);

$t->write_file('test.js', <<EOF);
    function engine(r) {
        r.return(200, njs.engine);
    }

    async function keepalive(s) {
        let responses = [];

        for (let i = 0; i < 3; i++) {
            let resp = await ngx.fetch('http://127.0.0.1:$p1/count');
            let body = await resp.text();
            responses.push(parseInt(body.trim()));
        }

        s.variables.message = JSON.stringify(responses);
        s.done();
    }

    async function keepalive_simultaneous(s) {
        let promises = [];
        let n = 8;
        for (let i = 0; i < n; i++) {
            promises.push(ngx.fetch('http://127.0.0.1:$p1/count'));
        }

        let results = await Promise.all(promises);
        let bodies = await Promise.all(results.map(r => r.text()));
        let responses = bodies.map(b => parseInt(b.trim()));

        s.variables.message = JSON.stringify(responses);
        s.done();
    }

    export default {engine, keepalive, keepalive_simultaneous};
EOF

$t->try_run('no stream js_fetch_keepalive');

$t->plan(10);

###############################################################################

like(stream('127.0.0.1:' . port(8083))->io('GO'), qr/\[1,1,1]/,
    'no keepalive connections');
like(stream('127.0.0.1:' . port(8082))->io('GO'), qr/\[1,2,3]/,
    'keepalive reuses connection');
like(stream('127.0.0.1:' . port(8082))->io('GO'), qr/\[4,5,6]/,
    'keepalive reuses connection across sessions');

like(stream('127.0.0.1:' . port(8087))->io('GO'), qr/^\[(1,){7}1\]$/,
    'keepalive simultaneous requests');
like(stream('127.0.0.1:' . port(8087))->io('GO'),
    qr/\[2,2,2,2,1,1,1,1\]/,
    'keepalive simultaneous requests reused connections');

like(stream('127.0.0.1:' . port(8084))->io('GO'), qr/\[1,2,1]/,
    'keepalive with limited requests per connection');

like(stream('127.0.0.1:' . port(8085))->io('GO'), qr/\[1,2,3]/,
    'keepalive with time limit, first round');

select undef, undef, undef, 0.15;

like(stream('127.0.0.1:' . port(8085))->io('GO'), qr/\[4,1,2]/,
    'keepalive with time limit, second round');

like(stream('127.0.0.1:' . port(8086))->io('GO'), qr/\[1,2,3]/,
    'keepalive with timeout limit, first round');

select undef, undef, undef, 0.15;

like(stream('127.0.0.1:' . port(8086))->io('GO'), qr/\[1,2,3]/,
    'keepalive with timeout limit, second round');

###############################################################################
