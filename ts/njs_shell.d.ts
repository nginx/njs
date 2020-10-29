/// <reference path="index.d.ts" />

interface Console {
    log(...args: any[]): void;
    dump(...args: any[]): void;

    time(label?: NjsStringLike): void;
    timeEnd(label?: NjsStringLike): void;
}

declare const console: Console;
