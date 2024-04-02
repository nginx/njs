#!/usr/bin/perl

# (C) Dmitry Volyntsev
# (C) Nginx, Inc.

# Tests for stream njs module, js_import directive in server context.

###############################################################################

use warnings;
use strict;

use Test::More;

BEGIN { use FindBin; chdir($FindBin::Bin); }

use lib 'lib';
use Test::Nginx;
use Test::Nginx::Stream qw/ stream /;

###############################################################################

select STDERR; $| = 1;
select STDOUT; $| = 1;

my $t = Test::Nginx->new()->has(qw/stream stream_return/)
	->write_file_expand('nginx.conf', <<'EOF');

%%TEST_GLOBALS%%

daemon off;

events {
}

stream {
    %%TEST_GLOBALS_STREAM%%

    server {
        listen  127.0.0.1:8081;
        js_import foo from ./main.js;
        js_set $test foo.bar.p;
        return  $test;
    }
}

EOF

$t->write_file('main.js', <<EOF);
    export default {bar: {p(s) {return "P-TEST"}}};

EOF

$t->try_run('no njs available')->plan(1);

###############################################################################

is(stream('127.0.0.1:' . port(8081))->read(), 'P-TEST', 'foo.bar.p');

###############################################################################
