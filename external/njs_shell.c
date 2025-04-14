
/*
 * Copyright (C) Dmitry Volyntsev
 * Copyright (C) NGINX, Inc.
 */


#include <njs.h>
#include <njs_unix.h>
#include <njs_arr.h>
#include <njs_queue.h>
#include <njs_rbtree.h>

#if (NJS_HAVE_QUICKJS)
#include <qjs.h>
#endif

#if (!defined NJS_FUZZER_TARGET && defined NJS_HAVE_READLINE)

#include <locale.h>
#include <signal.h>
#include <sys/select.h>
#if (NJS_HAVE_EDITLINE)
#include <editline/readline.h>
#elif (NJS_HAVE_EDIT_READLINE)
#include <edit/readline/readline.h>
#else
#include <readline/readline.h>
#endif

#endif


typedef struct {
    uint8_t                 disassemble;
    uint8_t                 denormals;
    uint8_t                 interactive;
    uint8_t                 module;
    uint8_t                 quiet;
    uint8_t                 sandbox;
    uint8_t                 safe;
    uint8_t                 version;
    uint8_t                 ast;
    uint8_t                 unhandled_rejection;
    uint8_t                 suppress_stdout;
    uint8_t                 opcode_debug;
    uint8_t                 generator_debug;
    uint8_t                 can_block;
    int                     exit_code;
    int                     stack_size;

    char                    *file;
    njs_str_t               command;
    size_t                  n_paths;
    njs_str_t               *paths;
    char                    **argv;
    njs_uint_t              argc;

    enum {
        NJS_ENGINE_NJS = 0,
        NJS_ENGINE_QUICKJS = 1,
    }                       engine;
} njs_opts_t;


typedef enum {
    NJS_LOG_ERROR = 4,
    NJS_LOG_WARN = 5,
    NJS_LOG_INFO = 7,
} njs_log_level_t;


typedef struct {
    size_t                  index;
    njs_arr_t               *suffix_completions;
} njs_completion_t;


typedef struct {
    NJS_RBTREE_NODE         (node);
    union {
        struct {
            njs_function_t      *function;
            njs_value_t         *args;
        }                       njs;
#if (NJS_HAVE_QUICKJS)
        struct {
            JSValue             function;
            JSValue             *args;
        }                       qjs;
#endif
    }                           u;

    njs_uint_t              nargs;
    uint32_t                id;

    njs_queue_link_t        link;
} njs_ev_t;


typedef struct {
    njs_str_t               name;
    uint64_t                time;
    njs_queue_link_t        link;
} njs_timelabel_t;


typedef struct {
    union {
        struct {
            njs_opaque_value_t  promise;
            njs_opaque_value_t  message;
        }                       njs;
#if (NJS_HAVE_QUICKJS)
        struct {
            JSValue             promise;
            JSValue             message;
        }                       qjs;
#endif
    }                           u;
} njs_rejected_promise_t;


typedef struct {
    int                 fd;
    njs_str_t           name;
    njs_str_t           file;
    char                path[NJS_MAX_PATH + 1];
} njs_module_info_t;


typedef struct  njs_engine_s     njs_engine_t;


struct njs_engine_s {
    union {
        struct {
            njs_vm_t            *vm;

            njs_opaque_value_t  value;
        }                       njs;
#if (NJS_HAVE_QUICKJS)
        struct {
            JSRuntime           *rt;
            JSContext           *ctx;
            JSValue             value;
        }                       qjs;
#endif
    }                           u;

    njs_int_t                 (*eval)(njs_engine_t *engine, njs_str_t *script);
    njs_int_t                 (*execute_pending_job)(njs_engine_t *engine);
    njs_int_t                 (*unhandled_rejection)(njs_engine_t *engine);
    njs_int_t                 (*process_events)(njs_engine_t *engine);
    njs_int_t                 (*destroy)(njs_engine_t *engine);
    njs_int_t                 (*output)(njs_engine_t *engine, njs_int_t ret);
    njs_arr_t                *(*complete)(njs_engine_t *engine, njs_str_t *ex);

    unsigned                    type;
    njs_mp_t                    *pool;
    njs_completion_t            completion;
};

typedef struct {
    njs_engine_t            *engine;

    uint32_t                event_id;
    njs_rbtree_t            events;  /* njs_ev_t * */
    njs_queue_t             posted_events;

    njs_queue_t             labels;

    njs_str_t               cwd;

    njs_arr_t               *rejected_promises;

    njs_bool_t              suppress_stdout;
    njs_bool_t              interactive;
    njs_bool_t              module;
    char                    **argv;
    njs_uint_t              argc;

#if (NJS_HAVE_QUICKJS)
    JSValue                 process;

    njs_queue_t             agents;
    njs_queue_t             reports;
    pthread_mutex_t         agent_mutex;
    pthread_cond_t          agent_cond;
    pthread_mutex_t         report_mutex;
#endif
} njs_console_t;


#if (NJS_HAVE_QUICKJS)
typedef struct {
    njs_queue_link_t        link;
    pthread_t               tid;
    njs_console_t           *console;
    char                    *script;
    JSValue                 broadcast_func;
    njs_bool_t              broadcast_pending;
    JSValue                 broadcast_sab;
    uint8_t                 *broadcast_sab_buf;
    size_t                  broadcast_sab_size;
    int32_t                 broadcast_val;
} njs_262agent_t;


typedef struct {
    njs_queue_link_t        link;
    char                    *str;
} njs_agent_report_t;
#endif


static njs_int_t njs_main(njs_opts_t *opts);
static njs_int_t njs_console_init(njs_opts_t *opts, njs_console_t *console);
static njs_int_t njs_externals_init(njs_vm_t *vm);
static njs_engine_t *njs_create_engine(njs_opts_t *opts);
static njs_int_t njs_process_file(njs_opts_t *opts);
static njs_int_t njs_process_script(njs_engine_t *engine,
    njs_console_t *console, njs_str_t *script);

#ifndef NJS_FUZZER_TARGET

static njs_int_t njs_options_parse(njs_opts_t *opts, int argc, char **argv);
static njs_int_t njs_options_parse_engine(njs_opts_t *opts, const char *engine);
static njs_int_t njs_options_add_path(njs_opts_t *opts, char *path, size_t len);
static void njs_options_free(njs_opts_t *opts);

#ifdef NJS_HAVE_READLINE
static njs_int_t njs_interactive_shell(njs_opts_t *opts);
static njs_int_t njs_editline_init(void);
static char *njs_completion_generator(const char *text, int state);
#endif

#endif

static njs_int_t njs_set_timeout(njs_vm_t *vm, njs_value_t *args,
    njs_uint_t nargs, njs_index_t unused, njs_value_t *retval);
static njs_int_t njs_set_immediate(njs_vm_t *vm, njs_value_t *args,
    njs_uint_t nargs, njs_index_t unused, njs_value_t *retval);
static njs_int_t njs_clear_timeout(njs_vm_t *vm, njs_value_t *args,
    njs_uint_t nargs, njs_index_t unused, njs_value_t *retval);
static njs_int_t njs_ext_console_log(njs_vm_t *vm, njs_value_t *args,
    njs_uint_t nargs, njs_index_t magic, njs_value_t *retval);
static njs_int_t njs_ext_console_time(njs_vm_t *vm, njs_value_t *args,
    njs_uint_t nargs, njs_index_t unused, njs_value_t *retval);
static njs_int_t njs_ext_console_time_end(njs_vm_t *vm, njs_value_t *args,
    njs_uint_t nargs, njs_index_t unused, njs_value_t *retval);

static void njs_console_log(njs_log_level_t level, const char *fmt, ...);
static void njs_console_logger(njs_log_level_t level, const u_char *start,
    size_t length);

static njs_int_t njs_console_time(njs_console_t *console, njs_str_t *name);
static void njs_console_time_end(njs_console_t *console, njs_str_t *name,
    uint64_t time);
static intptr_t njs_event_rbtree_compare(njs_rbtree_node_t *node1,
    njs_rbtree_node_t *node2);
static uint64_t njs_time(void);

njs_int_t njs_array_buffer_detach(njs_vm_t *vm, njs_value_t *args,
    njs_uint_t nargs, njs_index_t unused, njs_value_t *retval);


static njs_external_t  njs_ext_console[] = {

    {
        .flags = NJS_EXTERN_METHOD,
        .name.string = njs_str("dump"),
        .writable = 1,
        .configurable = 1,
        .enumerable = 1,
        .u.method = {
            .native = njs_ext_console_log,
#define NJS_LOG_DUMP  16
#define NJS_LOG_MASK  15
            .magic8 = NJS_LOG_INFO | NJS_LOG_DUMP,
        }
    },

    {
        .flags = NJS_EXTERN_METHOD,
        .name.string = njs_str("error"),
        .writable = 1,
        .configurable = 1,
        .enumerable = 1,
        .u.method = {
            .native = njs_ext_console_log,
            .magic8 = NJS_LOG_ERROR,
        }
    },

    {
        .flags = NJS_EXTERN_METHOD,
        .name.string = njs_str("info"),
        .writable = 1,
        .configurable = 1,
        .enumerable = 1,
        .u.method = {
            .native = njs_ext_console_log,
            .magic8 = NJS_LOG_INFO,
        }
    },

    {
        .flags = NJS_EXTERN_METHOD,
        .name.string = njs_str("log"),
        .writable = 1,
        .configurable = 1,
        .enumerable = 1,
        .u.method = {
            .native = njs_ext_console_log,
            .magic8 = NJS_LOG_INFO,
        }
    },

    {
        .flags = NJS_EXTERN_PROPERTY | NJS_EXTERN_SYMBOL,
        .name.symbol = NJS_SYMBOL_TO_STRING_TAG,
        .u.property = {
            .value = "Console",
        }
    },

    {
        .flags = NJS_EXTERN_METHOD,
        .name.string = njs_str("time"),
        .writable = 1,
        .configurable = 1,
        .enumerable = 1,
        .u.method = {
            .native = njs_ext_console_time,
        }
    },

    {
        .flags = NJS_EXTERN_METHOD,
        .name.string = njs_str("timeEnd"),
        .writable = 1,
        .configurable = 1,
        .enumerable = 1,
        .u.method = {
            .native = njs_ext_console_time_end,
        }
    },

    {
        .flags = NJS_EXTERN_METHOD,
        .name.string = njs_str("warn"),
        .writable = 1,
        .configurable = 1,
        .enumerable = 1,
        .u.method = {
            .native = njs_ext_console_log,
            .magic8 = NJS_LOG_WARN,
        }
    },

};


static njs_external_t  njs_ext_262[] = {

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

};


njs_module_t  njs_console_module = {
    .name = njs_str("console"),
    .preinit = NULL,
    .init = njs_externals_init,
};


static njs_module_t *njs_console_addon_modules[] = {
    &njs_console_module,
    NULL,
};


static njs_int_t      njs_console_proto_id;


static njs_console_t  njs_console;


static njs_int_t
njs_main(njs_opts_t *opts)
{
    njs_int_t     ret;
    njs_engine_t  *engine;

    njs_mm_denormals(opts->denormals);

    if (opts->file == NULL) {
        if (opts->command.length != 0) {
            opts->file = (char *) "string";
        }

#ifdef NJS_HAVE_READLINE
        else if (opts->interactive) {
            opts->file = (char *) "shell";
        }
#endif

        if (opts->file == NULL) {
            njs_stderror("file name is required in non-interactive mode\n");
            return NJS_ERROR;
        }
    }

    ret = njs_console_init(opts, &njs_console);
    if (njs_slow_path(ret != NJS_OK)) {
        njs_stderror("njs_console_init() failed\n");
        return NJS_ERROR;
    }

#if (!defined NJS_FUZZER_TARGET && defined NJS_HAVE_READLINE)

    if (opts->interactive) {
        ret = njs_interactive_shell(opts);

    } else

#endif

    if (opts->command.length != 0) {
        engine = njs_create_engine(opts);
        if (engine == NULL) {
            return NJS_ERROR;
        }

        ret = njs_process_script(engine, &njs_console, &opts->command);
        engine->destroy(engine);

    } else {
        ret = njs_process_file(opts);
    }

    return ret;
}


#ifndef NJS_FUZZER_TARGET

int
main(int argc, char **argv)
{
    njs_int_t   ret;
    njs_opts_t  opts;

    njs_memzero(&opts, sizeof(njs_opts_t));
    opts.interactive = 1;

    ret = njs_options_parse(&opts, argc, argv);
    if (ret != NJS_OK) {
        ret = (ret == NJS_DONE) ? NJS_OK : NJS_ERROR;
        goto done;
    }

    if (opts.version != 0) {
        njs_printf("%s\n", NJS_VERSION);
        ret = NJS_OK;
        goto done;
    }

    ret = njs_main(&opts);

done:

    njs_options_free(&opts);

    return (ret == NJS_OK) ? EXIT_SUCCESS : opts.exit_code;
}


