#!/usr/bin/perl

# (C) Dmitry Volyntsev
# (C) F5, Inc.

# Tests for exact shared dictionary number persistence.

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
    js_shared_dict_zone zone=http_nums:512k type=number state=http.json;

    server {
        listen 127.0.0.1:8080;
        location /fill { js_content test.http_fill; }
        location /check { js_content test.http_check; }
    }
}

stream {
    %%TEST_GLOBALS_STREAM%%
    js_import test.js;
    js_shared_dict_zone zone=stream_nums:512k type=number state=stream.json;
    js_set $fill test.fill;
    js_set $check test.check;

    server {
        listen 127.0.0.1:8081;
        return $fill;
    }
    server {
        listen 127.0.0.1:8082;
        return $check;
    }
}
EOF

$t->write_file('test.js', <<'EOF');
    const values = {zero: 0, negative_zero: -0, fraction: 0.1,
        pi: Math.PI, tiny: 1e-7, minimum: Number.MIN_VALUE,
        maximum: Number.MAX_VALUE, integer: Number.MAX_SAFE_INTEGER};

    function fill(s) {
        const d = ngx.shared[s ? 'stream_nums' : 'http_nums'];
        Object.keys(values).forEach(key => d.set(key, values[key]));
        return 'filled';
    }

    function check(s) {
        const d = ngx.shared[s ? 'stream_nums' : 'http_nums'];
        return JSON.stringify(Object.keys(values).map(key => ({key,
            ok: Object.is(d.get(key), values[key]),
            value: String(d.get(key))})));
    }

    function http_fill(r) { r.return(200, fill()); }
    function http_check(r) { r.return(200, check()); }
    export default {fill, check, http_fill, http_check};
EOF

$t->run()->plan(34);

http_get('/fill');
stream('127.0.0.1:' . port(8081))->read();
check_values('before restart');

$t->stop();

for my $file ('http.json', 'stream.json') {
    my $state = $t->read_file($file);
    my $valid = eval { JSON::PP::decode_json($state); 1 };
    ok($valid, "$file is valid JSON") or diag($state);
}

$t->run();
check_values('after restart');

sub check_values {
    my ($phase) = @_;
    my ($http) = http_get('/check') =~ /\x0d\x0a\x0d\x0a(.*)/s;
    my $stream = stream('127.0.0.1:' . port(8082))->read();

    for my $response (['http', $http], ['stream', $stream]) {
        my $results = JSON::PP::decode_json($response->[1]);
        for my $result (@$results) {
            ok($result->{ok}, "$response->[0] $result->{key} $phase")
                or diag(JSON::PP::encode_json($result));
        }
    }
}
