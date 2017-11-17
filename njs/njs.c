
/*
 * Copyright (C) Dmitry Volyntsev
 * Copyright (C) NGINX, Inc.
 */

#include <time.h>
#include <errno.h>
#include <string.h>
#include <stdio.h>
#include <fcntl.h>
#include <stdlib.h>
#include <sys/stat.h>

#include <nxt_auto_config.h>
#include <nxt_types.h>
#include <nxt_clang.h>
#include <nxt_string.h>
#include <nxt_stub.h>
#include <nxt_malloc.h>
#include <nxt_array.h>
#include <nxt_lvlhsh.h>
#include <nxt_random.h>
#include <nxt_djb_hash.h>
#include <nxt_mem_cache_pool.h>
#include <njscript.h>
#include <njs_vm.h>
#include <njs_object.h>
#include <njs_builtin.h>
#include <njs_variable.h>
#include <njs_parser.h>

#include <readline.h>


typedef enum {
    NJS_COMPLETION_GLOBAL = 0,
    NJS_COMPLETION_SUFFIX,
} njs_completion_phase_t;


typedef struct {
    char                    *file;
    nxt_int_t               disassemble;
    nxt_int_t               interactive;
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
static nxt_int_t njs_externals_init(njs_opts_t *opts, njs_vm_opt_t *vm_options);
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
static njs_ret_t njs_ext_console_help(njs_vm_t *vm, njs_value_t *args,
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


static nxt_lvlhsh_t      njs_externals_hash;
static njs_completion_t  njs_completion;


int
main(int argc, char **argv)
{
    nxt_int_t             ret;
    njs_opts_t            opts;
    njs_vm_opt_t          vm_options;
    nxt_mem_cache_pool_t  *mcp;

    memset(&opts, 0, sizeof(njs_opts_t));
    opts.interactive = 1;

    ret = njs_get_options(&opts, argc, argv);
    if (ret != NXT_OK) {
        return (ret == NXT_DONE) ? EXIT_SUCCESS : EXIT_FAILURE;
    }

    mcp = nxt_mem_cache_pool_create(&njs_vm_mem_cache_pool_proto, NULL,
                                    NULL, 2 * nxt_pagesize(), 128, 512, 16);
    if (nxt_slow_path(mcp == NULL)) {
        return EXIT_FAILURE;
    }

    memset(&vm_options, 0, sizeof(njs_vm_opt_t));

    vm_options.mcp = mcp;
    vm_options.accumulative = 1;
    vm_options.backtrace = 1;

    if (njs_externals_init(&opts, &vm_options) != NXT_OK) {
        return EXIT_FAILURE;
    }

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
        case 'd':
            opts->disassemble = 1;
            break;

        default:
            fprintf(stderr, "Unknown argument: \"%s\"\n", argv[i]);
            ret = NXT_ERROR;

            /* Fall through. */

        case 'h':
        case '?':
            printf("Usage: %s [<file>|-] [-d]\n", argv[0]);
            return ret;
        }
    }

    return NXT_OK;
}


static nxt_int_t
njs_externals_init(njs_opts_t *opts, njs_vm_opt_t *vm_options)
{
    void        **ext;
    nxt_uint_t  i;

    nxt_lvlhsh_init(&njs_externals_hash);

    for (i = 0; i < nxt_nitems(njs_externals); i++) {
        if (njs_vm_external_add(&njs_externals_hash, vm_options->mcp,
                                (uintptr_t) i, &njs_externals[i], 1)
            != NXT_OK)
        {
            fprintf(stderr, "could not add external objects\n");
            return NXT_ERROR;
        }
    }

    ext = nxt_mem_cache_zalloc(vm_options->mcp, sizeof(void *) * i);
    if (ext == NULL) {
        return NXT_ERROR;
    }

    vm_options->external = ext;
    vm_options->externals_hash = &njs_externals_hash;

    return NXT_OK;
}


