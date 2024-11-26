#!/usr/bin/perl

# (C) Sergey Kandaurov
# (C) Nginx, Inc.

# Tests for http njs module, return method.

###############################################################################

use warnings;
use strict;

use Test::More;

use Config;

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

        location / {
            js_content test.returnf;
        }

        location /limit {
            sendfile_max_chunk 5;
            js_content test.returnf;
        }

        location /njs {
            js_content test.njs;
        }
    }
}

EOF

$t->write_file('test.js', <<EOF);
    function test_njs(r) {
        r.return(200, njs.version);
    }

    function returnf(r) {
        let body = r.args.t;
        if (body && r.args.repeat) {
            body = body.repeat(r.args.repeat);
        }

        r.return(Number(r.args.c), body);
    }

    export default {njs:test_njs, returnf};

EOF

$t->try_run('no njs return')->plan(7);

###############################################################################

like(http_get('/?c=200'), qr/200 OK.*\x0d\x0a?\x0d\x0a?$/s, 'return code');
like(http_get('/?c=200&t=SEE-THIS'), qr/200 OK.*^SEE-THIS$/ms, 'return text');
like(http_get('/?c=301&t=path'), qr/ 301 .*Location: path/s, 'return redirect');
like(http_get('/?c=404'), qr/404 Not.*html/s, 'return error page');
like(http_get('/?c=inv'), qr/ 500 /, 'return invalid');

TODO: {
local $TODO = 'not yet' unless has_version('0.8.6');

unlike(http_get('/?c=404&t='), qr/Not.*html/s, 'return empty body');

}

TODO: {
local $TODO = 'not yet' unless has_version('0.8.8');

like(http_get('/limit?c=200&t=X&repeat=50'), qr/200 OK.*X{50}/s,
	'return limited');

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
