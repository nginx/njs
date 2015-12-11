
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) NGINX, Inc.
 */

#include <nxt_types.h>
#include <nxt_clang.h>
#include <nxt_alignment.h>
#include <nxt_stub.h>
#include <nxt_array.h>
#include <nxt_lvlhsh.h>
#include <nxt_mem_cache_pool.h>
#include <njscript.h>
#include <njs_vm.h>
#include <njs_object.h>
#include <njs_array.h>
#include <njs_function.h>
#include <string.h>


njs_function_t *
njs_function_alloc(njs_vm_t *vm)
{
    njs_function_t  *function;

    function = nxt_mem_cache_zalloc(vm->mem_cache_pool, sizeof(njs_function_t));

    if (nxt_fast_path(function != NULL)) {
        function->object.__proto__ = &vm->prototypes[NJS_PROTOTYPE_FUNCTION];
        function->args_offset = 1;

        function->u.lambda = nxt_mem_cache_zalloc(vm->mem_cache_pool,
                                                 sizeof(njs_function_lambda_t));
        if (nxt_slow_path(function->u.lambda == NULL)) {
            return NULL;
        }
    }

    return function;
}


njs_value_t *
njs_function_native_frame(njs_vm_t *vm, njs_native_t native, size_t local_size,
    njs_vmcode_t *code)
{
    size_t              size;
    njs_value_t         *this;
    njs_native_frame_t  *frame;

    size = NJS_NATIVE_FRAME_SIZE + local_size
           + code->nargs * sizeof(njs_value_t);

    frame = njs_function_frame_alloc(vm, size);
    if (nxt_slow_path(frame == NULL)) {
        return NULL;
    }

    frame->u.native = native;
    frame->native = 1;
    frame->ctor = code->ctor;

    this = (njs_value_t *) ((u_char *) njs_native_data(frame) + local_size);
    frame->arguments = this + 1;
    vm->scopes[NJS_SCOPE_CALLEE_ARGUMENTS] = frame->arguments;

    return this;
}


nxt_noinline njs_native_frame_t *
njs_function_frame_alloc(njs_vm_t *vm, size_t size)
{
    size_t              spare_size;
    njs_native_frame_t  *frame;

    spare_size = vm->frame->free_size;

    if (nxt_fast_path(size <= spare_size)) {
        frame = (njs_native_frame_t *) vm->frame->free;
        frame->first = 0;
        frame->skip = 0;

    } else {
        spare_size = size + NJS_FRAME_SPARE_SIZE;
        spare_size = nxt_align_size(spare_size, NJS_FRAME_SPARE_SIZE);

        frame = nxt_mem_cache_align(vm->mem_cache_pool, sizeof(njs_value_t),
                                    spare_size);
        if (nxt_slow_path(frame == NULL)) {
            return NULL;
        }

        frame->first = 1;
        frame->skip = 0;
    }

    frame->free_size = spare_size - size;
    frame->free = (u_char *) frame + size;

    frame->reentrant = 0;
    frame->trap_reference = 0;

    frame->exception.next = NULL;
    frame->exception.catch = NULL;

    frame->previous = vm->frame;
    vm->frame = frame;

    return frame;
}


njs_ret_t
njs_function_constructor(njs_vm_t *vm, njs_param_t *param)
{
    return NXT_ERROR;
}


nxt_noinline njs_ret_t
njs_function_apply(njs_vm_t *vm, njs_value_t *name, njs_param_t *param)
{
    njs_ret_t       ret;
    njs_function_t  *function;

    if (njs_is_native(name)) {
        return name->data.u.method(vm, param);

    } else if (njs_is_function(name)) {

        function = name->data.u.function;

        if (function->native) {
            return function->u.native(vm, param);
        }

        ret = njs_function_frame(vm, function, param, 0);

        if (nxt_fast_path(ret == NXT_OK)) {
            vm->retval = njs_value_void;

            return njs_function_call(vm, param->retval);
        }
    }

    return NXT_ERROR;
}


