#!/usr/bin/perl

# (C) Dmitry Volyntsev
# (C) Nginx, Inc.

# Tests for js_periodic directive.

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

worker_shutdown_timeout 100ms;

http {
    %%TEST_GLOBALS_HTTP%%

    js_import test.js;

    js_shared_dict_zone zone=nums:32k type=number;
    js_shared_dict_zone zone=strings:32k;

    server {
        listen       127.0.0.1:8080;
        server_name  localhost;

        location @periodic {
            js_periodic test.tick interval=30ms jitter=1ms;
            js_periodic test.timer interval=1s;
            js_periodic test.overrun interval=30ms;
            js_periodic test.file interval=1s;
            js_periodic test.fetch interval=40ms;
            js_periodic test.multiple_fetches interval=1s;

            js_periodic test.fetch_exception interval=1s;
            js_periodic test.tick_exception interval=1s;
            js_periodic test.timer_exception interval=1s;
            js_periodic test.timeout_exception interval=30ms;
        }

        location /fetch_ok {
            return 200 'ok';
        }

        location /fetch_foo {
            return 200 'foo';
        }

        location /test_fetch {
            js_content test.test_fetch;
        }

        location /test_file {
            js_content test.test_file;
        }

        location /test_multiple_fetches {
            js_content test.test_multiple_fetches;
        }

        location /test_tick {
            js_content test.test_tick;
        }

        location /test_timer {
            js_content test.test_timer;
        }

        location /test_timeout_exception {
            js_content test.test_timeout_exception;
        }
    }
}

EOF

my $p0 = port(8080);

$t->write_file('test.js', <<EOF);
    import fs from 'fs';

    async function fetch() {
        if (ngx.worker_id != 0) {
            return;
        }

        let reply = await ngx.fetch('http://127.0.0.1:$p0/fetch_ok');
        let body = await reply.text();

        let v = ngx.shared.strings.get('fetch') || '';
        ngx.shared.strings.set('fetch', v + body);
    }

    async function multiple_fetches() {
        if (ngx.worker_id != 0) {
            return;
        }

        let reply = await ngx.fetch('http://127.0.0.1:$p0/fetch_ok');
        let reply2 = await ngx.fetch('http://127.0.0.1:$p0/fetch_foo');
        let body = await reply.text();
        let body2 = await reply2.text();

        ngx.shared.strings.set('multiple_fetches', body + '\@' + body2);
    }

    async function fetch_exception() {
        if (ngx.worker_id != 0) {
            return;
        }

        let reply = await ngx.fetch('garbage');
     }

    async function file() {
        if (ngx.worker_id != 0) {
            return;
        }

        let fh = await fs.promises.open(ngx.conf_prefix + 'file', 'a+');

        await fh.write('abc');
        await fh.close();
    }

    async function overrun() {
        if (ngx.worker_id != 0) {
            return;
        }

        setTimeout(() => {}, 100000);
    }


    function tick() {
        if (ngx.worker_id != 0) {
            return;
        }

        ngx.shared.nums.incr('tick', 1);
    }

    function tick_exception() {
        if (ngx.worker_id != 0) {
            return;
        }

        throw new Error("EXCEPTION");
    }

    function timer() {
        if (ngx.worker_id != 0) {
            return;
        }

        setTimeout(() => {ngx.shared.nums.set('timer', 1)}, 10);
    }

    function timer_exception() {
        if (ngx.worker_id != 0) {
            return;
        }

        setTimeout(() => {ngx.log(ngx.ERR, 'should not be seen')}, 10);
        throw new Error("EXCEPTION");
    }

    function timeout_exception() {
        if (ngx.worker_id != 0) {
            return;
        }

        setTimeout(() => {
            var v = ngx.shared.nums.get('timeout_exception') || 0;

            if (v == 0) {
                ngx.shared.nums.set('timeout_exception', 1);
                throw new Error("EXCEPTION");
                return;
            }

            ngx.shared.nums.incr('timeout_exception', 1);
        }, 1);
    }

    function test_fetch(r) {
        r.return(200, ngx.shared.strings.get('fetch').startsWith('okok'));
    }

    function test_file(r) {
        r.return(200,
             fs.readFileSync(ngx.conf_prefix + 'file').toString()  == 'abc');
    }

    function test_multiple_fetches(r) {
        r.return(200, ngx.shared.strings.get('multiple_fetches')
                                                        .startsWith('ok\@foo'));
    }

    function test_tick(r) {
        r.return(200, ngx.shared.nums.get('tick') >= 3);
    }

    function test_timer(r) {
        r.return(200, ngx.shared.nums.get('timer') == 1);
    }

    function test_timeout_exception(r) {
        r.return(200, ngx.shared.nums.get('timeout_exception') >= 2);
    }

    export default { fetch, fetch_exception, file, multiple_fetches, overrun,
                     test_fetch, test_file, test_multiple_fetches, test_tick,
                     test_timeout_exception, test_timer, tick, tick_exception,
                     timer, timer_exception, timeout_exception };
EOF

$t->try_run('no js_periodic')->plan(7);

###############################################################################

select undef, undef, undef, 0.1;

like(http_get('/test_tick'), qr/true/, '3x tick test');
like(http_get('/test_timer'), qr/true/, 'timer test');
like(http_get('/test_file'), qr/true/, 'file test');
like(http_get('/test_fetch'), qr/true/, 'periodic fetch test');
like(http_get('/test_multiple_fetches'), qr/true/, 'multiple fetch test');

like(http_get('/test_timeout_exception'), qr/true/, 'timeout exception test');

$t->stop();

unlike($t->read_file('error.log'), qr/\[error\].*should not be seen/,
	'check for not discadred events');
