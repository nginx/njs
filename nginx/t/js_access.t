#!/usr/bin/perl

# (C) Dmitry Volyntsev
# (C) F5, Inc.

# Tests for http njs module, js_access directive.

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

my $t = Test::Nginx->new()->has(qw/http rewrite proxy/)
	->write_file_expand('nginx.conf', <<'EOF');

%%TEST_GLOBALS%%

daemon off;

events {
}

http {
    %%TEST_GLOBALS_HTTP%%

    js_import test.js;

    js_var $foo;
    js_var $upstream;

    server {
        listen       127.0.0.1:8080;
        server_name  localhost;

        js_access test.set_var;

        location /var {
            js_content test.content;
        }

        location /deny {
            js_access test.deny;
            js_content test.content;
        }

        location /exception {
            js_access test.exception;
            js_content test.content;
        }

        location /noop {
            js_access test.noop;
            js_content test.content;
        }

        location /override {
            js_access test.override;
            js_content test.content;
        }

        location /content_only {
            js_content test.content_only;
        }

        location /async_timeout {
            js_access test.async_timeout;
            js_content test.content;
        }

        location /async_deny {
            js_access test.async_deny;
            js_content test.content;
        }

        location /async_exception {
            js_access test.async_exception;
            js_content test.content;
        }

        location /sr_skip {
            js_content test.sr_skip;
        }

        location /sub {
            js_access test.deny;
            js_content test.content;
        }

        location /sr {
            js_access test.sr;
            js_content test.content;
        }

        location /fetch {
            js_access test.fetch;
            js_content test.content;
        }

        location /route {
            js_access test.route;
            proxy_pass http://$upstream;
        }

        location /auth_check {
            js_content test.auth_check;
        }
    }

    server {
        listen       127.0.0.1:8081;

        location / {
            return 200 "backend1";
        }
    }

    server {
        listen       127.0.0.1:8082;

        location / {
            return 200 "backend2";
        }
    }
}

EOF

my $p0 = port(8080);
my $p1 = port(8081);
my $p2 = port(8082);

$t->write_file('test.js', <<EOF);
    function set_var(r) {
        r.variables.foo = 'access_ok';
    }

    function content(r) {
        r.return(200, `var:\${r.variables.foo}`);
    }

    function deny(r) {
        r.return(403);
    }

    function exception(r) {
        throw new Error("access_error");
    }

    function noop(r) {
        /* does nothing, should continue to content handler */
    }

    function override(r) {
        r.variables.foo = 'overridden';
    }

    function content_only(r) {
        r.return(200, 'content_only');
    }

    async function async_timeout(r) {
        await new Promise(resolve => setTimeout(resolve, 5));
        r.variables.foo = 'timeout_ok';
    }

    async function async_deny(r) {
        await new Promise(resolve => setTimeout(resolve, 5));
        r.return(403);
    }

    async function async_exception(r) {
        await new Promise(resolve => setTimeout(resolve, 5));
        throw new Error("async_access_error");
    }

    async function sr_skip(r) {
        let reply = await r.subrequest('/sub');
        r.return(reply.status, reply.responseText);
    }

    async function fetch(r) {
        let resp = await ngx.fetch(
            \`http://127.0.0.1:$p0/auth_check?token=\${r.variables.arg_token}\`);

        if (resp.status != 200) {
            r.return(resp.status);
            return;
        }

        r.variables.foo = await resp.text();
    }

    async function sr(r) {
        let reply = await r.subrequest('/auth_check?token='
                                       + r.variables.arg_token);
        if (reply.status != 200) {
            r.return(reply.status);
            return;
        }

        r.variables.foo = reply.responseText;
    }

    function route(r) {
        let dest = r.variables.arg_dest;
        r.variables.upstream = (dest === 'one')
            ? '127.0.0.1:$p1' : '127.0.0.1:$p2';
    }

    function auth_check(r) {
        let token = r.variables.arg_token;

        if (token === 'valid') {
            r.return(200, 'authenticated');
        } else {
            r.return(403);
        }
    }

    export default { set_var, content, deny, exception, noop, override,
                     content_only, async_timeout, async_deny,
                     async_exception, sr_skip, sr, fetch, route,
                     auth_check };

EOF

$t->try_run('no js_access')->plan(17);

###############################################################################

like(http_get('/var'), qr/var:access_ok/,
	'js_access sets variable from server level');
like(http_get('/deny'), qr/403 Forbidden/,
	'js_access sync r.return(403) rejects');
like(http_post('/deny'), qr/403 Forbidden/,
	'js_access deny with request body');
like(http_get('/exception'), qr/500 Internal Server Error/,
	'js_access sync exception returns 500');
like(http_get('/noop'), qr/var:/,
	'js_access noop continues to content');
like(http_get('/override'), qr/var:overridden/,
	'js_access override in location');
like(http_get('/content_only'), qr/content_only/,
	'js_content without js_access');
like(http_get('/async_timeout'), qr/var:timeout_ok/,
	'async js_access with setTimeout');
like(http_get('/async_deny'), qr/403 Forbidden/,
	'async js_access r.return(403) rejects');
like(http_get('/async_exception'), qr/500 Internal Server Error/,
	'async js_access exception returns 500');
like(http_get('/sr_skip'), qr/var:/,
	'js_access skipped for subrequests');
like(http_get('/sr?token=valid'), qr/var:authenticated/,
	'subrequest access allow');
like(http_get('/sr?token=invalid'), qr/403 Forbidden/,
	'subrequest access deny');
like(http_get('/fetch?token=valid'), qr/var:authenticated/,
	'fetch access allow');
like(http_get('/fetch?token=invalid'), qr/403 Forbidden/,
	'fetch access deny');
like(http_get('/route?dest=one'), qr/backend1/,
	'variable routing to backend1');
like(http_get('/route?dest=two'), qr/backend2/,
	'variable routing to backend2');

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
