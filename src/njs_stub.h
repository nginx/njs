
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) NGINX, Inc.
 */

#ifndef _NJS_STUB_H_INCLUDED_
#define _NJS_STUB_H_INCLUDED_


#define njs_max(val1, val2)                                                   \
    ((val1 < val2) ? (val2) : (val1))

#define njs_min(val1, val2)                                                   \
    ((val1 < val2) ? (val1) : (val2))


#define NJS_OK             0
#define NJS_ERROR          (-1)
#define NJS_AGAIN          (-2)
#define NJS_DECLINED       (-3)
#define NJS_DONE           (-4)


#define njs_thread_log_alert(...)
#define njs_thread_log_error(...)
#define njs_log_error(...)
#define njs_thread_log_debug(...)

#include <unistd.h>
#define njs_pagesize()      getpagesize()


#endif /* _NJS_STUB_H_INCLUDED_ */
