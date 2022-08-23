type BufferEncoding = "utf8" | "hex" | "base64" | "base64url";

interface StringConstructor {
    /**
     * Creates a byte string from an encoded string.
     *
     * @deprecated will be removed in the future.
     */
    bytesFrom(bytes: string, encoding: Exclude<BufferEncoding, "utf8">): NjsByteString;
    /**
     * Creates a byte string from an array that contains octets.
     *
     * @deprecated will be removed in the future.
     */
    bytesFrom(bytes: Array<number>): NjsByteString;
}

interface String {
    /**
     * Serializes a Unicode string with code points up to 255
     * into a byte string, otherwise, null is returned.
     *
     * @deprecated will be removed in the future.
     */
    toBytes(start?: number, end?: number): NjsByteString | null;
    /**
     * Serializes a Unicode string to a byte string using UTF8 encoding.
     *
     * @deprecated will be removed in the future.
     */
    toUTF8(start?: number, end?: number): NjsByteString;
}

type NjsByteString = string & {
    /**
     * Returns a new Unicode string from a byte string where each byte is replaced
     * with a corresponding Unicode code point.
     *
     * @deprecated will be removed in the future.
     */
    fromBytes(start?: number, end?: number): string;
    /**
     * Converts a byte string containing a valid UTF8 string into a Unicode string,
     * otherwise null is returned.
     *
     * @deprecated will be removed in the future.
     */
    fromUTF8(start?: number, end?: number): string | null;
    /**
     * Encodes a byte string to hex, base64, or base64url.
     *
     * @deprecated will be removed in the future.
     */
    toString(encoding: Exclude<BufferEncoding, "utf8">): string;
};

type NjsStringLike = string | NjsByteString;

type TypedArray =
    | Uint8Array
    | Uint8ClampedArray
    | Uint16Array
    | Uint32Array
    | Int8Array
    | Int16Array
    | Int32Array
    | Float32Array
    | Float64Array;

/**
 * Raw data is stored in instances of the `Buffer` class.
 */
declare class Buffer extends Uint8Array {
    /**
     * Allocates a new `Buffer` of a specified `size`.
     *
     * @param size The count of octets to allocate.
     * @param fill If specified, the allocated `Buffer` will be initialized by calling `buf.fill(fill)`.
     *   Otherwise, the `Buffer` will be zero-filled.
     * @param encoding The character encoding used for call to `buf.fill(fill, encoding)` while
     *   initalizing. Defaults to`'utf8'`.
     */
    static alloc(size: number, fill?: NjsStringLike | Uint8Array | number, encoding?: BufferEncoding): Buffer;
    /**
     * The same as `Buffer.alloc()`, with the difference that the memory allocated for the buffer
     * is not initialized, the contents of the new buffer is unknown and may contain sensitive data.
     *
     * @param size The count of octets to allocate.
     */
    static allocUnsafe(size: number): Buffer;

    /**
     * Returns the byte length of the specified `value`, when encoded using `encoding`.
     *
     * @param value The value to test.
     * @param encoding The character encoding used to evaluate `value` if `value` is a `string`.
     *   Defaults to `'utf8'`.
     */
    static byteLength(value: NjsStringLike | Buffer | TypedArray | DataView | ArrayBuffer, encoding?: BufferEncoding): number;

    /**
     * Compares `buffer1` with `buffer2` when sorting arrays of buffer instances.
     *
     * @return
     * - `0` if `buffer2` is the same as `buffer1`,
     * - `1` if `buffer2` should come _before_ `buffer1` when sorted,
     * - `-1` if `buffer2` should come _after_ `buffer1` when sorted.
     */
    static compare(buf1: Uint8Array, buf2: Uint8Array): -1 | 0 | 1;

