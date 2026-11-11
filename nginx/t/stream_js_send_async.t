#!/usr/bin/perl

# (C) Dmitry Volyntsev
# (C) F5, Inc.

# Tests for asynchronous s.send() calls in the stream js filter.  Output
# chain pointers belong to a filter call and must not be used after it.

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

my $t = Test::Nginx->new()->has(qw/stream/)
    ->write_file_expand('nginx.conf', <<'EOF');

%%TEST_GLOBALS%%

daemon off;

events {
}

stream {
    %%TEST_GLOBALS_STREAM%%

    js_import test.js;

    server {
        listen      127.0.0.1:8081;
        js_filter   test.send;
        proxy_pass  127.0.0.1:8090;
    }

    server {
        listen      127.0.0.1:8082;
        js_filter   test.send_options;
        proxy_pass  127.0.0.1:8090;
    }

    server {
        listen      127.0.0.1:8083;
        js_filter   test.send_direct;
        proxy_pass  127.0.0.1:8090;
    }
}

EOF

$t->write_file('test.js', <<'EOF');
    function later(s, fn) {
        s.on('upload', () => {
            s.off('upload');

            setTimeout(() => {
                try {
                    fn();

                } catch (e) {
                    s.sendDownstream(e instanceof TypeError ? 'TypeError'
                                                            : e.name);
                }
            }, 1);
        });
    }

    function send(s) {
        later(s, () => {
            s.send('late');
            s.sendDownstream('accepted');
        });
    }

    function send_options(s) {
        later(s, () => {
            s.send('late', {last: false});
            s.sendDownstream('accepted');
        });
    }

    function send_direct(s) {
        later(s, () => {
            s.sendDownstream('a');
            s.send('b', {from_upstream: true});
        });
    }

    export default {send, send_options, send_direct};
EOF

$t->run_daemon(\&stream_daemon, port(8090));
$t->try_run('no stream njs available')->plan(3);
$t->waitforsocket('127.0.0.1:' . port(8090));

###############################################################################

is(stream('127.0.0.1:' . port(8081))->io('go'), 'TypeError',
    'async send without direction');
is(stream('127.0.0.1:' . port(8082))->io('go'), 'TypeError',
    'async send with options without direction');
is(stream('127.0.0.1:' . port(8083))->io('go', read => 2), 'ab',
    'async send with direction');

$t->stop();

###############################################################################

sub stream_daemon {
    my $server = IO::Socket::INET->new(
        Proto => 'tcp',
        LocalAddr => '127.0.0.1:' . port(8090),
        Listen => 5,
        Reuse => 1
    )
        or die "Can't create listening socket: $!\n";

    local $SIG{PIPE} = 'IGNORE';

    while (my $client = $server->accept()) {
        $client->autoflush(1);
        $client->sysread(my $buffer, 65536);
        close $client;
    }
}

###############################################################################
