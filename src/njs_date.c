
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) NGINX, Inc.
 */


#include <njs_main.h>


/*
 * njs_timegm() is used because
 *   Solaris lacks timegm(),
 *   FreeBSD and MacOSX timegm() cannot handle years before 1900.
 */

#define NJS_ISO_DATE_TIME_LEN   sizeof("+001970-09-28T12:00:00.000Z")

#define NJS_HTTP_DATE_TIME_LEN  sizeof("Mon, 28 Sep 1970 12:00:00 GMT")

#define NJS_DATE_TIME_LEN                                                     \
    sizeof("Mon Sep 28 1970 12:00:00 GMT+0600 (XXXXX)")


static double njs_date_string_parse(njs_value_t *date);
static double njs_date_rfc2822_string_parse(struct tm *tm, const u_char *p,
    const u_char *end);
static double njs_date_js_string_parse(struct tm *tm, const u_char *p,
    const u_char *end);
static const u_char *njs_date_skip_week_day(const u_char *p, const u_char *end);
static const u_char *njs_date_skip_spaces(const u_char *p, const u_char *end);
static njs_int_t njs_date_month_parse(const u_char *p, const u_char *end);
static const u_char *njs_date_time_parse(struct tm *tm, const u_char *p,
    const u_char *end);
static njs_int_t njs_date_gmtoff_parse(const u_char *start, const u_char *end);
static const u_char *njs_date_number_parse(int *value, const u_char *p,
    const u_char *end, size_t size);
static int64_t njs_timegm(struct tm *tm);
static njs_int_t njs_date_string(njs_vm_t *vm, const char *fmt, double time);
static double njs_date_time(struct tm *tm, int64_t ms);
static double njs_date_utc_time(struct tm *tm, double time);


static const njs_value_t  njs_string_invalid_date = njs_string("Invalid Date");


njs_inline int64_t
njs_mod(int64_t a, int64_t b)
{
    int64_t  m;

    m = a % b;

    return m + (m < 0) * b;
}


njs_inline int64_t
njs_floor_div(int64_t a, int64_t b)
{
    int64_t  m;

    m = a % b;

    return (a - (m + (m < 0) * b)) / b;
}


njs_inline uint64_t
njs_gettime(void)
{
    struct timeval  tv;

    gettimeofday(&tv, NULL);

    return (uint64_t) tv.tv_sec * 1000 + tv.tv_usec / 1000;
}


njs_inline double
njs_timeclip(double time)
{
    if (time < -8.64e15 || time > 8.64e15) {
        return NAN;
    }

    return trunc(time) + 0.0;
}


njs_inline int64_t
njs_make_time(int64_t h, int64_t min, int64_t s, int64_t milli)
{
    return ((h * 60 + min) * 60 + s) * 1000 + milli;
}


njs_inline int64_t
njs_days_in_year(int64_t y)
{
    return 365 + !(y % 4) - !(y % 100) + !(y % 400);
}


njs_inline int64_t
njs_days_from_year(int64_t y)
{
    return 365 * (y - 1970) + njs_floor_div(y - 1969, 4)
           - njs_floor_div(y - 1901, 100) + njs_floor_div(y - 1601, 400);
}


njs_inline int64_t
njs_make_day(int64_t yr, int64_t month, int64_t date)
{
    int64_t  i, ym, mn, md, days;

    static const int month_days[] = { 31, 28, 31, 30, 31, 30,
                                      31, 31, 30, 31, 30, 31 };

    mn = njs_mod(month, 12);
    ym = yr + (month - mn) / 12;

    days = njs_days_from_year(ym);

    for (i = 0; i < mn; i++) {
        md = month_days[i];

        if (i == 1) {
            /* Leap day. */
            md += njs_days_in_year(ym) - 365;
        }

        days += md;
    }

    return days + date - 1;
}


njs_inline int64_t
njs_tz_offset(int64_t time)
{
    time_t     ti;
    struct tm  tm;

    time /= 1000;

    ti = time;
    localtime_r(&ti, &tm);

    return -njs_timezone(&tm) / 60;
}


njs_inline int64_t
njs_make_date(int64_t days, int64_t time, njs_bool_t local)
{
    int64_t  date;

    date = days * 86400000 + time;

    if (local) {
        date += njs_tz_offset(date) * 60000;
    }

    return date;
}


njs_int_t
njs_date_constructor(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t unused)
{
    double      num, time;
    int64_t     day, tm;
    njs_int_t   ret;
    njs_uint_t  i, n;
    njs_date_t  *date;
    int64_t     values[8];

    if (vm->top_frame->ctor) {

        if (nargs == 1) {
            time = njs_gettime();

        } else if (nargs == 2) {
            if (njs_is_object(&args[1])) {
                if (!njs_is_date(&args[1])) {
                    ret = njs_value_to_primitive(vm, &args[1], &args[1], 0);
                    if (ret != NJS_OK) {
                        return ret;
                    }
                }
            }

            if (njs_is_date(&args[1])) {
                time = njs_date(&args[1])->time;

            } else if (njs_is_string(&args[1])) {
                time = njs_date_string_parse(&args[1]);

            } else {
                time = njs_number(&args[1]);
            }

        } else {

            time = NAN;

            njs_memzero(values, 8 * sizeof(int64_t));

            /* Day. */
            values[3] = 1;

            n = njs_min(8, nargs);

            for (i = 1; i < n; i++) {
                if (!njs_is_numeric(&args[i])) {
                    ret = njs_value_to_numeric(vm, &args[i], &args[i]);
                    if (ret != NJS_OK) {
                        return ret;
                    }
                }

                num = njs_number(&args[i]);

                if (isnan(num) || isinf(num)) {
                    goto done;
                }

                values[i] = num;
            }

            /* Year. */
            if (values[1] >= 0 && values[1] < 100) {
                values[1] += 1900;
            }

            day = njs_make_day(values[1], values[2], values[3]);

            tm = njs_make_time(values[4], values[5], values[6], values[7]);

            time = njs_make_date(day, tm, 1);
        }

    done:

        date = njs_mp_alloc(vm->mem_pool, sizeof(njs_date_t));
        if (njs_slow_path(date == NULL)) {
            njs_memory_error(vm);
            return NJS_ERROR;
        }

        njs_lvlhsh_init(&date->object.hash);
        njs_lvlhsh_init(&date->object.shared_hash);
        date->object.type = NJS_DATE;
        date->object.shared = 0;
        date->object.extensible = 1;
        date->object.__proto__ = &vm->prototypes[NJS_PROTOTYPE_DATE].object;

        date->time = njs_timeclip(time);

        njs_set_date(&vm->retval, date);

        return NJS_OK;
    }

    return njs_date_string(vm, "%a %b %d %Y %T GMT%z (%Z)", njs_gettime());
}


