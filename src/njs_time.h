
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) NGINX, Inc.
 */

#ifndef _NJS_TIME_H_INCLUDED_
#define _NJS_TIME_H_INCLUDED_


#if (NJS_HAVE_TM_GMTOFF)

#define njs_timezone(tm)                                                      \
    ((tm)->tm_gmtoff)

#elif (NJS_HAVE_ALTZONE)

#define njs_timezone(tm)                                                      \
    (-(((tm)->tm_isdst > 0) ? altzone : timezone))

#endif


uint64_t njs_time(void);


#endif /* _NJS_TIME_H_INCLUDED_ */
