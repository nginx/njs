#!/usr/bin/perl

# (C) Dmitry Volyntsev
# (C) F5, Inc.

# Tests for stream njs module, fetch method with forward proxy.

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

my $t = Test::Nginx->new()->has(qw/http stream stream_map/)
	->write_file_expand('nginx.conf', <<'EOF');

%%TEST_GLOBALS%%

daemon off;

events {
}

http {
    %%TEST_GLOBALS_HTTP%%

    js_import test.js;

    server {
        listen       127.0.0.1:%%PORT_8080%%;
        server_name  localhost;

        location = /test {
            js_content test.origin_endpoint;
        }
    }

    server {
        listen       127.0.0.1:%%PORT_8081%%;
        server_name  localhost;

        location = /test {
            js_content test.proxy_endpoint;
        }
    }
}

stream {
    %%TEST_GLOBALS_STREAM%%

    js_import test.js;

    server {
        listen      127.0.0.1:%%PORT_8091%%;
        js_fetch_proxy http://testuser:testpass@127.0.0.1:%%PORT_8081%%;
        js_filter   test.http_fetch;
        proxy_pass  127.0.0.1:%%PORT_8094%%;
    }

    server {
        listen      127.0.0.1:%%PORT_8092%%;

        set $proxy_url http://testuser:testpass@127.0.0.1:%%PORT_8081%%;
        js_fetch_proxy $proxy_url;
        js_filter   test.http_fetch;
        proxy_pass  127.0.0.1:%%PORT_8094%%;
    }

    server {
        listen      127.0.0.1:%%PORT_8093%%;

        set $proxy_url "";
        js_fetch_proxy $proxy_url;
        js_filter   test.http_fetch;
        proxy_pass  127.0.0.1:%%PORT_8094%%;
    }
}

EOF

my $p0 = port(8080);
my $p1 = port(8081);

$t->write_file('test.js', <<EOF);
    function origin_endpoint(r) {
        let proxy_auth = r.headersIn['Proxy-Authorization'] || '';

        if (proxy_auth) {
            r.return(500, 'ORIGIN:HAS-PROXY-AUTH');
            return;
        }

        r.return(200, 'ORIGIN:OK');
    }

    function proxy_endpoint(r) {
        let proxy_auth = r.headersIn['Proxy-Authorization'] || '';
        let expected = 'Basic dGVzdHVzZXI6dGVzdHBhc3M=';

        if (!proxy_auth) {
            r.return(500, 'PROXY:NO-AUTH');
            return;
        }

        if (proxy_auth !== expected) {
            r.return(407, 'PROXY:BAD-AUTH');
            return;
        }

        r.return(200, 'PROXY:' + proxy_auth);
    }

    function http_fetch(s) {
        var collect = '';

        s.on('upload', async function (data, flags) {
            collect += data;

            if (collect.length > 0) {
                s.off('upload');

                let reply = await ngx.fetch('http://127.0.0.1:$p0/test');
                let body = await reply.text();

                s.send(body, flags);
            }
        });
    }

    export default {origin_endpoint, proxy_endpoint, http_fetch};

EOF

$t->try_run('no js_fetch_proxy available')->plan(3);

$t->run_daemon(\&stream_daemon, port(8094));
$t->waitforsocket('127.0.0.1:' . port(8094));

###############################################################################

is(stream('127.0.0.1:' . port(8091))->io('TEST'),
	'PROXY:Basic dGVzdHVzZXI6dGVzdHBhc3M=', 'static proxy');
is(stream('127.0.0.1:' . port(8092))->io('TEST'),
	'PROXY:Basic dGVzdHVzZXI6dGVzdHBhc3M=', 'dynamic proxy');
is(stream('127.0.0.1:' . port(8093))->io('TEST'), 'ORIGIN:OK', 'no proxy');

###############################################################################

$t->stop();

###############################################################################

sub stream_daemon {
	my ($port) = @_;

	my $server = IO::Socket::INET->new(
		Proto => 'tcp',
		LocalAddr => "127.0.0.1:$port",
		Listen => 5,
		Reuse => 1
	) or die "Can't create listening socket: $!\n";

	local $SIG{PIPE} = 'IGNORE';

	while (my $client = $server->accept()) {
		$client->autoflush(1);

		log2c("(new connection $client)");

		$client->sysread(my $buffer, 65536);

		log2i("$client $buffer");

		$client->syswrite($buffer);

		log2o("$client $buffer");

		$client->close();
	}
}

sub log2i { Test::Nginx::log_core('|| <<', @_); }
sub log2o { Test::Nginx::log_core('|| >>', @_); }
sub log2c { Test::Nginx::log_core('||', @_); }

###############################################################################