static njs_int_t
njs_date_utc(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t unused)
{
    int64_t     day, tm;
    double      num, time;
    njs_int_t   ret;
    njs_uint_t  i, n;
    int64_t     values[8];

    time = NAN;

    if (nargs > 1) {
        njs_memzero(values, 8 * sizeof(int64_t));

        /* Day. */
        values[3] = 1;

        n = njs_min(8, nargs);

        for (i = 1; i < n; i++) {
            if (!njs_is_numeric(&args[i])) {
                ret = njs_value_to_numeric(vm, &args[i], &args[i]);
                if (ret != NJS_OK) {
                    return ret;
                }
            }

            num = njs_number(&args[i]);

            if (isnan(num) || isinf(num)) {
                goto done;
            }

            values[i] = num;
        }

        /* Year. */
        if (values[1] >= 0 && values[1] < 100) {
            values[1] += 1900;
        }

        day = njs_make_day(values[1], values[2], values[3]);
        tm = njs_make_time(values[4], values[5], values[6], values[7]);

        time = njs_timeclip(njs_make_date(day, tm, 0));
    }

done:

    njs_set_number(&vm->retval, time);

    return NJS_OK;
}


static int64_t
njs_timegm(struct tm *tm)
{
    int32_t  year, month, days;

    year = tm->tm_year + 1900;

    /*
     * Shift new year to March 1 and start months
     * from 1 (not 0), as required for Gauss' formula.
     */

    month = tm->tm_mon - 1;

    if (month <= 0) {
        month += 12;
        year -= 1;
    }

    /* Gauss' formula for Gregorian days since March 1, 1 BCE. */

    /* Days in years including leap years since March 1, 1 BCE. */
    days = 365 * year + year / 4 - year / 100 + year / 400;

    /* Days before the month. */
    days += 367 * month / 12 - 30;

    /* Days before the day. */
    if (year >= 0) {
        days += tm->tm_mday - 1;

    } else {
        /* 1 BCE was a leap year. */
        days += tm->tm_mday - 2;
    }

    /*
     * 719527 days were between March 1, 1 BCE and March 1, 1970,
     * 31 and 28 days were in January and February 1970.
     */
    days = days - 719527 + 31 + 28;

    return (int64_t) days * 86400
            + tm->tm_hour * 3600
            + tm->tm_min * 60
            + tm->tm_sec;
}


static njs_int_t
njs_date_now(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t unused)
{
    njs_set_number(&vm->retval, njs_gettime());

    return NJS_OK;
}


static njs_int_t
njs_date_parse(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t unused)
{
    double  time;

    if (nargs > 1) {
        time = njs_date_string_parse(&args[1]);

    } else {
        time = NAN;
    }

    njs_set_number(&vm->retval, time);

    return NJS_OK;
}


static double
njs_date_string_parse(njs_value_t *date)
{
    int                ext, ms, ms_length, skipped;
    double             time;
    njs_str_t          string;
    struct tm          tm;
    njs_bool_t         sign, week, utc;
    const u_char       *p, *next, *end;

    njs_string_get(date, &string);

    p = string.start;
    end = p + string.length;

    if (njs_slow_path(p >= end)) {
        return NAN;
    }

    if (*p == '+' || *p == '-') {
        p++;
        sign = 1;

    } else {
        sign = 0;
    }

    tm.tm_mon = 0;
    tm.tm_mday = 1;
    tm.tm_hour = 0;
    tm.tm_min = 0;
    tm.tm_sec = 0;

    next = njs_date_number_parse(&tm.tm_year, p, end, 4);

    if (next != NULL) {
        /* ISO-8601 format: "1970-09-28T06:00:00.000Z" */

        if (next == end) {
            goto year;
        }

        if (*next != '-') {
            /* Extended ISO-8601 format: "+001970-09-28T06:00:00.000Z" */

            next = njs_date_number_parse(&ext, next, end, 2);
            if (njs_slow_path(next == NULL)) {
                return NAN;
            }

            tm.tm_year = tm.tm_year * 100 + ext;

            if (string.start[0] == '-') {
                if (tm.tm_year == 0) {
                    return NAN;
                }

                tm.tm_year = -tm.tm_year;
            }

            if (next == end) {
                goto year;
            }

            if (*next != '-') {
                return NAN;
            }
        }

        tm.tm_year -= 1900;

        p = njs_date_number_parse(&tm.tm_mon, next + 1, end, 2);
        if (njs_slow_path(p == NULL)) {
            return NAN;
        }

        tm.tm_mon--;

        if (p == end) {
            goto done;
        }

        if (njs_slow_path(*p != '-')) {
            return NAN;
        }

        p = njs_date_number_parse(&tm.tm_mday, p + 1, end, 2);
        if (njs_slow_path(p == NULL)) {
            return NAN;
        }

        if (p == end) {
            goto done;
        }

        if (njs_slow_path(*p != 'T')) {
            return NAN;
        }

        utc = 1;
        end--;

        if (*end != 'Z') {
           utc = 0;
           end++;
        }

        p = njs_date_time_parse(&tm, p + 1, end);
        if (njs_slow_path(p == NULL)) {
            return NAN;
        }

        if (p == end) {
            goto done;
        }

        if (njs_slow_path(p > end || *p != '.')) {
            return NAN;
        }

        p++;

        ms_length = (end - p < 3) ? end - p : 3;

        p = njs_date_number_parse(&ms, p, end, ms_length);
        if (njs_slow_path(p == NULL)) {
            return NAN;
        }

        if (end > p) {
            p = njs_date_number_parse(&skipped, p, end, end - p);
            if (njs_slow_path(p == NULL)) {
                return NAN;
            }
        }

        if (ms_length == 1) {
            ms *= 100;

        } else if (ms_length == 2) {
            ms *= 10;
        }

        if (utc) {
            time = njs_timegm(&tm);

        } else {
            tm.tm_isdst = -1;
            time = mktime(&tm);
        }

        return time * 1000 + ms;
    }

    if (sign) {
        return NAN;
    }

    week = 1;

    for ( ;; ) {
        next = njs_date_number_parse(&tm.tm_mday, p, end, 2);

        if (next != NULL) {
            /*
             * RFC 2822 format:
             *   "Mon, 28 Sep 1970 06:00:00 GMT",
             *   "Mon, 28 Sep 1970 06:00:00 UTC",
             *   "Mon, 28 Sep 1970 12:00:00 +0600".
             */
            return njs_date_rfc2822_string_parse(&tm, next, end);
        }

        tm.tm_mon = njs_date_month_parse(p, end);

        if (tm.tm_mon >= 0) {
            /* Date.toString() format: "Mon Sep 28 1970 12:00:00 GMT+0600". */

            return njs_date_js_string_parse(&tm, p + 3, end);
        }

        if (!week) {
            return NAN;
        }

        p = njs_date_skip_week_day(p, end);
        if (njs_slow_path(p == NULL)) {
            return NAN;
        }

        p = njs_date_skip_spaces(p, end);
        if (njs_slow_path(p == NULL)) {
            return NAN;
        }

        week = 0;
    }

year:

    tm.tm_year -= 1900;

done:

    return njs_timegm(&tm) * 1000;
}


