/// <reference path="index.d.ts" />
/// <reference path="ngx_core.d.ts" />

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

    [prop: string]: NjsByteString | undefined;
}

/**
 * @since 0.5.0
 */
type NginxStreamRawVariables = {
    [K in keyof NginxStreamVariables]: Buffer | undefined;
};

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
     * Successfully finalizes the phase handler. An alias to s.done(0).
     *
     * @since 0.2.4
     * @see done()
     */
    allow(): void;
    /**
     * Passing control to the next handler of the current phase (if any).
     * An alias to s.done(-5).
     *
     * @since 0.2.4
     * @see done()
     */
    decline(): void;
    /**
     * Finalizes the phase handler with the access error code.
     * An alias to s.done(403).
     *
     * @since 0.2.4
     * @see done()
     */
    deny(): void;
    /**
     * Sets an exit code for the current phase handler to a code value.
     * The actual finalization happens when the js handler is completed and
     * all pending events, for example from ngx.fetch() or setTimeout(),
     * are processed.
     *
     * @param code Finalization code, by default is 0.
     * Possible code values:
     *   0 - successful finalization, passing control to the next phase
     *  -5 - undecided, passing control to the next handler of the current
     *  phase (if any)
     * 403 - access is forbidden
     * @since 0.2.4
     * @see allow()
     * @see decline()
     * @see deny()
     */
    done(code?: number): void;
    /**
     * Writes a string to the error log on the error level of logging.
     * @param message Message to log.
     */
    error(message: NjsStringOrBuffer): void;
    /**
     * Writes a string to the error log on the info level of logging.
     * @param message Message to log.
     */
    log(message: NjsStringOrBuffer): void;
    /**
     * Unregisters the callback set by on() method.
     * @param event Event type to unregister.
     * @see on()
     */
    off(event: "upload" | "download" | "upstream" | "downstream"): void;
    /**
     * Registers a callback for the specified event.
     * @param event Event type to register. The callback data value type
     * depends on the event type. For "upload" | "download" the data type is string.
     * For "upstream" | "downstream" the data type is Buffer.
     * String and buffer events cannot be mixed for a single session.
     *
     * **Warning:** For string data type bytes invalid in UTF-8 encoding may be
     * converted into the replacement character.
     * @see off()
     */
    on(event: "upload" | "download",
       callback: (data: NjsByteString, flags: NginxStreamCallbackFlags) => void): void;
    on(event: "upstream" | "downstream",
       callback: (data: Buffer, flags: NginxStreamCallbackFlags) => void): void;
    /**
     * Client address.
     */
    readonly remoteAddress: NjsByteString;
    /**
     * Adds data to the chain of data chunks that will be forwarded in
     * the forward direction: in download callback to a client; in upload
     * to an upstream server. The actual forwarding happens later, when the all
     * the data chunks of the current chain are processed.
     *
     * @since 0.2.4
     * @param data Data to send.
     * @param options Object used to override nginx buffer flags derived from
     * an incoming data chunk buffer.
     * @see on()
     */
    send(data: NjsStringOrBuffer, options?: NginxStreamSendOptions): void;
    /**
     * The stream session exit status. It is an alias to the $status variable.
     * @since 0.5.2
     */
    readonly status: number;
    /**
     * nginx variables as Buffers.
     *
     * @since 0.5.0
     * @see variables
     */
    readonly rawVariables: NginxStreamRawVariables;
    /**
     * nginx variables as strings.
     *
     * **Warning:** Bytes invalid in UTF-8 encoding may be converted into the replacement character.
     *
     * @see rawVariables
     */
    readonly variables: NginxStreamVariables;
    /**
     * Writes a string to the error log on the warn level of logging.
     * @param message Message to log.
     */
    warn(message: NjsStringOrBuffer): void;
}
