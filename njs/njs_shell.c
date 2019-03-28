
/*
 * Copyright (C) Dmitry Volyntsev
 * Copyright (C) NGINX, Inc.
 */


#include <njs_core.h>
#include <njs_builtin.h>
#include <time.h>
#include <errno.h>
#include <string.h>
#include <fcntl.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/param.h>
#include <locale.h>

#include <stdio.h>
#include <readline.h>


typedef struct {
    char                    *file;
    size_t                  n_paths;
    char                    **paths;
    nxt_int_t               version;
    nxt_int_t               disassemble;
    nxt_int_t               interactive;
    nxt_int_t               sandbox;
    nxt_int_t               quiet;
} njs_opts_t;


typedef struct {
    size_t                  index;
    size_t                  length;
    nxt_array_t             *completions;
    nxt_array_t             *suffix_completions;
    nxt_lvlhsh_each_t       lhe;

    enum {
       NJS_COMPLETION_VAR = 0,
       NJS_COMPLETION_SUFFIX,
       NJS_COMPLETION_GLOBAL
    }                       phase;
} njs_completion_t;


typedef struct {
    njs_vm_event_t          vm_event;
    nxt_queue_link_t        link;
} njs_ev_t;


typedef struct {
    njs_vm_t                *vm;

    nxt_lvlhsh_t            events;  /* njs_ev_t * */
    nxt_queue_t             posted_events;

    uint64_t                time;

    njs_completion_t        completion;
} njs_console_t;


static nxt_int_t njs_get_options(njs_opts_t *opts, int argc, char **argv);
static nxt_int_t njs_console_init(njs_vm_t *vm, njs_console_t *console);
static nxt_int_t njs_externals_init(njs_vm_t *vm, njs_console_t *console);
static nxt_int_t njs_interactive_shell(njs_opts_t *opts,
    njs_vm_opt_t *vm_options);
static njs_vm_t *njs_create_vm(njs_opts_t *opts, njs_vm_opt_t *vm_options);
static nxt_int_t njs_process_file(njs_opts_t *opts, njs_vm_opt_t *vm_options);
static nxt_int_t njs_process_script(njs_console_t *console, njs_opts_t *opts,
    const nxt_str_t *script);
static nxt_int_t njs_editline_init(void);
static char **njs_completion_handler(const char *text, int start, int end);
static char *njs_completion_generator(const char *text, int state);

static njs_ret_t njs_ext_console_log(njs_vm_t *vm, njs_value_t *args,
    nxt_uint_t nargs, njs_index_t unused);
static njs_ret_t njs_ext_console_dump(njs_vm_t *vm, njs_value_t *args,
    nxt_uint_t nargs, njs_index_t unused);
static njs_ret_t njs_ext_console_help(njs_vm_t *vm, njs_value_t *args,
    nxt_uint_t nargs, njs_index_t unused);
static njs_ret_t njs_ext_console_time(njs_vm_t *vm, njs_value_t *args,
    nxt_uint_t nargs, njs_index_t unused);
static njs_ret_t njs_ext_console_time_end(njs_vm_t *vm, njs_value_t *args,
    nxt_uint_t nargs, njs_index_t unused);

static njs_host_event_t njs_console_set_timer(njs_external_ptr_t external,
    uint64_t delay, njs_vm_event_t vm_event);
static void njs_console_clear_timer(njs_external_ptr_t external,
    njs_host_event_t event);

static nxt_int_t lvlhsh_key_test(nxt_lvlhsh_query_t *lhq, void *data);
static void *lvlhsh_pool_alloc(void *pool, size_t size, nxt_uint_t nalloc);
static void lvlhsh_pool_free(void *pool, void *p, size_t size);


static njs_external_t  njs_ext_console[] = {

    { nxt_string("log"),
      NJS_EXTERN_METHOD,
      NULL,
      0,
      NULL,
      NULL,
      NULL,
      NULL,
      NULL,
      njs_ext_console_log,
      0 },

    { nxt_string("dump"),
      NJS_EXTERN_METHOD,
      NULL,
      0,
      NULL,
      NULL,
      NULL,
      NULL,
      NULL,
      njs_ext_console_dump,
      0 },

    { nxt_string("help"),
      NJS_EXTERN_METHOD,
      NULL,
      0,
      NULL,
      NULL,
      NULL,
      NULL,
      NULL,
      njs_ext_console_help,
      0 },

    { nxt_string("time"),
      NJS_EXTERN_METHOD,
      NULL,
      0,
      NULL,
      NULL,
      NULL,
      NULL,
      NULL,
      njs_ext_console_time,
      0 },

    { nxt_string("timeEnd"),
      NJS_EXTERN_METHOD,
      NULL,
      0,
      NULL,
      NULL,
      NULL,
      NULL,
      NULL,
      njs_ext_console_time_end,
      0 },
};

