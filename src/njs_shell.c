
/*
 * Copyright (C) Dmitry Volyntsev
 * Copyright (C) NGINX, Inc.
 */


#include <njs_main.h>
#include <locale.h>
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


typedef struct {
    uint8_t                 disassemble;
    uint8_t                 interactive;
    uint8_t                 module;
    uint8_t                 quiet;
    uint8_t                 sandbox;
    uint8_t                 safe;
    uint8_t                 version;

    char                    *file;
    char                    *command;
    size_t                  n_paths;
    char                    **paths;
} njs_opts_t;


typedef struct {
    size_t                  index;
    size_t                  length;
    njs_arr_t               *completions;
    njs_arr_t               *suffix_completions;
    njs_lvlhsh_each_t       lhe;

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
    njs_vm_t                *vm;

    njs_lvlhsh_t            events;  /* njs_ev_t * */
    njs_queue_t             posted_events;

    uint64_t                time;

    njs_completion_t        completion;
} njs_console_t;


static njs_int_t njs_get_options(njs_opts_t *opts, int argc, char **argv);
static njs_int_t njs_console_init(njs_vm_t *vm, njs_console_t *console);
static njs_int_t njs_externals_init(njs_vm_t *vm, njs_console_t *console);
static njs_int_t njs_interactive_shell(njs_opts_t *opts,
    njs_vm_opt_t *vm_options);
static njs_vm_t *njs_create_vm(njs_opts_t *opts, njs_vm_opt_t *vm_options);
static njs_int_t njs_process_file(njs_opts_t *opts, njs_vm_opt_t *vm_options);
static njs_int_t njs_process_script(njs_console_t *console,
    const njs_str_t *script);
static njs_int_t njs_editline_init(void);
static char **njs_completion_handler(const char *text, int start, int end);
static char *njs_completion_generator(const char *text, int state);

static njs_int_t njs_ext_console_log(njs_vm_t *vm, njs_value_t *args,
    njs_uint_t nargs, njs_index_t unused);
static njs_int_t njs_ext_console_dump(njs_vm_t *vm, njs_value_t *args,
    njs_uint_t nargs, njs_index_t unused);
static njs_int_t njs_ext_console_help(njs_vm_t *vm, njs_value_t *args,
    njs_uint_t nargs, njs_index_t unused);
static njs_int_t njs_ext_console_time(njs_vm_t *vm, njs_value_t *args,
    njs_uint_t nargs, njs_index_t unused);
static njs_int_t njs_ext_console_time_end(njs_vm_t *vm, njs_value_t *args,
    njs_uint_t nargs, njs_index_t unused);

static njs_host_event_t njs_console_set_timer(njs_external_ptr_t external,
    uint64_t delay, njs_vm_event_t vm_event);
static void njs_console_clear_timer(njs_external_ptr_t external,
    njs_host_event_t event);

static njs_int_t lvlhsh_key_test(njs_lvlhsh_query_t *lhq, void *data);
static void *lvlhsh_pool_alloc(void *pool, size_t size);
static void lvlhsh_pool_free(void *pool, void *p, size_t size);


static njs_external_t  njs_ext_console[] = {

    { njs_str("log"),
      NJS_EXTERN_METHOD,
      NULL,
      0,
      NULL,
      NULL,
      NULL,
      NULL,
      njs_ext_console_log,
      0 },

    { njs_str("dump"),
      NJS_EXTERN_METHOD,
      NULL,
      0,
      NULL,
      NULL,
      NULL,
      NULL,
      njs_ext_console_dump,
      0 },

    { njs_str("help"),
      NJS_EXTERN_METHOD,
      NULL,
      0,
      NULL,
      NULL,
      NULL,
      NULL,
      njs_ext_console_help,
      0 },

    { njs_str("time"),
      NJS_EXTERN_METHOD,
      NULL,
      0,
      NULL,
      NULL,
      NULL,
      NULL,
      njs_ext_console_time,
      0 },

    { njs_str("timeEnd"),
      NJS_EXTERN_METHOD,
      NULL,
      0,
      NULL,
      NULL,
      NULL,
      NULL,
      njs_ext_console_time_end,
      0 },
};

static njs_external_t  njs_externals[] = {

    { njs_str("console"),
      NJS_EXTERN_OBJECT,
      njs_ext_console,
      njs_nitems(njs_ext_console),
      NULL,
      NULL,
      NULL,
      NULL,
      NULL,
      0 },
};


