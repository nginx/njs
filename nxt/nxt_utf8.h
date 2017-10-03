
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) NGINX, Inc.
 */

#ifndef _NXT_UTF8_H_INCLUDED_
#define _NXT_UTF8_H_INCLUDED_


/*
 * Since the maximum valid Unicode character is 0x0010FFFF, the maximum
 * difference between Unicode characters is lesser 0x0010FFFF and
 * 0x0EEE0EEE can be used as value to indicate UTF-8 encoding error.
 */
#define NXT_UTF8_SORT_INVALID  0x0EEE0EEE


NXT_EXPORT u_char *nxt_utf8_encode(u_char *p, uint32_t u);
NXT_EXPORT uint32_t nxt_utf8_decode(const u_char **start, const u_char *end);
NXT_EXPORT uint32_t nxt_utf8_decode2(const u_char **start, const u_char *end);
NXT_EXPORT nxt_int_t nxt_utf8_casecmp(const u_char *start1,
    const u_char *start2, size_t len1, size_t len2);
NXT_EXPORT uint32_t nxt_utf8_lower_case(const u_char **start,
    const u_char *end);
NXT_EXPORT uint32_t nxt_utf8_upper_case(const u_char **start,
    const u_char *end);
NXT_EXPORT ssize_t nxt_utf8_length(const u_char *p, size_t len);
NXT_EXPORT nxt_bool_t nxt_utf8_is_valid(const u_char *p, size_t len);


/*
 * nxt_utf8_next() and nxt_utf8_prev() expect a valid UTF-8 string.
 *
 * The leading UTF-8 byte is either 0xxxxxxx or 11xxxxxx.
 * The continuation UTF-8 bytes are 10xxxxxx.
 */

nxt_inline const u_char *
nxt_utf8_next(const u_char *p, const u_char *end)
{
    u_char  c;

    c = *p++;

    if ((c & 0x80) != 0) {

        do {
            c = *p;

            if ((c & 0xC0) != 0x80) {
                return p;
            }

            p++;

        } while (p < end);
    }

    return p;
}


nxt_inline const u_char *
nxt_utf8_prev(const u_char *p)
{
   u_char  c;

   do {
       p--;
       c = *p;

   } while ((c & 0xC0) == 0x80);

   return p;
}


nxt_inline u_char *
nxt_utf8_copy(u_char *dst, const u_char **src, const u_char *end)
{
    u_char        c;
    const u_char  *p;

    p = *src;
    c = *p++;
    *dst++ = c;

    if ((c & 0x80) != 0) {

        do {
            c = *p;

            if ((c & 0xC0) != 0x80) {
                break;
            }

            *dst++ = c;
            p++;

        } while (p < end);
    }

    *src = p;
    return dst;
}


#define nxt_utf8_size(u)                                                      \
    ((u < 0x80) ? 1 : ((u < 0x0800) ? 2 : ((u < 0x10000) ? 3 : 4)))


#endif /* _NXT_UTF8_H_INCLUDED_ */
