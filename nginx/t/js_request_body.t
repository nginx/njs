#!/usr/bin/perl

# (C) Dmitry Volyntsev
# (C) Nginx, Inc.

# Tests for http njs module, r.requestText method.

###############################################################################

use warnings;
use strict;

use Test::More;

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

        location /body {
            js_content test.body;
        }

        location /in_file {
            client_body_in_file_only on;
            js_content test.body;
        }

        location /read_body_from_temp_file {
            client_body_in_file_only clean;
            js_content test.read_body_from_temp_file;
        }
    }
}

EOF

$t->write_file('test.js', <<EOF);
    import fs from 'fs';

    function body(r) {
        try {
            var body = r.requestText;
            r.return(200, body);

        } catch (e) {
            r.return(500, e.message);
        }
    }

    function read_body_from_temp_file(r) {
        let fn = r.variables.request_body_file;
        r.return(200, fs.readFileSync(fn));
    }

    export default {body, read_body_from_temp_file};

EOF

$t->try_run('no njs request body')->plan(4);

###############################################################################

like(http_post('/body'), qr/REQ-BODY/, 'request body');
like(http_post('/in_file'), qr/request body is in a file/,
	'request body in file');
like(http_post_big('/body'), qr/200.*^(1234567890){1024}$/ms,
		'request body big');
like(http_post_big('/read_body_from_temp_file'),
	qr/200.*^(1234567890){1024}$/ms, 'request body big from temp file');

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

sub http_post_big {
	my ($url, %extra) = @_;

	my $p = "POST $url HTTP/1.0" . CRLF .
		"Host: localhost" . CRLF .
		"Content-Length: 10240" . CRLF .
		CRLF .
		("1234567890" x 1024);

	return http($p, %extra);
}

###############################################################################
