interface  RsaOaepParams {
    name: "RSA-OAEP";
}

interface  AesCtrParams {
    name: "AES-CTR";
    counter: NjsStringOrBuffer;
    length: number;
}

interface  AesCbcParams {
    name: "AES-CBC";
    iv: NjsStringOrBuffer;
}

interface  AesGcmParams {
    name: "AES-GCM";
    iv: NjsStringOrBuffer;
    additionalData?: NjsStringOrBuffer;
    tagLength?: number;
}

type CipherAlgorithm =
    | RsaOaepParams
    | AesCtrParams
    | AesCbcParams
    | AesCbcParams;

type HashVariants = "SHA-256" | "SHA-384" | "SHA-512" | "SHA-1";

interface  RsaHashedImportParams {
    name: "RSASSA-PKCS1-v1_5" | "RSA-PSS" | "RSA-OAEP";
    hash: HashVariants;
}

interface  RsaHashedKeyGenParams {
    name: "RSASSA-PKCS1-v1_5" | "RSA-PSS" | "RSA-OAEP";
    hash: HashVariants;
    modulusLength: number;
    publicExponent: Uint8Array;
}

interface  EcKeyImportParams {
    name: "ECDSA" | "ECDH";
    namedCurve: "P-256" | "P-384" | "P-521";
}

interface EcKeyGenParams {
    name: "ECDSA" | "ECDH";
    namedCurve: "P-256" | "P-384" | "P-521";
}

interface  HmacImportParams {
    name: "HMAC";
    hash: HashVariants;
}

type AesVariants = "AES-CTR" | "AES-CBC" | "AES-GCM";

interface  AesImportParams {
    name: AesVariants;
}

type ImportAlgorithm =
    | RsaHashedImportParams
    | EcKeyImportParams
    | HmacImportParams
    | AesImportParams
    | AesVariants
    | "PBKDF2"
    | "HKDF"
    | "ECDH";

type GenerateAlgorithm =
    | RsaHashedKeyGenParams
    | EcKeyGenParams
    | HmacKeyGenParams
    | AesKeyGenParams;

type JWK =
    | { kty: "RSA"; }
    | { kty: "EC"; }
    | { kty: "oct"; };

type KeyData =
    | NjsStringOrBuffer
    | JWK;

interface   HkdfParams {
    name: "HKDF";
    hash: HashVariants;
    salt: NjsStringOrBuffer;
    info: NjsStringOrBuffer;
}

interface   Pbkdf2Params {
    name: "PBKDF2";
    hash: HashVariants;
    salt: NjsStringOrBuffer;
    interations: number;
}

interface   EcdhParams {
    name: "ECDH";
    public: CryptoKey;
}

type DeriveAlgorithm =
    | HkdfParams
    | Pbkdf2Params
    | EcdhParams;

interface   HmacKeyGenParams {
    name: "HMAC";
    hash: HashVariants;
}

interface   AesKeyGenParams {
    name: AesVariants;
    length: number;
}

type DeriveKeyAlgorithm =
    | HmacKeyGenParams
    | AesKeyGenParams;

interface   RsaPssParams {
    name: "RSA-PSS";
    saltLength: number;
}

interface   EcdsaParams {
    name: "ECDSA";
    hash: HashVariants;
}

type SignOrVerifyAlgorithm =
    | RsaPssParams
    | EcdsaParams
    | { name: "HMAC"; }
    | { name: "RSASSA-PKCS1-v1_5"; }
    | "HMAC"
    | "RSASSA-PKCS1-v1_5";

interface CryptoKey {
    /*
     * An object describing the algorithm for which this key can be used
     * and any associated extra parameters.
     * @since 0.8.0
     */
    readonly algorithm: GenerateAlgorithm;
    /*
     * A boolean value that is true if the key can be exported and false if not.
     * @since 0.8.0
     */
    readonly extractable: boolean;
    /*
     * A string value indicates which kind of key is represented by the object.
     *
     * It can have the following values:
     *  "secret": This key is a secret key for use with a symmetric algorithm.
     *  "private": This key is the private half of an asymmetric algorithm's CryptoKeyPair.
     *  "public": This key is the public half of an asymmetric algorithm's CryptoKeyPair.
     * @since 0.8.0
     */
    readonly type: string;

    /*
     * An array of strings indicating what this key can be used for.
     * Possible array values: "encrypt", "decrypt", "sign", "verify",
     *  "deriveKey", "deriveBits", "wrapKey", "unwrapKey".
     * @since 0.8.0
     */
    readonly usages: Array<string>;
}

type CryptoKeyPair = { privateKey: CryptoKey, publicKey: CryptoKey };

interface SubtleCrypto {
    /**
     * Decrypts encrypted data.
     *
     * @param algorithm Object specifying the algorithm to be used,
     *  and any extra parameters as required.
     * @param key CryptoKey containing the key to be used for decryption.
     * @param data Data to be decrypted.
     */
    decrypt(algorithm: CipherAlgorithm,
            key: CryptoKey,
            data: NjsStringOrBuffer): Promise<ArrayBuffer>;

