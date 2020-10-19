/// <reference path="../njs_core.d.ts" />

declare module "querystring" {

    export interface ParsedUrlQuery {
        [key: string]: string | string[] | undefined;
    }

    export interface ParsedUrlQueryInput {
        [key: string]: NjsStringLike | number | boolean | NjsStringLike[] | number[] | boolean[] | null | undefined;
    }

    interface ParseOptions {
        /**
         * Function used to decode percent-encoded characters in the query string.
         * Defaults to `querystring.unescape()`.
         */
        decodeURIComponent?: (str: NjsStringLike) => string;

        /**
         * The maximum number of keys to parse; defaults to `1000`.
         * The `0` value removes limitations for counting keys.
         */
        maxKeys?: number;
    }

    interface StringifyOptions {
        /**
         * The function to use when converting URL-unsafe characters to percent-encoding in the
         * query string; defaults to `querystring.escape()`.
         */
        encodeURIComponent?: (str: NjsStringLike) => string;
    }

    interface QueryString {
        /**
         * Performs URL encoding of the given string `str`, returns an escaped query string.
         * The method is used by `querystring.stringify()` and should not be used directly.
         *
         * @param str The query string to escape.
         * @return The escaped query string.
         */
        escape(str: NjsStringLike): string;

        /**
         * Parses the query string URL and returns an object.
         *
         * By default, percent-encoded characters within the query string are assumed to use the
         * UTF-8 encoding, invalid UTF-8 sequences will be replaced with the `U+FFFD` replacement
         * character.
         *
         * @param query The query string.
         * @param separator The substring for delimiting key and value pairs in the query string; defaults to `'&'`.
         * @param equal The substring for delimiting keys and values in the query string, defaults to `'='`.
         * @param options An object optionally specifying `decodeURIComponent` function and `maxKeys` number.
         * @return An object containing the components of the query string.
         */
        parse(query: NjsStringLike, separator?: NjsStringLike, equal?: NjsStringLike, options?: ParseOptions): ParsedUrlQuery;

        /**
         * An alias for `querystring.parse()`.
         */
        decode(query: NjsStringLike, separator?: NjsStringLike, equal?: NjsStringLike, options?: ParseOptions): ParsedUrlQuery;

        /**
         * Serializes an object and returns a URL query string.
         *
         * By default, characters that require percent-encoding within the query string are encoded
         * as UTF-8. If other encoding is required, then `encodeURIComponent` option should be
         * specified.
         *
         * @param obj The data to convert to a query string.
         * @param separator The substring for delimiting key and value pairs in the query string; defaults to `'&'`.
         * @param equal The substring for delimiting keys and values in the query string; defaults to `'='`.
         * @param options An object optionally specifying `encodeURIComponent` function.
         * @return A query string.
         */
        stringify(obj: ParsedUrlQueryInput, separator?: NjsStringLike, equal?: NjsStringLike, options?: StringifyOptions): string;

        /**
         * An alias for `querystring.stringify()`.
         */
        encode(obj: ParsedUrlQueryInput, separator?: NjsStringLike, equal?: NjsStringLike, options?: StringifyOptions): string;

        /**
         * Performs decoding of URL percent-encoded characters of the string `str`, returns an
         * unescaped query string. The method is used by `querystring.parse()` and should not be
         * used directly.
         *
         * @param str An escaped query string.
         * @return An unescaped string.
         */
        unescape(str: NjsStringLike): string;
    }

    const querystring: QueryString;

    // It's exported like this because njs doesn't support named imports.
    // TODO: Replace NjsFS with individual named exports as soon as njs supports named imports.
    export default querystring;
}
