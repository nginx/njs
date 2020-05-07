/// <reference path="njs_core.d.ts" />

interface Console {
    log(...args: any[]): void;
    dump(...args: any[]): void;

    time(label?: NjsStringLike): void;
    timeEnd(label?: NjsStringLike): void;
}
