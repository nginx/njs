
/*
 * Copyright (C) Dmitry Volyntsev
 * Copyright (C) NGINX, Inc.
 */

#ifndef _NJS_EVENT_H_INCLUDED_
#define _NJS_EVENT_H_INCLUDED_


#define NJS_EVENT_RELEASE      1
#define NJS_EVENT_DELETE       2


#define njs_is_pending_events(vm) (!nxt_lvlhsh_is_empty(&(vm)->events_hash))


typedef struct {
    njs_function_t        *function;
    njs_value_t           *args;
    nxt_uint_t            nargs;
    njs_host_event_t      host_event;
    njs_event_destructor  destructor;

    njs_value_t           id;
    nxt_queue_link_t      link;

    unsigned              posted:1;
    unsigned              once:1;
} njs_event_t;


nxt_int_t njs_add_event(njs_vm_t *vm, njs_event_t *event);
void njs_del_event(njs_vm_t *vm, njs_event_t *event, nxt_uint_t action);


extern const nxt_lvlhsh_proto_t  njs_event_hash_proto;


#endif /* _NJS_EVENT_H_INCLUDED_ */
