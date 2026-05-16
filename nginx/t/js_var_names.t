#!/usr/bin/perl

# (C) Dmitry Volyntsev
# (C) F5, Inc.

# Tests for http njs module, r.jsVarNames() method.

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

    js_import test.js;

    js_var $foo;
    js_var $test_method;
    js_var $test_params_name;
    js_var $test_params_arguments_workspace;
    js_var $other_name;
    js_set $js_set_var test.set_var;

    server {
        listen       127.0.0.1:8080;
        server_name  localhost;

        set $set_var conf_set;

        location /all {
            js_content test.all;
        }

        location /prefix {
            js_content test.prefix;
        }

        location /empty_prefix {
            js_content test.empty_prefix;
        }

        location /none {
            js_content test.none;
        }

        location /array {
            js_content test.array;
        }

        location /excludes {
            js_content test.excludes;
        }

        location /fresh {
            js_content test.fresh;
        }

        location /type {
            js_content test.type;
        }
    }
}

EOF

$t->write_file('test.js', <<'EOF');
    function render(names) {
        return names.sort().join('|');
    }

    function all(r) {
        r.return(200, render(r.jsVarNames()));
    }

    function prefix(r) {
        r.return(200, render(r.jsVarNames('test_')));
    }

    function empty_prefix(r) {
        r.return(200, render(r.jsVarNames('')));
    }

    function none(r) {
        r.return(200, String(r.jsVarNames('none_').length));
    }

    function array(r) {
        r.return(200, String(Array.isArray(r.jsVarNames())));
    }

    function excludes(r) {
        let names = r.jsVarNames();

        r.return(200, String(names.indexOf('js_set_var') == -1
                             && names.indexOf('set_var') == -1
                             && names.indexOf('remote_addr') == -1));
    }

    function fresh(r) {
        let names = r.jsVarNames('test_');
        names.push('test_bad');

        r.return(200, r.jsVarNames('test_').indexOf('test_bad') == -1
                      ? 'fresh' : 'shared');
    }

    function type(r) {
        try {
            r.jsVarNames(1);
            r.return(200, 'no error');

        } catch (e) {
            r.return(200, e.name + ':' + e.message);
        }
    }

    function set_var(r) {
        return 'set';
    }

    export default {all, prefix, empty_prefix, none, array, excludes, fresh,
                    type, set_var};
EOF

$t->try_run('no r.jsVarNames')->plan(8);

###############################################################################

my $all = 'foo|other_name|test_method|'
    . 'test_params_arguments_workspace|test_params_name';

my $prefix = 'test_method|test_params_arguments_workspace|test_params_name';

is(http_get_body('/all'), $all, 'jsVarNames all js_var names');
is(http_get_body('/prefix'), $prefix, 'jsVarNames prefix');
is(http_get_body('/empty_prefix'), $all, 'jsVarNames empty prefix');
is(http_get_body('/none'), '0', 'jsVarNames prefix no match');
is(http_get_body('/array'), 'true', 'jsVarNames returns an array');
is(http_get_body('/excludes'), 'true', 'jsVarNames excludes other variables');
is(http_get_body('/fresh'), 'fresh', 'jsVarNames fresh array');
like(http_get_body('/type'), qr/^TypeError:.*prefix.*string/,
    'jsVarNames prefix type');

$t->stop();

###############################################################################

sub http_get_body {
    my ($uri) = @_;

    http_get($uri) =~ /\x0d\x0a?\x0d\x0a?(.*)/ms;
    return $1;
}

###############################################################################
