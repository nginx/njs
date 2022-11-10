
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) NGINX, Inc.
 */


#include <njs_main.h>


njs_function_t *
njs_function_alloc(njs_vm_t *vm, njs_function_lambda_t *lambda,
    njs_bool_t async)
{
    size_t          size;
    njs_object_t    *proto;
    njs_function_t  *function;

    size = sizeof(njs_function_t) + lambda->nclosures * sizeof(njs_value_t *);

    function = njs_mp_zalloc(vm->mem_pool, size);
    if (njs_slow_path(function == NULL)) {
        goto fail;
    }

    /*
     * njs_mp_zalloc() does also:
     *   njs_lvlhsh_init(&function->object.hash);
     *   function->object.__proto__ = NULL;
     */

    function->ctor = lambda->ctor;
    function->u.lambda = lambda;

    if (function->ctor) {
        function->object.shared_hash = vm->shared->function_instance_hash;

    } else if (async) {
        function->object.shared_hash = vm->shared->async_function_instance_hash;

    } else {
        function->object.shared_hash = vm->shared->arrow_instance_hash;
    }

    if (async) {
        proto = &vm->prototypes[NJS_OBJ_TYPE_ASYNC_FUNCTION].object;

    } else {
        proto = &vm->prototypes[NJS_OBJ_TYPE_FUNCTION].object;
    }

    function->object.__proto__ = proto;
    function->object.type = NJS_FUNCTION;

    function->object.extensible = 1;

    return function;

fail:

    njs_memory_error(vm);

    return NULL;
}


njs_function_t *
njs_vm_function_alloc(njs_vm_t *vm, njs_function_native_t native)
{
    njs_function_t  *function;

    function = njs_mp_zalloc(vm->mem_pool, sizeof(njs_function_t));
    if (njs_slow_path(function == NULL)) {
        njs_memory_error(vm);
        return NULL;
    }

    function->native = 1;
    function->u.native = native;

    return function;
}


njs_function_t *
njs_function_value_copy(njs_vm_t *vm, njs_value_t *value)
{
    njs_function_t  *function, *copy;

    function = njs_function(value);

    if (!function->object.shared) {
        return function;
    }

    copy = njs_function_copy(vm, function);
    if (njs_slow_path(copy == NULL)) {
        njs_memory_error(vm);
        return NULL;
    }

    value->data.u.function = copy;

    return copy;
}


njs_int_t
njs_function_name_set(njs_vm_t *vm, njs_function_t *function,
    njs_value_t *name, const char *prefix)
{
    u_char              *p;
    size_t              len, symbol;
    njs_int_t           ret;
    njs_value_t         value;
    njs_string_prop_t   string;
    njs_object_prop_t   *prop;
    njs_lvlhsh_query_t  lhq;

    prop = njs_object_prop_alloc(vm, &njs_string_name, name, 0);
    if (njs_slow_path(prop == NULL)) {
        return NJS_ERROR;
    }

    symbol = 0;

    if (njs_is_symbol(njs_prop_value(prop))) {
        symbol = 2;
        njs_value_assign(njs_prop_value(prop),
                        njs_symbol_description(njs_prop_value(prop)));
    }

    if (prefix != NULL || symbol != 0) {
        if (njs_is_defined(njs_prop_value(prop))) {
            njs_value_assign(&value, njs_prop_value(prop));
            (void) njs_string_prop(&string, &value);

            len = (prefix != NULL) ? njs_strlen(prefix) + 1: 0;
            p = njs_string_alloc(vm, njs_prop_value(prop),
                                 string.size + len + symbol,
                                 string.length + len + symbol);
            if (njs_slow_path(p == NULL)) {
                return NJS_ERROR;
            }

            if (len != 0) {
                p = njs_cpymem(p, prefix, len - 1);
                *p++ = ' ';
            }

            if (symbol != 0) {
                *p++ = '[';
            }

            p = njs_cpymem(p, string.start, string.size);

            if (symbol != 0) {
                *p++ = ']';
            }

        } else {
            njs_value_assign(njs_prop_value(prop), &njs_string_empty);
        }
    }

    prop->configurable = 1;

    lhq.key_hash = NJS_NAME_HASH;
    lhq.key = njs_str_value("name");
    lhq.replace = 0;
    lhq.value = prop;
    lhq.proto = &njs_object_hash_proto;
    lhq.pool = vm->mem_pool;

    ret = njs_lvlhsh_insert(&function->object.hash, &lhq);
    if (njs_slow_path(ret != NJS_OK)) {
        njs_internal_error(vm, "lvlhsh insert failed");
        return NJS_ERROR;
    }

    return NJS_OK;
}


njs_function_t *
njs_function_copy(njs_vm_t *vm, njs_function_t *function)
{
    size_t             size, n;
    njs_value_t        **from, **to;
    njs_function_t     *copy;
    njs_object_type_t  type;

    n = (function->native) ? 0 : function->u.lambda->nclosures;

    size = sizeof(njs_function_t) + n * sizeof(njs_value_t *);

    copy = njs_mp_alloc(vm->mem_pool, size);
    if (njs_slow_path(copy == NULL)) {
        return NULL;
    }

    *copy = *function;

    type = njs_function_object_type(vm, function);

    copy->object.__proto__ = &vm->prototypes[type].object;
    copy->object.shared = 0;

    if (copy->ctor) {
        copy->object.shared_hash = vm->shared->function_instance_hash;

    } else if (type == NJS_OBJ_TYPE_ASYNC_FUNCTION) {
        copy->object.shared_hash = vm->shared->async_function_instance_hash;

    } else {
        copy->object.shared_hash = vm->shared->arrow_instance_hash;
    }

    if (n == 0) {
        return copy;
    }

    from = njs_function_closures(function);
    to = njs_function_closures(copy);

    do {
        n--;

        to[n] = from[n];

    } while (n != 0);

    return copy;
}


