#!/usr/bin/perl

# (C) Dmitry Volyntsev
# (C) Nginx, Inc.

# Tests for http njs module, setting nginx variables, no js_import.

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

    js_set $no_import   test.no_import;

    server {
        listen       127.0.0.1:8080;
        server_name  localhost;

        location /no_import {
            return 200 "NO IMPORT:$no_import";
        }
    }
}

EOF

$t->try_run('no njs')->plan(2);

###############################################################################

like(http_get('/no_import'), qr/NO IMPORT:/,
	'js_set without js_import is empty');

$t->stop();

ok(index($t->read_file('error.log'),
	'no "js_import" directives found for "js_set" "test.no_import"') > 0,
	'js_set without function logs error');

###############################################################################
