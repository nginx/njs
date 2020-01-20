
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


typedef struct {
    njs_str_t   name;
    int         value;
} njs_fs_entry_t;


typedef struct {
    int         errn;
    const char  *desc;
    const char  *syscall;
} njs_fs_ioerror_t;


static njs_int_t njs_fs_read_file(njs_vm_t *vm, njs_value_t *args,
    njs_uint_t nargs, njs_index_t calltype);
static njs_int_t njs_fs_write_file(njs_vm_t *vm, njs_value_t *args,
    njs_uint_t nargs, njs_index_t magic);
static njs_int_t njs_fs_rename_sync(njs_vm_t *vm, njs_value_t *args,
    njs_uint_t nargs, njs_index_t unused);

static njs_int_t njs_fs_fd_read(njs_vm_t *vm, int fd, njs_str_t *data,
    njs_fs_ioerror_t *ioerror);
static njs_int_t njs_fs_error(njs_vm_t *vm, const char *syscall,
    const char *description, njs_value_t *path, int errn, njs_value_t *retval);
static int njs_fs_flags(njs_str_t *value);
static mode_t njs_fs_mode(njs_value_t *value);
static njs_int_t njs_fs_add_event(njs_vm_t *vm, const njs_value_t *callback,
    const njs_value_t *err, const njs_value_t *result);


static const njs_value_t  njs_fs_errno_string = njs_string("errno");
static const njs_value_t  njs_fs_path_string = njs_string("path");
static const njs_value_t  njs_fs_syscall_string = njs_string("syscall");


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


njs_inline void
njs_fs_set_ioerr(njs_fs_ioerror_t *ioerror, int errn, const char *desc,
    const char *syscall)
{
    ioerror->errn = errn;
    ioerror->desc = desc;
    ioerror->syscall = syscall;
}


