
/*
 * Copyright (C) Dmitry Volyntsev
 * Copyright (C) F5, Inc.
 *
 * A native runner for the official TC39 test262 suite.
 *
 * njs implements strict mode only, so a non-strict or a raw variant
 *    cannot be executed and is a capability skip.
 */

#include <njs.h>
#include <njs_unix.h>
#include <njs_arr.h>
#include <njs_utils.h>

#include <dirent.h>
#include <sys/resource.h>
#include <sys/wait.h>


#define NJS_TEST262_OUT_SIZE       (64 * 1024)
#define NJS_TEST262_LINE_SIZE      8192
#define NJS_TEST262_ERR_SIZE       512
#define NJS_TEST262_MAX_FILE       (8 * 1024 * 1024)
#define NJS_TEST262_MAX_OUTPUT     4096
#define NJS_TEST262_MAX_MESSAGE    256
#define NJS_TEST262_MAX_JOBS       100000

/* Duplicate masks for the frontmatter keys that carry meaning. */
#define NJS_TEST262_META_NEGATIVE  1
#define NJS_TEST262_META_INCLUDES  2
#define NJS_TEST262_META_FEATURES  4
#define NJS_TEST262_META_FLAGS     8

#define NJS_TEST262_NEGATIVE_PHASE 1
#define NJS_TEST262_NEGATIVE_TYPE  2

#define NJS_TEST262_ONLY_STRICT    0x0001
#define NJS_TEST262_NO_STRICT      0x0002
#define NJS_TEST262_MODULE         0x0004
#define NJS_TEST262_RAW            0x0008
#define NJS_TEST262_ASYNC          0x0010
#define NJS_TEST262_GENERATED      0x0020
#define NJS_TEST262_CAN_BLOCK_NO   0x0040
#define NJS_TEST262_CAN_BLOCK_YES  0x0080
#define NJS_TEST262_NON_DET        0x0100


typedef struct njs_test262_s  njs_test262_t;


typedef enum {
    NJS_TEST262_MODE_STRICT = 0,
    NJS_TEST262_MODE_NON_STRICT,
    NJS_TEST262_MODE_BOTH,
} njs_test262_mode_t;


typedef enum {
    NJS_TEST262_POLICY_YES = 0,
    NJS_TEST262_POLICY_NO,
    NJS_TEST262_POLICY_SKIP,
} njs_test262_policy_t;


typedef enum {
    NJS_TEST262_VARIANT_RAW = 0,
    NJS_TEST262_VARIANT_MODULE,
    NJS_TEST262_VARIANT_NON_STRICT,
    NJS_TEST262_VARIANT_STRICT,
} njs_test262_variant_type_t;


typedef enum {
    NJS_TEST262_PHASE_NONE = 0,
    NJS_TEST262_PHASE_PARSE,
    NJS_TEST262_PHASE_RESOLUTION,
    NJS_TEST262_PHASE_RUNTIME,
} njs_test262_phase_t;


typedef enum {
    NJS_TEST262_PASS = 0,
    NJS_TEST262_FAIL,
    NJS_TEST262_SKIP,
    NJS_TEST262_CRASH,
    NJS_TEST262_INFRA,
    NJS_TEST262_STATUS_MAX,
} njs_test262_status_t;


typedef enum {
    NJS_TEST262_STAGE_NONE = 0,
    NJS_TEST262_STAGE_HARNESS,
    NJS_TEST262_STAGE_COMPILE,
    NJS_TEST262_STAGE_RESOLVE,
    NJS_TEST262_STAGE_RUN,
    NJS_TEST262_STAGE_JOBS,
} njs_test262_stage_t;


typedef struct {
    njs_str_t                   path;
    uint8_t                     dir;
    uint8_t                     prefix;
} njs_test262_path_t;


typedef struct {
    njs_str_t                   name;
    uint8_t                     skip;
} njs_test262_feature_t;


typedef struct {
    uint32_t                    flags;
    njs_test262_phase_t         phase;
    njs_str_t                   type;
    njs_arr_t                   *includes;
    njs_arr_t                   *features;
} njs_test262_metadata_t;


typedef struct {
    njs_str_t                   path;
    njs_test262_variant_type_t  type;
    uint32_t                    flags;
    njs_str_t                   reason;
    njs_str_t                   info;
} njs_test262_variant_t;


typedef struct {
    njs_str_t                   suite;
    njs_str_t                   failures;
    njs_test262_mode_t          mode;
    njs_test262_policy_t        async;
    njs_test262_policy_t        module;
} njs_test262_conf_t;


typedef struct {
    njs_str_t                   path;
    njs_test262_variant_type_t  type;
    uint8_t                     matched;
} njs_test262_expect_entry_t;


typedef enum {
    NJS_TEST262_SAME = 0,
    NJS_TEST262_NEW,
    NJS_TEST262_FIXED,
    NJS_TEST262_TRANSITION_MAX,
} njs_test262_transition_t;


typedef struct {
    njs_str_t                   name;
    njs_str_t                   source;
} njs_test262_harness_t;


typedef struct {
    njs_str_t                   file;
    njs_str_t                   source;
} njs_test262_source_t;


typedef struct {
    njs_test262_status_t        status;
    njs_test262_stage_t         stage;
    njs_str_t                   expected;
    njs_str_t                   actual;
    njs_str_t                   message;
    uint64_t                    elapsed;
} njs_test262_result_t;


typedef struct {
    uint32_t                    status;
    uint32_t                    stage;
    uint64_t                    elapsed;
    uint32_t                    expected;
    uint32_t                    actual;
    uint32_t                    message;
} njs_test262_wire_t;


typedef struct {
    njs_opaque_value_t          promise;
    njs_opaque_value_t          reason;
} njs_test262_rejected_t;


typedef enum {
    NJS_TEST262_ASYNC_PENDING = 0,
    NJS_TEST262_ASYNC_COMPLETE,
    NJS_TEST262_ASYNC_FAILED,
} njs_test262_async_t;


typedef struct {
    njs_test262_t               *ctx;
    njs_mp_t                    *pool;
    njs_arr_t                   *rejected;

    /*
     * Asynchronous completion is signalled through print(), which is the
     * interface INTERPRETING.md defines.  A second completion is recorded
     * separately: it is a defect in the test, not a result.
     */
    njs_test262_async_t         async;
    njs_str_t                   async_message;
    njs_uint_t                  completions;

    /*
     * njs resolves an import while parsing the importing module, so the
     * parse and the resolution phases collapse into one.  Recording that the
     * loader was the failing component is what keeps them apart.
     */
    njs_str_t                   dir;
    uint8_t                      resolving;
    uint8_t                      resolve_failed;

    /* Names the $262 operation a test asked for and the host cannot do. */
    njs_str_t                   missing;

    size_t                      size;
    u_char                      output[NJS_TEST262_MAX_OUTPUT];
} njs_test262_run_t;


typedef struct {
    int                         fd;
    size_t                      size;
    u_char                      buf[NJS_TEST262_OUT_SIZE];
} njs_test262_out_t;


typedef struct {
    size_t                      root_len;
    u_char                      path[NJS_MAX_PATH];
} njs_test262_walk_t;


struct njs_test262_s {
    njs_mp_t                    *pool;
    njs_test262_conf_t          conf;

    njs_arr_t                   *tests;
    njs_arr_t                   *excludes;
    njs_arr_t                   *features;
    njs_arr_t                   *args;
    njs_arr_t                   *files;
    njs_arr_t                   *variants;
    njs_arr_t                   *cache;
    njs_arr_t                   *expected;

    /* One slot per manifest entry, so a baseline can be written from it. */
    njs_test262_result_t        *results;

    njs_str_t                   suite;
    njs_str_t                   root;
    njs_str_t                   harness;
    njs_str_t                   emit;

    uint8_t                     list;
    uint8_t                     quiet;
    uint8_t                     verbose;

    uint8_t                     update;
    uint8_t                     cli_excludes;
    uint8_t                     isolation;
    uint32_t                    timeout;

    njs_uint_t                  infra_errors;
    njs_uint_t                  summary[NJS_TEST262_STATUS_MAX];
    njs_uint_t                  transitions[NJS_TEST262_TRANSITION_MAX];

    njs_test262_out_t           *out;
    size_t                      err_len;
    u_char                      err[NJS_TEST262_ERR_SIZE];
};


typedef struct {
    njs_str_t                   name;
    uint32_t                    flag;
} njs_test262_flag_t;


static const njs_test262_flag_t  njs_test262_flags[] = {
    { njs_str("onlyStrict"), NJS_TEST262_ONLY_STRICT },
    { njs_str("noStrict"), NJS_TEST262_NO_STRICT },
    { njs_str("module"), NJS_TEST262_MODULE },
    { njs_str("raw"), NJS_TEST262_RAW },
    { njs_str("async"), NJS_TEST262_ASYNC },
    { njs_str("generated"), NJS_TEST262_GENERATED },
    { njs_str("CanBlockIsFalse"), NJS_TEST262_CAN_BLOCK_NO },
    { njs_str("CanBlockIsTrue"), NJS_TEST262_CAN_BLOCK_YES },
    { njs_str("non-deterministic"), NJS_TEST262_NON_DET },
};


static const char  njs_test262_usage[] =
    "usage: njs_test262 [options] [path ...]\n"
    "\n"
    "  -c FILE, --config FILE   read configuration\n"
    "  -s DIR,  --suite DIR     set test262 checkout\n"
    "  -x PATH, --exclude PATH  add a file, directory, or prefix exclusion\n"
    "           --emit PATH     write a runnable script for one test\n"
    "  -f FILE, --failures FILE compare failures with FILE\n"
    "  -u,      --update-failures  replace FILE with current failures\n"
    "  -q,      --quiet         do not print the summary\n"
    "  -v,      --verbose       print every result, and the parsed metadata\n"
    "                           of every variant when listing\n"
    "           --list          list selected variants without running them\n"
    "           --isolation=process\n"
    "                           run each variant in a new child process\n"
    "           --timeout=N     abort a variant after N seconds\n"
#if NJS_TEST262_UNIT_TEST
    "           --unit-test     run the internal runner unit tests\n"
#endif
    "  -h,      --help          print usage\n"
    "\n"
    "Each positional path is relative to the test262 test/ directory and may\n"
    "name a file or a directory.  Multiple paths form a union.  Paths narrow\n"
    "the configured [tests] section and never re-enable excluded tests.\n"
    "\n"
    "--emit writes the selected script test with its harness files to stdout.\n"
    "It cannot materialize module tests.\n"
    "\n"
    "An exclusion is matched lexically, so that a path which is not present\n"
    "in the checkout can still be excluded: a trailing slash excludes a\n"
    "subtree, a trailing '*' matches a path prefix, otherwise the path names\n"
    "an exact file.\n"
    "\n"
    "--list writes tab separated records to stdout and the summary to stderr:\n"
    "\n"
    "  RUN|SKIP <TAB> path <TAB> variant <TAB> reason [<TAB> metadata]\n"
    "  INFRA    <TAB> path <TAB> -       <TAB> message\n"
    "\n"
    "A run writes the unexpected results to stdout and the summary to\n"
    "stderr.  Verbose mode adds every result and an EXEC record before each\n"
    "variant:\n"
    "\n"
    "  PASS|FAIL|SKIP|CRASH|INFRA <TAB> path <TAB> variant <TAB> stage <TAB> "
    "detail\n"
    "\n"
    "The test262 checkout is selected by --suite, then NJS_TEST262_DIR, then\n"
    "the \"suite\" configuration setting.\n"
    "\n"
    "Execution is sequential in this process by default.  --isolation=process\n"
    "forks one child per runnable variant so a child crash is recorded and\n"
    "the parent continues.  --timeout=N sets a per variant wall clock limit\n"
    "that is enforced with setrlimit(RLIMIT_CPU); without it isolation has\n"
    "no timeout.\n"
    "\n"
    "A failures file is a sorted set of path and variant pairs.  With one,\n"
    "the run reports new and fixed failures; a CRASH or INFRA result is\n"
    "neither and prevents --update-failures.  Updating is explicit and cannot\n"
    "be combined with --list.\n"
    "\n"
    "Exit status: 0 success, 1 test failures or baseline changes, 2 usage,\n"
    "configuration, suite or other infrastructure failure.\n";


static njs_int_t njs_test262_run(njs_test262_t *ctx, int argc, char **argv);


static njs_int_t
njs_test262_error(njs_test262_t *ctx, const char *fmt, ...)
{
    u_char   *p;
    va_list  args;

    va_start(args, fmt);
    p = njs_vsprintf(ctx->err, ctx->err + sizeof(ctx->err) - 1, fmt, args);
    va_end(args);

    ctx->err_len = p - ctx->err;
    *p = '\0';

    return NJS_ERROR;
}


static void
njs_test262_report_error(njs_test262_t *ctx)
{
    if (ctx->err_len != 0) {
        njs_stderror("njs_test262: %*s\n", ctx->err_len, ctx->err);
        ctx->err_len = 0;
    }
}


static void
njs_test262_flush(njs_test262_out_t *out)
{
    if (out == NULL) {
        return;
    }

    if (out->size != 0) {
        (void) njs_dprint(out->fd, out->buf, out->size);
        out->size = 0;
    }
}


static void
njs_test262_print(njs_test262_out_t *out, const char *fmt, ...)
{
    size_t   size;
    u_char   *p, line[NJS_TEST262_LINE_SIZE];
    va_list  args;

    va_start(args, fmt);
    p = njs_vsprintf(line, line + sizeof(line), fmt, args);
    va_end(args);

    size = p - line;

    if (out->size + size > sizeof(out->buf)) {
        njs_test262_flush(out);
    }

    memcpy(&out->buf[out->size], line, size);
    out->size += size;
}


static njs_int_t
njs_test262_str_dup(njs_test262_t *ctx, njs_str_t *dst, const njs_str_t *src)
{
    dst->start = njs_mp_alloc(ctx->pool, src->length + 1);
    if (dst->start == NULL) {
        return njs_test262_error(ctx, "memory allocation failed");
    }

    memcpy(dst->start, src->start, src->length);
    dst->start[src->length] = '\0';
    dst->length = src->length;

    return NJS_OK;
}


static njs_int_t
njs_test262_str_printf(njs_test262_t *ctx, njs_str_t *dst, const char *fmt,
    ...)
{
    u_char     *p, line[NJS_TEST262_LINE_SIZE];
    va_list    args;
    njs_str_t  str;

    va_start(args, fmt);
    p = njs_vsprintf(line, line + sizeof(line), fmt, args);
    va_end(args);

    str.start = line;
    str.length = p - line;

    return njs_test262_str_dup(ctx, dst, &str);
}


static njs_int_t
njs_test262_path_join(njs_test262_t *ctx, njs_str_t *dst, const njs_str_t *dir,
    const njs_str_t *name)
{
    u_char  *p;

    p = njs_mp_alloc(ctx->pool, dir->length + 1 + name->length + 1);
    if (p == NULL) {
        return njs_test262_error(ctx, "memory allocation failed");
    }

    dst->start = p;

    p = njs_cpymem(p, dir->start, dir->length);

    if (name->length != 0) {
        if (dir->length != 0) {
            *p++ = '/';
        }

        p = njs_cpymem(p, name->start, name->length);
    }

    *p = '\0';
    dst->length = p - dst->start;

    return NJS_OK;
}


/*
 * Converts a user supplied path into the canonical internal form: "/" as the
 * separator, no "." or ".." components, no leading or trailing separator and
 * relative to the test262 "test" directory.  A trailing '*' is preserved as
 * a prefix marker for exclusions.
 */
static njs_int_t
njs_test262_path_normalize(njs_test262_t *ctx, const njs_str_t *src,
    njs_test262_path_t *dst, njs_bool_t allow_prefix)
{
    u_char  *p, *start, *end, *out, *star;
    size_t  size;

    start = src->start;
    end = start + src->length;

    if (start != end && *start == '/') {
        return njs_test262_error(ctx, "path \"%V\" must be relative to the "
                                 "test262 test/ directory", src);
    }

    out = njs_mp_alloc(ctx->pool, src->length + 1);
    if (out == NULL) {
        return njs_test262_error(ctx, "memory allocation failed");
    }

    dst->path.start = out;
    dst->dir = 0;
    dst->prefix = 0;

    star = njs_strlchr(start, end, '*');

    if (star != NULL) {
        if (!allow_prefix || star != end - 1 || src->length == 1) {
            return njs_test262_error(ctx, "path \"%V\" must use a single "
                                     "trailing '*'", src);
        }

        end--;
        dst->prefix = 1;
    }

    while (start < end) {
        p = njs_strlchr(start, end, '/');
        if (p == NULL) {
            p = end;
        }

        size = p - start;

        if (size == 0 || (size == 1 && start[0] == '.')) {
            goto next;
        }

        if (size == 2 && start[0] == '.' && start[1] == '.') {
            if (out == dst->path.start) {
                return njs_test262_error(ctx, "path \"%V\" escapes the "
                                         "test262 test/ directory", src);
            }

            do {
                out--;
            } while (out != dst->path.start && out[-1] != '/');

            if (out != dst->path.start) {
                out--;
            }

            goto next;
        }

        if (out != dst->path.start) {
            *out++ = '/';
        }

        out = njs_cpymem(out, start, size);

    next:

        if (p == end) {
            break;
        }

        start = p + 1;
    }

    *out = '\0';
    dst->path.length = out - dst->path.start;

    if (src->length != 0 && src->start[src->length - 1] == '/') {
        dst->dir = 1;
    }

    return NJS_OK;
}


static njs_int_t
njs_test262_path_contains(const njs_test262_path_t *dir,
    const njs_test262_path_t *path)
{
    if (dir->prefix) {
        return path->path.length >= dir->path.length
               && memcmp(dir->path.start, path->path.start,
                         dir->path.length) == 0;
    }

    if (!dir->dir) {
        return dir->path.length == path->path.length
               && memcmp(dir->path.start, path->path.start,
                         path->path.length) == 0;
    }

    if (dir->path.length == 0) {
        return 1;
    }

    if (path->path.length < dir->path.length) {
        return 0;
    }

    if (memcmp(dir->path.start, path->path.start, dir->path.length) != 0) {
        return 0;
    }

    return path->path.length == dir->path.length
           || path->path.start[dir->path.length] == '/';
}


static int
njs_test262_str_cmp(const void *one, const void *two, void *ctx)
{
    size_t     size;
    njs_int_t  ret;

    const njs_str_t  *a = one;
    const njs_str_t  *b = two;

    size = njs_min(a->length, b->length);

    ret = memcmp(a->start, b->start, size);
    if (ret != 0) {
        return ret;
    }

    if (a->length == b->length) {
        return 0;
    }

    return (a->length < b->length) ? -1 : 1;
}


static njs_int_t
njs_test262_read_file(njs_test262_t *ctx, njs_mp_t *pool, const njs_str_t *path,
    njs_str_t *dst)
{
    int          fd;
    u_char       *p;
    size_t       size;
    ssize_t      n;
    njs_int_t    ret;
    struct stat  sb;

    fd = open((char *) path->start, O_RDONLY);
    if (fd < 0) {
        return njs_test262_error(ctx, "open(\"%V\") failed: %s", path,
                                 njs_errno_string(errno));
    }

    if (fstat(fd, &sb) != 0) {
        ret = njs_test262_error(ctx, "fstat(\"%V\") failed: %s", path,
                                njs_errno_string(errno));
        goto done;
    }

    if (!S_ISREG(sb.st_mode)) {
        ret = njs_test262_error(ctx, "\"%V\" is not a regular file", path);
        goto done;
    }

    if (sb.st_size > NJS_TEST262_MAX_FILE) {
        ret = njs_test262_error(ctx, "\"%V\" is too large", path);
        goto done;
    }

    size = sb.st_size;

    p = njs_mp_alloc(pool, size + 1);
    if (p == NULL) {
        ret = njs_test262_error(ctx, "memory allocation failed");
        goto done;
    }

    dst->start = p;
    dst->length = size;

    while (size != 0) {
        n = read(fd, p, size);
        if (n < 0) {
            ret = njs_test262_error(ctx, "read(\"%V\") failed: %s", path,
                                    njs_errno_string(errno));
            goto done;
        }

        if (n == 0) {
            ret = njs_test262_error(ctx, "\"%V\" was truncated while reading",
                                    path);
            goto done;
        }

        p += n;
        size -= n;
    }

    *p = '\0';
    ret = NJS_OK;

done:

    (void) close(fd);

    return ret;
}


static njs_int_t
njs_test262_is_test(const char *name, size_t len)
{
    static const njs_str_t  js = njs_str(".js");
    static const njs_str_t  fixture = njs_str("_FIXTURE.js");

    if (len <= js.length
        || memcmp(name + len - js.length, js.start, js.length) != 0)
    {
        return 0;
    }

    if (len >= fixture.length
        && memcmp(name + len - fixture.length, fixture.start,
                  fixture.length) == 0)
    {
        return 0;
    }

    return 1;
}