static double
njs_date_rfc2822_string_parse(struct tm *tm, const u_char *p, const u_char *end)
{
    int  gmtoff;

    p = njs_date_skip_spaces(p, end);
    if (njs_slow_path(p == NULL)) {
        return NAN;
    }

    tm->tm_mon = njs_date_month_parse(p, end);
    if (njs_slow_path(tm->tm_mon < 0)) {
        return NAN;
    }

    p = njs_date_skip_spaces(p + 3, end);
    if (njs_slow_path(p == NULL)) {
        return NAN;
    }

    p = njs_date_number_parse(&tm->tm_year, p, end, 4);
    if (njs_slow_path(p == NULL)) {
        return NAN;
    }

    tm->tm_year -= 1900;

    if (p == end) {
        goto done;
    }

    p = njs_date_skip_spaces(p, end);
    if (njs_slow_path(p == NULL)) {
        return NAN;
    }

    if (p == end) {
        goto done;
    }

    p = njs_date_time_parse(tm, p, end);
    if (njs_slow_path(p == NULL)) {
        return NAN;
    }

    if (p == end) {
        goto done;
    }

    p = njs_date_skip_spaces(p, end);
    if (njs_slow_path(p == NULL)) {
        return NAN;
    }

    if (p == end) {
        goto done;
    }

    if (njs_slow_path(p + 2 >= end)) {
        return NAN;
    }

    if ((p[0] == 'G' && p[1] == 'M' && p[2] == 'T')
        || (p[0] == 'U' && p[1] == 'T' && p[2] == 'C'))
    {
        gmtoff = 0;

    } else {
        gmtoff = njs_date_gmtoff_parse(p, end);

        if (njs_slow_path(gmtoff == -1)) {
            return NAN;
        }
    }

    return (njs_timegm(tm) - gmtoff * 60) * 1000;

done:

    return njs_timegm(tm) * 1000;
}


static double
njs_date_js_string_parse(struct tm *tm, const u_char *p, const u_char *end)
{
    int  gmtoff;

    p = njs_date_skip_spaces(p, end);
    if (njs_slow_path(p == NULL)) {
        return NAN;
    }

    p = njs_date_number_parse(&tm->tm_mday, p, end, 2);
    if (njs_slow_path(p == NULL)) {
        return NAN;
    }

    p = njs_date_skip_spaces(p, end);
    if (njs_slow_path(p == NULL)) {
        return NAN;
    }

    p = njs_date_number_parse(&tm->tm_year, p, end, 4);
    if (njs_slow_path(p == NULL)) {
        return NAN;
    }

    tm->tm_year -= 1900;

    if (p == end) {
        goto done;
    }

    p = njs_date_skip_spaces(p, end);
    if (njs_slow_path(p == NULL)) {
        return NAN;
    }

    if (p == end) {
        goto done;
    }

    p = njs_date_time_parse(tm, p, end);
    if (njs_slow_path(p == NULL)) {
        return NAN;
    }

    if (p == end) {
        goto done;
    }

    p = njs_date_skip_spaces(p, end);
    if (njs_slow_path(p == NULL)) {
        return NAN;
    }

    if (p == end) {
        goto done;
    }

    if (p + 2 < end && p[0] == 'G' && p[1] == 'M' && p[2] == 'T') {

        gmtoff = njs_date_gmtoff_parse(&p[3], end);

        if (njs_fast_path(gmtoff != -1)) {
            return (njs_timegm(tm) - gmtoff * 60) * 1000;
        }
    }

    return NAN;

done:

    return njs_timegm(tm) * 1000;
}


static const u_char *
njs_date_skip_week_day(const u_char *p, const u_char *end)
{
    while (p < end) {
        if (*p == ' ') {
            return p;
        }

        p++;
    }

    return NULL;
}


static const u_char *
njs_date_skip_spaces(const u_char *p, const u_char *end)
{
    if (p < end && *p++ == ' ') {

        while (p < end) {
            if (*p != ' ') {
                return p;
            }

            p++;
        }

        return p;
    }

    return NULL;
}


static njs_int_t
njs_date_month_parse(const u_char *p, const u_char *end)
{
    if (p + 2 < end) {
        switch (p[0]) {

        case 'J':
            if (p[1] == 'a' && p[2] == 'n') {
                return 0;
            }

            if (p[1] == 'u') {
                if (p[2] == 'n') {
                    return 5;
                }

                if (p[2] == 'l') {
                    return 6;
                }
            }

            break;

        case 'F':
            if (p[1] == 'e' && p[2] == 'b') {
                return 1;
            }

            break;

        case 'M':
            if (p[1] == 'a') {
                if (p[2] == 'r') {
                    return 2;
                }

                if (p[2] == 'y') {
                    return 4;
                }
            }

            break;

        case 'A':
            if (p[1] == 'p' && p[2] == 'r') {
                return 3;
            }

            if (p[1] == 'u' && p[2] == 'g') {
                return 7;
            }

            break;

        case 'S':
            if (p[1] == 'e' && p[2] == 'p') {
                return 8;
            }

            break;

        case 'O':
            if (p[1] == 'c' && p[2] == 't') {
                return 9;
            }

            break;

        case 'N':
            if (p[1] == 'o' && p[2] == 'v') {
                return 10;
            }

            break;

        case 'D':
            if (p[1] == 'e' && p[2] == 'c') {
                return 11;
            }

            break;
        }
    }

    return -1;
}


