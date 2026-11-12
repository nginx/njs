#!/usr/bin/perl

# (C) Dmitry Volyntsev
# (C) F5, Inc.

# Tests for http njs module, njs.on('exit', ...).

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

    log_format test '[var:$bs header:$foo url:$url]';
    access_log %%TESTDIR%%/access.log test;

    js_import test.js;
    js_set $foo test.foo_header;
    js_set $url test.url;
    js_set $bs test.bytes_sent;

    server {
        listen       127.0.0.1:8080;
        server_name  localhost;

        location /test {
            js_content test.test;
        }

        location /replaced {
            js_context_reuse 0;
            js_content test.replaced;
        }
    }
}

EOF

$t->write_file('test.js', <<EOF);
    function test(r) {
        njs.on('exit', function() {
            ngx.log(ngx.WARN, `exit hook: bs: \${r.variables.bytes_sent}`);

            new Promise((resolve) => {resolve()})
            .then(v => ngx.log(ngx.WARN, "exit hook promise"));
        });

        r.return(200, `bs: \${r.variables.bytes_sent}`);
    }

    function replaced(r) {
        njs.on('exit', () => ngx.log(ngx.WARN, "replaced exit hook"));

        globalThis.njs = {};

        r.return(200, 'replaced');
    }

    function bytes_sent(r) {
        return r.variables.bytes_sent;
    }

    function foo_header(r) {
        return Buffer.from(r.headersIn.foo).toString('hex');
    }

    function url(r) {
        return r.uri;
    }

    export default { test, replaced, bytes_sent, foo_header, url };

EOF

$t->try_run('no njs')->plan(6);

###############################################################################

like(http(
	'GET /test HTTP/1.0' . CRLF
	. 'Foo: bar' . CRLF
	. 'Host: localhost' . CRLF . CRLF
), qr/bs: 0/, 'response');
like(http_get('/replaced'), qr/replaced$/, 'replaced njs response');

$t->stop();

my $error_log = $t->read_file('error.log');

like($error_log, qr/\[warn\].*exit hook: bs: \d+/, 'exit hook logged');
like($error_log, qr/\[warn\].*exit hook promise/, 'exit hook promise logged');
like($error_log, qr/\[warn\].*replaced exit hook/,
	'exit hook with replaced njs');
like($t->read_file('access.log'), qr/\[var:\d+ header:626172 url:\/test\]/,
	'access log has bytes_sent');

###############################################################################
