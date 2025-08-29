
/*
 * Copyright (C) F5, Inc.
 */

#ifndef __has_warning
    #define __has_warning(x) 0
#endif

#if (defined(__GNUC__) && (__GNUC__ >= 8))                                    \
    || (defined(__clang__) && __has_warning("-Wcast-function-type"))
    #pragma GCC diagnostic push
    #pragma GCC diagnostic ignored "-Wcast-function-type"

    #include <quickjs.h>

    #pragma GCC diagnostic pop
#else
    #include <quickjs.h>
#endif

#ifndef JS_BOOL
    #define JS_BOOL bool
#endif
