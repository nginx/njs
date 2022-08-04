/// <reference path="index.d.ts" />
/// <reference path="ngx_core.d.ts" />

interface NginxHTTPArgs {
    readonly [prop: string]: NjsByteString;
}

interface NginxHeadersIn {
    // common request headers
    readonly 'Accept'?: NjsByteString;
    readonly 'Accept-Charset'?: NjsByteString;
    readonly 'Accept-Encoding'?: NjsByteString;
    readonly 'Accept-Language'?: NjsByteString;
    readonly 'Authorization'?: NjsByteString;
    readonly 'Cache-Control'?: NjsByteString;
    readonly 'Connection'?: NjsByteString;
    readonly 'Content-Length'?: NjsByteString;
    readonly 'Content-Type'?: NjsByteString;
    readonly 'Cookie'?: NjsByteString;
    readonly 'Date'?: NjsByteString;
    readonly 'Expect'?: NjsByteString;
    readonly 'Forwarded'?: NjsByteString;
    readonly 'From'?: NjsByteString;
    readonly 'Host'?: NjsByteString;
    readonly 'If-Match'?: NjsByteString;
    readonly 'If-Modified-Since'?: NjsByteString;
    readonly 'If-None-Match'?: NjsByteString;
    readonly 'If-Range'?: NjsByteString;
    readonly 'If-Unmodified-Since'?: NjsByteString;
    readonly 'Max-Forwards'?: NjsByteString;
    readonly 'Origin'?: NjsByteString;
    readonly 'Pragma'?: NjsByteString;
    readonly 'Proxy-Authorization'?: NjsByteString;
    readonly 'Range'?: NjsByteString;
    readonly 'Referer'?: NjsByteString;
    readonly 'TE'?: NjsByteString;
    readonly 'User-Agent'?: NjsByteString;
    readonly 'Upgrade'?: NjsByteString;
    readonly 'Via'?: NjsByteString;
    readonly 'Warning'?: NjsByteString;
    readonly 'X-Forwarded-For'?: NjsByteString;

    readonly [prop: string]: NjsByteString | undefined;
}

interface NginxHeadersOut {
    // common response headers
    'Age'?: NjsStringLike;
    'Allow'?: NjsStringLike;
    'Alt-Svc'?: NjsStringLike;
    'Cache-Control'?: NjsStringLike;
    'Connection'?: NjsStringLike;
    'Content-Disposition'?: NjsStringLike;
    'Content-Encoding'?: NjsStringLike;
    'Content-Language'?: NjsStringLike;
    'Content-Length'?: NjsStringLike;
    'Content-Location'?: NjsStringLike;
    'Content-Range'?: NjsStringLike;
    'Content-Type'?: NjsStringLike;
    'Date'?: NjsStringLike;
    'ETag'?: NjsStringLike;
    'Expires'?: NjsStringLike;
    'Last-Modified'?: NjsStringLike;
    'Link'?: NjsStringLike;
    'Location'?: NjsStringLike;
    'Pragma'?: NjsStringLike;
    'Proxy-Authenticate'?: NjsStringLike;
    'Retry-After'?: NjsStringLike;
    'Server'?: NjsStringLike;
    'Trailer'?: NjsStringLike;
    'Transfer-Encoding'?: NjsStringLike;
    'Upgrade'?: NjsStringLike;
    'Vary'?: NjsStringLike;
    'Via'?: NjsStringLike;
    'Warning'?: NjsStringLike;
    'WWW-Authenticate'?: NjsStringLike;

    'Set-Cookie'?: NjsStringLike[];

    [prop: string]: NjsStringLike | NjsStringLike[] | undefined;
}

