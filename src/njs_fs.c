
/*
 * Copyright (C) Dmitry Volyntsev
 * Copyright (C) NGINX, Inc.
 */


#include <njs_main.h>


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
    NJS_FS_ENC_INVALID,
    NJS_FS_ENC_NONE,
    NJS_FS_ENC_UTF8,
} njs_fs_encoding_t;


typedef struct {
    njs_str_t   name;
    int         value;
} njs_fs_entry_t;


static njs_int_t njs_fs_fd_read(njs_vm_t *vm, int fd, njs_str_t *data);

static njs_int_t njs_fs_error(njs_vm_t *vm, const char *syscall,
    const char *desc, njs_value_t *path, int errn, njs_value_t *retval);
static njs_int_t njs_fs_result(njs_vm_t *vm, njs_value_t *result,
    njs_index_t calltype, const njs_value_t* callback, njs_uint_t nargs);

static int njs_fs_flags(njs_vm_t *vm, njs_value_t *value, int default_flags);
static mode_t njs_fs_mode(njs_vm_t *vm, njs_value_t *value,
    mode_t default_mode);
static njs_fs_encoding_t njs_fs_encoding(njs_vm_t *vm, njs_value_t *value);

static njs_int_t njs_fs_add_event(njs_vm_t *vm, const njs_value_t *callback,
    const njs_value_t *args, njs_uint_t nargs);


static njs_fs_entry_t njs_flags_table[] = {
    { njs_str("r"),   O_RDONLY },
    { njs_str("r+"),  O_RDWR },
    { njs_str("w"),   O_TRUNC  | O_CREAT  | O_WRONLY },
    { njs_str("w+"),  O_TRUNC  | O_CREAT  | O_RDWR },
    { njs_str("a"),   O_APPEND | O_CREAT  | O_WRONLY },
    { njs_str("a+"),  O_APPEND | O_CREAT  | O_RDWR },
    { njs_str("rs"),  O_SYNC   | O_RDONLY },
    { njs_str("sr"),  O_SYNC   | O_RDONLY },
    { njs_str("wx"),  O_TRUNC  | O_CREAT  | O_EXCL | O_WRONLY },
    { njs_str("xw"),  O_TRUNC  | O_CREAT  | O_EXCL | O_WRONLY },
    { njs_str("ax"),  O_APPEND | O_CREAT  | O_EXCL | O_WRONLY },
    { njs_str("xa"),  O_APPEND | O_CREAT  | O_EXCL | O_WRONLY },
    { njs_str("rs+"), O_SYNC   | O_RDWR },
    { njs_str("sr+"), O_SYNC   | O_RDWR },
    { njs_str("wx+"), O_TRUNC  | O_CREAT  | O_EXCL | O_RDWR },
    { njs_str("xw+"), O_TRUNC  | O_CREAT  | O_EXCL | O_RDWR },
    { njs_str("ax+"), O_APPEND | O_CREAT  | O_EXCL | O_RDWR },
    { njs_str("xa+"), O_APPEND | O_CREAT  | O_EXCL | O_RDWR },
    { njs_null_str, 0 }
};


njs_inline njs_int_t
njs_fs_path_arg(njs_vm_t *vm, const char **dst, const njs_value_t* src,
    const njs_str_t *prop_name)
{
    if (njs_slow_path(!njs_is_string(src))) {
        njs_type_error(vm, "\"%V\" must be a string", prop_name);
        return NJS_ERROR;
    }

    *dst = njs_string_to_c_string(vm, njs_value_arg(src));
    if (njs_slow_path(*dst == NULL)) {
        return NJS_ERROR;
    }

    return NJS_OK;
}


