#!/usr/bin/perl

# (C) Dmitry Volyntsev
# (C) Nginx, Inc.

# Tests for http njs module, setting nginx variables, no function.

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

    js_set $no_function   test.variable;

    js_import test.js;

    server {
        listen       127.0.0.1:8080;
        server_name  localhost;

        location /no_function {
            return 200 "NO FUNCTION:$no_function";
        }
    }
}

EOF

$t->write_file('test.js', <<EOF);
    export default {};

EOF

$t->try_run('no njs')->plan(2);

###############################################################################

like(http_get('/no_function'), qr/NO FUNCTION:/,
	'js_set without function is empty');

$t->stop();

ok(index($t->read_file('error.log'),
	'js function "test.variable" not found') > 0,
	'js_set without function logs error');

###############################################################################
