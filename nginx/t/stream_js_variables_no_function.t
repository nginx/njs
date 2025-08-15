#!/usr/bin/perl

# (C) Dmitry Volyntsev
# (C) Nginx, Inc.

# Tests for stream njs module, setting nginx variables, no function.

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

    js_set $no_function  test.variable;

    js_import test.js;

    server {
        listen  127.0.0.1:8081;
        return  "NO FUNCTION:$no_function";
    }
}

EOF

$t->write_file('test.js', <<EOF);
    export default {};

EOF

$t->try_run('no stream njs available')->plan(2);

###############################################################################

is(stream('127.0.0.1:' . port(8081))->read(), 'NO FUNCTION:',
	'js_set without function is empty');

$t->stop();

ok(index($t->read_file('error.log'),
	'js function "test.variable" not found') > 0,
	'js_set without function logs error');

###############################################################################
