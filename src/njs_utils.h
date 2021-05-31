
/*
 * Copyright (C) Dmitry Volyntsev
 * Copyright (C) NGINX, Inc.
 */

#ifndef _NJS_UTILS_H_INCLUDED_
#define _NJS_UTILS_H_INCLUDED_


typedef union {
    float       f;
    uint32_t    u;
} njs_conv_f32_t;


typedef union {
    double      f;
    uint64_t    u;
} njs_conv_f64_t;


struct NJS_PACKED njs_packed_u16_t {
    uint16_t v;
};

struct NJS_PACKED njs_packed_u32_t {
    uint32_t v;
};

struct NJS_PACKED njs_packed_u64_t {
    uint64_t v;
};


typedef int (*njs_sort_cmp_t)(const void *, const void *, void *ctx);

void njs_qsort(void *base, size_t n, size_t size, njs_sort_cmp_t cmp,
    void *ctx);

const char *njs_errno_string(int errnum);


njs_inline void
njs_swap_u8(void *a, void *b, size_t size)
{
    uint8_t  u, *au, *bu;

    au = (uint8_t *) a;
    bu = (uint8_t *) b;

    u = au[0];
    au[0] = bu[0];
    bu[0] = u;
}


njs_inline void
njs_swap_u16(void *a, void *b, size_t size)
{
    uint16_t  u, *au, *bu;

    au = (uint16_t *) a;
    bu = (uint16_t *) b;

    u = au[0];
    au[0] = bu[0];
    bu[0] = u;
}


njs_inline void
njs_swap_u32(void *a, void *b, size_t size)
{
    uint32_t  u, *au, *bu;

    au = (uint32_t *) a;
    bu = (uint32_t *) b;

    u = au[0];
    au[0] = bu[0];
    bu[0] = u;
}


njs_inline void
njs_swap_u64(void *a, void *b, size_t size)
{
    uint64_t  u, *au, *bu;

    au = (uint64_t *) a;
    bu = (uint64_t *) b;

    u = au[0];
    au[0] = bu[0];
    bu[0] = u;
}


njs_inline uint16_t
njs_bswap_u16(uint16_t u16)
{
    return (u16 >> 8)
           | (u16 << 8);
}


njs_inline uint32_t
njs_bswap_u32(uint32_t u32)
{
    return ((u32 & 0xff000000) >> 24)
           | ((u32 & 0x00ff0000) >> 8)
           | ((u32 & 0x0000ff00) << 8)
           | ((u32 & 0x000000ff) << 24);
}


njs_inline uint64_t
njs_bswap_u64(uint64_t u64)
{
    return ((u64 & 0xff00000000000000ULL) >> 56)
           | ((u64 & 0x00ff000000000000ULL) >> 40)
           | ((u64 & 0x0000ff0000000000ULL) >> 24)
           | ((u64 & 0x000000ff00000000ULL) >> 8)
           | ((u64 & 0x00000000ff000000ULL) << 8)
           | ((u64 & 0x0000000000ff0000ULL) << 24)
           | ((u64 & 0x000000000000ff00ULL) << 40)
           | ((u64 & 0x00000000000000ffULL) << 56);
}

njs_inline uint16_t
njs_get_u16(const uint8_t *p)
{
    return ((const struct njs_packed_u16_t *) p)->v;
}

njs_inline uint32_t
njs_get_u32(const uint8_t *p)
{
    return ((const struct njs_packed_u32_t *) p)->v;
}

njs_inline uint64_t
njs_get_u64(const uint8_t *p)
{
    return ((const struct njs_packed_u64_t *) p)->v;
}

njs_inline void
njs_set_u16(uint8_t *p, uint16_t val)
{
    ((struct njs_packed_u16_t *) p)->v = val;
}

njs_inline void
njs_set_u32(uint8_t *p, uint32_t val)
{
    ((struct njs_packed_u32_t *) p)->v = val;
}

njs_inline void
njs_set_u64(uint8_t *p, uint64_t val)
{
    ((struct njs_packed_u64_t *) p)->v = val;
}


#endif /* _NJS_UTILS_H_INCLUDED_ */
