
/*
 * Copyright (C) Dmitry Volyntsev
 * Copyright (C) NGINX, Inc.
 */

#include <njs_core.h>
#include <njs_fs.h>
#include <string.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <errno.h>
#include <stdio.h>


typedef struct {
    union {
        njs_continuation_t  cont;
        u_char              padding[NJS_CONTINUATION_SIZE];
    } u;

    nxt_bool_t              done;
} njs_fs_cont_t;


typedef struct {
    nxt_str_t               name;
    int                     value;
} njs_fs_entry_t;


static njs_ret_t njs_fs_read_file(njs_vm_t *vm, njs_value_t *args,
    nxt_uint_t nargs, njs_index_t unused);
static njs_ret_t njs_fs_read_file_sync(njs_vm_t *vm, njs_value_t *args,
    nxt_uint_t nargs, njs_index_t unused);
static njs_ret_t njs_fs_append_file(njs_vm_t *vm, njs_value_t *args,
    nxt_uint_t nargs, njs_index_t unused);
static njs_ret_t njs_fs_write_file(njs_vm_t *vm, njs_value_t *args,
    nxt_uint_t nargs, njs_index_t unused);
static njs_ret_t njs_fs_append_file_sync(njs_vm_t *vm, njs_value_t *args,
    nxt_uint_t nargs, njs_index_t unused);
static njs_ret_t njs_fs_write_file_sync(njs_vm_t *vm, njs_value_t *args,
    nxt_uint_t nargs, njs_index_t unused);
static njs_ret_t njs_fs_write_file_internal(njs_vm_t *vm, njs_value_t *args,
    nxt_uint_t nargs, int default_flags);
static njs_ret_t njs_fs_write_file_sync_internal(njs_vm_t *vm,
    njs_value_t *args, nxt_uint_t nargs, int default_flags);
static njs_ret_t njs_fs_done(njs_vm_t *vm, njs_value_t *args,
    nxt_uint_t nargs, njs_index_t unused);

static njs_ret_t njs_fs_error(njs_vm_t *vm, const char *syscall,
    const char *description, njs_value_t *path, int errn, njs_value_t *retval);
static int njs_fs_flags(nxt_str_t *value);
static mode_t njs_fs_mode(njs_value_t *value);


static const njs_value_t  njs_fs_errno_string = njs_string("errno");
static const njs_value_t  njs_fs_path_string = njs_string("path");
static const njs_value_t  njs_fs_syscall_string = njs_string("syscall");


static njs_fs_entry_t njs_flags_table[] = {
    { nxt_string("r"),   O_RDONLY },
    { nxt_string("r+"),  O_RDWR },
    { nxt_string("w"),   O_TRUNC  | O_CREAT  | O_WRONLY },
    { nxt_string("w+"),  O_TRUNC  | O_CREAT  | O_RDWR },
    { nxt_string("a"),   O_APPEND | O_CREAT  | O_WRONLY },
    { nxt_string("a+"),  O_APPEND | O_CREAT  | O_RDWR },
    { nxt_string("rs"),  O_SYNC   | O_RDONLY },
    { nxt_string("sr"),  O_SYNC   | O_RDONLY },
    { nxt_string("wx"),  O_TRUNC  | O_CREAT  | O_EXCL | O_WRONLY },
    { nxt_string("xw"),  O_TRUNC  | O_CREAT  | O_EXCL | O_WRONLY },
    { nxt_string("ax"),  O_APPEND | O_CREAT  | O_EXCL | O_WRONLY },
    { nxt_string("xa"),  O_APPEND | O_CREAT  | O_EXCL | O_WRONLY },
    { nxt_string("rs+"), O_SYNC   | O_RDWR },
    { nxt_string("sr+"), O_SYNC   | O_RDWR },
    { nxt_string("wx+"), O_TRUNC  | O_CREAT  | O_EXCL | O_RDWR },
    { nxt_string("xw+"), O_TRUNC  | O_CREAT  | O_EXCL | O_RDWR },
    { nxt_string("ax+"), O_APPEND | O_CREAT  | O_EXCL | O_RDWR },
    { nxt_string("xa+"), O_APPEND | O_CREAT  | O_EXCL | O_RDWR },
    { nxt_null_string, 0 }
};


