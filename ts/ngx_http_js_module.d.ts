/// <reference path="index.d.ts" />
/// <reference path="ngx_core.d.ts" />

interface NginxHTTPArgs {
    readonly [prop: string]: string;
}

interface NginxHeadersIn {
    // common request headers
    readonly 'Accept'?: string;
    readonly 'Accept-Charset'?: string;
    readonly 'Accept-Encoding'?: string;
    readonly 'Accept-Language'?: string;
    readonly 'Authorization'?: string;
    readonly 'Cache-Control'?: string;
    readonly 'Connection'?: string;
    readonly 'Content-Length'?: string;
    readonly 'Content-Type'?: string;
    readonly 'Cookie'?: string;
    readonly 'Date'?: string;
    readonly 'Expect'?: string;
    readonly 'Forwarded'?: string;
    readonly 'From'?: string;
    readonly 'Host'?: string;
    readonly 'If-Match'?: string;
    readonly 'If-Modified-Since'?: string;
    readonly 'If-None-Match'?: string;
    readonly 'If-Range'?: string;
    readonly 'If-Unmodified-Since'?: string;
    readonly 'Max-Forwards'?: string;
    readonly 'Origin'?: string;
    readonly 'Pragma'?: string;
    readonly 'Proxy-Authorization'?: string;
    readonly 'Range'?: string;
    readonly 'Referer'?: string;
    readonly 'TE'?: string;
    readonly 'User-Agent'?: string;
    readonly 'Upgrade'?: string;
    readonly 'Via'?: string;
    readonly 'Warning'?: string;
    readonly 'X-Forwarded-For'?: string;

    readonly [prop: string]: string | undefined;
}

interface NginxHeadersOut {
    // common response headers
    'Age'?: string;
    'Allow'?: string;
    'Alt-Svc'?: string;
    'Cache-Control'?: string;
    'Connection'?: string;
    'Content-Disposition'?: string;
    'Content-Encoding'?: string;
    'Content-Language'?: string;
    'Content-Length'?: string;
    'Content-Location'?: string;
    'Content-Range'?: string;
    'Content-Type'?: string;
    'Date'?: string;
    'ETag'?: string;
    'Expires'?: string;
    'Last-Modified'?: string;
    'Link'?: string;
    'Location'?: string;
    'Pragma'?: string;
    'Proxy-Authenticate'?: string;
    'Retry-After'?: string;
    'Server'?: string;
    'Trailer'?: string;
    'Transfer-Encoding'?: string;
    'Upgrade'?: string;
    'Vary'?: string;
    'Via'?: string;
    'Warning'?: string;
    'WWW-Authenticate'?: string;

    'Set-Cookie'?: string[];

    [prop: string]: string | string[] | undefined;
}

