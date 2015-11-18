
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) NGINX, Inc.
 */

#include <nxt_types.h>
#include <nxt_clang.h>
#include <nxt_alignment.h>
#include <nxt_stub.h>
#include <nxt_lvlhsh.h>
#include <nxt_mem_cache_pool.h>
#include <njscript.h>
#include <njs_vm.h>
#include <njs_object.h>
#include <njs_array.h>
#include <njs_function.h>
#include <string.h>


static const njs_vmcode_1addr_t  njs_trap_strings[] = {
    { .code = { .operation = njs_vmcode_string_primitive,
                .operands =  NJS_VMCODE_1OPERAND,
                .retval = NJS_VMCODE_NO_RETVAL },
      .index = 0 },
    { .code = { .operation = njs_vmcode_string_primitive,
                .operands =  NJS_VMCODE_1OPERAND,
                .retval = NJS_VMCODE_NO_RETVAL },
      .index = 1 },
    { .code = { .operation = njs_vmcode_restart,
                .operands =  NJS_VMCODE_NO_OPERAND,
                .retval = NJS_VMCODE_NO_RETVAL } },
};


static const njs_vmcode_1addr_t  njs_trap_numbers[] = {
    { .code = { .operation = njs_vmcode_number_primitive,
                .operands =  NJS_VMCODE_1OPERAND,
                .retval = NJS_VMCODE_NO_RETVAL },
      .index = 0 },
    { .code = { .operation = njs_vmcode_number_primitive,
                .operands =  NJS_VMCODE_1OPERAND,
                .retval = NJS_VMCODE_NO_RETVAL },
      .index = 1 },
    { .code = { .operation = njs_vmcode_restart,
                .operands =  NJS_VMCODE_NO_OPERAND,
                .retval = NJS_VMCODE_NO_RETVAL } },
};


static const njs_vmcode_1addr_t  njs_trap_number[] = {
    { .code = { .operation = njs_vmcode_number_primitive,
                .operands =  NJS_VMCODE_1OPERAND,
                .retval = NJS_VMCODE_NO_RETVAL },
      .index = 0 },
    { .code = { .operation = njs_vmcode_restart,
                .operands =  NJS_VMCODE_NO_OPERAND,
                .retval = NJS_VMCODE_NO_RETVAL } },
};


static const njs_vm_trap_t  njs_vm_traps[] = {
    /* NJS_TRAP_PROPERTY */  { &njs_trap_strings[1], 0 },
    /* NJS_TRAP_STRINGS */   { &njs_trap_strings[0], 0 },
    /* NJS_TRAP_INCDEC */    { &njs_trap_numbers[1], 1 },
    /* NJS_TRAP_NUMBERS */   { &njs_trap_numbers[0], 0 },
    /* NJS_TRAP_NUMBER */    { &njs_trap_number[0],  0 },
};


njs_function_t *
njs_function_alloc(njs_vm_t *vm)
{
    njs_function_t         *func;
    njs_function_script_t  *script;

    func = nxt_mem_cache_zalign(vm->mem_cache_pool, sizeof(njs_value_t),
                                sizeof(njs_function_t));

    if (nxt_fast_path(func != NULL)) {
        func->object.__proto__ = &vm->prototypes[NJS_PROTOTYPE_FUNCTION];
        func->args_offset = 1;

        script = nxt_mem_cache_zalloc(vm->mem_cache_pool,
                                      sizeof(njs_function_script_t));
        if (nxt_slow_path(script == NULL)) {
            return NULL;
        }

        func->code.script = script;
    }

    return func;
}


nxt_noinline njs_value_t *
njs_vmcode_native_frame(njs_vm_t *vm, njs_value_t *method, uintptr_t nargs,
    nxt_bool_t ctor)
{
    size_t              size, spare_size;
    njs_value_t         *this;
    njs_native_frame_t  *frame;

    size= NJS_NATIVE_FRAME_SIZE
           + method->data.string_size
           + nargs * sizeof(njs_value_t);

    if (nxt_fast_path(size <= vm->frame->size)) {
        frame = (njs_native_frame_t *) vm->frame->last;
        frame->size = vm->frame->size - size;
        frame->start = 0;

    } else {
        spare_size = size + NJS_FRAME_SPARE_SIZE;
        spare_size = nxt_align_size(spare_size, NJS_FRAME_SPARE_SIZE);

        frame = nxt_mem_cache_align(vm->mem_cache_pool, sizeof(njs_value_t),
                                    spare_size);
        if (nxt_slow_path(frame == NULL)) {
            return NULL;
        }

        frame->size = spare_size - size;
        frame->start = 1;
    }

    frame->ctor = ctor;
    frame->reentrant = 0;
    frame->trap_reference = 0;

    frame->u.exception.next = NULL;
    frame->u.exception.catch = NULL;

    frame->last = (u_char *) frame + size;
    frame->previous = vm->frame;
    vm->frame = frame;

    this = (njs_value_t *)
               ((u_char *) njs_native_data(frame) + method->data.string_size);
    frame->arguments = this + 1;
    vm->scopes[NJS_SCOPE_CALLEE_ARGUMENTS] = frame->arguments;

    return this;
}


