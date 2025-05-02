
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) NGINX, Inc.
 */


#include <njs_main.h>


/*
 * Supported formats:
 *
 *    %[0][width][x][X]O        njs_off_t
 *    %[0][width]T              njs_time_t
 *    %[0][width][u][x|X]z      ssize_t/size_t
 *    %[0][width][u][x|X]d      int/u_int
 *    %[0][width][u][x|X]l      long
 *    %[0][width|m][u][x|X]i    njs_int_t/njs_uint_t
 *    %[0][width][u][x|X]D      int32_t/uint32_t
 *    %[0][width][u][x|X]L      int64_t/uint64_t
 *    %[0][width][.width]f      double, max valid number fits to %18.15f
 *
 *    %d                        int
 *
 *    %s                        null-terminated string
 *    %*s                       length and string
 *
 *    %p                        void *
 *    %P                        symbolized function address
 *    %b                        njs_bool_t
 *    %V                        njs_str_t *
 *    %Z                        '\0'
 *    %n                        '\n'
 *    %c                        char
 *    %%                        %
 *
 *  Reserved:
 *    %t                        ptrdiff_t
 *    %S                        null-terminated wchar string
 *    %C                        wchar
 *    %[0][width][u][x|X]Q      int128_t/uint128_t
 */


u_char *
njs_sprintf(u_char *buf, u_char *end, const char *fmt, ...)
{
    u_char   *p;
    va_list  args;

    va_start(args, fmt);
    p = njs_vsprintf(buf, end, fmt, args);
    va_end(args);

    return p;
}


/*
 * njs_sprintf_t is used:
 *    to pass several parameters of njs_integer() via single pointer
 *    and to store little used variables of njs_vsprintf().
 */

typedef struct {
   u_char        *end;
   const u_char  *hex;
   uint32_t      width;
   int32_t       frac_width;
   uint8_t       max_width;
   u_char        padding;
} njs_sprintf_t;


static u_char *njs_integer(njs_sprintf_t *spf, u_char *buf, uint64_t ui64);
static u_char *njs_float(njs_sprintf_t *spf, u_char *buf, double n);


/* A right way of "f == 0.0". */
#define  njs_double_is_zero(f)  (fabs(f) <= FLT_EPSILON)


