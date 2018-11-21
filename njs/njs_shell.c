
/*
 * Copyright (C) Dmitry Volyntsev
 * Copyright (C) NGINX, Inc.
 */


#include <njs_core.h>
#include <njs_builtin.h>
#include <time.h>
#include <errno.h>
#include <string.h>
#include <stdio.h>
#include <fcntl.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <locale.h>

#include <readline.h>


typedef enum {
    NJS_COMPLETION_VAR = 0,
    NJS_COMPLETION_SUFFIX,
    NJS_COMPLETION_GLOBAL
} njs_completion_phase_t;


typedef struct {
    char                    *file;
    nxt_int_t               version;
    nxt_int_t               disassemble;
    nxt_int_t               interactive;
    nxt_int_t               sandbox;
    nxt_int_t               quiet;
} njs_opts_t;


typedef struct {
    size_t                  index;
    size_t                  length;
    njs_vm_t                *vm;
    nxt_array_t             *completions;
    nxt_array_t             *suffix_completions;
    nxt_lvlhsh_each_t       lhe;
    njs_completion_phase_t  phase;
} njs_completion_t;


static nxt_int_t njs_get_options(njs_opts_t *opts, int argc, char **argv);
static nxt_int_t njs_externals_init(njs_vm_t *vm);
static nxt_int_t njs_interactive_shell(njs_opts_t *opts,
    njs_vm_opt_t *vm_options);
static nxt_int_t njs_process_file(njs_opts_t *opts, njs_vm_opt_t *vm_options);
static nxt_int_t njs_process_script(njs_vm_t *vm, njs_opts_t *opts,
    const nxt_str_t *script, nxt_str_t *out);
static nxt_int_t njs_editline_init(njs_vm_t *vm);
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


static njs_completion_t  njs_completion;


static struct timeval njs_console_time;


int
main(int argc, char **argv)
{
    nxt_int_t     ret;
    njs_opts_t    opts;
    njs_vm_opt_t  vm_options;

    nxt_memzero(&opts, sizeof(njs_opts_t));
    opts.interactive = 1;

    ret = njs_get_options(&opts, argc, argv);
    if (ret != NXT_OK) {
        return (ret == NXT_DONE) ? EXIT_SUCCESS : EXIT_FAILURE;
    }

    if (opts.version != 0) {
        printf("%s\n", NJS_VERSION);
        return EXIT_SUCCESS;
    }

    nxt_memzero(&vm_options, sizeof(njs_vm_opt_t));

    vm_options.init = !opts.interactive;
    vm_options.accumulative = opts.interactive;
    vm_options.backtrace = 1;
    vm_options.sandbox = opts.sandbox;

    if (opts.interactive) {
        ret = njs_interactive_shell(&opts, &vm_options);

    } else {
        ret = njs_process_file(&opts, &vm_options);
    }

    return (ret == NXT_OK) ? EXIT_SUCCESS : EXIT_FAILURE;
}


static nxt_int_t
njs_get_options(njs_opts_t *opts, int argc, char** argv)
{
    char     *p;
    nxt_int_t  i, ret;

    static const char  help[] =
        "Interactive njs shell.\n"
        "\n"
        "Options:\n"
        "  -d              print disassembled code.\n"
        "  -q              disable interactive introduction prompt.\n"
        "  -s              sandbox mode.\n"
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

        case 'v':
        case 'V':
            opts->version = 1;
            break;

        default:
            fprintf(stderr, "Unknown argument: \"%s\" "
                    "try \"%s -h\" for available options\n", argv[i], argv[0]);
            return NXT_ERROR;
        }
    }

    return NXT_OK;
}


