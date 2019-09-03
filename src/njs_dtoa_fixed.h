
/*
 * Copyright (C) Dmitry Volyntsev
 * Copyright (C) Nginx, Inc.
 */

#ifndef _NJS_DTOA_FIXED_H_INCLUDED_
#define _NJS_DTOA_FIXED_H_INCLUDED_

NJS_EXPORT size_t njs_fixed_dtoa(double value, njs_uint_t frac, char *start,
    njs_int_t *point);

#endif /* _NJS_DTOA_FIXED_H_INCLUDED_ */