static njs_int_t
njs_test262_walk(njs_test262_t *ctx, njs_test262_walk_t *w, size_t len)
{
    DIR            *dir;
    size_t         size;
    njs_str_t      *file, rel;
    njs_int_t      ret;
    struct stat    sb;
    struct dirent  *de;

    w->path[len] = '\0';

    dir = opendir((char *) w->path);
    if (dir == NULL) {
        return njs_test262_error(ctx, "opendir(\"%s\") failed: %s", w->path,
                                 njs_errno_string(errno));
    }

    for ( ;; ) {
        errno = 0;

        de = readdir(dir);
        if (de == NULL) {
            if (errno != 0) {
                ret = njs_test262_error(ctx, "readdir(\"%s\") failed: %s",
                                        w->path, njs_errno_string(errno));
                goto done;
            }

            break;
        }

        if (de->d_name[0] == '.') {
            continue;
        }

        w->path[len] = '\0';

        size = njs_strlen(de->d_name);

        if (len + 1 + size >= sizeof(w->path)) {
            ret = njs_test262_error(ctx, "path \"%s/%s\" is too long",
                                    w->path, de->d_name);
            goto done;
        }

        w->path[len] = '/';
        memcpy(&w->path[len + 1], de->d_name, size + 1);

        /*
         * lstat() keeps enumeration inside the suite: a symlink would make
         * the normalized relative path meaningless and could form a cycle.
         */

        if (lstat((char *) w->path, &sb) != 0) {
            ret = njs_test262_error(ctx, "lstat(\"%s\") failed: %s", w->path,
                                    njs_errno_string(errno));
            goto done;
        }

        if (S_ISDIR(sb.st_mode)) {
            ret = njs_test262_walk(ctx, w, len + 1 + size);
            if (ret != NJS_OK) {
                goto done;
            }

            w->path[len] = '\0';

            continue;
        }

        if (!S_ISREG(sb.st_mode)
            || !njs_test262_is_test(de->d_name, size))
        {
            continue;
        }

        rel.start = &w->path[w->root_len];
        rel.length = len + 1 + size - w->root_len;

        file = njs_arr_add(ctx->files);
        if (file == NULL) {
            ret = njs_test262_error(ctx, "memory allocation failed");
            goto done;
        }

        ret = njs_test262_str_dup(ctx, file, &rel);
        if (ret != NJS_OK) {
            goto done;
        }
    }

    ret = NJS_OK;

done:

    (void) closedir(dir);

    return ret;
}


static njs_int_t
njs_test262_add_path(njs_test262_t *ctx, njs_arr_t *arr, const njs_str_t *src,
    njs_bool_t allow_prefix)
{
    njs_test262_path_t  *path;

    path = njs_arr_add(arr);
    if (path == NULL) {
        return njs_test262_error(ctx, "memory allocation failed");
    }

    return njs_test262_path_normalize(ctx, src, path, allow_prefix);
}


typedef struct {
    njs_str_t  key;
    njs_str_t  value;
    unsigned   has_value;
} njs_test262_entry_t;


static njs_int_t
njs_test262_conf_error(njs_test262_t *ctx, const njs_str_t *file,
    njs_uint_t line, const char *fmt, ...)
{
    u_char     *p, text[NJS_TEST262_LINE_SIZE];
    va_list    args;
    njs_str_t  str;

    va_start(args, fmt);
    p = njs_vsprintf(text, text + sizeof(text), fmt, args);
    va_end(args);

    str.start = text;
    str.length = p - text;

    return njs_test262_error(ctx, "%V:%ui: %V", file, line, &str);
}


static void
njs_test262_trim(njs_str_t *str)
{
    while (str->length != 0 && njs_is_whitespace(str->start[0])) {
        str->start++;
        str->length--;
    }

    while (str->length != 0
           && njs_is_whitespace(str->start[str->length - 1]))
    {
        str->length--;
    }
}


static u_char *
njs_test262_line_end(u_char *p, u_char *end)
{
    while (p < end && *p != '\n' && *p != '\r') {
        p++;
    }

    return p;
}


static u_char *
njs_test262_line_next(u_char *eol, u_char *end)
{
    if (eol == end) {
        return end;
    }

    if (*eol == '\r' && eol + 1 < end && eol[1] == '\n') {
        return eol + 2;
    }

    return eol + 1;
}


static size_t
njs_test262_indent(u_char *p, u_char *eol)
{
    size_t  indent;

    indent = 0;

    while (p + indent < eol && njs_is_whitespace(p[indent])) {
        indent++;
    }

    return indent;
}


static njs_int_t
njs_test262_bool(const njs_str_t *value, njs_test262_policy_t *dst)
{
    static const njs_str_t  yes = njs_str("yes");
    static const njs_str_t  no = njs_str("no");
    static const njs_str_t  skip = njs_str("skip");

    if (njs_strstr_eq(value, &yes)) {
        *dst = NJS_TEST262_POLICY_YES;
        return NJS_OK;
    }

    if (njs_strstr_eq(value, &no)) {
        *dst = NJS_TEST262_POLICY_NO;
        return NJS_OK;
    }

    if (njs_strstr_eq(value, &skip)) {
        *dst = NJS_TEST262_POLICY_SKIP;
        return NJS_OK;
    }

    return NJS_ERROR;
}


typedef enum {
    NJS_TEST262_CONF_SUITE = 0,
    NJS_TEST262_CONF_MODE,
    NJS_TEST262_CONF_ASYNC,
    NJS_TEST262_CONF_MODULE,
    NJS_TEST262_CONF_FAILURES,
    NJS_TEST262_CONF_MAX,
} njs_test262_conf_key_t;


static const njs_str_t  njs_test262_conf_keys[NJS_TEST262_CONF_MAX] = {
    [NJS_TEST262_CONF_SUITE]        = njs_str("suite"),
    [NJS_TEST262_CONF_MODE]         = njs_str("mode"),
    [NJS_TEST262_CONF_ASYNC]        = njs_str("async"),
    [NJS_TEST262_CONF_MODULE]       = njs_str("module"),
    [NJS_TEST262_CONF_FAILURES]     = njs_str("failures"),
};

typedef char njs_test262_conf_mask_fits[NJS_TEST262_CONF_MAX <= 32 ? 1 : -1];


static njs_int_t
njs_test262_conf_config(njs_test262_t *ctx, const njs_str_t *file,
    njs_uint_t line, njs_test262_entry_t *entry, uint32_t *seen)
{
    uint32_t                bit;
    njs_int_t               ret;
    njs_test262_conf_key_t  key;

    static const njs_str_t  strict = njs_str("strict");
    static const njs_str_t  non_strict = njs_str("non-strict");
    static const njs_str_t  both = njs_str("both");

    if (!entry->has_value) {
        return njs_test262_conf_error(ctx, file, line,
                                      "\"%V\" requires a value", &entry->key);
    }

    for (key = 0; key < NJS_TEST262_CONF_MAX; key++) {
        if (njs_strstr_eq(&entry->key, &njs_test262_conf_keys[key])) {
            break;
        }
    }

    if (key == NJS_TEST262_CONF_MAX) {
        return njs_test262_conf_error(ctx, file, line, "unknown [config] key "
                                      "\"%V\"", &entry->key);
    }

    bit = ((uint32_t) 1) << key;

    if (*seen & bit) {
        return njs_test262_conf_error(ctx, file, line, "duplicate [config] "
                                      "key \"%V\"", &entry->key);
    }

    *seen |= bit;

    switch (key) {

    case NJS_TEST262_CONF_SUITE:
        return njs_test262_str_dup(ctx, &ctx->conf.suite, &entry->value);

    case NJS_TEST262_CONF_FAILURES:
        /* A relative path is resolved against the current directory. */

        return njs_test262_str_dup(ctx, &ctx->conf.failures, &entry->value);

    case NJS_TEST262_CONF_MODE:
        if (njs_strstr_eq(&entry->value, &strict)) {
            ctx->conf.mode = NJS_TEST262_MODE_STRICT;

        } else if (njs_strstr_eq(&entry->value, &non_strict)) {
            ctx->conf.mode = NJS_TEST262_MODE_NON_STRICT;

        } else if (njs_strstr_eq(&entry->value, &both)) {
            ctx->conf.mode = NJS_TEST262_MODE_BOTH;

        } else {
            return njs_test262_conf_error(ctx, file, line,
                                          "invalid mode \"%V\", expected "
                                          "strict, non-strict or both",
                                          &entry->value);
        }

        return NJS_OK;

    case NJS_TEST262_CONF_ASYNC:
        ret = njs_test262_bool(&entry->value, &ctx->conf.async);
        goto policy;

    case NJS_TEST262_CONF_MODULE:
        ret = njs_test262_bool(&entry->value, &ctx->conf.module);
        goto policy;

    case NJS_TEST262_CONF_MAX:
        break;
    }

    return njs_test262_conf_error(ctx, file, line, "unhandled [config] key "
                                  "\"%V\"", &entry->key);

policy:

    if (ret != NJS_OK) {
        return njs_test262_conf_error(ctx, file, line, "invalid value \"%V\" "
                                      "for \"%V\", expected yes, no or skip",
                                      &entry->value, &entry->key);
    }

    return NJS_OK;
}


static njs_int_t
njs_test262_conf_feature(njs_test262_t *ctx, const njs_str_t *file,
    njs_uint_t line, njs_test262_entry_t *entry)
{
    njs_int_t              ret;
    njs_test262_feature_t  *feature;

    static const njs_str_t  yes = njs_str("yes");
    static const njs_str_t  skip = njs_str("skip");

    feature = njs_arr_add(ctx->features);
    if (feature == NULL) {
        return njs_test262_error(ctx, "memory allocation failed");
    }

    ret = njs_test262_str_dup(ctx, &feature->name, &entry->key);
    if (ret != NJS_OK) {
        return ret;
    }

    if (!entry->has_value || njs_strstr_eq(&entry->value, &yes)) {
        feature->skip = 0;
        return NJS_OK;
    }

    if (njs_strstr_eq(&entry->value, &skip)) {
        feature->skip = 1;
        return NJS_OK;
    }

    return njs_test262_conf_error(ctx, file, line, "invalid value \"%V\" for "
                                  "feature \"%V\", expected yes or skip",
                                  &entry->value, &entry->key);
}


static int
njs_test262_feature_cmp(const void *one, const void *two, void *ctx)
{
    const njs_test262_feature_t  *a = one;
    const njs_test262_feature_t  *b = two;

    return njs_test262_str_cmp(&a->name, &b->name, ctx);
}


static njs_test262_feature_t *
njs_test262_feature_find(njs_test262_t *ctx, const njs_str_t *name)
{
    njs_int_t              ret;
    njs_uint_t             lo, hi, mid;
    njs_test262_feature_t  *features;

    features = ctx->features->start;

    lo = 0;
    hi = ctx->features->items;

    while (lo < hi) {
        mid = lo + (hi - lo) / 2;

        ret = njs_test262_str_cmp(&features[mid].name, name, NULL);

        if (ret == 0) {
            return &features[mid];
        }

        if (ret < 0) {
            lo = mid + 1;

        } else {
            hi = mid;
        }
    }

    return NULL;
}


static njs_int_t
njs_test262_conf_parse(njs_test262_t *ctx, const njs_str_t *file,
    const njs_str_t *source)
{
    u_char                 *p, *end, *eol;
    uint32_t               seen;
    njs_int_t              ret;
    njs_str_t              line;
    njs_uint_t             i, lineno;
    njs_test262_entry_t    entry;
    njs_test262_feature_t  *feature;

    enum {
        NJS_TEST262_SECTION_NONE = 0,
        NJS_TEST262_SECTION_CONFIG,
        NJS_TEST262_SECTION_FEATURES,
        NJS_TEST262_SECTION_EXCLUDE,
        NJS_TEST262_SECTION_TESTS,
    } section;

    static const njs_str_t  config = njs_str("[config]");
    static const njs_str_t  features = njs_str("[features]");
    static const njs_str_t  exclude = njs_str("[exclude]");
    static const njs_str_t  tests = njs_str("[tests]");

    p = source->start;
    end = p + source->length;

    section = NJS_TEST262_SECTION_NONE;
    lineno = 0;
    seen = 0;

    while (p < end) {
        eol = njs_test262_line_end(p, end);

        line.start = p;
        line.length = eol - p;

        p = njs_test262_line_next(eol, end);
        lineno++;

        njs_test262_trim(&line);

        if (line.length == 0 || line.start[0] == '#') {
            continue;
        }

        if (line.start[0] == '[') {
            if (njs_strstr_eq(&line, &config)) {
                section = NJS_TEST262_SECTION_CONFIG;

            } else if (njs_strstr_eq(&line, &features)) {
                section = NJS_TEST262_SECTION_FEATURES;

            } else if (njs_strstr_eq(&line, &exclude)) {
                section = NJS_TEST262_SECTION_EXCLUDE;

            } else if (njs_strstr_eq(&line, &tests)) {
                section = NJS_TEST262_SECTION_TESTS;

            } else {
                return njs_test262_conf_error(ctx, file, lineno,
                                              "unknown section \"%V\"", &line);
            }

            continue;
        }

        if (section == NJS_TEST262_SECTION_NONE) {
            return njs_test262_conf_error(ctx, file, lineno, "\"%V\" appears "
                                          "before any section", &line);
        }

        eol = njs_strlchr(line.start, line.start + line.length, '=');

        entry.key = line;
        entry.value.start = NULL;
        entry.value.length = 0;
        entry.has_value = 0;

        if (eol != NULL) {
            entry.key.length = eol - line.start;
            entry.value.start = eol + 1;
            entry.value.length = line.start + line.length - (eol + 1);
            entry.has_value = 1;

            njs_test262_trim(&entry.key);
            njs_test262_trim(&entry.value);
        }

        if (entry.key.length == 0) {
            return njs_test262_conf_error(ctx, file, lineno,
                                          "empty key in \"%V\"", &line);
        }

        switch (section) {
        case NJS_TEST262_SECTION_CONFIG:
            ret = njs_test262_conf_config(ctx, file, lineno, &entry, &seen);
            break;

        case NJS_TEST262_SECTION_FEATURES:
            ret = njs_test262_conf_feature(ctx, file, lineno, &entry);
            break;

        case NJS_TEST262_SECTION_EXCLUDE:
        case NJS_TEST262_SECTION_TESTS:
            if (entry.has_value) {
                return njs_test262_conf_error(ctx, file, lineno, "\"%V\" does "
                                              "not accept a value", &line);
            }

            ret = njs_test262_add_path(ctx,
                        (section == NJS_TEST262_SECTION_EXCLUDE)
                            ? ctx->excludes : ctx->tests, &line,
                        section == NJS_TEST262_SECTION_EXCLUDE);

            if (ret != NJS_OK) {
                return njs_test262_conf_error(ctx, file, lineno, "%*s",
                                              ctx->err_len, ctx->err);
            }

            break;

        default:
            ret = NJS_OK;
            break;
        }

        if (ret != NJS_OK) {
            return ret;
        }
    }

    njs_qsort(ctx->features->start, ctx->features->items,
              sizeof(njs_test262_feature_t), njs_test262_feature_cmp, NULL);

    /*
     * A duplicate would make the binary search return an arbitrary one of
     * the conflicting policies.
     */

    feature = ctx->features->start;

    for (i = 1; i < ctx->features->items; i++) {
        if (njs_strstr_eq(&feature[i - 1].name, &feature[i].name)) {
            return njs_test262_error(ctx, "%V: duplicate feature \"%V\"", file,
                                     &feature[i].name);
        }
    }

    return NJS_OK;
}


static njs_int_t
njs_test262_conf_read(njs_test262_t *ctx, const njs_str_t *file)
{
    njs_str_t  source;
    njs_int_t  ret;

    ret = njs_test262_read_file(ctx, ctx->pool, file, &source);
    if (ret != NJS_OK) {
        return ret;
    }

    return njs_test262_conf_parse(ctx, file, &source);
}


/*
 * test262 frontmatter parsing.  This is a schema specific reader, not a
 * general YAML implementation: only the keys that change execution semantics
 * are interpreted, everything else is skipped without interpreting its
 * contents as top level keys.
 */

static njs_int_t
njs_test262_meta_error(njs_test262_t *ctx, const njs_str_t *path,
    njs_uint_t line, const char *fmt, ...)
{
    u_char     *p, text[NJS_TEST262_LINE_SIZE];
    va_list    args;
    njs_str_t  str;

    va_start(args, fmt);
    p = njs_vsprintf(text, text + sizeof(text), fmt, args);
    va_end(args);

    str.start = text;
    str.length = p - text;

    return njs_test262_error(ctx, "%V:%ui: %V", path, line, &str);
}


/*
 * The interpreted values are plain YAML scalars in the whole suite.  Anything
 * that would require real YAML semantics is rejected loudly instead of being
 * misread as a filename, a feature or an error type.
 */

static njs_int_t
njs_test262_meta_scalar(njs_test262_t *ctx, const njs_str_t *path,
    njs_uint_t line, const njs_str_t *key, const njs_str_t *value)
{
    size_t  i;

    static const njs_str_t  reserved = njs_str("\"'#[]{},&*!|>%@`");

    for (i = 0; i < value->length; i++) {
        if (njs_strlchr(reserved.start, reserved.start + reserved.length,
                        value->start[i]) != NULL)
        {
            return njs_test262_meta_error(ctx, path, line, "\"%V\" value "
                                          "\"%V\" needs YAML semantics that "
                                          "the runner does not implement",
                                          key, value);
        }
    }

    return NJS_OK;
}


static njs_int_t
njs_test262_meta_add(njs_test262_t *ctx, njs_mp_t *pool, njs_arr_t **arr,
    njs_str_t *item)
{
    njs_str_t  *dst;

    if (*arr == NULL) {
        *arr = njs_arr_create(pool, 4, sizeof(njs_str_t));
        if (*arr == NULL) {
            return njs_test262_error(ctx, "memory allocation failed");
        }
    }

    dst = njs_arr_add(*arr);
    if (dst == NULL) {
        return njs_test262_error(ctx, "memory allocation failed");
    }

    *dst = *item;

    return NJS_OK;
}


static njs_int_t
njs_test262_meta_inline(njs_test262_t *ctx, njs_mp_t *pool,
    const njs_str_t *path, njs_uint_t line, const njs_str_t *key,
    njs_str_t *value, njs_arr_t **arr)
{
    u_char     *p, *end, *comma;
    njs_int_t  ret;
    njs_str_t  item;

    if (value->length < 2 || value->start[0] != '['
        || value->start[value->length - 1] != ']')
    {
        return njs_test262_meta_error(ctx, path, line, "\"%V\" expects an "
                                      "inline or indented array, got \"%V\"",
                                      key, value);
    }

    p = value->start + 1;
    end = value->start + value->length - 1;

    for ( ;; ) {
        comma = njs_strlchr(p, end, ',');
        if (comma == NULL) {
            comma = end;
        }

        item.start = p;
        item.length = comma - p;

        njs_test262_trim(&item);

        if (item.length != 0) {
            ret = njs_test262_meta_scalar(ctx, path, line, key, &item);
            if (ret != NJS_OK) {
                return ret;
            }

            ret = njs_test262_meta_add(ctx, pool, arr, &item);
            if (ret != NJS_OK) {
                return ret;
            }
        }

        if (comma == end) {
            break;
        }

        p = comma + 1;
    }

    return NJS_OK;
}


static njs_int_t
njs_test262_meta_flags(njs_test262_t *ctx, const njs_str_t *path,
    njs_uint_t line, njs_arr_t *arr, uint32_t *flags)
{
    njs_str_t   *item;
    njs_uint_t  i, j;

    if (arr == NULL) {
        return NJS_OK;
    }

    item = arr->start;

    for (i = 0; i < arr->items; i++) {
        for (j = 0; j < njs_nitems(njs_test262_flags); j++) {
            if (njs_strstr_eq(&item[i], &njs_test262_flags[j].name)) {
                *flags |= njs_test262_flags[j].flag;
                break;
            }
        }

        if (j == njs_nitems(njs_test262_flags)) {
            return njs_test262_meta_error(ctx, path, line, "unknown flag "
                                          "\"%V\"", &item[i]);
        }
    }

    return NJS_OK;
}


static njs_int_t
njs_test262_meta_negative(njs_test262_t *ctx, const njs_str_t *path,
    njs_uint_t line, njs_str_t *block, njs_test262_metadata_t *meta)
{
    u_char      *p, *end, *eol, *colon;
    uint32_t    seen;
    njs_int_t   ret;
    njs_str_t   text, key, value;
    njs_uint_t  lineno;

    static const njs_str_t  phase = njs_str("phase");
    static const njs_str_t  type = njs_str("type");
    static const njs_str_t  parse = njs_str("parse");
    static const njs_str_t  early = njs_str("early");
    static const njs_str_t  resolution = njs_str("resolution");
    static const njs_str_t  runtime = njs_str("runtime");

    p = block->start;
    end = p + block->length;
    lineno = line;
    seen = 0;

    while (p < end) {
        eol = njs_test262_line_end(p, end);

        text.start = p;
        text.length = eol - p;

        p = njs_test262_line_next(eol, end);
        lineno++;

        njs_test262_trim(&text);

        if (text.length == 0 || text.start[0] == '#') {
            continue;
        }

        colon = njs_strlchr(text.start, text.start + text.length, ':');
        if (colon == NULL) {
            return njs_test262_meta_error(ctx, path, lineno, "malformed "
                                          "negative entry \"%V\"", &text);
        }

        key.start = text.start;
        key.length = colon - text.start;

        value.start = colon + 1;
        value.length = text.start + text.length - (colon + 1);

        njs_test262_trim(&key);
        njs_test262_trim(&value);

        ret = njs_test262_meta_scalar(ctx, path, lineno, &key, &value);
        if (ret != NJS_OK) {
            return ret;
        }

        if (njs_strstr_eq(&key, &phase)) {
            if (seen & NJS_TEST262_NEGATIVE_PHASE) {
                return njs_test262_meta_error(ctx, path, lineno, "duplicate "
                                              "negative key \"phase\"");
            }

            seen |= NJS_TEST262_NEGATIVE_PHASE;

            if (njs_strstr_eq(&value, &parse)
                || njs_strstr_eq(&value, &early))
            {
                meta->phase = NJS_TEST262_PHASE_PARSE;

            } else if (njs_strstr_eq(&value, &resolution)) {
                meta->phase = NJS_TEST262_PHASE_RESOLUTION;

            } else if (njs_strstr_eq(&value, &runtime)) {
                meta->phase = NJS_TEST262_PHASE_RUNTIME;

            } else {
                return njs_test262_meta_error(ctx, path, lineno, "unknown "
                                              "negative phase \"%V\"", &value);
            }

            continue;
        }

        if (njs_strstr_eq(&key, &type)) {
            if (seen & NJS_TEST262_NEGATIVE_TYPE) {
                return njs_test262_meta_error(ctx, path, lineno, "duplicate "
                                              "negative key \"type\"");
            }

            seen |= NJS_TEST262_NEGATIVE_TYPE;
            meta->type = value;

            continue;
        }

        return njs_test262_meta_error(ctx, path, lineno, "unknown negative "
                                      "key \"%V\"", &key);
    }

    if (meta->phase == NJS_TEST262_PHASE_NONE || meta->type.length == 0) {
        return njs_test262_meta_error(ctx, path, line, "negative requires "
                                      "both phase and type");
    }

    return NJS_OK;
}


