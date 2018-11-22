
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) NGINX, Inc.
 */

#include <njs_core.h>
#include <njs_regexp.h>
#include <string.h>


static nxt_int_t njs_vm_init(njs_vm_t *vm);
static nxt_int_t njs_vm_handle_events(njs_vm_t *vm);


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
        nxt_memzero(p, size);
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

    mcp = nxt_mem_cache_pool_create(&njs_vm_mem_cache_pool_proto, NULL,
                                    NULL, 2 * nxt_pagesize(), 128, 512, 16);
    if (nxt_slow_path(mcp == NULL)) {
        return NULL;
    }

    vm = nxt_mem_cache_zalign(mcp, sizeof(njs_value_t), sizeof(njs_vm_t));

    if (nxt_fast_path(vm != NULL)) {
        vm->mem_cache_pool = mcp;

        ret = njs_regexp_init(vm);
        if (nxt_slow_path(ret != NXT_OK)) {
            return NULL;
        }

        vm->options = *options;

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
                                                nxt_length("(?:)"), 0);
            if (nxt_slow_path(pattern == NULL)) {
                return NULL;
            }

            vm->shared->empty_regexp_pattern = pattern;

            nxt_lvlhsh_init(&vm->modules_hash);

            ret = njs_builtin_objects_create(vm);
            if (nxt_slow_path(ret != NXT_OK)) {
                return NULL;
            }
        }

        nxt_lvlhsh_init(&vm->values_hash);

        vm->external = options->external;

        vm->external_objects = nxt_array_create(4, sizeof(void *),
                                                &njs_array_mem_proto,
                                                vm->mem_cache_pool);
        if (nxt_slow_path(vm->external_objects == NULL)) {
            return NULL;
        }

        nxt_lvlhsh_init(&vm->externals_hash);
        nxt_lvlhsh_init(&vm->external_prototypes_hash);

        vm->trace.level = NXT_LEVEL_TRACE;
        vm->trace.size = 2048;
        vm->trace.handler = njs_parser_trace_handler;
        vm->trace.data = vm;

        if (options->backtrace) {
            debug = nxt_array_create(4, sizeof(njs_function_debug_t),
                                     &njs_array_mem_proto,
                                     vm->mem_cache_pool);
            if (nxt_slow_path(debug == NULL)) {
                return NULL;
            }

            vm->debug = debug;
        }

        if (options->accumulative) {
            ret = njs_vm_init(vm);
            if (nxt_slow_path(ret != NXT_OK)) {
                return NULL;
            }
        }
    }

    return vm;
}


