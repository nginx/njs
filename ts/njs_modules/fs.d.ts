/// <reference path="../njs_core.d.ts" />

declare module "fs" {

    /**
     * File system flag that controls opening of a file.
     *
     * - `'a'`   - Open a file for appending. The file is created if it does not exist.
     * - `'ax'`  - The same as `'a'` but fails if the file already exists.
     * - `'a+'`  - Open a file for reading and appending. If the file does not exist, it will be created.
     * - `'ax+'` - The same as `'a+'` but fails if the file already exists.
     * - `'as'`  - Open a file for appending in synchronous mode. If the file does not exist, it will be created.
     * - `'as+'` - Open a file for reading and appending in synchronous mode. If the file does not exist, it will be created.
     * - `'r'`   - Open a file for reading. An exception occurs if the file does not exist.
     * - `'r+'`  - Open a file for reading and writing. An exception occurs if the file does not exist.
     * - `'rs+'` - Open a file for reading and writing in synchronous mode. Instructs the operating system to bypass the local file system cache.
     * - `'w'`   - Open a file for writing. If the file does not exist, it will be created. If the file exists, it will be replaced.
     * - `'wx'`  - The same as `'w'` but fails if the file already exists.
     * - `'w+'`  - Open a file for reading and writing. If the file does not exist, it will be created. If the file exists, it will be replaced.
     * - `'wx+'` - The same as `'w+'` but fails if the file already exists.
     */
    export type OpenMode = "a" | "ax" | "a+" | "ax+" | "as" | "as+" | "r" | "r+" | "rs+" | "w" | "wx" | "w+" | "wx+";

    export type FileEncoding = BufferEncoding;

    /**
     * Valid types for path values in "fs".
     */
    export type PathLike = string | Buffer;

    /**
     * A representation of a directory entry - a file or a subdirectory.
     *
     * When `readdirSync()` is called with the `withFileTypes` option, the resulting array contains
     * `fs.Dirent` objects.
     */
    export interface Dirent {
        /**
         * @returns `true` if the object describes a block device.
         */
        isBlockDevice(): boolean;
        /**
         * @returns `true` if the object describes a character device.
         */
        isCharacterDevice(): boolean;
        /**
         * @returns `true` if the object describes a file system directory.
         */
        isDirectory(): boolean;
        /**
         * @returns `true` if the object describes a first-in-first-out (FIFO) pipe.
         */
        isFIFO(): boolean;
        /**
         * @returns `true` if the object describes a regular file.
         */
        isFile(): boolean;
        /**
         * @returns `true` if the object describes a socket.
         */
        isSocket(): boolean;
        /**
         * @returns `true` if the object describes a symbolic link.
         */
        isSymbolicLink(): boolean;

        /**
         * The name of the file this object refers to.
         */
        name: string;
    }

    /**
     * Stats object provides information about a file.
     *
     * The objects is returned from fs.stat(), fs.lstat() and friends.
     */
    export interface Stats {
        /**
         * @returns `true` if the object describes a block device.
         */
        isBlockDevice(): boolean;
        /**
         * @returns `true` if the object describes a character device.
         */
        isCharacterDevice(): boolean;
        /**
         * @returns `true` if the object describes a file system directory.
         */
        isDirectory(): boolean;
        /**
         * @returns `true` if the object describes a first-in-first-out (FIFO) pipe.
         */
        isFIFO(): boolean;
        /**
         * @returns `true` if the object describes a regular file.
         */
        isFile(): boolean;
        /**
         * @returns `true` if the object describes a socket.
         */
        isSocket(): boolean;
        /**
         * @returns `true` if the object describes a symbolic link.
         */
        isSymbolicLink(): boolean;

        /**
         * The numeric identifier of the device containing the file.
         */
        dev: number;

        /**
         * The file system specific "Inode" number for the file.
         */
        ino: number;

        /**
         * A bit-field describing the file type and mode.
         */
        mode: number;

        /**
         * The number of hard-links that exist for the file.
         */
        nlink: number;

        /**
         * The numeric user identifier of the user that owns the file (POSIX).
         */
        uid: number;

        /**
         * The numeric group identifier of the group that owns the file (POSIX).
         */
        gid: number;

        /**
         * A numeric device identifier if the file represents a device.
         */
        rdev: number;

        /**
         * The size of the file in bytes.
         */
        size: number;

        /**
         * The file system block size for i/o operations.
         */
        blksize: number;

        /**
         * The number of blocks allocated for this file.
         */
        blocks: number;

        /**
         * The timestamp indicating the last time this file was accessed expressed
         * in milliseconds since the POSIX Epoch.
         */
        atimeMs: number;

        /**
         * The timestamp indicating the last time this file was modified expressed
         * in milliseconds since the POSIX Epoch.
         */
        mtimeMs: number;

