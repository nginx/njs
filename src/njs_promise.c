
/*
 * Copyright (C) Alexander Borisov
 * Copyright (C) Nginx, Inc.
 */

#include <njs_main.h>


typedef enum {
    NJS_PROMISE_HANDLE = 0,
    NJS_PROMISE_REJECT
} njs_promise_rejection_type_t;

typedef enum {
    NJS_PROMISE_ALL = 0,
    NJS_PROMISE_ALL_SETTLED,
    NJS_PROMISE_ANY
} njs_promise_function_type_t;

typedef struct {
    njs_promise_capability_t  *capability;
    njs_promise_type_t        type;
    njs_queue_link_t          link;
    njs_value_t               handler;
} njs_promise_reaction_t;

typedef struct {
    njs_value_t               promise;
    njs_value_t               finally;
    njs_value_t               constructor;
    njs_bool_t                resolved;
    njs_bool_t                *resolved_ref;
    njs_promise_capability_t  *capability;
    njs_function_native_t     handler;
} njs_promise_context_t;

typedef struct {
    njs_bool_t                already_called;
    uint32_t                  index;
    uint32_t                  *remaining_elements;
    njs_array_t               *values;
    njs_promise_capability_t  *capability;
} njs_promise_all_context_t;

typedef struct {
    njs_iterator_args_t       args;
    uint32_t                  *remaining;
    njs_value_t               *constructor;
    njs_function_t            *function;
    njs_promise_capability_t  *capability;
} njs_promise_iterator_args_t;


static njs_promise_t *njs_promise_constructor_call(njs_vm_t *vm,
    njs_function_t *function);
static njs_int_t njs_promise_create_resolving_functions(njs_vm_t *vm,
    njs_promise_t *promise, njs_value_t *dst);
static njs_int_t njs_promise_value_constructor(njs_vm_t *vm, njs_value_t *value,
    njs_value_t *dst);
static njs_int_t njs_promise_capability_executor(njs_vm_t *vm,
    njs_value_t *args, njs_uint_t nargs, njs_index_t retval);
static njs_int_t njs_promise_host_rejection_tracker(njs_vm_t *vm,
    njs_promise_t *promise, njs_promise_rejection_type_t operation);
static njs_int_t njs_promise_resolve_function(njs_vm_t *vm, njs_value_t *args,
    njs_uint_t nargs, njs_index_t retval);
static njs_int_t njs_promise_reject_function(njs_vm_t *vm, njs_value_t *args,
    njs_uint_t nargs, njs_index_t retval);
static njs_int_t njs_promise_then_finally_function(njs_vm_t *vm,
    njs_value_t *args, njs_uint_t nargs, njs_index_t unused);
static njs_int_t njs_promise_then_finally_return(njs_vm_t *vm,
    njs_value_t *args, njs_uint_t nargs, njs_index_t unused);
static njs_int_t njs_promise_catch_finally_return(njs_vm_t *vm,
    njs_value_t *args, njs_uint_t nargs, njs_index_t unused);
static njs_int_t njs_promise_reaction_job(njs_vm_t *vm, njs_value_t *args,
    njs_uint_t nargs, njs_index_t unused);
static njs_int_t njs_promise_resolve_thenable_job(njs_vm_t *vm,
    njs_value_t *args, njs_uint_t nargs, njs_index_t unused);
static njs_int_t njs_promise_perform_all(njs_vm_t *vm, njs_value_t *iterator,
    njs_promise_iterator_args_t *pargs, njs_iterator_handler_t handler,
    njs_value_t *retval);
static njs_int_t njs_promise_perform_all_handler(njs_vm_t *vm,
    njs_iterator_args_t *args, njs_value_t *value, int64_t index);
static njs_int_t njs_promise_all_resolve_element_functions(njs_vm_t *vm,
    njs_value_t *args, njs_uint_t nargs, njs_index_t unused);
static njs_int_t njs_promise_perform_all_settled_handler(njs_vm_t *vm,
    njs_iterator_args_t *args, njs_value_t *value, int64_t index);
static njs_int_t njs_promise_all_settled_element_functions(njs_vm_t *vm,
    njs_value_t *args, njs_uint_t nargs, njs_index_t rejected);
static njs_int_t njs_promise_perform_any_handler(njs_vm_t *vm,
    njs_iterator_args_t *args, njs_value_t *value, int64_t index);
static njs_int_t njs_promise_any_reject_element_functions(njs_vm_t *vm,
    njs_value_t *args, njs_uint_t nargs, njs_index_t unused);
static njs_int_t njs_promise_perform_race_handler(njs_vm_t *vm,
    njs_iterator_args_t *args, njs_value_t *value, int64_t index);


static const njs_value_t  string_resolve = njs_string("resolve");
static const njs_value_t  string_any_rejected =
                                 njs_long_string("All promises were rejected");


static njs_promise_t *
njs_promise_alloc(njs_vm_t *vm)
{
    njs_promise_t       *promise;
    njs_promise_data_t  *data;

    promise = njs_mp_alloc(vm->mem_pool, sizeof(njs_promise_t)
                           + sizeof(njs_promise_data_t));
    if (njs_slow_path(promise == NULL)) {
        goto memory_error;
    }

    njs_lvlhsh_init(&promise->object.hash);
    njs_lvlhsh_init(&promise->object.shared_hash);
    promise->object.type = NJS_PROMISE;
    promise->object.shared = 0;
    promise->object.extensible = 1;
    promise->object.error_data = 0;
    promise->object.fast_array = 0;
    promise->object.__proto__ = &vm->prototypes[NJS_OBJ_TYPE_PROMISE].object;
    promise->object.slots = NULL;

    data = (njs_promise_data_t *) ((uint8_t *) promise + sizeof(njs_promise_t));

    data->state = NJS_PROMISE_PENDING;
    data->is_handled = 0;

    njs_queue_init(&data->fulfill_queue);
    njs_queue_init(&data->reject_queue);

    njs_set_data(&promise->value, data, 0);

    return promise;

memory_error:

    njs_memory_error(vm);

    return NULL;
}


njs_int_t
njs_promise_constructor(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t unused)
{
    njs_promise_t   *promise;
    njs_function_t  *function;

    if (njs_slow_path(!vm->top_frame->ctor)) {
        njs_type_error(vm, "the Promise constructor must be called with new");
        return NJS_ERROR;
    }

    if (njs_slow_path(!njs_is_function(njs_arg(args, nargs, 1)))) {
        njs_type_error(vm, "unexpected arguments");
        return NJS_ERROR;
    }

    function = njs_function(njs_argument(args, 1));

    promise = njs_promise_constructor_call(vm, function);
    if (njs_slow_path(promise == NULL)) {
        return NJS_ERROR;
    }

    njs_set_promise(&vm->retval, promise);

    return NJS_OK;
}


