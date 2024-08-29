
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) NGINX, Inc.
 */


#include <njs_main.h>


static njs_int_t njs_vm_protos_init(njs_vm_t *vm, njs_value_t *global);


const njs_str_t  njs_entry_empty =          njs_str("");
const njs_str_t  njs_entry_main =           njs_str("main");
const njs_str_t  njs_entry_module =         njs_str("module");
const njs_str_t  njs_entry_native =         njs_str("native");
const njs_str_t  njs_entry_unknown =        njs_str("unknown");
const njs_str_t  njs_entry_anonymous =      njs_str("anonymous");


void
njs_vm_opt_init(njs_vm_opt_t *options)
{
    njs_memzero(options, sizeof(njs_vm_opt_t));

    options->max_stack_size = NJS_MAX_STACK_SIZE;
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

    vm->spare_stack_size = options->max_stack_size;

    vm->trace.level = NJS_LEVEL_TRACE;
    vm->trace.size = 2048;
    vm->trace.data = vm;

    if (options->init) {
        ret = njs_vm_runtime_init(vm);
        if (njs_slow_path(ret != NJS_OK)) {
            return NULL;
        }
    }

    for (i = 0; njs_modules[i] != NULL; i++) {
        if (njs_modules[i]->preinit == NULL) {
            continue;
        }

        ret = njs_modules[i]->preinit(vm);
        if (njs_slow_path(ret != NJS_OK)) {
            return NULL;
        }
    }

    if (options->addons != NULL) {
        addons = options->addons;
        for (i = 0; addons[i] != NULL; i++) {
            if (addons[i]->preinit == NULL) {
                continue;
            }

            ret = addons[i]->preinit(vm);
            if (njs_slow_path(ret != NJS_OK)) {
                return NULL;
            }
        }
    }

    ret = njs_vm_protos_init(vm, &vm->global_value);
    if (njs_slow_path(ret != NJS_OK)) {
        return NULL;
    }

    for (i = 0; njs_modules[i] != NULL; i++) {
        if (njs_modules[i]->init == NULL) {
            continue;
        }

        ret = njs_modules[i]->init(vm);
        if (njs_slow_path(ret != NJS_OK)) {
            return NULL;
        }
    }

    if (options->addons != NULL) {
        addons = options->addons;
        for (i = 0; addons[i] != NULL; i++) {
            if (addons[i]->init == NULL) {
                continue;
            }

            ret = addons[i]->init(vm);
            if (njs_slow_path(ret != NJS_OK)) {
                return NULL;
            }
        }
    }

    if (njs_scope_undefined_index(vm, 0) == NJS_INDEX_ERROR) {
        return NULL;
    }

    return vm;
}


njs_int_t
njs_vm_ctor_push(njs_vm_t *vm)
{
    njs_function_t          *ctor;
    njs_vm_shared_t         *shared;
    njs_object_prototype_t  *prototype;

    shared = vm->shared;

    if (shared->constructors == NULL) {
        shared->constructors = njs_arr_create(vm->mem_pool,
                                              NJS_OBJ_TYPE_MAX + 8,
                                              sizeof(njs_function_t));
        if (njs_slow_path(shared->constructors == NULL)) {
            njs_memory_error(vm);
            return -1;
        }

        shared->prototypes = njs_arr_create(vm->mem_pool,
                                              NJS_OBJ_TYPE_MAX + 8,
                                              sizeof(njs_object_prototype_t));
        if (njs_slow_path(shared->prototypes == NULL)) {
            njs_memory_error(vm);
            return -1;
        }
    }

    ctor = njs_arr_add(shared->constructors);
    if (njs_slow_path(ctor == NULL)) {
        njs_memory_error(vm);
        return -1;
    }

    prototype = njs_arr_add(shared->prototypes);
    if (njs_slow_path(prototype == NULL)) {
        njs_memory_error(vm);
        return -1;
    }

    njs_assert(shared->constructors->items == shared->prototypes->items);

    return shared->constructors->items - 1;
}