njs_int_t
njs_function_arguments_object_init(njs_vm_t *vm, njs_native_frame_t *frame)
{
    njs_int_t     ret;
    njs_uint_t    n;
    njs_value_t   value, length;
    njs_object_t  *arguments;

    static const njs_value_t  string_length = njs_string("length");

    arguments = njs_object_alloc(vm);
    if (njs_slow_path(arguments == NULL)) {
        return NJS_ERROR;
    }

    arguments->shared_hash = vm->shared->arguments_object_instance_hash;

    njs_set_object(&value, arguments);
    njs_set_number(&length, frame->nargs);

    ret = njs_object_prop_define(vm, &value, njs_value_arg(&string_length),
                                 &length, NJS_OBJECT_PROP_VALUE_CW,
                                 NJS_LENGTH_HASH);
    if (njs_slow_path(ret != NJS_OK)) {
        return NJS_ERROR;
    }

    for (n = 0; n < frame->nargs; n++) {
        ret = njs_value_create_data_prop_i64(vm, &value, n,
                                             &frame->arguments[n], 0);
        if (njs_slow_path(ret != NJS_OK)) {
            return NJS_ERROR;
        }
    }

    frame->arguments_object = arguments;

    return NJS_OK;
}


njs_int_t
njs_function_rest_parameters_init(njs_vm_t *vm, njs_native_frame_t *frame)
{
    uint32_t     length;
    njs_uint_t   nargs, n, i;
    njs_array_t  *array;
    njs_value_t  *rest_arguments;

    nargs = frame->nargs;
    n = frame->function->u.lambda->nargs;
    length = (nargs >= n) ? (nargs - n + 1) : 0;

    array = njs_array_alloc(vm, 1, length, 0);
    if (njs_slow_path(array == NULL)) {
        return NJS_ERROR;
    }

    for (i = 0; i < length; i++) {
        array->start[i] = frame->arguments[i + n - 1];
    }

    rest_arguments = njs_mp_alloc(vm->mem_pool, sizeof(njs_value_t));
    if (njs_slow_path(rest_arguments == NULL)) {
        return NJS_ERROR;
    }

    /* GC: retain. */
    njs_set_array(rest_arguments, array);

    vm->top_frame->local[n] = rest_arguments;

    return NJS_OK;
}


static njs_int_t
njs_function_prototype_thrower(njs_vm_t *vm, njs_value_t *args,
    njs_uint_t nargs, njs_index_t unused)
{
    njs_type_error(vm, "\"caller\", \"callee\", \"arguments\" "
                   "properties may not be accessed");
    return NJS_ERROR;
}


const njs_object_prop_t  njs_arguments_object_instance_properties[] =
{
    {
        .type = NJS_ACCESSOR,
        .name = njs_string("callee"),
        .u.accessor = njs_accessor(njs_function_prototype_thrower, 0,
                                   njs_function_prototype_thrower, 0),
        .writable = NJS_ATTRIBUTE_UNSET,
    },
};


const njs_object_init_t  njs_arguments_object_instance_init = {
    njs_arguments_object_instance_properties,
    njs_nitems(njs_arguments_object_instance_properties),
};


njs_int_t
njs_function_native_frame(njs_vm_t *vm, njs_function_t *function,
    const njs_value_t *this, const njs_value_t *args, njs_uint_t nargs,
    njs_bool_t ctor)
{
    size_t              size;
    njs_value_t         *value;
    njs_native_frame_t  *frame;

    size = NJS_NATIVE_FRAME_SIZE + (1 /* this */ + nargs) * sizeof(njs_value_t);

    frame = njs_function_frame_alloc(vm, size);
    if (njs_slow_path(frame == NULL)) {
        return NJS_ERROR;
    }

    frame->function = function;
    frame->nargs = nargs;
    frame->ctor = ctor;
    frame->native = 1;
    frame->pc = NULL;

    value = (njs_value_t *) ((u_char *) frame + NJS_NATIVE_FRAME_SIZE);

    njs_value_assign(value++, this++);

    frame->arguments = value;

    if (args != NULL) {
        memcpy(value, args, nargs * sizeof(njs_value_t));
    }

    return NJS_OK;
}


