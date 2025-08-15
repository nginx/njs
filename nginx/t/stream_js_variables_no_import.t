#!/usr/bin/perl

# (C) Dmitry Volyntsev
# (C) Nginx, Inc.

# Tests for stream njs module, setting nginx variables, no js import.

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

    js_set $no_import  test.variable;

    server {
        listen  127.0.0.1:8081;
        return  "NO IMPORT:$no_import";
    }
}

EOF

$t->try_run('no stream njs available')->plan(2);

###############################################################################

is(stream('127.0.0.1:' . port(8081))->read(), 'NO IMPORT:',
	'js_set without function is empty');

$t->stop();

ok(index($t->read_file('error.log'),
	'no "js_import" directives found for "js_set" "test.variable"') > 0,
	'js_set without js_import logs error');

###############################################################################
