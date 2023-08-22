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

my $t = Test::Nginx->new()->has(qw/http stream/)
	->write_file_expand('nginx.conf', <<'EOF');

%%TEST_GLOBALS%%

daemon off;

events {
}

worker_shutdown_timeout 100ms;

stream {
    %%TEST_GLOBALS_STREAM%%

    js_import test.js;

    js_shared_dict_zone zone=nums:32k type=number;
    js_shared_dict_zone zone=strings:32k;

    server {
        listen       127.0.0.1:8080;

        js_periodic test.tick interval=30ms jitter=1ms;
        js_periodic test.timer interval=1s;
        js_periodic test.overrun interval=30ms;
        js_periodic test.file interval=1s;
        js_periodic test.fetch interval=40ms;
        js_periodic test.multiple_fetches interval=1s;

        js_periodic test.fetch_exception interval=1s;
        js_periodic test.tick_exception interval=1s;
        js_periodic test.timer_exception interval=1s;
        js_periodic test.timeout_exception interval=30ms;

        js_preread test.test;

        proxy_pass 127.0.0.1:8090;
    }
}

http {
    %%TEST_GLOBALS_HTTP%%

    server {
        listen       127.0.0.1:8081;
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

my $p1 = port(8081);

$t->write_file('test.js', <<EOF);
    import fs from 'fs';

    async function fetch() {
        if (ngx.worker_id != 0) {
            return;
        }

        let reply = await ngx.fetch('http://127.0.0.1:$p1/fetch_ok');
        let body = await reply.text();

        let v = ngx.shared.strings.get('fetch') || '';
        ngx.shared.strings.set('fetch', v + body);
    }

    async function fetch_exception() {
        if (ngx.worker_id != 0) {
            return;
        }

        let reply = await ngx.fetch('garbage');
     }

    async function multiple_fetches() {
        if (ngx.worker_id != 0) {
            return;
        }

        let reply = await ngx.fetch('http://127.0.0.1:$p1/fetch_ok');
        let reply2 = await ngx.fetch('http://127.0.0.1:$p1/fetch_foo');
        let body = await reply.text();
        let body2 = await reply2.text();

        ngx.shared.strings.set('multiple_fetches', body + '\@' + body2);
    }

    async function file() {
        if (ngx.worker_id != 0) {
            return;
        }

        let fh = await fs.promises.open(ngx.conf_prefix + 'file', 'a+');

        await fh.write('abc');
        await fh.close();
    }

    async function overrun() {
        if (ngx.worker_id != 0) {
            return;
        }

        setTimeout(() => {}, 100000);
    }

    function tick() {
        if (ngx.worker_id != 0) {
            return;
        }

        ngx.shared.nums.incr('tick', 1);
    }

    function tick_exception() {
        if (ngx.worker_id != 0) {
            return;
        }

        throw new Error("EXCEPTION");
    }

    function timer() {
        if (ngx.worker_id != 0) {
            return;
        }

        setTimeout(() => {ngx.shared.nums.set('timer', 1)}, 10);
    }

    function timer_exception() {
        if (ngx.worker_id != 0) {
            return;
        }

        setTimeout(() => {ngx.log(ngx.ERR, 'should not be seen')}, 10);
        throw new Error("EXCEPTION");
    }

    function timeout_exception() {
        if (ngx.worker_id != 0) {
            return;
        }

        setTimeout(() => {
            var v = ngx.shared.nums.get('timeout_exception') || 0;

            if (v == 0) {
                ngx.shared.nums.set('timeout_exception', 1);
                throw new Error("EXCEPTION");
                return;
            }

            ngx.shared.nums.incr('timeout_exception', 1);
        }, 1);
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

                case 'file':
                    let file_data = fs.readFileSync(ngx.conf_prefix + 'file')
                                                                    .toString();

                    if (file_data == 'abc') {
                        s.done();
                        return;
                    }

                    break;

                case 'tick':
                    if (ngx.shared.nums.get('tick') >= 3) {
                        s.done();
                        return;
                    }

                    break;

                case 'timeout_exception':
                    if (ngx.shared.nums.get('timeout_exception') >= 2) {
                        s.done();
                        return;
                    }

                    break;

                case 'timer':
                    if (ngx.shared.nums.get('timer') == 1) {
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

    export default { fetch, fetch_exception, multiple_fetches, file, overrun,
                     test, tick, tick_exception, timer, timer_exception,
                     timeout_exception };
EOF

$t->run_daemon(\&stream_daemon, port(8090));
$t->try_run('no js_periodic')->plan(7);
$t->waitforsocket('127.0.0.1:' . port(8090));

###############################################################################

select undef, undef, undef, 0.1;

is(stream('127.0.0.1:' . port(8080))->io('tick'), 'tick', '3x tick test');
is(stream('127.0.0.1:' . port(8080))->io('timer'), 'timer', 'timer test');
is(stream('127.0.0.1:' . port(8080))->io('file'), 'file', 'file test');
is(stream('127.0.0.1:' . port(8080))->io('fetch'), 'fetch', 'fetch test');
is(stream('127.0.0.1:' . port(8080))->io('multiple_fetches'),
	'multiple_fetches', 'muliple fetches test');
is(stream('127.0.0.1:' . port(8080))->io('timeout_exception'),
	'timeout_exception', 'timeout exception test');

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
