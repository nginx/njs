#!/usr/bin/perl

# (C) Dmitry Volyntsev
# (C) Nginx, Inc.

# Tests for http njs module, fetch method, dns support.

###############################################################################

use warnings;
use strict;

use Test::More;

use IO::Select;

BEGIN { use FindBin; chdir($FindBin::Bin); }

use lib 'lib';
use Test::Nginx;

###############################################################################

select STDERR; $| = 1;
select STDOUT; $| = 1;

plan(skip_all => '127.0.0.2 local address required')
	unless defined IO::Socket::INET->new( LocalAddr => '127.0.0.2' );

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

        location /njs {
            js_content test.njs;
        }

        location /dns {
            js_content test.dns;

            resolver   127.0.0.1:%%PORT_8981_UDP%%;
            resolver_timeout 1s;
        }
    }

    server {
        listen       127.0.0.1:8080;
        server_name  aaa;

        location /loc {
            js_content test.loc;
        }

        location /host {
            return 200 8080:$http_host;
        }
    }

    server {
        listen       127.0.0.1:8081;
        server_name  aaa;

        location /loc {
            js_content test.loc;
        }

        location /host {
            return 200 8081:$http_host;
        }
    }

    server {
        listen       127.0.0.1:8080;
        server_name  many;

        location /loc {
            js_content test.loc;
        }
    }
}

EOF

my $p0 = port(8080);
my $p1 = port(8081);

$t->write_file('test.js', <<EOF);
    function test_njs(r) {
        r.return(200, njs.version);
    }

    const p0 = $p0;
    const p1 = $p1;

    async function dns(r) {
        var port = r.args.port == undefined || r.args.port == 'p0' ? p0 : p1;
        var loc = r.args.loc == undefined ? 'loc' : r.args.loc;
        var url = `http://\${r.args.domain}:\${port}/\${loc}`;

        try {
            let reply = await ngx.fetch(url);
            let body = await reply.text();
            r.return(200, body);

        } catch (e) {
            r.return(501, e.message);
        }
    }

    function str(v) { return v ? v : ''};

    function loc(r) {
        var v = r.variables;
        var body = str(r.requestText);
        var foo = str(r.headersIn.foo);
        var bar = str(r.headersIn.bar);
        var c = r.headersIn.code ? Number(r.headersIn.code) : 200;
        r.return(c, `\${v.host}:\${v.request_method}:\${foo}:\${bar}:\${body}`);
    }

     export default {njs: test_njs, dns, loc};
EOF

$t->try_run('no njs.fetch');

$t->plan(5);

$t->run_daemon(\&dns_daemon, port(8981), $t);
$t->waitforfile($t->testdir . '/' . port(8981));

###############################################################################

like(http_get('/dns?domain=aaa'), qr/aaa:GET:::$/s, 'fetch dns aaa');
like(http_get('/dns?domain=many'), qr/many:GET:::$/s, 'fetch dns many');
like(http_get('/dns?domain=unknown'), qr/"unknown" could not be resolved/s,
	'fetch dns unknown');

TODO: {
local $TODO = 'not yet' unless has_version('0.8.5');

like(http_get('/dns?domain=aaa&port=p0&loc=host'), qr/8080:aaa:$p0$/s,
	'fetch aaa:8080');
like(http_get('/dns?domain=aaa&port=p1&loc=host'), qr/8081:aaa:$p1$/s,
	'fetch aaa:8081');
}

###############################################################################

sub has_version {
	my $need = shift;

	http_get('/njs') =~ /^([.0-9]+)$/m;

	my @v = split(/\./, $1);
	my ($n, $v);

	for $n (split(/\./, $need)) {
		$v = shift @v || 0;
		return 0 if $n > $v;
		return 1 if $v > $n;
	}

	return 1;
}

###############################################################################

sub reply_handler {
	my ($recv_data, $port, %extra) = @_;

	my (@name, @rdata);

	use constant NOERROR	=> 0;
	use constant FORMERR	=> 1;
	use constant SERVFAIL	=> 2;
	use constant NXDOMAIN	=> 3;

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

	if ($name eq 'aaa' && $type == A) {
		push @rdata, rd_addr($ttl, '127.0.0.1');

	} elsif ($name eq 'many' && $type == A) {
		push @rdata, rd_addr($ttl, '127.0.0.2');
		push @rdata, rd_addr($ttl, '127.0.0.1');
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
	my ($port, $t, %extra) = @_;

	my ($data, $recv_data);
	my $socket = IO::Socket::INET->new(
		LocalAddr => '127.0.0.1',
		LocalPort => $port,
		Proto => 'udp',
	)
		or die "Can't create listening socket: $!\n";

	my $sel = IO::Select->new($socket);

	local $SIG{PIPE} = 'IGNORE';

	# signal we are ready

	open my $fh, '>', $t->testdir() . '/' . $port;
	close $fh;

	while (my @ready = $sel->can_read) {
		foreach my $fh (@ready) {
			if ($socket == $fh) {
				$fh->recv($recv_data, 65536);
				$data = reply_handler($recv_data, $port);
				$fh->send($data);

			} else {
				$fh->recv($recv_data, 65536);
				unless (length $recv_data) {
					$sel->remove($fh);
					$fh->close;
					next;
				}

again:
				my $len = unpack("n", $recv_data);
				$data = substr $recv_data, 2, $len;
				$data = reply_handler($data, $port, tcp => 1);
				$data = pack("n", length $data) . $data;
				$fh->send($data);
				$recv_data = substr $recv_data, 2 + $len;
				goto again if length $recv_data;
			}
		}
	}
}

###############################################################################
