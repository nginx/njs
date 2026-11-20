#!/usr/bin/perl

# (C) Dmitry Volyntsev
# (C) F5, Inc.

# Tests for finite shared dictionary values and increment rollback.

use warnings;
use strict;
use Test::More;

BEGIN { use FindBin; chdir($FindBin::Bin); }
use lib 'lib';
use Test::Nginx;
use Test::Nginx::Stream qw/stream/;
use JSON::PP;

my $t = Test::Nginx->new()->has(qw/http stream/)
    ->write_file_expand('nginx.conf', <<'EOF');

%%TEST_GLOBALS%%
daemon off;
events {}

http {
    %%TEST_GLOBALS_HTTP%%
    js_import test.js;
    js_shared_dict_zone zone=http_nums:512k type=number timeout=1h;

    server {
        listen 127.0.0.1:8080;
        location / { js_content test.http; }
    }
}

stream {
    %%TEST_GLOBALS_STREAM%%
    js_import test.js;
    js_shared_dict_zone zone=stream_nums:512k type=number timeout=1h;
    js_set $result test.check;

    server {
        listen 127.0.0.1:8081;
        return $result;
    }
}
EOF

$t->write_file('test.js', <<'EOF');
    function check(s) {
        const d = ngx.shared[s ? 'stream_nums' : 'http_nums'];
        const invalid = [NaN, Infinity, -Infinity];
        const methods = ['set', 'add', 'replace', 'delta', 'init'];
        const results = [];

        function attempt(name, base, operation) {
            d.clear();
            d.set('old', base, 30000);
            const ttl = d.ttl('old');
            let error = '';

            try {
                operation();
            } catch (e) {
                error = e.name;
            }

            const after = d.ttl('old');
            results.push({name, error,
                ok: error === 'TypeError' && d.get('old') === base
                    && !d.has('new') && after <= ttl && after >= ttl - 1000});

            d.set('probe', 1);
        }

        for (let i = 0; i < invalid.length; i++) {
            const value = invalid[i];
            for (let j = 0; j < methods.length; j++) {
                const method = methods[j];
                attempt(method + ':' + String(value), 5, () => {
                    if (method === 'add') {
                        d.add('new', value, 1);
                    } else if (method === 'delta') {
                        d.incr('old', value, 0, 1);
                    } else if (method === 'init') {
                        d.incr('new', 1, value, 1);
                    } else {
                        d[method]('old', value, 1);
                    }
                });
            }
        }

        attempt('existing increment overflow', Number.MAX_VALUE,
                () => d.incr('old', Number.MAX_VALUE, 0, 1));
        attempt('new increment overflow', 5,
                () => d.incr('new', Number.MAX_VALUE, Number.MAX_VALUE, 1));

        d.clear();
        d.set('old', 5);
        results.push({name: 'finite increments',
            ok: d.incr('old', 1) === 6 && d.incr('new', 2, 4) === 6});

        return JSON.stringify(results);
    }

    function http(r) {
        r.return(200, check());
    }

    export default {check, http};
EOF

$t->run()->plan(36);

my ($http) = http_get('/') =~ /\x0d\x0a\x0d\x0a(.*)/s;
my $stream = stream('127.0.0.1:' . port(8081))->read();

for my $response (['http', $http], ['stream', $stream]) {
    my $results = JSON::PP::decode_json($response->[1]);
    for my $result (@$results) {
        ok($result->{ok}, "$response->[0] $result->{name}")
            or diag(JSON::PP::encode_json($result));
    }
}
