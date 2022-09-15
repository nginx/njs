
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) NGINX, Inc.
 */

#ifndef _NJS_STR_H_INCLUDED_
#define _NJS_STR_H_INCLUDED_


typedef struct {
    size_t  length;
    u_char  *start;
} njs_str_t;


/*
 * C99 allows to assign struct as compound literal with struct name cast only.
 * SunC however issues error on the cast in struct static initialization:
 *   non-constant initializer: op "NAME"
 * So a separate njs_str_value() macro is intended to use in assignment.
 */

#define njs_length(s)        (sizeof(s) - 1)
#define njs_str(s)           { njs_length(s), (u_char *) s }
#define njs_null_str         { 0, NULL }
#define njs_str_value(s)     (njs_str_t) njs_str(s)


njs_inline u_char
njs_lower_case(u_char c)
{
    return (u_char) ((c >= 'A' && c <= 'Z') ? c | 0x20 : c);
}


njs_inline u_char
njs_upper_case(u_char c)
{
    return (u_char) ((c >= 'a' && c <= 'z') ? c & 0xDF : c);
}


njs_inline njs_bool_t
njs_is_whitespace(u_char c)
{
    switch (c) {
    case 0x09:  /* <TAB>  */
    case 0x0A:  /* <LF>   */
    case 0x0B:  /* <VT>   */
    case 0x0C:  /* <FF>   */
    case 0x0D:  /* <CR>   */
    case 0x20:  /* <SP>   */
        return 1;

    default:
        return 0;
    }
}


njs_inline u_char *
njs_strlchr(u_char *p, u_char *last, u_char c)
{
    while (p < last) {

        if (*p == c) {
            return p;
        }

        p++;
    }

    return NULL;
}


#define                                                                       \
njs_strlen(s)                                                                 \
    strlen((char *) s)


#define                                                                       \
njs_cpymem(dst, src, n)                                                       \
    (((u_char *) memcpy(dst, src, n)) + (n))


#define                                                                       \
njs_strncmp(s1, s2, n)                                                        \
    strncmp((char *) s1, (char *) s2, n)


#define                                                                       \
njs_strchr(s1, c)                                                             \
    (u_char *) strchr((const char *) s1, (int) c)


#define                                                                       \
njs_memset(buf, c, length)                                                    \
    (void) memset(buf, c, length)


#define                                                                       \
njs_memzero(buf, length)                                                      \
    (void) memset(buf, 0, length)


#if (NJS_HAVE_EXPLICIT_BZERO && !NJS_HAVE_MEMORY_SANITIZER)
#define                                                                       \
njs_explicit_memzero(buf, length)                                             \
    explicit_bzero(buf, length)
#elif (NJS_HAVE_EXPLICIT_MEMSET)
#define                                                                       \
njs_explicit_memzero(buf, length)                                             \
    (void) explicit_memset(buf, 0, length)
#else
njs_inline void
njs_explicit_memzero(void *buf, size_t length)
{
    volatile u_char  *p = (volatile u_char *) buf;

    while (length != 0) {
        *p++ = 0;
        length--;
    }
}
#endif


#define                                                                       \
njs_strstr_eq(s1, s2)                                                         \
    (((s1)->length == (s2)->length)                                           \
     && (memcmp((s1)->start, (s2)->start, (s1)->length) == 0))


#define                                                                       \
njs_strstr_case_eq(s1, s2)                                                    \
    (((s1)->length == (s2)->length)                                           \
     && (njs_strncasecmp((s1)->start, (s2)->start, (s1)->length) == 0))


NJS_EXPORT njs_int_t njs_strncasecmp(u_char *s1, u_char *s2, size_t n);


#endif /* _NJS_STR_H_INCLUDED_ */
