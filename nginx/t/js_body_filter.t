#!/usr/bin/perl

# (C) Dmitry Volyntsev
# (C) Nginx, Inc.

# Tests for http njs module, body filter.

###############################################################################

use warnings;
use strict;

use Test::More;

use Socket qw/ CRLF IPPROTO_TCP TCP_NODELAY /;

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

        location /njs {
            js_content test.njs;
        }

        location /append {
            js_header_filter test.clear_content_length;
            js_body_filter test.append;
            proxy_pass http://127.0.0.1:8081/source;
        }

        location /buffer_type {
            js_body_filter test.buffer_type buffer_type=buffer;
            proxy_pass http://127.0.0.1:8081/source;
        }

        location /buffer_type_nonutf8 {
            js_body_filter test.buffer_type buffer_type=buffer;
            proxy_pass http://127.0.0.1:8081/nonutf8_source;
        }

        location /forward {
            js_body_filter test.forward buffer_type=string;
            proxy_pass http://127.0.0.1:8081/source;
        }

        location /filter {
            proxy_buffering off;
            js_header_filter test.clear_content_length;
            js_body_filter test.filter;
            proxy_pass http://127.0.0.1:8081/source;
        }

        location /prepend {
            js_header_filter test.clear_content_length;
            js_body_filter test.prepend;
            proxy_pass http://127.0.0.1:8081/source;
        }
    }
}

EOF

$t->write_file('test.js', <<EOF);
    function test_njs(r) {
        r.return(200, njs.version);
    }

    function append(r, data, flags) {
        r.sendBuffer(data, {last:false});

        if (flags.last) {
            r.sendBuffer("XXX", flags);
        }
    }

    function clear_content_length(r) {
        delete r.headersOut['Content-Length'];
    }

    var collect = Buffer.from([]);
    function buffer_type(r, data, flags) {
        collect = Buffer.concat([collect, data]);

        if (flags.last) {
            r.sendBuffer(collect, flags);
        }
    }

    function filter(r, data, flags) {
        if (flags.last || data.length >= Number(r.args.len)) {
            r.sendBuffer(`\${data}#`, flags);

            if (r.args.dup && !flags.last) {
                r.sendBuffer(data, flags);
            }
        }
    }

    function forward(r, data, flags) {
        r.sendBuffer(data, flags);
    }

    function prepend(r, data, flags) {
        r.sendBuffer("XXX");
        r.sendBuffer(data, flags);
        r.done();
    }

    export default {njs: test_njs, append, buffer_type, filter, forward,
                    prepend, clear_content_length};

EOF

$t->try_run('no njs body filter')->plan(8);

$t->run_daemon(\&http_daemon, port(8081));
$t->waitforsocket('127.0.0.1:' . port(8081));

###############################################################################

like(http_get('/append'), qr/AAABBCDDDDXXX$/, 'append');
unlike(http_get('/append'), qr/Content-Length/, 'append no content-length');
like(http_get('/buffer_type'), qr/AAABBCDDDD$/, 'buffer type');
like(http_get('/buffer_type_nonutf8'), qr/\xaa\xaa\xbb\xcc\xdd\xdd$/,
	'buffer type nonutf8');
like(http_get('/forward'), qr/AAABBCDDDD$/, 'forward');
like(http_get('/filter?len=3'), qr/AAA#DDDD##$/, 'filter 3');
like(http_get('/filter?len=2&dup=1'), qr/AAA#AAABB#BBDDDD#DDDD#$/,
	'filter 2 dup');
like(http_get('/prepend'), qr/XXXAAABBCDDDD$/, 'prepend');

###############################################################################

sub send_chunked {
	my ($client, @chunks) = @_;

	print $client
		"HTTP/1.1 200 OK" . CRLF .
		"Transfer-Encoding: chunked" . CRLF .
		"Connection: close" . CRLF .
		CRLF;

	for my $chunk (@chunks) {
		printf $client "%x\r\n%s\r\n", length($chunk), $chunk;
	}

	print $client "0\r\n\r\n";
}

sub http_daemon {
	my $port = shift;

	my $server = IO::Socket::INET->new(
		Proto => 'tcp',
		LocalAddr => '127.0.0.1:' . $port,
		Listen => 5,
		Reuse => 1
	) or die "Can't create listening socket: $!\n";

	local $SIG{PIPE} = 'IGNORE';

	while (my $client = $server->accept()) {
		$client->autoflush(1);

		setsockopt($client, IPPROTO_TCP, TCP_NODELAY, 1)
			or die "Can't set TCP_NODELAY: $!\n";

		my $headers = '';
		my $uri = '';

		while (<$client>) {
			$headers .= $_;
			last if (/^\x0d?\x0a?$/);
		}

		$uri = $1 if $headers =~ /^\S+\s+([^ ]+)\s+HTTP/i;
		$uri =~ s/\?.*//;

		log2c("(new connection $client $uri)");

		if ($uri eq '/source') {
			send_chunked($client, "AAA", "BB", "C", "DDDD");

		} elsif ($uri eq '/nonutf8_source') {
			send_chunked($client, "\xaa\xaa", "\xbb", "\xcc", "\xdd\xdd");

		} else {
			print $client
				"HTTP/1.1 404 Not Found" . CRLF .
				"Connection: close" . CRLF .
				CRLF;
		}

		$client->close();
	}
}

sub log2c { Test::Nginx::log_core('||', @_); }

###############################################################################