static njs_int_t
njs_options_parse(njs_opts_t *opts, int argc, char **argv)
{
    char        *p, *start;
    size_t      len;
    njs_int_t   i, ret;
    njs_uint_t  n;

    static const char  help[] =
        "njs [options] [-c string | script.js | -] [script args]\n"
        "\n"
        "Interactive shell: "
#ifdef NJS_HAVE_READLINE
        "enabled\n"
#else
        "disabled\n"
#endif
        "\n"
        "Options:\n"
        "  -a                print AST.\n"
        "  -c                specify the command to execute.\n"
        "  -d                print disassembled code.\n"
        "  -e <code>         set failure exit code.\n"
        "  -f                disabled denormals mode.\n"
#ifdef NJS_DEBUG_GENERATOR
        "  -g                enable generator debug.\n"
#endif
        "  -j <size>         set the maximum stack size in bytes.\n"
        "  -m                load as ES6 module (script is default).\n"
#ifdef NJS_HAVE_QUICKJS
        "  -n njs|QuickJS    set JS engine (njs is default)\n"
#endif
#ifdef NJS_DEBUG_OPCODE
        "  -o                enable opcode debug.\n"
#endif
        "  -p <path>         set path prefix for modules.\n"
        "  -q                disable interactive introduction prompt.\n"
        "  -r                ignore unhandled promise rejection.\n"
        "  -s                sandbox mode.\n"
        "  -v                print njs version and exit.\n"
        "  -u                disable \"unsafe\" mode.\n"
        "  script.js | -     run code from a file or stdin.\n";

    opts->denormals = 1;
    opts->can_block = 1;
    opts->exit_code = EXIT_FAILURE;
    opts->engine = NJS_ENGINE_NJS;
    opts->unhandled_rejection = 1;

    p = getenv("NJS_EXIT_CODE");
    if (p != NULL) {
        opts->exit_code = atoi(p);
    }

    p = getenv("NJS_CAN_BLOCK");
    if (p != NULL) {
        opts->can_block = atoi(p);
    }

    p = getenv("NJS_LOAD_AS_MODULE");
    if (p != NULL) {
        opts->module = 1;
    }

    p = getenv("NJS_ENGINE");
    if (p != NULL) {
        ret = njs_options_parse_engine(opts, p);
        if (ret != NJS_OK) {
            return NJS_ERROR;
        }
    }

    start = getenv("NJS_PATH");
    if (start != NULL) {
        for ( ;; ) {
            p = (char *) njs_strchr(start, ':');

            len = (p != NULL) ? (size_t) (p - start) : njs_strlen(start);

            ret = njs_options_add_path(opts, start, len);
            if (ret != NJS_OK) {
                njs_stderror("failed to add path\n");
                return NJS_ERROR;
            }

            if (p == NULL) {
                break;
            }

            start = p + 1;
        }
    }

    for (i = 1; i < argc; i++) {

        p = argv[i];

        if (p[0] != '-' || (p[0] == '-' && p[1] == '\0')) {
            opts->interactive = 0;
            opts->file = argv[i];
            goto done;
        }

        p++;

        switch (*p) {
        case '?':
        case 'h':
            njs_printf("%*s", njs_length(help), help);
            return NJS_DONE;

        case 'a':
            opts->ast = 1;
            break;

        case 'c':
            opts->interactive = 0;

            if (++i < argc) {
                opts->command.start = (u_char *) argv[i];
                opts->command.length = njs_strlen(argv[i]);
                goto done;
            }

            njs_stderror("option \"-c\" requires argument\n");
            return NJS_ERROR;

        case 'd':
            opts->disassemble = 1;
            break;

        case 'e':
            if (++i < argc) {
                opts->exit_code = atoi(argv[i]);
                break;
            }

            njs_stderror("option \"-e\" requires argument\n");
            return NJS_ERROR;

        case 'f':

#if !(NJS_HAVE_DENORMALS_CONTROL)
            njs_stderror("option \"-f\" is not supported\n");
            return NJS_ERROR;
#endif

            opts->denormals = 0;
            break;

#ifdef NJS_DEBUG_GENERATOR
        case 'g':
            opts->generator_debug = 1;
            break;
#endif
        case 'j':
            if (++i < argc) {
                opts->stack_size = atoi(argv[i]);
                break;
            }

            njs_stderror("option \"-j\" requires argument\n");
            return NJS_ERROR;

        case 'm':
            opts->module = 1;
            break;

        case 'n':
            if (++i < argc) {
                ret = njs_options_parse_engine(opts, argv[i]);
                if (ret != NJS_OK) {
                    return NJS_ERROR;
                }

                break;
            }

            njs_stderror("option \"-n\" requires argument\n");
            return NJS_ERROR;

#ifdef NJS_DEBUG_OPCODE
        case 'o':
            opts->opcode_debug = 1;
            break;
#endif

        case 'p':
            if (++i < argc) {
                ret = njs_options_add_path(opts, argv[i], njs_strlen(argv[i]));
                if (ret != NJS_OK) {
                    njs_stderror("failed to add path\n");
                    return NJS_ERROR;
                }

                break;
            }

            njs_stderror("option \"-p\" requires directory name\n");
            return NJS_ERROR;

        case 'q':
            opts->quiet = 1;
            break;

        case 'r':
            opts->unhandled_rejection = 0;
            break;

        case 's':
            opts->sandbox = 1;
            break;

        case 't':
            if (++i < argc) {
                if (strcmp(argv[i], "module") == 0) {
                    opts->module = 1;

                } else if (strcmp(argv[i], "script") != 0) {
                    njs_stderror("option \"-t\" unexpected source type: %s\n",
                                 argv[i]);
                    return NJS_ERROR;
                }

                break;
            }

            njs_stderror("option \"-t\" requires source type\n");
            return NJS_ERROR;
        case 'v':
        case 'V':
            opts->version = 1;
            break;

        case 'u':
            opts->safe = 1;
            break;

        default:
            njs_stderror("Unknown argument: \"%s\" "
                         "try \"%s -h\" for available options\n", argv[i],
                         argv[0]);
            return NJS_ERROR;
        }
    }

done:

#ifdef NJS_HAVE_QUICKJS
    if (opts->engine == NJS_ENGINE_QUICKJS) {
        if (opts->ast) {
            njs_stderror("option \"-a\" is not supported for quickjs\n");
            return NJS_ERROR;
        }

        if (opts->disassemble) {
            njs_stderror("option \"-d\" is not supported for quickjs\n");
            return NJS_ERROR;
        }

        if (opts->generator_debug) {
            njs_stderror("option \"-g\" is not supported for quickjs\n");
            return NJS_ERROR;
        }

        if (opts->opcode_debug) {
            njs_stderror("option \"-o\" is not supported for quickjs\n");
            return NJS_ERROR;
        }

        if (opts->sandbox) {
            njs_stderror("option \"-s\" is not supported for quickjs\n");
            return NJS_ERROR;
        }

        if (opts->safe) {
            njs_stderror("option \"-u\" is not supported for quickjs\n");
            return NJS_ERROR;
        }
    }
#endif

    opts->argc = njs_max(argc - i + 1, 2);
    opts->argv = malloc(sizeof(char*) * opts->argc);
    if (opts->argv == NULL) {
        njs_stderror("failed to alloc argv\n");
        return NJS_ERROR;
    }

    opts->argv[0] = argv[0];
    opts->argv[1] = (opts->file != NULL) ? opts->file : (char *) "";
    for (n = 2; n < opts->argc; n++) {
        opts->argv[n] = argv[i + n - 1];
    }

    return NJS_OK;
}


static njs_int_t
njs_options_parse_engine(njs_opts_t *opts, const char *engine)
{
    if (strncasecmp(engine, "njs", 3) == 0) {
        opts->engine = NJS_ENGINE_NJS;

#ifdef NJS_HAVE_QUICKJS
    } else if (strncasecmp(engine, "QuickJS", 7) == 0) {
        opts->engine = NJS_ENGINE_QUICKJS;
#endif

    } else {
        njs_stderror("unknown engine \"%s\"\n", engine);
        return NJS_ERROR;
    }

    return NJS_OK;
}


static njs_int_t
njs_options_add_path(njs_opts_t *opts, char *path, size_t len)
{
    njs_str_t  *paths;

    opts->n_paths++;

    paths = realloc(opts->paths, opts->n_paths * sizeof(njs_str_t));
    if (paths == NULL) {
        njs_stderror("failed to add path\n");
        return NJS_ERROR;
    }

    opts->paths = paths;
    opts->paths[opts->n_paths - 1].start = (u_char *) path;
    opts->paths[opts->n_paths - 1].length = len;

    return NJS_OK;
}


static void
njs_options_free(njs_opts_t *opts)
{
    if (opts->paths != NULL) {
        free(opts->paths);
    }

    if (opts->argv != NULL) {
        free(opts->argv);
    }
}


#else

int
LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
    njs_opts_t  opts;

    if (size == 0) {
        return 0;
    }

    njs_memzero(&opts, sizeof(njs_opts_t));

    opts.file = (char *) "fuzzer";
    opts.command.start = (u_char *) data;
    opts.command.length = size;
    opts.suppress_stdout = 1;

    return njs_main(&opts);
}

#endif

static njs_int_t
njs_console_init(njs_opts_t *opts, njs_console_t *console)
{
    njs_memzero(console, sizeof(njs_console_t));

    njs_rbtree_init(&console->events, njs_event_rbtree_compare);
    njs_queue_init(&console->posted_events);
    njs_queue_init(&console->labels);

    console->interactive = opts->interactive;
    console->suppress_stdout = opts->suppress_stdout;
    console->module = opts->module;
    console->argv = opts->argv;
    console->argc = opts->argc;

#if (NJS_HAVE_QUICKJS)
    if (opts->engine == NJS_ENGINE_QUICKJS) {
        njs_queue_init(&console->agents);
        njs_queue_init(&console->reports);
        pthread_mutex_init(&console->report_mutex, NULL);
        pthread_mutex_init(&console->agent_mutex, NULL);
        pthread_cond_init(&console->agent_cond, NULL);

        console->process = JS_UNDEFINED;
    }
#endif

    return NJS_OK;
}


static njs_int_t
njs_function_bind(njs_vm_t *vm, const njs_str_t *name,
    njs_function_native_t native, njs_bool_t ctor)
{
    njs_function_t      *f;
    njs_opaque_value_t   value;

    f = njs_vm_function_alloc(vm, native, 1, ctor);
    if (f == NULL) {
        return NJS_ERROR;
    }

    njs_value_function_set(njs_value_arg(&value), f);

    return njs_vm_bind(vm, name, njs_value_arg(&value), 1);
}


static njs_int_t
njs_externals_init(njs_vm_t *vm)
{
    njs_int_t           ret, proto_id;
    njs_console_t       *console;
    njs_opaque_value_t  value, method;

    static const njs_str_t  console_name = njs_str("console");
    static const njs_str_t  dollar_262 = njs_str("$262");
    static const njs_str_t  print_name = njs_str("print");
    static const njs_str_t  console_log = njs_str("console.log");
    static const njs_str_t  set_timeout = njs_str("setTimeout");
    static const njs_str_t  set_immediate = njs_str("setImmediate");
    static const njs_str_t  clear_timeout = njs_str("clearTimeout");

    console = njs_vm_external_ptr(vm);

    njs_console_proto_id = njs_vm_external_prototype(vm, njs_ext_console,
                                         njs_nitems(njs_ext_console));
    if (njs_slow_path(njs_console_proto_id < 0)) {
        njs_stderror("failed to add \"console\" proto\n");
        return NJS_ERROR;
    }

    ret = njs_vm_external_create(vm, njs_value_arg(&value),
                                 njs_console_proto_id, console, 0);
    if (njs_slow_path(ret != NJS_OK)) {
        return NJS_ERROR;
    }

    ret = njs_vm_bind(vm, &console_name, njs_value_arg(&value), 0);
    if (njs_slow_path(ret != NJS_OK)) {
        return NJS_ERROR;
    }

    ret = njs_vm_value(vm, &console_log, njs_value_arg(&method));
    if (njs_slow_path(ret != NJS_OK)) {
        return NJS_ERROR;
    }

    ret = njs_vm_bind(vm, &print_name, njs_value_arg(&method), 0);
    if (njs_slow_path(ret != NJS_OK)) {
        return NJS_ERROR;
    }

    ret = njs_function_bind(vm, &set_timeout, njs_set_timeout, 0);
    if (ret != NJS_OK) {
        return NJS_ERROR;
    }

    ret = njs_function_bind(vm, &set_immediate, njs_set_immediate, 0);
    if (ret != NJS_OK) {
        return NJS_ERROR;
    }

    ret = njs_function_bind(vm, &clear_timeout, njs_clear_timeout, 0);
    if (ret != NJS_OK) {
        return NJS_ERROR;
    }

    proto_id = njs_vm_external_prototype(vm, njs_ext_262,
                                         njs_nitems(njs_ext_262));
    if (njs_slow_path(proto_id < 0)) {
        njs_stderror("failed to add \"$262\" proto\n");
        return NJS_ERROR;
    }

    ret = njs_vm_external_create(vm,  njs_value_arg(&value), proto_id, NULL, 1);
    if (njs_slow_path(ret != NJS_OK)) {
        return NJS_ERROR;
    }

    ret = njs_vm_bind(vm, &dollar_262, njs_value_arg(&value), 1);
    if (njs_slow_path(ret != NJS_OK)) {
        return NJS_ERROR;
    }

    return NJS_OK;
}


