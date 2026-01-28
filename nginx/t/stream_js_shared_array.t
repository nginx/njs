#!/usr/bin/perl

# (C) Dmitry Volyntsev
# (C) F5, Inc.

# Tests for stream njs module - Shared Array Buffer support.

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
worker_processes  4;

events {
}

stream {
    %%TEST_GLOBALS_STREAM%%

    js_shared_array_zone zone=test:32k;
    js_shared_array_zone zone=buffer:32k;

    js_import test.js;

    js_set $init test.init;
    js_set $size test.size;
    js_set $increment test.increment;
    js_set $keys test.keys;

    js_set $result test.result;

    server {
        listen  127.0.0.1:8081;
        return  $init;
    }

    server {
        listen  127.0.0.1:8082;
        return  $size;
    }

    server {
        listen  127.0.0.1:8083;
        return  $increment;
    }

    server {
        listen  127.0.0.1:8084;
        return  $keys;
    }

    server {
        listen      127.0.0.1:8085;
        js_preread  test.handle_command;
        return      $result;
    }
}

EOF

$t->write_file('test.js', <<EOF);
    function init(s) {
        var sab = ngx.sharedArray.test.buffer;
        var view = new Int32Array(sab);
        return "init: " + view[0];
    }

    function increment(s) {
        var sab = ngx.sharedArray.test.buffer;
        var view = new Int32Array(sab);
        var val = Atomics.add(view, 0, 1);
        return "inc: " + (val + 1).toString();
    }

    function size(s) {
        var sab = ngx.sharedArray.buffer.buffer;
        return "size: " + sab.byteLength;
    }

    function keys(s) {
        var k = Object.keys(ngx.sharedArray);
        return JSON.stringify(k.sort());
    }

    function handle_command(s) {
        var collect = '';

        s.on('upload', function (data, flags) {
            collect += data;

            var n = collect.search('\\n');
            if (n == -1) {
                return;
            }

            var cmd = collect.substr(0, n);
            var parts = cmd.split(' ');
            var action = parts[0];
            var shared = ngx.sharedArray;
            var at = parseInt(parts[1]) || 0;

            if (action === 'read' || action === 'readLock') {

                if (action === 'readLock') {
                    var result = shared.test.readLock(function(buf) {
                        return new Int32Array(buf)[at];
                    });

                    s.variables.result = action + ": " + result;

                } else {
                    var result = new Uint8Array(shared.buffer.buffer)[at];
                    s.variables.result = action + ": " + result;
                }

            } else if (action === 'write' || action === 'writeLock') {
                var value = parseInt(parts[2]) || 0;

                if (action === 'writeLock') {
                    shared.test.writeLock(function(buf) {
                        new Int32Array(buf)[at] = value;
                    });

                } else {
                    new Uint8Array(shared.buffer.buffer)[at] = value;
                }

                s.variables.result = "written";
            }

            s.off('upload');
            s.done();
        });
    }

    function result(s) {
        return s.variables.result;
    }

    export default { init, increment, size, keys, handle_command, result };
EOF

$t->try_run('no js_shared_array_zone')->plan(15);

###############################################################################

is(stream('127.0.0.1:' . port(8081))->read(), 'init: 0',
    'initialize shared array');
is(stream('127.0.0.1:' . port(8082))->read(), 'size: 32768',
    'check buffer size');
is(stream('127.0.0.1:' . port(8083))->read(), 'inc: 1', 'first increment');
is(stream('127.0.0.1:' . port(8083))->read(), 'inc: 2', 'second increment');
is(stream('127.0.0.1:' . port(8085))->io("write 5 6\n"), "written",
    'write to buffer');
is(stream('127.0.0.1:' . port(8085))->io("read 5\n"), "read: 6",
    'read from buffer');
is(stream('127.0.0.1:' . port(8085))->io("read 32767\n"), "read: 0",
    'read from buffer end');
is(stream('127.0.0.1:' . port(8085))->io("read 32768\n"), "read: undefined",
    'read out of buffer bounds');
is(stream('127.0.0.1:' . port(8085))->io("write 32767 1\n"), "written",
    'write to buffer end');
is(stream('127.0.0.1:' . port(8085))->io("read 32767\n"), "read: 1",
    'read from buffer end again');
is(stream('127.0.0.1:' . port(8084))->read(), '["buffer","test"]',
    'enumerate zones');
is(stream('127.0.0.1:' . port(8085))->io("readLock 0\n"), 'readLock: 2',
    'read with lock');
is(stream('127.0.0.1:' . port(8085))->io("writeLock 0 42\n"), "written",
    'write with lock');
is(stream('127.0.0.1:' . port(8085))->io("readLock 0\n"), 'readLock: 42',
    'read locked value');
is(stream('127.0.0.1:' . port(8081))->read(), 'init: 42',
    'verify locked write');

###############################################################################
