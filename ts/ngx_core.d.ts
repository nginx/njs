type NgxHeaders = Headers | Object | [NjsFixedSizeArray<2, NjsStringLike>];

declare class Headers {
    /**
     * Appends a new value into an existing header in the Headers object,
     * or adds the header if it does not already exist.
     * @param name A name of the header.
     * @param value A value of the header.
     * @since 0.7.10
     */
    append(name:NjsStringLike, value: NjsStringLike): void;
    /**
     * Headers constructors.
     *
     * @param init is an optional initialization object.
     * @returns returns Headers object.
     * @since 0.7.10
     */
    constructor(init?: Object | [NjsFixedSizeArray<2, NjsStringLike>]);
    /**
     * Deletes a header from the Headers object.
     * @param name A name of the header to be deleted.
     * @since 0.7.10
     */
    delete(name:NjsStringLike): void;
    /**
     * Returns a string containing the values of all headers
     * with the specified name separated by a comma and a space.
     * @param name A name of the header.
     */
    get(name:NjsStringLike): NjsByteString;
    /**
     * Returns an array containing the values of all headers
     * with the specified name.
     * @param name A name of the header.
     */
    getAll(name:NjsStringLike): Array<NjsByteString>;
    /**
     * Executes a provided function once for each key/value
     * pair in the Headers object.
     * @param fn the function to be envoked.
     * @since 0.7.10
     */
    forEach(fn:(name: NjsStringLike, value: NjsStringLike) => void): void;
    /**
     * Returns a boolean value indicating whether a header with
     * the specified name exists.
     * @param name A name of the header.
     */
    has(name:NjsStringLike): boolean;
    /**
     * Sets a new value for an existing header inside the Headers object,
     * or adds the header if it does not already exist.
     * @param name A name of the header.
     * @param value A value of the header.
     * @since 0.7.10
     */
    set(name:NjsStringLike, value: NjsStringLike): void;
}

interface NgxRequestOptions {
    /**
     * Request body, by default is empty.
     */
    body?: NjsStringLike;
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
    method?: NjsStringLike;
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
    readonly cache: NjsByteString;
    /**
     * Request constructors.
     *
     * @param init is an optional initialization object.
     * @returns returns Request object.
     * @since 0.7.10
     */
    constructor(input: NjsStringLike | Request, options?: NgxRequestOptions);
    /**
     * Credentials.
     */
    readonly credentials: NjsByteString;
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
    readonly mode: NjsByteString;
    /**
     * Returns a Promise that resolves with an body as String.
     */
    text(): Promise<NjsByteString>;
    /**
     * Request url.
     */
    readonly url: NjsByteString;
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
    statusText?: NjsStringLike;
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
    constructor(body?: NjsStringLike, options?: NgxResponseOptions);
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
    readonly statusText: NjsByteString;
    /**
     * Takes a Response stream and reads it to completion.
     * Returns a Promise that resolves with a string.
     */
    text(): Promise<NjsByteString>;
    /**
     * The type of the response.
     */
    readonly type: NjsByteString;
    /**
     * Response url.
     */
    readonly url: NjsByteString;
}

interface NgxFetchOptions {
    /**
     * Request body, by default is empty.
     */
    body?: NjsStringLike,
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
    method?: NjsStringLike;
    /**
     * Enables or disables verification of the HTTPS server certificate,
     * by default is true.
     * @since 0.7.0
     */
    verify?: boolean;
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

}

declare const ngx: NgxObject;
