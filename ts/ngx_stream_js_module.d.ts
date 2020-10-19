/// <reference path="njs_core.d.ts" />

interface NginxStreamVariables {
    readonly 'binary_remote_addr'?: NjsByteString;
    readonly 'bytes_received'?: NjsByteString;
    readonly 'bytes_sent'?: NjsByteString;
    readonly 'connection'?: NjsByteString;
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
    readonly 'hostname'?: NjsByteString;
    readonly 'limit_conn_status'?: NjsByteString;
    readonly 'msec'?: NjsByteString;
    readonly 'nginx_version'?: NjsByteString;
    readonly 'pid'?: NjsByteString;
    readonly 'proxy_add_x_forwarded_for'?: NjsByteString;
    readonly 'proxy_host'?: NjsByteString;
    readonly 'proxy_port'?: NjsByteString;
    readonly 'proxy_protocol_addr'?: NjsByteString;
    readonly 'proxy_protocol_port'?: NjsByteString;
    readonly 'proxy_protocol_server_addr'?: NjsByteString;
    readonly 'proxy_protocol_server_port'?: NjsByteString;
    readonly 'realip_remote_addr'?: NjsByteString;
    readonly 'realip_remote_port'?: NjsByteString;
    readonly 'remote_addr'?: NjsByteString;
    readonly 'remote_port'?: NjsByteString;
    readonly 'server_addr'?: NjsByteString;
    readonly 'server_port'?: NjsByteString;
    readonly 'ssl_cipher'?: NjsByteString;
    readonly 'ssl_ciphers'?: NjsByteString;
    readonly 'ssl_client_cert'?: NjsByteString;
    readonly 'ssl_client_escaped_cert'?: NjsByteString;
    readonly 'ssl_client_fingerprint'?: NjsByteString;
    readonly 'ssl_client_i_dn'?: NjsByteString;
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
    readonly 'time_iso8601'?: NjsByteString;
    readonly 'time_local'?: NjsByteString;

    [prop: string]: NjsByteString;
}

interface NginxStreamCallbackFlags {
    /**
     * True if data is a last buffer.
     */
    last: boolean
}

interface NginxStreamSendOptions {
    /**
     * True if data is a last buffer.
     */
    last?: boolean
    /**
     * True if the buffer should have the flush flag.
     */
    flush?: boolean
}

interface NginxStreamRequest {
    /**
     * Successfully finalizes the phase handler.
     */
    allow(): void;
    /**
     * Finalizes the phase handler and passes control to the next handler.
     */
    decline(): void;
    /**
     * Finalizes the phase handler with the access error code.
     */
    deny(): void;
    /**
     * Successfully finalizes the current phase handler
     * or finalizes it with the specified numeric code.
     * @param code Finalization code.
     */
    done(code?: number): void;
    /**
     * Writes a string to the error log on the error level of logging.
     * @param message Message to log.
     */
    error(message: NjsStringLike): void;
    /**
     * Writes a string to the error log on the info level of logging.
     * @param message Message to log.
     */
    log(message: NjsStringLike): void;
    /**
     * Unregisters the callback set by on() method.
     */
    off(event: "upload" | "download"): void;
    /**
     * Registers a callback for the specified event.
     */
    on(event: "upload" | "download",
       callback:(data:NjsByteString,  flags: NginxStreamCallbackFlags) => void): void;
    /**
     * Client address.
     */
    readonly remoteAddress: NjsByteString;
    /**
     * Sends the data to the client.
     * @param data Data to send.
     * @param options Object used to override nginx buffer flags derived from
     * an incoming data chunk buffer.
     */
    send(data: NjsStringLike, options?: NginxStreamSendOptions): void;
    /**
     * nginx variables object.
     */
    readonly variables: NginxStreamVariables;
    /**
     * Writes a string to the error log on the warn level of logging.
     * @param message Message to log.
     */
    warn(message: NjsStringLike): void;
}