static const njs_lvlhsh_proto_t  lvlhsh_proto  njs_aligned(64) = {
    NJS_LVLHSH_LARGE_SLAB,
    lvlhsh_key_test,
    lvlhsh_pool_alloc,
    lvlhsh_pool_free,
};


static njs_vm_ops_t njs_console_ops = {
    njs_console_set_timer,
    njs_console_clear_timer
};


static njs_console_t  njs_console;


int
main(int argc, char **argv)
{
    char          path[MAXPATHLEN], *p;
    njs_vm_t      *vm;
    njs_int_t     ret;
    njs_opts_t    opts;
    njs_str_t     command;
    njs_vm_opt_t  vm_options;

    njs_memzero(&opts, sizeof(njs_opts_t));
    opts.interactive = 1;

    ret = njs_get_options(&opts, argc, argv);
    if (ret != NJS_OK) {
        ret = (ret == NJS_DONE) ? NJS_OK : NJS_ERROR;
        goto done;
    }

    if (opts.version != 0) {
        njs_printf("%s\n", NJS_VERSION);
        ret = NJS_OK;
        goto done;
    }

    njs_memzero(&vm_options, sizeof(njs_vm_opt_t));

    if (opts.file == NULL) {
        p = getcwd(path, sizeof(path));
        if (p == NULL) {
            njs_stderror("getcwd() failed:%s\n", strerror(errno));
            ret = NJS_ERROR;
            goto done;
        }

        if (opts.command == NULL) {
            memcpy(path + njs_strlen(path), "/shell", sizeof("/shell"));

        } else {
            memcpy(path + njs_strlen(path), "/string", sizeof("/string"));
        }

        opts.file = path;
    }

    vm_options.file.start = (u_char *) opts.file;
    vm_options.file.length = njs_strlen(opts.file);

    vm_options.init = !opts.interactive;
    vm_options.accumulative = opts.interactive;
    vm_options.disassemble = opts.disassemble;
    vm_options.backtrace = 1;
    vm_options.quiet = opts.quiet;
    vm_options.sandbox = opts.sandbox;
    vm_options.unsafe = !opts.safe;
    vm_options.module = opts.module;

    vm_options.ops = &njs_console_ops;
    vm_options.external = &njs_console;
    vm_options.argv = argv;
    vm_options.argc = argc;

    if (opts.interactive) {
        ret = njs_interactive_shell(&opts, &vm_options);

    } else if (opts.command) {
        vm = njs_create_vm(&opts, &vm_options);
        if (vm != NULL) {
            command.start = (u_char *) opts.command;
            command.length = njs_strlen(opts.command);
            ret = njs_process_script(vm_options.external, &command);
        }

    } else {
        ret = njs_process_file(&opts, &vm_options);
    }

done:

    if (opts.paths != NULL) {
        free(opts.paths);
    }

    return (ret == NJS_OK) ? EXIT_SUCCESS : EXIT_FAILURE;
}


