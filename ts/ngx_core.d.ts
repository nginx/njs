type NgxHeaders = Headers | Object | [NjsFixedSizeArray<2, string>];

declare class Headers {
    /**
     * Appends a new value into an existing header in the Headers object,
     * or adds the header if it does not already exist.
     * @param name A name of the header.
     * @param value A value of the header.
     * @since 0.7.10
     */
    append(name:string, value: string): void;
    /**
     * Headers constructors.
     *
     * @param init is an optional initialization object.
     * @returns returns Headers object.
     * @since 0.7.10
     */
    constructor(init?: Object | [NjsFixedSizeArray<2, string>]);
    /**
     * Deletes a header from the Headers object.
     * @param name A name of the header to be deleted.
     * @since 0.7.10
     */
    delete(name:string): void;
    /**
     * Returns a string containing the values of all headers
     * with the specified name separated by a comma and a space.
     * @param name A name of the header.
     */
    get(name:string): string;
    /**
     * Returns an array containing the values of all headers
     * with the specified name.
     * @param name A name of the header.
     */
    getAll(name:string): Array<string>;
    /**
     * Executes a provided function once for each key/value
     * pair in the Headers object.
     * @param fn the function to be envoked.
     * @since 0.7.10
     */
    forEach(fn:(name: string, value: string) => void): void;
    /**
     * Returns a boolean value indicating whether a header with
     * the specified name exists.
     * @param name A name of the header.
     */
    has(name:string): boolean;
    /**
     * Sets a new value for an existing header inside the Headers object,
     * or adds the header if it does not already exist.
     * @param name A name of the header.
     * @param value A value of the header.
     * @since 0.7.10
     */
    set(name:string, value: string): void;
}

interface NgxRequestOptions {
    /**
     * Request body, by default is empty.
     */
    body?: string;
    /**
     * Cache mode, by default is "default".
     */
    cache?: "default" | "no-store" | "reload" | "no-cache" | "force-cache" | "only-if-cached";
    /**
     * Credentials, by default is "same-origin".
     */
    credentials?: "omit" | "same-origin" | "include";
    /**
     * Request headers.
     */
    headers?: NgxHeaders;
    /**
     * Request method, by default the GET method is used.
     */
    method?: string;
    /**
     * Mode, by default is "no-cors".
     */
    mode?: "same-origin" | "no-cors" | "cors";
}

declare class Request {
    /**
     * Returns a Promise that resolves with an body as ArrayBuffer.
     */
    arrayBuffer(): Promise<ArrayBuffer>;
    /**
     * A boolean value, true if the body has been used.
     */
    readonly bodyUsed: boolean;
    /**
     * Cache mode.
     */
    readonly cache: string;
    /**
     * Request constructors.
     *
     * @param init is an optional initialization object.
     * @returns returns Request object.
     * @since 0.7.10
     */
    constructor(input: string | Request, options?: NgxRequestOptions);
    /**
     * Credentials.
     */
    readonly credentials: string;
    /**
     * Returns a Promise that resolves with an result of applying of
     * JSON.parse() to a body.
     */
    json(): Promise<Object>;
    /**
     * The Headers object associated with the request.
     */
    headers: Headers;
    /**
     * Request mode.
     */
    readonly mode: string;
    /**
     * Returns a Promise that resolves with an body as String.
     */
    text(): Promise<string>;
    /**
     * Request url.
     */
    readonly url: string;
}

interface NgxResponseOptions {
    /**
     * Request headers.
     */
    headers?: NgxHeaders;
    /**
     * Response status, 200 by default.
     */
    status?: number;
    /**
     * Response status test, '' by default.
     */
    statusText?: string;
}

declare class Response {
    /**
     * Takes a Response stream and reads it to completion.
     * Returns a Promise that resolves with an ArrayBuffer.
     */
    arrayBuffer(): Promise<ArrayBuffer>;
    /**
     * A boolean value, true if the body has been used.
     */
    readonly bodyUsed: boolean;
    /**
     * Response constructors.
     *
     * @param init is an optional initialization object.
     * @returns returns Response object.
     * @since 0.7.10
     */
    constructor(body?: string, options?: NgxResponseOptions);
    /**
     * Takes a Response stream and reads it to completion.
     * Returns a Promise that resolves with the result of
     * parsing the body text as JSON.
     */
    json(): Promise<Object>;
    /**
     * The Headers object associated with the response.
     */
    headers: Headers;
    /**
     * A boolean value, true if the response was successful
     * (status in the range 200-299).
     */
    readonly ok: boolean;
    /**
     * A boolean value, true if the response is the result
     * of a redirect.
     */
    readonly redirected: boolean;
    /**
     * The status code of the response.
     */
    readonly status: number;
    /**
     * The status message corresponding to the status code.
     */
    readonly statusText: string;
    /**
     * Takes a Response stream and reads it to completion.
     * Returns a Promise that resolves with a string.
     */
    text(): Promise<string>;
    /**
     * The type of the response.
     */
    readonly type: string;
    /**
     * Response url.
     */
    readonly url: string;
}