static const u_char *
njs_date_time_parse(struct tm *tm, const u_char *p, const u_char *end)
{
    p = njs_date_number_parse(&tm->tm_hour, p, end, 2);
    if (njs_slow_path(p == NULL)) {
        return p;
    }

    if (njs_slow_path(p >= end || *p != ':')) {
        return NULL;
    }

    p = njs_date_number_parse(&tm->tm_min, p + 1, end, 2);
    if (njs_slow_path(p == NULL)) {
        return p;
    }

    if (p == end) {
        return p;
    }

    if (njs_slow_path(*p != ':')) {
        return NULL;
    }

    return njs_date_number_parse(&tm->tm_sec, p + 1, end, 2);
}


static njs_int_t
njs_date_gmtoff_parse(const u_char *start, const u_char *end)
{
    int           gmtoff, hour, min;
    const u_char  *p;

    if (njs_fast_path(start + 4 < end && (*start == '+' || *start == '-'))) {

        p = njs_date_number_parse(&hour, start + 1, end, 2);
        if (njs_slow_path(p == NULL)) {
            return -1;
        }

        p = njs_date_number_parse(&min, p, end, 2);
        if (njs_slow_path(p == NULL)) {
            return -1;
        }

        gmtoff = hour * 60 + min;

        if (*start == '-') {
            gmtoff = -gmtoff;
        }

        return gmtoff;
    }

    return -1;
}


static const u_char *
njs_date_number_parse(int *value, const u_char *p, const u_char *end,
    size_t size)
{
    u_char     c;
    njs_int_t  n;

    n = 0;

    do {
        if (njs_slow_path(p >= end)) {
            return NULL;
        }

        c = *p++;

        /* Values below '0' become >= 208. */
        c = c - '0';

        if (njs_slow_path(c > 9)) {
            return NULL;
        }

        n = n * 10 + c;

        size--;

    } while (size != 0);

    *value = n;

    return p;
}


static const njs_object_prop_t  njs_date_constructor_properties[] =
{
    /* Date.name == "Date". */
    {
        .type = NJS_PROPERTY,
        .name = njs_string("name"),
        .value = njs_string("Date"),
        .configurable = 1,
    },

    /* Date.length == 7. */
    {
        .type = NJS_PROPERTY,
        .name = njs_string("length"),
        .value = njs_value(NJS_NUMBER, 1, 7.0),
        .configurable = 1,
    },

    /* Date.prototype. */
    {
        .type = NJS_PROPERTY_HANDLER,
        .name = njs_string("prototype"),
        .value = njs_prop_handler(njs_object_prototype_create),
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("UTC"),
        .value = njs_native_function(njs_date_utc, 0),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("now"),
        .value = njs_native_function(njs_date_now, 0),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("parse"),
        .value = njs_native_function(njs_date_parse,
                                     NJS_SKIP_ARG, NJS_STRING_ARG),
        .writable = 1,
        .configurable = 1,
    },
};


const njs_object_init_t  njs_date_constructor_init = {
    njs_str("Date"),
    njs_date_constructor_properties,
    njs_nitems(njs_date_constructor_properties),
};


static njs_int_t
njs_date_prototype_value_of(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t unused)
{
    njs_set_number(&vm->retval, njs_date(&args[0])->time);

    return NJS_OK;
}


static njs_int_t
njs_date_prototype_to_string(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t unused)
{
    return njs_date_string(vm, "%a %b %d %Y %T GMT%z (%Z)",
                           njs_date(&args[0])->time);
}


static njs_int_t
njs_date_prototype_to_date_string(njs_vm_t *vm, njs_value_t *args,
    njs_uint_t nargs, njs_index_t unused)
{
    return njs_date_string(vm, "%a %b %d %Y", njs_date(&args[0])->time);
}


static njs_int_t
njs_date_prototype_to_time_string(njs_vm_t *vm, njs_value_t *args,
    njs_uint_t nargs, njs_index_t unused)
{
    return njs_date_string(vm, "%T GMT%z (%Z)", njs_date(&args[0])->time);
}


static njs_int_t
njs_date_string(njs_vm_t *vm, const char *fmt, double time)
{
    size_t     size;
    time_t     clock;
    u_char     buf[NJS_DATE_TIME_LEN];
    struct tm  tm;

    if (!isnan(time)) {
        clock = time / 1000;
        localtime_r(&clock, &tm);

        size = strftime((char *) buf, NJS_DATE_TIME_LEN, fmt, &tm);

        return njs_string_new(vm, &vm->retval, buf, size, size);
    }

    vm->retval = njs_string_invalid_date;

    return NJS_OK;
}


static njs_int_t
njs_date_prototype_to_utc_string(njs_vm_t *vm, njs_value_t *args,
    njs_uint_t nargs, njs_index_t unused)
{
    double             time;
    time_t             clock;
    u_char             buf[NJS_HTTP_DATE_TIME_LEN], *p;
    struct tm          tm;

    static const char  *week[] = { "Sun", "Mon", "Tue", "Wed",
                                   "Thu", "Fri", "Sat" };

    static const char  *month[] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                    "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };

    time = njs_date(&args[0])->time;

    if (!isnan(time)) {
        clock = time / 1000;
        gmtime_r(&clock, &tm);

        p = njs_sprintf(buf, buf + NJS_HTTP_DATE_TIME_LEN,
                        "%s, %02d %s %4d %02d:%02d:%02d GMT",
                        week[tm.tm_wday], tm.tm_mday, month[tm.tm_mon],
                        tm.tm_year + 1900, tm.tm_hour, tm.tm_min, tm.tm_sec);

        return njs_string_new(vm, &vm->retval, buf, p - buf, p - buf);
    }

    vm->retval = njs_string_invalid_date;

    return NJS_OK;
}


static njs_int_t
njs_date_prototype_to_iso_string(njs_vm_t *vm, njs_value_t *args,
    njs_uint_t nargs, njs_index_t unused)
{
    return njs_date_to_string(vm, &vm->retval, &args[0]);
}


njs_int_t
njs_date_to_string(njs_vm_t *vm, njs_value_t *retval, const njs_value_t *date)
{
    int32_t    year;
    double     time;
    time_t     clock;
    u_char     buf[NJS_ISO_DATE_TIME_LEN], *p;
    struct tm  tm;

    time = njs_date(date)->time;

    if (!isnan(time)) {
        clock = time / 1000;

        gmtime_r(&clock, &tm);

        year = tm.tm_year + 1900;

        p = njs_sprintf(buf, buf + NJS_ISO_DATE_TIME_LEN,
                        (year < 0) ? "%07d-%02d-%02dT%02d:%02d:%02d.%03dZ"
                                   : "%04d-%02d-%02dT%02d:%02d:%02d.%03dZ",
                        year, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min,
                        tm.tm_sec, (int) ((int64_t) time % 1000));

        return njs_string_new(vm, retval, buf, p - buf, p - buf);
    }

    *retval = njs_string_invalid_date;

    return NJS_OK;
}