static void
njs_rejection_tracker(njs_vm_t *vm, njs_external_ptr_t external,
    njs_bool_t is_handled, njs_value_t *promise, njs_value_t *reason)
{
    void                    *promise_obj;
    uint32_t                i, length;
    njs_console_t           *console;
    njs_rejected_promise_t  *rejected_promise;

    console = external;

    if (is_handled && console->rejected_promises != NULL) {
        rejected_promise = console->rejected_promises->start;
        length = console->rejected_promises->items;

        promise_obj = njs_value_ptr(promise);

        for (i = 0; i < length; i++) {
            if (njs_value_ptr(njs_value_arg(&rejected_promise[i].u.njs.promise))
                == promise_obj)
            {
                njs_arr_remove(console->rejected_promises,
                               &rejected_promise[i]);

                break;
            }
        }

        return;
    }

    if (console->rejected_promises == NULL) {
        console->rejected_promises = njs_arr_create(console->engine->pool, 4,
                                                sizeof(njs_rejected_promise_t));
        if (njs_slow_path(console->rejected_promises == NULL)) {
            return;
        }
    }

    rejected_promise = njs_arr_add(console->rejected_promises);
    if (njs_slow_path(rejected_promise == NULL)) {
        return;
    }

    njs_value_assign(&rejected_promise->u.njs.promise, promise);
    njs_value_assign(&rejected_promise->u.njs.message, reason);
}


static njs_int_t
njs_module_path(const njs_str_t *dir, njs_module_info_t *info)
{
    char        *p;
    size_t      length;
    njs_bool_t  trail;
    char        src[NJS_MAX_PATH + 1];

    trail = 0;
    length = info->name.length;

    if (dir != NULL) {
        length += dir->length;

        if (length == 0 || dir->length == 0) {
            return NJS_DECLINED;
        }

        trail = (dir->start[dir->length - 1] != '/');

        if (trail) {
            length++;
        }
    }

    if (njs_slow_path(length > NJS_MAX_PATH)) {
        return NJS_ERROR;
    }

    p = &src[0];

    if (dir != NULL) {
        p = (char *) njs_cpymem(p, dir->start, dir->length);

        if (trail) {
            *p++ = '/';
        }
    }

    p = (char *) njs_cpymem(p, info->name.start, info->name.length);
    *p = '\0';

    p = realpath(&src[0], &info->path[0]);
    if (p == NULL) {
        return NJS_DECLINED;
    }

    info->fd = open(&info->path[0], O_RDONLY);
    if (info->fd < 0) {
        return NJS_DECLINED;
    }

    info->file.start = (u_char *) &info->path[0];
    info->file.length = njs_strlen(info->file.start);

    return NJS_OK;
}


static njs_int_t
njs_module_lookup(njs_opts_t *opts, const njs_str_t *cwd,
    njs_module_info_t *info)
{
    njs_int_t   ret;
    njs_str_t   *path;
    njs_uint_t  i;

    if (info->name.start[0] == '/') {
        return njs_module_path(NULL, info);
    }

    ret = njs_module_path(cwd, info);

    if (ret != NJS_DECLINED) {
        return ret;
    }

    path = opts->paths;

    for (i = 0; i < opts->n_paths; i++) {
        ret = njs_module_path(&path[i], info);

        if (ret != NJS_DECLINED) {
            return ret;
        }
    }

    return NJS_DECLINED;
}


static njs_int_t
njs_module_read(njs_mp_t *mp, int fd, njs_str_t *text)
{
    ssize_t      n;
    struct stat  sb;

    text->start = NULL;

    if (fstat(fd, &sb) == -1) {
        goto fail;
    }

    if (!S_ISREG(sb.st_mode)) {
        goto fail;
    }

    text->length = sb.st_size;

    text->start = njs_mp_alloc(mp, text->length + 1);
    if (text->start == NULL) {
        goto fail;
    }

    n = read(fd, text->start, sb.st_size);

    if (n < 0 || n != sb.st_size) {
        goto fail;
    }

    text->start[text->length] = '\0';

    return NJS_OK;

fail:

    if (text->start != NULL) {
        njs_mp_free(mp, text->start);
    }

    return NJS_ERROR;
}


static void
njs_file_dirname(const njs_str_t *path, njs_str_t *name)
{
    const u_char  *p, *end;

    if (path->length == 0) {
        goto current_dir;
    }

    p = path->start + path->length - 1;

    /* Stripping basename. */

    while (p >= path->start && *p != '/') { p--; }

    end = p + 1;

    if (end == path->start) {
        goto current_dir;
    }

    /* Stripping trailing slashes. */

    while (p >= path->start && *p == '/') { p--; }

    p++;

    if (p == path->start) {
        p = end;
    }

    name->start = path->start;
    name->length = p - path->start;

    return;

current_dir:

    *name = njs_str_value(".");
}


static njs_int_t
njs_console_set_cwd(njs_console_t *console, njs_str_t *file)
{
    njs_str_t  cwd;

    njs_file_dirname(file, &cwd);

    console->cwd.start = njs_mp_alloc(console->engine->pool, cwd.length);
    if (njs_slow_path(console->cwd.start == NULL)) {
        return NJS_ERROR;
    }

    memcpy(console->cwd.start, cwd.start, cwd.length);
    console->cwd.length = cwd.length;

    return NJS_OK;
}


static njs_mod_t *
njs_module_loader(njs_vm_t *vm, njs_external_ptr_t external, njs_str_t *name)
{
    u_char             *start;
    njs_int_t          ret;
    njs_str_t          text, prev_cwd;
    njs_mod_t          *module;
    njs_opts_t         *opts;
    njs_console_t      *console;
    njs_module_info_t  info;

    opts = external;
    console = njs_vm_external_ptr(vm);

    njs_memzero(&info, sizeof(njs_module_info_t));

    info.name = *name;

    ret = njs_module_lookup(opts, &console->cwd, &info);
    if (njs_slow_path(ret != NJS_OK)) {
        return NULL;
    }

    ret = njs_module_read(console->engine->pool, info.fd, &text);

    (void) close(info.fd);

    if (njs_slow_path(ret != NJS_OK)) {
        njs_vm_internal_error(vm, "while reading \"%V\" module", &info.file);
        return NULL;
    }

    prev_cwd = console->cwd;

    ret = njs_console_set_cwd(console, &info.file);
    if (njs_slow_path(ret != NJS_OK)) {
        njs_vm_internal_error(vm, "while setting cwd for \"%V\" module",
                              &info.file);
        return NULL;
    }

    start = text.start;

    module = njs_vm_compile_module(vm, &info.file, &start,
                                   &text.start[text.length]);

    njs_mp_free(console->engine->pool, console->cwd.start);
    console->cwd = prev_cwd;

    njs_mp_free(console->engine->pool, text.start);

    return module;
}


static njs_int_t
njs_engine_njs_init(njs_engine_t *engine, njs_opts_t *opts)
{
    njs_vm_t      *vm;
    njs_int_t     ret;
    njs_vm_opt_t  vm_options;

    njs_vm_opt_init(&vm_options);

    vm_options.file.start = (u_char *) opts->file;
    vm_options.file.length = njs_strlen(opts->file);

    vm_options.init = 1;
    vm_options.interactive = opts->interactive;
    vm_options.disassemble = opts->disassemble;
    vm_options.backtrace = 1;
    vm_options.quiet = opts->quiet;
    vm_options.sandbox = opts->sandbox;
    vm_options.unsafe = !opts->safe;
    vm_options.module = opts->module;
#ifdef NJS_DEBUG_GENERATOR
    vm_options.generator_debug = opts->generator_debug;
#endif
#ifdef NJS_DEBUG_OPCODE
    vm_options.opcode_debug = opts->opcode_debug;
#endif

    vm_options.addons = njs_console_addon_modules;
    vm_options.external = &njs_console;
    vm_options.argv = opts->argv;
    vm_options.argc = opts->argc;
    vm_options.ast = opts->ast;

    if (opts->stack_size != 0) {
        vm_options.max_stack_size = opts->stack_size;
    }

    vm = njs_vm_create(&vm_options);
    if (vm == NULL) {
        njs_stderror("failed to create vm\n");
        return NJS_ERROR;
    }

    if (opts->unhandled_rejection) {
        njs_vm_set_rejection_tracker(vm, njs_rejection_tracker,
                                     njs_vm_external_ptr(vm));
    }

    ret = njs_console_set_cwd(njs_vm_external_ptr(vm), &vm_options.file);
    if (njs_slow_path(ret != NJS_OK)) {
        njs_stderror("failed to set cwd\n");
        return NJS_ERROR;
    }

    njs_vm_set_module_loader(vm, njs_module_loader, opts);

    engine->u.njs.vm = vm;

    return NJS_OK;
}


static njs_int_t
njs_engine_njs_destroy(njs_engine_t *engine)
{
    njs_vm_destroy(engine->u.njs.vm);
    njs_mp_destroy(engine->pool);

    return NJS_OK;
}


static njs_int_t
njs_engine_njs_eval(njs_engine_t *engine, njs_str_t *script)
{
     u_char     *start, *end;
     njs_int_t  ret;

     start = script->start;
     end = start + script->length;

     ret = njs_vm_compile(engine->u.njs.vm, &start, end);

     if (ret == NJS_OK && start == end) {
        return njs_vm_start(engine->u.njs.vm,
                           njs_value_arg(&engine->u.njs.value));
     }

     return NJS_ERROR;
}


static njs_int_t
njs_engine_njs_execute_pending_job(njs_engine_t *engine)
{
    return njs_vm_execute_pending_job(engine->u.njs.vm);
}


static njs_int_t
njs_engine_njs_output(njs_engine_t *engine, njs_int_t ret)
{
    njs_vm_t       *vm;
    njs_str_t      out;
    njs_console_t  *console;

    vm = engine->u.njs.vm;
    console = njs_vm_external_ptr(vm);

    if (ret == NJS_OK) {
        if (console->interactive) {
            if (njs_vm_value_dump(vm, &out, njs_value_arg(&engine->u.njs.value),
                                  0, 1)
                != NJS_OK)
            {
                njs_stderror("Shell:failed to get retval from VM\n");
                return NJS_ERROR;
            }

            njs_print(out.start, out.length);
            njs_print("\n", 1);
        }

    } else {
        njs_vm_exception_string(vm, &out);
        njs_stderror("Thrown:\n%V\n", &out);
    }

    return NJS_OK;
}


static njs_arr_t *
njs_object_completions(njs_vm_t *vm, njs_value_t *object, njs_str_t *expression)
{
    u_char              *prefix;
    size_t              len, prefix_len;
    int64_t             k, n, length;
    njs_int_t           ret;
    njs_arr_t           *array;
    njs_str_t           *completion, key;
    njs_value_t         *keys;
    njs_opaque_value_t  *start, retval, prototype;

    prefix = expression->start + expression->length;

    while (prefix > expression->start && *prefix != '.') {
        prefix--;
    }

    if (prefix != expression->start) {
        prefix++;
    }

    prefix_len = prefix - expression->start;
    len = expression->length - prefix_len;

    array = njs_arr_create(njs_vm_memory_pool(vm), 8, sizeof(njs_str_t));
    if (njs_slow_path(array == NULL)) {
        goto fail;
    }

    while (!njs_value_is_null(object)) {
        keys = njs_vm_value_enumerate(vm, object, NJS_ENUM_KEYS
                                      | NJS_ENUM_STRING,
                                      njs_value_arg(&retval));
        if (njs_slow_path(keys == NULL)) {
            goto fail;
        }

        (void) njs_vm_array_length(vm, keys, &length);

        start = (njs_opaque_value_t *) njs_vm_array_start(vm, keys);
        if (start == NULL) {
            goto fail;
        }


        for (n = 0; n < length; n++) {
            ret = njs_vm_value_to_string(vm, &key, njs_value_arg(start));
            if (njs_slow_path(ret != NJS_OK)) {
                goto fail;
            }

            start++;

            if (len > key.length || njs_strncmp(key.start, prefix, len) != 0) {
                continue;
            }

            for (k = 0; k < array->items; k++) {
                completion = njs_arr_item(array, k);

                if ((completion->length - prefix_len - 1) == key.length
                    && njs_strncmp(&completion->start[prefix_len],
                                   key.start, key.length)
                       == 0)
                {
                    break;
                }
            }

            if (k != array->items) {
                continue;
            }

            completion = njs_arr_add(array);
            if (njs_slow_path(completion == NULL)) {
                goto fail;
            }

            completion->length = prefix_len + key.length + 1;
            completion->start = njs_mp_alloc(njs_vm_memory_pool(vm),
                                             completion->length);
            if (njs_slow_path(completion->start == NULL)) {
                goto fail;
            }


            njs_sprintf(completion->start,
                        completion->start + completion->length,
                        "%*s%V%Z", prefix_len, expression->start, &key);
        }

        ret = njs_vm_prototype(vm, object, njs_value_arg(&prototype));
        if (njs_slow_path(ret != NJS_OK)) {
            goto fail;
        }

        object = njs_value_arg(&prototype);
    }

    return array;

fail:

    if (array != NULL) {
        njs_arr_destroy(array);
    }

    return NULL;
}


