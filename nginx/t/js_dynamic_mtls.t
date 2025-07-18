#!/usr/bin/perl

# Tests for http njs module, Dynamic mTLS Certificate Generation.

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

        location /ca_init_test {
            js_content test.ca_init_test;
        }

        location /client_cert_generation_test {
            js_content test.client_cert_generation_test;
        }

        location /cert_cache_test {
            js_content test.cert_cache_test;
        }

        location /der_to_pem_test {
            js_content test.der_to_pem_test;
        }

        location /multiple_client_types_test {
            js_content test.multiple_client_types_test;
        }

        location /certificate_validation_test {
            js_content test.certificate_validation_test;
        }
    }
}

EOF

$t->write_file('test.js', <<EOF);

    // Helper function to satisfy qjs
    function btoa (b) {
        return Buffer.from(b).toString('base64');
    }

    // Helper function to convert ArrayBuffer to base64
    function arrayBufferToBase64(buffer) {
        const bytes = new Uint8Array(buffer);
        var binary = '';
        for (var i = 0; i < bytes.byteLength; i++) {
            binary += String.fromCharCode(bytes[i]);
        }
        return btoa(binary);
    }

    // Helper function to convert DER to PEM format
    function derToPem(derArrayBuffer, type) {
        if (type === undefined) {
            type = 'CERTIFICATE';
        }
        const base64 = arrayBufferToBase64(derArrayBuffer);
        
        // Split base64 into lines of 64 characters
        const lines = [];
        for (var i = 0; i < base64.length; i += 64) {
            lines.push(base64.slice(i, i + 64));
        }
        
        var result = [`-----BEGIN \${type}-----`];
        for (var i = 0; i < lines.length; i++) {
            result.push(lines[i]);
        }
        result.push(`-----END \${type}-----`);
        return result.join('\\n');
    }

    // Cache for generated certificates
    var certificateCache = {};

    // CA certificate and key pair
    var caKeyPair = null;
    var caCertificate = null;

    // Initialize CA certificate
    async function initializeCertificateAuthority() {
        // Generate CA key pair
        caKeyPair = await crypto.subtle.generateKey(
            {
                name: "RSASSA-PKCS1-v1_5",
                hash: "SHA-256",
                modulusLength: 2048,
                publicExponent: new Uint8Array([1, 0, 1])
            },
            false,
            ["sign", "verify"]
        );
        
        // Generate CA certificate
        caCertificate = await crypto.subtle.generateCertificate(
            {
                subject: "CN=NGINX Internal CA,O=Test Organization,C=US",
                serialNumber: "ca-root-001",
                notBefore: Date.now(),
                notAfter: Date.now() + (10 * 365 * 24 * 60 * 60 * 1000) // 10 years
            },
            caKeyPair
        );
    }

    // Generate client certificate
    async function generateClientCertificate(clientId, clientType, validityDays) {
        if (validityDays === undefined) {
            validityDays = 90;
        }
        
        // Check cache first
        const cacheKey = `\${clientId}-\${clientType}`;
        if (certificateCache[cacheKey]) {
            return certificateCache[cacheKey];
        }
        
        // Generate new client key pair
        const clientKeyPair = await crypto.subtle.generateKey(
            {
                name: "ECDSA",
                namedCurve: "P-256"
            },
            false,
            ["sign", "verify"]
        );
        
        // Generate client certificate
        const clientCert = await crypto.subtle.generateCertificate(
            {
                subject: `CN=\${clientId}.\${clientType}.internal`,
                issuer: "CN=NGINX Internal CA,O=Test Organization,C=US",
                serialNumber: `client-\${Date.now()}-\${Math.floor(Math.random() * 1000)}`,
                notBefore: Date.now(),
                notAfter: Date.now() + (validityDays * 24 * 60 * 60 * 1000)
            },
            clientKeyPair
        );
        
        const certData = {
            certificate: clientCert,
            keyPair: clientKeyPair,
            pem: derToPem(clientCert),
            generatedAt: Date.now(),
            validityDays: validityDays
        };
        
        // Cache the certificate
        certificateCache[cacheKey] = certData;
        
        return certData;
    }

    async function ca_init_test(r) {
        try {
            await initializeCertificateAuthority();
            
            // Test CA certificate is valid
            const isValid = caCertificate && caCertificate.byteLength > 500 && caCertificate.byteLength < 2000;
            const bytes = new Uint8Array(caCertificate);
            const derValid = bytes[0] === 0x30; // DER SEQUENCE tag
            
            r.return(200, isValid && derValid);
        } catch (error) {
            r.return(500, false);
        }
    }

    async function client_cert_generation_test(r) {
        try {
            if (!caCertificate) {
                await initializeCertificateAuthority();
            }
            
            const certData = await generateClientCertificate('test-client', 'mobile', 30);
            
            // Test client certificate is valid
            const isValid = certData.certificate.byteLength > 200 && certData.certificate.byteLength < 1000;
            const bytes = new Uint8Array(certData.certificate);
            const derValid = bytes[0] === 0x30; // DER SEQUENCE tag
            const hasPem = certData.pem.includes('-----BEGIN CERTIFICATE-----');
            
            r.return(200, isValid && derValid && hasPem);
        } catch (error) {
            r.return(500, false);
        }
    }

    async function cert_cache_test(r) {
        try {
            if (!caCertificate) {
                await initializeCertificateAuthority();
            }
            
            // Generate first certificate
            const cert1 = await generateClientCertificate('cache-test', 'api', 60);
            
            // Generate same certificate again (should be cached)
            const cert2 = await generateClientCertificate('cache-test', 'api', 60);
            
            // Test that cached certificate is returned
            const sameReference = cert1 === cert2;
            const sameGeneratedAt = cert1.generatedAt === cert2.generatedAt;
            
            r.return(200, sameReference && sameGeneratedAt);
        } catch (error) {
            r.return(500, false);
        }
    }

    async function der_to_pem_test(r) {
        try {
            if (!caCertificate) {
                await initializeCertificateAuthority();
            }
            
            const pem = derToPem(caCertificate);
            
            // Test PEM format
            const hasBeginMarker = pem.includes('-----BEGIN CERTIFICATE-----');
            const hasEndMarker = pem.includes('-----END CERTIFICATE-----');
            const hasBase64 = pem.length > 100;
            
            r.return(200, hasBeginMarker && hasEndMarker && hasBase64);
        } catch (error) {
            r.return(500, false);
        }
    }

    async function multiple_client_types_test(r) {
        try {
            if (!caCertificate) {
                await initializeCertificateAuthority();
            }
            
            // Generate certificates for different client types
            const scenarios = [
                { clientId: 'mobile-app', clientType: 'mobile', validityDays: 30 },
                { clientId: 'web-app', clientType: 'web', validityDays: 90 },
                { clientId: 'iot-device', clientType: 'iot', validityDays: 180 }
            ];
            
            var allValid = true;
            for (var i = 0; i < scenarios.length; i++) {
                const scenario = scenarios[i];
                const certData = await generateClientCertificate(
                    scenario.clientId,
                    scenario.clientType,
                    scenario.validityDays
                );
                
                const isValid = certData.certificate.byteLength > 200 && certData.certificate.byteLength < 1000;
                const bytes = new Uint8Array(certData.certificate);
                const derValid = bytes[0] === 0x30; // DER SEQUENCE tag
                
                if (!isValid || !derValid) {
                    allValid = false;
                    break;
                }
            }
            
            // Test that cache contains all generated certificates
            const cacheSize = Object.keys(certificateCache).length;
            
            r.return(200, allValid && cacheSize >= 3);
        } catch (error) {
            r.return(500, false);
        }
    }

    async function certificate_validation_test(r) {
        try {
            if (!caCertificate) {
                await initializeCertificateAuthority();
            }
            
            const certData = await generateClientCertificate('validation-test', 'service', 365);
            
            // Test certificate properties
            const hasValidCert = certData.certificate && certData.certificate.byteLength > 0;
            const hasValidPem = certData.pem && certData.pem.includes('-----BEGIN CERTIFICATE-----');
            const hasValidTimestamp = certData.generatedAt && certData.generatedAt > 0;
            const hasValidValidity = certData.validityDays === 365;
            
            r.return(200, hasValidCert && hasValidPem && hasValidTimestamp && hasValidValidity);
        } catch (error) {
            r.return(500, false);
        }
    }

    export default {ca_init_test, client_cert_generation_test, cert_cache_test,
                    der_to_pem_test, multiple_client_types_test, certificate_validation_test};

EOF

$t->try_run('no njs')->plan(6);

###############################################################################

like(http_get('/ca_init_test'), qr/true/, 'ca_init_test');
like(http_get('/client_cert_generation_test'), qr/true/, 'client_cert_generation_test');
like(http_get('/cert_cache_test'), qr/true/, 'cert_cache_test');
like(http_get('/der_to_pem_test'), qr/true/, 'der_to_pem_test');
like(http_get('/multiple_client_types_test'), qr/true/, 'multiple_client_types_test');
like(http_get('/certificate_validation_test'), qr/true/, 'certificate_validation_test');

###############################################################################