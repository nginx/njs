#!/usr/bin/perl

# (C) Dmitry Volyntsev
# (C) Nginx, Inc.

# Tests for http njs module, process object.

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

env FOO=bar;
env BAR=baz;

http {
    %%TEST_GLOBALS_HTTP%%

    js_import test.js;

    server {
        listen       127.0.0.1:8080;
        server_name  localhost;

        location /env {
            js_content test.env;
        }

        location /env_keys {
            js_content test.env_keys;
        }
    }
}

EOF

$t->write_file('test.js', <<EOF);
    function env(r) {
        r.return(200, process.env[r.args.var]);
    }

    function env_keys(r) {
		r.return(200, Object.keys(process.env).join(','));
	}

    export default { env, env_keys };

EOF

$t->try_run('no njs process object')->plan(3);

###############################################################################

like(http_get('/env?var=FOO'), qr/bar/, 'env FOO');
like(http_get('/env?var=BAR'), qr/baz/, 'env BAR');
like(http_get('/env_keys'), qr/FOO,BAR/, 'env keys');

###############################################################################
