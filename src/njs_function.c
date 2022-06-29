
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
    function->args_offset = 1;
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
    function->args_offset = 1;
    function->u.native = native;

    return function;
}


njs_function_t *
njs_function_value_copy(njs_vm_t *vm, njs_value_t *value)
{
    njs_function_t     *function, *copy;
    njs_object_type_t  type;

    function = njs_function(value);

    if (!function->object.shared) {
        return function;
    }

    copy = njs_function_copy(vm, function);
    if (njs_slow_path(copy == NULL)) {
        njs_memory_error(vm);
        return NULL;
    }

    type = njs_function_object_type(vm, function);

    if (copy->ctor) {
        copy->object.shared_hash = vm->shared->function_instance_hash;

    } else if (type == NJS_OBJ_TYPE_ASYNC_FUNCTION) {
        copy->object.shared_hash = vm->shared->async_function_instance_hash;

    } else {
        copy->object.shared_hash = vm->shared->arrow_instance_hash;
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

    if (njs_is_symbol(&prop->value)) {
        symbol = 2;
        prop->value = *njs_symbol_description(&prop->value);
    }

    if (prefix != NULL || symbol != 0) {
        value = prop->value;
        (void) njs_string_prop(&string, &value);

        len = (prefix != NULL) ? njs_strlen(prefix) + 1: 0;
        p = njs_string_alloc(vm, &prop->value, string.size + len + symbol,
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
    njs_int_t           ret;
    njs_uint_t          nargs, n;
    njs_value_t         value;
    njs_object_t        *arguments;
    njs_object_prop_t   *prop;
    njs_lvlhsh_query_t  lhq;

    static const njs_value_t  njs_string_length = njs_string("length");

    arguments = njs_object_alloc(vm);
    if (njs_slow_path(arguments == NULL)) {
        return NJS_ERROR;
    }

    arguments->shared_hash = vm->shared->arguments_object_instance_hash;

    nargs = frame->nargs;

    njs_set_number(&value, nargs);

    prop = njs_object_prop_alloc(vm, &njs_string_length, &value, 1);
    if (njs_slow_path(prop == NULL)) {
        return NJS_ERROR;
    }

    prop->enumerable = 0;

    lhq.value = prop;
    lhq.key_hash = NJS_LENGTH_HASH;
    njs_string_get(&prop->name, &lhq.key);

    lhq.replace = 0;
    lhq.pool = vm->mem_pool;
    lhq.proto = &njs_object_hash_proto;

    ret = njs_lvlhsh_insert(&arguments->hash, &lhq);
    if (njs_slow_path(ret != NJS_OK)) {
        njs_internal_error(vm, "lvlhsh insert failed");
        return NJS_ERROR;
    }

    for (n = 0; n < nargs; n++) {
        njs_uint32_to_string(&value, n);

        prop = njs_object_prop_alloc(vm, &value, &frame->arguments[n], 1);
        if (njs_slow_path(prop == NULL)) {
            return NJS_ERROR;
        }

        lhq.value = prop;
        njs_string_get(&prop->name, &lhq.key);
        lhq.key_hash = njs_djb_hash(lhq.key.start, lhq.key.length);

        ret = njs_lvlhsh_insert(&arguments->hash, &lhq);
        if (njs_slow_path(ret != NJS_OK)) {
            njs_internal_error(vm, "lvlhsh insert failed");
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
        .type = NJS_PROPERTY,
        .name = njs_string("callee"),
        .value = njs_value(NJS_INVALID, 1, NAN),
        .getter = njs_native_function(njs_function_prototype_thrower, 0),
        .setter = njs_native_function(njs_function_prototype_thrower, 0),
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
    njs_uint_t          n;
    njs_value_t         *value, *bound;
    njs_native_frame_t  *frame;

    size = NJS_NATIVE_FRAME_SIZE
           + (function->args_offset + nargs) * sizeof(njs_value_t);

    frame = njs_function_frame_alloc(vm, size);
    if (njs_slow_path(frame == NULL)) {
        return NJS_ERROR;
    }

    frame->function = function;
    frame->nargs = function->args_offset + nargs;
    frame->ctor = ctor;
    frame->native = 1;
    frame->pc = NULL;

    value = (njs_value_t *) ((u_char *) frame + NJS_NATIVE_FRAME_SIZE);

    frame->arguments = value;
    frame->arguments_offset = value + function->args_offset;

    bound = function->bound;

    if (bound == NULL) {
        /* GC: njs_retain(this); */
        *value++ = *this;

    } else {
        n = function->args_offset;

        do {
            /* GC: njs_retain(bound); */
            *value++ = *bound++;
            n--;
        } while (n != 0);
    }

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
    njs_value_t            *value, *bound, **new;
    njs_frame_t            *frame;
    njs_function_t         *target;
    njs_native_frame_t     *native_frame;
    njs_function_lambda_t  *lambda;

    bound = function->bound;

    if (njs_fast_path(bound == NULL)) {
        lambda = function->u.lambda;
        target = function;

    } else {
        target = function->u.bound_target;

        if (njs_slow_path(target->bound != NULL)) {

            /*
             * FIXME: bound functions should call target function with
             * bound "this" and bound args.
             */

            njs_internal_error(vm, "chain of bound function are not supported");
            return NJS_ERROR;
        }

        lambda = target->u.lambda;
    }

    args_count = function->args_offset + njs_max(nargs, lambda->nargs);
    value_count = args_count + njs_max(args_count, lambda->nlocal);

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
    native_frame->arguments_offset = value + (function->args_offset - 1);
    native_frame->local = new + args_count;
    native_frame->function = target;
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

    if (bound != NULL) {
        n = function->args_offset;
        native_frame->nargs += n - 1;

        if (!ctor) {
            *native_frame->local[0] = *bound;
        }

        bound++;
        n--;

        while (n != 0) {
            *value++ = *bound++;
            n--;
        };
    }

    /* Copy arguments. */

    if (args != NULL) {
        while (nargs != 0) {
            *value++ = *args++;
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

        if (vm->stack_size + spare_size > NJS_MAX_STACK_SIZE) {
            njs_range_error(vm, "Maximum call stack size exceeded");
            return NULL;
        }

        frame = njs_mp_align(vm->mem_pool, sizeof(njs_value_t), spare_size);
        if (njs_slow_path(frame == NULL)) {
            njs_memory_error(vm);
            return NULL;
        }

        chunk_size = spare_size;
        vm->stack_size += spare_size;
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
    njs_int_t    ret;
    njs_value_t  dst njs_aligned(16);

    ret = njs_function_frame(vm, function, this, args, nargs, ctor);
    if (njs_slow_path(ret != NJS_OK)) {
        return ret;
    }

    ret = njs_function_frame_invoke(vm, &dst);

    if (ret == NJS_OK) {
        *retval = dst;
    }

    return ret;
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
    local = vm->top_frame->local + function->args_offset;

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
    njs_function_t         *function, *target;
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

    if (njs_fast_path(function->bound == NULL)) {
        call = function->u.native;

    } else {
        target = function->u.bound_target;

        if (njs_slow_path(target->bound != NULL)) {
            njs_internal_error(vm, "chain of bound function are not supported");
            return NJS_ERROR;
        }

        call = target->u.native;
    }

    ret = call(vm, native->arguments, native->nargs, function->magic8);

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
            vm->stack_size -= native->size;
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

    args_count = function->args_offset + njs_max(native->nargs, lambda->nargs);
    value_count = args_count + njs_max(args_count, lambda->nlocal);

    new = (njs_value_t **) ((u_char *) native + NJS_FRAME_SIZE);
    value = (njs_value_t *) (new + value_count);

    native->arguments = value;
    native->arguments_offset = value + (function->args_offset - 1);
    native->local = new + njs_function_frame_args_count(active);
    native->pc = pc;

    start = njs_function_frame_values(active, &end);
    p = native->arguments;

    while (start < end) {
        *p = *start++;
        *new++ = p++;
    }

    /* Move all arguments. */

    p = native->arguments;
    local = native->local + function->args_offset;

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
        return &prop->value;
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

    njs_set_function(&vm->retval, function);

    return NJS_OK;

fail:

    njs_type_error(vm, "function constructor is disabled in \"safe\" mode");
    return NJS_ERROR;
}


static const njs_object_prop_t  njs_function_constructor_properties[] =
{
    {
        .type = NJS_PROPERTY,
        .name = njs_string("name"),
        .value = njs_string("Function"),
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("length"),
        .value = njs_value(NJS_NUMBER, 1, 1.0),
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY_HANDLER,
        .name = njs_string("prototype"),
        .value = njs_prop_handler(njs_object_prototype_create),
    },
};


const njs_object_init_t  njs_function_constructor_init = {
    njs_function_constructor_properties,
    njs_nitems(njs_function_constructor_properties),
};


njs_int_t
njs_function_instance_length(njs_vm_t *vm, njs_object_prop_t *prop,
    njs_value_t *value, njs_value_t *setval, njs_value_t *retval)
{
    njs_object_t    *proto;
    njs_function_t  *function;

    proto = njs_object(value);

    do {
        if (njs_fast_path(proto->type == NJS_FUNCTION)) {
            break;
        }

        proto = proto->__proto__;
    } while (proto != NULL);

    if (njs_slow_path(proto == NULL)) {
        njs_internal_error(vm, "no function in proto chain");
        return NJS_ERROR;
    }

    function = (njs_function_t *) proto;

    njs_set_number(retval, function->args_count);

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
njs_function_prototype_bind(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t unused)
{
    size_t              size;
    njs_int_t           ret;
    njs_value_t         *values, name;
    njs_function_t      *function;
    njs_lvlhsh_query_t  lhq;

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

    njs_lvlhsh_init(&function->object.hash);

    /* Bound functions have no "prototype" property. */
    function->object.shared_hash = vm->shared->arrow_instance_hash;

    function->object.__proto__ = &vm->prototypes[NJS_OBJ_TYPE_FUNCTION].object;
    function->object.shared = 0;

    function->u.bound_target = njs_function(&args[0]);

    njs_object_property_init(&lhq, &njs_string_name, NJS_NAME_HASH);

    ret = njs_object_property(vm, &args[0], &lhq, &name);
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

    } else {
        nargs--;
        args++;
    }

    if (nargs > function->args_count) {
        function->args_count = 0;

    } else {
        function->args_count -= nargs - 1;
    }

    function->args_offset = nargs;
    size = nargs * sizeof(njs_value_t);

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
    {
        .type = NJS_PROPERTY,
        .name = njs_string("name"),
        .value = njs_string(""),
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("length"),
        .value = njs_value(NJS_NUMBER, 0, 0.0),
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY_HANDLER,
        .name = njs_string("constructor"),
        .value = njs_prop_handler(njs_object_prototype_create_constructor),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("call"),
        .value = njs_native_function(njs_function_prototype_call, 1),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("apply"),
        .value = njs_native_function(njs_function_prototype_apply, 2),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("bind"),
        .value = njs_native_function(njs_function_prototype_bind, 1),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("caller"),
        .value = njs_value(NJS_INVALID, 1, NAN),
        .getter = njs_native_function(njs_function_prototype_thrower, 0),
        .setter = njs_native_function(njs_function_prototype_thrower, 0),
        .writable = NJS_ATTRIBUTE_UNSET,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("arguments"),
        .value = njs_value(NJS_INVALID, 1, NAN),
        .getter = njs_native_function(njs_function_prototype_thrower, 0),
        .setter = njs_native_function(njs_function_prototype_thrower, 0),
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
    {
        .type = NJS_PROPERTY_HANDLER,
        .name = njs_string("length"),
        .value = njs_prop_handler(njs_function_instance_length),
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY_HANDLER,
        .name = njs_string("prototype"),
        .value = njs_prop_handler(njs_function_prototype_create),
        .writable = 1
    },
};


const njs_object_init_t  njs_function_instance_init = {
    njs_function_instance_properties,
    njs_nitems(njs_function_instance_properties),
};


const njs_object_prop_t  njs_arrow_instance_properties[] =
{
    {
        .type = NJS_PROPERTY_HANDLER,
        .name = njs_string("length"),
        .value = njs_prop_handler(njs_function_instance_length),
        .configurable = 1,
    },
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
                                      .args_offset = 1,
                                      .u.native = njs_prototype_function,
                                      .object = { .type = NJS_FUNCTION } } },
};
