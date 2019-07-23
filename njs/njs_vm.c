
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) NGINX, Inc.
 */

#include <njs_core.h>
#include <njs_regexp.h>
#include <string.h>


const nxt_str_t  njs_entry_main =           nxt_string("main");
const nxt_str_t  njs_entry_module =         nxt_string("module");
const nxt_str_t  njs_entry_native =         nxt_string("native");
const nxt_str_t  njs_entry_unknown =        nxt_string("unknown");
const nxt_str_t  njs_entry_anonymous =      nxt_string("anonymous");


void
njs_vm_scopes_restore(njs_vm_t *vm, njs_frame_t *frame,
    njs_native_frame_t *previous)
{
    nxt_uint_t      n, nesting;
    njs_value_t     *args;
    njs_function_t  *function;

    vm->top_frame = previous;

    args = previous->arguments;
    function = previous->function;

    if (function != NULL) {
        args += function->args_offset;
    }

    vm->scopes[NJS_SCOPE_CALLEE_ARGUMENTS] = args;

    function = frame->native.function;

    if (function->native) {
        return;
    }

    if (function->closure) {
        /* GC: release function closures. */
    }

    frame = frame->previous_active_frame;
    vm->active_frame = frame;

    /* GC: arguments, local, and local block closures. */

    vm->scopes[NJS_SCOPE_ARGUMENTS] = frame->native.arguments;
    vm->scopes[NJS_SCOPE_LOCAL] = frame->local;

    function = frame->native.function;

    nesting = (function != NULL) ? function->u.lambda->nesting : 0;

    for (n = 0; n <= nesting; n++) {
        vm->scopes[NJS_SCOPE_CLOSURE + n] = &frame->closures[n]->u.values;
    }

    while (n < NJS_MAX_NESTING) {
        vm->scopes[NJS_SCOPE_CLOSURE + n] = NULL;
        n++;
    }
}


njs_ret_t
njs_vm_value_to_string(njs_vm_t *vm, nxt_str_t *dst, const njs_value_t *src)
{
    u_char       *start;
    size_t       size;
    njs_ret_t    ret;
    njs_value_t  value;

    if (nxt_slow_path(src == NULL)) {
        return NXT_ERROR;
    }

    if (nxt_slow_path(src->type == NJS_OBJECT_INTERNAL_ERROR)) {
        /* MemoryError is a nonextensible internal error. */
        if (!njs_object(src)->extensible) {
            njs_string_get(&njs_string_memory_error, dst);
            return NXT_OK;
        }
    }

    value = *src;

    ret = njs_value_to_string(vm, &value, &value);

    if (nxt_fast_path(ret == NXT_OK)) {
        size = value.short_string.size;

        if (size != NJS_STRING_LONG) {
            start = nxt_mp_alloc(vm->mem_pool, size);
            if (nxt_slow_path(start == NULL)) {
                njs_memory_error(vm);
                return NXT_ERROR;
            }

            memcpy(start, value.short_string.start, size);

        } else {
            size = value.long_string.size;
            start = value.long_string.data->start;
        }

        dst->length = size;
        dst->start = start;
    }

    return ret;
}


nxt_int_t
njs_vm_value_string_copy(njs_vm_t *vm, nxt_str_t *retval,
    const njs_value_t *value, uintptr_t *next)
{
    uintptr_t    n;
    njs_array_t  *array;

    switch (value->type) {

    case NJS_STRING:
        if (*next != 0) {
            return NXT_DECLINED;
        }

        *next = 1;
        break;

    case NJS_ARRAY:
        array = njs_array(value);

        do {
            n = (*next)++;

            if (n == array->length) {
                return NXT_DECLINED;
            }

            value = &array->start[n];

        } while (!njs_is_valid(value));

        break;

    default:
        return NXT_ERROR;
    }

    return njs_vm_value_to_string(vm, retval, value);
}


njs_ret_t
njs_vm_add_backtrace_entry(njs_vm_t *vm, njs_frame_t *frame)
{
    nxt_int_t              ret;
    nxt_uint_t             i;
    njs_function_t         *function;
    njs_native_frame_t     *native_frame;
    njs_function_debug_t   *debug_entry;
    njs_function_lambda_t  *lambda;
    njs_backtrace_entry_t  *be;

    native_frame = &frame->native;
    function = native_frame->function;

    be = nxt_array_add(vm->backtrace, &njs_array_mem_proto, vm->mem_pool);
    if (nxt_slow_path(be == NULL)) {
        return NXT_ERROR;
    }

    be->line = 0;

    if (function == NULL) {
        be->name = njs_entry_main;
        return NXT_OK;
    }

    if (function->native) {
        ret = njs_builtin_match_native_function(vm, function, &be->name);
        if (ret == NXT_OK) {
            return NXT_OK;
        }

        ret = njs_external_match_native_function(vm, function->u.native,
                                                 &be->name);
        if (ret == NXT_OK) {
            return NXT_OK;
        }

        be->name = njs_entry_native;

        return NXT_OK;
    }

    lambda = function->u.lambda;
    debug_entry = vm->debug->start;

    for (i = 0; i < vm->debug->items; i++) {
        if (lambda == debug_entry[i].lambda) {
            if (debug_entry[i].name.length != 0) {
                be->name = debug_entry[i].name;

            } else {
                be->name = njs_entry_anonymous;
            }

            be->file = debug_entry[i].file;
            be->line = debug_entry[i].line;

            return NXT_OK;
        }
    }

    be->name = njs_entry_unknown;

    return NXT_OK;
}


void *
njs_lvlhsh_alloc(void *data, size_t size)
{
    return nxt_mp_align(data, size, size);
}


void
njs_lvlhsh_free(void *data, void *p, size_t size)
{
    nxt_mp_free(data, p);
}