njs_int_t
njs_vm_promise_create(njs_vm_t *vm, njs_value_t *retval, njs_value_t *callbacks)
{
    njs_int_t      ret;
    njs_promise_t  *promise;

    promise = njs_promise_alloc(vm);
    if (njs_slow_path(promise == NULL)) {
        return NJS_ERROR;
    }

    ret = njs_promise_create_resolving_functions(vm, promise, callbacks);
    if (njs_slow_path(ret != NJS_OK)) {
        return NJS_ERROR;
    }

    njs_set_promise(retval, promise);

    return NJS_OK;
}


static njs_promise_t *
njs_promise_constructor_call(njs_vm_t *vm, njs_function_t *function)
{
    njs_int_t      ret;
    njs_value_t    retval, arguments[2];
    njs_promise_t  *promise;

    promise = njs_promise_alloc(vm);
    if (njs_slow_path(promise == NULL)) {
        return NULL;
    }

    ret = njs_promise_create_resolving_functions(vm, promise, arguments);
    if (njs_slow_path(ret != NJS_OK)) {
        return NULL;
    }

    ret = njs_function_call(vm, function, &njs_value_undefined, arguments, 2,
                            &retval);
    if (njs_slow_path(ret != NJS_OK)) {
        if (njs_slow_path(njs_is_memory_error(vm, &vm->retval))) {
            return NULL;
        }

        ret = njs_function_call(vm, njs_function(&arguments[1]),
                                &njs_value_undefined, &vm->retval, 1, &retval);
        if (njs_slow_path(ret != NJS_OK)) {
            return NULL;
        }
    }

    return promise;
}


njs_function_t *
njs_promise_create_function(njs_vm_t *vm, size_t context_size)
{
    njs_function_t         *function;
    njs_promise_context_t  *context;

    function = njs_mp_zalloc(vm->mem_pool, sizeof(njs_function_t));
    if (njs_slow_path(function == NULL)) {
        goto memory_error;
    }

    if (context_size > 0) {
        context = njs_mp_zalloc(vm->mem_pool, context_size);
        if (njs_slow_path(context == NULL)) {
            njs_mp_free(vm->mem_pool, function);
            goto memory_error;
        }

    } else {
        context = NULL;
    }

    function->object.__proto__ = &vm->prototypes[NJS_OBJ_TYPE_FUNCTION].object;
    function->object.shared_hash = vm->shared->arrow_instance_hash;
    function->object.type = NJS_FUNCTION;
    function->object.extensible = 1;
    function->args_offset = 1;
    function->native = 1;
    function->context = context;

    return function;

memory_error:

    njs_memory_error(vm);

    return NULL;
}


static njs_int_t
njs_promise_create_resolving_functions(njs_vm_t *vm, njs_promise_t *promise,
    njs_value_t *dst)
{
    unsigned               i;
    njs_function_t         *function;
    njs_promise_context_t  *context, *resolve_context;

    i = 0;

    /* Some compilers give at error an uninitialized context if using for. */
    do {
        function = njs_promise_create_function(vm,
                                               sizeof(njs_promise_context_t));
        if (njs_slow_path(function == NULL)) {
            return NJS_ERROR;
        }

        function->args_count = 1;

        context = function->context;
        context->resolved_ref = &context->resolved;

        njs_set_promise(&context->promise, promise);
        njs_set_function(&dst[i], function);

    } while (++i < 2);

    njs_function(&dst[0])->u.native = njs_promise_resolve_function;
    njs_function(&dst[1])->u.native = njs_promise_reject_function;

    resolve_context = njs_function(&dst[0])->context;
    resolve_context->resolved_ref = &context->resolved;

    return NJS_OK;
}


njs_promise_capability_t *
njs_promise_new_capability(njs_vm_t *vm, njs_value_t *constructor)
{
    njs_int_t                 ret;
    njs_value_t               argument, this;
    njs_object_t              *object;
    njs_function_t            *function;
    njs_promise_context_t     *context;
    njs_promise_capability_t  *capability;

    object = NULL;
    function = NULL;

    ret = njs_promise_value_constructor(vm, constructor, constructor);
    if (njs_slow_path(ret != NJS_OK)) {
        return NULL;
    }

    capability = njs_mp_zalloc(vm->mem_pool, sizeof(njs_promise_capability_t));
    if (njs_slow_path(capability == NULL)) {
        njs_memory_error(vm);
        return NULL;
    }

    function = njs_promise_create_function(vm, sizeof(njs_promise_context_t));
    if (njs_slow_path(function == NULL)) {
        return NULL;
    }

    njs_set_undefined(&capability->resolve);
    njs_set_undefined(&capability->reject);

    function->u.native = njs_promise_capability_executor;
    function->args_count = 2;

    context = function->context;
    context->capability = capability;

    njs_set_function(&argument, function);

    object = njs_function_new_object(vm, constructor);
    if (njs_slow_path(object == NULL)) {
        return NULL;
    }

    njs_set_object(&this, object);

    ret = njs_function_call2(vm, njs_function(constructor), &this,
                             &argument, 1, &capability->promise, 1);
    if (njs_slow_path(ret != NJS_OK)) {
        return NULL;
    }

    if (njs_slow_path(!njs_is_function(&capability->resolve))) {
        njs_type_error(vm, "capability resolve slot is not callable");
        return NULL;
    }

    if (njs_slow_path(!njs_is_function(&capability->reject))) {
        njs_type_error(vm, "capability reject slot is not callable");
        return NULL;
    }

    return capability;
}


static njs_int_t
njs_promise_value_constructor(njs_vm_t *vm, njs_value_t *value,
    njs_value_t *dst)
{
    njs_int_t  ret;

    static const njs_value_t  string_constructor = njs_string("constructor");

    if (njs_is_function(value)) {
        *dst = *value;
        return NJS_OK;
    }

    ret = njs_value_property(vm, value, njs_value_arg(&string_constructor),
                             dst);
    if (njs_slow_path(ret == NJS_ERROR)) {
        return ret;
    }

    if (!njs_is_function(dst)) {
        njs_type_error(vm, "the object does not contain a constructor");
        return NJS_ERROR;
    }

    return NJS_OK;
}


static njs_int_t
njs_promise_capability_executor(njs_vm_t *vm, njs_value_t *args,
    njs_uint_t nargs, njs_index_t unused)
{
    njs_promise_context_t     *context;
    njs_promise_capability_t  *capability;

    context = vm->top_frame->function->context;
    capability = context->capability;

    if (njs_slow_path(capability == NULL)) {
        njs_type_error(vm, "failed to get function capability");
        return NJS_ERROR;
    }

    if (!njs_is_undefined(&capability->resolve)) {
        njs_type_error(vm, "capability resolve slot is not undefined");
        return NJS_ERROR;
    }

    if (!njs_is_undefined(&capability->reject)) {
        njs_type_error(vm, "capability reject slot is not undefined");
        return NJS_ERROR;
    }

    capability->resolve = *njs_arg(args, nargs, 1);
    capability->reject = *njs_arg(args, nargs, 2);

    njs_vm_retval_set(vm, &njs_value_undefined);

    return NJS_OK;
}


