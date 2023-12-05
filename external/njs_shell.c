
/*
 * Copyright (C) Dmitry Volyntsev
 * Copyright (C) NGINX, Inc.
 */


#include <njs.h>
#include <njs_unix.h>
#include <njs_time.h>
#include <njs_arr.h>
#include <njs_queue.h>
#include <njs_rbtree.h>

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
    uint8_t                 opcode_debug;
    uint8_t                 generator_debug;
    int                     exit_code;
    int                     stack_size;

    char                    *file;
    njs_str_t               command;
    size_t                  n_paths;
    char                    **paths;
    char                    **argv;
    njs_uint_t              argc;
} njs_opts_t;


typedef struct {
    size_t                  index;
    size_t                  length;
    njs_arr_t               *completions;
    njs_arr_t               *suffix_completions;
    njs_rbtree_node_t       *node;

    enum {
       NJS_COMPLETION_SUFFIX = 0,
       NJS_COMPLETION_GLOBAL
    }                       phase;
} njs_completion_t;


typedef struct {
    NJS_RBTREE_NODE         (node);
    njs_function_t          *function;
    njs_value_t             *args;
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
    njs_vm_t                *vm;

    uint32_t                event_id;
    njs_rbtree_t            events;  /* njs_ev_t * */
    njs_queue_t             posted_events;

    njs_queue_t             labels;

    njs_bool_t              suppress_stdout;

    njs_completion_t        completion;
} njs_console_t;


static njs_int_t njs_main(njs_opts_t *opts);
static njs_int_t njs_console_init(njs_vm_t *vm, njs_console_t *console);
static void njs_console_output(njs_vm_t *vm, njs_value_t *value,
    njs_int_t ret);
static njs_int_t njs_externals_init(njs_vm_t *vm);
static njs_vm_t *njs_create_vm(njs_opts_t *opts);
static void njs_process_output(njs_vm_t *vm, njs_value_t *value, njs_int_t ret);
static njs_int_t njs_process_file(njs_opts_t *opts);
static njs_int_t njs_process_script(njs_vm_t *vm, void *runtime,
    const njs_str_t *script);

#ifndef NJS_FUZZER_TARGET

static njs_int_t njs_options_parse(njs_opts_t *opts, int argc, char **argv);
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

static void njs_console_log(njs_vm_t *vm, njs_external_ptr_t external,
    njs_log_level_t level, const u_char *start, size_t length);

static intptr_t njs_event_rbtree_compare(njs_rbtree_node_t *node1,
    njs_rbtree_node_t *node2);

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
            .magic8 = NJS_LOG_LEVEL_INFO | NJS_LOG_DUMP,
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
            .magic8 = NJS_LOG_LEVEL_ERROR,
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
            .magic8 = NJS_LOG_LEVEL_INFO,
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
            .magic8 = NJS_LOG_LEVEL_INFO,
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
            .magic8 = NJS_LOG_LEVEL_WARN,
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


