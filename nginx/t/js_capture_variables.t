#!/usr/bin/perl

# (C) Thomas P.

# Tests for http njs module, reading location capture variables.

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

        location ~ /(.+)/(.+) {
            js_content test.variables;
        }
    }
}

EOF

$t->write_file('test.js', <<EOF);
    function variables(r) {
        return r.return(200, `"\${r.variables[r.args.index]}"`);
    }

    function test_njs(r) {
        r.return(200, njs.version);
    }

    export default {njs:test_njs, variables};

EOF

$t->try_run('no njs capture variables')->plan(4);

###############################################################################

TODO: {
local $TODO = 'not yet' unless has_version('0.8.6');

like(http_get('/test/hello?index=0'), qr/"\/test\/hello"/, 'global capture');
like(http_get('/test/hello?index=1'), qr/"test"/, 'local capture 1');
like(http_get('/test/hello?index=2'), qr/"hello"/, 'local capture 2');
like(http_get('/test/hello?index=3'), qr/"undefined"/, 'undefined capture');

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
