
/*
 * Copyright (C) Alexander Borisov
 * Copyright (C) Nginx, Inc.
 */

#include <njs_main.h>


static void
njs_async_context_free(njs_vm_t *vm, njs_async_ctx_t *ctx);


njs_int_t
njs_async_function_frame_invoke(njs_vm_t *vm, njs_value_t *retval)
{
    njs_int_t                 ret;
    njs_value_t               ctor;
    njs_promise_capability_t  *capability;

    njs_set_function(&ctor, &njs_vm_ctor(vm, NJS_OBJ_TYPE_PROMISE));

    capability = njs_promise_new_capability(vm, &ctor);
    if (njs_slow_path(capability == NULL)) {
        return NJS_ERROR;
    }

    ret = njs_function_lambda_call(vm, retval, capability);

    if (ret == NJS_OK) {
        ret = njs_function_call(vm, njs_function(&capability->resolve),
                                &njs_value_undefined, retval, 1, retval);

    } else if (ret == NJS_AGAIN) {
        ret = NJS_OK;

    } else if (ret == NJS_ERROR) {
        if (njs_is_memory_error(vm, &vm->exception)) {
            return NJS_ERROR;
        }

        *retval = njs_vm_exception(vm);

        ret = njs_function_call(vm, njs_function(&capability->reject),
                                &njs_value_undefined, retval, 1,
                                retval);
    }

    njs_value_assign(retval, &capability->promise);

    return ret;
}


njs_int_t
njs_await_fulfilled(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t exception, njs_value_t *retval)
{
    njs_int_t           ret;
    njs_value_t         **cur_local, **cur_closures, *value, result;
    njs_frame_t         *frame, *async_frame;
    njs_async_ctx_t     *ctx;
    njs_native_frame_t  *top, *async;

    ctx = vm->top_frame->function->context;

    value = njs_arg(args, nargs, 1);

    async_frame = ctx->await;
    async = &async_frame->native;
    async->previous = vm->top_frame;

    cur_local = vm->levels[NJS_LEVEL_LOCAL];
    cur_closures = vm->levels[NJS_LEVEL_CLOSURE];
    top = vm->top_frame;
    frame = vm->active_frame;

    vm->levels[NJS_LEVEL_LOCAL] = async->local;
    vm->levels[NJS_LEVEL_CLOSURE] = njs_function_closures(async->function);

    vm->top_frame = async;
    vm->active_frame = async_frame;

    if (exception) {
        njs_vm_throw(vm, value);

    } else {
        *njs_scope_value(vm, ctx->index) = *value;
    }

    ret = njs_vmcode_interpreter(vm, ctx->pc, &result, ctx->capability, ctx);

    vm->levels[NJS_LEVEL_LOCAL] = cur_local;
    vm->levels[NJS_LEVEL_CLOSURE] = cur_closures;

    vm->top_frame = top;
    vm->active_frame = frame;

    if (ret == NJS_OK) {
        ret = njs_function_call(vm, njs_function(&ctx->capability->resolve),
                                &njs_value_undefined, &result, 1, retval);

        njs_async_context_free(vm, ctx);

    } else if (ret == NJS_AGAIN) {
        ret = NJS_OK;

    } else if (ret == NJS_ERROR) {
        if (njs_is_memory_error(vm, &vm->exception)) {
            return NJS_ERROR;
        }

        result = njs_vm_exception(vm);

        (void) njs_function_call(vm, njs_function(&ctx->capability->reject),
                                 &njs_value_undefined, &result, 1, retval);

        njs_async_context_free(vm, ctx);

        return NJS_ERROR;
    }

    return ret;
}


njs_int_t
njs_await_rejected(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t unused, njs_value_t *retval)
{
    njs_value_t      *value;
    njs_async_ctx_t  *ctx;

    ctx = vm->top_frame->function->context;

    value = njs_arg(args, nargs, 1);

    if (ctx->await->native.pc == ctx->pc) {
        /* No catch block was set before await. */
        (void) njs_function_call(vm, njs_function(&ctx->capability->reject),
                                 &njs_value_undefined, value, 1, retval);

        njs_async_context_free(vm, ctx);

        return NJS_ERROR;
    }

    /* ctx->await->native.pc points to a catch block here. */

    ctx->pc = ctx->await->native.pc;

    return njs_await_fulfilled(vm, args, nargs, 1, retval);
}


static void
njs_async_context_free(njs_vm_t *vm, njs_async_ctx_t *ctx)
{
    njs_mp_free(vm->mem_pool, ctx->capability);
    njs_mp_free(vm->mem_pool, ctx);
}


static njs_object_propi_t  njs_async_constructor_properties[] =
{
    NJS_DECLARE_PROP_LENGTH(1),

    NJS_DECLARE_PROP_NAME(vs_AsyncFunction),

    NJS_DECLARE_PROP_HANDLER(vs_prototype, njs_object_prototype_create,
                             0, 0),
};


static const njs_object_init_t  njs_async_constructor_init = {
    njs_async_constructor_properties,
    njs_nitems(njs_async_constructor_properties),
};


static njs_object_propi_t  njs_async_prototype_properties[] =
{
    NJS_DECLARE_PROP_VALUE(vw_toStringTag, njs_atom.vs_AsyncFunction,
                           NJS_OBJECT_PROP_VALUE_C),

    NJS_DECLARE_PROP_HANDLER(vs_constructor,
                             njs_object_prototype_create_constructor, 0,
                             NJS_OBJECT_PROP_VALUE_CW),
};


static const njs_object_init_t  njs_async_prototype_init = {
    njs_async_prototype_properties,
    njs_nitems(njs_async_prototype_properties),
};


const njs_object_type_init_t  njs_async_function_type_init = {
    .constructor = njs_native_ctor(njs_function_constructor, 1, 1),
    .constructor_props = &njs_async_constructor_init,
    .prototype_props = &njs_async_prototype_init,
    .prototype_value = { .object = { .type = NJS_OBJECT } },
};


static njs_object_propi_t  njs_async_function_instance_properties[] =
{
    NJS_DECLARE_PROP_HANDLER(vs_length, njs_function_instance_length, 0,
                             NJS_OBJECT_PROP_VALUE_C),

    NJS_DECLARE_PROP_HANDLER(vs_name, njs_function_instance_name, 0,
                             NJS_OBJECT_PROP_VALUE_C),
};


const njs_object_init_t  njs_async_function_instance_init = {
    njs_async_function_instance_properties,
    njs_nitems(njs_async_function_instance_properties),
};
