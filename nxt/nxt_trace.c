
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) NGINX, Inc.
 */

#include <nxt_auto_config.h>
#include <nxt_types.h>
#include <nxt_clang.h>
#include <nxt_malloc.h>
#include <nxt_trace.h>
#include <stdio.h>


static u_char *
nxt_last_handler(nxt_trace_t *trace, nxt_trace_data_t *td, u_char *start)
{
    int      n;
    ssize_t  size;

    size = td->end - start;
    n = vsnprintf((char *) start, size, td->fmt, td->args);

    if (n < size) {
        start += n;
    }

    return start;
}


void
nxt_trace_handler(nxt_trace_t *trace, uint32_t level, const char *fmt, ...)
{
    u_char            *start;
    nxt_trace_t       last;
    nxt_trace_data_t  td;

    td.level = level;
    td.fmt = fmt;

    va_start(td.args, fmt);

    start = alloca(trace->size);
    td.end = start + trace->size;

    last.handler = nxt_last_handler;
    trace->next = &last;

    while (trace->prev != NULL) {
        trace = trace->prev;
    }

    (void) trace->handler(trace, &td, start);

    va_end(td.args);
}
