
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) NGINX, Inc.
 */


#include <njs_main.h>


static u_char *
njs_last_handler(njs_trace_t *trace, njs_trace_data_t *td, u_char *start)
{
    u_char   *p;
    ssize_t  size;

    size = td->end - start;
    p = njs_vsprintf(start, start + size, td->fmt, td->args);

    if (p - start < size) {
        start = p;
    }

    return start;
}


void
njs_trace_handler(njs_trace_t *trace, uint32_t level, const char *fmt, ...)
{
    u_char            *start;
    njs_trace_t       last;
    njs_trace_data_t  td;

    td.level = level;
    td.fmt = fmt;

    va_start(td.args, fmt);

    start = alloca(trace->size);
    td.end = start + trace->size;

    last.handler = njs_last_handler;
    trace->next = &last;

    while (trace->prev != NULL) {
        trace = trace->prev;
    }

    (void) trace->handler(trace, &td, start);

    va_end(td.args);
}