static njs_int_t
njs_fs_read_file(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t calltype)
{
    int                fd, flags;
    u_char             *start;
    size_t             size;
    ssize_t            length;
    njs_str_t          data;
    njs_int_t          ret;
    const char         *file_path;
    njs_value_t        flag, encoding, retval, *callback, *options, *path;
    struct stat        sb;
    njs_fs_encoding_t  enc;

    static const njs_value_t  string_flag = njs_string("flag");
    static const njs_value_t  string_encoding = njs_string("encoding");

    path = njs_arg(args, nargs, 1);
    ret = njs_fs_path_arg(vm, &file_path, path, &njs_str_value("path"));
    if (njs_slow_path(ret != NJS_OK)) {
        return ret;
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
    njs_set_undefined(&encoding);

    switch (options->type) {
    case NJS_STRING:
        encoding = *options;
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
                                 &encoding);
        if (njs_slow_path(ret == NJS_ERROR)) {
            return ret;
        }
    }

    flags = njs_fs_flags(vm, &flag, O_RDONLY);
    if (njs_slow_path(flags == -1)) {
        return NJS_ERROR;
    }

    enc = njs_fs_encoding(vm, &encoding);
    if (njs_slow_path(enc == NJS_FS_ENC_INVALID)) {
        return NJS_ERROR;
    }

    fd = open(file_path, flags);
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

    if (enc == NJS_FS_ENC_UTF8) {
        length = sb.st_size;

        if (length > NJS_STRING_MAP_STRIDE) {
            /*
             * At this point length is not known, in order to set it to
             * the correct value after file is read, we need to ensure that
             * offset_map is allocated by njs_string_alloc(). This can be
             * achieved by making length != size.
             */
            length += 1;
        }

    } else {
        length = 0;
    }

    size = sb.st_size;

    if (njs_fast_path(size != 0)) {
        start = njs_string_alloc(vm, &retval, size, length);
        if (njs_slow_path(start == NULL)) {
            ret = NJS_ERROR;
            goto done;
        }

        data.start = start;
        data.length = size;

        ret = njs_fs_fd_read(vm, fd, &data);
        if (njs_slow_path(ret != NJS_OK)) {
            if (ret == NJS_DECLINED) {
                ret = njs_fs_error(vm, "read", strerror(errno), path, errno,
                                   &retval);
            }

            goto done;
        }

        if (njs_slow_path(data.length < size)) {
            /* Pseudo-files may return less data than declared by st_size. */
            njs_string_truncate(&retval, data.length);
        }

        size = data.length;
        start = data.start;

    } else {
        /* size of the file is not known in advance. */

        data.length = 0;

        ret = njs_fs_fd_read(vm, fd, &data);
        if (njs_slow_path(ret != NJS_OK)) {
            if (ret == NJS_DECLINED) {
                ret = njs_fs_error(vm, "read", strerror(errno), path, errno,
                                   &retval);
            }

            goto done;
        }

        size = data.length;
        start = data.start;

        ret = njs_string_new(vm, &retval, start, size, length);
        if (njs_slow_path(ret != NJS_OK)) {
            goto done;
        }
    }

    if (enc == NJS_FS_ENC_UTF8) {
        length = njs_utf8_length(start, size);

        if (length >= 0) {
            njs_string_length_set(&retval, length);

        } else {
            ret = njs_fs_error(vm, NULL, "Non-UTF8 file, convertion "
                               "is not implemented", path, 0, &retval);
            goto done;
        }
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
    int                fd, flags;
    u_char             *p, *end;
    mode_t             md;
    ssize_t            n;
    njs_str_t          content;
    njs_int_t          ret;
    const char         *file_path;
    njs_value_t        flag, mode, encoding, retval,
                       *path, *data, *callback, *options;
    njs_fs_encoding_t  enc;
    njs_fs_calltype_t  calltype;

    static const njs_value_t  string_flag = njs_string("flag");
    static const njs_value_t  string_mode = njs_string("mode");
    static const njs_value_t  string_encoding = njs_string("encoding");

    path = njs_arg(args, nargs, 1);
    ret = njs_fs_path_arg(vm, &file_path, path, &njs_str_value("path"));
    if (njs_slow_path(ret != NJS_OK)) {
        return ret;
    }

    data = njs_arg(args, nargs, 2);
    if (njs_slow_path(!njs_is_string(data))) {
        njs_type_error(vm, "\"data\" must be a string");
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
    njs_set_undefined(&encoding);

    switch (options->type) {
    case NJS_STRING:
        encoding = *options;
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
                                 &encoding);
        if (njs_slow_path(ret == NJS_ERROR)) {
            return ret;
        }
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

    enc = njs_fs_encoding(vm, &encoding);
    if (njs_slow_path(enc == NJS_FS_ENC_INVALID)) {
        return NJS_ERROR;
    }

    fd = open(file_path, flags, md);
    if (njs_slow_path(fd < 0)) {
        ret = njs_fs_error(vm, "open", strerror(errno), path, errno, &retval);
        goto done;
    }

    njs_string_get(data, &content);

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
    const char   *old_path, *new_path;
    njs_value_t  retval, *callback;

    callback = NULL;

    if (calltype == NJS_FS_CALLBACK) {
        callback = njs_arg(args, nargs, 3);
        if (!njs_is_function(callback)) {
            njs_type_error(vm, "\"callback\" must be a function");
            return NJS_ERROR;
        }
    }

    ret = njs_fs_path_arg(vm, &old_path, njs_arg(args, nargs, 1),
                          &njs_str_value("oldPath"));
    if (njs_slow_path(ret != NJS_OK)) {
        return ret;
    }

    ret = njs_fs_path_arg(vm, &new_path, njs_arg(args, nargs, 2),
                          &njs_str_value("newPath"));
    if (njs_slow_path(ret != NJS_OK)) {
        return ret;
    }

    njs_set_undefined(&retval);

    ret = rename(old_path, new_path);
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
    const char  *file_path;
    njs_value_t  retval, *path, *callback, *mode;

    path = njs_arg(args, nargs, 1);
    ret = njs_fs_path_arg(vm, &file_path, path, &njs_str_value("path"));
    if (njs_slow_path(ret != NJS_OK)) {
        return ret;
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

    ret = access(file_path, md);
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
    const char  *target_path, *file_path;
    njs_value_t  retval, *target, *path, *callback, *type;

    target = njs_arg(args, nargs, 1);
    ret = njs_fs_path_arg(vm, &target_path, target, &njs_str_value("target"));
    if (njs_slow_path(ret != NJS_OK)) {
        return ret;
    }

    path = njs_arg(args, nargs, 2);
    ret = njs_fs_path_arg(vm, &file_path, path, &njs_str_value("path"));
    if (njs_slow_path(ret != NJS_OK)) {
        return ret;
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

    ret = symlink(target_path, file_path);
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
    const char   *file_path;
    njs_value_t  retval, *path, *callback;

    path = njs_arg(args, nargs, 1);
    ret = njs_fs_path_arg(vm, &file_path, path, &njs_str_value("path"));
    if (njs_slow_path(ret != NJS_OK)) {
        return ret;
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

    ret = unlink(file_path);
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
    u_char             *resolved_path;
    size_t             size;
    ssize_t            length;
    njs_int_t          ret;
    const char         *file_path;
    njs_value_t        encoding, retval, *path, *callback, *options;
    njs_fs_encoding_t  enc;
    char               path_buf[MAXPATHLEN];

    static const njs_value_t  string_encoding = njs_string("encoding");

    path = njs_arg(args, nargs, 1);
    ret = njs_fs_path_arg(vm, &file_path, path, &njs_str_value("path"));
    if (njs_slow_path(ret != NJS_OK)) {
        return ret;
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

    njs_set_undefined(&encoding);

    switch (options->type) {
    case NJS_STRING:
        encoding = *options;
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
                                 &encoding);
        if (njs_slow_path(ret == NJS_ERROR)) {
            return ret;
        }
    }

    enc = njs_fs_encoding(vm, &encoding);
    if (njs_slow_path(enc == NJS_FS_ENC_INVALID)) {
        return NJS_ERROR;
    }

    resolved_path = (u_char *) realpath(file_path, path_buf);
    if (njs_slow_path(resolved_path == NULL)) {
        ret = njs_fs_error(vm, "realpath", strerror(errno), path, errno,
                           &retval);
        goto done;
    }

    size = njs_strlen(resolved_path);
    length = njs_utf8_length(resolved_path, size);
    if (njs_slow_path(length < 0)) {
        length = 0;
    }

    ret = njs_string_new(vm, &retval, resolved_path, size, length);
    if (njs_slow_path(ret != NJS_OK)) {
        return NJS_ERROR;
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
    mode_t       md;
    njs_int_t    ret;
    const char   *file_path;
    njs_value_t  mode, recursive, retval, *path, *callback, *options;

    static const njs_value_t  string_mode = njs_string("mode");
    static const njs_value_t  string_recursive = njs_string("recursive");

    path = njs_arg(args, nargs, 1);
    ret = njs_fs_path_arg(vm, &file_path, path, &njs_str_value("path"));
    if (njs_slow_path(ret != NJS_OK)) {
        return ret;
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

    if (njs_is_true(&recursive)) {
        njs_type_error(vm, "\"options.recursive\" is not supported");
        return NJS_ERROR;
    }

    njs_set_undefined(&retval);

    ret = mkdir(file_path, md);
    if (njs_slow_path(ret != 0)) {
        ret = njs_fs_error(vm, "mkdir", strerror(errno), path, errno,
                           &retval);
    }

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
    const char   *file_path;
    njs_value_t  recursive, retval, *path, *callback, *options;

    static const njs_value_t  string_recursive = njs_string("recursive");

    path = njs_arg(args, nargs, 1);
    ret = njs_fs_path_arg(vm, &file_path, path, &njs_str_value("path"));
    if (njs_slow_path(ret != NJS_OK)) {
        return ret;
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

    if (njs_is_true(&recursive)) {
        njs_type_error(vm, "\"options.recursive\" is not supported");
        return NJS_ERROR;
    }

    njs_set_undefined(&retval);

    ret = rmdir(file_path);
    if (njs_slow_path(ret != 0)) {
        ret = njs_fs_error(vm, "rmdir", strerror(errno), path, errno,
                           &retval);
    }

    if (ret == NJS_OK) {
        return njs_fs_result(vm, &retval, calltype, callback, 1);
    }

    return NJS_ERROR;
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

        data->start = njs_mp_alloc(vm->mem_pool, size);
        if (data->start == NULL) {
            njs_memory_error(vm);
            return NJS_ERROR;
        }
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


static njs_fs_encoding_t
njs_fs_encoding(njs_vm_t *vm, njs_value_t *value)
{
    njs_str_t  enc;
    njs_int_t  ret;

    if (njs_is_undefined(value)) {
        return NJS_FS_ENC_NONE;
    }

    ret = njs_value_to_string(vm, value, value);
    if (njs_slow_path(ret != NJS_OK)) {
        return NJS_FS_ENC_INVALID;
    }

    njs_string_get(value, &enc);

    if (enc.length != 4 || memcmp(enc.start, "utf8", 4) != 0) {
        njs_type_error(vm, "Unknown encoding: \"%V\"", &enc);
        return NJS_FS_ENC_INVALID;
    }

    return NJS_FS_ENC_UTF8;
}


static njs_int_t
njs_fs_error(njs_vm_t *vm, const char *syscall, const char *description,
    njs_value_t *path, int errn, njs_value_t *retval)
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

    ret = njs_string_new(vm, &value, (u_char *) description, size, size);
    if (njs_slow_path(ret != NJS_OK)) {
        return NJS_ERROR;
    }

    error = njs_error_alloc(vm, NJS_OBJ_TYPE_ERROR, NULL, &value);
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
        size = njs_strlen(code);

        ret = njs_string_new(vm, &value, (u_char *) code, size, size);
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
        ret = njs_value_property_set(vm, retval, njs_value_arg(&string_path),
                                     path);
        if (njs_slow_path(ret != NJS_OK)) {
            return NJS_ERROR;
        }
    }

    if (syscall != NULL) {
        size = njs_strlen(syscall);
        ret = njs_string_new(vm, &value, (u_char *) syscall, size, size);
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
};


const njs_object_init_t  njs_fs_object_init = {
    njs_fs_object_properties,
    njs_nitems(njs_fs_object_properties),
};