        /**
         * The timestamp indicating the last time this file was changed expressed
         * in milliseconds since the POSIX Epoch.
         */
        ctimeMs: number;

        /**
         * The timestamp indicating the creation time of this file expressed
         * in milliseconds since the POSIX Epoch.
         */
        birthtimeMs: number;

        /**
         * The timestamp indicating the last time this file was accessed.
         */
        atime: Date;

        /**
         * The timestamp indicating the last time this file was modified.
         */
        mtime: Date;

        /**
         * The timestamp indicating the last time this file was changed.
         */
        ctime: Date;

        /**
         * The timestamp indicating the creation time of this file.
         */
        birthtime: Date;
    }

    type WriteFileOptions = {
        mode?: number;
        flag?: OpenMode;
    };

    type Constants = {
        /**
         * Indicates that the file is visible to the calling process, used by default if no mode
         * is specified.
         */
        F_OK: 0;
        /**
         * Indicates that the file can be read by the calling process.
         */
        R_OK: 4;
        /**
         * Indicates that the file can be written by the calling process.
         */
        W_OK: 2;
        /**
         * Indicates that the file can be executed by the calling process.
         */
        X_OK: 1;
    };

    interface Promises {
        /**
         * Asynchronously tests permissions for a file or directory specified in the `path`.
         * If the check fails, an error will be returned, otherwise, the method will return undefined.
         *
         * @example
         *   import fs from 'fs'
         *   fs.promises.access('/file/path', fs.constants.R_OK | fs.constants.W_OK)
         *     .then(() => console.log('has access'))
         *     .catch(() => console.log('no access'))
         *
         * @since 0.3.9
         * @param path A path to a file or directory.
         * @param mode An optional integer that specifies the accessibility checks to be performed.
         *   Defaults to `fs.constants.F_OK`.
         */
        access(path: PathLike, mode?: number): Promise<void>;

        /**
         * Asynchronously appends specified `data` to a file with provided `filename`.
         * If the file does not exist, it will be created.
         *
         * @since 0.4.4
         * @param path A path to a file.
         * @param data The data to write.
         * @param options An object optionally specifying the file mode and flag.
         *   If `mode` is not supplied, the default of `0o666` is used.
         *   If `flag` is not supplied, the default of `'a'` is used.
         */
        appendFile(path: PathLike, data: NjsStringOrBuffer, options?: WriteFileOptions): Promise<void>;

        /**
         * Asynchronously retrieves `fs.Stats` object for the symbolic link referred to by `path`.
         * See `lstat(2)` for more details.
         *
         * @since 0.7.1
         * @param path A path to a file.
         * @param options An object with the following optional keys:
         *   - `throwIfNoEntry` - Whether an exception will be thrown if no file system entry exists,
         *      rather than returning undefined, defaults to `true`.
         */
        lstat(path: PathLike, options?: { throwIfNoEntry?: boolean; }): Promise<Stats>;

        /**
         * Asynchronously creates a directory at the specified `path`.
         *
         * @since 0.4.2
         * @param path A path to a file.
         * @param options The file mode (or an object specifying the file mode). Defaults to `0o777`.
         */
        mkdir(path: PathLike, options?: { mode?: number } | number): Promise<void>;

        /**
         * Asynchronously reads the contents of a directory at the specified `path`.
         *
         * @since 0.4.2
         * @param path A path to a file.
         * @param options A string that specifies encoding or an object optionally specifying
         *   the following keys:
         *   - `encoding` - `'utf8'` (default) or `'buffer'` (since 0.4.4)
         *   - `withFileTypes` - if set to `true`, the files array will contain `fs.Dirent` objects; defaults to `false`.
         */
        readdir(path: PathLike, options?: { encoding?: "utf8"; withFileTypes?: false; } | "utf8"): Promise<string[]>;
        readdir(path: PathLike, options: { encoding: "buffer"; withFileTypes?: false; } | "buffer"): Promise<Buffer[]>;
        readdir(path: PathLike, options: { encoding?: "utf8" | "buffer"; withFileTypes: true; }): Promise<Dirent[]>;

        /**
         * Asynchronously returns the contents of the file with provided `filename`.
         * If an encoding is specified, a `string` is returned, otherwise, a `Buffer`.
         *
         * @param path A path to a file.
         * @param options A string that specifies encoding or an object with the following optional keys:
         *   - `encoding` - `'utf8'`, `'hex'`, `'base64'`, or `'base64url'` (the last three since 0.4.4).
         *   - `flag` - file system flag, defaults to `r`.
         */
        readFile(path: PathLike): Promise<Buffer>;
        readFile(path: PathLike, options?: { flag?: OpenMode; }): Promise<Buffer>;
        readFile(path: PathLike, options: { encoding?: FileEncoding; flag?: OpenMode; } | FileEncoding): Promise<string>;

