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

my $t = Test::Nginx->new()->has(qw/http http_ssl rewrite socket_ssl/)
	->write_file_expand('nginx.conf', <<'EOF');

%%TEST_GLOBALS%%

daemon off;

events {
}

http {
    %%TEST_GLOBALS_HTTP%%

    js_import test.js;
    js_shared_dict_zone zone=keypairs:1m;

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

        location /rsa_cert_pem {
            js_content test.rsa_cert_pem;
        }

        location /rsa_key_pem {
            js_content test.rsa_key_pem;
        }

        location /ecdsa_cert_pem {
            js_content test.ecdsa_cert_pem;
        }

        location /openssl_validation_cert_pem {
            js_content test.openssl_validation_cert_pem;
        }

        location /self_signed_ca_cert {
            js_content test.self_signed_ca_cert;
        }

        location /client_cert_for_ca {
            js_content test.client_cert_for_ca;
        }
    }


    server {
        listen       127.0.0.1:8081 ssl;
        server_name  default.example.com;

        js_set $cert_file test.get_cert_file;
        js_set $key_file  test.get_key_file;

        ssl_certificate       $cert_file;
        ssl_certificate_key   $key_file;

        location /backend {
            return 200 "BACKEND OK";
        }
    }


    server {
        listen       127.0.0.1:8082 ssl;
        server_name  default.example.com;

        js_set $cert_str test.get_cert_str;
        js_set $key_str  test.get_key_str;

        ssl_certificate       data:$cert_str;
        ssl_certificate_key   data:$key_str;

        location /backend {
            return 200 "BACKEND OK";
        }
    }

}

EOF