nxt_noinline njs_ret_t
njs_function_frame(njs_vm_t *vm, njs_function_t *function, njs_param_t *param,
    nxt_bool_t ctor)
{
    size_t              size;
    uintptr_t           nargs, n;
    njs_value_t         *args, *arguments;
    njs_frame_t         *frame;
    njs_native_frame_t  *native_frame;

    nargs = nxt_max(param->nargs, function->u.lambda->nargs);

    size = NJS_FRAME_SIZE
           + nargs * sizeof(njs_value_t)
           + function->u.lambda->local_size;

    native_frame = njs_function_frame_alloc(vm, size);
    if (nxt_slow_path(native_frame == NULL)) {
        return NXT_ERROR;
    }

    native_frame->u.function = function;
    native_frame->native = 0;
    native_frame->ctor = ctor;

    args = (njs_value_t *) ((u_char *) native_frame + NJS_FRAME_SIZE);
    native_frame->arguments = args + function->args_offset;
    vm->scopes[NJS_SCOPE_CALLEE_ARGUMENTS] = native_frame->arguments;

    frame = (njs_frame_t *) native_frame;

    frame->local = &args[nargs];

    *args++ = *param->object;
    nargs--;

    arguments = param->args;

    if (arguments != NULL) {
        n = param->nargs;

        while (n != 0) {
            *args++ = *arguments++;
            nargs--;
            n--;
        }
    }

    while (nargs != 0) {
        *args++ = njs_value_void;
        nargs--;
    }

    memcpy(frame->local, function->u.lambda->local_scope,
           function->u.lambda->local_size);

    return NXT_OK;
}


nxt_noinline njs_ret_t
njs_function_call(njs_vm_t *vm, njs_index_t retval)
{
    njs_frame_t     *frame;
    njs_function_t  *function;

    frame = (njs_frame_t *) vm->frame;

    frame->retval = retval;

    function = frame->native.u.function;
    frame->native.u.return_address = vm->current;
    vm->current = function->u.lambda->u.start;

    frame->prev_arguments = vm->scopes[NJS_SCOPE_ARGUMENTS];
    vm->scopes[NJS_SCOPE_ARGUMENTS] = frame->native.arguments
                                      - function->args_offset;
#if (NXT_DEBUG)
    vm->scopes[NJS_SCOPE_CALLEE_ARGUMENTS] = NULL;
#endif
    frame->prev_local = vm->scopes[NJS_SCOPE_LOCAL];
    vm->scopes[NJS_SCOPE_LOCAL] = frame->local;

    return NJS_PASS;
}


static const njs_object_prop_t  njs_function_constructor_properties[] =
{
    /* Function.name == "Function". */
    { njs_string("Function"),
      njs_string("name"),
      NJS_PROPERTY, 0, 0, 0, },

    /* Function.length == 1. */
    { njs_value(NJS_NUMBER, 0, 1.0),
      njs_string("length"),
      NJS_PROPERTY, 0, 0, 0, },

    /* Function.prototype. */
    { njs_getter(njs_object_prototype_create),
      njs_string("prototype"),
      NJS_NATIVE_GETTER, 0, 0, 0, },
};


const njs_object_init_t  njs_function_constructor_init = {
     njs_function_constructor_properties,
     nxt_nitems(njs_function_constructor_properties),
};


static njs_ret_t
njs_function_prototype_call(njs_vm_t *vm, njs_param_t *param)
{
    uintptr_t                   nargs;
    njs_ret_t                   ret;
    njs_param_t                 p;
    njs_value_t                 *func;
    njs_function_t              *function;
    njs_vmcode_function_call_t  *call;

    p.object = &param->args[0];
    p.args = &param->args[1];

    func = param->object;
    nargs = param->nargs;

    if (njs_is_native(func)) {

        if (nargs != 0) {
            p.nargs = nargs - 1;
            p.retval = param->retval;

            return func->data.u.method(vm, &p);
        }

        vm->exception = &njs_exception_type_error;
        return NXT_ERROR;
    }

    function = func->data.u.function;

    if (function->native) {

        if (nargs != 0) {
            p.nargs = nargs - 1;
            p.retval = param->retval;

            return function->u.native(vm, &p);
        }

        vm->exception = &njs_exception_type_error;
        return NXT_ERROR;
    }

    if (nargs != 0) {
        nargs--;

    } else {
        p.object = (njs_value_t *) &njs_value_void;
    }

    p.nargs = nargs;

    ret = njs_function_frame(vm, function, &p, 0);

    if (nxt_slow_path(ret != NXT_OK)) {
        return NXT_ERROR;
    }

    /* Skip the "call" method frame. */
    vm->frame->previous->skip = 1;

    call = (njs_vmcode_function_call_t *) vm->current;

    return njs_function_call(vm, call->retval);
}