static njs_int_t
njs_fs_read_file(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t calltype)
{
    int                 fd, flags;
    u_char              *start;
    size_t              size;
    ssize_t             length;
    njs_str_t           flag, encoding, data;
    njs_int_t           ret;
    const char          *path;
    struct stat         sb;
    const njs_value_t   *callback, *options;
    njs_value_t         err;
    njs_object_prop_t   *prop;
    njs_lvlhsh_query_t  lhq;
    njs_fs_ioerror_t    ioerror;

    ret = njs_fs_path_arg(vm, &path, njs_arg(args, nargs, 1),
                          &njs_str_value("path"));
    if (njs_slow_path(ret != NJS_OK)) {
        return ret;
    }

    options = njs_arg(args, nargs, 2);

    if (njs_slow_path(calltype == NJS_FS_CALLBACK)) {
        callback = njs_arg(args, nargs, njs_min(nargs - 1, 3));
        if (!njs_is_function(callback)) {
            njs_type_error(vm, "\"callback\" must be a function");
            return NJS_ERROR;
        }
        if (options == callback) {
            options = &njs_value_undefined;
        }

    } else {
        /* GCC complains about uninitialized callback. */
        callback = NULL;
    }

    flag.start = NULL;
    encoding.length = 0;
    encoding.start = NULL;

    if (njs_slow_path(!njs_is_undefined(options))) {
        if (njs_is_string(&args[2])) {
            njs_string_get(&args[2], &encoding);

        } else if (njs_is_object(&args[2])) {
            lhq.key_hash = NJS_FLAG_HASH;
            lhq.key = njs_str_value("flag");
            lhq.proto = &njs_object_hash_proto;

            ret = njs_lvlhsh_find(njs_object_hash(&args[2]), &lhq);
            if (ret == NJS_OK) {
                prop = lhq.value;
                njs_string_get(&prop->value, &flag);
            }

            lhq.key_hash = NJS_ENCODING_HASH;
            lhq.key = njs_str_value("encoding");
            lhq.proto = &njs_object_hash_proto;

            ret = njs_lvlhsh_find(njs_object_hash(&args[2]), &lhq);
            if (ret == NJS_OK) {
                prop = lhq.value;
                njs_string_get(&prop->value, &encoding);
            }

        } else {
            njs_type_error(vm, "Unknown options type "
                           "(a string or object required)");
            return NJS_ERROR;
        }
    }

    if (flag.start == NULL) {
        flag = njs_str_value("r");
    }

    flags = njs_fs_flags(&flag);
    if (njs_slow_path(flags == -1)) {
        njs_type_error(vm, "Unknown file open flags: \"%V\"", &flag);
        return NJS_ERROR;
    }

    if (encoding.length != 0
        && (encoding.length != 4 || memcmp(encoding.start, "utf8", 4) != 0))
    {
        njs_type_error(vm, "Unknown encoding: \"%V\"", &encoding);
        return NJS_ERROR;
    }

    njs_fs_set_ioerr(&ioerror, 0, NULL, NULL);

    fd = open(path, flags);
    if (njs_slow_path(fd < 0)) {
        njs_fs_set_ioerr(&ioerror, errno, strerror(errno), "open");
        goto done;
    }

    ret = fstat(fd, &sb);
    if (njs_slow_path(ret == -1)) {
        njs_fs_set_ioerr(&ioerror, errno, strerror(errno), "fstat");
        goto done;
    }

    if (njs_slow_path(!S_ISREG(sb.st_mode))) {
        njs_fs_set_ioerr(&ioerror, 0, "File is not regular", "stat");
        goto done;
    }

    if (encoding.length != 0) {
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
        start = njs_string_alloc(vm, &vm->retval, size, length);
        if (njs_slow_path(start == NULL)) {
            goto fail;
        }

        data.start = start;
        data.length = size;

        ret = njs_fs_fd_read(vm, fd, &data, &ioerror);
        if (ret != NJS_OK) {
            if (ioerror.desc != NULL) {
                goto done;
            }

            goto fail;
        }

        start = data.start;

    } else {
        /* size of the file is not known in advance. */

        data.length = 0;

        ret = njs_fs_fd_read(vm, fd, &data, &ioerror);
        if (ret != NJS_OK) {
            if (ioerror.desc != NULL) {
                goto done;
            }

            goto fail;
        }

        size = data.length;
        start = data.start;

        ret = njs_string_new(vm, &vm->retval, start, size, length);
        if (njs_slow_path(ret != NJS_OK)) {
            goto fail;
        }
    }

    if (encoding.length != 0) {
        length = njs_utf8_length(start, size);

        if (length >= 0) {
            njs_string_length_set(&vm->retval, length);

        } else {
            njs_fs_set_ioerr(&ioerror, 0,
                             "Non-UTF8 file, convertion is not implemented",
                             NULL);
            goto done;
        }
    }

done:

    if (fd != -1) {
        (void) close(fd);
    }

    if (njs_fast_path(calltype == NJS_FS_DIRECT)) {
        if (njs_slow_path(ioerror.desc != NULL)) {
            (void) njs_fs_error(vm, ioerror.syscall, ioerror.desc,
                                &args[1], ioerror.errn, &vm->retval);
            return NJS_ERROR;
        }

        return NJS_OK;
    }

    if (calltype == NJS_FS_PROMISE) {
        njs_internal_error(vm, "promise callback is not implemented");
        return NJS_ERROR;
    }

    if (calltype == NJS_FS_CALLBACK) {
        if (njs_slow_path(ioerror.desc)) {
            ret = njs_fs_error(vm, ioerror.syscall, ioerror.desc,
                               &args[1], ioerror.errn, &err);
            if (njs_slow_path(ret != NJS_OK)) {
                return ret;
            }

            njs_set_undefined(&vm->retval);

        } else {
            njs_set_undefined(&err);
        }

        ret = njs_fs_add_event(vm, callback, &err, &vm->retval);
        if (njs_slow_path(ret != NJS_OK)) {
            return ret;
        }

        njs_set_undefined(&vm->retval);
        return NJS_OK;
    }

    njs_internal_error(vm, "invalid calltype");
    return NJS_ERROR;

fail:

    if (fd != -1) {
        (void) close(fd);
    }

    return NJS_ERROR;
}