    /**
     * Returns a new `Buffer` which is the result of concatenating all the `Buffer` instances in
     * the `list`. If there are no items in the `list` or the total length is 0, a new zero-length
     * `Buffer` is returned.
     *
     * @param list An array of `Buffer` or `Uint8Array` objects to concatenate.
     * @param totalLength Total length of the buffers when concatenated, coerced to an unsigned
     *   integer. If not specified, it is calculated from the `Buffer` instances in `list` by adding
     *   their lengths. If the combined length of the Buffers in list exceeds `totalLength`, the
     *   result is truncated to `totalLength`.
     */
    static concat(list: Uint8Array[], totalLength?: number): Buffer;

    /**
     * @param arrayBuffer The `.buffer` property of any `TypedArray` or a `new ArrayBuffer()`.
     * @param byteOffset An integer specifying the index of the first byte to expose. Defaults to `0`.
     * @param length An integer specifying number of bytes to expose.
     *   Defaults to `arrayBuffer.byteLength - byteOffset`.
     */
    static from(arrayBuffer: ArrayBuffer, byteOffset?: number, length?: number): Buffer;
    /**
     * Allocates a new `Buffer` using an array of bytes in the range `0 – 255`. Array entries
     * outside that range will be truncated.
     *
     * @param data The data to create a new `Buffer`.
     */
    static from(data: number[]): Buffer;
    /**
     * Copies the passed buffer `data` onto a new `Buffer` instance.
     *
     * @param data The buffer to copy.
     */
    static from(data: Uint8Array): Buffer;
    /**
     * For objects whose `valueOf()` function returns a value not strictly equal to object, returns
     * `Buffer.from(object.valueOf(), offsetOrEncoding, length)`.
     *
     * @param obj An object supporting `valueOf()`.
     */
    static from(obj: { valueOf(): NjsStringLike | object }, byteOffset?: number, length?: number): Buffer;
    /**
     * Creates a new `Buffer` with a string `str`.
     *
     * @param str The string to create a new `Buffer`.
     * @param encoding The character encoding to be used when converting a string into bytes.
     *   Defaults to `'utf8'`.
     */
    static from(str: NjsStringLike, encoding?: BufferEncoding): Buffer;

    /**
     * Returns true if the `obj` is a `Buffer`.
     *
     * @param obj The object to test.
     */
    static isBuffer(obj: any): obj is Buffer;

    /**
     * Returns `true` if `encoding` is the name of a supported character encoding.
     *
     * @param encoding The string to test.
     */
    static isEncoding(encoding: NjsStringLike): encoding is BufferEncoding;

    /**
     * The underlying `ArrayBuffer` object based on which this `Buffer` object is created.
     */
    readonly buffer: ArrayBuffer;
    /**
     * Specifies the `byteOffset` of the Buffer's underlying `ArrayBuffer` object.
     */
    readonly byteOffset: number;
    /**
     * The number of bytes in this buffer.
     */
    readonly length: number;

    /**
     * Constructor cannot be called.
     */
    private constructor();

    /**
     * The index operator can be used to get and set the octet at position index in buffer.
     * The values refer to individual bytes, so the legal value range is between 0 and 255 (decimal).
     */
    [index: number]: number;

    /**
     * Compares this buffer (source) with the `target` and returns a number indicating whether this
     * buffer comes before, after, or is the same as the `target` in sort order. Comparison is based
     * on the actual sequence of bytes in each `Buffer`.
     *
     * @param target The target buffer for comparison.
     * @param targetStart An integer specifying the offset within `target` at which to begin
     *   comparison. Defaults to `0`.
     * @param targetEnd An integer specifying the offset within `target` at which to end comparison.
     *   Defaults to `target.length`.
     * @param sourceStart An integer specifying the offset within this buffer at which to begin
     *   comparison. Defaults to `0`.
     * @param sourceEnd An integer specifying the offset within this buffer at which to end comparison
     *   (not inclusive). Defaults to `buf.length`.
     * @return
     * - `0` if `target` is the same as this buffer,
     * - `1` if `target` should come _before_ this buffer when sorted,
     * - `-1` if `target` should come _after_ this buffer when sorted.
     */
    compare(target: Uint8Array, targetStart?: number, targetEnd?: number, sourceStart?: number, sourceEnd?: number): -1 | 0 | 1;