static njs_int_t
njs_date_prototype_get_full_year(njs_vm_t *vm, njs_value_t *args,
    njs_uint_t nargs, njs_index_t unused)
{
    double     value;
    time_t     clock;
    struct tm  tm;

    value = njs_date(&args[0])->time;

    if (njs_fast_path(!isnan(value))) {
        clock = value / 1000;
        localtime_r(&clock, &tm);

        value = tm.tm_year + 1900;
    }

    njs_set_number(&vm->retval, value);

    return NJS_OK;
}


static njs_int_t
njs_date_prototype_get_utc_full_year(njs_vm_t *vm, njs_value_t *args,
    njs_uint_t nargs, njs_index_t unused)
{
    double     value;
    time_t     clock;
    struct tm  tm;

    value = njs_date(&args[0])->time;

    if (njs_fast_path(!isnan(value))) {
        clock = value / 1000;
        gmtime_r(&clock, &tm);

        value = tm.tm_year + 1900;
    }

    njs_set_number(&vm->retval, value);

    return NJS_OK;
}


static njs_int_t
njs_date_prototype_get_month(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t unused)
{
    double     value;
    time_t     clock;
    struct tm  tm;

    value = njs_date(&args[0])->time;

    if (njs_fast_path(!isnan(value))) {
        clock = value / 1000;
        localtime_r(&clock, &tm);

        value = tm.tm_mon;
    }

    njs_set_number(&vm->retval, value);

    return NJS_OK;
}


static njs_int_t
njs_date_prototype_get_utc_month(njs_vm_t *vm, njs_value_t *args,
    njs_uint_t nargs, njs_index_t unused)
{
    double     value;
    time_t     clock;
    struct tm  tm;

    value = njs_date(&args[0])->time;

    if (njs_fast_path(!isnan(value))) {
        clock = value / 1000;

        gmtime_r(&clock, &tm);

        value = tm.tm_mon;
    }

    njs_set_number(&vm->retval, value);

    return NJS_OK;
}


static njs_int_t
njs_date_prototype_get_date(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t unused)
{
    double     value;
    time_t     clock;
    struct tm  tm;

    value = njs_date(&args[0])->time;

    if (njs_fast_path(!isnan(value))) {
        clock = value / 1000;
        localtime_r(&clock, &tm);

        value = tm.tm_mday;
    }

    njs_set_number(&vm->retval, value);

    return NJS_OK;
}


static njs_int_t
njs_date_prototype_get_utc_date(njs_vm_t *vm, njs_value_t *args,
    njs_uint_t nargs, njs_index_t unused)
{
    double     value;
    time_t     clock;
    struct tm  tm;

    value = njs_date(&args[0])->time;

    if (njs_fast_path(!isnan(value))) {
        clock = value / 1000;
        gmtime_r(&clock, &tm);

        value = tm.tm_mday;
    }

    njs_set_number(&vm->retval, value);

    return NJS_OK;
}


static njs_int_t
njs_date_prototype_get_day(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t unused)
{
    double     value;
    time_t     clock;
    struct tm  tm;

    value = njs_date(&args[0])->time;

    if (njs_fast_path(!isnan(value))) {
        clock = value / 1000;
        localtime_r(&clock, &tm);

        value = tm.tm_wday;
    }

    njs_set_number(&vm->retval, value);

    return NJS_OK;
}


static njs_int_t
njs_date_prototype_get_utc_day(njs_vm_t *vm, njs_value_t *args,
    njs_uint_t nargs, njs_index_t unused)
{
    double     value;
    time_t     clock;
    struct tm  tm;

    value = njs_date(&args[0])->time;

    if (njs_fast_path(!isnan(value))) {
        clock = value / 1000;
        gmtime_r(&clock, &tm);

        value = tm.tm_wday;
    }

    njs_set_number(&vm->retval, value);

    return NJS_OK;
}


static njs_int_t
njs_date_prototype_get_hours(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t unused)
{
    double     value;
    time_t     clock;
    struct tm  tm;

    value = njs_date(&args[0])->time;

    if (njs_fast_path(!isnan(value))) {
        clock = value / 1000;

        localtime_r(&clock, &tm);

        value = tm.tm_hour;
    }

    njs_set_number(&vm->retval, value);

    return NJS_OK;
}


static njs_int_t
njs_date_prototype_get_utc_hours(njs_vm_t *vm, njs_value_t *args,
    njs_uint_t nargs, njs_index_t unused)
{
    double     value;
    time_t     clock;
    struct tm  tm;

    value = njs_date(&args[0])->time;

    if (njs_fast_path(!isnan(value))) {
        clock = value / 1000;
        gmtime_r(&clock, &tm);

        value = tm.tm_hour;
    }

    njs_set_number(&vm->retval, value);

    return NJS_OK;
}


static njs_int_t
njs_date_prototype_get_minutes(njs_vm_t *vm, njs_value_t *args,
    njs_uint_t nargs, njs_index_t unused)
{
    double     value;
    time_t     clock;
    struct tm  tm;

    value = njs_date(&args[0])->time;

    if (njs_fast_path(!isnan(value))) {
        clock = value / 1000;

        localtime_r(&clock, &tm);

        value = tm.tm_min;
    }

    njs_set_number(&vm->retval, value);

    return NJS_OK;
}


static njs_int_t
njs_date_prototype_get_utc_minutes(njs_vm_t *vm, njs_value_t *args,
    njs_uint_t nargs, njs_index_t unused)
{
    double     value;
    time_t     clock;
    struct tm  tm;

    value = njs_date(&args[0])->time;

    if (njs_fast_path(!isnan(value))) {
        clock = value / 1000;
        gmtime_r(&clock, &tm);

        value = tm.tm_min;
    }

    njs_set_number(&vm->retval, value);

    return NJS_OK;
}


static njs_int_t
njs_date_prototype_get_seconds(njs_vm_t *vm, njs_value_t *args,
    njs_uint_t nargs, njs_index_t unused)
{
    double  value;

    value = njs_date(&args[0])->time;

    if (njs_fast_path(!isnan(value))) {
        value = (int64_t) (value / 1000) % 60;
    }

    njs_set_number(&vm->retval, value);

    return NJS_OK;
}


