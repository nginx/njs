
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) NGINX, Inc.
 */


#include <njs_main.h>


static njs_function_t *njs_function_copy(njs_vm_t *vm,
    njs_function_t *function);
static njs_native_frame_t *njs_function_frame_alloc(njs_vm_t *vm, size_t size);
static njs_int_t njs_normalize_args(njs_vm_t *vm, njs_value_t *args,
    uint8_t *args_types, njs_uint_t nargs);


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

    function->object.__proto__ = &vm->prototypes[NJS_PROTOTYPE_FUNCTION].object;
    function->object.type = NJS_FUNCTION;
    function->object.shared = shared;
    function->object.extensible = 1;

    if (nesting != 0 && closures != NULL) {
        function->closure = 1;

        n = 0;

        do {
            /* GC: retain closure. */
            function->closures[n] = closures[n];
            n++;
        } while (n < nesting);
    }

    return function;

fail:

    njs_memory_error(vm);

    return NULL;
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


njs_inline njs_closure_t **
njs_function_closures(njs_vm_t *vm, njs_function_t *function)
{
    return (function->closure) ? function->closures
                               : vm->active_frame->closures;
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
    copy->object.__proto__ = &vm->prototypes[NJS_PROTOTYPE_FUNCTION].object;
    copy->object.shared = 0;

    if (nesting == 0) {
        return copy;
    }

    copy->closure = 1;

    closures = njs_function_closures(vm, function);

    n = 0;

    do {
        /* GC: retain closure. */
        copy->closures[n] = closures[n];
        n++;
    } while (n < nesting);

    return copy;
}


/*
 * ES5.1, 10.6: CreateArgumentsObject.
 */
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

    array = njs_array_alloc(vm, length, 0);
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
njs_function_arguments_thrower(njs_vm_t *vm, njs_value_t *value,
    njs_value_t *setval, njs_value_t *retval)
{
    njs_type_error(vm, "\"caller\", \"callee\" properties may not be accessed");
    return NJS_ERROR;
}


const njs_object_prop_t  njs_arguments_object_instance_properties[] =
{
    {
        .type = NJS_PROPERTY_HANDLER,
        .name = njs_string("caller"),
        .value = njs_prop_handler(njs_function_arguments_thrower),
    },

    {
        .type = NJS_PROPERTY_HANDLER,
        .name = njs_string("callee"),
        .value = njs_prop_handler(njs_function_arguments_thrower),
    },
};