static njs_external_t  njs_externals[] = {

    { nxt_string("console"),
      NJS_EXTERN_OBJECT,
      njs_ext_console,
      nxt_nitems(njs_ext_console),
      NULL,
      NULL,
      NULL,
      NULL,
      NULL,
      NULL,
      0 },
};


static const nxt_lvlhsh_proto_t  lvlhsh_proto  nxt_aligned(64) = {
    NXT_LVLHSH_LARGE_SLAB,
    0,
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
    nxt_int_t     ret;
    njs_opts_t    opts;
    njs_vm_opt_t  vm_options;

    nxt_memzero(&opts, sizeof(njs_opts_t));
    opts.interactive = 1;

    ret = njs_get_options(&opts, argc, argv);
    if (ret != NXT_OK) {
        ret = (ret == NXT_DONE) ? NXT_OK : NXT_ERROR;
        goto done;
    }

    if (opts.version != 0) {
        nxt_printf("%s\n", NJS_VERSION);
        ret = NXT_OK;
        goto done;
    }

    nxt_memzero(&vm_options, sizeof(njs_vm_opt_t));

    if (!opts.quiet) {
        if (opts.file == NULL) {
            p = getcwd(path, sizeof(path));
            if (p == NULL) {
                nxt_error("getcwd() failed:%s\n", strerror(errno));
                ret = NXT_ERROR;
                goto done;
            }

            memcpy(path + strlen(path), "/shell", sizeof("/shell"));
            opts.file = path;
        }

        vm_options.file.start = (u_char *) opts.file;
        vm_options.file.length = strlen(opts.file);
    }

    vm_options.init = !opts.interactive;
    vm_options.accumulative = opts.interactive;
    vm_options.backtrace = 1;
    vm_options.sandbox = opts.sandbox;
    vm_options.ops = &njs_console_ops;
    vm_options.external = &njs_console;

    if (opts.interactive) {
        ret = njs_interactive_shell(&opts, &vm_options);

    } else {
        ret = njs_process_file(&opts, &vm_options);
    }

done:

    if (opts.paths != NULL) {
        free(opts.paths);
    }

    return (ret == NXT_OK) ? EXIT_SUCCESS : EXIT_FAILURE;
}


static nxt_int_t
njs_get_options(njs_opts_t *opts, int argc, char** argv)
{
    char     *p, **paths;
    nxt_int_t  i, ret;

    static const char  help[] =
        "Interactive njs shell.\n"
        "\n"
        "Options:\n"
        "  -d              print disassembled code.\n"
        "  -q              disable interactive introduction prompt.\n"
        "  -s              sandbox mode.\n"
        "  -p              set path prefix for modules.\n"
        "  -v              print njs version and exit.\n"
        "  <filename> | -  run code from a file or stdin.\n";

    ret = NXT_DONE;

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
            (void) write(STDIN_FILENO, help, nxt_length(help));
            return ret;

        case 'd':
            opts->disassemble = 1;
            break;

        case 'q':
            opts->quiet = 1;
            break;

        case 's':
            opts->sandbox = 1;
            break;

        case 'p':
            if (argv[++i] != NULL) {
                opts->n_paths++;
                paths = realloc(opts->paths, opts->n_paths * sizeof(char *));
                if (paths == NULL) {
                    nxt_error("failed to add path\n");
                    return NXT_ERROR;
                }

                opts->paths = paths;
                opts->paths[opts->n_paths - 1] = argv[i];
                break;
            }

            nxt_error("option \"-p\" requires directory name\n");
            return NXT_ERROR;

        case 'v':
        case 'V':
            opts->version = 1;
            break;

        default:
            nxt_error("Unknown argument: \"%s\" "
                      "try \"%s -h\" for available options\n", argv[i],
                      argv[0]);
            return NXT_ERROR;
        }
    }

    return NXT_OK;
}


static nxt_int_t
njs_console_init(njs_vm_t *vm, njs_console_t *console)
{
    console->vm = vm;

    nxt_lvlhsh_init(&console->events);
    nxt_queue_init(&console->posted_events);

    console->time = UINT64_MAX;

    console->completion.completions = njs_vm_completions(vm, NULL);
    if (console->completion.completions == NULL) {
        return NXT_ERROR;
    }

    return NXT_OK;
}


