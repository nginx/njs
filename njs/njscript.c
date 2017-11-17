
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) NGINX, Inc.
 */

#include <nxt_auto_config.h>
#include <nxt_types.h>
#include <nxt_clang.h>
#include <nxt_alignment.h>
#include <nxt_string.h>
#include <nxt_stub.h>
#include <nxt_array.h>
#include <nxt_lvlhsh.h>
#include <nxt_random.h>
#include <nxt_malloc.h>
#include <nxt_mem_cache_pool.h>
#include <njscript.h>
#include <njs_vm.h>
#include <njs_string.h>
#include <njs_object.h>
#include <njs_function.h>
#include <njs_variable.h>
#include <njs_parser.h>
#include <njs_regexp.h>
#include <string.h>
#include <stdio.h>


static nxt_int_t njs_vm_init(njs_vm_t *vm);


static void *
njs_alloc(void *mem, size_t size)
{
    return nxt_malloc(size);
}


static void *
njs_zalloc(void *mem, size_t size)
{
    void  *p;

    p = nxt_malloc(size);

    if (p != NULL) {
        memset(p, 0, size);
    }

    return p;
}


static void *
njs_align(void *mem, size_t alignment, size_t size)
{
    return nxt_memalign(alignment, size);
}


static void
njs_free(void *mem, void *p)
{
    nxt_free(p);
}


const nxt_mem_proto_t  njs_vm_mem_cache_pool_proto = {
    njs_alloc,
    njs_zalloc,
    njs_align,
    NULL,
    njs_free,
    NULL,
    NULL,
};


static void *
njs_array_mem_alloc(void *mem, size_t size)
{
    return nxt_mem_cache_alloc(mem, size);
}


static void
njs_array_mem_free(void *mem, void *p)
{
    nxt_mem_cache_free(mem, p);
}


const nxt_mem_proto_t  njs_array_mem_proto = {
    njs_array_mem_alloc,
    NULL,
    NULL,
    NULL,
    njs_array_mem_free,
    NULL,
    NULL,
};


njs_vm_t *
njs_vm_create(njs_vm_opt_t *options)
{
    njs_vm_t              *vm;
    nxt_int_t             ret;
    nxt_array_t           *debug;
    nxt_mem_cache_pool_t  *mcp;
    njs_regexp_pattern_t  *pattern;

    mcp = options->mcp;

    if (mcp == NULL) {
        mcp = nxt_mem_cache_pool_create(&njs_vm_mem_cache_pool_proto, NULL,
                                        NULL, 2 * nxt_pagesize(), 128, 512, 16);
        if (nxt_slow_path(mcp == NULL)) {
            return NULL;
        }
    }

    vm = nxt_mem_cache_zalign(mcp, sizeof(njs_value_t), sizeof(njs_vm_t));

    if (nxt_fast_path(vm != NULL)) {
        vm->mem_cache_pool = mcp;

        ret = njs_regexp_init(vm);
        if (nxt_slow_path(ret != NXT_OK)) {
            return NULL;
        }

        if (options->shared != NULL) {
            vm->shared = options->shared;

        } else {
            vm->shared = nxt_mem_cache_zalloc(mcp, sizeof(njs_vm_shared_t));
            if (nxt_slow_path(vm->shared == NULL)) {
                return NULL;
            }

            options->shared = vm->shared;

            nxt_lvlhsh_init(&vm->shared->keywords_hash);

            ret = njs_lexer_keywords_init(mcp, &vm->shared->keywords_hash);
            if (nxt_slow_path(ret != NXT_OK)) {
                return NULL;
            }

            nxt_lvlhsh_init(&vm->shared->values_hash);

            pattern = njs_regexp_pattern_create(vm, (u_char *) "(?:)",
                                                sizeof("(?:)") - 1, 0);
            if (nxt_slow_path(pattern == NULL)) {
                return NULL;
            }

            vm->shared->empty_regexp_pattern = pattern;

            nxt_lvlhsh_init(&vm->modules_hash);

            ret = njs_builtin_objects_create(vm);
            if (nxt_slow_path(ret != NXT_OK)) {
                return NULL;
            }

            if (options->externals_hash != NULL) {
                vm->external = options->external;
            }
        }

        nxt_lvlhsh_init(&vm->values_hash);

        if (options->externals_hash != NULL) {
            vm->externals_hash = *options->externals_hash;
        }

        vm->trace.level = NXT_LEVEL_TRACE;
        vm->trace.size = 2048;
        vm->trace.handler = njs_parser_trace_handler;
        vm->trace.data = vm;

        vm->trailer = options->trailer;

        if (options->backtrace) {
            debug = nxt_array_create(4, sizeof(njs_function_debug_t),
                                     &njs_array_mem_proto,
                                     vm->mem_cache_pool);
            if (nxt_slow_path(debug == NULL)) {
                return NULL;
            }

            vm->debug = debug;
        }

        vm->accumulative = options->accumulative;
        if (vm->accumulative) {
            ret = njs_vm_init(vm);
            if (nxt_slow_path(ret != NXT_OK)) {
                return NULL;
            }

            vm->retval = njs_value_void;
        }
    }

    return vm;
}