    /**
     * Copies data from a region of this buffer to a region in `target`, even if the `target`
     * memory region overlaps with this buffer.
     *
     * @param target The target buffer.
     * @param targetStart An integer specifying the offset within `target` at which to begin writing.
     *   Defaults to `0`.
     * @param sourceStart An integer specifying the offset within this buffer from which to begin
     *   copying. Defaults to `0`.
     * @param sourceEnd An integer specifying the offset within this buffer at which to stop copying
     *   (not inclusive). Defaults to `buf.length`.
     * @return The number of bytes copied.
     */
    copy(target: Uint8Array, targetStart?: number, sourceStart?: number, sourceEnd?: number): number;

    /**
     * Returns `true` if both this buffer and `other` buffer have exactly the same bytes.
     *
     * @param other The other buffer to compare with.
     */
    equals(other: Uint8Array): boolean;

    /**
     * Fills this buffer with the specified `value`. If the `offset` and `end` are not specified,
     * the entire buffer will be filled. The `value` is coerced to `uint32` if it is not a `string`,
     * `Buffer`, or `integer`. If the resulting integer is greater than `255`, the buffer will be
     * filled with `value` and `255`.
     *
     * @param value The value with which to fill this buffer.
     * @param offset Number of bytes to skip before starting to fill this buffer. Defaults to `0`.
     * @param end Where to stop filling this buffer (not inclusive). Defaults to `buf.length`.
     * @param encoding The encoding for `value` if `value` is a `string`. Defaults to `'utf8'`.
     */
    fill(value: NjsStringLike | Uint8Array | number, offset?: number, end?: number, encoding?: BufferEncoding): this;

    /**
     * Equivalent to `buf.indexOf() !== -1`, returns `true` if the `value` was found in this buffer.
     *
     * @param value What to search for. If a `number`, it must be between `0` and `255`.
     * @param byteOffset Where to begin search in this buffer. Defaults to `0`.
     * @param encoding The encoding for `value` if `value` is a `string`. Defaults to `'utf8'`.
     */
    includes(value: NjsStringLike | number | Uint8Array, byteOffset?: number, encoding?: BufferEncoding): boolean;

    /**
     * Returns an integer which is the index of the first occurrence of `value` in this buffer,
     * or `-1` if this buffer does not contain `value`.
     *
     * @param value What to search for. If a `number`, it must be between `0` and `255`.
     * @param byteOffset Where to begin search in this buffer. Defaults to `0`.
     * @param encoding The encoding for `value` if `value` is a `string`. Defaults to `'utf8'`.
     */
    indexOf(value: NjsStringLike | number | Uint8Array, byteOffset?: number, encoding?: BufferEncoding): number;
    /**
     * The same as `buf.indexOf()`, except the last occurrence of the `value` is found instead of
     * the first occurrence. If the `value` is an empty `string` or empty `Buffer`, `byteOffset`
     * will be returned.
     *
     * @param value What to search for. If a `number`, it must be between `0` and `255`.
     * @param byteOffset Where to begin search in this buffer. Defaults to `0`.
     * @param encoding The encoding for `value` if `value` is a `string`. Defaults to `'utf8'`.
     */
    lastIndexOf(value: NjsStringLike | number | Uint8Array, byteOffset?: number, encoding?: BufferEncoding): number;

