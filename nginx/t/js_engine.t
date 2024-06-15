#!/usr/bin/perl

# (C) Dmitry Volyntsev
# (C) Nginx, Inc.

# Tests for http njs module, js_engine directive.

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

    server {
        listen       127.0.0.1:8080;
        server_name  localhost;

        location /njs {
            js_content test.njs;
        }

        location /njs/ {
            proxy_pass http://127.0.0.1:8081/;
        }

        location /qjs/ {
            proxy_pass http://127.0.0.1:8082/;
        }
    }

    server {
        listen       127.0.0.1:8081;
        server_name  localhost;

        js_engine njs;

        location /test {
            js_content test.test;
        }

        location /override {
            js_engine qjs;
            js_content test.test;
        }
    }

    server {
        listen       127.0.0.1:8082;
        server_name  localhost;

        js_engine qjs;

        location /test {
            js_content test.test;
        }

        location /override {
            js_engine njs;
            js_content test.test;
        }
    }
}

EOF

$t->write_file('test.js', <<EOF);
    function test_njs(r) {
        r.return(200, njs.version);
    }

    function test(r) {
        r.return(200, njs.engine);
    }

    export default {njs: test_njs, test};

EOF

$t->try_run('no njs js_engine')->plan(4);

###############################################################################

TODO: {
local $TODO = 'not yet' unless has_version('0.8.6');

like(http_get('/njs/test'), qr/njs/, 'js_engine njs server');
like(http_get('/njs/override'), qr/QuickJS/, 'js_engine override');
like(http_get('/qjs/test'), qr/QuickJS/, 'js_engine qjs server');
like(http_get('/qjs/override'), qr/njs/, 'js_engine override');

}

$t->stop();

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
