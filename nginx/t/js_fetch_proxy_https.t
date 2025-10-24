#!/usr/bin/perl

# (C) Dmitry Volyntsev
# (C) F5, Inc.

# Tests for http njs module, fetch method with HTTPS through forward proxy.

###############################################################################

use warnings;
use strict;

use Test::More;

use Socket qw/ CRLF SOCK_STREAM /;
use IO::Select;

BEGIN { use FindBin; chdir($FindBin::Bin); }

use lib 'lib';
use Test::Nginx;

###############################################################################

select STDERR; $| = 1;
select STDOUT; $| = 1;

my $t = Test::Nginx->new()->has(qw/http http_ssl/)
	->write_file_expand('nginx.conf', <<'EOF');

%%TEST_GLOBALS%%

daemon off;

events {
}

http {
    %%TEST_GLOBALS_HTTP%%

    js_import test.js;

    resolver   127.0.0.1:%%PORT_8981_UDP%%;
    resolver_timeout 1s;

    server {
        listen       127.0.0.1:8080;
        server_name  localhost;

        js_fetch_trusted_certificate myca.crt;

        location /engine {
            js_content test.engine;
        }

        location /https_via_proxy {
            js_fetch_proxy http://user:pass@forward.proxy.net:%%PORT_8082%%;
            js_content test.https_fetch;
        }

        location /https_via_broken_proxy {
            js_fetch_proxy http://user:pass@nonexistent.domain:%%PORT_8082%%;
            js_content test.https_fetch;
        }

        location /https_no_proxy {
            js_content test.https_fetch;
        }
    }

    server {
        listen       127.0.0.1:%%PORT_8083%% ssl;
        server_name  example.com;

        ssl_certificate example.com.chained.crt;
        ssl_certificate_key example.com.key;

        location = /test {
            js_content test.endpoint;
        }
    }
}

EOF

my $p2 = port(8082);
my $p3 = port(8083);

$t->write_file('test.js', <<EOF);
    function engine(r) {
        r.return(200, njs.engine);
    }

    function endpoint(r) {
        let s = `METHOD: \${r.method}\\n`;
        s += `URI: \${r.requestLine.split(" ")[1]}\\n`;
        s += 'BODY: ' + (r.requestText || '') + '\\n';

        r.return(200, s + 'ORIGIN');
    }

    async function https_fetch(r) {
        try {
            let domain = decodeURIComponent(r.args.domain);
            let method = r.method;
            let body = r.requestText;
            let reply = await ngx.fetch(`https://\${domain}:$p3/test`,
                                        {method, body});
            body = await reply.text();
            r.return(200, body);

        } catch (e) {
            r.return(500, e.message);
        }
    }

    export default {engine, endpoint, https_fetch};

EOF

my $d = $t->testdir();

$t->write_file('openssl.conf', <<EOF);
[ req ]
default_bits = 2048
encrypt_key = no
distinguished_name = req_distinguished_name
x509_extensions = myca_extensions
[ req_distinguished_name ]
[ myca_extensions ]
basicConstraints = critical,CA:TRUE
EOF

$t->write_file('myca.conf', <<EOF);
[ ca ]
default_ca = myca

[ myca ]
new_certs_dir = $d
database = $d/certindex
default_md = sha256
policy = myca_policy
serial = $d/certserial
default_days = 1
x509_extensions = myca_extensions

[ myca_policy ]
commonName = supplied

[ myca_extensions ]
basicConstraints = critical,CA:TRUE
EOF

system('openssl req -x509 -new '
	. "-config $d/openssl.conf -subj /CN=myca/ "
	. "-out $d/myca.crt -keyout $d/myca.key "
	. ">>$d/openssl.out 2>&1") == 0
	or die "Can't create self-signed certificate for CA: $!\n";

foreach my $name ('intermediate', 'example.com') {
	system("openssl req -new "
		. "-config $d/openssl.conf -subj /CN=$name/ "
		. "-out $d/$name.csr -keyout $d/$name.key "
		. ">>$d/openssl.out 2>&1") == 0
		or die "Can't create certificate signing req for $name: $!\n";
}

$t->write_file('certserial', '1000');
$t->write_file('certindex', '');

system("openssl ca -batch -config $d/myca.conf "
	. "-keyfile $d/myca.key -cert $d/myca.crt "
	. "-subj /CN=intermediate/ -in $d/intermediate.csr "
	. "-out $d/intermediate.crt "
	. ">>$d/openssl.out 2>&1") == 0
	or die "Can't sign certificate for intermediate: $!\n";

