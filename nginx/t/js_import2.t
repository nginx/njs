#!/usr/bin/perl

# (C) Dmitry Volyntsev
# (c) Nginx, Inc.

# Tests for http njs module, js_import directive in server | location contexts.

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

my $t = Test::Nginx->new()->has(qw/http proxy rewrite/)
	->write_file_expand('nginx.conf', <<'EOF');

%%TEST_GLOBALS%%

daemon off;

events {
}

http {
    %%TEST_GLOBALS_HTTP%%

    server {
        listen       127.0.0.1:8080;
        server_name  localhost;

        js_set $test foo.bar.p;

        # context 1
        js_import foo from main.js;

        location /njs {
            js_content foo.version;
        }

        location /test_foo {
            js_content foo.test;
        }

        location /test_bar {
            js_content foo.test;
        }

        location /test_lib {
            # context 2
            js_import lib.js;
            js_content lib.test;
        }

        location /test_fun {
            # context 3
            js_import fun.js;
            js_content fun;
        }

        location /test_var {
            return 200 $test;
        }

        location /proxy {
            proxy_pass http://127.0.0.1:8081/;
        }
    }

    server {
        listen       127.0.0.1:8081;
        server_name  localhost;

        location /test_fun {
            # context 4
            js_import fun.js;
            js_content fun;
        }
    }
}

EOF

$t->write_file('lib.js', <<EOF);
    function test(r) {
        r.return(200, "LIB-TEST:" + ngx.engine_id);
    }

    function p(r) {
        return "LIB-P";
    }

    export default {test, p};

EOF

$t->write_file('fun.js', <<EOF);
    export default function (r) {
        r.return(200, "FUN-TEST:" + ngx.engine_id);
    };

EOF

$t->write_file('main.js', <<EOF);
    function version(r) {
        r.return(200, njs.version);
    }

    function test(r) {
        r.return(200, "MAIN-TEST:" + ngx.engine_id);
    }

    export default {version, test, bar: {p(r) {return "P-TEST:" + ngx.engine_id}}};

EOF

$t->try_run('no njs available');

###############################################################################

my ($mainid) = http_get('/test_foo') =~ /MAIN-TEST:(\d+)/s;

plan(skip_all => 'ngx.engine_id requires --with-debug')
    unless defined $mainid;

$t->plan(5);

my ($barid) = http_get('/test_bar') =~ /MAIN-TEST:(\d+)/s;

ok($barid == $mainid, 'same context for main.js');

my ($libid) = http_get('/test_lib') =~ /LIB-TEST:(\d+)/s;

ok($libid != $mainid, 'different context for lib.js');

my ($funid) = http_get('/test_fun') =~ /FUN-TEST:(\d+)/s;

ok($funid != $mainid && $funid != $libid,
   'different context for fun.js');

my ($pfunid) = http_get('/proxy/test_fun') =~ /FUN-TEST:(\d+)/s;

ok($pfunid != $funid && $pfunid != $mainid && $pfunid != $libid,
   'different context for fun.js in proxy');

my ($varid) = http_get('/test_var') =~ /P-TEST:(\d+)/s;

ok($varid == $mainid, 'variable from main.js');

###############################################################################
