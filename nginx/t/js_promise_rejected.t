#!/usr/bin/perl

# (C) Test for NJS promise rejection handling

# Tests for proper handling of rejected promises in both NJS and QuickJS engines.

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

        location /promise_reject_sync {
            js_content test.promise_reject_sync;
        }

        location /promise_reject_async {
            js_content test.promise_reject_async;
        }

        location /promise_reject_catch {
            js_content test.promise_reject_catch;
        }

        location /promise_pending {
            js_content test.promise_pending;
        }

        location /promise_chain_reject {
            js_content test.promise_chain_reject;
        }

        location /async_function_reject {
            js_content test.async_function_reject;
        }
    }
}

EOF

$t->write_file('test.js', <<'EOF');
    function test_njs(r) {
        r.return(200, njs.version);
    }

    function promise_reject_sync(r) {
        // Return a synchronously rejected promise
        const p = Promise.reject(new Error("sync rejection"));
        
        // This should trigger the promise rejection handling
        return p.then(
            (value) => r.return(200, "resolved: " + value),
            (error) => r.return(500, "rejected: " + error.message)
        );
    }

    function promise_reject_async(r) {
        // Create an async rejected promise
        const p = new Promise((resolve, reject) => {
            setTimeout(() => {
                reject(new Error("async rejection"));
            }, 1);
        });
        
        return p.then(
            (value) => r.return(200, "resolved: " + value),
            (error) => r.return(500, "rejected: " + error.message)
        );
    }

    function promise_reject_catch(r) {
        // Test that caught rejections don't trigger error handling
        Promise.reject(new Error("caught rejection"))
            .catch((error) => {
                r.return(200, "caught: " + error.message);
            });
    }

    function promise_pending(r) {
        // Create a promise that never resolves
        const p = new Promise((resolve, reject) => {
            // Never call resolve or reject - this should timeout/error
        });
        
        // This should trigger pending promise handling
        return p.then(
            (value) => r.return(200, "resolved: " + value),
            (error) => r.return(500, "rejected: " + error.message)
        );
    }

    function promise_chain_reject(r) {
        // Test rejection in a promise chain
        Promise.resolve("initial")
            .then((value) => {
                throw new Error("chain rejection");
            })
            .then(
                (value) => r.return(200, "resolved: " + value),
                (error) => r.return(500, "rejected: " + error.message)
            );
    }

    async function async_function_reject(r) {
        try {
            // This should throw and be caught
            await Promise.reject(new Error("async function rejection"));
            r.return(200, "should not reach here");
        } catch (error) {
            r.return(500, "caught in async: " + error.message);
        }
    }

    export default {njs: test_njs, promise_reject_sync, promise_reject_async, 
                    promise_reject_catch, promise_pending, promise_chain_reject,
                    async_function_reject};

EOF

$t->try_run('no njs available')->plan(7);

###############################################################################

# Test basic functionality
like(http_get('/njs'), qr/\d+\.\d+\.\d+/, 'njs version');

# Test synchronous promise rejection
like(http_get('/promise_reject_sync'), qr/rejected: sync rejection/, 'sync promise rejection handled');

# Test asynchronous promise rejection  
like(http_get('/promise_reject_async'), qr/rejected: async rejection/, 'async promise rejection handled');

# Test caught promise rejection (should not trigger error handling)
like(http_get('/promise_reject_catch'), qr/caught: caught rejection/, 'caught promise rejection');

# Test promise chain rejection
like(http_get('/promise_chain_reject'), qr/rejected: chain rejection/, 'promise chain rejection handled');

# Test async function rejection handling
like(http_get('/async_function_reject'), qr/caught in async: async function rejection/, 'async function rejection caught');

$t->stop();

# Check that promise rejections are properly handled without causing unhandled rejections
# when they are properly caught by JavaScript code
my $error_log = $t->read_file('error.log');
unlike($error_log, qr/js unhandled rejection.*sync rejection/, 'no unhandled rejection for caught sync rejection');

###############################################################################