foreach my $name ('example.com') {
	system("openssl ca -batch -config $d/myca.conf "
		. "-keyfile $d/intermediate.key -cert $d/intermediate.crt "
		. "-subj /CN=$name/ -in $d/$name.csr -out $d/$name.crt "
		. ">>$d/openssl.out 2>&1") == 0
		or die "Can't sign certificate for $name $!\n";
	$t->write_file("$name.chained.crt", $t->read_file("$name.crt")
		. $t->read_file('intermediate.crt'));
}

$t->try_run('no js_fetch_proxy')->plan(8);

$t->run_daemon(\&https_proxy_daemon, $p2);
$t->run_daemon(\&dns_daemon, port(8981), $t);
$t->waitforsocket('127.0.0.1:' . $p2);
$t->waitforfile($t->testdir . '/' . port(8981));

###############################################################################

my $resp = http_get('/https_via_proxy?domain=example.com');
like($resp, qr/METHOD: GET/, 'https through proxy received GET method');
like($resp, qr/URI: \/test/, 'https through proxy received /test URI');
like($resp, qr/ORIGIN$/, 'https through proxy origin response');

$resp = http_post('/https_via_proxy?domain=example.com');
like($resp, qr/METHOD: POST/, 'https through proxy received POST method');
like($resp, qr/BODY: REQ-BODY/, 'https through proxy received request body');

like(http_get('/https_no_proxy?domain=example.com'), qr/ORIGIN/,
	'https without proxy');

like(http_get('/https_via_proxy?domain=nonexistent.dest.domain'),
	qr/connect failed/, 'https through proxy nonexistent.dest.domain');