static njs_int_t
njs_test262_meta_block(njs_test262_t *ctx, njs_mp_t *pool,
    const njs_str_t *path, njs_uint_t line, const njs_str_t *key,
    njs_str_t *block, njs_arr_t **arr)
{
    u_char      *p, *end, *eol;
    njs_int_t   ret;
    njs_str_t   text;
    njs_uint_t  lineno;

    p = block->start;
    end = p + block->length;
    lineno = line;

    while (p < end) {
        eol = njs_test262_line_end(p, end);

        text.start = p;
        text.length = eol - p;

        p = njs_test262_line_next(eol, end);
        lineno++;

        njs_test262_trim(&text);

        if (text.length == 0 || text.start[0] == '#') {
            continue;
        }

        if (text.length < 2 || text.start[0] != '-'
            || !njs_is_whitespace(text.start[1]))
        {
            return njs_test262_meta_error(ctx, path, lineno, "\"%V\" expects "
                                          "a \"- item\" entry, got \"%V\"",
                                          key, &text);
        }

        text.start += 2;
        text.length -= 2;

        njs_test262_trim(&text);

        if (text.length == 0) {
            continue;
        }

        ret = njs_test262_meta_scalar(ctx, path, lineno, key, &text);
        if (ret != NJS_OK) {
            return ret;
        }

        ret = njs_test262_meta_add(ctx, pool, arr, &text);
        if (ret != NJS_OK) {
            return ret;
        }
    }

    return NJS_OK;
}


static njs_int_t
njs_test262_metadata(njs_test262_t *ctx, njs_mp_t *pool, const njs_str_t *path,
    const njs_str_t *source, njs_test262_metadata_t *meta)
{
    u_char      *p, *end, *eol, *colon, *body;
    size_t      indent, base;
    uint32_t    seen, bit;
    njs_arr_t   *tmp, **arr;
    njs_int_t   ret;
    njs_str_t   line, key, value, block;
    njs_uint_t  lineno, cur, block_line;

    static const njs_str_t  open = njs_str("/*---");
    static const njs_str_t  close = njs_str("---*/");

    static const njs_str_t  flags = njs_str("flags");
    static const njs_str_t  includes = njs_str("includes");
    static const njs_str_t  features = njs_str("features");
    static const njs_str_t  negative = njs_str("negative");

    njs_memzero(meta, sizeof(njs_test262_metadata_t));

    p = source->start;
    end = p + source->length;

    /*
     * Both delimiters are recognized only as delimiter lines.  A substring
     * search would start at an opening marker inside a string literal and
     * would end the frontmatter at a closing marker inside a block scalar.
     */

    body = NULL;
    lineno = 0;

    while (p < end) {
        eol = njs_test262_line_end(p, end);
        lineno++;

        line.start = p;
        line.length = eol - p;

        njs_test262_trim(&line);

        if (njs_strstr_eq(&line, &open)) {
            body = njs_test262_line_next(eol, end);
            break;
        }

        p = njs_test262_line_next(eol, end);
    }

    if (body == NULL) {
        return njs_test262_meta_error(ctx, path, 1, "no test262 frontmatter "
                                      "found");
    }

    cur = lineno;

    for (p = body; p < end; p = njs_test262_line_next(eol, end)) {
        eol = njs_test262_line_end(p, end);

        line.start = p;
        line.length = eol - p;

        njs_test262_trim(&line);

        if (njs_strstr_eq(&line, &close)) {
            break;
        }
    }

    if (p == end) {
        return njs_test262_meta_error(ctx, path, cur, "unterminated test262 "
                                      "frontmatter");
    }

    end = p;
    p = body;
    lineno = cur + 1;
    seen = 0;

    /*
     * The frontmatter is valid YAML, so its top level keys may share any
     * indentation as long as it is consistent.  The first non blank line
     * defines it.
     */

    base = (size_t) -1;

    while (p < end) {
        eol = njs_test262_line_end(p, end);

        line.start = p;
        line.length = eol - p;

        cur = lineno;

        p = njs_test262_line_next(eol, end);
        lineno++;

        indent = njs_test262_indent(line.start, eol);

        if (indent == line.length) {
            continue;
        }

        if (base == (size_t) -1) {
            base = indent;

        } else if (indent != base) {
            return njs_test262_meta_error(ctx, path, cur, "unexpected "
                                          "indented line at the top level");
        }

        line.start += indent;
        line.length -= indent;

        if (line.start[0] == '#') {
            continue;
        }

        colon = njs_strlchr(line.start, line.start + line.length, ':');
        if (colon == NULL) {
            return njs_test262_meta_error(ctx, path, cur, "malformed "
                                          "frontmatter line \"%V\"", &line);
        }

        key.start = line.start;
        key.length = colon - line.start;

        value.start = colon + 1;
        value.length = line.start + line.length - (colon + 1);

        njs_test262_trim(&key);
        njs_test262_trim(&value);

        /*
         * Every following blank or indented line belongs to this key, which
         * keeps block scalars from being mistaken for top level keys.
         */

        block.start = p;
        block_line = cur;

        while (p < end) {
            eol = njs_test262_line_end(p, end);

            indent = njs_test262_indent(p, eol);

            if (p + indent != eol && indent <= base) {
                break;
            }

            p = njs_test262_line_next(eol, end);
            lineno++;
        }

        block.length = p - block.start;

        if (njs_strstr_eq(&key, &negative)) {
            bit = NJS_TEST262_META_NEGATIVE;
            arr = NULL;

        } else if (njs_strstr_eq(&key, &includes)) {
            bit = NJS_TEST262_META_INCLUDES;
            arr = &meta->includes;

        } else if (njs_strstr_eq(&key, &features)) {
            bit = NJS_TEST262_META_FEATURES;
            arr = &meta->features;

        } else if (njs_strstr_eq(&key, &flags)) {
            bit = NJS_TEST262_META_FLAGS;
            tmp = NULL;
            arr = &tmp;

        } else {
            continue;
        }

        /*
         * A repeated key is invalid YAML.  Appending or overwriting would
         * silently derive variants from metadata a YAML reader does not see.
         */

        if (seen & bit) {
            return njs_test262_meta_error(ctx, path, block_line, "duplicate "
                                          "key \"%V\"", &key);
        }

        seen |= bit;

        if (arr == NULL) {
            if (value.length != 0) {
                return njs_test262_meta_error(ctx, path, block_line,
                                              "\"negative\" expects an "
                                              "indented mapping");
            }

            ret = njs_test262_meta_negative(ctx, path, block_line, &block,
                                            meta);
            if (ret != NJS_OK) {
                return ret;
            }

            continue;
        }

        if (value.length != 0) {
            ret = njs_test262_meta_inline(ctx, pool, path, block_line, &key,
                                          &value, arr);

        } else {
            ret = njs_test262_meta_block(ctx, pool, path, block_line, &key,
                                         &block, arr);
        }

        if (ret != NJS_OK) {
            return ret;
        }

        if (arr == &tmp) {
            ret = njs_test262_meta_flags(ctx, path, block_line, tmp,
                                         &meta->flags);
            if (ret != NJS_OK) {
                return ret;
            }
        }
    }

    return NJS_OK;
}


static const char *
njs_test262_variant_name(njs_test262_variant_type_t type)
{
    switch (type) {
    case NJS_TEST262_VARIANT_RAW:
        return "raw";

    case NJS_TEST262_VARIANT_MODULE:
        return "module";

    case NJS_TEST262_VARIANT_NON_STRICT:
        return "non-strict";

    default:
        return "strict";
    }
}


static const char *
njs_test262_phase_name(njs_test262_phase_t phase)
{
    switch (phase) {
    case NJS_TEST262_PHASE_PARSE:
        return "parse";

    case NJS_TEST262_PHASE_RESOLUTION:
        return "resolution";

    case NJS_TEST262_PHASE_RUNTIME:
        return "runtime";

    default:
        return "none";
    }
}


static u_char *
njs_test262_info_list(u_char *p, u_char *end, const char *name, njs_arr_t *arr)
{
    njs_str_t   *item;
    njs_uint_t  i;

    p = njs_sprintf(p, end, " %s=", name);

    if (arr == NULL) {
        return p;
    }

    item = arr->start;

    for (i = 0; i < arr->items; i++) {
        p = njs_sprintf(p, end, "%V,", &item[i]);
    }

    return (arr->items != 0) ? p - 1 : p;
}


static njs_int_t
njs_test262_variant_info(njs_test262_t *ctx, const njs_str_t *path,
    njs_test262_variant_t *variant, njs_test262_metadata_t *meta)
{
    u_char      *p, *end, text[NJS_TEST262_LINE_SIZE];
    njs_str_t   str;
    njs_uint_t  i;

    p = text;
    end = text + sizeof(text);

    p = njs_sprintf(p, end, "flags=");

    for (i = 0; i < njs_nitems(njs_test262_flags); i++) {
        if (meta->flags & njs_test262_flags[i].flag) {
            p = njs_sprintf(p, end, "%V,", &njs_test262_flags[i].name);
        }
    }

    if (p[-1] == ',') {
        p--;
    }

    p = njs_sprintf(p, end, " negative=%s",
                    njs_test262_phase_name(meta->phase));

    if (meta->phase != NJS_TEST262_PHASE_NONE) {
        p = njs_sprintf(p, end, ":%V", &meta->type);
    }

    p = njs_test262_info_list(p, end, "includes", meta->includes);
    p = njs_test262_info_list(p, end, "features", meta->features);

    if (p >= end - 1) {
        return njs_test262_error(ctx, "%V: metadata digest does not fit into "
                                 "%uz bytes", path, sizeof(text));
    }

    str.start = text;
    str.length = p - text;

    return njs_test262_str_dup(ctx, &variant->info, &str);
}


static njs_int_t
njs_test262_variant_add(njs_test262_t *ctx, const njs_str_t *path,
    njs_test262_variant_type_t type, njs_test262_metadata_t *meta,
    const njs_str_t *reason)
{
    njs_int_t              ret;
    njs_str_t              *feature;
    njs_uint_t             i;
    njs_test262_variant_t  *variant;
    njs_test262_feature_t  *known;

    variant = njs_arr_add(ctx->variants);
    if (variant == NULL) {
        return njs_test262_error(ctx, "memory allocation failed");
    }

    njs_memzero(variant, sizeof(njs_test262_variant_t));

    variant->path = *path;
    variant->type = type;
    variant->flags = meta->flags;

    if (ctx->verbose) {
        ret = njs_test262_variant_info(ctx, path, variant, meta);
        if (ret != NJS_OK) {
            return ret;
        }
    }

    if (reason != NULL) {
        return njs_test262_str_dup(ctx, &variant->reason, reason);
    }

    if (meta->features != NULL) {
        feature = meta->features->start;

        for (i = 0; i < meta->features->items; i++) {
            known = njs_test262_feature_find(ctx, &feature[i]);

            if (known == NULL) {
                return njs_test262_str_printf(ctx, &variant->reason,
                                              "unknown feature \"%V\"",
                                              &feature[i]);
            }

            if (known->skip) {
                return njs_test262_str_printf(ctx, &variant->reason,
                                              "feature \"%V\" is skipped",
                                              &feature[i]);
            }
        }
    }

    if ((meta->flags & NJS_TEST262_ASYNC)
        && ctx->conf.async == NJS_TEST262_POLICY_SKIP)
    {
        return njs_test262_str_printf(ctx, &variant->reason,
                                      "async tests are skipped");
    }

    /*
     * Host capability skips.  Each one names what is missing instead of
     * being folded into a generic feature skip.
     *
     * njs implements ES5.1 strict mode only, which was verified rather than
     * assumed: an assignment to an undeclared name throws, "this" is
     * undefined in a plain call, and both "with" and a legacy octal literal
     * are syntax errors.
     */

    if (type == NJS_TEST262_VARIANT_NON_STRICT
        || type == NJS_TEST262_VARIANT_RAW)
    {
        return njs_test262_str_printf(ctx, &variant->reason, "the njs engine "
                                      "implements strict mode only");
    }

    return NJS_OK;
}


/*
 * A file yields one variant per executable strictness, and the configured
 * mode decides which of them exist.  A file that the mode leaves without any
 * executable variant is still reported once, with the reason, so that --list
 * accounts for every selected file.
 */
static njs_int_t
njs_test262_variants(njs_test262_t *ctx, const njs_str_t *path,
    njs_test262_metadata_t *meta)
{
    njs_int_t   ret;
    njs_str_t   reason;
    njs_bool_t  skipped;

    if ((meta->flags & NJS_TEST262_ASYNC)
        && ctx->conf.async == NJS_TEST262_POLICY_NO)
    {
        return NJS_OK;
    }

    if (meta->flags & NJS_TEST262_MODULE) {
        if (ctx->conf.module == NJS_TEST262_POLICY_NO) {
            return NJS_OK;
        }

        skipped = (ctx->conf.module == NJS_TEST262_POLICY_SKIP);
        reason = njs_str_value("module tests are skipped");

        return njs_test262_variant_add(ctx, path, NJS_TEST262_VARIANT_MODULE,
                                       meta, skipped ? &reason : NULL);
    }

    /* A raw test is executed verbatim, so it is inherently non-strict. */

    if (meta->flags & NJS_TEST262_RAW) {
        reason = njs_str_value("raw test in strict mode");

        return njs_test262_variant_add(ctx, path, NJS_TEST262_VARIANT_RAW,
                   meta,
                   (ctx->conf.mode == NJS_TEST262_MODE_STRICT)
                       ? &reason : NULL);
    }

    if (ctx->conf.mode == NJS_TEST262_MODE_STRICT) {
        if (meta->flags & NJS_TEST262_NO_STRICT) {
            reason = njs_str_value("noStrict test in strict mode");

            return njs_test262_variant_add(ctx, path,
                                           NJS_TEST262_VARIANT_NON_STRICT,
                                           meta, &reason);
        }

        return njs_test262_variant_add(ctx, path, NJS_TEST262_VARIANT_STRICT,
                                       meta, NULL);
    }

    if (ctx->conf.mode == NJS_TEST262_MODE_NON_STRICT) {
        if (meta->flags & NJS_TEST262_ONLY_STRICT) {
            reason = njs_str_value("onlyStrict test in non-strict mode");

            return njs_test262_variant_add(ctx, path,
                                           NJS_TEST262_VARIANT_STRICT, meta,
                                           &reason);
        }

        return njs_test262_variant_add(ctx, path,
                                       NJS_TEST262_VARIANT_NON_STRICT, meta,
                                       NULL);
    }

    if (!(meta->flags & NJS_TEST262_ONLY_STRICT)) {
        ret = njs_test262_variant_add(ctx, path,
                                      NJS_TEST262_VARIANT_NON_STRICT, meta,
                                      NULL);
        if (ret != NJS_OK) {
            return ret;
        }
    }

    if (!(meta->flags & NJS_TEST262_NO_STRICT)) {
        return njs_test262_variant_add(ctx, path, NJS_TEST262_VARIANT_STRICT,
                                       meta, NULL);
    }

    return NJS_OK;
}


njs_int_t njs_array_buffer_detach(njs_vm_t *vm, njs_value_t *args,
    njs_uint_t nargs, njs_index_t unused, njs_value_t *retval);


static njs_int_t
njs_test262_ext_global(njs_vm_t *vm, njs_object_prop_t *prop, uint32_t unused,
    njs_value_t *value, njs_value_t *setval, njs_value_t *retval)
{
    if (retval == NULL || setval != NULL) {
        return NJS_DECLINED;
    }

    return njs_vm_global(vm, retval);
}


/*
 * An operation the host cannot provide records what is missing and throws.
 * The recorded name turns the result into a skip that states the capability,
 * which is more useful than a failure against a stub and more precise than a
 * TypeError from an absent property.
 *
 * evalScript cannot be implemented on top of njs_vm_compile(): a nested
 * compilation reallocates the global scope, so the interpreter frame that
 * called it then reads a stale scope.  njs has no eval() either, so there is
 * no re-entrant path to build on.  An earlier attempt did segfault whenever
 * the evaluated script threw.
 */

static njs_int_t
njs_test262_ext_missing(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t magic, njs_value_t *retval)
{
    njs_str_t          name;
    njs_test262_run_t  *run;

    static const njs_str_t  names[] = {
        njs_str("$262.evalScript"),
        njs_str("$262.createRealm"),
        njs_str("$262.gc"),
    };

    name = names[magic % njs_nitems(names)];

    run = njs_vm_external_ptr(vm);

    if (run->missing.length == 0) {
        run->missing = name;
    }

    njs_vm_error(vm, "the host does not provide %V", &name);

    return NJS_ERROR;
}


static const njs_external_t  njs_test262_ext_262[] = {

    {
        .flags = NJS_EXTERN_PROPERTY | NJS_EXTERN_SYMBOL,
        .name.symbol = NJS_SYMBOL_TO_STRING_TAG,
        .u.property = {
            .value = "$262",
        }
    },

    {
        .flags = NJS_EXTERN_METHOD,
        .name.string = njs_str("detachArrayBuffer"),
        .writable = 1,
        .configurable = 1,
        .enumerable = 1,
        .u.method = {
            .native = njs_array_buffer_detach,
        }
    },

    {
        .flags = NJS_EXTERN_METHOD,
        .name.string = njs_str("evalScript"),
        .writable = 1,
        .configurable = 1,
        .enumerable = 1,
        .u.method = {
            .native = njs_test262_ext_missing,
            .magic8 = 0,
        }
    },

    {
        .flags = NJS_EXTERN_METHOD,
        .name.string = njs_str("createRealm"),
        .writable = 1,
        .configurable = 1,
        .enumerable = 1,
        .u.method = {
            .native = njs_test262_ext_missing,
            .magic8 = 1,
        }
    },

    {
        .flags = NJS_EXTERN_METHOD,
        .name.string = njs_str("gc"),
        .writable = 1,
        .configurable = 1,
        .enumerable = 1,
        .u.method = {
            .native = njs_test262_ext_missing,
            .magic8 = 2,
        }
    },

    {
        .flags = NJS_EXTERN_PROPERTY,
        .name.string = njs_str("global"),
        .configurable = 1,
        .enumerable = 1,
        .u.property = {
            .handler = njs_test262_ext_global,
        }
    },

};


static uint64_t
njs_test262_now(void)
{
    struct timespec  ts;

    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
        return 0;
    }

    return (uint64_t) ts.tv_sec * 1000000 + ts.tv_nsec / 1000;
}


/*
 * Recognizes the completion protocol of an asynchronous test.  Only the two
 * documented prefixes are interpreted; anything else printed by a test is
 * captured as output.
 */
static void
njs_test262_async_message(njs_test262_run_t *run, njs_str_t *str)
{
    njs_str_t  message;

    static const njs_str_t  complete = njs_str("Test262:AsyncTestComplete");
    static const njs_str_t  failure = njs_str("Test262:AsyncTestFailure:");

    if (njs_strstr_eq(str, &complete)) {
        run->completions++;

        if (run->async == NJS_TEST262_ASYNC_PENDING) {
            run->async = NJS_TEST262_ASYNC_COMPLETE;
        }

        return;
    }

    if (njs_strstr_starts_with(str, &failure)) {
        run->completions++;

        /*
         * INTERPRETING.md makes this unconditional: a message with the
         * failure prefix means the test failed, whenever it arrives.  Some
         * tests signal a failure and then complete, so a later completion
         * must not overwrite it.
         */

        if (run->async == NJS_TEST262_ASYNC_FAILED) {
            return;
        }

        run->async = NJS_TEST262_ASYNC_FAILED;

        message.start = str->start + failure.length;
        message.length = str->length - failure.length;

        while (message.length != 0 && njs_is_whitespace(message.start[0])) {
            message.start++;
            message.length--;
        }

        if (message.length > NJS_TEST262_MAX_MESSAGE) {
            message.length = NJS_TEST262_MAX_MESSAGE;
        }

        run->async_message.start = njs_mp_alloc(run->pool,
                                                message.length + 1);
        if (run->async_message.start == NULL) {
            return;
        }

        memcpy(run->async_message.start, message.start, message.length);
        run->async_message.start[message.length] = '\0';
        run->async_message.length = message.length;
    }
}


