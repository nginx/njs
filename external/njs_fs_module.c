
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

#define njs_fs_magic2(field, type)                                           \
    (((type) << 4) | field)


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
    NJS_FS_FSTAT,
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


typedef struct {
    njs_int_t       fd;
    njs_vm_t        *vm;
} njs_filehandle_t;


typedef struct {
    njs_int_t       bytes;
    njs_value_t     buffer;
} njs_bytes_struct_t;


typedef njs_int_t (*njs_file_tree_walk_cb_t)(const char *, const struct stat *,
     njs_ftw_type_t);


static njs_int_t njs_fs_access(njs_vm_t *vm, njs_value_t *args,
    njs_uint_t nargs, njs_index_t calltype);
static njs_int_t njs_fs_mkdir(njs_vm_t *vm, njs_value_t *args,
    njs_uint_t nargs, njs_index_t calltype);
static njs_int_t njs_fs_open(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t calltype);
static njs_int_t njs_fs_close(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t calltype);
static njs_int_t njs_fs_read(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t calltype);
static njs_int_t njs_fs_read_file(njs_vm_t *vm, njs_value_t *args,
    njs_uint_t nargs, njs_index_t calltype);
static njs_int_t njs_fs_readdir(njs_vm_t *vm, njs_value_t *args,
    njs_uint_t nargs, njs_index_t calltype);
static njs_int_t njs_fs_realpath(njs_vm_t *vm, njs_value_t *args,
    njs_uint_t nargs, njs_index_t calltype);
static njs_int_t njs_fs_rename(njs_vm_t *vm, njs_value_t *args,
    njs_uint_t nargs, njs_index_t calltype);
static njs_int_t njs_fs_rmdir(njs_vm_t *vm, njs_value_t *args,
    njs_uint_t nargs, njs_index_t calltype);
static njs_int_t njs_fs_stat(njs_vm_t *vm, njs_value_t *args,
    njs_uint_t nargs, njs_index_t calltype);
static njs_int_t njs_fs_symlink(njs_vm_t *vm, njs_value_t *args,
    njs_uint_t nargs, njs_index_t calltype);
static njs_int_t njs_fs_unlink(njs_vm_t *vm, njs_value_t *args,
    njs_uint_t nargs, njs_index_t calltype);
static njs_int_t njs_fs_write(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t calltype);
static njs_int_t njs_fs_write_file(njs_vm_t *vm, njs_value_t *args,
    njs_uint_t nargs, njs_index_t calltype);

static njs_int_t njs_fs_constants(njs_vm_t *vm, njs_object_prop_t *prop,
    njs_value_t *value, njs_value_t *unused, njs_value_t *retval);
static njs_int_t njs_fs_promises(njs_vm_t *vm, njs_object_prop_t *prop,
    njs_value_t *value, njs_value_t *unused, njs_value_t *retval);

static njs_int_t njs_fs_dirent_constructor(njs_vm_t *vm, njs_value_t *args,
    njs_uint_t nargs, njs_index_t unused);
static njs_int_t njs_fs_dirent_test(njs_vm_t *vm, njs_value_t *args,
    njs_uint_t nargs, njs_index_t testtype);

static njs_int_t njs_fs_stats_test(njs_vm_t *vm, njs_value_t *args,
    njs_uint_t nargs, njs_index_t testtype);
static njs_int_t njs_fs_stats_prop(njs_vm_t *vm, njs_object_prop_t *prop,
    njs_value_t *value, njs_value_t *setval, njs_value_t *retval);
static njs_int_t njs_fs_stats_create(njs_vm_t *vm, struct stat *st,
    njs_value_t *retval);

static njs_int_t njs_fs_filehandle_close(njs_vm_t *vm, njs_value_t *args,
    njs_uint_t nargs, njs_index_t unused);
static njs_int_t njs_fs_filehandle_value_of(njs_vm_t *vm, njs_value_t *args,
    njs_uint_t nargs, njs_index_t unused);
static njs_int_t njs_fs_filehandle_create(njs_vm_t *vm, int fd,
    njs_bool_t shadow, njs_value_t *retval);

static njs_int_t njs_fs_bytes_read_create(njs_vm_t *vm, int bytes,
    njs_value_t *buffer, njs_value_t *retval);
static njs_int_t njs_fs_bytes_written_create(njs_vm_t *vm, int bytes,
    njs_value_t *buffer, njs_value_t *retval);

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


static njs_int_t njs_fs_init(njs_vm_t *vm);


static const njs_value_t  string_flag = njs_string("flag");
static const njs_value_t  string_mode = njs_string("mode");
static const njs_value_t  string_buffer = njs_string("buffer");
static const njs_value_t  string_encoding = njs_string("encoding");
static const njs_value_t  string_recursive = njs_string("recursive");


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