void
njs_vm_destroy(njs_vm_t *vm)
{
    if (vm->hooks[NJS_HOOK_EXIT] != NULL) {
        (void) njs_vm_call(vm, vm->hooks[NJS_HOOK_EXIT], NULL, 0);
    }

    njs_mp_destroy(vm->mem_pool);
}


njs_int_t
njs_vm_compile(njs_vm_t *vm, u_char **start, u_char *end)
{
    size_t              global_items;
    njs_int_t           ret;
    njs_str_t           ast;
    njs_chb_t           chain;
    njs_value_t         **global, **new;
    njs_parser_t        parser;
    njs_vm_code_t       *code;
    njs_generator_t     generator;
    njs_parser_scope_t  *scope;

    vm->codes = NULL;

    global_items = (vm->global_scope != NULL) ? vm->global_scope->items : 0;

    ret = njs_parser_init(vm, &parser, vm->global_scope, &vm->options.file,
                          *start, end);
    if (njs_slow_path(ret != NJS_OK)) {
        return NJS_ERROR;
    }

    ret = njs_parser(vm, &parser);
    if (njs_slow_path(ret != NJS_OK)) {
        return NJS_ERROR;
    }

    if (njs_slow_path(vm->options.ast)) {
        NJS_CHB_MP_INIT(&chain, njs_vm_memory_pool(vm));
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
        if (!njs_is_error(&vm->exception)) {
            njs_internal_error(vm, "njs_generate_scope() failed");
        }

        return NJS_ERROR;
    }

    if (scope->items > global_items) {
        global = vm->levels[NJS_LEVEL_GLOBAL];

        new = njs_scope_make(vm, scope->items);
        if (njs_slow_path(new == NULL)) {
            return ret;
        }

        vm->levels[NJS_LEVEL_GLOBAL] = new;

        if (global != NULL) {
            while (global_items != 0) {
                global_items--;

                *new++ = *global++;
            }
        }
    }

    /* globalThis and this */
    njs_scope_value_set(vm, njs_scope_global_this_index(), &vm->global_value);

    vm->start = generator.code_start;
    vm->global_scope = scope;

    if (vm->options.disassemble) {
        njs_disassembler(vm);
    }

    return NJS_OK;
}


njs_mod_t *
njs_vm_add_module(njs_vm_t *vm, njs_str_t *name, njs_value_t *value)
{
    return njs_module_add(vm, name, value);
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

    module = njs_module_add(vm, name, NULL);
    if (njs_slow_path(module == NULL)) {
        return NULL;
    }

    ret = njs_parser_init(vm, &parser, NULL, name, *start, end);
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

    module->function.u.lambda = lambda;

    return module;
}


njs_int_t
njs_vm_reuse(njs_vm_t *vm)
{
    vm->active_frame = NULL;
    vm->top_frame = NULL;
    vm->modules = NULL;

    return njs_object_make_shared(vm, njs_object(&vm->global_value));
}


njs_vm_t *
njs_vm_clone(njs_vm_t *vm, njs_external_ptr_t external)
{
    njs_mp_t     *nmp;
    njs_vm_t     *nvm;
    njs_int_t    ret;
    njs_value_t  **global, **value;

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

    nvm->shared_atom_count = vm->atom_id_generator;

    njs_lvlhsh_init(&nvm->atom_hash);
    nvm->atom_hash_current = &nvm->atom_hash;

    ret = njs_vm_runtime_init(nvm);
    if (njs_slow_path(ret != NJS_OK)) {
        goto fail;
    }

    ret = njs_vm_protos_init(nvm, &nvm->global_value);
    if (njs_slow_path(ret != NJS_OK)) {
        goto fail;
    }

    global = njs_scope_make(nvm, nvm->global_scope->items);
    if (njs_slow_path(global == NULL)) {
        goto fail;
    }

    if (nvm->options.unsafe) {
        nvm->scope_absolute = njs_arr_create(nvm->mem_pool,
                                             vm->scope_absolute->items,
                                             sizeof(njs_value_t *));
        if (njs_slow_path(nvm->scope_absolute == NULL)) {
            goto fail;
        }

        value = njs_arr_add_multiple(nvm->scope_absolute,
                                     vm->scope_absolute->items);
        if (njs_slow_path(value == NULL)) {
            goto fail;
        }

        memcpy(value, vm->scope_absolute->start,
               vm->scope_absolute->items * sizeof(njs_value_t *));
    }

    nvm->levels[NJS_LEVEL_GLOBAL] = global;

    /* globalThis and this */
    njs_scope_value_set(nvm, njs_scope_global_this_index(), &nvm->global_value);

    nvm->levels[NJS_LEVEL_LOCAL] = NULL;

    return nvm;

fail:

    njs_mp_destroy(nmp);

    return NULL;
}


