#!/usr/bin/perl

# (C) Dmitry Volyntsev
# (C) Nginx, Inc.

# Tests for http njs module, console object.

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

my $t = Test::Nginx->new()->has(qw/http/)
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

        location /dump {
            js_content test.dump;
        }

        location /error {
            js_content test.error;
        }

        location /info {
            js_content test.info;
        }

        location /log {
            js_content test.log;
        }

        location /time {
            js_content test.time;
        }

        location /time_test {
            js_content test.time_test;
        }

        location /warn {
            js_content test.warn;
        }
    }
}

EOF

$t->write_file('test.js', <<EOF);
    function engine(r) {
        r.return(200, njs.engine);
    }

    function l(r, method) {
        const data = Buffer.from(r.args.data, 'base64');
        const object = JSON.parse(data);
        console[method](object);
        r.return(200);
    }

    function dump(r) {
        l(r, 'dump');
    }

    function error(r) {
        l(r, 'error');
    }

    function info(r) {
        l(r, 'info');
    }

    function log(r) {
        l(r, 'log');
    }

    function time(r) {
        console.time(r.args.timer);
        setTimeout(function() {
            console.timeEnd(r.args.timer);
            r.return(200);
        }, parseInt(r.args.delay));
    }

    function time_test(r) {
        console.time();
        console.time();
        console.timeEnd();
        console.timeEnd('test');
    }

    function warn(r) {
        l(r, 'warn');
    }

    export default {engine, dump, error, info, log, time, time_test, warn};

EOF

$t->try_run('no njs console')->plan(7);

###############################################################################

my $engine = http_get('/engine');

http_get('/dump?data=eyJhIjpbMiwzXX0');
http_get('/error?data=IldBS0Ei');
http_get('/info?data=IkJBUiI');
http_get('/log?data=eyJhIjpbIkIiLCJDIl19');
http_get('/time?delay=7&timer=foo');
http_get('/time_test');
http_get('/warn?data=IkZPTyI');

$t->stop();

like($t->read_file('error.log'), qr/\[error\].*js: WAKA/, 'console.error');
like($t->read_file('error.log'), qr/\[info\].*js: BAR/, 'console.info');

SKIP: {
	skip "QuickJS has no console.dump() method.", 1
		if $engine =~ /QuickJS$/m;

like($t->read_file('error.log'), qr/\[info\].*js: \{a:\['B','C'\]\}/,
	'console.log with object');

}

like($t->read_file('error.log'), qr/\[warn\].*js: FOO/, 'console.warn');
like($t->read_file('error.log'), qr/\[info\].*js: foo: \d+\.\d\d\d\d\d\dms/,
	'console.time foo');
like($t->read_file('error.log'), qr/\[info\].*js: Timer \"default\" already/,
	'console.time already started');
like($t->read_file('error.log'), qr/\[info\].*js: Timer \"test\" doesn't/,
	'console.time not started');

###############################################################################