static njs_external_t  njs_ext_fs[] = {

    {
        .flags = NJS_EXTERN_PROPERTY | NJS_EXTERN_SYMBOL,
        .name.symbol = NJS_SYMBOL_TO_STRING_TAG,
        .u.property = {
            .value = "fs",
        }
    },

    {
        .flags = NJS_EXTERN_METHOD,
        .name.string = njs_str("access"),
        .writable = 1,
        .configurable = 1,
        .u.method = {
            .native = njs_fs_access,
            .magic8 = NJS_FS_CALLBACK,
        }
    },

    {
        .flags = NJS_EXTERN_METHOD,
        .name.string = njs_str("accessSync"),
        .writable = 1,
        .configurable = 1,
        .u.method = {
            .native = njs_fs_access,
            .magic8 = NJS_FS_DIRECT,
        }
    },

    {
        .flags = NJS_EXTERN_METHOD,
        .name.string = njs_str("appendFile"),
        .writable = 1,
        .configurable = 1,
        .u.method = {
            .native = njs_fs_write_file,
            .magic8 = njs_fs_magic(NJS_FS_CALLBACK, NJS_FS_APPEND),
        }
    },

    {
        .flags = NJS_EXTERN_METHOD,
        .name.string = njs_str("appendFileSync"),
        .writable = 1,
        .configurable = 1,
        .u.method = {
            .native = njs_fs_write_file,
            .magic8 = njs_fs_magic(NJS_FS_DIRECT, NJS_FS_APPEND),
        }
    },

    {
        .flags = NJS_EXTERN_METHOD,
        .name.string = njs_str("closeSync"),
        .writable = 1,
        .configurable = 1,
        .u.method = {
            .native = njs_fs_close,
            .magic8 = NJS_FS_DIRECT,
        }
    },

    {
        .flags = NJS_EXTERN_PROPERTY,
        .name.string = njs_str("constants"),
        .enumerable = 1,
        .u.property = {
            .handler = njs_fs_constants,
        }
    },

    {
        .flags = NJS_EXTERN_METHOD,
        .name.string = njs_str("Dirent"),
        .writable = 1,
        .configurable = 1,
        .u.method = {
            .native = njs_fs_dirent_constructor,
            .ctor = 1,
        }
    },

    {
        .flags = NJS_EXTERN_METHOD,
        .name.string = njs_str("fstatSync"),
        .writable = 1,
        .configurable = 1,
        .u.method = {
            .native = njs_fs_stat,
            .magic8 = njs_fs_magic(NJS_FS_DIRECT, NJS_FS_FSTAT),
        }
    },

    {
        .flags = NJS_EXTERN_METHOD,
        .name.string = njs_str("lstat"),
        .writable = 1,
        .configurable = 1,
        .u.method = {
            .native = njs_fs_stat,
            .magic8 = njs_fs_magic(NJS_FS_CALLBACK, NJS_FS_LSTAT),
        }
    },

    {
        .flags = NJS_EXTERN_METHOD,
        .name.string = njs_str("lstatSync"),
        .writable = 1,
        .configurable = 1,
        .u.method = {
            .native = njs_fs_stat,
            .magic8 = njs_fs_magic(NJS_FS_DIRECT, NJS_FS_LSTAT),
        }
    },

    {
        .flags = NJS_EXTERN_METHOD,
        .name.string = njs_str("mkdir"),
        .writable = 1,
        .configurable = 1,
        .u.method = {
            .native = njs_fs_mkdir,
            .magic8 = NJS_FS_CALLBACK,
        }
    },

    {
        .flags = NJS_EXTERN_METHOD,
        .name.string = njs_str("mkdirSync"),
        .writable = 1,
        .configurable = 1,
        .u.method = {
            .native = njs_fs_mkdir,
            .magic8 = NJS_FS_DIRECT,
        }
    },

    {
        .flags = NJS_EXTERN_METHOD,
        .name.string = njs_str("openSync"),
        .writable = 1,
        .configurable = 1,
        .u.method = {
            .native = njs_fs_open,
            .magic8 = NJS_FS_DIRECT,
        }
    },

    {
        .flags = NJS_EXTERN_PROPERTY,
        .name.string = njs_str("promises"),
        .enumerable = 1,
        .u.property = {
            .handler = njs_fs_promises,
        }
    },

    {
        .flags = NJS_EXTERN_METHOD,
        .name.string = njs_str("readdir"),
        .writable = 1,
        .configurable = 1,
        .u.method = {
            .native = njs_fs_readdir,
            .magic8 = NJS_FS_CALLBACK,
        }
    },

    {
        .flags = NJS_EXTERN_METHOD,
        .name.string = njs_str("readdirSync"),
        .writable = 1,
        .configurable = 1,
        .u.method = {
            .native = njs_fs_readdir,
            .magic8 = NJS_FS_DIRECT,
        }
    },

    {
        .flags = NJS_EXTERN_METHOD,
        .name.string = njs_str("readFile"),
        .writable = 1,
        .configurable = 1,
        .u.method = {
            .native = njs_fs_read_file,
            .magic8 = NJS_FS_CALLBACK,
        }
    },

    {
        .flags = NJS_EXTERN_METHOD,
        .name.string = njs_str("readFileSync"),
        .writable = 1,
        .configurable = 1,
        .u.method = {
            .native = njs_fs_read_file,
            .magic8 = NJS_FS_DIRECT,
        }
    },

    {
        .flags = NJS_EXTERN_METHOD,
        .name.string = njs_str("readSync"),
        .writable = 1,
        .configurable = 1,
        .u.method = {
            .native = njs_fs_read,
            .magic8 = NJS_FS_DIRECT,
        }
    },

    {
        .flags = NJS_EXTERN_METHOD,
        .name.string = njs_str("realpath"),
        .writable = 1,
        .configurable = 1,
        .u.method = {
            .native = njs_fs_realpath,
            .magic8 = NJS_FS_CALLBACK,
        }
    },

    {
        .flags = NJS_EXTERN_METHOD,
        .name.string = njs_str("realpathSync"),
        .writable = 1,
        .configurable = 1,
        .u.method = {
            .native = njs_fs_realpath,
            .magic8 = NJS_FS_DIRECT,
        }
    },

    {
        .flags = NJS_EXTERN_METHOD,
        .name.string = njs_str("rename"),
        .writable = 1,
        .configurable = 1,
        .u.method = {
            .native = njs_fs_rename,
            .magic8 = NJS_FS_CALLBACK,
        }
    },

    {
        .flags = NJS_EXTERN_METHOD,
        .name.string = njs_str("renameSync"),
        .writable = 1,
        .configurable = 1,
        .u.method = {
            .native = njs_fs_rename,
            .magic8 = NJS_FS_DIRECT,
        }
    },

    {
        .flags = NJS_EXTERN_METHOD,
        .name.string = njs_str("rmdir"),
        .writable = 1,
        .configurable = 1,
        .u.method = {
            .native = njs_fs_rmdir,
            .magic8 = NJS_FS_CALLBACK,
        }
    },

    {
        .flags = NJS_EXTERN_METHOD,
        .name.string = njs_str("rmdirSync"),
        .writable = 1,
        .configurable = 1,
        .u.method = {
            .native = njs_fs_rmdir,
            .magic8 = NJS_FS_DIRECT,
        }
    },

    {
        .flags = NJS_EXTERN_METHOD,
        .name.string = njs_str("stat"),
        .writable = 1,
        .configurable = 1,
        .u.method = {
            .native = njs_fs_stat,
            .magic8 = njs_fs_magic(NJS_FS_CALLBACK, NJS_FS_STAT),
        }
    },

    {
        .flags = NJS_EXTERN_METHOD,
        .name.string = njs_str("statSync"),
        .writable = 1,
        .configurable = 1,
        .u.method = {
            .native = njs_fs_stat,
            .magic8 = njs_fs_magic(NJS_FS_DIRECT, NJS_FS_STAT),
        }
    },

    {
        .flags = NJS_EXTERN_METHOD,
        .name.string = njs_str("symlink"),
        .writable = 1,
        .configurable = 1,
        .u.method = {
            .native = njs_fs_symlink,
            .magic8 = NJS_FS_CALLBACK,
        }
    },

    {
        .flags = NJS_EXTERN_METHOD,
        .name.string = njs_str("symlinkSync"),
        .writable = 1,
        .configurable = 1,
        .u.method = {
            .native = njs_fs_symlink,
            .magic8 = NJS_FS_DIRECT,
        }
    },

    {
        .flags = NJS_EXTERN_METHOD,
        .name.string = njs_str("unlink"),
        .writable = 1,
        .configurable = 1,
        .u.method = {
            .native = njs_fs_unlink,
            .magic8 = NJS_FS_CALLBACK,
        }
    },

    {
        .flags = NJS_EXTERN_METHOD,
        .name.string = njs_str("unlinkSync"),
        .writable = 1,
        .configurable = 1,
        .u.method = {
            .native = njs_fs_unlink,
            .magic8 = NJS_FS_DIRECT,
        }
    },

    {
        .flags = NJS_EXTERN_METHOD,
        .name.string = njs_str("writeFile"),
        .writable = 1,
        .configurable = 1,
        .u.method = {
            .native = njs_fs_write_file,
            .magic8 = njs_fs_magic(NJS_FS_CALLBACK, NJS_FS_TRUNC),
        }
    },

    {
        .flags = NJS_EXTERN_METHOD,
        .name.string = njs_str("writeFileSync"),
        .writable = 1,
        .configurable = 1,
        .u.method = {
            .native = njs_fs_write_file,
            .magic8 = njs_fs_magic(NJS_FS_DIRECT, NJS_FS_TRUNC),
        }
    },

    {
        .flags = NJS_EXTERN_METHOD,
        .name.string = njs_str("writeSync"),
        .writable = 1,
        .configurable = 1,
        .u.method = {
            .native = njs_fs_write,
            .magic8 = NJS_FS_DIRECT,
        }
    },

};