njs_int_t
njs_vm_runtime_init(njs_vm_t *vm)
{
    njs_int_t    ret;
    njs_frame_t  *frame;

    if (vm->active_frame == NULL) {
        frame = (njs_frame_t *) njs_function_frame_alloc(vm, NJS_FRAME_SIZE);
        if (njs_slow_path(frame == NULL)) {
            njs_memory_error(vm);
            return NJS_ERROR;
        }

        frame->exception.catch = NULL;
        frame->exception.next = NULL;
        frame->previous_active_frame = NULL;

        vm->active_frame = frame;
    }

    ret = njs_regexp_init(vm);
    if (njs_slow_path(ret != NJS_OK)) {
        return NJS_ERROR;
    }

    njs_lvlhsh_init(&vm->values_hash);

    njs_lvlhsh_init(&vm->modules_hash);

    njs_rbtree_init(&vm->global_symbols, njs_symbol_rbtree_cmp);

    njs_queue_init(&vm->jobs);

    return NJS_OK;
}


void
njs_vm_constructors_init(njs_vm_t *vm)
{
    njs_uint_t    i;
    njs_object_t  *object_prototype, *function_prototype,
                  *typed_array_prototype, *error_prototype, *async_prototype,
                  *typed_array_ctor, *error_ctor;

    object_prototype = njs_vm_proto(vm, NJS_OBJ_TYPE_OBJECT);

    for (i = NJS_OBJ_TYPE_ARRAY; i < NJS_OBJ_TYPE_NORMAL_MAX; i++) {
        vm->prototypes[i].object.__proto__ = object_prototype;
    }

    typed_array_prototype = njs_vm_proto(vm, NJS_OBJ_TYPE_TYPED_ARRAY);

    for (i = NJS_OBJ_TYPE_TYPED_ARRAY_MIN;
         i < NJS_OBJ_TYPE_TYPED_ARRAY_MAX;
         i++)
    {
        vm->prototypes[i].object.__proto__ = typed_array_prototype;
    }

    vm->prototypes[NJS_OBJ_TYPE_ARRAY_ITERATOR].object.__proto__ =
                                       njs_vm_proto(vm, NJS_OBJ_TYPE_ITERATOR);

    vm->prototypes[NJS_OBJ_TYPE_BUFFER].object.__proto__ =
                                    njs_vm_proto(vm, NJS_OBJ_TYPE_UINT8_ARRAY);

    error_prototype = njs_vm_proto(vm, NJS_OBJ_TYPE_ERROR);
    error_prototype->__proto__ = object_prototype;

    for (i = NJS_OBJ_TYPE_EVAL_ERROR; i < vm->constructors_size; i++) {
        vm->prototypes[i].object.__proto__ = error_prototype;
    }

    function_prototype = njs_vm_proto(vm, NJS_OBJ_TYPE_FUNCTION);

    async_prototype = njs_vm_proto(vm, NJS_OBJ_TYPE_ASYNC_FUNCTION);
    async_prototype->__proto__ = function_prototype;

    for (i = NJS_OBJ_TYPE_OBJECT; i < NJS_OBJ_TYPE_NORMAL_MAX; i++) {
        vm->constructors[i].object.__proto__ = function_prototype;
    }

    typed_array_ctor = &njs_vm_ctor(vm, NJS_OBJ_TYPE_TYPED_ARRAY).object;

    for (i = NJS_OBJ_TYPE_TYPED_ARRAY_MIN;
         i < NJS_OBJ_TYPE_TYPED_ARRAY_MAX;
         i++)
    {
        vm->constructors[i].object.__proto__ = typed_array_ctor;
    }

    error_ctor = &njs_vm_ctor(vm, NJS_OBJ_TYPE_ERROR).object;
    error_ctor->__proto__ = function_prototype;

    for (i = NJS_OBJ_TYPE_EVAL_ERROR; i < vm->constructors_size; i++) {
        vm->constructors[i].object.__proto__ = error_ctor;
    }
}


