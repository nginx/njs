/*---
includes: [runTsuite.js]
flags: [async]
---*/

import cr from 'crypto';


function p(args, default_opts) {
    let params = merge({}, default_opts);
    params = merge(params, args);

    return params;
}


let createHash_tsuite = {
    name: "createHash tests",
    skip: () => !cr.createHash,
    T: async (params) => {

        if (params.value !== params.expected) {
            throw Error(`unexpected output "${params.value}" != "${params.expected}"`);
        }

        return 'SUCCESS';
    },
    prepare_args: p,
    opts: { },

    tests: [

        { id: 1, value: (cr.createHash('sha1')+"").substring(0,8), expected: "[object " },

        { id: 2, value: (
              function(){
                  var h = cr.createHash('sha1');
                  var Hash = h.constructor;
                  return Hash('sha1').update('AB').digest('hex')
              }
          )(),
          expected: "06d945942aa26a61be18c3e22bf19bbca8dd2b5d" },

        { id: 3, value: (
              function() {
                  var hash = cr.createHash.bind(undefined, 'md5');
                  return ['hex', 'base64', 'base64url'].map(e => {
                      var h = hash().update('AB').digest().toString(e);
                      var h2 = hash().update(Buffer.from('XABX').subarray(1,3)).digest(e);
                      var h3 = hash().update('A').update('B').digest(e);
                      if (h !== h2) {throw new Error(`digest().toString($e):$h != digest($e):$h2`)};
                      if (h !== h3) {throw new Error(`digest().toString($e):$h != update('A').update('B').digest($e):$h3`)};
                      return h;
                  }).join(',')
              }
          )(),
          expected: "b86fc6b051f63d73de262d4c34e3a0a9,uG/GsFH2PXPeJi1MNOOgqQ==,uG_GsFH2PXPeJi1MNOOgqQ" },

        { id: 4, value: (
              function() {
                  var hash = cr.createHash.bind(undefined, 'sha1');
                  return ['hex', 'base64', 'base64url'].map(e => {
                      var h = hash().update('AB').digest().toString(e);
                      var h2 = hash().update(Buffer.from('XABX').subarray(1,3)).digest(e);
                      var h3 = hash().update('A').update('B').digest(e);
                      if (h !== h2) {throw new Error(`digest().toString($e):$h != digest($e):$h2`)};
                      if (h !== h3) {throw new Error(`digest().toString($e):$h != update('A').update('B').digest($e):$h3`)};
                      return h;
                  }).join(',')
              }
          )(),
          expected: "06d945942aa26a61be18c3e22bf19bbca8dd2b5d,BtlFlCqiamG+GMPiK/GbvKjdK10=,BtlFlCqiamG-GMPiK_GbvKjdK10" },

        { id: 5, value: (
              function() {
                  var hash = cr.createHash.bind(undefined, 'sha1');
                  return ['hex', 'base64', 'base64url'].every(e => {
                      var h = hash().digest(e);
                      var h2 = hash().update('').digest(e);
                     if (h !== h2) {throw new Error(`digest($e):$h != update('').digest($e):$h2`)};
                     return true;
                  });
              }
          )(),
          expected: true },

        { id: 6, value: (
              function() {
                  var hash = cr.createHash.bind(undefined, 'sha1');
                  return [
                      ['AB'],
                      ['4142', 'hex'],
                      ['QUI=', 'base64'],
                      ['QUI', 'base64url'],
                  ].every(args => {
                      return hash().update(args[0], args[1]).digest('hex') === '06d945942aa26a61be18c3e22bf19bbca8dd2b5d';
                  })
              }
          )(),
          expected: true },

        { id: 7, value: (
              function() {
                  var hash = cr.createHash.bind(undefined, 'sha256');

                  return ['hex', 'base64', 'base64url'].map(e => {
                      var h = hash().update('AB').digest().toString(e);
                      var h2 = hash().update(Buffer.from('XABX').subarray(1,3)).digest(e);
                      var h3 = hash().update('A').update('B').digest(e);
                      if (h !== h2) {throw new Error(`digest().toString($e):$h != digest($e):$h2`)};
                      if (h !== h3) {throw new Error(`digest().toString($e):$h != update('A').update('B').digest($e):$h3`)};
                      return h;
                  }).join(',')
              }
          )(),
          expected: "38164fbd17603d73f696b8b4d72664d735bb6a7c88577687fd2ae33fd6964153,"+
              "OBZPvRdgPXP2lri01yZk1zW7anyIV3aH/SrjP9aWQVM=,"+
              "OBZPvRdgPXP2lri01yZk1zW7anyIV3aH_SrjP9aWQVM" },

        { id: 8, value: (
              function() {
                  let hash = cr.createHash('sha256');
                  let digests = [];
                  hash.update('one');
                  digests.push(hash.copy().digest('hex'));
                  hash.update('two');
                  digests.push(hash.copy().digest('hex'));
                  hash.update('three');
                  digests.push(hash.copy().digest('hex'));
                  return digests.join(',');
              }
          )(),
          expected: "7692c3ad3540bb803c020b3aee66cd8887123234ea0c6e7143c0add73ff431ed,"+
              "25b6746d5172ed6352966a013d93ac846e1110d5a25e8f183b5931f4688842a1,"+
              "4592092e1061c7ea85af2aed194621cc17a2762bae33a79bf8ce33fd0168b801" },

        { id: 9, value: (
              function() {
                  try {
                      let hash = cr.createHash('sha256');
                      hash.update('one').digest();
                      hash.copy();
                  } catch(e) {
                      return e.message;
                  }
              }
          )(),
          expected: "Digest already called" },

        { id: 10, value: (
              function() {
                  var hash = cr.createHash;
                  return ['', 'abc'.repeat(100)].map(v => {
                      return ['md5', 'sha1', 'sha256'].map(h => {
                          return hash(h).update(v).digest('hex');
                       }).join(',');
                  }).join(',');
              }
          )(),
          expected: "d41d8cd98f00b204e9800998ecf8427e,"+
              "da39a3ee5e6b4b0d3255bfef95601890afd80709,"+
              "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855,"+
              "f571117acbd8153c8dc3c81b8817773a,"+
              "c95466320eaae6d19ee314ae4f135b12d45ced9a,"+
              "d9f5aeb06abebb3be3f38adec9a2e3b94228d52193be923eb4e24c9b56ee0930" },

        { id: 11, value: (
              function() {
                  try {
                      cr.createHash();
                  } catch(e) {
                      return /algorithm/.test(e.message);
                  }
              }
          )(),
          expected: true },

        { id: 12, value: (
              function() {
                  try {
                      cr.createHash([]);
                  } catch(e) {
                      return /algorithm/.test(e.message);  // our implemenation njs/qjs
                  }
                  // node fall to 'buffer'
                  return true;
              }
          )(),
          expected: true },

        { id: 13, value: (
              function() {
                  try {
                      cr.createHash('sha512');
                  } catch(e) {
                      return /supported/.test(e.message); // our implemenation njs/qjs
                  }
                  // node fall to 'buffer'
                  return true;
              }
          )(),
          expected: true },

        { id: 14, value: (
              function() {
                  var h = cr.createHash('sha1');
                  try {
                      h.update();
                  } catch(e) {
                      return /data/.test(e.message);
                  }
              }
          )(),
          expected: true },

        { id: 15, value: (
              function() {
                  var h = cr.createHash('sha1');
                  try {
                      h.update({});
                  } catch(e) {
                      return /data/.test(e.message);
                  }
              }
          )(),
          expected: true },

        { id: 16, value: (
              function() {
                  var h = cr.createHash('sha1');
                  try {
                      h.update('A').digest('latin1');
                  } catch(e) {
                      return /Unknown/.test(e.message); // our implemenation njs/qjs
                  }
                  // node fall to 'buffer'
                  return true;
              }
          )(),
          expected: true },

        { id: 17, value: (
              function() {
                  var h = cr.createHash('sha1');
                  return h.digest() instanceof Buffer;
              }
          )(),
          expected: true },

        { id: 18, value: (
              function() {
                  var h = cr.createHash('sha1');
                  try {
                      h.update('A').digest('hex'); h.digest('hex');
                  } catch(e) {
                      return /Digest already called/.test(e.message);
                  }
              }
          )(),
          expected: true },

        { id: 19, value: (
              function() {
                  var h = cr.createHash('sha1');
                  try {
                      h.update('A').digest('hex'); h.update('B');
                  } catch(e) {
                      return /Digest already called/.test(e.message);
                  }
              }
          )(),
          expected: true },

        { id: 20, value: (
              function() {
                  var h = cr.createHash('md5');
                  return typeof h;
              }
          )(),
          expected: "object" },

]};



