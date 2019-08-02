
/*
 * Copyright (C) Dmitry Volyntsev
 * Copyright (C) NGINX, Inc.
 *
 * An internal SHA2 implementation.
 */


#include <njs_main.h>


static const u_char *njs_sha2_body(njs_sha2_t *ctx, const u_char *data,
    size_t size);


void
njs_sha2_init(njs_sha2_t *ctx)
{
    ctx->a = 0x6a09e667;
    ctx->b = 0xbb67ae85;
    ctx->c = 0x3c6ef372;
    ctx->d = 0xa54ff53a;
    ctx->e = 0x510e527f;
    ctx->f = 0x9b05688c;
    ctx->g = 0x1f83d9ab;
    ctx->h = 0x5be0cd19;

    ctx->bytes = 0;
}


void
njs_sha2_update(njs_sha2_t *ctx, const void *data, size_t size)
{
    size_t  used, free;

    used = (size_t) (ctx->bytes & 0x3f);
    ctx->bytes += size;

    if (used) {
        free = 64 - used;

        if (size < free) {
            memcpy(&ctx->buffer[used], data, size);
            return;
        }

        memcpy(&ctx->buffer[used], data, free);
        data = (u_char *) data + free;
        size -= free;
        (void) njs_sha2_body(ctx, ctx->buffer, 64);
    }

    if (size >= 64) {
        data = njs_sha2_body(ctx, data, size & ~(size_t) 0x3f);
        size &= 0x3f;
    }

    memcpy(ctx->buffer, data, size);
}


void
njs_sha2_final(u_char result[32], njs_sha2_t *ctx)
{
    size_t  used, free;

    used = (size_t) (ctx->bytes & 0x3f);

    ctx->buffer[used++] = 0x80;

    free = 64 - used;

    if (free < 8) {
        njs_memzero(&ctx->buffer[used], free);
        (void) njs_sha2_body(ctx, ctx->buffer, 64);
        used = 0;
        free = 64;
    }

    njs_memzero(&ctx->buffer[used], free - 8);

    ctx->bytes <<= 3;
    ctx->buffer[56] = (u_char) (ctx->bytes >> 56);
    ctx->buffer[57] = (u_char) (ctx->bytes >> 48);
    ctx->buffer[58] = (u_char) (ctx->bytes >> 40);
    ctx->buffer[59] = (u_char) (ctx->bytes >> 32);
    ctx->buffer[60] = (u_char) (ctx->bytes >> 24);
    ctx->buffer[61] = (u_char) (ctx->bytes >> 16);
    ctx->buffer[62] = (u_char) (ctx->bytes >> 8);
    ctx->buffer[63] = (u_char)  ctx->bytes;

    (void) njs_sha2_body(ctx, ctx->buffer, 64);

    result[0]  = (u_char) (ctx->a >> 24);
    result[1]  = (u_char) (ctx->a >> 16);
    result[2]  = (u_char) (ctx->a >> 8);
    result[3]  = (u_char)  ctx->a;
    result[4]  = (u_char) (ctx->b >> 24);
    result[5]  = (u_char) (ctx->b >> 16);
    result[6]  = (u_char) (ctx->b >> 8);
    result[7]  = (u_char)  ctx->b;
    result[8]  = (u_char) (ctx->c >> 24);
    result[9]  = (u_char) (ctx->c >> 16);
    result[10] = (u_char) (ctx->c >> 8);
    result[11] = (u_char)  ctx->c;
    result[12] = (u_char) (ctx->d >> 24);
    result[13] = (u_char) (ctx->d >> 16);
    result[14] = (u_char) (ctx->d >> 8);
    result[15] = (u_char)  ctx->d;
    result[16] = (u_char) (ctx->e >> 24);
    result[17] = (u_char) (ctx->e >> 16);
    result[18] = (u_char) (ctx->e >> 8);
    result[19] = (u_char)  ctx->e;
    result[20] = (u_char) (ctx->f >> 24);
    result[21] = (u_char) (ctx->f >> 16);
    result[22] = (u_char) (ctx->f >> 8);
    result[23] = (u_char)  ctx->f;
    result[24] = (u_char) (ctx->g >> 24);
    result[25] = (u_char) (ctx->g >> 16);
    result[26] = (u_char) (ctx->g >> 8);
    result[27] = (u_char)  ctx->g;
    result[28] = (u_char) (ctx->h >> 24);
    result[29] = (u_char) (ctx->h >> 16);
    result[30] = (u_char) (ctx->h >> 8);
    result[31] = (u_char)  ctx->h;

    njs_explicit_memzero(ctx, sizeof(*ctx));
}