interface NgxFetchOptions {
    /**
     * Request body, by default is empty.
     */
    body?: string,
    /**
     * The buffer size for reading the response, by default is 16384 (4096 before 0.7.4).
     * Nginx specific.
     * @deprecated Use `js_fetch_buffer_size` directive instead.
     */
    buffer_size?: Number,
    /**
     * Request headers object.
     */
    headers?: NgxHeaders;
    /**
     * The maximum size of the response body in bytes, by default is 1048576 (32768 before 0.7.4).
     * Nginx specific.
     * @deprecated Use `js_fetch_max_response_buffer_size` directive instead.
     */
    max_response_body_size?: Number,
    /**
     * Request method, by default the GET method is used.
     */
    method?: string;
    /**
     * Enables or disables verification of the HTTPS server certificate,
     * by default is true.
     * @since 0.7.0
     */
    verify?: boolean;
}

/**
 * This Error object is thrown when adding an item to a shared dictionary
 * that does not have enough free space.
 * @since 0.8.0
 */
declare class SharedMemoryError extends Error {}

type NgxSharedDictValue = string | number;
type NgxKeyValuePair<V> = [string, V];

/**
 * Interface of a dictionary shared among the working processes.
 * It can store either `string` or `number` values which is specified when
 * declaring the zone.
 *
 * @template {V} The type of stored values.
 * @since 0.8.0
 */
interface NgxSharedDict<V extends string | number = string | number> {
    /**
     * The capacity of this shared dictionary in bytes.
     */
    readonly capacity: number;
    /**
     * The name of this shared dictionary.
     */
    readonly name: string;

    /**
     * Sets the `value` for the specified `key` in the dictionary only if the
     * `key` does not exist yet.
     *
     * @param key The key of the item to add.
     * @param value The value of the item to add.
     * @param timeout Overrides the default timeout for this item in milliseconds.
     * @returns `true` if the value has been added successfully, `false`
     *   if the `key` already exists in this dictionary.
     * @throws {SharedMemoryError} if there's not enough free space in this
     *   dictionary.
     * @throws {TypeError} if the `value` is of a different type than expected
     *   by this dictionary.
     */
    add(key: string, value: V, timeout?: number): boolean;
    /**
     * Removes all items from this dictionary.
     */
    clear(): void;
    /**
     * Removes the item associated with the specified `key` from the dictionary.
     *
     * @param key The key of the item to remove.
     * @returns `true` if the item in the dictionary existed and has been
     *   removed, `false` otherwise.
     */
    delete(key: string): boolean;
    /**
     * Increments the value associated with the `key` by the given `delta`.
     * If the `key` doesn't exist, the item will be initialized to `init`.
     *
     * **Important:** This method can be used only if the dictionary was
     * declared with `type=number`!
     *
     * @param key is a string key.
     * @param delta The number to increment/decrement the value by.
     * @param init The number to initialize the item with if it didn't exist
     *   (default is `0`).
     * @param timeout Overrides the default timeout for this item in milliseconds.
     * @returns The new value.
     * @throws {SharedMemoryError} if there's not enough free space in this
     *   dictionary.
     * @throws {TypeError} if this dictionary does not expect numbers.
     */
    incr: V extends number
      ? (key: string, delta: V, init?: number, timeout?: number) => number
      : never;
    /**
     * @param maxCount The maximum number of pairs to retrieve (default is 1024).
     * @returns An array of the key-value pairs.
     */
    items(maxCount?: number): NgxKeyValuePair<V>[];
    /**
     * @returns The free page size in bytes.
     *   Note that even if the free page is zero the dictionary may still accept
     *   new values if there is enough space in the occupied pages.
     */
    freeSpace(): number;
    /**
     * @param key The key of the item to retrieve.
     * @returns The value associated with the `key`, or `undefined` if there
     *   is none.
     */
    get(key: string): V | undefined;
    /**
     * @param key The key to search for.
     * @returns `true` if an item with the specified `key` exists, `false`
     *   otherwise.
     */
    has(key: string): boolean;
    /**
     * @param maxCount The maximum number of keys to retrieve (default is 1024).
     * @returns An array of the dictionary keys.
     */
    keys(maxCount?: number): string[];
    /**
     * Removes the item associated with the specified `key` from the dictionary
     * and returns its value.
     *
     * @param key The key of the item to remove.
     * @returns The value associated with the `key`, or `undefined` if there
     *   is none.
     */
    pop(key: string): V | undefined;
     /**
     * Sets the `value` for the specified `key` in the dictionary only if the
     * `key` already exists.
     *
     * @param key The key of the item to replace.
     * @param value The new value of the item.
     * @returns `true` if the value has been replaced successfully, `false`
     *   if the key doesn't exist in this dictionary.
     * @throws {SharedMemoryError} if there's not enough free space in this
     *   dictionary.
     * @throws {TypeError} if the `value` is of a different type than expected
     *   by this dictionary.
     */
    replace(key: string, value: V): boolean;
    /**
     * Sets the `value` for the specified `key` in the dictionary.
     *
     * @param key The key of the item to set.
     * @param value The value of the item to set.
     * @param timeout Overrides the default timeout for this item in milliseconds.
     * @returns This dictionary (for method chaining).
     * @throws {SharedMemoryError} if there's not enough free space in this
     *   dictionary.
     * @throws {TypeError} if the `value` is of a different type than expected
     *   by this dictionary.
     */
    set(key: string, value: V, timeout?: number): this;
    /**
     * @returns The number of items in this shared dictionary.
     */
    size(): number;
}

