
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) NGINX, Inc.
 */


#include <njs_main.h>


static njs_int_t njs_vm_init(njs_vm_t *vm);
static njs_int_t njs_vm_handle_events(njs_vm_t *vm);


const njs_str_t  njs_entry_main =           njs_str("main");
const njs_str_t  njs_entry_module =         njs_str("module");
const njs_str_t  njs_entry_native =         njs_str("native");
const njs_str_t  njs_entry_unknown =        njs_str("unknown");
const njs_str_t  njs_entry_anonymous =      njs_str("anonymous");


void
njs_vm_opt_init(njs_vm_opt_t *options)
{
    njs_memzero(options, sizeof(njs_vm_opt_t));

    options->log_level = NJS_LOG_LEVEL_INFO;
}


njs_vm_t *
njs_vm_create(njs_vm_opt_t *options)
{
    njs_mp_t      *mp;
    njs_vm_t      *vm;
    njs_int_t     ret;
    njs_uint_t    i;
    njs_module_t  **addons;

    mp = njs_mp_fast_create(2 * njs_pagesize(), 128, 512, 16);
    if (njs_slow_path(mp == NULL)) {
        return NULL;
    }

    vm = njs_mp_zalign(mp, sizeof(njs_value_t), sizeof(njs_vm_t));
    if (njs_slow_path(vm == NULL)) {
        return NULL;
    }

    vm->mem_pool = mp;

    ret = njs_regexp_init(vm);
    if (njs_slow_path(ret != NJS_OK)) {
        return NULL;
    }

    njs_lvlhsh_init(&vm->values_hash);

    vm->options = *options;

    if (options->shared != NULL) {
        vm->shared = options->shared;

    } else {
        ret = njs_builtin_objects_create(vm);
        if (njs_slow_path(ret != NJS_OK)) {
            return NULL;
        }
    }

    vm->external = options->external;

    vm->trace.level = NJS_LEVEL_TRACE;
    vm->trace.size = 2048;
    vm->trace.data = vm;

    njs_set_undefined(&vm->retval);

    if (options->init) {
        ret = njs_vm_init(vm);
        if (njs_slow_path(ret != NJS_OK)) {
            return NULL;
        }
    }

    for (i = 0; njs_modules[i] != NULL; i++) {
        ret = njs_modules[i]->init(vm);
        if (njs_slow_path(ret != NJS_OK)) {
            return NULL;
        }
    }

    if (options->addons != NULL) {
        addons = options->addons;
        for (i = 0; addons[i] != NULL; i++) {
            ret = addons[i]->init(vm);
            if (njs_slow_path(ret != NJS_OK)) {
                return NULL;
            }
        }
    }

    vm->symbol_generator = NJS_SYMBOL_KNOWN_MAX;

    if (njs_scope_undefined_index(vm, 0) == NJS_INDEX_ERROR) {
        return NULL;
    }

    return vm;
}


void
njs_vm_destroy(njs_vm_t *vm)
{
    njs_event_t        *event;
    njs_lvlhsh_each_t  lhe;

    if (vm->hooks[NJS_HOOK_EXIT] != NULL) {
        (void) njs_vm_call(vm, vm->hooks[NJS_HOOK_EXIT], NULL, 0);
    }

    if (njs_waiting_events(vm)) {
        njs_lvlhsh_each_init(&lhe, &njs_event_hash_proto);

        for ( ;; ) {
            event = njs_lvlhsh_each(&vm->events_hash, &lhe);

            if (event == NULL) {
                break;
            }

            njs_del_event(vm, event, NJS_EVENT_RELEASE);
        }
    }

    njs_mp_destroy(vm->mem_pool);
}


