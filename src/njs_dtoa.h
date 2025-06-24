
/*
 * Copyright (C) Dmitry Volyntsev
 * Copyright (C) Nginx, Inc.
 */

#ifndef _NJS_DTOA_H_INCLUDED_
#define _NJS_DTOA_H_INCLUDED_

#define NJS_DTOA_MAX_LEN  njs_length("-1.7976931348623157e+308")

NJS_EXPORT size_t njs_dtoa(double value, char *start);
NJS_EXPORT size_t njs_dtoa_precision(double value, char *start, size_t prec);
NJS_EXPORT size_t njs_dtoa_exponential(double value, char *start,
    njs_int_t frac);

#endif /* _NJS_DTOA_H_INCLUDED_ */