static njs_arr_t *
njs_engine_njs_complete(njs_engine_t *engine, njs_str_t *expression)
{
    u_char              *p, *start, *end;
    njs_vm_t            *vm;
    njs_int_t           ret;
    njs_bool_t          global;
    njs_opaque_value_t  value, key, retval;

    vm = engine->u.njs.vm;

    p = expression->start;
    end = p + expression->length;

    global = 1;
    (void) njs_vm_global(vm, njs_value_arg(&value));

    while (p < end && *p != '.') { p++; }

    if (p == end) {
        goto done;
    }

    p = expression->start;

    for ( ;; ) {

        start = (*p == '.' && p < end) ? ++p: p;

        if (p == end) {
            break;
        }

        while (p < end && *p != '.') { p++; }

        ret = njs_vm_value_string_create(vm, njs_value_arg(&key), start,
                                         p - start);
        if (njs_slow_path(ret != NJS_OK)) {
            return NULL;
        }

        ret = njs_value_property_val(vm, njs_value_arg(&value),
                                     njs_value_arg(&key),
                                     njs_value_arg(&retval));
        if (njs_slow_path(ret != NJS_OK)) {
            if (ret == NJS_DECLINED && !global) {
                goto done;
            }

            return NULL;
        }

        global = 0;
        njs_value_assign(&value, &retval);
    }

done:

    return njs_object_completions(vm, njs_value_arg(&value), expression);
}


static njs_int_t
njs_engine_njs_process_events(njs_engine_t *engine)
{
    njs_ev_t            *ev;
    njs_vm_t            *vm;
    njs_int_t           ret;
    njs_queue_t         *events;
    njs_console_t       *console;
    njs_queue_link_t    *link;
    njs_opaque_value_t  retval;

    vm = engine->u.njs.vm;
    console = njs_vm_external_ptr(vm);
    events = &console->posted_events;

    for ( ;; ) {
        link = njs_queue_first(events);

        if (link == njs_queue_tail(events)) {
            break;
        }

        ev = njs_queue_link_data(link, njs_ev_t, link);

        njs_queue_remove(&ev->link);
        njs_rbtree_delete(&console->events, &ev->node);

        ret = njs_vm_invoke(vm, ev->u.njs.function, ev->u.njs.args, ev->nargs,
                            njs_value_arg(&retval));
        if (ret == NJS_ERROR) {
            njs_engine_njs_output(engine, ret);

            if (!console->interactive) {
                return NJS_ERROR;
            }
        }
    }

    if (!njs_rbtree_is_empty(&console->events)) {
        return NJS_AGAIN;
    }

    return njs_vm_pending(vm) ? NJS_AGAIN: NJS_OK;
}


static njs_int_t
njs_engine_njs_unhandled_rejection(njs_engine_t *engine)
{
    njs_vm_t                *vm;
    njs_int_t               ret;
    njs_str_t               message;
    njs_console_t           *console;
    njs_rejected_promise_t  *rejected_promise;

    vm = engine->u.njs.vm;
    console = njs_vm_external_ptr(vm);

    if (console->rejected_promises == NULL
        || console->rejected_promises->items == 0)
    {
        return 0;
    }

    rejected_promise = console->rejected_promises->start;

    ret = njs_vm_value_to_string(vm, &message,
                               njs_value_arg(&rejected_promise->u.njs.message));
    if (njs_slow_path(ret != NJS_OK)) {
        return -1;
    }

    njs_vm_error(vm, "unhandled promise rejection: %V", &message);

    njs_arr_destroy(console->rejected_promises);
    console->rejected_promises = NULL;

    return 1;
}

#ifdef NJS_HAVE_QUICKJS

static JSValue
njs_qjs_console_log(JSContext *ctx, JSValueConst this_val, int argc,
    JSValueConst *argv, int magic)
{
    int         i;
    size_t      len;
    const char  *str;

    for (i = 0; i < argc; i++) {
        str = JS_ToCStringLen(ctx, &len, argv[i]);
        if (!str) {
            return JS_EXCEPTION;
        }

        njs_console_logger(magic, (const u_char*) str, len);
        JS_FreeCString(ctx, str);
    }

    return JS_UNDEFINED;
}


static JSValue
njs_qjs_console_time(JSContext *ctx, JSValueConst this_val, int argc,
    JSValueConst *argv)
{
    njs_str_t      name;
    const char     *str;
    njs_console_t  *console;

    static const njs_str_t  default_label = njs_str("default");

    console = JS_GetRuntimeOpaque(JS_GetRuntime(ctx));

    name = default_label;

    if (argc > 0 && !JS_IsUndefined(argv[0])) {
        str = JS_ToCStringLen(ctx, &name.length, argv[0]);
        if (str == NULL) {
            return JS_EXCEPTION;
        }

        name.start = njs_mp_alloc(console->engine->pool, name.length);
        if (njs_slow_path(name.start == NULL)) {
            JS_ThrowOutOfMemory(ctx);
            return JS_EXCEPTION;
        }

        (void) memcpy(name.start, str, name.length);

        JS_FreeCString(ctx, str);
    }

    if (njs_console_time(console, &name) != NJS_OK) {
        return JS_EXCEPTION;
    }

    return JS_UNDEFINED;
}


static JSValue
njs_qjs_console_time_end(JSContext *ctx, JSValueConst this_val, int argc,
    JSValueConst *argv)
{
    uint64_t       ns;
    njs_str_t      name;
    const char     *str;
    njs_console_t  *console;

    static const njs_str_t  default_label = njs_str("default");

    ns = njs_time();

    console = JS_GetRuntimeOpaque(JS_GetRuntime(ctx));

    name = default_label;

    if (argc > 0 && !JS_IsUndefined(argv[0])) {
        str = JS_ToCStringLen(ctx, &name.length, argv[0]);
        if (str == NULL) {
            return JS_EXCEPTION;
        }

        name.start = njs_mp_alloc(console->engine->pool, name.length);
        if (njs_slow_path(name.start == NULL)) {
            JS_ThrowOutOfMemory(ctx);
            return JS_EXCEPTION;
        }

        (void) memcpy(name.start, str, name.length);

        JS_FreeCString(ctx, str);
    }

    njs_console_time_end(console, &name, ns);

    return JS_UNDEFINED;
}


static JSValue
njs_qjs_set_timer(JSContext *ctx, JSValueConst this_val, int argc,
    JSValueConst *argv, int immediate)
{
    int            n;
    int64_t        delay;
    njs_ev_t       *ev;
    njs_uint_t     i;
    njs_console_t  *console;

    if (njs_slow_path(argc < 1)) {
        JS_ThrowTypeError(ctx, "too few arguments");
        return JS_EXCEPTION;
    }

    if (njs_slow_path(!JS_IsFunction(ctx, argv[0]))) {
        JS_ThrowTypeError(ctx, "first arg must be a function");
        return JS_EXCEPTION;
    }

    delay = 0;

    if (!immediate && argc >= 2 && JS_IsNumber(argv[1])) {
        JS_ToInt64(ctx, &delay, argv[1]);
    }

    if (delay != 0) {
        JS_ThrowInternalError(ctx, "njs_set_timer(): async timers unsupported");
        return JS_EXCEPTION;
    }

    console = JS_GetRuntimeOpaque(JS_GetRuntime(ctx));

    n = immediate ? 1 : 2;
    argc = (argc >= n) ? argc - n : 0;

    ev = njs_mp_alloc(console->engine->pool,
                      sizeof(njs_ev_t) + sizeof(njs_opaque_value_t) * argc);
    if (njs_slow_path(ev == NULL)) {
        JS_ThrowOutOfMemory(ctx);
        return JS_EXCEPTION;
    }

    ev->u.qjs.function = JS_DupValue(ctx, argv[0]);
    ev->u.qjs.args = (JSValue *) &ev[1];
    ev->nargs = (njs_uint_t) argc;
    ev->id = console->event_id++;

    if (ev->nargs != 0) {
        for (i = 0; i < ev->nargs; i++) {
            ev->u.qjs.args[i] = JS_DupValue(ctx, argv[i + n]);
        }
    }

    njs_rbtree_insert(&console->events, &ev->node);

    njs_queue_insert_tail(&console->posted_events, &ev->link);

    return JS_NewUint32(ctx, ev->id);
}


static void
njs_qjs_destroy_event(JSContext *ctx, njs_console_t *console, njs_ev_t *ev)
{
    njs_uint_t  i;

    JS_FreeValue(ctx, ev->u.qjs.function);

    if (ev->nargs != 0) {
        for (i = 0; i < ev->nargs; i++) {
            JS_FreeValue(ctx, ev->u.qjs.args[i]);
        }
    }

    njs_mp_free(console->engine->pool, ev);
}


static JSValue
njs_qjs_clear_timeout(JSContext *ctx, JSValueConst this_val, int argc,
    JSValueConst *argv)
{
    njs_ev_t           ev_lookup, *ev;
    njs_console_t      *console;
    njs_rbtree_node_t  *rb;

    if (argc < 1 || !JS_IsNumber(argv[0])) {
        return JS_UNDEFINED;
    }

    console = JS_GetRuntimeOpaque(JS_GetRuntime(ctx));

    if (JS_ToUint32(ctx, &ev_lookup.id, argv[0])) {
        return JS_EXCEPTION;
    }

    rb = njs_rbtree_find(&console->events, &ev_lookup.node);
    if (njs_slow_path(rb == NULL)) {
        JS_ThrowTypeError(ctx, "failed to find timer");
        return JS_EXCEPTION;
    }

    ev = (njs_ev_t *) rb;
    njs_queue_remove(&ev->link);
    njs_rbtree_delete(&console->events, (njs_rbtree_part_t *) rb);

    njs_qjs_destroy_event(ctx, console, ev);

    return JS_UNDEFINED;
}


static JSValue
njs_qjs_process_getter(JSContext *ctx, JSValueConst this_val)
{
    JSValue         obj;
    njs_console_t  *console;

    console = JS_GetRuntimeOpaque(JS_GetRuntime(ctx));

    if (!JS_IsUndefined(console->process)) {
        return JS_DupValue(ctx, console->process);
    }

    obj = qjs_process_object(ctx, console->argc, (const char **) console->argv);
    if (JS_IsException(obj)) {
        return JS_EXCEPTION;
    }

    console->process = JS_DupValue(ctx, obj);

    return obj;
}


static njs_int_t njs_qjs_global_init(JSContext *ctx, JSValue global_obj);
static void njs_qjs_dump_error(JSContext *ctx);


static void
njs_qjs_dump_obj(JSContext *ctx, FILE *f, JSValueConst val, const char *prefix,
    const char *quote)
{
    njs_bool_t  is_str;
    const char  *str;

    is_str = JS_IsString(val);

    str = JS_ToCString(ctx, val);
    if (str) {
        fprintf(f, "%s%s%s%s\n", prefix, is_str ? quote : "",
                str, is_str ? quote : "");
        JS_FreeCString(ctx, str);

    } else {
        njs_qjs_dump_error(ctx);
    }
}


static void
njs_qjs_dump_error2(JSContext *ctx, JSValueConst exception)
{
    _Bool    is_error;
    JSValue  val;

    is_error = JS_IsError(ctx, exception);

    njs_qjs_dump_obj(ctx, stderr, exception, "Thrown:\n", "");

    if (is_error) {
        val = JS_GetPropertyStr(ctx, exception, "stack");
        if (!JS_IsUndefined(val)) {
            njs_qjs_dump_obj(ctx, stderr, val, "", "");
        }

        JS_FreeValue(ctx, val);
    }
}


static void
njs_qjs_dump_error(JSContext *ctx)
{
    JSValue  exception;

    exception = JS_GetException(ctx);
    njs_qjs_dump_error2(ctx, exception);
    JS_FreeValue(ctx, exception);
}