/*
 * Helper functions.
 */

#define ROTATE(bits, word)  (((word) >> (bits)) | ((word) << (32 - (bits))))

#define S0(a) (ROTATE(2, a) ^ ROTATE(13, a) ^ ROTATE(22, a))
#define S1(e) (ROTATE(6, e) ^ ROTATE(11, e) ^ ROTATE(25, e))
#define CH(e, f, g) (((e) & (f)) ^ ((~(e)) & (g)))
#define MAJ(a, b, c) (((a) & (b)) ^ ((a) & (c)) ^ ((b) & (c)))

#define STEP(a, b, c, d, e, f, g, h, w, k)                                    \
    temp1 = (h) + S1(e) + CH(e, f, g) + (k) + (w);                            \
    temp2 = S0(a) + MAJ(a, b, c);                                             \
    (h) = (g);                                                                \
    (g) = (f);                                                                \
    (f) = (e);                                                                \
    (e) = (d) + temp1;                                                        \
    (d) = (c);                                                                \
    (c) = (b);                                                                \
    (b) = (a);                                                                \
    (a) = temp1 + temp2;


/*
 * GET() reads 4 input bytes in big-endian byte order and returns
 * them as uint32_t.
 */

#define GET(n)                                                                \
    (  ((uint32_t) p[n * 4 + 3])                                              \
     | ((uint32_t) p[n * 4 + 2] << 8)                                         \
     | ((uint32_t) p[n * 4 + 1] << 16)                                        \
     | ((uint32_t) p[n * 4]     << 24))


/*
 * This processes one or more 64-byte data blocks, but does not update
 * the bit counters.  There are no alignment requirements.
 */

