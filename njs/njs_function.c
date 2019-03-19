
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) NGINX, Inc.
 */

#include <njs_core.h>
#include <string.h>


static njs_native_frame_t *njs_function_frame_alloc(njs_vm_t *vm, size_t size);
static njs_ret_t njs_normalize_args(njs_vm_t *vm, njs_value_t *args,
    uint8_t *args_types, nxt_uint_t nargs);


njs_function_t *
njs_function_alloc(njs_vm_t *vm)
{
    njs_function_t  *function;

    function = nxt_mp_zalloc(vm->mem_pool, sizeof(njs_function_t));

    if (nxt_fast_path(function != NULL)) {
        /*
         * nxt_mp_zalloc() does also:
         *   nxt_lvlhsh_init(&function->object.hash);
         *   function->object.__proto__ = NULL;
         */

        function->object.shared_hash = vm->shared->function_prototype_hash;
        function->object.type = NJS_FUNCTION;
        function->object.shared = 1;
        function->object.extensible = 1;
        function->args_offset = 1;
        function->ctor = 1;

        function->u.lambda = nxt_mp_zalloc(vm->mem_pool,
                                           sizeof(njs_function_lambda_t));
        if (nxt_slow_path(function->u.lambda == NULL)) {
            njs_memory_error(vm);
            return NULL;
        }

        return function;
    }

    njs_memory_error(vm);

    return NULL;
}


njs_function_t *
njs_function_value_copy(njs_vm_t *vm, njs_value_t *value)
{
    size_t          size;
    nxt_uint_t      n, nesting;
    njs_function_t  *function, *copy;

    function = value->data.u.function;

    if (!function->object.shared) {
        return function;
    }

    nesting = (function->native) ? 0 : function->u.lambda->nesting;

    size = sizeof(njs_function_t) + nesting * sizeof(njs_closure_t *);

    copy = nxt_mp_alloc(vm->mem_pool, size);
    if (nxt_slow_path(copy == NULL)) {
        njs_memory_error(vm);
        return NULL;
    }

    value->data.u.function = copy;

    *copy = *function;
    copy->object.__proto__ = &vm->prototypes[NJS_PROTOTYPE_FUNCTION].object;
    copy->object.shared = 0;

    if (nesting == 0) {
        return copy;
    }

    copy->closure = 1;

    n = 0;

    do {
        /* GC: retain closure. */
        copy->closures[n] = vm->active_frame->closures[n];
        n++;
    } while (n < nesting);

    return copy;
}


/*
 * ES5.1, 10.6: CreateArgumentsObject.
 */
njs_ret_t
njs_function_arguments_object_init(njs_vm_t *vm, njs_native_frame_t *frame)
{
    nxt_int_t           ret;
    nxt_uint_t          nargs, n;
    njs_value_t         value;
    njs_object_t        *arguments;
    njs_object_prop_t   *prop;
    nxt_lvlhsh_query_t  lhq;

    static const njs_value_t  njs_string_length = njs_string("length");

    arguments = njs_object_alloc(vm);
    if (nxt_slow_path(arguments == NULL)) {
        return NXT_ERROR;
    }

    arguments->shared_hash = vm->shared->arguments_object_hash;

    nargs = frame->nargs;

    njs_value_number_set(&value, nargs);

    prop = njs_object_prop_alloc(vm, &njs_string_length, &value, 1);
    if (nxt_slow_path(prop == NULL)) {
        return NXT_ERROR;
    }

    prop->enumerable = 0;

    lhq.value = prop;
    lhq.key_hash = NJS_LENGTH_HASH;
    njs_string_get(&prop->name, &lhq.key);

    lhq.replace = 0;
    lhq.pool = vm->mem_pool;
    lhq.proto = &njs_object_hash_proto;

    ret = nxt_lvlhsh_insert(&arguments->hash, &lhq);
    if (nxt_slow_path(ret != NXT_OK)) {
        njs_internal_error(vm, "lvlhsh insert failed");
        return NXT_ERROR;
    }

    for (n = 0; n < nargs; n++) {
        njs_uint32_to_string(&value, n);

        prop = njs_object_prop_alloc(vm, &value, &frame->arguments[n + 1], 1);
        if (nxt_slow_path(prop == NULL)) {
            return NXT_ERROR;
        }

        lhq.value = prop;
        njs_string_get(&prop->name, &lhq.key);
        lhq.key_hash = nxt_djb_hash(lhq.key.start, lhq.key.length);

        ret = nxt_lvlhsh_insert(&arguments->hash, &lhq);
        if (nxt_slow_path(ret != NXT_OK)) {
            njs_internal_error(vm, "lvlhsh insert failed");
            return NXT_ERROR;
        }
    }

    frame->arguments_object = arguments;

    return NXT_OK;
}


