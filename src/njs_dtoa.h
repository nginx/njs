
/*
 * Copyright (C) Dmitry Volyntsev
 * Copyright (C) Nginx, Inc.
 */

#ifndef _NJS_DTOA_H_INCLUDED_
#define _NJS_DTOA_H_INCLUDED_

NJS_EXPORT size_t njs_dtoa(double value, char *start);
NJS_EXPORT size_t njs_dtoa_precision(double value, char *start, size_t prec);

#endif /* _NJS_DTOA_H_INCLUDED_ */
