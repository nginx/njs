/// <reference path="../njs_core.d.ts" />

declare module "crypto" {

    export type Algorithm = "md5" | "sha1" | "sha256";

    export type DigestEncoding = Exclude<BufferEncoding, "utf8">;

    export interface Hash {
        /**
         * Updates the hash content with the given `data` and returns self.
         */
        update(data: NjsStringOrBuffer): Hash;

        /**
         * Calculates the digest of all of the data passed using `hash.update()`.
         *
         * @example
         *   import cr from 'crypto'
         *   cr.createHash('sha1').update('A').update('B').digest('base64url')  // => 'BtlFlCqiamG-GMPiK_GbvKjdK10'
         *
         * @param encoding The encoding of the return value. If not provided, a `Buffer` object
         *   (or a byte string before version 0.4.4) is returned.
         * @return A calculated digest.
         */
        digest(): Buffer;
        digest(encoding: DigestEncoding): string;
    }

    export interface Hmac {
        /**
         * Updates the HMAC content with the given `data` and returns self.
         */
        update(data: NjsStringOrBuffer): Hmac;

        /**
         * Calculates the HMAC digest of all of the data passed using `hmac.update()`.
         *
         * @example
         *   import cr from 'crypto'
         *   cr.createHmac('sha1', 'secret.key').update('AB').digest('base64url')  // => 'Oglm93xn23_MkiaEq_e9u8zk374'
         *
         * @param encoding The encoding of the return value. If not provided, a `Buffer` object
         *   (or a byte string before version 0.4.4) is returned.
         * @return The calculated HMAC digest.
         */
        digest(): Buffer;
        digest(encoding: DigestEncoding): string;
    }

    interface Crypto {
        /**
         * Creates and returns a `Hash` object that can be used to generate hash digests using
         * the given `algorithm`.
         *
         * @param algorithm `'md5'`, `'sha1'`, or `'sha256'`
         * @returns A `Hash` object.
         */
        createHash(algorithm: Algorithm): Hash;

        /**
         * Creates and returns an HMAC object that uses the given `algorithm` and secret `key`.
         *
         * @param algorithm `'md5'`, `'sha1'`, or `'sha256'`
         * @param key The secret key.
         * @returns An `HMAC` object.
         */
        createHmac(algorithm: Algorithm, key: NjsStringOrBuffer): Hmac;
    }

    const crypto: Crypto;

    // It's exported like this because njs doesn't support named imports.
    // TODO: Replace NjsFS with individual named exports as soon as njs supports named imports.
    export default crypto;
}