njs_ret_t
njs_function_rest_parameters_init(njs_vm_t *vm, njs_native_frame_t *frame)
{
    uint32_t     length;
    nxt_uint_t   nargs, n, i;
    njs_array_t  *array;
    njs_value_t  *rest_arguments;

    nargs = frame->nargs;
    n = frame->function->u.lambda->nargs;
    length = (nargs >= n) ? (nargs - n + 1) : 0;

    array = njs_array_alloc(vm, length, 0);
    if (nxt_slow_path(array == NULL)) {
        return NXT_ERROR;
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
    rest_arguments->type = NJS_ARRAY;
    rest_arguments->data.u.array = array;
    rest_arguments->data.truth = 1;

    return NXT_OK;
}


njs_ret_t
njs_function_arguments_thrower(njs_vm_t *vm, njs_value_t *value,
    njs_value_t *setval, njs_value_t *retval)
{
    njs_type_error(vm, "\"caller\", \"callee\" properties may not be accessed");
    return NXT_ERROR;
}


njs_ret_t
njs_function_native_frame(njs_vm_t *vm, njs_function_t *function,
    const njs_value_t *this, const njs_value_t *args, nxt_uint_t nargs,
    size_t continuation_size, nxt_bool_t ctor)
{
    size_t              size;
    nxt_uint_t          n;
    njs_value_t         *value, *bound;
    njs_native_frame_t  *frame;

    continuation_size = nxt_max(continuation_size, function->continuation_size);

    size = NJS_NATIVE_FRAME_SIZE + continuation_size
           + (function->args_offset + nargs) * sizeof(njs_value_t);

    frame = njs_function_frame_alloc(vm, size);
    if (nxt_slow_path(frame == NULL)) {
        return NXT_ERROR;
    }

    frame->function = function;
    frame->nargs = function->args_offset + nargs;
    frame->ctor = ctor;

    value = (njs_value_t *) (njs_continuation(frame) + continuation_size);
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

    return NXT_OK;
}


nxt_noinline njs_ret_t
njs_function_lambda_frame(njs_vm_t *vm, njs_function_t *function,
    const njs_value_t *this, const njs_value_t *args, nxt_uint_t nargs,
    nxt_bool_t ctor)
{
    size_t                 size;
    nxt_uint_t             n, max_args, closures;
    njs_value_t            *value, *bound;
    njs_frame_t            *frame;
    njs_native_frame_t     *native_frame;
    njs_function_lambda_t  *lambda;

    lambda = function->u.lambda;

    max_args = nxt_max(nargs, lambda->nargs);

    closures = lambda->nesting + lambda->block_closures;

    size = njs_frame_size(closures)
           + (function->args_offset + max_args) * sizeof(njs_value_t)
           + lambda->local_size;

    native_frame = njs_function_frame_alloc(vm, size);
    if (nxt_slow_path(native_frame == NULL)) {
        return NXT_ERROR;
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
        *value++ = njs_value_undefined;
        max_args--;
    }

    frame = (njs_frame_t *) native_frame;
    frame->local = value;
    frame->previous_active_frame = vm->active_frame;

    return NXT_OK;
}


nxt_noinline njs_native_frame_t *
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
    size = nxt_align_size(size, sizeof(njs_value_t));

    spare_size = vm->top_frame->free_size;

    if (nxt_fast_path(size <= spare_size)) {
        frame = (njs_native_frame_t *) vm->top_frame->free;
        chunk_size = 0;

    } else {
        spare_size = size + NJS_FRAME_SPARE_SIZE;
        spare_size = nxt_align_size(spare_size, NJS_FRAME_SPARE_SIZE);

        if (vm->stack_size + spare_size > NJS_MAX_STACK_SIZE) {
            njs_range_error(vm, "Maximum call stack size exceeded");
            return NULL;
        }

        frame = nxt_mp_align(vm->mem_pool, sizeof(njs_value_t), spare_size);
        if (nxt_slow_path(frame == NULL)) {
            njs_memory_error(vm);
            return NULL;
        }

        chunk_size = spare_size;
        vm->stack_size += spare_size;
    }

    nxt_memzero(frame, sizeof(njs_native_frame_t));

    frame->size = chunk_size;
    frame->free_size = spare_size - size;
    frame->free = (u_char *) frame + size;

    frame->previous = vm->top_frame;
    vm->top_frame = frame;

    return frame;
}


