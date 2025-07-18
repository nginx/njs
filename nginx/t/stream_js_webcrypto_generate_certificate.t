#!/usr/bin/perl

# Tests for stream njs module, WebCrypto generateCertificate function.

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

my $t = Test::Nginx->new()->has(qw/http stream stream_return/)
	->write_file_expand('nginx.conf', <<'EOF');

%%TEST_GLOBALS%%

daemon off;

events {
}

stream {
    %%TEST_GLOBALS_STREAM%%

    js_import test.js;

    server {
        listen      127.0.0.1:8081;
        js_preread  test.stream_cert_test;
        return      $cert_result;
    }
}

EOF

$t->write_file('test.js', <<EOF);
    async function stream_cert_test(s) {
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
                    subject: "CN=Stream Test Certificate",
                    serialNumber: "stream-123"
                },
                keyPair
            );
            
            /* Test certificate is valid DER format and has reasonable size */
            const isValid = cert.byteLength > 500 && cert.byteLength < 2000;
            const bytes = new Uint8Array(cert);
            const derValid = bytes[0] === 0x30; /* DER SEQUENCE tag */
            
            s.variables.cert_result = isValid && derValid ? "PASS" : "FAIL";
        } catch (error) {
            s.variables.cert_result = "ERROR";
        }
    }

    export default {stream_cert_test};

EOF

$t->try_run('no njs')->plan(1);

###############################################################################

like(http_get('/', socket => getconn('127.0.0.1:8081')), qr/PASS/, 'stream_cert_test');

###############################################################################

sub getconn {
    my $peer = shift;
    my $s = IO::Socket::INET->new(
        Proto => "tcp",
        PeerAddr => $peer
    ) or die "Can't connect to $peer: $!";

    return $s;
}

###############################################################################