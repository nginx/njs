
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) NGINX, Inc.
 */

#ifndef _NXT_STUB_H_INCLUDED_
#define _NXT_STUB_H_INCLUDED_


#define nxt_max(val1, val2)                                                   \
    ((val1 < val2) ? (val2) : (val1))

#define nxt_min(val1, val2)                                                   \
    ((val1 < val2) ? (val1) : (val2))


#define NXT_OK             0
#define NXT_ERROR          (-1)
#define NXT_AGAIN          (-2)
#define NXT_DECLINED       (-3)
#define NXT_DONE           (-4)


typedef struct {
    void           *(*alloc)(void *mem, size_t size);
    void           *(*zalloc)(void *mem, size_t size);
    void           *(*align)(void *mem, size_t alignment, size_t size);
    void           *(*zalign)(void *mem, size_t alignment, size_t size);
    void           (*free)(void *mem, void *p);
    void           (*alert)(void *trace, const char *fmt, ...);
    void nxt_cdecl  (*trace)(void *trace, const char *fmt, ...);
} nxt_mem_proto_t;


#define nxt_thread_log_alert(...)
#define nxt_thread_log_error(...)
#define nxt_log_error(...)
#define nxt_thread_log_debug(...)

#define NXT_DOUBLE_LEN   1024

#include <unistd.h>
#define nxt_pagesize()      getpagesize()


#endif /* _NXT_STUB_H_INCLUDED_ */
