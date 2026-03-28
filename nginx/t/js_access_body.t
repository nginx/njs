#!/usr/bin/perl

# (C) Dmitry Volyntsev
# (C) F5, Inc.

# Tests for http njs module, js_access body reading methods.

###############################################################################

use warnings;
use strict;

use Test::More;

use Socket qw/ CRLF /;

BEGIN { use FindBin; chdir($FindBin::Bin); }

use lib 'lib';
use Test::Nginx qw/ :DEFAULT http_end /;

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

    js_var $foo;

    server {
        listen       127.0.0.1:8080;
        server_name  localhost;

        location /text {
            js_access test.read_text;
            js_content test.content;
        }

        location /buffer {
            js_access test.read_buffer;
            js_content test.content;
        }

        location /text_twice {
            js_access test.read_text_twice;
            js_content test.content;
        }

        location /buffer_twice {
            js_access test.read_buffer_twice;
            js_content test.content;
        }

        location /concurrent_text_buffer {
            js_access test.read_concurrent_text_buffer;
            js_content test.content;
        }

        location /text_then_buffer {
            js_access test.read_text_then_buffer;
            js_content test.content;
        }

        location /json {
            js_access test.read_json;
            js_content test.content;
        }

        location /json_invalid {
            js_access test.read_json_invalid;
            js_content test.content;
        }

        location /empty {
            js_access test.read_text;
            js_content test.content;
        }

        location /big {
            client_body_buffer_size 64k;
            js_access test.read_text_length;
            js_content test.content;
        }

        location /big_4k {
            client_body_buffer_size 4k;
            js_access test.read_text_length;
            js_content test.content;
        }

        location /slow {
            js_access test.read_text;
            js_content test.content;
        }

        location /chunked {
            js_access test.read_text;
            js_content test.content;
        }

        location /text_timeout {
            js_access test.read_text_timeout;
            js_content test.content;
        }

        location /access_content_async {
            js_access test.read_text_timeout;
            js_content test.content_async;
        }

        location /content_text {
            js_content test.content_text;
        }

        location /proxy {
            js_access test.read_text;
            proxy_pass http://127.0.0.1:%%PORT_8081%%;
        }

        location /in_file {
            client_body_in_file_only on;
            js_access test.read_text;
            js_content test.content;
        }

        location /too_large {
            client_max_body_size 4;
            js_access test.read_text;
            js_content test.content;
        }

        location /too_large_chunked {
            client_max_body_size 4;
            client_body_timeout 2s;
            js_access test.read_text;
            js_content test.content;
        }


    }

    server {
        listen       127.0.0.1:8081;

        location / {
            js_content test.echo_body;
        }
    }
}

EOF

$t->write_file('test.js', <<EOF);
    function content(r) {
        r.return(200, `var:\${r.variables.foo}`);
    }

    async function content_async(r) {
        await new Promise(resolve => setTimeout(resolve, 5));
        r.return(200, `var:\${r.variables.foo}:content-async`);
    }

    async function content_text(r) {
        let body = await r.readRequestText();
        r.return(200, `content:\${body}`);
    }

    async function read_text(r) {
        let body = await r.readRequestText();
        r.variables.foo = body;
    }

    async function read_text_timeout(r) {
        let body = await r.readRequestText();
        await new Promise(resolve => setTimeout(resolve, 5));
        r.variables.foo = body + ':after-timeout';
    }

    async function read_buffer(r) {
        let buf = await r.readRequestArrayBuffer();
        r.variables.foo = String.fromCharCode.apply(null, new Uint8Array(buf));
    }

    async function read_text_twice(r) {
        let first = await r.readRequestText();
        let second = await r.readRequestText();
        r.variables.foo = (first === second) ? 'same' : 'different';
    }

    async function read_buffer_twice(r) {
        let a = new Uint8Array(await r.readRequestArrayBuffer());
        let b = new Uint8Array(await r.readRequestArrayBuffer());
        let eq = a.length === b.length
                 && a.every((v, i) => v === b[i]);
        r.variables.foo = eq ? 'same' : 'different';
    }

    async function read_concurrent_text_buffer(r) {
        try {
            await Promise.all([
                r.readRequestText(),
                r.readRequestArrayBuffer()
            ]);

            r.variables.foo = 'no_error';

        } catch (e) {
            r.variables.foo = e.message;
        }
    }

    async function read_text_then_buffer(r) {
        let text = await r.readRequestText();
        let buf = await r.readRequestArrayBuffer();
        let text2 = String.fromCharCode.apply(null, new Uint8Array(buf));
        r.variables.foo = (text === text2) ? 'same' : 'different';
    }

    async function read_json(r) {
        let obj = await r.readRequestJSON();
        r.variables.foo = obj.method + ':' + obj.name;
    }

    async function read_json_invalid(r) {
        try {
            await r.readRequestJSON();
            r.variables.foo = 'no_error';
        } catch (e) {
            r.variables.foo = e.constructor.name;
        }
    }

    async function read_text_length(r) {
        let body = await r.readRequestText();
        r.variables.foo = body.length;
    }

    function echo_body(r) {
        r.return(200, 'echo:' + r.requestText);
    }

    export default { content, content_async, content_text, read_text,
                     read_text_timeout, read_buffer, read_text_twice,
                     read_buffer_twice, read_concurrent_text_buffer,
                     read_text_then_buffer, read_json, read_json_invalid,
                     read_text_length, echo_body };

EOF

$t->try_run('no js_access')->plan(23);

###############################################################################

