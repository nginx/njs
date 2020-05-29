
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) NGINX, Inc.
 */


#include <njs_main.h>


static njs_function_t *njs_function_copy(njs_vm_t *vm,
    njs_function_t *function);
static njs_native_frame_t *njs_function_frame_alloc(njs_vm_t *vm, size_t size);


njs_function_t *
njs_function_alloc(njs_vm_t *vm, njs_function_lambda_t *lambda,
    njs_closure_t *closures[], njs_bool_t shared)
{
    size_t          size;
    njs_uint_t      n, nesting;
    njs_function_t  *function;

    nesting = lambda->nesting;
    size = sizeof(njs_function_t) + nesting * sizeof(njs_closure_t *);

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

    } else {
        function->object.shared_hash = vm->shared->arrow_instance_hash;
    }

    function->object.__proto__ = &vm->prototypes[NJS_OBJ_TYPE_FUNCTION].object;
    function->object.type = NJS_FUNCTION;
    function->object.shared = shared;
    function->object.extensible = 1;

    if (nesting != 0 && closures != NULL) {
        function->closure = 1;

        n = 0;

        do {
            /* GC: retain closure. */
            njs_function_closures(function)[n] = closures[n];
            n++;
        } while (n < nesting);
    }

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

    if (copy->ctor) {
        copy->object.shared_hash = vm->shared->function_instance_hash;

    } else {
        copy->object.shared_hash = vm->shared->arrow_instance_hash;
    }

    value->data.u.function = copy;

    return copy;
}


njs_inline njs_closure_t **
njs_function_active_closures(njs_vm_t *vm, njs_function_t *function)
{
    return (function->closure) ? njs_function_closures(function)
                               : njs_frame_closures(vm->active_frame);
}


