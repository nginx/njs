
/*
 * Copyright (C) Dmitry Volyntsev
 * Copyright (C) NGINX, Inc.
 */

#ifndef _NJS_ASSERT_H_INCLUDED_
#define _NJS_ASSERT_H_INCLUDED_

#if (NJS_DEBUG)

#define njs_assert(condition)                                                 \
    do {                                                                      \
        if (!(condition)) {                                                   \
            njs_stderror("Assertion \"%s\" failed at %s:%d\n", #condition,    \
                         __FILE__, __LINE__);                                 \
            abort();                                                          \
        }                                                                     \
    } while (0)

#else

#define njs_assert(condition) (void) (condition)

#endif

#endif /* _NJS_ASSERT_H_INCLUDED_ */