like(http_post('/text'), qr/var:REQ-BODY/, 'readRequestText');
like(http_post('/buffer'), qr/var:REQ-BODY/, 'readRequestArrayBuffer');
like(http_post_json('/json', '{"method":"GET","name":"test"}'),
	qr/var:GET:test/, 'readRequestJSON');
like(http_post_json('/json_invalid', 'not-json'), qr/var:SyntaxError/,
	'readRequestJSON invalid rejects with SyntaxError');
like(http_get('/empty'), qr/var:/, 'readRequestText empty body');

like(http_post('/text_twice'), qr/var:same/,
	'readRequestText twice returns same value');
like(http_post('/buffer_twice'), qr/var:same/,
	'readRequestArrayBuffer twice returns same value');
like(http_post('/text_then_buffer'), qr/var:same/,
	'readRequestText then readRequestArrayBuffer same content');
like(http_post('/concurrent_text_buffer'),
	qr/var:request body is already being read/,
	'concurrent body read throws error');

like(http_post_big('/big'), qr/var:10240/,
	'readRequestText large body');
like(http_post_big('/big_4k'), qr/var:10240/,
	'readRequestText large body with small buffer');

like(http_post('/proxy'), qr/echo:REQ-BODY/,
	'body preserved for proxy_pass');
like(http_post('/in_file'), qr/var:REQ-BODY/,
	'readRequestText from temp file');

like(http_post_slow('/slow'), qr/var:SLOW-BODY/,
	'readRequestText with slow client');
like(http_post_chunked('/chunked'), qr/var:CHUNKED-BODY/,
	'readRequestText chunked transfer encoding');
like(http_post('/text_timeout'), qr/var:REQ-BODY:after-timeout/,
	'readRequestText before async action in js_access');
like(http_post('/access_content_async'),
	qr/var:REQ-BODY:after-timeout:content-async/,
	'async js_content after async js_access body read');
like(http_post('/content_text'), qr/content:REQ-BODY/,
	'readRequestText in js_content');

http_post_disconnect('/text');
like(http_post('/text'), qr/var:REQ-BODY/,
	'readRequestText after client disconnect');

like(http_post('/too_large'), qr/413 Request Entity Too Large/,
	'readRequestText client_max_body_size exceeded');

like(http_post_slow_chunked('/too_large_chunked'),
	qr/413 Request Entity Too Large/,
	'readRequestText chunked body exceeds client_max_body_size');
like(http_post_chunked_too_large('/too_large_chunked'),
	qr/413 Request Entity Too Large/,
	'readRequestText chunked body rejected in preread');
like(http_post('/text'), qr/var:REQ-BODY/,
	'readRequestText works after chunked 413');

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

sub http_post_json {
	my ($url, $body, %extra) = @_;

	my $p = "POST $url HTTP/1.0" . CRLF .
		"Host: localhost" . CRLF .
		"Content-Type: application/json" . CRLF .
		"Content-Length: " . length($body) . CRLF .
		CRLF .
		$body;

	return http($p, %extra);
}

sub http_post_big {
	my ($url, %extra) = @_;

	my $p = "POST $url HTTP/1.0" . CRLF .
		"Host: localhost" . CRLF .
		"Content-Length: 10240" . CRLF .
		CRLF .
		("1234567890" x 1024);

	return http($p, %extra);
}

sub http_post_slow {
	my ($url, %extra) = @_;

	my $header = "POST $url HTTP/1.1" . CRLF .
		"Host: localhost" . CRLF .
		"Content-Length: 9" . CRLF .
		"Connection: close" . CRLF .
		CRLF;

	my $s = http($header, start => 1);

	select undef, undef, undef, 0.1;
	print $s "SLOW";

	select undef, undef, undef, 0.1;
	print $s "-BODY";

	return http_end($s);
}

sub http_post_disconnect {
	my ($url) = @_;

	my $header = "POST $url HTTP/1.1" . CRLF .
		"Host: localhost" . CRLF .
		"Content-Length: 1024" . CRLF .
		"Connection: close" . CRLF .
		CRLF;

	my $s = http($header, start => 1);

	select undef, undef, undef, 0.1;
	print $s "PARTIAL";

	select undef, undef, undef, 0.1;
	close($s);

	select undef, undef, undef, 0.3;
}

sub http_post_slow_chunked {
	my ($url, %extra) = @_;

	my $header = "POST $url HTTP/1.1" . CRLF .
		"Host: localhost" . CRLF .
		"Transfer-Encoding: chunked" . CRLF .
		"Connection: close" . CRLF .
		CRLF;

	my $s = http($header, start => 1);

	select undef, undef, undef, 0.1;
	print $s "8" . CRLF . "TOOLARGE" . CRLF;

	my $resp = http_end($s);

	# wait for nginx to finish lingering close and cleanup
	select undef, undef, undef, 0.5;

	return $resp;
}

sub http_post_chunked {
	my ($url, %extra) = @_;

	my $p = "POST $url HTTP/1.1" . CRLF .
		"Host: localhost" . CRLF .
		"Transfer-Encoding: chunked" . CRLF .
		"Connection: close" . CRLF .
		CRLF .
		"8" . CRLF .
		"CHUNKED-" . CRLF .
		"4" . CRLF .
		"BODY" . CRLF .
		"0" . CRLF .
		CRLF;

	return http($p, %extra);
}

sub http_post_chunked_too_large {
	my ($url, %extra) = @_;

	my $p = "POST $url HTTP/1.1" . CRLF .
		"Host: localhost" . CRLF .
		"Transfer-Encoding: chunked" . CRLF .
		"Connection: close" . CRLF .
		CRLF .
		"8" . CRLF .
		"TOOLARGE" . CRLF .
		"0" . CRLF .
		CRLF;

	return http($p, %extra);
}

###############################################################################