static njs_int_t
njs_fs_write_file(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t magic)
{
    int                 fd, flags;
    u_char              *p, *end;
    mode_t              md;
    ssize_t             n;
    njs_str_t           data, flag, encoding;
    njs_int_t           ret;
    const char          *path;
    njs_value_t         *mode, err;
    njs_fs_calltype_t   calltype;
    const njs_value_t   *callback, *options;
    njs_fs_ioerror_t    ioerror;
    njs_object_prop_t   *prop;
    njs_lvlhsh_query_t  lhq;

    ret = njs_fs_path_arg(vm, &path, njs_arg(args, nargs, 1),
                          &njs_str_value("path"));
    if (njs_slow_path(ret != NJS_OK)) {
        return ret;
    }

    if (njs_slow_path(!njs_is_string(njs_arg(args, nargs, 2)))) {
        njs_type_error(vm, "\"data\" must be a string");
        return NJS_ERROR;
    }

    callback = NULL;
    calltype = magic & 3;
    options = njs_arg(args, nargs, 3);

    if (njs_slow_path(calltype == NJS_FS_CALLBACK)) {
        callback = njs_arg(args, nargs, njs_min(nargs - 1, 4));
        if (!njs_is_function(callback)) {
            njs_type_error(vm, "\"callback\" must be a function");
            return NJS_ERROR;
        }
        if (options == callback) {
            options = &njs_value_undefined;
        }
    }

    mode = NULL;
    /* GCC complains about uninitialized flag.length. */
    flag.length = 0;
    flag.start = NULL;
    encoding.length = 0;
    encoding.start = NULL;

    if (njs_slow_path(!njs_is_undefined(options))) {
        if (njs_is_string(&args[3])) {
            njs_string_get(&args[3], &encoding);

        } else if (njs_is_object(&args[3])) {
            lhq.key_hash = NJS_FLAG_HASH;
            lhq.key = njs_str_value("flag");
            lhq.proto = &njs_object_hash_proto;

            ret = njs_lvlhsh_find(njs_object_hash(&args[3]), &lhq);
            if (ret == NJS_OK) {
                prop = lhq.value;
                njs_string_get(&prop->value, &flag);
            }

            lhq.key_hash = NJS_ENCODING_HASH;
            lhq.key = njs_str_value("encoding");
            lhq.proto = &njs_object_hash_proto;

            ret = njs_lvlhsh_find(njs_object_hash(&args[3]), &lhq);
            if (ret == NJS_OK) {
                prop = lhq.value;
                njs_string_get(&prop->value, &encoding);
            }

            lhq.key_hash = NJS_MODE_HASH;
            lhq.key = njs_str_value("mode");
            lhq.proto = &njs_object_hash_proto;

            ret = njs_lvlhsh_find(njs_object_hash(&args[3]), &lhq);
            if (ret == NJS_OK) {
                prop = lhq.value;
                mode = &prop->value;
            }

        } else {
            njs_type_error(vm, "Unknown options type "
                           "(a string or object required)");
            return NJS_ERROR;
        }
    }

    if (flag.start != NULL) {
        flags = njs_fs_flags(&flag);
        if (njs_slow_path(flags == -1)) {
            njs_type_error(vm, "Unknown file open flags: \"%V\"", &flag);
            return NJS_ERROR;
        }

    } else {
        flags = O_CREAT | O_WRONLY;
        flags |= ((magic >> 2) == NJS_FS_APPEND) ? O_APPEND : O_TRUNC;
    }

    if (mode != NULL) {
        md = njs_fs_mode(mode);

    } else {
        md = 0666;
    }

    if (encoding.length != 0
        && (encoding.length != 4 || memcmp(encoding.start, "utf8", 4) != 0))
    {
        njs_type_error(vm, "Unknown encoding: \"%V\"", &encoding);
        return NJS_ERROR;
    }

    njs_fs_set_ioerr(&ioerror, 0, NULL, NULL);

    fd = open(path, flags, md);
    if (njs_slow_path(fd < 0)) {
        njs_fs_set_ioerr(&ioerror, errno, strerror(errno), "open");
        goto done;
    }

    njs_string_get(&args[2], &data);

    p = data.start;
    end = p + data.length;

    while (p < end) {
        n = write(fd, p, end - p);
        if (njs_slow_path(n == -1)) {
            if (errno == EINTR) {
                continue;
            }

            njs_fs_set_ioerr(&ioerror, errno, strerror(errno), "write");
            goto done;
        }

        p += n;
    }

done:

    if (fd != -1) {
        (void) close(fd);
    }

    if (njs_fast_path(calltype == NJS_FS_DIRECT)) {
        if (njs_slow_path(ioerror.desc != NULL)) {
            (void) njs_fs_error(vm, ioerror.syscall, ioerror.desc, &args[1],
                                ioerror.errn, &vm->retval);
            return NJS_ERROR;
        }

        njs_set_undefined(&vm->retval);
        return NJS_OK;
    }

    if (calltype == NJS_FS_PROMISE) {
        njs_internal_error(vm, "not implemented");
        return NJS_ERROR;
    }

    if (calltype == NJS_FS_CALLBACK) {
        if (ioerror.desc != NULL) {
            ret = njs_fs_error(vm, ioerror.syscall, ioerror.desc, &args[1],
                               ioerror.errn, &err);
            if (njs_slow_path(ret != NJS_OK)) {
                return ret;
            }

        } else {
            njs_set_undefined(&err);
        }

        ret = njs_fs_add_event(vm, callback, &err, NULL);
        if (njs_slow_path(ret != NJS_OK)) {
            return ret;
        }

        njs_set_undefined(&vm->retval);
        return NJS_OK;
    }

    njs_internal_error(vm, "invalid calltype");
    return NJS_ERROR;
}


