#!/usr/bin/perl

# (C) Test for NJS promise pending state handling

# Tests for proper handling of pending promises with no waiting events.

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

    server {
        listen       127.0.0.1:8080;
        server_name  localhost;

        location /njs {
            js_content test.njs;
        }

        location /promise_never_resolves {
            js_content test.promise_never_resolves;
        }

        location /promise_with_timeout {
            js_content test.promise_with_timeout;
        }


    }
}

EOF

$t->write_file('test.js', <<'EOF');
    function test_njs(r) {
        r.return(200, njs.version);
    }

    function promise_never_resolves(r) {
        // Create a promise that never resolves and has no pending events
        // This should trigger the condition:
        // promise_data->state == NJS_PROMISE_PENDING && 
        // njs_rbtree_is_empty(&ctx->waiting_events)
        return new Promise((resolve, reject) => {
            // Intentionally never call resolve or reject
            // No setTimeout, no async operations - truly pending with no events
        });
    }

    function promise_with_timeout(r) {
        // Create a promise with a timeout (has waiting events)
        const p = new Promise((resolve, reject) => {
            setTimeout(() => {
                resolve("timeout resolved");
            }, 10);
        });

        // This should NOT trigger pending error because there are waiting events
        return p.then((value) => {
            r.return(200, "resolved: " + value);
        });
    }

    export default {njs: test_njs, promise_never_resolves, promise_with_timeout};

EOF

$t->try_run('no njs available')->plan(5);

###############################################################################

# Test basic functionality
like(http_get('/njs'), qr/\d+\.\d+\.\d+/, 'njs version');

# Test promise with timeout (should work - has waiting events) 
like(http_get('/promise_with_timeout'), qr/resolved: timeout resolved/, 'promise with timeout resolves');

# Test pending promise scenario - should trigger error response
# because it returns a promise that will never resolve with no waiting events
my $never_resolves_response = http_get('/promise_never_resolves');
like($never_resolves_response, qr/HTTP\/1\.[01] 500|Internal Server Error/, 'never resolving promise causes error');

$t->stop();

# Check error log for the specific pending promise error message
my $error_log = $t->read_file('error.log');

# Now that we use ngx_log_error, the specific error message should appear in the log
ok(index($error_log, 'js promise pending, no jobs, no waiting_events') > 0, 
   'pending promise error message logged');

# Should have no error for promises with waiting events
unlike($error_log, qr/js exception.*timeout resolved/, 'no error for promise with waiting events');

###############################################################################
