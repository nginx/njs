
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) NGINX, Inc.
 */


#include <njs_main.h>


#define NJS_UTF8_START_TEST  0xC2


static u_char  invalid[] = {

    /* Invalid first byte less than 0xC2. */
    1, 0x80, 0x00, 0x00, 0x00,
    1, 0xC0, 0x00, 0x00, 0x00,
    2, 0xC0, 0x00, 0x00, 0x00,
    3, 0xC0, 0x00, 0x00, 0x00,
    4, 0xC0, 0x00, 0x00, 0x00,

    /* Invalid 0x0x110000 value. */
    4, 0xF4, 0x90, 0x80, 0x80,

    /* Incomplete length. */
    2, 0xE0, 0xAF, 0xB5, 0x00,

    /* Overlong values. */
    2, 0xC0, 0x80, 0x00, 0x00,
    2, 0xC1, 0xB3, 0x00, 0x00,
    3, 0xE0, 0x80, 0x80, 0x00,
    3, 0xE0, 0x81, 0xB3, 0x00,
    3, 0xE0, 0x90, 0x9A, 0x00,
    4, 0xF0, 0x80, 0x8A, 0x80,
    4, 0xF0, 0x80, 0x81, 0xB3,
    4, 0xF0, 0x80, 0xAF, 0xB5,
};


static njs_int_t
utf8_overlong(u_char *overlong, size_t len)
{
    u_char                *p, utf8[4];
    size_t                size;
    uint32_t              u, d;
    njs_uint_t            i;
    const u_char          *pp;
    njs_unicode_decode_t  ctx;

    njs_utf8_decode_init(&ctx);

    pp = overlong;

    d = njs_utf8_decode(&ctx, &pp, overlong + len);

    len = pp - overlong;

    if (d != 0xFFFFFFFF) {
        p = njs_utf8_encode(utf8, d);

        size = (p != NULL) ? p - utf8 : 0;

        if (len != size || memcmp(overlong, utf8, size) != 0) {

            u = 0;
            for (i = 0; i < len; i++) {
                u = (u << 8) + overlong[i];
            }

            njs_printf("njs_utf8_decode(%05uXD, %uz) failed: %05uXD, %uz\n",
                       u, len, d, size);

            return NJS_ERROR;
        }
    }

    return NJS_OK;
}


static njs_int_t
utf8_unit_test(njs_uint_t start)
{
    u_char                *p, utf8[4];
    size_t                len;
    int32_t               n;
    uint32_t              u, d;
    njs_uint_t            i, k, l, m;
    const u_char          *pp;
    njs_unicode_decode_t  ctx;

    njs_printf("utf8 test started\n");

    /* Test valid UTF-8. */

    for (u = 0; u <= NJS_UNICODE_MAX_CODEPOINT; u++) {

        p = njs_utf8_encode(utf8, u);

        if (p == NULL) {
            njs_printf("njs_utf8_encode(%05uXD) failed\n", u);
            return NJS_ERROR;
        }

        pp = utf8;

        njs_utf8_decode_init(&ctx);

        d = njs_utf8_decode(&ctx, &pp, p);

        /* In UTF-8 not allowed UTF-16 surrogate pair sequences. */

        if (u >= 0xD800 && u <= 0xDFFF) {
            if (d != NJS_UNICODE_ERROR) {
                njs_printf("njs_utf8_decode(%05uXD) failed for "
                           "surrogate pair: %05uxD\n", u, d);

                return NJS_ERROR;
            }

            continue;
        }

        if (u != d) {
            njs_printf("njs_utf8_decode(%05uXD) failed: %05uxD\n", u, d);
            return NJS_ERROR;
        }
    }

    /* Test some invalid UTF-8. */

    for (i = 0; i < sizeof(invalid); i += 5) {

        len = invalid[i];
        utf8[0] = invalid[i + 1];
        utf8[1] = invalid[i + 2];
        utf8[2] = invalid[i + 3];
        utf8[3] = invalid[i + 4];

        pp = utf8;

        njs_utf8_decode_init(&ctx);

        d = njs_utf8_decode(&ctx, &pp, utf8 + len);

        if (d <= NJS_UNICODE_MAX_CODEPOINT) {

            u = 0;
            for (i = 0; i < len; i++) {
                u = (u << 8) + utf8[i];
            }

            njs_printf("njs_utf8_decode(%05uXD, %uz) failed: %05uXD\n",
                       u, len, d);
            return NJS_ERROR;
        }
    }

    /* Test all overlong UTF-8. */

    for (i = start; i < 256; i++) {
        utf8[0] = i;

        if (utf8_overlong(utf8, 1) != NJS_OK) {
            return NJS_ERROR;
        }

        for (k = 0; k < 256; k++) {
            utf8[1] = k;

            if (utf8_overlong(utf8, 2) != NJS_OK) {
                return NJS_ERROR;
            }

            for (l = 0; l < 256; l++) {
                utf8[2] = l;

                if (utf8_overlong(utf8, 3) != NJS_OK) {
                    return NJS_ERROR;
                }

                for (m = 0; m < 256; m++) {
                    utf8[3] = m;

                    if (utf8_overlong(utf8, 4) != NJS_OK) {
                        return NJS_ERROR;
                    }
                }
            }
        }
    }

    n = njs_utf8_casecmp((u_char *) "ABC АБВ ΑΒΓ",
                         (u_char *) "abc абв αβγ",
                         njs_length("ABC АБВ ΑΒΓ"),
                         njs_length("abc абв αβγ"));

    if (n != 0) {
        njs_printf("njs_utf8_casecmp() failed\n");
        return NJS_ERROR;
    }

    njs_printf("utf8 test passed\n");
    return NJS_OK;
}


