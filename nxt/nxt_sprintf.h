
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) NGINX, Inc.
 */

#ifndef _NXT_SPRINTF_H_INCLUDED_
#define _NXT_SPRINTF_H_INCLUDED_


NXT_EXPORT u_char *nxt_sprintf(u_char *buf, u_char *end, const char *fmt, ...);
NXT_EXPORT u_char *nxt_vsprintf(u_char *buf, u_char *end, const char *fmt,
    va_list args);

NXT_EXPORT int nxt_dprint(int fd, u_char *buf, size_t size);
NXT_EXPORT int nxt_dprintf(int fd, const char *fmt, ...);

#define nxt_print(buf, size)                                                 \
    nxt_dprint(STDOUT_FILENO, (u_char *) buf, size)

#define nxt_printf(fmt, ...)                                                 \
    nxt_dprintf(STDOUT_FILENO, fmt, ##__VA_ARGS__)

#define nxt_error(fmt, ...)                                                  \
    nxt_dprintf(STDERR_FILENO, fmt, ##__VA_ARGS__)

#endif /* _NXT_SPRINTF_H_INCLUDED_ */
