#!/usr/bin/perl

# (C) Test for ngx_qjs_await() pending promise with empty waiting_events
#
# This test specifically validates the waiting_events checking functionality
# in ngx_qjs_await() for promises that remain pending with no waiting events

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

    js_import pending_test.js;

    server {
        listen       127.0.0.1:8080;
        server_name  localhost;

        location /njs {
            js_content pending_test.njs;
        }

        location /pending_no_events {
            js_content pending_test.test;
        }
    }
}

EOF

$t->write_file('pending_test.js', <<'EOF');
// Test with promise that stays pending with no waiting events
// This should trigger our "promise pending, no jobs, no waiting_events" error
var globalResult = await new Promise((resolve, reject) => {
    // Intentionally never call resolve or reject
    // No setTimeout, no async operations - truly pending with no events
});

function test_njs(r) {
    r.return(200, njs.version);
}

function test(r) {
    r.return(200, "should never reach this: " + globalResult);
}

export default {njs: test_njs, test};
EOF

$t->try_run('no qjs engine available')->plan(3);

###############################################################################

# Test basic functionality (this should also fail due to the pending promise in global code)
my $njs_response = http_get('/njs');
like($njs_response, qr/HTTP\/1\.[01] 500|Internal Server Error/, 'njs version endpoint fails due to pending promise in global code');

# Test pending promise with no waiting events (should cause error)
my $pending_response = http_get('/pending_no_events');
like($pending_response, qr/HTTP\/1\.[01] 500|Internal Server Error/, 'pending promise with no waiting events causes error');

$t->stop();

# Check error log for specific error messages
my $error_log = $t->read_file('error.log');

# Check for waiting events error message from our ngx_qjs_await() improvements
ok(index($error_log, 'js promise pending, no jobs, no waiting_events') > 0, 
   'waiting events error message logged');

###############################################################################