static void *
njs_qjs_agent(void *arg)
{
    int            ret;
    JSValue        ret_val, global_obj;
    JSRuntime      *rt;
    JSContext      *ctx, *ctx1;
    njs_console_t  *console;
    JSValue        args[2];

    njs_262agent_t *agent = arg;
    console = agent->console;

    rt = JS_NewRuntime();
    if (rt == NULL) {
        njs_stderror("JS_NewRuntime failure\n");
        exit(1);
    }

    ctx = JS_NewContext(rt);
    if (ctx == NULL) {
        JS_FreeRuntime(rt);
        njs_stderror("JS_NewContext failure\n");
        exit(1);
    }

    JS_SetContextOpaque(ctx, agent);
    JS_SetRuntimeInfo(rt, "agent");
    JS_SetCanBlock(rt, 1);

    global_obj = JS_GetGlobalObject(ctx);

    ret = njs_qjs_global_init(ctx, global_obj);
    if (ret == -1) {
        JS_FreeContext(ctx);
        JS_FreeRuntime(rt);
        njs_stderror("njs_qjs_global_init failure\n");
        exit(1);
    }

    JS_FreeValue(ctx, global_obj);

    ret_val = JS_Eval(ctx, agent->script, strlen(agent->script),
                      "<evalScript>", JS_EVAL_TYPE_GLOBAL);

    free(agent->script);
    agent->script = NULL;

    if (JS_IsException(ret_val)) {
        njs_qjs_dump_error(ctx);
    }

    JS_FreeValue(ctx, ret_val);

    for (;;) {
        ret = JS_ExecutePendingJob(JS_GetRuntime(ctx), &ctx1);
        if (ret < 0) {
            njs_qjs_dump_error(ctx);
            break;

        } else if (ret == 0) {
            if (JS_IsUndefined(agent->broadcast_func)) {
                break;

            } else {
                pthread_mutex_lock(&console->agent_mutex);

                while (!agent->broadcast_pending) {
                    pthread_cond_wait(&console->agent_cond,
                                      &console->agent_mutex);
                }

                agent->broadcast_pending = 0;
                pthread_cond_signal(&console->agent_cond);
                pthread_mutex_unlock(&console->agent_mutex);

                args[0] = JS_NewArrayBuffer(ctx, agent->broadcast_sab_buf,
                                            agent->broadcast_sab_size,
                                            NULL, NULL, 1);
                args[1] = JS_NewInt32(ctx, agent->broadcast_val);

                ret_val = JS_Call(ctx, agent->broadcast_func, JS_UNDEFINED,
                                  2, (JSValueConst *)args);

                JS_FreeValue(ctx, args[0]);
                JS_FreeValue(ctx, args[1]);

                if (JS_IsException(ret_val)) {
                    njs_qjs_dump_error(ctx);
                }

                JS_FreeValue(ctx, ret_val);
                JS_FreeValue(ctx, agent->broadcast_func);
                agent->broadcast_func = JS_UNDEFINED;
            }
        }
    }

    JS_FreeValue(ctx, agent->broadcast_func);

    JS_FreeContext(ctx);
    JS_FreeRuntime(rt);

    return NULL;
}


static JSValue
njs_qjs_agent_start(JSContext *ctx, JSValue this_val, int argc, JSValue *argv)
{
    const char      *script;
    njs_console_t   *console;
    njs_262agent_t  *agent;

    if (JS_GetContextOpaque(ctx) != NULL) {
        return JS_ThrowTypeError(ctx, "cannot be called inside an agent");
    }

    script = JS_ToCString(ctx, argv[0]);
    if (script == NULL) {
        return JS_EXCEPTION;
    }

    console = JS_GetRuntimeOpaque(JS_GetRuntime(ctx));

    agent = malloc(sizeof(*agent));
    if (agent == NULL) {
        return JS_ThrowOutOfMemory(ctx);
    }

    njs_memzero(agent, sizeof(*agent));

    agent->broadcast_func = JS_UNDEFINED;
    agent->broadcast_sab = JS_UNDEFINED;
    agent->script = strdup(script);
    if (agent->script == NULL) {
        return JS_ThrowOutOfMemory(ctx);
    }

    JS_FreeCString(ctx, script);

    agent->console = console;
    njs_queue_insert_tail(&console->agents, &agent->link);

    pthread_create(&agent->tid, NULL, njs_qjs_agent, agent);

    return JS_UNDEFINED;
}


static JSValue
njs_qjsr_agent_get_report(JSContext *ctx, JSValue this_val, int argc,
    JSValue *argv)
{
    JSValue             ret;
    njs_console_t       *console;
    njs_queue_link_t    *link;
    njs_agent_report_t  *rep;

    console = JS_GetRuntimeOpaque(JS_GetRuntime(ctx));

    pthread_mutex_lock(&console->report_mutex);

    rep = NULL;

    for ( ;; ) {
        link = njs_queue_first(&console->reports);

        if (link == njs_queue_tail(&console->reports)) {
            break;
        }

        rep = njs_queue_link_data(link, njs_agent_report_t, link);

        njs_queue_remove(&rep->link);
        break;
    }

    pthread_mutex_unlock(&console->report_mutex);

    if (rep != NULL) {
        ret = JS_NewString(ctx, rep->str);
        free(rep->str);
        free(rep);

    } else {
        ret = JS_NULL;
    }

    return ret;
}


static njs_bool_t
njs_qjs_broadcast_pending(njs_console_t *console)
{
    njs_262agent_t    *agent;
    njs_queue_link_t  *link;

    link = njs_queue_first(&console->agents);

    for ( ;; ) {
        if (link == njs_queue_tail(&console->agents)) {
            break;
        }

        agent = njs_queue_link_data(link, njs_262agent_t, link);

        if (agent->broadcast_pending) {
            return 1;
        }

        link = njs_queue_next(link);
    }

    return 0;
}

static JSValue
njs_qjs_agent_broadcast(JSContext *ctx, JSValue this_val, int argc,
    JSValue *argv)
{
    uint8_t           *buf;
    size_t            buf_size;
    int32_t           val;
    njs_console_t     *console;
    njs_262agent_t    *agent;
    njs_queue_link_t  *link;

    JSValueConst sab = argv[0];

    if (JS_GetContextOpaque(ctx) != NULL) {
        return JS_ThrowTypeError(ctx, "cannot be called inside an agent");
    }

    buf = JS_GetArrayBuffer(ctx, &buf_size, sab);
    if (buf == NULL) {
        return JS_EXCEPTION;
    }

    if (JS_ToInt32(ctx, &val, argv[1])) {
        return JS_EXCEPTION;
    }

    console = JS_GetRuntimeOpaque(JS_GetRuntime(ctx));

    pthread_mutex_lock(&console->agent_mutex);

    link = njs_queue_first(&console->agents);

    for ( ;; ) {
        if (link == njs_queue_tail(&console->agents)) {
            break;
        }

        agent = njs_queue_link_data(link, njs_262agent_t, link);

        agent->broadcast_pending = 1;
        agent->broadcast_sab = JS_DupValue(ctx, sab);
        agent->broadcast_sab_buf = buf;
        agent->broadcast_sab_size = buf_size;
        agent->broadcast_val = val;

        link = njs_queue_next(link);
    }

    pthread_cond_broadcast(&console->agent_cond);

    while (njs_qjs_broadcast_pending(console)) {
        pthread_cond_wait(&console->agent_cond, &console->agent_mutex);
    }

    pthread_mutex_unlock(&console->agent_mutex);

    return JS_UNDEFINED;
}


static JSValue
njs_qjs_agent_receive_broadcast(JSContext *ctx, JSValue this_val, int argc,
    JSValue *argv)
{
    njs_262agent_t *agent = JS_GetContextOpaque(ctx);
    if (agent == NULL) {
        return JS_ThrowTypeError(ctx, "must be called inside an agent");
    }

    if (!JS_IsFunction(ctx, argv[0])) {
        return JS_ThrowTypeError(ctx, "expecting function");
    }

    JS_FreeValue(ctx, agent->broadcast_func);
    agent->broadcast_func = JS_DupValue(ctx, argv[0]);

    return JS_UNDEFINED;
}


static JSValue
njs_qjs_agent_report(JSContext *ctx, JSValue this_val, int argc, JSValue *argv)
{
    const char          *str;
    njs_console_t       *console;
    njs_262agent_t      *agent;
    njs_agent_report_t  *rep;

    str = JS_ToCString(ctx, argv[0]);
    if (str == NULL) {
        return JS_EXCEPTION;
    }

    rep = malloc(sizeof(*rep));
    rep->str = strdup(str);
    JS_FreeCString(ctx, str);

    agent = JS_GetContextOpaque(ctx);
    console = agent->console;

    pthread_mutex_lock(&console->report_mutex);
    njs_queue_insert_tail(&console->reports, &rep->link);
    pthread_mutex_unlock(&console->report_mutex);

    return JS_UNDEFINED;
}


static JSValue
njs_qjs_agent_leaving(JSContext *ctx, JSValue this_val, int argc, JSValue *argv)
{
    njs_262agent_t *agent = JS_GetContextOpaque(ctx);
    if (agent == NULL) {
        return JS_ThrowTypeError(ctx, "must be called inside an agent");
    }

    return JS_UNDEFINED;
}


static JSValue
njs_qjs_agent_sleep(JSContext *ctx, JSValue this_val, int argc, JSValue *argv)
{
    uint32_t duration;

    if (JS_ToUint32(ctx, &duration, argv[0])) {
        return JS_EXCEPTION;
    }

    usleep(duration * 1000);

    return JS_UNDEFINED;
}


static JSValue
njs_qjs_agent_monotonic_now(JSContext *ctx, JSValue this_val, int argc,
    JSValue *argv)
{
    return JS_NewInt64(ctx, njs_time() / 1000000);
}


static const JSCFunctionListEntry njs_qjs_agent_proto[] = {
    JS_CFUNC_DEF("start", 1, njs_qjs_agent_start),
    JS_CFUNC_DEF("getReport", 0, njs_qjsr_agent_get_report),
    JS_CFUNC_DEF("broadcast", 2, njs_qjs_agent_broadcast),
    JS_CFUNC_DEF("report", 1, njs_qjs_agent_report),
    JS_CFUNC_DEF("leaving", 0, njs_qjs_agent_leaving),
    JS_CFUNC_DEF("receiveBroadcast", 1, njs_qjs_agent_receive_broadcast),
    JS_CFUNC_DEF("sleep", 1, njs_qjs_agent_sleep),
    JS_CFUNC_DEF("monotonicNow", 0, njs_qjs_agent_monotonic_now),
};


static JSValue
njs_qjs_new_agent(JSContext *ctx)
{
    JSValue  agent;

    agent = JS_NewObject(ctx);
    if (JS_IsException(agent)) {
        return JS_EXCEPTION;
    }

    JS_SetPropertyFunctionList(ctx, agent, njs_qjs_agent_proto,
                               njs_nitems(njs_qjs_agent_proto));
    return agent;
}


static JSValue
njs_qjs_detach_array_buffer(JSContext *ctx, JSValueConst this_val, int argc,
    JSValueConst *argv)
{
    JS_DetachArrayBuffer(ctx, argv[0]);

    return JS_NULL;
}

static JSValue
njs_qjs_eval_script(JSContext *ctx, JSValue this_val, int argc, JSValue *argv)
{
    size_t     len;
    JSValue    ret;
    const char *str;

    str = JS_ToCStringLen(ctx, &len, argv[0]);
    if (str == NULL) {
        return JS_EXCEPTION;
    }

    ret = JS_Eval(ctx, str, len, "<evalScript>", JS_EVAL_TYPE_GLOBAL);

    JS_FreeCString(ctx, str);

    return ret;
}


static JSValue
njs_qjs_create_realm(JSContext *ctx, JSValue this_val, int argc, JSValue *argv)
{
    JSValue    ret_val, global_obj;
    njs_int_t  ret;
    JSContext  *ctx1;

    ctx1 = JS_NewContext(JS_GetRuntime(ctx));
    if (ctx1 == NULL) {
        return JS_ThrowOutOfMemory(ctx);
    }

    global_obj = JS_GetGlobalObject(ctx1);

    ret = njs_qjs_global_init(ctx1, global_obj);
    if (ret == -1) {
        JS_FreeContext(ctx1);
        return JS_EXCEPTION;
    }

    ret_val = JS_GetPropertyStr(ctx1, global_obj, "$262");

    JS_FreeValue(ctx1, global_obj);
    JS_FreeContext(ctx1);

    return ret_val;
}


static JSValue
njs_qjs_is_HTMLDDA(JSContext *ctx, JSValue this_val, int argc, JSValue *argv)
{
    return JS_NULL;
}


static const JSCFunctionListEntry njs_qjs_262_proto[] = {
    JS_CFUNC_DEF("detachArrayBuffer", 1, njs_qjs_detach_array_buffer),
    JS_CFUNC_DEF("evalScript", 1, njs_qjs_eval_script),
    JS_CFUNC_DEF("codePointRange", 2, js_string_codePointRange),
    JS_CFUNC_DEF("createRealm", 0, njs_qjs_create_realm),
};


static JSValue
njs_qjs_new_262(JSContext *ctx, JSValueConst this_val)
{
    JSValue  obj, obj262, global_obj;

    obj262 = JS_NewObject(ctx);
    if (JS_IsException(obj262)) {
        return JS_EXCEPTION;
    }

    JS_SetPropertyFunctionList(ctx, obj262, njs_qjs_262_proto,
                               njs_nitems(njs_qjs_262_proto));

    global_obj = JS_GetGlobalObject(ctx);
    JS_SetPropertyStr(ctx, obj262, "global", JS_DupValue(ctx, global_obj));
    JS_FreeValue(ctx, global_obj);

    obj = JS_NewCFunction(ctx, njs_qjs_is_HTMLDDA, "IsHTMLDDA", 0);
    JS_SetIsHTMLDDA(ctx, obj);
    JS_SetPropertyStr(ctx, obj262, "IsHTMLDDA", obj);

    JS_SetPropertyStr(ctx, obj262, "agent", njs_qjs_new_agent(ctx));

    return obj262;
}