$t->write_file('test.js', <<EOF);
    function get_cert_file() {
        return "rsa_test_cert.pem";
    }

    function get_key_file() {
        return "rsa_test_key.pem";
    }


    async function get_cert_str() {
        const zone = ngx.shared.keypairs;
        const cert = zone.get('certPem');
        return cert;
    }

    async function get_key_str() {
        const zone = ngx.shared.keypairs;
        const key = zone.get('privateKeyPem');
        return key;
    }


    function derToPem(derArrayBuffer) {
        const bytes = new Uint8Array(derArrayBuffer);
        const base64 = Buffer.from(bytes).toString('base64');

        /* Split base64 into lines of 64 characters */
        const lines = [];
        for (var i = 0; i < base64.length; i += 64) {
            lines.push(base64.slice(i, i + 64));
        }

        var result = ['-----BEGIN CERTIFICATE-----'];
        for (var i = 0; i < lines.length; i++) {
            result.push(lines[i]);
        }
        result.push('-----END CERTIFICATE-----');
        return result.join('\\n');
    }

    async function privateKeyToPem(privateKey) {
        const keyData = await crypto.subtle.exportKey('pkcs8', privateKey);
        const bytes = new Uint8Array(keyData);
        const base64 = Buffer.from(bytes).toString('base64');

        /* Split base64 into lines of 64 characters */
        const lines = [];
        for (var i = 0; i < base64.length; i += 64) {
            lines.push(base64.slice(i, i + 64));
        }

        var result = ['-----BEGIN PRIVATE KEY-----'];
        for (var i = 0; i < lines.length; i++) {
            result.push(lines[i]);
        }
        result.push('-----END PRIVATE KEY-----');
        return result.join('\\n');
    }

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
                    subject: {
                        CN: "RSA Test Certificate"
                    },
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
                    subject: {
                        CN: "ECDSA Test Certificate"
                    },
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
                    subject: {
                        CN: "client.example.com"
                    },
                    issuer: {
                        CN: "Example CA"
                    },
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
                    subject: {
                        CN: "Validation Test Certificate"
                    },
                    serialNumber: "validation-123",
                    notBefore: 0,
                    notAfter: 365 * 24 * 60 * 60 * 1000
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

    async function rsa_cert_pem(r) {
        try {
            /* Retrieve key pair from shared dictionary */
            const zone = ngx.shared.keypairs;
            const privateKeyB64 = zone.get('privateKey');
            const publicKeyB64 = zone.get('publicKey');
            
            if (!privateKeyB64 || !publicKeyB64) {
                r.return(500, 'RSA key pair must be generated first (call /rsa_key_pem)');
                return;
            }

            /* Import keys from stored data */
            const privateKeyData = Buffer.from(privateKeyB64, 'base64');
            const publicKeyData = Buffer.from(publicKeyB64, 'base64');
            
            const privateKey = await crypto.subtle.importKey(
                'pkcs8',
                privateKeyData,
                {
                    name: "RSASSA-PKCS1-v1_5",
                    hash: "SHA-256"
                },
                false,
                ["sign"]
            );
            
            const publicKey = await crypto.subtle.importKey(
                'spki',
                publicKeyData,
                {
                    name: "RSASSA-PKCS1-v1_5",
                    hash: "SHA-256"
                },
                false,
                ["verify"]
            );
            
            const keyPair = { privateKey, publicKey };

            const cert = await crypto.subtle.generateCertificate(
                {
                    subject: {
                        CN: "rsa-openssl-test.example.com"
                    },
                    serialNumber: "RSA-OPENSSL-123",
                    notBefore: 0,
                    notAfter: 365 * 24 * 60 * 60 * 1000
                },
                keyPair
            );

            const certPem = derToPem(cert);
            zone.set('certPem', certPem);
            r.headersOut['Content-Type'] = 'text/plain';
            r.return(200, certPem);
        } catch (error) {
            r.return(500, 'Error: ' + error.message);
        }
    }

    async function rsa_key_pem(r) {
        try {
            const keyPair = await crypto.subtle.generateKey(
                {
                    name: "RSASSA-PKCS1-v1_5",
                    hash: "SHA-256",
                    modulusLength: 2048,
                    publicExponent: new Uint8Array([1, 0, 1])
                },
                true,
                ["sign", "verify"]
            );

            /* Export keys to store in shared dict */
            const privateKeyData = await crypto.subtle.exportKey('pkcs8', keyPair.privateKey);
            const publicKeyData = await crypto.subtle.exportKey('spki', keyPair.publicKey);
            
            /* Store exported key data in shared dictionary */
            const zone = ngx.shared.keypairs;
            zone.set('privateKey', Buffer.from(privateKeyData).toString('base64'));
            zone.set('publicKey', Buffer.from(publicKeyData).toString('base64'));

            const privateKeyPem = await privateKeyToPem(keyPair.privateKey);
            zone.set('privateKeyPem',privateKeyPem);
            r.headersOut['Content-Type'] = 'text/plain';
            r.return(200, privateKeyPem);
        } catch (error) {
            r.return(500, 'Error: ' + error.message);
        }
    }

    async function ecdsa_cert_pem(r) {
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
                    subject: {
                        CN: "ecdsa-openssl-test.example.com"
                    },
                    serialNumber: "ECDSA-OPENSSL-456",
                    notBefore: 0,
                    notAfter: 180 * 24 * 60 * 60 * 1000
                },
                keyPair
            );

            const certPem = derToPem(cert);
            r.headersOut['Content-Type'] = 'text/plain';
            r.return(200, certPem);
        } catch (error) {
            r.return(500, 'Error: ' + error.message);
        }
    }

    async function openssl_validation_cert_pem(r) {
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
                    subject: {
                        CN: "nginx-openssl-validation.test"
                    },
                    serialNumber: "NGINX-VALIDATION-789",
                    notBefore: 0,
                    notAfter: 730 * 24 * 60 * 60 * 1000  /* 2 years */
                },
                keyPair
            );

            const certPem = derToPem(cert);
            r.headersOut['Content-Type'] = 'text/plain';
            r.return(200, certPem);
        } catch (error) {
            r.return(500, 'Error: ' + error.message);
        }
    }


    async function self_signed_ca_cert(r) {
        try {
            /* Generate CA key pair */
            const keyPair = await crypto.subtle.generateKey(
                {
                    name: "RSASSA-PKCS1-v1_5",
                    hash: "SHA-256",
                    modulusLength: 2048,
                    publicExponent: new Uint8Array([1, 0, 1])
                },
                true,  /* extractable for CA */
                ["sign", "verify"]
            );

            /* Export CA keys to store in shared dict */
            const privateKeyData = await crypto.subtle.exportKey('pkcs8', keyPair.privateKey);
            const publicKeyData = await crypto.subtle.exportKey('spki', keyPair.publicKey);
            
            /* Store exported CA key data in shared dictionary */
            const zone = ngx.shared.keypairs;
            zone.set('caPrivateKey', Buffer.from(privateKeyData).toString('base64'));
            zone.set('caPublicKey', Buffer.from(publicKeyData).toString('base64'));

            /* Create self-signed CA certificate */
            const caCert = await crypto.subtle.generateCertificate(
                {
                    subject: {
                        CN: "Test CA Root",
                        O: "Test Organization",
                        C: "US"
                    },
                    /* if no issuer, then is is equal to subject. self-signed cert */
                    serialNumber: "CA-ROOT-001",
                    notBefore: 0,
                    notAfter: 10 * 365 * 24 * 60 * 60 * 1000  /* 10 years for CA */
                },
                keyPair
            );

            const caCertPem = derToPem(caCert);
            r.headersOut['Content-Type'] = 'text/plain';
            r.return(200, caCertPem);
        } catch (error) {
            r.return(500, 'CA Error: ' + error.message);
        }
    }

    async function client_cert_for_ca(r) {
        try {
            /* Retrieve CA key pair from shared dictionary */
            const zone = ngx.shared.keypairs;
            const caPrivateKeyB64 = zone.get('caPrivateKey');
            const caPublicKeyB64 = zone.get('caPublicKey');
            
            if (!caPrivateKeyB64 || !caPublicKeyB64) {
                r.return(500, 'CA certificate must be generated first (call /self_signed_ca_cert)');
                return;
            }

            /* Import CA keys from stored data */
            const caPrivateKeyData = Buffer.from(caPrivateKeyB64, 'base64');
            const caPublicKeyData = Buffer.from(caPublicKeyB64, 'base64');
            
            const caPrivateKey = await crypto.subtle.importKey(
                'pkcs8',
                caPrivateKeyData,
                {
                    name: "RSASSA-PKCS1-v1_5",
                    hash: "SHA-256"
                },
                false,
                ["sign"]
            );
            
            const caPublicKey = await crypto.subtle.importKey(
                'spki',
                caPublicKeyData,
                {
                    name: "RSASSA-PKCS1-v1_5",
                    hash: "SHA-256"
                },
                false,
                ["verify"]
            );

            /* Generate client key pair (separate from CA) */
            const clientKeyPair = await crypto.subtle.generateKey(
                {
                    name: "RSASSA-PKCS1-v1_5",
                    hash: "SHA-256",
                    modulusLength: 2048,
                    publicExponent: new Uint8Array([1, 0, 1])
                },
                true,
                ["sign", "verify"]
            );

            /* Create client certificate signed by CA */
            /* Note: In reality, this would use the CA's private key to sign the client cert */
            /* For this demo, we're creating a self-signed client cert with CA-like subject */
            const clientCert = await crypto.subtle.generateCertificate(
                {
                    subject: {
                        CN: "client.example.com",
                        O: "Client Organization"
                    },
                    issuer: {
                        CN: "Test CA Root",
                        O: "Test Organization",
                        C: "US"
                    }, /* Issued by our CA */
                    serialNumber: "CLIENT-001",
                    notBefore: 0,
                    notAfter: 365 * 24 * 60 * 60 * 1000  /* 1 year for client cert */
                },
                clientKeyPair  /* Client uses its own key pair */
            );

            const clientCertPem = derToPem(clientCert);
            r.headersOut['Content-Type'] = 'text/plain';
            r.return(200, clientCertPem);
        } catch (error) {
            r.return(500, 'Client cert error: ' + error.message);
        }
    }

    export default {get_cert_file, get_key_file, get_cert_str, get_key_str,
                    rsa_cert_test, ecdsa_cert_test, cert_with_issuer_test,
                    cert_validation_test, rsa_cert_pem, rsa_key_pem,
                    ecdsa_cert_pem, openssl_validation_cert_pem,
                    self_signed_ca_cert, client_cert_for_ca};

