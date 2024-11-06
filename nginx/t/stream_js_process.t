#!/usr/bin/perl

# (C) Dmitry Volyntsev
# (C) Nginx, Inc.

# Tests for stream njs module, process object.

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

env FOO=bar;
env BAR=baz;

stream {
    %%TEST_GLOBALS_STREAM%%

    js_import test.js;

    js_set $env_foo            test.env_foo;
    js_set $env_bar            test.env_bar;
    js_set $env_home           test.env_home;
    js_set $argv               test.argv;

    server {
        listen  127.0.0.1:8081;
        return  $env_foo;
    }

    server {
        listen  127.0.0.1:8082;
        return  $env_bar;
    }

    server {
        listen 127.0.0.1:8083;
        return $env_home;
    }

    server {
        listen 127.0.0.1:8084;
        return $argv;
    }
}

EOF

$t->write_file('test.js', <<EOF);
    function env(s, v) {
        var e = process.env[v];
        return e ? e : 'undefined';
    }

    function env_foo(s) {
        return env(s, 'FOO');
    }

    function env_bar(s) {
        return env(s, 'BAR');
    }

    function env_home(s) {
        return env(s, 'HOME');
    }

    function argv(r) {
        var av = process.argv;
        return `\${Array.isArray(av)} \${av[0].indexOf('nginx') >= 0}`;
    }

    export default { env_foo, env_bar, env_home, argv };

EOF

$t->try_run('no njs stream session object')->plan(4);

###############################################################################

is(stream('127.0.0.1:' . port(8081))->read(), 'bar', 'env.FOO');
is(stream('127.0.0.1:' . port(8082))->read(), 'baz', 'env.BAR');
is(stream('127.0.0.1:' . port(8083))->read(), 'undefined', 'env HOME');
is(stream('127.0.0.1:' . port(8084))->read(), 'true true', 'argv');

###############################################################################
