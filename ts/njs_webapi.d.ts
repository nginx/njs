/**
 * This class represents a decoder for a specific text encoding. Currently,
 * only `utf-8` is supported. A decoder takes a stream of bytes as input and
 * emits a stream of code points.
 *
 * @since 0.4.3
 */
declare class TextDecoder {
    /**
     * The name of the encoding used by this `TextDecoder`.
     */
    readonly encoding: "utf-8";
    /**
     * Whether the error mode is "fatal".
     */
    readonly fatal: boolean;
    /**
     * Whether the byte order marker is ignored.
     */
    readonly ignoreBOM: boolean;

    /**
     * Creates a new `TextDecoder` object for the specified encoding. Currently,
     * only `utf-8` is supported.
     */
    constructor(encoding?: "utf-8" | "utf8", options?: TextDecoderOptions);

    /**
     * Returns a string containing the text decoded with the method of the
     * specific `TextDecoder` object.
     *
     * The method can be invoked zero or more times with `options`'s `stream` set
     * to `true`, and then once without `options`'s stream (or set to `false`), to
     * process a fragmented input.
     *
     * If the error mode is `fatal` and `encoding`'s decoder returns an error, it
     * throws a `TypeError`.
     *
     * @example
     * ```
     * new TextDecoder().decode(new Uint8Array([206,177,206,178])) //=> αβ
     * ```
     *
     * @example
     * ```
     * const decoder = new TextDecoder("utf-8");
     * let buffer: ArrayBuffer;
     * let str = "";
     *
     * while (buffer = nextChunk()) {
     *     str += decoder.decode(buffer, { stream: true });
     * }
     * str += decoder.decode(); // end-of-queue
     * ```
     */
    decode(buffer?: ArrayBuffer, options?: TextDecodeOptions): string;
}

interface TextDecoderOptions {
    /**
     * The flag indicating if `TextDecoder.decode()` must throw the `TypeError`
     * exception when a coding error is found, by default is `false`.
     */
    fatal?: boolean;
}

interface TextDecodeOptions {
    /**
     * The flag indicating if additional data will follow in subsequent calls to
     * `decode()`: `true` if processing the data in chunks, and `false` for the
     * final chunk or if the data is not chunked. By default is `false`.
     */
    stream?: boolean;
}

/**
 * The `TextEncoder` object takes a stream of code points as input and emits a
 * stream of UTF-8 bytes.
 *
 * @since 0.4.3
 */
declare class TextEncoder {
    /**
     * Always returns `utf-8`.
     */
    readonly encoding: "utf-8";

    /**
     * Returns a newly constructed `TextEncoder` that will generate a byte stream
     * with UTF-8 encoding.
     */
    constructor();

    /**
     * Encodes given `input` string into a `Uint8Array` with UTF-8 encoded text.
     */
    encode(input?: string): Uint8Array;
    /**
     * Encodes given `source` string to UTF-8, puts the result into `destination`
     * `Uint8Array`, and returns an object indicating the progress of the
     * encoding.
     */
    encodeInto(source: string, destination: Uint8Array): TextEncoderEncodeIntoResult;
}

interface TextEncoderEncodeIntoResult {
    /**
     * The number of UTF-16 units of code from the source string converted to
     * UTF-8.
     */
    read: number;
    /**
     * The number of bytes modified in the destination `Uint8Array`.
     */
    written: number;
}
