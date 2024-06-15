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

my $t = Test::Nginx->new()->has(qw/http rewrite/)
	->write_file_expand('nginx.conf', <<'EOF');

%%TEST_GLOBALS%%

daemon off;
worker_processes 4;

events {
}

worker_shutdown_timeout 100ms;

http {
    %%TEST_GLOBALS_HTTP%%

    js_import test.js;

    js_shared_dict_zone zone=nums:32k type=number;
    js_shared_dict_zone zone=strings:32k;
    js_shared_dict_zone zone=workers:32k type=number;

    js_set $js_set  test.js_set;
    js_var $js_var  JS-VAR;
    map _ $map_var {
        default "MAP-VAR";
    }

    server {
        listen       127.0.0.1:8080;
        server_name  localhost;

        location @periodic {
            js_periodic test.tick interval=30ms jitter=1ms;
            js_periodic test.timer interval=1s worker_affinity=all;
            js_periodic test.overrun interval=30ms;
            js_periodic test.file interval=1s;
            js_periodic test.fetch interval=40ms;
            js_periodic test.multiple_fetches interval=1s;
            js_periodic test.affinity interval=50ms worker_affinity=0101;
            js_periodic test.vars interval=10s;

            js_periodic test.fetch_exception interval=1s;
            js_periodic test.tick_exception interval=1s;
            js_periodic test.timer_exception interval=1s;
            js_periodic test.timeout_exception interval=30ms;
        }

        location /engine {
            js_content test.engine;
        }

        location /fetch_ok {
            return 200 'ok';
        }

        location /fetch_foo {
            return 200 'foo';
        }

        location /test_affinity {
            js_content test.test_affinity;
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

        location /test_vars {
            js_content test.test_vars;
        }
    }
}

EOF

my $p0 = port(8080);

$t->write_file('test.js', <<EOF);
    import fs from 'fs';

    function engine(r) {
        r.return(200, njs.engine);
    }

    function affinity() {
        ngx.shared.workers.set(ngx.worker_id, 1);
    }

    async function fetch() {
        let reply = await ngx.fetch('http://127.0.0.1:$p0/fetch_ok');
        let body = await reply.text();

        let v = ngx.shared.strings.get('fetch') || '';
        ngx.shared.strings.set('fetch', v + body);
    }

    function js_set() {
        return 'JS-SET';
    }

    async function multiple_fetches() {
        let reply = await ngx.fetch('http://127.0.0.1:$p0/fetch_ok');
        let reply2 = await ngx.fetch('http://127.0.0.1:$p0/fetch_foo');
        let body = await reply.text();
        let body2 = await reply2.text();

        ngx.shared.strings.set('multiple_fetches', body + '\@' + body2);
    }

    async function fetch_exception() {
        let reply = await ngx.fetch('garbage');
     }

    async function file() {
        let fh = await fs.promises.open(ngx.conf_prefix + 'file', 'a+');

        await fh.write('abc');
        await fh.close();
    }

    async function overrun() {
        setTimeout(() => {}, 100000);
    }


    function tick() {
        ngx.shared.nums.incr('tick', 1);
    }

    function tick_exception() {
        throw new Error("EXCEPTION");
    }

    function timer() {
        if (ngx.worker_id != 0) {
            return;
        }

        setTimeout(() => {ngx.shared.nums.set('timer', 1)}, 10);
    }

    function timer_exception() {
        setTimeout(() => {ngx.log(ngx.ERR, 'should not be seen')}, 10);
        throw new Error("EXCEPTION");
    }

    function timeout_exception() {
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

    function vars(s) {
        var v = s.variables;
        ngx.shared.strings.set('vars',
                               `\${v.js_var}|\${v.js_set}|\${v.map_var}`);
    }

    function test_affinity(r) {
        r.return(200, `[\${ngx.shared.workers.keys().toSorted()}]`);
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

    function test_vars(r) {
        r.return(200, ngx.shared.strings.get('vars'));
    }

    export default { affinity, fetch, fetch_exception, file, js_set,
                     multiple_fetches, overrun, vars, test_affinity, test_fetch,
                     test_file, test_multiple_fetches, test_tick,
                     test_timeout_exception, test_timer, test_vars, tick,
                     tick_exception, timer, timer_exception,
                     timeout_exception, engine };
EOF

$t->try_run('no js_periodic');

plan(skip_all => 'not yet') if http_get('/engine') =~ /QuickJS$/m;

$t->plan(9);

###############################################################################

select undef, undef, undef, 0.1;

like(http_get('/test_affinity'), qr/\[1,3]/, 'affinity test');
like(http_get('/test_tick'), qr/true/, '3x tick test');
like(http_get('/test_timer'), qr/true/, 'timer test');
like(http_get('/test_file'), qr/true/, 'file test');
like(http_get('/test_fetch'), qr/true/, 'periodic fetch test');
like(http_get('/test_multiple_fetches'), qr/true/, 'multiple fetch test');

like(http_get('/test_timeout_exception'), qr/true/, 'timeout exception test');
like(http_get('/test_vars'), qr/JS-VAR\|JS-SET\|MAP-VAR/, 'vars test');

$t->stop();

unlike($t->read_file('error.log'), qr/\[error\].*should not be seen/,
	'check for not discadred events');
