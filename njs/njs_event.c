
/*
 * Copyright (C) Dmitry Volyntsev
 * Copyright (C) NGINX, Inc.
 */

#include <njs_core.h>
#include <string.h>


static nxt_int_t njs_event_hash_test(nxt_lvlhsh_query_t *lhq, void *data);


const nxt_lvlhsh_proto_t  njs_event_hash_proto
    nxt_aligned(64) =
{
    NXT_LVLHSH_DEFAULT,
    0,
    njs_event_hash_test,
    njs_lvlhsh_alloc,
    njs_lvlhsh_free,
};


static nxt_int_t
njs_event_hash_test(nxt_lvlhsh_query_t *lhq, void *data)
{
    nxt_str_t    id;
    njs_event_t  *event;

    event = data;

    njs_string_get(&event->id, &id);

    if (nxt_strstr_eq(&lhq->key, &id)) {
        return NXT_OK;
    }

    return NXT_DECLINED;
}


nxt_int_t
njs_add_event(njs_vm_t *vm, njs_event_t *event)
{
    nxt_int_t           ret;
    nxt_lvlhsh_query_t  lhq;

    njs_uint32_to_string(&event->id, vm->event_id++);

    njs_string_get(&event->id, &lhq.key);
    lhq.key_hash = nxt_djb_hash(lhq.key.start, lhq.key.length);
    lhq.value = event;
    lhq.proto = &njs_event_hash_proto;
    lhq.pool = vm->mem_cache_pool;

    ret = nxt_lvlhsh_insert(&vm->events_hash, &lhq);
    if (nxt_slow_path(ret != NXT_OK)) {
        njs_internal_error(vm, "Failed to add event with id: %s",
                           njs_string_short_start(&event->id));

        njs_del_event(vm, event, NJS_EVENT_RELEASE | NJS_EVENT_DELETE);
        return NJS_ERROR;
    }

    njs_value_number_set(&vm->retval, vm->event_id - 1);

    return NJS_OK;
}


void
njs_del_event(njs_vm_t *vm, njs_event_t *ev, nxt_uint_t action)
{
    nxt_lvlhsh_query_t  lhq;

    if (action & NJS_EVENT_RELEASE) {
        if (ev->destructor != NULL && ev->host_event != NULL) {
            ev->destructor(vm->external, ev->host_event);
        }

        ev->host_event = NULL;
    }

    if (action & NJS_EVENT_DELETE) {
        njs_string_get(&ev->id, &lhq.key);
        lhq.key_hash = nxt_djb_hash(lhq.key.start, lhq.key.length);
        lhq.proto = &njs_event_hash_proto;
        lhq.pool = vm->mem_cache_pool;

        if (ev->posted) {
            ev->posted = 0;
            nxt_queue_remove(&ev->link);
        }

        (void) nxt_lvlhsh_delete(&vm->events_hash, &lhq);
    }
}
