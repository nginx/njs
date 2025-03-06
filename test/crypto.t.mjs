/*---
includes: [runTsuite.js]
flags: [async]
---*/

import cr from 'crypto';

let createHash_tsuite = {
    name: "createHash tests",
    skip: () => !cr.createHash,
    T: async (params) => {
        var h = params.hash_value ? params.hash_value(params.hash)
                                  : cr.createHash(params.hash);

        if (typeof h !== 'object') {
            throw Error(`unexpected result "${h}" is not object`);
        }

        for (let i = 0; i < params.data.length; i++) {
            let args = params.data[i];

            if (Array.isArray(args)) {
                h.update(args[0], args[1]);

            } else {
                h.update(args);
            }
        }

        let r = h.digest(params.digest);

        if (!params.digest) {
            if (!(r instanceof Buffer)) {
                throw Error(`unexpected result "${r}" is not Buffer`);
            }

            if (r.compare(params.expected) !== 0) {
                throw Error(`unexpected output "${r}" != "${params.expected}"`);
            }

        } else {
            if (typeof r !== 'string') {
                throw Error(`unexpected result "${r}" is not string`);
            }

            if (r !== params.expected) {
                throw Error(`unexpected output "${r}" != "${params.expected}"`);
            }
        }

        return 'SUCCESS';
    },

    tests: [
        { hash: 'md5', data: [], digest: 'hex',
          expected: "d41d8cd98f00b204e9800998ecf8427e" },
        { hash: 'md5', data: [''], digest: 'hex',
          expected: "d41d8cd98f00b204e9800998ecf8427e" },
        { hash: 'md5', data: [''],
          expected: Buffer.from("d41d8cd98f00b204e9800998ecf8427e", "hex") },

        { hash: 'md5', data: ['AB'], digest: 'hex',
          expected: "b86fc6b051f63d73de262d4c34e3a0a9" },
        { hash: 'md5', data: ['A', 'B'], digest: 'hex',
          expected: "b86fc6b051f63d73de262d4c34e3a0a9" },
        { hash: 'md5', data: [Buffer.from('XABX').subarray(1,3)], digest: 'hex',
          expected: "b86fc6b051f63d73de262d4c34e3a0a9" },

        { hash: 'md5', data: [['4142', 'hex']], digest: 'hex',
          expected: "b86fc6b051f63d73de262d4c34e3a0a9" },
        { hash: 'md5', data: [['QUI=', 'base64']], digest: 'hex',
          expected: "b86fc6b051f63d73de262d4c34e3a0a9" },
        { hash: 'md5', data: [['QUI', 'base64url']], digest: 'hex',
          expected: "b86fc6b051f63d73de262d4c34e3a0a9" },

        { hash: 'md5', data: ['abc'.repeat(100)], digest: 'hex',
          expected: "f571117acbd8153c8dc3c81b8817773a" },

        { hash: 'md5', data: ['AB'], digest: 'base64',
          expected: "uG/GsFH2PXPeJi1MNOOgqQ==" },
        { hash: 'md5', data: ['A', 'B'], digest: 'base64',
          expected: "uG/GsFH2PXPeJi1MNOOgqQ==" },
        { hash: 'md5', data: [Buffer.from('XABX').subarray(1,3)], digest: 'base64',
          expected: "uG/GsFH2PXPeJi1MNOOgqQ==" },

        { hash: 'md5', data: ['AB'], digest: 'base64url',
          expected: "uG_GsFH2PXPeJi1MNOOgqQ" },
        { hash: 'md5', data: ['A', 'B'], digest: 'base64url',
          expected: "uG_GsFH2PXPeJi1MNOOgqQ" },
        { hash: 'md5', data: [Buffer.from('XABX').subarray(1,3)], digest: 'base64url',
          expected: "uG_GsFH2PXPeJi1MNOOgqQ" },

        { hash: 'sha1', data: [], digest: 'hex',
          expected: "da39a3ee5e6b4b0d3255bfef95601890afd80709" },
        { hash: 'sha1', data: [''], digest: 'hex',
          expected: "da39a3ee5e6b4b0d3255bfef95601890afd80709" },
        { hash: 'sha1', data: [''],
          expected: Buffer.from("da39a3ee5e6b4b0d3255bfef95601890afd80709", "hex") },

        { hash: 'sha1', data: ['AB'], digest: 'hex',
          expected: "06d945942aa26a61be18c3e22bf19bbca8dd2b5d" },
        { hash: 'sha1', data: ['A', 'B'], digest: 'hex',
          expected: "06d945942aa26a61be18c3e22bf19bbca8dd2b5d" },
        { hash: 'sha1', data: [Buffer.from('XABX').subarray(1,3)], digest: 'hex',
          expected: "06d945942aa26a61be18c3e22bf19bbca8dd2b5d" },

        { hash: 'sha1', data: [['4142', 'hex']], digest: 'hex',
          expected: "06d945942aa26a61be18c3e22bf19bbca8dd2b5d" },
        { hash: 'sha1', data: [['QUI=', 'base64']], digest: 'hex',
          expected: "06d945942aa26a61be18c3e22bf19bbca8dd2b5d" },
        { hash: 'sha1', data: [['QUI', 'base64url']], digest: 'hex',
          expected: "06d945942aa26a61be18c3e22bf19bbca8dd2b5d" },

        { hash: 'sha1', data: ['abc'.repeat(100)], digest: 'hex',
          expected: "c95466320eaae6d19ee314ae4f135b12d45ced9a" },

        { hash: 'sha1', data: ['AB'], digest: 'base64',
          expected: "BtlFlCqiamG+GMPiK/GbvKjdK10=" },
        { hash: 'sha1', data: ['A', 'B'], digest: 'base64',
          expected: "BtlFlCqiamG+GMPiK/GbvKjdK10=" },
        { hash: 'sha1', data: [Buffer.from('XABX').subarray(1,3)], digest: 'base64',
          expected: "BtlFlCqiamG+GMPiK/GbvKjdK10=" },

        { hash: 'sha1', data: ['AB'], digest: 'base64url',
          expected: "BtlFlCqiamG-GMPiK_GbvKjdK10" },
        { hash: 'sha1', data: ['A', 'B'], digest: 'base64url',
          expected: "BtlFlCqiamG-GMPiK_GbvKjdK10" },
        { hash: 'sha1', data: [Buffer.from('XABX').subarray(1,3)], digest: 'base64url',
          expected: "BtlFlCqiamG-GMPiK_GbvKjdK10" },

        { hash: 'sha256', data: [], digest: 'hex',
          expected: "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855" },
        { hash: 'sha256', data: [''], digest: 'hex',
          expected: "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855" },
        { hash: 'sha256', data: [''],
          expected: Buffer.from("e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855", "hex") },

        { hash: 'sha256', data: ['AB'], digest: 'hex',
          expected: "38164fbd17603d73f696b8b4d72664d735bb6a7c88577687fd2ae33fd6964153" },
        { hash: 'sha256', data: ['A', 'B'], digest: 'hex',
          expected: "38164fbd17603d73f696b8b4d72664d735bb6a7c88577687fd2ae33fd6964153" },
        { hash: 'sha256', data: [Buffer.from('XABX').subarray(1,3)], digest: 'hex',
          expected: "38164fbd17603d73f696b8b4d72664d735bb6a7c88577687fd2ae33fd6964153" },

        { hash: 'sha256', data: [['4142', 'hex']], digest: 'hex',
          expected: "38164fbd17603d73f696b8b4d72664d735bb6a7c88577687fd2ae33fd6964153" },
        { hash: 'sha256', data: [['QUI=', 'base64']], digest: 'hex',
          expected: "38164fbd17603d73f696b8b4d72664d735bb6a7c88577687fd2ae33fd6964153" },
        { hash: 'sha256', data: [['QUI', 'base64url']], digest: 'hex',
          expected: "38164fbd17603d73f696b8b4d72664d735bb6a7c88577687fd2ae33fd6964153" },

        { hash: 'sha256', data: ['abc'.repeat(100)], digest: 'hex',
          expected: "d9f5aeb06abebb3be3f38adec9a2e3b94228d52193be923eb4e24c9b56ee0930" },

        { hash: 'sha256', data: ['AB'], digest: 'base64',
          expected: "OBZPvRdgPXP2lri01yZk1zW7anyIV3aH/SrjP9aWQVM=" },
        { hash: 'sha256', data: ['A', 'B'], digest: 'base64',
          expected: "OBZPvRdgPXP2lri01yZk1zW7anyIV3aH/SrjP9aWQVM=" },
        { hash: 'sha256', data: [Buffer.from('XABX').subarray(1,3)], digest: 'base64',
          expected: "OBZPvRdgPXP2lri01yZk1zW7anyIV3aH/SrjP9aWQVM=" },

        { hash: 'sha256', data: ['AB'], digest: 'base64url',
          expected: "OBZPvRdgPXP2lri01yZk1zW7anyIV3aH_SrjP9aWQVM" },
        { hash: 'sha256', data: ['A', 'B'], digest: 'base64url',
          expected: "OBZPvRdgPXP2lri01yZk1zW7anyIV3aH_SrjP9aWQVM" },
        { hash: 'sha256', data: [Buffer.from('XABX').subarray(1,3)], digest: 'base64url',
          expected: "OBZPvRdgPXP2lri01yZk1zW7anyIV3aH_SrjP9aWQVM" },

        { hash: 'sha1',
          hash_value(hash) {
              var Hash = cr.createHash(hash).constructor;
              return Hash(hash);
          },
          data: [], digest: 'hex',
          expected: "da39a3ee5e6b4b0d3255bfef95601890afd80709" },

        { hash: 'sha1',
          hash_value(hash) {
              var h = cr.createHash(hash);
              h.copy().digest('hex');
              return h;
          },
          data: [], digest: 'hex',
          expected: "da39a3ee5e6b4b0d3255bfef95601890afd80709" },

        { hash: 'sha1',
          hash_value(hash) {
              var h = cr.createHash(hash);
              h.digest('hex');
              return h;
          },
          data: [], digest: 'hex',
          exception: "TypeError: Digest already called" },

        { hash: 'sha1',
          hash_value(hash) {
              var h = cr.createHash(hash);
              h.digest('hex');
              h.copy();
              return h;
          },
          data: [], digest: 'hex',
          exception: "TypeError: Digest already called" },

        { hash: 'sha1',
          hash_value(hash) {
              var h = cr.createHash(hash);
              h.update('AB');
              return h;
          },
          data: [], digest: 'hex',
          exception: "TypeError: Digest already called" },

        { hash: 'sha1', data: [undefined], digest: 'hex',
          exception: "TypeError: data is not a string or Buffer-like object" },
        { hash: 'sha1', data: [{}], digest: 'hex',
          exception: "TypeError: data is not a string or Buffer-like object" },

        { hash: 'unknown', data: [], digest: 'hex',
          exception: 'TypeError: not supported algorithm: "unknown"' },
        { hash: 'sha1', data: [], digest: 'unknown',
          exception: 'TypeError: unknown digest type: "unknown"' },
]};