static njs_int_t
njs_vm_protos_init(njs_vm_t *vm, njs_value_t *global)
{
    size_t  ctor_size, proto_size;

    vm->constructors_size = vm->shared->constructors->items;

    ctor_size = vm->constructors_size * sizeof(njs_function_t);
    proto_size = vm->constructors_size * sizeof(njs_object_prototype_t);

    vm->constructors = njs_mp_alloc(vm->mem_pool, ctor_size + proto_size);
    if (njs_slow_path(vm->constructors == NULL)) {
        njs_memory_error(vm);
        return NJS_ERROR;
    }

    vm->prototypes = (njs_object_prototype_t *)
                                     ((u_char *) vm->constructors + ctor_size);

    memcpy(vm->constructors, vm->shared->constructors->start, ctor_size);
    memcpy(vm->prototypes, vm->shared->prototypes->start, proto_size);

    njs_vm_constructors_init(vm);

    vm->global_object.__proto__ = njs_vm_proto(vm, NJS_OBJ_TYPE_OBJECT);

    njs_set_undefined(global);
    njs_set_object(global, &vm->global_object);

    vm->string_object = vm->shared->string_object;
    vm->string_object.__proto__ = njs_vm_proto(vm, NJS_OBJ_TYPE_STRING);

    return NJS_OK;
}