static nxt_int_t
njs_interactive_shell(njs_opts_t *opts, njs_vm_opt_t *vm_options)
{
    njs_vm_t   *vm;
    nxt_int_t  ret;
    nxt_str_t  line, out;

    vm = njs_vm_create(vm_options);
    if (vm == NULL) {
        fprintf(stderr, "failed to create vm\n");
        return NXT_ERROR;
    }

    if (njs_editline_init(vm) != NXT_OK) {
        fprintf(stderr, "failed to init completions\n");
        return NXT_ERROR;
    }

    printf("interactive njscript\n\n");

    printf("v.<Tab> -> the properties and prototype methods of v.\n");
    printf("type console.help() for more information\n\n");

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

        ret = njs_process_script(vm, opts, &line, &out);
        if (ret != NXT_OK) {
            printf("shell: failed to get retval from VM\n");
            continue;
        }

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

    ret = njs_process_script(vm, opts, &script, &out);
    if (ret != NXT_OK) {
        fprintf(stderr, "failed to get retval from VM\n");
        ret = NXT_ERROR;
        goto done;
    }

    if (!opts->disassemble) {
        printf("%.*s\n", (int) out.length, out.start);
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
        if (ret == NXT_AGAIN) {
            return ret;
        }
    }

    if (njs_vm_retval(vm, out) != NXT_OK) {
        return NXT_ERROR;
    }

    return NXT_OK;
}


static nxt_int_t
njs_editline_init(njs_vm_t *vm)
{
    rl_completion_append_character = '\0';
    rl_attempted_completion_function = njs_completion_handler;
    rl_basic_word_break_characters = (char *) " \t\n\"\\'`@$><=;,|&{(";

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
        cmpl->index = 0;
        cmpl->length = strlen(text);
        cmpl->phase = NJS_COMPLETION_GLOBAL;

        nxt_lvlhsh_each_init(&cmpl->lhe, &njs_variables_hash_proto);
    }

    if (cmpl->phase == NJS_COMPLETION_GLOBAL) {
        for ( ;; ) {
            if (cmpl->index >= cmpl->completions->items) {
                break;
            }

            suffix = njs_completion(cmpl->completions, cmpl->index++);

            if (suffix->start[0] == '.' || suffix->length < cmpl->length) {
                continue;
            }

            if (strncmp(text, (char *) suffix->start,
                        nxt_min(suffix->length, cmpl->length)) == 0)
            {
                return njs_editline(suffix);
            }
        }

        if (cmpl->vm->parser != NULL) {
            for ( ;; ) {
                var = nxt_lvlhsh_each(&cmpl->vm->parser->scope->variables,
                                      &cmpl->lhe);
                if (var == NULL || var->name.length < cmpl->length) {
                    break;
                }

                if (strncmp(text, (char *) var->name.start,
                            nxt_min(var->name.length, cmpl->length)) == 0)
                {
                    return njs_editline(&var->name);
                }
            }
        }

        if (cmpl->length == 0) {
            return NULL;
        }

        /* Getting the longest prefix before a '.' */

        p = &text[cmpl->length - 1];
        while (p > text && *p != '.') { p--; }

        if (*p != '.') {
            return NULL;
        }

        expression.start = (u_char *) text;
        expression.length = p - text;

        cmpl->suffix_completions = njs_vm_completions(cmpl->vm, &expression);
        if (cmpl->suffix_completions == NULL) {
            return NULL;
        }

        cmpl->index = 0;
        cmpl->phase = NJS_COMPLETION_SUFFIX;
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
            break;
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

    return NULL;
}


static njs_ret_t
njs_ext_console_log(njs_vm_t *vm, njs_value_t *args, nxt_uint_t nargs,
    njs_index_t unused)
{
    nxt_str_t  msg;

    msg.length = 0;
    msg.start = NULL;

    if (nargs >= 2
        && njs_value_to_ext_string(vm, &msg, njs_argument(args, 1))
           == NJS_ERROR)
    {

        return NJS_ERROR;
    }

    printf("%.*s\n", (int) msg.length, msg.start);

    vm->retval = njs_value_void;

    return NJS_OK;
}


static njs_ret_t
njs_ext_console_help(njs_vm_t *vm, njs_value_t *args, nxt_uint_t nargs,
    njs_index_t unused)
{
    nxt_uint_t  i;

    printf("VM built-in objects:\n");
    for (i = NJS_CONSTRUCTOR_OBJECT; i < NJS_CONSTRUCTOR_MAX; i++) {
        printf("  %.*s\n", (int) njs_constructor_init[i]->name.length,
               njs_constructor_init[i]->name.start);
    }

    for (i = NJS_OBJECT_THIS; i < NJS_OBJECT_MAX; i++) {
        if (njs_object_init[i] != NULL) {
            printf("  %.*s\n", (int) njs_object_init[i]->name.length,
                   njs_object_init[i]->name.start);
        }
    }

    printf("\nEmbedded objects:\n");
    for (i = 0; i < nxt_nitems(njs_externals); i++) {
        printf("  %.*s\n", (int) njs_externals[i].name.length,
               njs_externals[i].name.start);
    }

    printf("\n");

    return NJS_OK;
}