let createHmac_tsuite = {
    name: "createHmac tests",
    skip: () => !cr.createHmac,
    T: async (params) => {

        if (params.value !== params.expected) {
            throw Error(`unexpected output "${params.value}" != "${params.expected}"`);
        }

        return 'SUCCESS';
    },
    prepare_args: p,
    opts: { },

    tests: [

        { id: 1, value: (cr.createHmac('sha1','')+"").substring(0,8), expected: "[object " },

        { id: 2, value: (
              function() {
                  var hash = cr.createHmac.bind(undefined, 'md5', '');
                  return ['hex', 'base64', 'base64url'].map(e => {
                      var h = hash().update('AB').digest().toString(e);
                      var h2 = hash().update(Buffer.from('XABX').subarray(1,3)).digest(e);
                      var h3 = hash().update('A').update('B').digest(e);
                      if (h !== h2) {throw new Error(`digest().toString($e):$h != digest($e):$h2`)};
                      if (h !== h3) {throw new Error(`digest().toString($e):$h != update('A').update('B').digest($e):$h3`)};
                      return h;
                  }).join(',')
              }
          )(),
          expected: "9e0e9e545ef63d41dfb653daecf8ebc7,ng6eVF72PUHftlPa7Pjrxw==,ng6eVF72PUHftlPa7Pjrxw" },

        { id: 3, value: (
              function() {
                  var hash = cr.createHmac.bind(undefined, 'sha1', '');
                  return ['hex', 'base64', 'base64url'].map(e => {
                      var h = hash().update('AB').digest().toString(e);
                      var h2 = hash().update(Buffer.from('XABX').subarray(1,3)).digest(e);
                      var h3 = hash().update('A').update('B').digest(e);
                      if (h !== h2) {throw new Error(`digest().toString($e):$h != digest($e):$h2`)};
                      if (h !== h3) {throw new Error(`digest().toString($e):$h != update('A').update('B').digest($e):$h3`)};
                      return h;
                  }).join(',')
              }
          )(),
          expected: "d32c0b6637cc2dfe4670f3fe48ef4434123c4810,0ywLZjfMLf5GcPP+SO9ENBI8SBA=,0ywLZjfMLf5GcPP-SO9ENBI8SBA" },

        { id: 4, value: (
              function() {
                  var hash = cr.createHmac.bind(undefined, 'sha1', '');
                  return [
                      ['AB'],
                      ['4142', 'hex'],
                      ['QUI=', 'base64'],
                      ['QUI', 'base64url'],
                  ].every(args => {
                      return hash().update(args[0], args[1]).digest('hex') === 'd32c0b6637cc2dfe4670f3fe48ef4434123c4810';
                  })
              }
          )(),
          expected: true },

        { id: 5, value: (
              function() {
                  var hash = cr.createHmac.bind(undefined, 'sha256', '');

                  return ['hex', 'base64', 'base64url'].map(e => {
                      var h = hash().update('AB').digest().toString(e);
                      var h2 = hash().update(Buffer.from('XABX').subarray(1,3)).digest(e);
                      var h3 = hash().update('A').update('B').digest(e);
                      if (h !== h2) {throw new Error(`digest().toString($e):$h != digest($e):$h2`)};
                      if (h !== h3) {throw new Error(`digest().toString($e):$h != update('A').update('B').digest($e):$h3`)};
                      return h;
                  }).join(',')
              }
          )(),
          expected: "d53400095496267cf02e5dbd4b0bf9fbfb5f36f311ea7d9809af5487421743e3,1TQACVSWJnzwLl29Swv5+/tfNvMR6n2YCa9Uh0IXQ+M=,1TQACVSWJnzwLl29Swv5-_tfNvMR6n2YCa9Uh0IXQ-M" },

        { id: 6, value: (
              function() {
                  var hash = cr.createHmac;
                  return ['', 'abc'.repeat(100)].map(v => {
                      return ['md5', 'sha1', 'sha256'].map(h => {
                          return hash(h, Buffer.from('secret')).update(v).digest('hex');
                       }).join(',');
                  }).join(',');
              }
          )(),
          expected: "5c8db03f04cec0f43bcb060023914190,"+
              "25af6174a0fcecc4d346680a72b7ce644b9a88e8,"+
              "f9e66e179b6747ae54108f82f8ade8b3c25d76fd30afde6c395822c530196169,"+
              "91eb74a225cdd3bbfccc34396c6e3ac5,"+
              "0aac71e3a813a7acc4a809cfdedb2ecba04ffc5e,"+
              "8660d2d51d6f20f61d5aadfb6c43df7fd05fc2fc4967d8aec1846f3d9ec03987"},

        { id: 7, value: (
              function() {
                  var h = cr.createHmac('sha1', '');
                  var Hmac = h.constructor;
                  return Hmac('sha1', '').digest('hex');
              }
          )(),
          expected: "fbdb1d1b18aa6c08324b7d64b71fb76370690e1d" },


        { id: 8, value: (
              function() {
                  var h = cr.createHmac('sha1','');
                  return h.digest() instanceof Buffer;
              }
          )(),
          expected: true },

        { id: 9, value: (
              function() {
                  var h = cr.createHmac('sha256', 'A'.repeat(64));
                  return h.update('AB').digest('hex');
              }
          )(),
          expected: "ee9dce43b12eb3e865614ad9c1a8d4fad4b6eac2b64647bd24cd192888d3f367" },

        { id: 10, value: (
              function() {
                  var h = cr.createHmac('sha256', 'A'.repeat(100));
                  return h.update('AB').digest('hex');
              }
          )(),
          expected: "5647b6c429701ff512f0f18232b4507065d2376ca8899a816a0a6e721bf8ddcc" },

        { id: 11, value: (
              function() {
                  try {
                      cr.createHmac();
                  } catch(e) {
                      return /algorithm/.test(e.message) || /must be/.test(e.message);
                  }
              }
          )(),
          expected: true },

        { id: 12, value: (
              function() {
                  try {
                      cr.createHmac([]);
                  } catch(e) {
                      return /algorithm/.test(e.message) ||  /must be/.test(e.message);
                  }
              }
          )(),
          expected: true },

        { id: 13, value: (
              function() {
                  try {
                      cr.createHmac('sha512','');
                  } catch(e) {
                      return /supported/.test(e.message); // our implemenation njs/qjs
                  }
                  // node fall to 'buffer'
                  return true;
              }
          )(),
          expected: true },

        { id: 14, value: (
              function() {
                  try {
                      cr.createHmac('sha1',[]);
                  } catch(e) {
                      return /key/.test(e.message); // our implemenation njs/qjs
                  }
                  // node fall to 'buffer'
                  return true;
              }
          )(),
          expected: true },

        { id: 15, value: (
              function() {
                  var h = cr.createHmac('sha1', 'secret key');
                  try {
                      h.update('A').digest('hex'); h.digest('hex');
                  } catch(e) {
                      return /Digest already called/.test(e.message);
                  }
                  // 2nd pass for nodejs
                  try {
                      h.update('A').digest('hex'); h.digest('hex');
                  } catch(e) {
                      return /Digest already called/.test(e.message);
                  }
              }
          )(),
          expected: true },

        { id: 16, value: (
              function() {
                  var h = cr.createHmac('sha1', 'secret key');
                  try {
                      h.update('A').digest('hex'); h.update('B');
                  } catch(e) {
                      return /Digest already called/.test(e.message);
                  }
                  // 2nd pass for nodejs
                  try {
                      h.update('A').digest('hex'); h.update('B');
                  } catch(e) {
                      return /Digest already called/.test(e.message);
                  }
              }
          )(),
          expected: true },

        { id: 17, value: (
              function() {
                  return typeof cr.createHmac('sha1', 'secret key');
              }
          )(),
          expected: "object" },

        { id: 18, value: (
              function() {
                  var h = cr.createHash('sha1');
                  try {
                      h.update.call(cr.createHmac('sha1', 's'), '')
                  } catch(e) {
                      return /is not a hash object/.test(e.message);
                  }

                  return true;
              }
          )(),
          expected: true },
]};


run([
    createHash_tsuite,
    createHmac_tsuite
])
.then($DONE, $DONE);
