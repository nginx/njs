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

        location /argv {
            js_content test.argv;
        }

        location /env {
            js_content test.env;
        }
    }
}

EOF

$t->write_file('test.js', <<EOF);
    function argv(r) {
        var av = process.argv;
        r.return(200,`\${Array.isArray(av)} \${av[0].indexOf('nginx') >= 0}`);
    }

    function env(r) {
        var e = process.env[r.args.var];
        r.return(200, e ? e : 'undefined');
    }

    export default { argv, env };

EOF

$t->try_run('no njs process object')->plan(4);

###############################################################################

like(http_get('/argv'), qr/true true/, 'argv');
like(http_get('/env?var=FOO'), qr/bar/, 'env FOO');
like(http_get('/env?var=BAR'), qr/baz/, 'env BAR');
like(http_get('/env?var=HOME'), qr/undefined/, 'env HOME');

###############################################################################