static njs_int_t
njs_test262_ext_print(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t unused, njs_value_t *retval)
{
    size_t             room;
    njs_str_t          str;
    njs_int_t          ret;
    njs_uint_t         i;
    njs_test262_run_t  *run;

    run = njs_vm_external_ptr(vm);

    for (i = 1; i < nargs; i++) {
        ret = njs_vm_value_to_string(vm, &str, njs_argument(args, i));
        if (ret != NJS_OK) {
            return NJS_ERROR;
        }

        if (i == 1) {
            njs_test262_async_message(run, &str);
        }

        room = sizeof(run->output) - run->size;

        if (str.length + 1 < room) {
            if (i != 1) {
                run->output[run->size++] = ' ';
                room--;
            }

            memcpy(&run->output[run->size], str.start, str.length);
            run->size += str.length;
        }
    }

    if (run->size < sizeof(run->output)) {
        run->output[run->size++] = '\n';
    }

    njs_value_undefined_set(retval);

    return NJS_OK;
}


/*
 * A promise rejected without a handler is a failure: the assertions of a test
 * can all pass while its asynchronous part throws.
 */

static void
njs_test262_rejection(njs_vm_t *vm, njs_external_ptr_t external,
    njs_bool_t is_handled, njs_value_t *promise, njs_value_t *reason)
{
    void                    *ptr;
    njs_uint_t              i;
    njs_test262_run_t       *run;
    njs_test262_rejected_t  *rejected;

    run = external;

    if (run->rejected == NULL) {
        return;
    }

    rejected = run->rejected->start;

    if (is_handled) {
        ptr = njs_value_ptr(promise);

        for (i = 0; i < run->rejected->items; i++) {
            if (njs_value_ptr(njs_value_arg(&rejected[i].promise)) == ptr) {
                njs_arr_remove(run->rejected, &rejected[i]);
                break;
            }
        }

        return;
    }

    rejected = njs_arr_add(run->rejected);
    if (rejected == NULL) {
        return;
    }

    njs_value_assign(&rejected->promise, promise);
    njs_value_assign(&rejected->reason, reason);
}


/*
 * Resolves an import relative to the importing module and keeps the result
 * inside the suite tree.  A _FIXTURE.js file is not enumerated as a test but
 * has to be loadable as a dependency.
 */

static njs_mod_t *
njs_test262_module_loader(njs_vm_t *vm, njs_external_ptr_t external,
    njs_str_t *name)
{
    u_char              *start;
    njs_mod_t           *module;
    njs_str_t           path, full, source, prev;
    njs_int_t           ret;
    njs_test262_t       *ctx;
    njs_test262_run_t   *run;
    njs_test262_path_t  normalized;

    run = external;
    ctx = run->ctx;

    /* An import is resolved against the directory of its importer. */

    ret = njs_test262_path_join(ctx, &path, &run->dir, name);
    if (ret != NJS_OK) {
        goto failed;
    }

    ret = njs_test262_path_normalize(ctx, &path, &normalized, 0);
    if (ret != NJS_OK) {
        goto failed;
    }

    ret = njs_test262_path_join(ctx, &full, &ctx->root, &normalized.path);
    if (ret != NJS_OK) {
        goto failed;
    }

    /* njs_vm_compile_module() returns an already loaded module as is. */

    ret = njs_test262_read_file(ctx, run->pool, &full, &source);
    if (ret != NJS_OK) {
        goto failed;
    }

    /*
     * The importer's directory has to be restored afterwards, otherwise a
     * sibling import of the outer module would be resolved against the
     * directory of the inner one.
     */

    prev = run->dir;

    run->dir = normalized.path;

    while (run->dir.length != 0
           && run->dir.start[run->dir.length - 1] != '/')
    {
        run->dir.length--;
    }

    if (run->dir.length != 0) {
        run->dir.length--;
    }

    start = source.start;

    run->resolving++;

    module = njs_vm_compile_module(vm, &normalized.path, &start,
                                   &source.start[source.length]);

    run->resolving--;

    run->dir = prev;

    if (module == NULL) {
        run->resolve_failed = 1;
        return NULL;
    }

    return module;

failed:

    run->resolve_failed = 1;
    ctx->err_len = 0;

    njs_vm_error(vm, "cannot resolve module \"%V\"", name);

    return NULL;
}


static njs_int_t
njs_test262_bindings(njs_test262_t *ctx, njs_vm_t *vm)
{
    njs_int_t           ret, proto_id;
    njs_function_t      *f;
    njs_opaque_value_t  value;

    static const njs_str_t  print = njs_str("print");
    static const njs_str_t  dollar_262 = njs_str("$262");

    f = njs_vm_function_alloc(vm, njs_test262_ext_print, 1, 0);
    if (f == NULL) {
        return njs_test262_error(ctx, "njs_vm_function_alloc() failed");
    }

    njs_value_function_set(njs_value_arg(&value), f);

    ret = njs_vm_bind(vm, &print, njs_value_arg(&value), 1);
    if (ret != NJS_OK) {
        return njs_test262_error(ctx, "failed to bind \"print\"");
    }

    proto_id = njs_vm_external_prototype(vm, njs_test262_ext_262,
                                         njs_nitems(njs_test262_ext_262));
    if (proto_id < 0) {
        return njs_test262_error(ctx, "failed to add the \"$262\" prototype");
    }

    ret = njs_vm_external_create(vm, njs_value_arg(&value), proto_id, NULL, 1);
    if (ret != NJS_OK) {
        return njs_test262_error(ctx, "failed to create \"$262\"");
    }

    ret = njs_vm_bind(vm, &dollar_262, njs_value_arg(&value), 1);
    if (ret != NJS_OK) {
        return njs_test262_error(ctx, "failed to bind \"$262\"");
    }

    return NJS_OK;
}


static njs_int_t
njs_test262_harness_source(njs_test262_t *ctx, const njs_str_t *name,
    njs_str_t *source)
{
    njs_str_t              full;
    njs_int_t              ret;
    njs_uint_t             i;
    njs_test262_harness_t  *entry;

    entry = ctx->cache->start;

    for (i = 0; i < ctx->cache->items; i++) {
        if (njs_strstr_eq(&entry[i].name, name)) {
            *source = entry[i].source;
            return NJS_OK;
        }
    }

    ret = njs_test262_path_join(ctx, &full, &ctx->harness, name);
    if (ret != NJS_OK) {
        return ret;
    }

    ret = njs_test262_read_file(ctx, ctx->pool, &full, source);
    if (ret != NJS_OK) {
        return ret;
    }

    entry = njs_arr_add(ctx->cache);
    if (entry == NULL) {
        return njs_test262_error(ctx, "memory allocation failed");
    }

    ret = njs_test262_str_dup(ctx, &entry->name, name);
    if (ret != NJS_OK) {
        return ret;
    }

    entry->source = *source;

    return NJS_OK;
}


static njs_int_t
njs_test262_source_add(njs_test262_t *ctx, njs_arr_t *sources,
    const njs_str_t *file, const njs_str_t *source)
{
    njs_test262_source_t  *entry;

    entry = njs_arr_add(sources);
    if (entry == NULL) {
        return njs_test262_error(ctx, "memory allocation failed");
    }

    entry->file = *file;
    entry->source = *source;

    return NJS_OK;
}


/*
 * The exception has to be described before the VM is destroyed, so both the
 * name and the message are copied out of the VM pool.
 */

static void
njs_test262_exception(njs_test262_t *ctx, njs_vm_t *vm,
    njs_test262_result_t *result)
{
    njs_str_t           str;
    njs_value_t         *value;
    njs_opaque_value_t  exception, retval;

    static const njs_str_t  name = njs_str("name");
    static const njs_str_t  message = njs_str("message");

    njs_vm_exception_get(vm, njs_value_arg(&exception));

    if (njs_value_is_object(njs_value_arg(&exception))) {
        value = njs_vm_object_prop(vm, njs_value_arg(&exception), &name,
                                   &retval);

        if (value != NULL && njs_vm_value_to_string(vm, &str, value) == NJS_OK)
        {
            (void) njs_test262_str_dup(ctx, &result->actual, &str);
        }

        value = njs_vm_object_prop(vm, njs_value_arg(&exception), &message,
                                   &retval);

        if (value != NULL && njs_vm_value_to_string(vm, &str, value) == NJS_OK)
        {
            if (str.length > NJS_TEST262_MAX_MESSAGE) {
                str.length = NJS_TEST262_MAX_MESSAGE;
            }

            (void) njs_test262_str_dup(ctx, &result->message, &str);
        }

        return;
    }

    if (njs_vm_value_to_string(vm, &str, njs_value_arg(&exception)) == NJS_OK) {
        if (str.length > NJS_TEST262_MAX_MESSAGE) {
            str.length = NJS_TEST262_MAX_MESSAGE;
        }

        (void) njs_test262_str_dup(ctx, &result->message, &str);
    }
}


/*
 * A harness file is always a script.  Only the test itself is compiled as a
 * module, which matches INTERPRETING.md: the harness is evaluated in the
 * global scope before the module.
 */

static njs_int_t
njs_test262_eval(njs_vm_t *vm, njs_test262_source_t *source, njs_bool_t module)
{
    u_char     *start, *end;
    njs_int_t  ret;

    njs_vm_options(vm)->file = source->file;
    njs_vm_options(vm)->module = module;

    start = source->source.start;
    end = start + source->source.length;

    ret = njs_vm_compile(vm, &start, end);
    if (ret != NJS_OK) {
        return NJS_DECLINED;
    }

    if (start != end) {
        return NJS_DECLINED;
    }

    return NJS_OK;
}


/*
 * The metadata lives in the per-variant pool, so an expected type that is
 * reported after the pool is destroyed has to be copied out of it.
 */

static void
njs_test262_negative(njs_test262_t *ctx, njs_test262_result_t *result,
    njs_test262_metadata_t *meta, njs_test262_stage_t stage)
{
    njs_test262_phase_t  phase;

    switch (stage) {
    case NJS_TEST262_STAGE_COMPILE:
        phase = NJS_TEST262_PHASE_PARSE;
        break;

    case NJS_TEST262_STAGE_RESOLVE:
        phase = NJS_TEST262_PHASE_RESOLUTION;
        break;

    default:
        phase = NJS_TEST262_PHASE_RUNTIME;
        break;
    }

    result->stage = stage;

    (void) njs_test262_str_dup(ctx, &result->expected, &meta->type);

    if (meta->phase == phase && njs_strstr_eq(&result->actual, &meta->type)) {
        result->status = NJS_TEST262_PASS;
        return;
    }

    result->status = NJS_TEST262_FAIL;
}


static njs_int_t
njs_test262_sources(njs_test262_t *ctx, njs_mp_t *pool,
    njs_test262_variant_t *variant, njs_test262_metadata_t *meta,
    njs_str_t *test, njs_arr_t *sources)
{
    u_char     *p;
    njs_str_t  *include, source;
    njs_int_t  ret;
    njs_uint_t i;

    static const njs_str_t  sta = njs_str("sta.js");
    static const njs_str_t  assert = njs_str("assert.js");
    static const njs_str_t  done = njs_str("doneprintHandle.js");
    static const njs_str_t  prefix = njs_str("\"use strict\";\n");

    if (!(meta->flags & NJS_TEST262_RAW)) {
        ret = njs_test262_harness_source(ctx, &sta, &source);

        if (ret == NJS_OK) {
            ret = njs_test262_source_add(ctx, sources, &sta, &source);
        }

        if (ret == NJS_OK) {
            ret = njs_test262_harness_source(ctx, &assert, &source);
        }

        if (ret == NJS_OK) {
            ret = njs_test262_source_add(ctx, sources, &assert, &source);
        }

        if (ret != NJS_OK) {
            return ret;
        }

        /*
         * INTERPRETING.md requires doneprintHandle.js to be evaluated after
         * the test262 defined bindings and before the test, which is what
         * defines $DONE.
         */

        if (meta->flags & NJS_TEST262_ASYNC) {
            ret = njs_test262_harness_source(ctx, &done, &source);

            if (ret == NJS_OK) {
                ret = njs_test262_source_add(ctx, sources, &done, &source);
            }

            if (ret != NJS_OK) {
                return ret;
            }
        }

        if (meta->includes != NULL) {
            include = meta->includes->start;

            for (i = 0; i < meta->includes->items; i++) {
                ret = njs_test262_harness_source(ctx, &include[i], &source);

                if (ret == NJS_OK) {
                    ret = njs_test262_source_add(ctx, sources, &include[i],
                                                 &source);
                }

                if (ret != NJS_OK) {
                    return ret;
                }
            }
        }
    }

    if (variant->type != NJS_TEST262_VARIANT_STRICT) {
        return njs_test262_source_add(ctx, sources, &variant->path, test);
    }

    p = njs_mp_alloc(pool, prefix.length + test->length + 1);
    if (p == NULL) {
        return njs_test262_error(ctx, "memory allocation failed");
    }

    source.start = p;

    p = njs_cpymem(p, prefix.start, prefix.length);
    p = njs_cpymem(p, test->start, test->length);
    *p = '\0';

    source.length = p - source.start;

    return njs_test262_source_add(ctx, sources, &variant->path, &source);
}


static njs_int_t
njs_test262_run_variant(njs_test262_t *ctx, njs_test262_variant_t *variant,
    njs_test262_result_t *result)
{
    njs_mp_t                *pool;
    njs_vm_t                *vm;
    njs_arr_t               *sources;
    njs_str_t               full, test;
    uint64_t                start;
    njs_int_t               ret;
    njs_uint_t              i, jobs;
    njs_vm_opt_t            options;
    njs_bool_t              module, last;
    njs_test262_run_t       *run;
    njs_opaque_value_t      retval;
    njs_test262_stage_t     stage;
    njs_test262_source_t    *source;
    njs_test262_metadata_t  meta;
    njs_test262_rejected_t  *rejected;

    njs_memzero(result, sizeof(njs_test262_result_t));

    start = njs_test262_now();

    pool = njs_mp_fast_create(2 * njs_pagesize(), 128, 512, 16);
    if (pool == NULL) {
        return njs_test262_error(ctx, "memory allocation failed");
    }

    vm = NULL;

    run = njs_mp_zalloc(pool, sizeof(njs_test262_run_t));
    if (run == NULL) {
        ret = njs_test262_error(ctx, "memory allocation failed");
        goto done;
    }

    run->ctx = ctx;
    run->pool = pool;

    /* Imports of the test resolve against the directory of the test. */

    run->dir = variant->path;

    while (run->dir.length != 0
           && run->dir.start[run->dir.length - 1] != '/')
    {
        run->dir.length--;
    }

    if (run->dir.length != 0) {
        run->dir.length--;
    }

    run->rejected = njs_arr_create(pool, 4, sizeof(njs_test262_rejected_t));
    if (run->rejected == NULL) {
        ret = njs_test262_error(ctx, "memory allocation failed");
        goto done;
    }

    sources = njs_arr_create(pool, 8, sizeof(njs_test262_source_t));
    if (sources == NULL) {
        ret = njs_test262_error(ctx, "memory allocation failed");
        goto done;
    }

    ret = njs_test262_path_join(ctx, &full, &ctx->root, &variant->path);
    if (ret != NJS_OK) {
        goto done;
    }

    ret = njs_test262_read_file(ctx, pool, &full, &test);
    if (ret != NJS_OK) {
        goto done;
    }

    ret = njs_test262_metadata(ctx, pool, &variant->path, &test, &meta);
    if (ret != NJS_OK) {
        goto done;
    }

    ret = njs_test262_sources(ctx, pool, variant, &meta, &test, sources);
    if (ret != NJS_OK) {
        goto done;
    }

    njs_vm_opt_init(&options);

    options.file = variant->path;
    options.init = 1;
    options.quiet = 1;
    options.unsafe = 1;
    options.external = run;

    vm = njs_vm_create(&options);
    if (vm == NULL) {
        ret = njs_test262_error(ctx, "njs_vm_create() failed");
        goto done;
    }

    njs_vm_set_rejection_tracker(vm, njs_test262_rejection, run);
    njs_vm_set_module_loader(vm, njs_test262_module_loader, run);

    ret = njs_test262_bindings(ctx, vm);
    if (ret != NJS_OK) {
        goto done;
    }

    module = (variant->type == NJS_TEST262_VARIANT_MODULE);

    source = sources->start;

    for (i = 0; i < sources->items; i++) {
        last = (i + 1 == sources->items);

        result->stage = last ? NJS_TEST262_STAGE_COMPILE
                             : NJS_TEST262_STAGE_HARNESS;

        if (njs_test262_eval(vm, &source[i], last && module) != NJS_OK) {
            njs_test262_exception(ctx, vm, result);

            if (result->stage == NJS_TEST262_STAGE_HARNESS) {
                goto harness;
            }

            /*
             * An import is resolved while the importer is parsed, so the
             * loader is the only component that can tell a failed resolution
             * from a syntax error in the test itself.
             */

            stage = run->resolve_failed ? NJS_TEST262_STAGE_RESOLVE
                                        : NJS_TEST262_STAGE_COMPILE;

            if (meta.phase != NJS_TEST262_PHASE_NONE) {
                njs_test262_negative(ctx, result, &meta, stage);

            } else {
                result->status = NJS_TEST262_FAIL;
                result->stage = stage;
            }

            ret = NJS_OK;
            goto done;
        }

        result->stage = last ? NJS_TEST262_STAGE_RUN
                             : NJS_TEST262_STAGE_HARNESS;

        if (njs_vm_start(vm, njs_value_arg(&retval)) != NJS_OK) {
            njs_test262_exception(ctx, vm, result);

            if (result->stage == NJS_TEST262_STAGE_HARNESS) {
                goto harness;
            }

            if (meta.phase != NJS_TEST262_PHASE_NONE) {
                njs_test262_negative(ctx, result, &meta,
                                     NJS_TEST262_STAGE_RUN);

            } else {
                result->status = NJS_TEST262_FAIL;
            }

            ret = NJS_OK;
            goto done;
        }
    }

    /*
     * A synchronous test may still have enqueued promise jobs.  Draining them
     * keeps a rejected promise from being reported as a pass.
     */

    for (jobs = 0; jobs < NJS_TEST262_MAX_JOBS; jobs++) {
        ret = njs_vm_execute_pending_job(vm);

        if (ret == NJS_OK) {
            break;
        }

        if (ret == NJS_ERROR) {
            result->stage = NJS_TEST262_STAGE_JOBS;

            njs_test262_exception(ctx, vm, result);

            if (meta.phase != NJS_TEST262_PHASE_NONE) {
                njs_test262_negative(ctx, result, &meta,
                                     NJS_TEST262_STAGE_JOBS);

            } else {
                result->status = NJS_TEST262_FAIL;
            }

            ret = NJS_OK;
            goto done;
        }
    }

    if (jobs == NJS_TEST262_MAX_JOBS) {
        result->status = NJS_TEST262_FAIL;
        result->stage = NJS_TEST262_STAGE_JOBS;
        result->message = njs_str_value("the promise job limit was reached");

        ret = NJS_OK;
        goto done;
    }

    if (run->rejected->items != 0) {
        rejected = run->rejected->start;

        result->status = NJS_TEST262_FAIL;
        result->stage = NJS_TEST262_STAGE_JOBS;

        if (njs_vm_value_to_string(vm, &test,
                                   njs_value_arg(&rejected->reason)) == NJS_OK)
        {
            if (test.length > NJS_TEST262_MAX_MESSAGE) {
                test.length = NJS_TEST262_MAX_MESSAGE;
            }

            (void) njs_test262_str_printf(ctx, &result->message, "unhandled "
                                          "promise rejection: %V", &test);

        } else {
            result->message = njs_str_value("unhandled promise rejection");
        }

        ret = NJS_OK;
        goto done;
    }

    result->stage = NJS_TEST262_STAGE_NONE;

    if (meta.phase != NJS_TEST262_PHASE_NONE) {
        /* A negative test that completed did not fail where it had to. */

        result->status = NJS_TEST262_FAIL;
        result->message = njs_str_value("no exception was thrown");

        (void) njs_test262_str_dup(ctx, &result->expected, &meta.type);

        ret = NJS_OK;
        goto done;
    }

    /*
     * An asynchronous test is complete only once it has signalled through
     * print().  Reaching the end of the manifest without a signal is not a
     * pass: it means the test never finished.
     */

    if (meta.flags & NJS_TEST262_ASYNC) {
        result->stage = NJS_TEST262_STAGE_JOBS;

        if (run->async == NJS_TEST262_ASYNC_PENDING) {
            result->status = NJS_TEST262_FAIL;

            result->message = njs_vm_pending(vm)
                ? njs_str_value("the test did not complete, work is still "
                                "pending")
                : njs_str_value("the test did not complete and nothing is "
                                "pending");

            ret = NJS_OK;
            goto done;
        }

        if (run->async == NJS_TEST262_ASYNC_FAILED) {
            result->status = NJS_TEST262_FAIL;

            (void) njs_test262_str_printf(ctx, &result->message,
                                          "asynchronous failure: %V",
                                          &run->async_message);

            ret = NJS_OK;
            goto done;
        }

        if (run->completions != 1) {
            result->status = NJS_TEST262_FAIL;

            (void) njs_test262_str_printf(ctx, &result->message, "the test "
                                          "completed %ui times",
                                          run->completions);

            ret = NJS_OK;
            goto done;
        }

        result->stage = NJS_TEST262_STAGE_NONE;
    }

    result->status = NJS_TEST262_PASS;

    ret = NJS_OK;

    goto done;

harness:

    /*
     * The harness file exists and was read, so this is the engine failing to
     * run the test's own scaffolding, not a runner failure.  Reporting it as
     * a failure keeps the missing capability visible instead of hiding the
     * case behind a skip.
     */

    result->status = NJS_TEST262_FAIL;

    (void) njs_test262_str_printf(ctx, &result->message,
                                  "harness file \"%V\" failed: %V: %V",
                                  &source[i].file, &result->actual,
                                  &result->message);

    result->actual.length = 0;
    ret = NJS_OK;

done:

    /*
     * A test that asked for an operation the host cannot provide is skipped
     * whatever it did next.  Checked after classification so that it also
     * overrides a negative match that the stub's own exception would
     * otherwise satisfy by accident.
     */

    if (ret == NJS_OK && run != NULL && run->missing.length != 0) {
        result->status = NJS_TEST262_SKIP;
        result->stage = NJS_TEST262_STAGE_NONE;
        result->expected.length = 0;
        result->actual.length = 0;

        (void) njs_test262_str_printf(ctx, &result->message, "the host does "
                                      "not provide %V", &run->missing);

        goto teardown;
    }

    /*
     * Whatever the test printed is worth reporting, except the completion
     * message of an asynchronous test, which is protocol rather than output.
     */

    if (result->message.length == 0 && run != NULL && run->size != 0
        && run->async != NJS_TEST262_ASYNC_COMPLETE)
    {
        test.start = run->output;
        test.length = njs_min(run->size, (size_t) NJS_TEST262_MAX_MESSAGE);

        while (test.length != 0
               && njs_is_whitespace(test.start[test.length - 1]))
        {
            test.length--;
        }

        if (test.length != 0) {
            (void) njs_test262_str_dup(ctx, &result->message, &test);
        }
    }

teardown:

    if (vm != NULL) {
        njs_vm_destroy(vm);
    }

    njs_mp_destroy(pool);

    result->elapsed = njs_test262_now() - start;

    return ret;
}


