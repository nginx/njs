#!/usr/bin/perl

# Tests for http njs module, WebCrypto generateCertificate function.

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

        location /rsa_cert_test {
            js_content test.rsa_cert_test;
        }

        location /ecdsa_cert_test {
            js_content test.ecdsa_cert_test;
        }

        location /cert_with_issuer_test {
            js_content test.cert_with_issuer_test;
        }

        location /cert_validation_test {
            js_content test.cert_validation_test;
        }
    }
}

EOF

$t->write_file('test.js', <<EOF);
    async function rsa_cert_test(r) {
        try {
            const keyPair = await crypto.subtle.generateKey(
                {
                    name: "RSASSA-PKCS1-v1_5",
                    hash: "SHA-256",
                    modulusLength: 2048,
                    publicExponent: new Uint8Array([1, 0, 1])
                },
                false,
                ["sign", "verify"]
            );
            
            const cert = await crypto.subtle.generateCertificate(
                {
                    subject: "CN=RSA Test Certificate",
                    serialNumber: "123"
                },
                keyPair
            );
            
            /* Test certificate is valid DER format and has reasonable size */
            const isValid = cert.byteLength > 500 && cert.byteLength < 2000;
            const bytes = new Uint8Array(cert);
            const derValid = bytes[0] === 0x30; /* DER SEQUENCE tag */
            
            r.return(200, isValid && derValid);
        } catch (error) {
            r.return(500, false);
        }
    }

    async function ecdsa_cert_test(r) {
        try {
            const keyPair = await crypto.subtle.generateKey(
                {
                    name: "ECDSA",
                    namedCurve: "P-256"
                },
                false,
                ["sign", "verify"]
            );
            
            const cert = await crypto.subtle.generateCertificate(
                {
                    subject: "CN=ECDSA Test Certificate",
                    serialNumber: "456"
                },
                keyPair
            );
            
            /* ECDSA certificates should be smaller than RSA */
            const isValid = cert.byteLength > 200 && cert.byteLength < 500;
            const bytes = new Uint8Array(cert);
            const derValid = bytes[0] === 0x30; /* DER SEQUENCE tag */
            
            r.return(200, isValid && derValid);
        } catch (error) {
            r.return(500, false);
        }
    }

    async function cert_with_issuer_test(r) {
        try {
            const keyPair = await crypto.subtle.generateKey(
                {
                    name: "RSASSA-PKCS1-v1_5",
                    hash: "SHA-256",
                    modulusLength: 2048,
                    publicExponent: new Uint8Array([1, 0, 1])
                },
                false,
                ["sign", "verify"]
            );
            
            const cert = await crypto.subtle.generateCertificate(
                {
                    subject: "CN=client.example.com",
                    issuer: "CN=Example CA",
                    serialNumber: "789"
                },
                keyPair
            );
            
            /* Test certificate with issuer is created successfully */
            const isValid = cert.byteLength > 500 && cert.byteLength < 2000;
            const bytes = new Uint8Array(cert);
            const derValid = bytes[0] === 0x30; /* DER SEQUENCE tag */
            
            r.return(200, isValid && derValid);
        } catch (error) {
            r.return(500, false);
        }
    }

    async function cert_validation_test(r) {
        try {
            const keyPair = await crypto.subtle.generateKey(
                {
                    name: "RSASSA-PKCS1-v1_5",
                    hash: "SHA-256",
                    modulusLength: 2048,
                    publicExponent: new Uint8Array([1, 0, 1])
                },
                false,
                ["sign", "verify"]
            );
            
            const cert = await crypto.subtle.generateCertificate(
                {
                    subject: "CN=Validation Test Certificate",
                    serialNumber: "validation-123",
                    notBefore: Date.now(),
                    notAfter: Date.now() + (365 * 24 * 60 * 60 * 1000)
                },
                keyPair
            );
            
            /* Test certificate with validity period */
            const isValid = cert.byteLength > 500 && cert.byteLength < 2000;
            const bytes = new Uint8Array(cert);
            const derValid = bytes[0] === 0x30; /* DER SEQUENCE tag */
            
            r.return(200, isValid && derValid);
        } catch (error) {
            r.return(500, false);
        }
    }

    export default {rsa_cert_test, ecdsa_cert_test, cert_with_issuer_test,
                    cert_validation_test};

EOF

$t->try_run('no njs')->plan(4);

###############################################################################

like(http_get('/rsa_cert_test'), qr/true/, 'rsa_cert_test');
like(http_get('/ecdsa_cert_test'), qr/true/, 'ecdsa_cert_test');
like(http_get('/cert_with_issuer_test'), qr/true/, 'cert_with_issuer_test');
like(http_get('/cert_validation_test'), qr/true/, 'cert_validation_test');

###############################################################################