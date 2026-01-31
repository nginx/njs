#!/usr/bin/perl

# (C) Dmitry Volyntsev
# (C) F5, Inc.

# Tests for http njs module, fetch method error stack traces.

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

my $t = Test::Nginx->new()->has(qw/http/)
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

        location /testAsync {
            js_content test.testAsync;
        }

        location /testSync {
            js_content test.testSync;
        }

        location /testAsyncThrow {
            js_content test.testAsyncThrow;
        }
    }
}

EOF

$t->write_file('test.js', <<'EOF');
	async function testAsync(r) {
		try {
			await ngx.fetch('http://127.0.0.1:12345');

		} catch(e) {
			r.return(200, `stack: ${e.stack}\n`);
		}
	}

	function testSync(r) {
		try {
			throw Error('oops');

		} catch(e) {
			r.return(200, `stack: ${e.stack}\n`);
		}
	}

	async function testAsyncThrow(r) {
		try {
			throw Error('oops');

		} catch(e) {
			r.return(200, `stack: ${e.stack}\n`);
		}
	}

	export default { testAsync, testSync, testAsyncThrow }
EOF

$t->try_run('no njs');

$t->plan(6);

###############################################################################

like(http_get('/testSync'), qr/stack: Error: oops/s, 'sync stack exists');
like(http_get('/testSync'), qr/at testSync \([^)]*test\.js:12\)/s,
    'sync stack line number');

like(http_get('/testAsync'), qr/stack: Error: connect failed/s,
    'async stack exists');
like(http_get('/testAsync'), qr/at testAsync \([^)]*test\.js:3\)/s,
    'async stack line number');

like(http_get('/testAsyncThrow'), qr/stack: Error: oops/s,
    'async throw stack exists');
like(http_get('/testAsyncThrow'), qr/at testAsyncThrow \([^)]*test\.js:21\)/s,
    'async throw stack line number');

###############################################################################
