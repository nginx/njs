/*---
includes: [runTsuite.js]
flags: [async]
---*/

import cr from 'crypto';

function has_algorithm(alg) {
    try {
        cr.createHash(alg).digest();
        return true;
    } catch (e) {
        return false;
    }
}

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

        { hash: 'sha384', data: [], digest: 'hex',
          expected: "38b060a751ac96384cd9327eb1b1e36a21fdb71114be07434c0cc7bf63f6e1da274edebfe76f65fbd51ad2f14898b95b" },
        { hash: 'sha384', data: ['AB'], digest: 'hex',
          expected: "4f9179c7b48de00c642b3782c3f9435bf21bde99cbf9ca13b6c3f1e58fff9064ad47464da97e6277c7f438d8f5a91d6b" },
        { hash: 'sha384', data: ['A', 'B'], digest: 'hex',
          expected: "4f9179c7b48de00c642b3782c3f9435bf21bde99cbf9ca13b6c3f1e58fff9064ad47464da97e6277c7f438d8f5a91d6b" },

        { hash: 'sha512', data: [], digest: 'hex',
          expected: "cf83e1357eefb8bdf1542850d66d8007d620e4050b5715dc83f4a921d36ce9ce47d0d13c5d85f2b0ff8318d2877eec2f63b931bd47417a81a538327af927da3e" },
        { hash: 'sha512', data: ['AB'], digest: 'hex',
          expected: "71edc062331872ff3c13c77d98f4af0e8e27bb360d03690558beab6711f9733e5dc7f114b7af58cfbcd6360575873c09a667a9af749dc912e4ca276a7dfee5d3" },
        { hash: 'sha512', data: ['A', 'B'], digest: 'hex',
          expected: "71edc062331872ff3c13c77d98f4af0e8e27bb360d03690558beab6711f9733e5dc7f114b7af58cfbcd6360575873c09a667a9af749dc912e4ca276a7dfee5d3" },

        { hash: 'sha3-256', skip: () => !has_algorithm('sha3-256'),
          data: [], digest: 'hex',
          expected: "a7ffc6f8bf1ed76651c14756a061d662f580ff4de43b49fa82d80a4b80f8434a" },
        { hash: 'sha3-256', skip: () => !has_algorithm('sha3-256'),
          data: ['AB'], digest: 'hex',
          expected: "21f29d555c7c5f9e5859a7984c0b2f8ccad9d5245fbe6c4703fd75c0922d7735" },
        { hash: 'sha3-256', skip: () => !has_algorithm('sha3-256'),
          data: ['A', 'B'], digest: 'hex',
          expected: "21f29d555c7c5f9e5859a7984c0b2f8ccad9d5245fbe6c4703fd75c0922d7735" },

        { hash: 'sha3-512', skip: () => !has_algorithm('sha3-512'),
          data: [], digest: 'hex',
          expected: "a69f73cca23a9ac5c8b567dc185a756e97c982164fe25859e0d1dcc1475c80a615b2123af1f5f94c11e3e9402c3ac558f500199d95b6d3e301758586281dcd26" },
        { hash: 'sha3-512', skip: () => !has_algorithm('sha3-512'),
          data: ['AB'], digest: 'hex',
          expected: "fcc802621fee9efe4d8ee032d886f75431edb29d480e945d8f0efb1c0ad419bf9b652fca1fa1f5af0f5b4a74f76a6e86b00dbfbec7dcf00e3f4ef34840e9b720" },

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

        { hash: 'sha384', key: '', data: [], digest: 'hex',
          expected: "6c1f2ee938fad2e24bd91298474382ca218c75db3d83e114b3d4367776d14d3551289e75e8209cd4b792302840234adc" },
        { hash: 'sha384', key: '', data: ['AB'], digest: 'hex',
          expected: "0145ec85556f28b82b49d7bd8c3373312d95c308b758e3bd3cd972f8ab9d0ea9245f60ef5b994ec936eb42c6fc7ca033" },
        { hash: 'sha384', key: Buffer.from('secret'), data: ['AB'], digest: 'hex',
          expected: "afb6653433bc3b3e570fa9c7f0fa1f40d070af21085d7292ce8c93c4a280c2c91b1da980c39a3738361458c75a8c6f64" },

        { hash: 'sha512', key: '', data: [], digest: 'hex',
          expected: "b936cee86c9f87aa5d3c6f2e84cb5a4239a5fe50480a6ec66b70ab5b1f4ac6730c6c515421b327ec1d69402e53dfb49ad7381eb067b338fd7b0cb22247225d47" },
        { hash: 'sha512', key: '', data: ['AB'], digest: 'hex',
          expected: "8bf9155a8dbd563d879ecb5ad27e7b9e8e30ab98c138802594bedd9d839ddecb85443fdc64e18274311975b4ec1c5dd5dc41a6e7530cf34ea3d6545cf5844501" },
        { hash: 'sha512', key: Buffer.from('secret'), data: ['AB'], digest: 'hex',
          expected: "1169372f39b4ea921bed3cff6d029c0be7116bf2966098b9923d0730e9bd569e2f52e65f63c3adebe988b0be174fd42578b1536f1f1b8b46a1382da55e27de9d" },

        { hash: 'sha512', key: 'A'.repeat(200), data: ['AB'], digest: 'hex',
          expected: "d64ea04999cba0f2f167f471569129f9b3105a2e099fb883be3cdee452b38cf7c34d1705b83caaaf784dd4e63703a328d9a5167b03dd059d516b386ff86d5a40" },

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
