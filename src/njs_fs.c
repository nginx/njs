
/*
 * Copyright (C) Dmitry Volyntsev
 * Copyright (C) NGINX, Inc.
 */


#include <njs_main.h>


typedef struct {
    njs_str_t               name;
    int                     value;
} njs_fs_entry_t;


static njs_int_t njs_fs_read_file(njs_vm_t *vm, njs_value_t *args,
    njs_uint_t nargs, njs_index_t unused);
static njs_int_t njs_fs_read_file_sync(njs_vm_t *vm, njs_value_t *args,
    njs_uint_t nargs, njs_index_t unused);
static njs_int_t njs_fs_append_file(njs_vm_t *vm, njs_value_t *args,
    njs_uint_t nargs, njs_index_t unused);
static njs_int_t njs_fs_write_file(njs_vm_t *vm, njs_value_t *args,
    njs_uint_t nargs, njs_index_t unused);
static njs_int_t njs_fs_append_file_sync(njs_vm_t *vm, njs_value_t *args,
    njs_uint_t nargs, njs_index_t unused);
static njs_int_t njs_fs_write_file_sync(njs_vm_t *vm, njs_value_t *args,
    njs_uint_t nargs, njs_index_t unused);
static njs_int_t njs_fs_write_file_internal(njs_vm_t *vm, njs_value_t *args,
    njs_uint_t nargs, int default_flags);
static njs_int_t njs_fs_write_file_sync_internal(njs_vm_t *vm,
    njs_value_t *args, njs_uint_t nargs, int default_flags);
static njs_int_t njs_fs_rename_sync(njs_vm_t *vm, njs_value_t *args,
    njs_uint_t nargs, njs_index_t unused);

static njs_int_t njs_fs_fd_read(njs_vm_t *vm, njs_value_t *path, int fd,
    njs_str_t *data);
static njs_int_t njs_fs_error(njs_vm_t *vm, const char *syscall,
    const char *description, njs_value_t *path, int errn, njs_value_t *retval);
static int njs_fs_flags(njs_str_t *value);
static mode_t njs_fs_mode(njs_value_t *value);


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


static njs_int_t
njs_fs_read_file(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t unused)
{
    int                 fd, errn, flags;
    u_char              *start;
    size_t              size;
    ssize_t             length;
    njs_str_t           flag, encoding, data;
    njs_int_t           ret;
    const char          *path, *syscall, *description;
    struct stat         sb;
    njs_value_t         *callback, arguments[3];
    njs_object_prop_t   *prop;
    njs_lvlhsh_query_t  lhq;

    if (njs_slow_path(nargs < 3)) {
        njs_type_error(vm, "too few arguments");
        return NJS_ERROR;
    }

    if (njs_slow_path(!njs_is_string(&args[1]))) {
        njs_type_error(vm, "path must be a string");
        return NJS_ERROR;
    }

    flag.start = NULL;
    encoding.length = 0;
    encoding.start = NULL;

    if (!njs_is_function(&args[2])) {
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

        if (njs_slow_path(nargs < 4 || !njs_is_function(&args[3]))) {
            njs_type_error(vm, "callback must be a function");
            return NJS_ERROR;
        }

        callback = &args[3];

    } else {
        if (njs_slow_path(!njs_is_function(&args[2]))) {
            njs_type_error(vm, "callback must be a function");
            return NJS_ERROR;
        }

        callback = &args[2];
    }

    if (flag.start == NULL) {
        flag = njs_str_value("r");
    }

    flags = njs_fs_flags(&flag);
    if (njs_slow_path(flags == -1)) {
        njs_type_error(vm, "Unknown file open flags: \"%V\"", &flag);
        return NJS_ERROR;
    }

    path = njs_string_to_c_string(vm, &args[1]);
    if (njs_slow_path(path == NULL)) {
        return NJS_ERROR;
    }

    if (encoding.length != 0
        && (encoding.length != 4 || memcmp(encoding.start, "utf8", 4) != 0))
    {
        njs_type_error(vm, "Unknown encoding: \"%V\"", &encoding);
        return NJS_ERROR;
    }

    description = NULL;

    /* GCC 4 complains about uninitialized errn and syscall. */
    errn = 0;
    syscall = NULL;

    fd = open(path, flags);
    if (njs_slow_path(fd < 0)) {
        errn = errno;
        description = strerror(errno);
        syscall = "open";
        goto done;
    }

    ret = fstat(fd, &sb);
    if (njs_slow_path(ret == -1)) {
        errn = errno;
        description = strerror(errno);
        syscall = "stat";
        goto done;
    }

    if (njs_slow_path(!S_ISREG(sb.st_mode))) {
        errn = 0;
        description = "File is not regular";
        syscall = "stat";
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

        start = njs_string_alloc(vm, &arguments[2], size, length);
        if (njs_slow_path(start == NULL)) {
            goto fail;
        }

        data.start = start;
        data.length = size;

        ret = njs_fs_fd_read(vm, &args[1], fd, &data);
        if (ret != NJS_OK) {
            goto fail;
        }

        start = data.start;

    } else {
        /* size of the file is not known in advance. */

        data.length = 0;

        ret = njs_fs_fd_read(vm, &args[1], fd, &data);
        if (ret != NJS_OK) {
            goto fail;
        }

        size = data.length;
        start = data.start;

        ret = njs_string_new(vm, &arguments[2], start, size, length);
        if (njs_slow_path(ret != NJS_OK)) {
            goto fail;
        }
    }

    if (encoding.length != 0) {
        length = njs_utf8_length(start, size);

        if (length >= 0) {
            njs_string_length_set(&arguments[2], length);

        } else {
            errn = 0;
            description = "Non-UTF8 file, convertion is not implemented";
            syscall = NULL;
            goto done;
        }
    }

done:

    if (fd != -1) {
        (void) close(fd);
    }

    if (description != 0) {
        (void) njs_fs_error(vm, syscall, description, &args[1], errn,
                            &arguments[1]);

        njs_set_undefined(&arguments[2]);

    } else {
        njs_set_undefined(&arguments[1]);
    }

    njs_set_undefined(&arguments[0]);

    ret = njs_function_apply(vm, njs_function(callback), arguments, 3,
                             &vm->retval);

    if (njs_slow_path(ret != NJS_OK)) {
        return ret;
    }

    njs_set_undefined(&vm->retval);

    return NJS_OK;

fail:

    if (fd != -1) {
        (void) close(fd);
    }

    return NJS_ERROR;
}


