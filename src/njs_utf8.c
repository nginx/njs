
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) NGINX, Inc.
 */


#include <njs_main.h>

/*
 * The njs_unicode_lower_case.h and njs_unicode_upper_case.h files are
 * files auto-generated from the UnicodeData.txt file version 12.1.0 (May 2019)
 * provided by Unicode, Inc.:
 *
 *   ./njs_unicode_lower_case.pl UnicodeData.txt
 *   ./njs_unicode_upper_case.pl UnicodeData.txt
 *
 * Only common and simple case foldings are supported.  Full case foldings
 * are not supported.  Combined characters are also not supported.
 */

#include <njs_unicode_lower_case.h>
#include <njs_unicode_upper_case.h>


u_char *
njs_utf8_encode(u_char *p, uint32_t u)
{
    if (u < 0x80) {
        *p++ = (u_char) (u & 0xFF);
        return p;
    }

    if (u < 0x0800) {
        *p++ = (u_char) (( u >> 6)          | 0xC0);
        *p++ = (u_char) (( u        & 0x3F) | 0x80);
        return p;
    }

    if (u < 0x10000) {
        *p++ = (u_char) ( (u >> 12)         | 0xE0);
        *p++ = (u_char) (((u >>  6) & 0x3F) | 0x80);
        *p++ = (u_char) (( u        & 0x3F) | 0x80);
        return p;
    }

    if (u < 0x110000) {
        *p++ = (u_char) ( (u >> 18)         | 0xF0);
        *p++ = (u_char) (((u >> 12) & 0x3F) | 0x80);
        *p++ = (u_char) (((u >>  6) & 0x3F) | 0x80);
        *p++ = (u_char) (( u        & 0x3F) | 0x80);
        return p;
    }

    return NULL;
}


/*
 * njs_utf8_decode() decodes UTF-8 sequences and returns a valid
 * character 0x00 - 0x10FFFF, or 0xFFFFFFFF for invalid or overlong
 * UTF-8 sequence.
 */

uint32_t
njs_utf8_decode(const u_char **start, const u_char *end)
{
    uint32_t  u;

    u = (uint32_t) **start;

    if (u < 0x80) {
        (*start)++;
        return u;
    }

    return njs_utf8_decode2(start, end);
}


/*
 * njs_utf8_decode2() decodes two and more bytes UTF-8 sequences only
 * and returns a valid character 0x80 - 0x10FFFF, OR 0xFFFFFFFF for
 * invalid or overlong UTF-8 sequence.
 */

uint32_t
njs_utf8_decode2(const u_char **start, const u_char *end)
{
    u_char        c;
    size_t        n;
    uint32_t      u, overlong;
    const u_char  *p;

    p = *start;
    u = (uint32_t) *p;

    if (u >= 0xE0) {

        if (u >= 0xF0) {

            if (njs_slow_path(u > 0xF4)) {
                /*
                 * The maximum valid Unicode character is 0x10FFFF
                 * which is encoded as 0xF4 0x8F 0xBF 0xBF.
                 */
                return 0xFFFFFFFF;
            }

            u &= 0x07;
            overlong = 0x00FFFF;
            n = 3;

        } else {
            u &= 0x0F;
            overlong = 0x07FF;
            n = 2;
        }

    } else if (u >= 0xC2) {

        /* 0x80 is encoded as 0xC2 0x80. */

        u &= 0x1F;
        overlong = 0x007F;
        n = 1;

    } else {
        /* u <= 0xC2 */
        return 0xFFFFFFFF;
    }

    p++;

    if (njs_fast_path(p + n <= end)) {

        do {
            c = *p++;
            /*
             * The byte must in the 0x80 - 0xBF range.
             * Values below 0x80 become >= 0x80.
             */
            c = c - 0x80;

            if (njs_slow_path(c > 0x3F)) {
                return 0xFFFFFFFF;
            }

            u = (u << 6) | c;
            n--;

        } while (n != 0);

        if (overlong < u && u < 0x110000) {
            *start = p;
            return u;
        }
    }

    return 0xFFFFFFFF;
}


uint32_t
njs_utf8_safe_decode(const u_char **start, const u_char *end)
{
    uint32_t  u;

    u = (uint32_t) **start;

    if (u < 0x80) {
        (*start)++;
        return u;
    }

    return njs_utf8_safe_decode2(start, end);
}


