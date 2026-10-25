#!/usr/bin/perl

# (C) Dmitry Volyntsev.
# (C) F5, Inc.

# Tests for subrequest headers in http njs module.

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

my $t = Test::Nginx->new()->has(qw/http proxy/)
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

        location /sr_custom {
            js_content test.sr_custom;
        }

        location /sr_host {
            js_content test.sr_host;
        }

        location /sr_host_port {
            js_content test.sr_host_port;
        }

        location /sr_inherited {
            js_content test.sr_inherited;
        }

        location /sr_body {
            js_content test.sr_body;
        }

        location /sr_empty {
            js_content test.sr_empty;
        }

        location /sr_ignored {
            js_content test.sr_ignored;
        }

        location /sr_override_ua {
            js_content test.sr_override_ua;
        }

        location /sr_dedup {
            js_content test.sr_dedup;
        }

        location /sr_body_only {
            js_content test.sr_body_only;
        }

        location /sr_pass_headers_in {
            js_content test.sr_pass_headers_in;
        }

        location /sr_no_body_ctype {
            js_content test.sr_no_body_ctype;
        }

        location /sr_body_ctype {
            js_content test.sr_body_ctype;
        }

        location /sr_body_inherit_entity_headers {
            js_content test.sr_body_inherit_entity_headers;
        }

        location /sr_explicit_no_body_ctype {
            js_content test.sr_explicit_no_body_ctype;
        }

        location /sr_host_port_vars {
            js_content test.sr_host_port_vars;
        }

        location /echo_host {
            js_content test.echo_host;
        }

        location /echo_host_vars {
            return 200 '{"rp":"$request_port","hh":"$http_host","h":"$host"}';
        }

        location /echo_ua {
            js_content test.echo_ua;
        }

        location /p/ {
            proxy_pass http://127.0.0.1:8081/;
        }
    }

    server {
        listen       127.0.0.1:8081;
        server_name  localhost;

        location /echo {
            return 200 '{"x":"$http_x_custom","ua":"$http_user_agent"}';
        }

        location /echo_body {
            js_content test.echo_body;
        }

        location /echo_ctype {
            return 200 '{"ct":"$http_content_type"}';
        }
    }
}

EOF

$t->write_file('test.js', <<EOF);
    async function sr_custom(r) {
        let reply = await r.subrequest('/p/echo',
                                       {headers: {'X-Custom': 'test-value'}});
        r.return(200, reply.responseText);
    }

    async function sr_host(r) {
        let reply = await r.subrequest('/echo_host',
                                       {headers: {'Host': 'custom.host'}});
        r.return(200, reply.responseText);
    }

    async function sr_host_port(r) {
        let reply = await r.subrequest('/echo_host',
                                       {headers: {'Host': 'example.com:8080'}});
        r.return(200, reply.responseText);
    }

    async function sr_inherited(r) {
        let reply = await r.subrequest('/p/echo',
                                       {headers: {'X-Custom': 'added'}});
        r.return(200, reply.responseText);
    }

    async function sr_body(r) {
        let reply = await r.subrequest('/p/echo_body',
                                       {method: 'POST', body: 'BODY',
                                        headers: {'Content-Length': '999'}});
        r.return(200, reply.responseText);
    }

    async function sr_empty(r) {
        let reply = await r.subrequest('/p/echo', {headers: {}});
        r.return(200, reply.responseText);
    }

    async function sr_ignored(r) {
        let reply = await r.subrequest('/p/echo',
            {headers: {'Connection': 'close', 'X-Custom': 'yes'}});
        r.return(200, reply.responseText);
    }

    async function sr_override_ua(r) {
        let reply = await r.subrequest('/echo_ua',
                                     {headers: {'User-Agent': 'custom-agent'}});
        r.return(200, reply.responseText);
    }

    async function sr_dedup(r) {
        let reply = await r.subrequest('/p/echo',
                                       {headers: {'X-Custom': 'first',
                                                  'x-custom': 'last'}});
        r.return(200, reply.responseText);
    }

    async function sr_body_only(r) {
        let reply = await r.subrequest('/p/echo_body', {method: 'POST',
                                                        body: 'PAYLOAD'});
        r.return(200, reply.responseText);
    }

    async function sr_pass_headers_in(r) {
        let reply = await r.subrequest('/p/echo',
                                       {method: 'POST', body: 'DATA',
                                        headers: r.headersIn});
        r.return(200, reply.responseText);
    }

    async function sr_body_ctype(r) {
        let reply = await r.subrequest('/p/echo_ctype',
                                       {method: 'POST', body: '{}',
                                        headers: {'Content-Type':
                                                          'application/json'}});
        r.return(200, reply.responseText);
    }

    async function sr_body_inherit_entity_headers(r) {
        let reply = await r.subrequest('/p/echo_ctype',
                                       {method: 'POST', body: '{}'});
        r.return(200, reply.responseText);
    }

    async function sr_explicit_no_body_ctype(r) {
        let reply = await r.subrequest('/p/echo_ctype',
                                       {headers: {'Content-Type':
                                                          'application/json'}});
        r.return(200, reply.responseText);
    }

    async function sr_host_port_vars(r) {
        let reply = await r.subrequest('/echo_host_vars',
                                       {headers: {'Host':
                                                          'example.com:8080'}});
        r.return(200, reply.responseText);
    }

    async function sr_no_body_ctype(r) {
        let reply = await r.subrequest('/p/echo_ctype',
                                       {headers: r.headersIn});
        r.return(200, reply.responseText);
    }

    function echo_host(r) {
        r.return(200, JSON.stringify({host: r.headersIn.Host}));
    }

    function echo_ua(r) {
        let ua = r.headersIn['User-Agent'] || '';
        r.return(200, JSON.stringify({ua: ua}));
    }

    function echo_body(r) {
        let cl = r.headersIn['Content-Length'] || '';
        r.return(200, JSON.stringify({ body: r.variables.request_body || '',
                                       cl: cl}));
    }

    export default {sr_custom, sr_host, sr_host_port, sr_inherited, sr_body,
                    sr_empty, sr_ignored, sr_override_ua, sr_dedup,
                    sr_body_only, sr_pass_headers_in, sr_body_ctype,
                    sr_body_inherit_entity_headers, sr_explicit_no_body_ctype,
                    sr_host_port_vars, sr_no_body_ctype, echo_host, echo_ua,
                    echo_body};

