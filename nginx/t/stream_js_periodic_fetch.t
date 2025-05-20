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

    js_shared_dict_zone zone=strings:32k;

    server {
        listen       127.0.0.1:8081;

        js_periodic test.fetch interval=40ms;
        js_periodic test.multiple_fetches interval=1s;

        js_periodic test.fetch_exception interval=1s;

        js_preread test.test;

        proxy_pass 127.0.0.1:8090;
    }
}

http {
    %%TEST_GLOBALS_HTTP%%

    js_import test.js;

    server {
        listen       127.0.0.1:8080;
        server_name  localhost;

        location /fetch_ok {
            return 200 'ok';
        }

        location /fetch_foo {
            return 200 'foo';
        }
    }
}

EOF

my $p1 = port(8080);

$t->write_file('test.js', <<EOF);
    async function fetch() {
        let reply = await ngx.fetch('http://127.0.0.1:$p1/fetch_ok');
        let body = await reply.text();

        let v = ngx.shared.strings.get('fetch') || '';
        ngx.shared.strings.set('fetch', v + body);
    }

    async function fetch_exception() {
        let reply = await ngx.fetch('garbage');
     }

    async function multiple_fetches() {
        let reply = await ngx.fetch('http://127.0.0.1:$p1/fetch_ok');
        let reply2 = await ngx.fetch('http://127.0.0.1:$p1/fetch_foo');
        let body = await reply.text();
        let body2 = await reply2.text();

        ngx.shared.strings.set('multiple_fetches', body + '\@' + body2);
    }

    function test(s) {
        s.on('upload', function (data) {
            if (data.length > 0) {
                switch (data) {
                case 'fetch':
                    if (ngx.shared.strings.get('fetch').startsWith('okok')) {
                        s.done();
                        return;
                    }

                    break;

                case 'multiple_fetches':
                    if (ngx.shared.strings.get('multiple_fetches')
                        .startsWith('ok\@foo'))
                    {
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

    export default { fetch, fetch_exception, test, multiple_fetches };
EOF

$t->run_daemon(\&stream_daemon, port(8090));
$t->try_run('no js_periodic with fetch');
$t->plan(3);
$t->waitforsocket('127.0.0.1:' . port(8090));

###############################################################################

select undef, undef, undef, 0.2;

is(stream('127.0.0.1:' . port(8081))->io('fetch'), 'fetch', 'fetch test');
is(stream('127.0.0.1:' . port(8081))->io('multiple_fetches'),
	'multiple_fetches', 'muliple fetches test');

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