njs_inline njs_int_t
njs_promise_add_event(njs_vm_t *vm, njs_function_t *function, njs_value_t *args,
    njs_uint_t nargs)
{
    njs_event_t  *event;

    event = njs_mp_zalloc(vm->mem_pool, sizeof(njs_event_t));
    if (njs_slow_path(event == NULL)) {
        njs_memory_error(vm);
        return NJS_ERROR;
    }

    event->function = function;
    event->once = 1;

    if (nargs != 0) {
        event->args = njs_mp_alloc(vm->mem_pool, sizeof(njs_value_t) * nargs);
        if (njs_slow_path(event->args == NULL)) {
            njs_memory_error(vm);
            return NJS_ERROR;
        }

        memcpy(event->args, args, sizeof(njs_value_t) * nargs);

        event->nargs = nargs;
    }

    njs_queue_insert_tail(&vm->promise_events, &event->link);

    return NJS_OK;
}


njs_inline njs_value_t *
njs_promise_trigger_reactions(njs_vm_t *vm, njs_value_t *value,
    njs_queue_t *queue)
{
    njs_int_t               ret;
    njs_value_t             arguments[2];
    njs_function_t          *function;
    njs_queue_link_t        *link;
    njs_promise_reaction_t  *reaction;

    for (link = njs_queue_first(queue);
         link != njs_queue_tail(queue);
         link = njs_queue_next(link))
    {
        reaction = njs_queue_link_data(link, njs_promise_reaction_t, link);

        function = njs_promise_create_function(vm,
                                               sizeof(njs_promise_context_t));
        function->u.native = njs_promise_reaction_job;

        njs_set_data(&arguments[0], reaction, 0);
        arguments[1] = *value;

        ret = njs_promise_add_event(vm, function, arguments, 2);
        if (njs_slow_path(ret != NJS_OK)) {
            return njs_value_arg(&njs_value_null);
        }
    }

    return njs_value_arg(&njs_value_undefined);
}


njs_inline njs_value_t *
njs_promise_fulfill(njs_vm_t *vm, njs_promise_t *promise, njs_value_t *value)
{
    njs_queue_t         queue;
    njs_promise_data_t  *data;

    data = njs_data(&promise->value);

    data->result = *value;
    data->state = NJS_PROMISE_FULFILL;

    if (njs_queue_is_empty(&data->fulfill_queue)) {
        return njs_value_arg(&njs_value_undefined);

    } else {
        queue = data->fulfill_queue;

        queue.head.prev->next = &queue.head;
        queue.head.next->prev = &queue.head;
    }

    njs_queue_init(&data->fulfill_queue);
    njs_queue_init(&data->reject_queue);

    return njs_promise_trigger_reactions(vm, value, &queue);
}


njs_inline njs_value_t *
njs_promise_reject(njs_vm_t *vm, njs_promise_t *promise, njs_value_t *reason)
{
    njs_int_t           ret;
    njs_queue_t         queue;
    njs_promise_data_t  *data;

    data = njs_data(&promise->value);

    data->result = *reason;
    data->state = NJS_PROMISE_REJECTED;

    if (!data->is_handled) {
        ret = njs_promise_host_rejection_tracker(vm, promise,
                                                 NJS_PROMISE_REJECT);
        if (njs_slow_path(ret != NJS_OK)) {
            return njs_value_arg(&njs_value_null);
        }
    }

    if (njs_queue_is_empty(&data->reject_queue)) {
        return njs_value_arg(&njs_value_undefined);

    } else {
        queue = data->reject_queue;

        queue.head.prev->next = &queue.head;
        queue.head.next->prev = &queue.head;
    }

    njs_queue_init(&data->fulfill_queue);
    njs_queue_init(&data->reject_queue);

    return njs_promise_trigger_reactions(vm, reason, &queue);
}


static njs_int_t
njs_promise_host_rejection_tracker(njs_vm_t *vm, njs_promise_t *promise,
    njs_promise_rejection_type_t operation)
{
    uint32_t            i, length;
    njs_value_t         *value;
    njs_promise_data_t  *data;

    if (vm->options.unhandled_rejection
        == NJS_VM_OPT_UNHANDLED_REJECTION_IGNORE)
    {
        return NJS_OK;
    }

    if (vm->promise_reason == NULL) {
        vm->promise_reason = njs_array_alloc(vm, 1, 0, NJS_ARRAY_SPARE);
        if (njs_slow_path(vm->promise_reason == NULL)) {
            return NJS_ERROR;
        }
    }

    data = njs_data(&promise->value);

    if (operation == NJS_PROMISE_REJECT) {
        if (vm->promise_reason != NULL) {
            return njs_array_add(vm, vm->promise_reason, &data->result);
        }

    } else {
        value = vm->promise_reason->start;
        length = vm->promise_reason->length;

        for (i = 0; i < length; i++) {
            if (njs_values_same(&value[i], &data->result)) {
                length--;

                if (i < length) {
                    memmove(&value[i], &value[i + 1],
                            sizeof(njs_value_t) * (length - i));
                }

                break;
            }
        }

        vm->promise_reason->length = length;
    }

    return NJS_OK;
}


static njs_int_t
njs_promise_invoke_then(njs_vm_t *vm, njs_value_t *promise, njs_value_t *args,
    njs_int_t nargs)
{
    njs_int_t    ret;
    njs_value_t  function;

    static const njs_value_t  string_then = njs_string("then");

    ret = njs_value_property(vm, promise, njs_value_arg(&string_then),
                             &function);
    if (njs_slow_path(ret != NJS_OK)) {
        if (ret == NJS_DECLINED) {
            goto failed;
        }

        return NJS_ERROR;
    }

    if (njs_fast_path(njs_is_function(&function))) {
        return njs_function_call(vm, njs_function(&function), promise, args,
                                 nargs, &vm->retval);
    }

failed:

    njs_type_error(vm, "is not a function");

    return NJS_ERROR;
}


