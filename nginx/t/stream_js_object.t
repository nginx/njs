#!/usr/bin/perl

# (C) Dmitry Volyntsev
# (C) Nginx, Inc.

# Tests for stream njs module, stream session object.

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

    js_import test.js;

    server {
        listen       127.0.0.1:8080;
        server_name  localhost;

        location /engine {
            js_content test.engine;
        }
    }
}

stream {
    %%TEST_GLOBALS_STREAM%%

    js_import test.js;

    js_set $to_string            test.to_string;
    js_set $define_prop          test.define_prop;
    js_set $in_operator          test.in_operator;
    js_set $redefine_proto       test.redefine_proto;
    js_set $get_own_prop_descs   test.get_own_prop_descs;

    server {
        listen  127.0.0.1:8081;
        return  $to_string;
    }

    server {
        listen  127.0.0.1:8082;
        return  $define_prop$status;
    }

    server {
        listen 127.0.0.1:8083;
        return $in_operator;
    }

    server {
        listen 127.0.0.1:8084;
        return $redefine_proto;
    }

    server {
        listen 127.0.0.1:8085;
        return $get_own_prop_descs;
    }
}

EOF

$t->write_file('test.js', <<EOF);
    function engine(r) {
        r.return(200, njs.engine);
    }

    function to_string(s) {
        return s.toString();
    }

    function define_prop(s) {
        Object.defineProperty(s.variables, 'status', {value:400});
        return s.variables.status;
    }

    function in_operator(s) {
        return ['status', 'unknown'].map(v=>v in s.variables).toString();
    }

    function redefine_proto(s) {
        s[0] = 'a';
        s[1] = 'b';
        s.length = 2;
        Object.setPrototypeOf(s, Array.prototype);
        return s.join('|') === 'a|b';
    }

    function get_own_prop_descs(s) {
        return Object.getOwnPropertyDescriptors(s)['on'].value === s.on;
    }

    export default { engine, to_string, define_prop, in_operator,
                     redefine_proto, get_own_prop_descs };

EOF

$t->try_run('no njs stream session object')->plan(5);

###############################################################################

is(stream('127.0.0.1:' . port(8081))->read(), '[object Stream Session]',
	'to_string');
is(stream('127.0.0.1:' . port(8082))->read(), '400400', 'define_prop');
is(stream('127.0.0.1:' . port(8083))->read(), 'true,false', 'in_operator');
is(stream('127.0.0.1:' . port(8084))->read(), 'true', 'redefine_proto');

SKIP: {
	skip "In QuickJS methods are in the prototype", 1
		if http_get('/engine') =~ /QuickJS$/m;

is(stream('127.0.0.1:' . port(8085))->read(), 'true', 'get_own_prop_descs');

}

###############################################################################
