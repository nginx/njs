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
            js_fetch_proxy http://127.0.0.1:%%PORT_8081%%;
            js_fetch_proxy_auth_basic testuser testpass;
            js_content test.http_fetch;
        }

        location /http_no_proxy {
            js_content test.http_fetch;
        }

        location /http_via_proxy_bad_auth {
            js_fetch_proxy http://127.0.0.1:%%PORT_8081%%;
            js_fetch_proxy_auth_basic wronguser wrongpass;
            js_content test.http_fetch_status;
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
		if (proxy_auth && proxy_auth !== 'Basic dGVzdHVzZXI6dGVzdHBhc3M=') {
			r.return(407, 'Proxy Authentication Required');
			return;
		}

		let s = `METHOD: \${r.method}\\n`;
		s += `URI: \${r.requestLine.split(" ")[1]}\\n`;

		for (let h in r.headersIn) {
			s += h + ': ' + r.headersIn[h] + '\\n';
		}

		r.return(200, s + 'ORIGIN:response');
	}

    async function http_fetch(r) {
        try {
            let domain = decodeURIComponent(r.args.domain);
            let reply = await ngx.fetch(`http://\${domain}:$p/test`);
            let body = await reply.text();
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

$t->try_run('no njs.fetch')->plan(6);

###############################################################################

my $resp = http_get('/http_via_proxy?domain=127.0.0.1');
like($resp, qr/METHOD: GET/, 'proxy received GET method');
like($resp, qr/URI: http:\/\/127\.0\.0\.1:$p\/test/,
	'proxy received absolute-form URI');
like($resp, qr/Proxy-Authorization: Basic\s+dGVzdHVzZXI6dGVzdHBhc3M=/,
    'proxy Proxy-Authorization has expected Basic credentials');

like(http_get('/http_via_proxy?domain=example.com'),
	qr/URI: http:\/\/example\.com:/, 'proxy received example.com URI');

like(http_get('/http_via_proxy_bad_auth'), qr/STATUS:407/,
	'Proxy-Authorization is invalid');

$resp = http_get('/http_no_proxy?domain=127.0.0.1');
like($resp, qr/URI: \/test/, 'no proxy, origin URI is origin-form');

###############################################################################