njs_int_t
njs_vm_compile(njs_vm_t *vm, u_char **start, u_char *end)
{
    njs_int_t           ret;
    njs_str_t           ast;
    njs_chb_t           chain;
    njs_value_t         **global, **new;
    njs_parser_t        parser;
    njs_vm_code_t       *code;
    njs_generator_t     generator;
    njs_parser_scope_t  *scope;

    vm->codes = NULL;

    ret = njs_parser_init(vm, &parser, vm->global_scope, &vm->options.file,
                          *start, end, 0);
    if (njs_slow_path(ret != NJS_OK)) {
        return NJS_ERROR;
    }

    ret = njs_parser(vm, &parser);
    if (njs_slow_path(ret != NJS_OK)) {
        return NJS_ERROR;
    }

    if (njs_slow_path(vm->options.ast)) {
        njs_chb_init(&chain, vm->mem_pool);
        ret = njs_parser_serialize_ast(parser.node, &chain);
        if (njs_slow_path(ret == NJS_ERROR)) {
            return ret;
        }

        if (njs_slow_path(njs_chb_join(&chain, &ast) != NJS_OK)) {
            return NJS_ERROR;
        }

        njs_print(ast.start, ast.length);

        njs_chb_destroy(&chain);
        njs_mp_free(vm->mem_pool, ast.start);
    }

    *start = parser.lexer->start;
    scope = parser.scope;

    ret = njs_generator_init(&generator, &vm->options.file, 0, 0);
    if (njs_slow_path(ret != NJS_OK)) {
        njs_internal_error(vm, "njs_generator_init() failed");
        return NJS_ERROR;
    }

    code = njs_generate_scope(vm, &generator, scope, &njs_entry_main);
    if (njs_slow_path(code == NULL)) {
        if (!njs_is_error(&vm->retval)) {
            njs_internal_error(vm, "njs_generate_scope() failed");
        }

        return NJS_ERROR;
    }

    vm->global_scope = scope;

    if (scope->items > vm->global_items) {
        global = vm->levels[NJS_LEVEL_GLOBAL];

        new = njs_scope_make(vm, scope->items);
        if (njs_slow_path(new == NULL)) {
            return ret;
        }

        vm->levels[NJS_LEVEL_GLOBAL] = new;

        if (global != NULL) {
            while (vm->global_items != 0) {
                vm->global_items--;

                *new++ = *global++;
            }
        }
    }

    /* globalThis and this */
    njs_scope_value_set(vm, njs_scope_global_this_index(), &vm->global_value);

    vm->start = generator.code_start;
    vm->variables_hash = &scope->variables;
    vm->global_items = scope->items;

    if (vm->options.disassemble) {
        njs_disassembler(vm);
    }

    return NJS_OK;
}


njs_mod_t *
njs_vm_compile_module(njs_vm_t *vm, njs_str_t *name, u_char **start,
    u_char *end)
{
    njs_int_t              ret;
    njs_arr_t              *arr;
    njs_mod_t              *module;
    njs_parser_t           parser;
    njs_vm_code_t          *code;
    njs_generator_t        generator;
    njs_parser_scope_t     *scope;
    njs_function_lambda_t  *lambda;

    module = njs_module_find(vm, name, 1);
    if (module != NULL) {
        return module;
    }

    module = njs_module_add(vm, name);
    if (njs_slow_path(module == NULL)) {
        return NULL;
    }

    ret = njs_parser_init(vm, &parser, NULL, name, *start, end, 1);
    if (njs_slow_path(ret != NJS_OK)) {
        return NULL;
    }

    parser.module = 1;

    ret = njs_parser(vm, &parser);
    if (njs_slow_path(ret != NJS_OK)) {
        return NULL;
    }

    *start = parser.lexer->start;

    ret = njs_generator_init(&generator, &module->name, 0, 0);
    if (njs_slow_path(ret != NJS_OK)) {
        njs_internal_error(vm, "njs_generator_init() failed");
        return NULL;
    }

    code = njs_generate_scope(vm, &generator, parser.scope, &njs_entry_module);
    if (njs_slow_path(code == NULL)) {
        njs_internal_error(vm, "njs_generate_scope() failed");

        return NULL;
    }

    lambda = njs_mp_zalloc(vm->mem_pool, sizeof(njs_function_lambda_t));
    if (njs_fast_path(lambda == NULL)) {
        njs_memory_error(vm);
        return NULL;
    }

    scope = parser.scope;

    lambda->start = generator.code_start;
    lambda->nlocal = scope->items;

    arr = scope->declarations;
    lambda->declarations = (arr != NULL) ? arr->start : NULL;
    lambda->ndeclarations = (arr != NULL) ? arr->items : 0;

    module->function.args_offset = 1;
    module->function.u.lambda = lambda;

    return module;
}


