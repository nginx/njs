#!/usr/bin/perl

# (C) Test for ngx_qjs_await() infinite job queue protection
#
# This test specifically validates the NGX_MAX_JOB_ITERATIONS limit
# in ngx_qjs_await() for infinite microtask loops

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

    js_import infinite_jobs_test.js;

    server {
        listen       127.0.0.1:8080;
        server_name  localhost;

        location /njs {
            js_content infinite_jobs_test.njs;
        }

        location /infinite_jobs {
            js_content infinite_jobs_test.test;
        }
    }
}

EOF

$t->write_file('infinite_jobs_test.js', <<'EOF');
// Test with infinite job queue in global code
// This should trigger the NGX_MAX_JOB_ITERATIONS limit in ngx_qjs_await()
function createInfiniteJobs() {
    return Promise.resolve().then(() => {
        // Create more microtasks infinitely
        return createInfiniteJobs();
    });
}

createInfiniteJobs();

function test_njs(r) {
    r.return(200, njs.version);
}

function test(r) {
    r.return(200, "should never reach this");
}

export default {njs: test_njs, test};
EOF

$t->try_run('no njs available')->plan(3);

###############################################################################

# Note: With the ngx_qjs_clone improvements, workers no longer crash
# when our protection mechanism triggers. They return proper 500 errors.

# Test endpoints - these should now return 500 errors instead of crashing workers
my $njs_response = http_get('/njs');
like($njs_response, qr/HTTP\/1\.[01] 500|Internal Server Error/, 'njs endpoint returns 500 error due to infinite jobs protection');

my $infinite_response = http_get('/infinite_jobs');
like($infinite_response, qr/HTTP\/1\.[01] 500|Internal Server Error/, 'infinite_jobs endpoint returns 500 error due to protection');

$t->stop();

# Check error log for specific error messages
my $error_log = $t->read_file('error.log');

# Check for job queue limit exceeded message from ngx_qjs_await()
ok(index($error_log, 'js job queue processing exceeded') > 0,
   'job queue limit exceeded error message logged');

###############################################################################