njs_int_t
njs_function_lambda_frame(njs_vm_t *vm, njs_function_t *function,
    const njs_value_t *this, const njs_value_t *args, njs_uint_t nargs,
    njs_bool_t ctor)
{
    size_t                 n, frame_size;
    uint32_t               args_count, value_count, value_size;
    njs_value_t            *value, **new;
    njs_frame_t            *frame;
    njs_native_frame_t     *native_frame;
    njs_function_lambda_t  *lambda;

    lambda = function->u.lambda;

    args_count = njs_max(nargs, lambda->nargs);
    value_count = args_count + lambda->nlocal;

    value_size = value_count * sizeof(njs_value_t *);

    frame_size = value_size + (value_count * sizeof(njs_value_t));

    native_frame = njs_function_frame_alloc(vm, NJS_FRAME_SIZE + frame_size);
    if (njs_slow_path(native_frame == NULL)) {
        return NJS_ERROR;
    }

    /* Local */

    new = (njs_value_t **) ((u_char *) native_frame + NJS_FRAME_SIZE);
    value = (njs_value_t *) ((u_char *) new + value_size);

    n = value_count;

    while (n != 0) {
        n--;
        new[n] = &value[n];
        njs_set_invalid(new[n]);
    }

    native_frame->arguments = value;
    native_frame->local = new + args_count;
    native_frame->function = function;
    native_frame->nargs = nargs;
    native_frame->ctor = ctor;
    native_frame->native = 0;
    native_frame->pc = NULL;

    /* Set this and bound arguments. */
    *native_frame->local[0] = *this;

    if (njs_slow_path(function->global_this
                      && njs_is_null_or_undefined(this)))
    {
        njs_set_object(native_frame->local[0], &vm->global_object);
    }

    /* Copy arguments. */

    if (args != NULL) {
        while (nargs != 0) {
            njs_value_assign(value++, args++);
            nargs--;
        }
    }

    frame = (njs_frame_t *) native_frame;
    frame->exception.catch = NULL;
    frame->exception.next = NULL;
    frame->previous_active_frame = vm->active_frame;

    return NJS_OK;
}


njs_native_frame_t *
njs_function_frame_alloc(njs_vm_t *vm, size_t size)
{
    size_t              spare_size, chunk_size;
    njs_native_frame_t  *frame;

    spare_size = vm->top_frame ? vm->top_frame->free_size : 0;

    if (njs_fast_path(size <= spare_size)) {
        frame = (njs_native_frame_t *) vm->top_frame->free;
        chunk_size = 0;

    } else {
        spare_size = size + NJS_FRAME_SPARE_SIZE;
        spare_size = njs_align_size(spare_size, NJS_FRAME_SPARE_SIZE);

        if (spare_size > vm->spare_stack_size) {
            njs_range_error(vm, "Maximum call stack size exceeded");
            return NULL;
        }

        frame = njs_mp_align(vm->mem_pool, sizeof(njs_value_t), spare_size);
        if (njs_slow_path(frame == NULL)) {
            njs_memory_error(vm);
            return NULL;
        }

        chunk_size = spare_size;
        vm->spare_stack_size -= spare_size;
    }

    njs_memzero(frame, sizeof(njs_native_frame_t));

    frame->size = chunk_size;
    frame->free_size = spare_size - size;
    frame->free = (u_char *) frame + size;

    frame->previous = vm->top_frame;
    vm->top_frame = frame;

    return frame;
}


njs_int_t
njs_function_call2(njs_vm_t *vm, njs_function_t *function,
    const njs_value_t *this, const njs_value_t *args,
    njs_uint_t nargs, njs_value_t *retval, njs_bool_t ctor)
{
    njs_int_t  ret;

    ret = njs_function_frame(vm, function, this, args, nargs, ctor);
    if (njs_slow_path(ret != NJS_OK)) {
        return ret;
    }

    return njs_function_frame_invoke(vm, retval);
}


njs_int_t
njs_function_lambda_call(njs_vm_t *vm, void *promise_cap)
{
    uint32_t               n;
    njs_int_t              ret;
    njs_frame_t            *frame;
    njs_value_t            *args, **local, *value;
    njs_value_t            **cur_local, **cur_closures;
    njs_function_t         *function;
    njs_declaration_t      *declr;
    njs_function_lambda_t  *lambda;

    frame = (njs_frame_t *) vm->top_frame;
    function = frame->native.function;

    njs_assert(function->context == NULL);

    if (function->global && !function->closure_copied) {
        ret = njs_function_capture_global_closures(vm, function);
        if (njs_slow_path(ret != NJS_OK)) {
            return NJS_ERROR;
        }
    }

    lambda = function->u.lambda;

    args = vm->top_frame->arguments;
    local = vm->top_frame->local + 1 /* this */;

    /* Move all arguments. */

    for (n = 0; n < function->args_count; n++) {
        if (!njs_is_valid(args)) {
            njs_set_undefined(args);
        }

        *local++ = args++;
    }

    /* Store current level. */

    cur_local = vm->levels[NJS_LEVEL_LOCAL];
    cur_closures = vm->levels[NJS_LEVEL_CLOSURE];

    /* Replace current level. */

    vm->levels[NJS_LEVEL_LOCAL] = vm->top_frame->local;
    vm->levels[NJS_LEVEL_CLOSURE] = njs_function_closures(function);

    if (lambda->rest_parameters) {
        ret = njs_function_rest_parameters_init(vm, &frame->native);
        if (njs_slow_path(ret != NJS_OK)) {
            return NJS_ERROR;
        }
    }

    /* Self */

    if (lambda->self != NJS_INDEX_NONE) {
        value = njs_scope_value(vm, lambda->self);

        if (!njs_is_valid(value)) {
            njs_set_function(value, function);
        }
    }

    vm->active_frame = frame;

    /* Closures */

    n = lambda->ndeclarations;

    while (n != 0) {
        n--;

        declr = &lambda->declarations[n];
        value = njs_scope_value(vm, declr->index);

        *value = *declr->value;

        function = njs_function_value_copy(vm, value);
        if (njs_slow_path(function == NULL)) {
            return NJS_ERROR;
        }

        ret = njs_function_capture_closure(vm, function, function->u.lambda);
        if (njs_slow_path(ret != NJS_OK)) {
            return ret;
        }
    }

    ret = njs_vmcode_interpreter(vm, lambda->start, promise_cap, NULL);

    /* Restore current level. */
    vm->levels[NJS_LEVEL_LOCAL] = cur_local;
    vm->levels[NJS_LEVEL_CLOSURE] = cur_closures;

    return ret;
}


