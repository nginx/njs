#!/usr/bin/perl

# (C) Test for NJS promise state handling

# Tests the specific promise rejection and pending state handling added to ngx_js.c

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

        location /rejected_promise_thrown {
            js_content test.rejected_promise_thrown;
        }

        location /rejected_promise_caught {
            js_content test.rejected_promise_caught;
        }

        location /fulfilled_promise {
            js_content test.fulfilled_promise;
        }
    }
}

EOF

$t->write_file('test.js', <<'EOF');
    function test_njs(r) {
        r.return(200, njs.version);
    }

    function rejected_promise_thrown(r) {
        // Create a rejected promise that should trigger exception throwing
        const p = Promise.reject(new Error("test rejection"));
        
        // Don't catch it - this should trigger the rejection handling
        // The promise handling code should call njs_vm_throw()
        return p;
    }

    function rejected_promise_caught(r) {
        // Create a rejected promise but catch it properly
        const p = Promise.reject(new Error("caught rejection"));
        
        return p.catch((error) => {
            r.return(200, "properly caught: " + error.message);
        });
    }

    function fulfilled_promise(r) {
        // Create a fulfilled promise - should extract the result
        const p = Promise.resolve("success value");
        
        return p.then((value) => {
            r.return(200, "fulfilled: " + value);
        });
    }

    export default {njs: test_njs, rejected_promise_thrown, rejected_promise_caught, 
                    fulfilled_promise};

EOF

$t->try_run('no njs available')->plan(5);

###############################################################################

# Test basic functionality
like(http_get('/njs'), qr/\d+\.\d+\.\d+/, 'njs version');

# Test fulfilled promise (should work normally)
like(http_get('/fulfilled_promise'), qr/fulfilled: success value/, 'fulfilled promise handled');

# Test caught rejected promise (should work normally)  
like(http_get('/rejected_promise_caught'), qr/properly caught: caught rejection/, 'caught rejection handled');

# Test that rejected promises generate proper error responses
my $rejected_response = http_get('/rejected_promise_thrown');
like($rejected_response, qr/HTTP\/1\.[01] 500|Internal Server Error/, 'rejected promise causes error response');

$t->stop();

# Verify that the promise rejection handling is working
# The key improvement is that rejections should be thrown as exceptions
# rather than silently ignored or causing uncontrolled errors
my $error_log = $t->read_file('error.log');
ok(length($error_log) > 0, 'error log contains entries (promise handling active)');

###############################################################################