static njs_int_t
njs_test262_resolve(njs_test262_t *ctx, njs_test262_path_t *path)
{
    njs_str_t    full;
    njs_int_t    ret;
    struct stat  sb;

    ret = njs_test262_path_join(ctx, &full, &ctx->root, &path->path);
    if (ret != NJS_OK) {
        return ret;
    }

    if (stat((char *) full.start, &sb) != 0) {
        return njs_test262_error(ctx, "\"%V\" is not present in the test262 "
                                 "checkout: %s", &path->path,
                                 njs_errno_string(errno));
    }

    if (S_ISDIR(sb.st_mode)) {
        path->dir = 1;

    } else if (S_ISREG(sb.st_mode)) {
        if (path->dir) {
            return njs_test262_error(ctx, "\"%V/\" is not a directory",
                                     &path->path);
        }

        /* An explicit file is held to the same rule as an enumerated one. */

        if (!njs_test262_is_test((char *) path->path.start,
                                 path->path.length))
        {
            return njs_test262_error(ctx, "\"%V\" is not a test262 test file",
                                     &path->path);
        }

    } else {
        return njs_test262_error(ctx, "\"%V\" is neither a file nor a "
                                 "directory", &path->path);
    }

    return NJS_OK;
}


static njs_int_t
njs_test262_roots(njs_test262_t *ctx, njs_arr_t **rootsp)
{
    njs_arr_t           *roots;
    njs_int_t           ret;
    njs_uint_t          i, j, n;
    njs_test262_path_t  *tests, *args, *root, all;

    roots = njs_arr_create(ctx->pool, 4, sizeof(njs_test262_path_t));
    if (roots == NULL) {
        return njs_test262_error(ctx, "memory allocation failed");
    }

    *rootsp = roots;

    if (ctx->tests->items == 0) {
        all.path = njs_str_value("");
        all.dir = 1;

        tests = &all;
        n = 1;

    } else {
        tests = ctx->tests->start;
        n = ctx->tests->items;

        for (i = 0; i < n; i++) {
            ret = njs_test262_resolve(ctx, &tests[i]);
            if (ret != NJS_OK) {
                return ret;
            }
        }
    }

    args = ctx->args->start;

    for (i = 0; i < ctx->args->items; i++) {
        ret = njs_test262_resolve(ctx, &args[i]);
        if (ret != NJS_OK) {
            return ret;
        }
    }

    for (i = 0; i < n; i++) {
        if (ctx->args->items == 0) {
            root = njs_arr_add(roots);
            if (root == NULL) {
                return njs_test262_error(ctx, "memory allocation failed");
            }

            *root = tests[i];

            continue;
        }

        for (j = 0; j < ctx->args->items; j++) {
            if (njs_test262_path_contains(&tests[i], &args[j])) {
                root = njs_arr_add(roots);

            } else if (njs_test262_path_contains(&args[j], &tests[i])) {
                root = njs_arr_add(roots);

            } else {
                continue;
            }

            if (root == NULL) {
                return njs_test262_error(ctx, "memory allocation failed");
            }

            *root = njs_test262_path_contains(&tests[i], &args[j])
                    ? args[j] : tests[i];
        }
    }

    return NJS_OK;
}


static njs_int_t
njs_test262_enumerate(njs_test262_t *ctx)
{
    size_t              len;
    njs_arr_t           *roots;
    njs_str_t           *files, *file;
    njs_int_t           ret;
    njs_uint_t          i, j, n;
    njs_test262_walk_t  *walk;
    njs_test262_path_t  *root, *excludes, candidate;

    roots = NULL;

    ret = njs_test262_roots(ctx, &roots);
    if (ret != NJS_OK) {
        return ret;
    }

    walk = njs_mp_alloc(ctx->pool, sizeof(njs_test262_walk_t));
    if (walk == NULL) {
        return njs_test262_error(ctx, "memory allocation failed");
    }

    if (ctx->root.length + 1 >= sizeof(walk->path)) {
        return njs_test262_error(ctx, "test262 path \"%V\" is too long",
                                 &ctx->root);
    }

    walk->root_len = ctx->root.length + 1;

    root = roots->start;

    for (i = 0; i < roots->items; i++) {
        memcpy(walk->path, ctx->root.start, ctx->root.length);

        len = ctx->root.length;

        if (root[i].path.length != 0) {
            if (len + 1 + root[i].path.length >= sizeof(walk->path)) {
                return njs_test262_error(ctx, "path \"%V\" is too long",
                                         &root[i].path);
            }

            walk->path[len] = '/';
            memcpy(&walk->path[len + 1], root[i].path.start,
                   root[i].path.length);

            len += 1 + root[i].path.length;
        }

        walk->path[len] = '\0';

        if (root[i].dir) {
            ret = njs_test262_walk(ctx, walk, len);
            if (ret != NJS_OK) {
                return ret;
            }

            continue;
        }

        file = njs_arr_add(ctx->files);
        if (file == NULL) {
            return njs_test262_error(ctx, "memory allocation failed");
        }

        ret = njs_test262_str_dup(ctx, file, &root[i].path);
        if (ret != NJS_OK) {
            return ret;
        }
    }

    njs_qsort(ctx->files->start, ctx->files->items, sizeof(njs_str_t),
              njs_test262_str_cmp, NULL);

    /* Drop duplicates and excluded entries in one pass. */

    files = ctx->files->start;
    excludes = ctx->excludes->start;
    n = 0;

    for (i = 0; i < ctx->files->items; i++) {
        if (n != 0 && njs_test262_str_cmp(&files[n - 1], &files[i],
                                          NULL) == 0)
        {
            continue;
        }

        candidate.path = files[i];
        candidate.dir = 0;

        for (j = 0; j < ctx->excludes->items; j++) {
            if (njs_test262_path_contains(&excludes[j], &candidate)) {
                break;
            }
        }

        if (j != ctx->excludes->items) {
            continue;
        }

        files[n++] = files[i];
    }

    ctx->files->items = n;

    return NJS_OK;
}


static njs_int_t
njs_test262_select(njs_test262_t *ctx)
{
    njs_mp_t                *pool;
    njs_str_t               *files, full, source;
    njs_int_t               ret;
    njs_uint_t              i;
    njs_test262_metadata_t  meta;

    ret = njs_test262_enumerate(ctx);
    if (ret != NJS_OK) {
        return ret;
    }

    files = ctx->files->start;

    for (i = 0; i < ctx->files->items; i++) {
        pool = njs_mp_fast_create(2 * njs_pagesize(), 128, 512, 16);
        if (pool == NULL) {
            return njs_test262_error(ctx, "memory allocation failed");
        }

        ret = njs_test262_path_join(ctx, &full, &ctx->root, &files[i]);
        if (ret != NJS_OK) {
            njs_mp_destroy(pool);
            return ret;
        }

        ret = njs_test262_read_file(ctx, pool, &full, &source);

        if (ret == NJS_OK) {
            ret = njs_test262_metadata(ctx, pool, &files[i], &source, &meta);
        }

        if (ret == NJS_OK) {
            ret = njs_test262_variants(ctx, &files[i], &meta);
        }

        njs_mp_destroy(pool);

        if (ret != NJS_OK) {
            if (ctx->err_len == 0) {
                return NJS_ERROR;
            }

            if (ctx->out != NULL) {
                njs_test262_print(ctx->out, "INFRA\t%V\t-\t%*s\n", &files[i],
                                  ctx->err_len, ctx->err);

            } else {
                njs_stderror("INFRA\t%V\t-\t%*s\n", &files[i], ctx->err_len,
                             ctx->err);
            }

            ctx->err_len = 0;
            ctx->infra_errors++;
        }
    }

    return NJS_OK;
}


static njs_int_t
njs_test262_list(njs_test262_t *ctx)
{
    njs_uint_t             i, run, skip;
    njs_test262_variant_t  *variant;

    variant = ctx->variants->start;
    run = 0;
    skip = 0;

    if (ctx->out == NULL) {
        return njs_test262_error(ctx, "--list requires standard output");
    }

    for (i = 0; i < ctx->variants->items; i++) {
        if (variant[i].reason.length == 0) {
            run++;

        } else {
            skip++;
        }

        njs_test262_print(ctx->out, "%s\t%V\t%s\t%V",
                          (variant[i].reason.length == 0) ? "RUN" : "SKIP",
                          &variant[i].path,
                          njs_test262_variant_name(variant[i].type),
                          &variant[i].reason);

        if (ctx->verbose) {
            njs_test262_print(ctx->out, "\t%V", &variant[i].info);
        }

        njs_test262_print(ctx->out, "\n");
    }

    njs_test262_flush(ctx->out);

    if (!ctx->quiet) {
        njs_stderror("files: %ui, variants: %ui (run: %ui, skip: %ui), "
                     "infra errors: %ui\n", ctx->files->items,
                     ctx->variants->items, run, skip, ctx->infra_errors);
    }

    return (ctx->infra_errors != 0) ? NJS_ERROR : NJS_OK;
}


static const char *
njs_test262_status_name(njs_test262_status_t status)
{
    switch (status) {
    case NJS_TEST262_PASS:
        return "PASS";

    case NJS_TEST262_FAIL:
        return "FAIL";

    case NJS_TEST262_SKIP:
        return "SKIP";

    case NJS_TEST262_CRASH:
        return "CRASH";

    default:
        return "INFRA";
    }
}


static const char *
njs_test262_stage_name(njs_test262_stage_t stage)
{
    switch (stage) {
    case NJS_TEST262_STAGE_HARNESS:
        return "harness";

    case NJS_TEST262_STAGE_COMPILE:
        return "compile";

    case NJS_TEST262_STAGE_RESOLVE:
        return "resolve";

    case NJS_TEST262_STAGE_RUN:
        return "run";

    default:
        return "-";
    }
}


static njs_int_t
njs_test262_variant_parse(const njs_str_t *name,
    njs_test262_variant_type_t *type)
{
    njs_uint_t  i;

    for (i = 0; i <= NJS_TEST262_VARIANT_STRICT; i++) {
        if (name->length == njs_strlen(njs_test262_variant_name(i))
            && memcmp(name->start, njs_test262_variant_name(i),
                      name->length) == 0)
        {
            *type = i;
            return NJS_OK;
        }
    }

    return NJS_ERROR;
}


static int
njs_test262_expect_cmp(const void *one, const void *two, void *ctx)
{
    njs_int_t  ret;

    const njs_test262_expect_entry_t  *a = one;
    const njs_test262_expect_entry_t  *b = two;

    ret = njs_test262_str_cmp(&a->path, &b->path, ctx);
    if (ret != 0) {
        return ret;
    }

    return (int) a->type - (int) b->type;
}


static njs_test262_expect_entry_t *
njs_test262_expect_find(njs_test262_t *ctx, const njs_str_t *path,
    njs_test262_variant_type_t type)
{
    njs_int_t                   ret;
    njs_uint_t                  lo, hi, mid;
    njs_test262_expect_entry_t  key, *entry;

    if (ctx->expected == NULL) {
        return NULL;
    }

    key.path = *path;
    key.type = type;

    entry = ctx->expected->start;

    lo = 0;
    hi = ctx->expected->items;

    while (lo < hi) {
        mid = lo + (hi - lo) / 2;

        ret = njs_test262_expect_cmp(&entry[mid], &key, NULL);

        if (ret == 0) {
            return &entry[mid];
        }

        if (ret < 0) {
            lo = mid + 1;

        } else {
            hi = mid;
        }
    }

    return NULL;
}


static njs_int_t
njs_test262_expect_read(njs_test262_t *ctx, const njs_str_t *file)
{
    u_char                      *p, *end, *eol, *tab;
    njs_str_t                   source, line, field[2];
    njs_int_t                   ret;
    njs_uint_t                  lineno, n;
    struct stat                 sb;
    njs_test262_expect_entry_t  *entry;

    ctx->expected = njs_arr_create(ctx->pool, 64,
                                   sizeof(njs_test262_expect_entry_t));
    if (ctx->expected == NULL) {
        return njs_test262_error(ctx, "memory allocation failed");
    }

    if (stat((char *) file->start, &sb) != 0) {
        if (errno == ENOENT) {
            /*
             * An absent baseline is not an error: it is how the first one is
             * created.  Every failure is then reported as new.
             */

            return NJS_OK;
        }

        return njs_test262_error(ctx, "stat(\"%V\") failed: %s", file,
                                 njs_errno_string(errno));
    }

    ret = njs_test262_read_file(ctx, ctx->pool, file, &source);
    if (ret != NJS_OK) {
        return ret;
    }

    p = source.start;
    end = p + source.length;
    lineno = 0;
    while (p < end) {
        eol = njs_test262_line_end(p, end);

        line.start = p;
        line.length = eol - p;

        p = njs_test262_line_next(eol, end);
        lineno++;

        if (line.length == 0) {
            continue;
        }

        for (n = 0; n < njs_nitems(field); n++) {
            tab = njs_strlchr(line.start, line.start + line.length, '\t');

            if (tab == NULL) {
                field[n] = line;
                line.length = 0;
                n++;
                break;
            }

            field[n].start = line.start;
            field[n].length = tab - line.start;

            line.length -= field[n].length + 1;
            line.start = tab + 1;
        }

        if (n != njs_nitems(field) || line.length != 0 || field[0].length == 0
            || field[1].length == 0)
        {
            return njs_test262_error(ctx, "%V:%ui: expected path, tab, and "
                                     "variant", file, lineno);
        }

        entry = njs_arr_add(ctx->expected);
        if (entry == NULL) {
            return njs_test262_error(ctx, "memory allocation failed");
        }

        njs_memzero(entry, sizeof(njs_test262_expect_entry_t));

        ret = njs_test262_str_dup(ctx, &entry->path, &field[0]);
        if (ret != NJS_OK) {
            return ret;
        }

        if (njs_test262_variant_parse(&field[1], &entry->type) != NJS_OK) {
            return njs_test262_error(ctx, "%V:%ui: unknown variant \"%V\"",
                                     file, lineno, &field[1]);
        }

        if (ctx->expected->items > 1
            && njs_test262_expect_cmp(entry - 1, entry, NULL) >= 0)
        {
            return njs_test262_error(ctx, "%V:%ui: entries are not strictly "
                                     "sorted", file, lineno);
        }

    }

    return NJS_OK;
}


njs_inline njs_bool_t
njs_test262_recordable(njs_test262_status_t status)
{
    return status == NJS_TEST262_FAIL;
}


/*
 * PASS, FAIL and SKIP are verdicts about the engine.  A crash and an
 * infrastructure error are not: the run produced no answer, so there is
 * nothing a baseline could have expected and nothing that can have changed.
 */

njs_inline njs_bool_t
njs_test262_conclusive(njs_test262_status_t status)
{
    return status != NJS_TEST262_CRASH && status != NJS_TEST262_INFRA;
}


static njs_test262_transition_t
njs_test262_compare(njs_test262_t *ctx, njs_test262_variant_t *variant,
    njs_test262_result_t *result)
{
    njs_test262_expect_entry_t  *entry;

    if (ctx->expected == NULL) {
        return NJS_TEST262_SAME;
    }

    entry = njs_test262_expect_find(ctx, &variant->path, variant->type);

    if (entry != NULL) {
        entry->matched = 1;
    }

    if (!njs_test262_conclusive(result->status)) {
        return NJS_TEST262_SAME;
    }

    if (!njs_test262_recordable(result->status)) {
        return (entry != NULL) ? NJS_TEST262_FIXED : NJS_TEST262_SAME;
    }

    if (entry == NULL) {
        return NJS_TEST262_NEW;
    }

    return NJS_TEST262_SAME;
}


static const char *
njs_test262_transition_name(njs_test262_transition_t transition)
{
    switch (transition) {
    case NJS_TEST262_NEW:
        return "NEW";

    case NJS_TEST262_FIXED:
        return "FIXED";

    default:
        return "-";
    }
}


static njs_int_t
njs_test262_expect_write(njs_test262_t *ctx, const njs_str_t *file)
{
    int                    fd;
    njs_str_t              tmp;
    njs_int_t              ret;
    njs_uint_t             i, n;
    njs_test262_out_t      *out;
    njs_test262_result_t   *result;
    njs_test262_variant_t  *variant;

    if (ctx->summary[NJS_TEST262_CRASH] != 0
        || ctx->summary[NJS_TEST262_INFRA] != 0)
    {
        return njs_test262_error(ctx, "refusing to update \"%V\": the run "
                                 "produced %ui crashes and %ui infrastructure "
                                 "errors", file,
                                 ctx->summary[NJS_TEST262_CRASH],
                                 ctx->summary[NJS_TEST262_INFRA]);
    }

    /*
     * Write a sibling and rename it, so that an interrupted update cannot
     * leave a half written baseline behind.
     */

    ret = njs_test262_str_printf(ctx, &tmp, "%V.tmp", file);
    if (ret != NJS_OK) {
        return ret;
    }

    fd = open((char *) tmp.start, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) {
        return njs_test262_error(ctx, "open(\"%V\") failed: %s", &tmp,
                                 njs_errno_string(errno));
    }

    out = njs_mp_alloc(ctx->pool, sizeof(njs_test262_out_t));
    if (out == NULL) {
        (void) close(fd);
        return njs_test262_error(ctx, "memory allocation failed");
    }

    out->fd = fd;
    out->size = 0;

    variant = ctx->variants->start;
    n = 0;

    for (i = 0; i < ctx->variants->items; i++) {
        result = &ctx->results[i];

        if (!njs_test262_recordable(result->status)) {
            continue;
        }

        njs_test262_print(out, "%V\t%s\n", &variant[i].path,
                           njs_test262_variant_name(variant[i].type));
        n++;
    }

    njs_test262_flush(out);

    if (close(fd) != 0) {
        (void) unlink((char *) tmp.start);

        return njs_test262_error(ctx, "close(\"%V\") failed: %s", &tmp,
                                 njs_errno_string(errno));
    }

    if (rename((char *) tmp.start, (char *) file->start) != 0) {
        (void) unlink((char *) tmp.start);

        return njs_test262_error(ctx, "rename(\"%V\") failed: %s", &tmp,
                                 njs_errno_string(errno));
    }

    if (!ctx->quiet) {
        njs_stderror("wrote %ui entries to %V\n", n, file);
    }

    return NJS_OK;
}


static void
njs_test262_report(njs_test262_t *ctx, njs_test262_variant_t *variant,
    njs_test262_result_t *result, njs_test262_transition_t transition)
{
    if (ctx->out == NULL) {
        return;
    }

    /*
     * With a baseline the interesting record is the transition, not the
     * status: a failure that the baseline already knows about is as quiet as
     * a pass.  A result with no verdict has no transition to speak of, so
     * suppressing it here would leave the summary counter as its only trace.
     */

    if (!njs_test262_conclusive(result->status)) {
        /* Always reported. */

    } else if (ctx->expected != NULL && !ctx->update) {
        if (transition == NJS_TEST262_SAME && !ctx->verbose) {
            return;
        }

    } else if (!ctx->verbose
               && (result->status == NJS_TEST262_PASS
                   || result->status == NJS_TEST262_SKIP))
    {
        return;
    }

    if (transition != NJS_TEST262_SAME) {
        njs_test262_print(ctx->out, "%s ",
                          njs_test262_transition_name(transition));
    }

    njs_test262_print(ctx->out, "%s\t%V\t%s\t%s\t",
                      njs_test262_status_name(result->status), &variant->path,
                      njs_test262_variant_name(variant->type),
                      njs_test262_stage_name(result->stage));

    if (result->expected.length != 0) {
        njs_test262_print(ctx->out, "expected %V, got %V", &result->expected,
                          (result->actual.length != 0) ? &result->actual
                                                       : &result->message);

    } else if (result->actual.length != 0) {
        njs_test262_print(ctx->out, "%V: %V", &result->actual,
                          &result->message);

    } else {
        njs_test262_print(ctx->out, "%V", &result->message);
    }

    njs_test262_print(ctx->out, "\n");

}


/*
 * The single place a committed result is counted, compared against the
 * baseline and printed.
 */

