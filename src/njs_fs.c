
/*
 * Copyright (C) Dmitry Volyntsev
 * Copyright (C) NGINX, Inc.
 */


#include <njs_main.h>

#include <dirent.h>

#if (NJS_SOLARIS)

#define DT_DIR         0
#define DT_REG         1
#define DT_CHR         2
#define DT_LNK         3
#define DT_BLK         4
#define DT_FIFO        5
#define DT_SOCK        6
#define NJS_DT_INVALID 0xffffffff

#define njs_dentry_type(_dentry)                                             \
    (NJS_DT_INVALID)

#else

#define NJS_DT_INVALID 0xffffffff

#define njs_dentry_type(_dentry)                                             \
    ((_dentry)->d_type)

#endif


#define njs_fs_magic(calltype, mode)                                         \
    (((mode) << 2) | calltype)


typedef enum {
    NJS_FS_DIRECT,
    NJS_FS_PROMISE,
    NJS_FS_CALLBACK,
} njs_fs_calltype_t;


typedef enum {
    NJS_FS_TRUNC,
    NJS_FS_APPEND,
} njs_fs_writemode_t;


typedef enum {
    NJS_FS_STAT,
    NJS_FS_LSTAT,
} njs_fs_statmode_t;


typedef struct {
    njs_str_t       name;
    int             value;
} njs_fs_entry_t;


typedef enum {
    NJS_FTW_PHYS = 1,
    NJS_FTW_MOUNT = 2,
    NJS_FTW_DEPTH = 8,
} njs_ftw_flags_t;


typedef enum {
    NJS_FTW_F,
    NJS_FTW_D,
    NJS_FTW_DNR,
    NJS_FTW_NS,
    NJS_FTW_SL,
    NJS_FTW_DP,
    NJS_FTW_SLN,
} njs_ftw_type_t;


typedef struct {
    long tv_sec;
    long tv_nsec;
} njs_timespec_t;


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
    njs_timespec_t  st_atim;
    njs_timespec_t  st_mtim;
    njs_timespec_t  st_ctim;
    njs_timespec_t  st_birthtim;
} njs_stat_t;


typedef enum {
    NJS_FS_STAT_DEV,
    NJS_FS_STAT_INO,
    NJS_FS_STAT_MODE,
    NJS_FS_STAT_NLINK,
    NJS_FS_STAT_UID,
    NJS_FS_STAT_GID,
    NJS_FS_STAT_RDEV,
    NJS_FS_STAT_SIZE,
    NJS_FS_STAT_BLKSIZE,
    NJS_FS_STAT_BLOCKS,
    NJS_FS_STAT_ATIME,
    NJS_FS_STAT_BIRTHTIME,
    NJS_FS_STAT_CTIME,
    NJS_FS_STAT_MTIME,
} njs_stat_prop_t;


typedef njs_int_t (*njs_file_tree_walk_cb_t)(const char *, const struct stat *,
     njs_ftw_type_t);


static njs_int_t njs_fs_fd_read(njs_vm_t *vm, int fd, njs_str_t *data);

static njs_int_t njs_fs_error(njs_vm_t *vm, const char *syscall,
    const char *desc, const char *path, int errn, njs_value_t *retval);
static njs_int_t njs_fs_result(njs_vm_t *vm, njs_value_t *result,
    njs_index_t calltype, const njs_value_t* callback, njs_uint_t nargs);

static njs_int_t njs_file_tree_walk(const char *path,
    njs_file_tree_walk_cb_t cb, int fd_limit, njs_ftw_flags_t flags);

static njs_int_t njs_fs_make_path(njs_vm_t *vm, char *path, mode_t md,
    njs_bool_t recursive, njs_value_t *retval);
static njs_int_t njs_fs_rmtree(njs_vm_t *vm, const char *path,
    njs_bool_t recursive, njs_value_t *retval);

static const char *njs_fs_path(njs_vm_t *vm, char storage[NJS_MAX_PATH + 1],
    const njs_value_t *src, const char *prop_name);
static int njs_fs_flags(njs_vm_t *vm, njs_value_t *value, int default_flags);
static mode_t njs_fs_mode(njs_vm_t *vm, njs_value_t *value,
    mode_t default_mode);

static njs_int_t njs_fs_add_event(njs_vm_t *vm, const njs_value_t *callback,
    const njs_value_t *args, njs_uint_t nargs);

static njs_int_t njs_fs_dirent_create(njs_vm_t *vm, njs_value_t *name,
    njs_value_t *type, njs_value_t *retval);

static njs_int_t njs_fs_stats_create(njs_vm_t *vm, struct stat *st,
    njs_value_t *retval);

