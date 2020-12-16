
/*
 * Copyright (C) Dmitry Volyntsev
 * Copyright (C) NGINX, Inc.
 */


#include <njs_main.h>


static njs_int_t njs_event_hash_test(njs_lvlhsh_query_t *lhq, void *data);


const njs_lvlhsh_proto_t  njs_event_hash_proto
    njs_aligned(64) =
{
    NJS_LVLHSH_DEFAULT,
    njs_event_hash_test,
    njs_lvlhsh_alloc,
    njs_lvlhsh_free,
};


static njs_int_t
njs_event_hash_test(njs_lvlhsh_query_t *lhq, void *data)
{
    njs_str_t    id;
    njs_event_t  *event;

    event = data;

    njs_string_get(&event->id, &id);

    if (njs_strstr_eq(&lhq->key, &id)) {
        return NJS_OK;
    }

    return NJS_DECLINED;
}


njs_int_t
njs_add_event(njs_vm_t *vm, njs_event_t *event)
{
    njs_int_t           ret;
    njs_lvlhsh_query_t  lhq;

    njs_uint32_to_string(&event->id, vm->event_id++);

    njs_string_get(&event->id, &lhq.key);
    lhq.key_hash = njs_djb_hash(lhq.key.start, lhq.key.length);
    lhq.value = event;
    lhq.proto = &njs_event_hash_proto;
    lhq.pool = vm->mem_pool;

    ret = njs_lvlhsh_insert(&vm->events_hash, &lhq);
    if (njs_slow_path(ret != NJS_OK)) {
        njs_internal_error(vm, "Failed to add event with id: %s",
                           njs_string_short_start(&event->id));

        njs_del_event(vm, event, NJS_EVENT_RELEASE | NJS_EVENT_DELETE);
        return NJS_ERROR;
    }

    return NJS_OK;
}


void
njs_del_event(njs_vm_t *vm, njs_event_t *ev, njs_uint_t action)
{
    njs_lvlhsh_query_t  lhq;

    if (action & NJS_EVENT_RELEASE) {
        if (ev->destructor != NULL && ev->host_event != NULL) {
            ev->destructor(vm->external, ev->host_event);
        }

        ev->host_event = NULL;
    }

    if (action & NJS_EVENT_DELETE) {
        njs_string_get(&ev->id, &lhq.key);
        lhq.key_hash = njs_djb_hash(lhq.key.start, lhq.key.length);
        lhq.proto = &njs_event_hash_proto;
        lhq.pool = vm->mem_pool;

        if (ev->posted) {
            ev->posted = 0;
            njs_queue_remove(&ev->link);
        }

        (void) njs_lvlhsh_delete(&vm->events_hash, &lhq);
    }
}