static void
njs_test262_account(njs_test262_t *ctx, njs_test262_variant_t *variant,
    njs_test262_result_t *result)
{
    njs_test262_transition_t  transition;

    ctx->summary[result->status]++;

    transition = njs_test262_compare(ctx, variant, result);

    ctx->transitions[transition]++;

    njs_test262_report(ctx, variant, result, transition);
}


static njs_int_t
njs_test262_write_all(int fd, const void *data, size_t size)
{
    ssize_t       n;
    const u_char  *p;

    p = data;

    while (size != 0) {
        n = write(fd, p, size);

        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }

            return NJS_ERROR;
        }

        if (n == 0) {
            return NJS_ERROR;
        }

        p += n;
        size -= n;
    }

    return NJS_OK;
}


static njs_int_t
njs_test262_read_all(int fd, void *data, size_t size)
{
    u_char   *p;
    ssize_t  n;

    p = data;

    while (size != 0) {
        n = read(fd, p, size);

        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }

            return NJS_ERROR;
        }

        if (n == 0) {
            return NJS_DECLINED;
        }

        p += n;
        size -= n;
    }

    return NJS_OK;
}


static njs_int_t
njs_test262_result_pack(njs_test262_result_t *result, u_char *buf,
    size_t *size)
{
    u_char              *p;
    size_t              length[3];
    njs_uint_t          i;
    njs_test262_wire_t  wire;

    njs_str_t  *fields[] = {
        &result->expected, &result->actual, &result->message
    };

    njs_memzero(&wire, sizeof(wire));

    for (i = 0; i < njs_nitems(fields); i++) {
        length[i] = njs_min(fields[i]->length,
                            (size_t) NJS_TEST262_MAX_MESSAGE);
    }

    wire.status = result->status;
    wire.stage = result->stage;
    wire.elapsed = result->elapsed;
    wire.expected = length[0];
    wire.actual = length[1];
    wire.message = length[2];

    p = njs_cpymem(buf, &wire, sizeof(wire));

    for (i = 0; i < njs_nitems(fields); i++) {
        if (length[i] != 0) {
            p = njs_cpymem(p, fields[i]->start, length[i]);
        }
    }

    *size = p - buf;

    return NJS_OK;
}


static njs_int_t
njs_test262_result_unpack(njs_test262_t *ctx, njs_test262_result_t *result,
    u_char *buf, size_t size)
{
    u_char              *p;
    njs_str_t           str;
    njs_int_t           ret;
    njs_uint_t          i;
    njs_test262_wire_t  wire;

    njs_str_t  *fields[] = {
        &result->expected, &result->actual, &result->message
    };

    if (size < sizeof(wire)) {
        return NJS_ERROR;
    }

    memcpy(&wire, buf, sizeof(wire));

    if (wire.status >= NJS_TEST262_STATUS_MAX
        || wire.stage > NJS_TEST262_STAGE_JOBS
        || wire.expected > NJS_TEST262_MAX_MESSAGE
        || wire.actual > NJS_TEST262_MAX_MESSAGE
        || wire.message > NJS_TEST262_MAX_MESSAGE
        || sizeof(wire) + wire.expected + wire.actual + wire.message != size)
    {
        return NJS_ERROR;
    }

    njs_memzero(result, sizeof(njs_test262_result_t));

    result->status = wire.status;
    result->stage = wire.stage;
    result->elapsed = wire.elapsed;

    p = buf + sizeof(wire);

    for (i = 0; i < njs_nitems(fields); i++) {
        fields[i]->length = (i == 0) ? wire.expected
                          : (i == 1) ? wire.actual : wire.message;

        if (fields[i]->length != 0) {
            str.start = p;
            str.length = fields[i]->length;

            ret = njs_test262_str_dup(ctx, fields[i], &str);
            if (ret != NJS_OK) {
                return ret;
            }
        }

        p += fields[i]->length;
    }

    return NJS_OK;
}


static njs_int_t
njs_test262_isolated(njs_test262_t *ctx, njs_test262_variant_t *variant,
    njs_test262_result_t *result)
{
    int                   fd[2], status;
    pid_t                 pid;
    size_t                size;
    u_char                extra;
    njs_int_t             ret, wret;
    njs_test262_wire_t    wire;
    njs_test262_result_t  child;
    u_char                payload[sizeof(njs_test262_wire_t)
                               + NJS_TEST262_MAX_MESSAGE * 3];

    size = 0;
    status = 0;

    if (pipe(fd) != 0) {
        return njs_test262_error(ctx, "pipe() failed: %s",
                                 njs_errno_string(errno));
    }

    njs_test262_flush(ctx->out);

    pid = fork();
    if (pid < 0) {
        (void) close(fd[0]);
        (void) close(fd[1]);

        return njs_test262_error(ctx, "fork() failed: %s",
                                 njs_errno_string(errno));
    }

    if (pid == 0) {
        (void) close(fd[0]);

        if (ctx->timeout != 0) {
            struct rlimit  rl;

            rl.rlim_cur = ctx->timeout;
            rl.rlim_max = ctx->timeout + 5;
            (void) setrlimit(RLIMIT_CPU, &rl);
        }

        ctx->out = NULL;
        ret = njs_test262_run_variant(ctx, variant, &child);

        if (ret != NJS_OK) {
            child.status = NJS_TEST262_INFRA;
            child.message.start = ctx->err;
            child.message.length = ctx->err_len;
        }

        (void) njs_test262_result_pack(&child, payload, &size);
        ret = njs_test262_write_all(fd[1], payload, size);

        (void) close(fd[1]);
        _exit((ret == NJS_OK) ? 0 : 1);
    }

    (void) close(fd[1]);
    ret = njs_test262_read_all(fd[0], payload, sizeof(njs_test262_wire_t));

    if (ret == NJS_OK) {
        memcpy(&wire, payload, sizeof(wire));

        if (wire.status >= NJS_TEST262_STATUS_MAX
            || wire.stage > NJS_TEST262_STAGE_JOBS
            || wire.expected > NJS_TEST262_MAX_MESSAGE
            || wire.actual > NJS_TEST262_MAX_MESSAGE
            || wire.message > NJS_TEST262_MAX_MESSAGE)
        {
            ret = NJS_ERROR;

        } else {
            size = sizeof(wire) + wire.expected + wire.actual + wire.message;

            ret = njs_test262_read_all(fd[0], &payload[sizeof(wire)],
                                       size - sizeof(wire));

            if (ret == NJS_OK) {
                do {
                    ret = read(fd[0], &extra, 1);
                } while (ret < 0 && errno == EINTR);

                if (ret != 0) {
                    ret = NJS_ERROR;

                } else {
                    ret = NJS_OK;
                }
            }
        }
    }

    (void) close(fd[0]);
    fd[0] = -1;

    do {
        wret = waitpid(pid, &status, 0);
    } while (wret < 0 && errno == EINTR);

    if (wret != pid || (ret == NJS_OK
                        && !(WIFEXITED(status) && WEXITSTATUS(status) == 0)))
    {
        ret = NJS_ERROR;
    }

    if (ret == NJS_OK) {
        ret = njs_test262_result_unpack(ctx, result, payload, size);
    }

    if (ret != NJS_OK) {
        njs_memzero(result, sizeof(njs_test262_result_t));
        result->status = NJS_TEST262_INFRA;
        result->stage = NJS_TEST262_STAGE_RUN;

        if (WIFSIGNALED(status)) {
            result->status = NJS_TEST262_CRASH;

            (void) njs_test262_str_printf(ctx, &result->message,
                                          "child terminated by signal %d",
                                          WTERMSIG(status));

        } else if (WIFEXITED(status)) {
            result->status = NJS_TEST262_INFRA;

            (void) njs_test262_str_printf(ctx, &result->message,
                                          "child exited without a valid "
                                          "result (status %d)",
                                          WEXITSTATUS(status));

        } else {
            result->status = NJS_TEST262_INFRA;
            result->message = njs_str_value("child result protocol failed");
        }
    }

    return NJS_OK;
}


static njs_int_t
njs_test262_direct(njs_test262_t *ctx)
{
    njs_int_t              ret;
    njs_uint_t             i;
    njs_test262_result_t   result;
    njs_test262_variant_t  *variant;

    variant = ctx->variants->start;

    for (i = 0; i < ctx->variants->items; i++) {
        if (variant[i].reason.length != 0) {
            njs_memzero(&result, sizeof(njs_test262_result_t));

            result.status = NJS_TEST262_SKIP;
            result.message = variant[i].reason;

            goto report;
        }

        if (ctx->verbose && ctx->out != NULL) {
            njs_test262_print(ctx->out, "EXEC\t%V\t%s\n", &variant[i].path,
                              njs_test262_variant_name(variant[i].type));
            njs_test262_flush(ctx->out);
        }

        ret = ctx->isolation
              ? njs_test262_isolated(ctx, &variant[i], &result)
              : njs_test262_run_variant(ctx, &variant[i], &result);

        if (ret != NJS_OK) {
            result.status = NJS_TEST262_INFRA;

            if (ctx->err_len != 0) {
                result.message.start = ctx->err;
                result.message.length = ctx->err_len;
                ctx->err_len = 0;
            }
        }

    report:

        ctx->results[i] = result;

        njs_test262_account(ctx, &variant[i], &result);
    }

    return NJS_OK;
}


static njs_int_t
njs_test262_execute(njs_test262_t *ctx)
{
    uint64_t                    start, elapsed;
    njs_int_t                   ret;
    njs_uint_t                  i;
    njs_test262_expect_entry_t  *entry;

    start = njs_test262_now();

    ctx->results = njs_mp_zalloc(ctx->pool, ctx->variants->items
                                            * sizeof(njs_test262_result_t));
    if (ctx->results == NULL && ctx->variants->items != 0) {
        return njs_test262_error(ctx, "memory allocation failed");
    }

    ret = njs_test262_direct(ctx);

    if (ret != NJS_OK) {
        return ret;
    }

    if (ctx->infra_errors != 0) {
        return NJS_ERROR;
    }

    if (ctx->expected != NULL && !ctx->update) {
        entry = ctx->expected->start;

        for (i = 0; i < ctx->expected->items; i++) {
            if (entry[i].matched) {
                continue;
            }

            ctx->transitions[NJS_TEST262_FIXED]++;

            if (ctx->out != NULL) {
                njs_test262_print(ctx->out, "STALE\t%V\t%s\n",
                                  &entry[i].path,
                                  njs_test262_variant_name(entry[i].type));
            }
        }
    }

    elapsed = njs_test262_now() - start;

    if (ctx->out != NULL) {
        njs_test262_flush(ctx->out);
    }

    if (!ctx->quiet) {
        njs_stderror("variants: %ui, pass: %ui, fail: %ui, skip: %ui, "
                     "crash: %ui, infra: %ui, duration: %uL.%03uLs\n",
                     ctx->variants->items, ctx->summary[NJS_TEST262_PASS],
                     ctx->summary[NJS_TEST262_FAIL],
                     ctx->summary[NJS_TEST262_SKIP],
                     ctx->summary[NJS_TEST262_CRASH],
                     ctx->summary[NJS_TEST262_INFRA],
                     elapsed / 1000000, (elapsed / 1000) % 1000);

        if (ctx->expected != NULL) {
            njs_stderror("new: %ui, fixed: %ui\n",
                         ctx->transitions[NJS_TEST262_NEW],
                         ctx->transitions[NJS_TEST262_FIXED]);
        }
    }

    if (ctx->update) {
        ret = njs_test262_expect_write(ctx, &ctx->conf.failures);
        if (ret != NJS_OK) {
            return ret;
        }

        return NJS_OK;
    }

    if (ctx->summary[NJS_TEST262_CRASH] != 0
        || ctx->summary[NJS_TEST262_INFRA] != 0)
    {
        return NJS_ERROR;
    }

    /*
     * With a baseline the verdict is whether anything moved.  Without one
     * any failure is the verdict.
     */

    if (ctx->expected != NULL) {
        if (ctx->transitions[NJS_TEST262_NEW] != 0
            || ctx->transitions[NJS_TEST262_FIXED] != 0)
        {
            return NJS_DECLINED;
        }

        return NJS_OK;
    }

    if (ctx->summary[NJS_TEST262_FAIL] != 0)
    {
        return NJS_DECLINED;
    }

    return NJS_OK;
}


static njs_int_t
njs_test262_suite(njs_test262_t *ctx, const njs_str_t *conf_file)
{
    char         *env;
    njs_str_t    dir, name;
    njs_int_t    ret;
    struct stat  sb;

    if (ctx->suite.length != 0) {
        goto found;
    }

    env = getenv("NJS_TEST262_DIR");

    if (env != NULL && env[0] != '\0') {
        ctx->suite.start = (u_char *) env;
        ctx->suite.length = njs_strlen(env);
        goto found;
    }

    if (ctx->conf.suite.length != 0) {
        /* A relative "suite" is resolved against the config location. */

        if (ctx->conf.suite.start[0] == '/' || conf_file == NULL) {
            ctx->suite = ctx->conf.suite;
            goto found;
        }

        dir = *conf_file;

        while (dir.length != 0 && dir.start[dir.length - 1] != '/') {
            dir.length--;
        }

        if (dir.length != 0) {
            dir.length--;
        }

        ret = njs_test262_path_join(ctx, &ctx->suite, &dir, &ctx->conf.suite);
        if (ret != NJS_OK) {
            return ret;
        }

        goto found;
    }

    return njs_test262_error(ctx, "test262 checkout is not configured, use "
                             "--suite, NJS_TEST262_DIR or the config \"suite\" "
                             "setting");

found:

    name = njs_str_value("test");

    ret = njs_test262_path_join(ctx, &ctx->root, &ctx->suite, &name);
    if (ret != NJS_OK) {
        return ret;
    }

    name = njs_str_value("harness");

    ret = njs_test262_path_join(ctx, &ctx->harness, &ctx->suite, &name);
    if (ret != NJS_OK) {
        return ret;
    }

    if (stat((char *) ctx->root.start, &sb) != 0 || !S_ISDIR(sb.st_mode)) {
        return njs_test262_error(ctx, "\"%V\" is not a test262 checkout: "
                                 "\"%V\" is missing", &ctx->suite, &ctx->root);
    }

    if (stat((char *) ctx->harness.start, &sb) != 0 || !S_ISDIR(sb.st_mode)) {
        return njs_test262_error(ctx, "\"%V\" is not a test262 checkout: "
                                 "\"%V\" is missing", &ctx->suite,
                                 &ctx->harness);
    }

    return NJS_OK;
}


static njs_int_t
njs_test262_emit(njs_test262_t *ctx)
{
    njs_str_t               full, test;
    njs_int_t               ret;
    njs_uint_t              i;
    njs_arr_t               *sources;
    njs_test262_path_t      path;
    njs_test262_variant_t   variant;
    njs_test262_source_t    *source;
    njs_test262_metadata_t  meta;

    ret = njs_test262_path_normalize(ctx, &ctx->emit, &path, 0);
    if (ret != NJS_OK) {
        return ret;
    }

    ret = njs_test262_resolve(ctx, &path);
    if (ret != NJS_OK) {
        return ret;
    }

    if (path.dir) {
        return njs_test262_error(ctx, "--emit requires a test file, not a "
                                 "directory");
    }

    ret = njs_test262_path_join(ctx, &full, &ctx->root, &path.path);
    if (ret != NJS_OK) {
        return ret;
    }

    ret = njs_test262_read_file(ctx, ctx->pool, &full, &test);
    if (ret != NJS_OK) {
        return ret;
    }

    ret = njs_test262_metadata(ctx, ctx->pool, &path.path, &test, &meta);
    if (ret != NJS_OK) {
        return ret;
    }

    if (meta.flags & NJS_TEST262_MODULE) {
        return njs_test262_error(ctx, "--emit does not support module tests");
    }

    njs_memzero(&variant, sizeof(njs_test262_variant_t));
    variant.path = path.path;
    variant.type = (meta.flags & NJS_TEST262_RAW) ? NJS_TEST262_VARIANT_RAW
                                                   : NJS_TEST262_VARIANT_STRICT;

    sources = njs_arr_create(ctx->pool, 8, sizeof(njs_test262_source_t));
    if (sources == NULL) {
        return njs_test262_error(ctx, "memory allocation failed");
    }

    ret = njs_test262_sources(ctx, ctx->pool, &variant, &meta, &test, sources);
    if (ret != NJS_OK) {
        return ret;
    }

    source = sources->start;

    for (i = 0; i < sources->items; i++) {
        njs_test262_print(ctx->out, "/* test262: %V */\n", &source[i].file);
        njs_test262_flush(ctx->out);
        (void) njs_dprint(STDOUT_FILENO, source[i].source.start,
                          source[i].source.length);
        (void) njs_dprint(STDOUT_FILENO, (u_char *) "\n", 1);
    }

    return NJS_OK;
}


/*
 * The configuration path has to be known before the configuration is read,
 * while every other option has to be applied after it so that the command
 * line overrides the file.  A pre-scan keeps that precedence independent of
 * the order the options appear in.
 */

static njs_int_t
njs_test262_config_path(njs_test262_t *ctx, int argc, char **argv,
    njs_str_t *conf_file)
{
    char  *p;
    int   i;

    for (i = 1; i < argc; i++) {
        p = argv[i];

        if (strcmp(p, "-c") != 0 && strcmp(p, "--config") != 0) {
            continue;
        }

        if (++i == argc) {
            return njs_test262_error(ctx, "option \"%s\" requires an "
                                     "argument", p);
        }

        conf_file->start = (u_char *) argv[i];
        conf_file->length = njs_strlen(argv[i]);
    }

    return NJS_OK;
}


static njs_int_t
njs_test262_options(njs_test262_t *ctx, int argc, char **argv,
    njs_str_t *conf_file)
{
    char        *p, *eq, *inlined, name[32];
    int         i;
    size_t      len;
    njs_str_t   value;
    njs_int_t   ret;
    njs_bool_t  config, suite, exclude, failures, emit;

    for (i = 1; i < argc; i++) {
        p = argv[i];
        inlined = NULL;

        /*
         * Accept "--option=value" as well as "--option value".  The
         * diagnostics and the help text use the first form, so a command
         * printed by the runner has to parse when it is pasted back.
         */

        if (p[0] == '-' && p[1] == '-') {
            eq = strchr(p, '=');

            if (eq != NULL) {
                len = eq - p;

                if (len >= sizeof(name)) {
                    return njs_test262_error(ctx, "unknown option \"%s\", try "
                                             "--help", p);
                }

                memcpy(name, p, len);
                name[len] = '\0';

                p = name;
                inlined = eq + 1;
            }
        }

        if (p[0] != '-' || p[1] == '\0') {
            value.start = (u_char *) p;
            value.length = njs_strlen(p);

            ret = njs_test262_add_path(ctx, ctx->args, &value, 0);
            if (ret != NJS_OK) {
                return ret;
            }

            continue;
        }

        /* A flag takes no value, so "--list=1" is a mistake, not a flag. */

        if (inlined != NULL
            && (strcmp(p, "--help") == 0 || strcmp(p, "--list") == 0
                || strcmp(p, "--update-failures") == 0
                || strcmp(p, "--quiet") == 0 || strcmp(p, "--verbose") == 0
                || strcmp(p, "--unit-test") == 0))
        {
            return njs_test262_error(ctx, "option \"%s\" takes no argument",
                                     p);
        }

        if (strcmp(p, "--help") == 0 || strcmp(p, "-h") == 0) {
            njs_printf("%s", njs_test262_usage);
            return NJS_DONE;
        }

        if (strcmp(p, "--list") == 0) {
            ctx->list = 1;
            continue;
        }

        if (strcmp(p, "-u") == 0 || strcmp(p, "--update-failures") == 0) {
            ctx->update = 1;
            continue;
        }

        if (strcmp(p, "-q") == 0 || strcmp(p, "--quiet") == 0) {
            ctx->quiet = 1;
            continue;
        }

        if (strcmp(p, "-v") == 0 || strcmp(p, "--verbose") == 0) {
            ctx->verbose = 1;
            continue;
        }

        if (strcmp(p, "--isolation") == 0) {
            if (inlined == NULL) {
                if (++i == argc) {
                    return njs_test262_error(ctx, "option \"%s\" requires an "
                                             "argument", p);
                }

                inlined = argv[i];
            }

            if (strcmp(inlined, "process") != 0) {
                return njs_test262_error(ctx, "option \"--isolation\" "
                                         "requires the value \"process\"");
            }

            ctx->isolation = 1;
            continue;
        }

        if (strcmp(p, "--timeout") == 0) {
            if (inlined == NULL) {
                if (++i == argc) {
                    return njs_test262_error(ctx, "option \"%s\" requires an "
                                             "argument", p);
                }

                inlined = argv[i];
            }

            ctx->timeout = (uint32_t) atol(inlined);
            if (ctx->timeout == 0) {
                return njs_test262_error(ctx, "option \"--timeout\" requires "
                                         "a positive value");
            }

            continue;
        }

        config = (strcmp(p, "-c") == 0 || strcmp(p, "--config") == 0);
        suite = (strcmp(p, "-s") == 0 || strcmp(p, "--suite") == 0);
        exclude = (strcmp(p, "-x") == 0 || strcmp(p, "--exclude") == 0);
        failures = (strcmp(p, "-f") == 0 || strcmp(p, "--failures") == 0);
        emit = (strcmp(p, "--emit") == 0);

        if (!config && !suite && !exclude && !failures && !emit)
        {
            return njs_test262_error(ctx, "unknown option \"%s\", try --help",
                                     p);
        }

        if (inlined != NULL) {
            value.start = (u_char *) inlined;
            value.length = njs_strlen(inlined);

        } else {
            if (++i == argc) {
                return njs_test262_error(ctx, "option \"%s\" requires an "
                                         "argument", p);
            }

            value.start = (u_char *) argv[i];
            value.length = njs_strlen(argv[i]);
        }

        if (value.length == 0) {
            return njs_test262_error(ctx, "option \"%s\" requires a non-empty "
                                     "argument", p);
        }

        if (config) {
            *conf_file = value;
            continue;
        }

        if (suite) {
            ctx->suite = value;
            continue;
        }

        if (failures) {
            ret = njs_test262_str_dup(ctx, &ctx->conf.failures, &value);
            if (ret != NJS_OK) {
                return ret;
            }

            continue;
        }

        if (emit) {
            if (ctx->emit.length != 0) {
                return njs_test262_error(ctx, "--emit may only be specified "
                                         "once");
            }

            ret = njs_test262_str_dup(ctx, &ctx->emit, &value);
            if (ret != NJS_OK) {
                return ret;
            }

            continue;
        }

        ret = njs_test262_add_path(ctx, ctx->excludes, &value, 1);
        if (ret != NJS_OK) {
            return ret;
        }

        ctx->cli_excludes = 1;

    }

    return NJS_OK;
}