njs_int_t
njs_function_native_call(njs_vm_t *vm)
{
    njs_int_t              ret;
    njs_function_t         *function;
    njs_native_frame_t     *native, *previous;
    njs_function_native_t  call;

    native = vm->top_frame;
    function = native->function;

#ifdef NJS_DEBUG_OPCODE
    njs_str_t              name;

    if (vm->options.opcode_debug) {

        ret = njs_builtin_match_native_function(vm, function, &name);
        if (ret != NJS_OK) {
           name = njs_str_value("unmapped");
        }

        njs_printf("CALL NATIVE %V %P\n", &name, function->u.native);
    }
#endif

    call = function->u.native;

    ret = call(vm, &native->arguments[-1], 1 /* this */ + native->nargs,
               function->magic8);

#ifdef NJS_DEBUG_OPCODE
    if (vm->options.opcode_debug) {
        njs_printf("CALL NATIVE RETCODE: %i %V %P\n", ret, &name,
                   function->u.native);
    }
#endif

    if (njs_slow_path(ret == NJS_ERROR)) {
        return ret;
    }

    if (ret == NJS_DECLINED) {
        return NJS_OK;
    }

    previous = njs_function_previous_frame(native);

    njs_vm_scopes_restore(vm, native, previous);

    if (!native->skip) {
        *native->retval = vm->retval;
    }

    njs_function_frame_free(vm, native);

    return NJS_OK;
}


njs_int_t
njs_function_frame_invoke(njs_vm_t *vm, njs_value_t *retval)
{
    njs_native_frame_t  *frame;

    frame = vm->top_frame;
    frame->retval = retval;

    if (njs_function_object_type(vm, frame->function)
        == NJS_OBJ_TYPE_ASYNC_FUNCTION)
    {
        return njs_async_function_frame_invoke(vm, retval);
    }

    if (frame->native) {
        return njs_function_native_call(vm);

    } else {
        return njs_function_lambda_call(vm, NULL);
    }
}


void
njs_function_frame_free(njs_vm_t *vm, njs_native_frame_t *native)
{
    njs_native_frame_t  *previous;

    do {
        previous = native->previous;

        /* GC: free frame->local, etc. */

        if (native->size != 0) {
            vm->spare_stack_size += native->size;
            njs_mp_free(vm->mem_pool, native);
        }

        native = previous;
    } while (native->skip);
}


njs_int_t
njs_function_frame_save(njs_vm_t *vm, njs_frame_t *frame, u_char *pc)
{
    size_t              args_count, value_count, n;
    njs_value_t         *start, *end, *p, **new, *value, **local;
    njs_function_t      *function;
    njs_function_lambda_t  *lambda;
    njs_native_frame_t  *active, *native;

    *frame = *vm->active_frame;

    frame->previous_active_frame = NULL;

    native = &frame->native;
    native->size = 0;
    native->free = NULL;
    native->free_size = 0;

    active = &vm->active_frame->native;
    function = active->function;
    lambda = function->u.lambda;

    args_count = njs_max(native->nargs, lambda->nargs);
    value_count = args_count + lambda->nlocal;

    new = (njs_value_t **) ((u_char *) native + NJS_FRAME_SIZE);
    value = (njs_value_t *) (new + value_count);

    native->arguments = value;
    native->local = new + njs_function_frame_args_count(active);
    native->pc = pc;

    start = njs_function_frame_values(active, &end);
    p = native->arguments;

    while (start < end) {
        njs_value_assign(p, start++);
        *new++ = p++;
    }

    /* Move all arguments. */

    p = native->arguments;
    local = native->local + 1 /* this */;

    for (n = 0; n < function->args_count; n++) {
        if (!njs_is_valid(p)) {
            njs_set_undefined(p);
        }

        *local++ = p++;
    }

    return NJS_OK;
}


njs_object_type_t
njs_function_object_type(njs_vm_t *vm, njs_function_t *function)
{
    if (function->object.shared_hash.slot
        == vm->shared->async_function_instance_hash.slot)
    {
        return NJS_OBJ_TYPE_ASYNC_FUNCTION;
    }

    return NJS_OBJ_TYPE_FUNCTION;
}


njs_int_t
njs_function_capture_closure(njs_vm_t *vm, njs_function_t *function,
    njs_function_lambda_t *lambda)
{
    void                *start, *end;
    uint32_t            n;
    njs_value_t         *value, **closure;
    njs_native_frame_t  *frame;

    if (lambda->nclosures == 0) {
        return NJS_OK;
    }

    frame = &vm->active_frame->native;

    while (frame->native) {
        frame = frame->previous;
    }

    start = frame;
    end = frame->free;

    closure = njs_function_closures(function);
    n = lambda->nclosures;

    do {
        n--;

        value = njs_scope_value(vm, lambda->closures[n]);

        if (start <= (void *) value && (void *) value < end) {
            value = njs_scope_value_clone(vm, lambda->closures[n], value);
            if (njs_slow_path(value == NULL)) {
                return NJS_ERROR;
            }
        }

        closure[n] = value;

    } while (n != 0);

    return NJS_OK;
}