static njs_external_t  njs_ext_dirent[] = {

    {
        .flags = NJS_EXTERN_PROPERTY | NJS_EXTERN_SYMBOL,
        .name.symbol = NJS_SYMBOL_TO_STRING_TAG,
        .u.property = {
            .value = "Dirent",
        }
    },

    {
        .flags = NJS_EXTERN_METHOD,
        .name.string = njs_str("constructor"),
        .writable = 1,
        .configurable = 1,
        .u.method = {
            .native = njs_fs_dirent_constructor,
        }
    },

    {
        .flags = NJS_EXTERN_METHOD,
        .name.string = njs_str("isBlockDevice"),
        .writable = 1,
        .configurable = 1,
        .u.method = {
            .native = njs_fs_dirent_test,
            .magic8 = DT_BLK,
        }
    },

    {
        .flags = NJS_EXTERN_METHOD,
        .name.string = njs_str("isCharacterDevice"),
        .writable = 1,
        .configurable = 1,
        .u.method = {
            .native = njs_fs_dirent_test,
            .magic8 = DT_CHR,
        }
    },

    {
        .flags = NJS_EXTERN_METHOD,
        .name.string = njs_str("isDirectory"),
        .writable = 1,
        .configurable = 1,
        .u.method = {
            .native = njs_fs_dirent_test,
            .magic8 = DT_DIR,
        }
    },

    {
        .flags = NJS_EXTERN_METHOD,
        .name.string = njs_str("isFIFO"),
        .writable = 1,
        .configurable = 1,
        .u.method = {
            .native = njs_fs_dirent_test,
            .magic8 = DT_FIFO,
        }
    },

    {
        .flags = NJS_EXTERN_METHOD,
        .name.string = njs_str("isFile"),
        .writable = 1,
        .configurable = 1,
        .u.method = {
            .native = njs_fs_dirent_test,
            .magic8 = DT_REG,
        }
    },

    {
        .flags = NJS_EXTERN_METHOD,
        .name.string = njs_str("isSocket"),
        .writable = 1,
        .configurable = 1,
        .u.method = {
            .native = njs_fs_dirent_test,
            .magic8 = DT_SOCK,
        }
    },

    {
        .flags = NJS_EXTERN_METHOD,
        .name.string = njs_str("isSymbolicLink"),
        .writable = 1,
        .configurable = 1,
        .u.method = {
            .native = njs_fs_dirent_test,
            .magic8 = DT_LNK,
        }
    },
};