static njs_int_t
njs_test262_init(njs_test262_t *ctx)
{
    ctx->pool = njs_mp_fast_create(2 * njs_pagesize(), 128, 512, 16);
    if (ctx->pool == NULL) {
        return NJS_ERROR;
    }

    ctx->out = njs_mp_alloc(ctx->pool, sizeof(njs_test262_out_t));
    if (ctx->out == NULL) {
        return NJS_ERROR;
    }

    ctx->out->fd = STDOUT_FILENO;
    ctx->out->size = 0;

    ctx->tests = njs_arr_create(ctx->pool, 4, sizeof(njs_test262_path_t));
    ctx->excludes = njs_arr_create(ctx->pool, 4, sizeof(njs_test262_path_t));
    ctx->args = njs_arr_create(ctx->pool, 4, sizeof(njs_test262_path_t));
    ctx->features = njs_arr_create(ctx->pool, 16,
                                   sizeof(njs_test262_feature_t));
    ctx->files = njs_arr_create(ctx->pool, 64, sizeof(njs_str_t));
    ctx->variants = njs_arr_create(ctx->pool, 64,
                                   sizeof(njs_test262_variant_t));
    ctx->cache = njs_arr_create(ctx->pool, 16,
                                sizeof(njs_test262_harness_t));

    if (ctx->tests == NULL || ctx->excludes == NULL || ctx->args == NULL
        || ctx->features == NULL || ctx->files == NULL
        || ctx->variants == NULL || ctx->cache == NULL)
    {
        return NJS_ERROR;
    }

    return NJS_OK;
}


static njs_int_t
njs_test262_run(njs_test262_t *ctx, int argc, char **argv)
{
    njs_str_t  conf_file;
    njs_int_t  ret;

    conf_file.start = NULL;
    conf_file.length = 0;

    ret = njs_test262_config_path(ctx, argc, argv, &conf_file);
    if (ret != NJS_OK) {
        return ret;
    }

    if (conf_file.length != 0) {
        ret = njs_test262_conf_read(ctx, &conf_file);
        if (ret != NJS_OK) {
            return ret;
        }

    } else if (!ctx->quiet) {
        njs_stderror("njs_test262: no configuration given, using the "
                     "compiled defaults: no feature policy, no exclusions. "
                     "Add -c "
                     "test/njs_test262.conf for the full configuration\n");
    }

    /* The command line is applied last so that it wins. */

    ret = njs_test262_options(ctx, argc, argv, &conf_file);
    if (ret != NJS_OK) {
        return ret;
    }

    ret = njs_test262_suite(ctx, (conf_file.length != 0) ? &conf_file : NULL);
    if (ret != NJS_OK) {
        return ret;
    }

    if (ctx->emit.length != 0) {
        if (ctx->args->items != 0 || ctx->cli_excludes || ctx->list
            || ctx->update || ctx->conf.failures.length != 0 || ctx->isolation)
        {
            return njs_test262_error(ctx, "--emit cannot be combined with test "
                                     "selection, exclusions, result baselines, "
                                     "--list, --update-failures, or isolation");
        }

        return njs_test262_emit(ctx);
    }

    ret = njs_test262_select(ctx);
    if (ret != NJS_OK) {
        return ret;
    }

    if (ctx->conf.failures.length == 0 && ctx->update)
    {
        return njs_test262_error(ctx, "--update-failures needs an "
                                 "expected-results file, add --failures FILE "
                                 "or the \"failures\" configuration setting");
    }

    if (ctx->update) {
        if (ctx->list) {
            return njs_test262_error(ctx, "--update-failures needs a run, it "
                                  "cannot be combined with --list");
        }

        if (ctx->args->items != 0 || ctx->cli_excludes) {
            return njs_test262_error(ctx, "--update-failures needs the whole "
                                     "configured profile");
        }
    }

    if (ctx->conf.failures.length != 0) {
        ret = njs_test262_expect_read(ctx, &ctx->conf.failures);
        if (ret != NJS_OK) {
            return ret;
        }
    }

    if (ctx->list) {
        return njs_test262_list(ctx);
    }

    return njs_test262_execute(ctx);
}

#if NJS_TEST262_UNIT_TEST


/*
 * Unit tests.  Every parser is exercised through literal fixtures so that
 * neither a test262 checkout nor the checked-in configuration is required.
 */

typedef struct {
    njs_str_t                   path;
    njs_str_t                   expect;
    uint8_t                     dir;
    uint8_t                     prefix;
    uint8_t                     error;
} njs_test262_path_case_t;


typedef struct {
    njs_str_t                   dir;
    uint8_t                     is_dir;
    uint8_t                     is_prefix;
    njs_str_t                   path;
    uint8_t                     expect;
} njs_test262_contains_case_t;


typedef struct {
    njs_str_t                   source;
    njs_str_t                   error;
} njs_test262_conf_case_t;


typedef struct {
    njs_str_t                   source;
    uint32_t                    flags;
    njs_test262_phase_t         phase;
    njs_str_t                   type;
    njs_uint_t                  includes;
    njs_uint_t                  features;
    njs_str_t                   error;
} njs_test262_meta_case_t;


typedef struct {
    njs_test262_variant_type_t  type;
    uint8_t                     skip;
} njs_test262_expect_t;


typedef struct {
    uint32_t                    flags;
    njs_test262_mode_t          mode;
    njs_uint_t                  n;
    njs_test262_expect_t        expect[2];
} njs_test262_variant_case_t;


static njs_uint_t  njs_test262_failed;


static void
njs_test262_failure(const char *fmt, ...)
{
    u_char   *p, text[NJS_TEST262_LINE_SIZE];
    va_list  args;

    va_start(args, fmt);
    p = njs_vsprintf(text, text + sizeof(text) - 1, fmt, args);
    va_end(args);

    *p++ = '\n';

    (void) njs_dprint(STDERR_FILENO, text, p - text);

    njs_test262_failed++;
}


static njs_bool_t
njs_test262_expect_error(njs_test262_t *ctx, const njs_str_t *expect,
    const char *tag, const njs_str_t *input)
{
    njs_str_t  err;

    err.start = ctx->err;
    err.length = ctx->err_len;

    ctx->err_len = 0;

    if (expect->length == 0) {
        if (err.length != 0) {
            njs_test262_failure("%s \"%V\": unexpected error \"%V\"", tag,
                                input, &err);
            return 0;
        }

        return 1;
    }

    if (!njs_strstr_eq(&err, expect)) {
        njs_test262_failure("%s \"%V\": expected error \"%V\", got \"%V\"",
                            tag, input, expect, &err);
        return 0;
    }

    return 1;
}


static void
njs_test262_test_paths(njs_test262_t *ctx)
{
    njs_int_t           ret;
    njs_uint_t          i;
    njs_test262_path_t  path, dir, arg;

    static const njs_test262_path_case_t  cases[] = {
        { njs_str("a/b"), njs_str("a/b"), 0, 0, 0 },
        { njs_str("a/b/"), njs_str("a/b"), 1, 0, 0 },
        { njs_str("./a//b/"), njs_str("a/b"), 1, 0, 0 },
        { njs_str("a/./b"), njs_str("a/b"), 0, 0, 0 },
        { njs_str("a/c/../b"), njs_str("a/b"), 0, 0, 0 },
        { njs_str("a/b/../../c"), njs_str("c"), 0, 0, 0 },
        { njs_str("language/arguments-object/cls-*"),
          njs_str("language/arguments-object/cls-"), 0, 1, 0 },
        { njs_str(""), njs_str(""), 0, 0, 0 },
        { njs_str("built-ins/Array/from"), njs_str("built-ins/Array/from"),
          0, 0, 0 },
        { njs_str("/a"), njs_str("path \"/a\" must be relative to the test262 "
                                  "test/ directory"), 0, 0, 1 },
        { njs_str(".."), njs_str("path \"..\" escapes the test262 test/ "
                                  "directory"), 0, 0, 1 },
        { njs_str("a/../.."), njs_str("path \"a/../..\" escapes the test262 "
                                       "test/ directory"), 0, 0, 1 },
        { njs_str("*"), njs_str("path \"*\" must use a single trailing '*'"),
          0, 0, 1 },
        { njs_str("a/*/b"),
          njs_str("path \"a/*/b\" must use a single trailing '*'"), 0, 0, 1 },
        { njs_str("a/**"), njs_str("path \"a/**\" must use a single trailing "
                                    "'*'"), 0, 0, 1 },
    };

    static const njs_test262_contains_case_t  contains[] = {
        { njs_str("a"), 1, 0, njs_str("a/b"), 1 },
        { njs_str("a"), 1, 0, njs_str("a"), 1 },
        { njs_str("a"), 1, 0, njs_str("ab"), 0 },
        { njs_str("a"), 1, 0, njs_str("b/a"), 0 },
        { njs_str(""), 1, 0, njs_str("a/b/c"), 1 },
        { njs_str("a/b.js"), 0, 0, njs_str("a/b.js"), 1 },
        { njs_str("a/b.js"), 0, 0, njs_str("a/b.js.js"), 0 },
        { njs_str("a"), 0, 0, njs_str("a/b"), 0 },
        { njs_str("a/cls-"), 0, 1, njs_str("a/cls-test.js"), 1 },
        { njs_str("a/cls-"), 0, 1, njs_str("a/class-test.js"), 0 },
    };

    for (i = 0; i < njs_nitems(cases); i++) {
        ctx->err_len = 0;

        ret = njs_test262_path_normalize(ctx, &cases[i].path, &path, 1);

        if (cases[i].error) {
            if (ret == NJS_OK) {
                njs_test262_failure("normalize \"%V\": expected an error",
                                    &cases[i].path);
                ctx->err_len = 0;
                continue;
            }

            (void) njs_test262_expect_error(ctx, &cases[i].expect,
                                            "normalize", &cases[i].path);
            continue;
        }

        if (ret != NJS_OK) {
            (void) njs_test262_expect_error(ctx, &cases[i].expect,
                                            "normalize", &cases[i].path);
            continue;
        }

        if (!njs_strstr_eq(&path.path, &cases[i].expect)) {
            njs_test262_failure("normalize \"%V\": expected \"%V\", got "
                                "\"%V\"", &cases[i].path, &cases[i].expect,
                                &path.path);
            continue;
        }

        if (path.dir != cases[i].dir) {
            njs_test262_failure("normalize \"%V\": expected dir %d, got %d",
                                &cases[i].path, (int) cases[i].dir,
                                (int) path.dir);
        }

        if (path.prefix != cases[i].prefix) {
            njs_test262_failure("normalize \"%V\": expected prefix %d, got "
                                "%d", &cases[i].path, (int) cases[i].prefix,
                                (int) path.prefix);
        }
    }

    for (i = 0; i < njs_nitems(contains); i++) {
        dir.path = contains[i].dir;
        dir.dir = contains[i].is_dir;
        dir.prefix = contains[i].is_prefix;

        arg.path = contains[i].path;
        arg.dir = 0;

        if (njs_test262_path_contains(&dir, &arg) != contains[i].expect) {
            njs_test262_failure("contains(\"%V\", \"%V\"): expected %d",
                                &contains[i].dir, &contains[i].path,
                                (int) contains[i].expect);
        }
    }
}


static void
njs_test262_test_order(void)
{
    njs_str_t   files[6];
    njs_uint_t  i;

    static const njs_str_t  sorted[] = {
        njs_str("built-ins/Array/from/a.js"),
        njs_str("built-ins/Array/from/b.js"),
        njs_str("built-ins/Array/of.js"),
        njs_str("built-ins/Array2/a.js"),
        njs_str("built-ins/ArrayBuffer/a.js"),
        njs_str("language/expressions/a.js"),
    };

    files[0] = sorted[3];
    files[1] = sorted[5];
    files[2] = sorted[0];
    files[3] = sorted[4];
    files[4] = sorted[2];
    files[5] = sorted[1];

    njs_qsort(files, njs_nitems(files), sizeof(njs_str_t),
              njs_test262_str_cmp, NULL);

    for (i = 0; i < njs_nitems(files); i++) {
        if (!njs_strstr_eq(&files[i], &sorted[i])) {
            njs_test262_failure("order: at %ui expected \"%V\", got \"%V\"", i,
                                &sorted[i], &files[i]);
            return;
        }
    }
}


static void
njs_test262_test_conf(void)
{
    njs_int_t              ret;
    njs_str_t              name;
    njs_uint_t             i;
    njs_test262_t          ctx;
    njs_test262_path_t     *paths;
    njs_test262_feature_t  *feature;

    static const njs_str_t  file = njs_str("conf");

    static const njs_str_t  source = njs_str(
        "# comment\n"
        "[config]\n"
        "suite = ../test262\n"
        "mode=both\n"
        "async=skip\n"
        "module=no\n"
        "\n"
        "[features]\n"
        "Promise\n"
        "Proxy=skip\n"
        "async-functions=yes\n"
        "\n"
        "[exclude]\n"
        "intl402/\n"
        "built-ins/Atomics/foo.js\n"
        "\n"
        "[tests]\n"
        "built-ins/Array\n");

    static const njs_test262_conf_case_t  errors[] = {
        { njs_str("[bogus]\n"),
          njs_str("conf:1: unknown section \"[bogus]\"") },
        { njs_str("[config]\nbogus=1\n"),
          njs_str("conf:2: unknown [config] key \"bogus\"") },
        { njs_str("[config]\nmode=x\n"),
          njs_str("conf:2: invalid mode \"x\", expected strict, non-strict "
                  "or both") },
        { njs_str("[config]\nasync=maybe\n"),
          njs_str("conf:2: invalid value \"maybe\" for \"async\", expected "
                  "yes, no or skip") },
        { njs_str("[config]\nmode=strict\nmode=both\n"),
          njs_str("conf:3: duplicate [config] key \"mode\"") },
        { njs_str("[config]\nsuite\n"),
          njs_str("conf:2: \"suite\" requires a value") },
        { njs_str("x=1\n"),
          njs_str("conf:1: \"x=1\" appears before any section") },
        { njs_str("[features]\nProxy=maybe\n"),
          njs_str("conf:2: invalid value \"maybe\" for feature \"Proxy\", "
                  "expected yes or skip") },
        { njs_str("[tests]\nfoo=1\n"),
          njs_str("conf:2: \"foo=1\" does not accept a value") },
        { njs_str("[exclude]\n/abs\n"),
          njs_str("conf:2: path \"/abs\" must be relative to the test262 "
                  "test/ directory") },
        { njs_str("[features]\nProxy\nProxy=skip\n"),
          njs_str("conf: duplicate feature \"Proxy\"") },
    };

    njs_memzero(&ctx, sizeof(njs_test262_t));

    if (njs_test262_init(&ctx) != NJS_OK) {
        njs_test262_failure("conf: initialization failed");
        return;
    }

    ret = njs_test262_conf_parse(&ctx, &file, &source);
    if (ret != NJS_OK) {
        njs_test262_failure("conf: unexpected error \"%*s\"", ctx.err_len,
                            ctx.err);
        goto done;
    }

    name = njs_str_value("../test262");

    if (!njs_strstr_eq(&ctx.conf.suite, &name)) {
        njs_test262_failure("conf: suite is \"%V\"", &ctx.conf.suite);
    }

    if (ctx.conf.mode != NJS_TEST262_MODE_BOTH) {
        njs_test262_failure("conf: mode is %d", (int) ctx.conf.mode);
    }

    if (ctx.conf.async != NJS_TEST262_POLICY_SKIP) {
        njs_test262_failure("conf: async is %d", (int) ctx.conf.async);
    }

    if (ctx.conf.module != NJS_TEST262_POLICY_NO) {
        njs_test262_failure("conf: module is %d", (int) ctx.conf.module);
    }

    if (ctx.features->items != 3) {
        njs_test262_failure("conf: %ui features", ctx.features->items);

    } else {
        feature = ctx.features->start;

        name = njs_str_value("Promise");

        if (!njs_strstr_eq(&feature[0].name, &name) || feature[0].skip) {
            njs_test262_failure("conf: feature[0] is \"%V\"",
                                &feature[0].name);
        }

        name = njs_str_value("Proxy");

        if (!njs_strstr_eq(&feature[1].name, &name) || !feature[1].skip) {
            njs_test262_failure("conf: feature[1] is \"%V\"",
                                &feature[1].name);
        }

        name = njs_str_value("async-functions");

        if (!njs_strstr_eq(&feature[2].name, &name) || feature[2].skip) {
            njs_test262_failure("conf: feature[2] is \"%V\"",
                                &feature[2].name);
        }

        name = njs_str_value("Proxy");

        if (njs_test262_feature_find(&ctx, &name) != &feature[1]) {
            njs_test262_failure("conf: feature lookup failed");
        }

        name = njs_str_value("Nope");

        if (njs_test262_feature_find(&ctx, &name) != NULL) {
            njs_test262_failure("conf: unknown feature was found");
        }
    }

    if (ctx.excludes->items != 2) {
        njs_test262_failure("conf: %ui excludes", ctx.excludes->items);

    } else {
        paths = ctx.excludes->start;

        name = njs_str_value("intl402");

        if (!njs_strstr_eq(&paths[0].path, &name) || !paths[0].dir) {
            njs_test262_failure("conf: exclude[0] is \"%V\"", &paths[0].path);
        }

        name = njs_str_value("built-ins/Atomics/foo.js");

        if (!njs_strstr_eq(&paths[1].path, &name) || paths[1].dir) {
            njs_test262_failure("conf: exclude[1] is \"%V\"", &paths[1].path);
        }
    }

    if (ctx.tests->items != 1) {
        njs_test262_failure("conf: %ui tests", ctx.tests->items);

    } else {
        paths = ctx.tests->start;

        name = njs_str_value("built-ins/Array");

        if (!njs_strstr_eq(&paths[0].path, &name)) {
            njs_test262_failure("conf: tests[0] is \"%V\"", &paths[0].path);
        }
    }

done:

    njs_mp_destroy(ctx.pool);

    for (i = 0; i < njs_nitems(errors); i++) {
        njs_memzero(&ctx, sizeof(njs_test262_t));

        if (njs_test262_init(&ctx) != NJS_OK) {
            njs_test262_failure("conf: initialization failed");
            return;
        }

        ret = njs_test262_conf_parse(&ctx, &file, &errors[i].source);

        if (ret == NJS_OK) {
            njs_test262_failure("conf \"%V\": expected an error",
                                &errors[i].source);

        } else {
            (void) njs_test262_expect_error(&ctx, &errors[i].error, "conf",
                                            &errors[i].source);
        }

        njs_mp_destroy(ctx.pool);
    }
}


