
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) NGINX, Inc.
 */

#ifndef _NJS_TRACE_H_INCLUDED_
#define _NJS_TRACE_H_INCLUDED_


typedef enum {
    NJS_LEVEL_CRIT = 0,
    NJS_LEVEL_ERROR,
    NJS_LEVEL_WARN,
    NJS_LEVEL_INFO,
    NJS_LEVEL_TRACE,
} njs_trace_level_t;


typedef struct {
    uint32_t             level;
    u_char               *end;
    const char           *fmt;
    va_list              args;
} njs_trace_data_t;


typedef struct njs_trace_s  njs_trace_t;

typedef u_char *(*njs_trace_handler_t)(njs_trace_t *trace, njs_trace_data_t *td,
    u_char *start);

struct njs_trace_s {
    uint32_t             level;
    uint32_t             size;
    njs_trace_handler_t  handler;
    void                 *data;
    njs_trace_t          *prev;
    njs_trace_t          *next;
};


#define njs_alert(_trace, _level, ...)                                        \
    do {                                                                      \
        njs_trace_t  *_trace_ = _trace;                                       \
        uint32_t     _level_ = _level;                                        \
                                                                              \
        if (njs_slow_path(_trace_->level >= _level_)) {                       \
            njs_trace_handler(_trace_, _level_, __VA_ARGS__);                 \
        }                                                                     \
    } while (0)


#define njs_trace(_trace, ...)                                                \
    do {                                                                      \
        njs_trace_t  *_trace_ = _trace;                                       \
                                                                              \
        if (njs_slow_path(_trace_->level == NJS_LEVEL_TRACE)) {               \
            njs_trace_handler(_trace_, NJS_LEVEL_TRACE, __VA_ARGS__);         \
        }                                                                     \
    } while (0)


#define njs_thread_log_alert(...)
#define njs_thread_log_error(...)
#define njs_log_error(...)
#define njs_thread_log_debug(...)


NJS_EXPORT void njs_trace_handler(njs_trace_t *trace, uint32_t level,
    const char *fmt, ...);


#endif /* _NJS_TRACE_H_INCLUDED_ */