static njs_int_t
njs_date_prototype_get_milliseconds(njs_vm_t *vm, njs_value_t *args,
    njs_uint_t nargs, njs_index_t unused)
{
    double  value;

    value = njs_date(&args[0])->time;

    if (njs_fast_path(!isnan(value))) {
        value = (int64_t) value % 1000;
    }

    njs_set_number(&vm->retval, value);

    return NJS_OK;
}


static njs_int_t
njs_date_prototype_get_timezone_offset(njs_vm_t *vm, njs_value_t *args,
    njs_uint_t nargs, njs_index_t unused)
{
    double  value;

    value = njs_date(&args[0])->time;

    if (njs_fast_path(!isnan(value))) {
        value = njs_tz_offset(value);
    }

    njs_set_number(&vm->retval, value);

    return NJS_OK;
}


static njs_int_t
njs_date_prototype_set_time(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t unused)
{
    double  time;

    time = njs_date(&args[0])->time;

    if (njs_fast_path(!isnan(time))) {

        if (nargs > 1) {
            time = njs_number(&args[1]);

        } else {
            time = NAN;
        }
    }

    njs_date(&args[0])->time = time;
    njs_set_number(&vm->retval, time);

    return NJS_OK;
}


static njs_int_t
njs_date_prototype_set_milliseconds(njs_vm_t *vm, njs_value_t *args,
    njs_uint_t nargs, njs_index_t unused)
{
    double  time;

    time = njs_date(&args[0])->time;

    if (njs_fast_path(!isnan(time))) {

        if (nargs > 1) {
            time = (int64_t) (time / 1000) * 1000 + njs_number(&args[1]);

        } else {
            time = NAN;
        }
    }

    njs_date(&args[0])->time = time;
    njs_set_number(&vm->retval, time);

    return NJS_OK;
}


static njs_int_t
njs_date_prototype_set_seconds(njs_vm_t *vm, njs_value_t *args,
    njs_uint_t nargs, njs_index_t unused)
{
    double   time;
    int64_t  sec, ms;

    time = njs_date(&args[0])->time;

    if (njs_fast_path(!isnan(time))) {

        if (nargs > 1) {
            sec = njs_number(&args[1]);
            ms = (nargs > 2) ? njs_number(&args[2]) : (int64_t) time % 1000;

            time = (int64_t) (time / 60000) * 60000 + sec * 1000 + ms;

        } else {
            time = NAN;
        }
    }

    njs_date(&args[0])->time = time;
    njs_set_number(&vm->retval, time);

    return NJS_OK;
}


static njs_int_t
njs_date_prototype_set_minutes(njs_vm_t *vm, njs_value_t *args,
    njs_uint_t nargs, njs_index_t unused)
{
    double     time;
    time_t     clock;
    int64_t    ms;
    struct tm  tm;

    time = njs_date(&args[0])->time;

    if (njs_fast_path(!isnan(time))) {

        if (nargs > 1) {
            clock = time / 1000;
            localtime_r(&clock, &tm);

            tm.tm_min = njs_number(&args[1]);

            if (nargs > 2) {
                tm.tm_sec = njs_number(&args[2]);
            }

            ms = (nargs > 3) ? njs_number(&args[3]) : (int64_t) time % 1000;

            time = njs_date_time(&tm, ms);

        } else {
            time = NAN;
        }
    }

    njs_date(&args[0])->time = time;
    njs_set_number(&vm->retval, time);

    return NJS_OK;
}


static njs_int_t
njs_date_prototype_set_utc_minutes(njs_vm_t *vm, njs_value_t *args,
    njs_uint_t nargs, njs_index_t unused)
{
    double   time;
    int64_t  clock, min, sec, ms;

    time = njs_date(&args[0])->time;

    if (njs_fast_path(!isnan(time))) {

        if (nargs > 1) {
            clock = time / 1000;

            sec = (nargs > 2) ? njs_number(&args[2]) : clock % 60;
            min = njs_number(&args[1]);

            clock = clock / 3600 * 3600 + min * 60 + sec;

            ms = (nargs > 3) ? njs_number(&args[3]) : (int64_t) time % 1000;

            time = clock * 1000 + ms;

        } else {
            time = NAN;
        }
    }

    njs_date(&args[0])->time = time;
    njs_set_number(&vm->retval, time);

    return NJS_OK;
}


static njs_int_t
njs_date_prototype_set_hours(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t unused)
{
    double     time;
    time_t     clock;
    int64_t    ms;
    struct tm  tm;

    time = njs_date(&args[0])->time;

    if (njs_fast_path(!isnan(time))) {

        if (nargs > 1) {
            clock = time / 1000;
            localtime_r(&clock, &tm);

            tm.tm_hour = njs_number(&args[1]);

            if (nargs > 2) {
                tm.tm_min = njs_number(&args[2]);
            }

            if (nargs > 3) {
                tm.tm_sec = njs_number(&args[3]);
            }

            ms = (nargs > 4) ? njs_number(&args[4]) : (int64_t) time % 1000;

            time = njs_date_time(&tm, ms);

        } else {
            time = NAN;
        }
    }

    njs_date(&args[0])->time = time;
    njs_set_number(&vm->retval, time);

    return NJS_OK;
}


static njs_int_t
njs_date_prototype_set_utc_hours(njs_vm_t *vm, njs_value_t *args,
    njs_uint_t nargs, njs_index_t unused)
{
    double   time;
    int64_t  clock, hour, min, sec, ms;

    time = njs_date(&args[0])->time;

    if (njs_fast_path(!isnan(time))) {

        if (nargs > 1) {
            clock = time / 1000;

            sec = (nargs > 3) ? njs_number(&args[3]) : clock % 60;
            min = (nargs > 2) ? njs_number(&args[2]) : clock / 60 % 60;
            hour = njs_number(&args[1]);

            clock = clock / 86400 * 86400 + hour * 3600 + min * 60 + sec;

            ms = (nargs > 4) ? njs_number(&args[4]) : (int64_t) time % 1000;

            time = clock * 1000 + ms;

        } else {
            time = NAN;
        }
    }

    njs_date(&args[0])->time = time;
    njs_set_number(&vm->retval, time);

    return NJS_OK;
}


