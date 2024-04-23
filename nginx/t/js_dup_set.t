#!/usr/bin/perl

# (C) Dmitry Volyntsev
# (C) Nginx, Inc.

# Tests for http njs module, duplicate identical js_set directives.

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

        location /set1 {
            js_set $test test.foo;
            return 200 set1:$test;
        }

        location /set2 {
            js_set $test test.foo;
            return 200 set2:$test;
        }
    }
}

EOF

$t->write_file('test.js', <<EOF);
    function foo(r) {
        return 42;
    }

    export default {foo};

EOF

$t->try_run('no njs')->plan(2);

###############################################################################

like(http_get('/set1'), qr/set1:42/, '/set1 location');
like(http_get('/set2'), qr/set2:42/, '/set2 location');

###############################################################################