static const JSCFunctionListEntry njs_qjs_global_proto[] = {
    JS_CFUNC_DEF("clearTimeout", 1, njs_qjs_clear_timeout),
    JS_CFUNC_MAGIC_DEF("print", 0, njs_qjs_console_log, NJS_LOG_INFO),
    JS_CGETSET_DEF("process", njs_qjs_process_getter, NULL),
    JS_CFUNC_MAGIC_DEF("setImmediate", 0, njs_qjs_set_timer, 1),
    JS_CFUNC_MAGIC_DEF("setTimeout", 0, njs_qjs_set_timer, 0),
};


static const JSCFunctionListEntry njs_qjs_console_proto[] = {
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "Console",
                       JS_PROP_CONFIGURABLE),
    JS_CFUNC_MAGIC_DEF("error", 0, njs_qjs_console_log, NJS_LOG_ERROR),
    JS_CFUNC_MAGIC_DEF("info", 0, njs_qjs_console_log, NJS_LOG_INFO),
    JS_CFUNC_MAGIC_DEF("log", 0, njs_qjs_console_log, NJS_LOG_INFO),
    JS_CFUNC_DEF("time", 0, njs_qjs_console_time),
    JS_CFUNC_DEF("timeEnd", 0, njs_qjs_console_time_end),
    JS_CFUNC_MAGIC_DEF("warn", 0, njs_qjs_console_log, NJS_LOG_WARN),
};


static njs_int_t
njs_qjs_global_init(JSContext *ctx, JSValue global_obj)
{
    JS_SetPropertyFunctionList(ctx, global_obj, njs_qjs_global_proto,
                               njs_nitems(njs_qjs_global_proto));

    return JS_SetPropertyStr(ctx, global_obj, "$262",
                             njs_qjs_new_262(ctx, global_obj));
}


static void
njs_qjs_rejection_tracker(JSContext *ctx, JSValueConst promise,
    JSValueConst reason, JS_BOOL is_handled, void *opaque)
{
    void                    *promise_obj;
    uint32_t                i, length;
    njs_console_t           *console;
    njs_rejected_promise_t  *rejected_promise;

    console = opaque;

    if (is_handled && console->rejected_promises != NULL) {
        rejected_promise = console->rejected_promises->start;
        length = console->rejected_promises->items;

        promise_obj = JS_VALUE_GET_PTR(promise);

        for (i = 0; i < length; i++) {
            if (JS_VALUE_GET_PTR(rejected_promise[i].u.qjs.promise)
                == promise_obj)
            {
                JS_FreeValue(ctx, rejected_promise[i].u.qjs.promise);
                JS_FreeValue(ctx, rejected_promise[i].u.qjs.message);
                njs_arr_remove(console->rejected_promises,
                               &rejected_promise[i]);

                break;
            }
        }

        return;
    }

    if (console->rejected_promises == NULL) {
        console->rejected_promises = njs_arr_create(console->engine->pool, 4,
                                                sizeof(njs_rejected_promise_t));
        if (njs_slow_path(console->rejected_promises == NULL)) {
            return;
        }
    }

    rejected_promise = njs_arr_add(console->rejected_promises);
    if (njs_slow_path(rejected_promise == NULL)) {
        return;
    }

    rejected_promise->u.qjs.promise = JS_DupValue(ctx, promise);
    rejected_promise->u.qjs.message = JS_DupValue(ctx, reason);
}


static JSModuleDef *
njs_qjs_module_loader(JSContext *ctx, const char *module_name, void *opaque)
{
    JSValue            func_val;
    njs_int_t          ret;
    njs_str_t          text, prev_cwd;
    njs_opts_t         *opts;
    njs_console_t      *console;
    JSModuleDef        *m;
    njs_module_info_t  info;

    opts = opaque;
    console = JS_GetRuntimeOpaque(JS_GetRuntime(ctx));

    njs_memzero(&info, sizeof(njs_module_info_t));

    info.name.start = (u_char *) module_name;
    info.name.length = njs_strlen(module_name);

    ret = njs_module_lookup(opts, &console->cwd, &info);
    if (njs_slow_path(ret != NJS_OK)) {
        JS_ThrowReferenceError(ctx, "could not load module filename '%s'",
                               module_name);
        return NULL;
    }

    ret = njs_module_read(console->engine->pool, info.fd, &text);

    (void) close(info.fd);

    if (njs_slow_path(ret != NJS_OK)) {
        JS_ThrowInternalError(ctx, "while reading \"%.*s\" module",
                              (int) info.file.length, info.file.start);
        return NULL;
    }

    prev_cwd = console->cwd;

    ret = njs_console_set_cwd(console, &info.file);
    if (njs_slow_path(ret != NJS_OK)) {
        JS_ThrowInternalError(ctx, "while setting cwd for \"%.*s\" module",
                              (int) info.file.length, info.file.start);
        return NULL;
    }

    func_val = JS_Eval(ctx, (char *) text.start, text.length, module_name,
                       JS_EVAL_TYPE_MODULE | JS_EVAL_FLAG_COMPILE_ONLY);

    njs_mp_free(console->engine->pool, console->cwd.start);
    console->cwd = prev_cwd;

    njs_mp_free(console->engine->pool, text.start);

    if (JS_IsException(func_val)) {
        return NULL;
    }

    m = JS_VALUE_GET_PTR(func_val);
    JS_FreeValue(ctx, func_val);

    return m;
}


static njs_int_t
njs_engine_qjs_init(njs_engine_t *engine, njs_opts_t *opts)
{
    JSValue    global_obj, obj;
    njs_int_t  ret;
    JSContext  *ctx;

    engine->u.qjs.rt = JS_NewRuntime();
    if (engine->u.qjs.rt == NULL) {
        njs_stderror("JS_NewRuntime() failed\n");
        return NJS_ERROR;
    }

    engine->u.qjs.ctx = qjs_new_context(engine->u.qjs.rt, NULL);
    if (engine->u.qjs.ctx == NULL) {
        njs_stderror("JS_NewContext() failed\n");
        return NJS_ERROR;
    }

    JS_SetRuntimeOpaque(engine->u.qjs.rt, &njs_console);

    engine->u.qjs.value = JS_UNDEFINED;

    ctx = engine->u.qjs.ctx;

    global_obj = JS_GetGlobalObject(ctx);

    ret = njs_qjs_global_init(ctx, global_obj);
    if (ret == -1) {
        njs_stderror("njs_qjs_global_init() failed\n");
        ret = NJS_ERROR;
        goto done;
    }

    obj = JS_NewObject(ctx);
    if (JS_IsException(obj)) {
        njs_stderror("JS_NewObject() failed\n");
        ret = NJS_ERROR;
        goto done;
    }

    JS_SetOpaque(obj, &njs_console);

    JS_SetPropertyFunctionList(ctx, obj, njs_qjs_console_proto,
                               njs_nitems(njs_qjs_console_proto));

    ret = JS_SetPropertyStr(ctx, global_obj, "console", obj);
    if (ret == -1) {
        njs_stderror("JS_SetPropertyStr() failed\n");
        ret = NJS_ERROR;
        goto done;
    }

    if (opts->unhandled_rejection) {
        JS_SetHostPromiseRejectionTracker(engine->u.qjs.rt,
                                         njs_qjs_rejection_tracker,
                                         JS_GetRuntimeOpaque(engine->u.qjs.rt));
    }

    JS_SetModuleLoaderFunc(engine->u.qjs.rt, NULL, njs_qjs_module_loader, opts);

    JS_SetCanBlock(engine->u.qjs.rt, opts->can_block);

    ret = NJS_OK;

done:

    JS_FreeValue(ctx, global_obj);

    return ret;
}


static njs_int_t
njs_engine_qjs_destroy(njs_engine_t *engine)
{
    uint32_t                i;
    njs_ev_t                *ev;
    njs_queue_t             *events;
    njs_console_t           *console;
    njs_262agent_t          *agent;
    njs_queue_link_t        *link;
    njs_rejected_promise_t  *rejected_promise;

    console = JS_GetRuntimeOpaque(engine->u.qjs.rt);

    if (console->rejected_promises != NULL) {
        rejected_promise = console->rejected_promises->start;

        for (i = 0; i < console->rejected_promises->items; i++) {
            JS_FreeValue(engine->u.qjs.ctx, rejected_promise[i].u.qjs.promise);
            JS_FreeValue(engine->u.qjs.ctx, rejected_promise[i].u.qjs.message);
        }
    }

    events = &console->posted_events;

    for ( ;; ) {
        link = njs_queue_first(events);

        if (link == njs_queue_tail(events)) {
            break;
        }

        ev = njs_queue_link_data(link, njs_ev_t, link);

        njs_queue_remove(&ev->link);
        njs_rbtree_delete(&console->events, &ev->node);

        njs_qjs_destroy_event(engine->u.qjs.ctx, console, ev);
    }

    for ( ;; ) {
        link = njs_queue_first(&console->agents);

        if (link == njs_queue_tail(&console->agents)) {
            break;
        }

        agent = njs_queue_link_data(link, njs_262agent_t, link);

        njs_queue_remove(&agent->link);

        pthread_join(agent->tid, NULL);
        JS_FreeValue(engine->u.qjs.ctx, agent->broadcast_sab);
        free(agent->script);
        free(agent);
    }

    JS_FreeValue(engine->u.qjs.ctx, console->process);
    JS_FreeValue(engine->u.qjs.ctx, engine->u.qjs.value);
    JS_FreeContext(engine->u.qjs.ctx);
    JS_FreeRuntime(engine->u.qjs.rt);

    return NJS_OK;
}


static njs_int_t
njs_engine_qjs_eval(njs_engine_t *engine, njs_str_t *script)
{
    int            flags;
    JSValue        code;
    njs_console_t  *console;

    flags = JS_EVAL_TYPE_GLOBAL | JS_EVAL_FLAG_STRICT
            | JS_EVAL_FLAG_COMPILE_ONLY;

    console = JS_GetRuntimeOpaque(engine->u.qjs.rt);

    if (console->module) {
        flags |= JS_EVAL_TYPE_MODULE;
    }

    code = JS_Eval(engine->u.qjs.ctx, (char *) script->start,
                   script->length, "<input>", flags);

    if (JS_IsException(code)) {
        return NJS_ERROR;
    }

    JS_FreeValue(engine->u.qjs.ctx, engine->u.qjs.value);

    engine->u.qjs.value = JS_EvalFunction(engine->u.qjs.ctx, code);

    return (JS_IsException(engine->u.qjs.value)) ? NJS_ERROR : NJS_OK;
}


static njs_int_t
njs_engine_qjs_execute_pending_job(njs_engine_t *engine)
{
    JSContext  *ctx1;

    return JS_ExecutePendingJob(engine->u.qjs.rt, &ctx1);
}


static njs_int_t
njs_engine_qjs_unhandled_rejection(njs_engine_t *engine)
{
    size_t                  len;
    uint32_t                i;
    JSContext               *ctx;
    const char              *str;
    njs_console_t           *console;
    njs_rejected_promise_t  *rejected_promise;

    ctx = engine->u.qjs.ctx;
    console = JS_GetRuntimeOpaque(engine->u.qjs.rt);

    if (console->rejected_promises == NULL
        || console->rejected_promises->items == 0)
    {
        return 0;
    }

    rejected_promise = console->rejected_promises->start;

    str = JS_ToCStringLen(ctx, &len, rejected_promise->u.qjs.message);
    if (njs_slow_path(str == NULL)) {
        return -1;
    }

    JS_ThrowTypeError(ctx, "unhandled promise rejection: %.*s", (int) len, str);
    JS_FreeCString(ctx, str);

    for (i = 0; i < console->rejected_promises->items; i++) {
        JS_FreeValue(ctx, rejected_promise[i].u.qjs.promise);
        JS_FreeValue(ctx, rejected_promise[i].u.qjs.message);
    }

    njs_arr_destroy(console->rejected_promises);
    console->rejected_promises = NULL;

    return 1;
}


static njs_int_t
njs_engine_qjs_process_events(njs_engine_t *engine)
{
    JSValue           ret;
    njs_ev_t          *ev;
    JSContext         *ctx;
    njs_queue_t       *events;
    njs_console_t     *console;
    njs_queue_link_t  *link;

    ctx = engine->u.qjs.ctx;
    console = JS_GetRuntimeOpaque(engine->u.qjs.rt);
    events = &console->posted_events;

    for ( ;; ) {
        link = njs_queue_first(events);

        if (link == njs_queue_tail(events)) {
            break;
        }

        ev = njs_queue_link_data(link, njs_ev_t, link);

        njs_queue_remove(&ev->link);
        njs_rbtree_delete(&console->events, &ev->node);

        ret = JS_Call(ctx, ev->u.qjs.function, JS_UNDEFINED, ev->nargs,
                      ev->u.qjs.args);

        njs_qjs_destroy_event(ctx, console, ev);

        if (JS_IsException(ret)) {
            engine->output(engine, NJS_ERROR);

            if (!console->interactive) {
                return NJS_ERROR;
            }
        }

        JS_FreeValue(ctx, ret);
    }

    if (!njs_rbtree_is_empty(&console->events)) {
        return NJS_AGAIN;
    }

    return JS_IsJobPending(engine->u.qjs.rt) ? NJS_AGAIN: NJS_OK;
}