static njs_int_t
njs_fs_read_file_sync(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t unused)
{
    int                 fd, errn, flags;
    u_char              *start;
    size_t              size;
    ssize_t             length;
    njs_str_t           flag, encoding, data;
    njs_int_t           ret;
    const char          *path, *syscall, *description;
    struct stat         sb;
    njs_object_prop_t   *prop;
    njs_lvlhsh_query_t  lhq;

    if (njs_slow_path(nargs < 2)) {
        njs_type_error(vm, "too few arguments");
        return NJS_ERROR;
    }

    if (njs_slow_path(!njs_is_string(&args[1]))) {
        njs_type_error(vm, "path must be a string");
        return NJS_ERROR;
    }

    flag.start = NULL;
    encoding.length = 0;
    encoding.start = NULL;

    if (nargs == 3) {
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

    path = njs_string_to_c_string(vm, &args[1]);
    if (njs_slow_path(path == NULL)) {
        return NJS_ERROR;
    }

    if (encoding.length != 0
        && (encoding.length != 4 || memcmp(encoding.start, "utf8", 4) != 0))
    {
        njs_type_error(vm, "Unknown encoding: \"%V\"", &encoding);
        return NJS_ERROR;
    }

    description = NULL;

    /* GCC 4 complains about uninitialized errn and syscall. */
    errn = 0;
    syscall = NULL;

    fd = open(path, flags);
    if (njs_slow_path(fd < 0)) {
        errn = errno;
        description = strerror(errno);
        syscall = "open";
        goto done;
    }

    ret = fstat(fd, &sb);
    if (njs_slow_path(ret == -1)) {
        errn = errno;
        description = strerror(errno);
        syscall = "stat";
        goto done;
    }

    if (njs_slow_path(!S_ISREG(sb.st_mode))) {
        errn = 0;
        description = "File is not regular";
        syscall = "stat";
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

        ret = njs_fs_fd_read(vm, &args[1], fd, &data);
        if (ret != NJS_OK) {
            goto fail;
        }

        start = data.start;

    } else {
        /* size of the file is not known in advance. */

        data.length = 0;

        ret = njs_fs_fd_read(vm, &args[1], fd, &data);
        if (ret != NJS_OK) {
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
            errn = 0;
            description = "Non-UTF8 file, convertion is not implemented";
            syscall = NULL;
            goto done;
        }
    }

done:

    if (fd != -1) {
        (void) close(fd);
    }

    if (description != 0) {
        return njs_fs_error(vm, syscall, description, &args[1], errn,
                            &vm->retval);
    }

    return NJS_OK;

fail:

    if (fd != -1) {
        (void) close(fd);
    }

    return NJS_ERROR;
}


static njs_int_t
njs_fs_append_file(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t unused)
{
    return njs_fs_write_file_internal(vm, args, nargs,
                                      O_APPEND | O_CREAT | O_WRONLY);
}


static njs_int_t
njs_fs_write_file(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t unused)
{
    return njs_fs_write_file_internal(vm, args, nargs,
                                      O_TRUNC | O_CREAT | O_WRONLY);
}


static njs_int_t njs_fs_append_file_sync(njs_vm_t *vm, njs_value_t *args,
    njs_uint_t nargs, njs_index_t unused)
{
    return njs_fs_write_file_sync_internal(vm, args, nargs,
                                           O_APPEND | O_CREAT | O_WRONLY);
}


static njs_int_t njs_fs_write_file_sync(njs_vm_t *vm, njs_value_t *args,
    njs_uint_t nargs, njs_index_t unused)
{
    return njs_fs_write_file_sync_internal(vm, args, nargs,
                                           O_TRUNC | O_CREAT | O_WRONLY);
}


static njs_int_t njs_fs_write_file_internal(njs_vm_t *vm, njs_value_t *args,
    njs_uint_t nargs, int default_flags)
{
    int                 fd, errn, flags;
    u_char              *p, *end;
    mode_t              md;
    ssize_t             n;
    njs_str_t           data, flag, encoding;
    njs_int_t           ret;
    const char          *path, *syscall, *description;
    njs_value_t         *callback, *mode, arguments[2];
    njs_object_prop_t   *prop;
    njs_lvlhsh_query_t  lhq;

    if (njs_slow_path(nargs < 4)) {
        njs_type_error(vm, "too few arguments");
        return NJS_ERROR;
    }

    if (njs_slow_path(!njs_is_string(&args[1]))) {
        njs_type_error(vm, "path must be a string");
        return NJS_ERROR;
    }

    if (njs_slow_path(!njs_is_string(&args[2]))) {
        njs_type_error(vm, "data must be a string");
        return NJS_ERROR;
    }

    mode = NULL;
    /* GCC complains about uninitialized flag.length. */
    flag.length = 0;
    flag.start = NULL;
    encoding.length = 0;
    encoding.start = NULL;

    if (!njs_is_function(&args[3])) {
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

        if (njs_slow_path(nargs < 5 || !njs_is_function(&args[4]))) {
            njs_type_error(vm, "callback must be a function");
            return NJS_ERROR;
        }

        callback = &args[4];

    } else {
        if (njs_slow_path(!njs_is_function(&args[3]))) {
            njs_type_error(vm, "callback must be a function");
            return NJS_ERROR;
        }

        callback = &args[3];
    }

    if (flag.start != NULL) {
        flags = njs_fs_flags(&flag);
        if (njs_slow_path(flags == -1)) {
            njs_type_error(vm, "Unknown file open flags: \"%V\"", &flag);
            return NJS_ERROR;
        }

    } else {
        flags = default_flags;
    }

    if (mode != NULL) {
        md = njs_fs_mode(mode);

    } else {
        md = 0666;
    }

    path = njs_string_to_c_string(vm, &args[1]);
    if (njs_slow_path(path == NULL)) {
        return NJS_ERROR;
    }

    if (encoding.length != 0
        && (encoding.length != 4 || memcmp(encoding.start, "utf8", 4) != 0))
    {
        njs_type_error(vm, "Unknown encoding: \"%V\"", &encoding);
        return NJS_ERROR;
    }

    description = NULL;

    /* GCC 4 complains about uninitialized errn and syscall. */
    errn = 0;
    syscall = NULL;

    fd = open(path, flags, md);
    if (njs_slow_path(fd < 0)) {
        errn = errno;
        description = strerror(errno);
        syscall = "open";
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

            errn = errno;
            description = strerror(errno);
            syscall = "write";
            goto done;
        }

        p += n;
    }

done:

    if (fd != -1) {
        (void) close(fd);
    }

    if (description != 0) {
        (void) njs_fs_error(vm, syscall, description, &args[1], errn,
                            &arguments[1]);

    } else {
        njs_set_undefined(&arguments[1]);
    }

    njs_set_undefined(&arguments[0]);

    ret = njs_function_apply(vm, njs_function(callback), arguments, 2,
                             &vm->retval);

    if (njs_slow_path(ret != NJS_OK)) {
        return ret;
    }

    njs_set_undefined(&vm->retval);

    return NJS_OK;
}