static njs_external_t  njs_ext_stats[] = {

    {
        .flags = NJS_EXTERN_PROPERTY | NJS_EXTERN_SYMBOL,
        .name.symbol = NJS_SYMBOL_TO_STRING_TAG,
        .u.property = {
            .value = "Stats",
        }
    },

    {
        .flags = NJS_EXTERN_PROPERTY,
        .name.string = njs_str("atime"),
        .enumerable = 1,
        .u.property = {
            .handler = njs_fs_stats_prop,
            .magic32 = njs_fs_magic2(NJS_FS_STAT_ATIME, NJS_DATE),
        }
    },

    {
        .flags = NJS_EXTERN_PROPERTY,
        .name.string = njs_str("atimeMs"),
        .enumerable = 1,
        .u.property = {
            .handler = njs_fs_stats_prop,
            .magic32 = njs_fs_magic2(NJS_FS_STAT_ATIME, NJS_NUMBER),
        }
    },

    {
        .flags = NJS_EXTERN_PROPERTY,
        .name.string = njs_str("birthtime"),
        .enumerable = 1,
        .u.property = {
            .handler = njs_fs_stats_prop,
            .magic32 = njs_fs_magic2(NJS_FS_STAT_BIRTHTIME, NJS_DATE),
        }
    },

    {
        .flags = NJS_EXTERN_PROPERTY,
        .name.string = njs_str("birthtimeMs"),
        .enumerable = 1,
        .u.property = {
            .handler = njs_fs_stats_prop,
            .magic32 = njs_fs_magic2(NJS_FS_STAT_BIRTHTIME, NJS_NUMBER),
        }
    },

    {
        .flags = NJS_EXTERN_PROPERTY,
        .name.string = njs_str("ctime"),
        .enumerable = 1,
        .u.property = {
            .handler = njs_fs_stats_prop,
            .magic32 = njs_fs_magic2(NJS_FS_STAT_CTIME, NJS_DATE),
        }
    },

    {
        .flags = NJS_EXTERN_PROPERTY,
        .name.string = njs_str("ctimeMs"),
        .enumerable = 1,
        .u.property = {
            .handler = njs_fs_stats_prop,
            .magic32 = njs_fs_magic2(NJS_FS_STAT_CTIME, NJS_NUMBER),
        }
    },

    {
        .flags = NJS_EXTERN_PROPERTY,
        .name.string = njs_str("blksize"),
        .enumerable = 1,
        .u.property = {
            .handler = njs_fs_stats_prop,
            .magic32 = njs_fs_magic2(NJS_FS_STAT_BLKSIZE, NJS_NUMBER),
        }
    },

    {
        .flags = NJS_EXTERN_PROPERTY,
        .name.string = njs_str("blocks"),
        .enumerable = 1,
        .u.property = {
            .handler = njs_fs_stats_prop,
            .magic32 = njs_fs_magic2(NJS_FS_STAT_BLOCKS, NJS_NUMBER),
        }
    },

    {
        .flags = NJS_EXTERN_PROPERTY,
        .name.string = njs_str("dev"),
        .enumerable = 1,
        .u.property = {
            .handler = njs_fs_stats_prop,
            .magic32 = njs_fs_magic2(NJS_FS_STAT_DEV, NJS_NUMBER),
        }
    },

    {
        .flags = NJS_EXTERN_PROPERTY,
        .name.string = njs_str("gid"),
        .enumerable = 1,
        .u.property = {
            .handler = njs_fs_stats_prop,
            .magic32 = njs_fs_magic2(NJS_FS_STAT_GID, NJS_NUMBER),
        }
    },

    {
        .flags = NJS_EXTERN_PROPERTY,
        .name.string = njs_str("ino"),
        .enumerable = 1,
        .u.property = {
            .handler = njs_fs_stats_prop,
            .magic32 = njs_fs_magic2(NJS_FS_STAT_INO, NJS_NUMBER),
        }
    },

    {
        .flags = NJS_EXTERN_PROPERTY,
        .name.string = njs_str("mode"),
        .enumerable = 1,
        .u.property = {
            .handler = njs_fs_stats_prop,
            .magic32 = njs_fs_magic2(NJS_FS_STAT_MODE, NJS_NUMBER),
        }
    },

    {
        .flags = NJS_EXTERN_PROPERTY,
        .name.string = njs_str("mtime"),
        .enumerable = 1,
        .u.property = {
            .handler = njs_fs_stats_prop,
            .magic32 = njs_fs_magic2(NJS_FS_STAT_MTIME, NJS_DATE),
        }
    },

    {
        .flags = NJS_EXTERN_PROPERTY,
        .name.string = njs_str("mtimeMs"),
        .enumerable = 1,
        .u.property = {
            .handler = njs_fs_stats_prop,
            .magic32 = njs_fs_magic2(NJS_FS_STAT_MTIME, NJS_NUMBER),
        }
    },

    {
        .flags = NJS_EXTERN_PROPERTY,
        .name.string = njs_str("nlink"),
        .enumerable = 1,
        .u.property = {
            .handler = njs_fs_stats_prop,
            .magic32 = njs_fs_magic2(NJS_FS_STAT_NLINK, NJS_NUMBER),
        }
    },

    {
        .flags = NJS_EXTERN_PROPERTY,
        .name.string = njs_str("rdev"),
        .enumerable = 1,
        .u.property = {
            .handler = njs_fs_stats_prop,
            .magic32 = njs_fs_magic2(NJS_FS_STAT_RDEV, NJS_NUMBER),
        }
    },

    {
        .flags = NJS_EXTERN_PROPERTY,
        .name.string = njs_str("size"),
        .enumerable = 1,
        .u.property = {
            .handler = njs_fs_stats_prop,
            .magic32 = njs_fs_magic2(NJS_FS_STAT_SIZE, NJS_NUMBER),
        }
    },

    {
        .flags = NJS_EXTERN_PROPERTY,
        .name.string = njs_str("uid"),
        .enumerable = 1,
        .u.property = {
            .handler = njs_fs_stats_prop,
            .magic32 = njs_fs_magic2(NJS_FS_STAT_UID, NJS_NUMBER),
        }
    },

    {
        .flags = NJS_EXTERN_METHOD,
        .name.string = njs_str("isBlockDevice"),
        .writable = 1,
        .configurable = 1,
        .u.method = {
            .native = njs_fs_stats_test,
            .magic8 = DT_BLK,
        }
    },

    {
        .flags = NJS_EXTERN_METHOD,
        .name.string = njs_str("isCharacterDevice"),
        .writable = 1,
        .configurable = 1,
        .u.method = {
            .native = njs_fs_stats_test,
            .magic8 = DT_CHR,
        }
    },

    {
        .flags = NJS_EXTERN_METHOD,
        .name.string = njs_str("isDirectory"),
        .writable = 1,
        .configurable = 1,
        .u.method = {
            .native = njs_fs_stats_test,
            .magic8 = DT_DIR,
        }
    },

    {
        .flags = NJS_EXTERN_METHOD,
        .name.string = njs_str("isFIFO"),
        .writable = 1,
        .configurable = 1,
        .u.method = {
            .native = njs_fs_stats_test,
            .magic8 = DT_FIFO,
        }
    },

    {
        .flags = NJS_EXTERN_METHOD,
        .name.string = njs_str("isFile"),
        .writable = 1,
        .configurable = 1,
        .u.method = {
            .native = njs_fs_stats_test,
            .magic8 = DT_REG,
        }
    },

    {
        .flags = NJS_EXTERN_METHOD,
        .name.string = njs_str("isSocket"),
        .writable = 1,
        .configurable = 1,
        .u.method = {
            .native = njs_fs_stats_test,
            .magic8 = DT_SOCK,
        }
    },

    {
        .flags = NJS_EXTERN_METHOD,
        .name.string = njs_str("isSymbolicLink"),
        .writable = 1,
        .configurable = 1,
        .u.method = {
            .native = njs_fs_stats_test,
            .magic8 = DT_LNK,
        }
    },

};