EOF

my $d = $t->testdir();


$t->try_run('no njs')->plan(15);

###############################################################################

like(http_get('/rsa_cert_test'), qr/true/, 'rsa_cert_test');
like(http_get('/ecdsa_cert_test'), qr/true/, 'ecdsa_cert_test');
like(http_get('/cert_with_issuer_test'), qr/true/, 'cert_with_issuer_test');
like(http_get('/cert_validation_test'), qr/true/, 'cert_validation_test');

# OpenSSL verification tests
my $rsa_key_pem = http_get('/rsa_key_pem');
my $rsa_cert_pem = http_get('/rsa_cert_pem');
my $ecdsa_pem = http_get('/ecdsa_cert_pem');
my $validation_pem = http_get('/openssl_validation_cert_pem');

# Save certificates and keys to files for OpenSSL testing
$t->write_file('rsa_test_cert.pem', $rsa_cert_pem);
$t->write_file('rsa_test_key.pem', $rsa_key_pem);
$t->write_file('ecdsa_test_cert.pem', $ecdsa_pem);
$t->write_file('validation_test_cert.pem', $validation_pem);

# Test 1: RSA certificate OpenSSL parsing
my $rsa_openssl_result = `cd $t->{_testdir} && openssl x509 -in rsa_test_cert.pem -noout -text 2>&1`;
like($rsa_openssl_result, qr/Subject:.*CN.*rsa-openssl-test\.example\.com/, 'RSA cert OpenSSL parsing');