    /**
     * Reads the `byteLength` from this buffer at the specified `offset` and interprets the result
     * as a big-endian, two's complement signed value supporting up to 48 bits of accuracy.
     *
     * @param offset An integer specifying the number of bytes to skip before starting to read.
     *   Must satisfy `0 <= offset <= buf.length - byteLength`.
     * @param byteLength An integer between `1` and `6` specifying the number of bytes to read.
     *   Must satisfy `0 < byteLength <= 6`.
     */
    readIntBE(offset: number, byteLength: number): number;
    /**
     * @see {Buffer.prototype.readIntBE}.
     */
    readInt8(offset?: number): number;
    /**
     * @see {Buffer.prototype.readIntBE}.
     */
    readInt16BE(offset?: number): number;
    /**
     * @see {Buffer.prototype.readIntBE}.
     */
    readInt32BE(offset?: number): number;
    /**
     * Reads the `byteLength` from this buffer at the specified `offset` and interprets the result
     * as a little-endian, two's complement signed value supporting up to 48 bits of accuracy.
     *
     * @param offset An integer specifying the number of bytes to skip before starting to read.
     *   Must satisfy `0 <= offset <= buf.length - byteLength`.
     * @param byteLength An integer between `1` and `6` specifying the number of bytes to read.
     *   Must satisfy `0 < byteLength <= 6`.
     */
    readIntLE(offset: number, byteLength: number): number;
    /**
     * @see {Buffer.prototype.readIntLE}.
     */
    readInt16LE(offset?: number): number;
    /**
     * @see {Buffer.prototype.readIntLE}.
     */
    readInt32LE(offset?: number): number;

    /**
     * Reads the `byteLength` from this buffer at the specified `offset` and interprets the result
     * as a big-endian integer supporting up to 48 bits of accuracy.
     *
     * @param offset An integer specifying the number of bytes to skip before starting to read.
     *   Must satisfy `0 <= offset <= buf.length - byteLength`.
     * @param byteLength An integer between `1` and `6` specifying the number of bytes to read.
     *   Must satisfy `0 < byteLength <= 6`.
     */
    readUIntBE(offset: number, byteLength: number): number;
    /**
     * @see {Buffer.prototype.readUIntBE}.
     */
    readUInt8(offset?: number): number;
    /**
     * @see {Buffer.prototype.readUIntBE}.
     */
    readUInt16BE(offset?: number): number;
    /**
     * @see {Buffer.prototype.readUIntBE}.
     */
    readUInt32BE(offset?: number): number;
    /**
     * Reads the `byteLength` from this buffer at the specified `offset` and interprets the result
     * as a little-endian integer supporting up to 48 bits of accuracy.
     *
     * @param offset An integer specifying the number of bytes to skip before starting to read.
     *   Must satisfy `0 <= offset <= buf.length - byteLength`.
     * @param byteLength An integer between `1` and `6` specifying the number of bytes to read.
     *   Must satisfy `0 < byteLength <= 6`.
     */
    readUIntLE(offset: number, byteLength: number): number;
    /**
     * @see {Buffer.prototype.readUIntLE}.
     */
    readUInt16LE(offset?: number): number;
    /**
     * @see {Buffer.prototype.readUIntLE}.
     */
    readUInt32LE(offset?: number): number;

    /**
     * Reads a 64-bit, big-endian double from this buffer at the specified `offset`.
     *
     * @param offset An integer specifying the number of bytes to skip before starting to read.
     *   Must satisfy `0 <= offset <= buf.length - 8`. Defaults to `0`.
     */
    readDoubleBE(offset?: number): number;
    /**
     * Reads a 64-bit, little-endian double from this buffer at the specified `offset`.
     *
     * @param offset An integer specifying the number of bytes to skip before starting to read.
     *   Must satisfy `0 <= offset <= buf.length - 8`. Defaults to `0`.
     */
    readDoubleLE(offset?: number): number;