static njs_int_t
njs_promise_resolve_function(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t unused)
{
    njs_int_t              ret;
    njs_value_t            *resolution, error, then, arguments[3];
    njs_promise_t          *promise;
    njs_function_t         *function;
    njs_native_frame_t     *active_frame;
    njs_promise_context_t  *context;

    static const njs_value_t  string_then = njs_string("then");

    active_frame = vm->top_frame;
    context = active_frame->function->context;
    promise = njs_promise(&context->promise);

    if (*context->resolved_ref) {
        njs_vm_retval_set(vm, &njs_value_undefined);
        return NJS_OK;
    }

    *context->resolved_ref = 1;

    resolution = njs_arg(args, nargs, 1);

    if (njs_values_same(resolution, &context->promise)) {
        njs_error_fmt_new(vm, &error, NJS_OBJ_TYPE_TYPE_ERROR,
                          "promise self resolution");
        if (njs_slow_path(!njs_is_error(&error))) {
            return NJS_ERROR;
        }

        njs_vm_retval_set(vm, njs_promise_reject(vm, promise, &error));

        return NJS_OK;
    }

    if (!njs_is_object(resolution)) {
        goto fulfill;
    }

    ret = njs_value_property(vm, resolution, njs_value_arg(&string_then),
                             &then);
    if (njs_slow_path(ret == NJS_ERROR)) {
        if (njs_slow_path(njs_is_memory_error(vm, &vm->retval))) {
            return NJS_ERROR;
        }

        njs_vm_retval_set(vm, njs_promise_reject(vm, promise, &vm->retval));
        if (njs_slow_path(njs_vm_retval(vm)->type == NJS_NULL)) {
            return NJS_ERROR;
        }

        return NJS_OK;
    }

    if (!njs_is_function(&then)) {
        goto fulfill;
    }

    arguments[0] = context->promise;
    arguments[1] = *resolution;
    arguments[2] = then;

    function = njs_promise_create_function(vm, sizeof(njs_promise_context_t));
    if (njs_slow_path(function == NULL)) {
        return NJS_ERROR;
    }

    function->u.native = njs_promise_resolve_thenable_job;

    ret = njs_promise_add_event(vm, function, arguments, 3);
    if (njs_slow_path(ret != NJS_OK)) {
        return ret;
    }

    njs_vm_retval_set(vm, &njs_value_undefined);

    return NJS_OK;

fulfill:

    njs_vm_retval_set(vm, njs_promise_fulfill(vm, promise, resolution));
    if (njs_slow_path(njs_vm_retval(vm)->type == NJS_NULL)) {
        return NJS_ERROR;
    }

    return NJS_OK;
}


static njs_int_t
njs_promise_object_resolve(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t unused)
{
    njs_promise_t  *promise;

    if (njs_slow_path(!njs_is_object(njs_argument(args, 0)))) {
        njs_type_error(vm, "this value is not an object");
        return NJS_ERROR;
    }

    promise = njs_promise_resolve(vm, njs_argument(args, 0),
                                  njs_arg(args, nargs, 1));
    if (njs_slow_path(promise == NULL)) {
        return NJS_ERROR;
    }

    njs_set_promise(&vm->retval, promise);

    return NJS_OK;
}


njs_promise_t *
njs_promise_resolve(njs_vm_t *vm, njs_value_t *constructor, njs_value_t *x)
{
    njs_int_t                 ret;
    njs_value_t               value;
    njs_promise_capability_t  *capability;

    static const njs_value_t  string_constructor = njs_string("constructor");

    if (njs_is_promise(x)) {
        ret = njs_value_property(vm, x, njs_value_arg(&string_constructor),
                                 &value);
        if (njs_slow_path(ret == NJS_ERROR)) {
            return NULL;
        }

        if (njs_values_same(&value, constructor)) {
            return njs_promise(x);
        }
    }

    capability = njs_promise_new_capability(vm, constructor);
    if (njs_slow_path(capability == NULL)) {
        return NULL;
    }

    ret = njs_function_call(vm, njs_function(&capability->resolve),
                            &njs_value_undefined, x, 1, &value);
    if (njs_slow_path(ret != NJS_OK)) {
        return NULL;
    }

    return njs_promise(&capability->promise);
}


static njs_int_t
njs_promise_reject_function(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t unused)
{
    njs_value_t            *value;
    njs_native_frame_t     *active_frame;
    njs_promise_context_t  *context;

    active_frame = vm->top_frame;
    context = active_frame->function->context;

    if (*context->resolved_ref) {
        njs_vm_retval_set(vm, &njs_value_undefined);
        return NJS_OK;
    }

    *context->resolved_ref = 1;

    value = njs_promise_reject(vm, njs_promise(&context->promise),
                               njs_arg(args, nargs, 1));
    if (njs_slow_path(value->type == NJS_NULL)) {
        return NJS_ERROR;
    }

    njs_vm_retval_set(vm, value);

    return NJS_OK;
}


static njs_int_t
njs_promise_object_reject(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t unused)
{
    njs_int_t                 ret;
    njs_value_t               value;
    njs_promise_capability_t  *capability;

    if (njs_slow_path(!njs_is_object(njs_argument(args, 0)))) {
        njs_type_error(vm, "this value is not an object");
        return NJS_ERROR;
    }

    capability = njs_promise_new_capability(vm, njs_argument(args, 0));
    if (njs_slow_path(capability == NULL)) {
        return NJS_ERROR;
    }

    ret = njs_function_call(vm, njs_function(&capability->reject),
                            &njs_value_undefined, njs_arg(args, nargs, 1),
                            1, &value);
    if (njs_slow_path(ret != NJS_OK)) {
        return ret;
    }

    njs_vm_retval_set(vm, &capability->promise);

    return NJS_OK;
}


static njs_int_t
njs_promise_prototype_then(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t unused)
{
    njs_int_t                 ret;
    njs_value_t               *promise, *fulfilled, *rejected, constructor;
    njs_function_t            *function;
    njs_promise_capability_t  *capability;

    promise = njs_argument(args, 0);

    if (njs_slow_path(!njs_is_promise(promise))) {
        goto failed;
    }

    function = njs_promise_create_function(vm, sizeof(njs_promise_context_t));
    function->u.native = njs_promise_constructor;

    njs_set_function(&constructor, function);

    ret = njs_value_species_constructor(vm, promise, &constructor,
                                        &constructor);
    if (njs_slow_path(ret != NJS_OK)) {
        return ret;
    }

    capability = njs_promise_new_capability(vm, &constructor);
    if (njs_slow_path(capability == NULL)) {
        return NJS_ERROR;
    }

    fulfilled = njs_arg(args, nargs, 1);
    rejected = njs_arg(args, nargs, 2);

    return njs_promise_perform_then(vm, promise, fulfilled, rejected,
                                    capability);

failed:

    njs_type_error(vm, "required a promise object");

    return NJS_ERROR;
}


