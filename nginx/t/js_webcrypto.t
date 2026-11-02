#!/usr/bin/perl

# (C) Dmitry Volyntsev
# (C) Nginx, Inc.

# Tests for http njs module, WebCrypto module.

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

    server {
        listen       127.0.0.1:8080;
        server_name  localhost;

        location /has_crypto {
            js_content test.has_crypto;
        }

        location /random_values_test {
            js_content test.random_values_test;
        }

        location /algorithms {
            js_content test.algorithms;
        }
    }
}

EOF

$t->write_file('test.js', <<EOF);
    function has_crypto(r) {
        r.return(200, (typeof crypto !== 'undefined').toString());
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

    function random_values_test(r) {
        let buf = new Uint32Array(32);
        crypto.getRandomValues(buf);
        let bits1 = buf.reduce((a, v)=> a + count1(v), 0);
        let nbits = buf.length * 32;
        let mean = nbits / 2;
        let stdd = Math.sqrt(nbits / 4);

        r.return(200, bits1 > (mean - 10 * stdd) && bits1 < (mean + 10 * stdd));
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

    async function algorithms(r) {
        try {
            switch (r.args.algorithm) {
            case "Ed25519":
                await ed25519();
                break;
            case "X25519":
                await x25519();
                break;
            case "AES-KW":
                await aes_kw();
                break;
            default:
                throw Error("unknown test algorithm");
            }

            r.return(200, "SUCCESS");
        } catch (e) {
            r.return(500, e.message);
        }
    }

    export default {has_crypto, random_values_test, algorithms};

EOF

$t->run();

my $response = http_get('/has_crypto');
my ($has_crypto) = $response =~ /\x0d\x0a\x0d\x0a(true|false)\z/;

die "invalid WebCrypto capability response\n"
	unless $response =~ /^HTTP\/1\.[01] 200 / && defined $has_crypto;

if ($has_crypto eq 'false') {
	die "WebCrypto is unavailable in an SSL-enabled CI job\n"
		if defined $ENV{CI_SSL} && $ENV{CI_SSL} eq 'yes';

	plan(skip_all => 'njs crypto module not available');
}

$t->plan(4);

###############################################################################

like(http_get('/random_values_test'), qr/true/, 'random_values_test');

SKIP: {
	skip 'no 25519 support in SSL library', 2
		unless $t->has_feature('openssl:1.1.1')
		|| $t->has_feature('libressl:3.7.0')
		|| $t->has_module('BoringSSL|AWS-LC');

	like(http_get('/algorithms?algorithm=Ed25519'), qr/SUCCESS/, 'Ed25519');
	like(http_get('/algorithms?algorithm=X25519'), qr/SUCCESS/, 'X25519');
}

SKIP: {
	skip 'no AES-KW support in SSL library', 1
		unless $t->has_feature('openssl:1.0.2')
		|| $t->has_feature('libressl:3.0.0');

	like(http_get('/algorithms?algorithm=AES-KW'), qr/SUCCESS/, 'AES-KW');
}

###############################################################################
