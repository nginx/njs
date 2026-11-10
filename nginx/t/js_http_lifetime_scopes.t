#!/usr/bin/perl

# (C) Dmitry Volyntsev
# (C) F5, Inc.

# Tests for QuickJS lifetime scopes during redirects and periodic cleanup.

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
worker_processes 1;

events {
}

http {
    %%TEST_GLOBALS_HTTP%%

    js_context_reuse 1;
    js_import test.js;
    js_var $test_value original;
    js_shared_dict_zone zone=scopes:32k;

    server {
        listen       127.0.0.1:8080;
        server_name  localhost;

        location /engine {
            js_content test.engine;
        }

        location /source {
            js_import source.js;
            js_content source.content;
        }

        location /destination {
            js_import destination.js;
            js_content destination.content;
        }

        location @periodic {
            js_periodic test.periodic interval=40ms;
        }

        location @forced {
            js_periodic test.forced interval=50ms;
        }

        location /results {
            js_content test.results;
        }
    }
}

EOF

$t->write_file('source.js', <<'EOF');
    let headers;

    function content(r) {
        let expired = headers === undefined;
        if (headers !== undefined) {
            try {
                headers.host;
            } catch (e) {
                expired = e instanceof TypeError;
            }
        }

        headers = r.headersIn;
        njs.on('exit', () => {
            ngx.shared.scopes.set('source_exit',
                                  String(headers.host === 'localhost'));
        });
        r.internalRedirect('/destination?source=' + expired);
    }

    export default {content};
EOF

$t->write_file('destination.js', <<'EOF');
    let variables;

    function content(r) {
        let expired = variables === undefined;
        if (variables !== undefined) {
            try {
                variables.test_value;
            } catch (e) {
                expired = e instanceof TypeError;
            }
        }

        variables = r.variables;
        njs.on('exit', () => {
            ngx.shared.scopes.set('destination_exit',
                                  String(variables.test_value === 'original'));
        });
        r.return(200, r.args.source + ':' + expired);
    }

    export default {content};
EOF

$t->write_file('test.js', <<'EOF');
    let previous, previousForced;

    function engine(r) {
        r.return(200, njs.engine);
    }

    function check(value) {
        try {
            value.test_value;
        } catch (e) {
            return e instanceof TypeError;
        }

        return false;
    }

    function periodic(s) {
        if (njs.engine !== 'QuickJS') {
            return;
        }

        if (previous !== undefined) {
            ngx.shared.scopes.set('periodic', String(check(previous)));
        }

        previous = s.variables;
    }

    function forced(s) {
        if (njs.engine !== 'QuickJS') {
            return;
        }

        if (previousForced === undefined) {
            previousForced = s.rawVariables;
            setTimeout(() => {}, 100000);
            return;
        }

        ngx.shared.scopes.set('forced', String(check(previousForced)));
    }

    function results(r) {
        const d = ngx.shared.scopes;
        r.return(200, ['periodic', 'forced', 'source_exit', 'destination_exit']
                      .map(name => d.get(name)).join(':'));
    }

    export default {engine, periodic, forced, results};
EOF

$t->try_run('no njs available');

plan(skip_all => 'QuickJS-specific lifetime tests')
    if http_get('/engine') =~ /njs$/m;

$t->plan(3);

###############################################################################

like(http_get('/source'), qr/true:true$/, 'initial redirect scopes');
like(http_get('/source'), qr/true:true$/, 'old scopes expire independently');

my $result;
for (1 .. 50) {
    $result = http_get('/results');
    last if $result =~ /true:true:true:true$/;
    select undef, undef, undef, 0.02;
}

like($result, qr/true:true:true:true$/,
    'periodic expiry and synchronous exit-hook access');

###############################################################################
