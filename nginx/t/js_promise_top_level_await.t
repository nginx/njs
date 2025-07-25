#!/usr/bin/perl

# (C) F5, Inc.
# Vadim Zhestikov

# Tests for top-level await of promises in QuickJS engine.

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
    var globalResult = await Promise.resolve("resolved value");

    function test(r) {
        r.return(200, "global result: " + globalResult);
    }

    export default {test};

EOF

$t->write_file('fulfilled_test.js', <<'EOF');
    var globalResult = await new Promise((resolve) => {
        Promise.resolve().then(() => {
            resolve("fulfilled value");
        });
    });

    function test(r) {
        r.return(200, "fulfilled result: " + globalResult);
    }

    export default {test};

EOF

$t->try_run('no top-level await support')->plan(2);

###############################################################################

like(http_get('/resolved'), qr/global result: resolved value/,
    'basic global await works');
like(http_get('/fulfilled'), qr/fulfilled result: fulfilled value/,
    'fulfilled promise via microtask works');

###############################################################################