njs_vm_t *
njs_vm_clone(njs_vm_t *vm, njs_external_ptr_t external)
{
    njs_mp_t     *nmp;
    njs_vm_t     *nvm;
    njs_int_t    ret;
    njs_value_t  **global;

    njs_thread_log_debug("CLONE:");

    if (vm->options.interactive) {
        return NULL;
    }

    nmp = njs_mp_fast_create(2 * njs_pagesize(), 128, 512, 16);
    if (njs_slow_path(nmp == NULL)) {
        return NULL;
    }

    nvm = njs_mp_align(nmp, sizeof(njs_value_t), sizeof(njs_vm_t));
    if (njs_slow_path(nvm == NULL)) {
        goto fail;
    }

    *nvm = *vm;

    nvm->mem_pool = nmp;
    nvm->trace.data = nvm;
    nvm->external = external;

    ret = njs_vm_init(nvm);
    if (njs_slow_path(ret != NJS_OK)) {
        goto fail;
    }

    global = njs_scope_make(nvm, nvm->global_items);
    if (njs_slow_path(global == NULL)) {
        goto fail;
    }

    nvm->levels[NJS_LEVEL_GLOBAL] = global;

    njs_set_object(&nvm->global_value, &nvm->global_object);

    /* globalThis and this */
    njs_scope_value_set(nvm, njs_scope_global_this_index(), &nvm->global_value);

    nvm->levels[NJS_LEVEL_LOCAL] = NULL;

    return nvm;

fail:

    njs_mp_destroy(nmp);

    return NULL;
}


static njs_int_t
njs_vm_init(njs_vm_t *vm)
{
    njs_int_t    ret;
    njs_frame_t  *frame;

    frame = (njs_frame_t *) njs_function_frame_alloc(vm, NJS_FRAME_SIZE);
    if (njs_slow_path(frame == NULL)) {
        njs_memory_error(vm);
        return NJS_ERROR;
    }

    frame->exception.catch = NULL;
    frame->exception.next = NULL;
    frame->previous_active_frame = NULL;

    vm->active_frame = frame;

    ret = njs_regexp_init(vm);
    if (njs_slow_path(ret != NJS_OK)) {
        return NJS_ERROR;
    }

    ret = njs_builtin_objects_clone(vm, &vm->global_value);
    if (njs_slow_path(ret != NJS_OK)) {
        return NJS_ERROR;
    }

    njs_lvlhsh_init(&vm->values_hash);
    njs_lvlhsh_init(&vm->keywords_hash);
    njs_lvlhsh_init(&vm->modules_hash);
    njs_lvlhsh_init(&vm->events_hash);

    njs_rbtree_init(&vm->global_symbols, njs_symbol_rbtree_cmp);

    njs_queue_init(&vm->posted_events);
    njs_queue_init(&vm->promise_events);

    return NJS_OK;
}


njs_int_t
njs_vm_call(njs_vm_t *vm, njs_function_t *function, const njs_value_t *args,
    njs_uint_t nargs)
{
    return njs_vm_invoke(vm, function, args, nargs, &vm->retval);
}


njs_int_t
njs_vm_invoke(njs_vm_t *vm, njs_function_t *function, const njs_value_t *args,
    njs_uint_t nargs, njs_value_t *retval)
{
    njs_int_t  ret;

    ret = njs_function_frame(vm, function, &njs_value_undefined, args, nargs,
                             0);
    if (njs_slow_path(ret != NJS_OK)) {
        return ret;
    }

    return njs_function_frame_invoke(vm, retval);
}


void
njs_vm_scopes_restore(njs_vm_t *vm, njs_native_frame_t *native,
    njs_native_frame_t *previous)
{
    njs_frame_t  *frame;

    vm->top_frame = previous;

    if (native->function->native) {
        return;
    }

    frame = (njs_frame_t *) native;
    frame = frame->previous_active_frame;
    vm->active_frame = frame;
}