static njs_ret_t
njs_fs_read_file(njs_vm_t *vm, njs_value_t *args, nxt_uint_t nargs,
    njs_index_t unused)
{
    int                 fd, errn, flags;
    u_char              *p, *start, *end;
    ssize_t             n, length;
    nxt_str_t           flag, encoding;
    njs_ret_t           ret;
    const char          *path, *syscall, *description;
    struct stat         sb;
    njs_value_t         *callback, arguments[3];
    njs_fs_cont_t       *cont;
    njs_object_prop_t   *prop;
    nxt_lvlhsh_query_t  lhq;

    if (nxt_slow_path(nargs < 3)) {
        njs_type_error(vm, "too few arguments");
        return NJS_ERROR;
    }

    if (nxt_slow_path(!njs_is_string(&args[1]))) {
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
            lhq.key = nxt_string_value("flag");
            lhq.proto = &njs_object_hash_proto;

            ret = nxt_lvlhsh_find(&args[2].data.u.object->hash, &lhq);
            if (ret == NXT_OK) {
                prop = lhq.value;
                njs_string_get(&prop->value, &flag);
            }

            lhq.key_hash = NJS_ENCODING_HASH;
            lhq.key = nxt_string_value("encoding");
            lhq.proto = &njs_object_hash_proto;

            ret = nxt_lvlhsh_find(&args[2].data.u.object->hash, &lhq);
            if (ret == NXT_OK) {
                prop = lhq.value;
                njs_string_get(&prop->value, &encoding);
            }

        } else {
            njs_type_error(vm, "Unknown options type "
                           "(a string or object required)");
            return NJS_ERROR;
        }

        if (nxt_slow_path(nargs < 4 || !njs_is_function(&args[3]))) {
            njs_type_error(vm, "callback must be a function");
            return NJS_ERROR;
        }

        callback = &args[3];

    } else {
        if (nxt_slow_path(!njs_is_function(&args[2]))) {
            njs_type_error(vm, "callback must be a function");
            return NJS_ERROR;
        }

        callback = &args[2];
    }

    if (flag.start == NULL) {
        flag = nxt_string_value("r");
    }

    flags = njs_fs_flags(&flag);
    if (nxt_slow_path(flags == -1)) {
        njs_type_error(vm, "Unknown file open flags: '%.*s'",
                       (int) flag.length, flag.start);
        return NJS_ERROR;
    }

    path = (char *) njs_string_to_c_string(vm, &args[1]);
    if (nxt_slow_path(path == NULL)) {
        return NJS_ERROR;
    }

    if (encoding.length != 0
        && (encoding.length != 4 || memcmp(encoding.start, "utf8", 4) != 0))
    {
        njs_type_error(vm, "Unknown encoding: '%.*s'",
                       (int) encoding.length, encoding.start);
        return NJS_ERROR;
    }

    description = NULL;

    /* GCC 4 complains about uninitialized errn and syscall. */
    errn = 0;
    syscall = NULL;

    fd = open(path, flags);
    if (nxt_slow_path(fd < 0)) {
        errn = errno;
        description = strerror(errno);
        syscall = "open";
        goto done;
    }

    ret = fstat(fd, &sb);
    if (nxt_slow_path(ret == -1)) {
        errn = errno;
        description = strerror(errno);
        syscall = "stat";
        goto done;
    }

    if (nxt_slow_path(!S_ISREG(sb.st_mode))) {
        errn = 0;
        description = "File is not regular";
        syscall = "stat";
        goto done;
    }

    if (encoding.length != 0) {
        length = sb.st_size;

    } else {
        length = 0;
    }

    start = njs_string_alloc(vm, &arguments[2], sb.st_size, length);
    if (nxt_slow_path(start == NULL)) {
        goto fail;
    }

    p = start;
    end = p + sb.st_size;

    while (p < end) {
        n = read(fd, p, end - p);
        if (nxt_slow_path(n == -1)) {
            if (errno == EINTR) {
                continue;
            }

            errn = errno;
            description = strerror(errno);
            syscall = "read";
            goto done;
        }

        p += n;
    }

    if (encoding.length != 0) {
        length = nxt_utf8_length(start, sb.st_size);

        if (length >= 0) {
            njs_string_offset_map_init(start, sb.st_size);
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
        ret = njs_fs_error(vm, syscall, description, &args[1], errn,
                           &arguments[1]);

        if (nxt_slow_path(ret != NJS_OK)) {
            return NJS_ERROR;
        }

        arguments[2] = njs_value_void;

    } else {
        arguments[1] = njs_value_void;
    }

    arguments[0] = njs_value_void;

    cont = njs_vm_continuation(vm);
    cont->u.cont.function = njs_fs_done;

    return njs_function_apply(vm, callback->data.u.function,
                              arguments, 3, (njs_index_t) &vm->retval);

fail:

    if (fd != -1) {
        (void) close(fd);
    }

    return NJS_ERROR;
}


