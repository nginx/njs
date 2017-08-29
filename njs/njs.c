
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
#include <nxt_mem_cache_pool.h>
#include <njscript.h>
#include <njs_vm.h>
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
    const char              **completions;
    nxt_lvlhsh_each_t       lhe;
    njs_completion_phase_t  phase;
} njs_completion_t;


static nxt_int_t njs_get_options(njs_opts_t *opts, int argc, char **argv);
static nxt_int_t njs_interactive_shell(njs_opts_t *opts,
    njs_vm_opt_t *vm_options);
static nxt_int_t njs_process_file(njs_opts_t *opts, njs_vm_opt_t *vm_options);
static nxt_int_t njs_process_script(njs_vm_t *vm, njs_opts_t *opts,
    const nxt_str_t *script, nxt_str_t *out);
static void njs_print_backtrace(nxt_array_t *backtrace);
static nxt_int_t njs_editline_init(njs_vm_t *vm);
static char **njs_completion_handler(const char *text, int start, int end);
static char *njs_completion_generator(const char *text, int state);


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
njs_interactive_shell(njs_opts_t *opts, njs_vm_opt_t *vm_options)
{
    njs_vm_t     *vm;
    nxt_int_t    ret;
    nxt_str_t    line, out;
    nxt_array_t  *backtrace;

    vm = njs_vm_create(vm_options);
    if (vm == NULL) {
        fprintf(stderr, "failed to create vm\n");
        return NXT_ERROR;
    }

    if (njs_editline_init(vm) != NXT_OK) {
        fprintf(stderr, "failed to init completions\n");
        return NXT_ERROR;
    }

    printf("interactive njscript\n");

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
            printf("njs_process_script() failed\n");
            return NXT_ERROR;
        }

        printf("%.*s\n", (int) out.length, out.start);

        backtrace = njs_vm_backtrace(vm);
        if (backtrace != NULL) {
            njs_print_backtrace(backtrace);
        }

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
    u_char       buf[4096], *p, *end;
    size_t       size;
    ssize_t      n;
    njs_vm_t     *vm;
    nxt_int_t    ret;
    nxt_str_t    out, script;
    struct stat  sb;
    nxt_array_t  *backtrace;

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

    fstat(fd, &sb);

    size = sizeof(buf);

    if (S_ISREG(sb.st_mode) && sb.st_size) {
        size = sb.st_size;
    }

    script.length = 0;
    script.start = realloc(NULL, size);
    if (script.start == NULL) {
        fprintf(stderr, "alloc failed while reading '%s'\n", file);
        return NXT_ERROR;
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
            return NXT_ERROR;
        }

        if (p + n > end) {
            size *= 2;

            script.start = realloc(script.start, size);
            if (script.start == NULL) {
                fprintf(stderr, "alloc failed while reading '%s'\n", file);
                return NXT_ERROR;
            }

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
        return NXT_ERROR;
    }

    ret = njs_process_script(vm, opts, &script, &out);
    if (ret != NXT_OK) {
        fprintf(stderr, "njs_process_script() failed\n");
        return NXT_ERROR;
    }

    if (!opts->disassemble) {
        printf("%.*s\n", (int) out.length, out.start);

        backtrace = njs_vm_backtrace(vm);
        if (backtrace != NULL) {
            njs_print_backtrace(backtrace);
        }
    }

    return NXT_OK;
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

        if (ret == NXT_OK) {
            if (njs_vm_retval(vm, out) != NXT_OK) {
                return NXT_ERROR;
            }

        } else {
            njs_vm_exception(vm, out);
        }

    } else {
        njs_vm_exception(vm, out);
    }

    return NXT_OK;
}


static void
njs_print_backtrace(nxt_array_t *backtrace)
{
    nxt_uint_t             i;
    njs_backtrace_entry_t  *be;

    be = backtrace->start;

    for (i = 0; i < backtrace->items; i++) {
        if (be[i].line != 0) {
            printf("at %.*s (:%d)\n", (int) be[i].name.length, be[i].name.start,
                   be[i].line);

        } else {
            printf("at %.*s\n", (int) be[i].name.length, be[i].name.start);
        }
    }
}


static nxt_int_t
njs_editline_init(njs_vm_t *vm)
{
    rl_completion_append_character = '\0';
    rl_attempted_completion_function = njs_completion_handler;
    rl_basic_word_break_characters = (char *) " \t\n\"\\'`@$><=;,|&{(";

    njs_completion.completions = njs_vm_completions(vm);
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


static char *
njs_completion_generator(const char *text, int state)
{
    char              *completion;
    size_t            len;
    const char        *name, *p;
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
            name = cmpl->completions[cmpl->index];
            if (name == NULL) {
                break;
            }

            cmpl->index++;

            if (name[0] == '.') {
                continue;
            }

            if (strncmp(name, text, cmpl->length) == 0) {
                /* editline frees the buffer every time. */
                return strdup(name);
            }
        }

        if (cmpl->vm->parser != NULL) {
            for ( ;; ) {
                var = nxt_lvlhsh_each(&cmpl->vm->parser->scope->variables,
                                      &cmpl->lhe);
                if (var == NULL) {
                    break;
                }

                if (strncmp((char *) var->name.start, text, cmpl->length)
                    == 0)
                {
                    completion = malloc(var->name.length + 1);
                    if (completion == NULL) {
                        return NULL;
                    }

                    memcpy(completion, var->name.start, var->name.length);
                    completion[var->name.length] = '\0';

                    return completion;
                }
            }
        }

        if (cmpl->length == 0) {
            return NULL;
        }

        cmpl->index = 0;
        cmpl->phase = NJS_COMPLETION_SUFFIX;
    }

    len = 1;
    p = &text[cmpl->length - 1];

    while (p > text && *p != '.') {
        p--;
        len++;
    }

    if (*p != '.') {
        return NULL;
    }

    for ( ;; ) {
        name = cmpl->completions[cmpl->index++];
        if (name == NULL) {
            break;
        }

        if (name[0] != '.') {
            continue;
        }

        if (strncmp(name, p, len) != 0) {
            continue;
        }

        len = strlen(name) + (p - text) + 2;
        completion = malloc(len);
        if (completion == NULL) {
            return NULL;
        }

        snprintf(completion, len, "%.*s%s", (int) (p - text), text, name);
        return completion;
    }

    return NULL;
}