EOF

$t->try_run('no njs available')->plan(16);

###############################################################################

like(http_get('/sr_custom'), qr/"x":"test-value"/, 'custom header');
like(http_get('/sr_host'), qr/"host":"custom\.host"/, 'replace host header');
like(http_get('/sr_host_port'), qr/"host":"example\.com:8080"/,
	'host header with port');
like(http_get('/sr_inherited'), qr/"x":"added"/, 'inherited headers with add');
like(http_get('/sr_body'), qr/"cl":"4"/, 'body overrides Content-Length');
like(http_get('/sr_empty'), qr/"ua"/, 'empty headers is no-op');
like(http_get('/sr_ignored'), qr/"x":"yes"/, 'transport headers ignored');

like(http(<<EOF), qr/\{"ua":"custom-agent"\}/s,
GET /sr_override_ua HTTP/1.0
Host: localhost
User-Agent: parent-agent

EOF
	'override standard header');

like(http_get('/sr_dedup'), qr/\{"x":"last","ua":""\}/,
	'case-variant dedup last-wins');
like(http_get('/sr_body_only'), qr/"cl":"7"/,
	'body without headers sets Content-Length');
like(http(<<EOF), qr/"x":"from-parent"/s,
GET /sr_pass_headers_in HTTP/1.0
Host: localhost
X-Custom: from-parent

EOF
	'r.headersIn as options.headers');

like(http_get('/sr_body_ctype'), qr/"ct":"application\/json"/,
	'body with Content-Type preserved');

like(http(<<EOF), qr/\{"ct":""\}/s,
POST /sr_body_inherit_entity_headers HTTP/1.0
Host: localhost
Content-Type: application/json
Content-Encoding: gzip
Content-Length: 2

{}
EOF
	'body must not inherit parent entity headers');

like(http_get('/sr_explicit_no_body_ctype'), qr/\{"ct":""\}/,
	'explicit Content-Type dropped without body');

like(http_get('/sr_host_port_vars'),
	qr/\{"rp":"8080","hh":"example\.com:8080","h":"example\.com"\}/,
	'custom host with port updates request port vars');

like(http(<<EOF), qr/\{"ct":""\}/s,
POST /sr_no_body_ctype HTTP/1.0
Host: localhost
Content-Type: application/json
Content-Length: 2

{}
EOF
	'body headers suppressed without body');

###############################################################################
