
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
#include <nxt_mem_cache_pool.h>
#include <njscript.h>
#include <njs_vm.h>
#include <njs_string.h>
#include <njs_object.h>
#include <njs_array.h>
#include <njs_function.h>
#include <njs_error.h>
#include <string.h>


static njs_ret_t njs_function_activate(njs_vm_t *vm, njs_function_t *function,
    njs_value_t *this, njs_value_t *args, nxt_uint_t nargs, njs_index_t retval);


njs_function_t *
njs_function_alloc(njs_vm_t *vm)
{
    njs_function_t  *function;

    function = nxt_mem_cache_zalloc(vm->mem_cache_pool, sizeof(njs_function_t));

    if (nxt_fast_path(function != NULL)) {
        /*
         * nxt_mem_cache_zalloc() does also:
         *   nxt_lvlhsh_init(&function->object.hash);
         *   function->object.__proto__ = NULL;
         */

        function->object.shared_hash = vm->shared->function_prototype_hash;
        function->object.type = NJS_FUNCTION;
        function->object.shared = 1;
        function->object.extensible = 1;
        function->args_offset = 1;

        function->u.lambda = nxt_mem_cache_zalloc(vm->mem_cache_pool,
                                                 sizeof(njs_function_lambda_t));
        if (nxt_slow_path(function->u.lambda == NULL)) {
            return NULL;
        }
    }

    return function;
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

    copy = nxt_mem_cache_alloc(vm->mem_cache_pool, size);
    if (nxt_slow_path(copy == NULL)) {
        return copy;
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


njs_ret_t
njs_function_native_frame(njs_vm_t *vm, njs_function_t *function,
    const njs_value_t *this, njs_value_t *args, nxt_uint_t nargs,
    size_t reserve, nxt_bool_t ctor)
{
    size_t              size;
    nxt_uint_t          n;
    njs_value_t         *value, *bound;
    njs_native_frame_t  *frame;

    reserve = nxt_max(reserve, function->continuation_size);

    size = NJS_NATIVE_FRAME_SIZE + reserve
           + (function->args_offset + nargs) * sizeof(njs_value_t);

    frame = njs_function_frame_alloc(vm, size);
    if (nxt_slow_path(frame == NULL)) {
        return NXT_ERROR;
    }

    frame->function = function;
    frame->nargs = function->args_offset + nargs;
    frame->ctor = ctor;

    value = (njs_value_t *) (njs_continuation(frame) + reserve);
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
njs_function_frame(njs_vm_t *vm, njs_function_t *function,
    const njs_value_t *this, njs_value_t *args, nxt_uint_t nargs,
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

    size = NJS_FRAME_SIZE
           + (function->args_offset + max_args) * sizeof(njs_value_t)
           + lambda->local_size
           + closures * sizeof(njs_closure_t *);

    native_frame = njs_function_frame_alloc(vm, size);
    if (nxt_slow_path(native_frame == NULL)) {
        return NXT_ERROR;
    }

    native_frame->function = function;
    native_frame->nargs = nargs;
    native_frame->ctor = ctor;

    /* Function arguments. */

    value = (njs_value_t *) ((u_char *) native_frame + NJS_FRAME_SIZE);
    native_frame->arguments = value;

    bound = function->bound;

    if (bound == NULL) {
        *value++ = *this;

    } else {
        n = function->args_offset;

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
        *value++ = njs_value_void;
        max_args--;
    }

    frame = (njs_frame_t *) native_frame;
    frame->local = value;

    return NXT_OK;
}


nxt_noinline njs_native_frame_t *
njs_function_frame_alloc(njs_vm_t *vm, size_t size)
{
    size_t              spare_size, chunk_size;
    njs_native_frame_t  *frame;

    spare_size = vm->top_frame->free_size;

    if (nxt_fast_path(size <= spare_size)) {
        frame = (njs_native_frame_t *) vm->top_frame->free;
        chunk_size = 0;

    } else {
        spare_size = size + NJS_FRAME_SPARE_SIZE;
        spare_size = nxt_align_size(spare_size, NJS_FRAME_SPARE_SIZE);

        if (vm->stack_size + spare_size > NJS_MAX_STACK_SIZE) {
            njs_exception_range_error(vm, "Maximum call stack size exceeded",
                                      NULL);
            return NULL;
        }

        frame = nxt_mem_cache_align(vm->mem_cache_pool, sizeof(njs_value_t),
                                    spare_size);
        if (nxt_slow_path(frame == NULL)) {
            return NULL;
        }

        chunk_size = spare_size;
        vm->stack_size += spare_size;
    }

    memset(frame, 0, sizeof(njs_native_frame_t));

    frame->size = chunk_size;
    frame->free_size = spare_size - size;
    frame->free = (u_char *) frame + size;

    frame->previous = vm->top_frame;
    vm->top_frame = frame;

    return frame;
}


nxt_noinline njs_ret_t
njs_function_apply(njs_vm_t *vm, njs_function_t *function, njs_value_t *args,
    nxt_uint_t nargs, njs_index_t retval)
{
    njs_ret_t           ret;
    njs_continuation_t  *cont;

    if (function->native) {
        ret = njs_function_native_frame(vm, function, &args[0], &args[1],
                                        nargs - 1, NJS_CONTINUATION_SIZE, 0);
        if (ret != NJS_OK) {
            return ret;
        }

        cont = njs_vm_continuation(vm);

        cont->function = function->u.native;
        cont->args_types = function->args_types;
        cont->retval = retval;

        cont->return_address = vm->current;
        vm->current = (u_char *) njs_continuation_nexus;

        return NJS_APPLIED;
    }

    ret = njs_function_frame(vm, function, &args[0], &args[1], nargs - 1, 0);

    if (nxt_fast_path(ret == NXT_OK)) {
        return njs_function_call(vm, retval, 0);
    }

    return ret;
}


nxt_noinline njs_ret_t
njs_function_call(njs_vm_t *vm, njs_index_t retval, size_t advance)
{
    size_t                 size;
    nxt_uint_t             n, nesting;
    njs_frame_t            *frame;
    njs_value_t            *value;
    njs_closure_t          *closure, **closures;
    njs_function_t         *function;
    njs_function_lambda_t  *lambda;

    frame = (njs_frame_t *) vm->top_frame;

    frame->retval = retval;

    function = frame->native.function;
    frame->return_address = vm->current + advance;

    lambda = function->u.lambda;
    vm->current = lambda->u.start;

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
            closure = nxt_mem_cache_align(vm->mem_cache_pool,
                                          sizeof(njs_value_t), size);
            if (nxt_slow_path(closure == NULL)) {
                return NXT_ERROR;
            }

            /* TODO: copy initialzed values. */

            size -= sizeof(njs_value_t);
            closure->u.count = 0;
            value = closure->values;

            do {
                *value++ = njs_value_void;
                size -= sizeof(njs_value_t);
            } while (size != 0);
        }

        frame->closures[n] = closure;
        vm->scopes[NJS_SCOPE_CLOSURE + n] = &closure->u.values;
    }

    frame->previous_active_frame = vm->active_frame;
    vm->active_frame = frame;

    return NJS_APPLIED;
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
njs_function_prototype_create(njs_vm_t *vm, njs_value_t *value)
{
    njs_value_t  *proto;

    proto = njs_function_property_prototype_create(vm, value);

    if (nxt_fast_path(proto != NULL)) {
        vm->retval = *proto;
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
        .type = NJS_NATIVE_GETTER,
        .name = njs_string("prototype"),
        .value = njs_native_getter(njs_object_prototype_create),
    },
};


const njs_object_init_t  njs_function_constructor_init = {
    nxt_string("Function"),
    njs_function_constructor_properties,
    nxt_nitems(njs_function_constructor_properties),
};


static njs_ret_t
njs_function_prototype_call(njs_vm_t *vm, njs_value_t *args, nxt_uint_t nargs,
    njs_index_t retval)
{
    njs_value_t     *this;
    njs_function_t  *function;

    if (!njs_is_function(&args[0])) {
        njs_exception_type_error(vm, NULL, NULL);
        return NXT_ERROR;
    }

    if (nargs > 1) {
        this = &args[1];
        nargs -= 2;

    } else {
        this = (njs_value_t *) &njs_value_void;
        nargs = 0;
    }

    function = args[0].data.u.function;

    return njs_function_activate(vm, function, this, &args[2], nargs, retval);
}


static njs_ret_t
njs_function_prototype_apply(njs_vm_t *vm, njs_value_t *args, nxt_uint_t nargs,
    njs_index_t retval)
{
    njs_array_t     *array;
    njs_value_t     *this;
    njs_function_t  *function;

    if (!njs_is_function(&args[0])) {
        goto type_error;
    }

    function = args[0].data.u.function;
    this = &args[1];

    if (nargs > 2) {
        if (!njs_is_array(&args[2])) {
            goto type_error;
        }

        array = args[2].data.u.array;
        args = array->start;
        nargs = array->length;

    } else {
        if (nargs == 1) {
            this = (njs_value_t *) &njs_value_void;
        }

        nargs = 0;
    }

    return njs_function_activate(vm, function, this, args, nargs, retval);

type_error:

    njs_exception_type_error(vm, NULL, NULL);

    return NXT_ERROR;
}


static njs_ret_t
njs_function_activate(njs_vm_t *vm, njs_function_t *function, njs_value_t *this,
    njs_value_t *args, nxt_uint_t nargs, njs_index_t retval)
{
    njs_ret_t           ret;
    njs_continuation_t  *cont;

    if (function->native) {
        ret = njs_function_native_frame(vm, function, this, args, nargs,
                                        NJS_CONTINUATION_SIZE, 0);
        if (nxt_slow_path(ret != NXT_OK)) {
            return ret;
        }

        /* Skip the "call/apply" method frame. */
        vm->top_frame->previous->skip = 1;

        cont = njs_vm_continuation(vm);

        cont->function = function->u.native;
        cont->args_types = function->args_types;
        cont->retval = retval;

        cont->return_address = vm->current
                               + sizeof(njs_vmcode_function_call_t);
        vm->current = (u_char *) njs_continuation_nexus;

        return NJS_APPLIED;
    }

    ret = njs_function_frame(vm, function, this, args, nargs, 0);

    if (nxt_slow_path(ret != NXT_OK)) {
        return ret;
    }

    /* Skip the "call/apply" method frame. */
    vm->top_frame->previous->skip = 1;

    return njs_function_call(vm, retval, sizeof(njs_vmcode_function_call_t));
}


static njs_ret_t
njs_function_prototype_bind(njs_vm_t *vm, njs_value_t *args, nxt_uint_t nargs,
    njs_index_t unused)
{
    size_t          size;
    njs_value_t     *values;
    njs_function_t  *function;

    if (!njs_is_function(&args[0])) {
        njs_exception_type_error(vm, NULL, NULL);
        return NXT_ERROR;
    }

    function = nxt_mem_cache_alloc(vm->mem_cache_pool, sizeof(njs_function_t));
    if (nxt_slow_path(function == NULL)) {
        return NXT_ERROR;
    }

    *function = *args[0].data.u.function;

    function->object.__proto__ = &vm->prototypes[NJS_PROTOTYPE_FUNCTION].object;
    function->object.shared = 0;
    function->object.extensible = 1;

    if (nargs == 1) {
        args = (njs_value_t *) &njs_value_void;

    } else {
        nargs--;
        args++;
    }

    function->args_offset = nargs;
    size = nargs * sizeof(njs_value_t);

    values = nxt_mem_cache_alloc(vm->mem_cache_pool, size);
    if (nxt_slow_path(values == NULL)) {
        nxt_mem_cache_free(vm->mem_cache_pool, function);
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
    njs_exception_internal_error(vm, "Not implemented", NULL);

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
    nxt_string("Function"),
    njs_eval_function_properties,
    nxt_nitems(njs_eval_function_properties),
};
