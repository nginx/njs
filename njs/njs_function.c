
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


typedef struct {
    njs_continuation_t  continuation;
    njs_function_t      *function;
} njs_function_apply_t;


static nxt_int_t njs_function_apply_frame(njs_vm_t *vm,
    njs_function_t *function, njs_value_t *this, njs_value_t *args,
    nxt_uint_t nargs);
static njs_ret_t njs_function_prototype_apply_continuation(njs_vm_t *vm,
    njs_param_t *param);


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
njs_function_native_frame(njs_vm_t *vm, njs_function_t *function,
    njs_vmcode_t *code)
{
    size_t              size;
    njs_value_t         *this;
    njs_native_frame_t  *frame;

    size = NJS_NATIVE_FRAME_SIZE + function->local_state_size
           + code->nargs * sizeof(njs_value_t);

    frame = njs_function_frame_alloc(vm, size);
    if (nxt_slow_path(frame == NULL)) {
        return NULL;
    }

    frame->function = function;
    frame->ctor = code->ctor;

    this = (njs_value_t *) ((u_char *) njs_native_data(frame)
                            + function->local_state_size);
    frame->arguments = this + 1;
    vm->scopes[NJS_SCOPE_CALLEE_ARGUMENTS] = frame->arguments;

    return this;
}


