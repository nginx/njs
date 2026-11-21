#!/usr/bin/perl

# (C) Dmitry Volyntsev
# (C) F5, Inc.

# Tests for shared dictionary timeout conversion and rollback.

use warnings;
use strict;
use Test::More;
use Config;

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
    js_shared_dict_zone zone=http_nums:512k type=number timeout=1m;
    server {
        listen 127.0.0.1:8080;
        location / { js_content test.http; }
    }
}

stream {
    %%TEST_GLOBALS_STREAM%%
    js_import test.js;
    js_shared_dict_zone zone=stream_nums:512k type=number timeout=1m;
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
        const cases = [
            {name: 'omitted', omit: true},
            {name: 'undefined', timeout: undefined},
            {name: 'one millisecond', timeout: 1, ttl: 1},
            {name: 'fraction', timeout: 1200.9, ttl: 1200},
            {name: 'upper bound', timeout: 0xffffffff, ttl: 0xffffffff,
             upper: true},
            {name: 'zero', timeout: 0, reject: true},
            {name: 'sub-millisecond', timeout: 0.9, reject: true},
            {name: 'negative', timeout: -1, reject: true},
            {name: 'NaN', timeout: NaN, reject: true},
            {name: 'Infinity', timeout: Infinity, reject: true},
            {name: '-Infinity', timeout: -Infinity, reject: true},
            {name: '32-bit wrap', timeout: 0x100000000 + 1000, reject: true},
            {name: '64-bit overflow', timeout: 2 ** 64, reject: true},
            {name: 'string', timeout: '1000', reject: true},
        ];
        const methods = ['set', 'add', 'replace', 'incr'];
        const results = [];

        for (let i = 0; i < cases.length; i++) {
            const c = cases[i];
            for (let j = 0; j < methods.length; j++) {
                const method = methods[j];
                const key = method === 'add' ? 'new' : 'old';
                d.clear();
                d.set('old', 1, 30000);
                const before = d.ttl('old');
                const args = method === 'incr' ? [key, 2, 0] : [key, 2];
                if (!c.omit) {
                    args.push(c.timeout);
                }
                let error = '';
                try {
                    d[method].apply(d, args);
                } catch (e) {
                    error = e.name;
                }

                const ttl = d.ttl(key);
                let ok;
                if (c.reject) {
                    const after = d.ttl('old');
                    ok = error === 'TypeError' && d.get('old') === 1
                         && !d.has('new') && after <= before
                         && after >= before - 1000;
                } else {
                    const expected = c.ttl === undefined
                        ? (method === 'incr' ? 30000 : 60000) : c.ttl;
                    ok = error === ''
                         && d.get(key) === (method === 'incr' ? 3 : 2)
                         && ttl <= expected && ttl >= expected - 100;
                }
                results.push({name: method + ':' + c.name, ok, error, ttl,
                              upper: c.upper});
                d.set('probe', 1, 1000);
            }
        }
        return JSON.stringify(results);
    }

    function http(r) { r.return(200, check()); }
    export default {check, http};
EOF

$t->run()->plan(120);
my ($http) = http_get('/') =~ /\x0d\x0a\x0d\x0a(.*)/s;
my $connection = stream('127.0.0.1:' . port(8081));
my $stream = '';
my $chunk;
while (defined($chunk = $connection->read()) && length($chunk)) {
    $stream .= $chunk;
}

for my $response (['http', $http], ['stream', $stream]) {
    my $results = JSON::PP::decode_json($response->[1]);
    for my $result (@$results) {
        if ($result->{upper}) {
            is($result->{error}, '',
               "$response->[0] $result->{name} accepted");
        }

        SKIP: {
            skip 'upper-bound expiry needs 64-bit ngx_msec_t', 1
                if $result->{upper} && $Config{ptrsize} == 4;

            ok($result->{ok}, "$response->[0] $result->{name}")
                or diag(JSON::PP::encode_json($result));
        }
    }
}
