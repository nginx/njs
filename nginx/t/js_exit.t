#!/usr/bin/perl

# (C) Dmitry Volyntsev
# (C) F5, Inc.

# Tests for http njs module, njs.on('exit', ...).

###############################################################################

use warnings;
use strict;

use Test::More;
use Socket qw/ CRLF /;

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

        location /test {
            js_content test.test;
        }
    }
}

EOF

$t->write_file('test.js', <<EOF);
    function test(r) {
	    njs.on('exit', function() {
		    ngx.log(ngx.WARN, `exit hook: bs: \${r.variables.bytes_sent}`);
	    });

	    r.return(200, `bs: \${r.variables.bytes_sent}`);
    }

    export default { test };

EOF

$t->try_run('no njs')->plan(2);

###############################################################################

like(http_get('/test'), qr/bs: 0/, 'response');

$t->stop();

like($t->read_file('error.log'), qr/\[warn\].*exit hook: bs: \d+/, 'exit hook logged');

###############################################################################