static njs_int_t
njs_engine_qjs_output(njs_engine_t *engine, njs_int_t ret)
{
    JSContext      *ctx;
    njs_console_t  *console;

    ctx = engine->u.qjs.ctx;
    console = JS_GetRuntimeOpaque(engine->u.qjs.rt);

    if (ret == NJS_OK) {
        if (console->interactive) {
            njs_qjs_dump_obj(ctx, stdout, engine->u.qjs.value, "", "\'");
        }

    } else {
        njs_qjs_dump_error(ctx);
    }

    return NJS_OK;
}


static njs_arr_t *
njs_qjs_object_completions(njs_engine_t *engine, JSContext *ctx,
    JSValueConst object, njs_str_t *expression)
{
    u_char          *prefix;
    size_t          len, prefix_len;
    JSValue         prototype;
    uint32_t        k, n, length;
    njs_int_t       ret;
    njs_arr_t       *array;
    njs_str_t       *completion, key;
    JSPropertyEnum  *ptab;

    prefix = expression->start + expression->length;

    while (prefix > expression->start && *prefix != '.') {
        prefix--;
    }

    if (prefix != expression->start) {
        prefix++;
    }

    ptab = NULL;
    key.start = NULL;
    prefix_len = prefix - expression->start;
    len = expression->length - prefix_len;

    array = njs_arr_create(engine->pool, 8, sizeof(njs_str_t));
    if (njs_slow_path(array == NULL)) {
        goto fail;
    }

    while (!JS_IsNull(object)) {
        ret = JS_GetOwnPropertyNames(ctx, &ptab, &length, object,
                                     JS_GPN_STRING_MASK);
        if (ret < 0) {
            goto fail;
        }

        for (n = 0; n < length; n++) {
            key.start = (u_char *) JS_AtomToCString(ctx, ptab[n].atom);
            if (njs_slow_path(key.start == NULL)) {
                goto fail;
            }

            key.length = njs_strlen(key.start);

            if (len > key.length || njs_strncmp(key.start, prefix, len) != 0) {
                goto next;
            }

            for (k = 0; k < array->items; k++) {
                completion = njs_arr_item(array, k);

                if ((completion->length - prefix_len - 1) == key.length
                    && njs_strncmp(&completion->start[prefix_len],
                                   key.start, key.length)
                       == 0)
                {
                    goto next;
                }
            }

            completion = njs_arr_add(array);
            if (njs_slow_path(completion == NULL)) {
                goto fail;
            }

            completion->length = prefix_len + key.length + 1;
            completion->start = njs_mp_alloc(engine->pool, completion->length);
            if (njs_slow_path(completion->start == NULL)) {
                goto fail;
            }

            njs_sprintf(completion->start,
                        completion->start + completion->length,
                        "%*s%V%Z", prefix_len, expression->start, &key);

next:

            JS_FreeCString(ctx, (const char *) key.start);
        }

        qjs_free_prop_enum(ctx, ptab, length);

        prototype = JS_GetPrototype(ctx, object);
        if (JS_IsException(prototype)) {
            goto fail;
        }

        JS_FreeValue(ctx, object);
        object = prototype;
    }

    return array;

fail:

    if (array != NULL) {
        njs_arr_destroy(array);
    }

    if (key.start != NULL) {
        JS_FreeCString(ctx, (const char *) key.start);
    }

    if (ptab != NULL) {
        qjs_free_prop_enum(ctx, ptab, length);
    }

    JS_FreeValue(ctx, object);

    return NULL;
}


static njs_arr_t *
njs_engine_qjs_complete(njs_engine_t *engine, njs_str_t *expression)
{
    u_char      *p, *start, *end;
    JSAtom      key;
    JSValue     value, retval;
    njs_arr_t   *arr;
    JSContext   *ctx;
    njs_bool_t  global;

    ctx = engine->u.qjs.ctx;

    p = expression->start;
    end = p + expression->length;

    global = 1;
    value = JS_GetGlobalObject(ctx);

    while (p < end && *p != '.') { p++; }

    if (p == end) {
        goto done;
    }

    p = expression->start;

    for ( ;; ) {

        start = (*p == '.' && p < end) ? ++p: p;

        if (p == end) {
            break;
        }

        while (p < end && *p != '.') { p++; }

        key = JS_NewAtomLen(ctx, (char *) start, p - start);
        if (key == JS_ATOM_NULL) {
            goto fail;
        }

        retval = JS_GetProperty(ctx, value, key);

        JS_FreeAtom(ctx, key);

        if (JS_IsUndefined(retval)) {
            if (global) {
                goto fail;
            }

            goto done;
        }

        if (JS_IsException(retval)) {
            goto fail;
        }

        JS_FreeValue(ctx, value);
        value = retval;
        global = 0;
    }

done:

    arr = njs_qjs_object_completions(engine, ctx, JS_DupValue(ctx, value),
                                     expression);

    JS_FreeValue(ctx, value);

    return arr;

fail:

    JS_FreeValue(ctx, value);

    return NULL;
}

#endif


static njs_engine_t *
njs_create_engine(njs_opts_t *opts)
{
    njs_mp_t      *mp;
    njs_int_t     ret;
    njs_engine_t  *engine;

    mp = njs_mp_fast_create(2 * njs_pagesize(), 128, 512, 16);
    if (njs_slow_path(mp == NULL)) {
        return NULL;
    }

    engine = njs_mp_zalloc(mp, sizeof(njs_engine_t));
    if (njs_slow_path(engine == NULL)) {
        return NULL;
    }

    engine->pool = mp;

    njs_console.engine = engine;

    switch (opts->engine) {
    case NJS_ENGINE_NJS:
        ret = njs_engine_njs_init(engine, opts);
        if (njs_slow_path(ret != NJS_OK)) {
            njs_stderror("njs_engine_njs_init() failed\n");
            return NULL;
        }

        engine->type = NJS_ENGINE_NJS;
        engine->eval = njs_engine_njs_eval;
        engine->execute_pending_job = njs_engine_njs_execute_pending_job;
        engine->unhandled_rejection = njs_engine_njs_unhandled_rejection;
        engine->process_events = njs_engine_njs_process_events;
        engine->destroy = njs_engine_njs_destroy;
        engine->output = njs_engine_njs_output;
        engine->complete = njs_engine_njs_complete;
        break;

#ifdef NJS_HAVE_QUICKJS
    case NJS_ENGINE_QUICKJS:
        ret = njs_engine_qjs_init(engine, opts);
        if (njs_slow_path(ret != NJS_OK)) {
            njs_stderror("njs_engine_qjs_init() failed\n");
            return NULL;
        }

        engine->type = NJS_ENGINE_QUICKJS;
        engine->eval = njs_engine_qjs_eval;
        engine->execute_pending_job = njs_engine_qjs_execute_pending_job;
        engine->unhandled_rejection = njs_engine_qjs_unhandled_rejection;
        engine->process_events = njs_engine_qjs_process_events;
        engine->destroy = njs_engine_qjs_destroy;
        engine->output = njs_engine_qjs_output;
        engine->complete = njs_engine_qjs_complete;
        break;
#endif

    default:
        njs_stderror("unknown engine type\n");
        return NULL;
    }

    return engine;
}


static njs_int_t
njs_read_file(njs_opts_t *opts, njs_str_t *content)
{
    int          fd;
    char         *file;
    u_char       *p, *end, *start;
    size_t       size;
    ssize_t      n;
    njs_int_t    ret;
    struct stat  sb;

    file = opts->file;

    if (file[0] == '-' && file[1] == '\0') {
        fd = STDIN_FILENO;

    } else {
        fd = open(file, O_RDONLY);
        if (fd == -1) {
            njs_stderror("failed to open file: '%s' (%s)\n",
                         file, strerror(errno));
            return NJS_ERROR;
        }
    }

    if (fstat(fd, &sb) == -1) {
        njs_stderror("fstat(%d) failed while reading '%s' (%s)\n",
                     fd, file, strerror(errno));
        ret = NJS_ERROR;
        goto close_fd;
    }

    size = 4096;

    if (S_ISREG(sb.st_mode) && sb.st_size) {
        size = sb.st_size;
    }

    content->length = 0;
    content->start = realloc(NULL, size + 1);
    if (content->start == NULL) {
        njs_stderror("alloc failed while reading '%s'\n", file);
        ret = NJS_ERROR;
        goto close_fd;
    }

    p = content->start;
    end = p + size;

    for ( ;; ) {
        n = read(fd, p, end - p);

        if (n == 0) {
            break;
        }

        if (n < 0) {
            njs_stderror("failed to read file: '%s' (%s)\n",
                      file, strerror(errno));
            ret = NJS_ERROR;
            goto close_fd;
        }

        if (p + n == end) {
            size *= 2;

            start = realloc(content->start, size + 1);
            if (start == NULL) {
                njs_stderror("alloc failed while reading '%s'\n", file);
                ret = NJS_ERROR;
                goto close_fd;
            }

            content->start = start;

            p = content->start + content->length;
            end = content->start + size;
        }

        p += n;
        content->length += n;
    }

    content->start[content->length] = '\0';

    ret = NJS_OK;

close_fd:

    if (fd != STDIN_FILENO) {
        (void) close(fd);
    }

    return ret;
}


static njs_int_t
njs_process_file(njs_opts_t *opts)
{
    u_char        *p;
    njs_int_t     ret;
    njs_str_t     source, script;
    njs_engine_t  *engine;

    engine = NULL;
    source.start = NULL;

    ret = njs_read_file(opts, &source);
    if (ret != NJS_OK) {
        goto done;
    }

    script = source;

    /* shebang */

    if (script.length > 2 && memcmp(script.start, "#!", 2) == 0) {
        p = njs_strlchr(script.start, script.start + script.length, '\n');

        if (p != NULL) {
            script.length -= (p + 1 - script.start);
            script.start = p + 1;

        } else {
            script.length = 0;
        }
    }

    engine = njs_create_engine(opts);
    if (engine == NULL) {
        ret = NJS_ERROR;
        goto done;
    }

    ret = njs_process_script(engine, &njs_console, &script);
    if (ret != NJS_OK) {
        ret = NJS_ERROR;
        goto done;
    }

    ret = NJS_OK;

done:

    if (engine != NULL) {
        engine->destroy(engine);
    }

    if (source.start != NULL) {
        free(source.start);
    }

    return ret;
}


static njs_int_t
njs_process_script(njs_engine_t *engine, njs_console_t *console,
    njs_str_t *script)
{
    njs_int_t   ret;

    ret = engine->eval(engine, script);

    if (!console->suppress_stdout) {
        engine->output(engine, ret);
    }

    if (!console->interactive && ret == NJS_ERROR) {
        return NJS_ERROR;
    }

    for ( ;; ) {
        for ( ;; ) {
            ret = engine->execute_pending_job(engine);
            if (ret <= NJS_OK) {
                if (ret == NJS_ERROR) {
                    if (!console->suppress_stdout) {
                        engine->output(engine, ret);
                    }

                    if (!console->interactive) {
                         return NJS_ERROR;
                    }
                }

                break;
            }
        }

        ret = engine->process_events(engine);
        if (njs_slow_path(ret == NJS_ERROR)) {
            break;
        }

        if (engine->unhandled_rejection(engine)) {
            if (!console->suppress_stdout) {
                engine->output(engine, NJS_ERROR);
            }

            if (!console->interactive) {
                return NJS_ERROR;
            }
        }

        if (ret == NJS_OK) {
            break;
        }
    }

    return ret;
}


#if (!defined NJS_FUZZER_TARGET && defined NJS_HAVE_READLINE)


volatile sig_atomic_t njs_running;
volatile sig_atomic_t njs_sigint_count;
volatile sig_atomic_t njs_sigint_received;


static void
njs_cb_line_handler(char *line_in)
{
    njs_int_t  ret;
    njs_str_t  line;

    if (line_in == NULL) {
        njs_running = NJS_DONE;
        return;
    }

    line.start = (u_char *) line_in;
    line.length = njs_strlen(line.start);

    if (strcmp(line_in, ".exit") == 0) {
        njs_running = NJS_DONE;
        goto free_line;
    }

    njs_sigint_count = 0;

    if (line.length == 0) {
        rl_callback_handler_install(">> ", njs_cb_line_handler);
        goto free_line;
    }

    add_history((char *) line.start);

    ret = njs_process_script(njs_console.engine, &njs_console, &line);
    if (ret == NJS_ERROR) {
        njs_running = NJS_ERROR;
    }

    if (ret == NJS_OK) {
        rl_callback_handler_install(">> ", njs_cb_line_handler);
    }

free_line:

    free(line.start);
}