static nxt_int_t
njs_externals_init(njs_vm_t *vm, njs_console_t *console)
{
    nxt_uint_t          ret;
    njs_value_t         *value;
    const njs_extern_t  *proto;

    static const nxt_str_t name = nxt_string("console");

    proto = njs_vm_external_prototype(vm, &njs_externals[0]);
    if (proto == NULL) {
        nxt_error("failed to add console proto\n");
        return NXT_ERROR;
    }

    value = nxt_mp_zalloc(vm->mem_pool, sizeof(njs_opaque_value_t));
    if (value == NULL) {
        return NXT_ERROR;
    }

    ret = njs_vm_external_create(vm, value, proto, console);
    if (ret != NXT_OK) {
        return NXT_ERROR;
    }

    ret = njs_vm_external_bind(vm, &name, value);
    if (ret != NXT_OK) {
        return NXT_ERROR;
    }

    ret = njs_console_init(vm, console);
    if (ret != NXT_OK) {
        return NXT_ERROR;
    }

    return NXT_OK;
}


static nxt_int_t
njs_interactive_shell(njs_opts_t *opts, njs_vm_opt_t *vm_options)
{
    njs_vm_t   *vm;
    nxt_str_t  line;

    if (njs_editline_init() != NXT_OK) {
        nxt_error("failed to init completions\n");
        return NXT_ERROR;
    }

    vm = njs_create_vm(opts, vm_options);
    if (vm == NULL) {
        return NXT_ERROR;
    }

    if (!opts->quiet) {
        nxt_printf("interactive njs %s\n\n", NJS_VERSION);

        nxt_printf("v.<Tab> -> the properties and prototype methods of v.\n");
        nxt_printf("type console.help() for more information\n\n");
    }

    for ( ;; ) {
        line.start = (u_char *) readline(">> ");
        if (line.start == NULL) {
            break;
        }

        line.length = strlen((char *) line.start);
        if (line.length == 0) {
            continue;
        }

        add_history((char *) line.start);

        njs_process_script(vm_options->external, opts, &line);

        /* editline allocs a new buffer every time. */
        free(line.start);
    }

    return NXT_OK;
}


static nxt_int_t
njs_process_file(njs_opts_t *opts, njs_vm_opt_t *vm_options)
{
    int          fd;
    char         *file;
    u_char       buf[4096], *p, *end, *start;
    size_t       size;
    ssize_t      n;
    njs_vm_t     *vm;
    nxt_int_t    ret;
    nxt_str_t    script;
    struct stat  sb;

    file = opts->file;

    if (file[0] == '-' && file[1] == '\0') {
        fd = STDIN_FILENO;

    } else {
        fd = open(file, O_RDONLY);
        if (fd == -1) {
            nxt_error("failed to open file: '%s' (%s)\n",
                      file, strerror(errno));
            return NXT_ERROR;
        }
    }

    if (fstat(fd, &sb) == -1) {
        nxt_error("fstat(%d) failed while reading '%s' (%s)\n",
                  fd, file, strerror(errno));
        ret = NXT_ERROR;
        goto close_fd;
    }

    size = sizeof(buf);

    if (S_ISREG(sb.st_mode) && sb.st_size) {
        size = sb.st_size;
    }

    script.length = 0;
    script.start = realloc(NULL, size);
    if (script.start == NULL) {
        nxt_error("alloc failed while reading '%s'\n", file);
        ret = NXT_ERROR;
        goto done;
    }

    p = script.start;
    end = p + size;

    for ( ;; ) {
        n = read(fd, buf, sizeof(buf));

        if (n == 0) {
            break;
        }

        if (n < 0) {
            nxt_error("failed to read file: '%s' (%s)\n",
                      file, strerror(errno));
            ret = NXT_ERROR;
            goto done;
        }

        if (p + n > end) {
            size *= 2;

            start = realloc(script.start, size);
            if (start == NULL) {
                nxt_error("alloc failed while reading '%s'\n", file);
                ret = NXT_ERROR;
                goto done;
            }

            script.start = start;

            p = script.start + script.length;
            end = script.start + size;
        }

        memcpy(p, buf, n);

        p += n;
        script.length += n;
    }

    vm = njs_create_vm(opts, vm_options);
    if (vm == NULL) {
        ret = NXT_ERROR;
        goto done;
    }

    ret = njs_process_script(vm_options->external, opts, &script);
    if (ret != NXT_OK) {
        ret = NXT_ERROR;
        goto done;
    }

    ret = NXT_OK;

done:

    if (script.start != NULL) {
        free(script.start);
    }

close_fd:

    if (fd != STDIN_FILENO) {
        close(fd);
    }

    return ret;
}