        /**
         * Asynchronously computes the canonical pathname by resolving `.`, `..` and symbolic links using
         * `realpath(3)`.
         *
         * @since 0.3.9
         * @param path A path to a file.
         * @param options The encoding (or an object specifying the encoding), used as the encoding of the result.
         */
        realpath(path: PathLike, options?: { encoding?: "utf8" } | "utf8"): Promise<string>;
        realpath(path: PathLike, options: { encoding: "buffer" } | "buffer"): Promise<Buffer>;

        /**
         * Asynchronously changes the name or location of a file from `oldPath` to `newPath`.
         *
         * @since 0.3.4
         * @param oldPath A path to a file.
         * @param newPath A path to a file.
         */
        rename(oldPath: PathLike, newPath: PathLike): Promise<void>;

        /**
         * Asynchronously removes a directory at the specified `path`.
         *
         * @since 0.4.2
         * @param path A path to a file.
         */
        rmdir(path: PathLike): Promise<void>;

        /**
         * Asynchronously retrieves `fs.Stats` object for the specified `path`.
         *
         * @since 0.7.1
         * @param path A path to a file.
         * @param options An object with the following optional keys:
         *   - `throwIfNoEntry` - Whether an exception will be thrown if no file system entry exists,
         *      rather than returning undefined, defaults to `true`.
         */
        stat(path: PathLike, options?: { throwIfNoEntry?: boolean; }): Promise<Stats>;

        /**
         * Asynchronously creates the link called `path` pointing to `target` using `symlink(2)`.
         * Relative targets are relative to the link’s parent directory.
         *
         * @since 0.3.9
         * @param target A path to an existing file.
         * @param path A path to the new symlink.
         */
        symlink(target: PathLike, path: PathLike): Promise<void>;

        /**
         * Asynchronously unlinks a file by `path`.
         *
         * @since 0.3.9
         * @param path A path to a file.
         */
        unlink(path: PathLike): Promise<void>;

        /**
         * Asynchronously writes `data` to a file with provided `filename`. If the file does not
         * exist, it will be created, if the file exists, it will be replaced.
         *
         * @since 0.4.4
         * @param path A path to a file.
         * @param data The data to write.
         * @param options An object optionally specifying the file mode and flag.
         *   If `mode` is not supplied, the default of `0o666` is used.
         *   If `flag` is not supplied, the default of `'w'` is used.
         */
        writeFile(path: PathLike, data: NjsStringOrBuffer, options?: WriteFileOptions): Promise<void>;
    }

    interface NjsFS {
        /**
         * Promissified versions of file system methods.
         *
         * @since 0.3.9
         */
        promises: Promises
        /**
         * File Access Constants
         */
        constants: Constants

        /**
         * Synchronously tests permissions for a file or directory specified in the `path`.
         * If the check fails, an error will be returned, otherwise, the method will return undefined.
         *
         * @example
         *   try {
         *     fs.accessSync('/file/path', fs.constants.R_OK | fs.constants.W_OK);
         *     console.log('has access');
         *   } catch (e) {
         *     console.log('no access');
         *   }
         *
         * @since 0.3.9
         * @param path A path to a file or directory.
         * @param mode An optional integer that specifies the accessibility checks to be performed.
         *   Defaults to `fs.constants.F_OK`.
         */
        accessSync(path: PathLike, mode?: number): void;

        /**
         * Synchronously appends specified `data` to a file with provided `filename`.
         * If the file does not exist, it will be created.
         *
         * @since 0.4.4
         * @param path A path to a file.
         * @param data The data to write.
         * @param options An object optionally specifying the file mode and flag.
         *   If `mode` is not supplied, the default of `0o666` is used.
         *   If `flag` is not supplied, the default of `'a'` is used.
         */
        appendFileSync(path: PathLike, data: NjsStringOrBuffer, options?: WriteFileOptions): void;

        /**
         * Synchronously retrieves `fs.Stats` object for the symbolic link referred to by path.
         * See `lstat(2)` for more details.
         *
         * @since 0.7.1
         * @param path A path to a file.
         * @param options An object with the following optional keys:
         *   - `throwIfNoEntry` - Whether an exception will be thrown if no file system entry exists,
         *      rather than returning undefined, defaults to `true`.
         */
        lstatSync(path: PathLike, options?: { throwIfNoEntry?: boolean; }): Stats;

        /**
         * Synchronously creates a directory at the specified `path`.
         *
         * @since 0.4.2
         * @param path A path to a file.
         * @param options The file mode (or an object specifying the file mode). Defaults to `0o777`.
         */
        mkdirSync(path: PathLike, options?: { mode?: number } | number): void;

