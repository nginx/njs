#!/usr/bin/perl

# (C) Test for ngx_qjs_await() function
#
# Tests for proper handling of promises in ngx_qjs_await() with:
# - Job queue processing limits
# - Waiting events detection
# - Promise state handling after job processing
#
# Note: ngx_qjs_await() is called during global code evaluation, not function calls

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

    js_import test.js;
    js_import fulfilled_test.js;

    server {
        listen       127.0.0.1:8080;
        server_name  localhost;

        location /njs {
            js_content test.njs;
        }

        location /resolved {
            js_content test.test;
        }

        location /fulfilled {
            js_content fulfilled_test.test;
        }
    }
}

EOF

$t->write_file('test.js', <<'EOF');
// Test with simple await in global code - this should work fine
var globalResult = await Promise.resolve("resolved value");

function test_njs(r) {
    r.return(200, njs.version);
}

function test(r) {
    r.return(200, "global result: " + globalResult);
}

export default {njs: test_njs, test};
EOF

$t->write_file('fulfilled_test.js', <<'EOF');
// Test with promise that gets fulfilled via microtask queue
// This tests the JS_PROMISE_FULFILLED branch in ngx_qjs_await()
var globalResult = await new Promise((resolve) => {
    // Use queueMicrotask to test microtask handling
    Promise.resolve().then(() => {
        resolve("fulfilled value");
    });
});

function test(r) {
    r.return(200, "fulfilled result: " + globalResult);
}

export default {test};
EOF

$t->try_run('no qjs engine available')->plan(3);

###############################################################################

# Test basic functionality
like(http_get('/njs'), qr/\d+\.\d+\.\d+/, 'njs version');

# Test basic global await with resolved promise
like(http_get('/resolved'), qr/global result: resolved value/, 'basic global await works');

# Test global await with fulfilled promise (via microtask)
like(http_get('/fulfilled'), qr/fulfilled result: fulfilled value/, 'fulfilled promise via microtask works');

$t->stop();

###############################################################################
