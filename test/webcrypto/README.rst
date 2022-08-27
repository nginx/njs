===============
WebCrypto tests
===============

Intro
=====

Tests in this folder are expected to be compatible with node.js

Tested versions
---------------

node: v16.4.0
openssl: OpenSSL 1.1.1f  31 Mar 2020

Keys generation
===============

Generating RSA PKCS8/SPKI key files
-----------------------------------

.. code-block:: shell

  openssl genrsa -out rsa.pem 1024
  openssl pkcs8 -inform PEM -in rsa.pem -nocrypt -topk8 -outform PEM -out rsa.pkcs8
  openssl rsa -in rsa.pkcs8 -pubout > rsa.spki

Generating EC PKCS8/SPKI key files
----------------------------------

.. code-block:: shell

  openssl ecparam -name prime256v1 -genkey -noout -out ec.pem
  openssl pkcs8 -inform PEM -in ec.pem -nocrypt -topk8 -outform PEM -out ec.pkcs8
  openssl ec -in ec.pkcs8 -pubout > ec.spki

Encoding
========

Encoding data using RSA-OAEP
----------------------------

.. code-block:: shell

    echo -n "WAKAWAKA" > text.txt
    openssl rsautl -inkey key.spki -pubin -in text.txt -out - -oaep -encrypt | \
        base64 > text.base64.rsa-oaep.enc

Decoding ciphertext using RSA-OAEP
----------------------------------

.. code-block:: shell

    base64 -d text.base64.rsa-oaep.enc | openssl rsautl -inkey key.pkcs8 -in - -out - -oaep -decrypt
    WAKAWAKA

Encoding data using AES-GCM
---------------------------

.. code-block:: shell

   echo -n "AES-GCM-SECRET-TEXT" > text.txt
   node ./test/webcrypto/aes_gcm_enc.js '{"in":"text.txt"}' > text.base64.aes-gcm128.enc

   echo -n "AES-GCM-96-TAG-LENGTH-SECRET-TEXT" > text.txt
   node ./test/webcrypto/aes_gcm_enc.js '{"in":"text.txt","tagLength":96}' > text.base64.aes-gcm128-96.enc

Encoding data using AES-CTR
---------------------------

.. code-block:: shell

    echo -n "AES-CTR-SECRET-TEXT" | \
        openssl enc -aes-128-ctr -K 00112233001122330011223300112233 -iv 44556677445566774455667744556677 | \
        base64 > text.base64.aes-ctr128.enc

Encoding data using AES-CBC
---------------------------

.. code-block:: shell

    echo -n "AES-CBC-SECRET-TEXT" | \
        openssl enc -aes-128-cbc -K 00112233001122330011223300112233 -iv 44556677445566774455667744556677 | \
        base64 > text.base64.aes-cbc128.enc

Signing
=======

Signing data using HMAC
-----------------------

.. code-block:: shell

    echo -n "SigneD-TExt" > text.txt
    openssl dgst -sha256 -mac hmac -macopt hexkey:aabbcc -binary text.txt | \
        base64 > test/webcrypto/text.base64.sha256.hmac.sig

Signing data using RSASSA-PKCS1-v1_5
------------------------------------

.. code-block:: shell

    echo -n "SigneD-TExt" > text.txt
    openssl dgst -sha256 -sigopt rsa_padding_mode:pkcs1 -sign test/webcrypto/rsa.pkcs8 text.txt | \
        base64 > test/webcrypto/text.base64.sha256.pkcs1.sig
    base64 -d test/webcrypto/text.base64.sha256.pkcs1.sig > text.sha256.pkcs1.sig
    openssl dgst -sha256 -sigopt rsa_padding_mode:pkcs1 -verify test/webcrypto/rsa.spki \
        -signature text.sha256.pkcs1.sig text.txt
    Verified OK

Signing data using RSA-PSS
--------------------------

.. code-block:: shell

    echo -n "SigneD-TExt" > text.txt
    openssl dgst -sha256 -sigopt rsa_padding_mode:pss -sigopt rsa_pss_saltlen:32 -sign test/webcrypto/rsa.pkcs8 text.txt | \
        base64 > test/webcrypto/text.base64.sha256.rsa-pss.32.sig
    base64 -d test/webcrypto/text.base64.sha256.rsa-pss.32.sig > text.sha256.rsa-pss.32.sig
    openssl dgst -sha256 -sigopt rsa_padding_mode:pss -sigopt rsa_pss_saltlen:32 \
        -verify test/webcrypto/rsa.spki -signature text.sha256.rsa-pss.sig text.txt
    Verified OK

Signing data using ECDSA
------------------------

Note: there are two types of ECDSA signatures: ASN.1 and IEEE P1363
Webcrypto requires IEEE P1363, but OpenSSL outputs only ASN.1 variety.
To create P1363, we build an auxilary program asn12IEEEP1336

.. code-block:: shell

    echo -n "SigneD-TExt" > text.txt
    openssl dgst -sha256 -binary text.txt > text.sha256
    openssl pkeyutl -sign -in text.sha256 -inkey test/webcrypto/ec.pkcs8 | \
        base64 > test/webcrypto/text.base64.sha256.ecdsa.asn1.sig
    base64 -d test/webcrypto/text.base64.sha256.ecdsa.asn1.sig > text.sha256.ecdsa.sig
    openssl pkeyutl -verify -in text.sha256 -pubin -inkey test/webcrypto/ec.spki  -sigfile text.sha256.ecdsa.sig
    Signature Verified Successfully

    # convert to IEEE P1363
    gcc test/webcrypto/asn12ieeep1336.c  -lcrypto -o test/webcrypto/asn12ieeep1336
    base64 -d test/webcrypto/text.base64.sha256.ecdsa.asn1.sig | ./test/webcrypto/asn12IEEEP1336 | \
        base64 > test/webcrypto/text.base64.sha256.ecdsa.sig
