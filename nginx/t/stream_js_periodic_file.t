#!/usr/bin/perl

# (C) Dmitry Volyntsev
# (C) Nginx, Inc.

# Tests for stream njs module, js_periodic directive.

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

my $t = Test::Nginx->new()->has(qw/http rewrite stream/)
	->write_file_expand('nginx.conf', <<'EOF');

%%TEST_GLOBALS%%

daemon off;
worker_processes 4;

events {
}

worker_shutdown_timeout 100ms;

stream {
    %%TEST_GLOBALS_STREAM%%

    js_import test.js;

    server {
        listen       127.0.0.1:8081;
        js_periodic test.file interval=1s;

        js_preread test.test;

        proxy_pass 127.0.0.1:8090;
    }
}

EOF

$t->write_file('test.js', <<EOF);
    import fs from 'fs';

    async function file() {
        let fh = await fs.promises.open(ngx.conf_prefix + 'file', 'a+');

        await fh.write('abc');
        await fh.close();
    }

    function test(s) {
        s.on('upload', function (data) {
            if (data.length > 0) {
                switch (data) {
                case 'file':
                    let file_data = fs.readFileSync(ngx.conf_prefix + 'file')
                                                                    .toString();

                    if (file_data == 'abc') {
                        s.done();
                        return;
                    }

                    break;

                default:
                    throw new Error(`Unknown test "\${data}"`);
                }

                throw new Error(`Test "\${data}" failed`);
            }
        });
    }

    export default { file, test };
EOF

$t->run_daemon(\&stream_daemon, port(8090));
$t->try_run('no js_periodic with fs support');
$t->plan(2);
$t->waitforsocket('127.0.0.1:' . port(8090));

###############################################################################

select undef, undef, undef, 0.2;

is(stream('127.0.0.1:' . port(8081))->io('file'), 'file', 'file test');

$t->stop();

unlike($t->read_file('error.log'), qr/\[error\].*should not be seen/,
	'check for not discadred events');

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
