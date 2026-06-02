#!/usr/bin/perl

# (C) Dmitry Volyntsev
# (C) F5, Inc.

# Tests for http njs module, js_set inline expressions without js_import.

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

my $t = Test::Nginx->new()->has(qw/http rewrite/)
	->write_file_expand('nginx.conf', <<'EOF');

%%TEST_GLOBALS%%

daemon off;

events {
}

http {
    %%TEST_GLOBALS_HTTP%%

    js_set $template   `/p${r.uri}/post`;

    server {
        listen       127.0.0.1:8080;
        server_name  localhost;

        js_set $a '(r.args.a || "none")';
        js_set $expanded 'r.var.uri.toUpperCase()';

        location /inline {
            js_set $method '(r.method)';

            return 200 "uri=$template a=$a method=$method";
        }

        location /expanded {
            return 200 "expanded=$expanded";
        }
    }
}

EOF

$t->try_run('no njs')->plan(6);

###############################################################################

like(http_get('/inline'), qr/uri=\/p\/inline\/post/, 'inline only uri');
like(http_get('/inline'), qr/a=none/, 'inline only args');
like(http_get('/inline?a=1'), qr/a=1/, 'inline only args with value');
like(http_get('/inline'), qr/method=GET/, 'inline only method');
like(http_get('/expanded'), qr/expanded=\/EXPANDED/,
	'inline only expanded r.var');

$t->stop();

ok(index($t->read_file('error.log'), 'SyntaxError') < 0, 'no syntax errors');

###############################################################################
