
/*
 * Copyright (C) Dmitry Volyntsev
 * Copyright (C) Nginx, Inc.
 */

#ifndef _NJS_STRTOD_H_INCLUDED_
#define _NJS_STRTOD_H_INCLUDED_

NJS_EXPORT double njs_strtod(const u_char **start, const u_char *end,
    njs_bool_t literal);

#endif /* _NJS_STRTOD_H_INCLUDED_ */