static njs_ret_t
njs_function_prototype_apply(njs_vm_t *vm, njs_param_t *param)
{
    uintptr_t                   nargs;
    njs_ret_t                   ret;
    njs_param_t                 p;
    njs_array_t                 *array;
    njs_value_t                 *func, *args;
    njs_function_t              *function;
    njs_vmcode_function_call_t  *code;

    args = param->args;
    p.object = &args[0];

    nargs = param->nargs;
    p.nargs = nargs;

    if (nargs > 1) {
        if (!njs_is_array(&args[1])) {
            goto type_error;
        }

        array = args[1].data.u.array;
        p.args = array->start;
        p.nargs = array->length;
    }

    func = param->object;

    if (njs_is_native(func)) {
        p.retval = param->retval;

        if (nargs < 2) {
            if (nargs != 0) {
                p.args = &args[1];
                p.nargs = nargs - 1;

            } else {
                goto type_error;
            }
        }

        return func->data.u.method(vm, &p);
    }

    function = func->data.u.function;

    if (function->native) {
        p.retval = param->retval;

        if (nargs < 2) {
            if (nargs != 0) {
                p.args = &args[1];
                p.nargs = nargs - 1;

            } else {
                goto type_error;
            }
        }

        return function->u.native(vm, &p);
    }

    if (nargs < 2) {
        if (nargs != 0) {
            p.nargs = 0;

        } else {
            p.object = (njs_value_t *) &njs_value_void;
        }
    }

    ret = njs_function_frame(vm, function, &p, 0);

    if (nxt_fast_path(ret == NXT_OK)) {
        /* Skip the "apply" method frame. */
        vm->frame->previous->skip = 1;

        code = (njs_vmcode_function_call_t *) vm->current;

        return njs_function_call(vm, code->retval);
    }

    return NXT_ERROR;

type_error:

    vm->exception = &njs_exception_type_error;

    return NXT_ERROR;
}


static njs_ret_t
njs_function_prototype_bind(njs_vm_t *vm, njs_param_t *param)
{
    njs_value_t     *func;
    njs_function_t  *bound;

    bound = nxt_mem_cache_alloc(vm->mem_cache_pool, sizeof(njs_function_t));

    if (nxt_fast_path(bound != NULL)) {
        nxt_lvlhsh_init(&bound->object.hash);
        nxt_lvlhsh_init(&bound->object.shared_hash);
        bound->object.__proto__ = &vm->prototypes[NJS_PROTOTYPE_FUNCTION];
        bound->args_offset = 1;

        func = param->object;
        bound->u.lambda = func->data.u.function->u.lambda;

        vm->retval.data.u.function = bound;
        vm->retval.type = NJS_FUNCTION;
        vm->retval.data.truth = 1;

        return NXT_OK;
    }

    return NXT_ERROR;
}


static const njs_object_prop_t  njs_function_prototype_properties[] =
{
    { njs_native_function(njs_function_prototype_call, 0),
      njs_string("call"),
      NJS_METHOD, 0, 0, 0, },

    { njs_native_function(njs_function_prototype_apply, 0),
      njs_string("apply"),
      NJS_METHOD, 0, 0, 0, },

    { njs_native_function(njs_function_prototype_bind, 0),
      njs_string("bind"),
      NJS_METHOD, 0, 0, 0, },
};


const njs_object_init_t  njs_function_prototype_init = {
     njs_function_prototype_properties,
     nxt_nitems(njs_function_prototype_properties),
};


njs_ret_t
njs_eval_function(njs_vm_t *vm, njs_param_t *param)
{
    return NXT_ERROR;
}


static const njs_object_prop_t  njs_eval_function_properties[] =
{
    /* eval.name == "eval". */
    { njs_string("eval"),
      njs_string("name"),
      NJS_PROPERTY, 0, 0, 0, },

    /* eval.length == 1. */
    { njs_value(NJS_NUMBER, 0, 1.0),
      njs_string("length"),
      NJS_PROPERTY, 0, 0, 0, },
};


const njs_object_init_t  njs_eval_function_init = {
     njs_eval_function_properties,
     nxt_nitems(njs_eval_function_properties),
};
