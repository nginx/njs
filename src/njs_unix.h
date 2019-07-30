
/*
 * Copyright (C) Dmitry Volyntsev
 * Copyright (C) NGINX, Inc.
 */


#ifndef _NJS_UNIX_H_INCLUDED_
#define _NJS_UNIX_H_INCLUDED_

#if (NJS_LINUX)

#ifdef _FORTIFY_SOURCE
/*
 * _FORTIFY_SOURCE
 *     does not allow to use "(void) write()";
 */
#undef _FORTIFY_SOURCE
#endif

#endif /* NJS_LINUX */

#endif /* _NJS_UNIX_H_INCLUDED_ */