static njs_fs_entry_t njs_flags_table[] = {
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


static const njs_value_t  string_flag = njs_string("flag");
static const njs_value_t  string_mode = njs_string("mode");
static const njs_value_t  string_buffer = njs_string("buffer");
static const njs_value_t  string_encoding = njs_string("encoding");
static const njs_value_t  string_recursive = njs_string("recursive");


static njs_int_t
njs_fs_read_file(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t calltype)
{
    int                          fd, flags;
    njs_str_t                    data;
    njs_int_t                    ret;
    const char                   *path;
    njs_value_t                  flag, encode, retval, *callback, *options;
    struct stat                  sb;
    const njs_buffer_encoding_t  *encoding;
    char                         path_buf[NJS_MAX_PATH + 1];

    path = njs_fs_path(vm, path_buf, njs_arg(args, nargs, 1), "path");
    if (njs_slow_path(path == NULL)) {
        return NJS_ERROR;
    }

    callback = NULL;

    options = njs_arg(args, nargs, 2);

    if (calltype == NJS_FS_CALLBACK) {
        callback = njs_arg(args, nargs, njs_min(nargs - 1, 3));
        if (!njs_is_function(callback)) {
            njs_type_error(vm, "\"callback\" must be a function");
            return NJS_ERROR;
        }

        if (options == callback) {
            options = njs_value_arg(&njs_value_undefined);
        }
    }

    njs_set_undefined(&flag);
    njs_set_undefined(&encode);

    switch (options->type) {
    case NJS_STRING:
        encode = *options;
        break;

    case NJS_UNDEFINED:
        break;

    default:
        if (!njs_is_object(options)) {
            njs_type_error(vm, "Unknown options type: \"%s\" "
                           "(a string or object required)",
                           njs_type_string(options->type));
            return NJS_ERROR;
        }

        ret = njs_value_property(vm, options, njs_value_arg(&string_flag),
                                 &flag);
        if (njs_slow_path(ret == NJS_ERROR)) {
            return ret;
        }

        ret = njs_value_property(vm, options, njs_value_arg(&string_encoding),
                                 &encode);
        if (njs_slow_path(ret == NJS_ERROR)) {
            return ret;
        }
    }

    flags = njs_fs_flags(vm, &flag, O_RDONLY);
    if (njs_slow_path(flags == -1)) {
        return NJS_ERROR;
    }

    encoding = NULL;
    if (njs_is_defined(&encode)) {
        encoding = njs_buffer_encoding(vm, &encode);
        if (njs_slow_path(encoding == NULL)) {
            return NJS_ERROR;
        }
    }

    fd = open(path, flags);
    if (njs_slow_path(fd < 0)) {
        ret = njs_fs_error(vm, "open", strerror(errno), path, errno, &retval);
        goto done;
    }

    ret = fstat(fd, &sb);
    if (njs_slow_path(ret == -1)) {
        ret = njs_fs_error(vm, "stat", strerror(errno), path, errno, &retval);
        goto done;
    }

    if (njs_slow_path(!S_ISREG(sb.st_mode))) {
        ret = njs_fs_error(vm, "stat", "File is not regular", path, 0, &retval);
        goto done;
    }

    data.start = NULL;
    data.length = sb.st_size;

    ret = njs_fs_fd_read(vm, fd, &data);
    if (njs_slow_path(ret != NJS_OK)) {
        if (ret == NJS_DECLINED) {
            ret = njs_fs_error(vm, "read", strerror(errno), path, errno,
                               &retval);
        }

        goto done;
    }

    if (encoding == NULL) {
        ret = njs_buffer_set(vm, &retval, data.start, data.length);

    } else {
        ret = encoding->encode(vm, &retval, &data);
        njs_mp_free(vm->mem_pool, data.start);
    }

done:

    if (fd != -1) {
        (void) close(fd);
    }

    if (ret == NJS_OK) {
        return njs_fs_result(vm, &retval, calltype, callback, 2);
    }

    return NJS_ERROR;
}


static njs_int_t
njs_fs_write_file(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t magic)
{
    int                          fd, flags;
    u_char                       *p, *end;
    mode_t                       md;
    ssize_t                      n;
    njs_str_t                    content;
    njs_int_t                    ret;
    const char                   *path;
    njs_value_t                  flag, mode, encode, retval, *data, *callback,
                                 *options;
    njs_typed_array_t            *array;
    njs_fs_calltype_t            calltype;
    njs_array_buffer_t           *buffer;
    const njs_buffer_encoding_t  *encoding;
    char                         path_buf[NJS_MAX_PATH + 1];

    path = njs_fs_path(vm, path_buf, njs_arg(args, nargs, 1), "path");
    if (njs_slow_path(path == NULL)) {
        return NJS_ERROR;
    }

    callback = NULL;
    calltype = magic & 3;
    options = njs_arg(args, nargs, 3);

    if (calltype == NJS_FS_CALLBACK) {
        callback = njs_arg(args, nargs, njs_min(nargs - 1, 4));
        if (!njs_is_function(callback)) {
            njs_type_error(vm, "\"callback\" must be a function");
            return NJS_ERROR;
        }

        if (options == callback) {
            options = njs_value_arg(&njs_value_undefined);
        }
    }

    njs_set_undefined(&flag);
    njs_set_undefined(&mode);
    njs_set_undefined(&encode);

    switch (options->type) {
    case NJS_STRING:
        encode = *options;
        break;

    case NJS_UNDEFINED:
        break;

    default:
        if (!njs_is_object(options)) {
            njs_type_error(vm, "Unknown options type: \"%s\" "
                           "(a string or object required)",
                           njs_type_string(options->type));
            return NJS_ERROR;
        }

        ret = njs_value_property(vm, options, njs_value_arg(&string_flag),
                                 &flag);
        if (njs_slow_path(ret == NJS_ERROR)) {
            return ret;
        }

        ret = njs_value_property(vm, options, njs_value_arg(&string_mode),
                                 &mode);
        if (njs_slow_path(ret == NJS_ERROR)) {
            return ret;
        }

        ret = njs_value_property(vm, options, njs_value_arg(&string_encoding),
                                 &encode);
        if (njs_slow_path(ret == NJS_ERROR)) {
            return ret;
        }
    }

    data = njs_arg(args, nargs, 2);

    switch (data->type) {
    case NJS_TYPED_ARRAY:
    case NJS_DATA_VIEW:
        array = njs_typed_array(data);
        buffer = array->buffer;
        if (njs_slow_path(njs_is_detached_buffer(buffer))) {
            njs_type_error(vm, "detached buffer");
            return NJS_ERROR;
        }

        content.start = &buffer->u.u8[array->offset];
        content.length = array->byte_length;
        break;

    case NJS_STRING:
    default:
        encoding = njs_buffer_encoding(vm, &encode);
        if (njs_slow_path(encoding == NULL)) {
            return NJS_ERROR;
        }

        ret = njs_value_to_string(vm, &retval, data);
        if (njs_slow_path(ret != NJS_OK)) {
            return NJS_ERROR;
        }

        ret = njs_buffer_decode_string(vm, &retval, &retval, encoding);
        if (njs_slow_path(ret != NJS_OK)) {
            return NJS_ERROR;
        }

        njs_string_get(&retval, &content);
        break;
    }

    flags = njs_fs_flags(vm, &flag, O_CREAT | O_WRONLY);
    if (njs_slow_path(flags == -1)) {
        return NJS_ERROR;
    }

    flags |= ((magic >> 2) == NJS_FS_APPEND) ? O_APPEND : O_TRUNC;

    md = njs_fs_mode(vm, &mode, 0666);
    if (njs_slow_path(md == (mode_t) -1)) {
        return NJS_ERROR;
    }

    fd = open(path, flags, md);
    if (njs_slow_path(fd < 0)) {
        ret = njs_fs_error(vm, "open", strerror(errno), path, errno, &retval);
        goto done;
    }

    p = content.start;
    end = p + content.length;

    while (p < end) {
        n = write(fd, p, end - p);
        if (njs_slow_path(n == -1)) {
            if (errno == EINTR) {
                continue;
            }

            ret = njs_fs_error(vm, "write", strerror(errno), path, errno,
                               &retval);
            goto done;
        }

        p += n;
    }

    ret = NJS_OK;
    njs_set_undefined(&retval);

done:

    if (fd != -1) {
        (void) close(fd);
    }

    if (ret == NJS_OK) {
        return njs_fs_result(vm, &retval, calltype, callback, 1);
    }

    return NJS_ERROR;
}


static njs_int_t
njs_fs_rename(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t calltype)
{
    njs_int_t    ret;
    const char   *path, *newpath;
    njs_value_t  retval, *callback;
    char         path_buf[NJS_MAX_PATH + 1], newpath_buf[NJS_MAX_PATH + 1];

    callback = NULL;

    if (calltype == NJS_FS_CALLBACK) {
        callback = njs_arg(args, nargs, 3);
        if (!njs_is_function(callback)) {
            njs_type_error(vm, "\"callback\" must be a function");
            return NJS_ERROR;
        }
    }

    path = njs_fs_path(vm, path_buf, njs_arg(args, nargs, 1), "oldPath");
    if (njs_slow_path(path == NULL)) {
        return NJS_ERROR;
    }

    newpath = njs_fs_path(vm, newpath_buf, njs_arg(args, nargs, 2), "newPath");
    if (njs_slow_path(newpath == NULL)) {
        return NJS_ERROR;
    }

    njs_set_undefined(&retval);

    ret = rename(path, newpath);
    if (njs_slow_path(ret != 0)) {
        ret = njs_fs_error(vm, "rename", strerror(errno), NULL, errno, &retval);
    }

    if (ret == NJS_OK) {
        return njs_fs_result(vm, &retval, calltype, callback, 1);
    }

    return NJS_ERROR;
}


static njs_int_t
njs_fs_access(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t calltype)
{
    int          md;
    njs_int_t    ret;
    const char  *path;
    njs_value_t  retval, *callback, *mode;
    char         path_buf[NJS_MAX_PATH + 1];

    path = njs_fs_path(vm, path_buf, njs_arg(args, nargs, 1), "path");
    if (njs_slow_path(path == NULL)) {
        return NJS_ERROR;
    }

    callback = NULL;
    mode = njs_arg(args, nargs, 2);

    if (calltype == NJS_FS_CALLBACK) {
        callback = njs_arg(args, nargs, njs_min(nargs - 1, 3));
        if (!njs_is_function(callback)) {
            njs_type_error(vm, "\"callback\" must be a function");
            return NJS_ERROR;
        }

        if (mode == callback) {
            mode = njs_value_arg(&njs_value_undefined);
        }
    }

    switch (mode->type) {
    case NJS_UNDEFINED:
        md = F_OK;
        break;

    case NJS_NUMBER:
        md = njs_number(mode);
        break;

    default:
        njs_type_error(vm, "\"mode\" must be a number");
        return NJS_ERROR;
    }

    njs_set_undefined(&retval);

    ret = access(path, md);
    if (njs_slow_path(ret != 0)) {
        ret = njs_fs_error(vm, "access", strerror(errno), path, errno, &retval);
    }

    if (ret == NJS_OK) {
        return njs_fs_result(vm, &retval, calltype, callback, 1);
    }

    return NJS_ERROR;
}


static njs_int_t
njs_fs_symlink(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t calltype)
{
    njs_int_t    ret;
    const char  *target, *path;
    njs_value_t  retval, *callback, *type;
    char         target_buf[NJS_MAX_PATH + 1], path_buf[NJS_MAX_PATH + 1];

    target = njs_fs_path(vm, target_buf, njs_arg(args, nargs, 1), "target");
    if (njs_slow_path(target == NULL)) {
        return NJS_ERROR;
    }

    path = njs_fs_path(vm, path_buf, njs_arg(args, nargs, 2), "path");
    if (njs_slow_path(path == NULL)) {
        return NJS_ERROR;
    }

    callback = NULL;
    type = njs_arg(args, nargs, 3);

    if (calltype == NJS_FS_CALLBACK) {
        callback = njs_arg(args, nargs, njs_min(nargs - 1, 4));
        if (!njs_is_function(callback)) {
            njs_type_error(vm, "\"callback\" must be a function");
            return NJS_ERROR;
        }

        if (type == callback) {
            type = njs_value_arg(&njs_value_undefined);
        }
    }

    if (njs_slow_path(!njs_is_undefined(type) && !njs_is_string(type))) {
        njs_type_error(vm, "\"type\" must be a string");
        return NJS_ERROR;
    }

    njs_set_undefined(&retval);

    ret = symlink(target, path);
    if (njs_slow_path(ret != 0)) {
        ret = njs_fs_error(vm, "symlink", strerror(errno), path, errno,
                           &retval);
    }

    if (ret == NJS_OK) {
        return njs_fs_result(vm, &retval, calltype, callback, 1);
    }

    return NJS_ERROR;
}


static njs_int_t
njs_fs_unlink(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t calltype)
{
    njs_int_t    ret;
    const char   *path;
    njs_value_t  retval, *callback;
    char         path_buf[NJS_MAX_PATH + 1];

    path = njs_fs_path(vm, path_buf, njs_arg(args, nargs, 1), "path");
    if (njs_slow_path(path == NULL)) {
        return NJS_ERROR;
    }

    callback = NULL;

    if (calltype == NJS_FS_CALLBACK) {
        callback = njs_arg(args, nargs, 2);
        if (!njs_is_function(callback)) {
            njs_type_error(vm, "\"callback\" must be a function");
            return NJS_ERROR;
        }
    }

    njs_set_undefined(&retval);

    ret = unlink(path);
    if (njs_slow_path(ret != 0)) {
        ret = njs_fs_error(vm, "unlink", strerror(errno), path, errno, &retval);
    }

    if (ret == NJS_OK) {
        return njs_fs_result(vm, &retval, calltype, callback, 1);
    }

    return NJS_ERROR;
}


static njs_int_t
njs_fs_realpath(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t calltype)
{
    njs_int_t                    ret;
    njs_str_t                    s;
    const char                   *path;
    njs_value_t                  encode, retval, *callback, *options;
    const njs_buffer_encoding_t  *encoding;
    char                         path_buf[NJS_MAX_PATH + 1],
                                 dst_buf[NJS_MAX_PATH + 1];

    path = njs_fs_path(vm, path_buf, njs_arg(args, nargs, 1), "path");
    if (njs_slow_path(path == NULL)) {
        return NJS_ERROR;
    }

    callback = NULL;
    options = njs_arg(args, nargs, 2);

    if (calltype == NJS_FS_CALLBACK) {
        callback = njs_arg(args, nargs, njs_min(nargs - 1, 3));
        if (!njs_is_function(callback)) {
            njs_type_error(vm, "\"callback\" must be a function");
            return NJS_ERROR;
        }

        if (options == callback) {
            options = njs_value_arg(&njs_value_undefined);
        }
    }

    njs_set_undefined(&encode);

    switch (options->type) {
    case NJS_STRING:
        encode = *options;
        break;

    case NJS_UNDEFINED:
        break;

    default:
        if (!njs_is_object(options)) {
            njs_type_error(vm, "Unknown options type: \"%s\" "
                           "(a string or object required)",
                           njs_type_string(options->type));
            return NJS_ERROR;
        }

        ret = njs_value_property(vm, options, njs_value_arg(&string_encoding),
                                 &encode);
        if (njs_slow_path(ret == NJS_ERROR)) {
            return ret;
        }
    }

    encoding = NULL;
    if (!njs_is_string(&encode) || !njs_string_eq(&encode, &string_buffer)) {
        encoding = njs_buffer_encoding(vm, &encode);
        if (njs_slow_path(encoding == NULL)) {
            return NJS_ERROR;
        }
    }

    s.start = (u_char *) realpath(path, dst_buf);
    if (njs_slow_path(s.start == NULL)) {
        ret = njs_fs_error(vm, "realpath", strerror(errno), path, errno,
                           &retval);
        goto done;
    }

    s.length = njs_strlen(s.start);

    if (encoding == NULL) {
        ret = njs_buffer_new(vm, &retval, s.start, s.length);

    } else {
        ret = encoding->encode(vm, &retval, &s);
    }

done:

    if (ret == NJS_OK) {
        return njs_fs_result(vm, &retval, calltype, callback, 2);
    }

    return NJS_ERROR;
}


static njs_int_t
njs_fs_mkdir(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t calltype)
{
    char         *path;
    mode_t       md;
    njs_int_t    ret;
    njs_value_t  mode, recursive, retval, *callback, *options;
    char         path_buf[NJS_MAX_PATH + 1];

    path = (char *) njs_fs_path(vm, path_buf, njs_arg(args, nargs, 1), "path");
    if (njs_slow_path(path == NULL)) {
        return NJS_ERROR;
    }

    callback = NULL;
    options = njs_arg(args, nargs, 2);

    if (njs_slow_path(calltype == NJS_FS_CALLBACK)) {
        callback = njs_arg(args, nargs, njs_min(nargs - 1, 3));
        if (!njs_is_function(callback)) {
            njs_type_error(vm, "\"callback\" must be a function");
            return NJS_ERROR;
        }
        if (options == callback) {
            options = njs_value_arg(&njs_value_undefined);
        }
    }

    njs_set_undefined(&mode);
    njs_set_false(&recursive);

    switch (options->type) {
    case NJS_NUMBER:
        mode = *options;
        break;

    case NJS_UNDEFINED:
        break;

    default:
        if (!njs_is_object(options)) {
            njs_type_error(vm, "Unknown options type: \"%s\" "
                           "(a number or object required)",
                           njs_type_string(options->type));
            return NJS_ERROR;
        }

        ret = njs_value_property(vm, options, njs_value_arg(&string_mode),
                                 &mode);
        if (njs_slow_path(ret == NJS_ERROR)) {
            return ret;
        }

        ret = njs_value_property(vm, options, njs_value_arg(&string_recursive),
                                 &recursive);
        if (njs_slow_path(ret == NJS_ERROR)) {
            return ret;
        }
    }

    md = njs_fs_mode(vm, &mode, 0777);
    if (njs_slow_path(md == (mode_t) -1)) {
        return NJS_ERROR;
    }

    ret = njs_fs_make_path(vm, path, md, njs_is_true(&recursive), &retval);

    if (ret == NJS_OK) {
        return njs_fs_result(vm, &retval, calltype, callback, 1);
    }

    return NJS_ERROR;
}


static njs_int_t
njs_fs_rmdir(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t calltype)
{
    njs_int_t    ret;
    const char   *path;
    njs_value_t  recursive, retval, *callback, *options;
    char         path_buf[NJS_MAX_PATH + 1];

    path = njs_fs_path(vm, path_buf, njs_arg(args, nargs, 1), "path");
    if (njs_slow_path(path == NULL)) {
        return NJS_ERROR;
    }

    callback = NULL;
    options = njs_arg(args, nargs, 2);

    if (njs_slow_path(calltype == NJS_FS_CALLBACK)) {
        callback = njs_arg(args, nargs, njs_min(nargs - 1, 3));
        if (!njs_is_function(callback)) {
            njs_type_error(vm, "\"callback\" must be a function");
            return NJS_ERROR;
        }
        if (options == callback) {
            options = njs_value_arg(&njs_value_undefined);
        }
    }

    njs_set_false(&recursive);

    switch (options->type) {
    case NJS_UNDEFINED:
        break;

    default:
        if (!njs_is_object(options)) {
            njs_type_error(vm, "Unknown options type: \"%s\" "
                           "(an object required)",
                           njs_type_string(options->type));
            return NJS_ERROR;
        }

        ret = njs_value_property(vm, options, njs_value_arg(&string_recursive),
                                 &recursive);
        if (njs_slow_path(ret == NJS_ERROR)) {
            return ret;
        }
    }

    ret = njs_fs_rmtree(vm, path, njs_is_true(&recursive), &retval);

    if (ret == NJS_OK) {
        return njs_fs_result(vm, &retval, calltype, callback, 1);
    }

    return NJS_ERROR;
}


static njs_int_t
njs_fs_readdir(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t calltype)
{
    DIR                          *dir;
    njs_str_t                    s;
    njs_int_t                    ret;
    const char                   *path;
    njs_value_t                  encode, types, ename, etype, retval,
                                 *callback, *options, *value;
    njs_array_t                  *results;
    struct dirent                *entry;
    const njs_buffer_encoding_t  *encoding;
    char                         path_buf[NJS_MAX_PATH + 1];

    static const njs_value_t  string_types = njs_string("withFileTypes");

    path = njs_fs_path(vm, path_buf, njs_arg(args, nargs, 1), "path");
    if (njs_slow_path(path == NULL)) {
        return NJS_ERROR;
    }

    callback = NULL;
    options = njs_arg(args, nargs, 2);

    if (njs_slow_path(calltype == NJS_FS_CALLBACK)) {
        callback = njs_arg(args, nargs, njs_min(nargs - 1, 3));
        if (!njs_is_function(callback)) {
            njs_type_error(vm, "\"callback\" must be a function");
            return NJS_ERROR;
        }
        if (options == callback) {
            options = njs_value_arg(&njs_value_undefined);
        }
    }

    njs_set_false(&types);
    njs_set_undefined(&encode);

    switch (options->type) {
    case NJS_STRING:
        encode = *options;
        break;

    case NJS_UNDEFINED:
        break;

    default:
        if (!njs_is_object(options)) {
            njs_type_error(vm, "Unknown options type: \"%s\" "
                           "(a string or object required)",
                           njs_type_string(options->type));
            return NJS_ERROR;
        }

        ret = njs_value_property(vm, options, njs_value_arg(&string_encoding),
                                 &encode);
        if (njs_slow_path(ret == NJS_ERROR)) {
            return ret;
        }

        ret = njs_value_property(vm, options, njs_value_arg(&string_types),
                                 &types);
        if (njs_slow_path(ret == NJS_ERROR)) {
            return ret;
        }
    }

    encoding = NULL;
    if (!njs_is_string(&encode) || !njs_string_eq(&encode, &string_buffer)) {
        encoding = njs_buffer_encoding(vm, &encode);
        if (njs_slow_path(encoding == NULL)) {
            return NJS_ERROR;
        }
    }

    results = njs_array_alloc(vm, 1, 0, NJS_ARRAY_SPARE);
    if (njs_slow_path(results == NULL)) {
        return NJS_ERROR;
    }

    njs_set_array(&retval, results);

    dir = opendir(path);
    if (njs_slow_path(dir == NULL)) {
        ret = njs_fs_error(vm, "opendir", strerror(errno), path, errno,
                           &retval);
        goto done;
    }

    ret = NJS_OK;

    for ( ;; ) {
        errno = 0;
        entry = readdir(dir);
        if (njs_slow_path(entry == NULL)) {
            if (errno != 0) {
                ret = njs_fs_error(vm, "readdir", strerror(errno), path, errno,
                                   &retval);
            }

            goto done;
        }

        s.start = (u_char *) entry->d_name;
        s.length = njs_strlen(s.start);

        if ((s.length == 1 && s.start[0] == '.')
            || (s.length == 2 && (s.start[0] == '.' && s.start[1] == '.')))
        {
            continue;
        }

        value = njs_array_push(vm, results);
        if (njs_slow_path(value == NULL)) {
            goto done;
        }

        if (encoding == NULL) {
            ret = njs_buffer_set(vm, &ename, s.start, s.length);

        } else {
            ret = encoding->encode(vm, &ename, &s);
        }

        if (njs_slow_path(ret != NJS_OK)) {
            goto done;
        }

        if (njs_fast_path(!njs_is_true(&types))) {
            *value = ename;
            continue;
        }

        njs_set_number(&etype, njs_dentry_type(entry));

        ret = njs_fs_dirent_create(vm, &ename, &etype, value);
        if (njs_slow_path(ret != NJS_OK)) {
            goto done;
        }
    }

done:

    if (dir != NULL) {
        (void) closedir(dir);
    }

    if (ret == NJS_OK) {
        return njs_fs_result(vm, &retval, calltype, callback, 2);
    }

    return NJS_ERROR;
}


static njs_int_t
njs_fs_stat(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t magic)
{
    njs_int_t          ret;
    njs_bool_t         throw;
    struct stat        sb;
    const char         *path;
    njs_value_t        retval, *callback, *options;
    njs_fs_calltype_t  calltype;
    char               path_buf[NJS_MAX_PATH + 1];

    static const njs_value_t  string_bigint = njs_string("bigint");
    static const njs_value_t  string_throw = njs_string("throwIfNoEntry");

    path = njs_fs_path(vm, path_buf, njs_arg(args, nargs, 1), "path");
    if (njs_slow_path(path == NULL)) {
        return NJS_ERROR;
    }

    callback = NULL;
    calltype = magic & 3;
    options = njs_arg(args, nargs, 2);

    if (njs_slow_path(calltype == NJS_FS_CALLBACK)) {
        callback = njs_arg(args, nargs, njs_min(nargs - 1, 3));
        if (!njs_is_function(callback)) {
            njs_type_error(vm, "\"callback\" must be a function");
            return NJS_ERROR;
        }
        if (options == callback) {
            options = njs_value_arg(&njs_value_undefined);
        }
    }

    throw = 1;

    switch (options->type) {
    case NJS_UNDEFINED:
        break;

    default:
        if (!njs_is_object(options)) {
            njs_type_error(vm, "Unknown options type: \"%s\" "
                           "(an object required)",
                           njs_type_string(options->type));
            return NJS_ERROR;
        }

        ret = njs_value_property(vm, options, njs_value_arg(&string_bigint),
                                 &retval);
        if (njs_slow_path(ret == NJS_ERROR)) {
            return ret;
        }

        if (njs_bool(&retval)) {
            njs_type_error(vm, "\"bigint\" is not supported");
            return NJS_ERROR;
        }

        if (calltype == NJS_FS_DIRECT) {
            ret = njs_value_property(vm, options, njs_value_arg(&string_throw),
                                     &retval);
            if (njs_slow_path(ret == NJS_ERROR)) {
                return ret;
            }

            throw = njs_bool(&retval);
        }
    }

    ret = ((magic >> 2) == NJS_FS_STAT) ? stat(path, &sb) : lstat(path, &sb);
    if (njs_slow_path(ret != 0)) {
        if (errno != ENOENT || throw) {
            ret = njs_fs_error(vm,
                               ((magic >> 2) == NJS_FS_STAT) ? "stat" : "lstat",
                               strerror(errno), path, errno, &retval);
            if (njs_slow_path(ret != NJS_OK)) {
                return NJS_ERROR;
            }
        } else {
            njs_set_undefined(&retval);
        }

        return njs_fs_result(vm, &retval, calltype, callback, 2);
    }

    ret = njs_fs_stats_create(vm, &sb, &retval);
    if (njs_slow_path(ret != NJS_OK)) {
        return NJS_ERROR;
    }

    return njs_fs_result(vm, &retval, calltype, callback, 2);
}


static njs_int_t
njs_fs_fd_read(njs_vm_t *vm, int fd, njs_str_t *data)
{
    u_char   *p, *end, *start;
    size_t   size;
    ssize_t  n;

    size = data->length;

    if (size == 0) {
        size = 4096;
    }

    data->start = njs_mp_alloc(vm->mem_pool, size);
    if (data->start == NULL) {
        njs_memory_error(vm);
        return NJS_ERROR;
    }

    p = data->start;
    end = p + size;

    for ( ;; ) {
        n = read(fd, p, end - p);

        if (njs_slow_path(n < 0)) {
            return NJS_DECLINED;
        }

        p += n;

        if (n == 0) {
            break;
        }

        if (end - p < 2048) {
            size *= 2;

            start = njs_mp_alloc(vm->mem_pool, size);
            if (start == NULL) {
                njs_memory_error(vm);
                return NJS_ERROR;
            }

            memcpy(start, data->start, p - data->start);

            njs_mp_free(vm->mem_pool, data->start);

            p = start + (p - data->start);
            end = start + size;
            data->start = start;
        }
    }

    data->length = p - data->start;

    return NJS_OK;
}


static njs_int_t
njs_fs_make_path(njs_vm_t *vm, char *path, mode_t md, njs_bool_t recursive,
    njs_value_t *retval)
{
    int          err;
    njs_int_t    ret;
    const char   *p, *prev, *end;
    struct stat  sb;

    njs_set_undefined(retval);

    end = path + njs_strlen(path);

    if (!recursive) {
        ret = mkdir(path, md);
        if (ret != 0) {
            err = errno;
            goto failed;
        }

        return NJS_OK;
    }

    p = path;
    prev = p;

    for ( ;; ) {
        p = strchr(prev + 1, '/');
        if (p == NULL) {
            p = end;
        }

        if (njs_slow_path((p - path) > NJS_MAX_PATH)) {
            njs_internal_error(vm, "too large path");
            return NJS_ERROR;
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

    return NJS_OK;

failed:

    return njs_fs_error(vm, "mkdir", strerror(err), path, err, retval);
}


typedef struct njs_ftw_trace_s  njs_ftw_trace_t;

struct njs_ftw_trace_s {
    struct njs_ftw_trace_s  *chain;
    dev_t                   dev;
    ino_t                   ino;
};


static int
njs_ftw(char *path, njs_file_tree_walk_cb_t cb, int fd_limit,
    njs_ftw_flags_t flags, njs_ftw_trace_t *parent)
{
    int              type, ret, dfd;
    DIR              *d;
    size_t           base, len, length;
    const char       *d_name;
    struct stat      st;
    struct dirent    *entry;
    njs_ftw_trace_t  trace, *h;

    ret = (flags & NJS_FTW_PHYS) ? lstat(path, &st) : stat(path, &st);

    if (ret < 0) {
        if (!(flags & NJS_FTW_PHYS) && errno == ENOENT && !lstat(path, &st)) {
            type = NJS_FTW_SLN;

        } else if (errno != EACCES) {
            return NJS_ERROR;

        } else {
            type = NJS_FTW_NS;
        }

    } else if (S_ISDIR(st.st_mode)) {
        type = (flags & NJS_FTW_DEPTH) ? NJS_FTW_DP : NJS_FTW_D;

    } else if (S_ISLNK(st.st_mode)) {
        type = (flags & NJS_FTW_PHYS) ? NJS_FTW_SL : NJS_FTW_SLN;

    } else {
        type = NJS_FTW_F;
    }

    if ((flags & NJS_FTW_MOUNT) && parent != NULL && st.st_dev != parent->dev) {
        return NJS_OK;
    }

    for (h = parent; h != NULL; h = h->chain) {
        if (h->dev == st.st_dev && h->ino == st.st_ino) {
            return NJS_OK;
        }
    }

    len = njs_strlen(path);
    base = len && (path[len - 1] == '/') ? len - 1 : len;

    trace.chain = parent;
    trace.dev = st.st_dev;
    trace.ino = st.st_ino;

    d = NULL;
    dfd = -1;

    if (type == NJS_FTW_D || type == NJS_FTW_DP) {
        dfd = open(path, O_RDONLY);
        if (dfd < 0) {
            if (errno != EACCES) {
                return NJS_ERROR;
            }

            type = NJS_FTW_DNR;
        }
    }

    if (!(flags & NJS_FTW_DEPTH)) {
        ret = cb(path, &st, type);
        if (njs_slow_path(ret != 0)) {
            goto done;
        }
    }

    if (type == NJS_FTW_D || type == NJS_FTW_DP) {
        d = fdopendir(dfd);
        if (njs_slow_path(d == NULL)) {
            ret = NJS_ERROR;
            goto done;
        }

        for ( ;; ) {
            entry = readdir(d);

            if (entry == NULL) {
                break;
            }

            d_name = entry->d_name;
            length = njs_strlen(d_name);

            if ((length == 1 && d_name[0] == '.')
                || (length == 2 && (d_name[0] == '.' && d_name[1] == '.')))
            {
                continue;
            }

            if (njs_slow_path(length >= (NJS_MAX_PATH - len))) {
                errno = ENAMETOOLONG;
                ret = NJS_ERROR;
                goto done;
            }

            path[base] = '/';
            strcpy(path + base + 1, d_name);

            if (fd_limit != 0) {
                ret = njs_ftw(path, cb, fd_limit - 1, flags, &trace);
                if (njs_slow_path(ret != 0)) {
                    goto done;
                }
            }
        }

        (void) closedir(d);
        d = NULL;
        dfd = -1;
    }

    path[len] = '\0';

    if (flags & NJS_FTW_DEPTH) {
        ret = cb(path, &st, type);
        if (njs_slow_path(ret != 0)) {
            return ret;
        }
    }

    ret = NJS_OK;

done:

    if (d != NULL) {
        /* closedir() also closes underlying dfd. */
        (void) closedir(d);

    } else if (dfd >= 0) {
        (void) close(dfd);
    }

    return ret;
}


static njs_int_t
njs_file_tree_walk(const char *path, njs_file_tree_walk_cb_t cb, int fd_limit,
    njs_ftw_flags_t flags)
{
    size_t  len;
    char    pathbuf[NJS_MAX_PATH + 1];

    len = njs_strlen(path);
    if (njs_slow_path(len > NJS_MAX_PATH)) {
        errno = ENAMETOOLONG;
        return -1;
    }

    memcpy(pathbuf, path, len + 1);

    return njs_ftw(pathbuf, cb, fd_limit, flags, NULL);
}


static njs_int_t
njs_fs_rmtree_cb(const char *path, const struct stat *sb, njs_ftw_type_t type)
{
    njs_int_t  ret;

    ret = remove(path);
    if (ret != 0) {
        return NJS_ERROR;
    }

    return NJS_OK;
}


static njs_int_t
njs_fs_rmtree(njs_vm_t *vm, const char *path, njs_bool_t recursive,
    njs_value_t *retval)
{
    njs_int_t   ret;
    const char  *description;

    njs_set_undefined(retval);

    ret = rmdir(path);
    if (ret == 0) {
        return NJS_OK;
    }

    description = strerror(errno);

    if (recursive && (errno == ENOTEMPTY || errno == EEXIST)) {
        ret = njs_file_tree_walk(path, njs_fs_rmtree_cb, 16,
                                 NJS_FTW_PHYS | NJS_FTW_MOUNT | NJS_FTW_DEPTH);

        if (ret == NJS_OK) {
            return NJS_OK;
        }

        description = strerror(errno);
    }

    return njs_fs_error(vm, "rmdir", description, path, errno, retval);
}


static const char *
njs_fs_path(njs_vm_t *vm, char storage[NJS_MAX_PATH + 1],
    const njs_value_t *src, const char *prop_name)
{
    u_char              *p;
    njs_str_t           str;
    njs_typed_array_t   *array;
    njs_array_buffer_t  *buffer;

    switch (src->type) {
    case NJS_STRING:
        njs_string_get(src, &str);
        break;

    case NJS_TYPED_ARRAY:
    case NJS_DATA_VIEW:
        array = njs_typed_array(src);
        buffer = array->buffer;
        if (njs_slow_path(njs_is_detached_buffer(buffer))) {
            njs_type_error(vm, "detached buffer");
            return NULL;
        }

        str.start = &buffer->u.u8[array->offset];
        str.length = array->byte_length;
        break;

    default:
        njs_type_error(vm, "\"%s\" must be a string or Buffer", prop_name);
        return NULL;
    }

    if (njs_slow_path(str.length > NJS_MAX_PATH - 1)) {
        njs_type_error(vm, "\"%s\" is too long >= %d", prop_name, NJS_MAX_PATH);
        return NULL;
    }

    if (njs_slow_path(memchr(str.start, '\0', str.length) != 0)) {
        njs_type_error(vm, "\"%s\" must be a Buffer without null bytes",
                       prop_name);
        return NULL;
    }

    p = njs_cpymem(storage, str.start, str.length);
    *p++ = '\0';

    return storage;
}


static int
njs_fs_flags(njs_vm_t *vm, njs_value_t *value, int default_flags)
{
    njs_str_t       flags;
    njs_int_t       ret;
    njs_fs_entry_t  *fl;

    if (njs_is_undefined(value)) {
        return default_flags;
    }

    ret = njs_value_to_string(vm, value, value);
    if (njs_slow_path(ret != NJS_OK)) {
        return -1;
    }

    njs_string_get(value, &flags);

    for (fl = &njs_flags_table[0]; fl->name.length != 0; fl++) {
        if (njs_strstr_eq(&flags, &fl->name)) {
            return fl->value;
        }
    }

    njs_type_error(vm, "Unknown file open flags: \"%V\"", &flags);

    return -1;
}


static mode_t
njs_fs_mode(njs_vm_t *vm, njs_value_t *value, mode_t default_mode)
{
    uint32_t   u32;
    njs_int_t  ret;

    /* GCC complains about uninitialized u32. */
    u32 = 0;

    if (njs_is_undefined(value)) {
        return default_mode;
    }

    ret = njs_value_to_uint32(vm, value, &u32);
    if (njs_slow_path(ret != NJS_OK)) {
        return (mode_t) -1;
    }

    return (mode_t) u32;
}


static njs_int_t
njs_fs_error(njs_vm_t *vm, const char *syscall, const char *description,
    const char *path, int errn, njs_value_t *retval)
{
    size_t        size;
    njs_int_t     ret;
    njs_value_t   value;
    const char    *code;
    njs_object_t  *error;

    static const njs_value_t  string_errno = njs_string("errno");
    static const njs_value_t  string_code = njs_string("code");
    static const njs_value_t  string_path = njs_string("path");
    static const njs_value_t  string_syscall = njs_string("syscall");

    size = description != NULL ? njs_strlen(description) : 0;

    ret = njs_string_create(vm, &value, description, size);
    if (njs_slow_path(ret != NJS_OK)) {
        return NJS_ERROR;
    }

    error = njs_error_alloc(vm, NJS_OBJ_TYPE_ERROR, NULL, &value, NULL);
    if (njs_slow_path(error == NULL)) {
        return NJS_ERROR;
    }

    njs_set_object(retval, error);

    if (errn != 0) {
        njs_set_number(&value, errn);
        ret = njs_value_property_set(vm, retval, njs_value_arg(&string_errno),
                                     &value);
        if (njs_slow_path(ret != NJS_OK)) {
            return NJS_ERROR;
        }

        code = njs_errno_string(errn);

        ret = njs_string_create(vm, &value, code, njs_strlen(code));
        if (njs_slow_path(ret != NJS_OK)) {
            return NJS_ERROR;
        }

        ret = njs_value_property_set(vm, retval, njs_value_arg(&string_code),
                                     &value);
        if (njs_slow_path(ret != NJS_OK)) {
            return NJS_ERROR;
        }
    }

    if (path != NULL) {
        ret = njs_string_create(vm, &value, path, njs_strlen(path));
        if (njs_slow_path(ret != NJS_OK)) {
            return NJS_ERROR;
        }

        ret = njs_value_property_set(vm, retval, njs_value_arg(&string_path),
                                     &value);
        if (njs_slow_path(ret != NJS_OK)) {
            return NJS_ERROR;
        }
    }

    if (syscall != NULL) {
        ret = njs_string_create(vm, &value, syscall, njs_strlen(syscall));
        if (njs_slow_path(ret != NJS_OK)) {
            return NJS_ERROR;
        }

        ret = njs_value_property_set(vm, retval, njs_value_arg(&string_syscall),
                                     &value);
        if (njs_slow_path(ret != NJS_OK)) {
            return NJS_ERROR;
        }
    }

    return NJS_OK;
}


static njs_int_t
ngx_fs_promise_trampoline(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t unused)
{
    njs_value_t value;

    return njs_function_call(vm, njs_function(&args[1]), &njs_value_undefined,
                             &args[2], 1, &value);
}


static const njs_value_t  promise_trampoline  =
    njs_native_function(ngx_fs_promise_trampoline, 2);


static njs_int_t
njs_fs_result(njs_vm_t *vm, njs_value_t *result, njs_index_t calltype,
    const njs_value_t *callback, njs_uint_t nargs)
{
    njs_int_t    ret;
    njs_value_t  promise, callbacks[2], arguments[2];

    switch (calltype) {
    case NJS_FS_DIRECT:
        vm->retval = *result;
        return njs_is_error(result) ? NJS_ERROR : NJS_OK;

    case NJS_FS_PROMISE:
        ret = njs_vm_promise_create(vm, &promise, &callbacks[0]);
        if (njs_slow_path(ret != NJS_OK)) {
            return ret;
        }

        arguments[0] = njs_is_error(result) ? callbacks[1] : callbacks[0];
        arguments[1] = *result;

        ret = njs_fs_add_event(vm, njs_value_arg(&promise_trampoline),
                               njs_value_arg(&arguments), 2);
        if (njs_slow_path(ret != NJS_OK)) {
            return ret;
        }

        vm->retval = promise;

        return NJS_OK;

    case NJS_FS_CALLBACK:
        if (njs_is_error(result)) {
            arguments[0] = *result;
            njs_set_undefined(&arguments[1]);

        } else {
            njs_set_undefined(&arguments[0]);
            arguments[1] = *result;
        }

        ret = njs_fs_add_event(vm, callback, njs_value_arg(&arguments),
                               nargs);
        if (njs_slow_path(ret != NJS_OK)) {
            return ret;
        }

        njs_set_undefined(&vm->retval);

        return NJS_OK;

    default:
        njs_internal_error(vm, "invalid calltype");

        return NJS_ERROR;
    }
}


static njs_int_t
njs_fs_add_event(njs_vm_t *vm, const njs_value_t *callback,
    const njs_value_t *args, njs_uint_t nargs)
{
    njs_event_t   *event;
    njs_vm_ops_t  *ops;

    ops = vm->options.ops;
    if (njs_slow_path(ops == NULL)) {
        njs_internal_error(vm, "not supported by host environment");
        return NJS_ERROR;
    }

    event = njs_mp_alloc(vm->mem_pool, sizeof(njs_event_t));
    if (njs_slow_path(event == NULL)) {
        goto memory_error;
    }

    event->destructor = ops->clear_timer;
    event->function = njs_function(callback);
    event->nargs = nargs;
    event->once = 1;
    event->posted = 0;

    event->args = njs_mp_alloc(vm->mem_pool, sizeof(njs_value_t) * nargs);
    if (njs_slow_path(event->args == NULL)) {
        goto memory_error;
    }

    memcpy(event->args, args, sizeof(njs_value_t) * nargs);

    event->host_event = ops->set_timer(vm->external, 0, event);
    if (njs_slow_path(event->host_event == NULL)) {
        njs_internal_error(vm, "set_timer() failed");
        return NJS_ERROR;
    }

    return njs_add_event(vm, event);

memory_error:

    njs_memory_error(vm);

    return NJS_ERROR;
}


static njs_int_t
njs_fs_dirent_create(njs_vm_t *vm, njs_value_t *name, njs_value_t *type,
    njs_value_t *retval)
{
    njs_int_t     ret;
    njs_object_t  *object;

    static const njs_value_t  string_name = njs_string("name");
    static const njs_value_t  string_type = njs_string("type");

    object = njs_object_alloc(vm);
    if (njs_slow_path(object == NULL)) {
        return NJS_ERROR;
    }

    object->__proto__ = &vm->prototypes[NJS_OBJ_TYPE_FS_DIRENT].object;

    njs_set_object(retval, object);

    ret = njs_value_property_set(vm, retval, njs_value_arg(&string_name),
                                 name);
    if (njs_slow_path(ret != NJS_OK)) {
        return ret;
    }

    /* TODO: use a private symbol as a key. */
    ret = njs_value_property_set(vm, retval, njs_value_arg(&string_type),
                                 type);
    if (njs_slow_path(ret != NJS_OK)) {
        return ret;
    }

    return NJS_OK;
}


static njs_int_t
njs_dirent_constructor(njs_vm_t *vm, njs_value_t *args,
    njs_uint_t nargs, njs_index_t unused)
{
    if (njs_slow_path(!vm->top_frame->ctor)) {
        njs_type_error(vm, "the Dirent constructor must be called with new");
        return NJS_ERROR;
    }

    return njs_fs_dirent_create(vm, njs_arg(args, nargs, 1),
                                njs_arg(args, nargs, 2), &vm->retval);
}


static const njs_object_prop_t  njs_dirent_constructor_properties[] =
{
    {
        .type = NJS_PROPERTY,
        .name = njs_string("name"),
        .value = njs_string("Dirent"),
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("length"),
        .value = njs_value(NJS_NUMBER, 1, 2.0),
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY_HANDLER,
        .name = njs_string("prototype"),
        .value = njs_prop_handler(njs_object_prototype_create),
    },
};


const njs_object_init_t  njs_dirent_constructor_init = {
    njs_dirent_constructor_properties,
    njs_nitems(njs_dirent_constructor_properties),
};


static njs_int_t
njs_fs_dirent_test(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t testtype)
{
    njs_int_t    ret;
    njs_value_t  type, *this;

    static const njs_value_t  string_type = njs_string("type");

    this = njs_argument(args, 0);

    ret = njs_value_property(vm, this, njs_value_arg(&string_type), &type);
    if (njs_slow_path(ret == NJS_ERROR)) {
        return ret;
    }

    if (njs_slow_path(njs_is_number(&type)
                      && (njs_number(&type) == NJS_DT_INVALID)))
    {
        njs_internal_error(vm, "dentry type is not supported on this platform");
        return NJS_ERROR;
    }

    njs_set_boolean(&vm->retval,
                    njs_is_number(&type) && testtype == njs_number(&type));

    return NJS_OK;
}


static const njs_object_prop_t  njs_dirent_prototype_properties[] =
{
    {
        .type = NJS_PROPERTY,
        .name = njs_wellknown_symbol(NJS_SYMBOL_TO_STRING_TAG),
        .value = njs_string("Dirent"),
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY_HANDLER,
        .name = njs_string("constructor"),
        .value = njs_prop_handler(njs_object_prototype_create_constructor),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("isDirectory"),
        .value = njs_native_function2(njs_fs_dirent_test, 0, DT_DIR),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("isFile"),
        .value = njs_native_function2(njs_fs_dirent_test, 0, DT_REG),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("isBlockDevice"),
        .value = njs_native_function2(njs_fs_dirent_test, 0, DT_BLK),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_long_string("isCharacterDevice"),
        .value = njs_native_function2(njs_fs_dirent_test, 0, DT_CHR),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("isSymbolicLink"),
        .value = njs_native_function2(njs_fs_dirent_test, 0, DT_LNK),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("isFIFO"),
        .value = njs_native_function2(njs_fs_dirent_test, 0, DT_FIFO),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("isSocket"),
        .value = njs_native_function2(njs_fs_dirent_test, 0, DT_SOCK),
        .writable = 1,
        .configurable = 1,
    },
};


const njs_object_init_t  njs_dirent_prototype_init = {
    njs_dirent_prototype_properties,
    njs_nitems(njs_dirent_prototype_properties),
};


const njs_object_type_init_t  njs_dirent_type_init = {
    .constructor = njs_native_ctor(njs_dirent_constructor, 2, 0),
    .prototype_props = &njs_dirent_prototype_init,
    .constructor_props = &njs_dirent_constructor_init,
    .prototype_value = { .object = { .type = NJS_OBJECT } },
};


static void
njs_fs_to_stat(njs_stat_t *dst, struct stat *st)
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
    dst->st_birthtim.tv_sec = st->st_birthtimespec.tv_sec;
    dst->st_birthtim.tv_nsec = st->st_birthtimespec.tv_nsec;

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


static njs_int_t
njs_fs_stats_create(njs_vm_t *vm, struct stat *st, njs_value_t *retval)
{
    njs_stat_t          *copy;
    njs_object_value_t  *stat;

    stat = njs_object_value_alloc(vm, NJS_OBJ_TYPE_FS_STATS, 0, NULL);
    if (njs_slow_path(stat == NULL)) {
        return NJS_ERROR;
    }

    stat->object.shared_hash =
                      vm->prototypes[NJS_OBJ_TYPE_FS_STATS].object.shared_hash;

    copy = njs_mp_alloc(vm->mem_pool, sizeof(njs_stat_t));
    if (copy == NULL) {
        njs_memory_error(vm);
        return NJS_ERROR;
    }

    njs_fs_to_stat(copy, st);

    njs_set_data(&stat->value, copy, NJS_DATA_TAG_FS_STAT);
    njs_set_object_value(retval, stat);

    return NJS_OK;
}


static njs_int_t
njs_stats_constructor(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t unused)
{
    njs_type_error(vm, "Stats is not a constructor");
    return NJS_ERROR;
}


static njs_int_t
njs_fs_stats_test(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t testtype)
{
    unsigned     mask;
    njs_stat_t   *st;
    njs_value_t  *this;

    this = njs_argument(args, 0);

    if (njs_slow_path(!njs_is_object_data(this, NJS_DATA_TAG_FS_STAT))) {
        return NJS_DECLINED;
    }

    st = njs_object_data(this);

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

    njs_set_boolean(&vm->retval, (st->st_mode & S_IFMT) == mask);

    return NJS_OK;
}


static njs_int_t
njs_fs_stats_prop(njs_vm_t *vm, njs_object_prop_t *prop, njs_value_t *value,
    njs_value_t *setval, njs_value_t *retval)
{
    double      v;
    njs_date_t  *date;
    njs_stat_t  *st;

#define njs_fs_time_ms(ts) ((ts)->tv_sec * 1000.0 + (ts)->tv_nsec / 1000000.0)

    if (njs_slow_path(!njs_is_object_data(value, NJS_DATA_TAG_FS_STAT))) {
        return NJS_DECLINED;
    }

    st = njs_object_data(value);

    switch (prop->value.data.magic16) {
    case NJS_FS_STAT_DEV:
        v = st->st_dev;
        break;

    case NJS_FS_STAT_INO:
        v = st->st_ino;
        break;

    case NJS_FS_STAT_MODE:
        v = st->st_mode;
        break;

    case NJS_FS_STAT_NLINK:
        v = st->st_nlink;
        break;

    case NJS_FS_STAT_UID:
        v = st->st_uid;
        break;

    case NJS_FS_STAT_GID:
        v = st->st_gid;
        break;

    case NJS_FS_STAT_RDEV:
        v = st->st_rdev;
        break;

    case NJS_FS_STAT_SIZE:
        v = st->st_size;
        break;

    case NJS_FS_STAT_BLKSIZE:
        v = st->st_blksize;
        break;

    case NJS_FS_STAT_BLOCKS:
        v = st->st_blocks;
        break;

    case NJS_FS_STAT_ATIME:
        v = njs_fs_time_ms(&st->st_atim);
        break;

    case NJS_FS_STAT_BIRTHTIME:
        v = njs_fs_time_ms(&st->st_birthtim);
        break;

    case NJS_FS_STAT_CTIME:
        v = njs_fs_time_ms(&st->st_ctim);
        break;

    case NJS_FS_STAT_MTIME:
    default:
        v = njs_fs_time_ms(&st->st_mtim);
        break;
    }

    switch (prop->value.data.magic32) {
    case NJS_NUMBER:
        njs_set_number(retval, v);
        break;

    case NJS_DATE:
    default:
        date = njs_date_alloc(vm, v);
        if (njs_slow_path(date == NULL)) {
            return NJS_ERROR;
        }

        njs_set_date(retval, date);
        break;
    }

    return NJS_OK;
}


static const njs_object_prop_t  njs_stats_constructor_properties[] =
{
    {
        .type = NJS_PROPERTY,
        .name = njs_string("name"),
        .value = njs_string("Stats"),
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("length"),
        .value = njs_value(NJS_NUMBER, 1, 0),
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY_HANDLER,
        .name = njs_string("prototype"),
        .value = njs_prop_handler(njs_object_prototype_create),
    },
};


const njs_object_init_t  njs_stats_constructor_init = {
    njs_stats_constructor_properties,
    njs_nitems(njs_stats_constructor_properties),
};


static const njs_object_prop_t  njs_stats_prototype_properties[] =
{
    {
        .type = NJS_PROPERTY,
        .name = njs_wellknown_symbol(NJS_SYMBOL_TO_STRING_TAG),
        .value = njs_string("Stats"),
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY_HANDLER,
        .name = njs_string("constructor"),
        .value = njs_prop_handler(njs_object_prototype_create_constructor),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("isBlockDevice"),
        .value = njs_native_function2(njs_fs_stats_test, 0, DT_BLK),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_long_string("isCharacterDevice"),
        .value = njs_native_function2(njs_fs_stats_test, 0, DT_CHR),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("isDirectory"),
        .value = njs_native_function2(njs_fs_stats_test, 0, DT_DIR),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("isFIFO"),
        .value = njs_native_function2(njs_fs_stats_test, 0, DT_FIFO),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("isFile"),
        .value = njs_native_function2(njs_fs_stats_test, 0, DT_REG),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("isSocket"),
        .value = njs_native_function2(njs_fs_stats_test, 0, DT_SOCK),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("isSymbolicLink"),
        .value = njs_native_function2(njs_fs_stats_test, 0, DT_LNK),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY_HANDLER,
        .name = njs_string("dev"),
        .value = njs_prop_handler2(njs_fs_stats_prop, NJS_FS_STAT_DEV,
                                   NJS_NUMBER),
        .enumerable = 1,
    },

    {
        .type = NJS_PROPERTY_HANDLER,
        .name = njs_string("ino"),
        .value = njs_prop_handler2(njs_fs_stats_prop, NJS_FS_STAT_INO,
                                   NJS_NUMBER),
        .enumerable = 1,
    },

    {
        .type = NJS_PROPERTY_HANDLER,
        .name = njs_string("mode"),
        .value = njs_prop_handler2(njs_fs_stats_prop, NJS_FS_STAT_MODE,
                                   NJS_NUMBER),
        .enumerable = 1,
    },

    {
        .type = NJS_PROPERTY_HANDLER,
        .name = njs_string("nlink"),
        .value = njs_prop_handler2(njs_fs_stats_prop, NJS_FS_STAT_NLINK,
                                   NJS_NUMBER),
        .enumerable = 1,
    },

    {
        .type = NJS_PROPERTY_HANDLER,
        .name = njs_string("uid"),
        .value = njs_prop_handler2(njs_fs_stats_prop, NJS_FS_STAT_UID,
                                   NJS_NUMBER),
        .enumerable = 1,
    },

    {
        .type = NJS_PROPERTY_HANDLER,
        .name = njs_string("gid"),
        .value = njs_prop_handler2(njs_fs_stats_prop, NJS_FS_STAT_GID,
                                   NJS_NUMBER),
        .enumerable = 1,
    },

    {
        .type = NJS_PROPERTY_HANDLER,
        .name = njs_string("rdev"),
        .value = njs_prop_handler2(njs_fs_stats_prop, NJS_FS_STAT_RDEV,
                                   NJS_NUMBER),
        .enumerable = 1,
    },

    {
        .type = NJS_PROPERTY_HANDLER,
        .name = njs_string("size"),
        .value = njs_prop_handler2(njs_fs_stats_prop, NJS_FS_STAT_SIZE,
                                   NJS_NUMBER),
        .enumerable = 1,
    },

    {
        .type = NJS_PROPERTY_HANDLER,
        .name = njs_string("blksize"),
        .value = njs_prop_handler2(njs_fs_stats_prop, NJS_FS_STAT_BLKSIZE,
                                   NJS_NUMBER),
        .enumerable = 1,
    },

    {
        .type = NJS_PROPERTY_HANDLER,
        .name = njs_string("blocks"),
        .value = njs_prop_handler2(njs_fs_stats_prop, NJS_FS_STAT_BLOCKS,
                                   NJS_NUMBER),
        .enumerable = 1,
    },

    {
        .type = NJS_PROPERTY_HANDLER,
        .name = njs_string("atimeMs"),
        .value = njs_prop_handler2(njs_fs_stats_prop, NJS_FS_STAT_ATIME,
                                   NJS_NUMBER),
        .enumerable = 1,
    },

    {
        .type = NJS_PROPERTY_HANDLER,
        .name = njs_string("birthtimeMs"),
        .value = njs_prop_handler2(njs_fs_stats_prop, NJS_FS_STAT_BIRTHTIME,
                                   NJS_NUMBER),
        .enumerable = 1,
    },

    {
        .type = NJS_PROPERTY_HANDLER,
        .name = njs_string("ctimeMs"),
        .value = njs_prop_handler2(njs_fs_stats_prop, NJS_FS_STAT_CTIME,
                                   NJS_NUMBER),
        .enumerable = 1,
    },

    {
        .type = NJS_PROPERTY_HANDLER,
        .name = njs_string("mtimeMs"),
        .value = njs_prop_handler2(njs_fs_stats_prop, NJS_FS_STAT_MTIME,
                                   NJS_NUMBER),
        .enumerable = 1,
    },

    {
        .type = NJS_PROPERTY_HANDLER,
        .name = njs_string("atime"),
        .value = njs_prop_handler2(njs_fs_stats_prop, NJS_FS_STAT_ATIME,
                                   NJS_DATE),
        .enumerable = 1,
    },

    {
        .type = NJS_PROPERTY_HANDLER,
        .name = njs_string("birthtime"),
        .value = njs_prop_handler2(njs_fs_stats_prop, NJS_FS_STAT_BIRTHTIME,
                                   NJS_DATE),
        .enumerable = 1,
    },

    {
        .type = NJS_PROPERTY_HANDLER,
        .name = njs_string("ctime"),
        .value = njs_prop_handler2(njs_fs_stats_prop, NJS_FS_STAT_CTIME,
                                   NJS_DATE),
        .enumerable = 1,
    },

    {
        .type = NJS_PROPERTY_HANDLER,
        .name = njs_string("mtime"),
        .value = njs_prop_handler2(njs_fs_stats_prop, NJS_FS_STAT_MTIME,
                                   NJS_DATE),
        .enumerable = 1,
    },
};


const njs_object_init_t  njs_stats_prototype_init = {
    njs_stats_prototype_properties,
    njs_nitems(njs_stats_prototype_properties),
};


const njs_object_type_init_t  njs_stats_type_init = {
    .constructor = njs_native_ctor(njs_stats_constructor, 0, 0),
    .prototype_props = &njs_stats_prototype_init,
    .constructor_props = &njs_stats_constructor_init,
    .prototype_value = { .object = { .type = NJS_OBJECT } },
};


static const njs_object_prop_t  njs_fs_promises_properties[] =
{
    {
        .type = NJS_PROPERTY,
        .name = njs_string("readFile"),
        .value = njs_native_function2(njs_fs_read_file, 0, NJS_FS_PROMISE),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("appendFile"),
        .value = njs_native_function2(njs_fs_write_file, 0,
                                  njs_fs_magic(NJS_FS_PROMISE, NJS_FS_APPEND)),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("writeFile"),
        .value = njs_native_function2(njs_fs_write_file, 0,
                                  njs_fs_magic(NJS_FS_PROMISE, NJS_FS_TRUNC)),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("access"),
        .value = njs_native_function2(njs_fs_access, 0, NJS_FS_PROMISE),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("mkdir"),
        .value = njs_native_function2(njs_fs_mkdir, 0, NJS_FS_PROMISE),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("rename"),
        .value = njs_native_function2(njs_fs_rename, 0, NJS_FS_PROMISE),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("rmdir"),
        .value = njs_native_function2(njs_fs_rmdir, 0, NJS_FS_PROMISE),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("readdir"),
        .value = njs_native_function2(njs_fs_readdir, 0, NJS_FS_PROMISE),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("lstat"),
        .value = njs_native_function2(njs_fs_stat, 0,
                                   njs_fs_magic(NJS_FS_PROMISE, NJS_FS_LSTAT)),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("stat"),
        .value = njs_native_function2(njs_fs_stat, 0,
                                    njs_fs_magic(NJS_FS_PROMISE, NJS_FS_STAT)),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("symlink"),
        .value = njs_native_function2(njs_fs_symlink, 0, NJS_FS_PROMISE),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("unlink"),
        .value = njs_native_function2(njs_fs_unlink, 0, NJS_FS_PROMISE),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("realpath"),
        .value = njs_native_function2(njs_fs_realpath, 0, NJS_FS_PROMISE),
        .writable = 1,
        .configurable = 1,
    },
};


static const njs_object_init_t  njs_fs_promises_init = {
    njs_fs_promises_properties,
    njs_nitems(njs_fs_promises_properties),
};


static njs_int_t
njs_fs_promises(njs_vm_t *vm, njs_object_prop_t *prop, njs_value_t *value,
    njs_value_t *unused, njs_value_t *retval)
{
    return njs_object_prop_init(vm, &njs_fs_promises_init, prop, value, retval);
}


static const njs_object_prop_t  njs_fs_constants_properties[] =
{
    {
        .type = NJS_PROPERTY,
        .name = njs_string("F_OK"),
        .value = njs_value(NJS_NUMBER, 0, F_OK),
        .enumerable = 1,
    },
    {
        .type = NJS_PROPERTY,
        .name = njs_string("R_OK"),
        .value = njs_value(NJS_NUMBER, 1, R_OK),
        .enumerable = 1,
    },
    {
        .type = NJS_PROPERTY,
        .name = njs_string("W_OK"),
        .value = njs_value(NJS_NUMBER, 1, W_OK),
        .enumerable = 1,
    },
    {
        .type = NJS_PROPERTY,
        .name = njs_string("X_OK"),
        .value = njs_value(NJS_NUMBER, 1, X_OK),
        .enumerable = 1,
    },
};


static const njs_object_init_t  njs_fs_constants_init = {
    njs_fs_constants_properties,
    njs_nitems(njs_fs_constants_properties),
};


static njs_int_t
njs_fs_constants(njs_vm_t *vm, njs_object_prop_t *prop, njs_value_t *value,
    njs_value_t *unused, njs_value_t *retval)
{
    return njs_object_prop_init(vm, &njs_fs_constants_init, prop, value,
                                retval);
}


static const njs_object_prop_t  njs_fs_object_properties[] =
{
    {
        .type = NJS_PROPERTY,
        .name = njs_string("name"),
        .value = njs_string("fs"),
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY_HANDLER,
        .name = njs_string("constants"),
        .value = njs_prop_handler(njs_fs_constants),
        .enumerable = 1,
    },

    {
        .type = NJS_PROPERTY_HANDLER,
        .name = njs_string("promises"),
        .value = njs_prop_handler(njs_fs_promises),
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("Dirent"),
        .value = _njs_native_function(njs_dirent_constructor, 2, 1, 0),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("access"),
        .value = njs_native_function2(njs_fs_access, 0, NJS_FS_CALLBACK),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("accessSync"),
        .value = njs_native_function2(njs_fs_access, 0, NJS_FS_DIRECT),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("readFile"),
        .value = njs_native_function2(njs_fs_read_file, 0, NJS_FS_CALLBACK),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("readFileSync"),
        .value = njs_native_function2(njs_fs_read_file, 0, NJS_FS_DIRECT),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("appendFile"),
        .value = njs_native_function2(njs_fs_write_file, 0,
                                  njs_fs_magic(NJS_FS_CALLBACK, NJS_FS_APPEND)),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("appendFileSync"),
        .value = njs_native_function2(njs_fs_write_file, 0,
                                   njs_fs_magic(NJS_FS_DIRECT, NJS_FS_APPEND)),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("writeFile"),
        .value = njs_native_function2(njs_fs_write_file, 0,
                                  njs_fs_magic(NJS_FS_CALLBACK, NJS_FS_TRUNC)),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("writeFileSync"),
        .value = njs_native_function2(njs_fs_write_file, 0,
                                    njs_fs_magic(NJS_FS_DIRECT, NJS_FS_TRUNC)),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("rename"),
        .value = njs_native_function2(njs_fs_rename, 0, NJS_FS_CALLBACK),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("renameSync"),
        .value = njs_native_function2(njs_fs_rename, 0, NJS_FS_DIRECT),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("symlink"),
        .value = njs_native_function2(njs_fs_symlink, 0, NJS_FS_CALLBACK),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("symlinkSync"),
        .value = njs_native_function2(njs_fs_symlink, 0, NJS_FS_DIRECT),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("unlink"),
        .value = njs_native_function2(njs_fs_unlink, 0, NJS_FS_CALLBACK),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("unlinkSync"),
        .value = njs_native_function2(njs_fs_unlink, 0, NJS_FS_DIRECT),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("realpath"),
        .value = njs_native_function2(njs_fs_realpath, 0, NJS_FS_CALLBACK),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("realpathSync"),
        .value = njs_native_function2(njs_fs_realpath, 0, NJS_FS_DIRECT),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("mkdir"),
        .value = njs_native_function2(njs_fs_mkdir, 0, NJS_FS_CALLBACK),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("mkdirSync"),
        .value = njs_native_function2(njs_fs_mkdir, 0, NJS_FS_DIRECT),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("rmdir"),
        .value = njs_native_function2(njs_fs_rmdir, 0, NJS_FS_CALLBACK),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("rmdirSync"),
        .value = njs_native_function2(njs_fs_rmdir, 0, NJS_FS_DIRECT),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("readdir"),
        .value = njs_native_function2(njs_fs_readdir, 0, NJS_FS_CALLBACK),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("readdirSync"),
        .value = njs_native_function2(njs_fs_readdir, 0, NJS_FS_DIRECT),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("lstat"),
        .value = njs_native_function2(njs_fs_stat, 0,
                                  njs_fs_magic(NJS_FS_CALLBACK, NJS_FS_LSTAT)),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("lstatSync"),
        .value = njs_native_function2(njs_fs_stat, 0,
                                    njs_fs_magic(NJS_FS_DIRECT, NJS_FS_LSTAT)),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("stat"),
        .value = njs_native_function2(njs_fs_stat, 0,
                                   njs_fs_magic(NJS_FS_CALLBACK, NJS_FS_STAT)),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("statSync"),
        .value = njs_native_function2(njs_fs_stat, 0,
                                     njs_fs_magic(NJS_FS_DIRECT, NJS_FS_STAT)),
        .writable = 1,
        .configurable = 1,
    },
};


const njs_object_init_t  njs_fs_object_init = {
    njs_fs_object_properties,
    njs_nitems(njs_fs_object_properties),
};
