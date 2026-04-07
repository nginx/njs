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

    js_import main from main.js;
    js_set $stream_engine_id main.engine_id;

    server {
        listen  127.0.0.1:8081;
        js_import foo from ./main.js;
        js_set $test foo.bar.p;
        return  $test;
    }

    server {
        listen  127.0.0.1:8082;
        return  $stream_engine_id;
    }

    server {
        listen  127.0.0.1:8083;
        return  $stream_engine_id;
    }

    server {
        listen  127.0.0.1:8084;
        js_import bar from ./main.js;
        return  $stream_engine_id;
    }
}

EOF

$t->write_file('main.js', <<EOF);
    function engine_id(s) {
        return String(ngx.engine_id);
    }

    export default {engine_id, bar: {p(s) {return "P-TEST"}}};

EOF

$t->try_run('no njs available');

###############################################################################

my ($id_82) = stream('127.0.0.1:' . port(8082))->read() =~ /(\d+)/;
plan(skip_all => 'ngx.engine_id requires --with-debug')
    unless defined $id_82;

$t->plan(3);

is(stream('127.0.0.1:' . port(8081))->read(), 'P-TEST', 'foo.bar.p');

my ($id_83) = stream('127.0.0.1:' . port(8083))->read() =~ /(\d+)/;
my ($id_84) = stream('127.0.0.1:' . port(8084))->read() =~ /(\d+)/;

ok($id_82 == $id_83, 'same context for stream level import');
ok($id_84 != $id_82, 'different context for server level import');

###############################################################################