njs_inline njs_value_t *
njs_function_closure_value(njs_vm_t *vm, njs_value_t **scope, njs_index_t index,
    void *start, void *end)
{
    njs_value_t  *value, *newval;

    value = scope[njs_scope_index_value(index)];

    if (start <= (void *) value && end > (void *) value) {
        newval = njs_mp_alloc(vm->mem_pool, sizeof(njs_value_t));
        if (njs_slow_path(newval == NULL)) {
            njs_memory_error(vm);
            return NULL;
        }

        *newval = *value;
        value = newval;
    }

    scope[njs_scope_index_value(index)] = value;

    return value;
}


njs_int_t
njs_function_capture_global_closures(njs_vm_t *vm, njs_function_t *function)
{
    void                   *start, *end;
    uint32_t               n;
    njs_value_t            *value, **refs, **global;
    njs_index_t            *indexes, index;
    njs_native_frame_t     *native;
    njs_function_lambda_t  *lambda;

    lambda = function->u.lambda;

    if (lambda->nclosures == 0) {
        return NJS_OK;
    }

    native = vm->top_frame;

    while (native->previous->function != NULL) {
        native = native->previous;
    }

    start = native;
    end = native->free;

    indexes = lambda->closures;
    refs = njs_function_closures(function);

    global = vm->levels[NJS_LEVEL_GLOBAL];

    n = lambda->nclosures;

    while (n > 0) {
        n--;

        index = indexes[n];

        switch (njs_scope_index_type(index)) {
        case NJS_LEVEL_LOCAL:
            value = njs_function_closure_value(vm, native->local, index,
                                               start, end);
            break;

        case NJS_LEVEL_GLOBAL:
            value = njs_function_closure_value(vm, global, index, start, end);
            break;

        default:
            njs_type_error(vm, "unexpected value type for closure \"%uD\"",
                           njs_scope_index_type(index));
            return NJS_ERROR;
        }

        if (njs_slow_path(value == NULL)) {
            return NJS_ERROR;
        }

        refs[n] = value;
    }

    function->closure_copied = 1;

    return NJS_OK;
}


static njs_value_t *
njs_function_property_prototype_set(njs_vm_t *vm, njs_lvlhsh_t *hash,
    njs_value_t *prototype)
{
    njs_int_t           ret;
    njs_object_prop_t   *prop;
    njs_lvlhsh_query_t  lhq;

    const njs_value_t  proto_string = njs_string("prototype");

    prop = njs_object_prop_alloc(vm, &proto_string, prototype, 0);
    if (njs_slow_path(prop == NULL)) {
        return NULL;
    }

    prop->writable = 1;

    lhq.value = prop;
    lhq.key_hash = NJS_PROTOTYPE_HASH;
    lhq.key = njs_str_value("prototype");
    lhq.replace = 1;
    lhq.pool = vm->mem_pool;
    lhq.proto = &njs_object_hash_proto;

    ret = njs_lvlhsh_insert(hash, &lhq);

    if (njs_fast_path(ret == NJS_OK)) {
        return njs_prop_value(prop);
    }

    njs_internal_error(vm, "lvlhsh insert failed");

    return NULL;
}


/*
 * The "prototype" property of user defined functions is created on
 * demand in private hash of the functions by the "prototype" getter.
 * The getter creates a copy of function which is private to nJSVM,
 * adds a "prototype" object property to the copy, and then adds a
 * "constructor" property in the prototype object.  The "constructor"
 * property points to the copy of function:
 *   "F.prototype.constructor === F"
 */

njs_int_t
njs_function_prototype_create(njs_vm_t *vm, njs_object_prop_t *prop,
    njs_value_t *value, njs_value_t *setval, njs_value_t *retval)
{
    njs_value_t     *proto, proto_value, *cons;
    njs_object_t    *prototype;
    njs_function_t  *function;

    if (setval == NULL) {
        prototype = njs_object_alloc(vm);
        if (njs_slow_path(prototype == NULL)) {
            return NJS_ERROR;
        }

        njs_set_object(&proto_value, prototype);

        setval = &proto_value;
    }

    function = njs_function_value_copy(vm, value);
    if (njs_slow_path(function == NULL)) {
        return NJS_ERROR;
    }

    proto = njs_function_property_prototype_set(vm, njs_object_hash(value),
                                                setval);
    if (njs_slow_path(proto == NULL)) {
        return NJS_ERROR;
    }

    if (setval == &proto_value && njs_is_object(proto)) {
        /* Only in getter context. */
        cons = njs_property_constructor_set(vm, njs_object_hash(proto), value);
        if (njs_slow_path(cons == NULL)) {
            return NJS_ERROR;
        }
    }

    *retval = *proto;

    return NJS_OK;
}