static njs_int_t
njs_get_options(njs_opts_t *opts, int argc, char **argv)
{
    char       *p, **paths;
    njs_int_t  i, ret;

    static const char  help[] =
        "Interactive njs shell.\n"
        "\n"
        "Options:\n"
        "  -c                specify the command to execute.\n"
        "  -d                print disassembled code.\n"
        "  -p                set path prefix for modules.\n"
        "  -q                disable interactive introduction prompt.\n"
        "  -s                sandbox mode.\n"
        "  -t script|module  source code type (script is default).\n"
        "  -v                print njs version and exit.\n"
        "  -u                disable \"unsafe\" mode.\n"
        "  <filename> | -    run code from a file or stdin.\n";

    ret = NJS_DONE;

    for (i = 1; i < argc; i++) {

        p = argv[i];

        if (p[0] != '-' || (p[0] == '-' && p[1] == '\0')) {
            opts->interactive = 0;
            opts->file = argv[i];
            continue;
        }

        p++;

        switch (*p) {
        case '?':
        case 'h':
            (void) write(STDOUT_FILENO, help, njs_length(help));
            return ret;

        case 'c':
            opts->interactive = 0;

            if (++i < argc) {
                opts->command = argv[i];
                break;
            }

            njs_stderror("option \"-c\" requires argument\n");
            return NJS_ERROR;

        case 'd':
            opts->disassemble = 1;
            break;

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

    return NJS_OK;
}


static njs_int_t
njs_console_init(njs_vm_t *vm, njs_console_t *console)
{
    console->vm = vm;

    njs_lvlhsh_init(&console->events);
    njs_queue_init(&console->posted_events);

    console->time = UINT64_MAX;

    console->completion.completions = njs_vm_completions(vm, NULL);
    if (console->completion.completions == NULL) {
        return NJS_ERROR;
    }

    return NJS_OK;
}


static njs_int_t
njs_externals_init(njs_vm_t *vm, njs_console_t *console)
{
    njs_uint_t          ret;
    njs_value_t         *value;
    const njs_extern_t  *proto;

    static const njs_str_t name = njs_str("console");

    proto = njs_vm_external_prototype(vm, &njs_externals[0]);
    if (proto == NULL) {
        njs_stderror("failed to add console proto\n");
        return NJS_ERROR;
    }

    value = njs_mp_zalloc(vm->mem_pool, sizeof(njs_opaque_value_t));
    if (value == NULL) {
        return NJS_ERROR;
    }

    ret = njs_vm_external_create(vm, value, proto, console);
    if (ret != NJS_OK) {
        return NJS_ERROR;
    }

    ret = njs_vm_external_bind(vm, &name, value);
    if (ret != NJS_OK) {
        return NJS_ERROR;
    }

    ret = njs_console_init(vm, console);
    if (ret != NJS_OK) {
        return NJS_ERROR;
    }

    return NJS_OK;
}


static njs_int_t
njs_interactive_shell(njs_opts_t *opts, njs_vm_opt_t *vm_options)
{
    njs_vm_t   *vm;
    njs_str_t  line;

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

        njs_printf("v.<Tab> -> the properties and prototype methods of v.\n");
        njs_printf("type console.help() for more information\n\n");
    }

    for ( ;; ) {
        line.start = (u_char *) readline(">> ");
        if (line.start == NULL) {
            break;
        }

        line.length = njs_strlen(line.start);
        if (line.length == 0) {
            continue;
        }

        add_history((char *) line.start);

        njs_process_script(vm_options->external, &line);

        /* editline allocs a new buffer every time. */
        free(line.start);
    }

    return NJS_OK;
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

    ret = njs_process_script(vm_options->external, &script);
    if (ret != NJS_OK) {
        ret = NJS_ERROR;
        goto done;
    }

    ret = NJS_OK;

done:

    if (source.start != NULL) {
        free(source.start);
    }

close_fd:

    if (fd != STDIN_FILENO) {
        (void) close(fd);
    }

    return ret;
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

    if (njs_externals_init(vm, vm_options->external) != NJS_OK) {
        njs_stderror("failed to add external protos\n");
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
njs_output(njs_vm_t *vm, njs_int_t ret)
{
    njs_str_t  out;

    if (njs_vm_retval_dump(vm, &out, 1) != NJS_OK) {
        out = njs_str_value("failed to get retval from VM");
        ret = NJS_ERROR;
    }

    if (ret != NJS_OK) {
        njs_stderror("%V\n", &out);

    } else if (vm->options.accumulative) {
        njs_print(out.start, out.length);
        njs_printf("\n");
    }
}


static njs_int_t
njs_process_events(njs_console_t *console)
{
    njs_ev_t          *ev;
    njs_queue_t       *events;
    njs_queue_link_t  *link;

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
njs_process_script(njs_console_t *console, const njs_str_t *script)
{
    u_char     *start;
    njs_vm_t   *vm;
    njs_int_t  ret;

    vm = console->vm;
    start = script->start;

    ret = njs_vm_compile(vm, &start, start + script->length);

    if (ret == NJS_OK) {
        ret = njs_vm_start(vm);
    }

    njs_output(vm, ret);

    for ( ;; ) {
        if (!njs_vm_pending(vm)) {
            break;
        }

        ret = njs_process_events(console);
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
            njs_output(vm, ret);
        }
    }

    return ret;
}


static njs_int_t
njs_editline_init(void)
{
    rl_completion_append_character = '\0';
    rl_attempted_completion_function = njs_completion_handler;
    rl_basic_word_break_characters = (char *) " \t\n\"\\'`@$><=;,|&{(";

    setlocale(LC_ALL, "");

    return NJS_OK;
}


static char **
njs_completion_handler(const char *text, int start, int end)
{
    rl_attempted_completion_over = 1;

    return rl_completion_matches(text, njs_completion_generator);
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
    char              *completion;
    size_t            len;
    njs_str_t         expression, *suffix;
    const char        *p;
    njs_vm_t          *vm;
    njs_variable_t    *var;
    njs_completion_t  *cmpl;

    vm = njs_console.vm;
    cmpl = &njs_console.completion;

    if (state == 0) {
        cmpl->phase = 0;
        cmpl->index = 0;
        cmpl->length = njs_strlen(text);
        cmpl->suffix_completions = NULL;

        njs_lvlhsh_each_init(&cmpl->lhe, &njs_variables_hash_proto);
    }

next:

    switch (cmpl->phase) {
    case NJS_COMPLETION_VAR:
        if (vm->parser == NULL) {
            njs_next_phase(cmpl);
        }

        for ( ;; ) {
            var = njs_lvlhsh_each(&vm->parser->scope->variables,
                                  &cmpl->lhe);

            if (var == NULL) {
                break;
            }

            if (var->name.length < cmpl->length) {
                continue;
            }

            if (njs_strncmp(text, var->name.start, cmpl->length) == 0) {
                return njs_editline(&var->name);
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


static njs_int_t
njs_ext_console_log(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t unused)
{
    njs_str_t   msg;
    njs_uint_t  n;

    n = 1;

    while (n < nargs) {
        if (njs_vm_value_dump(vm, &msg, njs_argument(args, n), 1, 0)
            == NJS_ERROR)
        {
            return NJS_ERROR;
        }

        njs_printf("%s", (n != 1) ? " " : "");
        njs_print(msg.start, msg.length);

        n++;
    }

    if (nargs > 1) {
        njs_printf("\n");
    }

    njs_set_undefined(&vm->retval);

    return NJS_OK;
}


static njs_int_t
njs_ext_console_dump(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t unused)
{
    njs_str_t   msg;
    njs_uint_t  n;

    n = 1;

    while (n < nargs) {
        if (njs_vm_value_dump(vm, &msg, njs_argument(args, n), 1, 1)
            == NJS_ERROR)
        {
            return NJS_ERROR;
        }

        njs_printf("%s", (n != 1) ? " " : "");
        njs_print(msg.start, msg.length);

        n++;
    }

    if (nargs > 1) {
        njs_printf("\n");
    }

    njs_set_undefined(&vm->retval);

    return NJS_OK;
}


static njs_int_t
njs_ext_console_help(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t unused)
{
    const njs_object_init_t  *obj, **objpp;

    njs_printf("VM built-in objects:\n");

    for (objpp = njs_constructor_init; *objpp != NULL; objpp++) {
        obj = *objpp;

        njs_printf("  %V\n", &obj->name);
    }

    for (objpp = njs_object_init; *objpp != NULL; objpp++) {
        obj = *objpp;

        njs_printf("  %V\n", &obj->name);
    }

    njs_printf("\nEmbedded objects:\n");
    njs_printf("  console\n");

    njs_printf("\n");

    njs_set_undefined(&vm->retval);

    return NJS_OK;
}


static njs_int_t
njs_ext_console_time(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t unused)
{
    njs_console_t  *console;

    if (!njs_value_is_undefined(njs_arg(args, nargs, 1))) {
        njs_vm_error(vm, "labels not implemented");
        return NJS_ERROR;
    }

    console = njs_vm_external(vm, njs_arg(args, nargs, 0));
    if (njs_slow_path(console == NULL)) {
        return NJS_ERROR;
    }

    console->time = njs_time();

    njs_set_undefined(&vm->retval);

    return NJS_OK;
}


static njs_int_t
njs_ext_console_time_end(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t unused)
{
    uint64_t       ns, ms;
    njs_console_t  *console;

    ns = njs_time();

    if (!njs_value_is_undefined(njs_arg(args, nargs, 1))) {
        njs_vm_error(vm, "labels not implemented");
        return NJS_ERROR;
    }

    console = njs_vm_external(vm, njs_arg(args, nargs, 0));
    if (njs_slow_path(console == NULL)) {
        return NJS_ERROR;
    }

    if (njs_fast_path(console->time != UINT64_MAX)) {

        ns = ns - console->time;

        ms = ns / 1000000;
        ns = ns % 1000000;

        njs_printf("default: %uL.%06uLms\n", ms, ns);

        console->time = UINT64_MAX;

    } else {
        njs_printf("Timer \"default\" doesn’t exist.\n");
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

    if (delay != 0) {
        njs_stderror("njs_console_set_timer(): async timers unsupported\n");
        return NULL;
    }

    console = external;
    vm = console->vm;

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
        njs_stderror("njs_lvlhsh_delete() failed\n");
    }

    njs_mp_free(vm->mem_pool, ev);
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
