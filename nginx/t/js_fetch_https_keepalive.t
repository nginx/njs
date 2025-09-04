#!/usr/bin/perl

# (C) Dmitry Volyntsev
# (C) F5, Inc.

# Tests for http njs module, fetch method, https keepalive support.

###############################################################################

use warnings;
use strict;

use Test::More;

BEGIN { use FindBin; chdir($FindBin::Bin); }

use lib 'lib';
use Test::Nginx;

###############################################################################

select STDERR; $| = 1;
select STDOUT; $| = 1;

my $t = Test::Nginx->new()->has(qw/http http_ssl rewrite/)
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

        resolver   127.0.0.1:%%PORT_8981_UDP%%;
        resolver_timeout 1s;

        location /njs {
            js_content test.njs;
        }

        location /https {
            js_content test.https;

            js_fetch_keepalive 4;
            js_fetch_ciphers HIGH:!aNull:!MD5;
            js_fetch_protocols TLSv1.1 TLSv1.2;
            js_fetch_trusted_certificate myca.crt;
        }

        location /sni_isolation {
            js_content test.sni_isolation;

            js_fetch_keepalive 4;
            js_fetch_ciphers HIGH:!aNull:!MD5;
            js_fetch_protocols TLSv1.1 TLSv1.2;
            js_fetch_trusted_certificate myca.crt;
        }

        location /plain_vs_https_isolation {
            js_content test.plain_vs_https_isolation;

            js_fetch_keepalive 4;
            js_fetch_ciphers HIGH:!aNull:!MD5;
            js_fetch_protocols TLSv1.1 TLSv1.2;
            js_fetch_trusted_certificate myca.crt;
        }
    }

    server {
        listen       127.0.0.1:8081 ssl;
        server_name  ka.example.com;

        keepalive_requests 100;

        ssl_certificate ka.example.com.chained.crt;
        ssl_certificate_key ka.example.com.key;

        location /loc {
            return 200 CONN:$connection_requests;
        }
    }

    server {
        listen       127.0.0.1:8081 ssl;
        server_name  1.example.com;

        ssl_certificate 1.example.com.chained.crt;
        ssl_certificate_key 1.example.com.key;

        location /loc {
            return 200 "You are at 1.example.com.";
        }
    }

    server {
        listen       127.0.0.1:8082;
        server_name  plain.example.com;

        keepalive_requests 100;

        location /loc {
            return 200 PLAIN:$connection_requests;
        }
    }
}

EOF

my $p1 = port(8081);
my $p2 = port(8082);

$t->write_file('test.js', <<EOF);
    function test_njs(r) {
        r.return(200, njs.version);
    }

    function https(r) {
        var url = `https://\${r.args.domain}:$p1/loc`;
        var opt = {};

        if (r.args.verify != null && r.args.verify == "false") {
            opt.verify = false;
        }

        ngx.fetch(url, opt)
        .then(reply => reply.text())
        .then(body => r.return(200, body))
        .catch(e => r.return(501, e.message))
    }

    async function sni_isolation(r) {
        try {
            let resp = await ngx.fetch(`https://ka.example.com:$p1/loc`);
            let body1 = await resp.text();

            resp = await ngx.fetch(`https://1.example.com:$p1/loc`);
            let body2 = await resp.text();

            resp = await ngx.fetch(`https://ka.example.com:$p1/loc`);
            let body3 = await resp.text();

            r.return(200, `\${body1}|\${body2}|\${body3}`);

        } catch (e) {
            r.return(501, e.message);
        }
    }

    async function plain_vs_https_isolation(r) {
        try {
            let resp = await ngx.fetch(`https://ka.example.com:$p1/loc`);
            let body1 = await resp.text();

            resp = await ngx.fetch(`http://plain.example.com:$p2/loc`);
            let body2 = await resp.text();

            resp = await ngx.fetch(`https://ka.example.com:$p1/loc`);
            let body3 = await resp.text();

            r.return(200, `\${body1}|\${body2}|\${body3}`);

        } catch (e) {
            r.return(501, e.message);
        }
    }

    export default {njs: test_njs, https, sni_isolation,
                    plain_vs_https_isolation};
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

foreach my $name ('intermediate', '1.example.com', 'ka.example.com') {
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

foreach my $name ('1.example.com', 'ka.example.com') {
	system("openssl ca -batch -config $d/myca.conf "
		. "-keyfile $d/intermediate.key -cert $d/intermediate.crt "
		. "-subj /CN=$name/ -in $d/$name.csr -out $d/$name.crt "
		. ">>$d/openssl.out 2>&1") == 0
		or die "Can't sign certificate for $name $!\n";
	$t->write_file("$name.chained.crt", $t->read_file("$name.crt")
		. $t->read_file('intermediate.crt'));
}

$t->try_run('no njs.fetch');

$t->plan(5);

$t->run_daemon(\&dns_daemon, port(8981), $t);
$t->waitforfile($t->testdir . '/' . port(8981));

###############################################################################

like(http_get('/https?domain=localhost'),
	qr/connect failed/s, 'fetch https wrong CN certificate');
like(http_get('/https?domain=ka.example.com'),
	qr/CONN:1$/s, 'fetch https keepalive');
like(http_get('/https?domain=ka.example.com'),
	qr/CONN:2$/s, 'fetch https keepalive reused');
like(http_get('/sni_isolation'),
	qr/CONN:1\|You are at 1\.example\.com\.\|CONN:2$/s,
	'fetch https keepalive SNI isolation');
like(http_get('/plain_vs_https_isolation'),
	qr/CONN:1\|PLAIN:1\|CONN:2$/s,
	'fetch https->plain->https keepalive isolation');

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

	if ($type == A) {
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
