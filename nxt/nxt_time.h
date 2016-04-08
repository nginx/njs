
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) NGINX, Inc.
 */

#ifndef _NXT_TIME_H_INCLUDED_
#define _NXT_TIME_H_INCLUDED_


#if (NXT_HAVE_TM_GMTOFF)

#define nxt_timezone(tm)                                                      \
    ((tm)->tm_gmtoff)

#elif (NXT_HAVE_ALTZONE)

#define nxt_timezone(tm)                                                      \
    (-(((tm)->tm_isdst > 0) ? altzone : timezone))

#endif


#endif /* _NXT_TIME_H_INCLUDED_ */