njs_int_t
njs_function_constructor(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t async)
{
    njs_chb_t               chain;
    njs_int_t               ret;
    njs_str_t               str, file;
    njs_uint_t              i;
    njs_parser_t            parser;
    njs_vm_code_t           *code;
    njs_function_t          *function;
    njs_generator_t         generator;
    njs_parser_node_t       *node;
    njs_function_lambda_t   *lambda;
    const njs_token_type_t  *type;

    static const njs_token_type_t  safe_ast[] = {
        NJS_TOKEN_END,
        NJS_TOKEN_FUNCTION_EXPRESSION,
        NJS_TOKEN_STATEMENT,
        NJS_TOKEN_RETURN,
        NJS_TOKEN_THIS,
        NJS_TOKEN_ILLEGAL
    };

    static const njs_token_type_t  safe_ast_async[] = {
        NJS_TOKEN_END,
        NJS_TOKEN_ASYNC_FUNCTION_EXPRESSION,
        NJS_TOKEN_STATEMENT,
        NJS_TOKEN_RETURN,
        NJS_TOKEN_THIS,
        NJS_TOKEN_ILLEGAL
    };

    if (!vm->options.unsafe && nargs != 2) {
        goto fail;
    }

    njs_chb_init(&chain, vm->mem_pool);

    if (async) {
        njs_chb_append_literal(&chain, "(async function(");

    } else {
        njs_chb_append_literal(&chain, "(function(");
    }

    for (i = 1; i < nargs - 1; i++) {
        ret = njs_value_to_chain(vm, &chain, njs_argument(args, i));
        if (njs_slow_path(ret < NJS_OK)) {
            return ret;
        }

        if (i != (nargs - 2)) {
            njs_chb_append_literal(&chain, ",");
        }
    }

    njs_chb_append_literal(&chain, "){");

    ret = njs_value_to_chain(vm, &chain, njs_argument(args, nargs - 1));
    if (njs_slow_path(ret < NJS_OK)) {
        return ret;
    }

    njs_chb_append_literal(&chain, "})");

    ret = njs_chb_join(&chain, &str);
    if (njs_slow_path(ret != NJS_OK)) {
        njs_memory_error(vm);
        return NJS_ERROR;
    }

    file = njs_str_value("runtime");

    ret = njs_parser_init(vm, &parser, NULL, &file, str.start,
                          str.start + str.length, 1);
    if (njs_slow_path(ret != NJS_OK)) {
        return ret;
    }

    ret = njs_parser(vm, &parser);
    if (njs_slow_path(ret != NJS_OK)) {
        return ret;
    }

    if (!vm->options.unsafe) {
        /*
         * Safe mode exception:
         * "(new Function('return this'))" is often used to get
         * the global object in a portable way.
         */

        node = parser.node;
        type = (async) ? &safe_ast_async[0] : &safe_ast[0];

        for (; *type != NJS_TOKEN_ILLEGAL; type++, node = node->right) {
            if (node == NULL) {
                goto fail;
            }

            if (node->left != NULL
                && node->token_type != NJS_TOKEN_FUNCTION_EXPRESSION
                && node->left->token_type != NJS_TOKEN_NAME)
            {
                goto fail;
            }

            if (node->token_type != *type) {
                goto fail;
            }
        }
    }

    ret = njs_generator_init(&generator, &file, 0, 1);
    if (njs_slow_path(ret != NJS_OK)) {
        njs_internal_error(vm, "njs_generator_init() failed");
        return NJS_ERROR;
    }

    code = njs_generate_scope(vm, &generator, parser.scope,
                              &njs_entry_anonymous);
    if (njs_slow_path(code == NULL)) {
        if (!njs_is_error(&vm->retval)) {
            njs_internal_error(vm, "njs_generate_scope() failed");
        }

        return NJS_ERROR;
    }

    njs_chb_destroy(&chain);

    lambda = ((njs_vmcode_function_t *) generator.code_start)->lambda;

    function = njs_function_alloc(vm, lambda, (njs_bool_t) async);
    if (njs_slow_path(function == NULL)) {
        return NJS_ERROR;
    }

    function->global = 1;
    function->global_this = 1;
    function->args_count = lambda->nargs - lambda->rest_parameters;

    ret = njs_function_name_set(vm, function,
                                njs_value_arg(&njs_string_anonymous), NULL);
    if (njs_slow_path(ret == NJS_ERROR)) {
        return ret;
    }

    njs_set_function(&vm->retval, function);

    return NJS_OK;

fail:

    njs_type_error(vm, "function constructor is disabled in \"safe\" mode");
    return NJS_ERROR;
}


static const njs_object_prop_t  njs_function_constructor_properties[] =
{
    NJS_DECLARE_PROP_NAME("Function"),

    NJS_DECLARE_PROP_LENGTH(1),

    NJS_DECLARE_PROP_HANDLER("prototype", njs_object_prototype_create, 0, 0, 0),
};


const njs_object_init_t  njs_function_constructor_init = {
    njs_function_constructor_properties,
    njs_nitems(njs_function_constructor_properties),
};


njs_int_t
njs_function_instance_length(njs_vm_t *vm, njs_object_prop_t *prop,
    njs_value_t *value, njs_value_t *setval, njs_value_t *retval)
{
    njs_function_t  *function;

    function = njs_object_proto_lookup(njs_object(value), NJS_FUNCTION,
                                     njs_function_t);
    if (njs_slow_path(function == NULL)) {
        njs_set_undefined(retval);
        return NJS_DECLINED;
    }

    njs_set_number(retval, function->args_count);

    return NJS_OK;
}


