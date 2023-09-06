/// <reference path="index.d.ts" />
/// <reference path="ngx_core.d.ts" />

interface NginxStreamVariables {
    readonly 'binary_remote_addr'?: string;
    readonly 'bytes_received'?: string;
    readonly 'bytes_sent'?: string;
    readonly 'connection'?: string;
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
    readonly 'hostname'?: string;
    readonly 'limit_conn_status'?: string;
    readonly 'msec'?: string;
    readonly 'nginx_version'?: string;
    readonly 'pid'?: string;
    readonly 'proxy_add_x_forwarded_for'?: string;
    readonly 'proxy_host'?: string;
    readonly 'proxy_port'?: string;
    readonly 'proxy_protocol_addr'?: string;
    readonly 'proxy_protocol_port'?: string;
    readonly 'proxy_protocol_server_addr'?: string;
    readonly 'proxy_protocol_server_port'?: string;
    readonly 'realip_remote_addr'?: string;
    readonly 'realip_remote_port'?: string;
    readonly 'remote_addr'?: string;
    readonly 'remote_port'?: string;
    readonly 'server_addr'?: string;
    readonly 'server_port'?: string;
    readonly 'ssl_cipher'?: string;
    readonly 'ssl_ciphers'?: string;
    readonly 'ssl_client_cert'?: string;
    readonly 'ssl_client_escaped_cert'?: string;
    readonly 'ssl_client_fingerprint'?: string;
    readonly 'ssl_client_i_dn'?: string;
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
    readonly 'time_iso8601'?: string;
    readonly 'time_local'?: string;

    [prop: string]: string | undefined;
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
       callback: (data: string, flags: NginxStreamCallbackFlags) => void): void;
    on(event: "upstream" | "downstream",
       callback: (data: Buffer, flags: NginxStreamCallbackFlags) => void): void;
    /**
     * Client address.
     */
    readonly remoteAddress: string;
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