    /**
     * Reads a 32-bit, big-endian float from this buffer at the specified `offset`.
     *
     * @param offset An integer specifying the number of bytes to skip before starting to read.
     *   Must satisfy `0 <= offset <= buf.length - 4`. Defaults to `0`.
     */
    readFloatBE(offset?: number): number;
    /**
     * Reads a 32-bit, little-endian float from this buffer at the specified `offset`.
     *
     * @param offset An integer specifying the number of bytes to skip before starting to read.
     *   Must satisfy `0 <= offset <= buf.length - 4`. Defaults to `0`.
     */
    readFloatLE(offset?: number): number;

    /**
     * Returns a new `Buffer` that references **the same memory as the original**, but offset and
     * cropped by `start` and `end`.
     *
     * @param start Where the new `Buffer` will start. Defaults to `0`.
     * @param end Where the new `Buffer` will end (not inclusive). If `end` is greater than
     *   `buf.length`, the same result as that of end equal to `buf.length` is returned.
     *   Defaults to `buf.length`.
     */
    subarray(start?: number, end?: number): Buffer;

    /**
     * Returns a new `Buffer` that references **the same memory as the original**, but offset and
     * cropped by the `start` and end `values`.
     *
     * @param start Where the new `Buffer` will start. Defaults to `0`.
     * @param end Where the new `Buffer` will end (not inclusive). Defaults to `buf.length`.
     */
    slice(begin?: number, end?: number): Buffer;

    /**
     * Interprets this buffer as an array of unsigned 16-bit numbers and swaps the byte order
     * in-place.
     *
     * @throws {RangeError} if `buf.length` is not a multiple of 2.
     */
    swap16(): Buffer;
    /**
     * Interprets this buffer as an array of unsigned 32-bit numbers and swaps the byte order
     * in-place.
     *
     * @throws {RangeError} if `buf.length` is not a multiple of 4.
     */
    swap32(): Buffer;
    /**
     * Interprets this buffer as an array of 64-bit numbers and swaps byte order in-place.
     *
     * @throws {RangeError} if `buf.length` is not a multiple of 8.
     */
    swap32(): Buffer;

    /**
     * Returns a JSON representation of this buffer. `JSON.stringify()` implicitly calls this
     * function when stringifying a `Buffer` instance.
     */
    toJSON(): { type: "Buffer"; data: number[] };

    /**
     * Decodes this buffer to a string according to the specified character `encoding`.
     *
     * @param encoding The character encoding. Defaults to `'utf8'`.
     * @param start The byte offset to start decoding at. Defaults to `0`.
     * @param end The byte offset to stop decoding at (not inclusive). Defaults to `buf.length`.
     */
    toString(encoding?: BufferEncoding, start?: number, end?: number): string;

    /**
     * Writes a string `str` to this buffer at the `offset` according to the character `encoding`.
     * If this buffer did not contain enough space to fit the entire string, only part of the
     * string will be written, however, partially encoded characters will not be written.
     *
     * @param str The string to write into this buffer.
     * @param offset An integer specifying the number of bytes to skip before starting to write `str`.
     *   Defaults to `0`.
     * @param length An integer specifying the number of bytes to write.
     *   Defaults to `buf.length - offset`.
     * @param encoding The character encoding of `str`. Defaults to `'utf8'`.
     * @return Offset plus the number of bytes written.
     */
    write(str: NjsStringLike, encoding?: BufferEncoding): number;
    write(str: NjsStringLike, offset: number, encoding?: BufferEncoding): number;
    write(str: NjsStringLike, offset: number, length: number, encoding?: BufferEncoding): number;