void
njs_vm_destroy(njs_vm_t *vm)
{
    nxt_mem_cache_pool_destroy(vm->mem_cache_pool);
}


nxt_int_t
njs_vm_compile(njs_vm_t *vm, u_char **start, u_char *end)
{
    nxt_int_t          ret;
    njs_lexer_t        *lexer;
    njs_parser_t       *parser, *prev;
    njs_parser_node_t  *node;

    parser = nxt_mem_cache_zalloc(vm->mem_cache_pool, sizeof(njs_parser_t));
    if (nxt_slow_path(parser == NULL)) {
        return NJS_ERROR;
    }

    if (vm->parser != NULL && !vm->accumulative) {
        return NJS_ERROR;
    }

    prev = vm->parser;
    vm->parser = parser;

    lexer = nxt_mem_cache_zalloc(vm->mem_cache_pool, sizeof(njs_lexer_t));
    if (nxt_slow_path(lexer == NULL)) {
        return NJS_ERROR;
    }

    parser->lexer = lexer;
    lexer->start = *start;
    lexer->end = end;
    lexer->line = 1;
    lexer->keywords_hash = vm->shared->keywords_hash;

    parser->code_size = sizeof(njs_vmcode_stop_t);
    parser->scope_offset = NJS_INDEX_GLOBAL_OFFSET;

    if (vm->backtrace != NULL) {
        nxt_array_reset(vm->backtrace);
    }

    node = njs_parser(vm, parser, prev);
    if (nxt_slow_path(node == NULL)) {
        goto fail;
    }

    ret = njs_variables_scope_reference(vm, parser->scope);
    if (nxt_slow_path(ret != NXT_OK)) {
        goto fail;
    }

    *start = parser->lexer->start;

    /*
     * Reset the code array to prevent it from being disassembled
     * again in the next iteration of the accumulative mode.
     */
    vm->code = NULL;

    ret = njs_generate_scope(vm, parser, node);
    if (nxt_slow_path(ret != NXT_OK)) {
        goto fail;
    }

    vm->current = parser->code_start;

    vm->global_scope = parser->local_scope;
    vm->scope_size = parser->scope_size;
    vm->variables_hash = parser->scope->variables;

    return NJS_OK;

fail:

    vm->parser = prev;

    return NXT_ERROR;
}


