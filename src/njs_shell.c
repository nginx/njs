
/*
 * Copyright (C) Dmitry Volyntsev
 * Copyright (C) NGINX, Inc.
 */


#include <njs_main.h>

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
#if (NJS_HAVE_GNU_READLINE)
#include <readline/history.h>
#endif
#endif

#endif


typedef void (*njs_console_output_pt)(njs_vm_t *vm, njs_int_t ret);


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
    char                    *command;
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
       NJS_COMPLETION_VAR = 0,
       NJS_COMPLETION_SUFFIX,
       NJS_COMPLETION_GLOBAL
    }                       phase;
} njs_completion_t;


typedef struct {
    njs_vm_event_t          vm_event;
    njs_queue_link_t        link;
} njs_ev_t;


typedef struct {
    njs_value_t             name;
    uint64_t                time;
} njs_timelabel_t;


typedef struct {
    njs_vm_t                *vm;

    njs_lvlhsh_t            events;  /* njs_ev_t * */
    njs_queue_t             posted_events;

    njs_lvlhsh_t            labels;  /* njs_timelabel_t */

    njs_completion_t        completion;
} njs_console_t;


static njs_int_t njs_console_init(njs_vm_t *vm, njs_console_t *console);
static void njs_console_output(njs_vm_t *vm, njs_int_t ret);
static njs_int_t njs_externals_init(njs_vm_t *vm);
static njs_vm_t *njs_create_vm(njs_opts_t *opts, njs_vm_opt_t *vm_options);
static void njs_process_output(njs_vm_t *vm, njs_int_t ret);
static njs_int_t njs_process_script(njs_vm_t *vm, void *runtime,
    const njs_str_t *script);

#ifndef NJS_FUZZER_TARGET

static njs_int_t njs_options_parse(njs_opts_t *opts, int argc, char **argv);
static void njs_options_free(njs_opts_t *opts);
static njs_int_t njs_process_file(njs_opts_t *opts, njs_vm_opt_t *vm_options);

#ifdef NJS_HAVE_READLINE
static njs_int_t njs_interactive_shell(njs_opts_t *opts,
    njs_vm_opt_t *vm_options);
static njs_int_t njs_editline_init(void);
static char *njs_completion_generator(const char *text, int state);
#endif

#endif

static njs_int_t njs_ext_console_log(njs_vm_t *vm, njs_value_t *args,
    njs_uint_t nargs, njs_index_t indent);
static njs_int_t njs_ext_console_time(njs_vm_t *vm, njs_value_t *args,
    njs_uint_t nargs, njs_index_t unused);
static njs_int_t njs_ext_console_time_end(njs_vm_t *vm, njs_value_t *args,
    njs_uint_t nargs, njs_index_t unused);

static njs_host_event_t njs_console_set_timer(njs_external_ptr_t external,
    uint64_t delay, njs_vm_event_t vm_event);

static void njs_console_clear_timer(njs_external_ptr_t external,
    njs_host_event_t event);
static void njs_console_log(njs_vm_t *vm, njs_external_ptr_t external,
    njs_log_level_t level, const u_char *start, size_t length);

static njs_int_t njs_timelabel_hash_test(njs_lvlhsh_query_t *lhq, void *data);

static njs_int_t lvlhsh_key_test(njs_lvlhsh_query_t *lhq, void *data);
static void *lvlhsh_pool_alloc(void *pool, size_t size);
static void lvlhsh_pool_free(void *pool, void *p, size_t size);