njs_int_t
njs_function_instance_name(njs_vm_t *vm, njs_object_prop_t *prop,
    njs_value_t *value, njs_value_t *setval, njs_value_t *retval)
{
    njs_function_t  *function;

    function = njs_object_proto_lookup(njs_object(value), NJS_FUNCTION,
                                     njs_function_t);
    if (njs_slow_path(function == NULL)) {
        njs_set_undefined(retval);
        return NJS_DECLINED;
    }

    if (!function->native) {
        njs_value_assign(retval, &function->u.lambda->name);
        return NJS_OK;
    }

    njs_value_assign(retval, &njs_string_empty);

    return NJS_OK;
}


static njs_int_t
njs_function_prototype_call(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t unused)
{
    njs_int_t           ret;
    njs_function_t      *function;
    const njs_value_t   *this;
    njs_native_frame_t  *frame;

    if (!njs_is_function(&args[0])) {
        njs_type_error(vm, "\"this\" argument is not a function");
        return NJS_ERROR;
    }

    if (nargs > 1) {
        this = &args[1];
        nargs -= 2;

    } else {
        this = (njs_value_t *) &njs_value_undefined;
        nargs = 0;
    }

    frame = vm->top_frame;

    /* Skip the "call" method frame. */
    frame->skip = 1;

    function = njs_function(&args[0]);

    ret = njs_function_frame(vm, function, this, &args[2], nargs, 0);
    if (njs_slow_path(ret != NJS_OK)) {
        return ret;
    }

    ret = njs_function_frame_invoke(vm, frame->retval);
    if (njs_slow_path(ret != NJS_OK)) {
        return ret;
    }

    return NJS_DECLINED;
}


static njs_int_t
njs_function_prototype_apply(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t unused)
{
    int64_t         i, length;
    njs_int_t       ret;
    njs_frame_t     *frame;
    njs_value_t     *this, *arr_like;
    njs_array_t     *arr;
    njs_function_t  *func;

    if (!njs_is_function(njs_argument(args, 0))) {
        njs_type_error(vm, "\"this\" argument is not a function");
        return NJS_ERROR;
    }

    func = njs_function(njs_argument(args, 0));
    this = njs_arg(args, nargs, 1);
    arr_like = njs_arg(args, nargs, 2);

    if (njs_is_null_or_undefined(arr_like)) {
        length = 0;
        goto activate;
    }

    if (njs_slow_path(!njs_is_object(arr_like))) {
        njs_type_error(vm, "second argument is not an array-like object");
        return NJS_ERROR;
    }

    ret = njs_object_length(vm, arr_like, &length);
    if (njs_slow_path(ret != NJS_OK)) {
        return ret;
    }

    if (njs_slow_path(length > 1024)) {
        njs_internal_error(vm, "argument list is too long");
        return NJS_ERROR;
    }

    arr = njs_array_alloc(vm, 1, length, NJS_ARRAY_SPARE);
    if (njs_slow_path(arr == NULL)) {
        return NJS_ERROR;
    }

    args = arr->start;

    for (i = 0; i < length; i++) {
        ret = njs_value_property_i64(vm, arr_like, i, &args[i]);
        if (njs_slow_path(ret == NJS_ERROR)) {
            return ret;
        }
    }

activate:

    /* Skip the "apply" method frame. */
    vm->top_frame->skip = 1;

    frame = (njs_frame_t *) vm->top_frame;

    ret = njs_function_frame(vm, func, this, args, length, 0);
    if (njs_slow_path(ret != NJS_OK)) {
        return ret;
    }

    ret = njs_function_frame_invoke(vm, frame->native.retval);
    if (njs_slow_path(ret != NJS_OK)) {
        return ret;
    }

    return NJS_DECLINED;
}


static njs_int_t
njs_function_bound_call(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t unused)
{
    u_char          *p;
    njs_int_t       ret;
    size_t          args_count;
    njs_value_t     *arguments;
    njs_function_t  *function, *bound;

    function = vm->top_frame->function;
    bound = function->context;

    njs_assert(bound != NULL);

    args_count = 1 /* this */ + function->bound_args;

    if (nargs == 1) {
        return njs_function_apply(vm, bound, function->bound, args_count,
                                 &vm->retval);
    }

    arguments = njs_mp_alloc(vm->mem_pool,
                             (args_count + nargs - 1) * sizeof(njs_value_t));
    if (njs_slow_path(arguments == NULL)) {
        njs_memory_error(vm);
        return NJS_ERROR;
    }

    p = njs_cpymem(arguments, function->bound,
                   args_count * sizeof(njs_value_t));
    memcpy(p, &args[1], (nargs - 1) * sizeof(njs_value_t));

    ret = njs_function_apply(vm, bound, arguments, args_count + nargs - 1,
                             &vm->retval);

    njs_mp_free(vm->mem_pool, arguments);

    return ret;
}