njs_vm_event_t
njs_vm_add_event(njs_vm_t *vm, njs_function_t *function, njs_uint_t once,
    njs_host_event_t host_ev, njs_event_destructor_t destructor)
{
    njs_event_t  *event;

    event = njs_mp_alloc(vm->mem_pool, sizeof(njs_event_t));
    if (njs_slow_path(event == NULL)) {
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


njs_int_t
njs_vm_waiting(njs_vm_t *vm)
{
    return njs_waiting_events(vm);
}


njs_int_t
njs_vm_posted(njs_vm_t *vm)
{
    return njs_posted_events(vm) || njs_promise_events(vm);
}


njs_int_t
njs_vm_post_event(njs_vm_t *vm, njs_vm_event_t vm_event,
    const njs_value_t *args, njs_uint_t nargs)
{
    njs_event_t  *event;

    event = (njs_event_t *) vm_event;

    if (nargs != 0 && !event->posted) {
        event->nargs = nargs;
        event->args = njs_mp_alloc(vm->mem_pool, sizeof(njs_value_t) * nargs);
        if (njs_slow_path(event->args == NULL)) {
            return NJS_ERROR;
        }

        memcpy(event->args, args, sizeof(njs_value_t) * nargs);
    }

    if (!event->posted) {
        event->posted = 1;
        njs_queue_insert_tail(&vm->posted_events, &event->link);
    }

    return NJS_OK;
}


njs_int_t
njs_vm_run(njs_vm_t *vm)
{
    return njs_vm_handle_events(vm);
}


njs_int_t
njs_vm_start(njs_vm_t *vm)
{
    njs_int_t  ret;

    ret = njs_vmcode_interpreter(vm, vm->start, NULL, NULL);

    return (ret == NJS_ERROR) ? NJS_ERROR : NJS_OK;
}


static njs_int_t
njs_vm_handle_events(njs_vm_t *vm)
{
    njs_int_t         ret;
    njs_str_t         str;
    njs_value_t       string;
    njs_event_t       *ev;
    njs_queue_t       *promise_events, *posted_events;
    njs_queue_link_t  *link;

    promise_events = &vm->promise_events;
    posted_events = &vm->posted_events;

    do {
        for ( ;; ) {
            link = njs_queue_first(promise_events);

            if (link == njs_queue_tail(promise_events)) {
                break;
            }

            ev = njs_queue_link_data(link, njs_event_t, link);

            njs_queue_remove(&ev->link);

            ret = njs_vm_call(vm, ev->function, ev->args, ev->nargs);
            if (njs_slow_path(ret == NJS_ERROR)) {
                return ret;
            }
        }

        if (njs_vm_unhandled_rejection(vm)) {
            ret = njs_value_to_string(vm, &string,
                                      &vm->promise_reason->start[0]);
            if (njs_slow_path(ret != NJS_OK)) {
                return ret;
            }

            njs_string_get(&string, &str);
            njs_vm_error(vm, "unhandled promise rejection: %V", &str);

            njs_mp_free(vm->mem_pool, vm->promise_reason);
            vm->promise_reason = NULL;

            return NJS_ERROR;
        }

        for ( ;; ) {
            link = njs_queue_first(posted_events);

            if (link == njs_queue_tail(posted_events)) {
                break;
            }

            ev = njs_queue_link_data(link, njs_event_t, link);

            if (ev->once) {
                njs_del_event(vm, ev, NJS_EVENT_RELEASE | NJS_EVENT_DELETE);

            } else {
                ev->posted = 0;
                njs_queue_remove(&ev->link);
            }

            ret = njs_vm_call(vm, ev->function, ev->args, ev->nargs);

            if (ret == NJS_ERROR) {
                return ret;
            }
        }

    } while (!njs_queue_is_empty(promise_events));

    return njs_vm_pending(vm) ? NJS_AGAIN : NJS_OK;
}


njs_int_t
njs_vm_add_path(njs_vm_t *vm, const njs_str_t *path)
{
    njs_str_t  *item;

    if (vm->paths == NULL) {
        vm->paths = njs_arr_create(vm->mem_pool, 4, sizeof(njs_str_t));
        if (njs_slow_path(vm->paths == NULL)) {
            return NJS_ERROR;
        }
    }

    item = njs_arr_add(vm->paths);
    if (njs_slow_path(item == NULL)) {
        return NJS_ERROR;
    }

    *item = *path;

    return NJS_OK;
}


njs_value_t *
njs_vm_retval(njs_vm_t *vm)
{
    return &vm->retval;
}


njs_mp_t *
njs_vm_memory_pool(njs_vm_t *vm)
{
    return vm->mem_pool;
}


uintptr_t
njs_vm_meta(njs_vm_t *vm, njs_uint_t index)
{
    njs_vm_meta_t  *metas;

    metas = vm->options.metas;
    if (njs_slow_path(metas == NULL || metas->size <= index)) {
        return -1;
    }

    return metas->values[index];
}


void
njs_vm_retval_set(njs_vm_t *vm, const njs_value_t *value)
{
    vm->retval = *value;
}


njs_int_t
njs_vm_value(njs_vm_t *vm, const njs_str_t *path, njs_value_t *retval)
{
    u_char       *start, *p, *end;
    size_t       size;
    njs_int_t    ret;
    njs_value_t  value, key;

    start = path->start;
    end = start + path->length;

    njs_set_object(&value, &vm->global_object);

    for ( ;; ) {
        p = njs_strlchr(start, end, '.');

        size = ((p != NULL) ? p : end) - start;
        if (njs_slow_path(size == 0)) {
            njs_type_error(vm, "empty path element");
            return NJS_ERROR;
        }

        ret = njs_string_set(vm, &key, start, size);
        if (njs_slow_path(ret != NJS_OK)) {
            return NJS_ERROR;
        }

        ret = njs_value_property(vm, &value, &key, njs_value_arg(retval));
        if (njs_slow_path(ret != NJS_OK)) {
            return ret;
        }

        if (p == NULL) {
            break;
        }

        start = p + 1;
        value = *retval;
    }

    return NJS_OK;
}


njs_int_t
njs_vm_bind(njs_vm_t *vm, const njs_str_t *var_name, const njs_value_t *value,
    njs_bool_t shared)
{
    njs_int_t           ret;
    njs_object_t        *global;
    njs_lvlhsh_t        *hash;
    njs_object_prop_t   *prop;
    njs_lvlhsh_query_t  lhq;

    prop = njs_object_prop_alloc(vm, &njs_value_undefined, value, 1);
    if (njs_slow_path(prop == NULL)) {
        return NJS_ERROR;
    }

    ret = njs_string_new(vm, &prop->name, var_name->start, var_name->length, 0);
    if (njs_slow_path(ret != NJS_OK)) {
        return NJS_ERROR;
    }

    lhq.value = prop;
    lhq.key = *var_name;
    lhq.key_hash = njs_djb_hash(lhq.key.start, lhq.key.length);
    lhq.replace = 1;
    lhq.pool = vm->mem_pool;
    lhq.proto = &njs_object_hash_proto;

    global = &vm->global_object;
    hash = shared ? &global->shared_hash : &global->hash;

    ret = njs_lvlhsh_insert(hash, &lhq);
    if (njs_slow_path(ret != NJS_OK)) {
        njs_internal_error(vm, "lvlhsh insert failed");
        return ret;
    }

    return NJS_OK;
}


void
njs_value_string_get(njs_value_t *value, njs_str_t *dst)
{
    njs_string_get(value, dst);
}


njs_int_t
njs_vm_value_string_set(njs_vm_t *vm, njs_value_t *value, const u_char *start,
    uint32_t size)
{
    return njs_string_set(vm, value, start, size);
}


njs_int_t
njs_vm_value_array_buffer_set(njs_vm_t *vm, njs_value_t *value,
    const u_char *start, uint32_t size)
{
    njs_array_buffer_t  *array;

    array = njs_array_buffer_alloc(vm, 0, 0);
    if (njs_slow_path(array == NULL)) {
        return NJS_ERROR;
    }

    array->u.data = (u_char *) start;
    array->size = size;

    njs_set_array_buffer(value, array);

    return NJS_OK;
}


njs_int_t
njs_vm_value_buffer_set(njs_vm_t *vm, njs_value_t *value, const u_char *start,
    uint32_t size)
{
    return njs_buffer_set(vm, value, start, size);
}


u_char *
njs_vm_value_string_alloc(njs_vm_t *vm, njs_value_t *value, uint32_t size)
{
    return njs_string_alloc(vm, value, size, 0);
}


njs_function_t *
njs_vm_function(njs_vm_t *vm, const njs_str_t *path)
{
    njs_int_t    ret;
    njs_value_t  retval;

    ret = njs_vm_value(vm, path, &retval);
    if (njs_slow_path(ret != NJS_OK || !njs_is_function(&retval))) {
        return NULL;
    }

    return njs_function(&retval);
}


uint16_t
njs_vm_prop_magic16(njs_object_prop_t *prop)
{
    return prop->value.data.magic16;
}


uint32_t
njs_vm_prop_magic32(njs_object_prop_t *prop)
{
    return prop->value.data.magic32;
}


njs_int_t
njs_vm_prop_name(njs_vm_t *vm, njs_object_prop_t *prop, njs_str_t *dst)
{
    if (njs_slow_path(!njs_is_string(&prop->name))) {
        njs_type_error(vm, "property name is not a string");
        return NJS_ERROR;
    }

    njs_string_get(&prop->name, dst);

    return NJS_OK;
}


njs_noinline void
njs_vm_value_error_set(njs_vm_t *vm, njs_value_t *value, const char *fmt, ...)
{
    va_list  args;
    u_char   buf[NJS_MAX_ERROR_STR], *p;

    p = buf;

    if (fmt != NULL) {
        va_start(args, fmt);
        p = njs_vsprintf(buf, buf + sizeof(buf), fmt, args);
        va_end(args);
    }

    njs_error_new(vm, value, NJS_OBJ_TYPE_ERROR, buf, p - buf);
}


njs_noinline void
njs_vm_memory_error(njs_vm_t *vm)
{
    njs_memory_error_set(vm, &vm->retval);
}


njs_noinline void
njs_vm_logger(njs_vm_t *vm, njs_log_level_t level, const char *fmt, ...)
{
    u_char        *p;
    va_list       args;
    njs_logger_t  logger;
    u_char        buf[NJS_MAX_ERROR_STR];

    if (vm->options.ops == NULL) {
        return;
    }

    logger = vm->options.ops->logger;

    if (logger != NULL && vm->options.log_level >= level) {
        va_start(args, fmt);
        p = njs_vsprintf(buf, buf + sizeof(buf), fmt, args);
        va_end(args);

        logger(vm, vm->external, level, buf, p - buf);
    }
}


njs_int_t
njs_vm_value_string(njs_vm_t *vm, njs_str_t *dst, njs_value_t *src)
{
    njs_int_t    ret;
    njs_uint_t   exception;

    if (njs_slow_path(src->type == NJS_NUMBER
                      && njs_number(src) == 0
                      && signbit(njs_number(src))))
    {
        njs_string_get(&njs_string_minus_zero, dst);
        return NJS_OK;
    }

    exception = 0;

again:

    ret = njs_vm_value_to_string(vm, dst, src);
    if (njs_fast_path(ret == NJS_OK)) {
        return NJS_OK;
    }

    if (!exception) {
        exception = 1;

        /* value evaluation threw an exception. */

        src = &vm->retval;
        goto again;
    }

    dst->length = 0;
    dst->start = NULL;

    return NJS_ERROR;
}


njs_int_t
njs_vm_retval_string(njs_vm_t *vm, njs_str_t *dst)
{
    if (vm->top_frame == NULL) {
        /* An exception was thrown during compilation. */

        njs_vm_init(vm);
    }

    return njs_vm_value_string(vm, dst, &vm->retval);
}


njs_int_t
njs_vm_retval_dump(njs_vm_t *vm, njs_str_t *dst, njs_uint_t indent)
{
    if (vm->top_frame == NULL) {
        /* An exception was thrown during compilation. */

        njs_vm_init(vm);
    }

    return njs_vm_value_dump(vm, dst, &vm->retval, 0, 1);
}


njs_int_t
njs_vm_object_alloc(njs_vm_t *vm, njs_value_t *retval, ...)
{
    va_list             args;
    njs_int_t           ret;
    njs_value_t         *name, *value;
    njs_object_t        *object;
    njs_object_prop_t   *prop;
    njs_lvlhsh_query_t  lhq;

    object = njs_object_alloc(vm);
    if (njs_slow_path(object == NULL)) {
        return NJS_ERROR;
    }

    ret = NJS_ERROR;

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

        if (njs_slow_path(!njs_is_string(name))) {
            njs_type_error(vm, "prop name is not a string");
            goto done;
        }

        lhq.replace = 0;
        lhq.pool = vm->mem_pool;
        lhq.proto = &njs_object_hash_proto;

        njs_string_get(name, &lhq.key);
        lhq.key_hash = njs_djb_hash(lhq.key.start, lhq.key.length);

        prop = njs_object_prop_alloc(vm, name, value, 1);
        if (njs_slow_path(prop == NULL)) {
            goto done;
        }

        lhq.value = prop;

        ret = njs_lvlhsh_insert(&object->hash, &lhq);
        if (njs_slow_path(ret != NJS_OK)) {
            njs_internal_error(vm, NULL);
            goto done;
        }
    }

    ret = NJS_OK;

    njs_set_object(retval, object);

done:

    va_end(args);

    return ret;
}


njs_value_t *
njs_vm_object_keys(njs_vm_t *vm, njs_value_t *value, njs_value_t *retval)
{
    njs_array_t  *keys;

    keys = njs_value_own_enumerate(vm, value, NJS_ENUM_KEYS,
                                   NJS_ENUM_STRING, 0);
    if (njs_slow_path(keys == NULL)) {
        return NULL;
    }

    njs_set_array(retval, keys);

    return retval;
}


njs_int_t
njs_vm_array_alloc(njs_vm_t *vm, njs_value_t *retval, uint32_t spare)
{
    njs_array_t  *array;

    array = njs_array_alloc(vm, 1, 0, spare);

    if (njs_slow_path(array == NULL)) {
        return NJS_ERROR;
    }

    njs_set_array(retval, array);

    return NJS_OK;
}


njs_value_t *
njs_vm_array_push(njs_vm_t *vm, njs_value_t *value)
{
    if (njs_slow_path(!njs_is_array(value))) {
        njs_type_error(vm, "njs_vm_array_push() argument is not array");
        return NULL;
    }

    return njs_array_push(vm, njs_array(value));
}


njs_value_t *
njs_vm_object_prop(njs_vm_t *vm, njs_value_t *value, const njs_str_t *prop,
    njs_opaque_value_t *retval)
{
    njs_int_t    ret;
    njs_value_t  key;

    if (njs_slow_path(!njs_is_object(value))) {
        njs_type_error(vm, "njs_vm_object_prop() argument is not object");
        return NULL;
    }

    ret = njs_vm_value_string_set(vm, &key, prop->start, prop->length);
    if (njs_slow_path(ret != NJS_OK)) {
        return NULL;
    }

    ret = njs_value_property(vm, value, &key, njs_value_arg(retval));
    if (njs_slow_path(ret != NJS_OK)) {
        return NULL;
    }

    return njs_value_arg(retval);
}


njs_value_t *
njs_vm_array_prop(njs_vm_t *vm, njs_value_t *value, int64_t index,
    njs_opaque_value_t *retval)
{
    njs_int_t    ret;
    njs_array_t  *array;

    if (njs_slow_path(!njs_is_object(value))) {
        njs_type_error(vm, "njs_vm_array_prop() argument is not object");
        return NULL;
    }

    if (njs_fast_path(njs_is_fast_array(value))) {
        array = njs_array(value);

        if (index >= 0 && index < array->length) {
            return &array->start[index];
        }

        return NULL;
    }

    ret = njs_value_property_i64(vm, value, index, njs_value_arg(retval));
    if (njs_slow_path(ret != NJS_OK)) {
        return NULL;
    }

    return njs_value_arg(retval);
}


njs_value_t *
njs_vm_array_start(njs_vm_t *vm, njs_value_t *value)
{
    if (njs_slow_path(!njs_is_fast_array(value))) {
        njs_type_error(vm, "njs_vm_array_start() argument is not a fast array");
        return NULL;
    }

    return njs_array(value)->start;
}


njs_int_t
njs_vm_array_length(njs_vm_t *vm, njs_value_t *value, int64_t *length)
{
    if (njs_fast_path(njs_is_array(value))) {
        *length = njs_array(value)->length;
    }

    return njs_object_length(vm, value, length);
}


njs_int_t
njs_vm_value_to_string(njs_vm_t *vm, njs_str_t *dst, njs_value_t *src)
{
    u_char       *start;
    size_t       size;
    njs_int_t    ret;
    njs_value_t  value, stack;

    if (njs_slow_path(src == NULL)) {
        return NJS_ERROR;
    }

    if (njs_is_error(src)) {
        if (njs_is_memory_error(vm, src)) {
            njs_string_get(&njs_string_memory_error, dst);
            return NJS_OK;
        }

        ret = njs_error_stack(vm, src, &stack);
        if (njs_slow_path(ret == NJS_ERROR)) {
            return ret;
        }

        if (ret == NJS_OK) {
            src = &stack;
        }
    }

    value = *src;

    ret = njs_value_to_string(vm, &value, &value);

    if (njs_fast_path(ret == NJS_OK)) {
        size = value.short_string.size;

        if (size != NJS_STRING_LONG) {
            start = njs_mp_alloc(vm->mem_pool, size);
            if (njs_slow_path(start == NULL)) {
                njs_memory_error(vm);
                return NJS_ERROR;
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


njs_int_t
njs_vm_value_to_bytes(njs_vm_t *vm, njs_str_t *dst, njs_value_t *src)
{
    u_char              *start;
    size_t              size, length, offset;
    njs_int_t           ret;
    njs_value_t         value;
    njs_typed_array_t   *array;
    njs_array_buffer_t  *buffer;

    if (njs_slow_path(src == NULL)) {
        return NJS_ERROR;
    }

    ret = NJS_OK;
    value = *src;

    switch (value.type) {
    case NJS_TYPED_ARRAY:
    case NJS_DATA_VIEW:
    case NJS_ARRAY_BUFFER:

        if (value.type != NJS_ARRAY_BUFFER) {
            array = njs_typed_array(&value);
            buffer = njs_typed_array_buffer(array);
            offset = array->offset;
            length = array->byte_length;

        } else {
            buffer = njs_array_buffer(&value);
            offset = 0;
            length = buffer->size;
        }

        if (njs_slow_path(njs_is_detached_buffer(buffer))) {
            njs_type_error(vm, "detached buffer");
            return NJS_ERROR;
        }

        dst->start = &buffer->u.u8[offset];
        dst->length = length;
        break;

    default:
        ret = njs_value_to_string(vm, &value, &value);
        if (njs_slow_path(ret != NJS_OK)) {
            return NJS_ERROR;
        }

        size = value.short_string.size;

        if (size != NJS_STRING_LONG) {
            start = njs_mp_alloc(vm->mem_pool, size);
            if (njs_slow_path(start == NULL)) {
                njs_memory_error(vm);
                return NJS_ERROR;
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


njs_int_t
njs_vm_value_string_copy(njs_vm_t *vm, njs_str_t *retval,
    njs_value_t *value, uintptr_t *next)
{
    uintptr_t    n;
    njs_array_t  *array;

    switch (value->type) {

    case NJS_STRING:
        if (*next != 0) {
            return NJS_DECLINED;
        }

        *next = 1;
        break;

    case NJS_ARRAY:
        array = njs_array(value);

        do {
            n = (*next)++;

            if (n == array->length) {
                return NJS_DECLINED;
            }

            value = &array->start[n];

        } while (!njs_is_valid(value));

        break;

    default:
        return NJS_ERROR;
    }

    return njs_vm_value_to_string(vm, retval, value);
}


void *
njs_lvlhsh_alloc(void *data, size_t size)
{
    return njs_mp_align(data, size, size);
}


void
njs_lvlhsh_free(void *data, void *p, size_t size)
{
    njs_mp_free(data, p);
}