interface NginxVariables {
    readonly 'ancient_browser'?: NjsByteString;
    readonly 'arg_'?: NjsByteString;
    readonly 'args'?: NjsByteString;
    readonly 'binary_remote_addr'?: NjsByteString;
    readonly 'body_bytes_sent'?: NjsByteString;
    readonly 'bytes_received'?: NjsByteString;
    readonly 'bytes_sent'?: NjsByteString;
    readonly 'connection'?: NjsByteString;
    readonly 'connection_requests'?: NjsByteString;
    readonly 'connections_active'?: NjsByteString;
    readonly 'connections_reading'?: NjsByteString;
    readonly 'connections_waiting'?: NjsByteString;
    readonly 'connections_writing'?: NjsByteString;
    readonly 'content_length'?: NjsByteString;
    readonly 'content_type'?: NjsByteString;
    readonly 'cookie_'?: NjsByteString;
    readonly 'date_gmt'?: NjsByteString;
    readonly 'date_local'?: NjsByteString;
    readonly 'document_root'?: NjsByteString;
    readonly 'document_uri'?: NjsByteString;
    readonly 'fastcgi_path_info'?: NjsByteString;
    readonly 'fastcgi_script_name'?: NjsByteString;
    readonly 'geoip_area_code'?: NjsByteString;
    readonly 'geoip_city'?: NjsByteString;
    readonly 'geoip_city_continent_code'?: NjsByteString;
    readonly 'geoip_city_country_code'?: NjsByteString;
    readonly 'geoip_city_country_code3'?: NjsByteString;
    readonly 'geoip_city_country_name'?: NjsByteString;
    readonly 'geoip_country_code'?: NjsByteString;
    readonly 'geoip_country_code3'?: NjsByteString;
    readonly 'geoip_country_name'?: NjsByteString;
    readonly 'geoip_dma_code'?: NjsByteString;
    readonly 'geoip_latitude'?: NjsByteString;
    readonly 'geoip_longitude'?: NjsByteString;
    readonly 'geoip_org'?: NjsByteString;
    readonly 'geoip_postal_code'?: NjsByteString;
    readonly 'geoip_region'?: NjsByteString;
    readonly 'geoip_region_name'?: NjsByteString;
    readonly 'gzip_ratio'?: NjsByteString;
    readonly 'host'?: NjsByteString;
    readonly 'hostname'?: NjsByteString;
    readonly 'http2'?: NjsByteString;
    readonly 'http_'?: NjsByteString;
    readonly 'https'?: NjsByteString;
    readonly 'invalid_referer'?: NjsByteString;
    readonly 'is_args'?: NjsByteString;
    readonly 'jwt_claim_'?: NjsByteString;
    readonly 'jwt_header_'?: NjsByteString;
    readonly 'limit_conn_status'?: NjsByteString;
    readonly 'limit_rate'?: NjsByteString;
    readonly 'limit_req_status'?: NjsByteString;
    readonly 'memcached_key'?: NjsByteString;
    readonly 'modern_browser'?: NjsByteString;
    readonly 'msec'?: NjsByteString;
    readonly 'msie'?: NjsByteString;
    readonly 'nginx_version'?: NjsByteString;
    readonly 'pid'?: NjsByteString;
    readonly 'pipe'?: NjsByteString;
    readonly 'protocol'?: NjsByteString;
    readonly 'proxy_add_x_forwarded_for'?: NjsByteString;
    readonly 'proxy_host'?: NjsByteString;
    readonly 'proxy_port'?: NjsByteString;
    readonly 'proxy_protocol_addr'?: NjsByteString;
    readonly 'proxy_protocol_port'?: NjsByteString;
    readonly 'proxy_protocol_server_addr'?: NjsByteString;
    readonly 'proxy_protocol_server_port'?: NjsByteString;
    readonly 'query_string'?: NjsByteString;
    readonly 'realip_remote_addr'?: NjsByteString;
    readonly 'realip_remote_port'?: NjsByteString;
    readonly 'realpath_root'?: NjsByteString;
    readonly 'remote_addr'?: NjsByteString;
    readonly 'remote_port'?: NjsByteString;
    readonly 'remote_user'?: NjsByteString;
    readonly 'request'?: NjsByteString;
    readonly 'request_body'?: NjsByteString;
    readonly 'request_body_file'?: NjsByteString;
    readonly 'request_completion'?: NjsByteString;
    readonly 'request_filename'?: NjsByteString;
    readonly 'request_id'?: NjsByteString;
    readonly 'request_length'?: NjsByteString;
    readonly 'request_method'?: NjsByteString;
    readonly 'request_time'?: NjsByteString;
    readonly 'request_uri'?: NjsByteString;
    readonly 'scheme'?: NjsByteString;
    readonly 'secure_link'?: NjsByteString;
    readonly 'secure_link_expires'?: NjsByteString;
    readonly 'sent_http_'?: NjsByteString;
    readonly 'sent_trailer_'?: NjsByteString;
    readonly 'server_addr'?: NjsByteString;
    readonly 'server_name'?: NjsByteString;
    readonly 'server_port'?: NjsByteString;
    readonly 'server_protocol'?: NjsByteString;
    readonly 'session_log_binary_id'?: NjsByteString;
    readonly 'session_log_id'?: NjsByteString;
    readonly 'session_time'?: NjsByteString;
    readonly 'slice_range'?: NjsByteString;
    readonly 'spdy'?: NjsByteString;
    readonly 'spdy_request_priority'?: NjsByteString;
    readonly 'ssl_cipher'?: NjsByteString;
    readonly 'ssl_ciphers'?: NjsByteString;
    readonly 'ssl_client_cert'?: NjsByteString;
    readonly 'ssl_client_escaped_cert'?: NjsByteString;
    readonly 'ssl_client_fingerprint'?: NjsByteString;
    readonly 'ssl_client_i_dn'?: NjsByteString;
    readonly 'ssl_client_i_dn_legacy'?: NjsByteString;
    readonly 'ssl_client_raw_cert'?: NjsByteString;
    readonly 'ssl_client_s_dn'?: NjsByteString;
    readonly 'ssl_client_s_dn_legacy'?: NjsByteString;
    readonly 'ssl_client_serial'?: NjsByteString;
    readonly 'ssl_client_v_end'?: NjsByteString;
    readonly 'ssl_client_v_remain'?: NjsByteString;
    readonly 'ssl_client_v_start'?: NjsByteString;
    readonly 'ssl_client_verify'?: NjsByteString;
    readonly 'ssl_curves'?: NjsByteString;
    readonly 'ssl_early_data'?: NjsByteString;
    readonly 'ssl_preread_alpn_protocols'?: NjsByteString;
    readonly 'ssl_preread_protocol'?: NjsByteString;
    readonly 'ssl_preread_server_name'?: NjsByteString;
    readonly 'ssl_protocol'?: NjsByteString;
    readonly 'ssl_server_name'?: NjsByteString;
    readonly 'ssl_session_id'?: NjsByteString;
    readonly 'ssl_session_reused'?: NjsByteString;
    readonly 'status'?: NjsByteString;
    readonly 'tcpinfo_rtt'?: NjsByteString;
    readonly 'tcpinfo_rttvar'?: NjsByteString;
    readonly 'tcpinfo_snd_cwnd'?: NjsByteString;
    readonly 'tcpinfo_rcv_space'?: NjsByteString;
    readonly 'time_iso8601'?: NjsByteString;
    readonly 'time_local'?: NjsByteString;
    readonly 'uid_got'?: NjsByteString;
    readonly 'uid_reset'?: NjsByteString;
    readonly 'uid_set'?: NjsByteString;
    readonly 'upstream_addr'?: NjsByteString;
    readonly 'upstream_bytes_received'?: NjsByteString;
    readonly 'upstream_bytes_sent'?: NjsByteString;
    readonly 'upstream_cache_status'?: NjsByteString;
    readonly 'upstream_connect_time'?: NjsByteString;
    readonly 'upstream_cookie_'?: NjsByteString;
    readonly 'upstream_first_byte_time'?: NjsByteString;
    readonly 'upstream_header_time'?: NjsByteString;
    readonly 'upstream_http_'?: NjsByteString;
    readonly 'upstream_queue_time'?: NjsByteString;
    readonly 'upstream_response_length'?: NjsByteString;
    readonly 'upstream_response_time'?: NjsByteString;
    readonly 'upstream_session_time'?: NjsByteString;
    readonly 'upstream_status'?: NjsByteString;
    readonly 'upstream_trailer_'?: NjsByteString;
    readonly 'uri'?: NjsByteString;