static njs_vm_t *
njs_create_vm(njs_opts_t *opts, njs_vm_opt_t *vm_options)
{
    char        *p, *start;
    njs_vm_t    *vm;
    nxt_int_t   ret;
    nxt_str_t   path;
    nxt_uint_t  i;

    vm = njs_vm_create(vm_options);
    if (vm == NULL) {
        nxt_error("failed to create vm\n");
        return NULL;
    }

    if (njs_externals_init(vm, vm_options->external) != NXT_OK) {
        nxt_error("failed to add external protos\n");
        return NULL;
    }

    for (i = 0; i < opts->n_paths; i++) {
        path.start = (u_char *) opts->paths[i];
        path.length = strlen(opts->paths[i]);

        ret = njs_vm_add_path(vm, &path);
        if (ret != NXT_OK) {
            nxt_error("failed to add path\n");
            return NULL;
        }
    }

    start = getenv("NJS_PATH");
    if (start == NULL) {
        return vm;
    }

    for ( ;; ) {
        p = strchr(start, ':');

        path.start = (u_char *) start;
        path.length = (p != NULL) ? (size_t) (p - start) : strlen(start);

        ret = njs_vm_add_path(vm, &path);
        if (ret != NXT_OK) {
            nxt_error("failed to add path\n");
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
njs_output(njs_vm_t *vm, njs_opts_t *opts, njs_ret_t ret)
{
    nxt_str_t  out;

    if (njs_vm_retval_dump(vm, &out, 1) != NXT_OK) {
        out = nxt_string_value("failed to get retval from VM");
        ret = NJS_ERROR;
    }

    if (ret != NJS_OK) {
        nxt_error("%V\n", &out);

    } else if (opts->interactive) {
        nxt_printf("%V\n", &out);
    }
}


static nxt_int_t
njs_process_events(njs_console_t *console, njs_opts_t *opts)
{
    njs_ev_t          *ev;
    nxt_queue_t       *events;
    nxt_queue_link_t  *link;

    events = &console->posted_events;

    for ( ;; ) {
        link = nxt_queue_first(events);

        if (link == nxt_queue_tail(events)) {
            break;
        }

        ev = nxt_queue_link_data(link, njs_ev_t, link);

        nxt_queue_remove(&ev->link);
        ev->link.prev = NULL;
        ev->link.next = NULL;

        njs_vm_post_event(console->vm, ev->vm_event, NULL, 0);
    }

    return NXT_OK;
}


static nxt_int_t
njs_process_script(njs_console_t *console, njs_opts_t *opts,
    const nxt_str_t *script)
{
    u_char     *start;
    njs_vm_t   *vm;
    nxt_int_t  ret;

    vm = console->vm;
    start = script->start;

    ret = njs_vm_compile(vm, &start, start + script->length);

    if (ret == NXT_OK) {
        if (opts->disassemble) {
            njs_disassembler(vm);
            nxt_printf("\n");
        }

        ret = njs_vm_start(vm);
    }

    njs_output(vm, opts, ret);

    for ( ;; ) {
        if (!njs_vm_pending(vm)) {
            break;
        }

        ret = njs_process_events(console, opts);
        if (nxt_slow_path(ret != NXT_OK)) {
            nxt_error("njs_process_events() failed\n");
            ret = NJS_ERROR;
            break;
        }

        if (njs_vm_waiting(vm) && !njs_vm_posted(vm)) {
            /*TODO: async events. */

            nxt_error("njs_process_script(): async events unsupported\n");
            ret = NJS_ERROR;
            break;
        }

        ret = njs_vm_run(vm);

        if (ret == NJS_ERROR) {
            njs_output(vm, opts, ret);
        }
    }

    return ret;
}


static nxt_int_t
njs_editline_init(void)
{
    rl_completion_append_character = '\0';
    rl_attempted_completion_function = njs_completion_handler;
    rl_basic_word_break_characters = (char *) " \t\n\"\\'`@$><=;,|&{(";

    setlocale(LC_ALL, "");

    return NXT_OK;
}


static char **
njs_completion_handler(const char *text, int start, int end)
{
    rl_attempted_completion_over = 1;

    return rl_completion_matches(text, njs_completion_generator);
}


/* editline frees the buffer every time. */
#define njs_editline(s) strndup((char *) (s)->start, (s)->length)

#define njs_completion(c, i) &(((nxt_str_t *) (c)->start)[i])

#define njs_next_phase(c)                                                   \
    (c)->index = 0;                                                         \
    (c)->phase++;                                                           \
    goto next;

static char *
njs_completion_generator(const char *text, int state)
{
    char              *completion;
    size_t            len;
    nxt_str_t         expression, *suffix;
    const char        *p;
    njs_vm_t          *vm;
    njs_variable_t    *var;
    njs_completion_t  *cmpl;

    vm = njs_console.vm;
    cmpl = &njs_console.completion;

    if (state == 0) {
        cmpl->phase = 0;
        cmpl->index = 0;
        cmpl->length = strlen(text);
        cmpl->suffix_completions = NULL;

        nxt_lvlhsh_each_init(&cmpl->lhe, &njs_variables_hash_proto);
    }

next:

    switch (cmpl->phase) {
    case NJS_COMPLETION_VAR:
        if (vm->parser == NULL) {
            njs_next_phase(cmpl);
        }

        for ( ;; ) {
            var = nxt_lvlhsh_each(&vm->parser->scope->variables,
                                  &cmpl->lhe);

            if (var == NULL) {
                break;
            }

            if (var->name.length < cmpl->length) {
                continue;
            }

            if (nxt_strncmp(text, var->name.start, cmpl->length) == 0) {
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

            if (len != 0 && nxt_strncmp(suffix->start, p,
                                        nxt_min(len, suffix->length)) != 0)
            {
                continue;
            }

            len = suffix->length + (p - text) + 1;
            completion = malloc(len);
            if (completion == NULL) {
                return NULL;
            }

            nxt_sprintf((u_char *) completion, (u_char *) completion + len,
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

            if (nxt_strncmp(text, suffix->start, cmpl->length) == 0) {
                return njs_editline(suffix);
            }
        }
    }

    return NULL;
}


static njs_ret_t
njs_ext_console_log(njs_vm_t *vm, njs_value_t *args, nxt_uint_t nargs,
    njs_index_t unused)
{
    nxt_str_t   msg;
    nxt_uint_t  n;

    n = 1;

    while (n < nargs) {
        if (njs_vm_value_dump(vm, &msg, njs_argument(args, n), 0)
            == NJS_ERROR)
        {
            return NJS_ERROR;
        }

        nxt_printf("%s%V", (n != 1) ? " " : "", &msg);

        n++;
    }

    if (nargs > 1) {
        nxt_printf("\n");
    }

    vm->retval = njs_value_undefined;

    return NJS_OK;
}


static njs_ret_t
njs_ext_console_dump(njs_vm_t *vm, njs_value_t *args, nxt_uint_t nargs,
    njs_index_t unused)
{
    nxt_str_t   msg;
    nxt_uint_t  n;

    n = 1;

    while (n < nargs) {
        if (njs_vm_value_dump(vm, &msg, njs_argument(args, n), 1)
            == NJS_ERROR)
        {
            return NJS_ERROR;
        }

        nxt_printf("%s%V", (n != 1) ? " " : "", &msg);

        n++;
    }

    if (nargs > 1) {
        nxt_printf("\n");
    }

    vm->retval = njs_value_undefined;

    return NJS_OK;
}


static njs_ret_t
njs_ext_console_help(njs_vm_t *vm, njs_value_t *args, nxt_uint_t nargs,
    njs_index_t unused)
{
    const njs_object_init_t  *obj, **objpp;

    nxt_printf("VM built-in objects:\n");

    for (objpp = njs_constructor_init; *objpp != NULL; objpp++) {
        obj = *objpp;

        nxt_printf("  %V\n", &obj->name);
    }

    for (objpp = njs_object_init; *objpp != NULL; objpp++) {
        obj = *objpp;

        nxt_printf("  %V\n", &obj->name);
    }

    nxt_printf("\nEmbedded objects:\n");
    nxt_printf("  console\n");

    nxt_printf("\n");

    vm->retval = njs_value_undefined;

    return NJS_OK;
}


static njs_ret_t
njs_ext_console_time(njs_vm_t *vm, njs_value_t *args, nxt_uint_t nargs,
    njs_index_t unused)
{
    njs_console_t  *console;

    if (!njs_value_is_undefined(njs_arg(args, nargs, 1))) {
        njs_vm_error(vm, "labels not implemented");
        return NJS_ERROR;
    }

    console = njs_vm_external(vm, njs_arg(args, nargs, 0));
    if (nxt_slow_path(console == NULL)) {
        return NJS_ERROR;
    }

    console->time = nxt_time();

    vm->retval = njs_value_undefined;

    return NJS_OK;
}


static njs_ret_t
njs_ext_console_time_end(njs_vm_t *vm, njs_value_t *args, nxt_uint_t nargs,
    njs_index_t unused)
{
    uint64_t       ns, ms;
    njs_console_t  *console;

    ns = nxt_time();

    if (!njs_value_is_undefined(njs_arg(args, nargs, 1))) {
        njs_vm_error(vm, "labels not implemented");
        return NJS_ERROR;
    }

    console = njs_vm_external(vm, njs_arg(args, nargs, 0));
    if (nxt_slow_path(console == NULL)) {
        return NJS_ERROR;
    }

    if (nxt_fast_path(console->time != UINT64_MAX)) {

        ns = ns - console->time;

        ms = ns / 1000000;
        ns = ns % 1000000;

        nxt_printf("default: %uL.%06uLms\n", ms, ns);

        console->time = UINT64_MAX;

    } else {
        nxt_printf("Timer \"default\" doesn’t exist.\n");
    }

    vm->retval = njs_value_undefined;

    return NJS_OK;
}


static njs_host_event_t
njs_console_set_timer(njs_external_ptr_t external, uint64_t delay,
    njs_vm_event_t vm_event)
{
    njs_ev_t            *ev;
    njs_vm_t            *vm;
    nxt_int_t           ret;
    njs_console_t       *console;
    nxt_lvlhsh_query_t  lhq;

    if (delay != 0) {
        nxt_error("njs_console_set_timer(): async timers unsupported\n");
        return NULL;
    }

    console = external;
    vm = console->vm;

    ev = nxt_mp_alloc(vm->mem_pool, sizeof(njs_ev_t));
    if (nxt_slow_path(ev == NULL)) {
        return NULL;
    }

    ev->vm_event = vm_event;

    lhq.key.start = (u_char *) &ev->vm_event;
    lhq.key.length = sizeof(njs_vm_event_t);
    lhq.key_hash = nxt_djb_hash(lhq.key.start, lhq.key.length);

    lhq.replace = 0;
    lhq.value = ev;
    lhq.proto = &lvlhsh_proto;
    lhq.pool = vm->mem_pool;

    ret = nxt_lvlhsh_insert(&console->events, &lhq);
    if (nxt_slow_path(ret != NXT_OK)) {
        return NULL;
    }

    nxt_queue_insert_tail(&console->posted_events, &ev->link);

    return (njs_host_event_t) ev;
}


static void
njs_console_clear_timer(njs_external_ptr_t external, njs_host_event_t event)
{
    njs_vm_t            *vm;
    njs_ev_t            *ev;
    nxt_int_t           ret;
    njs_console_t       *console;
    nxt_lvlhsh_query_t  lhq;

    ev = event;
    console = external;
    vm = console->vm;

    lhq.key.start = (u_char *) &ev->vm_event;
    lhq.key.length = sizeof(njs_vm_event_t);
    lhq.key_hash = nxt_djb_hash(lhq.key.start, lhq.key.length);

    lhq.proto = &lvlhsh_proto;
    lhq.pool = vm->mem_pool;

    if (ev->link.prev != NULL) {
        nxt_queue_remove(&ev->link);
    }

    ret = nxt_lvlhsh_delete(&console->events, &lhq);
    if (ret != NXT_OK) {
        nxt_error("nxt_lvlhsh_delete() failed\n");
    }

    nxt_mp_free(vm->mem_pool, ev);
}


static nxt_int_t
lvlhsh_key_test(nxt_lvlhsh_query_t *lhq, void *data)
{
    njs_ev_t  *ev;

    ev = data;

    if (memcmp(&ev->vm_event, lhq->key.start, sizeof(njs_vm_event_t)) == 0) {
        return NXT_OK;
    }

    return NXT_DECLINED;
}


static void *
lvlhsh_pool_alloc(void *pool, size_t size, nxt_uint_t nalloc)
{
    return nxt_mp_align(pool, size, size);
}


static void
lvlhsh_pool_free(void *pool, void *p, size_t size)
{
    nxt_mp_free(pool, p);
}