static njs_external_t  njs_ext_filehandle[] = {

    {
        .flags = NJS_EXTERN_PROPERTY | NJS_EXTERN_SYMBOL,
        .name.symbol = NJS_SYMBOL_TO_STRING_TAG,
        .u.property = {
            .value = "FileHandle",
        }
    },

    {
        .flags = NJS_EXTERN_METHOD,
        .name.string = njs_str("close"),
        .writable = 1,
        .configurable = 1,
        .u.method = {
            .native = njs_fs_filehandle_close,
        }
    },

    {
        .flags = NJS_EXTERN_PROPERTY,
        .name.string = njs_str("fd"),
        .enumerable = 1,
        .u.property = {
            .handler = njs_external_property,
            .magic32 = offsetof(njs_filehandle_t, fd),
            .magic16 = NJS_EXTERN_TYPE_INT,
        }
    },

    {
        .flags = NJS_EXTERN_METHOD,
        .name.string = njs_str("read"),
        .writable = 1,
        .configurable = 1,
        .u.method = {
            .native = njs_fs_read,
            .magic8 = NJS_FS_PROMISE,
        }
    },

    {
        .flags = NJS_EXTERN_METHOD,
        .name.string = njs_str("stat"),
        .writable = 1,
        .configurable = 1,
        .u.method = {
            .native = njs_fs_stat,
            .magic8 = njs_fs_magic(NJS_FS_PROMISE, NJS_FS_FSTAT),
        }
    },

    {
        .flags = NJS_EXTERN_METHOD,
        .name.string = njs_str("valueOf"),
        .writable = 1,
        .configurable = 1,
        .u.method = {
            .native = njs_fs_filehandle_value_of,
        }
    },

    {
        .flags = NJS_EXTERN_METHOD,
        .name.string = njs_str("write"),
        .writable = 1,
        .configurable = 1,
        .u.method = {
            .native = njs_fs_write,
            .magic8 = NJS_FS_PROMISE,
        }
    },

};


static njs_external_t  njs_ext_bytes_read[] = {

    {
        .flags = NJS_EXTERN_PROPERTY | NJS_EXTERN_SYMBOL,
        .name.symbol = NJS_SYMBOL_TO_STRING_TAG,
        .u.property = {
            .value = "BytesRead",
        }
    },

    {
        .flags = NJS_EXTERN_PROPERTY,
        .name.string = njs_str("buffer"),
        .enumerable = 1,
        .u.property = {
            .handler = njs_external_property,
            .magic32 = offsetof(njs_bytes_struct_t, buffer),
            .magic16 = NJS_EXTERN_TYPE_VALUE,
        }
    },

    {
        .flags = NJS_EXTERN_PROPERTY,
        .name.string = njs_str("bytesRead"),
        .enumerable = 1,
        .u.property = {
            .handler = njs_external_property,
            .magic32 = offsetof(njs_bytes_struct_t, bytes),
            .magic16 = NJS_EXTERN_TYPE_INT,
        }
    },

};


static njs_external_t  njs_ext_bytes_written[] = {

    {
        .flags = NJS_EXTERN_PROPERTY | NJS_EXTERN_SYMBOL,
        .name.symbol = NJS_SYMBOL_TO_STRING_TAG,
        .u.property = {
            .value = "BytesWritten",
        }
    },

    {
        .flags = NJS_EXTERN_PROPERTY,
        .name.string = njs_str("buffer"),
        .enumerable = 1,
        .u.property = {
            .handler = njs_external_property,
            .magic32 = offsetof(njs_bytes_struct_t, buffer),
            .magic16 = NJS_EXTERN_TYPE_VALUE,
        }
    },

    {
        .flags = NJS_EXTERN_PROPERTY,
        .name.string = njs_str("bytesWritten"),
        .enumerable = 1,
        .u.property = {
            .handler = njs_external_property,
            .magic32 = offsetof(njs_bytes_struct_t, bytes),
            .magic16 = NJS_EXTERN_TYPE_INT,
        }
    },

};


static njs_int_t    njs_fs_stats_proto_id;
static njs_int_t    njs_fs_dirent_proto_id;
static njs_int_t    njs_fs_filehandle_proto_id;
static njs_int_t    njs_fs_bytes_read_proto_id;
static njs_int_t    njs_fs_bytes_written_proto_id;


njs_module_t  njs_fs_module = {
    .name = njs_str("fs"),
    .init = njs_fs_init,
};


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
njs_fs_open(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t calltype)
{
    int          fd, flags;
    mode_t       md;
    njs_int_t    ret;
    const char   *path;
    njs_value_t  retval, *value;
    char         path_buf[NJS_MAX_PATH + 1];

    path = njs_fs_path(vm, path_buf, njs_arg(args, nargs, 1), "path");
    if (njs_slow_path(path == NULL)) {
        return NJS_ERROR;
    }

    value = njs_arg(args, nargs, 2);
    if (njs_is_function(value)) {
        value = njs_value_arg(&njs_value_undefined);
    }

    flags = njs_fs_flags(vm, value, O_RDONLY);
    if (njs_slow_path(flags == -1)) {
        return NJS_ERROR;
    }

    value = njs_arg(args, nargs, 3);
    if (njs_is_function(value)) {
        value = njs_value_arg(&njs_value_undefined);
    }

    md = njs_fs_mode(vm, value, 0666);
    if (njs_slow_path(md == (mode_t) -1)) {
        return NJS_ERROR;
    }

    fd = open(path, flags, md);
    if (njs_slow_path(fd < 0)) {
        ret = njs_fs_error(vm, "open", strerror(errno), path, errno, &retval);
        goto done;
    }

    ret = njs_fs_filehandle_create(vm, fd, calltype == NJS_FS_DIRECT, &retval);
    if (njs_slow_path(ret != NJS_OK)) {
        goto done;
    }

    if (calltype == NJS_FS_DIRECT) {
        njs_value_number_set(&retval, fd);
    }

done:

    if (ret == NJS_OK) {
        return njs_fs_result(vm, &retval, calltype, NULL, 2);
    }

    if (fd != -1) {
        (void) close(fd);
    }

    return NJS_ERROR;
}


