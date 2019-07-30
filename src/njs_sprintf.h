
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) NGINX, Inc.
 */

#ifndef _NJS_SPRINTF_H_INCLUDED_
#define _NJS_SPRINTF_H_INCLUDED_


NJS_EXPORT u_char *njs_sprintf(u_char *buf, u_char *end, const char *fmt, ...);
NJS_EXPORT u_char *njs_vsprintf(u_char *buf, u_char *end, const char *fmt,
    va_list args);

NJS_EXPORT int njs_dprint(int fd, u_char *buf, size_t size);
NJS_EXPORT int njs_dprintf(int fd, const char *fmt, ...);

#define njs_print(buf, size)                                                 \
    njs_dprint(STDOUT_FILENO, (u_char *) buf, size)

#define njs_printf(fmt, ...)                                                 \
    njs_dprintf(STDOUT_FILENO, fmt, ##__VA_ARGS__)

#define njs_stderror(fmt, ...)                                               \
    njs_dprintf(STDERR_FILENO, fmt, ##__VA_ARGS__)

#endif /* _NJS_SPRINTF_H_INCLUDED_ */
