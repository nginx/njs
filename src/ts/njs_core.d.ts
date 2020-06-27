interface StringConstructor {
    /**
     * Creates a byte string from an encoded string.
     */
    bytesFrom(bytes: string, encoding: "hex" | "base64" | "base64url"): NjsByteString;
    /**
     * Creates a byte string from an array that contains octets.
     */
    bytesFrom(bytes: Array<number>): NjsByteString;
}

interface String {
    /**
     * Serializes a Unicode string with code points up to 255
     * into a byte string, otherwise, null is returned.
     */
    toBytes(start?: number, end?: number): NjsByteString | null;
    /**
     * Serializes a Unicode string to a byte string using UTF8 encoding.
     */
    toUTF8(start?: number, end?: number): NjsByteString;
}

type NjsByteString = string & {
    /**
     * Returns a new Unicode string from a byte string where each byte is replaced
     * with a corresponding Unicode code point.
     */
    fromBytes(start?: number, end?: number): string;
    /**
     * Converts a byte string containing a valid UTF8 string into a Unicode string,
     * otherwise null is returned.
     */
    fromUTF8(start?: number, end?: number): string | null;
    /**
     * Encodes a byte string to hex, base64, or base64url.
     */
    toString(encoding: "hex" | "base64" | "base64url"): string;
};

type NjsStringLike = string | NjsByteString;

// Global objects

interface NjsGlobal {
    readonly version: string;
    dump(value: any, indent?: number): string;
}

declare const njs: NjsGlobal;

interface NjsEnv {
    readonly [prop: string]: NjsByteString;
}

interface NjsProcess {
    readonly pid: number;
    readonly ppid: number;
    readonly argv: string[];
    readonly env: NjsEnv;
}

declare const process: NjsProcess;
