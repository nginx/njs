#!/usr/bin/perl

# (C) Dmitry Volyntsev
# (C) F5, Inc.

# Tests for QuickJS context reuse after an internal redirect.  A context must
# be returned to the reuse queue of the location that created it.

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
worker_processes 1;

events {
}

http {
    %%TEST_GLOBALS_HTTP%%

    js_engine qjs;
    js_context_reuse 4;
    js_context_reuse_max_size 64m;

    server {
        listen       127.0.0.1:8080;
        server_name  localhost;

        location = /source {
            js_import handler from source.js;
            js_content handler.content;
        }

        location = /destination {
            js_import handler from destination.js;
            js_content handler.content;
        }
    }
}

EOF

$t->write_file('source.js', <<'EOF');
    let visits = 0;

    function content(r) {
        visits++;

        if (r.uri == '/source') {
            r.internalRedirect(`/destination?from=${visits}`);
            return;
        }

        r.return(200, `source:${visits}`);
    }

    export default { content };

EOF

$t->write_file('destination.js', <<'EOF');
    let visits = 0;

    function content(r) {
        visits++;
        r.return(200, `destination:${visits}:from=${r.args.from || '-'}`);
    }

    export default { content };

EOF

$t->try_run('no njs available')->plan(4);

###############################################################################

like(http_get('/source'), qr/destination:1:from=1/, 'internal redirect');
like(http_get('/destination'), qr/destination:2:from=-/, 'destination reuse');
like(http_get('/destination'), qr/destination:3:from=-/,
	'destination reuse queue isolation');
like(http_get('/source'), qr/destination:4:from=2/,
	'source reuse queue ownership');

###############################################################################