static njs_int_t
njs_date_prototype_set_date(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t unused)
{
    double     time;
    time_t     clock;
    struct tm  tm;

    time = njs_date(&args[0])->time;

    if (njs_fast_path(!isnan(time))) {

        if (nargs > 1) {
            clock = time / 1000;
            localtime_r(&clock, &tm);

            tm.tm_mday = njs_number(&args[1]);

            time = njs_date_time(&tm, (int64_t) time % 1000);

        } else {
            time = NAN;
        }
    }

    njs_date(&args[0])->time = time;
    njs_set_number(&vm->retval, time);

    return NJS_OK;
}


static njs_int_t
njs_date_prototype_set_utc_date(njs_vm_t *vm, njs_value_t *args,
    njs_uint_t nargs, njs_index_t unused)
{
    double     time;
    time_t     clock;
    struct tm  tm;

    time = njs_date(&args[0])->time;

    if (njs_fast_path(!isnan(time))) {

        if (nargs > 1) {
            clock = time / 1000;
            gmtime_r(&clock, &tm);

            tm.tm_mday = njs_number(&args[1]);

            time = njs_date_utc_time(&tm, time);

        } else {
            time = NAN;
        }
    }

    njs_date(&args[0])->time = time;
    njs_set_number(&vm->retval, time);

    return NJS_OK;
}


static njs_int_t
njs_date_prototype_set_month(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t unused)
{
    double     time;
    time_t     clock;
    struct tm  tm;

    time = njs_date(&args[0])->time;

    if (njs_fast_path(!isnan(time))) {

        if (nargs > 1) {
            clock = time / 1000;
            localtime_r(&clock, &tm);

            tm.tm_mon = njs_number(&args[1]);

            if (nargs > 2) {
                tm.tm_mday = njs_number(&args[2]);
            }

            time = njs_date_time(&tm, (int64_t) time % 1000);

        } else {
            time = NAN;
        }
    }

    njs_date(&args[0])->time = time;
    njs_set_number(&vm->retval, time);

    return NJS_OK;
}


static njs_int_t
njs_date_prototype_set_utc_month(njs_vm_t *vm, njs_value_t *args,
    njs_uint_t nargs, njs_index_t unused)
{
    double     time;
    time_t     clock;
    struct tm  tm;

    time = njs_date(&args[0])->time;

    if (njs_fast_path(!isnan(time))) {

        if (nargs > 1) {
            clock = time / 1000;
            gmtime_r(&clock, &tm);

            tm.tm_mon = njs_number(&args[1]);

            if (nargs > 2) {
                tm.tm_mday = njs_number(&args[2]);
            }

            time = njs_date_utc_time(&tm, time);

        } else {
            time = NAN;
        }
    }

    njs_date(&args[0])->time = time;
    njs_set_number(&vm->retval, time);

    return NJS_OK;
}


static njs_int_t
njs_date_prototype_set_full_year(njs_vm_t *vm, njs_value_t *args,
    njs_uint_t nargs, njs_index_t unused)
{
    double     time;
    time_t     clock;
    struct tm  tm;

    time = njs_date(&args[0])->time;

    if (njs_fast_path(!isnan(time))) {

        if (nargs > 1) {
            clock = time / 1000;
            localtime_r(&clock, &tm);

            tm.tm_year = njs_number(&args[1]) - 1900;

            if (nargs > 2) {
                tm.tm_mon = njs_number(&args[2]);
            }

            if (nargs > 3) {
                tm.tm_mday = njs_number(&args[3]);
            }

            time = njs_date_time(&tm, (int64_t) time % 1000);

        } else {
            time = NAN;
        }
    }

    njs_date(&args[0])->time = time;
    njs_set_number(&vm->retval, time);

    return NJS_OK;
}


static njs_int_t
njs_date_prototype_set_utc_full_year(njs_vm_t *vm, njs_value_t *args,
    njs_uint_t nargs, njs_index_t unused)
{
    double     time;
    time_t     clock;
    struct tm  tm;

    time = njs_date(&args[0])->time;

    if (njs_fast_path(!isnan(time))) {

        if (nargs > 1) {
            clock = time / 1000;
            gmtime_r(&clock, &tm);

            tm.tm_year = njs_number(&args[1]) - 1900;

            if (nargs > 2) {
                tm.tm_mon = njs_number(&args[2]);
            }

            if (nargs > 3) {
                tm.tm_mday = njs_number(&args[3]);
            }

            time = njs_date_utc_time(&tm, time);

        } else {
            time = NAN;
        }
    }

    njs_date(&args[0])->time = time;
    njs_set_number(&vm->retval, time);

    return NJS_OK;
}


static double
njs_date_time(struct tm *tm, int64_t ms)
{
    double  time;
    time_t  clock;

    tm->tm_isdst = -1;
    clock = mktime(tm);

    if (njs_fast_path(clock != -1)) {
        time = (int64_t) clock * 1000 + ms;

    } else {
        time = NAN;
    }

    return time;
}


static double
njs_date_utc_time(struct tm *tm, double time)
{
    return njs_timegm(tm) * 1000 + (int64_t) time % 1000;
}


static njs_int_t
njs_date_prototype_to_json(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t retval)
{
    njs_int_t           ret;
    njs_value_t         value;
    njs_lvlhsh_query_t  lhq;

    if (njs_is_object(&args[0])) {
        njs_object_property_init(&lhq, "toISOString", NJS_TO_ISO_STRING_HASH);

        ret = njs_object_property(vm, &args[0], &lhq, &value);

        if (njs_slow_path(ret == NJS_ERROR)) {
            return ret;
        }

        if (njs_is_function(&value)) {
            return njs_function_apply(vm, njs_function(&value), args, nargs,
                                      &vm->retval);
        }
    }

    njs_type_error(vm, "\"this\" argument is not an object");

    return NJS_ERROR;
}


