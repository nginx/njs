#!/usr/bin/perl

# (C) Dmitry Volyntsev
# (C) Nginx, Inc.

# Async tests for http njs module.

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

my $t = Test::Nginx->new()->has(qw/http rewrite/)
	->write_file_expand('nginx.conf', <<'EOF');

%%TEST_GLOBALS%%

daemon off;

events {
}

http {
    %%TEST_GLOBALS_HTTP%%

    js_set $test_async       test.set_timeout;
    js_set $context_var      test.context_var;
    js_set $test_set_rv_var  test.set_rv_var;
    js_set $test_promise_var test.set_promise_var;

    js_import test.js;

    server {
        listen       127.0.0.1:8080;
        server_name  localhost;

        location /njs {
            js_content test.njs;
        }

        location /async_var {
            return 200 $test_async;
        }

        location /shared_ctx {
            add_header H $context_var;
            js_content test.shared_ctx;
        }

        location /set_timeout {
            js_content test.set_timeout;
        }

        location /set_timeout_many {
            js_content test.set_timeout_many;
        }

        location /clear_timeout_fetch {
            js_content test.clear_timeout_fetch;
        }

        location /clear_timeout_unknown {
            js_content test.clear_timeout_unknown;
        }

        location /fetch {
            return 200 'fetch';
        }

        location /set_timeout_data {
            postpone_output 0;
            js_content test.set_timeout_data;
        }

        location /limit_rate {
            postpone_output 0;
            sendfile_max_chunk 5;
            js_content test.limit_rate;
        }

        location /async_content {
            js_content test.async_content;
        }

        location /set_rv_var {
            return 200 $test_set_rv_var;
        }

        location /promise_var {
            return 200 $test_promise_var;
        }

        location /await_reject {
            js_content test.await_reject;
        }
    }
}

EOF

my $p0 = port(8080);

$t->write_file('test.js', <<EOF);
    function test_njs(r) {
        r.return(200, njs.version);
    }

    function set_timeout(r) {
        var timerId = setTimeout(timeout_cb_r, 5, r, 0);
        clearTimeout(timerId);
        setTimeout(timeout_cb_r, 5, r, 0)
    }

    function set_timeout_data(r) {
        setTimeout(timeout_cb_data, 5, r, 0);
    }

    function set_timeout_many(r) {
        for (var i = 0; i < 5; i++) {
            setTimeout(timeout_cb_empty, 5, r, i);
        }

        setTimeout(timeout_cb_reply, 10, r);
    }

    async function clear_timeout_fetch(r) {
        let id = setTimeout(() => {}, 1);
        clearTimeout(id);

        let promise = ngx.fetch('http://127.0.0.1:$p0/fetch');

        /* Native events share the timer ID sequence. */
        clearTimeout(id + 1);

        let reply = await promise;
        r.return(200, await reply.text());
    }

    function clear_timeout_unknown(r) {
        let id = setTimeout(() => r.return(500), 1);
        clearTimeout(id);
        clearTimeout(id);
        clearTimeout(2147483647);
        r.return(200, 'cleared');
    }

    function timeout_cb_r(r, cnt) {
        if (cnt == 10) {
            r.status = 200;
            r.headersOut['Content-Type'] = 'foo';
            r.sendHeader();
            r.finish();

        } else {
            setTimeout(timeout_cb_r, 5, r, ++cnt);
        }
    }

    function timeout_cb_empty(r, arg) {
        r.log("timeout_cb_empty" + arg);
    }

    function timeout_cb_reply(r) {
        r.status = 200;
        r.headersOut['Content-Type'] = 'reply';
        r.sendHeader();
        r.finish();
    }

    function timeout_cb_data(r, counter) {
        if (counter == 0) {
            r.log("timeout_cb_data: init");
            r.status = 200;
            r.sendHeader();
            setTimeout(timeout_cb_data, 5, r, ++counter);

        } else if (counter == 10) {
            r.log("timeout_cb_data: finish");
            r.finish();

        } else {
            r.send("" + counter);
            setTimeout(timeout_cb_data, 5, r, ++counter);
        }
    }

    var js_;
    function context_var() {
        return js_;
    }

    function shared_ctx(r) {
        js_ = r.variables.arg_a;

        r.status = 200;
        r.sendHeader();
        r.finish();
    }

    function limit_rate_cb(r) {
        r.finish();
    }

    function limit_rate(r) {
        r.status = 200;
        r.sendHeader();
        r.send("AAAAA".repeat(10))
        setTimeout(limit_rate_cb, 1000, r);
    }

    function pr(x) {
        return new Promise(resolve => {resolve(x)}).then(v => v).then(v => v);
    }

    async function async_content(r) {
        const a1 = await pr('A');
        const a2 = await pr('B');

        r.return(200, `retval: \${a1 + a2}`);
    }

    async function set_rv_var(r) {
        const a1 = await pr(10);
        const a2 = await pr(20);

        r.setReturnValue(`retval: \${a1 + a2}`);
    }

    async function set_promise_var(r) {
        const a1 = await pr(10);
        const a2 = await pr(20);

        return `retval: \${a1 + a2}`;
    }

    async function timeout(ms) {
        return new Promise((resolve, reject) => {
            setTimeout(() => {
                reject(new Error('timeout'));
            }, ms);
        });
    }

    async function await_reject(r) {
        let v = await timeout(1);
        r.return(200);
    }

    export default {njs:test_njs, set_timeout, set_timeout_data,
                    set_timeout_many, context_var, shared_ctx, limit_rate,
                    clear_timeout_fetch, clear_timeout_unknown,
                    async_content, set_rv_var, set_promise_var, await_reject};

EOF

$t->try_run('no njs available')->plan(13);

###############################################################################

like(http_get('/set_timeout'), qr/Content-Type: foo/, 'setTimeout');
like(http_get('/set_timeout_many'), qr/Content-Type: reply/, 'setTimeout many');
like(http_get('/clear_timeout_fetch'), qr/fetch$/,
     'clearTimeout fetch id');
like(http_get('/clear_timeout_unknown'), qr/cleared$/,
     'clearTimeout unknown and cancelled ids');
like(http_get('/set_timeout_data'), qr/123456789/, 'setTimeout data');
like(http_get('/shared_ctx?a=xxx'), qr/H: xxx/, 'shared context');
like(http_get('/limit_rate'), qr/A{50}/, 'limit_rate');

like(http_get('/async_content'), qr/retval: AB/, 'async content');
like(http_get('/set_rv_var'), qr/retval: 30/, 'set return value variable');
like(http_get('/promise_var'), qr/retval: 30/, 'fulfilled promise variable');

http_get('/async_var');
http_get('/await_reject');

$t->stop();

ok(index($t->read_file('error.log'), 'pending events') > 0,
   'pending js events');
ok(index($t->read_file('error.log'), 'async operation inside') > 0,
   'async op in var handler');
ok(index($t->read_file('error.log'), 'js unhandled rejection') > 0,
   'unhandled rejection');

###############################################################################
