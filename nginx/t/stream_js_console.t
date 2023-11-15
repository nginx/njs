#!/usr/bin/perl

# (C) Dmitry Volyntsev
# (C) Nginx, Inc.

# Tests for stream njs module, console object.

###############################################################################

use warnings;
use strict;

use Test::More;
use Socket qw/ CRLF /;

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
        listen       127.0.0.1:8080;

        js_preread test.log;

        proxy_pass 127.0.0.1:8090;
    }

    server {
        listen       127.0.0.1:8081;

        js_preread test.timer;

        proxy_pass 127.0.0.1:8090;
    }
}

EOF

$t->write_file('test.js', <<EOF);
    function log(s) {
        s.on('upload', function (data) {
            if (data.length > 0) {
                s.off('upload');
                data = Buffer.from(data, 'base64');
                const object = JSON.parse(data);
                console.log(object);
                s.allow();
            }
        });
    }

    function timer(s) {
        s.on('upload', function (data) {
            if (data.length > 0) {
                s.off('upload');
                console.time('foo');
                setTimeout(function() {
                    console.timeEnd('foo');
                    s.allow();
                }, 7);
            }
        });
    }

    export default { log, timer };
EOF

$t->run_daemon(\&stream_daemon, port(8090));
$t->try_run('no njs console')->plan(4);
$t->waitforsocket('127.0.0.1:' . port(8090));

###############################################################################

is(stream('127.0.0.1:' . port(8080))->io('eyJhIjpbIkIiLCJDIl19'),
	'eyJhIjpbIkIiLCJDIl19', 'log test');
is(stream('127.0.0.1:' . port(8081))->io('timer'), 'timer', 'timer test');

$t->stop();

like($t->read_file('error.log'), qr/\[info\].*js: \{a:\['B','C'\]\}/,
	'console.log with object');
like($t->read_file('error.log'), qr/\[info\].*js: foo: \d+\.\d\d\d\d\d\dms/,
	'console.time foo');

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

		log2c("(new connection $client)");

		$client->sysread(my $buffer, 65536) or next;

		log2i("$client $buffer");

		log2o("$client $buffer");

		$client->syswrite($buffer);

		close $client;
	}
}

sub log2i { Test::Nginx::log_core('|| <<', @_); }
sub log2o { Test::Nginx::log_core('|| >>', @_); }
sub log2c { Test::Nginx::log_core('||', @_); }

###############################################################################