nxt_noinline njs_native_frame_t *
njs_function_frame_alloc(njs_vm_t *vm, size_t size)
{
    size_t              spare_size;
    uint8_t             first;
    njs_native_frame_t  *frame;

    spare_size = vm->frame->free_size;

    if (nxt_fast_path(size <= spare_size)) {
        frame = (njs_native_frame_t *) vm->frame->free;
        first = 0;

    } else {
        spare_size = size + NJS_FRAME_SPARE_SIZE;
        spare_size = nxt_align_size(spare_size, NJS_FRAME_SPARE_SIZE);

        frame = nxt_mem_cache_align(vm->mem_cache_pool, sizeof(njs_value_t),
                                    spare_size);
        if (nxt_slow_path(frame == NULL)) {
            return NULL;
        }

        first = 1;
    }

    memset(frame, 0, sizeof(njs_native_frame_t));

    frame->first = first;
    frame->free_size = spare_size - size;
    frame->free = (u_char *) frame + size;

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
njs_function_apply(njs_vm_t *vm, njs_function_t *function, njs_param_t *param)
{
    njs_ret_t  ret;

    if (function->native) {
        return function->u.native(vm, param);
    }

    ret = njs_function_frame(vm, function, param, 0);

    if (nxt_fast_path(ret == NXT_OK)) {
        return njs_function_call(vm, param->retval, 0);
    }

    return ret;
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

    native_frame->function = function;
    native_frame->ctor = ctor;

    args = (njs_value_t *) ((u_char *) native_frame + NJS_FRAME_SIZE);
    native_frame->arguments = args + function->args_offset;
    vm->scopes[NJS_SCOPE_CALLEE_ARGUMENTS] = native_frame->arguments;

    frame = (njs_frame_t *) native_frame;

    frame->local = &args[nargs];

    *args++ = *param->this;
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
njs_function_call(njs_vm_t *vm, njs_index_t retval, size_t advance)
{
    njs_frame_t     *frame;
    njs_function_t  *function;

    frame = (njs_frame_t *) vm->frame;

    frame->retval = retval;

    function = frame->native.function;
    frame->return_address = vm->current + advance;
    vm->current = function->u.lambda->u.start;

    frame->prev_arguments = vm->scopes[NJS_SCOPE_ARGUMENTS];
    vm->scopes[NJS_SCOPE_ARGUMENTS] = frame->native.arguments
                                      - function->args_offset;
#if (NXT_DEBUG)
    vm->scopes[NJS_SCOPE_CALLEE_ARGUMENTS] = NULL;
#endif
    frame->prev_local = vm->scopes[NJS_SCOPE_LOCAL];
    vm->scopes[NJS_SCOPE_LOCAL] = frame->local;

    return NJS_APPLIED;
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
    njs_function_constructor_properties,
    nxt_nitems(njs_function_constructor_properties),
};


static njs_ret_t
njs_function_prototype_call(njs_vm_t *vm, njs_param_t *param)
{
    uintptr_t                   nargs;
    njs_ret_t                   ret;
    njs_param_t                 p;
    njs_function_t              *function;
    njs_vmcode_function_call_t  *call;

    if (!njs_is_function(param->this)) {
        vm->exception = &njs_exception_type_error;
        return NXT_ERROR;
    }

    p.this = &param->args[0];
    p.args = &param->args[1];

    nargs = param->nargs;
    function = param->this->data.u.function;

    if (function->native) {

        if (nargs != 0) {
            nargs--;

        } else {
            param->args[0] = njs_value_void;
        }

        p.nargs = nargs;
        p.retval = param->retval;

        ret = njs_normalize_args(vm, &param->args[0], function->args_types,
                                 nargs + 1);
        if (ret != NJS_OK) {
            return ret;
        }

        return function->u.native(vm, &p);
    }

    if (nargs != 0) {
        nargs--;

    } else {
        p.this = (njs_value_t *) &njs_value_void;
    }

    p.nargs = nargs;

    ret = njs_function_frame(vm, function, &p, 0);

    if (nxt_slow_path(ret != NXT_OK)) {
        return NXT_ERROR;
    }

    /* Skip the "call" method frame. */
    vm->frame->previous->skip = 1;

    call = (njs_vmcode_function_call_t *) vm->current;

    return njs_function_call(vm, call->retval,
                             sizeof(njs_vmcode_function_call_t));
}


static njs_ret_t
njs_function_prototype_apply(njs_vm_t *vm, njs_param_t *param)
{
    uintptr_t                   nargs;
    njs_ret_t                   ret;
    njs_param_t                 p;
    njs_array_t                 *array;
    njs_value_t                 *args;
    njs_function_t              *function;
    njs_function_apply_t        *apply;
    njs_vmcode_function_call_t  *code;

    if (!njs_is_function(param->this)) {
        goto type_error;
    }

    args = param->args;
    p.this = &args[0];

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

    function = param->this->data.u.function;

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

        ret = njs_function_apply_frame(vm, function, p.this, p.args, p.nargs);

        if (nxt_fast_path(ret == NXT_OK)) {
            apply = nxt_mem_cache_alloc(vm->mem_cache_pool,
                                        sizeof(njs_function_apply_t));
            if (nxt_slow_path(apply == NULL)) {
                return NXT_ERROR;
            }

            p.this = vm->frame->arguments - 1;
            p.args = vm->frame->arguments;

            /* Skip the "apply" method frame. */
            vm->frame->previous->skip = 1;

            apply->continuation.function =
                                     njs_function_prototype_apply_continuation;
            apply->continuation.this = p.this;
            apply->continuation.args = p.args;
            apply->continuation.nargs = p.nargs;
            apply->function = function;
            vm->frame->continuation = &apply->continuation;

            return njs_function_prototype_apply_continuation(vm, &p);
        }

        return ret;
    }

    if (nargs < 2) {
        if (nargs != 0) {
            p.nargs = 0;

        } else {
            p.this = (njs_value_t *) &njs_value_void;
        }
    }

    ret = njs_function_frame(vm, function, &p, 0);

    if (nxt_fast_path(ret == NXT_OK)) {
        /* Skip the "apply" method frame. */
        vm->frame->previous->skip = 1;

        code = (njs_vmcode_function_call_t *) vm->current;

        return njs_function_call(vm, code->retval,
                                 sizeof(njs_vmcode_function_call_t));
    }

    return NXT_ERROR;

type_error:

    vm->exception = &njs_exception_type_error;

    return NXT_ERROR;
}


static nxt_int_t
njs_function_apply_frame(njs_vm_t *vm, njs_function_t *function,
    njs_value_t *this, njs_value_t *args, nxt_uint_t nargs)
{
    size_t              size;
    njs_value_t         *p;
    njs_native_frame_t  *frame;

    size = NJS_NATIVE_FRAME_SIZE + function->local_state_size
           + (nargs + 1) * sizeof(njs_value_t);

    frame = njs_function_frame_alloc(vm, size);
    if (nxt_slow_path(frame == NULL)) {
        return NXT_ERROR;
    }

    frame->function = function;

    p = (njs_value_t *) ((u_char *) njs_native_data(frame)
                         + function->local_state_size);
    frame->arguments = p + 1;
    vm->scopes[NJS_SCOPE_CALLEE_ARGUMENTS] = frame->arguments;

    frame->previous = vm->frame;
    vm->frame = frame;

    *p = *this;
    memcpy(p + 1, args, nargs * sizeof(njs_value_t));

    return NXT_OK;
}


static njs_ret_t
njs_function_prototype_apply_continuation(njs_vm_t *vm, njs_param_t *param)
{
    njs_ret_t             ret;
    njs_function_apply_t  *apply;

    apply = (njs_function_apply_t *) vm->frame->continuation;

    ret = njs_normalize_args(vm, param->this, apply->function->args_types,
                             param->nargs + 1);
    if (ret != NJS_OK) {
        return ret;
    }

    return apply->function->u.native(vm, param);
}


static njs_ret_t
njs_function_prototype_bind(njs_vm_t *vm, njs_param_t *param)
{
    njs_function_t  *bound;

    if (!njs_is_function(param->this)) {
        vm->exception = &njs_exception_type_error;
        return NXT_ERROR;
    }

    bound = nxt_mem_cache_alloc(vm->mem_cache_pool, sizeof(njs_function_t));

    if (nxt_fast_path(bound != NULL)) {
        nxt_lvlhsh_init(&bound->object.hash);
        nxt_lvlhsh_init(&bound->object.shared_hash);
        bound->object.__proto__ = &vm->prototypes[NJS_PROTOTYPE_FUNCTION];
        bound->args_offset = 1;
        bound->u.lambda = param->this->data.u.function->u.lambda;

        vm->retval.data.u.function = bound;
        vm->retval.type = NJS_FUNCTION;
        vm->retval.data.truth = 1;

        return NXT_OK;
    }

    return NXT_ERROR;
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
    njs_eval_function_properties,
    nxt_nitems(njs_eval_function_properties),
};
