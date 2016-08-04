
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) NGINX, Inc.
 */

#ifndef _NXT_TRACE_H_INCLUDED_
#define _NXT_TRACE_H_INCLUDED_


typedef enum {
    NXT_LEVEL_CRIT = 0,
    NXT_LEVEL_ERROR,
    NXT_LEVEL_WARN,
    NXT_LEVEL_INFO,
    NXT_LEVEL_TRACE,
} nxt_trace_level_t;


typedef struct {
    uint32_t             level;
    u_char               *end;
    const char           *fmt;
    va_list              args;
} nxt_trace_data_t;


typedef struct nxt_trace_s  nxt_trace_t;

typedef u_char *(*nxt_trace_handler_t)(nxt_trace_t *trace, nxt_trace_data_t *td,
    u_char *start);

struct nxt_trace_s {
    uint32_t             level;
    uint32_t             size;
    nxt_trace_handler_t  handler;
    void                 *data;
    nxt_trace_t          *prev;
    nxt_trace_t          *next;
};


#define nxt_alert(_trace, _level, ...)                                        \
    do {                                                                      \
        nxt_trace_t  *_trace_ = _trace;                                       \
        uint32_t     _level_ = _level;                                        \
                                                                              \
        if (nxt_slow_path(_trace_->level >= _level_)) {                       \
            nxt_trace_handler(_trace_, _level_, __VA_ARGS__);                 \
        }                                                                     \
    } while (0)


#define nxt_trace(_trace, ...)                                                \
    do {                                                                      \
        nxt_trace_t  *_trace_ = _trace;                                       \
                                                                              \
        if (nxt_slow_path(_trace_->level == NXT_LEVEL_TRACE)) {               \
            nxt_trace_handler(_trace_, NXT_LEVEL_TRACE, __VA_ARGS__);         \
        }                                                                     \
    } while (0)


NXT_EXPORT void nxt_trace_handler(nxt_trace_t *trace, uint32_t level,
    const char *fmt, ...);


#endif /* _NXT_TRACE_H_INCLUDED_ */
