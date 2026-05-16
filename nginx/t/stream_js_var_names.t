#!/usr/bin/perl

# (C) Dmitry Volyntsev
# (C) F5, Inc.

# Tests for stream njs module, s.jsVarNames() method.

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

    js_import test.js;

    js_var $foo;
    js_var $test_method;
    js_var $test_params_name;
    js_var $test_params_arguments_workspace;
    js_var $other_name;
    js_set $js_set_var test.set_var;
    js_set $all test.all;
    js_set $prefix test.prefix;
    js_set $empty_prefix test.empty_prefix;
    js_set $none test.none;
    js_set $array test.array;
    js_set $excludes test.excludes;
    js_set $fresh test.fresh;
    js_set $type test.type;
    server {
        listen  127.0.0.1:8081;
        return  $all;
    }

    server {
        listen  127.0.0.1:8082;
        return  $prefix;
    }

    server {
        listen  127.0.0.1:8083;
        return  $empty_prefix;
    }

    server {
        listen  127.0.0.1:8084;
        return  $none;
    }

    server {
        listen  127.0.0.1:8085;
        return  $array;
    }

    server {
        listen  127.0.0.1:8086;
        return  $excludes;
    }

    server {
        listen  127.0.0.1:8087;
        return  $fresh;
    }

    server {
        listen  127.0.0.1:8088;
        return  $type;
    }
}

EOF

$t->write_file('test.js', <<'EOF');
    function render(names) {
        return names.sort().join('|');
    }

    function all(s) {
        return render(s.jsVarNames());
    }

    function prefix(s) {
        return render(s.jsVarNames('test_'));
    }

    function empty_prefix(s) {
        return render(s.jsVarNames(''));
    }

    function none(s) {
        return String(s.jsVarNames('none_').length);
    }

    function array(s) {
        return String(Array.isArray(s.jsVarNames()));
    }

    function excludes(s) {
        let names = s.jsVarNames();

        return String(names.indexOf('js_set_var') == -1
                      && names.indexOf('remote_addr') == -1);
    }

    function fresh(s) {
        let names = s.jsVarNames('test_');
        names.push('test_bad');

        return s.jsVarNames('test_').indexOf('test_bad') == -1
               ? 'fresh' : 'shared';
    }

    function type(s) {
        try {
            s.jsVarNames(1);
            return 'no error';

        } catch (e) {
            return e.name + ':' + e.message;
        }
    }

    function set_var(s) {
        return 'set';
    }

    export default {all, prefix, empty_prefix, none, array, excludes, fresh,
                    type, set_var};
EOF

$t->try_run('no s.jsVarNames')->plan(8);

###############################################################################

my $all = 'foo|other_name|test_method|'
	. 'test_params_arguments_workspace|test_params_name';

my $prefix = 'test_method|test_params_arguments_workspace|test_params_name';

is(stream('127.0.0.1:' . port(8081))->read(), $all,
	'jsVarNames all js_var names');
is(stream('127.0.0.1:' . port(8082))->read(), $prefix,
	'jsVarNames prefix');
is(stream('127.0.0.1:' . port(8083))->read(), $all,
	'jsVarNames empty prefix');
is(stream('127.0.0.1:' . port(8084))->read(), '0',
	'jsVarNames prefix no match');
is(stream('127.0.0.1:' . port(8085))->read(), 'true',
	'jsVarNames returns an array');
is(stream('127.0.0.1:' . port(8086))->read(), 'true',
	'jsVarNames excludes other variables');
is(stream('127.0.0.1:' . port(8087))->read(), 'fresh',
	'jsVarNames fresh array');
like(stream('127.0.0.1:' . port(8088))->read(),
	qr/^TypeError:.*prefix.*string/, 'jsVarNames prefix type');

$t->stop();

###############################################################################
