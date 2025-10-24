#!/usr/bin/perl

# (C) Dmitry Volyntsev
# (C) F5, Inc.

# Tests for http njs module, fetch method with variable proxy URLs.

###############################################################################

use warnings;
use strict;

use Test::More;

use Socket qw/ CRLF /;

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

        location /static_proxy {
            js_fetch_proxy http://testuser:testpass@127.0.0.1:%%PORT_8081%%;
            js_content test.http_fetch;
        }

        location /dynamic_proxy {
            set $proxy_url http://testuser:testpass@127.0.0.1:%%PORT_8081%%;
            js_fetch_proxy $proxy_url;
            js_content test.http_fetch;
        }

        location /dynamic_empty_proxy {
            set $proxy_url "";
            js_fetch_proxy $proxy_url;
            js_content test.http_fetch;
        }
    }

    server {
        listen       127.0.0.1:%%PORT_8081%%;
        server_name  localhost;

        location = /test {
            js_content test.proxy_endpoint;
        }
    }

    server {
        listen       127.0.0.1:%%PORT_8082%%;
        server_name  localhost;

        location = /test {
            js_content test.origin_endpoint;
        }
    }
}

EOF

my $p1 = port(8081);
my $p2 = port(8082);

$t->write_file('test.js', <<EOF);
    function proxy_endpoint(r) {
        let proxy_auth = r.headersIn['Proxy-Authorization'] || '';
        let expected = 'Basic dGVzdHVzZXI6dGVzdHBhc3M=';

        if (!proxy_auth) {
            r.return(500, 'PROXY:NO-AUTH');
            return;
        }

        if (proxy_auth !== expected) {
            r.return(407, 'PROXY:BAD-AUTH');
            return;
        }

        r.return(200, 'PROXY:' + proxy_auth);
    }

    function origin_endpoint(r) {
        let proxy_auth = r.headersIn['Proxy-Authorization'] || '';

        if (proxy_auth) {
            r.return(500, 'ORIGIN:HAS-PROXY-AUTH');
            return;
        }

        r.return(200, 'ORIGIN:OK');
    }

    async function http_fetch(r) {
        try {
            let reply = await ngx.fetch('http://127.0.0.1:$p2/test');
            let body = await reply.text();
            r.return(200, body);
        } catch (e) {
            r.return(500, e.message);
        }
    }

    export default {proxy_endpoint, origin_endpoint, http_fetch};

EOF

$t->try_run('no js_fetch_proxy')->plan(3);

###############################################################################

like(http_get('/static_proxy'), qr/PROXY:Basic\s+dGVzdHVzZXI6dGVzdHBhc3M=/,
    'static proxy URL with auth');
like(http_get('/dynamic_proxy'), qr/PROXY:Basic\s+dGVzdHVzZXI6dGVzdHBhc3M=/,
    'dynamic proxy URL with auth');
like(http_get('/dynamic_empty_proxy'), qr/ORIGIN:OK/,
    'dynamic empty proxy URL bypasses proxy');

###############################################################################
