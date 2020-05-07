/// <reference path="njs_core.d.ts" />

interface NginxHTTPArgs {
    readonly [prop: string]: NjsByteString;
}

interface NginxHeadersIn {
    // common request headers
    readonly 'accept'?: NjsByteString;
    readonly 'accept-charset'?: NjsByteString;
    readonly 'accept-encoding'?: NjsByteString;
    readonly 'accept-language'?: NjsByteString;
    readonly 'authorization'?: NjsByteString;
    readonly 'cache-control'?: NjsByteString;
    readonly 'connection'?: NjsByteString;
    readonly 'content-length'?: NjsByteString;
    readonly 'content-type'?: NjsByteString;
    readonly 'cookie'?: NjsByteString;
    readonly 'date'?: NjsByteString;
    readonly 'expect'?: NjsByteString;
    readonly 'forwarded'?: NjsByteString;
    readonly 'from'?: NjsByteString;
    readonly 'host'?: NjsByteString;
    readonly 'if-match'?: NjsByteString;
    readonly 'if-modified-since'?: NjsByteString;
    readonly 'if-none-match'?: NjsByteString;
    readonly 'if-range'?: NjsByteString;
    readonly 'if-unmodified-since'?: NjsByteString;
    readonly 'max-forwards'?: NjsByteString;
    readonly 'origin'?: NjsByteString;
    readonly 'pragma'?: NjsByteString;
    readonly 'proxy-authorization'?: NjsByteString;
    readonly 'range'?: NjsByteString;
    readonly 'referer'?: NjsByteString;
    readonly 'te'?: NjsByteString;
    readonly 'user-agent'?: NjsByteString;
    readonly 'upgrade'?: NjsByteString;
    readonly 'via'?: NjsByteString;
    readonly 'warning'?: NjsByteString;
    readonly 'x-forwarded-for'?: NjsByteString;

    readonly [prop: string]: NjsByteString;
}

interface NginxHeadersOut {
    // common response headers
    'age'?: NjsStringLike;
    'allow'?: NjsStringLike;
    'alt-svc'?: NjsStringLike;
    'cache-control'?: NjsStringLike;
    'connection'?: NjsStringLike;
    'content-disposition'?: NjsStringLike;
    'content-encoding'?: NjsStringLike;
    'content-language'?: NjsStringLike;
    'content-length'?: NjsStringLike;
    'content-location'?: NjsStringLike;
    'content-range'?: NjsStringLike;
    'content-type'?: NjsStringLike;
    'date'?: NjsStringLike;
    'etag'?: NjsStringLike;
    'expires'?: NjsStringLike;
    'last-modified'?: NjsStringLike;
    'link'?: NjsStringLike;
    'location'?: NjsStringLike;
    'pragma'?: NjsStringLike;
    'proxy-authenticate'?: NjsStringLike;
    'retry-after'?: NjsStringLike;
    'server'?: NjsStringLike;
    'trailer'?: NjsStringLike;
    'transfer-encoding'?: NjsStringLike;
    'upgrade'?: NjsStringLike;
    'vary'?: NjsStringLike;
    'via'?: NjsStringLike;
    'warning'?: NjsStringLike;
    'www-authenticate'?: NjsStringLike;

    'set-cookie'?: NjsStringLike[];

    [prop: string]: NjsStringLike | NjsStringLike[];
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

    [prop: string]: NjsStringLike;
}

interface NginxSubrequestOptions {
    method?: "GET" | "POST" | "OPTIONS" | "HEAD" | "PROPFIND" | "PUT"
        | "MKCOL" | "DELETE" | "COPY" | "MOVE" | "PROPPATCH"
        | "LOCK" | "PATCH" | "TRACE",
    args?: NjsStringLike,
    body?: NjsStringLike,
    detached?: boolean
}

interface NginxHTTPRequest {
    // properties
    readonly args: NginxHTTPArgs;
    readonly headersIn: NginxHeadersIn;
    readonly headersOut: NginxHeadersOut;
    readonly httpVersion: NjsByteString;
    readonly method: NjsByteString;
    readonly parent?: NginxHTTPRequest;
    readonly remoteAddress: NjsByteString;
    readonly requestBody?: NjsByteString;
    readonly responseBody?: NjsByteString;
    readonly uri: NjsByteString;
    readonly variables: NginxVariables;

    // control
    status: number;
    sendHeader(): void;
    send(part: NjsStringLike): void;
    return(status: number, body?: NjsStringLike): void;
    internalRedirect(location: NjsStringLike): void;
    finish(): void;

    // Promise version
    subrequest(uri: NjsStringLike, options?: NginxSubrequestOptions | string): Promise<NginxHTTPRequest>;
    // Long callback version
    subrequest(uri: NjsStringLike, options: NginxSubrequestOptions | string,
               callback:(reply:NginxHTTPRequest) => void): void;
    // Short callback version
    subrequest(uri: NjsStringLike, callback:(reply:NginxHTTPRequest) => void): void;
    // Detached version
    subrequest(uri: NjsStringLike, options: NginxSubrequestOptions): void;

    // logging
    error(message: NjsStringLike): void;
    warn(message: NjsStringLike): void;
    log(message: NjsStringLike): void;
}