const njs_object_init_t  njs_arguments_object_instance_init = {
    njs_str("Argument object instance"),
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
    njs_native_frame_t     *native_frame;
    njs_function_lambda_t  *lambda;

    lambda = function->u.lambda;

    max_args = njs_max(nargs, lambda->nargs);

    closures = lambda->nesting + lambda->block_closures;

    size = njs_frame_size(closures)
           + (function->args_offset + max_args) * sizeof(njs_value_t)
           + lambda->local_size;

    native_frame = njs_function_frame_alloc(vm, size);
    if (njs_slow_path(native_frame == NULL)) {
        return NJS_ERROR;
    }

    native_frame->function = function;
    native_frame->nargs = nargs;
    native_frame->ctor = ctor;

    /* Function arguments. */

    value = (njs_value_t *) ((u_char *) native_frame +
                             njs_frame_size(closures));
    native_frame->arguments = value;

    bound = function->bound;

    if (bound == NULL) {
        *value++ = *this;

    } else {
        n = function->args_offset;
        native_frame->nargs += n - 1;

        do {
            *value++ = *bound++;
            n--;
        } while (n != 0);
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
njs_function_call(njs_vm_t *vm, njs_function_t *function,
    const njs_value_t *this, const njs_value_t *args,
    njs_uint_t nargs, njs_value_t *retval)
{
    njs_int_t    ret;
    njs_value_t  dst njs_aligned(16);

    ret = njs_function_frame(vm, function, this, args, nargs, 0);
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
        closures = njs_function_closures(vm, function);
        do {
            closure = *closures++;

            frame->closures[n] = closure;
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

        frame->closures[n] = closure;
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
    njs_int_t           ret;
    njs_value_t         *value;
    njs_frame_t         *frame;
    njs_function_t      *function;
    njs_native_frame_t  *native, *previous;

    native = vm->top_frame;
    frame = (njs_frame_t *) native;
    function = native->function;

    ret = njs_normalize_args(vm, native->arguments, function->args_types,
                             native->nargs);
    if (ret != NJS_OK) {
        return ret;
    }

    ret = function->u.native(vm, native->arguments, native->nargs,
                             frame->retval);

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


static njs_int_t
njs_normalize_args(njs_vm_t *vm, njs_value_t *args, uint8_t *args_types,
    njs_uint_t nargs)
{
    njs_int_t   ret;
    njs_uint_t  n;

    n = njs_min(nargs, NJS_ARGS_TYPES_MAX);

    while (n != 0) {

        switch (*args_types) {

        case NJS_STRING_OBJECT_ARG:

            if (njs_is_null_or_undefined(args)) {
                goto type_error;
            }

            /* Fall through. */

        case NJS_STRING_ARG:

            if (!njs_is_string(args)) {
                ret = njs_value_to_string(vm, args, args);
                if (ret != NJS_OK) {
                    return ret;
                }
            }

            break;

        case NJS_NUMBER_ARG:

            if (!njs_is_numeric(args)) {
                ret = njs_value_to_numeric(vm, args, args);
                if (ret != NJS_OK) {
                    return ret;
                }
            }

            break;

        case NJS_INTEGER_ARG:

            if (!njs_is_numeric(args)) {
                ret = njs_value_to_numeric(vm, args, args);
                if (ret != NJS_OK) {
                    return ret;
                }
            }

            /* Numbers are truncated to fit in 32-bit integers. */

            if (!isnan(njs_number(args))) {
                if (njs_number(args) > 2147483647.0) {
                    njs_number(args) = 2147483647.0;

                } else if (njs_number(args) < -2147483648.0) {
                    njs_number(args) = -2147483648.0;
                }
            }

            break;

        case NJS_FUNCTION_ARG:

            switch (args->type) {
            case NJS_STRING:
            case NJS_FUNCTION:
                break;

            default:
                ret = njs_value_to_string(vm, args, args);
                if (ret != NJS_OK) {
                    return ret;
                }
            }

            break;

        case NJS_REGEXP_ARG:

            switch (args->type) {
            case NJS_UNDEFINED:
            case NJS_STRING:
            case NJS_REGEXP:
                break;

            default:
                ret = njs_value_to_string(vm, args, args);
                if (ret != NJS_OK) {
                    return ret;
                }
            }

            break;

        case NJS_DATE_ARG:
            if (!njs_is_date(args)) {
                goto type_error;
            }

            break;

        case NJS_OBJECT_ARG:

            if (njs_is_null_or_undefined(args)) {
                goto type_error;
            }

            break;

        case NJS_SKIP_ARG:
            break;

        case 0:
            return NJS_OK;
        }

        args++;
        args_types++;
        n--;
    }

    return NJS_OK;

type_error:

    njs_type_error(vm, "cannot convert %s to %s", njs_type_string(args->type),
                   njs_arg_type_string(*args_types));

    return NJS_ERROR;
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
njs_function_prototype_create(njs_vm_t *vm, njs_value_t *value,
    njs_value_t *setval, njs_value_t *retval)
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


njs_int_t
njs_function_constructor(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t unused)
{
    njs_internal_error(vm, "Not implemented");

    return NJS_ERROR;
}


static const njs_object_prop_t  njs_function_constructor_properties[] =
{
    /* Function.name == "Function". */
    {
        .type = NJS_PROPERTY,
        .name = njs_string("name"),
        .value = njs_string("Function"),
        .configurable = 1,
    },

    /* Function.length == 1. */
    {
        .type = NJS_PROPERTY,
        .name = njs_string("length"),
        .value = njs_value(NJS_NUMBER, 1, 1.0),
        .configurable = 1,
    },

    /* Function.prototype. */
    {
        .type = NJS_PROPERTY_HANDLER,
        .name = njs_string("prototype"),
        .value = njs_prop_handler(njs_object_prototype_create),
    },
};


const njs_object_init_t  njs_function_constructor_init = {
    njs_str("Function"),
    njs_function_constructor_properties,
    njs_nitems(njs_function_constructor_properties),
};


/*
 * ES5.1, 15.3.5.1 length
 *      the typical number of arguments expected by the function.
 */
static njs_int_t
njs_function_instance_length(njs_vm_t *vm, njs_value_t *value,
    njs_value_t *setval, njs_value_t *retval)
{
    njs_uint_t             n;
    njs_object_t           *proto;
    njs_function_t         *function;
    njs_function_lambda_t  *lambda;

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

    if (function->native) {
        for (n = function->args_offset; n < NJS_ARGS_TYPES_MAX; n++) {
            if (function->args_types[n] == 0) {
                break;
            }
        }

    } else {
        lambda = function->u.lambda;
        n = lambda->nargs + 1 - lambda->rest_parameters;
    }

    if (n >= function->args_offset) {
        n -= function->args_offset;

    } else {
        n = 0;
    }

    njs_set_number(retval, n);

    return NJS_OK;
}


static njs_int_t
njs_function_prototype_call(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t retval)
{
    njs_int_t          ret;
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

    function = njs_function(&args[0]);

    /* Skip the "call" method frame. */
    vm->top_frame->skip = 1;

    ret = njs_function_frame(vm, function, this, &args[2], nargs, 0);
    if (njs_slow_path(ret != NJS_OK)) {
        return ret;
    }

    ret = njs_function_frame_invoke(vm, retval);
    if (njs_slow_path(ret != NJS_OK)) {
        return ret;
    }

    return NJS_DECLINED;
}


static njs_int_t
njs_function_prototype_apply(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t retval)
{
    uint32_t        i;
    njs_int_t       ret;
    njs_value_t     name, *this, *arr_like;
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
        nargs = 0;

        goto activate;

    } else if (njs_is_array(arr_like)) {
        arr = arr_like->data.u.array;

        args = arr->start;
        nargs = arr->length;

        goto activate;

    } else if (njs_slow_path(!njs_is_object(arr_like))) {
        njs_type_error(vm, "second argument is not an array-like object");
        return NJS_ERROR;
    }

    ret = njs_object_length(vm, arr_like, &nargs);
    if (njs_slow_path(ret != NJS_OK)) {
        return ret;
    }

    arr = njs_array_alloc(vm, nargs, NJS_ARRAY_SPARE);
    if (njs_slow_path(arr == NULL)) {
        return NJS_ERROR;
    }

    args = arr->start;

    for (i = 0; i < nargs; i++) {
        njs_uint32_to_string(&name, i);

        ret = njs_value_property(vm, arr_like, &name, &args[i]);
        if (njs_slow_path(ret == NJS_ERROR)) {
            return ret;
        }
    }

activate:

    /* Skip the "apply" method frame. */
    vm->top_frame->skip = 1;

    ret = njs_function_frame(vm, func, this, args, nargs, 0);
    if (njs_slow_path(ret != NJS_OK)) {
        return ret;
    }

    ret = njs_function_frame_invoke(vm, retval);
    if (njs_slow_path(ret != NJS_OK)) {
        return ret;
    }

    return NJS_DECLINED;
}


static njs_int_t
njs_function_prototype_bind(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t unused)
{
    size_t          size;
    njs_value_t     *values;
    njs_function_t  *function;

    if (!njs_is_function(&args[0])) {
        njs_type_error(vm, "\"this\" argument is not a function");
        return NJS_ERROR;
    }

    function = njs_function_copy(vm, njs_function(&args[0]));
    if (njs_slow_path(function == NULL)) {
        njs_memory_error(vm);
        return NJS_ERROR;
    }

    if (nargs == 1) {
        args = njs_value_arg(&njs_value_undefined);

    } else {
        nargs--;
        args++;
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
        .value = njs_native_function(njs_function_prototype_call, 0),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("apply"),
        .value = njs_native_function(njs_function_prototype_apply, 0),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("bind"),
        .value = njs_native_function(njs_function_prototype_bind, 0),
        .writable = 1,
        .configurable = 1,
    },
};


const njs_object_init_t  njs_function_prototype_init = {
    njs_str("Function"),
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
    njs_str("Function instance"),
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
    njs_str("Arrow instance"),
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


static const njs_object_prop_t  njs_eval_function_properties[] =
{
    /* eval.name == "eval". */
    {
        .type = NJS_PROPERTY,
        .name = njs_string("name"),
        .value = njs_string("eval"),
        .configurable = 1,
    },

    /* eval.length == 1. */
    {
        .type = NJS_PROPERTY,
        .name = njs_string("length"),
        .value = njs_value(NJS_NUMBER, 1, 1.0),
        .configurable = 1,
    },
};


const njs_object_init_t  njs_eval_function_init = {
    njs_str("eval"),
    njs_eval_function_properties,
    njs_nitems(njs_eval_function_properties),
};
