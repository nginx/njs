#!/usr/bin/perl

# (C) Vadim Zhestikov
# (C) Nginx, Inc.

# Tests for stream njs module, js_preload_object directive.

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

http {
    %%TEST_GLOBALS_HTTP%%

    js_import main.js;

    server {
        listen       127.0.0.1:8080;
        server_name  localhost;

        location /engine {
            js_content main.engine;
        }
    }
}

stream {
    %%TEST_GLOBALS_STREAM%%

    js_preload_object g1 from g.json;

    js_set $test foo.bar.p;

    js_import lib.js;
    js_import foo from ./main.js;

    server {
        listen  127.0.0.1:8081;
        return  $test;
    }

    server {
        listen      127.0.0.1:8082;
        js_access   lib.access;
        js_preread  lib.preread;
        js_filter   lib.filter;
        proxy_pass  127.0.0.1:8083;
    }

    server {
        listen  127.0.0.1:8083;
        return  "x";
    }
}

EOF

$t->write_file('lib.js', <<EOF);
    var res = '';
    var acc, pup, fup, fdown;

    function access(s) {
        acc = g1.a;
        s.allow();
    }

    function preread(s) {
        s.on('upload', function (data) {
            pup = g1.b[1];
            if (data.length > 0) {
                s.done();
            }
        });
    }

    function filter(s) {
        s.on('upload', function(data, flags) {
            fup = g1.c.prop[0].a;
            s.send(data);
        });

        s.on('download', function(data, flags) {
            fdown = g1.b[3];
            s.send(data);

            if (flags.last) {
                s.send(`\${acc}\${pup}\${fup}\${fdown}`, flags);
                s.off('download');
            }
        });
    }

    export default {access, preread, filter};

EOF

$t->write_file('main.js', <<EOF);
    function engine(r) {
        r.return(200, njs.engine);
    }

    export default {engine, bar: {p(s) {return g1.b[2]}}};

EOF

$t->write_file('g.json',
	'{"a":1, "b":[1,2,"element",4,5], "c":{"prop":[{"a":3}]}}');

$t->try_run('no js_preload_object available');

plan(skip_all => 'not yet') if http_get('/engine') =~ /QuickJS$/m;

$t->plan(2);

###############################################################################

is(stream('127.0.0.1:' . port(8081))->read(), 'element', 'foo.bar.p');
is(stream('127.0.0.1:' . port(8082))->io('0'), 'x1234', 'filter chain');

###############################################################################