interface NginxVariables {
    readonly 'ancient_browser'?: string;
    readonly 'arg_'?: string;
    readonly 'args'?: string;
    readonly 'binary_remote_addr'?: string;
    readonly 'body_bytes_sent'?: string;
    readonly 'bytes_received'?: string;
    readonly 'bytes_sent'?: string;
    readonly 'connection'?: string;
    readonly 'connection_requests'?: string;
    readonly 'connections_active'?: string;
    readonly 'connections_reading'?: string;
    readonly 'connections_waiting'?: string;
    readonly 'connections_writing'?: string;
    readonly 'content_length'?: string;
    readonly 'content_type'?: string;
    readonly 'cookie_'?: string;
    readonly 'date_gmt'?: string;
    readonly 'date_local'?: string;
    readonly 'document_root'?: string;
    readonly 'document_uri'?: string;
    readonly 'fastcgi_path_info'?: string;
    readonly 'fastcgi_script_name'?: string;
    readonly 'geoip_area_code'?: string;
    readonly 'geoip_city'?: string;
    readonly 'geoip_city_continent_code'?: string;
    readonly 'geoip_city_country_code'?: string;
    readonly 'geoip_city_country_code3'?: string;
    readonly 'geoip_city_country_name'?: string;
    readonly 'geoip_country_code'?: string;
    readonly 'geoip_country_code3'?: string;
    readonly 'geoip_country_name'?: string;
    readonly 'geoip_dma_code'?: string;
    readonly 'geoip_latitude'?: string;
    readonly 'geoip_longitude'?: string;
    readonly 'geoip_org'?: string;
    readonly 'geoip_postal_code'?: string;
    readonly 'geoip_region'?: string;
    readonly 'geoip_region_name'?: string;
    readonly 'gzip_ratio'?: string;
    readonly 'host'?: string;
    readonly 'hostname'?: string;
    readonly 'http2'?: string;
    readonly 'http_'?: string;
    readonly 'https'?: string;
    readonly 'invalid_referer'?: string;
    readonly 'is_args'?: string;
    readonly 'jwt_claim_'?: string;
    readonly 'jwt_header_'?: string;
    readonly 'limit_conn_status'?: string;
    readonly 'limit_rate'?: string;
    readonly 'limit_req_status'?: string;
    readonly 'memcached_key'?: string;
    readonly 'modern_browser'?: string;
    readonly 'msec'?: string;
    readonly 'msie'?: string;
    readonly 'nginx_version'?: string;
    readonly 'pid'?: string;
    readonly 'pipe'?: string;
    readonly 'protocol'?: string;
    readonly 'proxy_add_x_forwarded_for'?: string;
    readonly 'proxy_host'?: string;
    readonly 'proxy_port'?: string;
    readonly 'proxy_protocol_addr'?: string;
    readonly 'proxy_protocol_port'?: string;
    readonly 'proxy_protocol_server_addr'?: string;
    readonly 'proxy_protocol_server_port'?: string;
    readonly 'query_string'?: string;
    readonly 'realip_remote_addr'?: string;
    readonly 'realip_remote_port'?: string;
    readonly 'realpath_root'?: string;
    readonly 'remote_addr'?: string;
    readonly 'remote_port'?: string;
    readonly 'remote_user'?: string;
    readonly 'request'?: string;
    readonly 'request_body'?: string;
    readonly 'request_body_file'?: string;
    readonly 'request_completion'?: string;
    readonly 'request_filename'?: string;
    readonly 'request_id'?: string;
    readonly 'request_length'?: string;
    readonly 'request_method'?: string;
    readonly 'request_time'?: string;
    readonly 'request_uri'?: string;
    readonly 'scheme'?: string;
    readonly 'secure_link'?: string;
    readonly 'secure_link_expires'?: string;
    readonly 'sent_http_'?: string;
    readonly 'sent_trailer_'?: string;
    readonly 'server_addr'?: string;
    readonly 'server_name'?: string;
    readonly 'server_port'?: string;
    readonly 'server_protocol'?: string;
    readonly 'session_log_binary_id'?: string;
    readonly 'session_log_id'?: string;
    readonly 'session_time'?: string;
    readonly 'slice_range'?: string;
    readonly 'spdy'?: string;
    readonly 'spdy_request_priority'?: string;
    readonly 'ssl_cipher'?: string;
    readonly 'ssl_ciphers'?: string;
    readonly 'ssl_client_cert'?: string;
    readonly 'ssl_client_escaped_cert'?: string;
    readonly 'ssl_client_fingerprint'?: string;
    readonly 'ssl_client_i_dn'?: string;
    readonly 'ssl_client_i_dn_legacy'?: string;
    readonly 'ssl_client_raw_cert'?: string;
    readonly 'ssl_client_s_dn'?: string;
    readonly 'ssl_client_s_dn_legacy'?: string;
    readonly 'ssl_client_serial'?: string;
    readonly 'ssl_client_v_end'?: string;
    readonly 'ssl_client_v_remain'?: string;
    readonly 'ssl_client_v_start'?: string;
    readonly 'ssl_client_verify'?: string;
    readonly 'ssl_curves'?: string;
    readonly 'ssl_early_data'?: string;
    readonly 'ssl_preread_alpn_protocols'?: string;
    readonly 'ssl_preread_protocol'?: string;
    readonly 'ssl_preread_server_name'?: string;
    readonly 'ssl_protocol'?: string;
    readonly 'ssl_server_name'?: string;
    readonly 'ssl_session_id'?: string;
    readonly 'ssl_session_reused'?: string;
    readonly 'status'?: string;
    readonly 'tcpinfo_rtt'?: string;
    readonly 'tcpinfo_rttvar'?: string;
    readonly 'tcpinfo_snd_cwnd'?: string;
    readonly 'tcpinfo_rcv_space'?: string;
    readonly 'time_iso8601'?: string;
    readonly 'time_local'?: string;
    readonly 'uid_got'?: string;
    readonly 'uid_reset'?: string;
    readonly 'uid_set'?: string;
    readonly 'upstream_addr'?: string;
    readonly 'upstream_bytes_received'?: string;
    readonly 'upstream_bytes_sent'?: string;
    readonly 'upstream_cache_status'?: string;
    readonly 'upstream_connect_time'?: string;
    readonly 'upstream_cookie_'?: string;
    readonly 'upstream_first_byte_time'?: string;
    readonly 'upstream_header_time'?: string;
    readonly 'upstream_http_'?: string;
    readonly 'upstream_queue_time'?: string;
    readonly 'upstream_response_length'?: string;
    readonly 'upstream_response_time'?: string;
    readonly 'upstream_session_time'?: string;
    readonly 'upstream_status'?: string;
    readonly 'upstream_trailer_'?: string;
    readonly 'uri'?: string;