static njs_external_t  njs_ext_console[] = {

    {
        .flags = NJS_EXTERN_METHOD,
        .name.string = njs_str("dump"),
        .writable = 1,
        .configurable = 1,
        .enumerable = 1,
        .u.method = {
            .native = njs_ext_console_log,
            .magic8 = 1,
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

};


static const njs_lvlhsh_proto_t  lvlhsh_proto  njs_aligned(64) = {
    NJS_LVLHSH_LARGE_SLAB,
    lvlhsh_key_test,
    lvlhsh_pool_alloc,
    lvlhsh_pool_free,
};


static const njs_lvlhsh_proto_t  njs_timelabel_hash_proto njs_aligned(64) = {
    NJS_LVLHSH_DEFAULT,
    njs_timelabel_hash_test,
    lvlhsh_pool_alloc,
    lvlhsh_pool_free,
};


static njs_vm_ops_t njs_console_ops = {
    njs_console_set_timer,
    njs_console_clear_timer,
    NULL,
    njs_console_log,
};


njs_module_t  njs_console_module = {
    .name = njs_str("console"),
    .init = njs_externals_init,
};


static njs_module_t *njs_console_addon_modules[] = {
    &njs_console_module,
    NULL,
};


static njs_int_t      njs_console_proto_id;


static njs_console_t  njs_console;


#ifndef NJS_FUZZER_TARGET

int
main(int argc, char **argv)
{
    njs_vm_t      *vm;
    njs_int_t     ret;
    njs_opts_t    opts;
    njs_str_t     command;
    njs_vm_opt_t  vm_options;

    static uintptr_t uptr[] = {
        (uintptr_t) njs_console_output,
    };

    static njs_vm_meta_t metas = {
        .size = njs_nitems(uptr),
        .values = uptr
    };

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

    njs_mm_denormals(opts.denormals);

    njs_vm_opt_init(&vm_options);

    if (opts.file == NULL) {
        opts.file = (opts.command == NULL) ? (char *) "shell"
                                           : (char *) "string";
    }

    vm_options.file.start = (u_char *) opts.file;
    vm_options.file.length = njs_strlen(opts.file);

    vm_options.init = 1;
    vm_options.interactive = opts.interactive;
    vm_options.disassemble = opts.disassemble;
    vm_options.backtrace = 1;
    vm_options.quiet = opts.quiet;
    vm_options.sandbox = opts.sandbox;
    vm_options.unsafe = !opts.safe;
    vm_options.module = opts.module;
#ifdef NJS_DEBUG_GENERATOR
    vm_options.generator_debug = opts.generator_debug;
#endif
#ifdef NJS_DEBUG_OPCODE
    vm_options.opcode_debug = opts.opcode_debug;
#endif

    vm_options.ops = &njs_console_ops;
    vm_options.addons = njs_console_addon_modules;
    vm_options.metas = &metas;
    vm_options.external = &njs_console;
    vm_options.argv = opts.argv;
    vm_options.argc = opts.argc;
    vm_options.ast = opts.ast;
    vm_options.unhandled_rejection = opts.unhandled_rejection;

    if (opts.stack_size != 0) {
        vm_options.max_stack_size = opts.stack_size;
    }

#ifdef NJS_HAVE_READLINE

    if (opts.interactive) {
        ret = njs_interactive_shell(&opts, &vm_options);

    } else

#endif

    if (opts.command) {
        vm = njs_create_vm(&opts, &vm_options);
        if (vm != NULL) {
            command.start = (u_char *) opts.command;
            command.length = njs_strlen(opts.command);
            ret = njs_process_script(vm, vm->external, &command);
            njs_vm_destroy(vm);
        }

    } else {
        ret = njs_process_file(&opts, &vm_options);
    }

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
            (void) write(STDOUT_FILENO, help, njs_length(help));
            return ret;

        case 'a':
            opts->ast = 1;
            break;

        case 'c':
            opts->interactive = 0;

            if (++i < argc) {
                opts->command = argv[i];
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


static njs_int_t
njs_process_file(njs_opts_t *opts, njs_vm_opt_t *vm_options)
{
    int          fd;
    char         *file;
    u_char       *p, *end, *start;
    size_t       size;
    ssize_t      n;
    njs_vm_t     *vm;
    njs_int_t    ret;
    njs_str_t    source, script;
    struct stat  sb;
    u_char       buf[4096];

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

    size = sizeof(buf);

    if (S_ISREG(sb.st_mode) && sb.st_size) {
        size = sb.st_size;
    }

    vm = NULL;

    source.length = 0;
    source.start = realloc(NULL, size);
    if (source.start == NULL) {
        njs_stderror("alloc failed while reading '%s'\n", file);
        ret = NJS_ERROR;
        goto done;
    }

    p = source.start;
    end = p + size;

    for ( ;; ) {
        n = read(fd, buf, sizeof(buf));

        if (n == 0) {
            break;
        }

        if (n < 0) {
            njs_stderror("failed to read file: '%s' (%s)\n",
                      file, strerror(errno));
            ret = NJS_ERROR;
            goto done;
        }

        if (p + n > end) {
            size *= 2;

            start = realloc(source.start, size);
            if (start == NULL) {
                njs_stderror("alloc failed while reading '%s'\n", file);
                ret = NJS_ERROR;
                goto done;
            }

            source.start = start;

            p = source.start + source.length;
            end = source.start + size;
        }

        memcpy(p, buf, n);

        p += n;
        source.length += n;
    }

    vm = njs_create_vm(opts, vm_options);
    if (vm == NULL) {
        ret = NJS_ERROR;
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

    ret = njs_process_script(vm, vm_options->external, &script);
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

close_fd:

    if (fd != STDIN_FILENO) {
        (void) close(fd);
    }

    return ret;
}

#else

int
LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
    njs_vm_t      *vm;
    njs_opts_t    opts;
    njs_str_t     script;
    njs_vm_opt_t  vm_options;

    static uintptr_t uptr[] = {
        (uintptr_t) NULL,
    };

    static njs_vm_meta_t metas = {
        .size = njs_nitems(uptr),
        .values = uptr
    };

    if (size == 0) {
        return 0;
    }

    njs_memzero(&opts, sizeof(njs_opts_t));

    njs_vm_opt_init(&vm_options);

    vm_options.init = 1;
    vm_options.backtrace = 0;
    vm_options.metas = &metas;
    vm_options.ops = &njs_console_ops;

    vm = njs_create_vm(&opts, &vm_options);

    if (njs_fast_path(vm != NULL)) {
        script.length = size;
        script.start = (u_char *) data;

        (void) njs_process_script(vm, NULL, &script);
        njs_vm_destroy(vm);
    }

    return 0;
}

#endif

static njs_int_t
njs_console_init(njs_vm_t *vm, njs_console_t *console)
{
    console->vm = vm;

    njs_lvlhsh_init(&console->events);
    njs_queue_init(&console->posted_events);

    njs_lvlhsh_init(&console->labels);

    console->completion.completions = njs_vm_completions(vm, NULL);
    if (console->completion.completions == NULL) {
        return NJS_ERROR;
    }

    return NJS_OK;
}


static njs_int_t
njs_externals_init(njs_vm_t *vm)
{
    njs_int_t      ret;
    njs_value_t    *value, method;
    njs_console_t  *console;

    static const njs_str_t  console_name = njs_str("console");
    static const njs_str_t  print_name = njs_str("print");
    static const njs_str_t  console_log = njs_str("console.log");

    console = vm->options.external;

    njs_console_proto_id = njs_vm_external_prototype(vm, njs_ext_console,
                                         njs_nitems(njs_ext_console));
    if (njs_slow_path(njs_console_proto_id < 0)) {
        njs_stderror("failed to add \"console\" proto\n");
        return NJS_ERROR;
    }

    value = njs_mp_zalloc(vm->mem_pool, sizeof(njs_opaque_value_t));
    if (njs_slow_path(value == NULL)) {
        return NJS_ERROR;
    }

    ret = njs_vm_external_create(vm, value, njs_console_proto_id, console, 0);
    if (njs_slow_path(ret != NJS_OK)) {
        return NJS_ERROR;
    }

    ret = njs_vm_bind(vm, &console_name, value, 0);
    if (njs_slow_path(ret != NJS_OK)) {
        return NJS_ERROR;
    }

    ret = njs_vm_value(vm, &console_log, &method);
    if (njs_slow_path(ret != NJS_OK)) {
        return NJS_ERROR;
    }

    ret = njs_vm_bind(vm, &print_name, &method, 0);
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
njs_create_vm(njs_opts_t *opts, njs_vm_opt_t *vm_options)
{
    u_char      *p, *start;
    njs_vm_t    *vm;
    njs_int_t   ret;
    njs_str_t   path;
    njs_uint_t  i;

    vm = njs_vm_create(vm_options);
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
njs_console_output(njs_vm_t *vm, njs_int_t ret)
{
    njs_str_t  out;

    if (ret == NJS_OK) {
        if (njs_vm_retval_dump(vm, &out, 1) != NJS_OK) {
            njs_stderror("Shell:failed to get retval from VM\n");
            return;
        }

        if (vm->options.interactive) {
            njs_print(out.start, out.length);
            njs_print("\n", 1);
        }

    } else {
        njs_vm_retval_string(vm, &out);
        njs_stderror("Thrown:\n%V\n", &out);
    }
}


static njs_int_t
njs_process_events(void *runtime)
{
    njs_ev_t          *ev;
    njs_queue_t       *events;
    njs_console_t     *console;
    njs_queue_link_t  *link;

    if (runtime == NULL) {
        njs_stderror("njs_process_events(): no runtime\n");
        return NJS_ERROR;
    }

    console = runtime;

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

        njs_vm_post_event(console->vm, ev->vm_event, NULL, 0);
    }

    return NJS_OK;
}


static njs_int_t
njs_process_script(njs_vm_t *vm, void *runtime, const njs_str_t *script)
{
    u_char     *start, *end;
    njs_int_t  ret;

    start = script->start;
    end = start + script->length;

    ret = njs_vm_compile(vm, &start, end);

    if (ret == NJS_OK) {
        if (start == end) {
            ret = njs_vm_start(vm);

        } else {
            njs_vm_error(vm, "Extra characters at the end of the script");
            ret = NJS_ERROR;
        }
    }

    njs_process_output(vm, ret);

    if (!vm->options.interactive && ret == NJS_ERROR) {
        return NJS_ERROR;
    }

    for ( ;; ) {
        if (!njs_vm_pending(vm) && !njs_vm_unhandled_rejection(vm)) {
            ret = NJS_OK;
            break;
        }

        ret = njs_process_events(runtime);
        if (njs_slow_path(ret != NJS_OK)) {
            njs_stderror("njs_process_events() failed\n");
            ret = NJS_ERROR;
            break;
        }

        if (njs_vm_waiting(vm) && !njs_vm_posted(vm)) {
            /*TODO: async events. */

            njs_stderror("njs_process_script(): async events unsupported\n");
            ret = NJS_ERROR;
            break;
        }

        ret = njs_vm_run(vm);

        if (ret == NJS_ERROR) {
            njs_process_output(vm, ret);

            if (!vm->options.interactive) {
                return NJS_ERROR;
            }
        }
    }

    return ret;
}


static void
njs_process_output(njs_vm_t *vm, njs_int_t ret)
{
    njs_console_output_pt  pt;

    pt = (njs_console_output_pt) njs_vm_meta(vm, 0);

    if (pt != NULL) {
        pt(vm, ret);
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

    if (line_in == NULL || strcmp(line_in, ".exit") == 0) {
        njs_running = NJS_DONE;
        return;
    }

    njs_sigint_count = 0;

    line.start = (u_char *) line_in;
    line.length = njs_strlen(line.start);

    if (line.length == 0) {
        return;
    }

    add_history((char *) line.start);

    ret = njs_process_script(njs_console.vm, &njs_console, &line);
    if (ret == NJS_ERROR) {
        njs_running = NJS_ERROR;
    }

    free(line.start);
}


static njs_int_t
njs_interactive_shell(njs_opts_t *opts, njs_vm_opt_t *vm_options)
{
    fd_set     fds;
    njs_vm_t   *vm;
    njs_int_t  ret;

    if (njs_editline_init() != NJS_OK) {
        njs_stderror("failed to init completions\n");
        return NJS_ERROR;
    }

    vm = njs_create_vm(opts, vm_options);
    if (vm == NULL) {
        return NJS_ERROR;
    }

    if (!opts->quiet) {
        njs_printf("interactive njs %s\n\n", NJS_VERSION);

        njs_printf("v.<Tab> -> the properties and prototype methods of v.\n\n");
    }

    rl_callback_handler_install(">> ", njs_cb_line_handler);

    njs_running = NJS_OK;

    while (njs_running == NJS_OK) {
        FD_ZERO(&fds);
        FD_SET(fileno(rl_instream), &fds);

        ret = select(FD_SETSIZE, &fds, NULL, NULL, NULL);
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
    char                     *completion;
    size_t                   len;
    njs_str_t                expression, *suffix;
    njs_vm_t                 *vm;
    const char               *p;
    njs_rbtree_t             *variables;
    njs_completion_t         *cmpl;
    njs_variable_node_t      *var_node;
    const njs_lexer_entry_t  *lex_entry;

    vm = njs_console.vm;
    cmpl = &njs_console.completion;

    if (state == 0) {
        cmpl->phase = 0;
        cmpl->index = 0;
        cmpl->length = njs_strlen(text);
        cmpl->suffix_completions = NULL;

        if (vm->variables_hash != NULL) {
            cmpl->node = njs_rbtree_min(vm->variables_hash);
        }
    }

next:

    switch (cmpl->phase) {
    case NJS_COMPLETION_VAR:
        variables = vm->variables_hash;

        if (variables == NULL) {
            njs_next_phase(cmpl);
        }

        while (njs_rbtree_is_there_successor(variables, cmpl->node)) {
            var_node = (njs_variable_node_t *) cmpl->node;

            lex_entry = njs_lexer_entry(var_node->key);
            if (lex_entry == NULL) {
                break;
            }

            cmpl->node = njs_rbtree_node_successor(variables, cmpl->node);

            if (lex_entry->name.length >= cmpl->length
                && njs_strncmp(text, lex_entry->name.start, cmpl->length) == 0)
            {
                return njs_editline(&lex_entry->name);
            }

        }

        njs_next_phase(cmpl);

    case NJS_COMPLETION_SUFFIX:
        if (cmpl->length == 0) {
            njs_next_phase(cmpl);
        }

        if (cmpl->suffix_completions == NULL) {
            /* Getting the longest prefix before a '.' */

            p = &text[cmpl->length - 1];
            while (p > text && *p != '.') { p--; }

            if (*p != '.') {
                njs_next_phase(cmpl);
            }

            expression.start = (u_char *) text;
            expression.length = p - text;

            cmpl->suffix_completions = njs_vm_completions(vm, &expression);
            if (cmpl->suffix_completions == NULL) {
                njs_next_phase(cmpl);
            }
        }

        /* Getting the right-most suffix after a '.' */

        len = 0;
        p = &text[cmpl->length - 1];

        while (p > text && *p != '.') {
            p--;
            len++;
        }

        p++;

        for ( ;; ) {
            if (cmpl->index >= cmpl->suffix_completions->items) {
                njs_next_phase(cmpl);
            }

            suffix = njs_completion(cmpl->suffix_completions, cmpl->index++);

            if (len != 0 && njs_strncmp(suffix->start, p,
                                        njs_min(len, suffix->length)) != 0)
            {
                continue;
            }

            len = suffix->length + (p - text) + 1;
            completion = malloc(len);
            if (completion == NULL) {
                return NULL;
            }

            njs_sprintf((u_char *) completion, (u_char *) completion + len,
                        "%*s%V%Z", p - text, text, suffix);
            return completion;
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
    njs_index_t indent)
{
    njs_str_t   msg;
    njs_uint_t  n;

    n = 1;

    while (n < nargs) {
        if (njs_vm_value_dump(vm, &msg, njs_argument(args, n), 1, indent)
            == NJS_ERROR)
        {
            return NJS_ERROR;
        }

        njs_vm_log(vm, "%s", (n != 1) ? " " : "");
        njs_vm_log(vm, "%*s", msg.length, msg.start);

        n++;
    }

    if (nargs > 1) {
        njs_vm_log(vm, "\n");
    }

    njs_set_undefined(&vm->retval);

    return NJS_OK;
}


static const njs_value_t  njs_default_label = njs_string("default");


static njs_int_t
njs_ext_console_time(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t unused)
{
    njs_int_t           ret;
    njs_str_t           name;
    njs_value_t         *value;
    njs_console_t       *console;
    njs_timelabel_t     *label;
    njs_lvlhsh_query_t  lhq;

    console = njs_vm_external(vm, njs_console_proto_id, njs_argument(args, 0));
    if (njs_slow_path(console == NULL)) {
        njs_type_error(vm, "external value is expected");
        return NJS_ERROR;
    }

    value = njs_arg(args, nargs, 1);

    if (njs_slow_path(!njs_is_string(value))) {
        if (njs_is_undefined(value)) {
            value = njs_value_arg(&njs_default_label);

        } else {
            ret = njs_value_to_string(vm, value, value);
            if (njs_slow_path(ret != NJS_OK)) {
                return ret;
            }
        }
    }

    njs_string_get(value, &name);

    label = njs_mp_alloc(vm->mem_pool, sizeof(njs_timelabel_t));
    if (njs_slow_path(label == NULL)) {
        njs_memory_error(vm);
        return NJS_ERROR;
    }

    lhq.replace = 0;
    lhq.key = name;
    lhq.key_hash = njs_djb_hash(name.start, name.length);
    lhq.value = label;
    lhq.pool = vm->mem_pool;
    lhq.proto = &njs_timelabel_hash_proto;

    ret = njs_lvlhsh_insert(&console->labels, &lhq);

    if (njs_fast_path(ret == NJS_OK)) {
        /* GC: retain. */
        label->name = *value;

    } else {
        njs_mp_free(vm->mem_pool, label);

        if (njs_slow_path(ret == NJS_ERROR)) {
            njs_internal_error(vm, "lvlhsh insert failed");

            return NJS_ERROR;
        }

        njs_vm_log(vm, "Timer \"%V\" already exists.\n", &name);

        label = lhq.value;
    }

    label->time = njs_time();

    njs_set_undefined(&vm->retval);

    return NJS_OK;
}


static njs_int_t
njs_ext_console_time_end(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t unused)
{
    uint64_t            ns, ms;
    njs_int_t           ret;
    njs_str_t           name;
    njs_value_t         *value;
    njs_console_t       *console;
    njs_timelabel_t     *label;
    njs_lvlhsh_query_t  lhq;

    ns = njs_time();

    console = njs_vm_external(vm, njs_console_proto_id, njs_argument(args, 0));
    if (njs_slow_path(console == NULL)) {
        njs_type_error(vm, "external value is expected");
        return NJS_ERROR;
    }

    value = njs_arg(args, nargs, 1);

    if (njs_slow_path(!njs_is_string(value))) {
        if (njs_is_undefined(value)) {
            value = njs_value_arg(&njs_default_label);

        } else {
            ret = njs_value_to_string(vm, value, value);
            if (njs_slow_path(ret != NJS_OK)) {
                return ret;
            }
        }
    }

    njs_string_get(value, &name);

    lhq.key = name;
    lhq.key_hash = njs_djb_hash(name.start, name.length);
    lhq.pool = vm->mem_pool;
    lhq.proto = &njs_timelabel_hash_proto;

    ret = njs_lvlhsh_delete(&console->labels, &lhq);

    if (njs_fast_path(ret == NJS_OK)) {

        label = lhq.value;

        ns = ns - label->time;

        ms = ns / 1000000;
        ns = ns % 1000000;

        njs_vm_log(vm, "%V: %uL.%06uLms\n", &name, ms, ns);

        /* GC: release. */
        njs_mp_free(vm->mem_pool, label);

    } else {
        if (ret == NJS_ERROR) {
            njs_internal_error(vm, "lvlhsh delete failed");

            return NJS_ERROR;
        }

        njs_vm_log(vm, "Timer \"%V\" doesn’t exist.\n", &name);
    }

    njs_set_undefined(&vm->retval);

    return NJS_OK;
}


static njs_host_event_t
njs_console_set_timer(njs_external_ptr_t external, uint64_t delay,
    njs_vm_event_t vm_event)
{
    njs_ev_t            *ev;
    njs_vm_t            *vm;
    njs_int_t           ret;
    njs_console_t       *console;
    njs_lvlhsh_query_t  lhq;

    console = external;
    vm = console->vm;

    if (delay != 0) {
        njs_vm_err(vm, "njs_console_set_timer(): async timers unsupported\n");
        return NULL;
    }

    ev = njs_mp_alloc(vm->mem_pool, sizeof(njs_ev_t));
    if (njs_slow_path(ev == NULL)) {
        return NULL;
    }

    ev->vm_event = vm_event;

    lhq.key.start = (u_char *) &ev->vm_event;
    lhq.key.length = sizeof(njs_vm_event_t);
    lhq.key_hash = njs_djb_hash(lhq.key.start, lhq.key.length);

    lhq.replace = 0;
    lhq.value = ev;
    lhq.proto = &lvlhsh_proto;
    lhq.pool = vm->mem_pool;

    ret = njs_lvlhsh_insert(&console->events, &lhq);
    if (njs_slow_path(ret != NJS_OK)) {
        return NULL;
    }

    njs_queue_insert_tail(&console->posted_events, &ev->link);

    return (njs_host_event_t) ev;
}


static void
njs_console_clear_timer(njs_external_ptr_t external, njs_host_event_t event)
{
    njs_vm_t            *vm;
    njs_ev_t            *ev;
    njs_int_t           ret;
    njs_console_t       *console;
    njs_lvlhsh_query_t  lhq;

    ev = event;
    console = external;
    vm = console->vm;

    lhq.key.start = (u_char *) &ev->vm_event;
    lhq.key.length = sizeof(njs_vm_event_t);
    lhq.key_hash = njs_djb_hash(lhq.key.start, lhq.key.length);

    lhq.proto = &lvlhsh_proto;
    lhq.pool = vm->mem_pool;

    if (ev->link.prev != NULL) {
        njs_queue_remove(&ev->link);
    }

    ret = njs_lvlhsh_delete(&console->events, &lhq);
    if (ret != NJS_OK) {
        njs_vm_err(vm, "njs_lvlhsh_delete() failed\n");
    }

    njs_mp_free(vm->mem_pool, ev);
}


static void
njs_console_log(njs_vm_t *vm, njs_external_ptr_t external,
    njs_log_level_t level, const u_char *start, size_t length)
{
    if (level == NJS_LOG_LEVEL_ERROR) {
        njs_stderror("%*s", length, start);

    } else {
        njs_printf("%*s", length, start);
    }
}


static njs_int_t
njs_timelabel_hash_test(njs_lvlhsh_query_t *lhq, void *data)
{
    njs_timelabel_t  *label;
    njs_str_t        str;

    label = data;
    njs_string_get(&label->name, &str);

    if (njs_strstr_eq(&lhq->key, &str)) {
        return NJS_OK;
    }

    return NJS_DECLINED;
}


static njs_int_t
lvlhsh_key_test(njs_lvlhsh_query_t *lhq, void *data)
{
    njs_ev_t  *ev;

    ev = data;

    if (memcmp(&ev->vm_event, lhq->key.start, sizeof(njs_vm_event_t)) == 0) {
        return NJS_OK;
    }

    return NJS_DECLINED;
}


static void *
lvlhsh_pool_alloc(void *pool, size_t size)
{
    return njs_mp_align(pool, size, size);
}


static void
lvlhsh_pool_free(void *pool, void *p, size_t size)
{
    njs_mp_free(pool, p);
}
