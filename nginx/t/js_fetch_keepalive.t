#!/usr/bin/perl

# (C) Dmitry Volyntsev
# (C) F5, Inc.

# Tests for http njs module, fetch method keepalive.

###############################################################################

use warnings;
use strict;

use Test::More;
use IO::Socket::INET;

use Socket qw/ CRLF /;

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

        location /keepalive {
            js_fetch_keepalive 4;
            js_fetch_keepalive_requests 100;
            js_fetch_keepalive_time 60s;
            js_fetch_keepalive_timeout 60s;
            js_content test.keepalive;
        }

        location /keepalive_simultaneous {
            js_fetch_keepalive 4;
            js_content test.keepalive_simultaneous;
        }

        location /keepalive_requests {
            js_fetch_keepalive 4;
            js_fetch_keepalive_requests 2;
            js_content test.keepalive;
        }

        location /keepalive_time {
            js_fetch_keepalive 4;
            js_fetch_keepalive_time 100ms;
            js_content test.keepalive;
        }

        location /keepalive_timeout {
            js_fetch_keepalive 4;
            js_fetch_keepalive_timeout 100ms;
            js_content test.keepalive;
        }

        location /no_keepalive {
            js_fetch_keepalive 0;
            js_content test.keepalive;
        }
    }

    server {
        listen       127.0.0.1:8081;
        keepalive_requests 100;
        keepalive_timeout 60s;

        location /count {
            add_header Connection-ID $connection_requests;
            return 200 $connection_requests;
        }

        location /count_close {
            add_header Connection close;
            add_header Connection-ID $connection_requests;
            return 200 $connection_requests;
        }

        location /count_close_mixed {
            add_header cOnNeCtiOn ClOsE;
            add_header Connection-ID $connection_requests;
            return 200 $connection_requests;
        }
    }
}

EOF

my $p1 = port(8081);
my $p2 = port(8082);

$t->write_file('test.js', <<EOF);
    function engine(r) {
        r.return(200, njs.engine);
    }

    function sleep(milliseconds) {
        return new Promise(resolve => setTimeout(resolve, milliseconds));
    }

    async function keepalive(r) {
        const path = r.args.path;
        let port = $p1;
        if (r.args.port) {
            port = r.args.port;
        }

        let responses = [];
        for (let i = 0; i < 3; i++) {
            let resp = await ngx.fetch(`http://127.0.0.1:\${port}/\${path}`)
                                 .then(resp => resp.text())
                                 .catch(err => err.message);
            responses.push(resp.trim());

            if (r.args.sleep) {
                await sleep(Number(r.args.sleep));
            }
        }

        r.return(200, responses.toString());
    }

    async function keepalive_simultaneous(r) {
        let promises = [];
        for (let i = 0; i < Number(r.args.n); i++) {
            promises.push(ngx.fetch('http://127.0.0.1:$p1/count'));
        }

        let results = await Promise.all(promises);
        let bodies = await Promise.all(results.map(r => r.text()));
        let responses = bodies.map(b => parseInt(b.trim()));

        r.return(200, JSON.stringify(responses));
    }

    export default {engine, keepalive, keepalive_simultaneous};
EOF

$t->try_run('no js_fetch_keepalive');

$t->run_daemon(\&http_daemon, $p2);
$t->waitforsocket('127.0.0.1:' . $p2);

$t->plan(16);

###############################################################################

like(http_get('/no_keepalive?path=count'), qr/1,1,1/,
	'no keepalive connections');
like(http_get('/keepalive?path=count_close'), qr/1,1,1/,
	'upstream Connection: close (HTTP/1.1)');
like(http_get('/keepalive?path=count_close_mixed'), qr/1,1,1/,
	'upstream Connection: close, mixed-case (HTTP/1.1)');
like(http_get('/keepalive?path=count'), qr/1,2,3/,
	'keepalive reuses connection');
like(http_get('/keepalive?path=count'), qr/4,5,6/,
	'keepalive reuses connection across requests');
like(http_get('/keepalive_simultaneous?n=8'), qr/1,1,1,1,1,1,1,1/,
	'keepalive simultaneous requests');
like(http_get('/keepalive_simultaneous?n=8'), qr/2,2,2,2,1,1,1,1/,
	'keepalive simultaneous requests reused connections');
like(http_get('/keepalive_requests?path=count'), qr/1,2,1/,
	'keepalive with limited requests per connection');

like(http_get('/keepalive_time?path=count'), qr/1,2,3/,
	'keepalive with time limit, first round');

select undef, undef, undef, 0.15;

like(http_get('/keepalive_time?path=count'), qr/4,1,2/,
	'keepalive with time limit, second round');

like(http_get('/keepalive_timeout?path=count'), qr/1,2,3/,
	'keepalive with timeout limit, first round');

select undef, undef, undef, 0.15;

like(http_get('/keepalive_timeout?path=count'), qr/1,2,3/,
	'keepalive with timeout limit, second round');

like(http_get("/keepalive?path=broken_keepalive&port=$p2&sleep=1"), qr/1,1,1/,
	'upstream broken keepalive (connection closed by upstream)');
like(http_get("/keepalive?path=http10&port=$p2"), qr/1,1,1/,
	'upstream HTTP/1.0 (no keepalive)');
like(http_get("/keepalive?path=count&port=$p2&sleep=1"), qr/1,2,3/,
	'normal keepalive');
like(http_get("/keepalive?path=assumed_keepalive&port=$p2&sleep=1"), qr/4,5,6/,
	'assumed keepalive');

###############################################################################

sub http_daemon {
	my $port = shift;

	my $server = IO::Socket::INET->new(
		Proto => 'tcp',
		LocalAddr => '127.0.0.1:' . $port,
		Listen => 5,
		Reuse => 1
	) or die "Can't create listening socket: $!\n";

	my $ccount = 0;
	my $rcount = 0;

	# dumb server which is able to keep connections alive

	while (my $client = $server->accept()) {
		Test::Nginx::log_core('||',
			"connection from " . $client->peerhost());
		$client->autoflush(1);
		$ccount++;
		$rcount = 0;

		while (1) {
			my $headers = '';
			my $uri = '';

			while (<$client>) {
				Test::Nginx::log_core('||', $_);
				$headers .= $_;
				last if (/^\x0d?\x0a?$/);
			}

			last if $headers eq '';
			$rcount++;

			$uri = $1 if $headers =~ /^\S+\s+([^ ]+)\s+HTTP/i;
			my $body = $rcount;

			if ($uri eq '/broken_keepalive') {
				print $client
					"HTTP/1.1 200 OK" . CRLF .
					"Content-Length: " . length($body) . CRLF .
					"Connection: keep-alive" . CRLF . CRLF .
					$body;

				last;

			} elsif ($uri eq '/assumed_keepalive') {
				print $client
					"HTTP/1.1 200 OK" . CRLF .
					"Content-Length: " . length($body) . CRLF . CRLF .
					$body;

			} elsif ($uri eq '/count') {
				print $client
					"HTTP/1.1 200 OK" . CRLF .
					"Content-Length: " . length($body) . CRLF .
					"Connection: keep-alive" . CRLF . CRLF .
					$body;

			} elsif ($uri eq '/http10') {
				print $client
					"HTTP/1.0 200 OK" . CRLF .
					"Content-Length: " . length($body) . CRLF . CRLF .
					$body;
			}
		}

		close $client;
	}
}

###############################################################################