    [prop: string]: string | undefined;
}

/**
 * @since 0.5.0
 */
type NginxRawVariables = {
    [K in keyof NginxVariables]: Buffer | undefined;
};

interface NginxSubrequestOptions {
    /**
     * Arguments string, by default an empty string is used.
     */
    args?: string,
    /**
     * Request body, by default the request body of the parent request object is used.
     */
    body?: string,
    /**
     * HTTP method, by default the GET method is used.
     */
    method?: "GET" | "POST" | "OPTIONS" | "HEAD" | "PROPFIND" | "PUT"
        | "MKCOL" | "DELETE" | "COPY" | "MOVE" | "PROPPATCH"
        | "LOCK" | "PATCH" | "TRACE",
    /**
     * if true, the created subrequest is a detached subrequest.
     * Responses to detached subrequests are ignored.
     */
    detached?: boolean
}

interface NginxHTTPSendBufferOptions {
    /**
     * True if data is a last buffer.
     */
    last?: boolean
    /**
     * True if the buffer should have the flush flag.
     */
    flush?: boolean
}

interface NginxHTTPRequest {
    /**
     * Request arguments object.
     *
     * Since 0.7.6, duplicate keys are returned as an array, keys are
     * case-sensitive, both keys and values are percent-decoded.
     * For example, the query string
     *
     * 'a=1&b=%32&A=3&b=4&B=two%20words'
     * is converted to r.args as:
     *
     *   {a: "1", b: ["2", "4"], A: "3", B: "two words"}
     */
    readonly args: NginxHTTPArgs;
    /**
     * After calling this function, next data chunks will be passed to
     * the client without calling js_body_filter.
     *
     * **Warning:**  May be called only from the js_body_filter function.
     *
     * @since 0.5.2
     */
    done(): void;
    /**
     * Writes a string to the error log on the error level of logging.
     * @param message Message to log.
     */
    error(message: NjsStringOrBuffer): void;
    /**
     * Finishes sending a response to the client.
     */
    finish(): void;
    /**
     * Incoming headers object.
     */
    readonly headersIn: NginxHeadersIn;
    /**
     * Outgoing headers object.
     */
    readonly headersOut: NginxHeadersOut;
    /**
     * HTTP protocol version.
     */
    readonly httpVersion: string;
    /**
     * Performs an internal redirect to the specified uri.
     * If the uri starts with the “@” prefix, it is considered a named location.
     * The actual redirect happens after the handler execution is completed.
     * Since 0.7.4, the method accepts escaped URIs.
     * @param uri Location to redirect to.
     */
    internalRedirect(uri: NjsStringOrBuffer): void;
    /**
     * Writes a string to the error log on the info level of logging.
     * @param message Message to log.
     */
    log(message: NjsStringOrBuffer): void;
    /**
     * HTTP method.
     */
    readonly method: string;
    /**
     * Parent for subrequest object.
     */
    readonly parent?: NginxHTTPRequest;
    /**
     * An array of key-value pairs exactly as they were received from the client.
     * @since 0.4.1
     */
    readonly rawHeadersIn: [NjsFixedSizeArray<2, string>];
    /**
     * An array of key-value pairs of response headers.
     * Header field names are not converted to lower case, duplicate field values are not merged.
     * @since 0.4.1
     */
    readonly rawHeadersOut: [NjsFixedSizeArray<2, string>];
    /**
     * Client address.
     */
    readonly remoteAddress: string;
    /**
     * Client request body if it has not been written to a temporary file.
     * To ensure that the client request body is in memory, its size should be
     * limited by client_max_body_size, and a sufficient buffer size should be set
     * using client_body_buffer_size. The property is available only in the js_content directive.
     *
     * @since 0.5.0
     */
    readonly requestBuffer?: Buffer;
    /**
     * The same as `requestBuffer`, but returns a string.
     *
     * **Warning:** It may convert bytes invalid in UTF-8 encoding into the replacement character.
     *
     * @see requestBuffer
     * @since 0.5.0
     */
    readonly requestText?: string;
    /**
     * The same as `requestBuffer`, but returns a string.
     *
     * **Warning:** It may convert bytes invalid in UTF-8 encoding into the replacement character.
     *
     * @see requestBuffer
     * @see requestText
     * @deprecated Use `requestText` or `requestBuffer` instead.
     */
    readonly requestBody?: string;
    /**
     * Subrequest response body. The size of response body is limited by
     * the subrequest_output_buffer_size directive.
     *
     * @since 0.5.0
     */
    readonly responseBuffer?: Buffer;
    /**
     * The same as `responseBuffer`, but returns a string.
     *
     * **Warning:** It may convert bytes invalid in UTF-8 encoding into the replacement character.
     *
     * @see responseBuffer
     */
    readonly responseText?: string;
    /**
     * The same as `responseBuffer`, but returns a string.
     *
     * **Warning:** It may convert bytes invalid in UTF-8 encoding into the replacement character.
     *
     * @see responseBuffer
     * @see responseText
     * @deprecated Use `responseText` or `responseBuffer` instead.
     */
    readonly responseBody?: string;
    /**
     * Sends the entire response with the specified status to the client.
     * It is possible to specify either a redirect URL (for codes 301, 302, 303, 307, and 308)
     * or the response body text (for other codes) as the second argument.
     * @param status Respose status code.
     * @param body Respose body.
     */
    return(status: number, body?: NjsStringOrBuffer): void;
    /**
     * Sends a part of the response body to the client.
     */
    send(part: NjsStringOrBuffer): void;
    /**
     * Adds data to the chain of data chunks to be forwarded to the next body filter.
     * The actual forwarding happens later, when the all the data chunks of the current
     * chain are processed.
     *
     * **Warning:**  May be called only from the js_body_filter function.
     *
     * @since 0.5.2
     * @param data Data to send.
     * @param options Object used to override nginx buffer flags derived from
     * an incoming data chunk buffer.
     */
    sendBuffer(data: NjsStringOrBuffer, options?: NginxHTTPSendBufferOptions): void;
    /**
     * Sends the HTTP headers to the client.
     */
    sendHeader(): void;
    /**
     * Respose status code.
     */
    status: number;
    /**
     * Creates a subrequest with the given uri and options.
     * A subrequest shares its input headers with the client request.
     * To send headers different from original headers to a proxied server,
     * the proxy_set_header directive can be used. To send a completely new
     * set of headers to a proxied server, the proxy_pass_request_headers directive can be used.
     * @param uri Subrequest location.
     * @param options Subrequest options.
     * @param callback Completion callback.
     */
    subrequest(uri: NjsStringOrBuffer, options: NginxSubrequestOptions & { detached: true }): void;
    subrequest(uri: NjsStringOrBuffer, options?: NginxSubrequestOptions | string): Promise<NginxHTTPRequest>;
    subrequest(uri: NjsStringOrBuffer, options: NginxSubrequestOptions & { detached?: false } | string,
               callback:(reply:NginxHTTPRequest) => void): void;
    subrequest(uri: NjsStringOrBuffer, callback:(reply:NginxHTTPRequest) => void): void;
    /**
     * Current URI in request, normalized.
     */
    readonly uri: string;
    /**
     * nginx variables as Buffers.
     *
     * @since 0.5.0
     * @see variables
     */
    readonly rawVariables: NginxRawVariables;
    /**
     * nginx variables as strings.
     *
     * After 0.8.5 bytes invalid in UTF-8 encoding are converted into the replacement characters.
     *
     * @see rawVariables
     */
    readonly variables: NginxVariables;
    /**
     * Writes a string to the error log on the warn level of logging.
     * @param message Message to log.
     */
    warn(message: NjsStringOrBuffer): void;
}


/**
 * NginxPeriodicSession object is available as the first argument in the js_periodic handler.
 * @since 0.8.1
 */
interface NginxPeriodicSession {
    /**
     * nginx variables as Buffers.
     *
     * @see variables
     */
    readonly rawVariables: NginxRawVariables;
    /**
     * nginx variables as strings.
     *
     * **Warning:** Bytes invalid in UTF-8 encoding may be converted into the replacement character.
     *
     * @see rawVariables
     */
    readonly variables: NginxVariables;
}