void
njs_vm_destroy(njs_vm_t *vm)
{
    njs_event_t        *event;
    nxt_lvlhsh_each_t  lhe;

    if (njs_is_pending_events(vm)) {
        nxt_lvlhsh_each_init(&lhe, &njs_event_hash_proto);

        for ( ;; ) {
            event = nxt_lvlhsh_each(&vm->events_hash, &lhe);

            if (event == NULL) {
                break;
            }

            njs_del_event(vm, event, NJS_EVENT_RELEASE);
        }
    }

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

    if (vm->parser != NULL && !vm->options.accumulative) {
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

    vm->retval = njs_value_void;

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

    if (vm->options.init) {
        ret = njs_vm_init(vm);
        if (nxt_slow_path(ret != NXT_OK)) {
            return ret;
        }
    }

    return NJS_OK;

fail:

    vm->parser = prev;

    return NXT_ERROR;
}


njs_vm_t *
njs_vm_clone(njs_vm_t *vm, njs_external_ptr_t external)
{
    njs_vm_t              *nvm;
    uint32_t              items;
    nxt_int_t             ret;
    nxt_array_t           *externals;
    nxt_mem_cache_pool_t  *nmcp;

    nxt_thread_log_debug("CLONE:");

    if (vm->options.accumulative) {
        return NULL;
    }

    nmcp = nxt_mem_cache_pool_create(&njs_vm_mem_cache_pool_proto, NULL,
                                    NULL, 2 * nxt_pagesize(), 128, 512, 16);
    if (nxt_slow_path(nmcp == NULL)) {
        return NULL;
    }

    nvm = nxt_mem_cache_zalign(nmcp, sizeof(njs_value_t), sizeof(njs_vm_t));

    if (nxt_fast_path(nvm != NULL)) {
        nvm->mem_cache_pool = nmcp;

        nvm->shared = vm->shared;

        nvm->variables_hash = vm->variables_hash;
        nvm->values_hash = vm->values_hash;
        nvm->modules_hash = vm->modules_hash;

        nvm->externals_hash = vm->externals_hash;
        nvm->external_prototypes_hash = vm->external_prototypes_hash;

        items = vm->external_objects->items;
        externals = nxt_array_create(items + 4, sizeof(void *),
                                     &njs_array_mem_proto, nvm->mem_cache_pool);

        if (nxt_slow_path(externals == NULL)) {
            return NULL;
        }

        if (items > 0) {
            memcpy(externals->start, vm->external_objects->start,
                   items * sizeof(void *));
            externals->items = items;
        }

        nvm->external_objects = externals;

        nvm->options = vm->options;

        nvm->current = vm->current;

        nvm->external = external;

        nvm->global_scope = vm->global_scope;
        nvm->scope_size = vm->scope_size;

        nvm->debug = vm->debug;

        ret = njs_vm_init(nvm);
        if (nxt_slow_path(ret != NXT_OK)) {
            goto fail;
        }

        return nvm;
    }

fail:

    nxt_mem_cache_pool_destroy(nmcp);

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

    nxt_memzero(frame, NJS_GLOBAL_FRAME_SIZE);

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

    nxt_lvlhsh_init(&vm->events_hash);
    nxt_queue_init(&vm->posted_events);

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

    if (njs_is_null(&vm->retval)) {
        vm->retval = njs_value_void;
    }

    return NXT_OK;
}


nxt_int_t
njs_vm_call(njs_vm_t *vm, njs_function_t *function, const njs_value_t *args,
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

    ret = njs_function_frame(vm, function, this, args, nargs, 0);
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


njs_vm_event_t
njs_vm_add_event(njs_vm_t *vm, njs_function_t *function, nxt_uint_t once,
    njs_host_event_t host_ev, njs_event_destructor destructor)
{
    njs_event_t  *event;

    event = nxt_mem_cache_alloc(vm->mem_cache_pool, sizeof(njs_event_t));
    if (nxt_slow_path(event == NULL)) {
        return NULL;
    }

    event->host_event = host_ev;
    event->destructor = destructor;
    event->function = function;
    event->once = once;
    event->posted = 0;
    event->nargs = 0;
    event->args = NULL;

    if (njs_add_event(vm, event) != NJS_OK) {
        return NULL;
    }

    return event;
}


void
njs_vm_del_event(njs_vm_t *vm, njs_vm_event_t vm_event)
{
    njs_event_t  *event;

    event = (njs_event_t *) vm_event;

    njs_del_event(vm, event, NJS_EVENT_RELEASE | NJS_EVENT_DELETE);
}


nxt_int_t
njs_vm_pending(njs_vm_t *vm)
{
    return njs_is_pending_events(vm);
}


nxt_int_t
njs_vm_post_event(njs_vm_t *vm, njs_vm_event_t vm_event,
    const njs_value_t *args, nxt_uint_t nargs)
{
    njs_event_t  *event;

    event = (njs_event_t *) vm_event;

    if (nargs != 0 && !event->posted) {
        event->nargs = nargs;
        event->args = nxt_mem_cache_alloc(vm->mem_cache_pool,
                                          sizeof(njs_value_t) * nargs);
        if (nxt_slow_path(event->args == NULL)) {
            return NJS_ERROR;
        }

        memcpy(event->args, args, sizeof(njs_value_t) * nargs);
    }

    if (!event->posted) {
        event->posted = 1;
        nxt_queue_insert_tail(&vm->posted_events, &event->link);
    }

    return NJS_OK;
}


nxt_int_t
njs_vm_run(njs_vm_t *vm)
{
    nxt_int_t  ret;

    if (nxt_slow_path(vm->backtrace != NULL)) {
        nxt_array_reset(vm->backtrace);
    }

    ret = njs_vmcode_interpreter(vm);

    if (ret == NJS_STOP) {
        ret = njs_vm_handle_events(vm);
    }

    switch (ret) {
    case NJS_STOP:
        return NJS_OK;

    case NXT_AGAIN:
    case NXT_ERROR:
    default:
        return ret;
    }
}


static nxt_int_t
njs_vm_handle_events(njs_vm_t *vm)
{
    nxt_int_t         ret;
    njs_event_t       *ev;
    nxt_queue_t       *events;
    nxt_queue_link_t  *link;

    events = &vm->posted_events;

    for ( ;; ) {
        link = nxt_queue_first(events);

        if (link == nxt_queue_tail(events)) {
            break;
        }

        ev = nxt_queue_link_data(link, njs_event_t, link);

        if (ev->once) {
            njs_del_event(vm, ev, NJS_EVENT_DELETE);

        } else {
            ev->posted = 0;
            nxt_queue_remove(&ev->link);
        }

        ret = njs_vm_call(vm, ev->function, ev->args, ev->nargs);

        if (ret == NJS_ERROR) {
            return ret;
        }
    }

    return njs_is_pending_events(vm) ? NJS_AGAIN : NJS_STOP;
}


nxt_noinline njs_value_t *
njs_vm_retval(njs_vm_t *vm)
{
    return &vm->retval;
}


nxt_noinline void
njs_vm_retval_set(njs_vm_t *vm, const njs_value_t *value)
{
    vm->retval = *value;
}


nxt_noinline void
njs_vm_memory_error(njs_vm_t *vm)
{
    njs_set_memory_error(vm, &vm->retval);
}


njs_ret_t
njs_vm_retval_to_ext_string(njs_vm_t *vm, nxt_str_t *dst)
{
    if (vm->top_frame == NULL) {
        /* An exception was thrown during compilation. */

        njs_vm_init(vm);
    }

    return njs_vm_value_to_ext_string(vm, dst, &vm->retval, 1);
}


njs_ret_t
njs_vm_retval_dump(njs_vm_t *vm, nxt_str_t *dst, nxt_uint_t indent)
{
    if (vm->top_frame == NULL) {
        /* An exception was thrown during compilation. */

        njs_vm_init(vm);
    }

    return njs_vm_value_dump(vm, dst, &vm->retval, 1);
}


njs_ret_t
njs_vm_object_alloc(njs_vm_t *vm, njs_value_t *retval, ...)
{
    va_list             args;
    nxt_int_t           ret;
    njs_ret_t           rc;
    njs_value_t         *name, *value;
    njs_object_t        *object;
    njs_object_prop_t   *prop;
    nxt_lvlhsh_query_t  lhq;

    object = njs_object_alloc(vm);
    if (nxt_slow_path(object == NULL)) {
        return NJS_ERROR;
    }

    rc = NJS_ERROR;

    va_start(args, retval);

    for ( ;; ) {
        name = va_arg(args, njs_value_t *);
        if (name == NULL) {
            break;
        }

        value = va_arg(args, njs_value_t *);
        if (value == NULL) {
            njs_type_error(vm, "missed value for a key");
            goto done;
        }

        if (nxt_slow_path(!njs_is_string(name))) {
            njs_type_error(vm, "prop name is not a string");
            goto done;
        }

        lhq.replace = 0;
        lhq.pool = vm->mem_cache_pool;
        lhq.proto = &njs_object_hash_proto;

        njs_string_get(name, &lhq.key);
        lhq.key_hash = nxt_djb_hash(lhq.key.start, lhq.key.length);

        prop = njs_object_prop_alloc(vm, name, value, 1);
        if (nxt_slow_path(prop == NULL)) {
            goto done;
        }

        lhq.value = prop;

        ret = nxt_lvlhsh_insert(&object->hash, &lhq);
        if (nxt_slow_path(ret != NXT_OK)) {
            njs_internal_error(vm, NULL);
            goto done;
        }
    }

    rc = NJS_OK;

    retval->data.u.object = object;
    retval->type = NJS_OBJECT;
    retval->data.truth = 1;

done:

    va_end(args);

    return rc;
}


njs_value_t *
njs_vm_object_prop(njs_vm_t *vm, const njs_value_t *value, const nxt_str_t *key)
{
    nxt_int_t           ret;
    njs_object_prop_t   *prop;
    nxt_lvlhsh_query_t  lhq;

    if (nxt_slow_path(!njs_is_object(value))) {
        return NULL;
    }

    lhq.key = *key;
    lhq.key_hash = nxt_djb_hash(lhq.key.start, lhq.key.length);
    lhq.proto = &njs_object_hash_proto;

    ret = nxt_lvlhsh_find(&value->data.u.object->hash, &lhq);
    if (nxt_slow_path(ret != NXT_OK)) {
        return NULL;
    }

    prop = lhq.value;

    return &prop->value;
}
