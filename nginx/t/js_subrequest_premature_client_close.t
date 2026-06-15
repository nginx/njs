#!/usr/bin/perl

# (C) Dmitry Volyntsev
# (C) F5, Inc.

# Tests for http njs module, r.subrequest() finalization safety.

###############################################################################

use warnings;
use strict;

use Test::More;

use Socket qw/ SOL_SOCKET SO_LINGER CRLF /;
use IO::Socket::INET;

BEGIN { use FindBin; chdir($FindBin::Bin); }

use lib 'lib';
use Test::Nginx;

###############################################################################

select STDERR; $| = 1;
select STDOUT; $| = 1;

my $t = Test::Nginx->new()->has(qw/http proxy/)
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

        location /sub {
            js_content test.sub;
        }

        location = /backend {
            internal;
            proxy_pass http://127.0.0.1:8081;
        }

        location /alive {
            return 200 alive;
        }
    }
}

EOF

$t->write_file('test.js', <<'EOF');
    function sub(r) {
        r.subrequest('/backend', { method: 'GET' })
        .then(reply => r.return(reply.status))
        .catch(e => r.return(502));
    }

    export default { sub };
EOF

$t->run_daemon(\&http_daemon, port(8081));
$t->try_run('no njs available')->plan(3);
$t->waitforsocket('127.0.0.1:' . port(8081));

###############################################################################

like(http_get('/sub'), qr/502/, 'subrequest upstream premature close');

like($t->read_file('error.log'), qr/upstream prematurely closed connection/,
	'upstream premature close logged');

reset_requests('/sub');

like(http_get('/alive'), qr/200 OK.*alive/s, 'worker alive');

###############################################################################

sub reset_requests {
	my ($uri) = @_;

	for (1 .. 50) {
		my $s = IO::Socket::INET->new(
			Proto => 'tcp',
			PeerAddr => '127.0.0.1:' . port(8080)
		)
			or next;

		$s->autoflush(1);
		$s->print('GET ' . $uri . ' HTTP/1.1' . CRLF
			. 'Host: localhost' . CRLF
			. 'Connection: close' . CRLF . CRLF);

		select undef, undef, undef, 0.002;
		setsockopt($s, SOL_SOCKET, SO_LINGER, pack('ii', 1, 0));
		close $s;
	}
}

sub http_daemon {
	my $port = shift;

	my $server = IO::Socket::INET->new(
		Proto => 'tcp',
		LocalAddr => '127.0.0.1:' . $port,
		Listen => 50,
		Reuse => 1
	)
		or die "Can't create listening socket: $!\n";

	local $SIG{PIPE} = 'IGNORE';

	while (my $client = $server->accept()) {
		$client->autoflush(1);

		while (<$client>) {
			last if (/^\x0d?\x0a?$/);
		}

		select undef, undef, undef, 0.05;

		close $client;
	}
}

###############################################################################