njs_vm_t *
njs_vm_clone(njs_vm_t *vm, nxt_mem_cache_pool_t *mcp, void **external)
{
    njs_vm_t              *nvm;
    nxt_int_t             ret;
    nxt_mem_cache_pool_t  *nmcp;

    nxt_thread_log_debug("CLONE:");

    if (vm->accumulative) {
        return NULL;
    }

    nmcp = mcp;

    if (nmcp == NULL) {
        nmcp = nxt_mem_cache_pool_create(&njs_vm_mem_cache_pool_proto, NULL,
                                        NULL, 2 * nxt_pagesize(), 128, 512, 16);
        if (nxt_slow_path(nmcp == NULL)) {
            return NULL;
        }
    }

    nvm = nxt_mem_cache_zalloc(nmcp, sizeof(njs_vm_t));

    if (nxt_fast_path(nvm != NULL)) {
        nvm->mem_cache_pool = nmcp;

        nvm->shared = vm->shared;

        nvm->variables_hash = vm->variables_hash;
        nvm->values_hash = vm->values_hash;
        nvm->modules_hash = vm->modules_hash;
        nvm->externals_hash = vm->externals_hash;

        nvm->current = vm->current;
        nvm->external = external;

        nvm->global_scope = vm->global_scope;
        nvm->scope_size = vm->scope_size;

        nvm->debug = vm->debug;

        ret = njs_vm_init(nvm);
        if (nxt_slow_path(ret != NXT_OK)) {
            goto fail;
        }

        nvm->retval = njs_value_void;

        return nvm;
    }

fail:

    if (mcp == NULL) {
        nxt_mem_cache_pool_destroy(nmcp);
    }

    return NULL;
}


static nxt_int_t
njs_vm_init(njs_vm_t *vm)
{
    size_t       size, scope_size;
    u_char       *values;
    nxt_int_t    ret;
    njs_frame_t  *frame;
    nxt_array_t  *backtrace;

    scope_size = vm->scope_size + NJS_INDEX_GLOBAL_OFFSET;

    size = NJS_GLOBAL_FRAME_SIZE + scope_size + NJS_FRAME_SPARE_SIZE;
    size = nxt_align_size(size, NJS_FRAME_SPARE_SIZE);

    frame = nxt_mem_cache_align(vm->mem_cache_pool, sizeof(njs_value_t), size);
    if (nxt_slow_path(frame == NULL)) {
        return NXT_ERROR;
    }

    memset(frame, 0, NJS_GLOBAL_FRAME_SIZE);

    vm->top_frame = &frame->native;
    vm->active_frame = frame;

    frame->native.size = size;
    frame->native.free_size = size - (NJS_GLOBAL_FRAME_SIZE + scope_size);

    values = (u_char *) frame + NJS_GLOBAL_FRAME_SIZE;

    frame->native.free = values + scope_size;

    vm->scopes[NJS_SCOPE_GLOBAL] = (njs_value_t *) values;
    memcpy(values + NJS_INDEX_GLOBAL_OFFSET, vm->global_scope,
           vm->scope_size);

    ret = njs_regexp_init(vm);
    if (nxt_slow_path(ret != NXT_OK)) {
        return NXT_ERROR;
    }

    ret = njs_builtin_objects_clone(vm);
    if (nxt_slow_path(ret != NXT_OK)) {
        return NXT_ERROR;
    }

    if (vm->debug != NULL) {
        backtrace = nxt_array_create(4, sizeof(njs_backtrace_entry_t),
                                     &njs_array_mem_proto, vm->mem_cache_pool);
        if (nxt_slow_path(backtrace == NULL)) {
            return NXT_ERROR;
        }

        vm->backtrace = backtrace;
    }

    vm->trace.level = NXT_LEVEL_TRACE;
    vm->trace.size = 2048;
    vm->trace.handler = njs_parser_trace_handler;
    vm->trace.data = vm;

    return NXT_OK;
}


nxt_int_t
njs_vm_call(njs_vm_t *vm, njs_function_t *function, njs_opaque_value_t *args,
    nxt_uint_t nargs)
{
    u_char       *current;
    njs_ret_t    ret;
    njs_value_t  *this;

    static const njs_vmcode_stop_t  stop[] = {
        { .code = { .operation = njs_vmcode_stop,
                    .operands =  NJS_VMCODE_1OPERAND,
                    .retval = NJS_VMCODE_NO_RETVAL },
          .retval = NJS_INDEX_GLOBAL_RETVAL },
    };

    this = (njs_value_t *) &njs_value_void;

    ret = njs_function_frame(vm, function, this,
                             (njs_value_t *) args, nargs, 0);
    if (nxt_slow_path(ret != NXT_OK)) {
        return ret;
    }

    current = vm->current;
    vm->current = (u_char *) stop;

    ret = njs_function_call(vm, NJS_INDEX_GLOBAL_RETVAL, 0);
    if (nxt_slow_path(ret == NXT_ERROR)) {
        return ret;
    }

    ret = njs_vmcode_interpreter(vm);

    vm->current = current;

    if (ret == NJS_STOP) {
        ret = NXT_OK;
    }

    return ret;
}


