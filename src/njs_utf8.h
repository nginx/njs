
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) NGINX, Inc.
 */

#ifndef _NJS_UTF8_H_INCLUDED_
#define _NJS_UTF8_H_INCLUDED_


NJS_EXPORT uint32_t njs_utf8_decode(njs_unicode_decode_t *ctx,
    const u_char **data, const u_char *end);
NJS_EXPORT u_char *njs_utf8_encode(u_char *p, uint32_t u);
NJS_EXPORT u_char *njs_utf8_stream_encode(njs_unicode_decode_t *ctx,
    const u_char *start, const u_char *end, u_char *dst, njs_bool_t last,
    njs_bool_t fatal);
NJS_EXPORT njs_int_t njs_utf8_casecmp(const u_char *start1,
    const u_char *start2, size_t len1, size_t len2);
NJS_EXPORT uint32_t njs_utf8_lower_case(const u_char **start,
    const u_char *end);
NJS_EXPORT uint32_t njs_utf8_upper_case(const u_char **start,
    const u_char *end);
NJS_EXPORT ssize_t njs_utf8_stream_length(njs_unicode_decode_t *ctx,
    const u_char *p, size_t len, njs_bool_t last, njs_bool_t fatal,
    size_t *out_size);
NJS_EXPORT njs_bool_t njs_utf8_is_valid(const u_char *p, size_t len);


njs_inline uint32_t
njs_utf8_consume(njs_unicode_decode_t *ctx, u_char byte)
{
    const u_char  *p;

    p = &byte;

    return njs_utf8_decode(ctx, &p, p + 1);
}


/*
 * njs_utf8_next() and njs_utf8_prev() expect a valid UTF-8 string.
 *
 * The leading UTF-8 byte is either 0xxxxxxx or 11xxxxxx.
 * The continuation UTF-8 bytes are 10xxxxxx.
 */

njs_inline const u_char *
njs_utf8_next(const u_char *p, const u_char *end)
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


njs_inline const u_char *
njs_utf8_prev(const u_char *p)
{
   u_char  c;

   do {
       p--;
       c = *p;

   } while ((c & 0xC0) == 0x80);

   return p;
}


njs_inline u_char *
njs_utf8_copy(u_char *dst, const u_char **src, const u_char *end)
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


njs_inline void
njs_utf8_decode_init(njs_unicode_decode_t *ctx)
{
    ctx->need = 0x00;
    ctx->lower = 0x00;
}


njs_inline ssize_t
njs_utf8_length(const u_char *p, size_t len)
{
    njs_unicode_decode_t  ctx;

    njs_utf8_decode_init(&ctx);

    return njs_utf8_stream_length(&ctx, p, len, 1, 1, NULL);
}


njs_inline size_t
njs_utf8_bom(const u_char *start, const u_char *end)
{
    if (start + 3 > end) {
        return 0;
    }

    if (start[0] == 0xEF && start[1] == 0xBB && start[2] == 0xBF) {
        return 3;
    }

    return 0;
}


njs_inline size_t
njs_utf8_size(uint32_t cp)
{
    return (cp < 0x80) ? 1 : ((cp < 0x0800) ? 2 : ((cp < 0x10000) ? 3 : 4));
}


njs_inline size_t
njs_utf8_size_uint16(uint32_t cp)
{
    return ((cp < 0x80) ? 1 : ((cp < 0x0800) ? 2 : 3));
}


njs_inline njs_bool_t
njs_utf8_is_whitespace(uint32_t c)
{
    switch (c) {
    case 0x0009:  /* <TAB>  */
    case 0x000A:  /* <LF>   */
    case 0x000B:  /* <VT>   */
    case 0x000C:  /* <FF>   */
    case 0x000D:  /* <CR>   */
    case 0x0020:  /* <SP>   */
    case 0x00A0:  /* <NBSP> */
    case 0x1680:
    case 0x2000:
    case 0x2001:
    case 0x2002:
    case 0x2003:
    case 0x2004:
    case 0x2005:
    case 0x2006:
    case 0x2007:
    case 0x2008:
    case 0x2009:
    case 0x200A:
    case 0x2028:  /* <LS>   */
    case 0x2029:  /* <PS>   */
    case 0x202F:
    case 0x205F:
    case 0x3000:
    case 0xFEFF:  /* <BOM>  */
        return 1;

    default:
        return 0;
    }
}


#endif /* _NJS_UTF8_H_INCLUDED_ */