u_char *
njs_vsprintf(u_char *buf, u_char *end, const char *fmt, va_list args)
{
    u_char         *p;
    int            d;
    double         f, i;
    size_t         size, length;
    int64_t        i64;
    uint64_t       ui64, frac;
    njs_str_t      *v;
    njs_uint_t     scale, n;
    njs_bool_t     sign;
    njs_sprintf_t  spf;

    static const u_char  hexadecimal[] = "0123456789abcdef";
    static const u_char  HEXADECIMAL[] = "0123456789ABCDEF";
    static const u_char  nan[] = "[nan]";
    static const u_char  infinity[] = "[infinity]";

    spf.end = end;

    while (*fmt != '\0' && buf < end) {

        /*
         * "buf < end" means that we could copy at least one character:
         * a plain character, "%%", "%c", or a minus without test.
         */

        if (*fmt != '%') {
            *buf++ = *fmt++;
            continue;
        }

        fmt++;

        /* Test some often used text formats first. */

        switch (*fmt) {

        case 'V':
            fmt++;
            v = va_arg(args, njs_str_t *);

            if (njs_fast_path(v != NULL)) {
                length = v->length;
                p = v->start;
                goto copy;
            }

            continue;

        case 's':
            p = va_arg(args, u_char *);

            if (njs_fast_path(p != NULL)) {
                while (*p != '\0' && buf < end) {
                    *buf++ = *p++;
                }
            }

            fmt++;
            continue;

        case '*':
            length = va_arg(args, size_t);

            fmt++;

            if (*fmt == 's') {
                fmt++;
                p = va_arg(args, u_char *);

                if (njs_fast_path(p != NULL)) {
                    goto copy;
                }
            }

            continue;

        default:
            break;
        }

        spf.hex = NULL;
        spf.width = 0;
        spf.frac_width = -1;
        spf.max_width = 0;
        spf.padding = (*fmt == '0') ? '0' : ' ';

        sign = 1;

        i64 = 0;
        ui64 = 0;

        while (*fmt >= '0' && *fmt <= '9') {
            spf.width = spf.width * 10 + (*fmt++ - '0');
        }


        for ( ;; ) {
            switch (*fmt) {

            case 'u':
                sign = 0;
                fmt++;
                continue;

            case 'm':
                spf.max_width = 1;
                fmt++;
                continue;

            case 'X':
                spf.hex = HEXADECIMAL;
                sign = 0;
                fmt++;
                continue;

            case 'x':
                spf.hex = hexadecimal;
                sign = 0;
                fmt++;
                continue;

            case '.':
                fmt++;
                spf.frac_width = 0;

                while (*fmt >= '0' && *fmt <= '9') {
                    spf.frac_width = spf.frac_width * 10 + *fmt++ - '0';
                }

                break;

            default:
                break;
            }

            break;
        }


        switch (*fmt) {

        case 'O':
            i64 = (int64_t) va_arg(args, njs_off_t);
            sign = 1;
            goto number;

        case 'T':
            i64 = (int64_t) va_arg(args, njs_time_t);
            sign = 1;
            goto number;

        case 'z':
            if (sign) {
                i64 = (int64_t) va_arg(args, ssize_t);
            } else {
                ui64 = (uint64_t) va_arg(args, size_t);
            }
            goto number;

        case 'i':
            if (sign) {
                i64 = (int64_t) va_arg(args, njs_int_t);
            } else {
                ui64 = (uint64_t) va_arg(args, njs_uint_t);
            }

            if (spf.max_width != 0) {
                spf.width = NJS_INT_T_LEN;
            }

            goto number;

        case 'd':
            if (sign) {
                i64 = (int64_t) va_arg(args, int);
            } else {
                ui64 = (uint64_t) va_arg(args, u_int);
            }
            goto number;

        case 'l':
            if (sign) {
                i64 = (int64_t) va_arg(args, long);
            } else {
                ui64 = (uint64_t) va_arg(args, u_long);
            }
            goto number;

        case 'D':
            if (sign) {
                i64 = (int64_t) va_arg(args, int32_t);
            } else {
                ui64 = (uint64_t) va_arg(args, uint32_t);
            }
            goto number;

        case 'L':
            if (sign) {
                i64 = va_arg(args, int64_t);
            } else {
                ui64 = va_arg(args, uint64_t);
            }
            goto number;

        case 'b':
            ui64 = (uint64_t) va_arg(args, njs_bool_t);
            sign = 0;
            goto number;

        case 'f':
            fmt++;

            f = va_arg(args, double);

            if (f < 0) {
                *buf++ = '-';
                f = -f;
            }

            if (njs_slow_path(isnan(f))) {
                p = (u_char *) nan;
                length = njs_length(nan);

                goto copy;

            } else if (njs_slow_path(isinf(f))) {
                p = (u_char *) infinity;
                length = njs_length(infinity);

                goto copy;
            }

            (void) modf(f, &i);
            frac = 0;

            if (spf.frac_width > 0) {

                scale = 1;
                for (n = spf.frac_width; n != 0; n--) {
                    scale *= 10;
                }

                frac = (uint64_t) ((f - i) * scale + 0.5);

                if (frac == scale) {
                    i += 1;
                    frac = 0;
                }
            }

            buf = njs_float(&spf, buf, i);

            if (spf.frac_width > 0) {

                if (buf < end) {
                    *buf++ = '.';

                    spf.hex = NULL;
                    spf.padding = '0';
                    spf.width = spf.frac_width;
                    buf = njs_integer(&spf, buf, frac);
                }

            } else if (spf.frac_width < 0) {
                f = modf(f, &i);

                if (!njs_double_is_zero(f) && buf < end) {
                    *buf++ = '.';

                    while (!njs_double_is_zero(f) && buf < end) {
                        f *= 10;
                        f = modf(f, &i);
                        *buf++ = (u_char) i + '0';
                    }
                }
            }

            continue;

        case 'p':
            ui64 = (uintptr_t) va_arg(args, void *);
            sign = 0;
            spf.hex = HEXADECIMAL;
            /*
             * spf.width = NJS_PTR_SIZE * 2;
             * spf.padding = '0';
             */
            goto number;

        case 'P':
            buf = njs_addr2line(buf, end, va_arg(args, void *));
            fmt++;
            continue;

        case 'c':
            d = va_arg(args, int);
            *buf++ = (u_char) (d & 0xFF);
            fmt++;

            continue;

        case 'Z':
            *buf++ = '\0';
            fmt++;
            continue;

        case 'n':
            *buf++ = '\n';
            fmt++;
            continue;

        case '%':
            *buf++ = '%';
            fmt++;
            continue;

        default:
            *buf++ = *fmt++;
            continue;
        }

    number:

        if (sign) {
            if (i64 < 0) {
                *buf++ = '-';
                ui64 = (uint64_t) -i64;

            } else {
                ui64 = (uint64_t) i64;
            }
        }

        buf = njs_integer(&spf, buf, ui64);

        fmt++;
        continue;

    copy:

        size = njs_min((size_t) (end - buf), length);

        if (size != 0) {
            buf = njs_cpymem(buf, p, size);
        }

        continue;
    }

    return buf;
}


