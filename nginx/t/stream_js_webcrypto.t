#!/usr/bin/perl

# (C) Dmitry Volyntsev
# (C) Nginx, Inc.

# Tests for stream njs module, WebCrypto module.

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

my $t = Test::Nginx->new()->has(qw/http stream stream_return/)
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

        location /has_crypto {
            js_content test.has_crypto;
        }
    }
}

stream {
    %%TEST_GLOBALS_STREAM%%

    js_import test.js;

    js_set $test test.random_values_test;

    server {
        listen  127.0.0.1:8081;
        return  $test;
    }

    server {
        listen  127.0.0.1:8082;
        js_preread test.test_ed25519;
        js_var $result;
        return $result;
    }

    server {
        listen  127.0.0.1:8083;
        js_preread test.test_x25519;
        js_var $result;
        return $result;
    }

    server {
        listen  127.0.0.1:8084;
        js_preread test.test_aes_kw;
        js_var $result;
        return $result;
    }
}

EOF

$t->write_file('test.js', <<EOF);
    function has_crypto(r) {
        r.return(200, (crypto !== undefined).toString());
    }

    function count1(v) {
        return v.toString(2).match(/1/g).length;
    }

    /*
     * Statistic test
     * bits1 is a random variable with Binomial distribution
     * Expected value is N / 2
     * Standard deviation is sqrt(N / 4)
     */

    function random_values_test(s) {
        let buf = new Uint32Array(32);
        crypto.getRandomValues(buf);
        let bits1 = buf.reduce((a, v)=> a + count1(v), 0);
        let nbits = buf.length * 32;
        let mean = nbits / 2;
        let stdd = Math.sqrt(nbits / 4);

        return bits1 > (mean - 10 * stdd) && bits1 < (mean + 10 * stdd);
    }

    async function ed25519() {
        let data = new Uint8Array(16);
        let ed = await crypto.subtle.generateKey("Ed25519", false,
                                                ["sign", "verify"]);
        let signature = await crypto.subtle.sign("Ed25519",
                                                 ed.privateKey, data);
        if (!await crypto.subtle.verify("Ed25519", ed.publicKey,
                                         signature, data))
        {
            throw Error("Ed25519 verification failed");
        }
    }

    async function x25519() {
        let x = await crypto.subtle.generateKey("X25519", false,
                                               ["deriveBits"]);
        let bits = await crypto.subtle.deriveBits(
            {name: "X25519", public: x.publicKey}, x.privateKey, 256);
        if (bits.byteLength !== 32) {
            throw Error("X25519 derivation failed");
        }
    }

    async function aes_kw() {
        let data = new Uint8Array(16);
        let key = await crypto.subtle.importKey("raw", data,
            "AES-GCM", true, ["encrypt"]);
        for (let length = 128; length <= 256; length += 64) {
            let kw = await crypto.subtle.generateKey(
                {name: "AES-KW", length}, false, ["wrapKey", "unwrapKey"]);
            let wrapped = await crypto.subtle.wrapKey("raw", key, kw,
                                                     "AES-KW");
            let unwrapped = await crypto.subtle.unwrapKey("raw", wrapped,
                kw, "AES-KW", "AES-GCM", true, ["encrypt"]);
            let raw = await crypto.subtle.exportKey("raw", unwrapped);
            if (Buffer.from(raw).compare(Buffer.from(data)) !== 0) {
                throw Error("AES-KW roundtrip failed");
            }
        }
    }

    async function algorithms(s, operation) {
        try {
            await operation();

            s.variables.result = "SUCCESS";
        } catch (e) {
            s.variables.result = e.message;
        }

        s.done();
    }

    async function test_ed25519(s) {
        await algorithms(s, ed25519);
    }

    async function test_x25519(s) {
        await algorithms(s, x25519);
    }

    async function test_aes_kw(s) {
        await algorithms(s, aes_kw);
    }

    export default {has_crypto, random_values_test,
                    test_ed25519, test_x25519, test_aes_kw};
EOF

$t->try_run('no stream js_var');

plan(skip_all => 'njs crypto module not available')
	if http_get('/has_crypto') !~ /true/;

$t->plan(4);

###############################################################################

is(stream('127.0.0.1:' . port(8081))->io('###'), 'true', 'random values test');

SKIP: {
	skip 'no 25519 support in SSL library', 2
		unless $t->has_feature('openssl:1.1.1')
		|| $t->has_feature('libressl:3.7.0')
		|| $t->has_module('BoringSSL|AWS-LC');

	is(stream('127.0.0.1:' . port(8082))->read(), 'SUCCESS', 'Ed25519');
	is(stream('127.0.0.1:' . port(8083))->read(), 'SUCCESS', 'X25519');
}

SKIP: {
	skip 'no AES-KW support in SSL library', 1
		unless $t->has_feature('openssl:1.0.2')
		|| $t->has_feature('libressl:3.0.0');

	is(stream('127.0.0.1:' . port(8084))->read(), 'SUCCESS', 'AES-KW');
}

###############################################################################
