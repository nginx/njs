
/*
 * Copyright (C) Dmitry Volyntsev
 * Copyright (C) NGINX, Inc.
 */


#include <ngx_config.h>

#if (NGX_PCRE2)

#define NJS_HAVE_PCRE2  1

#endif

#include "../external/njs_regex.c"