let createHmac_tsuite = {
    name: "createHmac tests",
    skip: () => !cr.createHmac,
    T: async (params) => {
        var h = params.hmac_value ? params.hmac_value(params.hash, params.key)
                                  : cr.createHmac(params.hash, params.key || '');

        if (typeof h !== 'object') {
            throw Error(`unexpected result "${h}" is not object`);
        }

        for (let i = 0; i < params.data.length; i++) {
            let args = params.data[i];

            if (Array.isArray(args)) {
                h.update(args[0], args[1]);

            } else {
                h.update(args);
            }
        }

        let r = h.digest(params.digest);

        if (!params.digest) {
            if (!(r instanceof Buffer)) {
                throw Error(`unexpected result "${r}" is not Buffer`);
            }

            if (r.compare(params.expected) !== 0) {
                throw Error(`unexpected output "${r}" != "${params.expected}"`);
            }

        } else {
            if (typeof r !== 'string') {
                throw Error(`unexpected result "${r}" is not string`);
            }

            if (r !== params.expected) {
                throw Error(`unexpected output "${r}" != "${params.expected}"`);
            }
        }

        return 'SUCCESS';
    },

    tests: [
        { hash: 'md5', key: '', data: [], digest: 'hex',
          expected: "74e6f7298a9c2d168935f58c001bad88" },
        { hash: 'md5', key: '', data: [''], digest: 'hex',
          expected: "74e6f7298a9c2d168935f58c001bad88" },
        { hash: 'md5', key: '', data: [''],
          expected: Buffer.from("74e6f7298a9c2d168935f58c001bad88", "hex") },

        { hash: 'md5', key: '', data: ['AB'], digest: 'hex',
          expected: "9e0e9e545ef63d41dfb653daecf8ebc7" },
        { hash: 'md5', key: '', data: ['A', 'B'], digest: 'hex',
          expected: "9e0e9e545ef63d41dfb653daecf8ebc7" },
        { hash: 'md5', key: '', data: [Buffer.from('XABX').subarray(1,3)], digest: 'hex',
          expected: "9e0e9e545ef63d41dfb653daecf8ebc7" },

        { hash: 'md5', key: '', data: [['4142', 'hex']], digest: 'hex',
          expected: "9e0e9e545ef63d41dfb653daecf8ebc7" },
        { hash: 'md5', key: '', data: [['QUI=', 'base64']], digest: 'hex',
          expected: "9e0e9e545ef63d41dfb653daecf8ebc7" },
        { hash: 'md5', key: '', data: [['QUI', 'base64url']], digest: 'hex',
          expected: "9e0e9e545ef63d41dfb653daecf8ebc7" },

        { hash: 'md5', key: Buffer.from('secret'), data: ['abc'.repeat(100)], digest: 'hex',
          expected: "91eb74a225cdd3bbfccc34396c6e3ac5" },

        { hash: 'md5', key: '', data: ['AB'], digest: 'base64',
          expected: "ng6eVF72PUHftlPa7Pjrxw==" },
        { hash: 'md5', key: '', data: ['A', 'B'], digest: 'base64',
          expected: "ng6eVF72PUHftlPa7Pjrxw==" },
        { hash: 'md5', key: '', data: [Buffer.from('XABX').subarray(1,3)], digest: 'base64',
          expected: "ng6eVF72PUHftlPa7Pjrxw==" },

        { hash: 'md5', key: '', data: ['AB'], digest: 'base64url',
          expected: "ng6eVF72PUHftlPa7Pjrxw" },
        { hash: 'md5', key: '', data: ['A', 'B'], digest: 'base64url',
          expected: "ng6eVF72PUHftlPa7Pjrxw" },
        { hash: 'md5', key: '', data: [Buffer.from('XABX').subarray(1,3)], digest: 'base64url',
          expected: "ng6eVF72PUHftlPa7Pjrxw" },

        { hash: 'sha1', key: '', data: [], digest: 'hex',
          expected: "fbdb1d1b18aa6c08324b7d64b71fb76370690e1d" },
        { hash: 'sha1', key: '', data: [''], digest: 'hex',
          expected: "fbdb1d1b18aa6c08324b7d64b71fb76370690e1d" },
        { hash: 'sha1', key: '', data: [''],
          expected: Buffer.from("fbdb1d1b18aa6c08324b7d64b71fb76370690e1d", "hex") },

        { hash: 'sha1', key: '', data: ['AB'], digest: 'hex',
          expected: "d32c0b6637cc2dfe4670f3fe48ef4434123c4810" },
        { hash: 'sha1', key: '', data: ['A', 'B'], digest: 'hex',
          expected: "d32c0b6637cc2dfe4670f3fe48ef4434123c4810" },
        { hash: 'sha1', key: '', data: [Buffer.from('XABX').subarray(1,3)], digest: 'hex',
          expected: "d32c0b6637cc2dfe4670f3fe48ef4434123c4810" },

        { hash: 'sha1', key: '', data: [['4142', 'hex']], digest: 'hex',
          expected: "d32c0b6637cc2dfe4670f3fe48ef4434123c4810" },
        { hash: 'sha1', key: '', data: [['QUI=', 'base64']], digest: 'hex',
          expected: "d32c0b6637cc2dfe4670f3fe48ef4434123c4810" },
        { hash: 'sha1', key: '', data: [['QUI', 'base64url']], digest: 'hex',
          expected: "d32c0b6637cc2dfe4670f3fe48ef4434123c4810" },

        { hash: 'sha1', key: Buffer.from('secret'), data: ['abc'.repeat(100)], digest: 'hex',
          expected: "0aac71e3a813a7acc4a809cfdedb2ecba04ffc5e" },

        { hash: 'sha1', key: '', data: ['AB'], digest: 'base64',
          expected: "0ywLZjfMLf5GcPP+SO9ENBI8SBA=" },
        { hash: 'sha1', key: '', data: ['A', 'B'], digest: 'base64',
          expected: "0ywLZjfMLf5GcPP+SO9ENBI8SBA=" },
        { hash: 'sha1', key: '', data: [Buffer.from('XABX').subarray(1,3)], digest: 'base64',
          expected: "0ywLZjfMLf5GcPP+SO9ENBI8SBA=" },

        { hash: 'sha1', key: '', data: ['AB'], digest: 'base64url',
          expected: "0ywLZjfMLf5GcPP-SO9ENBI8SBA" },
        { hash: 'sha1', key: '', data: ['A', 'B'], digest: 'base64url',
          expected: "0ywLZjfMLf5GcPP-SO9ENBI8SBA" },
        { hash: 'sha1', key: '', data: [Buffer.from('XABX').subarray(1,3)], digest: 'base64url',
          expected: "0ywLZjfMLf5GcPP-SO9ENBI8SBA" },

        { hash: 'sha256', key: '', data: [], digest: 'hex',
          expected: "b613679a0814d9ec772f95d778c35fc5ff1697c493715653c6c712144292c5ad" },
        { hash: 'sha256', key: '', data: [''], digest: 'hex',
          expected: "b613679a0814d9ec772f95d778c35fc5ff1697c493715653c6c712144292c5ad" },
        { hash: 'sha256', key: '', data: [''],
          expected: Buffer.from("b613679a0814d9ec772f95d778c35fc5ff1697c493715653c6c712144292c5ad", "hex") },

        { hash: 'sha256', key: '', data: ['AB'], digest: 'hex',
          expected: "d53400095496267cf02e5dbd4b0bf9fbfb5f36f311ea7d9809af5487421743e3" },
        { hash: 'sha256', key: '', data: ['A', 'B'], digest: 'hex',
          expected: "d53400095496267cf02e5dbd4b0bf9fbfb5f36f311ea7d9809af5487421743e3" },
        { hash: 'sha256', key: '', data: [Buffer.from('XABX').subarray(1,3)], digest: 'hex',
          expected: "d53400095496267cf02e5dbd4b0bf9fbfb5f36f311ea7d9809af5487421743e3" },

        { hash: 'sha256', key: '', data: [['4142', 'hex']], digest: 'hex',
          expected: "d53400095496267cf02e5dbd4b0bf9fbfb5f36f311ea7d9809af5487421743e3" },
        { hash: 'sha256', key: '', data: [['QUI=', 'base64']], digest: 'hex',
          expected: "d53400095496267cf02e5dbd4b0bf9fbfb5f36f311ea7d9809af5487421743e3" },
        { hash: 'sha256', key: '', data: [['QUI', 'base64url']], digest: 'hex',
          expected: "d53400095496267cf02e5dbd4b0bf9fbfb5f36f311ea7d9809af5487421743e3" },

        { hash: 'sha256', key: Buffer.from('secret'), data: ['abc'.repeat(100)], digest: 'hex',
          expected: "8660d2d51d6f20f61d5aadfb6c43df7fd05fc2fc4967d8aec1846f3d9ec03987" },

        { hash: 'sha256', key: '', data: ['AB'], digest: 'base64',
          expected: "1TQACVSWJnzwLl29Swv5+/tfNvMR6n2YCa9Uh0IXQ+M=" },
        { hash: 'sha256', key: '', data: ['A', 'B'], digest: 'base64',
          expected: "1TQACVSWJnzwLl29Swv5+/tfNvMR6n2YCa9Uh0IXQ+M=" },
        { hash: 'sha256', key: '', data: [Buffer.from('XABX').subarray(1,3)], digest: 'base64',
          expected: "1TQACVSWJnzwLl29Swv5+/tfNvMR6n2YCa9Uh0IXQ+M=" },

        { hash: 'sha256', key: '', data: ['AB'], digest: 'base64url',
          expected: "1TQACVSWJnzwLl29Swv5-_tfNvMR6n2YCa9Uh0IXQ-M" },
        { hash: 'sha256', key: '', data: ['A', 'B'], digest: 'base64url',
          expected: "1TQACVSWJnzwLl29Swv5-_tfNvMR6n2YCa9Uh0IXQ-M" },
        { hash: 'sha256', key: '', data: [Buffer.from('XABX').subarray(1,3)], digest: 'base64url',
          expected: "1TQACVSWJnzwLl29Swv5-_tfNvMR6n2YCa9Uh0IXQ-M" },

        { hash: 'sha256', key: 'A'.repeat(64), data: ['AB'], digest: 'hex',
          expected: "ee9dce43b12eb3e865614ad9c1a8d4fad4b6eac2b64647bd24cd192888d3f367" },

        { hash: 'sha256', key: 'A'.repeat(100), data: ['AB'], digest: 'hex',
          expected: "5647b6c429701ff512f0f18232b4507065d2376ca8899a816a0a6e721bf8ddcc" },

        { hash: 'sha1',
          hmac_value(hash, key) {
              var Hmac = cr.createHmac(hash, key).constructor;
              return Hmac(hash, key);
          },
          key: '', data: [], digest: 'hex',
          expected: "fbdb1d1b18aa6c08324b7d64b71fb76370690e1d" },

        { hash: 'sha1',
          hmac_value(hash, key) {
              var h = cr.createHmac(hash, key);
              h.digest('hex');
              return h;
          },
          key: '', data: [], digest: 'hex',
          exception: "TypeError: Digest already called" },

        { hash: 'sha1',
          hmac_value(hash, key) {
              var h = cr.createHmac(hash, key);
              h.digest('hex');
              h.update('A');
              return h;
          },
          key: '', data: [], digest: 'hex',
          exception: "TypeError: Digest already called" },

        { hash: 'sha1', key: '', data: [undefined], digest: 'hex',
          exception: "TypeError: data is not a string or Buffer-like object" },
        { hash: 'sha1', key: '', data: [{}], digest: 'hex',
          exception: "TypeError: data is not a string or Buffer-like object" },

        { hash: 'unknown', key: '', data: [], digest: 'hex',
          exception: 'TypeError: not supported algorithm: "unknown"' },
        { hash: 'sha1', key: '', data: [], digest: 'unknown',
          exception: 'TypeError: unknown digest type: "unknown"' },

        { hash: 'sha1', key: [], data: [], digest: 'hex',
          exception: 'TypeError: key is not a string or Buffer-like object' },

        { hash: 'sha1',
          hmac_value(hash, key) {
              var h = cr.createHash('sha1');
              h.update.call(cr.createHmac(hash, key), '');
          },
          key: '', data: [], digest: 'hex',
          exception: 'TypeError: "this" is not a hash object' },
]};


run([
    createHash_tsuite,
    createHmac_tsuite
])
.then($DONE, $DONE);