njs_int_t
njs_promise_perform_then(njs_vm_t *vm, njs_value_t *value,
    njs_value_t *fulfilled, njs_value_t *rejected,
    njs_promise_capability_t *capability)
{
    njs_int_t               ret;
    njs_value_t             arguments[2];
    njs_promise_t           *promise;
    njs_function_t          *function;
    njs_promise_data_t      *data;
    njs_promise_reaction_t  *fulfilled_reaction, *rejected_reaction;

    njs_assert(njs_is_promise(value));

    if (!njs_is_function(fulfilled)) {
        fulfilled = njs_value_arg(&njs_value_undefined);
    }

    if (!njs_is_function(rejected)) {
        rejected = njs_value_arg(&njs_value_undefined);
    }

    promise = njs_promise(value);
    data = njs_data(&promise->value);

    fulfilled_reaction = njs_mp_alloc(vm->mem_pool,
                                      sizeof(njs_promise_reaction_t));
    if (njs_slow_path(fulfilled_reaction == NULL)) {
        njs_memory_error(vm);
        return NJS_ERROR;
    }

    fulfilled_reaction->capability = capability;
    fulfilled_reaction->handler = *fulfilled;
    fulfilled_reaction->type = NJS_PROMISE_FULFILL;

    rejected_reaction = njs_mp_alloc(vm->mem_pool,
                                     sizeof(njs_promise_reaction_t));
    if (njs_slow_path(rejected_reaction == NULL)) {
        njs_memory_error(vm);
        return NJS_ERROR;
    }

    rejected_reaction->capability = capability;
    rejected_reaction->handler = *rejected;
    rejected_reaction->type = NJS_PROMISE_REJECTED;

    if (data->state == NJS_PROMISE_PENDING) {
        njs_queue_insert_tail(&data->fulfill_queue, &fulfilled_reaction->link);
        njs_queue_insert_tail(&data->reject_queue, &rejected_reaction->link);

    } else {
        function = njs_promise_create_function(vm,
                                               sizeof(njs_promise_context_t));
        function->u.native = njs_promise_reaction_job;

        if (data->state == NJS_PROMISE_REJECTED) {
            njs_set_data(&arguments[0], rejected_reaction, 0);

            ret = njs_promise_host_rejection_tracker(vm, promise,
                                                     NJS_PROMISE_HANDLE);
            if (njs_slow_path(ret != NJS_OK)) {
                return ret;
            }

        } else {
            njs_set_data(&arguments[0], fulfilled_reaction, 0);
        }

        arguments[1] = data->result;

        ret = njs_promise_add_event(vm, function, arguments, 2);
        if (njs_slow_path(ret != NJS_OK)) {
            return ret;
        }
    }

    data->is_handled = 1;

    if (capability == NULL) {
        njs_vm_retval_set(vm, &njs_value_undefined);

    } else {
        njs_vm_retval_set(vm, &capability->promise);
    }

    return NJS_OK;
}


static njs_int_t
njs_promise_prototype_catch(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t unused)
{
    njs_value_t  arguments[2];

    arguments[0] = njs_value_undefined;
    arguments[1] = *njs_arg(args, nargs, 1);

    return njs_promise_invoke_then(vm, njs_argument(args,  0), arguments, 2);
}


static njs_int_t
njs_promise_prototype_finally(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t unused)
{
    njs_int_t              ret;
    njs_value_t            *promise, *finally, constructor, arguments[2];
    njs_function_t         *function;
    njs_promise_context_t  *context;

    promise = njs_argument(args, 0);

    if (njs_slow_path(!njs_is_object(promise))) {
        njs_type_error(vm, "required a object");
        return NJS_ERROR;
    }

    finally = njs_arg(args, nargs, 1);

    function = njs_promise_create_function(vm, sizeof(njs_promise_context_t));
    function->u.native = njs_promise_constructor;

    njs_set_function(&constructor, function);

    ret = njs_value_species_constructor(vm, promise, &constructor,
                                        &constructor);
    if (njs_slow_path(ret != NJS_OK)) {
        return ret;
    }

    if (!njs_is_function(finally)) {
        arguments[0] = *finally;
        arguments[1] = *finally;

        return njs_promise_invoke_then(vm, promise, arguments, 2);
    }

    function = njs_promise_create_function(vm, sizeof(njs_promise_context_t));
    if (njs_slow_path(function == NULL)) {
        return NJS_ERROR;
    }

    function->u.native = njs_promise_then_finally_function;
    function->args_count = 1;

    context = function->context;
    context->constructor = constructor;
    context->finally = *finally;
    context->handler = njs_promise_then_finally_return;

    njs_set_function(&arguments[0], function);

    function = njs_promise_create_function(vm, sizeof(njs_promise_context_t));
    if (njs_slow_path(function == NULL)) {
        njs_mp_free(vm->mem_pool, njs_function(&arguments[0]));
        return NJS_ERROR;
    }

    function->u.native = njs_promise_then_finally_function;
    function->args_count = 1;

    context = function->context;
    context->constructor = constructor;
    context->finally = *finally;
    context->handler = njs_promise_catch_finally_return;

    njs_set_function(&arguments[1], function);

    return njs_promise_invoke_then(vm, promise, arguments, 2);
}


static njs_int_t
njs_promise_then_finally_function(njs_vm_t *vm, njs_value_t *args,
    njs_uint_t nargs, njs_index_t unused)
{
    njs_int_t              ret;
    njs_value_t            value, retval, argument;
    njs_promise_t          *promise;
    njs_function_t         *function;
    njs_native_frame_t     *frame;
    njs_promise_context_t  *context;

    frame = vm->top_frame;
    context = frame->function->context;

    ret = njs_function_call(vm, njs_function(&context->finally),
                            &njs_value_undefined, args, 0, &retval);
    if (njs_slow_path(ret != NJS_OK)) {
        return ret;
    }

    promise = njs_promise_resolve(vm, &context->constructor, &retval);
    if (njs_slow_path(promise == NULL)) {
        return NJS_ERROR;
    }

    njs_set_promise(&value, promise);

    function = njs_promise_create_function(vm, sizeof(njs_value_t));
    if (njs_slow_path(function == NULL)) {
        return NJS_ERROR;
    }

    function->u.native = context->handler;

    *((njs_value_t *) function->context) = *njs_arg(args, nargs, 1);

    njs_set_function(&argument, function);

    return njs_promise_invoke_then(vm, &value, &argument, 1);
}


static njs_int_t
njs_promise_then_finally_return(njs_vm_t *vm, njs_value_t *args,
    njs_uint_t nargs, njs_index_t unused)
{
    njs_vm_retval_set(vm, vm->top_frame->function->context);
    return NJS_OK;
}


static njs_int_t
njs_promise_catch_finally_return(njs_vm_t *vm, njs_value_t *args,
    njs_uint_t nargs, njs_index_t unused)
{
    njs_vm_retval_set(vm, vm->top_frame->function->context);
    return NJS_ERROR;
}


static njs_int_t
njs_promise_reaction_job(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t unused)
{
    njs_int_t                 ret;
    njs_bool_t                is_error;
    njs_value_t               *value, *argument, retval;
    njs_promise_reaction_t    *reaction;
    njs_promise_capability_t  *capability;

    value = njs_arg(args, nargs, 1);
    argument = njs_arg(args, nargs, 2);

    reaction = njs_data(value);
    capability = reaction->capability;

    is_error = 0;

    if (njs_is_undefined(&reaction->handler)) {
        if (reaction->type == NJS_PROMISE_REJECTED) {
            is_error = 1;
        }

        retval = *argument;

    } else {
        ret = njs_function_call(vm, njs_function(&reaction->handler),
                                &njs_value_undefined, argument, 1, &retval);
        if (njs_slow_path(ret != NJS_OK)) {
            if (njs_slow_path(njs_is_memory_error(vm, &vm->retval))) {
                return NJS_ERROR;
            }

            retval = vm->retval;
            is_error = 1;
        }
    }

    if (capability == NULL) {
        njs_vm_retval_set(vm, &retval);
        return NJS_OK;
    }

    if (is_error) {
        ret = njs_function_call(vm, njs_function(&capability->reject),
                                &njs_value_undefined, &retval, 1, &vm->retval);

    } else {
        ret = njs_function_call(vm, njs_function(&capability->resolve),
                                &njs_value_undefined, &retval, 1, &vm->retval);
    }

    if (njs_slow_path(ret != NJS_OK)) {
        return NJS_ERROR;
    }

    return NJS_OK;
}