    /**
     * Derives an array of bits from a base key.
     *
     * @param algorithm Object defining the derivation algorithm to use.
     * @param baseKey CryptoKey representing the input to the derivation algorithm.
     * @param length Number representing the number of bits to derive.
     */
    deriveBits(algorithm: DeriveAlgorithm,
               baseKey: CryptoKey,
               length: number): Promise<ArrayBuffer>;

    /**
     * Derives a secret key from a master key.
     *
     * @param algorithm Object defining the derivation algorithm to use.
     * @param baseKey CryptoKey representing the input to the derivation algorithm.
     * @param derivedKeyAlgorithm Object defining the algorithm the
     *  derived key will be used for.
     * @param extractable Unsupported.
     * @param usage Array indicating what can be done with the key.
     *  Possible array values: "encrypt", "decrypt", "sign", "verify",
     *  "deriveKey", "deriveBits", "wrapKey", "unwrapKey".
     */
    deriveKey(algorithm: DeriveAlgorithm,
              baseKey: CryptoKey,
              derivedKeyAlgorithm: DeriveKeyAlgorithm,
              extractable: boolean,
              usage: Array<string>): Promise<CryptoKey>;

    /**
     * Generates a digest of the given data.
     *
     * @param algorithm String defining the hash function to use.
     */
    digest(algorithm: HashVariants,
           data: NjsStringOrBuffer): Promise<ArrayBuffer>;

    /**
     * Encrypts data.
     *
     * @param algorithm Object specifying the algorithm to be used,
     *  and any extra parameters as required.
     * @param key CryptoKey containing the key to be used for encryption.
     * @param data Data to be encrypted.
     */
    encrypt(algorithm: CipherAlgorithm,
            key: CryptoKey,
            data: NjsStringOrBuffer): Promise<ArrayBuffer>;

    /**
     * Imports a key.
     *
     * @param format String describing the data format of the key to import.
     * Possible values: "raw", "pkcs8", "spki", "jwk" (since 0.7.10).
     * @param keyData Object containing the key in the given format.
     * @param algorithm Dictionary object defining the type of key to import
     *  and providing extra algorithm-specific parameters.
     * @param extractable Boolean indicating whether a key can be exported.
     * @param usage Array indicating what can be done with the key.
     *  Possible array values: "encrypt", "decrypt", "sign", "verify",
     *  "deriveKey", "deriveBits", "wrapKey", "unwrapKey".
     */
    importKey(format: "raw" | "pkcs8" | "spki" | "jwk",
              keyData: KeyData,
              algorithm: ImportAlgorithm,
              extractable: boolean,
              usage: Array<string>): Promise<CryptoKey>;

    /**
     * Exports a key.
     *
     * @since 0.7.10
     * @param format String describing the data format of the key to export.
     * Possible values: "raw", "pkcs8", "spki", "jwk".
     * @param key CryptoKey containing the key to be exported.
     */
    exportKey(format: "raw" | "pkcs8" | "spki" | "jwk",
              key: CryptoKey): Promise<ArrayBuffer|Object>;

    /**
     * Generates a key for symmetric algorithms.
     *
     * @since 0.7.10
     * @param algorithm Dictionary object defining the type of key to generate
     *  and providing extra algorithm-specific parameters.
     * @param extractable Boolean indicating whether a key can be exported.
     * @param usage Array indicating what can be done with the key.
     *  Possible array values: "encrypt", "decrypt", "sign", "verify",
     *  "deriveKey", "deriveBits", "wrapKey", "unwrapKey".
     */
    generateKey(algorithm: HmacKeyGenParams | AesKeyGenParams,
                extractable: boolean,
                usage: Array<string>): Promise<CryptoKey>;

    /**
     * Generates a key for asymmetric algorithms.
     *
     * @since 0.7.10
     * @param algorithm Dictionary object defining the type of key to generate
     *  and providing extra algorithm-specific parameters.
     * @param extractable Boolean indicating whether a key can be exported.
     * @param usage Array indicating what can be done with the key.
     *  Possible array values: "encrypt", "decrypt", "sign", "verify",
     *  "deriveKey", "deriveBits", "wrapKey", "unwrapKey".
     */
    generateKey(algorithm: RsaHashedKeyGenParams | EcKeyGenParams,
                extractable: boolean,
                usage: Array<string>): Promise<CryptoKeyPair>;

    /**
     * Generates a digital signature.
     *
     * @param algorithm String or object that specifies the signature
     *  algorithm to use and its parameters.
     * @param key CryptoKey containing the key to be used for signing.
     * @param data Data to be signed.
     */
    sign(algorithm: SignOrVerifyAlgorithm,
         key: CryptoKey,
         data: NjsStringOrBuffer): Promise<ArrayBuffer>;

    /**
     * Verifies a digital signature.
     *
     * @param algorithm String or object that specifies the signature
     *  algorithm to use and its parameters.
     * @param key CryptoKey containing the key to be used for verifying.
     * @param signature Signature to verify.
     * @param data Data to be verified.
     */
    verify(algorithm: SignOrVerifyAlgorithm,
           key: CryptoKey,
           signature: NjsStringOrBuffer,
           data: NjsStringOrBuffer): Promise<boolean>;
}

interface Crypto {
    readonly subtle: SubtleCrypto;
    getRandomValues(ta:TypedArray): TypedArray;
}

declare const crypto: Crypto;
