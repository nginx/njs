#!/usr/bin/perl

# (C) Dmitry Volyntsev
# (C) F5, Inc.

# Tests for js_shared_dict_zone eviction.

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

    js_shared_dict_zone zone=foo:32k timeout=2s evict;
    js_shared_dict_zone zone=stress:32k timeout=1000s evict;

    server {
        listen       127.0.0.1:8080;
        server_name  localhost;

        location /stress {
            js_content test.stress;
        }

        location /no_self_evict {
            js_content test.no_self_evict;
        }
    }
}

EOF

$t->write_file('test.js', <<'EOF');
    function stress(r) {
        var dict = ngx.shared.foo;
        dict.clear();

        var v = 'x'.repeat(1024);
        var n = 64;
        var failed = 0;

        for (var i = 0; i < n; i++) {
            try {
                dict.set('stress_' + i, v);
            } catch (e) {
                failed++;
            }
        }

        var last = dict.get('stress_' + (n - 1));
        var first = dict.get('stress_0');

        var last_s = last !== undefined ? 'exists' : 'missing';
        var first_s = first !== undefined ? 'exists' : 'missing';

        r.return(200, `failed:${failed},last:${last_s},first:${first_s}`);
    }

    function no_self_evict(r) {
        var dict = ngx.shared.stress;
        var v = 'x'.repeat(128);

        dict.clear();
        dict.set('target', v);

        /* Count how many items can be added while 'target' is present. */

        var elems = 0;
        while (dict.has('target')) {
            dict.set('fill_' + elems++, v);
        }

        dict.clear();
        dict.set('target', v);

        for (var i = 0; i < elems - 1; i++) {
            dict.set('fill_' + i, v);
        }

        /* Check that 'target' is not evicted by the updates of itself. */

        dict.set('target', 'y'.repeat(128));

        r.return(200, 'FILLED:' + elems);
    }

    export default { stress, no_self_evict };
EOF

$t->try_run('no js_shared_dict_zone');

$t->plan(6);

###############################################################################

my $evict_resp = http_get('/stress');
like($evict_resp, qr/failed:0/, 'evict stress: all writes succeeded');
like($evict_resp, qr/last:exists/, 'evict stress: last write exists');
like($evict_resp, qr/first:missing/, 'evict stress: first write evicted');
unlike($t->read_file('error.log'), qr/no memory in js shared zone "foo"/,
	'evict stress: no shared zone foo errors in error log');

my $update_resp = http_get('/no_self_evict');
like($update_resp, qr/FILLED:/, 'evict update: zone filled');
unlike($t->read_file('error.log'), qr/is already free/,
	'evict update: no double-free in error log');

###############################################################################