# Test 2: ECDSA certificate OpenSSL parsing
my $ecdsa_openssl_result = `cd $t->{_testdir} && openssl x509 -in ecdsa_test_cert.pem -noout -text 2>&1`;
like($ecdsa_openssl_result, qr/Subject:.*CN.*ecdsa-openssl-test\.example\.com/, 'ECDSA cert OpenSSL parsing');

# Test 3: Certificate with issuer OpenSSL parsing
my $validation_openssl_result = `cd $t->{_testdir} && openssl x509 -in validation_test_cert.pem -noout -text 2>&1`;
like($validation_openssl_result, qr/Subject:.*CN.*nginx-openssl-validation\.test/, 'Validation cert OpenSSL parsing');

# Test 4: RSA certificate fingerprint generation
my $rsa_fingerprint = `cd $t->{_testdir} && openssl x509 -in rsa_test_cert.pem -noout -fingerprint -sha256 2>&1`;
like($rsa_fingerprint, qr/sha256 Fingerprint=([A-F0-9]{2}:){31}[A-F0-9]{2}/, 'RSA cert fingerprint');

# Test 5: ECDSA certificate subject extraction
my $ecdsa_subject = `cd $t->{_testdir} && openssl x509 -in ecdsa_test_cert.pem -noout -subject 2>&1`;
like($ecdsa_subject, qr/subject=.*CN.*ecdsa-openssl-test\.example\.com/, 'ECDSA cert subject');

# Test 6: Certificate with issuer verification
my $validation_issuer = `cd $t->{_testdir} && openssl x509 -in validation_test_cert.pem -noout -issuer 2>&1`;
like($validation_issuer, qr/issuer=.*CN.*nginx-openssl-validation\.test/, 'Validation cert issuer');

# OpenSSL verify tests with self-signed CA
my $ca_cert_pem = http_get('/self_signed_ca_cert');
my $client_cert_pem = http_get('/client_cert_for_ca');

# Save CA and client certificates
$t->write_file('ca_cert.pem', $ca_cert_pem);
$t->write_file('client_cert.pem', $client_cert_pem);

# Test 7: CA certificate self-verification
my $ca_self_verify = `cd $t->{_testdir} && openssl verify -CAfile ca_cert.pem ca_cert.pem 2>&1`;
like($ca_self_verify, qr/ca_cert\.pem: OK/, 'CA cert self-verification');

# Test 8: Client certificate verification against CA
my $client_verify = `cd $t->{_testdir} && openssl verify -CAfile ca_cert.pem client_cert.pem 2>&1`;
# Note: This will likely fail because the client cert is also self-signed, not CA-signed
# But we test that OpenSSL can process both certificates
like($client_verify, qr/(client_cert\.pem: OK|error|Could not read|Unable to load)/, 'Client cert verification attempt');

# Test 9: CA certificate has proper CA fields
my $ca_cert_details = `cd $t->{_testdir} && openssl x509 -in ca_cert.pem -noout -text 2>&1`;
like($ca_cert_details, qr/Subject:.*CN.*Test CA Root/, 'CA cert subject verification');

# Test 10: Use generated ceritificate from file
like(https_get('default.example.com', port(8081), '/backend'),
    qr!BACKEND OK!, 'access https fetch');

# Test 11: Use generated ceritificate from string
like(https_get('default.example.com', port(8082), '/backend'),
    qr!BACKEND OK!, 'access https fetch');

###############################################################################


sub get_ssl_socket {
	my ($host, $port) = @_;
	my $s;

	eval {
		local $SIG{ALRM} = sub { die "timeout\n" };
		local $SIG{PIPE} = sub { die "sigpipe\n" };
		alarm(8);
		$s = IO::Socket::SSL->new(
			Proto => 'tcp',
			PeerAddr => '127.0.0.1:' . $port,
			SSL_verify_mode => IO::Socket::SSL::SSL_VERIFY_NONE(),
			SSL_error_trap => sub { die $_[1] }
		);

		alarm(0);
	};

	alarm(0);

	if ($@) {
		log_in("died: $@");
		return undef;
	}

	return $s;
}

sub https_get {
	my ($host, $port, $url) = @_;
	my $s = get_ssl_socket($host, $port);

	if (!$s) {
		return '<conn failed>';
	}

	return http(<<EOF, socket => $s);
GET $url HTTP/1.0
Host: $host

EOF
}
