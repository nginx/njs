
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) NGINX, Inc.
 */


#include <njs_main.h>

/*
 * The njs_unicode_lower_case.h and njs_unicode_upper_case.h files are
 * auto-generated from the UnicodeData.txt file version 14.0.0 (May 2021)
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


njs_inline njs_int_t
njs_utf8_boundary(njs_unicode_decode_t *ctx, const u_char **data,
    unsigned *need, u_char lower, u_char upper)
{
    u_char  ch;

    ch = **data;

    if (ch < lower || ch > upper) {
        return NJS_ERROR;
    }

    (*data)++;
    (*need)--;
    ctx->codepoint = (ctx->codepoint << 6) | (ch & 0x3F);

    return NJS_OK;
}


njs_inline void
njs_utf8_boundary_set(njs_unicode_decode_t *ctx, const u_char ch,
    u_char first, u_char second, u_char lower, u_char upper)
{
    if (ch == first) {
        ctx->lower = lower;
        ctx->upper = 0xBF;

    } else if (ch == second) {
        ctx->lower = 0x80;
        ctx->upper = upper;
    }
}


uint32_t
njs_utf8_decode(njs_unicode_decode_t *ctx, const u_char **start,
    const u_char *end)
{
    u_char        c;
    unsigned      need;
    njs_int_t     ret;
    const u_char  *p;

    if (ctx->need != 0) {
        need = ctx->need;
        ctx->need = 0;

        if (ctx->lower != 0x00) {
            ret = njs_utf8_boundary(ctx, start, &need, ctx->lower, ctx->upper);
            if (njs_slow_path(ret != NJS_OK)) {
                goto failed;
            }

            ctx->lower = 0x00;
        }

        goto decode;
    }

    c = *(*start)++;

    if (c < 0x80) {
        return c;

    } else if (c <= 0xDF) {
        if (c < 0xC2) {
            return NJS_UNICODE_ERROR;
        }

        need = 1;
        ctx->codepoint = c & 0x1F;

    } else if (c < 0xF0) {
        need = 2;
        ctx->codepoint = c & 0x0F;

        if (*start == end) {
            njs_utf8_boundary_set(ctx, c, 0xE0, 0xED, 0xA0, 0x9F);
            goto next;
        }

        ret = NJS_OK;

        if (c == 0xE0) {
            ret = njs_utf8_boundary(ctx, start, &need, 0xA0, 0xBF);

        } else if (c == 0xED) {
            ret = njs_utf8_boundary(ctx, start, &need, 0x80, 0x9F);
        }

        if (njs_slow_path(ret != NJS_OK)) {
            goto failed;
        }

    } else if (c < 0xF5) {
        need = 3;
        ctx->codepoint = c & 0x07;

        if (*start == end) {
            njs_utf8_boundary_set(ctx, c, 0xF0, 0xF4, 0x90, 0x8F);
            goto next;
        }

        ret = NJS_OK;

        if (c == 0xF0) {
            ret = njs_utf8_boundary(ctx, start, &need, 0x90, 0xBF);

        } else if (c == 0xF4) {
            ret = njs_utf8_boundary(ctx, start, &need, 0x80, 0x8F);
        }

        if (njs_slow_path(ret != NJS_OK)) {
            goto failed;
        }

    } else {
        return NJS_UNICODE_ERROR;
    }

decode:

    for (p = *start; p < end; p++) {
        c = *p;

        if (c < 0x80 || c > 0xBF) {
            *start = p;

            goto failed;
        }

        ctx->codepoint = (ctx->codepoint << 6) | (c & 0x3F);

        if (--need == 0) {
            *start = p + 1;

            return ctx->codepoint;
        }
    }

    *start = p;

next:

    ctx->need = need;

    return NJS_UNICODE_CONTINUE;

failed:

    ctx->lower = 0x00;
    ctx->need = 0;

    return NJS_UNICODE_ERROR;
}


u_char *
njs_utf8_stream_encode(njs_unicode_decode_t *ctx, const u_char *start,
    const u_char *end, u_char *dst, njs_bool_t last, njs_bool_t fatal)
{
    uint32_t  cp;

    while (start < end) {
        cp = njs_utf8_decode(ctx, &start, end);

        if (cp > NJS_UNICODE_MAX_CODEPOINT) {
            if (cp == NJS_UNICODE_CONTINUE) {
                break;
            }

            if (fatal) {
                return NULL;
            }

            cp = NJS_UNICODE_REPLACEMENT;
        }

        dst = njs_utf8_encode(dst, cp);
    }

    if (last && ctx->need != 0x00) {
        if (fatal) {
            return NULL;
        }

        dst = njs_utf8_encode(dst, NJS_UNICODE_REPLACEMENT);
    }

    return dst;
}


/*
 * njs_utf8_casecmp() tests only up to the minimum of given lengths, but
 * requires lengths of both strings because otherwise njs_utf8_decode()
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
            return NJS_UNICODE_ERROR;
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
    uint32_t              u;
    const uint32_t        *block;
    njs_unicode_decode_t  ctx;

    u = (uint32_t) **start;

    if (njs_fast_path(u < 0x80)) {
        (*start)++;

        return njs_unicode_lower_case_block_000[u];
    }

    njs_utf8_decode_init(&ctx);

    u = njs_utf8_decode(&ctx, start, end);

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
    uint32_t              u;
    const uint32_t        *block;
    njs_unicode_decode_t  ctx;

    u = (uint32_t) **start;

    if (njs_fast_path(u < 0x80)) {
        (*start)++;

        return njs_unicode_upper_case_block_000[u];
    }

    njs_utf8_decode_init(&ctx);

    u = njs_utf8_decode(&ctx, start, end);

    if (u <= NJS_UNICODE_MAX_UPPER_CASE) {
        block = njs_unicode_upper_case_blocks[u / NJS_UNICODE_BLOCK_SIZE];

        if (block != NULL) {
            return block[u % NJS_UNICODE_BLOCK_SIZE];
        }
    }

    return u;
}


ssize_t
njs_utf8_stream_length(njs_unicode_decode_t *ctx, const u_char *p, size_t len,
    njs_bool_t last, njs_bool_t fatal, size_t *out_size)
{
    size_t        size, length;
    uint32_t      codepoint;
    const u_char  *end;

    size = 0;
    length = 0;

    end = p + len;

    while (p < end) {
        codepoint = njs_utf8_decode(ctx, &p, end);

        if (codepoint > NJS_UNICODE_MAX_CODEPOINT) {
            if (codepoint == NJS_UNICODE_CONTINUE) {
                break;
            }

            if (fatal) {
                return -1;
            }

            codepoint = NJS_UNICODE_REPLACEMENT;
        }

        size += njs_utf8_size(codepoint);
        length++;
    }

    if (last && ctx->need != 0x00) {
        if (fatal) {
            return -1;
        }

        size += njs_utf8_size(NJS_UNICODE_REPLACEMENT);
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
    const u_char          *end;
    njs_unicode_decode_t  ctx;

    end = p + len;

    njs_utf8_decode_init(&ctx);

    while (p < end) {
        if (njs_slow_path(njs_utf8_decode(&ctx, &p, end)
                          > NJS_UNICODE_MAX_CODEPOINT))
        {
            return 0;
        }
    }

    return 1;
}