static njs_int_t
utf16_unit_test()
{
    int8_t                length, length_to;
    u_char                *start, *end, *end_to;
    uint32_t              cp, i;
    njs_unicode_decode_t  ctx;
    u_char                buf[8], to[4];

    njs_printf("utf16 test started\n");

    end = buf + sizeof(buf);
    end_to = to + sizeof(to);

    for (i = 0; i <= NJS_UNICODE_MAX_CODEPOINT; i++) {

        /* Skip surrogate pair. */

        if (i >= 0xD800 && i <= 0xDFFF) {
            continue;
        }

        start = buf;

        length = njs_utf16_encode(i, &start, end);
        if (length < NJS_OK) {
            njs_printf("utf16 test encode failed\n");
            return NJS_ERROR;
        }

        njs_utf16_decode_init(&ctx);

        start = buf;

        cp = njs_utf16_decode(&ctx, (const u_char **) &start, start + length);
        if (cp > NJS_UNICODE_MAX_CODEPOINT) {
            njs_printf("utf16 test decode failed\n");
            return NJS_ERROR;
        }

        if (cp != i) {
            njs_printf("utf16 test decode code point does not match\n");
            return NJS_ERROR;
        }

        start = to;

        length_to = njs_utf16_encode(cp, &start, end_to);
        if (length_to < NJS_OK) {
            njs_printf("utf16 test encode failed\n");
            return NJS_ERROR;
        }

        if (length_to != length || njs_strncmp(buf, to, length) != 0) {
            njs_printf("utf16 test decode-encode failed\n");
            return NJS_ERROR;
        }
    }

    /* Surrogate pair. */

    for (i = 0xD800; i <= 0xDFFF; i++) {
        start = buf;

        length = njs_utf16_encode(i, &start, end);
        if (length < NJS_OK) {
            njs_printf("utf16 test surrogate pair encode lead failed\n");
            return NJS_ERROR;
        }

        length_to = njs_utf16_encode(i - 0xD800 + 0xDC00, &start, end);
        if (length_to < NJS_OK) {
            njs_printf("utf16 test surrogate pair encode failed\n");
            return NJS_ERROR;
        }

        njs_utf16_decode_init(&ctx);

        start = buf;

        cp = njs_utf16_decode(&ctx, (const u_char **) &start,
                              start + length + length_to);
        if (cp > NJS_UNICODE_MAX_CODEPOINT) {
            if (i < 0xDC00) {
                njs_printf("utf16 test surrogate pair decode failed\n");
                return NJS_ERROR;
            }
        }
    }

    njs_printf("utf16 test passed\n");

    return NJS_OK;
}


int
main(int argc, char **argv)
{
    njs_int_t   ret;
    njs_uint_t  start;

    njs_printf("unicode unit test started\n");

    if (argc > 1 && argv[1][0] == 'a') {
        start = NJS_UTF8_START_TEST;

    } else {
        start = 256;
    }

    ret = utf8_unit_test(start);
    if (ret != NJS_OK) {
        return ret;
    }

    ret = utf16_unit_test();
    if (ret != NJS_OK) {
        return ret;
    }

    njs_printf("unicode unit test passed\n");

    return 0;
}
