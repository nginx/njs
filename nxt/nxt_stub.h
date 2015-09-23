
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

#define nxt_lowcase(c)                                                        \
    (u_char) ((c >= 'A' && c <= 'Z') ? c | 0x20 : c)

#define nxt_strstr_eq(s1, s2)                                                 \
    (((s1)->len == (s2)->len)                                                 \
      && (memcmp((s1)->data, (s2)->data, (s1)->len) == 0))


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


typedef struct {
    size_t         len;
    u_char         *data;
} nxt_str_t;


#define nxt_string(str)       { sizeof(str) - 1, (u_char *) str }
#define nxt_string_zero(str)  { sizeof(str), (u_char *) str }
#define nxt_null_string       { 0, NULL }


#define nxt_thread_log_alert(...)
#define nxt_thread_log_error(...)
#define nxt_log_error(...)
#define nxt_thread_log_debug(...)
#define nxt_number_parse(a, b)      1

#define NXT_DOUBLE_LEN   1024

#include <unistd.h>
#define nxt_pagesize()      getpagesize()


#endif /* _NXT_STUB_H_INCLUDED_ */
