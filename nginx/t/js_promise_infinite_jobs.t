#!/usr/bin/perl

# (C) Test for infinite loop protection in microtask queue processing

# Tests for proper handling of infinite microtask loops similar to Node.js protection.

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

        location /infinite_microtasks {
            js_content test.infinite_microtasks;
        }

        location /recursive_promises {
            js_content test.recursive_promises;
        }

        location /normal_promise_chain {
            js_content test.normal_promise_chain;
        }

    }
}

EOF

$t->write_file('test.js', <<'EOF');
    function test_njs(r) {
        r.return(200, njs.version);
    }

    function infinite_microtasks(r) {
        // Create an infinite microtask loop - should trigger protection limit
        function infiniteLoop() {
            return Promise.resolve().then(() => {
                // Recursively create more microtasks
                return infiniteLoop();
            });
        }

        return infiniteLoop();
    }

    function recursive_promises(r) {
        // Another variation of infinite microtask generation
        let count = 0;
        function createPromise() {
            return new Promise((resolve) => {
                resolve();
            }).then(() => {
                count++;
                // Keep creating more promises indefinitely
                return createPromise();
            });
        }

        return createPromise();
    }

    function normal_promise_chain(r) {
        // Normal promise chain that should complete without hitting the limit
        let result = Promise.resolve(1);

        // Create a reasonable chain of 50 promises (well under the NGX_MAX_JOB_ITERATIONS limit)
        for (let i = 0; i < 50; i++) {
            result = result.then((value) => value + 1);
        }

        return result.then((finalValue) => {
            r.return(200, "completed with value: " + finalValue);
        });
    }

    export default {njs: test_njs, infinite_microtasks, recursive_promises, normal_promise_chain};

EOF

$t->try_run('no njs available')->plan(5);

###############################################################################

# Test basic functionality
like(http_get('/njs'), qr/\d+\.\d+\.\d+/, 'njs version');

# Test normal promise chain (should work - under the limit)
like(http_get('/normal_promise_chain'), qr/completed with value: 51/, 'normal promise chain completes');

# Test infinite microtasks scenario - should trigger protection limit
my $infinite_response = http_get('/infinite_microtasks');
like($infinite_response, qr/HTTP\/1\.[01] 500|Internal Server Error/, 'infinite microtasks causes error');

# Test recursive promises scenario - should also trigger protection limit  
my $recursive_response = http_get('/recursive_promises');
like($recursive_response, qr/HTTP\/1\.[01] 500|Internal Server Error/, 'recursive promises causes error');

$t->stop();

# Check error log for the specific infinite loop protection messages
my $error_log = $t->read_file('error.log');

# Should have error messages for job queue limit exceeded
ok(index($error_log, 'js job queue processing exceeded') > 0, 
   'job queue limit exceeded message logged');

###############################################################################