njs_ret_t
njs_vmcode_trap(njs_vm_t *vm, nxt_uint_t trap, njs_value_t *value1,
    njs_value_t *value2)
{
    size_t              size, spare_size;
    njs_value_t         *values;
    njs_native_frame_t  *frame;

    size = NJS_NATIVE_FRAME_SIZE + 3 * sizeof(njs_value_t);

    if (nxt_fast_path(size <= vm->frame->size)) {
        frame = (njs_native_frame_t *) vm->frame->last;
        frame->size = vm->frame->size - size;
        frame->start = 0;

    } else {
        spare_size = size + NJS_FRAME_SPARE_SIZE;
        spare_size = nxt_align_size(spare_size, NJS_FRAME_SPARE_SIZE);

        frame = nxt_mem_cache_align(vm->mem_cache_pool, sizeof(njs_value_t),
                                    spare_size);
        if (nxt_slow_path(frame == NULL)) {
            return NXT_ERROR;
        }

        frame->size = spare_size - size;
        frame->start = 1;
    }

    frame->ctor = 0;
    frame->reentrant = 0;

    values = njs_native_data(frame);
    njs_set_invalid(&values[0]);
    values[2] = *value2;

    frame->trap_reference = njs_vm_traps[trap].reference_value;

    if (njs_vm_traps[trap].reference_value) {
        values[1].data.u.value = value1;

    } else {
        values[1] = *value1;
    }

    frame->u.exception.catch = NULL;
    frame->u.restart = vm->current;
    vm->current = (u_char *) njs_vm_traps[trap].code;

    frame->last = (u_char *) frame + size;
    frame->previous = vm->frame;
    vm->frame = frame;

    return NXT_OK;
}


nxt_noinline njs_ret_t
njs_function_apply(njs_vm_t *vm, njs_value_t *name, njs_param_t *param)
{
    njs_ret_t  ret;

    if (njs_is_native(name)) {
        return name->data.u.method(vm, param);

    } else if (njs_is_function(name)) {

        if (name->data.u.function->native) {
            return name->data.u.function->code.native(vm, param);
        }

        ret = njs_vmcode_function_frame(vm, name, param, 0);

        if (nxt_fast_path(ret == NXT_OK)) {
            vm->retval = njs_value_void;

            return njs_function_call(vm, name->data.u.function, param->retval);
        }
    }

    return NXT_ERROR;
}


nxt_noinline njs_ret_t
njs_vmcode_function_frame(njs_vm_t *vm, njs_value_t *name, njs_param_t *param,
    nxt_bool_t ctor)
{
    size_t          size, spare_size;
    uintptr_t       nargs, n;
    njs_value_t     *args, *arguments;
    njs_frame_t     *frame;
    njs_function_t  *func;

    func = name->data.u.function;
    nargs = nxt_max(param->nargs, func->code.script->nargs);

    size = NJS_FRAME_SIZE
           + nargs * sizeof(njs_value_t)
           + func->code.script->local_size;
    spare_size = size + func->code.script->spare_size;

    if (spare_size <= vm->frame->size) {
        frame = (njs_frame_t *) vm->frame->last;
        frame->native.size = vm->frame->size - size;
        frame->native.start = 0;

    } else {
        if (func->code.script->spare_size != 0) {
            spare_size = size + NJS_FRAME_SPARE_SIZE;
            spare_size = nxt_align_size(spare_size, NJS_FRAME_SPARE_SIZE);
        }

        frame = nxt_mem_cache_align(vm->mem_cache_pool, sizeof(njs_value_t),
                                    spare_size);
        if (nxt_slow_path(frame == NULL)) {
            return NXT_ERROR;
        }

        frame->native.size = spare_size - size;
        frame->native.start = 1;
    }

    frame->native.ctor = ctor;
    frame->native.reentrant = 0;
    frame->native.trap_reference = 0;

    frame->native.u.exception.next = NULL;
    frame->native.u.exception.catch = NULL;

    frame->native.last = (u_char *) frame + size;
    frame->native.previous = vm->frame;
    vm->frame = &frame->native;

    args = (njs_value_t *) ((u_char *) frame + NJS_FRAME_SIZE);
    frame->native.arguments = args + func->args_offset;
    vm->scopes[NJS_SCOPE_CALLEE_ARGUMENTS] = frame->native.arguments;

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

    memcpy(frame->local, func->code.script->local_scope,
           func->code.script->local_size);

    vm->retval = *name;

    return NXT_OK;
}


