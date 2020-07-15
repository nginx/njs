
/*
 * Copyright (C) Alexander Borisov
 * Copyright (C) NGINX, Inc.
 */

#ifndef _NJS_UTF16_H_INCLUDED_
#define _NJS_UTF16_H_INCLUDED_


NJS_EXPORT ssize_t njs_utf16_encode(uint32_t cp, u_char **start,
    const u_char *end);
NJS_EXPORT uint32_t njs_utf16_decode(njs_unicode_decode_t *ctx,
    const u_char **start, const u_char *end);


njs_inline void
njs_utf16_decode_init(njs_unicode_decode_t *ctx)
{
    ctx->upper = 0x00;
    ctx->codepoint = 0x00;
}


#endif /* _NJS_UTF16_H_INCLUDED_ */