static void
njs_test262_test_meta(njs_test262_t *ctx)
{
    njs_mp_t                *pool;
    njs_str_t               *entry;
    njs_int_t               ret;
    njs_uint_t              i, n;
    njs_test262_metadata_t  meta;

    static const njs_str_t  path = njs_str("t.js");
    static const njs_str_t  includes[] = {
        njs_str("compareArray.js"), njs_str("propertyHelper.js")
    };
    static const njs_str_t  features[] = {
        njs_str("Symbol.iterator"), njs_str("Proxy")
    };

    static const njs_test262_meta_case_t  cases[] = {
        { njs_str("// Copyright\n"
                  "/*---\n"
                  "description: foo\n"
                  "flags: [onlyStrict]\n"
                  "includes: [compareArray.js, propertyHelper.js]\n"
                  "features:\n"
                  "  - Symbol.iterator\n"
                  "  - Proxy\n"
                  "---*/\n"
                  "var x;\n"),
          NJS_TEST262_ONLY_STRICT, NJS_TEST262_PHASE_NONE, njs_null_str,
          2, 2, njs_null_str },

        { njs_str("/*---\n"
                  "info: |\n"
                  "  something:\n"
                  "  flags: [module]\n"
                  "esid: sec-foo\n"
                  "---*/\n"),
          0, NJS_TEST262_PHASE_NONE, njs_null_str, 0, 0, njs_null_str },

        { njs_str("/*---\n"
                  "negative:\n"
                  "  phase: parse\n"
                  "  type: SyntaxError\n"
                  "flags: [raw]\n"
                  "---*/\n"),
          NJS_TEST262_RAW, NJS_TEST262_PHASE_PARSE, njs_str("SyntaxError"),
          0, 0, njs_null_str },

        { njs_str("/*---\n"
                  "negative:\n"
                  "  phase: resolution\n"
                  "  type: SyntaxError\n"
                  "flags: [module]\n"
                  "---*/\n"),
          NJS_TEST262_MODULE, NJS_TEST262_PHASE_RESOLUTION,
          njs_str("SyntaxError"), 0, 0, njs_null_str },

        { njs_str("/*---\n"
                  "negative:\n"
                  "  phase: early\n"
                  "  type: ReferenceError\n"
                  "---*/\n"),
          0, NJS_TEST262_PHASE_PARSE, njs_str("ReferenceError"), 0, 0,
          njs_null_str },

        { njs_str("/*---\n"
                  "description: a: b\n"
                  "flags: [noStrict]\n"
                  "---*/\n"),
          NJS_TEST262_NO_STRICT, NJS_TEST262_PHASE_NONE, njs_null_str, 0, 0,
          njs_null_str },

        { njs_str("/*---\n"
                  "description: >\n"
                  "    text\n"
                  "    more\n"
                  "flags: [async]\n"
                  "---*/\n"),
          NJS_TEST262_ASYNC, NJS_TEST262_PHASE_NONE, njs_null_str, 0, 0,
          njs_null_str },

        { njs_str("/*---\n"
                  "features: []\n"
                  "flags: [generated, CanBlockIsFalse]\n"
                  "---*/\n"),
          NJS_TEST262_GENERATED | NJS_TEST262_CAN_BLOCK_NO,
          NJS_TEST262_PHASE_NONE, njs_null_str, 0, 0, njs_null_str },

        /* CR only line terminators. */
        { njs_str("// c\r"
                  "/*---\r"
                  "flags: [noStrict]\r"
                  "includes: [compareArray.js]\r"
                  "---*/\r"),
          NJS_TEST262_NO_STRICT, NJS_TEST262_PHASE_NONE, njs_null_str, 1, 0,
          njs_null_str },

        /* CRLF line terminators. */
        { njs_str("/*---\r\n"
                  "flags: [module]\r\n"
                  "---*/\r\n"),
          NJS_TEST262_MODULE, NJS_TEST262_PHASE_NONE, njs_null_str, 0, 0,
          njs_null_str },

        /* Uniformly indented top level keys. */
        { njs_str("/*---\n"
                  " description: >\n"
                  "    text\n"
                  "    more\n"
                  " flags: [module]\n"
                  "---*/\n"),
          NJS_TEST262_MODULE, NJS_TEST262_PHASE_NONE, njs_null_str, 0, 0,
          njs_null_str },

        { njs_str("/*---\n"
                  "  esid: x\n"
                  "flags: [module]\n"
                  "---*/\n"),
          0, NJS_TEST262_PHASE_NONE, njs_null_str, 0, 0,
          njs_str("t.js:3: unexpected indented line at the top level") },

        { njs_str("/*---\n"
                  "flags: [bogus]\n"
                  "---*/\n"),
          0, NJS_TEST262_PHASE_NONE, njs_null_str, 0, 0,
          njs_str("t.js:2: unknown flag \"bogus\"") },

        { njs_str("/*---\n"
                  "negative:\n"
                  "  phase: parse\n"
                  "  type: SyntaxError\n"
                  "  extra: 1\n"
                  "---*/\n"),
          0, NJS_TEST262_PHASE_NONE, njs_null_str, 0, 0,
          njs_str("t.js:5: unknown negative key \"extra\"") },

        { njs_str("/*---\n"
                  "negative:\n"
                  "  phase: parse\n"
                  "---*/\n"),
          0, NJS_TEST262_PHASE_NONE, njs_null_str, 0, 0,
          njs_str("t.js:2: negative requires both phase and type") },

        { njs_str("/*---\n"
                  "negative:\n"
                  "  phase: bogus\n"
                  "  type: SyntaxError\n"
                  "---*/\n"),
          0, NJS_TEST262_PHASE_NONE, njs_null_str, 0, 0,
          njs_str("t.js:3: unknown negative phase \"bogus\"") },

        { njs_str("var x;\n"),
          0, NJS_TEST262_PHASE_NONE, njs_null_str, 0, 0,
          njs_str("t.js:1: no test262 frontmatter found") },

        { njs_str("/*---\n"
                  "flags: [raw]\n"),
          0, NJS_TEST262_PHASE_NONE, njs_null_str, 0, 0,
          njs_str("t.js:1: unterminated test262 frontmatter") },

        { njs_str("/*---\n"
                  "just text\n"
                  "---*/\n"),
          0, NJS_TEST262_PHASE_NONE, njs_null_str, 0, 0,
          njs_str("t.js:2: malformed frontmatter line \"just text\"") },

        { njs_str("/*---\n"
                  "includes: compareArray.js\n"
                  "---*/\n"),
          0, NJS_TEST262_PHASE_NONE, njs_null_str, 0, 0,
          njs_str("t.js:2: \"includes\" expects an inline or indented array, "
                  "got \"compareArray.js\"") },

        /* A delimiter is recognized only as a whole line. */
        { njs_str("var s = \"/*---\";\n"
                  "/*---\n"
                  "info: |\n"
                  "  ---*/ inside a block scalar\n"
                  "flags: [module]\n"
                  "---*/\n"),
          NJS_TEST262_MODULE, NJS_TEST262_PHASE_NONE, njs_null_str, 0, 0,
          njs_null_str },

        { njs_str("/*---\n"
                  "flags: [raw]\n"
                  "flags: [module]\n"
                  "---*/\n"),
          0, NJS_TEST262_PHASE_NONE, njs_null_str, 0, 0,
          njs_str("t.js:3: duplicate key \"flags\"") },

        { njs_str("/*---\n"
                  "negative:\n"
                  "  phase: parse\n"
                  "  phase: runtime\n"
                  "  type: SyntaxError\n"
                  "---*/\n"),
          0, NJS_TEST262_PHASE_NONE, njs_null_str, 0, 0,
          njs_str("t.js:4: duplicate negative key \"phase\"") },

        { njs_str("/*---\n"
                  "features: [\"Proxy\"]\n"
                  "---*/\n"),
          0, NJS_TEST262_PHASE_NONE, njs_null_str, 0, 0,
          njs_str("t.js:2: \"features\" value \"\"Proxy\"\" needs YAML "
                  "semantics that the runner does not implement") },

        { njs_str("/*---\n"
                  "features:\n"
                  "  - Proxy # a comment\n"
                  "---*/\n"),
          0, NJS_TEST262_PHASE_NONE, njs_null_str, 0, 0,
          njs_str("t.js:3: \"features\" value \"Proxy # a comment\" needs "
                  "YAML semantics that the runner does not implement") },
    };

    for (i = 0; i < njs_nitems(cases); i++) {
        pool = njs_mp_fast_create(2 * njs_pagesize(), 128, 512, 16);
        if (pool == NULL) {
            njs_test262_failure("meta: memory allocation failed");
            return;
        }

        ctx->err_len = 0;

        ret = njs_test262_metadata(ctx, pool, &path, &cases[i].source, &meta);

        if (cases[i].error.length != 0) {
            if (ret == NJS_OK) {
                njs_test262_failure("meta \"%V\": expected an error",
                                    &cases[i].source);
                ctx->err_len = 0;

            } else {
                (void) njs_test262_expect_error(ctx, &cases[i].error, "meta",
                                                &cases[i].source);
            }

            njs_mp_destroy(pool);
            continue;
        }

        if (ret != NJS_OK) {
            (void) njs_test262_expect_error(ctx, &cases[i].error, "meta",
                                            &cases[i].source);
            njs_mp_destroy(pool);
            continue;
        }

        if (meta.flags != cases[i].flags) {
            njs_test262_failure("meta \"%V\": flags 0x%uxD, expected 0x%uxD",
                                &cases[i].source, meta.flags, cases[i].flags);
        }

        if (meta.phase != cases[i].phase) {
            njs_test262_failure("meta \"%V\": phase %d, expected %d",
                                &cases[i].source, (int) meta.phase,
                                (int) cases[i].phase);
        }

        if (cases[i].type.length != 0
            && !njs_strstr_eq(&meta.type, &cases[i].type))
        {
            njs_test262_failure("meta \"%V\": type \"%V\"", &cases[i].source,
                                &meta.type);
        }

        n = (meta.includes != NULL) ? meta.includes->items : 0;

        if (n != cases[i].includes) {
            njs_test262_failure("meta \"%V\": %ui includes, expected %ui",
                                &cases[i].source, n, cases[i].includes);
        }

        if (i == 0 && meta.includes != NULL) {
            entry = meta.includes->start;

            if (!njs_strstr_eq(&entry[0], &includes[0])
                || !njs_strstr_eq(&entry[1], &includes[1]))
            {
                njs_test262_failure("meta: includes are not preserved");
            }
        }

        n = (meta.features != NULL) ? meta.features->items : 0;

        if (n != cases[i].features) {
            njs_test262_failure("meta \"%V\": %ui features, expected %ui",
                                &cases[i].source, n, cases[i].features);
        }

        if (i == 0 && meta.features != NULL) {
            entry = meta.features->start;

            if (!njs_strstr_eq(&entry[0], &features[0])
                || !njs_strstr_eq(&entry[1], &features[1]))
            {
                njs_test262_failure("meta: features are not preserved");
            }
        }

        njs_mp_destroy(pool);
    }
}


static void
njs_test262_test_variants(njs_test262_t *ctx)
{
    njs_str_t               path, *feature;
    njs_int_t               ret;
    njs_uint_t              i, j;
    njs_test262_variant_t   *variant;
    njs_test262_feature_t   *known;
    njs_test262_metadata_t  meta;

    static const njs_test262_variant_case_t  cases[] = {
        { 0, NJS_TEST262_MODE_STRICT, 1,
          { { NJS_TEST262_VARIANT_STRICT, 0 } } },
        { NJS_TEST262_NO_STRICT, NJS_TEST262_MODE_STRICT, 1,
          { { NJS_TEST262_VARIANT_NON_STRICT, 1 } } },
        { NJS_TEST262_ONLY_STRICT, NJS_TEST262_MODE_STRICT, 1,
          { { NJS_TEST262_VARIANT_STRICT, 0 } } },
        { 0, NJS_TEST262_MODE_NON_STRICT, 1,
          { { NJS_TEST262_VARIANT_NON_STRICT, 1 } } },
        { NJS_TEST262_ONLY_STRICT, NJS_TEST262_MODE_NON_STRICT, 1,
          { { NJS_TEST262_VARIANT_STRICT, 1 } } },
        { NJS_TEST262_NO_STRICT, NJS_TEST262_MODE_NON_STRICT, 1,
          { { NJS_TEST262_VARIANT_NON_STRICT, 1 } } },
        { 0, NJS_TEST262_MODE_BOTH, 2,
          { { NJS_TEST262_VARIANT_NON_STRICT, 1 },
            { NJS_TEST262_VARIANT_STRICT, 0 } } },
        { NJS_TEST262_ONLY_STRICT, NJS_TEST262_MODE_BOTH, 1,
          { { NJS_TEST262_VARIANT_STRICT, 0 } } },
        { NJS_TEST262_NO_STRICT, NJS_TEST262_MODE_BOTH, 1,
          { { NJS_TEST262_VARIANT_NON_STRICT, 1 } } },
        { NJS_TEST262_MODULE, NJS_TEST262_MODE_STRICT, 1,
          { { NJS_TEST262_VARIANT_MODULE, 0 } } },
        { NJS_TEST262_ASYNC, NJS_TEST262_MODE_STRICT, 1,
          { { NJS_TEST262_VARIANT_STRICT, 0 } } },
        { NJS_TEST262_ASYNC | NJS_TEST262_MODULE, NJS_TEST262_MODE_STRICT, 1,
          { { NJS_TEST262_VARIANT_MODULE, 0 } } },
        { NJS_TEST262_RAW, NJS_TEST262_MODE_STRICT, 1,
          { { NJS_TEST262_VARIANT_RAW, 1 } } },
        { NJS_TEST262_RAW, NJS_TEST262_MODE_BOTH, 1,
          { { NJS_TEST262_VARIANT_RAW, 1 } } },
        { NJS_TEST262_RAW | NJS_TEST262_ASYNC, NJS_TEST262_MODE_BOTH, 1,
          { { NJS_TEST262_VARIANT_RAW, 1 } } },
    };

    path = njs_str_value("t.js");

    for (i = 0; i < njs_nitems(cases); i++) {
        njs_arr_reset(ctx->variants);

        njs_memzero(&meta, sizeof(njs_test262_metadata_t));

        meta.flags = cases[i].flags;
        ctx->conf.mode = cases[i].mode;
        ctx->conf.async = NJS_TEST262_POLICY_YES;
        ctx->conf.module = NJS_TEST262_POLICY_YES;

        ret = njs_test262_variants(ctx, &path, &meta);
        if (ret != NJS_OK) {
            njs_test262_failure("variants: case %ui failed", i);
            ctx->err_len = 0;
            continue;
        }

        if (ctx->variants->items != cases[i].n) {
            njs_test262_failure("variants: case %ui produced %ui variants, "
                                "expected %ui", i, ctx->variants->items,
                                cases[i].n);
            continue;
        }

        variant = ctx->variants->start;

        for (j = 0; j < cases[i].n; j++) {
            if (variant[j].type != cases[i].expect[j].type) {
                njs_test262_failure("variants: case %ui variant %ui is \"%s\"",
                                    i, j,
                                    njs_test262_variant_name(variant[j].type));
            }

            if ((variant[j].reason.length != 0) != cases[i].expect[j].skip) {
                njs_test262_failure("variants: case %ui variant %ui reason "
                                    "\"%V\"", i, j, &variant[j].reason);
            }
        }
    }

    /* An unlisted feature is a skip, not a silent run. */

    njs_arr_reset(ctx->variants);
    njs_memzero(&meta, sizeof(njs_test262_metadata_t));

    ctx->conf.mode = NJS_TEST262_MODE_STRICT;

    meta.features = njs_arr_create(ctx->pool, 1, sizeof(njs_str_t));
    if (meta.features == NULL) {
        njs_test262_failure("variants: memory allocation failed");
        return;
    }

    feature = njs_arr_add(meta.features);
    if (feature == NULL) {
        njs_test262_failure("variants: memory allocation failed");
        return;
    }

    *feature = njs_str_value("Nope");

    if (njs_test262_variants(ctx, &path, &meta) != NJS_OK) {
        njs_test262_failure("variants: feature case failed");
        ctx->err_len = 0;
        return;
    }

    variant = ctx->variants->start;
    path = njs_str_value("unknown feature \"Nope\"");

    if (ctx->variants->items != 1
        || !njs_strstr_eq(&variant[0].reason, &path))
    {
        njs_test262_failure("variants: feature skip reason is \"%V\"",
                            &variant[0].reason);
    }

    njs_arr_reset(ctx->variants);
    njs_arr_reset(ctx->features);

    known = njs_arr_add(ctx->features);
    if (known == NULL) {
        njs_test262_failure("variants: memory allocation failed");
        return;
    }

    known->name = njs_str_value("Proxy");
    known->skip = 1;

    feature = meta.features->start;
    *feature = known->name;

    if (njs_test262_variants(ctx, &path, &meta) != NJS_OK) {
        njs_test262_failure("variants: skipped feature case failed");
        ctx->err_len = 0;
        return;
    }

    path = njs_str_value("feature \"Proxy\" is skipped");

    variant = ctx->variants->start;

    if (ctx->variants->items != 1
        || !njs_strstr_eq(&variant[0].reason, &path))
    {
        njs_test262_failure("variants: skipped feature reason is \"%V\"",
                            &variant[0].reason);
    }

    njs_arr_reset(ctx->variants);
}


static void
njs_test262_test_sources(njs_test262_t *ctx)
{
    njs_str_t               test, *include;
    njs_arr_t               *sources;
    njs_uint_t              i;
    njs_test262_variant_t   variant;
    njs_test262_metadata_t  meta;
    njs_test262_source_t    *source;
    njs_test262_harness_t   *harness;

    static const njs_str_t  names[] = {
        njs_str("sta.js"),
        njs_str("assert.js"),
        njs_str("doneprintHandle.js"),
        njs_str("compareArray.js"),
        njs_str("propertyHelper.js"),
        njs_str("t.js"),
    };

    njs_arr_reset(ctx->cache);
    njs_memzero(&meta, sizeof(njs_test262_metadata_t));
    njs_memzero(&variant, sizeof(njs_test262_variant_t));

    for (i = 0; i < 5; i++) {
        harness = njs_arr_add(ctx->cache);
        if (harness == NULL) {
            njs_test262_failure("sources: memory allocation failed");
            return;
        }

        harness->name = names[i];
        harness->source = names[i];
    }

    meta.flags = NJS_TEST262_ASYNC;
    meta.includes = njs_arr_create(ctx->pool, 2, sizeof(njs_str_t));
    if (meta.includes == NULL) {
        njs_test262_failure("sources: memory allocation failed");
        return;
    }

    for (i = 3; i < 5; i++) {
        include = njs_arr_add(meta.includes);
        if (include == NULL) {
            njs_test262_failure("sources: memory allocation failed");
            return;
        }

        *include = names[i];
    }

    variant.path = names[5];
    variant.type = NJS_TEST262_VARIANT_STRICT;
    test = njs_str_value("test");

    sources = njs_arr_create(ctx->pool, 8, sizeof(njs_test262_source_t));
    if (sources == NULL) {
        njs_test262_failure("sources: memory allocation failed");
        return;
    }

    if (njs_test262_sources(ctx, ctx->pool, &variant, &meta, &test, sources)
        != NJS_OK)
    {
        njs_test262_failure("sources: failed");
        ctx->err_len = 0;
        return;
    }

    if (sources->items != njs_nitems(names)) {
        njs_test262_failure("sources: %ui fragments", sources->items);
        return;
    }

    source = sources->start;

    for (i = 0; i < sources->items; i++) {
        if (!njs_strstr_eq(&source[i].file, &names[i])) {
            njs_test262_failure("sources: fragment %ui is \"%V\"", i,
                                &source[i].file);
        }
    }

    if (source[5].source.length != njs_length("\"use strict\";\ntest")
        || memcmp(source[5].source.start, "\"use strict\";\ntest",
                  source[5].source.length) != 0)
    {
        njs_test262_failure("sources: strict prefix is missing");
    }
}


static void
njs_test262_test_result_wire(njs_test262_t *ctx)
{
    size_t                size;
    u_char                buf[sizeof(njs_test262_wire_t)
                             + NJS_TEST262_MAX_MESSAGE * 3];
    njs_int_t             ret;
    njs_test262_result_t  result, unpacked;

    njs_memzero(&result, sizeof(njs_test262_result_t));

    result.status = NJS_TEST262_FAIL;
    result.stage = NJS_TEST262_STAGE_RUN;
    result.expected = njs_str_value("TypeError");
    result.actual = njs_str_value("RangeError");
    result.message = njs_str_value("fixture failure");
    result.elapsed = 1234;

    if (njs_test262_result_pack(&result, buf, &size) != NJS_OK) {
        njs_test262_failure("wire: packing failed");
        return;
    }

    ret = njs_test262_result_unpack(ctx, &unpacked, buf, size);

    if (ret != NJS_OK || unpacked.status != result.status
        || unpacked.stage != result.stage || unpacked.elapsed != result.elapsed
        || !njs_strstr_eq(&unpacked.expected, &result.expected)
        || !njs_strstr_eq(&unpacked.actual, &result.actual)
        || !njs_strstr_eq(&unpacked.message, &result.message))
    {
        njs_test262_failure("wire: round trip failed");
    }

    if (njs_test262_result_unpack(ctx, &unpacked, buf, size - 1) == NJS_OK) {
        njs_test262_failure("wire: accepted a truncated payload");
    }

    ctx->err_len = 0;
}


static njs_int_t
njs_test262_unit_test(void)
{
    njs_test262_t  ctx;

    njs_memzero(&ctx, sizeof(njs_test262_t));

    if (njs_test262_init(&ctx) != NJS_OK) {
        njs_test262_failure("initialization failed");
        return NJS_ERROR;
    }

    njs_test262_test_paths(&ctx);
    njs_test262_test_order();
    njs_test262_test_meta(&ctx);
    njs_test262_test_variants(&ctx);
    njs_test262_test_sources(&ctx);
    njs_test262_test_result_wire(&ctx);

    njs_mp_destroy(ctx.pool);

    njs_test262_test_conf();

    if (njs_test262_failed != 0) {
        njs_printf("njs_test262 unit tests failed: %ui\n", njs_test262_failed);
        return NJS_ERROR;
    }

    njs_printf("njs_test262 unit tests passed\n");

    return NJS_OK;
}

#endif


int
main(int argc, char **argv)
{
    int            rc;
    njs_int_t      ret;
    njs_test262_t  ctx;

#if NJS_TEST262_UNIT_TEST
    if (argc == 2 && strcmp(argv[1], "--unit-test") == 0) {
        return (njs_test262_unit_test() == NJS_OK) ? 0 : 1;
    }
#endif


    njs_memzero(&ctx, sizeof(njs_test262_t));

    ret = njs_test262_init(&ctx);

    if (ret == NJS_OK) {
        ret = njs_test262_run(&ctx, argc, argv);

    } else {
        (void) njs_test262_error(&ctx, "memory allocation failed");
    }

    if (ctx.out != NULL) {
        njs_test262_flush(ctx.out);
    }

    njs_test262_report_error(&ctx);

    switch (ret) {
    case NJS_OK:
    case NJS_DONE:
        rc = 0;
        break;

    case NJS_DECLINED:
        rc = 1;
        break;

    default:
        rc = 2;
        break;
    }

    if (ctx.pool != NULL) {
        njs_mp_destroy(ctx.pool);
    }

    return rc;
}
