
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
    njs_native_frame_t        *frame;
    njs_promise_capability_t  *capability;

    frame = vm->top_frame;
    frame->retval = retval;

    njs_set_function(&ctor, &vm->constructors[NJS_OBJ_TYPE_PROMISE]);

    capability = njs_promise_new_capability(vm, &ctor);
    if (njs_slow_path(capability == NULL)) {
        return NJS_ERROR;
    }

    ret = njs_function_lambda_call(vm, capability);

    if (ret == NJS_OK) {
        ret = njs_function_call(vm, njs_function(&capability->resolve),
                                &njs_value_undefined, retval, 1, &vm->retval);

    } else if (ret == NJS_AGAIN) {
        ret = NJS_OK;

    } else if (ret == NJS_ERROR) {
        if (njs_is_memory_error(vm, &vm->retval)) {
            return NJS_ERROR;
        }

        ret = njs_function_call(vm, njs_function(&capability->reject),
                                &njs_value_undefined, &vm->retval, 1,
                                &vm->retval);
    }

    *retval = capability->promise;

    return ret;
}


njs_int_t
njs_await_fulfilled(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t unused)
{
    njs_int_t           ret;
    njs_value_t         **cur_local, **cur_closures, *value;
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

    *njs_scope_value(vm, ctx->index) = *value;
    vm->retval = *value;

    vm->top_frame->retval = &vm->retval;

    ret = njs_vmcode_interpreter(vm, ctx->pc, ctx->capability, ctx);

    vm->levels[NJS_LEVEL_LOCAL] = cur_local;
    vm->levels[NJS_LEVEL_CLOSURE] = cur_closures;

    vm->top_frame = top;
    vm->active_frame = frame;

    if (ret == NJS_OK) {
        ret = njs_function_call(vm, njs_function(&ctx->capability->resolve),
                            &njs_value_undefined, &vm->retval, 1, &vm->retval);

        njs_async_context_free(vm, ctx);

    } else if (ret == NJS_AGAIN) {
        ret = NJS_OK;

    } else if (ret == NJS_ERROR) {
        if (njs_is_memory_error(vm, &vm->retval)) {
            return NJS_ERROR;
        }

        value = &vm->retval;

        goto failed;
    }

    return ret;

failed:

    (void) njs_function_call(vm, njs_function(&ctx->capability->reject),
                             &njs_value_undefined, value, 1, &vm->retval);

    njs_async_context_free(vm, ctx);

    return NJS_ERROR;
}


njs_int_t
njs_await_rejected(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t unused)
{
    njs_value_t      *value;
    njs_async_ctx_t  *ctx;

    ctx = vm->top_frame->function->context;

    value = njs_arg(args, nargs, 1);

    if (ctx->await->native.pc == ctx->pc) {
        /* No catch block was set before await. */
        (void) njs_function_call(vm, njs_function(&ctx->capability->reject),
                                 &njs_value_undefined, value, 1, &vm->retval);

        njs_async_context_free(vm, ctx);

        return NJS_ERROR;
    }

    /* ctx->await->native.pc points to a catch block here. */

    ctx->pc = ctx->await->native.pc;

    return njs_await_fulfilled(vm, args, nargs, unused);
}


static void
njs_async_context_free(njs_vm_t *vm, njs_async_ctx_t *ctx)
{
    njs_mp_free(vm->mem_pool, ctx->capability);
    njs_mp_free(vm->mem_pool, ctx);
}


static const njs_object_prop_t  njs_async_constructor_properties[] =
{
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


const njs_object_init_t  njs_async_constructor_init = {
    njs_async_constructor_properties,
    njs_nitems(njs_async_constructor_properties),
};


static const njs_object_prop_t  njs_async_prototype_properties[] =
{
    {
        .type = NJS_PROPERTY,
        .name = njs_wellknown_symbol(NJS_SYMBOL_TO_STRING_TAG),
        .value = njs_string("AsyncFunction"),
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY_HANDLER,
        .name = njs_string("constructor"),
        .value = njs_prop_handler(njs_object_prototype_create_constructor),
        .configurable = 1,
    },
};


const njs_object_init_t  njs_async_prototype_init = {
    njs_async_prototype_properties,
    njs_nitems(njs_async_prototype_properties),
};


const njs_object_type_init_t  njs_async_function_type_init = {
    .constructor = njs_native_ctor(njs_function_constructor, 1, 1),
    .constructor_props = &njs_async_constructor_init,
    .prototype_props = &njs_async_prototype_init,
    .prototype_value = { .object = { .type = NJS_OBJECT } },
};


const njs_object_prop_t  njs_async_function_instance_properties[] =
{
    {
        .type = NJS_PROPERTY_HANDLER,
        .name = njs_string("length"),
        .value = njs_prop_handler(njs_function_instance_length),
        .configurable = 1,
    },
};


const njs_object_init_t  njs_async_function_instance_init = {
    njs_async_function_instance_properties,
    njs_nitems(njs_async_function_instance_properties),
};