static u_char *
njs_integer(njs_sprintf_t *spf, u_char *buf, uint64_t ui64)
{
    u_char  *p, *end;
    size_t  length;
    u_char  temp[NJS_INT64_T_LEN];

    p = temp + NJS_INT64_T_LEN;

    if (spf->hex == NULL) {

#if (NJS_32BIT)

        for ( ;; ) {
            u_char    *start;
            uint32_t  ui32;

            /*
             * 32-bit platforms usually lack hardware support of 64-bit
             * division and remainder operations.  For this reason C compiler
             * adds calls to the runtime library functions which provides
             * these operations.  These functions usually have about hundred
             * lines of code.
             *
             * For 32-bit numbers and some constant divisors GCC, Clang and
             * other compilers can use inlined multiplications and shifts
             * which are faster than division or remainder operations.
             * For example, unsigned "ui32 / 10" is compiled to
             *
             *     ((uint64_t) ui32 * 0xCCCCCCCD) >> 35
             *
             * So a 64-bit number is split to parts by 10^9.  The parts fit
             * to 32 bits and are processed separately as 32-bit numbers.  A
             * number of 64-bit division/remainder operations is significantly
             * decreased depending on the 64-bit number's value, it is
             *   0 if the 64-bit value is less than 4294967296,
             *   1 if the 64-bit value is greater than 4294967295
             *                           and less than 4294967296000000000,
             *   2 otherwise.
             */

            if (ui64 <= 0xFFFFFFFF) {
                ui32 = (uint32_t) ui64;
                start = NULL;

            } else {
                ui32 = (uint32_t) (ui64 % 1000000000);
                start = p - 9;
            }

            do {
                *(--p) = (u_char) (ui32 % 10 + '0');
                ui32 /= 10;
            } while (ui32 != 0);

            if (start == NULL) {
                break;
            }

            /* Add leading zeros of part. */

            while (p > start) {
                *(--p) = '0';
            }

            ui64 /= 1000000000;
        }

#else  /* NJS_64BIT */

        do {
            *(--p) = (u_char) (ui64 % 10 + '0');
            ui64 /= 10;
        } while (ui64 != 0);

#endif

    } else {

        do {
            *(--p) = spf->hex[ui64 & 0xF];
            ui64 >>= 4;
        } while (ui64 != 0);
    }

    length = (temp + NJS_INT64_T_LEN) - p;

    /* Zero or space padding. */

    if (length < spf->width) {
        end = buf + spf->width - length;
        end = njs_min(end, spf->end);

        while (buf < end) {
            *buf++ = spf->padding;
        }
    }

    /* Number copying. */

    end = buf + length;
    end = njs_min(end, spf->end);

    while (buf < end) {
        *buf++ = *p++;
    }

    return buf;
}


static u_char *
njs_float(njs_sprintf_t *spf, u_char *buf, double n)
{
    u_char  *p, *end;
    size_t  length;
    u_char  temp[NJS_DOUBLE_LEN];

    p = temp + NJS_DOUBLE_LEN;

    do {
        *(--p) = (u_char) (fmod(n, 10) + '0');
        n = trunc(n / 10);
    } while (!njs_double_is_zero(n));

    /* Zero or space padding. */

    if (spf->width != 0) {
        length = (temp + NJS_DOUBLE_LEN) - p;
        end = buf + (spf->width - length);
        end = njs_min(end, spf->end);

        while (buf < end) {
            *buf++ = spf->padding;
        }
    }

    /* Number copying. */

    length = (temp + NJS_DOUBLE_LEN) - p;

    end = buf + length;
    end = njs_min(end, spf->end);

    while (buf < end) {
        *buf++ = *p++;
    }

    return buf;
}


NJS_EXPORT
int njs_dprint(int fd, u_char *buf, size_t size)
{
    return write(fd, buf, size);
}


int
njs_dprintf(int fd, const char *fmt, ...)
{
    size_t   size;
    u_char   text[16384], *p;
    va_list  args;

    va_start(args, fmt);
    p = njs_vsprintf(text, text + sizeof(text), fmt, args);
    va_end(args);

    size = p - text;

    return write(fd, text, size);
}
