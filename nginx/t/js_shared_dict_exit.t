#!/usr/bin/perl

# (C) Dmitry Volyntsev
# (C) F5, Inc.

# Tests for js_shared_dict_zone directive, njs.on('exit', ...).

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

    js_shared_dict_zone zone=bar:64k type=string;

    server {
        listen       127.0.0.1:8080;
        server_name  localhost;

        location /exit {
            js_content test.exit;
        }
    }
}

EOF

$t->write_file('test.js', <<'EOF');
    function exit(r) {
        var dict = ngx.shared.bar;
        var key = r.args.key;

        if (!dict.add(key, 'value')) {
            r.return(200, `Key ${key} exists`);
            return;
        }

        njs.on('exit', function() {
            dict.delete(key);
        });

        r.return(200, 'SUCCESS');
    }

    export default { exit };
EOF

$t->try_run('no js_shared_dict_zone');

$t->plan(2);

###############################################################################

like(http_get('/exit?key=foo'), qr/SUCCESS/, 'first');
like(http_get('/exit?key=foo'), qr/SUCCESS/, 'second');

###############################################################################
