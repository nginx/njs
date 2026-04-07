#!/usr/bin/perl

# (C) Dmitry Volyntsev
# (c) Nginx, Inc.

# Tests for http njs module, check for proper server blocks merging.

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

    js_import main.js;

    server {
        listen       127.0.0.1:8080;

        location / {
            js_content main.engine_id;
        }
    }

    server {
        listen       127.0.0.1:8081;

        location / {
            js_content main.engine_id;
        }
    }

    server {
        listen       127.0.0.1:8082;

        location / {
            js_content main.engine_id;
        }
    }

    server {
        listen       127.0.0.1:8083;

        location / {
            js_content main.engine_id;
        }
    }
}

EOF

$t->write_file('main.js', <<EOF);
    function engine_id(r) {
        r.return(200, ngx.engine_id);
    }

    export default {engine_id};

EOF

$t->try_run('no njs available');

###############################################################################

my %ids;
for my $port (port(8080), port(8081), port(8082), port(8083)) {
	http("GET / HTTP/1.0\nHost: localhost\n\n",
		PeerAddr => "127.0.0.1:$port")
		=~ /\x0d\x0a\x0d\x0a(\d+)/ms;
	$ids{$1} = 1 if defined $1;
}

plan(skip_all => 'ngx.engine_id requires --with-debug')
    unless scalar keys %ids;

$t->plan(1);

is(scalar keys %ids, 1, 'http js block imported once');

###############################################################################