interface NgxGlobalShared {
    /**
     * Shared dictionaries.
     * @since 0.8.0
     */
    readonly [prop: string]: NgxSharedDict;
}

interface NgxObject {
    /**
     * A string containing an optional nginx build name, corresponds to the
     * --build=name argument of the configure script, by default is ""
     *  @since 0.8.0
     */
    readonly build: string;
    /**
     * A string containing the file path to current nginx configuration file
     * @since 0.8.0
     */
    readonly conf_file_path: string;
    /**
     * A string containing the file path to directory where nginx is currently
     * looking for configuration
     * @since 0.7.8
     */
    readonly conf_prefix: string;
    /**
     * The error level constant for ngx.log() function.
     * @since 0.5.1
     */
    readonly ERR: number;
    /**
     * A string containing the file path to the current error log file
     * @since 0.8.0
     */
    readonly error_log_path: string;
    /**
     * The info level constant for ngx.log() function.
     * @since 0.5.1
     */
    readonly INFO: number;
    /**
     * Makes a request to fetch an URL.
     * Returns a Promise that resolves with the Response object.
     * Since 0.7.0 HTTPS is supported, redirects are not handled.
     * @param init URL of a resource to fetch or a Request object.
     * @param options An object containing additional settings.
     * @since 0.5.1
     */
    fetch(init: NjsStringOrBuffer | Request, options?: NgxFetchOptions): Promise<Response>;
    /**
     * Writes a string to the error log with the specified level
     * of logging.
     * @param level Log level (ngx.INFO, ngx.WARN, ngx.ERR).
     * @param message Message to log.
     */
    log(level: number, message: NjsStringOrBuffer): void;
    /**
     * A string containing the file path to a directory that keeps server files
     * @since 0.8.0
     */
    readonly prefix: string;

    /**
     * An object containing shared data between all worker processes.
     * @since 0.8.0
     */
    readonly shared: NgxGlobalShared;
    /**
     * A string containing nginx version, for example: "1.25.0"
     * @since 0.8.0
     */
    readonly version: string;
    /**
     * A number containing nginx version, for example: 1025000
     * @since 0.8.0
     */
    readonly version_number: number;
    /**
     * The warn level constant for ngx.log() function.
     * @since 0.5.1
     */
    readonly WARN: number;
    /**
     * A number corresponding to the current worker process id.
     * Can have values from 0 to worker_processes - 1.
     * @since 0.8.0
     */
    readonly worker_id: number;

}

declare const ngx: NgxObject;
