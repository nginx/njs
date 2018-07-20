
/*
 * Copyright (C) Dmitry Volyntsev
 * Copyright (C) NGINX, Inc.
 */


#ifndef _NXT_UNIX_H_INCLUDED_
#define _NXT_UNIX_H_INCLUDED_

#if (NXT_LINUX)

#ifdef _FORTIFY_SOURCE
/*
 * _FORTIFY_SOURCE
 *     does not allow to use "(void) write()";
 */
#undef _FORTIFY_SOURCE
#endif

#endif /* NXT_LINUX */

#endif /* _NXT_UNIX_H_INCLUDED_ */
