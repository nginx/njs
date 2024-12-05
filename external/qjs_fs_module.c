
/*
 * Copyright (C) Dmitry Volyntsev
 * Copyright (C) F5, Inc.
 */

#include <qjs.h>
#include <njs_utils.h>

#include <dirent.h>
#include <njs_unix.h>


#if (NJS_SOLARIS)

#define DT_DIR         0
#define DT_REG         1
#define DT_CHR         2
#define DT_LNK         3
#define DT_BLK         4
#define DT_FIFO        5
#define DT_SOCK        6
#define QJS_DT_INVALID -1

#define qjs_dentry_type(_dentry)                                             \
    (QJS_DT_INVALID)

#else

#define QJS_DT_INVALID -1

#define qjs_dentry_type(_dentry)                                             \
    ((_dentry)->d_type)

#endif


#define qjs_fs_magic(calltype, mode)                                         \
    (((mode) << 2) | calltype)

#define qjs_fs_magic2(field, type)                                           \
    (((type) << 4) | field)


typedef enum {
    QJS_FS_DIRECT,
    QJS_FS_PROMISE,
    QJS_FS_CALLBACK,
} qjs_fs_calltype_t;


typedef enum {
    QJS_FTW_PHYS = 1,
    QJS_FTW_MOUNT = 2,
    QJS_FTW_DEPTH = 8,
} qjs_ftw_flags_t;


typedef enum {
    QJS_FTW_F,
    QJS_FTW_D,
    QJS_FTW_DNR,
    QJS_FTW_NS,
    QJS_FTW_SL,
    QJS_FTW_DP,
    QJS_FTW_SLN,
} qjs_ftw_type_t;


typedef enum {
    QJS_FS_TRUNC,
    QJS_FS_APPEND,
} qjs_fs_writemode_t;


typedef enum {
    QJS_FS_STAT,
    QJS_FS_LSTAT,
    QJS_FS_FSTAT,
} njs_fs_statmode_t;


typedef struct {
    long tv_sec;
    long tv_nsec;
} qjs_timespec_t;


typedef struct {
    uint64_t        st_dev;
    uint64_t        st_mode;
    uint64_t        st_nlink;
    uint64_t        st_uid;
    uint64_t        st_gid;
    uint64_t        st_rdev;
    uint64_t        st_ino;
    uint64_t        st_size;
    uint64_t        st_blksize;
    uint64_t        st_blocks;
    qjs_timespec_t  st_atim;
    qjs_timespec_t  st_mtim;
    qjs_timespec_t  st_ctim;
    qjs_timespec_t  st_birthtim;
} qjs_stat_t;


typedef enum {
    QJS_FS_STAT_DEV,
    QJS_FS_STAT_INO,
    QJS_FS_STAT_MODE,
    QJS_FS_STAT_NLINK,
    QJS_FS_STAT_UID,
    QJS_FS_STAT_GID,
    QJS_FS_STAT_RDEV,
    QJS_FS_STAT_SIZE,
    QJS_FS_STAT_BLKSIZE,
    QJS_FS_STAT_BLOCKS,
    QJS_FS_STAT_ATIME,
    QJS_FS_STAT_BIRTHTIME,
    QJS_FS_STAT_CTIME,
    QJS_FS_STAT_MTIME,
} qjs_stat_prop_t;


typedef struct {
    njs_str_t       name;
    int             value;
} qjs_fs_entry_t;


typedef int (*qjs_file_tree_walk_cb_t)(const char *, const struct stat *,
     qjs_ftw_type_t);


static JSValue qjs_fs_access(JSContext *cx, JSValueConst this_val,
    int argc, JSValueConst *argv, int calltype);
static JSValue qjs_fs_exists_sync(JSContext *cx, JSValueConst this_val,
    int argc,JSValueConst *argv);
static JSValue qjs_fs_close(JSContext *cx, JSValueConst this_val, int argc,
    JSValueConst *argv, int calltype);
static JSValue qjs_fs_mkdir(JSContext *cx, JSValueConst this_val, int argc,
    JSValueConst *argv, int calltype);
static JSValue qjs_fs_open(JSContext *cx, JSValueConst this_val, int argc,
    JSValueConst *argv, int calltype);
static JSValue qjs_fs_read(JSContext *cx, JSValueConst this_val, int argc,
    JSValueConst *argv, int calltype);
static JSValue qjs_fs_read_file(JSContext *cx, JSValueConst this_val, int argc,
    JSValueConst *argv, int calltype);
static JSValue qjs_fs_readlink(JSContext *cx, JSValueConst this_val, int argc,
    JSValueConst *argv, int calltype);
static JSValue qjs_fs_readdir(JSContext *cx, JSValueConst this_val, int argc,
    JSValueConst *argv, int calltype);
static JSValue qjs_fs_realpath(JSContext *cx, JSValueConst this_val, int argc,
    JSValueConst *argv, int calltype);
static JSValue qjs_fs_rename(JSContext *cx, JSValueConst this_val,
    int argc, JSValueConst *argv, int calltype);
static JSValue qjs_fs_rmdir(JSContext *cx, JSValueConst this_val, int argc,
    JSValueConst *argv, int calltype);
static JSValue qjs_fs_stat(JSContext *cx, JSValueConst this_val, int argc,
    JSValueConst *argv, int magic);
static JSValue qjs_fs_symlink(JSContext *cx, JSValueConst this_val, int argc,
    JSValueConst *argv, int calltype);
static JSValue qjs_fs_write(JSContext *cx, JSValueConst this_val, int argc,
    JSValueConst *argv, int calltype);
static JSValue qjs_fs_write_file(JSContext *cx, JSValueConst this_val, int argc,
    JSValueConst *argv, int magic);
static JSValue qjs_fs_unlink(JSContext *cx, JSValueConst this_val,
    int argc, JSValueConst *argv, int calltype);

static JSValue qjs_fs_stats_to_string_tag(JSContext *cx, JSValueConst this_val);
static JSValue qjs_fs_stats_test(JSContext *cx, JSValueConst this_val, int argc,
    JSValueConst *argv, int testtype);
static int qjs_fs_stats_get_own_property(JSContext *cx,
    JSPropertyDescriptor *pdesc, JSValueConst obj, JSAtom prop);
static int qjs_fs_stats_get_own_property_names(JSContext *cx,
    JSPropertyEnum **ptab, uint32_t *plen, JSValueConst obj);
static void qjs_fs_stats_finalizer(JSRuntime *rt, JSValue val);

static JSValue qjs_fs_dirent_to_string_tag(JSContext *cx,
    JSValueConst this_val);
static JSValue qjs_fs_dirent_ctor(JSContext *cx, JSValueConst new_target,
    int argc, JSValueConst *argv);
static JSValue qjs_fs_dirent_test(JSContext *cx, JSValueConst this_val,
    int argc, JSValueConst *argv, int testtype);

static JSValue qjs_fs_filehandle_to_string_tag(JSContext *cx,
    JSValueConst this_val);
static JSValue qjs_fs_filehandle_fd(JSContext *cx, JSValueConst this_val);
static JSValue qjs_fs_filehandle_value_of(JSContext *cx, JSValueConst this_val,
    int argc, JSValueConst *argv);
static void qjs_fs_filehandle_finalizer(JSRuntime *rt, JSValue val);

static char *qjs_fs_path(JSContext *cx, char storage[NJS_MAX_PATH + 1],
    JSValue src, const char *prop_name);
static JSValue qjs_fs_result(JSContext *cx, JSValue result, int calltype,
    JSValue callback);
static JSValue qjs_fs_error(JSContext *cx, const char *syscall,
    const char *description, const char *path, int errn);
static JSValue qjs_fs_encode(JSContext *cx,
    const qjs_buffer_encoding_t *encoding, njs_str_t *str);
static int qjs_fs_flags(JSContext *cx, JSValue value, int default_flags);
static mode_t qjs_fs_mode(JSContext *cx, JSValue value, mode_t default_mode);
static JSModuleDef *qjs_fs_init(JSContext *cx, const char *name);


static qjs_fs_entry_t qjs_flags_table[] = {
    { njs_str("a"),   O_APPEND | O_CREAT | O_WRONLY },
    { njs_str("a+"),  O_APPEND | O_CREAT | O_RDWR },
    { njs_str("as"),  O_APPEND | O_CREAT | O_SYNC | O_WRONLY },
    { njs_str("as+"), O_APPEND | O_CREAT | O_RDWR | O_SYNC },
    { njs_str("ax"),  O_APPEND | O_CREAT | O_EXCL | O_WRONLY },
    { njs_str("ax+"), O_APPEND | O_CREAT | O_EXCL | O_RDWR },
    { njs_str("r"),   O_RDONLY },
    { njs_str("r+"),  O_RDWR },
    { njs_str("rs+"), O_RDWR   | O_SYNC },
    { njs_str("w"),   O_CREAT  | O_TRUNC | O_WRONLY },
    { njs_str("w+"),  O_CREAT  | O_TRUNC | O_RDWR },
    { njs_str("wx"),  O_CREAT  | O_TRUNC | O_EXCL | O_WRONLY },
    { njs_str("wx+"), O_CREAT  | O_TRUNC | O_EXCL | O_RDWR },
    { njs_null_str, 0 }
};


static const JSCFunctionListEntry qjs_fs_stats_proto[] = {
    JS_CGETSET_DEF("[Symbol.toStringTag]", qjs_fs_stats_to_string_tag, NULL),
    JS_CFUNC_MAGIC_DEF("isBlockDevice", 0, qjs_fs_stats_test, DT_BLK),
    JS_CFUNC_MAGIC_DEF("isCharacterDevice", 0, qjs_fs_stats_test, DT_CHR),
    JS_CFUNC_MAGIC_DEF("isDirectory", 0, qjs_fs_stats_test, DT_DIR),
    JS_CFUNC_MAGIC_DEF("isFIFO", 0, qjs_fs_stats_test, DT_FIFO),
    JS_CFUNC_MAGIC_DEF("isFile", 0, qjs_fs_stats_test, DT_REG),
    JS_CFUNC_MAGIC_DEF("isSocket", 0, qjs_fs_stats_test, DT_SOCK),
    JS_CFUNC_MAGIC_DEF("isSymbolicLink", 0, qjs_fs_stats_test, DT_LNK),
};


static const JSCFunctionListEntry qjs_fs_dirent_proto[] = {
    JS_CGETSET_DEF("[Symbol.toStringTag]", qjs_fs_dirent_to_string_tag, NULL),
    JS_CFUNC_MAGIC_DEF("isBlockDevice", 0, qjs_fs_dirent_test, DT_BLK),
    JS_CFUNC_MAGIC_DEF("isCharacterDevice", 0, qjs_fs_dirent_test, DT_CHR),
    JS_CFUNC_MAGIC_DEF("isDirectory", 0, qjs_fs_dirent_test, DT_DIR),
    JS_CFUNC_MAGIC_DEF("isFIFO", 0, qjs_fs_dirent_test, DT_FIFO),
    JS_CFUNC_MAGIC_DEF("isFile", 0, qjs_fs_dirent_test, DT_REG),
    JS_CFUNC_MAGIC_DEF("isSocket", 0, qjs_fs_dirent_test, DT_SOCK),
    JS_CFUNC_MAGIC_DEF("isSymbolicLink", 0, qjs_fs_dirent_test, DT_LNK),
    JS_CFUNC_SPECIAL_DEF("constructor", 1, constructor, qjs_fs_dirent_ctor),
};