static njs_ret_t
njs_fs_read_file_sync(njs_vm_t *vm, njs_value_t *args, nxt_uint_t nargs,
    njs_index_t unused)
{
    int                 fd, errn, flags;
    u_char              *p, *start, *end;
    ssize_t             n, length;
    nxt_str_t           flag, encoding;
    njs_ret_t           ret;
    const char          *path, *syscall, *description;
    struct stat         sb;
    njs_object_prop_t   *prop;
    nxt_lvlhsh_query_t  lhq;

    if (nxt_slow_path(nargs < 2)) {
        njs_type_error(vm, "too few arguments");
        return NJS_ERROR;
    }

    if (nxt_slow_path(!njs_is_string(&args[1]))) {
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
            lhq.key = nxt_string_value("flag");
            lhq.proto = &njs_object_hash_proto;

            ret = nxt_lvlhsh_find(&args[2].data.u.object->hash, &lhq);
            if (ret == NXT_OK) {
                prop = lhq.value;
                njs_string_get(&prop->value, &flag);
            }

            lhq.key_hash = NJS_ENCODING_HASH;
            lhq.key = nxt_string_value("encoding");
            lhq.proto = &njs_object_hash_proto;

            ret = nxt_lvlhsh_find(&args[2].data.u.object->hash, &lhq);
            if (ret == NXT_OK) {
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
        flag = nxt_string_value("r");
    }

    flags = njs_fs_flags(&flag);
    if (nxt_slow_path(flags == -1)) {
        njs_type_error(vm, "Unknown file open flags: '%.*s'",
                       (int) flag.length, flag.start);
        return NJS_ERROR;
    }

    path = (char *) njs_string_to_c_string(vm, &args[1]);
    if (nxt_slow_path(path == NULL)) {
        return NJS_ERROR;
    }

    if (encoding.length != 0
        && (encoding.length != 4 || memcmp(encoding.start, "utf8", 4) != 0))
    {
        njs_type_error(vm, "Unknown encoding: '%.*s'",
                       (int) encoding.length, encoding.start);
        return NJS_ERROR;
    }

    description = NULL;

    /* GCC 4 complains about uninitialized errn and syscall. */
    errn = 0;
    syscall = NULL;

    fd = open(path, flags);
    if (nxt_slow_path(fd < 0)) {
        errn = errno;
        description = strerror(errno);
        syscall = "open";
        goto done;
    }

    ret = fstat(fd, &sb);
    if (nxt_slow_path(ret == -1)) {
        errn = errno;
        description = strerror(errno);
        syscall = "stat";
        goto done;
    }

    if (nxt_slow_path(!S_ISREG(sb.st_mode))) {
        errn = 0;
        description = "File is not regular";
        syscall = "stat";
        goto done;
    }

    if (encoding.length != 0) {
        length = sb.st_size;

    } else {
        length = 0;
    }

    start = njs_string_alloc(vm, &vm->retval, sb.st_size, length);
    if (nxt_slow_path(start == NULL)) {
        goto fail;
    }

    p = start;
    end = p + sb.st_size;

    while (p < end) {
        n = read(fd, p, end - p);
        if (nxt_slow_path(n == -1)) {
            if (errno == EINTR) {
                continue;
            }

            errn = errno;
            description = strerror(errno);
            syscall = "read";
            goto done;
        }

        p += n;
    }

    if (encoding.length != 0) {
        length = nxt_utf8_length(start, sb.st_size);

        if (length >= 0) {
            njs_string_offset_map_init(start, sb.st_size);
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
        (void) njs_fs_error(vm, syscall, description, &args[1], errn,
                            &vm->retval);

        return NJS_ERROR;
    }

    return NJS_OK;

fail:

    if (fd != -1) {
        (void) close(fd);
    }

    return NJS_ERROR;
}


static njs_ret_t
njs_fs_append_file(njs_vm_t *vm, njs_value_t *args, nxt_uint_t nargs,
    njs_index_t unused)
{
    return njs_fs_write_file_internal(vm, args, nargs,
                                      O_APPEND | O_CREAT | O_WRONLY);
}


static njs_ret_t
njs_fs_write_file(njs_vm_t *vm, njs_value_t *args, nxt_uint_t nargs,
    njs_index_t unused)
{
    return njs_fs_write_file_internal(vm, args, nargs,
                                      O_TRUNC | O_CREAT | O_WRONLY);
}


static njs_ret_t njs_fs_append_file_sync(njs_vm_t *vm, njs_value_t *args,
    nxt_uint_t nargs, njs_index_t unused)
{
    return njs_fs_write_file_sync_internal(vm, args, nargs,
                                           O_APPEND | O_CREAT | O_WRONLY);
}


static njs_ret_t njs_fs_write_file_sync(njs_vm_t *vm, njs_value_t *args,
    nxt_uint_t nargs, njs_index_t unused)
{
    return njs_fs_write_file_sync_internal(vm, args, nargs,
                                           O_TRUNC | O_CREAT | O_WRONLY);
}


static njs_ret_t njs_fs_write_file_internal(njs_vm_t *vm, njs_value_t *args,
    nxt_uint_t nargs, int default_flags)
{
    int                 fd, errn, flags;
    u_char              *p, *end;
    mode_t              md;
    ssize_t             n;
    nxt_str_t           data, flag, encoding;
    njs_ret_t           ret;
    const char          *path, *syscall, *description;
    njs_value_t         *callback, *mode, arguments[2];
    njs_fs_cont_t       *cont;
    njs_object_prop_t   *prop;
    nxt_lvlhsh_query_t  lhq;

    if (nxt_slow_path(nargs < 4)) {
        njs_type_error(vm, "too few arguments");
        return NJS_ERROR;
    }

    if (nxt_slow_path(!njs_is_string(&args[1]))) {
        njs_type_error(vm, "path must be a string");
        return NJS_ERROR;
    }

    if (nxt_slow_path(!njs_is_string(&args[2]))) {
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
            lhq.key = nxt_string_value("flag");
            lhq.proto = &njs_object_hash_proto;

            ret = nxt_lvlhsh_find(&args[3].data.u.object->hash, &lhq);
            if (ret == NXT_OK) {
                prop = lhq.value;
                njs_string_get(&prop->value, &flag);
            }

            lhq.key_hash = NJS_ENCODING_HASH;
            lhq.key = nxt_string_value("encoding");
            lhq.proto = &njs_object_hash_proto;

            ret = nxt_lvlhsh_find(&args[3].data.u.object->hash, &lhq);
            if (ret == NXT_OK) {
                prop = lhq.value;
                njs_string_get(&prop->value, &encoding);
            }

            lhq.key_hash = NJS_MODE_HASH;
            lhq.key = nxt_string_value("mode");
            lhq.proto = &njs_object_hash_proto;

            ret = nxt_lvlhsh_find(&args[3].data.u.object->hash, &lhq);
            if (ret == NXT_OK) {
                prop = lhq.value;
                mode = &prop->value;
            }

        } else {
            njs_type_error(vm, "Unknown options type "
                           "(a string or object required)");
            return NJS_ERROR;
        }

        if (nxt_slow_path(nargs < 5 || !njs_is_function(&args[4]))) {
            njs_type_error(vm, "callback must be a function");
            return NJS_ERROR;
        }

        callback = &args[4];

    } else {
        if (nxt_slow_path(!njs_is_function(&args[3]))) {
            njs_type_error(vm, "callback must be a function");
            return NJS_ERROR;
        }

        callback = &args[3];
    }

    if (flag.start != NULL) {
        flags = njs_fs_flags(&flag);
        if (nxt_slow_path(flags == -1)) {
            njs_type_error(vm, "Unknown file open flags: '%.*s'",
                           (int) flag.length, flag.start);
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

    path = (char *) njs_string_to_c_string(vm, &args[1]);
    if (nxt_slow_path(path == NULL)) {
        return NJS_ERROR;
    }

    if (encoding.length != 0
        && (encoding.length != 4 || memcmp(encoding.start, "utf8", 4) != 0))
    {
        njs_type_error(vm, "Unknown encoding: '%.*s'",
                       (int) encoding.length, encoding.start);
        return NJS_ERROR;
    }

    description = NULL;

    /* GCC 4 complains about uninitialized errn and syscall. */
    errn = 0;
    syscall = NULL;

    fd = open(path, flags, md);
    if (nxt_slow_path(fd < 0)) {
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
        if (nxt_slow_path(n == -1)) {
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
        ret = njs_fs_error(vm, syscall, description, &args[1], errn,
                           &arguments[1]);

        if (nxt_slow_path(ret != NJS_OK)) {
            return NJS_ERROR;
        }

    } else {
        arguments[1] = njs_value_void;
    }

    arguments[0] = njs_value_void;

    cont = njs_vm_continuation(vm);
    cont->u.cont.function = njs_fs_done;

    return njs_function_apply(vm, callback->data.u.function,
                              arguments, 2, (njs_index_t) &vm->retval);
}


static njs_ret_t
njs_fs_write_file_sync_internal(njs_vm_t *vm, njs_value_t *args,
    nxt_uint_t nargs, int default_flags)
{
    int                 fd, errn, flags;
    u_char              *p, *end;
    mode_t              md;
    ssize_t             n;
    nxt_str_t           data, flag, encoding;
    njs_ret_t           ret;
    const char          *path, *syscall, *description;
    njs_value_t         *mode;
    njs_object_prop_t   *prop;
    nxt_lvlhsh_query_t  lhq;

    if (nxt_slow_path(nargs < 3)) {
        njs_type_error(vm, "too few arguments");
        return NJS_ERROR;
    }

    if (nxt_slow_path(!njs_is_string(&args[1]))) {
        njs_type_error(vm, "path must be a string");
        return NJS_ERROR;
    }

    if (nxt_slow_path(!njs_is_string(&args[2]))) {
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
            lhq.key = nxt_string_value("flag");
            lhq.proto = &njs_object_hash_proto;

            ret = nxt_lvlhsh_find(&args[3].data.u.object->hash, &lhq);
            if (ret == NXT_OK) {
                prop = lhq.value;
                njs_string_get(&prop->value, &flag);
            }

            lhq.key_hash = NJS_ENCODING_HASH;
            lhq.key = nxt_string_value("encoding");
            lhq.proto = &njs_object_hash_proto;

            ret = nxt_lvlhsh_find(&args[3].data.u.object->hash, &lhq);
            if (ret == NXT_OK) {
                prop = lhq.value;
                njs_string_get(&prop->value, &encoding);
            }

            lhq.key_hash = NJS_MODE_HASH;
            lhq.key = nxt_string_value("mode");
            lhq.proto = &njs_object_hash_proto;

            ret = nxt_lvlhsh_find(&args[3].data.u.object->hash, &lhq);
            if (ret == NXT_OK) {
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
        if (nxt_slow_path(flags == -1)) {
            njs_type_error(vm, "Unknown file open flags: '%.*s'",
                           (int) flag.length, flag.start);
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

    path = (char *) njs_string_to_c_string(vm, &args[1]);
    if (nxt_slow_path(path == NULL)) {
        return NJS_ERROR;
    }

    if (encoding.length != 0
        && (encoding.length != 4 || memcmp(encoding.start, "utf8", 4) != 0))
    {
        njs_type_error(vm, "Unknown encoding: '%.*s'",
                       (int) encoding.length, encoding.start);
        return NJS_ERROR;
    }

    description = NULL;

    /* GCC 4 complains about uninitialized errn and syscall. */
    errn = 0;
    syscall = NULL;

    fd = open(path, flags, md);
    if (nxt_slow_path(fd < 0)) {
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
        if (nxt_slow_path(n == -1)) {
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
        ret = njs_fs_error(vm, syscall, description, &args[1], errn,
                           &vm->retval);

        if (nxt_slow_path(ret != NJS_OK)) {
            return NJS_ERROR;
        }

    } else {
        vm->retval = njs_value_void;
    }

    return NJS_OK;
}


static njs_ret_t njs_fs_done(njs_vm_t *vm, njs_value_t *args,
    nxt_uint_t nargs, njs_index_t unused)
{
    vm->retval = njs_value_void;

    return NJS_OK;
}


static njs_ret_t njs_fs_error(njs_vm_t *vm, const char *syscall,
    const char *description, njs_value_t *path, int errn, njs_value_t *retval)
{
    size_t              size;
    nxt_int_t           ret;
    njs_value_t         string, value;
    njs_object_t        *error;
    njs_object_prop_t   *prop;
    nxt_lvlhsh_query_t  lhq;

    size = description != NULL ? strlen(description) : 0;

    ret = njs_string_new(vm, &string, (u_char *) description, size, size);
    if (nxt_slow_path(ret != NXT_OK)) {
        return NJS_ERROR;
    }

    error = njs_error_alloc(vm, NJS_OBJECT_ERROR, NULL, &string);
    if (nxt_slow_path(error == NULL)) {
        return NJS_ERROR;
    }

    lhq.replace = 0;
    lhq.pool = vm->mem_cache_pool;

    if (errn != 0) {
        lhq.key = nxt_string_value("errno");
        lhq.key_hash = NJS_ERRNO_HASH;
        lhq.proto = &njs_object_hash_proto;

        value.data.type = NJS_NUMBER;
        value.data.truth = 1;
        value.data.u.number = errn;

        prop = njs_object_prop_alloc(vm, &njs_fs_errno_string, &value, 1);
        if (nxt_slow_path(prop == NULL)) {
            return NJS_ERROR;
        }

        lhq.value = prop;

        ret = nxt_lvlhsh_insert(&error->hash, &lhq);
        if (nxt_slow_path(ret != NXT_OK)) {
            njs_internal_error(vm, "lvlhsh insert failed");
            return NJS_ERROR;
        }
    }

    if (path != NULL) {
        lhq.key = nxt_string_value("path");
        lhq.key_hash = NJS_PATH_HASH;
        lhq.proto = &njs_object_hash_proto;

        prop = njs_object_prop_alloc(vm, &njs_fs_path_string, path, 1);
        if (nxt_slow_path(prop == NULL)) {
            return NJS_ERROR;
        }

        lhq.value = prop;

        ret = nxt_lvlhsh_insert(&error->hash, &lhq);
        if (nxt_slow_path(ret != NXT_OK)) {
            njs_internal_error(vm, "lvlhsh insert failed");
            return NJS_ERROR;
        }
    }

    if (syscall != NULL) {
        size = strlen(syscall);
        ret = njs_string_new(vm, &string, (u_char *) syscall, size, size);
        if (nxt_slow_path(ret != NXT_OK)) {
            return NJS_ERROR;
        }

        lhq.key = nxt_string_value("sycall");
        lhq.key_hash = NJS_SYSCALL_HASH;
        lhq.proto = &njs_object_hash_proto;

        prop = njs_object_prop_alloc(vm, &njs_fs_syscall_string, &string, 1);
        if (nxt_slow_path(prop == NULL)) {
            return NJS_ERROR;
        }

        lhq.value = prop;

        ret = nxt_lvlhsh_insert(&error->hash, &lhq);
        if (nxt_slow_path(ret != NXT_OK)) {
            njs_internal_error(vm, "lvlhsh insert failed");
            return NJS_ERROR;
        }
    }

    retval->data.u.object = error;
    retval->type = NJS_OBJECT_ERROR;
    retval->data.truth = 1;

    return NJS_OK;
}


static int
njs_fs_flags(nxt_str_t *value)
{
    njs_fs_entry_t  *fl;

    for (fl = &njs_flags_table[0]; fl->name.length != 0; fl++) {
        if (nxt_strstr_eq(value, &fl->name)) {
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
        value = &value->data.u.object_value->value;
        /* Fall through. */

    case NJS_NUMBER:
        return (mode_t) value->data.u.number;

    case NJS_OBJECT_STRING:
    value = &value->data.u.object_value->value;
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
    },

    {
        .type = NJS_METHOD,
        .name = njs_string("readFile"),
        .value = njs_native_function(njs_fs_read_file,
                                     njs_continuation_size(njs_fs_cont_t), 0),
    },

    {
        .type = NJS_METHOD,
        .name = njs_string("readFileSync"),
        .value = njs_native_function(njs_fs_read_file_sync, 0, 0),
    },

    {
        .type = NJS_METHOD,
        .name = njs_string("appendFile"),
        .value = njs_native_function(njs_fs_append_file,
                                     njs_continuation_size(njs_fs_cont_t), 0),
    },

    {
        .type = NJS_METHOD,
        .name = njs_string("appendFileSync"),
        .value = njs_native_function(njs_fs_append_file_sync,
                                     njs_continuation_size(njs_fs_cont_t), 0),
    },

    {
        .type = NJS_METHOD,
        .name = njs_string("writeFile"),
        .value = njs_native_function(njs_fs_write_file,
                                     njs_continuation_size(njs_fs_cont_t), 0),
    },

    {
        .type = NJS_METHOD,
        .name = njs_string("writeFileSync"),
        .value = njs_native_function(njs_fs_write_file_sync,
                                     njs_continuation_size(njs_fs_cont_t), 0),
    },

};


const njs_object_init_t  njs_fs_object_init = {
    nxt_string("fs"),
    njs_fs_object_properties,
    nxt_nitems(njs_fs_object_properties),
};
