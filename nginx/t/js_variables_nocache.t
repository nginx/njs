#!/usr/bin/perl

# (C) Thomas P.

# Tests for http njs module, setting non-cacheable nginx variables.

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

    js_set $nocache_var   test.variable nocache;
    js_set $default_var   test.variable;
    js_set $callcount_var test.callCount nocache;

    js_import test.js;

    server {
        listen       127.0.0.1:8080;
        server_name  localhost;

        location /default_var {
            set $a $default_var;
            set $b $default_var;
            return 200 '"$a/$b"';
        }

        location /nocache_var {
            set $a $nocache_var;
            set $b $nocache_var;
            return 200 '"$a/$b"';
        }

        location /callcount_var {
            set $a $callcount_var;
            set $b $callcount_var;
            return 200 '"$a/$b"';
        }
    }
}

EOF

$t->write_file('test.js', <<EOF);
    function variable(r) {
        return Math.random().toFixed(16);
    }

    let n = 0;
    function callCount(r) {
        return (n += 1).toString();
    }

    export default {variable, callCount};

EOF

$t->try_run('no nocache njs variables')->plan(3);

###############################################################################

# We use backreferences to make sure the same value was returned for the two uses
like(http_get('/default_var'), qr/"(.+)\/\1"/, 'cached variable');
# Negative lookaheads don't capture, hence the .+ after it
like(http_get('/nocache_var'), qr/"(.+)\/(?!\1).+"/, 'noncacheable variable');

TODO: {
    local $TODO = "Needs upstream nginx patch https://mailman.nginx.org/pipermail/nginx-devel/2024-August/N7VFIYUKSZFUIAO24OJODKQGTP63R5NV.html";

    # Without the patch, this will give 2/4 (calls are duplicated)
    like(http_get('/callcount_var'), qr/"1\/2"/, 'callcount variable');
}

###############################################################################