uint32_t
njs_utf8_safe_decode2(const u_char **start, const u_char *end)
{
    u_char        c;
    size_t        n;
    uint32_t      u, overlong;
    const u_char  *p;

    p = *start;
    u = (uint32_t) *p;

    if (u >= 0xE0) {

        if (u >= 0xF0) {

            if (njs_slow_path(u > 0xF4)) {
                /*
                 * The maximum valid Unicode character is 0x10FFFF
                 * which is encoded as 0xF4 0x8F 0xBF 0xBF.
                 */
                goto fail_one;
            }

            u &= 0x07;
            overlong = 0x00FFFF;
            n = 3;

        } else {
            u &= 0x0F;
            overlong = 0x07FF;
            n = 2;
        }

    } else if (u >= 0xC2) {

        /* 0x80 is encoded as 0xC2 0x80. */

        u &= 0x1F;
        overlong = 0x007F;
        n = 1;

    } else {
        /* u <= 0xC2 */
        goto fail_one;
    }

    p++;

    while (p < end && n != 0) {
        c = *p++;
        /*
         * The byte must in the 0x80 - 0xBF range.
         * Values below 0x80 become >= 0x80.
         */
        c = c - 0x80;

        if (njs_slow_path(c > 0x3F)) {
            *start = --p;
            return NJS_UTF8_REPLACEMENT;
        }

        u = (u << 6) | c;
        n--;
    }

    *start = p;

    if (n == 0 && overlong < u && u < 0x110000) {
        return u;
    }

    return NJS_UTF8_REPLACEMENT;

fail_one:

    (*start)++;

    return NJS_UTF8_REPLACEMENT;
}


/*
 * njs_utf8_casecmp() tests only up to the minimum of given lengths, but
 * requires lengths of both strings because otherwise njs_utf8_decode2()
 * may fail due to incomplete sequence.
 */

njs_int_t
njs_utf8_casecmp(const u_char *start1, const u_char *start2, size_t len1,
    size_t len2)
{
    int32_t       n;
    uint32_t      u1, u2;
    const u_char  *end1, *end2;

    end1 = start1 + len1;
    end2 = start2 + len2;

    while (start1 < end1 && start2 < end2) {

        u1 = njs_utf8_lower_case(&start1, end1);

        u2 = njs_utf8_lower_case(&start2, end2);

        if (njs_slow_path((u1 | u2) == 0xFFFFFFFF)) {
            return NJS_UTF8_SORT_INVALID;
        }

        n = u1 - u2;

        if (n != 0) {
            return (njs_int_t) n;
        }
    }

    return 0;
}


uint32_t
njs_utf8_lower_case(const u_char **start, const u_char *end)
{
    uint32_t        u;
    const uint32_t  *block;

    u = (uint32_t) **start;

    if (njs_fast_path(u < 0x80)) {
        (*start)++;

        return njs_unicode_lower_case_block_000[u];
    }

    u = njs_utf8_decode2(start, end);

    if (u <= NJS_UNICODE_MAX_LOWER_CASE) {
        block = njs_unicode_lower_case_blocks[u / NJS_UNICODE_BLOCK_SIZE];

        if (block != NULL) {
            return block[u % NJS_UNICODE_BLOCK_SIZE];
        }
    }

    return u;
}


uint32_t
njs_utf8_upper_case(const u_char **start, const u_char *end)
{
    uint32_t        u;
    const uint32_t  *block;

    u = (uint32_t) **start;

    if (njs_fast_path(u < 0x80)) {
        (*start)++;

        return njs_unicode_upper_case_block_000[u];
    }

    u = njs_utf8_decode2(start, end);

    if (u <= NJS_UNICODE_MAX_UPPER_CASE) {
        block = njs_unicode_upper_case_blocks[u / NJS_UNICODE_BLOCK_SIZE];

        if (block != NULL) {
            return block[u % NJS_UNICODE_BLOCK_SIZE];
        }
    }

    return u;
}


ssize_t
njs_utf8_length(const u_char *p, size_t len)
{
    ssize_t       length;
    const u_char  *end;

    length = 0;

    end = p + len;

    while (p < end) {
        if (njs_slow_path(njs_utf8_decode(&p, end) == 0xffffffff)) {
            return -1;
        }

        length++;
    }

    return length;
}


ssize_t
njs_utf8_safe_length(const u_char *p, size_t len, ssize_t *out_size)
{
    ssize_t       size, length;
    uint32_t      codepoint;
    const u_char  *end;

    size = 0;
    length = 0;

    end = p + len;

    while (p < end) {
        codepoint = njs_utf8_safe_decode(&p, end);

        size += njs_utf8_size(codepoint);

        length++;
    }

    if (out_size != NULL) {
        *out_size = size;
    }

    return length;
}


njs_bool_t
njs_utf8_is_valid(const u_char *p, size_t len)
{
    const u_char  *end;

    end = p + len;

    while (p < end) {
        if (njs_slow_path(njs_utf8_decode(&p, end) == 0xffffffff)) {
            return 0;
        }
    }

    return 1;
}