    /**
     * Writes `byteLength` bytes of `value` to this buffer at the specified `offset` as big-endian.
     * Supports up to 48 bits of accuracy.
     *
     * @param value The number to be written into this buffer.
     * @param offset An integer specifying the number of bytes to skip before starting to write.
     *   Must satisfy `0 <= offset <= buf.length - byteLength`.
     * @param byteLength An integer between `1` and `6` specifying the number of bytes to read.
     *   Must satisfy `0 < byteLength <= 6`.
     * @return Offset plus the number of bytes written.
     */
    writeIntBE(value: number, offset: number, byteLength: number): number;
    /**
     * @see {Buffer.prototype.writeIntBE}.
     */
    writeInt8(value: number, offset?: number): number;
    /**
     * @see {Buffer.prototype.writeIntBE}.
     */
    writeInt16BE(value: number, offset?: number): number;
    /**
     * @see {Buffer.prototype.writeIntBE}.
     */
    writeInt32BE(value: number, offset?: number): number;
    /**
     * Writes `byteLength` bytes of `value` to this buffer at the specified `offset` as
     * little-endian. Supports up to 48 bits of accuracy.
     *
     * @param value The number to be written into this buffer.
     * @param offset An integer specifying the number of bytes to skip before starting to write.
     *   Must satisfy `0 <= offset <= buf.length - byteLength`.
     * @param byteLength An integer between `1` and `6` specifying the number of bytes to read.
     *   Must satisfy `0 < byteLength <= 6`.
     * @return Offset plus the number of bytes written.
     */
    writeIntLE(value: number, offset: number, byteLength: number): number;
    /**
     * @see {Buffer.prototype.writeIntLE}.
     */
    writeInt16LE(value: number, offset?: number): number;
    /**
     * @see {Buffer.prototype.writeIntLE}.
     */
    writeInt32LE(value: number, offset?: number): number;

    /**
     * Writes `byteLength` bytes of `value` to this buffer at the specified `offset` as big-endian.
     * Supports up to 48 bits of accuracy.
     *
     * @param value The number to be written into this buffer.
     * @param offset An integer specifying the number of bytes to skip before starting to write.
     *   Must satisfy `0 <= offset <= buf.length - byteLength`.
     * @param byteLength An integer between `1` and `6` specifying the number of bytes to read.
     *   Must satisfy `0 < byteLength <= 6`.
     * @return Offset plus the number of bytes written.
     */
    writeUIntBE(value: number, offset: number, byteLength: number): number;
    /**
     * @see {Buffer.prototype.writeUIntBE}.
     */
    writeUInt8(value: number, offset?: number): number;
    /**
     * @see {Buffer.prototype.writeUIntBE}.
     */
    writeUInt16BE(value: number, offset?: number): number;
    /**
     * @see {Buffer.prototype.writeUIntBE}.
     */
    writeUInt32BE(value: number, offset?: number): number;
    /**
     * Writes `byteLength` bytes of `value` to this buffer at the specified `offset` as
     * little-endian. Supports up to 48 bits of accuracy.
     *
     * @param value The number to be written into this buffer.
     * @param offset An integer specifying the number of bytes to skip before starting to write.
     *   Must satisfy `0 <= offset <= buf.length - byteLength`.
     * @param byteLength An integer between `1` and `6` specifying the number of bytes to read.
     *   Must satisfy `0 < byteLength <= 6`.
     * @return Offset plus the number of bytes written.
     */
    writeUIntLE(value: number, offset: number, byteLength: number): number;
    /**
     * @see {Buffer.prototype.writeUIntLE}.
     */
    writeUInt16LE(value: number, offset?: number): number;
    /**
     * @see {Buffer.prototype.writeUIntLE}.
     */
    writeUInt32LE(value: number, offset?: number): number;

    /**
     * Writes the `value` to this buffer at the specified `offset` as big-endian.
     *
     * @param value The number to be written into this buffer.
     * @param offset An integer specifying the number of bytes to skip before starting to write.
     *   Must satisfy `0 <= offset <= buf.length - 8`. Defaults to `0`.
     * @return Offset plus the number of bytes written.
     */
    writeDoubleBE(value: number, offset?: number): number;
    /**
     * Writes the `value` to this buffer at the specified `offset` as little-endian.
     *
     * @param value The number to be written into this buffer.
     * @param offset An integer specifying the number of bytes to skip before starting to write.
     *   Must satisfy `0 <= offset <= buf.length - 8`. Defaults to `0`.
     * @return Offset plus the number of bytes written.
     */
    writeDoubleLE(value: number, offset?: number): number;

