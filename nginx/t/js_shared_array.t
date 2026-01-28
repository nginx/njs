#!/usr/bin/perl

# (C) Dmitry Volyntsev
# (C) F5, Inc.

# Tests for http njs module - Shared Array Buffer support.

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
worker_processes  4;

events {
}

http {
    %%TEST_GLOBALS_HTTP%%

    js_shared_array_zone zone=test:32k;
    js_shared_array_zone zone=buffer:32k;

    js_import test.js;

    server {
        listen       127.0.0.1:8080;
        server_name  localhost;

        location /init {
            js_content test.init;
        }

        location /size {
            js_content test.size;
        }

        location /increment {
            js_content test.increment;
        }

        location /read {
            js_content test.read;
        }

        location /write {
            js_content test.write;
        }

        location /keys {
            js_content test.keys;
        }

        location /readLocked {
            js_content test.readLocked;
        }

        location /writeLocked {
            js_content test.writeLocked;
        }

        location /lockException {
            js_content test.lockException;
        }

        location /lockReturnValue {
            js_content test.lockReturnValue;
        }
    }
}

EOF

$t->write_file('test.js', <<EOF);
    function init(r) {
        var sab = ngx.sharedArray.test.buffer;
        var view = new Int32Array(sab);
        r.return(200, "init: " + view[0]);
    }

    function increment(r) {
        var sab = ngx.sharedArray.test.buffer;
        var view = new Int32Array(sab);
        var val = Atomics.add(view, 0, 1);
        r.return(200, "inc: " + (val + 1).toString());
    }

    function size(r) {
        var sab = ngx.sharedArray.buffer.buffer;
        r.return(200, "size: " + sab.byteLength);
    }

    function read(r) {
        var sab = ngx.sharedArray.buffer.buffer;
        var view = new Uint8Array(sab);
        var at = r.args.at || 0;
        r.return(200, "read: " + view[at]);
    }

    function write(r) {
        var sab = ngx.sharedArray.buffer.buffer;
        var view = new Uint8Array(sab);
        var at = r.args.at || 0;
        view[at] = parseInt(r.args.value) || 0;

        r.return(200, "written");
    }

    function keys(r) {
        var k = Object.keys(ngx.sharedArray);
        r.return(200, JSON.stringify(k.sort()));
    }

    function readLocked(r) {
        var result = ngx.sharedArray.test.readLock((buf) => {
            var view = new Int32Array(buf);
            return view[0];
        });

        r.return(200, "readLocked: " + result);
    }

    function writeLocked(r) {
        ngx.sharedArray.test.writeLock((buf) => {
            var view = new Int32Array(buf);
            view[0] = parseInt(r.args.value) || 0;
        });

        r.return(200, "writeLocked");
    }

    function lockException(r) {
        try {
            ngx.sharedArray.test.writeLock((buf) => {
                throw new Error("test exception");
            });

            r.return(200, "should not reach here");

        } catch (e) {
            r.return(200, "exception: " + e.message);
        }
    }

    function lockReturnValue(r) {
        var result = ngx.sharedArray.test.readLock((buf) => {
            var view = new Int32Array(buf);
            return {value: view[0], doubled: view[0] * 2};
        });

        r.return(200, "result: " + JSON.stringify(result));
    }

    export default { init, increment, read, size, write, keys, readLocked,
                     writeLocked, lockException, lockReturnValue };
EOF

$t->try_run('no js_shared_array_zone')->plan(18);

###############################################################################

like(http_get('/init'), qr/init: 0/, 'initialize shared array');
like(http_get('/size'), qr/size: 32768/, 'check buffer size');
like(http_get('/increment'), qr/inc: 1/, 'first increment');
like(http_get('/increment'), qr/inc: 2/, 'second increment');
like(http_get('/write?at=5&value=6'), qr/written/, 'write to buffer');
like(http_get('/read?at=5'), qr/read: 6/, 'read from buffer');
like(http_get('/read?at=32767'), qr/read: 0/, 'read from buffer end');
like(http_get('/read?at=32768'), qr/read: undefined/,
	'read out of buffer bounds');
like(http_get('/write?at=32767&value=1'), qr/written/, 'write to buffer end');
like(http_get('/read?at=32767'), qr/read: 1/, 'read from buffer end again');
like(http_get('/keys'), qr/\["buffer","test"\]/, 'enumerate zones');
like(http_get('/readLocked'), qr/readLocked: 2/, 'read with lock');
like(http_get('/writeLocked?value=42'), qr/writeLocked/, 'write with lock');
like(http_get('/readLocked'), qr/readLocked: 42/, 'read locked value');
like(http_get('/writeLocked?value=100'), qr/writeLocked/,
	'write with lock again');
like(http_get('/init'), qr/init: 100/, 'verify locked write');
like(http_get('/lockException'), qr/exception: test exception/,
	'exception in lock callback');
like(http_get('/lockReturnValue'), qr/result: \{"value":100,"doubled":200\}/,
	'lock return value');

###############################################################################