like(http_get('/https_via_broken_proxy?domain=example.com'),
	qr/\"nonexistent.domain\" could not be res/, 'https through broken proxy');

###############################################################################

sub https_proxy_daemon {
	my ($port) = @_;

	my $server = IO::Socket::INET->new(
		Proto     => 'tcp',
		LocalAddr => "127.0.0.1:$port",
		Listen    => 128,
		Reuse     => 1
	) or die "Can't create listening socket: $!\n";

	local $SIG{PIPE} = 'IGNORE';

	my $s = {
		sel       => IO::Select->new($server),
		pending   => {},   # fd -> { sock, buf }
		conn      => {},   # client fd -> { client, origin }
		o2c       => {},   # origin fd -> client fd
	};

	while (1) {
		my @ready = $s->{sel}->can_read(1.0);
		for my $sock (@ready) {
			if ($sock == $server) {
				my $client = $server->accept();
				next unless $client;
				$client->autoflush(1);
				$s->{sel}->add($client);
				$s->{pending}->{ fileno($client) } = {
					sock       => $client,
					buf        => '',
				};

				next;
			}

			my $fd = fileno($sock);
			next unless defined $fd;

			if (exists $s->{o2c}->{$fd}) {
				my $buf;
				my $cfd = $s->{o2c}->{$fd};
				my $client = $s->{conn}->{$cfd}{client};

				my $n = sysread($sock, $buf, 4096);
				if (!defined($n) || $n == 0) {
					_cleanup($s, $client, $sock);
					next;
				}

				syswrite($client, $buf);
				next;
			}

			if (exists $s->{conn}->{$fd}) {
				my $buf;
				my $origin = $s->{conn}->{$fd}{origin};

				my $n = sysread($sock, $buf, 4096);
				if (!defined($n) || $n == 0) {
					_cleanup($s, $sock, $origin);
					next;
				}

				syswrite($origin, $buf);
				next;
			}

			if (exists $s->{pending}->{$fd}) {
				my $buf;
				my $p = $s->{pending}->{$fd};

				my $n = sysread($sock, $buf, 4096);
				if (!defined($n) || $n == 0) {
					$s->{sel}->remove($sock);
					delete $s->{pending}->{$fd};
					close $sock;
					next;
				}

				$p->{buf} .= $buf;

				if ($p->{buf} =~ /(\x0d\x0a)\1/s) {
					my $method = '', my $proxy_auth = '', my $target = '';

					my ($headers) = split(/\r\n\r\n/, $p->{buf}, 2);
					for my $line (split(/\r\n/, $headers)) {
						if ($method eq '' && $line =~ /^(\S+)\s+(\S+)\s+HTTP/i) {
							$method = $1;
							$target = $2;
							next;
						}

						if ($line =~ /^Proxy-Authorization:\s*(.+?)\s*$/i) {
							$proxy_auth = $1;
							next;
						}
					}

					if (uc($method) eq 'CONNECT'
						&& $proxy_auth =~ /^Basic\s+dXNlcjpwYXNz$/i)
					{
						print $sock "HTTP/1.1 200 established" . CRLF . CRLF;

						my ($host, $port) = split(/:/, $target);

						my $origin = IO::Socket::INET->new(
							PeerAddr => "127.0.0.1:$port",
							Proto    => 'tcp',
							Type     => SOCK_STREAM
						);

						if (!$origin) {
							$s->{sel}->remove($sock);
							delete $s->{pending}->{$fd};
							close $sock;
							next;
						}

						$origin->autoflush(1);
						$s->{sel}->add($origin);

						$s->{conn}->{$fd} = {
							client => $sock,
							origin => $origin,
						};

						$s->{o2c}->{ fileno($origin) } = $fd;
						delete $s->{pending}->{$fd};

					} else {
						print $sock
							"HTTP/1.1 407 Proxy Auth Required" . CRLF .
							"Proxy-Authenticate: Basic realm=\"proxy\"" . CRLF .
							"Content-Length: 0" . CRLF .
							"Connection: close" . CRLF . CRLF;
						$s->{sel}->remove($sock);
						delete $s->{pending}->{$fd};
						close $sock;
					}
				}

				next;
			}

			$s->{sel}->remove($sock);
			close $sock;
		}
	}
}

sub _cleanup {
	my ($s, $client, $origin) = @_;

	my $cfd = fileno($client);
	my $ofd = fileno($origin);

	delete $s->{o2c}->{$ofd} if defined $ofd;

	if (defined $cfd) {
		delete $s->{conn}->{$cfd};
		delete $s->{pending}->{$cfd} if exists $s->{pending}->{$cfd};
	}

	if ($client) {
		$s->{sel}->remove($client);
		close $client;
	}

	if ($origin) {
		$s->{sel}->remove($origin);
		close $origin;
	}
}

###############################################################################

sub reply_handler {
	my ($recv_data, $port, %extra) = @_;

	my (@name, @rdata);

	use constant NOERROR	=> 0;
	use constant A		=> 1;
	use constant IN		=> 1;

	# default values

	my ($hdr, $rcode, $ttl) = (0x8180, NOERROR, 3600);

	# decode name

	my ($len, $offset) = (undef, 12);
	while (1) {
		$len = unpack("\@$offset C", $recv_data);
		last if $len == 0;
		$offset++;
		push @name, unpack("\@$offset A$len", $recv_data);
		$offset += $len;
	}

	$offset -= 1;
	my ($id, $type, $class) = unpack("n x$offset n2", $recv_data);

	my $name = join('.', @name);

	if ($name eq 'example.com' || $name eq 'forward.proxy.net') {
		if ($type == A) {
			push @rdata, rd_addr($ttl, '127.0.0.1');
		}
	}

	$len = @name;
	pack("n6 (C/a*)$len x n2", $id, $hdr | $rcode, 1, scalar @rdata,
		0, 0, @name, $type, $class) . join('', @rdata);
}

sub rd_addr {
	my ($ttl, $addr) = @_;

	my $code = 'split(/\./, $addr)';

	return pack 'n3N', 0xc00c, A, IN, $ttl if $addr eq '';

	pack 'n3N nC4', 0xc00c, A, IN, $ttl, eval "scalar $code", eval($code);
}

sub dns_daemon {
	my ($port, $t) = @_;

	my ($data, $recv_data);
	my $socket = IO::Socket::INET->new(
		LocalAddr    => '127.0.0.1',
		LocalPort    => $port,
		Proto        => 'udp',
	)
		or die "Can't create listening socket: $!\n";

	local $SIG{PIPE} = 'IGNORE';

	# signal we are ready

	open my $fh, '>', $t->testdir() . '/' . $port;
	close $fh;

	while (1) {
		$socket->recv($recv_data, 65536);
		$data = reply_handler($recv_data, $port);
		$socket->send($data);
	}
}

###############################################################################

sub http_post {
	my ($url, %extra) = @_;

	my $p = "POST $url HTTP/1.0" . CRLF .
		"Host: localhost" . CRLF .
		"Content-Length: 8" . CRLF .
		CRLF .
		"REQ-BODY";

	return http($p, %extra);
}

###############################################################################
