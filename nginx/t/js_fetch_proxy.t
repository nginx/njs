#!/usr/bin/perl

# (C) Dmitry Volyntsev
# (C) F5, Inc.

# Tests for http njs module, fetch method with forward proxy.

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

        location /engine {
            js_content test.engine;
        }

        location /http_via_proxy {
            js_fetch_proxy http://testuser:testpass@127.0.0.1:%%PORT_8081%%;
            js_content test.http_fetch;
        }

        location /http_no_proxy {
            js_content test.http_fetch;
        }

        location /http_via_proxy_no_auth {
            js_fetch_proxy http://127.0.0.1:%%PORT_8081%%;
            js_content test.http_fetch;
        }

        location /http_via_proxy_bad_auth {
            js_fetch_proxy http://wronguser:wrongpass@127.0.0.1:%%PORT_8081%%;
            js_content test.http_fetch_status;
        }

        location /http_via_proxy_encoded {
            js_fetch_proxy http://user%40domain:p%40ss%3Aword@127.0.0.1:%%PORT_8081%%;
            js_content test.http_fetch;
        }
    }

    server {
        listen       127.0.0.1:%%PORT_8081%%;
        server_name  localhost;

        location = /test {
            js_content test.endpoint;
        }
    }
}

EOF

my $p = port(8081);

$t->write_file('test.js', <<EOF);
    function engine(r) {
        r.return(200, njs.engine);
    }

    function endpoint(r) {
        let proxy_auth = r.headersIn['Proxy-Authorization'] || '';
        let valid_creds = [
            'Basic dGVzdHVzZXI6dGVzdHBhc3M=',
            'Basic dXNlckBkb21haW46cEBzczp3b3Jk'
        ];

        if (proxy_auth && !valid_creds.includes(proxy_auth)) {
            r.return(407, 'Proxy Authentication Required');
            return;
        }

        let s = `METHOD: \${r.method}\\n`;
        s += `URI: \${r.requestLine.split(" ")[1]}\\n`;

        if (proxy_auth) {
            s += `PROXY-AUTH: \${proxy_auth}\\n`;
        }

        s += 'BODY: ' + (r.requestText || '') + '\\n';

        r.return(200, s + 'ORIGIN:response');
    }

    async function http_fetch(r) {
        try {
            let domain = decodeURIComponent(r.args.domain);
            let method = r.method;
            let body = r.requestText;

            let reply = await ngx.fetch(`http://\${domain}:$p/test`,
                                        {method, body});
            body = await reply.text();
            r.return(200, body);
        } catch (e) {
            r.return(500, e.message);
        }
    }

    async function http_fetch_status(r) {
        try {
            let reply = await ngx.fetch('http://127.0.0.1:$p/test');
            r.return(200, 'STATUS:' + reply.status);
        } catch (e) {
            r.return(500, e.message);
        }
    }

    export default {engine, endpoint, http_fetch, http_fetch_status};

EOF

$t->try_run('no js_fetch_proxy')->plan(11);

###############################################################################

my $resp = http_get('/http_via_proxy?domain=127.0.0.1');
like($resp, qr/METHOD: GET/, 'proxy received GET method');
like($resp, qr/URI: http:\/\/127\.0\.0\.1:$p\/test/,
	'proxy received absolute-form URI');
like($resp, qr/PROXY-AUTH: Basic\s+dGVzdHVzZXI6dGVzdHBhc3M=/,
    'proxy Proxy-Authorization has expected Basic credentials');

$resp = http_post('/http_via_proxy?domain=127.0.0.1');
like($resp, qr/METHOD: POST/, 'proxy received POST method');
like($resp, qr/BODY: REQ-BODY/, 'proxy received request body');

like(http_get('/http_via_proxy?domain=example.com'),
	qr/URI: http:\/\/example\.com:/, 'proxy received example.com URI');

$resp = http_get('/http_via_proxy_no_auth?domain=127.0.0.1');
like($resp, qr/URI: http:\/\/127\.0\.0\.1:$p\/test/,
	'proxy received absolute-form URI');
unlike($resp, qr/PROXY-AUTH:/, 'proxy received no Proxy-Authorization header');

like(http_get('/http_via_proxy_bad_auth'), qr/STATUS:407/,
	'Proxy-Authorization is invalid');

like(http_get('/http_no_proxy?domain=127.0.0.1'),
	qr/ORIGIN:response/, 'origin response without proxy');

like(http_get('/http_via_proxy_encoded?domain=127.0.0.1'),
	qr/PROXY-AUTH: Basic\s+dXNlckBkb21haW46cEBzczp3b3Jk/,
	'encoded username and password with special chars decoded correctly');

###############################################################################

sub http_post {
	my ($url, %extra) = @_;

	my $p = "POST $url HTTP/1.0" . CRLF .
		"Host: localhost" . CRLF .
		"Content-Length: 8" . CRLF .
		CRLF .
		"REQ-BODY";

	return http($p, %extra);
}

###############################################################################
