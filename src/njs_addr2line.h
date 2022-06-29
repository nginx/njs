
/*
 * Copyright (C) Dmitry Volyntsev
 * Copyright (C) Nginx, Inc.
 */

#ifndef _NJS_ADDR2LINE_H_INCLUDED_
#define _NJS_ADDR2LINE_H_INCLUDED_


 u_char *_njs_addr2line(u_char *buf, u_char *end, void *address);


#if defined(NJS_HAVE_LIBBFD) && defined(NJS_HAVE_DL_ITERATE_PHDR)
#define NJS_HAVE_ADDR2LINE            1
#define njs_addr2line(buf, end, addr) _njs_addr2line(buf, end, addr)
#else
#define njs_addr2line(buf, end, addr) \
                      njs_sprintf(buf, end, "\?\?() \?\?:0 [0x%p]", addr)
#endif

#endif /* _NJS_ADDR2LINE_H_INCLUDED_ */
