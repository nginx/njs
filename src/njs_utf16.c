
/*
 * Copyright (C) Alexander Borisov
 * Copyright (C) NGINX, Inc.
 */


#include <njs_main.h>


njs_inline void
njs_utf16_encode_write(uint32_t cp, u_char **start)
{
#ifdef NJS_HAVE_BIG_ENDIAN
        *(*start)++ = cp >> 8;
        *(*start)++ = cp & 0x00FF;
#else
        *(*start)++ = cp & 0x00FF;
        *(*start)++ = cp >> 8;
#endif
}


ssize_t
njs_utf16_encode(uint32_t cp, u_char **start, const u_char *end)
{
    if ((*start + 2) > end) {
        return NJS_ERROR;
    }

    if (cp < 0x10000) {
        njs_utf16_encode_write(cp, start);

        return 2;
    }

    if ((*start + 4) > end) {
        return NJS_ERROR;
    }

    cp -= 0x10000;

    njs_utf16_encode_write((0xD800 | (cp >> 0x0A)), start);
    njs_utf16_encode_write((0xDC00 | (cp & 0x03FF)), start);

    return 4;
}


uint32_t
njs_utf16_decode(njs_unicode_decode_t *ctx, const u_char **start,
    const u_char *end)
{
    uint32_t  unit;
    unsigned  lead;

    if (ctx->upper != 0x00) {
        lead = ctx->upper - 0x01;
        ctx->upper = 0x00;

        goto lead_state;
    }

pair_state:

    lead = *(*start)++;

    if (*start >= end) {
        ctx->upper = lead + 0x01;
        return NJS_UNICODE_CONTINUE;
    }

lead_state:

#ifdef NJS_HAVE_BIG_ENDIAN
        unit = (lead << 8) + *(*start)++;
#else
        unit = (*(*start)++ << 8) + lead;
#endif

    if (ctx->codepoint != 0x00) {
        if (njs_surrogate_trailing(unit)) {
            unit = njs_surrogate_pair(ctx->codepoint, unit);

            ctx->codepoint = 0x00;

            return unit;
        }

        (*start)--;

        ctx->upper = lead + 0x01;
        ctx->codepoint = 0x00;

        return NJS_UNICODE_ERROR;
    }

    if (njs_surrogate_any(unit)) {
        if (njs_surrogate_trailing(unit)) {
            return NJS_UNICODE_ERROR;
        }

        ctx->codepoint = unit;

        if (*start >= end) {
            return NJS_UNICODE_CONTINUE;
        }

        goto pair_state;
    }

    return unit;
}