    /**
     * Writes the `value` to this buffer at the specified `offset` as big-endian.
     *
     * @param value The number to be written into this buffer.
     * @param offset An integer specifying the number of bytes to skip before starting to write.
     *   Must satisfy `0 <= offset <= buf.length - 4`. Defaults to `0`.
     * @return Offset plus the number of bytes written.
     */
    writeFloatBE(value: number, offset?: number): number;
    /**
     * Writes the `value` to this buffer at the specified `offset` as little-endian.
     *
     * @param value The number to be written into this buffer.
     * @param offset An integer specifying the number of bytes to skip before starting to write.
     *   Must satisfy `0 <= offset <= buf.length - 4`. Defaults to `0`.
     * @return Offset plus the number of bytes written.
     */
    writeFloatLE(value: number, offset?: number): number;
}

type NjsStringOrBuffer = NjsStringLike | Buffer | DataView | TypedArray | ArrayBuffer;
type NjsBuffer = Buffer | DataView | TypedArray;

// Global objects

interface NjsGlobal {
    /**
     * Returns current njs version as a string.
     * For example, '0.7.4'.
     */
    readonly version: string;
    /**
     * Returns a number with the current version of njs.
     * For example, “0.7.4” is returned as 0x000704.
     * @since 0.7.4
     */
    readonly version_number: number;
    dump(value: any, indent?: number): string;
    /**
     * Registers a callback for the "exit" event. The callback is called before
     * the VM is destroyed.
     */
    on(event: "exit", callback: () => void): void;
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

/**
 * A value returned by `setTimeout()` and `setImmediate()` functions. It's an positive integer now,
 * but this may be changed in future, so it should be treated as an opaque value.
 */
type TimerHandle = number & { readonly '': unique symbol };

/**
 * Schedules the "immediate" execution of the given function after I/O events' callbacks.
 *
 * @param callback The function to call.
 * @param args Optional arguments to pass to the `callback` function.
 * @returns A value which identifies the timer created by the call.
 *
 * @throws {TypeError} if `callback` is not a function.
 * @throws {InternalError} if timers are not supported by host environment.
 */
declare function setImmediate<TArgs extends any[]>(callback: (...args: TArgs) => void, ...args: TArgs): TimerHandle;

/**
 * Schedules a timer which executes the given function after the specified delay.
 *
 * @param callback The function to call when the timer elapses.
 * @param delay The number of milliseconds to wait before calling the `callback`. Defaults to `0`,
 *   meaning execute "immediately", or more accurately, the next event cycle.
 * @param args Optional arguments to pass to the `callback` function.
 * @returns A value which identifies the timer created by the call; it can be passed to
 *   `clearTimeout()` to cancel the timeout.
 *
 * @throws {TypeError} if `callback` is not a function.
 * @throws {InternalError} if timers are not supported by host environment.
 */
declare function setTimeout<TArgs extends any[]>(callback: (...args: TArgs) => void, delay?: number, ...args: TArgs): TimerHandle;

/**
 * Cancels a timer previously established by calling `setTimeout()`.
 *
 * Note: Passing an invalid handle silently does nothing; no exception is thrown.
 *
 * @param handle A value returned by `setTimeout()`.
 */
declare function clearTimeout(handle?: TimerHandle): void;

/**
 * Decodes a string of data which has been encoded using Base64 encoding.
 *
 * @param encodedData is a binary string that contains Base64-encoded data.
 * @returns A string that contains decoded data from encodedData.
 */
declare function atob(encodedData: string): string;

/**
 * Creates a Base64-encoded ASCII string from a binary string.
 *
 * @param stringToEncode is a binary string to encode.
 * @returns A string containing the Base64 representation of stringToEncode.
 */
declare function btoa(stringToEncode: string): string;