static njs_vm_ops_t njs_console_ops = {
    NULL,
    njs_console_log,
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
    njs_vm_t   *vm;
    njs_int_t  ret;

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

#if (!defined NJS_FUZZER_TARGET && defined NJS_HAVE_READLINE)

    if (opts->interactive) {
        ret = njs_interactive_shell(opts);

    } else

#endif

    if (opts->command.length != 0) {
        vm = njs_create_vm(opts);
        if (vm == NULL) {
            return NJS_ERROR;
        }

        ret = njs_process_script(vm, njs_vm_external_ptr(vm), &opts->command);
        njs_vm_destroy(vm);

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
    char        *p, **paths;
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
#ifdef NJS_DEBUG_OPCODE
        "  -o                enable opcode debug.\n"
#endif
        "  -p <path>         set path prefix for modules.\n"
        "  -q                disable interactive introduction prompt.\n"
        "  -r                ignore unhandled promise rejection.\n"
        "  -s                sandbox mode.\n"
        "  -t script|module  source code type (script is default).\n"
        "  -v                print njs version and exit.\n"
        "  -u                disable \"unsafe\" mode.\n"
        "  script.js | -     run code from a file or stdin.\n";

    ret = NJS_DONE;

    opts->denormals = 1;
    opts->exit_code = EXIT_FAILURE;
    opts->unhandled_rejection = NJS_VM_OPT_UNHANDLED_REJECTION_THROW;

    p = getenv("NJS_EXIT_CODE");
    if (p != NULL) {
        opts->exit_code = atoi(p);
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
            return ret;

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

#ifdef NJS_DEBUG_OPCODE
        case 'o':
            opts->opcode_debug = 1;
            break;
#endif

        case 'p':
            if (++i < argc) {
                opts->n_paths++;
                paths = realloc(opts->paths, opts->n_paths * sizeof(char *));
                if (paths == NULL) {
                    njs_stderror("failed to add path\n");
                    return NJS_ERROR;
                }

                opts->paths = paths;
                opts->paths[opts->n_paths - 1] = argv[i];
                break;
            }

            njs_stderror("option \"-p\" requires directory name\n");
            return NJS_ERROR;

        case 'q':
            opts->quiet = 1;
            break;

        case 'r':
            opts->unhandled_rejection = NJS_VM_OPT_UNHANDLED_REJECTION_IGNORE;
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

    njs_memzero(&njs_console, sizeof(njs_console_t));

    njs_console.suppress_stdout = 1;

    return njs_main(&opts);
}

#endif

static njs_int_t
njs_console_init(njs_vm_t *vm, njs_console_t *console)
{
    console->vm = vm;

    console->event_id = 0;
    njs_rbtree_init(&console->events, njs_event_rbtree_compare);
    njs_queue_init(&console->posted_events);
    njs_queue_init(&console->labels);

    console->completion.completions = njs_vm_completions(vm, NULL);
    if (console->completion.completions == NULL) {
        return NJS_ERROR;
    }

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

    console = njs_vm_options(vm)->external;

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

    ret = njs_console_init(vm, console);
    if (njs_slow_path(ret != NJS_OK)) {
        return NJS_ERROR;
    }

    return NJS_OK;
}


static njs_vm_t *
njs_create_vm(njs_opts_t *opts)
{
    u_char        *p, *start;
    njs_vm_t      *vm;
    njs_int_t     ret;
    njs_str_t     path;
    njs_uint_t    i;
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

    vm_options.ops = &njs_console_ops;
    vm_options.addons = njs_console_addon_modules;
    vm_options.external = &njs_console;
    vm_options.argv = opts->argv;
    vm_options.argc = opts->argc;
    vm_options.ast = opts->ast;
    vm_options.unhandled_rejection = opts->unhandled_rejection;

    if (opts->stack_size != 0) {
        vm_options.max_stack_size = opts->stack_size;
    }

    vm = njs_vm_create(&vm_options);
    if (vm == NULL) {
        njs_stderror("failed to create vm\n");
        return NULL;
    }

    for (i = 0; i < opts->n_paths; i++) {
        path.start = (u_char *) opts->paths[i];
        path.length = njs_strlen(opts->paths[i]);

        ret = njs_vm_add_path(vm, &path);
        if (ret != NJS_OK) {
            njs_stderror("failed to add path\n");
            return NULL;
        }
    }

    start = (u_char *) getenv("NJS_PATH");
    if (start == NULL) {
        return vm;
    }

    for ( ;; ) {
        p = njs_strchr(start, ':');

        path.start = start;
        path.length = (p != NULL) ? (size_t) (p - start) : njs_strlen(start);

        ret = njs_vm_add_path(vm, &path);
        if (ret != NJS_OK) {
            njs_stderror("failed to add path\n");
            return NULL;
        }

        if (p == NULL) {
            break;
        }

        start = p + 1;
    }

    return vm;
}


static void
njs_console_output(njs_vm_t *vm, njs_value_t *value, njs_int_t ret)
{
    njs_str_t  out;

    if (ret == NJS_OK) {
        if (njs_vm_value_dump(vm, &out, value, 0, 1) != NJS_OK) {
            njs_stderror("Shell:failed to get retval from VM\n");
            return;
        }

        if (njs_vm_options(vm)->interactive) {
            njs_print(out.start, out.length);
            njs_print("\n", 1);
        }

    } else {
        njs_vm_exception_string(vm, &out);
        njs_stderror("Thrown:\n%V\n", &out);
    }
}


static njs_int_t
njs_process_events(void *runtime)
{
    njs_ev_t            *ev;
    njs_vm_t            *vm;
    njs_int_t           ret;
    njs_queue_t         *events;
    njs_console_t       *console;
    njs_queue_link_t    *link;
    njs_opaque_value_t  retval;

    if (runtime == NULL) {
        njs_stderror("njs_process_events(): no runtime\n");
        return NJS_ERROR;
    }

    console = runtime;
    vm = console->vm;

    events = &console->posted_events;

    for ( ;; ) {
        link = njs_queue_first(events);

        if (link == njs_queue_tail(events)) {
            break;
        }

        ev = njs_queue_link_data(link, njs_ev_t, link);

        njs_queue_remove(&ev->link);
        ev->link.prev = NULL;
        ev->link.next = NULL;

        njs_rbtree_delete(&console->events, &ev->node);

        ret = njs_vm_invoke(vm, ev->function, ev->args, ev->nargs,
                            njs_value_arg(&retval));
        if (ret == NJS_ERROR) {
            njs_process_output(vm, njs_value_arg(&retval), ret);

            if (!njs_vm_options(vm)->interactive) {
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
    content->start = realloc(NULL, size);
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

            start = realloc(content->start, size);
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
    u_char     *p;
    njs_vm_t   *vm;
    njs_int_t  ret;
    njs_str_t  source, script;

    vm = NULL;
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

    vm = njs_create_vm(opts);
    if (vm == NULL) {
        ret = NJS_ERROR;
        goto done;
    }

    ret = njs_process_script(vm, njs_vm_external_ptr(vm), &script);
    if (ret != NJS_OK) {
        ret = NJS_ERROR;
        goto done;
    }

    ret = NJS_OK;

done:

    if (vm != NULL) {
        njs_vm_destroy(vm);
    }

    if (source.start != NULL) {
        free(source.start);
    }

    return ret;
}


static njs_int_t
njs_process_script(njs_vm_t *vm, void *runtime, const njs_str_t *script)
{
    u_char              *start, *end;
    njs_int_t           ret;
    njs_opaque_value_t  retval;

    start = script->start;
    end = start + script->length;

    ret = njs_vm_compile(vm, &start, end);

    if (ret == NJS_OK) {
        if (start == end) {
            ret = njs_vm_start(vm, njs_value_arg(&retval));

        } else {
            njs_vm_error(vm, "Extra characters at the end of the script");
            ret = NJS_ERROR;
        }
    }

    njs_process_output(vm, njs_value_arg(&retval), ret);

    if (!njs_vm_options(vm)->interactive && ret == NJS_ERROR) {
        return NJS_ERROR;
    }

    for ( ;; ) {
        for ( ;; ) {
            ret = njs_vm_execute_pending_job(vm);
            if (ret <= NJS_OK) {
                if (ret == NJS_ERROR || njs_vm_unhandled_rejection(vm)) {
                    njs_process_output(vm, NULL, ret);

                    if (!njs_vm_options(vm)->interactive) {
                        return NJS_ERROR;
                    }
                }

                break;
            }
        }

        ret = njs_process_events(runtime);
        if (njs_slow_path(ret == NJS_ERROR)) {
            break;
        }

        if (ret == NJS_OK) {
            break;
        }
    }

    return ret;
}


static void
njs_process_output(njs_vm_t *vm, njs_value_t *value, njs_int_t ret)
{
    njs_console_t  *console;

    console = njs_vm_external_ptr(vm);

    if (!console->suppress_stdout) {
        njs_console_output(vm, value, ret);
    }
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

    ret = njs_process_script(njs_console.vm, &njs_console, &line);
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
    njs_vm_t        *vm;
    njs_int_t       ret;
    struct timeval  timeout;

    if (njs_editline_init() != NJS_OK) {
        njs_stderror("failed to init completions\n");
        return NJS_ERROR;
    }

    vm = njs_create_vm(opts);
    if (vm == NULL) {
        return NJS_ERROR;
    }

    if (!opts->quiet) {
        njs_printf("interactive njs %s\n\n", NJS_VERSION);

        njs_printf("v.<Tab> -> the properties and prototype methods of v.\n\n");
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

    njs_vm_destroy(vm);

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


/* editline frees the buffer every time. */
#define njs_editline(s) strndup((char *) (s)->start, (s)->length)

#define njs_completion(c, i) &(((njs_str_t *) (c)->start)[i])

#define njs_next_phase(c)                                                   \
    (c)->index = 0;                                                         \
    (c)->phase++;                                                           \
    goto next;

static char *
njs_completion_generator(const char *text, int state)
{
    njs_str_t         expression, *suffix;
    njs_vm_t          *vm;
    njs_completion_t  *cmpl;

    vm = njs_console.vm;
    cmpl = &njs_console.completion;

    if (state == 0) {
        cmpl->phase = NJS_COMPLETION_SUFFIX;
        cmpl->index = 0;
        cmpl->length = njs_strlen(text);
        cmpl->suffix_completions = NULL;
    }

next:

    switch (cmpl->phase) {
    case NJS_COMPLETION_SUFFIX:
        if (cmpl->length == 0) {
            njs_next_phase(cmpl);
        }

        if (cmpl->suffix_completions == NULL) {
            expression.start = (u_char *) text;
            expression.length = cmpl->length;

            cmpl->suffix_completions = njs_vm_completions(vm, &expression);
            if (cmpl->suffix_completions == NULL) {
                njs_next_phase(cmpl);
            }
        }

        for ( ;; ) {
            if (cmpl->index >= cmpl->suffix_completions->items) {
                njs_next_phase(cmpl);
            }

            suffix = njs_completion(cmpl->suffix_completions, cmpl->index++);

            return njs_editline(suffix);
        }

    case NJS_COMPLETION_GLOBAL:
        if (cmpl->suffix_completions != NULL) {
            /* No global completions if suffixes were found. */
            njs_next_phase(cmpl);
        }

        for ( ;; ) {
            if (cmpl->index >= cmpl->completions->items) {
                break;
            }

            suffix = njs_completion(cmpl->completions, cmpl->index++);

            if (suffix->start[0] == '.' || suffix->length < cmpl->length) {
                continue;
            }

            if (njs_strncmp(text, suffix->start, cmpl->length) == 0) {
                return njs_editline(suffix);
            }
        }
    }

    return NULL;
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

        njs_vm_logger(vm, level, "%*s\n", msg.length, msg.start);

        n++;
    }

    njs_value_undefined_set(retval);

    return NJS_OK;
}


static njs_int_t
njs_ext_console_time(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t unused, njs_value_t *retval)
{
    njs_int_t         ret;
    njs_str_t         name;
    njs_queue_t       *labels;
    njs_value_t       *value;
    njs_console_t     *console;
    njs_timelabel_t   *label;
    njs_queue_link_t  *link;

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

            njs_value_string_get(value, &name);
        }

    } else {
        njs_value_string_get(value, &name);
    }

    labels = &console->labels;
    link = njs_queue_first(labels);

    while (link != njs_queue_tail(labels)) {
        label = njs_queue_link_data(link, njs_timelabel_t, link);

        if (njs_strstr_eq(&name, &label->name)) {
            njs_vm_log(vm, "Timer \"%V\" already exists.\n", &name);
            njs_value_undefined_set(retval);
            return NJS_OK;
        }

        link = njs_queue_next(link);
    }

    label = njs_mp_alloc(njs_vm_memory_pool(vm), sizeof(njs_timelabel_t));
    if (njs_slow_path(label == NULL)) {
        njs_vm_memory_error(vm);
        return NJS_ERROR;
    }

    label->name = name;
    label->time = njs_time();

    njs_queue_insert_tail(&console->labels, &label->link);

    njs_value_undefined_set(retval);

    return NJS_OK;
}


static njs_int_t
njs_ext_console_time_end(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t unused, njs_value_t *retval)
{
    uint64_t          ns, ms;
    njs_int_t         ret;
    njs_str_t         name;
    njs_queue_t       *labels;
    njs_value_t       *value;
    njs_console_t     *console;
    njs_timelabel_t   *label;
    njs_queue_link_t  *link;

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

            njs_value_string_get(value, &name);
        }

    } else {
        njs_value_string_get(value, &name);
    }

    labels = &console->labels;
    link = njs_queue_first(labels);

    for ( ;; ) {
        if (link == njs_queue_tail(labels)) {
            njs_vm_log(vm, "Timer \"%V\" doesn’t exist.\n", &name);
            njs_value_undefined_set(retval);
            return NJS_OK;
        }

        label = njs_queue_link_data(link, njs_timelabel_t, link);

        if (njs_strstr_eq(&name, &label->name)) {
            njs_queue_remove(&label->link);
            break;
        }

        link = njs_queue_next(link);
    }

    ns = ns - label->time;

    ms = ns / 1000000;
    ns = ns % 1000000;

    njs_vm_log(vm, "%V: %uL.%06uLms\n", &name, ms, ns);

    njs_mp_free(njs_vm_memory_pool(vm), label);

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

    ev->function = njs_value_function(njs_argument(args, 1));
    ev->nargs = nargs;
    ev->args = (njs_value_t *) ((u_char *) ev + sizeof(njs_ev_t));
    ev->id = console->event_id++;

    if (ev->nargs != 0) {
        memcpy(ev->args, njs_argument(args, n),
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

    njs_rbtree_delete(&console->events, (njs_rbtree_part_t *) rb);

    ev = (njs_ev_t *) rb;
    njs_queue_remove(&ev->link);
    ev->link.prev = NULL;
    ev->link.next = NULL;

    njs_value_undefined_set(retval);

    return NJS_OK;
}


static void
njs_console_log(njs_vm_t *vm, njs_external_ptr_t external,
    njs_log_level_t level, const u_char *start, size_t length)
{
    switch (level) {
    case NJS_LOG_LEVEL_INFO:
        njs_printf("%*s", length, start);
        break;
    case NJS_LOG_LEVEL_WARN:
        njs_printf("W: %*s", length, start);
        break;
    case NJS_LOG_LEVEL_ERROR:
        njs_printf("E: %*s", length, start);
        break;
    }
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