static njs_int_t
njs_fs_close(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t calltype)
{
    int64_t      fd;
    njs_int_t    ret;
    njs_value_t  retval, *fh;

    fh = njs_arg(args, nargs, 1);

    ret = njs_value_to_integer(vm, fh, &fd);
    if (njs_slow_path(ret != NJS_OK)) {
        return ret;
    }

    njs_set_undefined(&retval);

    ret = close((int) fd);
    if (njs_slow_path(ret != 0)) {
        ret = njs_fs_error(vm, "close", strerror(errno), NULL, errno, &retval);
    }

    if (ret == NJS_OK) {
        return njs_fs_result(vm, &retval, calltype, NULL, 1);
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
njs_fs_read(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t calltype)
{
    int64_t             fd, length, pos, offset;
    ssize_t             n;
    njs_int_t           ret;
    njs_str_t           data;
    njs_uint_t          fd_offset;
    njs_value_t         retval, *buffer, *value;
    njs_typed_array_t   *array;
    njs_array_buffer_t  *array_buffer;

    fd_offset = !!(calltype == NJS_FS_DIRECT);

    ret = njs_value_to_integer(vm, njs_arg(args, nargs, fd_offset), &fd);
    if (njs_slow_path(ret != NJS_OK)) {
        return ret;
    }

    pos = -1;

    /*
     * fh.read(buffer, offset[, length[, position]])
     * fs.readSync(fd, buffer, offset[, length[, position]])
     */

    buffer = njs_arg(args, nargs, fd_offset + 1);
    array = njs_buffer_slot(vm, buffer, "buffer");
    if (njs_slow_path(array == NULL)) {
        return NJS_ERROR;
    }

    array_buffer = njs_typed_array_writable(vm, array);
    if (njs_slow_path(array_buffer == NULL)) {
        return NJS_ERROR;
    }

    ret = njs_value_to_integer(vm, njs_arg(args, nargs, fd_offset + 2),
                               &offset);
    if (njs_slow_path(ret != NJS_OK)) {
        return ret;
    }

    if (njs_slow_path(offset < 0 || (size_t) offset > array->byte_length)) {
        njs_range_error(vm, "offset is out of range (must be <= %z)",
                        array->byte_length);
        return NJS_ERROR;
    }

    data.length = array->byte_length - offset;
    data.start = &array_buffer->u.u8[array->offset + offset];

    value = njs_arg(args, nargs, fd_offset + 3);

    if (njs_is_defined(value)) {
        ret = njs_value_to_integer(vm, value, &length);
        if (njs_slow_path(ret != NJS_OK)) {
            return ret;
        }

        if (njs_slow_path(length < 0 || (size_t) length > data.length)) {
            njs_range_error(vm, "length is out of range (must be <= %z)",
                            data.length);
            return NJS_ERROR;
        }

        data.length = length;
    }

    value = njs_arg(args, nargs, fd_offset + 4);

    if (!njs_is_null_or_undefined(value)) {
        ret = njs_value_to_integer(vm, value, &pos);
        if (njs_slow_path(ret != NJS_OK)) {
            return ret;
        }
    }

    if (pos == -1) {
        n = read(fd, data.start, data.length);

    } else {
        n = pread(fd, data.start, data.length, pos);
    }

    if (njs_slow_path(n == -1)) {
        ret = njs_fs_error(vm, "read", strerror(errno), NULL, errno, &retval);
        goto done;
    }

    if (calltype == NJS_FS_PROMISE) {
        ret = njs_fs_bytes_read_create(vm, n, buffer, &retval);
        if (njs_slow_path(ret != NJS_OK)) {
            goto done;
        }

    } else {
        njs_value_number_set(&retval, n);
    }

done:

    if (ret == NJS_OK) {
        return njs_fs_result(vm, &retval, calltype, NULL, 1);
    }

    return NJS_ERROR;
}


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
njs_fs_stat(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t magic)
{
    int64_t            fd;
    njs_int_t          ret;
    njs_uint_t         fd_offset;
    njs_bool_t         throw;
    struct stat        sb;
    const char         *path;
    njs_value_t        retval, *callback, *options;
    njs_fs_calltype_t  calltype;
    char               path_buf[NJS_MAX_PATH + 1];

    static const njs_value_t  string_bigint = njs_string("bigint");
    static const njs_value_t  string_throw = njs_string("throwIfNoEntry");

    fd = -1;
    path = NULL;
    calltype = magic & 3;

    if ((magic >> 2) != NJS_FS_FSTAT) {
        path = njs_fs_path(vm, path_buf, njs_arg(args, nargs, 1), "path");
        if (njs_slow_path(path == NULL)) {
            return NJS_ERROR;
        }

        options = njs_arg(args, nargs, 2);

    } else {
        fd_offset = !!(calltype == NJS_FS_DIRECT);
        ret = njs_value_to_integer(vm, njs_argument(args, fd_offset), &fd);
        if (njs_slow_path(ret != NJS_OK)) {
            return ret;
        }

        options = njs_arg(args, nargs, fd_offset + 1);
    }

    callback = NULL;

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

    switch (magic >> 2) {
    case NJS_FS_STAT:
        ret = stat(path, &sb);
        break;

    case NJS_FS_LSTAT:
        ret = lstat(path, &sb);
        break;

    case NJS_FS_FSTAT:
    default:
        ret = fstat(fd, &sb);
        break;
    }

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
njs_fs_write(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t calltype)
{
    int64_t                      fd, length, pos, offset;
    ssize_t                      n;
    njs_int_t                    ret;
    njs_str_t                    data;
    njs_uint_t                   fd_offset;
    njs_value_t                  retval, *buffer, *value;
    const njs_buffer_encoding_t  *encoding;

    fd_offset = !!(calltype == NJS_FS_DIRECT);

    ret = njs_value_to_integer(vm, njs_arg(args, nargs, fd_offset), &fd);
    if (njs_slow_path(ret != NJS_OK)) {
        return ret;
    }

    buffer = njs_arg(args, nargs, fd_offset + 1);

    pos = -1;
    encoding = NULL;

    /*
     * fs.writeSync(fd, string[, position[, encoding]])
     * fh.write(string[, position[, encoding]])
     */

    if (njs_is_string(buffer)) {
        value = njs_arg(args, nargs, fd_offset + 2);

        if (!njs_is_null_or_undefined(value)) {
            ret = njs_value_to_integer(vm, value, &pos);
            if (njs_slow_path(ret != NJS_OK)) {
                return ret;
            }
        }

        encoding = njs_buffer_encoding(vm, njs_arg(args, nargs, fd_offset + 3));
        if (njs_slow_path(encoding == NULL)) {
            return NJS_ERROR;
        }

        ret = njs_buffer_decode_string(vm, buffer, &retval, encoding);
        if (njs_slow_path(ret != NJS_OK)) {
            return NJS_ERROR;
        }

        njs_string_get(&retval, &data);

        goto process;
    }

    /*
     * fh.write(buffer, offset[, length[, position]])
     * fs.writeSync(fd, buffer, offset[, length[, position]])
     */

    ret = njs_vm_value_to_bytes(vm, &data, buffer);
    if (njs_slow_path(ret != NJS_OK)) {
        return NJS_ERROR;
    }

    ret = njs_value_to_integer(vm, njs_arg(args, nargs, fd_offset + 2),
                               &offset);
    if (njs_slow_path(ret != NJS_OK)) {
        return ret;
    }

    if (njs_slow_path(offset < 0 || (size_t) offset > data.length)) {
        njs_range_error(vm, "offset is out of range (must be <= %z)",
                        data.length);
        return NJS_ERROR;
    }

    data.length -= offset;
    data.start += offset;

    value = njs_arg(args, nargs, fd_offset + 3);

    if (njs_is_defined(value)) {
        ret = njs_value_to_integer(vm, value, &length);
        if (njs_slow_path(ret != NJS_OK)) {
            return ret;
        }

        if (njs_slow_path(length < 0 || (size_t) length > data.length)) {
            njs_range_error(vm, "length is out of range (must be <= %z)",
                            data.length);
            return NJS_ERROR;
        }

        data.length = length;
    }

    value = njs_arg(args, nargs, fd_offset + 4);

    if (!njs_is_null_or_undefined(value)) {
        ret = njs_value_to_integer(vm, value, &pos);
        if (njs_slow_path(ret != NJS_OK)) {
            return ret;
        }
    }

process:

    if (pos == -1) {
        n = write(fd, data.start, data.length);

    } else {
        n = pwrite(fd, data.start, data.length, pos);
    }

    if (njs_slow_path(n == -1)) {
        ret = njs_fs_error(vm, "write", strerror(errno), NULL, errno, &retval);
        goto done;
    }

    if (njs_slow_path((size_t) n != data.length)) {
        ret = njs_fs_error(vm, "write", "failed to write all the data", NULL,
                           0, &retval);
        goto done;
    }

    if (calltype == NJS_FS_PROMISE) {
        ret = njs_fs_bytes_written_create(vm, n, buffer, &retval);
        if (njs_slow_path(ret != NJS_OK)) {
            goto done;
        }

    } else {
        njs_value_number_set(&retval, n);
    }

done:

    if (ret == NJS_OK) {
        return njs_fs_result(vm, &retval, calltype, NULL, 1);
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
            memcpy(&path[base + 1], d_name, length + njs_length("\0"));

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
    njs_int_t  ret;

    static const njs_value_t  string_name = njs_string("name");
    static const njs_value_t  string_type = njs_string("type");

    ret = njs_vm_external_create(vm, retval, njs_fs_dirent_proto_id, NULL, 0);
    if (njs_slow_path(ret != NJS_OK)) {
        return ret;
    }

    ret = njs_value_property_set(vm, retval, njs_value_arg(&string_name),
                                 name);
    if (njs_slow_path(ret != NJS_OK)) {
        return ret;
    }

    /* TODO: use a private symbol as a key. */
    return njs_value_property_set(vm, retval, njs_value_arg(&string_type),
                                  type);
}


static njs_int_t
njs_fs_dirent_constructor(njs_vm_t *vm, njs_value_t *args,
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
    NJS_DECLARE_PROP_NAME("Dirent"),

    NJS_DECLARE_PROP_LENGTH(2),

    NJS_DECLARE_PROP_HANDLER("prototype", njs_object_prototype_create, 0, 0, 0),
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
    njs_stat_t  *stat;

    stat = njs_mp_alloc(vm->mem_pool, sizeof(njs_stat_t));
    if (njs_slow_path(stat == NULL)) {
        njs_memory_error(vm);
        return NJS_ERROR;
    }

    njs_fs_to_stat(stat, st);

    return njs_vm_external_create(vm, retval, njs_fs_stats_proto_id,
                                  stat, 0);
}


static njs_int_t
njs_fs_stats_test(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t testtype)
{
    unsigned    mask;
    njs_stat_t  *st;

    st = njs_vm_external(vm, njs_fs_stats_proto_id, njs_argument(args, 0));
    if (njs_slow_path(st == NULL)) {
        return NJS_DECLINED;
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

    st = njs_vm_external(vm, njs_fs_stats_proto_id, value);
    if (njs_slow_path(st == NULL)) {
        return NJS_DECLINED;
    }

    switch (njs_prop_magic32(prop) & 0xf) {
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

    switch (njs_prop_magic32(prop) >> 4) {
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


static njs_int_t
njs_fs_filehandle_close(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t unused)
{
    njs_value_t       retval;
    njs_filehandle_t  *fh;

    fh = njs_vm_external(vm, njs_fs_filehandle_proto_id, njs_argument(args, 0));
    if (njs_slow_path(fh == NULL)) {
        njs_type_error(vm, "\"this\" is not a filehandle object");
        return NJS_ERROR;
    }

    if (njs_slow_path(fh->fd == -1)) {
        njs_type_error(vm, "file was already closed");
        return NJS_ERROR;
    }

    (void) close(fh->fd);
    fh->fd = -1;

    njs_set_undefined(&retval);

    return njs_fs_result(vm, &retval, NJS_FS_PROMISE, NULL, 1);
}


static njs_int_t
njs_fs_filehandle_value_of(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t unused)
{
    njs_filehandle_t  *fh;

    fh = njs_vm_external(vm, njs_fs_filehandle_proto_id, njs_argument(args, 0));
    if (njs_slow_path(fh == NULL)) {
        njs_type_error(vm, "\"this\" is not a filehandle object");
        return NJS_ERROR;
    }

    njs_set_number(njs_vm_retval(vm), fh->fd);

    return NJS_OK;
}


static void
njs_fs_filehandle_cleanup(void *data)
{
    njs_filehandle_t  *fh = data;

    if (fh->vm != NULL && fh->fd != -1) {
        njs_vm_warn(fh->vm, "closing file description %d on cleanup\n", fh->fd);
        (void) close(fh->fd);
    }
}


static njs_int_t
njs_fs_filehandle_create(njs_vm_t *vm, int fd, njs_bool_t shadow,
    njs_value_t *retval)
{
    njs_filehandle_t  *fh;
    njs_mp_cleanup_t  *cln;

    fh = njs_mp_alloc(vm->mem_pool, sizeof(njs_filehandle_t));
    if (njs_slow_path(fh == NULL)) {
        njs_memory_error(vm);
        return NJS_ERROR;
    }

    fh->fd = fd;
    fh->vm = !shadow ? vm : NULL;

    cln = njs_mp_cleanup_add(njs_vm_memory_pool(vm), 0);
    if (cln == NULL) {
        njs_memory_error(vm);
        return NJS_ERROR;
    }

    cln->handler = njs_fs_filehandle_cleanup;
    cln->data = fh;

    return njs_vm_external_create(vm, retval, njs_fs_filehandle_proto_id,
                                  fh, 0);
}


static njs_int_t
njs_fs_bytes_read_create(njs_vm_t *vm, int bytes, njs_value_t *buffer,
    njs_value_t *retval)
{
    njs_bytes_struct_t  *bs;

    bs = njs_mp_alloc(vm->mem_pool, sizeof(njs_bytes_struct_t));
    if (njs_slow_path(bs == NULL)) {
        njs_memory_error(vm);
        return NJS_ERROR;
    }

    bs->bytes = bytes;
    njs_value_assign(&bs->buffer, buffer);

    return njs_vm_external_create(vm, retval, njs_fs_bytes_read_proto_id,
                                  bs, 0);
}


static njs_int_t
njs_fs_bytes_written_create(njs_vm_t *vm, int bytes, njs_value_t *buffer,
    njs_value_t *retval)
{
    njs_bytes_struct_t  *bs;

    bs = njs_mp_alloc(vm->mem_pool, sizeof(njs_bytes_struct_t));
    if (njs_slow_path(bs == NULL)) {
        njs_memory_error(vm);
        return NJS_ERROR;
    }

    bs->bytes = bytes;
    njs_value_assign(&bs->buffer, buffer);

    return njs_vm_external_create(vm, retval, njs_fs_bytes_written_proto_id,
                                  bs, 0);
}


static const njs_object_prop_t  njs_fs_promises_properties[] =
{
    NJS_DECLARE_PROP_NATIVE("readFile", njs_fs_read_file, 0, NJS_FS_PROMISE),

    NJS_DECLARE_PROP_NATIVE("readSync", njs_fs_read, 0, NJS_FS_PROMISE),

    NJS_DECLARE_PROP_NATIVE("appendFile", njs_fs_write_file, 0,
                            njs_fs_magic(NJS_FS_PROMISE, NJS_FS_APPEND)),

    NJS_DECLARE_PROP_NATIVE("writeFile", njs_fs_write_file, 0,
                            njs_fs_magic(NJS_FS_PROMISE, NJS_FS_TRUNC)),

    NJS_DECLARE_PROP_NATIVE("access", njs_fs_access, 0, NJS_FS_PROMISE),

    NJS_DECLARE_PROP_NATIVE("mkdir", njs_fs_mkdir, 0, NJS_FS_PROMISE),

    NJS_DECLARE_PROP_NATIVE("open", njs_fs_open, 0, NJS_FS_PROMISE),

    NJS_DECLARE_PROP_NATIVE("close", njs_fs_close, 0, NJS_FS_PROMISE),

    NJS_DECLARE_PROP_NATIVE("rename", njs_fs_rename, 0, NJS_FS_PROMISE),

    NJS_DECLARE_PROP_NATIVE("rmdir", njs_fs_rmdir, 0, NJS_FS_PROMISE),

    NJS_DECLARE_PROP_NATIVE("readdir", njs_fs_readdir, 0, NJS_FS_PROMISE),

    NJS_DECLARE_PROP_NATIVE("fstat", njs_fs_stat, 0,
                            njs_fs_magic(NJS_FS_PROMISE, NJS_FS_FSTAT)),

    NJS_DECLARE_PROP_NATIVE("lstat", njs_fs_stat, 0,
                            njs_fs_magic(NJS_FS_PROMISE, NJS_FS_LSTAT)),

    NJS_DECLARE_PROP_NATIVE("stat", njs_fs_stat, 0,
                            njs_fs_magic(NJS_FS_PROMISE, NJS_FS_STAT)),

    NJS_DECLARE_PROP_NATIVE("symlink", njs_fs_symlink, 0, NJS_FS_PROMISE),

    NJS_DECLARE_PROP_NATIVE("unlink", njs_fs_unlink, 0, NJS_FS_PROMISE),

    NJS_DECLARE_PROP_NATIVE("realpath", njs_fs_realpath, 0, NJS_FS_PROMISE),
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
    NJS_DECLARE_PROP_VALUE("F_OK", njs_value(NJS_NUMBER, 0, F_OK),
                           NJS_OBJECT_PROP_VALUE_E),

    NJS_DECLARE_PROP_VALUE("R_OK", njs_value(NJS_NUMBER, 0, R_OK),
                           NJS_OBJECT_PROP_VALUE_E),

    NJS_DECLARE_PROP_VALUE("W_OK", njs_value(NJS_NUMBER, 0, W_OK),
                           NJS_OBJECT_PROP_VALUE_E),

    NJS_DECLARE_PROP_VALUE("X_OK", njs_value(NJS_NUMBER, 0, X_OK),
                           NJS_OBJECT_PROP_VALUE_E),
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


static njs_int_t
njs_fs_init(njs_vm_t *vm)
{
    njs_int_t           ret, proto_id;
    njs_mod_t           *module;
    njs_opaque_value_t  value;

    if (vm->options.sandbox) {
        return NJS_OK;
    }

    njs_fs_stats_proto_id = njs_vm_external_prototype(vm, njs_ext_stats,
                                                    njs_nitems(njs_ext_stats));
    if (njs_slow_path(njs_fs_stats_proto_id < 0)) {
        return NJS_ERROR;
    }

    njs_fs_dirent_proto_id = njs_vm_external_prototype(vm, njs_ext_dirent,
                                                   njs_nitems(njs_ext_dirent));
    if (njs_slow_path(njs_fs_dirent_proto_id < 0)) {
        return NJS_ERROR;
    }

    njs_fs_filehandle_proto_id = njs_vm_external_prototype(vm,
                                               njs_ext_filehandle,
                                               njs_nitems(njs_ext_filehandle));
    if (njs_slow_path(njs_fs_filehandle_proto_id < 0)) {
        return NJS_ERROR;
    }

    njs_fs_bytes_read_proto_id = njs_vm_external_prototype(vm,
                                               njs_ext_bytes_read,
                                               njs_nitems(njs_ext_bytes_read));
    if (njs_slow_path(njs_fs_bytes_written_proto_id < 0)) {
        return NJS_ERROR;
    }

    njs_fs_bytes_written_proto_id = njs_vm_external_prototype(vm,
                                             njs_ext_bytes_written,
                                             njs_nitems(njs_ext_bytes_written));
    if (njs_slow_path(njs_fs_bytes_written_proto_id < 0)) {
        return NJS_ERROR;
    }

    proto_id = njs_vm_external_prototype(vm, njs_ext_fs,
                                         njs_nitems(njs_ext_fs));
    if (njs_slow_path(proto_id < 0)) {
        return NJS_ERROR;
    }

    ret = njs_vm_external_create(vm, njs_value_arg(&value), proto_id, NULL, 1);
    if (njs_slow_path(ret != NJS_OK)) {
        return NJS_ERROR;
    }

    module = njs_module_add(vm, &njs_str_value("fs"));
    if (njs_slow_path(module == NULL)) {
        return NJS_ERROR;
    }

    njs_value_assign(&module->value, &value);
    module->function.native = 1;

    return NJS_OK;
}
