#!/usr/bin/perl

# (C) Dmitry Volyntsev
# (C) F5, Inc.

# Tests for http njs module, js_access directive with satisfy.

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

my $t = Test::Nginx->new()->has(qw/http rewrite access/)
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

        location /all_decline_allow {
            satisfy all;
            allow all;
            js_access test.decline;
            js_content test.content;
        }

        location /all_decline_deny {
            satisfy all;
            deny all;
            js_access test.decline;
            js_content test.content;
        }

        location /any_allow_deny {
            satisfy any;
            deny all;
            js_access test.allow;
            js_content test.content;
        }

        location /any_deny_allow {
            satisfy any;
            allow all;
            js_access test.deny;
            js_content test.content;
        }

        location /any_both_deny {
            satisfy any;
            deny all;
            js_access test.deny;
            js_content test.content;
        }

        location /any_decline_deny {
            satisfy any;
            deny all;
            js_access test.decline;
            js_content test.content;
        }

        location /any_decline_allow {
            satisfy any;
            allow all;
            js_access test.decline;
            js_content test.content;
        }

        location /any_async_allow_deny {
            satisfy any;
            deny all;
            js_access test.async_allow;
            js_content test.content;
        }

        location /any_async_decline_deny {
            satisfy any;
            deny all;
            js_access test.async_decline;
            js_content test.content;
        }
    }
}

EOF

$t->write_file('test.js', <<'EOF');
    function allow(r) {
        /* default: normal return yields NGX_OK */
    }

    function deny(r) {
        r.return(403);
    }

    function decline(r) {
        r.decline();
    }

    async function async_allow(r) {
        await new Promise(resolve => setTimeout(resolve, 5));
    }

    async function async_decline(r) {
        await new Promise(resolve => setTimeout(resolve, 5));
        r.decline();
    }

    function content(r) {
        r.return(200, 'PASSED');
    }

    export default { allow, deny, decline, async_allow, async_decline,
                     content };
EOF

$t->try_run('no js_access')->plan(9);

###############################################################################

# satisfy all + decline: ip decides
like(http_get('/all_decline_allow'), qr/PASSED/,
	'satisfy all: js declines + ip allows');
like(http_get('/all_decline_deny'), qr/403 Forbidden/,
	'satisfy all: js declines + ip denies');

# satisfy any: js allows overrides ip deny
like(http_get('/any_allow_deny'), qr/PASSED/,
	'satisfy any: js allows + ip denies');

# satisfy any: ip allows overrides js deny
like(http_get('/any_deny_allow'), qr/PASSED/,
	'satisfy any: js denies + ip allows');

# satisfy any: both deny
like(http_get('/any_both_deny'), qr/403 Forbidden/,
	'satisfy any: both deny');

# satisfy any + decline: js has no opinion, ip decides
like(http_get('/any_decline_deny'), qr/403 Forbidden/,
	'satisfy any: js declines + ip denies');
like(http_get('/any_decline_allow'), qr/PASSED/,
	'satisfy any: js declines + ip allows');

# async variants
like(http_get('/any_async_allow_deny'), qr/PASSED/,
	'satisfy any: async js allows + ip denies');
like(http_get('/any_async_decline_deny'), qr/403 Forbidden/,
	'satisfy any: async js declines + ip denies');

###############################################################################
