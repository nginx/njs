==========
XML tests
==========

SAML signing
============

Generating SAML signed AuthnRequest
-----------------------------------

Note: XMLSec library is used (https://www.aleksey.com/xmlsec/).

.. code-block:: shell

  xmlsec1 --sign  --pkcs8-pem test/webcrypto/rsa.pkcs8 \
    --output test/xml/<template>_signed.xml  --id-attr:ID AuthnRequest test/xml/<template>.xml
  xmlsec1 --sign  --pkcs8-pem test/webcrypto/rsa2.pkcs8 \
    --output test/xml/<template>_signed2.xml  --id-attr:ID AuthnRequest test/xml/<template>.xml

Generating X509 self-signed certificate with an existing RSA key
----------------------------------------------------------------

.. code-block:: shell

   openssl req -x509 -key test/webcrypto/rsa.pkcs8
     -out test/xml/example.com.crt -sha256 -days 3650 -subj '/CN=example.com'