static njs_int_t
njs_promise_resolve_thenable_job(njs_vm_t *vm, njs_value_t *args,
    njs_uint_t nargs, njs_index_t unused)
{
    njs_int_t    ret;
    njs_value_t  *promise, retval, arguments[2];

    promise = njs_arg(args, nargs, 1);

    ret = njs_promise_create_resolving_functions(vm, njs_promise(promise),
                                                 arguments);
    if (njs_slow_path(ret != NJS_OK)) {
        return ret;
    }

    ret = njs_function_call(vm, njs_function(njs_arg(args, nargs, 3)),
                            njs_arg(args, nargs, 2), arguments, 2, &retval);
    if (njs_slow_path(ret != NJS_OK)) {

        if (njs_slow_path(njs_is_memory_error(vm, &vm->retval))) {
            return NJS_ERROR;
        }

        ret = njs_function_call(vm, njs_function(&arguments[1]),
                                &njs_value_undefined, &vm->retval, 1,
                                &vm->retval);
        if (njs_slow_path(ret != NJS_OK)) {
            return NJS_ERROR;
        }
    }

    return NJS_OK;
}


static njs_int_t
njs_promise_all(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t function_type)
{
    njs_int_t                    ret;
    njs_value_t                  *promise_ctor, resolve;
    njs_iterator_handler_t       handler;
    njs_promise_iterator_args_t  pargs;

    promise_ctor = njs_argument(args, 0);

    pargs.capability = njs_promise_new_capability(vm, promise_ctor);
    if (njs_slow_path(pargs.capability == NULL)) {
        return NJS_ERROR;
    }

    ret = njs_value_property(vm, promise_ctor, njs_value_arg(&string_resolve),
                             &resolve);
    if (njs_slow_path(ret == NJS_ERROR)) {
        return ret;
    }

    if (njs_slow_path(!njs_is_function(&resolve))) {
        njs_type_error(vm, "resolve is not callable");
        return NJS_ERROR;
    }

    pargs.function = njs_function(&resolve);
    pargs.constructor = promise_ctor;

    switch (function_type) {
    case NJS_PROMISE_ALL_SETTLED:
        handler = njs_promise_perform_all_settled_handler;
        break;

    case NJS_PROMISE_ANY:
        handler = njs_promise_perform_any_handler;
        break;

    default:
        handler = njs_promise_perform_all_handler;
        break;
    }

    return njs_promise_perform_all(vm, njs_arg(args, nargs, 1), &pargs,
                                   handler, &vm->retval);
}


static njs_int_t
njs_promise_perform_all(njs_vm_t *vm, njs_value_t *iterator,
    njs_promise_iterator_args_t *pargs, njs_iterator_handler_t handler,
    njs_value_t *retval)
{
    int64_t       length;
    njs_int_t     ret;
    njs_value_t   argument;
    njs_object_t  *error;

    if (njs_slow_path(!njs_is_object(pargs->constructor))) {
        njs_type_error(vm, "constructor is not object");
        return NJS_ERROR;
    }

    njs_memzero(&pargs->args, sizeof(njs_iterator_args_t));

    ret = njs_object_length(vm, iterator, &length);
    if (njs_slow_path(ret != NJS_OK)) {
        return ret;
    }

    pargs->args.data = njs_array_alloc(vm, 1, 0, NJS_ARRAY_SPARE);
    if (njs_slow_path(pargs->args.data == NULL)) {
        return NJS_ERROR;
    }

    pargs->remaining = njs_mp_alloc(vm->mem_pool, sizeof(uint32_t));
    if (njs_slow_path(pargs->remaining == NULL)) {
        njs_memory_error(vm);
        return NJS_ERROR;
    }

    (*pargs->remaining) = 1;

    pargs->args.value = iterator;
    pargs->args.to = length;

    ret = njs_object_iterate(vm, &pargs->args, handler);
    if (njs_slow_path(ret == NJS_ERROR)) {
        return ret;
    }

    if (--(*pargs->remaining) == 0) {
        njs_mp_free(vm->mem_pool, pargs->remaining);

        njs_set_array(&argument, pargs->args.data);

        if (handler == njs_promise_perform_any_handler) {
            error = njs_error_alloc(vm, NJS_OBJ_TYPE_AGGREGATE_ERROR,
                                    NULL, &string_any_rejected, &argument);
            if (njs_slow_path(error == NULL)) {
                return NJS_ERROR;
            }

            njs_set_object(&argument, error);
        }

        ret = njs_function_call(vm, njs_function(&pargs->capability->resolve),
                                &njs_value_undefined, &argument, 1, retval);
        if (njs_slow_path(ret == NJS_ERROR)) {
            return ret;
        }
    }

    *retval = pargs->capability->promise;

    return NJS_OK;
}


static njs_int_t
njs_promise_perform_all_handler(njs_vm_t *vm, njs_iterator_args_t *args,
    njs_value_t *value, int64_t index)
{
    njs_int_t                    ret;
    njs_array_t                  *array;
    njs_value_t                  arguments[2], next, arr_value;
    njs_function_t               *on_fulfilled;
    njs_promise_capability_t     *capability;
    njs_promise_all_context_t    *context;
    njs_promise_iterator_args_t  *pargs;

    if (!njs_is_valid(value)) {
        value = njs_value_arg(&njs_value_undefined);
    }

    pargs = (njs_promise_iterator_args_t *) args;

    capability = pargs->capability;

    array = args->data;
    njs_set_array(&arr_value, array);

    ret = njs_value_property_i64_set(vm, &arr_value, index,
                                     njs_value_arg(&njs_value_undefined));
    if (njs_slow_path(ret == NJS_ERROR)) {
        return ret;
    }

    ret = njs_function_call(vm, pargs->function, pargs->constructor, value,
                            1, &next);
    if (njs_slow_path(ret == NJS_ERROR)) {
        return ret;
    }

    on_fulfilled = njs_promise_create_function(vm,
                                            sizeof(njs_promise_all_context_t));
    if (njs_slow_path(on_fulfilled == NULL)) {
        return NJS_ERROR;
    }

    on_fulfilled->u.native = njs_promise_all_resolve_element_functions;
    on_fulfilled->args_count = 1;

    context = on_fulfilled->context;

    context->already_called = 0;
    context->index = (uint32_t) index;
    context->values = pargs->args.data;
    context->capability = capability;
    context->remaining_elements = pargs->remaining;

    (*pargs->remaining)++;

    njs_set_function(&arguments[0], on_fulfilled);
    arguments[1] = capability->reject;

    ret = njs_promise_invoke_then(vm, &next, arguments, 2);
    if (njs_slow_path(ret == NJS_ERROR)) {
        return ret;
    }

    return NJS_OK;
}


