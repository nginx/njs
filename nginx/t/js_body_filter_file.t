#!/usr/bin/perl

# (C) Dmitry Volyntsev
# (C) Nginx, Inc.

# Tests for http njs module, body filter, when data is develivered from a file.

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

my $t = Test::Nginx->new()->has(qw/http proxy/)
	->write_file_expand('nginx.conf', <<'EOF');

%%TEST_GLOBALS%%

daemon off;

events {
}

http {
    %%TEST_GLOBALS_HTTP%%

    js_import test.js;

    proxy_cache_path /tmp/one levels=1 keys_zone=one:1m;

    server {
        listen       127.0.0.1:8080;
        server_name  localhost;

        proxy_cache one;
        proxy_cache_valid any 1s;

        location /origin/ {
            return 200 ORIGIN;
        }

         location /normal/ {
            proxy_pass http://127.0.0.1:8080/origin/;

            js_header_filter test.clear_content_length;
            js_body_filter   test.filter;
        }

         location /sendfile/ {
            sendfile on;
            proxy_pass http://127.0.0.1:8080/origin/;

            js_header_filter test.clear_content_length;
            js_body_filter   test.filter;
        }
    }
}

EOF

$t->write_file('test.js', <<EOF);
    var buffer = '';

    function filter(r, data, flags) {
        buffer += data;

        if (flags.last) {
            return r.sendBuffer(buffer, flags);
        }
    }

    function clear_content_length(r) {
        delete r.headersOut['Content-Length'];
    }

    export default {filter, clear_content_length};

EOF

$t->try_run('no njs body filter')->plan(2);

###############################################################################

like(http_get('/normal/'), qr/ORIGIN$/, 'normal proxy with njs body filter');
like(http_get('/sendfile/'), qr/ORIGIN$/,
	'sendfile proxy with njs body filter');

###############################################################################