static nxt_int_t
njs_externals_init(njs_vm_t *vm)
{
    nxt_uint_t          ret;
    njs_value_t         *value;
    const njs_extern_t  *proto;

    static const nxt_str_t name = nxt_string("console");

    proto = njs_vm_external_prototype(vm, &njs_externals[0]);
    if (proto == NULL) {
        fprintf(stderr, "failed to add console proto\n");
        return NXT_ERROR;
    }

    value = nxt_mem_cache_zalloc(vm->mem_cache_pool,
                                 sizeof(njs_opaque_value_t));
    if (value == NULL) {
        return NXT_ERROR;
    }

    ret = njs_vm_external_create(vm, value, proto, NULL);
    if (ret != NXT_OK) {
        return NXT_ERROR;
    }

    ret = njs_vm_external_bind(vm, &name, value);
    if (ret != NXT_OK) {
        return NXT_ERROR;
    }

    return NXT_OK;
}


static nxt_int_t
njs_interactive_shell(njs_opts_t *opts, njs_vm_opt_t *vm_options)
{
    njs_vm_t   *vm;
    nxt_str_t  line, out;

    vm = njs_vm_create(vm_options);
    if (vm == NULL) {
        fprintf(stderr, "failed to create vm\n");
        return NXT_ERROR;
    }

    if (njs_externals_init(vm) != NXT_OK) {
        fprintf(stderr, "failed to add external protos\n");
        return NXT_ERROR;
    }

    if (njs_editline_init(vm) != NXT_OK) {
        fprintf(stderr, "failed to init completions\n");
        return NXT_ERROR;
    }

    if (!opts->quiet) {
        printf("interactive njs %s\n\n", NJS_VERSION);

        printf("v.<Tab> -> the properties and prototype methods of v.\n");
        printf("type console.help() for more information\n\n");
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

        njs_process_script(vm, opts, &line, &out);

        printf("%.*s\n", (int) out.length, out.start);

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
    nxt_str_t    out, script;
    struct stat  sb;

    file = opts->file;

    if (file[0] == '-' && file[1] == '\0') {
        fd = STDIN_FILENO;

    } else {
        fd = open(file, O_RDONLY);
        if (fd == -1) {
            fprintf(stderr, "failed to open file: '%s' (%s)\n",
                    file, strerror(errno));
            return NXT_ERROR;
        }
    }

    if (fstat(fd, &sb) == -1) {
        fprintf(stderr, "fstat(%d) failed while reading '%s' (%s)\n",
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
        fprintf(stderr, "alloc failed while reading '%s'\n", file);
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
            fprintf(stderr, "failed to read file: '%s' (%s)\n",
                    file, strerror(errno));
            ret = NXT_ERROR;
            goto done;
        }

        if (p + n > end) {
            size *= 2;

            start = realloc(script.start, size);
            if (start == NULL) {
                fprintf(stderr, "alloc failed while reading '%s'\n", file);
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

    vm = njs_vm_create(vm_options);
    if (vm == NULL) {
        fprintf(stderr, "failed to create vm\n");
        ret = NXT_ERROR;
        goto done;
    }

    ret = njs_externals_init(vm);
    if (ret != NXT_OK) {
        fprintf(stderr, "failed to add external protos\n");
        ret = NXT_ERROR;
        goto done;
    }

    ret = njs_process_script(vm, opts, &script, &out);
    if (ret != NXT_OK) {
        fprintf(stderr, "%.*s\n", (int) out.length, out.start);
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


static nxt_int_t
njs_process_script(njs_vm_t *vm, njs_opts_t *opts, const nxt_str_t *script,
    nxt_str_t *out)
{
    u_char     *start;
    nxt_int_t  ret;

    start = script->start;

    ret = njs_vm_compile(vm, &start, start + script->length);

    if (ret == NXT_OK) {
        if (opts->disassemble) {
            njs_disassembler(vm);
            printf("\n");
        }

        ret = njs_vm_run(vm);
    }

    if (njs_vm_retval_dump(vm, out, 1) != NXT_OK) {
        *out = nxt_string_value("failed to get retval from VM");
        return NXT_ERROR;
    }

    return ret;
}


static nxt_int_t
njs_editline_init(njs_vm_t *vm)
{
    rl_completion_append_character = '\0';
    rl_attempted_completion_function = njs_completion_handler;
    rl_basic_word_break_characters = (char *) " \t\n\"\\'`@$><=;,|&{(";

    setlocale(LC_ALL, "");

    njs_completion.completions = njs_vm_completions(vm, NULL);
    if (njs_completion.completions == NULL) {
        return NXT_ERROR;
    }

    njs_completion.vm = vm;

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
    njs_variable_t    *var;
    njs_completion_t  *cmpl;

    cmpl = &njs_completion;

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
        if (cmpl->vm->parser == NULL) {
            njs_next_phase(cmpl);
        }

        for ( ;; ) {
            var = nxt_lvlhsh_each(&cmpl->vm->parser->scope->variables,
                                  &cmpl->lhe);

            if (var == NULL) {
                break;
            }

            if (var->name.length < cmpl->length) {
                continue;
            }

            if (strncmp(text, (char *) var->name.start, cmpl->length) == 0) {
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

            cmpl->suffix_completions = njs_vm_completions(cmpl->vm,
                                                          &expression);
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

            if (len != 0 && strncmp((char *) suffix->start, p,
                                    nxt_min(len, suffix->length)) != 0)
            {
                continue;
            }

            len = suffix->length + (p - text) + 1;
            completion = malloc(len);
            if (completion == NULL) {
                return NULL;
            }

            snprintf(completion, len, "%.*s%.*s", (int) (p - text), text,
                     (int) suffix->length, suffix->start);
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

            if (strncmp(text, (char *) suffix->start, cmpl->length) == 0) {
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

        printf("%s%.*s", (n != 1) ? " " : "", (int) msg.length, msg.start);

        n++;
    }

    if (nargs > 1) {
        printf("\n");
    }

    vm->retval = njs_value_void;

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

        printf("%s%.*s", (n != 1) ? " " : "", (int) msg.length, msg.start);

        n++;
    }

    if (nargs > 1) {
        printf("\n");
    }

    vm->retval = njs_value_void;

    return NJS_OK;
}


static njs_ret_t
njs_ext_console_help(njs_vm_t *vm, njs_value_t *args, nxt_uint_t nargs,
    njs_index_t unused)
{
    const njs_object_init_t  *obj, **objpp;

    printf("VM built-in objects:\n");

    for (objpp = njs_constructor_init; *objpp != NULL; objpp++) {
        obj = *objpp;

        printf("  %.*s\n", (int) obj->name.length, obj->name.start);
    }

    for (objpp = njs_object_init; *objpp != NULL; objpp++) {
        obj = *objpp;

        printf("  %.*s\n", (int) obj->name.length, obj->name.start);
    }

    printf("\nEmbedded objects:\n");
    printf("  console\n");

    printf("\n");

    vm->retval = njs_value_void;

    return NJS_OK;
}


static njs_ret_t
njs_ext_console_time(njs_vm_t *vm, njs_value_t *args, nxt_uint_t nargs,
    njs_index_t unused)
{
    if (!njs_value_is_void(njs_arg(args, nargs, 1))) {
        njs_vm_error(vm, "labels not implemented");
        return NJS_ERROR;
    }

    vm->retval = njs_value_void;

    gettimeofday(&njs_console_time, NULL);

    return NJS_OK;
}


static njs_ret_t
njs_ext_console_time_end(njs_vm_t *vm, njs_value_t *args, nxt_uint_t nargs,
    njs_index_t unused)
{
    int64_t         us, ms;
    struct timeval  tv;

    gettimeofday(&tv, NULL);

    if (!njs_value_is_void(njs_arg(args, nargs, 1))) {
        njs_vm_error(vm, "labels not implemented");
        return NJS_ERROR;
    }

    if (nxt_fast_path(njs_console_time.tv_sec || njs_console_time.tv_usec)) {

        us = ((int64_t) tv.tv_sec - njs_console_time.tv_sec) * 1000000
             + ((int64_t) tv.tv_usec - njs_console_time.tv_usec);

        ms = us / 1000;
        us = us % 1000;

        printf("default: %" PRIu64 ".%03" PRIu64 "ms\n", ms, us);

        njs_console_time.tv_sec = 0;
        njs_console_time.tv_usec = 0;

    } else {
        printf("Timer \"default\" doesn’t exist.\n");
    }

    vm->retval = njs_value_void;

    return NJS_OK;
}