njs_int_t
njs_vm_call(njs_vm_t *vm, njs_function_t *function, const njs_value_t *args,
    njs_uint_t nargs)
{
    njs_value_t  unused;

    return njs_vm_invoke(vm, function, args, nargs, &unused);
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
njs_vm_scopes_restore(njs_vm_t *vm, njs_native_frame_t *native)
{
    njs_frame_t  *frame;

    vm->top_frame = native->previous;

    if (native->function->native) {
        return;
    }

    frame = (njs_frame_t *) native;
    frame = frame->previous_active_frame;
    vm->active_frame = frame;
}


njs_int_t
njs_vm_pending(njs_vm_t *vm)
{
    return !njs_queue_is_empty(&(vm)->jobs);
}


njs_int_t
njs_vm_enqueue_job(njs_vm_t *vm, njs_function_t *function,
    const njs_value_t *args, njs_uint_t nargs)
{
    njs_event_t  *event;

    event = njs_mp_zalloc(vm->mem_pool, sizeof(njs_event_t));
    if (njs_slow_path(event == NULL)) {
        njs_memory_error(vm);
        return NJS_ERROR;
    }

    event->function = function;

    if (nargs != 0) {
        event->args = njs_mp_alloc(vm->mem_pool, sizeof(njs_value_t) * nargs);
        if (njs_slow_path(event->args == NULL)) {
            njs_memory_error(vm);
            return NJS_ERROR;
        }

        memcpy(event->args, args, sizeof(njs_value_t) * nargs);

        event->nargs = nargs;
    }

    njs_queue_insert_tail(&vm->jobs, &event->link);

    return NJS_OK;
}


njs_int_t
njs_vm_start(njs_vm_t *vm, njs_value_t *retval)
{
    njs_int_t  ret;

    ret = njs_vmcode_interpreter(vm, vm->start, retval, NULL, NULL);

    return (ret == NJS_ERROR) ? NJS_ERROR : NJS_OK;
}


njs_int_t
njs_vm_execute_pending_job(njs_vm_t *vm)
{
    njs_int_t         ret;
    njs_event_t       *ev;
    njs_queue_t       *jobs;
    njs_queue_link_t  *link;

    jobs = &vm->jobs;

    link = njs_queue_first(jobs);

    if (link == njs_queue_tail(jobs)) {
        return NJS_OK;
    }

    ev = njs_queue_link_data(link, njs_event_t, link);

    njs_queue_remove(&ev->link);

    ret = njs_vm_call(vm, ev->function, ev->args, ev->nargs);
    if (ret == NJS_ERROR) {
        return ret;
    }

    return 1;
}


void
njs_vm_set_module_loader(njs_vm_t *vm, njs_module_loader_t module_loader,
    void *opaque)
{
    vm->module_loader = module_loader;
    vm->module_loader_opaque = opaque;
}


void
njs_vm_set_rejection_tracker(njs_vm_t *vm,
    njs_rejection_tracker_t rejection_tracker, void *opaque)
{
    vm->rejection_tracker = rejection_tracker;
    vm->rejection_tracker_opaque = opaque;
}


njs_value_t
njs_vm_exception(njs_vm_t *vm)
{
    njs_value_t  value;

    value = vm->exception;
    njs_set_invalid(&vm->exception);

    return value;
}


void
njs_vm_exception_get(njs_vm_t *vm, njs_value_t *retval)
{
    *retval = njs_vm_exception(vm);
}


njs_mp_t *
njs_vm_memory_pool(njs_vm_t *vm)
{
    return vm->mem_pool;
}


njs_external_ptr_t
njs_vm_external_ptr(njs_vm_t *vm)
{
    return vm->external;
}


njs_bool_t
njs_vm_constructor(njs_vm_t *vm)
{
    return vm->top_frame->ctor;
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


njs_vm_opt_t *
njs_vm_options(njs_vm_t *vm)
{
    return &vm->options;
}


void
njs_vm_throw(njs_vm_t *vm, const njs_value_t *value)
{
    vm->exception = *value;
}


void
njs_vm_error2(njs_vm_t *vm, unsigned error_type, const char *fmt, ...)
{
    va_list  args;

    if (error_type > (NJS_OBJ_TYPE_ERROR_MAX - NJS_OBJ_TYPE_ERROR)) {
        return;
    }

    va_start(args, fmt);
    error_type += NJS_OBJ_TYPE_ERROR;
    njs_throw_error_va(vm, njs_vm_proto(vm, error_type), fmt, args);
    va_end(args);
}


void
njs_vm_error3(njs_vm_t *vm, unsigned type, const char *fmt, ...)
{
    va_list  args;

    if (type > vm->constructors_size) {
        return;
    }

    va_start(args, fmt);
    njs_throw_error_va(vm, njs_vm_proto(vm, type), fmt, args);
    va_end(args);
}


njs_int_t
njs_vm_global(njs_vm_t *vm, njs_value_t *retval)
{
    njs_value_assign(retval, &vm->global_value);
    return NJS_OK;
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

    njs_value_assign(&value, &vm->global_value);

    for ( ;; ) {
        p = njs_strlchr(start, end, '.');

        size = ((p != NULL) ? p : end) - start;
        if (njs_slow_path(size == 0)) {
            njs_type_error(vm, "empty path element");
            return NJS_ERROR;
        }

        ret = njs_atom_string_create(vm, &key, start, size);
        if (njs_slow_path(ret != NJS_OK)) {
            return NJS_ERROR;
        }

        ret = njs_value_property(vm, &value, key.atom_id,
                                 njs_value_arg(retval));
        if (njs_slow_path(ret == NJS_ERROR)) {
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


static njs_int_t
njs_vm_bind2(njs_vm_t *vm, const njs_str_t *var_name, njs_object_prop_t *prop,
    njs_bool_t shared)
{
    njs_int_t            ret;
    njs_value_t          prop_name;
    njs_object_t         *global;
    njs_flathsh_t        *hash;
    njs_flathsh_query_t  lhq;

    ret = njs_atom_string_create(vm, &prop_name, var_name->start,
                                 var_name->length);
    if (njs_slow_path(ret != NJS_OK)) {
        return NJS_ERROR;
    }

    lhq.value = prop;
    lhq.key_hash = prop_name.atom_id;
    lhq.replace = 1;
    lhq.pool = vm->mem_pool;
    lhq.proto = &njs_object_hash_proto;

    global = &vm->global_object;
    hash = shared ? &global->shared_hash : &global->hash;

    ret = njs_flathsh_unique_insert(hash, &lhq);
    if (njs_slow_path(ret != NJS_OK)) {
        njs_internal_error(vm, "lvlhsh insert failed");
        return ret;
    }

    return NJS_OK;
}


njs_int_t
njs_vm_bind(njs_vm_t *vm, const njs_str_t *var_name, const njs_value_t *value,
    njs_bool_t shared)
{
    njs_object_prop_t   *prop;

    prop = njs_object_prop_alloc(vm, value, 1);
    if (njs_slow_path(prop == NULL)) {
        return NJS_ERROR;
    }

    return njs_vm_bind2(vm, var_name, prop, shared);
}


njs_int_t
njs_vm_bind_handler(njs_vm_t *vm, const njs_str_t *var_name,
    njs_prop_handler_t handler, uint16_t magic16, uint32_t magic32,
    njs_bool_t shared)
{
    njs_object_prop_t  *prop;

    prop = njs_object_prop_alloc(vm, &njs_value_invalid, 1);
    if (njs_slow_path(prop == NULL)) {
        return NJS_ERROR;
    }

    prop->type = NJS_PROPERTY_HANDLER;
    prop->u.value.type = NJS_INVALID;
    prop->u.value.data.truth = 1;
    njs_prop_magic16(prop) = magic16;
    njs_prop_magic32(prop) = magic32;
    njs_prop_handler(prop) = handler;

    return njs_vm_bind2(vm, var_name, prop, shared);
}


void
njs_value_string_get(njs_vm_t *vm, njs_value_t *value, njs_str_t *dst)
{
    njs_string_get(vm, value, dst);
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


njs_int_t
njs_vm_value_string_create(njs_vm_t *vm, njs_value_t *value,
    const u_char *start, uint32_t size)
{
    return njs_string_create(vm, value, start, size);
}


njs_int_t
njs_vm_value_string_create_chb(njs_vm_t *vm, njs_value_t *value,
    njs_chb_t *chain)
{
    return njs_string_create_chb(vm, value, chain);
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
    return njs_prop_magic16(prop);
}


uint32_t
njs_vm_prop_magic32(njs_object_prop_t *prop)
{
    return njs_prop_magic32(prop);
}


njs_int_t
njs_vm_prop_name(njs_vm_t *vm, uint32_t atom_id, njs_str_t *dst)
{
    njs_int_t    ret;
    njs_value_t  prop_name;

    ret = njs_atom_to_value(vm, &prop_name, atom_id);
    if (ret != NJS_OK) {
        return NJS_ERROR;
    }

    if (njs_slow_path(!njs_is_string(&prop_name))) {
        return NJS_ERROR;
    }

    njs_string_get(vm, &prop_name, dst);

    return NJS_OK;
}


njs_noinline void
njs_vm_memory_error(njs_vm_t *vm)
{
    njs_memory_error_set(vm, &vm->exception);
}


njs_int_t
njs_vm_value_string(njs_vm_t *vm, njs_str_t *dst, njs_value_t *src)
{
    njs_int_t    ret;
    njs_uint_t   exception;
    njs_value_t  value;

    if (njs_slow_path(vm->top_frame == NULL)) {
        /* An exception was thrown during compilation. */
        njs_vm_runtime_init(vm);
    }

    if (njs_is_valid(&vm->exception)) {
        value = njs_vm_exception(vm);
        src = &value;
    }

    if (njs_slow_path(src->type == NJS_NUMBER
                      && njs_number(src) == 0
                      && signbit(njs_number(src))))
    {
        *dst = njs_str_value("-0");
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

        *src = njs_vm_exception(vm);
        goto again;
    }

    dst->length = 0;
    dst->start = NULL;

    return NJS_ERROR;
}


njs_int_t
njs_vm_exception_string(njs_vm_t *vm, njs_str_t *dst)
{
    njs_value_t  exception;

    exception = njs_vm_exception(vm);

    return njs_vm_value_string(vm, dst, &exception);
}


njs_value_t *
njs_vm_value_enumerate(njs_vm_t *vm, njs_value_t *value, uint32_t flags,
    njs_value_t *retval)
{
    njs_value_t          *val;
    njs_array_t          *keys;
    njs_rbtree_t         *variables;
    njs_rbtree_node_t    *rb_node;
    njs_variable_node_t  *node;

    keys = njs_value_enumerate(vm, value, flags);
    if (njs_slow_path(keys == NULL)) {
        return NULL;
    }

    if (!njs_values_same(vm, value, &vm->global_value)
        || vm->global_scope == NULL)
    {
        goto done;
    }

    /* TODO: workaround for values in global object. */

    variables = &vm->global_scope->variables;
    rb_node = njs_rbtree_min(variables);

    while (njs_rbtree_is_there_successor(variables, rb_node)) {
        node = (njs_variable_node_t *) rb_node;

        if (node->variable->atom_id == NJS_ATOM_STRING_this) {
            rb_node = njs_rbtree_node_successor(variables, rb_node);
            continue;
        }

        val = njs_array_push(vm, keys);
        if (njs_slow_path(value == NULL)) {
            return NULL;
        }

        njs_atom_to_value(vm, val, node->variable->atom_id);

        rb_node = njs_rbtree_node_successor(variables, rb_node);
    }

done:

     njs_set_array(retval, keys);

     return retval;
}


njs_value_t *
njs_vm_value_own_enumerate(njs_vm_t *vm, njs_value_t *value, uint32_t flags,
    njs_value_t *retval)
{
    njs_array_t  *keys;

    keys = njs_value_own_enumerate(vm, value, flags);
    if (njs_slow_path(keys == NULL)) {
        return NULL;
    }

    njs_set_array(retval, keys);

    return retval;
}


njs_int_t
njs_vm_object_alloc(njs_vm_t *vm, njs_value_t *retval, ...)
{
    va_list              args;
    njs_int_t            ret;
    njs_value_t          *name, *value;
    njs_object_t         *object;
    njs_object_prop_t    *prop;
    njs_flathsh_query_t  lhq;

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

        prop = njs_object_prop_alloc(vm, value, 1);
        if (njs_slow_path(prop == NULL)) {
            goto done;
        }

        if (name->atom_id == NJS_ATOM_STRING_unknown) {
            ret = njs_atom_atomize_key(vm, name);
            if (ret != NJS_OK) {
                goto done;
            }
        }

        lhq.value = prop;
        lhq.key_hash = name->atom_id;
        lhq.replace = 0;
        lhq.pool = vm->mem_pool;
        lhq.proto = &njs_object_hash_proto;

        ret = njs_flathsh_unique_insert(&object->hash, &lhq);
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

    keys = njs_value_own_enumerate(vm, value, NJS_ENUM_KEYS | NJS_ENUM_STRING
                                   | NJS_ENUM_ENUMERABLE_ONLY);
    if (njs_slow_path(keys == NULL)) {
        return NULL;
    }

    njs_set_array(retval, keys);

    return retval;
}


njs_int_t
njs_vm_prototype(njs_vm_t *vm, njs_value_t *value, njs_value_t *retval)
{
    njs_value_t arguments[2];

    njs_set_undefined(&arguments[0]);
    njs_value_assign(&arguments[1], value);

    return njs_object_get_prototype_of(vm, njs_value_arg(&arguments), 2, 0,
                                       retval);
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

    ret = njs_vm_value_string_create(vm, &key, prop->start, prop->length);
    if (njs_slow_path(ret != NJS_OK)) {
        return NULL;
    }

    ret = njs_value_property_val(vm, value, &key, njs_value_arg(retval));
    if (njs_slow_path(ret != NJS_OK)) {
        return NULL;
    }

    return njs_value_arg(retval);
}


njs_int_t
njs_vm_object_prop_set(njs_vm_t *vm, njs_value_t *value, const njs_str_t *prop,
    njs_opaque_value_t *setval)
{
    njs_int_t    ret;
    njs_value_t  key;

    if (njs_slow_path(!njs_is_object(value))) {
        njs_type_error(vm, "njs_vm_object_prop_set() argument is not object");
        return NJS_ERROR;
    }

    ret = njs_atom_string_create(vm, &key, prop->start, prop->length);
    if (njs_slow_path(ret != NJS_OK)) {
        return NJS_ERROR;
    }

    ret = njs_value_property_set(vm, value, key.atom_id,
                                 njs_value_arg(setval));
    if (njs_slow_path(ret != NJS_OK)) {
        return NJS_ERROR;
    }

    return NJS_OK;
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


njs_int_t
njs_vm_object_iterate(njs_vm_t *vm, njs_iterator_args_t *args,
    njs_iterator_handler_t handler, njs_value_t *retval)
{
    return njs_object_iterate(vm, args, handler, retval);
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
njs_vm_date_alloc(njs_vm_t *vm, njs_value_t *retval, double time)
{
    njs_date_t  *date;

    date = njs_date_alloc(vm, time);
    if (njs_slow_path(date == NULL)) {
        return NJS_ERROR;
    }

    njs_set_date(retval, date);

    return NJS_OK;
}


njs_int_t
njs_vm_value_to_string(njs_vm_t *vm, njs_str_t *dst, njs_value_t *src)
{
    njs_int_t    ret;
    njs_value_t  value, stack;

    if (njs_slow_path(src == NULL)) {
        return NJS_ERROR;
    }

    if (njs_is_error(src)) {
        if (njs_is_memory_error(vm, src)) {
            *dst = njs_str_value("MemoryError");
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
        njs_string_get(vm, &value, dst);
    }

    return ret;
}


/*
 * If string value is null-terminated the corresponding C string
 * is returned as is, otherwise the new copy is allocated with
 * the terminating zero byte.
 */
const char *
njs_vm_value_to_c_string(njs_vm_t *vm, njs_value_t *value)
{
    u_char  *p, *data;
    size_t  size;

    njs_assert(njs_is_string(value));

    size = value->string.data->size;

    data = njs_mp_alloc(vm->mem_pool, size + njs_length("\0"));
    if (njs_slow_path(data == NULL)) {
        njs_memory_error(vm);
        return NULL;
    }

    p = njs_cpymem(data, value->string.data->start, size);
    *p++ = '\0';

    return (const char *) data;
}


njs_int_t
njs_value_to_string(njs_vm_t *vm, njs_value_t *dst, njs_value_t *value)
{
    njs_int_t    ret;
    njs_value_t  primitive;

    if (njs_slow_path(!njs_is_primitive(value))) {
        if (njs_slow_path(njs_is_object_symbol(value))) {
            /* should fail */
            value = njs_object_value(value);

        } else {
            ret = njs_value_to_primitive(vm, &primitive, value, 1);
            if (njs_slow_path(ret != NJS_OK)) {
                return ret;
            }

            value = &primitive;
        }
    }

    return njs_primitive_value_to_string(vm, dst, value);
}


njs_int_t
njs_vm_value_to_bytes(njs_vm_t *vm, njs_str_t *dst, njs_value_t *src)
{
    size_t              length, offset;
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
            if (length == 0) {
                dst->length = 0;
                dst->start = NULL;
                return NJS_OK;
            }

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

        dst->length = value.string.data->size;
        dst->start = value.string.data->start;
    }

    return ret;
}


njs_int_t
njs_vm_string_compare(njs_vm_t *vm, const njs_value_t *v1,
    const njs_value_t *v2)
{
    return njs_string_cmp(vm, v1, v2);
}


void *
njs_lvlhsh_alloc(void *data, size_t size)
{
    return njs_mp_align(data, NJS_MAX_ALIGNMENT, size);
}


void
njs_lvlhsh_free(void *data, void *p, size_t size)
{
    njs_mp_free(data, p);
}