nxt_noinline njs_ret_t
njs_function_call(njs_vm_t *vm, njs_function_t *func, njs_index_t retval)
{
    njs_frame_t  *frame;

    frame = (njs_frame_t *) vm->frame;

    frame->retval = retval;

    frame->return_address = vm->current;

    vm->current = func->code.script->u.code;

    frame->prev_arguments = vm->scopes[NJS_SCOPE_ARGUMENTS];
    vm->scopes[NJS_SCOPE_ARGUMENTS] = frame->native.arguments
                                      - func->args_offset;
#if (NXT_DEBUG)
    vm->scopes[NJS_SCOPE_CALLEE_ARGUMENTS] = NULL;
#endif
    frame->prev_local = vm->scopes[NJS_SCOPE_LOCAL];
    vm->scopes[NJS_SCOPE_LOCAL] = frame->local;

    return NJS_PASS;
}


static const njs_object_prop_t  njs_function_function_properties[] =
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
    { njs_getter(njs_object_prototype_create_prototype),
      njs_string("prototype"),
      NJS_NATIVE_GETTER, 0, 0, 0, },
};


nxt_int_t
njs_function_function_hash(njs_vm_t *vm, nxt_lvlhsh_t *hash)
{
    return njs_object_hash_create(vm, hash, njs_function_function_properties,
                                  nxt_nitems(njs_function_function_properties));
}


static njs_ret_t
njs_function_prototype_call(njs_vm_t *vm, njs_param_t *param)
{
    uintptr_t          nargs;
    njs_ret_t          ret;
    njs_param_t        p;
    njs_value_t        *func;
    njs_vmcode_call_t  *call;

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

    if (func->data.u.function->native) {

        if (nargs != 0) {
            p.nargs = nargs - 1;
            p.retval = param->retval;

            return func->data.u.function->code.native(vm, &p);
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

    ret = njs_vmcode_function_frame(vm, func, &p, 0);

    if (nxt_slow_path(ret != NXT_OK)) {
        return NXT_ERROR;
    }

    /* Skip the "call" method frame. */
    vm->frame->previous = vm->frame->previous->previous;

    call = (njs_vmcode_call_t *) vm->current;

    return njs_function_call(vm, func->data.u.function, call->retval);
}


static njs_ret_t
njs_function_prototype_apply(njs_vm_t *vm, njs_param_t *param)
{
    uintptr_t          nargs;
    njs_ret_t          ret;
    njs_param_t        p;
    njs_array_t        *array;
    njs_value_t        *func, *args;
    njs_vmcode_call_t  *code;

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

    if (func->data.u.function->native) {
        p.retval = param->retval;

        if (nargs < 2) {
            if (nargs != 0) {
                p.args = &args[1];
                p.nargs = nargs - 1;

            } else {
                goto type_error;
            }
        }

        return func->data.u.function->code.native(vm, &p);
    }

    if (nargs < 2) {
        if (nargs != 0) {
            p.nargs = 0;

        } else {
            p.object = (njs_value_t *) &njs_value_void;
        }
    }

    ret = njs_vmcode_function_frame(vm, func, &p, 0);

    if (nxt_fast_path(ret == NXT_OK)) {
        /* Skip the "apply" method frame. */
        vm->frame->previous = vm->frame->previous->previous;

        code = (njs_vmcode_call_t *) vm->current;

        return njs_function_call(vm, func->data.u.function, code->retval);
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

    bound = nxt_mem_cache_align(vm->mem_cache_pool, sizeof(njs_value_t),
                                sizeof(njs_function_t));

    if (nxt_fast_path(bound != NULL)) {
        nxt_lvlhsh_init(&bound->object.hash);
        nxt_lvlhsh_init(&bound->object.shared_hash);
        bound->object.__proto__ = &vm->prototypes[NJS_PROTOTYPE_FUNCTION];
        bound->args_offset = 1;

        func = param->object;
        bound->code.script = func->data.u.function->code.script;

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


nxt_int_t
njs_function_prototype_hash(njs_vm_t *vm, nxt_lvlhsh_t *hash)
{
    return njs_object_hash_create(vm, hash, njs_function_prototype_properties,
                           nxt_nitems(njs_function_prototype_properties));
}
