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

        location /decline {
            js_access test.decline;
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

        location /deny_body_proxy {
            js_access test.deny_body;
            proxy_pass http://127.0.0.1:8083;
        }

        location /async_deny_body_proxy {
            js_access test.async_deny_body;
            proxy_pass http://127.0.0.1:8083;
        }

        location /access_return_200 {
            js_access test.access_return_200;
            js_content test.content;
        }

        location /deny_error_page {
            error_page 403 /custom_403;
            js_access test.deny;
            js_content test.content;
        }

        location /custom_403 {
            return 200 "custom403";
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

        location /redirect {
            js_access test.redirect;
            js_content test.content;
        }

        location /redirect_async {
            js_access test.redirect_async;
            js_content test.content;
        }

        location /callback {
            js_content test.content;
        }
    }

    server {
        listen       127.0.0.1:8080;
        server_name  noaccess;

        location /no_access {
            js_content test.content_only;
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

    server {
        listen       127.0.0.1:8083;
        access_log  %%TESTDIR%%/backend_access.log combined;

        location / {
            return 200 "backend_access";
        }
    }
}

EOF

my $p0 = port(8080);
my $p1 = port(8081);
my $p2 = port(8082);

$t->write_file('test.js', <<EOF);
    function content(r) {
        r.return(200, `var:\${r.variables.foo}`);
    }

    function deny(r) {
        r.return(403);
    }

    function deny_body(r) {
        r.return(403, 'denied');
    }

    function exception(r) {
        throw new Error("access_error");
    }

    function noop(r) {
        /* does nothing, should continue to content handler */
    }

    function decline(r) {
        r.decline();
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

    async function async_deny_body(r) {
        await new Promise(resolve => setTimeout(resolve, 5));
        r.return(403, 'denied_async');
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

    function access_return_200(r) {
        r.return(200, 'access_ok');
    }

    function redirect(r) {
        r.return(302, 'http://127.0.0.1:$p0/callback');
    }

    async function redirect_async(r) {
        await new Promise(resolve => setTimeout(resolve, 5));
        r.return(302, 'http://127.0.0.1:$p0/callback');
    }

    export default { content, deny, deny_body, exception, noop, override,
                     decline, content_only, async_timeout, async_deny,
                     async_deny_body, access_return_200,
                     async_exception, sr_skip, sr, fetch, route,
                     auth_check, redirect, redirect_async };

EOF

$t->write_file('backend_access.log', '');

$t->write_file_expand('duplicate.conf', bad_conf(
	http     => 'js_import test.js;',
	location => 'js_access test.noop; js_access test.noop;'));

$t->write_file_expand('no_import.conf', bad_conf(
	location => 'js_access test.noop;'));

$t->try_run('no js_access')->plan(33);

###############################################################################

like(http_get('/deny'), qr/403 Forbidden/,
	'js_access sync r.return(403) rejects');
like(http_post('/deny'), qr/403 Forbidden/,
	'js_access deny with request body');
like(http_get('/exception'), qr/500 Internal Server Error/,
	'js_access sync exception returns 500');
like(http_get('/noop'), qr/var:/,
	'js_access noop continues to content');
like(http_get('/decline'), qr/var:/,
	'js_access decline continues to content');
like(http_get('/override'), qr/var:overridden/,
	'js_access override in location');
like(http_get('/content_only'), qr/content_only/,
	'js_content without js_access');
like(http("GET /no_access HTTP/1.0" . CRLF .
		"Host: noaccess" . CRLF . CRLF),
	qr/content_only/,
	'js_access not inherited in sibling server');
like(http_get('/async_timeout'), qr/var:timeout_ok/,
	'async js_access with setTimeout');
like(http_get('/async_deny'), qr/403 Forbidden/,
	'async js_access r.return(403) rejects');
like(http_post('/deny_body_proxy'), qr/403 Forbidden.*denied/s,
	'js_access r.return(403, body) rejects');
is($t->read_file('backend_access.log'), '',
	'js_access r.return(403, body) skips proxy_pass');
like(http_post('/async_deny_body_proxy'), qr/403 Forbidden.*denied_async/s,
	'async js_access r.return(403, body) rejects');
is($t->read_file('backend_access.log'), '',
	'async js_access r.return(403, body) skips proxy_pass');
like(http_get('/access_return_200'), qr/200 OK.*access_ok/s,
	'js_access r.return(200, body) finalizes request');
like(http_get('/deny_error_page'), qr/custom403/,
	'js_access r.return(403) preserves error_page');
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
like(http_get('/redirect'), qr/302 Moved/,
	'js_access sync redirect');
like(http_get('/redirect'), qr!Location: http://127.0.0.1:$p0/callback!,
	'js_access sync redirect Location header');
like(http_get('/redirect_async'), qr/302 Moved/,
	'js_access async redirect');
like(http_get('/redirect_async'), qr!Location: http://127.0.0.1:$p0/callback!,
	'js_access async redirect Location header');

my ($rc, $out) = nginx_test_conf($t, 'duplicate.conf');

isnt($rc, 0, 'duplicate js_access fails');
like($out, qr/"js_access" directive is duplicate/,
	'duplicate js_access error');

($rc, $out) = nginx_test_conf($t, 'no_import.conf');

isnt($rc, 0, 'js_access without js_import fails');
like($out, qr/no imports defined for "js_access" "test\.noop"/,
	'js_access without js_import error');

unlike($t->read_file('error.log'), qr/header already sent/,
	'js_access body return does not double-send headers');

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

sub nginx_test_conf {
	my ($t, $conf) = @_;
	my $testdir = $t->testdir();
	my $cmd = "$Test::Nginx::NGINX -p $testdir/ -c $conf -t "
		. "-e error.log 2>&1";

	my $out = `$cmd`;

	return ($? >> 8, $out);
}

sub bad_conf {
	my %args = @_;
	my $http = $args{http} // '';
	my $loc = $args{location} // '';

	return <<"EOF";

%%TEST_GLOBALS%%

daemon off;

events {
}

http {
    %%TEST_GLOBALS_HTTP%%

    $http

    server {
        listen       127.0.0.1:8080;

        location / {
            $loc
        }
    }
}

EOF
}