static njs_int_t
njs_promise_all_resolve_element_functions(njs_vm_t *vm, njs_value_t *args,
    njs_uint_t nargs, njs_index_t unused)
{
    njs_int_t                  ret;
    njs_value_t                arr_value;
    njs_promise_all_context_t  *context;

    context = vm->top_frame->function->context;

    if (context->already_called) {
        njs_vm_retval_set(vm, &njs_value_undefined);
        return NJS_OK;
    }

    context->already_called = 1;

    njs_set_array(&arr_value, context->values);

    ret = njs_value_property_i64_set(vm, &arr_value, context->index,
                                     njs_arg(args, nargs, 1));
    if (njs_slow_path(ret == NJS_ERROR)) {
        return ret;
    }

    if (--(*context->remaining_elements) == 0) {
        njs_mp_free(vm->mem_pool, context->remaining_elements);

        return njs_function_call(vm,
                                 njs_function(&context->capability->resolve),
                                 &njs_value_undefined, &arr_value, 1,
                                 &vm->retval);
    }

    njs_vm_retval_set(vm, &njs_value_undefined);

    return NJS_OK;
}


static njs_int_t
njs_promise_perform_all_settled_handler(njs_vm_t *vm, njs_iterator_args_t *args,
    njs_value_t *value, int64_t index)
{
    njs_int_t                    ret;
    njs_array_t                  *array;
    njs_value_t                  arguments[2], next, arr_value;
    njs_function_t               *on_fulfilled, *on_rejected;
    njs_promise_capability_t     *capability;
    njs_promise_all_context_t    *context;
    njs_promise_iterator_args_t  *pargs;

    if (!njs_is_valid(value)) {
        value = njs_value_arg(&njs_value_undefined);
    }

    pargs = (njs_promise_iterator_args_t *) args;

    capability = pargs->capability;

    array = args->data;
    njs_set_array(&arr_value, array);

    ret = njs_value_property_i64_set(vm, &arr_value, index,
                                     njs_value_arg(&njs_value_undefined));
    if (njs_slow_path(ret == NJS_ERROR)) {
        return ret;
    }

    ret = njs_function_call(vm, pargs->function, pargs->constructor, value,
                            1, &next);
    if (njs_slow_path(ret == NJS_ERROR)) {
        return ret;
    }

    on_fulfilled = njs_promise_create_function(vm,
                                            sizeof(njs_promise_all_context_t));
    if (njs_slow_path(on_fulfilled == NULL)) {
        return NJS_ERROR;
    }

    context = on_fulfilled->context;

    context->already_called = 0;
    context->index = (uint32_t) index;
    context->values = pargs->args.data;
    context->capability = capability;
    context->remaining_elements = pargs->remaining;

    on_rejected = njs_promise_create_function(vm, 0);
    if (njs_slow_path(on_rejected == NULL)) {
        return NJS_ERROR;
    }

    on_fulfilled->u.native = njs_promise_all_settled_element_functions;
    on_rejected->u.native = njs_promise_all_settled_element_functions;
    on_rejected->magic8 = 1; /* rejected. */

    on_fulfilled->args_count = 1;
    on_rejected->args_count = 1;

    on_rejected->context = context;

    (*pargs->remaining)++;

    njs_set_function(&arguments[0], on_fulfilled);
    njs_set_function(&arguments[1], on_rejected);

    ret = njs_promise_invoke_then(vm, &next, arguments, 2);
    if (njs_slow_path(ret == NJS_ERROR)) {
        return ret;
    }

    return NJS_OK;
}


static njs_int_t
njs_promise_all_settled_element_functions(njs_vm_t *vm,
    njs_value_t *args, njs_uint_t nargs, njs_index_t rejected)
{
    njs_int_t                  ret;
    njs_value_t                obj_value, arr_value;
    njs_object_t               *obj;
    const njs_value_t          *status, *set;
    njs_promise_all_context_t  *context;

    static const njs_value_t  string_status = njs_string("status");
    static const njs_value_t  string_fulfilled = njs_string("fulfilled");
    static const njs_value_t  string_value = njs_string("value");
    static const njs_value_t  string_rejected = njs_string("rejected");
    static const njs_value_t  string_reason = njs_string("reason");

    context = vm->top_frame->function->context;

    if (context->already_called) {
        njs_vm_retval_set(vm, &njs_value_undefined);
        return NJS_OK;
    }

    context->already_called = 1;

    obj = njs_object_alloc(vm);
    if (njs_slow_path(obj == NULL)) {
        return NJS_ERROR;
    }

    njs_set_object(&obj_value, obj);

    if (rejected) {
        status = &string_rejected;
        set = &string_reason;

    } else {
        status = &string_fulfilled;
        set = &string_value;
    }

    ret = njs_value_property_set(vm, &obj_value, njs_value_arg(&string_status),
                                 njs_value_arg(status));
    if (njs_slow_path(ret == NJS_ERROR)) {
        return ret;
    }

    ret = njs_value_property_set(vm, &obj_value, njs_value_arg(set),
                                 njs_arg(args, nargs, 1));
    if (njs_slow_path(ret == NJS_ERROR)) {
        return ret;
    }

    njs_set_array(&arr_value, context->values);

    ret = njs_value_property_i64_set(vm, &arr_value, context->index,
                                     &obj_value);
    if (njs_slow_path(ret == NJS_ERROR)) {
        return ret;
    }

    if (--(*context->remaining_elements) == 0) {
        njs_mp_free(vm->mem_pool, context->remaining_elements);

        return njs_function_call(vm,
                                 njs_function(&context->capability->resolve),
                                 &njs_value_undefined, &arr_value, 1,
                                 &vm->retval);
    }

    njs_vm_retval_set(vm, &njs_value_undefined);

    return NJS_OK;
}


static njs_int_t
njs_promise_perform_any_handler(njs_vm_t *vm, njs_iterator_args_t *args,
    njs_value_t *value, int64_t index)
{
    njs_int_t                    ret;
    njs_array_t                  *array;
    njs_value_t                  arguments[2], next, arr_value;
    njs_function_t               *on_rejected;
    njs_promise_capability_t     *capability;
    njs_promise_all_context_t    *context;
    njs_promise_iterator_args_t  *pargs;

    if (!njs_is_valid(value)) {
        value = njs_value_arg(&njs_value_undefined);
    }

    pargs = (njs_promise_iterator_args_t *) args;

    capability = pargs->capability;

    array = args->data;
    njs_set_array(&arr_value, array);

    ret = njs_value_property_i64_set(vm, &arr_value, index,
                                     njs_value_arg(&njs_value_undefined));
    if (njs_slow_path(ret == NJS_ERROR)) {
        return ret;
    }

    ret = njs_function_call(vm, pargs->function, pargs->constructor, value, 1,
                            &next);
    if (njs_slow_path(ret == NJS_ERROR)) {
        return ret;
    }

    on_rejected = njs_promise_create_function(vm,
                                            sizeof(njs_promise_all_context_t));
    if (njs_slow_path(on_rejected == NULL)) {
        return NJS_ERROR;
    }

    on_rejected->u.native = njs_promise_any_reject_element_functions;
    on_rejected->args_count = 1;

    context = on_rejected->context;

    context->already_called = 0;
    context->index = (uint32_t) index;
    context->values = pargs->args.data;
    context->capability = capability;
    context->remaining_elements = pargs->remaining;

    (*pargs->remaining)++;

    arguments[0] = capability->resolve;
    njs_set_function(&arguments[1], on_rejected);

    ret = njs_promise_invoke_then(vm, &next, arguments, 2);
    if (njs_slow_path(ret == NJS_ERROR)) {
        return ret;
    }

    return NJS_OK;
}