static const JSCFunctionListEntry qjs_fs_filehandle_proto[] = {
    JS_CGETSET_DEF("[Symbol.toStringTag]", qjs_fs_filehandle_to_string_tag,
                   NULL),
    JS_CFUNC_MAGIC_DEF("close", 0, qjs_fs_close, QJS_FS_PROMISE),
    JS_CGETSET_DEF("fd", qjs_fs_filehandle_fd, NULL),
    JS_CFUNC_MAGIC_DEF("stat", 4, qjs_fs_stat,
                       qjs_fs_magic(QJS_FS_PROMISE, QJS_FS_FSTAT)),
    JS_CFUNC_MAGIC_DEF("read", 4, qjs_fs_read, QJS_FS_PROMISE),
    JS_CFUNC_DEF("valueOf", 0, qjs_fs_filehandle_value_of),
    JS_CFUNC_MAGIC_DEF("write", 4, qjs_fs_write, QJS_FS_PROMISE),
};


static const JSCFunctionListEntry qjs_fs_constants[] = {
    JS_PROP_INT32_DEF("F_OK", F_OK, JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("R_OK", R_OK, JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("W_OK", W_OK, JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("X_OK", X_OK, JS_PROP_ENUMERABLE),
};


static const JSCFunctionListEntry qjs_fs_promises[] = {
    JS_CFUNC_MAGIC_DEF("access", 2, qjs_fs_access, QJS_FS_PROMISE),
    JS_CFUNC_MAGIC_DEF("appendFile", 3, qjs_fs_write_file,
                       qjs_fs_magic(QJS_FS_PROMISE, QJS_FS_APPEND)),
    JS_CFUNC_MAGIC_DEF("fstat", 2, qjs_fs_stat,
                       qjs_fs_magic(QJS_FS_PROMISE, QJS_FS_FSTAT)),
    JS_CFUNC_MAGIC_DEF("lstat", 2, qjs_fs_stat,
                       qjs_fs_magic(QJS_FS_PROMISE, QJS_FS_LSTAT)),
    JS_CFUNC_MAGIC_DEF("mkdir", 2, qjs_fs_mkdir, QJS_FS_PROMISE),
    JS_CFUNC_MAGIC_DEF("open", 3, qjs_fs_open, QJS_FS_PROMISE),
    JS_CFUNC_MAGIC_DEF("readFile", 2, qjs_fs_read_file, QJS_FS_PROMISE),
    JS_CFUNC_MAGIC_DEF("realpath", 2, qjs_fs_realpath, QJS_FS_PROMISE),
    JS_CFUNC_MAGIC_DEF("readdir", 2, qjs_fs_readdir, QJS_FS_PROMISE),
    JS_CFUNC_MAGIC_DEF("readlink", 2, qjs_fs_readlink, QJS_FS_PROMISE),
    JS_CFUNC_MAGIC_DEF("rename", 2, qjs_fs_rename, QJS_FS_PROMISE),
    JS_CFUNC_MAGIC_DEF("rmdir", 2, qjs_fs_rmdir, QJS_FS_PROMISE),
    JS_CFUNC_MAGIC_DEF("stat", 2, qjs_fs_stat,
                       qjs_fs_magic(QJS_FS_PROMISE, QJS_FS_STAT)),
    JS_CFUNC_MAGIC_DEF("symlink", 3, qjs_fs_symlink, QJS_FS_PROMISE),
    JS_CFUNC_MAGIC_DEF("writeFile", 3, qjs_fs_write_file,
                       qjs_fs_magic(QJS_FS_PROMISE, QJS_FS_TRUNC)),
    JS_CFUNC_MAGIC_DEF("unlink", 1, qjs_fs_unlink, QJS_FS_PROMISE),
};


static const JSCFunctionListEntry qjs_fs_export[] = {
    JS_OBJECT_DEF("constants",
                  qjs_fs_constants,
                  njs_nitems(qjs_fs_constants),
                  JS_PROP_CONFIGURABLE),
    JS_OBJECT_DEF("promises",
                  qjs_fs_promises,
                  njs_nitems(qjs_fs_promises),
                  JS_PROP_CONFIGURABLE),
    JS_CFUNC_MAGIC_DEF("access", 3, qjs_fs_access, QJS_FS_CALLBACK),
    JS_CFUNC_MAGIC_DEF("accessSync", 2, qjs_fs_access, QJS_FS_DIRECT),
    JS_CFUNC_MAGIC_DEF("appendFile", 4, qjs_fs_write_file,
                       qjs_fs_magic(QJS_FS_CALLBACK, QJS_FS_APPEND)),
    JS_CFUNC_MAGIC_DEF("appendFileSync", 3, qjs_fs_write_file,
                       qjs_fs_magic(QJS_FS_DIRECT, QJS_FS_APPEND)),
    JS_CFUNC_MAGIC_DEF("closeSync", 1, qjs_fs_close, QJS_FS_DIRECT),
    JS_CFUNC_SPECIAL_DEF("Dirent", 1, constructor, qjs_fs_dirent_ctor),
    JS_CFUNC_DEF("existsSync", 1, qjs_fs_exists_sync),
    JS_CFUNC_MAGIC_DEF("fstatSync", 2, qjs_fs_stat,
                       qjs_fs_magic(QJS_FS_DIRECT, QJS_FS_FSTAT)),
    JS_CFUNC_MAGIC_DEF("lstat", 3, qjs_fs_stat,
                       qjs_fs_magic(QJS_FS_CALLBACK, QJS_FS_LSTAT)),
    JS_CFUNC_MAGIC_DEF("lstatSync", 2, qjs_fs_stat,
                       qjs_fs_magic(QJS_FS_DIRECT, QJS_FS_LSTAT)),
    JS_CFUNC_MAGIC_DEF("mkdir", 3, qjs_fs_mkdir, QJS_FS_CALLBACK),
    JS_CFUNC_MAGIC_DEF("mkdirSync", 2, qjs_fs_mkdir, QJS_FS_DIRECT),
    JS_CFUNC_MAGIC_DEF("openSync", 3, qjs_fs_open, QJS_FS_DIRECT),
    JS_CFUNC_MAGIC_DEF("readSync", 5, qjs_fs_read, QJS_FS_DIRECT),
    JS_CFUNC_MAGIC_DEF("readFile", 3, qjs_fs_read_file, QJS_FS_CALLBACK),
    JS_CFUNC_MAGIC_DEF("readFileSync", 2, qjs_fs_read_file, QJS_FS_DIRECT),
    JS_CFUNC_MAGIC_DEF("realpath", 3, qjs_fs_realpath, QJS_FS_CALLBACK),
    JS_CFUNC_MAGIC_DEF("realpathSync", 2, qjs_fs_realpath, QJS_FS_DIRECT),
    JS_CFUNC_MAGIC_DEF("readdir", 3, qjs_fs_readdir, QJS_FS_CALLBACK),
    JS_CFUNC_MAGIC_DEF("readdirSync", 2, qjs_fs_readdir, QJS_FS_DIRECT),
    JS_CFUNC_MAGIC_DEF("readlink", 3, qjs_fs_readlink, QJS_FS_CALLBACK),
    JS_CFUNC_MAGIC_DEF("readlinkSync", 2, qjs_fs_readlink, QJS_FS_DIRECT),
    JS_CFUNC_MAGIC_DEF("rename", 3, qjs_fs_rename, QJS_FS_CALLBACK),
    JS_CFUNC_MAGIC_DEF("renameSync", 2, qjs_fs_rename, QJS_FS_DIRECT),
    JS_CFUNC_MAGIC_DEF("rmdir", 3, qjs_fs_rmdir, QJS_FS_CALLBACK),
    JS_CFUNC_MAGIC_DEF("rmdirSync", 2, qjs_fs_rmdir, QJS_FS_DIRECT),
    JS_CFUNC_MAGIC_DEF("stat", 3, qjs_fs_stat,
                       qjs_fs_magic(QJS_FS_CALLBACK, QJS_FS_STAT)),
    JS_CFUNC_MAGIC_DEF("statSync", 2, qjs_fs_stat,
                       qjs_fs_magic(QJS_FS_DIRECT, QJS_FS_STAT)),
    JS_CFUNC_MAGIC_DEF("symlink", 4, qjs_fs_symlink, QJS_FS_CALLBACK),
    JS_CFUNC_MAGIC_DEF("symlinkSync", 3, qjs_fs_symlink, QJS_FS_DIRECT),
    JS_CFUNC_MAGIC_DEF("writeSync", 5, qjs_fs_write, QJS_FS_DIRECT),
    JS_CFUNC_MAGIC_DEF("writeFile", 4, qjs_fs_write_file,
                       qjs_fs_magic(QJS_FS_CALLBACK, QJS_FS_TRUNC)),
    JS_CFUNC_MAGIC_DEF("writeFileSync", 3, qjs_fs_write_file,
                       qjs_fs_magic(QJS_FS_DIRECT, QJS_FS_TRUNC)),
    JS_CFUNC_MAGIC_DEF("unlink", 2, qjs_fs_unlink, QJS_FS_CALLBACK),
    JS_CFUNC_MAGIC_DEF("unlinkSync", 1, qjs_fs_unlink, QJS_FS_DIRECT),
};


static JSClassDef qjs_fs_stats_class = {
    "Stats",
    .finalizer = qjs_fs_stats_finalizer,
    .exotic = & (JSClassExoticMethods) {
        .get_own_property = qjs_fs_stats_get_own_property,
        .get_own_property_names = qjs_fs_stats_get_own_property_names,
    },
};


static JSClassDef qjs_fs_filehandle_class = {
    "FileHandle",
    .finalizer = qjs_fs_filehandle_finalizer,
};


qjs_module_t  qjs_fs_module = {
    .name = "fs",
    .init = qjs_fs_init,
};


static JSValue
qjs_fs_access(JSContext *cx, JSValueConst this_val, int argc,
    JSValueConst *argv, int calltype)
{
    int         md, ret;
    JSValue     callback, mode, result;
    const char  *path;
    char        path_buf[NJS_MAX_PATH + 1];

    path = qjs_fs_path(cx, path_buf, argv[0], "path");
    if (path == NULL) {
        return JS_EXCEPTION;
    }

    mode = argv[1];
    callback = JS_UNDEFINED;

    if (calltype == QJS_FS_CALLBACK) {
        if (argc > 0) {
            callback = argv[njs_min(argc - 1, 2)];
        }

        if (!JS_IsFunction(cx, callback)) {
            JS_ThrowTypeError(cx, "\"callback\" must be a function");
            return JS_EXCEPTION;
        }

        if (JS_SameValue(cx, mode, callback)) {
            mode = JS_UNDEFINED;
        }
    }

    if (JS_IsNumber(mode)) {
        md = JS_VALUE_GET_INT(mode);

    } else if (JS_IsUndefined(mode)) {
        md = F_OK;

    } else {
        JS_ThrowTypeError(cx, "\"mode\" must be a number");
        return JS_EXCEPTION;
    }

    result = JS_UNDEFINED;

    ret = access(path, md);
    if (ret != 0) {
        result = qjs_fs_error(cx, "access", strerror(errno), path, errno);
    }

    if (JS_IsException(result)) {
        return JS_EXCEPTION;
    }

    return qjs_fs_result(cx, result, calltype, callback);
}


static JSValue
qjs_fs_exists_sync(JSContext *cx, JSValueConst this_val, int nargs,
    JSValueConst *args)
{
    const char  *path;
    char        path_buf[NJS_MAX_PATH + 1];

    path = qjs_fs_path(cx, path_buf, args[0], "path");
    if (path == NULL) {
        return JS_EXCEPTION;
    }

    return (access(path, F_OK) == 0) ? JS_TRUE : JS_FALSE;
}


static JSValue
qjs_fs_close(JSContext *cx, JSValueConst this_val, int nargs,
    JSValueConst *args, int calltype)
{
    int      fd;
    JSValue  result;

    if (calltype == QJS_FS_DIRECT) {
        if (JS_ToInt32(cx, &fd, args[0]) < 0) {
            return JS_EXCEPTION;
        }

    } else {
        fd = (intptr_t) JS_GetOpaque(this_val, QJS_CORE_CLASS_ID_FS_FILEHANDLE);
        if (fd == -1) {
            JS_ThrowTypeError(cx, "file was already closed");
            return JS_EXCEPTION;
        }

        JS_SetOpaque(this_val, (void *) -1);
    }

    result = JS_UNDEFINED;

    if (close(fd) != 0) {
        result = qjs_fs_error(cx, "close", strerror(errno), NULL, errno);
        goto done;
    }

done:

    return qjs_fs_result(cx, result, calltype, JS_UNDEFINED);
}


static JSValue
qjs_fs_make_path(JSContext *cx, char *path, mode_t md, int recursive)
{
    int          err;
    njs_int_t    ret;
    const char   *p, *prev, *end;
    struct stat  sb;

    end = path + strlen(path);

    if (!recursive) {
        ret = mkdir(path, md);
        if (ret != 0) {
            err = errno;
            goto failed;
        }

        return JS_UNDEFINED;
    }

    p = path;
    prev = p;

    for ( ;; ) {
        p = strchr(prev + 1, '/');
        if (p == NULL) {
            p = end;
        }

        if ((p - path) > NJS_MAX_PATH) {
            JS_ThrowInternalError(cx, "too large path");
            return JS_EXCEPTION;
        }

        path[p - path] = '\0';

        ret = mkdir(path, md);
        err = errno;

        switch (ret) {
        case 0:
            break;

        case EACCES:
        case ENOTDIR:
        case EPERM:
            goto failed;

        case EEXIST:
        default:
            ret = stat(path, &sb);
            if (ret == 0) {
                if (!S_ISDIR(sb.st_mode)) {
                    err = ENOTDIR;
                    goto failed;
                }

                break;
            }

            goto failed;
        }

        if (p == end) {
            break;
        }

        path[p - path] = '/';
        prev = p;
    }

    return JS_UNDEFINED;

failed:

    return qjs_fs_error(cx, "mkdir", strerror(err), path, err);
}


static JSValue
qjs_fs_mkdir(JSContext *cx, JSValueConst this_val, int argc,
    JSValueConst *argv, int calltype)
{
    int      recursive;
    char     *path;
    mode_t   md;
    JSValue  callback, v, options, result;
    char     path_buf[NJS_MAX_PATH + 1];

    path = qjs_fs_path(cx, path_buf, argv[0], "path");
    if (path == NULL) {
        return JS_EXCEPTION;
    }

    callback = JS_UNDEFINED;
    options = argv[1];

    if (calltype == QJS_FS_CALLBACK) {
        if (argc > 0) {
            callback = argv[njs_min(argc - 1, 2)];
        }

        if (!JS_IsFunction(cx, callback)) {
            JS_ThrowTypeError(cx, "\"callback\" must be a function");
            return JS_EXCEPTION;
        }

        if (JS_SameValue(cx, options, callback)) {
            options = JS_UNDEFINED;
        }
    }

    md = 0777;
    recursive = 0;

    if (JS_IsNumber(options)) {
        md = JS_VALUE_GET_INT(options);

    } else if (!JS_IsUndefined(options)) {
        if (!JS_IsObject(options)) {
            JS_ThrowTypeError(cx, "Unknown options type (a number or object "
                              "required)");
            return JS_EXCEPTION;
        }

        v = JS_GetPropertyStr(cx, options, "mode");
        if (!JS_IsUndefined(v) && !JS_IsException(v)) {
            md = qjs_fs_mode(cx, v, 0777);
            if (md == (mode_t) -1) {
                JS_FreeValue(cx, v);
                return JS_EXCEPTION;
            }
        }

        v = JS_GetPropertyStr(cx, options, "recursive");
        if (!JS_IsUndefined(v) && !JS_IsException(v)) {
            recursive = JS_ToBool(cx, v);
        }
    }

    result = qjs_fs_make_path(cx, path, md, recursive);

    if (JS_IsException(result)) {
        return JS_EXCEPTION;
    }

    return qjs_fs_result(cx, result, calltype, callback);
}


static JSValue
qjs_fs_open(JSContext *cx, JSValueConst this_val, int argc, JSValueConst *argv,
    int calltype)
{
    int         fd, flags;
    mode_t      md;
    JSValue     result;
    const char  *path;
    char        path_buf[NJS_MAX_PATH + 1];

    path = qjs_fs_path(cx, path_buf, argv[0], "path");
    if (path == NULL) {
        return JS_EXCEPTION;
    }

    flags = qjs_fs_flags(cx, argv[1], O_RDONLY);
    if (flags == -1) {
        return JS_EXCEPTION;
    }

    md = qjs_fs_mode(cx, argv[2], 0666);
    if (md == (mode_t) -1) {
        return JS_EXCEPTION;
    }

    fd = open(path, flags, md);
    if (fd < 0) {
        result = qjs_fs_error(cx, "open", strerror(errno), path, errno);
        goto done;
    }

    if (calltype == QJS_FS_DIRECT) {
        /* Leaks fd if user does not close it. */
        result = JS_NewInt32(cx, fd);

    } else {
        result = JS_NewObjectClass(cx, QJS_CORE_CLASS_ID_FS_FILEHANDLE);
        if (JS_IsException(result)) {
            (void) close(fd);
            goto done;
        }

        JS_SetOpaque(result, (void *) (intptr_t) fd);
    }

done:

    if (JS_IsException(result)) {
        return JS_EXCEPTION;
    }

    return qjs_fs_result(cx, result, calltype, JS_UNDEFINED);
}


static JSValue
qjs_fs_fd_read(JSContext *cx, int fd, njs_str_t *data)
{
    u_char   *p, *end, *start;
    size_t   size;
    ssize_t  n;

    size = data->length;

    if (size == 0) {
        size = 4096;
    }

    data->start = js_malloc(cx, size);
    if (data->start == NULL) {
        JS_ThrowOutOfMemory(cx);
        return JS_EXCEPTION;
    }

    p = data->start;
    end = p + size;

    for ( ;; ) {
        n = read(fd, p, end - p);

        if (n < 0) {
            js_free(cx, data->start);
            return JS_FALSE;
        }

        p += n;

        if (n == 0) {
            break;
        }

        if (end - p < 2048) {
            size *= 2;

            start = js_realloc(cx, data->start, size);
            if (start == NULL) {
                js_free(cx, data->start);
                JS_ThrowOutOfMemory(cx);
                return JS_EXCEPTION;
            }

            p = start + (p - data->start);
            end = start + size;
            data->start = start;
        }
    }

    data->length = p - data->start;

    return JS_TRUE;
}


static JSValue
qjs_fs_read(JSContext *cx, JSValueConst this_val, int argc,
    JSValueConst *argv, int calltype)
{
    int        fd, fd_offset;
    int64_t    length, pos, offset;
    ssize_t    n;
    JSValue    ret, result;
    njs_str_t  buffer;

    /*
     * fh.read(buffer, offset[, length[, position]])
     * fs.readSync(fd, buffer, offset[, length[, position]])
     */

    if (calltype == QJS_FS_DIRECT) {
        fd_offset = 0;
        if (JS_ToInt32(cx, &fd, argv[0]) < 0) {
            return JS_EXCEPTION;
        }

    } else {
        fd_offset = -1;
        if (JS_ToInt32(cx, &fd, this_val) < 0) {
            return JS_EXCEPTION;
        }
    }

    ret = qjs_typed_array_data(cx, argv[fd_offset + 1], &buffer);
    if (JS_IsException(ret)) {
        return ret;
    }

    if (JS_ToInt64(cx, &offset, argv[fd_offset + 2]) < 0) {
        return JS_EXCEPTION;
    }

    if (offset < 0 || (size_t) offset > buffer.length) {
        JS_ThrowRangeError(cx, "offset is out of range (must be <= %zu)",
                           buffer.length);
        return JS_EXCEPTION;
    }

    buffer.length -= offset;
    buffer.start += offset;

    if (!JS_IsUndefined(argv[fd_offset + 3])) {
        if (JS_ToInt64(cx, &length, argv[fd_offset + 3]) < 0) {
            return JS_EXCEPTION;
        }

        if (length < 0 || (size_t) length > buffer.length) {
            JS_ThrowRangeError(cx, "length is out of range (must be <= %zu)",
                               buffer.length);
            return JS_EXCEPTION;
        }

        buffer.length = length;
    }

    pos = -1;

    if (!JS_IsNullOrUndefined(argv[fd_offset + 4])) {
        if (JS_ToInt64(cx, &pos, argv[fd_offset + 4]) < 0) {
            return JS_EXCEPTION;
        }
    }

    if (pos == -1) {
        n = read(fd, buffer.start, buffer.length);

    } else {
        n = pread(fd, buffer.start, buffer.length, pos);
    }

    if (n == -1) {
        result = qjs_fs_error(cx, "read", strerror(errno), NULL, errno);
        goto done;
    }

    if (calltype == QJS_FS_PROMISE) {
        result = JS_NewObject(cx);
        if (JS_IsException(result)) {
            goto done;
        }

        if (JS_DefinePropertyValueStr(cx, result, "bytesRead",
                                      JS_NewInt32(cx, n), JS_PROP_ENUMERABLE)
            < 0)
        {
            JS_FreeValue(cx, result);
            result = JS_EXCEPTION;
            goto done;
        }

        if (JS_DefinePropertyValueStr(cx, result, "buffer",
                                      JS_DupValue(cx, argv[fd_offset + 1]),
                                      JS_PROP_ENUMERABLE)
            < 0)
        {
            JS_FreeValue(cx, result);
            result = JS_EXCEPTION;
            goto done;
        }

    } else {
        result = JS_NewInt32(cx, n);
    }

done:

    if (JS_IsException(result)) {
        return JS_EXCEPTION;
    }

    return qjs_fs_result(cx, result, calltype, JS_UNDEFINED);
}


static JSValue
qjs_fs_read_file(JSContext *cx, JSValueConst this_val, int argc,
    JSValueConst *argv, int calltype)
{
    int                          fd, flags;
    njs_str_t                    str;
    JSValue                      callback, v, encode, options, result;
    const char                   *path;
    struct stat                  sb;
    const qjs_buffer_encoding_t  *encoding;
    char                         path_buf[NJS_MAX_PATH + 1];

    path = qjs_fs_path(cx, path_buf, argv[0], "path");
    if (path == NULL) {
        return JS_EXCEPTION;
    }

    flags = O_RDONLY;

    options = argv[1];
    encode = JS_UNDEFINED;
    callback = JS_UNDEFINED;

    if (calltype == QJS_FS_CALLBACK) {
        if (argc > 0) {
            callback = argv[njs_min(argc - 1, 2)];
        }

        if (!JS_IsFunction(cx, callback)) {
            JS_ThrowTypeError(cx, "\"callback\" must be a function");
            return JS_EXCEPTION;
        }

        if (JS_SameValue(cx, options, callback)) {
            options = JS_UNDEFINED;
        }
    }

    if (JS_IsString(options)) {
        encode = JS_DupValue(cx, options);

    } else if (!JS_IsUndefined(options)) {
        if (!JS_IsObject(options)) {
            JS_ThrowTypeError(cx, "Unknown options type (a string or object "
                              "required)");
            return JS_EXCEPTION;
        }

        v = JS_GetPropertyStr(cx, options, "flag");
        if (!JS_IsUndefined(v) && !JS_IsException(v)) {
            flags = qjs_fs_flags(cx, v, O_RDONLY);
            if (flags == -1) {
                JS_FreeValue(cx, v);
                return JS_EXCEPTION;
            }
        }

        v = JS_GetPropertyStr(cx, options, "encoding");
        if (!JS_IsUndefined(v) && !JS_IsException(v)) {
            encode = v;
        }
    }

    encoding = NULL;
    if (!JS_IsUndefined(encode)) {
        encoding = qjs_buffer_encoding(cx, encode, 1);
        if (encoding == NULL) {
            JS_FreeValue(cx, encode);
            return JS_EXCEPTION;
        }
    }

    JS_FreeValue(cx, encode);

    fd = open(path, flags);
    if (fd < 0) {
        result = qjs_fs_error(cx, "open", strerror(errno), path, errno);
        goto done;
    }

    if (fstat(fd, &sb) == -1) {
        result = qjs_fs_error(cx, "stat", strerror(errno), path, errno);
        goto done;
    }

    if (!S_ISREG(sb.st_mode)) {
        result = qjs_fs_error(cx, "stat", "File is not regular", path, 0);
        goto done;
    }

    str.start = NULL;
    str.length = sb.st_size;

    v = qjs_fs_fd_read(cx, fd, &str);
    if (!JS_SameValue(cx, v, JS_TRUE)) {
        if (JS_IsException(v)) {
            result = JS_EXCEPTION;

        } else {
            result = qjs_fs_error(cx, "read", strerror(errno), path, errno);
        }

        goto done;
    }

    result = qjs_fs_encode(cx, encoding, &str);
    js_free(cx, str.start);

done:

    if (fd != -1) {
        (void) close(fd);
    }

    if (JS_IsException(result)) {
        return JS_EXCEPTION;
    }

    return qjs_fs_result(cx, result, calltype, callback);
}


static JSValue
qjs_fs_readlink(JSContext *cx, JSValueConst this_val, int argc,
    JSValueConst *argv, int calltype)
{
    ssize_t                      n;
    JSValue                      callback, v, result, encode, options;
    njs_str_t                    str;
    const char                   *path, *enc;
    const qjs_buffer_encoding_t  *encoding;
    char                         path_buf[NJS_MAX_PATH + 1],
                                 dst_buf[NJS_MAX_PATH + 1];

    path = qjs_fs_path(cx, path_buf, argv[0], "path");
    if (path == NULL) {
        return JS_EXCEPTION;
    }

    options = argv[1];
    encode = JS_UNDEFINED;
    callback = JS_UNDEFINED;

    if (calltype == QJS_FS_CALLBACK) {
        callback = argv[njs_min(argc - 1, 2)];

        if (!JS_IsFunction(cx, callback)) {
            JS_ThrowTypeError(cx, "\"callback\" must be a function");
            return JS_EXCEPTION;
        }

        if (JS_SameValue(cx, options, callback)) {
            options = JS_UNDEFINED;
        }
    }

    if (JS_IsString(options)) {
        encode = JS_DupValue(cx, options);

    } else if (!JS_IsUndefined(options)) {
        if (!JS_IsObject(options)) {
            JS_ThrowTypeError(cx, "Unknown options type (a string or object "
                              "required)");
            return JS_EXCEPTION;
        }

        v = JS_GetPropertyStr(cx, options, "encoding");
        if (!JS_IsUndefined(v) && !JS_IsException(v)) {
            encode = v;
        }
    }

    encoding = NULL;
    enc = JS_ToCString(cx, encode);
    if (enc == NULL) {
        JS_FreeValue(cx, encode);
        return JS_EXCEPTION;
    }

    if (strncmp(enc, "buffer", 6) != 0) {
        encoding = qjs_buffer_encoding(cx, encode, 1);
        if (encoding == NULL) {
            JS_FreeCString(cx, enc);
            JS_FreeValue(cx, encode);
            return JS_EXCEPTION;
        }
    }

    JS_FreeCString(cx, enc);
    JS_FreeValue(cx, encode);

    str.start = (u_char *) dst_buf;
    n = readlink(path, dst_buf, sizeof(dst_buf) - 1);
    if (n < 0) {
        result = qjs_fs_error(cx, "readlink", strerror(errno), path, errno);
        goto done;
    }

    str.length = n;

    result = qjs_fs_encode(cx, encoding, &str);

done:

    if (JS_IsException(result)) {
        return JS_EXCEPTION;
    }

    return qjs_fs_result(cx, result, calltype, callback);
}


static JSValue
qjs_fs_dirent_create(JSContext *cx, JSValueConst name, struct dirent *entry)
{
    JSValue  obj;

    obj = JS_NewObjectClass(cx, QJS_CORE_CLASS_ID_FS_DIRENT);
    if (JS_IsException(obj)) {
        return JS_EXCEPTION;
    }

    if (JS_DefinePropertyValueStr(cx, obj, "name", name,
                                  JS_PROP_ENUMERABLE) < 0)
    {
        JS_FreeValue(cx, obj);
        return JS_EXCEPTION;
    }

    if (entry != NULL) {
        if (JS_DefinePropertyValueStr(cx, obj, "type",
                                      JS_NewInt32(cx, qjs_dentry_type(entry)),
                                      0) < 0)
        {
            JS_FreeValue(cx, obj);
            return JS_EXCEPTION;
        }
    }

    return obj;
}


static JSValue
qjs_fs_dirent_ctor(JSContext *cx, JSValueConst new_target, int argc,
    JSValueConst *argv)
{
    if (argc < 1) {
        JS_ThrowTypeError(cx, "name is required");
        return JS_EXCEPTION;
    }

    return qjs_fs_dirent_create(cx, JS_DupValue(cx, argv[0]), NULL);
}


static JSValue
qjs_fs_readdir(JSContext *cx, JSValueConst this_val, int argc,
    JSValueConst *argv, int calltype)
{
    DIR                          *dir;
    int                          types, idx;
    njs_str_t                    str;
    JSValue                      callback, v, result, encode, options, ename;
    const char                   *path, *enc;
    struct dirent                *entry;
    const qjs_buffer_encoding_t  *encoding;
    char                         path_buf[NJS_MAX_PATH + 1];

    path = qjs_fs_path(cx, path_buf, argv[0], "path");
    if (path == NULL) {
        return JS_EXCEPTION;
    }

    types = 0;
    options = argv[1];
    encode = JS_UNDEFINED;
    callback = JS_UNDEFINED;

    if (calltype == QJS_FS_CALLBACK) {
        if (argc > 0) {
            callback = argv[njs_min(argc - 1, 2)];
        }

        if (!JS_IsFunction(cx, callback)) {
            JS_ThrowTypeError(cx, "\"callback\" must be a function");
            return JS_EXCEPTION;
        }

        if (JS_SameValue(cx, options, callback)) {
            options = JS_UNDEFINED;
        }
    }

    if (JS_IsString(options)) {
        encode = JS_DupValue(cx, options);

    } else if (!JS_IsUndefined(options)) {
        if (!JS_IsObject(options)) {
            JS_ThrowTypeError(cx, "Unknown options type (a string or object "
                              "required)");
            return JS_EXCEPTION;
        }

        v = JS_GetPropertyStr(cx, options, "encoding");
        if (!JS_IsUndefined(v) && !JS_IsException(v)) {
            encode = v;
        }

        v = JS_GetPropertyStr(cx, options, "withFileTypes");
        if (!JS_IsUndefined(v) && !JS_IsException(v)) {
            types = JS_ToBool(cx, v);
        }
    }

    encoding = NULL;

    enc = JS_ToCString(cx, encode);
    if (enc == NULL) {
        JS_FreeValue(cx, encode);
        return JS_EXCEPTION;
    }

    if (strncmp(enc, "buffer", 6) != 0) {
        encoding = qjs_buffer_encoding(cx, encode, 1);
        if (encoding == NULL) {
            JS_FreeCString(cx, enc);
            JS_FreeValue(cx, encode);
            return JS_EXCEPTION;
        }
    }

    JS_FreeCString(cx, enc);
    JS_FreeValue(cx, encode);

    idx = 0;
    dir = opendir(path);

    if (dir == NULL) {
        result = qjs_fs_error(cx, "opendir", strerror(errno), path, errno);
        goto done;
    }

    result = JS_NewArray(cx);
    if (JS_IsException(result)) {
        return JS_EXCEPTION;
    }

    for ( ;; ) {
        errno = 0;
        entry = readdir(dir);
        if (entry == NULL) {
            if (errno != 0) {
                JS_FreeValue(cx, result);
                result = qjs_fs_error(cx, "readdir", strerror(errno), path,
                                      errno);
            }

            break;
        }

        if (entry->d_name[0] == '.' && (entry->d_name[1] == '\0'
            || (entry->d_name[1] == '.' && entry->d_name[2] == '\0')))
        {
            continue;
        }

        str.start = (u_char *) entry->d_name;
        str.length = strlen((const char *) str.start);

        if (str.length == 0) {
            continue;
        }

        ename = qjs_fs_encode(cx, encoding, &str);
        if (JS_IsException(ename)) {
            JS_FreeValue(cx, result);
            goto done;
        }

        if (!types) {
            if (JS_DefinePropertyValueUint32(cx, result, idx++, ename, 0) < 0) {
                JS_FreeValue(cx, ename);
                JS_FreeValue(cx, result);
                goto done;
            }

        } else {
            v = qjs_fs_dirent_create(cx, ename, entry);
            if (JS_IsException(v)) {
                JS_FreeValue(cx, ename);
                JS_FreeValue(cx, result);
                goto done;
            }

            if (JS_DefinePropertyValueUint32(cx, result, idx++, v, 0) < 0) {
                JS_FreeValue(cx, ename);
                JS_FreeValue(cx, result);
                goto done;
            }
        }
    }

done:

    if (dir != NULL) {
        (void) closedir(dir);
    }

    if (JS_IsException(result)) {
        return JS_EXCEPTION;
    }

    return qjs_fs_result(cx, result, calltype, callback);
}



static JSValue
qjs_fs_realpath(JSContext *cx, JSValueConst this_val, int argc,
    JSValueConst *argv, int calltype)
{
    JSValue                      callback, v, encode, options, result;
    njs_str_t                    str;
    const char                   *path, *enc;
    const qjs_buffer_encoding_t  *encoding;
    char                         path_buf[NJS_MAX_PATH + 1],
                                 dst_buf[NJS_MAX_PATH + 1];

    path = qjs_fs_path(cx, path_buf, argv[0], "path");
    if (path == NULL) {
        return JS_EXCEPTION;
    }

    options = argv[1];
    encode = JS_UNDEFINED;
    callback = JS_UNDEFINED;

    if (calltype == QJS_FS_CALLBACK) {
        if (argc > 0) {
            callback = argv[njs_min(argc - 1, 2)];
        }

        if (!JS_IsFunction(cx, callback)) {
            JS_ThrowTypeError(cx, "\"callback\" must be a function");
            return JS_EXCEPTION;
        }

        if (JS_SameValue(cx, options, callback)) {
            options = JS_UNDEFINED;
        }
    }

    if (JS_IsString(options)) {
        encode = JS_DupValue(cx, options);

    } else if (!JS_IsUndefined(options)) {
        if (!JS_IsObject(options)) {
            JS_ThrowTypeError(cx, "Unknown options type (a string or object "
                              "required)");
            return JS_EXCEPTION;
        }

        v = JS_GetPropertyStr(cx, options, "encoding");
        if (!JS_IsUndefined(v) && !JS_IsException(v)) {
            encode = v;
        }
    }

    enc = JS_ToCString(cx, encode);
    if (enc == NULL) {
        JS_FreeValue(cx, encode);
        return JS_EXCEPTION;
    }

    encoding = NULL;
    if (strncmp(enc, "buffer", 6) != 0) {
        encoding = qjs_buffer_encoding(cx, encode, 1);
        if (encoding == NULL) {
            JS_FreeCString(cx, enc);
            JS_FreeValue(cx, encode);
            return JS_EXCEPTION;
        }
    }

    JS_FreeCString(cx, enc);
    JS_FreeValue(cx, encode);

    str.start = (u_char *) realpath(path, dst_buf);
    if (str.start == NULL) {
        result = qjs_fs_error(cx, "realpath", strerror(errno), path, errno);
        goto done;
    }

    str.length = strlen((const char *) str.start);

    result = qjs_fs_encode(cx, encoding, &str);

done:

    if (JS_IsException(result)) {
        return JS_EXCEPTION;
    }

    return qjs_fs_result(cx, result, calltype, callback);
}


static JSValue
qjs_fs_rename(JSContext *cx, JSValueConst this_val, int argc,
    JSValueConst *argv, int calltype)
{
    int         ret;
    JSValue     callback, result;
    const char  *path, *newpath;
    char        path_buf[NJS_MAX_PATH + 1],
                newpath_buf[NJS_MAX_PATH + 1];

    path = qjs_fs_path(cx, path_buf, argv[0], "oldPath");
    if (path == NULL) {
        return JS_EXCEPTION;
    }

    newpath = qjs_fs_path(cx, newpath_buf, argv[1], "newPath");
    if (newpath == NULL) {
        return JS_EXCEPTION;
    }

    callback = JS_UNDEFINED;

    if (calltype == QJS_FS_CALLBACK) {
        callback = argv[2];

        if (!JS_IsFunction(cx, callback)) {
            JS_ThrowTypeError(cx, "\"callback\" must be a function");
            return JS_EXCEPTION;
        }
    }

    result = JS_UNDEFINED;

    ret = rename(path, newpath);
    if (ret != 0) {
        result = qjs_fs_error(cx, "rename", strerror(errno), NULL, errno);
    }

    if (JS_IsException(result)) {
        return JS_EXCEPTION;
    }

    return qjs_fs_result(cx, result, calltype, callback);
}


typedef struct qjs_ftw_trace_s  qjs_ftw_trace_t;

struct qjs_ftw_trace_s {
    struct qjs_ftw_trace_s  *chain;
    dev_t                   dev;
    ino_t                   ino;
};


static int
qjs_ftw(char *path, qjs_file_tree_walk_cb_t cb, int fd_limit,
    qjs_ftw_flags_t flags, qjs_ftw_trace_t *parent)
{
    int              type, ret, dfd;
    DIR              *d;
    size_t           base, len, length;
    const char       *d_name;
    struct stat      st;
    struct dirent    *entry;
    qjs_ftw_trace_t  trace, *h;

    ret = (flags & QJS_FTW_PHYS) ? lstat(path, &st) : stat(path, &st);

    if (ret < 0) {
        if (!(flags & QJS_FTW_PHYS) && errno == ENOENT && !lstat(path, &st)) {
            type = QJS_FTW_SLN;

        } else if (errno != EACCES) {
            return -1;

        } else {
            type = QJS_FTW_NS;
        }

    } else if (S_ISDIR(st.st_mode)) {
        type = (flags & QJS_FTW_DEPTH) ? QJS_FTW_DP : QJS_FTW_D;

    } else if (S_ISLNK(st.st_mode)) {
        type = (flags & QJS_FTW_PHYS) ? QJS_FTW_SL : QJS_FTW_SLN;

    } else {
        type = QJS_FTW_F;
    }

    if ((flags & QJS_FTW_MOUNT) && parent != NULL && st.st_dev != parent->dev) {
        return 0;
    }

    for (h = parent; h != NULL; h = h->chain) {
        if (h->dev == st.st_dev && h->ino == st.st_ino) {
            return 0;
        }
    }

    len = strlen(path);
    base = len && (path[len - 1] == '/') ? len - 1 : len;

    trace.chain = parent;
    trace.dev = st.st_dev;
    trace.ino = st.st_ino;

    d = NULL;
    dfd = -1;

    if (type == QJS_FTW_D || type == QJS_FTW_DP) {
        dfd = open(path, O_RDONLY);
        if (dfd < 0) {
            if (errno != EACCES) {
                return -1;
            }

            type = QJS_FTW_DNR;
        }
    }

    if (!(flags & QJS_FTW_DEPTH)) {
        ret = cb(path, &st, type);
        if (ret != 0) {
            goto done;
        }
    }

    if (type == QJS_FTW_D || type == QJS_FTW_DP) {
        d = fdopendir(dfd);
        if (d == NULL) {
            ret = -1;
            goto done;
        }

        for ( ;; ) {
            entry = readdir(d);

            if (entry == NULL) {
                break;
            }

            d_name = entry->d_name;
            length = strlen(d_name);

            if ((length == 1 && d_name[0] == '.')
                || (length == 2 && (d_name[0] == '.' && d_name[1] == '.')))
            {
                continue;
            }

            if (length >= (NJS_MAX_PATH - len)) {
                errno = ENAMETOOLONG;
                ret = -1;
                goto done;
            }

            path[base] = '/';
            memcpy(&path[base + 1], d_name, length + njs_length("\0"));

            if (fd_limit != 0) {
                ret = qjs_ftw(path, cb, fd_limit - 1, flags, &trace);
                if (ret != 0) {
                    goto done;
                }
            }
        }

        (void) closedir(d);
        d = NULL;
        dfd = -1;
    }

    path[len] = '\0';

    if (flags & QJS_FTW_DEPTH) {
        ret = cb(path, &st, type);
        if (ret != 0) {
            return ret;
        }
    }

    ret = 0;

done:

    if (d != NULL) {
        /* closedir() also closes underlying dfd. */
        (void) closedir(d);

    } else if (dfd >= 0) {
        (void) close(dfd);
    }

    return ret;
}


static int
qjs_file_tree_walk(const char *path, qjs_file_tree_walk_cb_t cb, int fd_limit,
    qjs_ftw_flags_t flags)
{
    size_t  len;
    char    pathbuf[NJS_MAX_PATH + 1];

    len = strlen(path);
    if (len > NJS_MAX_PATH) {
        errno = ENAMETOOLONG;
        return -1;
    }

    memcpy(pathbuf, path, len + 1);

    return qjs_ftw(pathbuf, cb, fd_limit, flags, NULL);
}


static int
qjs_fs_rmtree_cb(const char *path, const struct stat *sb, qjs_ftw_type_t type)
{
    njs_int_t  ret;

    ret = remove(path);
    if (ret != 0) {
        return NJS_ERROR;
    }

    return NJS_OK;
}


static JSValue
qjs_fs_rmtree(JSContext *cx, const char *path, int recursive)
{
    njs_int_t   ret;
    const char  *description;

    ret = rmdir(path);
    if (ret == 0) {
        return JS_UNDEFINED;
    }

    description = strerror(errno);

    if (recursive && (errno == ENOTEMPTY || errno == EEXIST)) {
        ret = qjs_file_tree_walk(path, qjs_fs_rmtree_cb, 16,
                                 QJS_FTW_PHYS | QJS_FTW_MOUNT | QJS_FTW_DEPTH);

        if (ret == 0) {
            return JS_UNDEFINED;
        }

        description = strerror(errno);
    }

    return qjs_fs_error(cx, "rmdir", description, path, errno);
}


static JSValue
qjs_fs_rmdir(JSContext *cx, JSValueConst this_val, int argc,
    JSValueConst *argv, int calltype)
{
    int         recursive;
    JSValue     callback, v, options, result;
    const char  *path;
    char        path_buf[NJS_MAX_PATH + 1];

    path = qjs_fs_path(cx, path_buf, argv[0], "path");
    if (path == NULL) {
        return JS_EXCEPTION;
    }

    recursive = 0;

    options = argv[1];
    callback = JS_UNDEFINED;

    if (calltype == QJS_FS_CALLBACK) {
        callback = argv[njs_min(argc - 1, 2)];

        if (!JS_IsFunction(cx, callback)) {
            JS_ThrowTypeError(cx, "\"callback\" must be a function");
            return JS_EXCEPTION;
        }

        if (JS_SameValue(cx, options, callback)) {
            options = JS_UNDEFINED;
        }
    }

    if (!JS_IsUndefined(options)) {
        if (!JS_IsObject(options)) {
            JS_ThrowTypeError(cx, "Unknown options type (an object required)");
            return JS_EXCEPTION;
        }

        v = JS_GetPropertyStr(cx, options, "recursive");
        if (!JS_IsUndefined(v) && !JS_IsException(v)) {
            recursive = JS_ToBool(cx, v);
        }
    }

    result = qjs_fs_rmtree(cx, path, recursive);

    if (JS_IsException(result)) {
        return JS_EXCEPTION;
    }

    return qjs_fs_result(cx, result, calltype, callback);
}


static void
qjs_fs_to_stat(qjs_stat_t *dst, struct stat *st)
{
    dst->st_dev = st->st_dev;
    dst->st_mode = st->st_mode;
    dst->st_nlink = st->st_nlink;
    dst->st_uid = st->st_uid;
    dst->st_gid = st->st_gid;
    dst->st_rdev = st->st_rdev;
    dst->st_ino = st->st_ino;
    dst->st_size = st->st_size;
    dst->st_blksize = st->st_blksize;
    dst->st_blocks = st->st_blocks;

#if (NJS_HAVE_STAT_ATIMESPEC)

    dst->st_atim.tv_sec = st->st_atimespec.tv_sec;
    dst->st_atim.tv_nsec = st->st_atimespec.tv_nsec;
    dst->st_mtim.tv_sec = st->st_mtimespec.tv_sec;
    dst->st_mtim.tv_nsec = st->st_mtimespec.tv_nsec;
    dst->st_ctim.tv_sec = st->st_ctimespec.tv_sec;
    dst->st_ctim.tv_nsec = st->st_ctimespec.tv_nsec;

#elif (NJS_HAVE_STAT_ATIM)

    dst->st_atim.tv_sec = st->st_atim.tv_sec;
    dst->st_atim.tv_nsec = st->st_atim.tv_nsec;
    dst->st_mtim.tv_sec = st->st_mtim.tv_sec;
    dst->st_mtim.tv_nsec = st->st_mtim.tv_nsec;
    dst->st_ctim.tv_sec = st->st_ctim.tv_sec;
    dst->st_ctim.tv_nsec = st->st_ctim.tv_nsec;

#if (NJS_HAVE_STAT_BIRTHTIM)
    dst->st_birthtim.tv_sec = st->st_birthtim.tv_sec;
    dst->st_birthtim.tv_nsec = st->st_birthtim.tv_nsec;
#elif (NJS_HAVE__STAT_BIRTHTIM)
    dst->st_birthtim.tv_sec = st->__st_birthtim.tv_sec;
    dst->st_birthtim.tv_nsec = st->__st_birthtim.tv_nsec;
#else
    dst->st_birthtim.tv_sec = st->st_ctim.tv_sec;
    dst->st_birthtim.tv_nsec = st->st_ctim.tv_nsec;
#endif

#else

  dst->st_atim.tv_sec = st->st_atime;
  dst->st_atim.tv_nsec = 0;
  dst->st_mtim.tv_sec = st->st_mtime;
  dst->st_mtim.tv_nsec = 0;
  dst->st_ctim.tv_sec = st->st_ctime;
  dst->st_ctim.tv_nsec = 0;
  dst->st_birthtim.tv_sec = st->st_ctime;
  dst->st_birthtim.tv_nsec = 0;

#endif
}


static JSValue
qjs_fs_stats_create(JSContext *cx, struct stat *st)
{
    JSValue     obj;
    qjs_stat_t  *stat;

    stat = js_malloc(cx, sizeof(qjs_stat_t));
    if (stat == NULL) {
        JS_ThrowOutOfMemory(cx);
        return JS_EXCEPTION;
    }

    qjs_fs_to_stat(stat, st);

    obj = JS_NewObjectClass(cx, QJS_CORE_CLASS_ID_FS_STATS);
    if (JS_IsException(obj)) {
        js_free(cx, stat);
        return JS_EXCEPTION;
    }

    JS_SetOpaque(obj, stat);

    return obj;
}


static JSValue
qjs_fs_stat(JSContext *cx, JSValueConst this_val, int argc, JSValueConst *argv,
    int magic)
{
    int          ret, fd, fd_offset, throw, calltype;
    JSValue      callback, result, options, value;
    const char   *path;
    struct stat  sb;
    char         path_buf[NJS_MAX_PATH + 1];

    fd = -1;
    path = NULL;
    calltype = magic & 3;

    if ((magic >> 2) != QJS_FS_FSTAT) {
        path = qjs_fs_path(cx, path_buf, argv[0], "path");
        if (path == NULL) {
            return JS_EXCEPTION;
        }

        options = argv[1];

    } else {
        if (calltype == QJS_FS_DIRECT) {
            fd_offset = 0;
            if (JS_ToInt32(cx, &fd, argv[fd_offset]) < 0) {
                return JS_EXCEPTION;
            }

        } else {
            fd_offset = -1;
            if (JS_ToInt32(cx, &fd, this_val) < 0) {
                return JS_EXCEPTION;
            }
        }

        options = argv[fd_offset + 1];
    }

    callback = JS_UNDEFINED;

    if (calltype == QJS_FS_CALLBACK) {
        if (argc > 0) {
            callback = argv[njs_min(argc - 1, 2)];
        }

        if (!JS_IsFunction(cx, callback)) {
            JS_ThrowTypeError(cx, "\"callback\" must be a function");
            return JS_EXCEPTION;
        }

        if (JS_SameValue(cx, options, callback)) {
            options = JS_UNDEFINED;
        }
    }

    throw = 1;

    if (!JS_IsUndefined(options)) {
        if (!JS_IsObject(options)) {
            JS_ThrowTypeError(cx, "Unknown options type (an object required)");
            return JS_EXCEPTION;
        }

        value = JS_GetPropertyStr(cx, options, "bigint");
        if (!JS_IsUndefined(value)) {
            JS_ThrowTypeError(cx, "\"bigint\" is not supported");
            return JS_EXCEPTION;
        }

        if (calltype == QJS_FS_DIRECT) {
            value = JS_GetPropertyStr(cx, options, "throwIfNoEntry");
            if (!JS_IsUndefined(value)) {
                throw = JS_ToBool(cx, value);
            }
        }
    }

    switch (magic >> 2) {
    case QJS_FS_STAT:
        ret = stat(path, &sb);
        break;

    case QJS_FS_LSTAT:
        ret = lstat(path, &sb);
        break;

    case QJS_FS_FSTAT:
    default:
        ret = fstat(fd, &sb);
        break;
    }

    if (ret != 0) {
        if (errno != ENOENT || throw) {
            result = qjs_fs_error(cx, ((magic >> 2) == QJS_FS_STAT)
                                      ? "stat" : "lstat",
                                  strerror(errno), path, errno);
            if (JS_IsException(result)) {
                return JS_EXCEPTION;
            }

        } else {
            result = JS_UNDEFINED;
        }

        return qjs_fs_result(cx, result, calltype, callback);
    }

    result = qjs_fs_stats_create(cx, &sb);

    if (JS_IsException(result)) {
        return JS_EXCEPTION;
    }

    return qjs_fs_result(cx, result, calltype, callback);
}


static JSValue
qjs_fs_symlink(JSContext *cx, JSValueConst this_val, int argc,
    JSValueConst *argv, int calltype)
{
    int         ret;
    JSValue     callback, result, type;
    const char  *target, *path;
    char        target_buf[NJS_MAX_PATH + 1],
                path_buf[NJS_MAX_PATH + 1];

    target = qjs_fs_path(cx, target_buf, argv[0], "target");
    if (target == NULL) {
        return JS_EXCEPTION;
    }

    path = qjs_fs_path(cx, path_buf, argv[1], "path");
    if (path == NULL) {
        return JS_EXCEPTION;
    }

    callback = JS_UNDEFINED;
    type = argv[2];

    if (calltype == QJS_FS_CALLBACK) {
        callback = argv[njs_min(argc - 1, 3)];

        if (!JS_IsFunction(cx, callback)) {
            JS_ThrowTypeError(cx, "\"callback\" must be a function");
            return JS_EXCEPTION;
        }

        if (JS_SameValue(cx, type, callback)) {
            type = JS_UNDEFINED;
        }
    }

    if (!JS_IsUndefined(type) && !JS_IsString(type)) {
        JS_ThrowTypeError(cx, "\"type\" must be a string");
        return JS_EXCEPTION;
    }

    result = JS_UNDEFINED;

    ret = symlink(target, path);
    if (ret != 0) {
        result = qjs_fs_error(cx, "symlink", strerror(errno), path, errno);
    }

    if (JS_IsException(result)) {
        return JS_EXCEPTION;
    }

    return qjs_fs_result(cx, result, calltype, callback);
}


static JSValue
qjs_fs_write(JSContext *cx, JSValueConst this_val, int argc,
    JSValueConst *argv, int calltype)
{
    int                          fd, fd_offset;
    u_char                       *to_free_content;
    int64_t                      length, pos, offset;
    ssize_t                      n;
    JSValue                      buffer, v, result;
    njs_str_t                    str, content;
    const qjs_buffer_encoding_t  *encoding;

    if (calltype == QJS_FS_DIRECT) {
        fd_offset = 0;
        if (JS_ToInt32(cx, &fd, argv[0]) < 0) {
            return JS_EXCEPTION;
        }

    } else {
        fd_offset = -1;
        if (JS_ToInt32(cx, &fd, this_val) < 0) {
            return JS_EXCEPTION;
        }
    }

    pos = -1;
    encoding = NULL;
    str.start = NULL;
    to_free_content = NULL;
    buffer = argv[fd_offset + 1];

    /*
     * fs.writeSync(fd, string[, position[, encoding]])
     * fh.write(string[, position[, encoding]])
     */

    if (JS_IsString(buffer)) {
        v = argv[fd_offset + 2];

        if (!JS_IsNullOrUndefined(v)) {
            if (JS_ToInt64(cx, &pos, v) < 0) {
                return JS_EXCEPTION;
            }
        }

        encoding = qjs_buffer_encoding(cx, argv[fd_offset + 3], 1);
        if (encoding == NULL) {
            return JS_EXCEPTION;
        }

        str.start = (u_char *) JS_ToCStringLen(cx, &str.length, buffer);
        if (str.start == NULL) {
            return JS_EXCEPTION;
        }

        if (encoding->decode_length != NULL) {
            content.length = encoding->decode_length(cx, &str);
            content.start = js_malloc(cx, content.length);
            if (content.start == NULL) {
                JS_FreeCString(cx, (const char *) str.start);
                JS_ThrowOutOfMemory(cx);
                return JS_EXCEPTION;
            }

            to_free_content = content.start;

            if (encoding->decode(cx, &str, &content) != 0) {
                JS_FreeCString(cx, (const char *) str.start);
                return JS_EXCEPTION;
            }

        } else {
            content.start = (u_char *) str.start;
            content.length = str.length;
        }

        goto process;
    }

    /*
     * fh.write(buffer, offset[, length[, position]])
     * fs.writeSync(fd, buffer, offset[, length[, position]])
     */

    v = qjs_typed_array_data(cx, buffer, &content);
    if (JS_IsException(v)) {
        return JS_EXCEPTION;
    }

    if (JS_ToInt64(cx, &offset, argv[fd_offset + 2]) < 0) {
        return JS_EXCEPTION;
    }

    if (offset < 0 || (size_t) offset > content.length) {
        JS_ThrowRangeError(cx, "offset is out of range (must be <= %zu)",
                           content.length);
        return JS_EXCEPTION;
    }

    content.length -= offset;
    content.start += offset;

    v = argv[fd_offset + 3];
    if (!JS_IsNullOrUndefined(v)) {
        if (JS_ToInt64(cx, &length, v) < 0) {
            return JS_EXCEPTION;
        }

        if (length < 0 || (size_t) length > content.length) {
            JS_ThrowRangeError(cx, "length is out of range (must be <= %zu)",
                               content.length);
            return JS_EXCEPTION;
        }

        content.length = length;
    }

    v = argv[fd_offset + 4];
    if (!JS_IsNullOrUndefined(v)) {
        if (JS_ToInt64(cx, &pos, v) < 0) {
            return JS_EXCEPTION;
        }
    }

process:

    if (pos == -1) {
        n = write(fd, content.start, content.length);

    } else {
        n = pwrite(fd, content.start, content.length, pos);
    }

    if (n == -1) {
        result = qjs_fs_error(cx, "write", strerror(errno), NULL, errno);
        goto done;
    }

    if ((size_t) n != content.length) {
        result = qjs_fs_error(cx, "write", "failed to write all the data",
                              NULL, 0);
        goto done;
    }

    if (calltype == QJS_FS_PROMISE) {
        result = JS_NewObject(cx);
        if (JS_IsException(result)) {
            goto done;
        }

        if (JS_DefinePropertyValueStr(cx, result, "bytesWritten",
                                      JS_NewInt32(cx, n),
                                      JS_PROP_C_W_E) < 0)
        {
            JS_FreeValue(cx, result);
            result = JS_EXCEPTION;
            goto done;
        }

        buffer = JS_DupValue(cx, buffer);

        if (JS_DefinePropertyValueStr(cx, result, "buffer", buffer,
                                      JS_PROP_C_W_E) < 0)
        {
            JS_FreeValue(cx, result);
            JS_FreeValue(cx, buffer);
            result = JS_EXCEPTION;
            goto done;
        }

    } else {
        result = JS_NewInt32(cx, n);
    }

done:

    if (str.start != NULL) {
        JS_FreeCString(cx, (const char *) str.start);
    }

    if (to_free_content != NULL) {
        js_free(cx, to_free_content);
    }

    return qjs_fs_result(cx, result, calltype, JS_UNDEFINED);
}


static JSValue
qjs_fs_write_file(JSContext *cx, JSValueConst this_val, int argc,
    JSValueConst *argv, int magic)
{
    int                          fd, flags, want_to_free_content;
    u_char                       *p, *end;
    mode_t                       md;
    ssize_t                      n;
    JSValue                      callback, data, v, encode, options, result;
    njs_str_t                    str, content;
    const char                   *path;
    qjs_fs_calltype_t            calltype;
    const qjs_buffer_encoding_t  *encoding;
    char                         path_buf[NJS_MAX_PATH + 1];

    path = qjs_fs_path(cx, path_buf, argv[0], "path");
    if (path == NULL) {
        return JS_EXCEPTION;
    }

    md = 0666;
    flags = O_CREAT | O_WRONLY;
    flags |= ((magic >> 2) == QJS_FS_APPEND) ? O_APPEND : O_TRUNC;
    calltype = magic & 3;

    encode = JS_UNDEFINED;
    callback = JS_UNDEFINED;
    options = argv[2];

    if (calltype == QJS_FS_CALLBACK) {
        if (argc > 0) {
            callback = argv[njs_min(argc - 1, 3)];
        }

        if (!JS_IsFunction(cx, callback)) {
            JS_ThrowTypeError(cx, "\"callback\" must be a function");
            return JS_EXCEPTION;
        }

        if (JS_SameValue(cx, options, callback)) {
            options = JS_UNDEFINED;
        }
    }

    if (JS_IsString(options)) {
        encode = JS_DupValue(cx, options);

    } else if (!JS_IsUndefined(options)) {
        if (!JS_IsObject(options)) {
            JS_ThrowTypeError(cx, "Unknown options type (a string or object "
                              "required)");
            return JS_EXCEPTION;
        }

        v = JS_GetPropertyStr(cx, options, "flag");
        if (!JS_IsUndefined(v) && !JS_IsException(v)) {
            flags = qjs_fs_flags(cx, v, O_CREAT | O_WRONLY);
            if (flags == -1) {
                JS_FreeValue(cx, v);
                return JS_EXCEPTION;
            }
        }

        v = JS_GetPropertyStr(cx, options, "mode");
        if (!JS_IsUndefined(v) && !JS_IsException(v)) {
            md = qjs_fs_mode(cx, v, 0666);
            if (md == (mode_t) -1) {
                JS_FreeValue(cx, v);
                return JS_EXCEPTION;
            }
        }

        v = JS_GetPropertyStr(cx, options, "encoding");
        if (!JS_IsUndefined(v) && !JS_IsException(v)) {
            encode = v;
        }
    }

    encoding = qjs_buffer_encoding(cx, encode, 1);
    if (encoding == NULL) {
        JS_FreeValue(cx, encode);
        return JS_EXCEPTION;
    }

    JS_FreeValue(cx, encode);

    data = argv[1];
    str.start = NULL;
    want_to_free_content = 0;

    if (JS_IsString(data)) {
        goto decode;
    }

    v = qjs_typed_array_data(cx, data, &content);
    if (JS_IsException(v)) {
decode:

        str.start = (u_char *) JS_ToCStringLen(cx, &str.length, data);
        if (str.start == NULL) {
            return JS_EXCEPTION;
        }

        if (encoding->decode_length != NULL) {
            content.length = encoding->decode_length(cx, &str);
            content.start = js_malloc(cx, content.length);
            if (content.start == NULL) {
                JS_FreeCString(cx, (const char *) str.start);
                JS_ThrowOutOfMemory(cx);
                return JS_EXCEPTION;
            }

            want_to_free_content = 1;

            if (encoding->decode(cx, &str, &content) != 0) {
                JS_FreeCString(cx, (const char *) str.start);
                return JS_EXCEPTION;
            }

        } else {
            content.start = (u_char *) str.start;
            content.length = str.length;
        }
    }

    fd = open(path, flags, md);
    if (fd < 0) {
        result = qjs_fs_error(cx, "open", strerror(errno), path, errno);
        goto done;
    }

    p = content.start;
    end = p + content.length;

    while (p < end) {
        n = write(fd, p, end - p);
        if (n == -1) {
            if (errno == EINTR) {
                continue;
            }

            result = qjs_fs_error(cx, "write", strerror(errno), path, errno);
            goto done;
        }

        p += n;
    }

    result = JS_UNDEFINED;

done:

    if (fd != -1) {
        (void) close(fd);
    }

    if (str.start != NULL) {
        JS_FreeCString(cx, (const char *) str.start);
    }

    if (want_to_free_content) {
        js_free(cx, content.start);
    }

    if (JS_IsException(result)) {
        return JS_EXCEPTION;
    }

    return qjs_fs_result(cx, result, calltype, callback);
}


static JSValue
qjs_fs_unlink(JSContext *cx, JSValueConst this_val, int argc,
    JSValueConst *argv, int calltype)
{
    int         ret;
    JSValue     callback, result;
    const char  *path;
    char        path_buf[NJS_MAX_PATH + 1];

    path = qjs_fs_path(cx, path_buf, argv[0], "path");
    if (path == NULL) {
        return JS_EXCEPTION;
    }

    callback = JS_UNDEFINED;

    if (calltype == QJS_FS_CALLBACK) {
        callback = argv[1];

        if (!JS_IsFunction(cx, callback)) {
            JS_ThrowTypeError(cx, "\"callback\" must be a function");
            return JS_EXCEPTION;
        }
    }

    result = JS_UNDEFINED;

    ret = unlink(path);
    if (ret != 0) {
        result = qjs_fs_error(cx, "unlink", strerror(errno), path, errno);
    }

    if (JS_IsException(result)) {
        return JS_EXCEPTION;
    }

    return qjs_fs_result(cx, result, calltype, callback);
}


static JSValue
qjs_fs_stats_to_string_tag(JSContext *cx, JSValueConst this_val)
{
    return JS_NewString(cx, "Stats");
}


static JSValue
qjs_fs_stats_test(JSContext *cx, JSValueConst this_val, int argc,
    JSValueConst *argv, int testtype)
{
    unsigned    mask;
    qjs_stat_t  *st;

    st = JS_GetOpaque2(cx, this_val, QJS_CORE_CLASS_ID_FS_STATS);
    if (st == NULL) {
        return JS_EXCEPTION;
    }

    switch (testtype) {
    case DT_DIR:
        mask = S_IFDIR;
        break;

    case DT_REG:
        mask = S_IFREG;
        break;

    case DT_CHR:
        mask = S_IFCHR;
        break;

    case DT_LNK:
        mask = S_IFLNK;
        break;

    case DT_BLK:
        mask = S_IFBLK;
        break;

    case DT_FIFO:
        mask = S_IFIFO;
        break;

    case DT_SOCK:
    default:
        mask = S_IFSOCK;
    }

    return JS_NewBool(cx, (st->st_mode & S_IFMT) == mask);
}


static int
qjs_fs_stats_get_own_property(JSContext *cx, JSPropertyDescriptor *pdesc,
    JSValueConst obj, JSAtom prop)
{
    JSValue     value;
    njs_str_t   name;
    qjs_stat_t  *st;

#define qjs_fs_time_ms(ts) ((ts)->tv_sec * 1000.0 + (ts)->tv_nsec / 1000000.0)

    st = JS_GetOpaque2(cx, obj, QJS_CORE_CLASS_ID_FS_STATS);
    if (st == NULL) {
        (void) JS_ThrowInternalError(cx, "\"this\" is not a Stats object");
        return -1;
    }

    name.start = (u_char *) JS_AtomToCString(cx, prop);
    if (name.start == NULL) {
        return -1;
    }

    name.length = strlen((const char *) name.start);

    if (name.length < 3) {
        JS_FreeCString(cx, (const char *) name.start);
        return 0;
    }

    switch (name.start[0]) {
    case 'a':
        if (name.length == 5 && memcmp(name.start, "atime", 5) == 0) {
            value = JS_NewDate(cx, qjs_fs_time_ms(&st->st_atim));
            goto done;
        }

        if (name.length == 7 && memcmp(name.start, "atimeMs", 7) == 0) {
            value = JS_NewFloat64(cx, qjs_fs_time_ms(&st->st_atim));
            goto done;
        }

        break;

    case 'b':
        if (name.length == 6 && memcmp(name.start, "blocks", 6) == 0) {
            value = JS_NewFloat64(cx, st->st_blocks);
            goto done;
        }

        if (name.length == 7 && memcmp(name.start, "blksize", 7) == 0) {
            value = JS_NewFloat64(cx, st->st_blksize);
            goto done;
        }

        if (name.length == 9 && memcmp(name.start, "birthtime", 9) == 0) {
            value = JS_NewDate(cx, qjs_fs_time_ms(&st->st_birthtim));
            goto done;
        }

        if (name.length == 11 && memcmp(name.start, "birthtimeMs", 11) == 0) {
            value = JS_NewFloat64(cx, qjs_fs_time_ms(&st->st_birthtim));
            goto done;
        }

        break;

    case 'c':
        if (name.length == 5 && memcmp(name.start, "ctime", 5) == 0) {
            value = JS_NewDate(cx, qjs_fs_time_ms(&st->st_ctim));
            goto done;
        }

        if (name.length == 7 && memcmp(name.start, "ctimeMs", 7) == 0) {
            value = JS_NewFloat64(cx, qjs_fs_time_ms(&st->st_ctim));
            goto done;
        }

        break;

    case 'd':
        if (name.length == 3 && memcmp(name.start, "dev", 3) == 0) {
            value = JS_NewFloat64(cx, st->st_dev);
            goto done;
        }

        break;

    case 'g':
        if (name.length == 3 && memcmp(name.start, "gid", 3) == 0) {
            value = JS_NewFloat64(cx, st->st_gid);
            goto done;
        }

        break;

    case 'i':
        if (name.length == 3 && memcmp(name.start, "ino", 3) == 0) {
            value = JS_NewFloat64(cx, st->st_ino);
            goto done;
        }

        break;

    case 'm':
        if (name.length == 4 && memcmp(name.start, "mode", 4) == 0) {
            value = JS_NewFloat64(cx, st->st_mode);
            goto done;
        }

        if (name.length == 5 && memcmp(name.start, "mtime", 5) == 0) {
            value = JS_NewDate(cx, qjs_fs_time_ms(&st->st_mtim));
            goto done;
        }

        if (name.length == 7 && memcmp(name.start, "mtimeMs", 7) == 0) {
            value = JS_NewFloat64(cx, qjs_fs_time_ms(&st->st_mtim));
            goto done;
        }

        break;

    case 'n':
        if (name.length == 5 && memcmp(name.start, "nlink", 5) == 0) {
            value = JS_NewFloat64(cx, st->st_nlink);
            goto done;
        }

        break;

    case 'r':
        if (name.length == 4 && memcmp(name.start, "rdev", 4) == 0) {
            value = JS_NewFloat64(cx, st->st_rdev);
            goto done;
        }

        break;

    case 's':
        if (name.length == 4 && memcmp(name.start, "size", 4) == 0) {
            value = JS_NewFloat64(cx, st->st_size);
            goto done;
        }

        break;

    case 'u':
        if (name.length == 3 && memcmp(name.start, "uid", 3) == 0) {
            value = JS_NewFloat64(cx, st->st_uid);
            goto done;
        }

        break;
    }

    JS_FreeCString(cx, (const char *) name.start);

    return 0;

done:

    JS_FreeCString(cx, (const char *) name.start);

    if (pdesc != NULL) {
        pdesc->flags = JS_PROP_ENUMERABLE | JS_PROP_CONFIGURABLE;
        pdesc->getter = JS_UNDEFINED;
        pdesc->setter = JS_UNDEFINED;
        pdesc->value = value;
    }

    return 1;
}


static int
qjs_fs_stats_get_own_property_names(JSContext *cx, JSPropertyEnum **ptab,
    uint32_t *plen, JSValueConst obj)
{
    int       ret;
    JSValue   keys;
    unsigned  i;

    static const char *stat_props[] = {
        "atime",
        "atimeMs",
        "birthtime",
        "birthtimeMs",
        "blksize",
        "blocks",
        "ctime",
        "ctimeMs",
        "dev",
        "gid",
        "ino",
        "mode",
        "mtime",
        "mtimeMs",
        "nlink",
        "size",
        "rdev",
        "uid",
    };

    keys = JS_NewObject(cx);
    if (JS_IsException(keys)) {
        return -1;
    }

    for (i = 0; i < njs_nitems(stat_props); i++) {
        if (JS_DefinePropertyValueStr(cx, keys, stat_props[i],
                                      JS_UNDEFINED, JS_PROP_C_W_E) < 0)
        {
            JS_FreeValue(cx, keys);
            return -1;
        }
    }

    ret = JS_GetOwnPropertyNames(cx, ptab, plen, keys, JS_GPN_STRING_MASK);

    JS_FreeValue(cx, keys);

    return ret;
}


static void
qjs_fs_stats_finalizer(JSRuntime *rt, JSValue val)
{
    qjs_stat_t  *stat;

    stat = JS_GetOpaque(val, QJS_CORE_CLASS_ID_FS_STATS);
    if (stat != NULL) {
        js_free_rt(rt, stat);
    }
}


static JSValue
qjs_fs_dirent_to_string_tag(JSContext *cx, JSValueConst this_val)
{
    return JS_NewString(cx, "Dirent");
}


static JSValue
qjs_fs_dirent_test(JSContext *cx, JSValueConst this_val, int argc,
    JSValueConst *argv, int testtype)
{
    int       value;
    JSValue   type;

    type = JS_GetPropertyStr(cx, this_val, "type");
    if (JS_IsException(type)) {
        return JS_EXCEPTION;
    }

    if (JS_VALUE_GET_TAG(type) != JS_TAG_INT) {
        JS_FreeValue(cx, type);
        return JS_FALSE;
    }

    value = JS_VALUE_GET_INT(type);
    JS_FreeValue(cx, type);

    if (value == QJS_DT_INVALID) {
        JS_ThrowInternalError(cx, "dentry type is not supported on this "
                              "platform");
        return JS_EXCEPTION;
    }

    return JS_NewBool(cx, testtype == value);
}


static JSValue
qjs_fs_filehandle_to_string_tag(JSContext *cx, JSValueConst this_val)
{
    return JS_NewString(cx, "FileHandle");
}


static JSValue
qjs_fs_filehandle_fd(JSContext *cx, JSValueConst thisval)
{
    int  fd;

    fd = (intptr_t) JS_GetOpaque2(cx, thisval, QJS_CORE_CLASS_ID_FS_FILEHANDLE);

    if (fd == -1) {
        return JS_ThrowTypeError(cx, "file was already closed");
    }

    return JS_NewInt32(cx, fd);
}


static JSValue
qjs_fs_filehandle_value_of(JSContext *cx, JSValueConst thisval, int argc,
    JSValueConst *argv)
{
    int  fd;

    fd = (intptr_t) JS_GetOpaque2(cx, thisval, QJS_CORE_CLASS_ID_FS_FILEHANDLE);

    if (fd == -1) {
        return JS_ThrowTypeError(cx, "file was already closed");
    }

    return JS_NewInt32(cx, fd);
}


static void
qjs_fs_filehandle_finalizer(JSRuntime *rt, JSValue val)
{
    int   fd;

    fd = (intptr_t) JS_GetOpaque(val, QJS_CORE_CLASS_ID_FS_FILEHANDLE);

    (void) close(fd);
}


static JSValue
qjs_fs_promise_trampoline(JSContext *cx, int argc, JSValueConst *argv)
{
    return JS_Call(cx, argv[0], JS_UNDEFINED, 1, &argv[1]);
}


static JSValue
qjs_fs_result(JSContext *cx, JSValue result, int calltype, JSValue callback)
{
    JS_BOOL  is_error;
    JSValue  promise, callbacks[2], arguments[2];

    switch (calltype) {
    case QJS_FS_DIRECT:
        if (JS_IsError(cx, result)) {
            JS_Throw(cx, result);
            return JS_EXCEPTION;
        }

        return result;

    case QJS_FS_PROMISE:
        promise = JS_NewPromiseCapability(cx, callbacks);
        if (JS_IsException(promise)) {
            JS_FreeValue(cx, result);
            return JS_EXCEPTION;
        }

        is_error = !!JS_IsError(cx, result);

        arguments[0] = callbacks[is_error];
        arguments[1] = result;
        JS_FreeValue(cx, callbacks[!is_error]);

        if (JS_EnqueueJob(cx, qjs_fs_promise_trampoline, 2, arguments) < 0) {
            JS_FreeValue(cx, promise);
            JS_FreeValue(cx, callbacks[is_error]);
            JS_FreeValue(cx, result);
            return JS_EXCEPTION;
        }

        JS_FreeValue(cx, arguments[0]);
        JS_FreeValue(cx, arguments[1]);

        return promise;

    case QJS_FS_CALLBACK:
        if (JS_IsError(cx, result)) {
            arguments[0] = result;
            arguments[1] = JS_UNDEFINED;

        } else {
            arguments[0] = JS_UNDEFINED;
            arguments[1] = result;
        }

        promise = JS_Call(cx, callback, JS_UNDEFINED, 2, arguments);
        JS_FreeValue(cx, arguments[0]);
        JS_FreeValue(cx, arguments[1]);

        if (JS_IsException(promise)) {
            return JS_EXCEPTION;
        }

        return JS_UNDEFINED;

    default:
        return JS_ThrowInternalError(cx, "unexpected calltype %d", calltype);
    }
}


static int
qjs_fs_flags(JSContext *cx, JSValue value, int default_flags)
{
    JSValue          ret;
    njs_str_t       flags;
    qjs_fs_entry_t  *fl;

    if (JS_IsUndefined(value)) {
        return default_flags;
    }

    ret = JS_ToString(cx, value);
    if (JS_IsException(ret)) {
        return -1;
    }

    flags.start = (u_char *) JS_ToCStringLen(cx, &flags.length, ret);
    JS_FreeValue(cx, ret);
    if (flags.start == NULL) {
        return -1;
    }

    for (fl = &qjs_flags_table[0]; fl->name.length != 0; fl++) {
        if (njs_strstr_eq(&flags, &fl->name)) {
            JS_FreeCString(cx, (const char *) flags.start);
            return fl->value;
        }
    }

    JS_ThrowTypeError(cx, "Unknown file open flags: \"%s\"", flags.start);

    JS_FreeCString(cx, (const char *) flags.start);

    return -1;
}


static mode_t
qjs_fs_mode(JSContext *cx, JSValue value, mode_t default_mode)
{
    int64_t  i64;

    /* GCC complains about uninitialized i64. */
    i64 = 0;

    if (JS_IsUndefined(value)) {
        return default_mode;
    }

    if (JS_ToInt64(cx, &i64, value) < 0) {
        return (mode_t) -1;
    }

    return (mode_t) i64;
}


static JSValue
qjs_fs_error(JSContext *cx, const char *syscall, const char *description,
    const char *path, int errn)
{
    JSValue  value;

    value = JS_NewError(cx);
    if (JS_IsException(value)) {
        return JS_EXCEPTION;
    }

    if (JS_SetPropertyStr(cx, value, "message",
                          JS_NewString(cx, description)) < 0)
    {
        JS_FreeValue(cx, value);
        return JS_EXCEPTION;
    }

    if (errn != 0) {
        if (JS_SetPropertyStr(cx, value, "errno", JS_NewInt32(cx, errn)) < 0) {
            JS_FreeValue(cx, value);
            return JS_EXCEPTION;
        }

        if (JS_SetPropertyStr(cx, value, "code",
                              JS_NewString(cx, njs_errno_string(errn))) < 0)
        {
            JS_FreeValue(cx, value);
            return JS_EXCEPTION;
        }
    }

    if (path != NULL) {
        if (JS_SetPropertyStr(cx, value, "path",
                              JS_NewString(cx, path)) < 0)
        {
            JS_FreeValue(cx, value);
            return JS_EXCEPTION;
        }
    }

    if (syscall != NULL) {
        if (JS_SetPropertyStr(cx, value, "syscall",
                              JS_NewString(cx, syscall)) < 0)
        {
            JS_FreeValue(cx, value);
            return JS_EXCEPTION;
        }
    }

    return value;
}


static JSValue
qjs_fs_encode(JSContext *cx, const qjs_buffer_encoding_t *encoding,
    njs_str_t *str)
{
    JSValue    ret;
    njs_str_t  data;

    if (encoding == NULL) {
        return qjs_buffer_create(cx, str->start, str->length);

    } else if (encoding->encode_length != NULL) {
        data.length = encoding->encode_length(cx, str);

        data.start = js_malloc(cx, data.length);
        if (data.start == NULL) {
            JS_ThrowOutOfMemory(cx);
            return JS_EXCEPTION;
        }

        if (encoding->encode(cx, str, &data) != 0) {
            js_free(cx, data.start);
            return JS_EXCEPTION;
        }

        ret = JS_NewStringLen(cx, (const char *) data.start, data.length);
        js_free(cx, data.start);

        return ret;
    }

    return JS_NewStringLen(cx, (const char *) str->start, str->length);
}


static char *
qjs_fs_path(JSContext *cx, char storage[NJS_MAX_PATH + 1], JSValue src,
    const char *prop_name)
{
    u_char       *p;
    JSValue      val;
    qjs_bytes_t  bytes;

    if (!JS_IsString(src)) {
        val = JS_GetTypedArrayBuffer(cx, src, NULL, NULL, NULL);
        if (JS_IsException(val)) {
            JS_ThrowTypeError(cx, "\"%s\" must be a string or Buffer",
                              prop_name);
            return NULL;
        }

        JS_FreeValue(cx, val);
    }

    if (qjs_to_bytes(cx, &bytes, src) != 0) {
        return NULL;
    }

    if (bytes.length > NJS_MAX_PATH - 1) {
        qjs_bytes_free(cx, &bytes);
        JS_ThrowRangeError(cx, "\"%s\" is too long >= %d", prop_name,
                                  NJS_MAX_PATH);
        return NULL;
    }

    if (memchr(bytes.start, '\0', bytes.length) != 0) {
        qjs_bytes_free(cx, &bytes);
        JS_ThrowTypeError(cx, "\"%s\" must be a Buffer without null bytes",
                          prop_name);
        return NULL;
    }

    p = njs_cpymem(storage, bytes.start, bytes.length);
    *p++ = '\0';

    qjs_bytes_free(cx, &bytes);

    return storage;
}


static int
qjs_fs_module_init(JSContext *cx, JSModuleDef *m)
{
    int      rc;
    JSValue  proto;

    proto = JS_NewObject(cx);
    JS_SetPropertyFunctionList(cx, proto, qjs_fs_export,
                               njs_nitems(qjs_fs_export));

    rc = JS_SetModuleExport(cx, m, "default", proto);
    if (rc != 0) {
        return -1;
    }

    return JS_SetModuleExportList(cx, m, qjs_fs_export,
                                  njs_nitems(qjs_fs_export));
}


static JSModuleDef *
qjs_fs_init(JSContext *cx, const char *name)
{
    int          rc;
    JSValue      proto;
    JSModuleDef  *m;

    if (!JS_IsRegisteredClass(JS_GetRuntime(cx),
                              QJS_CORE_CLASS_ID_FS_STATS))
    {
        if (JS_NewClass(JS_GetRuntime(cx), QJS_CORE_CLASS_ID_FS_STATS,
                        &qjs_fs_stats_class) < 0)
        {
            return NULL;
        }

        proto = JS_NewObject(cx);
        if (JS_IsException(proto)) {
            return NULL;
        }

        JS_SetPropertyFunctionList(cx, proto, qjs_fs_stats_proto,
                                   njs_nitems(qjs_fs_stats_proto));

        JS_SetClassProto(cx, QJS_CORE_CLASS_ID_FS_STATS, proto);

        proto = JS_NewObject(cx);
        if (JS_IsException(proto)) {
            return NULL;
        }

        JS_SetPropertyFunctionList(cx, proto, qjs_fs_dirent_proto,
                                   njs_nitems(qjs_fs_dirent_proto));

        JS_SetClassProto(cx, QJS_CORE_CLASS_ID_FS_DIRENT, proto);

        if (JS_NewClass(JS_GetRuntime(cx), QJS_CORE_CLASS_ID_FS_FILEHANDLE,
                        &qjs_fs_filehandle_class) < 0)
        {
            return NULL;
        }

        proto = JS_NewObject(cx);
        if (JS_IsException(proto)) {
            return NULL;
        }

        JS_SetPropertyFunctionList(cx, proto, qjs_fs_filehandle_proto,
                                   njs_nitems(qjs_fs_filehandle_proto));

        JS_SetClassProto(cx, QJS_CORE_CLASS_ID_FS_FILEHANDLE, proto);
    }

    m = JS_NewCModule(cx, name, qjs_fs_module_init);
    if (m == NULL) {
        return NULL;
    }

    JS_AddModuleExport(cx, m, "default");
    rc = JS_AddModuleExportList(cx, m, qjs_fs_export,
                                njs_nitems(qjs_fs_export));
    if (rc != 0) {
        return NULL;
    }

    return m;
}