nxt_int_t
njs_vm_run(njs_vm_t *vm)
{
    nxt_str_t  s;
    nxt_int_t  ret;

    nxt_thread_log_debug("RUN:");

    if (vm->backtrace != NULL) {
        nxt_array_reset(vm->backtrace);
    }

    ret = njs_vmcode_interpreter(vm);

    if (nxt_slow_path(ret == NXT_AGAIN)) {
        nxt_thread_log_debug("VM: AGAIN");
        return ret;
    }

    if (nxt_slow_path(ret != NJS_STOP)) {
        nxt_thread_log_debug("VM: ERROR");
        return ret;
    }

    if (vm->retval.type == NJS_NUMBER) {
        nxt_thread_log_debug("VM: %f", vm->retval.data.u.number);

    } else if (vm->retval.type == NJS_BOOLEAN) {
        nxt_thread_log_debug("VM: boolean: %d", vm->retval.data.truth);

    } else if (vm->retval.type == NJS_STRING) {

        if (njs_value_to_ext_string(vm, &s, &vm->retval) == NJS_OK) {
            nxt_thread_log_debug("VM: '%V'", &s);
        }

    } else if (vm->retval.type == NJS_FUNCTION) {
        nxt_thread_log_debug("VM: function");

    } else if (vm->retval.type == NJS_NULL) {
        nxt_thread_log_debug("VM: null");

    } else if (vm->retval.type == NJS_VOID) {
        nxt_thread_log_debug("VM: void");

    } else {
        nxt_thread_log_debug("VM: unknown: %d", vm->retval.type);
    }

    return NJS_OK;
}


njs_ret_t
njs_vm_return_string(njs_vm_t *vm, u_char *start, size_t size)
{
    return njs_string_create(vm, &vm->retval, start, size, 0);
}


nxt_int_t
njs_vm_retval(njs_vm_t *vm, nxt_str_t *retval)
{
    u_char                 *p, *start;
    size_t                 len;
    nxt_int_t              ret;
    nxt_uint_t             i;
    nxt_array_t            *backtrace;
    njs_backtrace_entry_t  *be;

    if (vm->top_frame == NULL) {
        /* An exception was thrown during compilation. */

        njs_vm_init(vm);
    }

    ret = njs_value_to_ext_string(vm, retval, &vm->retval);

    if (ret != NXT_OK) {
        /* retval evaluation threw an exception. */

        vm->top_frame->trap_tries = 0;

        ret = njs_value_to_ext_string(vm, retval, &vm->retval);
        if (ret != NXT_OK) {
            return ret;
        }
    }

    backtrace = njs_vm_backtrace(vm);

    if (backtrace != NULL) {

        len = retval->length + 1;

        be = backtrace->start;

        for (i = 0; i < backtrace->items; i++) {
            if (be[i].line != 0) {
                len += sizeof("    at  (:)\n") - 1 + 10 + be[i].name.length;

            } else {
                len += sizeof("    at  (native)\n") - 1 + be[i].name.length;
            }
        }

        p = nxt_mem_cache_alloc(vm->mem_cache_pool, len);
        if (p == NULL) {
            return NXT_ERROR;
        }

        start = p;

        p = nxt_cpymem(p, retval->start, retval->length);
        *p++ = '\n';

        for (i = 0; i < backtrace->items; i++) {
            if (be[i].line != 0) {
                p += sprintf((char *) p, "    at %.*s (:%u)\n",
                             (int) be[i].name.length, be[i].name.start,
                             be[i].line);

            } else {
                p += sprintf((char *) p, "    at %.*s (native)\n",
                             (int) be[i].name.length, be[i].name.start);
            }
        }

        retval->start = start;
        retval->length = p - retval->start;
    }

    return NXT_OK;
}


nxt_array_t *
njs_vm_backtrace(njs_vm_t *vm)
{
    if (vm->backtrace != NULL && !nxt_array_is_empty(vm->backtrace)) {
        return vm->backtrace;
    }

    return NULL;
}