static njs_int_t
njs_fs_write_file_sync_internal(njs_vm_t *vm, njs_value_t *args,
    njs_uint_t nargs, int default_flags)
{
    int                 fd, errn, flags;
    u_char              *p, *end;
    mode_t              md;
    ssize_t             n;
    njs_str_t           data, flag, encoding;
    njs_int_t           ret;
    const char          *path, *syscall, *description;
    njs_value_t         *mode;
    njs_object_prop_t   *prop;
    njs_lvlhsh_query_t  lhq;

    if (njs_slow_path(nargs < 3)) {
        njs_type_error(vm, "too few arguments");
        return NJS_ERROR;
    }

    if (njs_slow_path(!njs_is_string(&args[1]))) {
        njs_type_error(vm, "path must be a string");
        return NJS_ERROR;
    }

    if (njs_slow_path(!njs_is_string(&args[2]))) {
        njs_type_error(vm, "data must be a string");
        return NJS_ERROR;
    }

    mode = NULL;
    /* GCC complains about uninitialized flag.length. */
    flag.length = 0;
    flag.start = NULL;
    encoding.length = 0;
    encoding.start = NULL;

    if (nargs == 4) {
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
        flags = default_flags;
    }

    if (mode != NULL) {
        md = njs_fs_mode(mode);

    } else {
        md = 0666;
    }

    path = njs_string_to_c_string(vm, &args[1]);
    if (njs_slow_path(path == NULL)) {
        return NJS_ERROR;
    }

    if (encoding.length != 0
        && (encoding.length != 4 || memcmp(encoding.start, "utf8", 4) != 0))
    {
        njs_type_error(vm, "Unknown encoding: \"%V\"", &encoding);
        return NJS_ERROR;
    }

    description = NULL;

    /* GCC 4 complains about uninitialized errn and syscall. */
    errn = 0;
    syscall = NULL;

    fd = open(path, flags, md);
    if (njs_slow_path(fd < 0)) {
        errn = errno;
        description = strerror(errno);
        syscall = "open";
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

            errn = errno;
            description = strerror(errno);
            syscall = "write";
            goto done;
        }

        p += n;
    }