static const u_char *
njs_sha2_body(njs_sha2_t *ctx, const u_char *data, size_t size)
{
    uint32_t       a, b, c, d, e, f, g, h, s0, s1, temp1, temp2;
    uint32_t       saved_a, saved_b, saved_c, saved_d, saved_e, saved_f,
                   saved_g, saved_h;
    uint32_t       words[64];
    njs_uint_t     i;
    const u_char  *p;

    p = data;

    a = ctx->a;
    b = ctx->b;
    c = ctx->c;
    d = ctx->d;
    e = ctx->e;
    f = ctx->f;
    g = ctx->g;
    h = ctx->h;

    do {
        saved_a = a;
        saved_b = b;
        saved_c = c;
        saved_d = d;
        saved_e = e;
        saved_f = f;
        saved_g = g;
        saved_h = h;

        /* Load data block into the words array */

        for (i = 0; i < 16; i++) {
            words[i] = GET(i);
        }

        for (i = 16; i < 64; i++) {
            s0 = ROTATE(7, words[i - 15])
                 ^ ROTATE(18, words[i - 15])
                 ^ (words[i - 15] >> 3);

            s1 = ROTATE(17, words[i - 2])
                 ^ ROTATE(19, words[i - 2])
                 ^ (words[i - 2] >> 10);

            words[i] = words[i - 16] + s0 + words[i - 7] + s1;
        }

        /* Transformations */

        STEP(a, b, c, d, e, f, g, h, words[0],  0x428a2f98);
        STEP(a, b, c, d, e, f, g, h, words[1],  0x71374491);
        STEP(a, b, c, d, e, f, g, h, words[2],  0xb5c0fbcf);
        STEP(a, b, c, d, e, f, g, h, words[3],  0xe9b5dba5);
        STEP(a, b, c, d, e, f, g, h, words[4],  0x3956c25b);
        STEP(a, b, c, d, e, f, g, h, words[5],  0x59f111f1);
        STEP(a, b, c, d, e, f, g, h, words[6],  0x923f82a4);
        STEP(a, b, c, d, e, f, g, h, words[7],  0xab1c5ed5);
        STEP(a, b, c, d, e, f, g, h, words[8],  0xd807aa98);
        STEP(a, b, c, d, e, f, g, h, words[9],  0x12835b01);
        STEP(a, b, c, d, e, f, g, h, words[10], 0x243185be);
        STEP(a, b, c, d, e, f, g, h, words[11], 0x550c7dc3);
        STEP(a, b, c, d, e, f, g, h, words[12], 0x72be5d74);
        STEP(a, b, c, d, e, f, g, h, words[13], 0x80deb1fe);
        STEP(a, b, c, d, e, f, g, h, words[14], 0x9bdc06a7);
        STEP(a, b, c, d, e, f, g, h, words[15], 0xc19bf174);

        STEP(a, b, c, d, e, f, g, h, words[16], 0xe49b69c1);
        STEP(a, b, c, d, e, f, g, h, words[17], 0xefbe4786);
        STEP(a, b, c, d, e, f, g, h, words[18], 0x0fc19dc6);
        STEP(a, b, c, d, e, f, g, h, words[19], 0x240ca1cc);
        STEP(a, b, c, d, e, f, g, h, words[20], 0x2de92c6f);
        STEP(a, b, c, d, e, f, g, h, words[21], 0x4a7484aa);
        STEP(a, b, c, d, e, f, g, h, words[22], 0x5cb0a9dc);
        STEP(a, b, c, d, e, f, g, h, words[23], 0x76f988da);
        STEP(a, b, c, d, e, f, g, h, words[24], 0x983e5152);
        STEP(a, b, c, d, e, f, g, h, words[25], 0xa831c66d);
        STEP(a, b, c, d, e, f, g, h, words[26], 0xb00327c8);
        STEP(a, b, c, d, e, f, g, h, words[27], 0xbf597fc7);
        STEP(a, b, c, d, e, f, g, h, words[28], 0xc6e00bf3);
        STEP(a, b, c, d, e, f, g, h, words[29], 0xd5a79147);
        STEP(a, b, c, d, e, f, g, h, words[30], 0x06ca6351);
        STEP(a, b, c, d, e, f, g, h, words[31], 0x14292967);

        STEP(a, b, c, d, e, f, g, h, words[32], 0x27b70a85);
        STEP(a, b, c, d, e, f, g, h, words[33], 0x2e1b2138);
        STEP(a, b, c, d, e, f, g, h, words[34], 0x4d2c6dfc);
        STEP(a, b, c, d, e, f, g, h, words[35], 0x53380d13);
        STEP(a, b, c, d, e, f, g, h, words[36], 0x650a7354);
        STEP(a, b, c, d, e, f, g, h, words[37], 0x766a0abb);
        STEP(a, b, c, d, e, f, g, h, words[38], 0x81c2c92e);
        STEP(a, b, c, d, e, f, g, h, words[39], 0x92722c85);
        STEP(a, b, c, d, e, f, g, h, words[40], 0xa2bfe8a1);
        STEP(a, b, c, d, e, f, g, h, words[41], 0xa81a664b);
        STEP(a, b, c, d, e, f, g, h, words[42], 0xc24b8b70);
        STEP(a, b, c, d, e, f, g, h, words[43], 0xc76c51a3);
        STEP(a, b, c, d, e, f, g, h, words[44], 0xd192e819);
        STEP(a, b, c, d, e, f, g, h, words[45], 0xd6990624);
        STEP(a, b, c, d, e, f, g, h, words[46], 0xf40e3585);
        STEP(a, b, c, d, e, f, g, h, words[47], 0x106aa070);

        STEP(a, b, c, d, e, f, g, h, words[48], 0x19a4c116);
        STEP(a, b, c, d, e, f, g, h, words[49], 0x1e376c08);
        STEP(a, b, c, d, e, f, g, h, words[50], 0x2748774c);
        STEP(a, b, c, d, e, f, g, h, words[51], 0x34b0bcb5);
        STEP(a, b, c, d, e, f, g, h, words[52], 0x391c0cb3);
        STEP(a, b, c, d, e, f, g, h, words[53], 0x4ed8aa4a);
        STEP(a, b, c, d, e, f, g, h, words[54], 0x5b9cca4f);
        STEP(a, b, c, d, e, f, g, h, words[55], 0x682e6ff3);
        STEP(a, b, c, d, e, f, g, h, words[56], 0x748f82ee);
        STEP(a, b, c, d, e, f, g, h, words[57], 0x78a5636f);
        STEP(a, b, c, d, e, f, g, h, words[58], 0x84c87814);
        STEP(a, b, c, d, e, f, g, h, words[59], 0x8cc70208);
        STEP(a, b, c, d, e, f, g, h, words[60], 0x90befffa);
        STEP(a, b, c, d, e, f, g, h, words[61], 0xa4506ceb);
        STEP(a, b, c, d, e, f, g, h, words[62], 0xbef9a3f7);
        STEP(a, b, c, d, e, f, g, h, words[63], 0xc67178f2);

        a += saved_a;
        b += saved_b;
        c += saved_c;
        d += saved_d;
        e += saved_e;
        f += saved_f;
        g += saved_g;
        h += saved_h;

        p += 64;

    } while (size -= 64);

    ctx->a = a;
    ctx->b = b;
    ctx->c = c;
    ctx->d = d;
    ctx->e = e;
    ctx->f = f;
    ctx->g = g;
    ctx->h = h;

    return p;
}
