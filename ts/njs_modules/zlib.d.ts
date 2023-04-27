/// <reference path="../njs_core.d.ts" />

declare module "zlib" {
    interface NjsZlibOptions {
        /**
         * the buffer size for feeding data to and pulling data
         * from the zlib routines, defaults to 1024.
         */
        chunkSize?: number;

        /**
         * The dictionary buffer.
         */
        dictionary?: NjsStringOrBuffer;

        /**
         * Compression level, from zlib.constants.Z_NO_COMPRESSION to
         * zlib.constants.Z_BEST_COMPRESSION. Defaults to
         * zlib.constants.Z_DEFAULT_COMPRESSION.
         */
        level?: number;

        /**
         * Specifies how much memory should be allocated for the internal compression state.
         * 1 uses minimum memory but is slow and reduces compression ratio;
         * 9 uses maximum memory for optimal speed.
         * The default value is 8.
         */
        memLevel?: number;

        /**
         * The compression strategy, defaults to zlib.constants.Z_DEFAULT_STRATEGY.
         */
        strategy?: number;

        /**
         * The log2 of window size.
         * -15 to -9 for raw data, from 9 to 15 for an ordinary stream.
         */
        windowBits?: number;
    }

    type NjsZlibConstants = {
        /**
         * No compression.
         */
        Z_NO_COMPRESSION: number;

        /**
         * Fastest, produces the least compression.
         */
        Z_BEST_SPEED: number;

        /**
         * Trade-off between speed and compression.
         */
        Z_DEFAULT_COMPRESSION: number;

        /**
         * Slowest, produces the most compression.
         */
        Z_BEST_COMPRESSION: number;

        /**
         * Filtered strategy: for the data produced by a filter or predictor.
         */
        Z_FILTERED: number;

        /**
         * Huffman-only strategy: only Huffman encoding, no string matching.
         */
        Z_HUFFMAN_ONLY: number;

        /**
         * Run Length Encoding strategy: limit match distances to one,
         * better compression of PNG image data.
         */
        Z_RLE: number;

        /**
         * Fixed table strategy: prevents the use of dynamic Huffman codes,
         * a simpler decoder for special applications.
         */
        Z_FIXED: number;

        /**
         * Default strategy, suitable for general purpose compression.
         */
        Z_DEFAULT_STRATEGY: number;
    };

    interface Zlib {
        /**
         * Compresses data using deflate, and do not append a zlib header.
         *
         * @param data - The data to be compressed.
         */
        deflateRawSync(data: NjsStringOrBuffer, options?:NjsZlibOptions): Buffer;

        /**
         * Compresses data using deflate.
         *
         * @param data - The data to be compressed.
         */
        deflateSync(data: NjsStringOrBuffer, options?:NjsZlibOptions): Buffer;

        /**
         * Decompresses a raw deflate stream.
         *
         * @param data - The data to be decompressed.
         */
        inflateRawSync(data: NjsStringOrBuffer, options?:NjsZlibOptions): Buffer;

        /**
         * Decompresses a deflate stream.
         *
         * @param data - The data to be decompressed.
         */
        inflateSync(data: NjsStringOrBuffer, options?:NjsZlibOptions): Buffer;

        constants: NjsZlibConstants;
    }

    const zlib: Zlib;

    export default zlib;
}