        /**
         * Synchronously reads the contents of a directory at the specified `path`.
         *
         * @since 0.4.2
         * @param path A path to a file.
         * @param options A string that specifies encoding or an object optionally specifying
         *   the following keys:
         *   - `encoding` - `'utf8'` (default) or `'buffer'` (since 0.4.4)
         *   - `withFileTypes` - if set to `true`, the files array will contain `fs.Dirent` objects;
         *     defaults to `false`.
         */
        readdirSync(path: PathLike, options?: { encoding?: "utf8"; withFileTypes?: false; } | "utf8"): string[];
        readdirSync(path: PathLike, options: { encoding: "buffer"; withFileTypes?: false; } | "buffer"): Buffer[];
        readdirSync(path: PathLike, options: { encoding?: "utf8" | "buffer"; withFileTypes: true; }): Dirent[];

        /**
         * Synchronously returns the contents of the file with provided `filename`.
         * If an encoding is specified, a `string` is returned, otherwise, a `Buffer`.
         *
         * @example
         *   import fs from 'fs'
         *   var file = fs.readFileSync('/file/path.tar.gz')
         *   var gzipped = file.slice(0,2).toString('hex') === '1f8b'; gzipped  // => true
         *
         * @param path A path to a file.
         * @param options A string that specifies encoding or an object with the following optional keys:
         *   - `encoding` - `'utf8'`, `'hex'`, `'base64'`, or `'base64url'` (the last three since 0.4.4).
         *   - `flag` - file system flag, defaults to `r`.
         */
        readFileSync(path: PathLike): Buffer;
        readFileSync(path: PathLike, options?: { flag?: OpenMode; }): Buffer;
        readFileSync(path: PathLike, options: { encoding?: FileEncoding; flag?: OpenMode; } | FileEncoding): string;

        /**
         * Synchronously computes the canonical pathname by resolving `.`, `..` and symbolic links using
         * `realpath(3)`.
         *
         * @since 0.3.9
         * @param path A path to a file.
         * @param options The encoding (or an object specifying the encoding), used as the encoding of the result.
         */
        realpathSync(path: PathLike, options?: { encoding?: "utf8" } | "utf8"): string;
        realpathSync(path: PathLike, options: { encoding: "buffer" } | "buffer"): Buffer;

        /**
         * Synchronously changes the name or location of a file from `oldPath` to `newPath`.
         *
         * @example
         *   import fs from 'fs'
         *   var file = fs.renameSync('hello.txt', 'HelloWorld.txt')
         *
         * @since 0.3.4
         * @param oldPath A path to a file.
         * @param newPath A path to a file.
         */
        renameSync(oldPath: PathLike, newPath: PathLike): void;

        /**
         * Synchronously removes a directory at the specified `path`.
         *
         * @since 0.4.2
         * @param path A path to a file.
         */
        rmdirSync(path: PathLike): void;

        /**
         * Synchronously retrieves `fs.Stats` object for the specified path.
         *
         * @since 0.7.1
         * @param path A path to a file.
         * @param options An object with the following optional keys:
         *   - `throwIfNoEntry` - Whether an exception will be thrown if no file system entry exists,
         *      rather than returning undefined, defaults to `true`.
         */
        statSync(path: PathLike, options?: { throwIfNoEntry?: boolean; }): Stats;

        /**
         * Synchronously creates the link called `path` pointing to `target` using `symlink(2)`.
         * Relative targets are relative to the link’s parent directory.
         *
         * @since 0.3.9
         * @param target A path to an existing file.
         * @param path A path to the new symlink.
         */
        symlinkSync(target: PathLike, path: PathLike): void;

        /**
         * Synchronously unlinks a file by `path`.
         *
         * @since 0.3.9
         * @param path A path to a file.
         */
        unlinkSync(path: PathLike): void;

        /**
         * Synchronously writes `data` to a file with provided `filename`. If the file does not exist,
         * it will be created, if the file exists, it will be replaced.
         *
         * @example
         *   import fs from 'fs'
         *   fs.writeFileSync('hello.txt', 'Hello world')
         *
         * @since 0.4.4
         * @param path A path to a file.
         * @param data The data to write.
         * @param options An object optionally specifying the file mode and flag.
         *   If `mode` is not supplied, the default of `0o666` is used.
         *   If `flag` is not supplied, the default of `'w'` is used.
         */
        writeFileSync(path: PathLike, data: NjsStringOrBuffer, options?: WriteFileOptions): void;
    }

    const fs: NjsFS;

    // It's exported like this because njs doesn't support named imports.
    // TODO: Replace NjsFS with individual named exports as soon as njs supports named imports.
    export default fs;
}
