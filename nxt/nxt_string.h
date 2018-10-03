
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) NGINX, Inc.
 */

#ifndef _NXT_STRING_H_INCLUDED_
#define _NXT_STRING_H_INCLUDED_


typedef struct {
    size_t  length;
    u_char  *start;
} nxt_str_t;


/*
 * C99 allows to assign struct as compound literal with struct name cast only.
 * SunC however issues error on the cast in struct static initialization:
 *   non-constant initializer: op "NAME"
 * So a separate nxt_string_value() macro is intended to use in assignment.
 */

#define nxt_length(s)        (sizeof(s) - 1)
#define nxt_string(s)        { nxt_length(s), (u_char *) s }
#define nxt_null_string      { 0, NULL }
#define nxt_string_value(s)  (nxt_str_t) nxt_string(s)


nxt_inline u_char
nxt_lower_case(u_char c)
{
    return (u_char) ((c >= 'A' && c <= 'Z') ? c | 0x20 : c);
}


nxt_inline u_char
nxt_upper_case(u_char c)
{
    return (u_char) ((c >= 'a' && c <= 'z') ? c & 0xDF : c);
}


#define nxt_cpymem(dst, src, n)   (((u_char *) memcpy(dst, src, n)) + (n))


#define nxt_memset(buf, c, length)   (void) (memset(buf, c, length))


#define nxt_memzero(buf, length)   (void) (memset(buf, 0, length))


#if (NXT_HAVE_EXPLICIT_BZERO)
#define nxt_explicit_memzero(buf, length)                                     \
    explicit_bzero(buf, length)
#elif (NXT_HAVE_EXPLICIT_MEMSET)
#define nxt_explicit_memzero(buf, length)                                     \
    (void) (explicit_memset(buf, 0, length))
#else
nxt_inline void
nxt_explicit_memzero(u_char *buf, size_t length)
{
    volatile u_char  *p = (volatile u_char *) buf;

    while (length != 0) {
        *p++ = 0;
        length--;
    }
}
#endif


#define nxt_strstr_eq(s1, s2)                                                 \
    (((s1)->length == (s2)->length)                                           \
     && (memcmp((s1)->start, (s2)->start, (s1)->length) == 0))


#endif /* _NXT_STRING_H_INCLUDED_ */