static njs_int_t
njs_interactive_shell(njs_opts_t *opts)
{
    int             flags;
    fd_set          fds;
    njs_int_t       ret;
    njs_engine_t    *engine;
    struct timeval  timeout;

    if (njs_editline_init() != NJS_OK) {
        njs_stderror("failed to init completions\n");
        return NJS_ERROR;
    }

    engine = njs_create_engine(opts);
    if (engine == NULL) {
        njs_stderror("njs_create_engine() failed\n");
        return NJS_ERROR;
    }

    if (!opts->quiet) {
        if (engine->type == NJS_ENGINE_NJS) {
            njs_printf("interactive njs (njs:%s)\n\n", NJS_VERSION);

#if (NJS_HAVE_QUICKJS)
        } else {
            njs_printf("interactive njs (QuickJS:%s)\n\n", NJS_QUICKJS_VERSION);
#endif
        }
    }

    rl_callback_handler_install(">> ", njs_cb_line_handler);

    flags = fcntl(STDIN_FILENO, F_GETFL, 0);
    fcntl(STDIN_FILENO, F_SETFL, flags | O_NONBLOCK);

    njs_running = NJS_OK;

    while (njs_running == NJS_OK) {
        FD_ZERO(&fds);
        FD_SET(STDIN_FILENO, &fds);
        timeout = (struct timeval) {1, 0};

        ret = select(FD_SETSIZE, &fds, NULL, NULL, &timeout);
        if (ret < 0 && errno != EINTR) {
            njs_stderror("select() failed\n");
            njs_running = NJS_ERROR;
            break;
        }

        if (njs_sigint_received) {
            if (njs_sigint_count > 1) {
                njs_running = NJS_DONE;
                break;
            }

            if (rl_end != 0) {
                njs_printf("\n");

                njs_sigint_count = 0;

            } else {
                njs_printf("(To exit, press Ctrl+C again or Ctrl+D "
                           "or type .exit)\n");

                njs_sigint_count = 1;
            }

            rl_point = rl_end = 0;
            rl_on_new_line();
            rl_redisplay();

            njs_sigint_received = 0;
        }

        if (ret < 0) {
            continue;
        }

        if (FD_ISSET(fileno(rl_instream), &fds)) {
            rl_callback_read_char();
        }
    }

    rl_callback_handler_remove();

    if (njs_running == NJS_DONE) {
        njs_printf("exiting\n");
    }

    engine->destroy(engine);

    return njs_running == NJS_DONE ? NJS_OK : njs_running;
}


static char **
njs_completion_handler(const char *text, int start, int end)
{
    rl_attempted_completion_over = 1;

    return rl_completion_matches(text, njs_completion_generator);
}


static void
njs_signal_handler(int signal)
{
    switch (signal) {
    case SIGINT:
        njs_sigint_received = 1;
        njs_sigint_count += 1;
        break;
    default:
        break;
    }
}


static njs_int_t
njs_editline_init(void)
{
    rl_completion_append_character = '\0';
    rl_attempted_completion_function = njs_completion_handler;
    rl_basic_word_break_characters = (char *) " \t\n\"\\'`@$><=;,|&{(";

    setlocale(LC_ALL, "");

    signal(SIGINT, njs_signal_handler);

    return NJS_OK;
}


static char *
njs_completion_generator(const char *text, int state)
{
    njs_str_t         expression, *suffix;
    njs_engine_t      *engine;
    njs_completion_t  *cmpl;

    engine = njs_console.engine;
    cmpl = &engine->completion;

    if (state == 0) {
        cmpl->index = 0;
        expression.start = (u_char *) text;
        expression.length = njs_strlen(text);

        cmpl->suffix_completions = engine->complete(engine, &expression);
        if (cmpl->suffix_completions == NULL) {
            return NULL;
        }
    }

    if (cmpl->index == cmpl->suffix_completions->items) {
        return NULL;
    }

    suffix = njs_arr_item(cmpl->suffix_completions, cmpl->index++);

    return strndup((char *) suffix->start, suffix->length);
}

#endif


static njs_int_t
njs_ext_console_log(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t magic, njs_value_t *retval)
{
    njs_str_t        msg;
    njs_uint_t       n;
    njs_log_level_t  level;

    n = 1;
    level = (njs_log_level_t) magic & NJS_LOG_MASK;

    while (n < nargs) {
        if (njs_vm_value_dump(vm, &msg, njs_argument(args, n), 1,
                              !!(magic & NJS_LOG_DUMP))
            == NJS_ERROR)
        {
            return NJS_ERROR;
        }

        njs_console_logger(level, msg.start, msg.length);

        n++;
    }

    njs_value_undefined_set(retval);

    return NJS_OK;
}


static njs_int_t
njs_ext_console_time(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t unused, njs_value_t *retval)
{
    njs_int_t      ret;
    njs_str_t      name;
    njs_value_t    *value;
    njs_console_t  *console;

    static const njs_str_t  default_label = njs_str("default");

    console = njs_vm_external(vm, njs_console_proto_id, njs_argument(args, 0));
    if (njs_slow_path(console == NULL)) {
        njs_vm_error(vm, "external value is expected");
        return NJS_ERROR;
    }

    name = default_label;

    value = njs_arg(args, nargs, 1);

    if (njs_slow_path(!njs_value_is_string(value))) {
        if (!njs_value_is_undefined(value)) {
            ret = njs_value_to_string(vm, value, value);
            if (njs_slow_path(ret != NJS_OK)) {
                return ret;
            }

            njs_value_string_get(vm, value, &name);
        }

    } else {
        njs_value_string_get(vm, value, &name);
    }

    if (njs_console_time(console, &name) != NJS_OK) {
        njs_vm_error(vm, "failed to add timer");
        return NJS_ERROR;
    }

    njs_value_undefined_set(retval);

    return NJS_OK;
}


static njs_int_t
njs_ext_console_time_end(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t unused, njs_value_t *retval)
{
    uint64_t       ns;
    njs_int_t      ret;
    njs_str_t      name;
    njs_value_t    *value;
    njs_console_t  *console;

    static const njs_str_t  default_label = njs_str("default");

    ns = njs_time();

    console = njs_vm_external(vm, njs_console_proto_id, njs_argument(args, 0));
    if (njs_slow_path(console == NULL)) {
        njs_vm_error(vm, "external value is expected");
        return NJS_ERROR;
    }

    name = default_label;

    value = njs_arg(args, nargs, 1);

    if (njs_slow_path(!njs_value_is_string(value))) {
        if (!njs_value_is_undefined(value)) {
            ret = njs_value_to_string(vm, value, value);
            if (njs_slow_path(ret != NJS_OK)) {
                return ret;
            }

            njs_value_string_get(vm, value, &name);
        }

    } else {
        njs_value_string_get(vm, value, &name);
    }

    njs_console_time_end(console, &name, ns);

    njs_value_undefined_set(retval);

    return NJS_OK;
}


static njs_int_t
njs_set_timer(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t unused, njs_bool_t immediate, njs_value_t *retval)
{
    njs_ev_t       *ev;
    uint64_t       delay;
    njs_uint_t     n;
    njs_console_t  *console;

    console = njs_vm_external_ptr(vm);

    if (njs_slow_path(nargs < 2)) {
        njs_vm_type_error(vm, "too few arguments");
        return NJS_ERROR;
    }

    if (njs_slow_path(!njs_value_is_function(njs_argument(args, 1)))) {
        njs_vm_type_error(vm, "first arg must be a function");
        return NJS_ERROR;
    }

    delay = 0;

    if (!immediate && nargs >= 3
        && njs_value_is_number(njs_argument(args, 2)))
    {
        delay = njs_value_number(njs_argument(args, 2));
    }

    if (delay != 0) {
        njs_vm_internal_error(vm, "njs_set_timer(): async timers unsupported");
        return NJS_ERROR;
    }

    n = immediate ? 2 : 3;
    nargs = (nargs >= n) ? nargs - n : 0;

    ev = njs_mp_alloc(njs_vm_memory_pool(vm),
                      sizeof(njs_ev_t) + sizeof(njs_opaque_value_t) * nargs);
    if (njs_slow_path(ev == NULL)) {
        njs_vm_memory_error(vm);
        return NJS_ERROR;
    }

    ev->u.njs.function = njs_value_function(njs_argument(args, 1));
    ev->u.njs.args = (njs_value_t *) ((u_char *) ev + sizeof(njs_ev_t));
    ev->nargs = nargs;
    ev->id = console->event_id++;

    if (ev->nargs != 0) {
        memcpy(ev->u.njs.args, njs_argument(args, n),
               sizeof(njs_opaque_value_t) * ev->nargs);
    }

    njs_rbtree_insert(&console->events, &ev->node);

    njs_queue_insert_tail(&console->posted_events, &ev->link);

    njs_value_number_set(retval, ev->id);

    return NJS_OK;
}


static njs_int_t
njs_set_timeout(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t unused, njs_value_t *retval)
{
    return njs_set_timer(vm, args, nargs, unused, 0, retval);
}


static njs_int_t
njs_set_immediate(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t unused, njs_value_t *retval)
{
    return njs_set_timer(vm, args, nargs, unused, 1, retval);
}


static njs_int_t
njs_clear_timeout(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t unused, njs_value_t *retval)
{
    njs_ev_t           ev_lookup, *ev;
    njs_console_t      *console;
    njs_rbtree_node_t  *rb;

    if (nargs < 2 || !njs_value_is_number(njs_argument(args, 1))) {
        njs_value_undefined_set(retval);
        return NJS_OK;
    }

    console = njs_vm_external_ptr(vm);

    ev_lookup.id = njs_value_number(njs_argument(args, 1));

    rb = njs_rbtree_find(&console->events, &ev_lookup.node);
    if (njs_slow_path(rb == NULL)) {
        njs_vm_internal_error(vm, "failed to find timer");
        return NJS_ERROR;
    }

    ev = (njs_ev_t *) rb;
    njs_queue_remove(&ev->link);
    njs_rbtree_delete(&console->events, (njs_rbtree_part_t *) rb);

    njs_mp_free(njs_vm_memory_pool(vm), ev);

    njs_value_undefined_set(retval);

    return NJS_OK;
}


static void
njs_console_log(njs_log_level_t level, const char *fmt, ...)
{
    u_char   *p;
    va_list  args;
    u_char   buf[2048];

    va_start(args, fmt);
    p = njs_vsprintf(buf, buf + sizeof(buf), fmt, args);
    va_end(args);

    njs_console_logger(level, buf, p - buf);
}


static void
njs_console_logger(njs_log_level_t level, const u_char *start, size_t length)
{
    switch (level) {
    case NJS_LOG_WARN:
        njs_printf("W: ");
        break;
    case NJS_LOG_ERROR:
        njs_printf("E: ");
        break;
    case NJS_LOG_INFO:
        break;
    }

    njs_print(start, length);
    njs_print("\n", 1);
}


static njs_int_t
njs_console_time(njs_console_t *console, njs_str_t *name)
{
    njs_queue_t       *labels;
    njs_timelabel_t   *label;
    njs_queue_link_t  *link;

    labels = &console->labels;
    link = njs_queue_first(labels);

    while (link != njs_queue_tail(labels)) {
        label = njs_queue_link_data(link, njs_timelabel_t, link);

        if (njs_strstr_eq(name, &label->name)) {
            njs_console_log(NJS_LOG_INFO, "Timer \"%V\" already exists.",
                            name);
            return NJS_OK;
        }

        link = njs_queue_next(link);
    }

    label = njs_mp_alloc(console->engine->pool,
                         sizeof(njs_timelabel_t) + name->length);
    if (njs_slow_path(label == NULL)) {
        return NJS_ERROR;
    }

    label->name.start = (u_char *) label + sizeof(njs_timelabel_t);
    memcpy(label->name.start, name->start, name->length);
    label->name.length = name->length;
    label->time = njs_time();

    njs_queue_insert_tail(&console->labels, &label->link);

    return NJS_OK;
}


static void
njs_console_time_end(njs_console_t *console, njs_str_t *name, uint64_t ns)
{
    uint64_t          ms;
    njs_queue_t       *labels;
    njs_timelabel_t   *label;
    njs_queue_link_t  *link;

    labels = &console->labels;
    link = njs_queue_first(labels);

    for ( ;; ) {
        if (link == njs_queue_tail(labels)) {
            njs_console_log(NJS_LOG_INFO, "Timer \"%V\" doesn’t exist.",
                            name);
            return;
        }

        label = njs_queue_link_data(link, njs_timelabel_t, link);

        if (njs_strstr_eq(name, &label->name)) {
            njs_queue_remove(&label->link);
            break;
        }

        link = njs_queue_next(link);
    }

    ns = ns - label->time;

    ms = ns / 1000000;
    ns = ns % 1000000;

    njs_console_log(NJS_LOG_INFO, "%V: %uL.%06uLms", name, ms, ns);

    njs_mp_free(console->engine->pool, label);
}


static intptr_t
njs_event_rbtree_compare(njs_rbtree_node_t *node1, njs_rbtree_node_t *node2)
{
    njs_ev_t  *ev1, *ev2;

    ev1 = (njs_ev_t *) node1;
    ev2 = (njs_ev_t *) node2;

    if (ev1->id < ev2->id) {
        return -1;
    }

    if (ev1->id > ev2->id) {
        return 1;
    }

    return 0;
}


static uint64_t
njs_time(void)
{
#if (NJS_HAVE_CLOCK_MONOTONIC)
    struct timespec ts;

    clock_gettime(CLOCK_MONOTONIC, &ts);

    return (uint64_t) ts.tv_sec * 1000000000 + ts.tv_nsec;
#else
    struct timeval tv;

    gettimeofday(&tv, NULL);

    return (uint64_t) tv.tv_sec * 1000000000 + tv.tv_usec * 1000;
#endif
}