nxt_noinline njs_ret_t
njs_function_lambda_call(njs_vm_t *vm, njs_index_t retval,
    u_char *return_address)
{
    size_t                 size;
    njs_ret_t              ret;
    nxt_uint_t             n, nesting;
    njs_frame_t            *frame;
    njs_value_t            *dst, *src;
    njs_closure_t          *closure, **closures;
    njs_function_t         *function;
    njs_function_lambda_t  *lambda;

    frame = (njs_frame_t *) vm->top_frame;

    frame->retval = retval;
    frame->return_address = return_address;

    function = frame->native.function;

    lambda = function->u.lambda;
    vm->current = lambda->start;

#if (NXT_DEBUG)
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
        closures = (function->closure) ? function->closures
                                       : vm->active_frame->closures;
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
            closure = nxt_mp_align(vm->mem_pool, sizeof(njs_value_t), size);
            if (nxt_slow_path(closure == NULL)) {
                njs_memory_error(vm);
                return NXT_ERROR;
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

    if (lambda->arguments_object) {
        ret = njs_function_arguments_object_init(vm, &frame->native);
        if (nxt_slow_path(ret != NXT_OK)) {
            return NXT_ERROR;
        }
    }

    if (lambda->rest_parameters) {
        ret = njs_function_rest_parameters_init(vm, &frame->native);
        if (nxt_slow_path(ret != NXT_OK)) {
            return NXT_ERROR;
        }
    }

    vm->active_frame = frame;

    return NJS_APPLIED;
}


njs_ret_t
njs_function_native_call(njs_vm_t *vm, njs_function_native_t native,
    njs_value_t *args, uint8_t *args_types, nxt_uint_t nargs,
    njs_index_t retval)
{
    njs_ret_t           ret;
    njs_value_t         *value;
    njs_function_t      *function;
    njs_native_frame_t  *frame;

    ret = njs_normalize_args(vm, args, args_types, nargs);
    if (ret != NJS_OK) {
        return ret;
    }

    ret = native(vm, args, nargs, retval);

    /*
     * A native method can return:
     *   NXT_OK on method success;
     *   NJS_APPLIED by Function.apply() and Function.call();
     *   NXT_AGAIN to postpone nJSVM processing;
     *   NXT_ERROR.
     *
     * The callee arguments must be preserved
     * for NJS_APPLIED and NXT_AGAIN cases.
     */
    if (ret == NXT_OK) {
        frame = vm->top_frame;

        vm->top_frame = njs_function_previous_frame(frame);

        /*
         * If a retval is in a callee arguments scope it
         * must be in the previous callee arguments scope.
         */
        args = vm->top_frame->arguments;
        function = vm->top_frame->function;

        if (function != NULL) {
            args += function->args_offset;
        }

        vm->scopes[NJS_SCOPE_CALLEE_ARGUMENTS] = args;

        if (!frame->skip) {
            value = njs_vmcode_operand(vm, retval);
            /*
             * GC: value external/internal++ depending
             * on vm->retval and retval type
             */
            *value = vm->retval;
        }

        njs_function_frame_free(vm, frame);

        return NXT_OK;
    }

    return ret;
}


static njs_ret_t
njs_normalize_args(njs_vm_t *vm, njs_value_t *args, uint8_t *args_types,
    nxt_uint_t nargs)
{
    nxt_uint_t  n;
    njs_trap_t  trap;

    n = nxt_min(nargs, NJS_ARGS_TYPES_MAX);

    while (n != 0) {

        switch (*args_types) {

        case NJS_STRING_OBJECT_ARG:

            if (njs_is_null_or_undefined(args)) {
                goto type_error;
            }

            /* Fall through. */

        case NJS_STRING_ARG:

            if (njs_is_string(args)) {
                break;
            }

            trap = NJS_TRAP_STRING_ARG;
            goto trap;

        case NJS_NUMBER_ARG:

            if (njs_is_numeric(args)) {
                break;
            }

            trap = NJS_TRAP_NUMBER_ARG;
            goto trap;

        case NJS_INTEGER_ARG:

            if (njs_is_numeric(args)) {

                /* Numbers are truncated to fit in 32-bit integers. */

                if (isnan(args->data.u.number)) {
                    args->data.u.number = 0;

                } else if (args->data.u.number > 2147483647.0) {
                    args->data.u.number = 2147483647.0;

                } else if (args->data.u.number < -2147483648.0) {
                    args->data.u.number = -2147483648.0;
                }

                break;
            }

            trap = NJS_TRAP_NUMBER_ARG;
            goto trap;

        case NJS_FUNCTION_ARG:

            switch (args->type) {
            case NJS_STRING:
            case NJS_FUNCTION:
                break;

            default:
                trap = NJS_TRAP_STRING_ARG;
                goto trap;
            }

            break;

        case NJS_REGEXP_ARG:

            switch (args->type) {
            case NJS_UNDEFINED:
            case NJS_STRING:
            case NJS_REGEXP:
                break;

            default:
                trap = NJS_TRAP_STRING_ARG;
                goto trap;
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

trap:

    njs_vm_trap_value(vm, args);

    return njs_trap(vm, trap);

type_error:

    njs_type_error(vm, "cannot convert %s to %s", njs_type_string(args->type),
                   njs_arg_type_string(*args_types));

    return NXT_ERROR;
}


void
njs_function_frame_free(njs_vm_t *vm, njs_native_frame_t *frame)
{
    njs_native_frame_t  *previous;

    do {
        previous = frame->previous;

        /* GC: free frame->local, etc. */

        if (frame->size != 0) {
            vm->stack_size -= frame->size;
            nxt_mp_free(vm->mem_pool, frame);
        }

        frame = previous;

    } while (frame->skip);
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

njs_ret_t
njs_function_prototype_create(njs_vm_t *vm, njs_value_t *value,
    njs_value_t *setval, njs_value_t *retval)
{
    njs_value_t  *proto;

    proto = njs_function_property_prototype_create(vm, value);

    if (nxt_fast_path(proto != NULL)) {
        *retval = *proto;
        return NXT_OK;
    }

    return NXT_ERROR;
}


njs_value_t *
njs_function_property_prototype_create(njs_vm_t *vm, njs_value_t *value)
{
    njs_value_t     *proto, *cons;
    njs_object_t    *prototype;
    njs_function_t  *function;

    prototype = njs_object_alloc(vm);

    if (nxt_slow_path(prototype == NULL)) {
        return NULL;
    }

    function = njs_function_value_copy(vm, value);

    if (nxt_slow_path(function == NULL)) {
        return NULL;
    }

    proto = njs_property_prototype_create(vm, &function->object.hash,
                                          prototype);
    if (nxt_slow_path(proto == NULL)) {
        return NULL;
    }

    cons = njs_property_constructor_create(vm, &prototype->hash, value);

    if (nxt_fast_path(cons != NULL)) {
        return proto;
    }

    return NULL;
}


njs_ret_t
njs_function_constructor(njs_vm_t *vm, njs_value_t *args, nxt_uint_t nargs,
    njs_index_t unused)
{
    njs_internal_error(vm, "Not implemented");

    return NXT_ERROR;
}


static const njs_object_prop_t  njs_function_constructor_properties[] =
{
    /* Function.name == "Function". */
    {
        .type = NJS_PROPERTY,
        .name = njs_string("name"),
        .value = njs_string("Function"),
    },

    /* Function.length == 1. */
    {
        .type = NJS_PROPERTY,
        .name = njs_string("length"),
        .value = njs_value(NJS_NUMBER, 1, 1.0),
    },

    /* Function.prototype. */
    {
        .type = NJS_PROPERTY_HANDLER,
        .name = njs_string("prototype"),
        .value = njs_prop_handler(njs_object_prototype_create),
    },
};


const njs_object_init_t  njs_function_constructor_init = {
    nxt_string("Function"),
    njs_function_constructor_properties,
    nxt_nitems(njs_function_constructor_properties),
};


/*
 * ES5.1, 15.3.5.1 length
 *      the typical number of arguments expected by the function.
 */
static njs_ret_t
njs_function_prototype_length(njs_vm_t *vm, njs_value_t *value,
    njs_value_t *setval, njs_value_t *retval)
{
    nxt_uint_t             n;
    njs_function_t         *function;
    njs_function_lambda_t  *lambda;

    function = value->data.u.function;

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

    njs_value_number_set(retval, n);

    return NXT_OK;
}


static njs_ret_t
njs_function_prototype_call(njs_vm_t *vm, njs_value_t *args, nxt_uint_t nargs,
    njs_index_t retval)
{
    njs_ret_t       ret;
    njs_value_t     *this;
    njs_function_t  *function;

    if (!njs_is_function(&args[0])) {
        njs_type_error(vm, "\"this\" argument is not a function");
        return NXT_ERROR;
    }

    if (nargs > 1) {
        this = &args[1];
        nargs -= 2;

    } else {
        this = (njs_value_t *) &njs_value_undefined;
        nargs = 0;
    }

    function = args[0].data.u.function;

    ret = njs_function_activate(vm, function, this, &args[2], nargs, retval,
                                sizeof(njs_vmcode_function_call_t));
    if (nxt_slow_path(ret == NXT_ERROR)) {
        return ret;
    }

    /* Skip the "call" method frame. */
    vm->top_frame->previous->skip = 1;

    return NJS_APPLIED;
}


static njs_ret_t
njs_function_prototype_apply(njs_vm_t *vm, njs_value_t *args, nxt_uint_t nargs,
    njs_index_t retval)
{
    njs_ret_t       ret;
    njs_array_t     *array;
    njs_value_t     *this;
    njs_function_t  *function;

    if (!njs_is_function(&args[0])) {
        njs_type_error(vm, "\"this\" argument is not a function");
        return NXT_ERROR;
    }

    function = args[0].data.u.function;
    this = &args[1];

    if (nargs > 2) {
        if (!njs_is_array(&args[2])) {
            njs_type_error(vm, "second argument is not an array");
            return NXT_ERROR;
        }

        array = args[2].data.u.array;
        args = array->start;
        nargs = array->length;

    } else {
        if (nargs == 1) {
            this = (njs_value_t *) &njs_value_undefined;
        }

        nargs = 0;
    }

    ret = njs_function_activate(vm, function, this, args, nargs, retval,
                                sizeof(njs_vmcode_function_call_t));

    if (nxt_slow_path(ret == NXT_ERROR)) {
        return ret;
    }

    /* Skip the "apply" method frame. */
    vm->top_frame->previous->skip = 1;

    return NJS_APPLIED;
}


njs_ret_t
njs_function_activate(njs_vm_t *vm, njs_function_t *function,
    const njs_value_t *this, const njs_value_t *args, nxt_uint_t nargs,
    njs_index_t retval, size_t advance)
{
    u_char              *return_address;
    njs_ret_t           ret;
    njs_continuation_t  *cont;

    ret = njs_function_frame(vm, function, this, args, nargs,
                             NJS_CONTINUATION_SIZE, 0);
    if (nxt_slow_path(ret != NXT_OK)) {
        return ret;
    }

    return_address = vm->current + advance;

    if (function->native) {
        cont = njs_vm_continuation(vm);

        cont->function = function->u.native;
        cont->args_types = function->args_types;
        cont->retval = retval;
        cont->return_address = return_address;

        vm->current = (u_char *) njs_continuation_nexus;

        ret = NJS_APPLIED;

    } else {
        ret = njs_function_lambda_call(vm, retval, return_address);
    }

    return ret;
}


static njs_ret_t
njs_function_prototype_bind(njs_vm_t *vm, njs_value_t *args, nxt_uint_t nargs,
    njs_index_t unused)
{
    size_t          size;
    njs_value_t     *values;
    njs_function_t  *function;

    if (!njs_is_function(&args[0])) {
        njs_type_error(vm, "\"this\" argument is not a function");
        return NXT_ERROR;
    }

    function = nxt_mp_alloc(vm->mem_pool, sizeof(njs_function_t));
    if (nxt_slow_path(function == NULL)) {
        njs_memory_error(vm);
        return NXT_ERROR;
    }

    *function = *args[0].data.u.function;

    function->object.__proto__ = &vm->prototypes[NJS_PROTOTYPE_FUNCTION].object;
    function->object.shared = 0;
    function->object.extensible = 1;

    if (nargs == 1) {
        args = (njs_value_t *) &njs_value_undefined;

    } else {
        nargs--;
        args++;
    }

    function->args_offset = nargs;
    size = nargs * sizeof(njs_value_t);

    values = nxt_mp_alloc(vm->mem_pool, size);
    if (nxt_slow_path(values == NULL)) {
        njs_memory_error(vm);
        nxt_mp_free(vm->mem_pool, function);
        return NXT_ERROR;
    }

    function->bound = values;

    /* GC: ? retain args. */

    memcpy(values, args, size);

    vm->retval.data.u.function = function;
    vm->retval.type = NJS_FUNCTION;
    vm->retval.data.truth = 1;

    return NXT_OK;
}


static const njs_object_prop_t  njs_function_prototype_properties[] =
{
    {
        .type = NJS_PROPERTY_HANDLER,
        .name = njs_string("length"),
        .value = njs_prop_handler(njs_function_prototype_length),
    },

    {
        .type = NJS_METHOD,
        .name = njs_string("call"),
        .value = njs_native_function(njs_function_prototype_call, 0, 0),
    },

    {
        .type = NJS_METHOD,
        .name = njs_string("apply"),
        .value = njs_native_function(njs_function_prototype_apply, 0, 0),
    },

    {
        .type = NJS_METHOD,
        .name = njs_string("bind"),
        .value = njs_native_function(njs_function_prototype_bind, 0, 0),
    },
};


const njs_object_init_t  njs_function_prototype_init = {
    nxt_string("Function"),
    njs_function_prototype_properties,
    nxt_nitems(njs_function_prototype_properties),
};


njs_ret_t
njs_eval_function(njs_vm_t *vm, njs_value_t *args, nxt_uint_t nargs,
    njs_index_t unused)
{
    njs_internal_error(vm, "Not implemented");

    return NXT_ERROR;
}


static const njs_object_prop_t  njs_eval_function_properties[] =
{
    /* eval.name == "eval". */
    {
        .type = NJS_PROPERTY,
        .name = njs_string("name"),
        .value = njs_string("eval"),
    },

    /* eval.length == 1. */
    {
        .type = NJS_PROPERTY,
        .name = njs_string("length"),
        .value = njs_value(NJS_NUMBER, 1, 1.0),
    },
};


const njs_object_init_t  njs_eval_function_init = {
    nxt_string("eval"),
    njs_eval_function_properties,
    nxt_nitems(njs_eval_function_properties),
};