static njs_int_t
njs_function_prototype_bind(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t unused)
{
    size_t          size;
    njs_int_t       ret;
    njs_uint_t      bound_args;
    njs_value_t     *values, name;
    njs_function_t  *function;

    if (!njs_is_function(&args[0])) {
        njs_type_error(vm, "\"this\" argument is not a function");
        return NJS_ERROR;
    }

    function = njs_mp_alloc(vm->mem_pool, sizeof(njs_function_t));
    if (njs_slow_path(function == NULL)) {
        njs_memory_error(vm);
        return NJS_ERROR;
    }

    *function = *njs_function(&args[0]);
    function->native = 1;
    function->u.native = njs_function_bound_call;

    njs_lvlhsh_init(&function->object.hash);

    /* Bound functions have no "prototype" property. */
    function->object.shared_hash = vm->shared->arrow_instance_hash;

    function->object.__proto__ = &vm->prototypes[NJS_OBJ_TYPE_FUNCTION].object;
    function->object.shared = 0;

    function->context = njs_function(&args[0]);

    ret = njs_value_property(vm, &args[0], njs_value_arg(&njs_string_name),
                             &name);
    if (njs_slow_path(ret == NJS_ERROR)) {
        return ret;
    }

    if (!njs_is_string(&name)) {
        name = njs_string_empty;
    }

    ret = njs_function_name_set(vm, function, &name, "bound");
    if (njs_slow_path(ret == NJS_ERROR)) {
        return ret;
    }

    if (nargs == 1) {
        args = njs_value_arg(&njs_value_undefined);
        bound_args = 0;

    } else {
        args++;
        bound_args = nargs - 2;
    }

    if (bound_args > function->args_count) {
        function->args_count = 0;

    } else {
        function->args_count -= bound_args;
    }

    function->bound_args = bound_args;

    size = (1 /* this */ + bound_args) * sizeof(njs_value_t);

    values = njs_mp_alloc(vm->mem_pool, size);
    if (njs_slow_path(values == NULL)) {
        njs_memory_error(vm);
        njs_mp_free(vm->mem_pool, function);
        return NJS_ERROR;
    }

    function->bound = values;

    /* GC: ? retain args. */

    memcpy(values, args, size);

    njs_set_function(&vm->retval, function);

    return NJS_OK;
}


static const njs_object_prop_t  njs_function_prototype_properties[] =
{
    NJS_DECLARE_PROP_NAME(""),

    NJS_DECLARE_PROP_LENGTH(0),

    NJS_DECLARE_PROP_HANDLER("constructor",
                             njs_object_prototype_create_constructor,
                             0, 0, NJS_OBJECT_PROP_VALUE_CW),

    NJS_DECLARE_PROP_NATIVE("call", njs_function_prototype_call, 1, 0),

    NJS_DECLARE_PROP_NATIVE("apply", njs_function_prototype_apply, 2, 0),

    NJS_DECLARE_PROP_NATIVE("bind", njs_function_prototype_bind, 1, 0),

    {
        .type = NJS_ACCESSOR,
        .name = njs_string("caller"),
        .u.accessor = njs_accessor(njs_function_prototype_thrower, 0,
                                   njs_function_prototype_thrower, 0),
        .writable = NJS_ATTRIBUTE_UNSET,
        .configurable = 1,
    },

    {
        .type = NJS_ACCESSOR,
        .name = njs_string("arguments"),
        .u.accessor = njs_accessor(njs_function_prototype_thrower, 0,
                                   njs_function_prototype_thrower, 0),
        .writable = NJS_ATTRIBUTE_UNSET,
        .configurable = 1,
    },
};


const njs_object_init_t  njs_function_prototype_init = {
    njs_function_prototype_properties,
    njs_nitems(njs_function_prototype_properties),
};


const njs_object_prop_t  njs_function_instance_properties[] =
{
    NJS_DECLARE_PROP_HANDLER("length", njs_function_instance_length, 0, 0,
                             NJS_OBJECT_PROP_VALUE_C),

    NJS_DECLARE_PROP_HANDLER("name", njs_function_instance_name, 0, 0,
                             NJS_OBJECT_PROP_VALUE_C),

    NJS_DECLARE_PROP_HANDLER("prototype", njs_function_prototype_create, 0, 0,
                             NJS_OBJECT_PROP_VALUE_W),
};


const njs_object_init_t  njs_function_instance_init = {
    njs_function_instance_properties,
    njs_nitems(njs_function_instance_properties),
};


const njs_object_prop_t  njs_arrow_instance_properties[] =
{
    NJS_DECLARE_PROP_HANDLER("length", njs_function_instance_length, 0, 0,
                             NJS_OBJECT_PROP_VALUE_C),

    NJS_DECLARE_PROP_HANDLER("name", njs_function_instance_name, 0, 0,
                             NJS_OBJECT_PROP_VALUE_C),
};


const njs_object_init_t  njs_arrow_instance_init = {
    njs_arrow_instance_properties,
    njs_nitems(njs_arrow_instance_properties),
};


njs_int_t
njs_eval_function(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t unused)
{
    njs_internal_error(vm, "Not implemented");

    return NJS_ERROR;
}


static njs_int_t
njs_prototype_function(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t unused)
{
    njs_set_undefined(&vm->retval);

    return NJS_OK;
}


const njs_object_type_init_t  njs_function_type_init = {
   .constructor = njs_native_ctor(njs_function_constructor, 1, 0),
   .constructor_props = &njs_function_constructor_init,
   .prototype_props = &njs_function_prototype_init,
   .prototype_value = { .function = { .native = 1,
                                      .u.native = njs_prototype_function,
                                      .object = { .type = NJS_FUNCTION } } },
};