static njs_int_t
njs_fs_rename_sync(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t unused)
{
    njs_int_t   ret;
    const char  *old_path, *new_path;

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

    ret = rename(old_path, new_path);
    if (njs_slow_path(ret != 0)) {
        (void) njs_fs_error(vm, "rename", strerror(errno), NULL, errno,
                            &vm->retval);
        return NJS_ERROR;
    }

    njs_set_undefined(&vm->retval);

    return NJS_OK;
}


static njs_int_t
njs_fs_fd_read(njs_vm_t *vm, int fd, njs_str_t *data,
    njs_fs_ioerror_t *ioerror)
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
            njs_fs_set_ioerr(ioerror, errno, strerror(errno), "read");
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
njs_fs_error(njs_vm_t *vm, const char *syscall, const char *description,
    njs_value_t *path, int errn, njs_value_t *retval)
{
    size_t              size;
    njs_int_t           ret;
    njs_value_t         string, value;
    njs_object_t        *error;
    njs_object_prop_t   *prop;
    njs_lvlhsh_query_t  lhq;

    size = description != NULL ? njs_strlen(description) : 0;

    ret = njs_string_new(vm, &string, (u_char *) description, size, size);
    if (njs_slow_path(ret != NJS_OK)) {
        return NJS_ERROR;
    }

    error = njs_error_alloc(vm, NJS_OBJ_TYPE_ERROR, NULL, &string);
    if (njs_slow_path(error == NULL)) {
        return NJS_ERROR;
    }

    lhq.replace = 0;
    lhq.pool = vm->mem_pool;

    if (errn != 0) {
        lhq.key = njs_str_value("errno");
        lhq.key_hash = NJS_ERRNO_HASH;
        lhq.proto = &njs_object_hash_proto;

        njs_set_number(&value, errn);

        prop = njs_object_prop_alloc(vm, &njs_fs_errno_string, &value, 1);
        if (njs_slow_path(prop == NULL)) {
            return NJS_ERROR;
        }

        lhq.value = prop;

        ret = njs_lvlhsh_insert(&error->hash, &lhq);
        if (njs_slow_path(ret != NJS_OK)) {
            njs_internal_error(vm, "lvlhsh insert failed");
            return NJS_ERROR;
        }
    }

    if (path != NULL) {
        lhq.key = njs_str_value("path");
        lhq.key_hash = NJS_PATH_HASH;
        lhq.proto = &njs_object_hash_proto;

        prop = njs_object_prop_alloc(vm, &njs_fs_path_string, path, 1);
        if (njs_slow_path(prop == NULL)) {
            return NJS_ERROR;
        }

        lhq.value = prop;

        ret = njs_lvlhsh_insert(&error->hash, &lhq);
        if (njs_slow_path(ret != NJS_OK)) {
            njs_internal_error(vm, "lvlhsh insert failed");
            return NJS_ERROR;
        }
    }

    if (syscall != NULL) {
        size = njs_strlen(syscall);
        ret = njs_string_new(vm, &string, (u_char *) syscall, size, size);
        if (njs_slow_path(ret != NJS_OK)) {
            return NJS_ERROR;
        }

        lhq.key = njs_str_value("sycall");
        lhq.key_hash = NJS_SYSCALL_HASH;
        lhq.proto = &njs_object_hash_proto;

        prop = njs_object_prop_alloc(vm, &njs_fs_syscall_string, &string, 1);
        if (njs_slow_path(prop == NULL)) {
            return NJS_ERROR;
        }

        lhq.value = prop;

        ret = njs_lvlhsh_insert(&error->hash, &lhq);
        if (njs_slow_path(ret != NJS_OK)) {
            njs_internal_error(vm, "lvlhsh insert failed");
            return NJS_ERROR;
        }
    }

    njs_set_object(retval, error);

    return NJS_OK;
}


