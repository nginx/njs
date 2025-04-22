#!/usr/bin/perl

# (C) Dmitry Volyntsev
# (C) Nginx, Inc.

# Tests for js_periodic directive.

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

my $t = Test::Nginx->new()->has(qw/http rewrite/)
	->write_file_expand('nginx.conf', <<'EOF');

%%TEST_GLOBALS%%

daemon off;
worker_processes 4;

events {
}

worker_shutdown_timeout 100ms;

http {
    %%TEST_GLOBALS_HTTP%%

    js_import test.js;

    js_shared_dict_zone zone=strings:32k;

    server {
        listen       127.0.0.1:8080;
        server_name  localhost;

        location @periodic {
            js_periodic test.fetch interval=40ms;
            js_periodic test.multiple_fetches interval=1s;

            js_periodic test.fetch_exception interval=1s;
        }

        location /engine {
            js_content test.engine;
        }

        location /fetch_ok {
            return 200 'ok';
        }

        location /fetch_foo {
            return 200 'foo';
        }

        location /test_fetch {
            js_content test.test_fetch;
        }

        location /test_multiple_fetches {
            js_content test.test_multiple_fetches;
        }
    }
}

EOF

my $p0 = port(8080);

$t->write_file('test.js', <<EOF);
    function engine(r) {
        r.return(200, njs.engine);
    }

    async function fetch() {
        let reply = await ngx.fetch('http://127.0.0.1:$p0/fetch_ok');
        let body = await reply.text();

        let v = ngx.shared.strings.get('fetch') || '';
        ngx.shared.strings.set('fetch', v + body);
    }

    async function multiple_fetches() {
        let reply = await ngx.fetch('http://127.0.0.1:$p0/fetch_ok');
        let reply2 = await ngx.fetch('http://127.0.0.1:$p0/fetch_foo');
        let body = await reply.text();
        let body2 = await reply2.text();

        ngx.shared.strings.set('multiple_fetches', body + '\@' + body2);
    }

    async function fetch_exception() {
        let reply = await ngx.fetch('garbage');
     }

    function test_fetch(r) {
        r.return(200, ngx.shared.strings.get('fetch').startsWith('okok'));
    }

    function test_multiple_fetches(r) {
        r.return(200, ngx.shared.strings.get('multiple_fetches')
                                                        .startsWith('ok\@foo'));
    }

    export default { fetch, fetch_exception, multiple_fetches, test_fetch,
                     test_multiple_fetches, engine };
EOF

$t->try_run('no js_periodic with fetch');

$t->plan(3);

###############################################################################

select undef, undef, undef, 0.1;

like(http_get('/test_fetch'), qr/true/, 'periodic fetch test');
like(http_get('/test_multiple_fetches'), qr/true/, 'multiple fetch test');

$t->stop();

unlike($t->read_file('error.log'), qr/\[error\].*should not be seen/,
	'check for not discadred events');