    [prop: string]: NjsStringLike | undefined;
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
    args?: NjsStringLike,
    /**
     * Request body, by default the request body of the parent request object is used.
     */
    body?: NjsStringLike,
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
    readonly httpVersion: NjsByteString;
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
    readonly method: NjsByteString;
    /**
     * Parent for subrequest object.
     */
    readonly parent?: NginxHTTPRequest;
    /**
     * Client address.
     */
    readonly remoteAddress: NjsByteString;
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
    readonly requestText?: NjsByteString;
    /**
     * The same as `requestBuffer`, but returns a string.
     *
     * **Warning:** It may convert bytes invalid in UTF-8 encoding into the replacement character.
     *
     * @see requestBuffer
     * @see requestText
     * @deprecated Use `requestText` or `requestBuffer` instead.
     */
    readonly requestBody?: NjsByteString;
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
    readonly responseText?: NjsByteString;
    /**
     * The same as `responseBuffer`, but returns a string.
     *
     * **Warning:** It may convert bytes invalid in UTF-8 encoding into the replacement character.
     *
     * @see responseBuffer
     * @see responseText
     * @deprecated Use `responseText` or `responseBuffer` instead.
     */
    readonly responseBody?: NjsByteString;
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
    readonly uri: NjsByteString;
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
     * **Warning:** Bytes invalid in UTF-8 encoding may be converted into the replacement character.
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
