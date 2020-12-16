
/*
 * Copyright (C) Dmitry Volyntsev
 * Copyright (C) NGINX, Inc.
 */


#include <njs_main.h>


static njs_int_t
njs_set_timer(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t unused, njs_bool_t immediate)
{
    njs_uint_t     n;
    uint64_t       delay;
    njs_event_t   *event;
    njs_vm_ops_t  *ops;

    if (njs_slow_path(nargs < 2)) {
        njs_type_error(vm, "too few arguments");
        return NJS_ERROR;
    }

    if (njs_slow_path(!njs_is_function(&args[1]))) {
        njs_type_error(vm, "first arg must be a function");
        return NJS_ERROR;
    }

    ops = vm->options.ops;
    if (njs_slow_path(ops == NULL)) {
        njs_internal_error(vm, "not supported by host environment");
        return NJS_ERROR;
    }

    delay = 0;

    if (!immediate && nargs >= 3 && njs_is_number(&args[2])) {
        delay = njs_number(&args[2]);
    }

    event = njs_mp_alloc(vm->mem_pool, sizeof(njs_event_t));
    if (njs_slow_path(event == NULL)) {
        goto memory_error;
    }

    n = immediate ? 2 : 3;

    event->destructor = ops->clear_timer;
    event->function = njs_function(&args[1]);
    event->nargs = (nargs >= n) ? nargs - n : 0;
    event->once = 1;
    event->posted = 0;

    if (event->nargs != 0) {
        event->args = njs_mp_alloc(vm->mem_pool,
                                   sizeof(njs_value_t) * event->nargs);
        if (njs_slow_path(event->args == NULL)) {
            goto memory_error;
        }

        memcpy(event->args, &args[n], sizeof(njs_value_t) * event->nargs);
    }

    event->host_event = ops->set_timer(vm->external, delay, event);
    if (njs_slow_path(event->host_event == NULL)) {
        njs_internal_error(vm, "set_timer() failed");
        return NJS_ERROR;
    }

    if (njs_add_event(vm, event) == NJS_OK) {
        njs_set_number(&vm->retval, vm->event_id - 1);
    }

    return NJS_OK;

memory_error:

    njs_memory_error(vm);

    return NJS_ERROR;
}


njs_int_t
njs_set_timeout(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t unused)
{
    return njs_set_timer(vm, args, nargs, unused, 0);
}


njs_int_t
njs_set_immediate(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t unused)
{
    return njs_set_timer(vm, args, nargs, unused, 1);
}


njs_int_t
njs_clear_timeout(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t unused)
{
    u_char              buf[16], *p;
    njs_int_t           ret;
    njs_event_t         *event;
    njs_lvlhsh_query_t  lhq;

    if (njs_fast_path(nargs < 2) || !njs_is_number(&args[1])) {
        njs_set_undefined(&vm->retval);
        return NJS_OK;
    }

    p = njs_sprintf(buf, buf + njs_length(buf), "%uD",
                    (unsigned) njs_number(&args[1]));

    lhq.key.start = buf;
    lhq.key.length = p - buf;
    lhq.key_hash = njs_djb_hash(lhq.key.start, lhq.key.length);
    lhq.proto = &njs_event_hash_proto;
    lhq.pool = vm->mem_pool;

    ret = njs_lvlhsh_find(&vm->events_hash, &lhq);
    if (ret == NJS_OK) {
        event = lhq.value;
        njs_del_event(vm, event, NJS_EVENT_RELEASE | NJS_EVENT_DELETE);
    }

    njs_set_undefined(&vm->retval);

    return NJS_OK;
}
