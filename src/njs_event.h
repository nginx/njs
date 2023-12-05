
/*
 * Copyright (C) Dmitry Volyntsev
 * Copyright (C) NGINX, Inc.
 */

#ifndef _NJS_EVENT_H_INCLUDED_
#define _NJS_EVENT_H_INCLUDED_


typedef struct {
    njs_function_t          *function;
    njs_value_t             *args;
    njs_uint_t              nargs;

    njs_queue_link_t        link;
} njs_event_t;


#endif /* _NJS_EVENT_H_INCLUDED_ */
