interface NgxObject {
    readonly INFO: number;
    readonly WARN: number;
    readonly ERR: number;
    /**
     * Writes a string to the error log with the specified level
     * of logging.
     * @param level Log level (ngx.INFO, ngx.WARN, ngx.ERR).
     * @param message Message to log.
     */
    log(level: number, message: NjsStringOrBuffer): void;
}

declare const ngx: NgxObject;
