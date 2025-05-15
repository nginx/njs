#!/usr/bin/perl

# (C) Dmitry Volyntsev
# (C) Nginx, Inc.

# Tests for js_shared_dict_zone directive, state= parameter.

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

    js_shared_dict_zone zone=foo:32k state=foo.json;

    server {
        listen      127.0.0.1:8081;
        js_preread  test.preread_verify;
        proxy_pass  127.0.0.1:8090;
    }
}

EOF

$t->write_file('foo.json', <<EOF);
{"QZ":{"value":"1"},"QQ":{"value":"1"}}
EOF

$t->write_file('test.js', <<EOF);
    function preread_verify(s) {
        var collect = Buffer.from([]);

        s.on('upstream', function (data, flags) {
            collect = Buffer.concat([collect, data]);

            if (collect.length >= 4 && collect.readUInt16BE(0) == 0xabcd) {
                let id = collect.slice(2,4);

                ngx.shared.foo.get(id) ? s.done(): s.deny();

            } else if (collect.length) {
                s.deny();
            }
        });
    }

    export default { preread_verify };

EOF

$t->try_run('no js_shared_dict_zone with state=');

$t->plan(2);

$t->run_daemon(\&stream_daemon, port(8090));
$t->waitforsocket('127.0.0.1:' . port(8090));

###############################################################################

is(stream('127.0.0.1:' . port(8081))->io("\xAB\xCDQY##"), "",
	'access failed, QY is not in the shared dict');
is(stream('127.0.0.1:' . port(8081))->io("\xAB\xCDQZ##"), "\xAB\xCDQZ##",
	'access granted');

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

sub get {
	my ($url, %extra) = @_;

	my $s = IO::Socket::INET->new(
		Proto => 'tcp',
		PeerAddr => '127.0.0.1:' . port(8082)
	) or die "Can't connect to nginx: $!\n";

	return http_get($url, socket => $s);
}

###############################################################################