done:

    if (fd != -1) {
        (void) close(fd);
    }

    if (description != 0) {
        return njs_fs_error(vm, syscall, description, &args[1], errn,
                            &vm->retval);

    } else {
        njs_set_undefined(&vm->retval);
    }

    return NJS_OK;
}


static njs_int_t
njs_fs_rename_sync(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t unused)
{
    int          ret;
    const char   *old_path, *new_path;
    njs_value_t  *old, *new;

    if (njs_slow_path(nargs < 3)) {
        if (nargs < 2) {
            njs_type_error(vm, "oldPath must be a string");
            return NJS_ERROR;
        }

        njs_type_error(vm, "newPath must be a string");
        return NJS_ERROR;
    }

    old = njs_argument(args, 1);
    new = njs_argument(args, 2);

    if (njs_slow_path(!njs_is_string(old))) {
        ret = njs_value_to_string(vm, old, old);
        if (njs_slow_path(ret != NJS_OK)) {
            return ret;
        }
    }

    if (njs_slow_path(!njs_is_string(new))) {
        ret = njs_value_to_string(vm, new, new);
        if (njs_slow_path(ret != NJS_OK)) {
            return ret;
        }
    }

    old_path = njs_string_to_c_string(vm, old);
    if (njs_slow_path(old_path == NULL)) {
        return NJS_ERROR;
    }

    new_path = njs_string_to_c_string(vm, new);
    if (njs_slow_path(new_path == NULL)) {
        return NJS_ERROR;
    }

    ret = rename(old_path, new_path);
    if (njs_slow_path(ret != 0)) {
        return njs_fs_error(vm, "rename", strerror(errno), NULL, errno,
                            &vm->retval);
    }

    njs_set_undefined(&vm->retval);

    return NJS_OK;
}


static njs_int_t
njs_fs_fd_read(njs_vm_t *vm, njs_value_t *path, int fd, njs_str_t *data)
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
            return njs_fs_error(vm, "read", strerror(errno), path, errno,
                                &vm->retval);
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

    return NJS_ERROR;
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
        .value = njs_native_function(njs_fs_read_file, 0),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("readFileSync"),
        .value = njs_native_function(njs_fs_read_file_sync, 0),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("appendFile"),
        .value = njs_native_function(njs_fs_append_file, 0),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("appendFileSync"),
        .value = njs_native_function(njs_fs_append_file_sync, 0),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("writeFile"),
        .value = njs_native_function(njs_fs_write_file, 0),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("writeFileSync"),
        .value = njs_native_function(njs_fs_write_file_sync, 0),
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