static const njs_object_prop_t  njs_date_prototype_properties[] =
{
    {
        .type = NJS_PROPERTY_HANDLER,
        .name = njs_string("__proto__"),
        .value = njs_prop_handler(njs_primitive_prototype_get_proto),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY_HANDLER,
        .name = njs_string("constructor"),
        .value = njs_prop_handler(njs_object_prototype_create_constructor),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("valueOf"),
        .value = njs_native_function(njs_date_prototype_value_of, NJS_DATE_ARG),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("toString"),
        .value = njs_native_function(njs_date_prototype_to_string,
                                     NJS_DATE_ARG),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("toDateString"),
        .value = njs_native_function(njs_date_prototype_to_date_string,
                                     NJS_DATE_ARG),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("toTimeString"),
        .value = njs_native_function(njs_date_prototype_to_time_string,
                                     NJS_DATE_ARG),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("toLocaleString"),
        .value = njs_native_function(njs_date_prototype_to_string,
                                     NJS_DATE_ARG),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_long_string("toLocaleDateString"),
        .value = njs_native_function(njs_date_prototype_to_date_string,
                                     NJS_DATE_ARG),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_long_string("toLocaleTimeString"),
        .value = njs_native_function(njs_date_prototype_to_time_string,
                                     NJS_DATE_ARG),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("toUTCString"),
        .value = njs_native_function(njs_date_prototype_to_utc_string,
                                     NJS_DATE_ARG),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("toISOString"),
        .value = njs_native_function(njs_date_prototype_to_iso_string,
                                     NJS_DATE_ARG),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("getTime"),
        .value = njs_native_function(njs_date_prototype_value_of, NJS_DATE_ARG),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("getFullYear"),
        .value = njs_native_function(njs_date_prototype_get_full_year,
                                     NJS_DATE_ARG),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("getUTCFullYear"),
        .value = njs_native_function(njs_date_prototype_get_utc_full_year,
                                     NJS_DATE_ARG),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("getMonth"),
        .value = njs_native_function(njs_date_prototype_get_month,
                                     NJS_DATE_ARG),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("getUTCMonth"),
        .value = njs_native_function(njs_date_prototype_get_utc_month,
                                     NJS_DATE_ARG),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("getDate"),
        .value = njs_native_function(njs_date_prototype_get_date,
                                     NJS_DATE_ARG),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("getUTCDate"),
        .value = njs_native_function(njs_date_prototype_get_utc_date,
                                     NJS_DATE_ARG),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("getDay"),
        .value = njs_native_function(njs_date_prototype_get_day, NJS_DATE_ARG),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("getUTCDay"),
        .value = njs_native_function(njs_date_prototype_get_utc_day,
                                     NJS_DATE_ARG),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("getHours"),
        .value = njs_native_function(njs_date_prototype_get_hours,
                                     NJS_DATE_ARG),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("getUTCHours"),
        .value = njs_native_function(njs_date_prototype_get_utc_hours,
                                     NJS_DATE_ARG),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("getMinutes"),
        .value = njs_native_function(njs_date_prototype_get_minutes,
                                     NJS_DATE_ARG),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("getUTCMinutes"),
        .value = njs_native_function(njs_date_prototype_get_utc_minutes,
                                     NJS_DATE_ARG),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("getSeconds"),
        .value = njs_native_function(njs_date_prototype_get_seconds,
                                     NJS_DATE_ARG),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("getUTCSeconds"),
        .value = njs_native_function(njs_date_prototype_get_seconds,
                                     NJS_DATE_ARG),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_long_string("getMilliseconds"),
        .value = njs_native_function(njs_date_prototype_get_milliseconds,
                                     NJS_DATE_ARG),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_long_string("getUTCMilliseconds"),
        .value = njs_native_function(njs_date_prototype_get_milliseconds,
                                     NJS_DATE_ARG),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_long_string("getTimezoneOffset"),
        .value = njs_native_function(njs_date_prototype_get_timezone_offset,
                                     NJS_DATE_ARG),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("setTime"),
        .value = njs_native_function(njs_date_prototype_set_time,
                                     NJS_DATE_ARG, NJS_NUMBER_ARG),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_long_string("setMilliseconds"),
        .value = njs_native_function(njs_date_prototype_set_milliseconds,
                                     NJS_DATE_ARG, NJS_NUMBER_ARG),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_long_string("setUTCMilliseconds"),
        .value = njs_native_function(njs_date_prototype_set_milliseconds,
                                     NJS_DATE_ARG, NJS_NUMBER_ARG),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("setSeconds"),
        .value = njs_native_function(njs_date_prototype_set_seconds,
                                     NJS_DATE_ARG, NJS_NUMBER_ARG),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("setUTCSeconds"),
        .value = njs_native_function(njs_date_prototype_set_seconds,
                     NJS_DATE_ARG, NJS_NUMBER_ARG, NJS_NUMBER_ARG),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("setMinutes"),
        .value = njs_native_function(njs_date_prototype_set_minutes,
                     NJS_DATE_ARG, NJS_NUMBER_ARG, NJS_NUMBER_ARG,
                     NJS_NUMBER_ARG),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("setUTCMinutes"),
        .value = njs_native_function(njs_date_prototype_set_utc_minutes,
                     NJS_DATE_ARG, NJS_NUMBER_ARG, NJS_NUMBER_ARG,
                     NJS_NUMBER_ARG),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("setHours"),
        .value = njs_native_function(njs_date_prototype_set_hours,
                     NJS_DATE_ARG, NJS_NUMBER_ARG, NJS_NUMBER_ARG,
                     NJS_NUMBER_ARG, NJS_NUMBER_ARG),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("setUTCHours"),
        .value = njs_native_function(njs_date_prototype_set_utc_hours,
                     NJS_DATE_ARG, NJS_NUMBER_ARG, NJS_NUMBER_ARG,
                     NJS_NUMBER_ARG, NJS_NUMBER_ARG),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("setDate"),
        .value = njs_native_function(njs_date_prototype_set_date,
                     NJS_DATE_ARG, NJS_NUMBER_ARG),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("setUTCDate"),
        .value = njs_native_function(njs_date_prototype_set_utc_date,
                     NJS_DATE_ARG, NJS_NUMBER_ARG),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("setMonth"),
        .value = njs_native_function(njs_date_prototype_set_month,
                     NJS_DATE_ARG, NJS_NUMBER_ARG, NJS_NUMBER_ARG),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("setUTCMonth"),
        .value = njs_native_function(njs_date_prototype_set_utc_month,
                     NJS_DATE_ARG, NJS_NUMBER_ARG, NJS_NUMBER_ARG),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("setFullYear"),
        .value = njs_native_function(njs_date_prototype_set_full_year,
                     NJS_DATE_ARG, NJS_NUMBER_ARG, NJS_NUMBER_ARG,
                     NJS_NUMBER_ARG),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("setUTCFullYear"),
        .value = njs_native_function(njs_date_prototype_set_utc_full_year,
                     NJS_DATE_ARG, NJS_NUMBER_ARG, NJS_NUMBER_ARG,
                     NJS_NUMBER_ARG),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("toJSON"),
        .value = njs_native_function(njs_date_prototype_to_json, 0),
        .writable = 1,
        .configurable = 1,
    },
};


const njs_object_init_t  njs_date_prototype_init = {
    njs_str("Date"),
    njs_date_prototype_properties,
    njs_nitems(njs_date_prototype_properties),
};
