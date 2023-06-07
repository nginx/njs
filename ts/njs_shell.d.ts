/// <reference path="index.d.ts" />

interface Console {
    log(...args: any[]): void;
    dump(...args: any[]): void;

    time(label?: string): void;
    timeEnd(label?: string): void;
}

declare const console: Console;