static int
njs_fs_flags(njs_str_t *value)
{
    njs_fs_entry_t  *fl;

    for (fl = &njs_flags_table[0]; fl->name.length != 0; fl++) {
        if (njs_strstr_eq(value, &fl->name)) {
            return fl->value;
        }
    }

    return -1;
}


static mode_t
njs_fs_mode(njs_value_t *value)
{
    switch (value->type) {
    case NJS_OBJECT_NUMBER:
        value = njs_object_value(value);
        /* Fall through. */

    case NJS_NUMBER:
        return (mode_t) njs_number(value);

    case NJS_OBJECT_STRING:
    value = njs_object_value(value);
        /* Fall through. */

    case NJS_STRING:
        return (mode_t) njs_string_to_number(value, 0);

    default:
        return (mode_t) 0;
    }
}


static njs_int_t
njs_fs_add_event(njs_vm_t *vm, const njs_value_t *callback,
    const njs_value_t *err, const njs_value_t *result)
{
    njs_int_t     nargs;
    njs_event_t   *event;
    njs_vm_ops_t  *ops;

    nargs = (result == NULL) ? 1 : 2;

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

    /* GC: retain */
    event->args[0] = *err;

    if (nargs == 2) {
        event->args[1] = *result;
    }

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


static const njs_object_prop_t  njs_fs_object_properties[] =
{
    {
        .type = NJS_PROPERTY,
        .name = njs_string("name"),
        .value = njs_string("fs"),
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
        .name = njs_string("renameSync"),
        .value = njs_native_function(njs_fs_rename_sync, 0),
        .writable = 1,
        .configurable = 1,
    },

};


const njs_object_init_t  njs_fs_object_init = {
    njs_fs_object_properties,
    njs_nitems(njs_fs_object_properties),
};