njs_int_t
njs_function_name_set(njs_vm_t *vm, njs_function_t *function,
    njs_value_t *name, njs_bool_t bound)
{
    u_char              *start;
    njs_int_t           ret;
    njs_string_prop_t   string;
    njs_object_prop_t   *prop;
    njs_lvlhsh_query_t  lhq;

    prop = njs_object_prop_alloc(vm, &njs_string_name, name, 0);
    if (njs_slow_path(name == NULL)) {
        return NJS_ERROR;
    }

    if (bound) {
        (void) njs_string_prop(&string, name);

        start = njs_string_alloc(vm, &prop->value, string.size + 6,
                                 string.length + 6);
        if (njs_slow_path(start == NULL)) {
            return NJS_ERROR;
        }

        start = njs_cpymem(start, "bound ", 6);
        memcpy(start, string.start, string.size);
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


static njs_function_t *
njs_function_copy(njs_vm_t *vm, njs_function_t *function)
{
    size_t          size;
    njs_uint_t      n, nesting;
    njs_closure_t   **closures;
    njs_function_t  *copy;

    nesting = (function->native) ? 0 : function->u.lambda->nesting;

    size = sizeof(njs_function_t) + nesting * sizeof(njs_closure_t *);

    copy = njs_mp_alloc(vm->mem_pool, size);
    if (njs_slow_path(copy == NULL)) {
        return NULL;
    }

    *copy = *function;
    copy->object.__proto__ = &vm->prototypes[NJS_OBJ_TYPE_FUNCTION].object;
    copy->object.shared = 0;

    if (nesting == 0) {
        return copy;
    }

    copy->closure = 1;

    closures = njs_function_active_closures(vm, function);

    n = 0;

    do {
        /* GC: retain closure. */
        njs_function_closures(copy)[n] = closures[n];
        n++;
    } while (n < nesting);

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

        prop = njs_object_prop_alloc(vm, &value, &frame->arguments[n + 1], 1);
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

    if (n <= nargs) {
        i = 0;
        do {
            /* GC: retain. */
            array->start[i++] = frame->arguments[n++];
        } while (n <= nargs);
    }

    rest_arguments = &frame->arguments[frame->function->u.lambda->nargs];

    /* GC: retain. */
    njs_set_array(rest_arguments, array);

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

    value = (njs_value_t *) ((u_char *) frame + NJS_NATIVE_FRAME_SIZE);
    frame->arguments = value;

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

    vm->scopes[NJS_SCOPE_CALLEE_ARGUMENTS] = value;

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
    size_t                 size;
    njs_uint_t             n, max_args, closures;
    njs_value_t            *value, *bound;
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

    max_args = njs_max(nargs, lambda->nargs);

    closures = lambda->nesting + lambda->block_closures;

    size = njs_frame_size(closures)
           + (function->args_offset + max_args) * sizeof(njs_value_t)
           + lambda->local_size;

    native_frame = njs_function_frame_alloc(vm, size);
    if (njs_slow_path(native_frame == NULL)) {
        return NJS_ERROR;
    }

    native_frame->function = target;
    native_frame->nargs = nargs;
    native_frame->ctor = ctor;

    /* Function arguments. */

    value = (njs_value_t *) ((u_char *) native_frame +
                             njs_frame_size(closures));
    native_frame->arguments = value;

    if (bound == NULL) {
        *value = *this;

        if (njs_slow_path(function->global_this
                          && njs_is_null_or_undefined(this))) {
            njs_set_object(value, &vm->global_object);
        }

        value++;

    } else {
        n = function->args_offset;
        native_frame->nargs += n - 1;

        if (ctor) {
            *value++ = *this;
            bound++;
            n--;
        }

        while (n != 0) {
            *value++ = *bound++;
            n--;
        };
    }

    vm->scopes[NJS_SCOPE_CALLEE_ARGUMENTS] = value;

    if (args != NULL) {
        while (nargs != 0) {
            *value++ = *args++;
            max_args--;
            nargs--;
        }
    }

    while (max_args != 0) {
        njs_set_undefined(value++);
        max_args--;
    }

    frame = (njs_frame_t *) native_frame;
    frame->local = value;
    frame->previous_active_frame = vm->active_frame;

    return NJS_OK;
}


njs_native_frame_t *
njs_function_frame_alloc(njs_vm_t *vm, size_t size)
{
    size_t              spare_size, chunk_size;
    njs_native_frame_t  *frame;

    /*
     * The size value must be aligned to njs_value_t because vm->top_frame
     * may point to frame->free and vm->top_frame is used as a base pointer
     * in njs_vm_continuation() which is expected to return pointers aligned
     * to njs_value_t.
     */
    size = njs_align_size(size, sizeof(njs_value_t));

    spare_size = vm->top_frame->free_size;

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

    ret = njs_function_frame_invoke(vm, (njs_index_t) &dst);

    if (ret == NJS_OK) {
        *retval = dst;
    }

    return ret;
}


njs_int_t
njs_function_lambda_call(njs_vm_t *vm)
{
    size_t                 size;
    njs_int_t              ret;
    njs_uint_t             n, nesting;
    njs_frame_t            *frame;
    njs_value_t            *dst, *src;
    njs_closure_t          *closure, **closures;
    njs_function_t         *function;
    njs_function_lambda_t  *lambda;

    frame = (njs_frame_t *) vm->top_frame;
    function = frame->native.function;

    lambda = function->u.lambda;

#if (NJS_DEBUG)
    vm->scopes[NJS_SCOPE_CALLEE_ARGUMENTS] = NULL;
#endif

    vm->scopes[NJS_SCOPE_ARGUMENTS] = frame->native.arguments;

    /* Function local variables and temporary values. */

    vm->scopes[NJS_SCOPE_LOCAL] = frame->local;

    memcpy(frame->local, lambda->local_scope, lambda->local_size);

    /* Parent closures values. */

    n = 0;
    nesting = lambda->nesting;

    if (nesting != 0) {
        closures = njs_function_active_closures(vm, function);
        do {
            closure = *closures++;

            njs_frame_closures(frame)[n] = closure;
            vm->scopes[NJS_SCOPE_CLOSURE + n] = &closure->u.values;

            n++;
        } while (n < nesting);
    }

    /* Function closure values. */

    if (lambda->block_closures > 0) {
        closure = NULL;

        size = lambda->closure_size;

        if (size != 0) {
            closure = njs_mp_align(vm->mem_pool, sizeof(njs_value_t), size);
            if (njs_slow_path(closure == NULL)) {
                njs_memory_error(vm);
                return NJS_ERROR;
            }

            size -= sizeof(njs_value_t);
            closure->u.count = 0;
            dst = closure->values;

            src = lambda->closure_scope;

            do {
                *dst++ = *src++;
                size -= sizeof(njs_value_t);
            } while (size != 0);
        }

        njs_frame_closures(frame)[n] = closure;
        vm->scopes[NJS_SCOPE_CLOSURE + n] = &closure->u.values;
    }

    if (lambda->rest_parameters) {
        ret = njs_function_rest_parameters_init(vm, &frame->native);
        if (njs_slow_path(ret != NJS_OK)) {
            return NJS_ERROR;
        }
    }

    vm->active_frame = frame;

    return njs_vmcode_interpreter(vm, lambda->start);
}


njs_int_t
njs_function_native_call(njs_vm_t *vm)
{
    njs_int_t              ret;
    njs_value_t            *value;
    njs_frame_t            *frame;
    njs_function_t         *function, *target;
    njs_native_frame_t     *native, *previous;
    njs_function_native_t  call;

    native = vm->top_frame;
    frame = (njs_frame_t *) native;
    function = native->function;

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
    if (njs_slow_path(ret == NJS_ERROR)) {
        return ret;
    }

    if (ret == NJS_DECLINED) {
        return NJS_OK;
    }

    previous = njs_function_previous_frame(native);

    njs_vm_scopes_restore(vm, frame, previous);

    if (!native->skip) {
        value = njs_vmcode_operand(vm, frame->retval);
        /*
         * GC: value external/internal++ depending
         * on vm->retval and retval type
         */
        *value = vm->retval;
    }

    njs_function_frame_free(vm, native);

    return NJS_OK;
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


static njs_value_t *
njs_function_property_prototype_create(njs_vm_t *vm, njs_lvlhsh_t *hash,
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

    proto = njs_function_property_prototype_create(vm, &function->object.hash,
                                                   setval);
    if (njs_slow_path(proto == NULL)) {
        return NJS_ERROR;
    }

    if (njs_is_object(proto)) {
        cons = njs_property_constructor_create(vm, njs_object_hash(proto),
                                               value);
        if (njs_slow_path(cons == NULL)) {
            return NJS_ERROR;
        }
    }

    *retval = *proto;

    return NJS_OK;
}


static njs_int_t
njs_function_constructor(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t unused)
{
    njs_chb_t              chain;
    njs_int_t              ret;
    njs_str_t              str, file;
    njs_uint_t             i;
    njs_value_t            *body;
    njs_lexer_t            lexer;
    njs_parser_t           *parser;
    njs_function_t         *function;
    njs_generator_t        generator;
    njs_parser_scope_t     *scope;
    njs_function_lambda_t  *lambda;
    njs_vmcode_function_t  *code;

    if (!vm->options.unsafe) {
        body = njs_argument(args, nargs - 1);
        ret = njs_value_to_string(vm, body, body);
        if (njs_slow_path(ret != NJS_OK)) {
            return ret;
        }

        njs_string_get(body, &str);

        /*
         * Safe mode exception:
         * "(new Function('return this'))" is often used to get
         * the global object in a portable way.
         */

        if (str.length != njs_length("return this")
            || memcmp(str.start, "return this", 11) != 0)
        {
            njs_type_error(vm, "function constructor is disabled"
                           " in \"safe\" mode");
            return NJS_ERROR;
        }
    }

    njs_chb_init(&chain, vm->mem_pool);

    njs_chb_append_literal(&chain, "(function(");

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

    vm->options.accumulative = 1;

    parser = njs_mp_zalloc(vm->mem_pool, sizeof(njs_parser_t));
    if (njs_slow_path(parser == NULL)) {
        return NJS_ERROR;
    }

    vm->parser = parser;

    file = njs_str_value("runtime");

    ret = njs_lexer_init(vm, &lexer, &file, str.start, str.start + str.length);
    if (njs_slow_path(ret != NJS_OK)) {
        return ret;
    }

    parser->vm = vm;
    parser->lexer = &lexer;

    ret = njs_parser(parser, NULL);
    if (njs_slow_path(ret != NJS_OK)) {
        return ret;
    }

    scope = parser->scope;

    ret = njs_variables_copy(vm, &scope->variables, vm->variables_hash);
    if (njs_slow_path(ret != NJS_OK)) {
        return ret;
    }

    ret = njs_variables_scope_reference(vm, scope);
    if (njs_slow_path(ret != NJS_OK)) {
        return ret;
    }

    njs_memzero(&generator, sizeof(njs_generator_t));

    ret = njs_generate_scope(vm, &generator, scope, &njs_entry_anonymous);
    if (njs_slow_path(ret != NJS_OK)) {
        return ret;
    }

    njs_chb_destroy(&chain);

    code = (njs_vmcode_function_t *) generator.code_start;
    lambda = code->lambda;

    function = njs_function_alloc(vm, lambda, NULL, 0);
    if (njs_slow_path(function == NULL)) {
        return NJS_ERROR;
    }

    function->global_this = 1;
    function->args_count = lambda->nargs - lambda->rest_parameters;

    njs_set_function(&vm->retval, function);

    return NJS_OK;
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


static njs_int_t
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
    njs_int_t          ret;
    njs_frame_t        *frame;
    njs_function_t     *function;
    const njs_value_t  *this;

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

    frame = (njs_frame_t *) vm->top_frame;

    function = njs_function(&args[0]);

    /* Skip the "call" method frame. */
    vm->top_frame->skip = 1;

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

    if (!njs_is_function(njs_arg(args, nargs, 0))) {
        njs_type_error(vm, "\"this\" argument is not a function");
        return NJS_ERROR;
    }

    func = njs_function(njs_argument(args, 0));
    this = njs_arg(args, nargs, 1);
    arr_like = njs_arg(args, nargs, 2);

    if (njs_is_null_or_undefined(arr_like)) {
        length = 0;

        goto activate;

    } else if (njs_is_array(arr_like)) {
        arr = arr_like->data.u.array;

        args = arr->start;
        length = arr->length;

        goto activate;

    } else if (njs_slow_path(!njs_is_object(arr_like))) {
        njs_type_error(vm, "second argument is not an array-like object");
        return NJS_ERROR;
    }

    ret = njs_object_length(vm, arr_like, &length);
    if (njs_slow_path(ret != NJS_OK)) {
        return ret;
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

    ret = njs_function_frame_invoke(vm, frame->retval);
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

    ret = njs_function_name_set(vm, function, &name, 1);
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
