
/*
 * Copyright (C) Dmitry Volyntsev
 * Copyright (C) NGINX, Inc.
 */

#ifndef _NJS_EVENT_H_INCLUDED_
#define _NJS_EVENT_H_INCLUDED_


#define NJS_EVENT_RELEASE      1
#define NJS_EVENT_DELETE       2


#define njs_waiting_events(vm) (!njs_lvlhsh_is_empty(&(vm)->events_hash))

#define njs_posted_events(vm) (!njs_queue_is_empty(&(vm)->posted_events))

#define njs_promise_events(vm) (!njs_queue_is_empty(&(vm)->promise_events))


typedef struct {
    njs_function_t          *function;
    njs_value_t             *args;
    njs_uint_t              nargs;
    njs_host_event_t        host_event;
    njs_event_destructor_t  destructor;

    njs_value_t             id;
    njs_queue_link_t        link;

    unsigned                posted:1;
    unsigned                once:1;
} njs_event_t;


njs_int_t njs_add_event(njs_vm_t *vm, njs_event_t *event);
void njs_del_event(njs_vm_t *vm, njs_event_t *event, njs_uint_t action);


extern const njs_lvlhsh_proto_t  njs_event_hash_proto;


#endif /* _NJS_EVENT_H_INCLUDED_ */