static njs_int_t
njs_promise_any_reject_element_functions(njs_vm_t *vm, njs_value_t *args,
    njs_uint_t nargs, njs_index_t unused)
{
    njs_int_t                  ret;
    njs_value_t                argument, arr_value;
    njs_object_t               *error;
    njs_promise_all_context_t  *context;

    context = vm->top_frame->function->context;

    if (context->already_called) {
        njs_vm_retval_set(vm, &njs_value_undefined);
        return NJS_OK;
    }

    context->already_called = 1;

    njs_set_array(&arr_value, context->values);

    ret = njs_value_property_i64_set(vm, &arr_value, context->index,
                                     njs_arg(args, nargs, 1));
    if (njs_slow_path(ret == NJS_ERROR)) {
        return ret;
    }

    if (--(*context->remaining_elements) == 0) {
        njs_mp_free(vm->mem_pool, context->remaining_elements);

        error = njs_error_alloc(vm, NJS_OBJ_TYPE_AGGREGATE_ERROR,
                                NULL, &string_any_rejected, &arr_value);
        if (njs_slow_path(error == NULL)) {
            return NJS_ERROR;
        }

        njs_set_object(&argument, error);

        return njs_function_call(vm, njs_function(&context->capability->reject),
                                 &njs_value_undefined, &argument, 1,
                                 &vm->retval);
    }

    njs_vm_retval_set(vm, &njs_value_undefined);

    return NJS_OK;
}


static njs_int_t
njs_promise_race(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t unused)
{
    int64_t                      length;
    njs_int_t                    ret;
    njs_value_t                  *promise_ctor, *iterator, resolve;
    njs_promise_iterator_args_t  pargs;

    promise_ctor = njs_argument(args, 0);
    iterator = njs_arg(args, nargs, 1);

    pargs.capability = njs_promise_new_capability(vm, promise_ctor);
    if (njs_slow_path(pargs.capability == NULL)) {
        return NJS_ERROR;
    }

    ret = njs_value_property(vm, promise_ctor, njs_value_arg(&string_resolve),
                             &resolve);
    if (njs_slow_path(ret == NJS_ERROR)) {
        return ret;
    }

    if (njs_slow_path(!njs_is_function(&resolve))) {
        njs_type_error(vm, "resolve is not callable");
        return NJS_ERROR;
    }

    ret = njs_object_length(vm, iterator, &length);
    if (njs_slow_path(ret != NJS_OK)) {
        return ret;
    }

    njs_memzero(&pargs.args, sizeof(njs_iterator_args_t));

    pargs.function = njs_function(&resolve);
    pargs.constructor = promise_ctor;

    pargs.args.value = iterator;
    pargs.args.to = length;

    ret = njs_object_iterate(vm, &pargs.args, njs_promise_perform_race_handler);
    if (njs_slow_path(ret == NJS_ERROR)) {
        return ret;
    }

    vm->retval = pargs.capability->promise;

    return NJS_OK;
}


static njs_int_t
njs_promise_perform_race_handler(njs_vm_t *vm, njs_iterator_args_t *args,
    njs_value_t *value, int64_t index)
{
    njs_int_t                    ret;
    njs_value_t                  arguments[2], next;
    njs_promise_capability_t     *capability;
    njs_promise_iterator_args_t  *pargs;

    if (!njs_is_valid(value)) {
        value = njs_value_arg(&njs_value_undefined);
    }

    pargs = (njs_promise_iterator_args_t *) args;

    ret = njs_function_call(vm, pargs->function, pargs->constructor, value,
                            1, &next);
    if (njs_slow_path(ret == NJS_ERROR)) {
        return ret;
    }

    capability = pargs->capability;

    arguments[0] = capability->resolve;
    arguments[1] = capability->reject;

    (void) njs_promise_invoke_then(vm, &next, arguments, 2);

    return NJS_OK;
}


static njs_int_t
njs_promise_species(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t unused)
{
    njs_vm_retval_set(vm, njs_argument(args, 0));

    return NJS_OK;
}


static const njs_object_prop_t  njs_promise_constructor_properties[] =
{
    {
        .type = NJS_PROPERTY,
        .name = njs_string("name"),
        .value = njs_string("Promise"),
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

    {
        .type = NJS_PROPERTY,
        .name = njs_string("resolve"),
        .value = njs_native_function(njs_promise_object_resolve, 1),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("reject"),
        .value = njs_native_function(njs_promise_object_reject, 1),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("all"),
        .value = njs_native_function2(njs_promise_all, 1, NJS_PROMISE_ALL),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("allSettled"),
        .value = njs_native_function2(njs_promise_all, 1,
                                      NJS_PROMISE_ALL_SETTLED),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("any"),
        .value = njs_native_function2(njs_promise_all, 1, NJS_PROMISE_ANY),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("race"),
        .value = njs_native_function(njs_promise_race, 1),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_wellknown_symbol(NJS_SYMBOL_SPECIES),
        .value = njs_value(NJS_INVALID, 1, NAN),
        .getter = njs_native_function(njs_promise_species, 0),
        .setter = njs_value(NJS_UNDEFINED, 0, NAN),
        .writable = NJS_ATTRIBUTE_UNSET,
        .configurable = 1,
        .enumerable = 0,
    },
};


const njs_object_init_t  njs_promise_constructor_init = {
    njs_promise_constructor_properties,
    njs_nitems(njs_promise_constructor_properties),
};


static const njs_object_prop_t  njs_promise_prototype_properties[] =
{
    {
        .type = NJS_PROPERTY_HANDLER,
        .name = njs_string("constructor"),
        .value = njs_prop_handler(njs_object_prototype_create_constructor),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_wellknown_symbol(NJS_SYMBOL_TO_STRING_TAG),
        .value = njs_string("Promise"),
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("then"),
        .value = njs_native_function(njs_promise_prototype_then, 2),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("catch"),
        .value = njs_native_function(njs_promise_prototype_catch, 1),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("finally"),
        .value = njs_native_function(njs_promise_prototype_finally, 1),
        .writable = 1,
        .configurable = 1,
    },
};


const njs_object_init_t  njs_promise_prototype_init = {
    njs_promise_prototype_properties,
    njs_nitems(njs_promise_prototype_properties),
};

const njs_object_type_init_t  njs_promise_type_init = {
    .constructor = njs_native_ctor(njs_promise_constructor, 1, 0),
    .prototype_props = &njs_promise_prototype_init,
    .constructor_props = &njs_promise_constructor_init,
    .prototype_value = { .object = { .type = NJS_OBJECT } },
};
