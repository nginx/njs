#!/usr/bin/perl

# (C) Dmitry Volyntsev
# (C) F5, Inc.

# Tests for QuickJS stream objects after session and periodic cleanup.

###############################################################################

use warnings;
use strict;
use Test::More;

BEGIN { use FindBin; chdir($FindBin::Bin); }

use lib 'lib';
use Test::Nginx;
use Test::Nginx::Stream qw/ stream /;

###############################################################################

select STDERR; $| = 1;
select STDOUT; $| = 1;

my $t = Test::Nginx->new()->has(qw/stream stream_return/)
    ->write_file_expand('nginx.conf', <<'EOF');

%%TEST_GLOBALS%%

daemon off;
worker_processes 1;

events {
}

stream {
    %%TEST_GLOBALS_STREAM%%

    js_context_reuse 1;
    js_import test.js;
    js_var $test_value original;
    js_var $result;
    js_set $engine test.engine;
    js_shared_dict_zone zone=scopes:32k;

    server {
        listen      127.0.0.1:8081;
        js_preread  test.preread;
        return      $result;
    }

    server {
        listen  127.0.0.1:8082;
        return  $engine;
    }

    server {
        listen       127.0.0.1:8083;
        js_periodic  test.periodic interval=40ms;
        return       OK;
    }

    server {
        listen      127.0.0.1:8084;
        js_preread  test.results;
        return      $result;
    }
}

EOF

$t->write_file('test.js', <<'EOF');
    let saved, snapshot, flags, address, previous;

    function engine() {
        return njs.engine;
    }

    const checks = {
        variables: () => saved.test_value,
        variables_set: () => { saved.test_value = 'changed'; },
        variables_define: () => {
            Object.defineProperty(saved, 'test_value', {value: 'changed'});
        },
        raw_variables: () => saved.test_value,
        session: () => saved.remoteAddress,
        status: () => saved.status,
        on: () => saved.on('upload', () => {}),
        off: () => saved.off('upload'),
        done: () => saved.done(),
        send: () => saved.sendUpstream('x'),
        return_value: () => saved.setReturnValue('x'),
        var_names: () => saved.jsVarNames()
    };

    function preread(s) {
        s.on('upload', (data, f) => {
            if (data.length === 0) {
                return;
            }

            const parts = data.split(':');
            const action = parts[0], name = parts[1];
            if (action === 'keep') {
                snapshot = s.rawVariables.test_value;
                flags = f;
                address = s.remoteAddress;
                if (name.startsWith('variables')) {
                    saved = s.variables;
                } else if (name === 'raw_variables') {
                    saved = s.rawVariables;
                } else {
                    saved = s;
                }

                s.variables.result = 'kept';
            } else {
                let expired = false;
                try {
                    checks[name]();
                } catch (e) {
                    expired = e instanceof TypeError;
                }

                const independent = snapshot.toString() === 'original'
                                    && flags.from_upstream === false
                                    && address === '127.0.0.1';
                saved = null;
                snapshot = null;
                flags = null;
                s.variables.result = expired + ':' + independent;
            }

            s.done();
        });
    }

    function periodic(s) {
        if (njs.engine !== 'QuickJS'
            || ngx.shared.scopes.get('enabled') !== '1')
        {
            return;
        }

        if (previous !== undefined) {
            let expired = false;
            try {
                previous.test_value;
            } catch (e) {
                expired = e instanceof TypeError;
            }

            ngx.shared.scopes.set('periodic', String(expired));
        }

        previous = s.variables;
    }

    function results(s) {
        s.on('upload', data => {
            if (data.length === 0) {
                return;
            }

            ngx.shared.scopes.set('enabled', '1');
            s.variables.result = ngx.shared.scopes.get('periodic') || '-';
            s.done();
        });
    }

    export default {engine, preread, periodic, results};
EOF

my @cases = qw/variables variables_set variables_define raw_variables
               session status on off done send return_value var_names/;
@cases = grep { $_ eq $ENV{NJS_LIFETIME_CASE} } @cases
    if defined $ENV{NJS_LIFETIME_CASE};
my $periodic = !defined $ENV{NJS_LIFETIME_CASE}
               || $ENV{NJS_LIFETIME_CASE} eq 'periodic';

$t->try_run('no stream njs available');
plan(skip_all => 'QuickJS-specific lifetime tests')
    if stream('127.0.0.1:' . port(8082))->read() =~ /njs$/;
$t->plan(2 * @cases + ($periodic ? 1 : 0));

###############################################################################

for my $case (@cases) {
    is(stream('127.0.0.1:' . port(8081))->io("keep:$case"), 'kept',
       "$case before cleanup");
    is(stream('127.0.0.1:' . port(8081))->io("read:$case"), 'true:true',
       "$case expires without invalidating extracted values");
}

if ($periodic) {
    my $result;
    for (1 .. 50) {
        $result = stream('127.0.0.1:' . port(8084))->io('results');
        last if $result eq 'true';
        select undef, undef, undef, 0.02;
    }

    is($result, 'true',
       'periodic variables expire with their originating session');
}

###############################################################################
