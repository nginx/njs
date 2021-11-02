
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) NGINX, Inc.
 */


#include <njs_main.h>


#define NJS_DATE_TIME_LEN                                                     \
    sizeof("Mon Sep 28 1970 12:00:00 GMT+0600 (XXXXX)")

#define NJS_DATE_MAX_FIELDS             8
#define NJS_DATE_WDAY                   0
#define NJS_DATE_YR                     1
#define NJS_DATE_MON                    2
#define NJS_DATE_DAY                    3
#define NJS_DATE_HR                     4
#define NJS_DATE_MIN                    5
#define NJS_DATE_SEC                    6
#define NJS_DATE_MSEC                   7


#define njs_date_magic(field, local)                                          \
    ((local << 6) + field)


#define njs_date_magic2(since, len, local)                                    \
    ((local << 6) + ((len & 7) << 3) + since)


typedef enum {
    NJS_DATE_FMT_TO_TIME_STRING,
    NJS_DATE_FMT_TO_DATE_STRING,
    NJS_DATE_FMT_TO_STRING,
    NJS_DATE_FMT_TO_UTC_STRING,
    NJS_DATE_FMT_TO_ISO_STRING,
} njs_date_fmt_t;


static double njs_date_string_parse(njs_value_t *date);
static double njs_date_rfc2822_string_parse(int64_t tm[], const u_char *p,
    const u_char *end);
static double njs_date_js_string_parse(int64_t tm[], const u_char *p,
    const u_char *end);
static const u_char *njs_date_skip_week_day(const u_char *p, const u_char *end);
static const u_char *njs_date_skip_spaces(const u_char *p, const u_char *end);
static njs_int_t njs_date_month_parse(const u_char *p, const u_char *end);
static const u_char *njs_date_time_parse(int64_t tm[], const u_char *p,
    const u_char *end);
static int64_t njs_date_gmtoff_parse(const u_char *start, const u_char *end);
static const u_char *njs_date_number_parse(int64_t *value, const u_char *p,
    const u_char *end, size_t size);
static njs_int_t njs_date_string(njs_vm_t *vm, njs_value_t *retval,
    njs_date_fmt_t fmt, double time);


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


njs_inline double
njs_make_day(int64_t yr, int64_t month, int64_t date)
{
    double   days;
    int64_t  i, ym, mn, md;

    static const int min_year = -271821;
    static const int max_year = 275760;
    static const int month_days[] = { 31, 28, 31, 30, 31, 30,
                                      31, 31, 30, 31, 30, 31 };

    if (yr < min_year || yr > max_year
        || month < (min_year * 12) || month > (max_year * 12)
        || date < (min_year * 12 * 366) || date > (max_year * 12 * 366))
    {
        return NAN;
    }

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

#if (NJS_TIME_T_SIZE < 8)

    /* Smart truncation. */

    if ((time_t) -1 < 0) {
        if (time < INT32_MIN) {
            time = INT32_MIN;

        } else if (time > INT32_MAX) {
            time = INT32_MAX;
        }

    } else {
        if (time < 0) {
            time = 0;

        } else if (time > UINT32_MAX) {
            time = UINT32_MAX;
        }
    }

#endif

    ti = time;
    localtime_r(&ti, &tm);

    /*
     * As njs_timezone(&tm) may return value which is not a multiple of 60
     * secs (see "zdump -v /etc/localtime" for MSK zone) rounding it to
     * minutes precision here to ensure:
     * var date = new Date(<args>)
     * date.valueOf() - date.getTimezoneOffset() * 60000 == Date.UTC(<args>)
     * which is expected by test262.
     */

    return -njs_timezone(&tm) / 60;
}


njs_inline int64_t
njs_year_from_days(int64_t *days)
{
    int64_t  y, d1, nd, d;

    d = *days;

    y = njs_floor_div(d * 10000, 3652425) + 1970;

    for ( ;; ) {
        d1 = d - njs_days_from_year(y);

        if (d1 < 0) {
            y--;

        } else {
            nd = njs_days_in_year(y);

            if (d1 < nd) {
                break;
            }

            y++;
        }
    }

    *days = d1;

    return y;
}


njs_inline double
njs_make_date(int64_t tm[], njs_bool_t local)
{
    double  time, days;

    days = njs_make_day(tm[NJS_DATE_YR], tm[NJS_DATE_MON],
                        tm[NJS_DATE_DAY]);

    time = ((tm[NJS_DATE_HR] * 60.0 + tm[NJS_DATE_MIN]) * 60.0
            + tm[NJS_DATE_SEC]) * 1000.0 + tm[NJS_DATE_MSEC];

    time += days * 86400000.0;

    if (local) {
        time += njs_tz_offset(time) * 60000;
    }

    return njs_timeclip(time);
}


njs_inline int64_t
njs_destruct_date(double time, int64_t tm[], int index, njs_bool_t local)
{
    int64_t  days, wd, y, i, md, h, m, s, ms;

    static const int month_days[] = { 31, 28, 31, 30, 31, 30,
                                      31, 31, 30, 31, 30, 31 };

    if (njs_slow_path(isnan(time))) {
        time = 0;

    } else if (local) {
        time -= njs_tz_offset(time) * 60000;
    }

    h = njs_mod(time, 86400000);
    days = (time - h) / 86400000;
    ms = h % 1000;
    h = (h - ms) / 1000;
    s = h % 60;
    h = (h - s) / 60;
    m = h % 60;
    h = (h - m) / 60;
    wd = njs_mod(days + 4, 7);
    y = njs_year_from_days(&days);

    for (i = 0; i < 11; i++) {
        md = month_days[i];

        if (i == 1) {
            /* Leap day. */
            md += njs_days_in_year(y) - 365;
        }

        if (days < md) {
            break;
        }

        days -= md;
    }

    tm[NJS_DATE_YR] = y;
    tm[NJS_DATE_MON] = i;
    tm[NJS_DATE_DAY] = days + 1;
    tm[NJS_DATE_HR] = h;
    tm[NJS_DATE_MIN] = m;
    tm[NJS_DATE_SEC] = s;
    tm[NJS_DATE_MSEC] = ms;
    tm[NJS_DATE_WDAY] = wd;

    return tm[index];
}


static njs_int_t
njs_date_args(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    int64_t tm[])
{
    double      num;
    njs_int_t   ret;
    njs_uint_t  i, n;

    njs_memzero(tm, NJS_DATE_MAX_FIELDS * sizeof(int64_t));

    tm[NJS_DATE_DAY] = 1;

    n = njs_min(8, nargs);

    for (i = 1; i < n; i++) {
        ret = njs_value_to_number(vm, &args[i], &num);
        if (njs_slow_path(ret != NJS_OK)) {
            return ret;
        }

        if (!isfinite(num)) {
            tm[NJS_DATE_YR] = INT64_MIN;
            continue;
        }

        tm[i] = njs_number_to_integer(num);
    }

    if (tm[NJS_DATE_YR] >= 0 && tm[NJS_DATE_YR] < 100) {
        tm[NJS_DATE_YR] += 1900;
    }

    return NJS_OK;
}


njs_date_t *
njs_date_alloc(njs_vm_t *vm, double time)
{
    njs_date_t  *date;

    date = njs_mp_alloc(vm->mem_pool, sizeof(njs_date_t));
    if (njs_slow_path(date == NULL)) {
        njs_memory_error(vm);
        return NULL;
    }

    njs_lvlhsh_init(&date->object.hash);
    njs_lvlhsh_init(&date->object.shared_hash);
    date->object.type = NJS_DATE;
    date->object.shared = 0;
    date->object.extensible = 1;
    date->object.error_data = 0;
    date->object.fast_array = 0;
    date->object.__proto__ = &vm->prototypes[NJS_OBJ_TYPE_DATE].object;
    date->object.slots = NULL;

    date->time = time;

    return date;
}


static njs_int_t
njs_date_constructor(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t unused)
{
    double      time;
    njs_int_t   ret;
    njs_date_t  *date;
    int64_t     tm[NJS_DATE_MAX_FIELDS];

    if (!vm->top_frame->ctor) {
        return njs_date_string(vm, &vm->retval, NJS_DATE_FMT_TO_STRING,
                               njs_gettime());
    }

    if (nargs == 1) {
        time = njs_gettime();

    } else if (nargs == 2) {
        if (njs_is_object(&args[1])) {
            if (!njs_is_date(&args[1])) {
                ret = njs_value_to_primitive(vm, &args[1], &args[1], 0);
                if (njs_slow_path(ret != NJS_OK)) {
                    return ret;
                }
            }
        }

        if (njs_is_date(&args[1])) {
            time = njs_date(&args[1])->time;

        } else if (njs_is_string(&args[1])) {
            time = njs_date_string_parse(&args[1]);

        } else {
            time = njs_timeclip(njs_number(&args[1]));
        }

    } else {

        ret = njs_date_args(vm, args, nargs, tm);
        if (njs_slow_path(ret != NJS_OK)) {
            return ret;
        }

        time = njs_make_date(tm, 1);
    }

    date = njs_date_alloc(vm, time);
    if (njs_slow_path(date == NULL)) {
        return NJS_ERROR;
    }

    njs_set_date(&vm->retval, date);

    return NJS_OK;
}


static njs_int_t
njs_date_utc(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t unused)
{
    double      time;
    njs_int_t   ret;
    int64_t     tm[NJS_DATE_MAX_FIELDS];

    time = NAN;

    if (nargs > 1) {
        ret = njs_date_args(vm, args, nargs, tm);
        if (njs_slow_path(ret != NJS_OK)) {
            return ret;
        }

        time = njs_make_date(tm, 0);
    }

    njs_set_number(&vm->retval, time);

    return NJS_OK;
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
    double     time;
    njs_int_t  ret;

    if (nargs > 1) {
        if (njs_slow_path(!njs_is_string(&args[1]))) {
            ret = njs_value_to_string(vm, &args[1], &args[1]);
            if (njs_slow_path(ret != NJS_OK)) {
                return ret;
            }
        }

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
    size_t         ms_length;
    int64_t        ext, skipped;
    njs_str_t      string;
    njs_bool_t     sign, week, utc;
    const u_char   *p, *next, *end;
    int64_t        tm[NJS_DATE_MAX_FIELDS];

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

    tm[NJS_DATE_MON] = 0;
    tm[NJS_DATE_DAY] = 1;
    tm[NJS_DATE_HR] = 0;
    tm[NJS_DATE_MIN] = 0;
    tm[NJS_DATE_SEC] = 0;
    tm[NJS_DATE_MSEC] = 0;

    next = njs_date_number_parse(&tm[NJS_DATE_YR], p, end, 4);

    if (next != NULL) {
        /* ISO-8601 format: "1970-09-28T06:00:00.000Z" */

        if (next == end) {
            goto done;
        }

        if (*next != '-') {
            /* Extended ISO-8601 format: "+001970-09-28T06:00:00.000Z" */

            next = njs_date_number_parse(&ext, next, end, 2);
            if (njs_slow_path(next == NULL)) {
                return NAN;
            }

            tm[NJS_DATE_YR] *= 100;
            tm[NJS_DATE_YR] += ext;

            if (string.start[0] == '-') {
                if (tm[NJS_DATE_YR] == 0) {
                    return NAN;
                }

                tm[NJS_DATE_YR] = -tm[NJS_DATE_YR];
            }

            if (next == end) {
                goto done;
            }

            if (*next != '-') {
                return NAN;
            }
        }

        p = njs_date_number_parse(&tm[NJS_DATE_MON], next + 1, end, 2);
        if (njs_slow_path(p == NULL)) {
            return NAN;
        }

        tm[NJS_DATE_MON]--;

        if (p == end) {
            goto done;
        }

        if (njs_slow_path(*p != '-')) {
            return NAN;
        }

        p = njs_date_number_parse(&tm[NJS_DATE_DAY], p + 1, end, 2);
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

        p = njs_date_time_parse(tm, p + 1, end);
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

        p = njs_date_number_parse(&tm[NJS_DATE_MSEC], p, end, ms_length);
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
            tm[NJS_DATE_MSEC] *= 100;

        } else if (ms_length == 2) {
            tm[NJS_DATE_MSEC] *= 10;
        }

        return njs_make_date(tm, !utc);
    }

    if (sign) {
        return NAN;
    }

    week = 1;

    for ( ;; ) {
        next = njs_date_number_parse(&tm[NJS_DATE_DAY], p, end, 2);

        if (next != NULL) {
            /*
             * RFC 2822 format:
             *   "Mon, 28 Sep 1970 06:00:00 GMT",
             *   "Mon, 28 Sep 1970 06:00:00 UTC",
             *   "Mon, 28 Sep 1970 12:00:00 +0600".
             */
            return njs_date_rfc2822_string_parse(tm, next, end);
        }

        tm[NJS_DATE_MON] = njs_date_month_parse(p, end);

        if (tm[NJS_DATE_MON] >= 0) {
            /* Date.toString() format: "Mon Sep 28 1970 12:00:00 GMT+0600". */

            return njs_date_js_string_parse(tm, p + 3, end);
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

done:

    return njs_make_date(tm, 0);
}


static double
njs_date_rfc2822_string_parse(int64_t tm[], const u_char *p, const u_char *end)
{
    int64_t  gmtoff;

    p = njs_date_skip_spaces(p, end);
    if (njs_slow_path(p == NULL)) {
        return NAN;
    }

    tm[NJS_DATE_MON] = njs_date_month_parse(p, end);
    if (njs_slow_path(tm[NJS_DATE_MON] < 0)) {
        return NAN;
    }

    p = njs_date_skip_spaces(p + 3, end);
    if (njs_slow_path(p == NULL)) {
        return NAN;
    }

    p = njs_date_number_parse(&tm[NJS_DATE_YR], p, end, 4);
    if (njs_slow_path(p == NULL)) {
        return NAN;
    }

    gmtoff = 0;

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

done:

    tm[NJS_DATE_MSEC] = -gmtoff * 60000;

    return njs_make_date(tm, 0);
}


static double
njs_date_js_string_parse(int64_t tm[], const u_char *p, const u_char *end)
{
    int64_t  gmtoff;

    p = njs_date_skip_spaces(p, end);
    if (njs_slow_path(p == NULL)) {
        return NAN;
    }

    p = njs_date_number_parse(&tm[NJS_DATE_DAY], p, end, 2);
    if (njs_slow_path(p == NULL)) {
        return NAN;
    }

    p = njs_date_skip_spaces(p, end);
    if (njs_slow_path(p == NULL)) {
        return NAN;
    }

    p = njs_date_number_parse(&tm[NJS_DATE_YR], p, end, 4);
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
            tm[NJS_DATE_MSEC] = -gmtoff * 60000;
            return njs_make_date(tm, 0);
        }
    }

    return NAN;

done:

    return njs_make_date(tm, 0);
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
njs_date_time_parse(int64_t tm[], const u_char *p, const u_char *end)
{
    p = njs_date_number_parse(&tm[NJS_DATE_HR], p, end, 2);
    if (njs_slow_path(p == NULL)) {
        return p;
    }

    if (njs_slow_path(p >= end || *p != ':')) {
        return NULL;
    }

    p = njs_date_number_parse(&tm[NJS_DATE_MIN], p + 1, end, 2);
    if (njs_slow_path(p == NULL)) {
        return p;
    }

    if (p == end) {
        return p;
    }

    if (njs_slow_path(*p != ':')) {
        return NULL;
    }

    return njs_date_number_parse(&tm[NJS_DATE_SEC], p + 1, end, 2);
}


static int64_t
njs_date_gmtoff_parse(const u_char *start, const u_char *end)
{
    int64_t       gmtoff, hour, min;
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
njs_date_number_parse(int64_t *value, const u_char *p, const u_char *end,
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
    {
        .type = NJS_PROPERTY,
        .name = njs_string("name"),
        .value = njs_string("Date"),
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("length"),
        .value = njs_value(NJS_NUMBER, 1, 7.0),
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY_HANDLER,
        .name = njs_string("prototype"),
        .value = njs_prop_handler(njs_object_prototype_create),
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("UTC"),
        .value = njs_native_function(njs_date_utc, 7),
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
        .value = njs_native_function(njs_date_parse, 1),
        .writable = 1,
        .configurable = 1,
    },
};


const njs_object_init_t  njs_date_constructor_init = {
    njs_date_constructor_properties,
    njs_nitems(njs_date_constructor_properties),
};


static njs_int_t
njs_date_prototype_value_of(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t unused)
{
    if (njs_slow_path(!njs_is_date(&args[0]))) {
        njs_type_error(vm, "cannot convert %s to date",
                       njs_type_string(args[0].type));

        return NJS_ERROR;
    }

    njs_set_number(&vm->retval, njs_date(&args[0])->time);

    return NJS_OK;
}


static njs_int_t
njs_date_prototype_to_string(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t fmt)
{
    double  time;

    if (njs_slow_path(!njs_is_date(&args[0]))) {
        njs_type_error(vm, "cannot convert %s to date",
                       njs_type_string(args[0].type));

        return NJS_ERROR;
    }

    time = njs_date(&args[0])->time;

    if (fmt == NJS_DATE_FMT_TO_ISO_STRING && isnan(time)) {
        njs_range_error(vm, "Invalid time value");
        return NJS_ERROR;
    }

    return njs_date_string(vm, &vm->retval, fmt, time);
}


static njs_int_t
njs_date_string(njs_vm_t *vm, njs_value_t *retval, njs_date_fmt_t fmt,
    double time)
{
    int      year, tz;
    u_char   *p, sign;
    u_char   buf[NJS_DATE_TIME_LEN];
    int64_t  tm[NJS_DATE_MAX_FIELDS];

    static const char  *week[] = { "Sun", "Mon", "Tue", "Wed",
                                   "Thu", "Fri", "Sat" };

    static const char  *month[] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                    "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };

    if (njs_slow_path(isnan(time))) {
        *retval = njs_string_invalid_date;
        return NJS_OK;
    }

    p = buf;

    switch (fmt) {
    case NJS_DATE_FMT_TO_ISO_STRING:
    case NJS_DATE_FMT_TO_UTC_STRING:
        njs_destruct_date(time, tm, 0, 0);
        year = tm[NJS_DATE_YR];

        if (fmt == NJS_DATE_FMT_TO_UTC_STRING) {
            p = njs_sprintf(p, buf + NJS_DATE_TIME_LEN,
                            "%s, %02L %s %04d %02L:%02L:%02L GMT",
                            week[tm[NJS_DATE_WDAY]], tm[NJS_DATE_DAY],
                            month[tm[NJS_DATE_MON]], year, tm[NJS_DATE_HR],
                            tm[NJS_DATE_MIN], tm[NJS_DATE_SEC]);

            break;
        }

        if (year >= 0 && year <= 9999) {
            p = njs_sprintf(p, buf + NJS_DATE_TIME_LEN, "%04d", year);

        } else {
            if (year > 0) {
                *p++ = '+';
            }

            p = njs_sprintf(p, buf + NJS_DATE_TIME_LEN, "%06d", year);
        }

        p = njs_sprintf(p, buf + NJS_DATE_TIME_LEN,
                        "-%02L-%02LT%02L:%02L:%02L.%03LZ",
                        tm[NJS_DATE_MON] + 1, tm[NJS_DATE_DAY], tm[NJS_DATE_HR],
                        tm[NJS_DATE_MIN], tm[NJS_DATE_SEC], tm[NJS_DATE_MSEC]);

        break;

    case NJS_DATE_FMT_TO_TIME_STRING:
    case NJS_DATE_FMT_TO_DATE_STRING:
    case NJS_DATE_FMT_TO_STRING:
    default:
        njs_destruct_date(time, tm, 0, 1);

        if (fmt != NJS_DATE_FMT_TO_TIME_STRING) {
            p = njs_sprintf(p, buf + NJS_DATE_TIME_LEN,
                            "%s %s %02L %04L",
                            week[tm[NJS_DATE_WDAY]], month[tm[NJS_DATE_MON]],
                            tm[NJS_DATE_DAY], tm[NJS_DATE_YR]);
        }

        if (fmt != NJS_DATE_FMT_TO_DATE_STRING) {
            tz = -njs_tz_offset(time);
            sign = (tz < 0) ? '-' : '+';

            if (tz < 0) {
                tz = -tz;
            }

            if (p != buf) {
                *p++ = ' ';
            }

            p = njs_sprintf(p, buf + NJS_DATE_TIME_LEN,
                            "%02L:%02L:%02L GMT%c%02d%02d",
                            tm[NJS_DATE_HR], tm[NJS_DATE_MIN], tm[NJS_DATE_SEC],
                            sign, tz / 60, tz % 60);
        }
    }

    return njs_string_new(vm, retval, buf, p - buf, p - buf);
}


njs_int_t
njs_date_to_string(njs_vm_t *vm, njs_value_t *retval, const njs_value_t *date)
{
    if (njs_slow_path(!njs_is_date(date))) {
        njs_type_error(vm, "cannot convert %s to date",
                       njs_type_string(date->type));

        return NJS_ERROR;
    }

    return njs_date_string(vm, retval, NJS_DATE_FMT_TO_ISO_STRING,
                           njs_date(date)->time);
}


static njs_int_t
njs_date_prototype_get_field(njs_vm_t *vm, njs_value_t *args,
    njs_uint_t nargs, njs_index_t magic)
{
    double   value;
    int64_t  tm[NJS_DATE_MAX_FIELDS];

    if (njs_slow_path(!njs_is_date(&args[0]))) {
        njs_type_error(vm, "cannot convert %s to date",
                       njs_type_string(args[0].type));

        return NJS_ERROR;
    }

    value = njs_date(&args[0])->time;

    if (njs_fast_path(!isnan(value))) {
        value = njs_destruct_date(value, tm, magic & 0xf, magic & 0x40);
    }

    njs_set_number(&vm->retval, value);

    return NJS_OK;
}


static njs_int_t
njs_date_prototype_get_timezone_offset(njs_vm_t *vm, njs_value_t *args,
    njs_uint_t nargs, njs_index_t unused)
{
    double  value;

    if (njs_slow_path(!njs_is_date(&args[0]))) {
        njs_type_error(vm, "cannot convert %s to date",
                       njs_type_string(args[0].type));

        return NJS_ERROR;
    }

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
    double     time;
    njs_int_t  ret;

    if (njs_slow_path(!njs_is_date(&args[0]))) {
        njs_type_error(vm, "cannot convert %s to date",
                       njs_type_string(args[0].type));

        return NJS_ERROR;
    }

    if (nargs > 1) {
        if (njs_slow_path(!njs_is_number(&args[1]))) {
            ret = njs_value_to_numeric(vm, &args[1], &args[1]);
            if (njs_slow_path(ret != NJS_OK)) {
                return ret;
            }
        }

        time = njs_timeclip(njs_number(&args[1]));

    } else {
        time = NAN;
    }

    njs_date(&args[0])->time = time;
    njs_set_number(&vm->retval, time);

    return NJS_OK;
}


static njs_int_t
njs_date_prototype_set_fields(njs_vm_t *vm, njs_value_t *args,
    njs_uint_t nargs, njs_index_t magic)
{
    double      time, num;
    njs_int_t   ret;
    njs_uint_t  since, left, i;
    int64_t     tm[NJS_DATE_MAX_FIELDS];

    if (njs_slow_path(!njs_is_date(&args[0]))) {
        njs_type_error(vm, "cannot convert %s to date",
                       njs_type_string(args[0].type));

        return NJS_ERROR;
    }

    time = njs_date(&args[0])->time;

    since = magic & 7;

    if (njs_slow_path(nargs < 2 || (since != NJS_DATE_YR && isnan(time)))) {
        time = NAN;
        goto done;
    }

    i = 1;
    left = njs_min(((magic >> 3) & 7), nargs - 1);

    njs_destruct_date(time, tm, 0, magic & 0x40);

    do {
        ret = njs_value_to_number(vm, &args[i++], &num);
        if (njs_slow_path(ret != NJS_OK)) {
            return ret;
        }

        if (!isfinite(num)) {
            tm[NJS_DATE_YR] = INT64_MIN;
            continue;
        }

        tm[since++] = njs_number_to_integer(num);

    } while (--left);

    time = njs_make_date(tm, 1);

done:

    njs_date(&args[0])->time = time;
    njs_set_number(&vm->retval, time);

    return NJS_OK;
}


static njs_int_t
njs_date_prototype_to_json(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t retval)
{
    njs_int_t           ret;
    njs_value_t         value;
    njs_lvlhsh_query_t  lhq;

    static const njs_value_t  to_iso_string = njs_string("toISOString");

    if (njs_is_object(&args[0])) {
        njs_object_property_init(&lhq, &to_iso_string, NJS_TO_ISO_STRING_HASH);

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
        .value = njs_native_function(njs_date_prototype_value_of, 0),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("toString"),
        .value = njs_native_function2(njs_date_prototype_to_string, 0,
                                      NJS_DATE_FMT_TO_STRING),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("toDateString"),
        .value = njs_native_function2(njs_date_prototype_to_string, 0,
                                      NJS_DATE_FMT_TO_DATE_STRING),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("toTimeString"),
        .value = njs_native_function2(njs_date_prototype_to_string, 0,
                                      NJS_DATE_FMT_TO_TIME_STRING),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("toLocaleString"),
        .value = njs_native_function2(njs_date_prototype_to_string, 0,
                                      NJS_DATE_FMT_TO_STRING),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_long_string("toLocaleDateString"),
        .value = njs_native_function2(njs_date_prototype_to_string, 0,
                                      NJS_DATE_FMT_TO_DATE_STRING),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_long_string("toLocaleTimeString"),
        .value = njs_native_function2(njs_date_prototype_to_string, 0,
                                      NJS_DATE_FMT_TO_TIME_STRING),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("toUTCString"),
        .value = njs_native_function2(njs_date_prototype_to_string, 0,
                                      NJS_DATE_FMT_TO_UTC_STRING),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("toISOString"),
        .value = njs_native_function2(njs_date_prototype_to_string, 0,
                                      NJS_DATE_FMT_TO_ISO_STRING),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("toJSON"),
        .value = njs_native_function(njs_date_prototype_to_json, 1),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("getTime"),
        .value = njs_native_function(njs_date_prototype_value_of, 0),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("getFullYear"),
        .value = njs_native_function2(njs_date_prototype_get_field, 0,
                                      njs_date_magic(NJS_DATE_YR, 1)),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("getUTCFullYear"),
        .value = njs_native_function2(njs_date_prototype_get_field, 0,
                                      njs_date_magic(NJS_DATE_YR, 0)),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("getMonth"),
        .value = njs_native_function2(njs_date_prototype_get_field, 0,
                                      njs_date_magic(NJS_DATE_MON, 1)),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("getUTCMonth"),
        .value = njs_native_function2(njs_date_prototype_get_field, 0,
                                      njs_date_magic(NJS_DATE_MON, 0)),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("getDate"),
        .value = njs_native_function2(njs_date_prototype_get_field, 0,
                                      njs_date_magic(NJS_DATE_DAY, 1)),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("getUTCDate"),
        .value = njs_native_function2(njs_date_prototype_get_field, 0,
                                      njs_date_magic(NJS_DATE_DAY, 0)),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("getDay"),
        .value = njs_native_function2(njs_date_prototype_get_field, 0,
                                      njs_date_magic(NJS_DATE_WDAY, 1)),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("getUTCDay"),
        .value = njs_native_function2(njs_date_prototype_get_field, 0,
                                      njs_date_magic(NJS_DATE_WDAY, 0)),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("getHours"),
        .value = njs_native_function2(njs_date_prototype_get_field, 0,
                                      njs_date_magic(NJS_DATE_HR, 1)),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("getUTCHours"),
        .value = njs_native_function2(njs_date_prototype_get_field, 0,
                                      njs_date_magic(NJS_DATE_HR, 0)),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("getMinutes"),
        .value = njs_native_function2(njs_date_prototype_get_field, 0,
                                      njs_date_magic(NJS_DATE_MIN, 1)),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("getUTCMinutes"),
        .value = njs_native_function2(njs_date_prototype_get_field, 0,
                                      njs_date_magic(NJS_DATE_MIN, 0)),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("getSeconds"),
        .value = njs_native_function2(njs_date_prototype_get_field, 0,
                                      njs_date_magic(NJS_DATE_SEC, 0)),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("getUTCSeconds"),
        .value = njs_native_function2(njs_date_prototype_get_field, 0,
                                      njs_date_magic(NJS_DATE_SEC, 1)),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_long_string("getMilliseconds"),
        .value = njs_native_function2(njs_date_prototype_get_field, 0,
                                      njs_date_magic(NJS_DATE_MSEC, 0)),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_long_string("getUTCMilliseconds"),
        .value = njs_native_function2(njs_date_prototype_get_field, 0,
                                      njs_date_magic(NJS_DATE_MSEC, 1)),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_long_string("getTimezoneOffset"),
        .value = njs_native_function(njs_date_prototype_get_timezone_offset, 0),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("setTime"),
        .value = njs_native_function(njs_date_prototype_set_time, 1),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_long_string("setMilliseconds"),
        .value = njs_native_function2(njs_date_prototype_set_fields, 1,
                                      njs_date_magic2(NJS_DATE_MSEC, 1, 1)),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_long_string("setUTCMilliseconds"),
        .value = njs_native_function2(njs_date_prototype_set_fields, 1,
                                      njs_date_magic2(NJS_DATE_MSEC, 1, 0)),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("setSeconds"),
        .value = njs_native_function2(njs_date_prototype_set_fields, 2,
                                      njs_date_magic2(NJS_DATE_SEC, 2, 1)),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("setUTCSeconds"),
        .value = njs_native_function2(njs_date_prototype_set_fields, 2,
                                      njs_date_magic2(NJS_DATE_SEC, 2, 0)),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("setMinutes"),
        .value = njs_native_function2(njs_date_prototype_set_fields, 3,
                                      njs_date_magic2(NJS_DATE_MIN, 3, 1)),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("setUTCMinutes"),
        .value = njs_native_function2(njs_date_prototype_set_fields, 3,
                                      njs_date_magic2(NJS_DATE_MIN, 3, 0)),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("setHours"),
        .value = njs_native_function2(njs_date_prototype_set_fields, 4,
                                      njs_date_magic2(NJS_DATE_HR, 4, 1)),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("setUTCHours"),
        .value = njs_native_function2(njs_date_prototype_set_fields, 4,
                                      njs_date_magic2(NJS_DATE_HR, 4, 0)),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("setDate"),
        .value = njs_native_function2(njs_date_prototype_set_fields, 1,
                                      njs_date_magic2(NJS_DATE_DAY, 1, 1)),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("setUTCDate"),
        .value = njs_native_function2(njs_date_prototype_set_fields, 1,
                                      njs_date_magic2(NJS_DATE_DAY, 1, 0)),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("setMonth"),
        .value = njs_native_function2(njs_date_prototype_set_fields, 2,
                                      njs_date_magic2(NJS_DATE_MON, 2, 1)),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("setUTCMonth"),
        .value = njs_native_function2(njs_date_prototype_set_fields, 2,
                                      njs_date_magic2(NJS_DATE_MON, 2, 0)),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("setFullYear"),
        .value = njs_native_function2(njs_date_prototype_set_fields, 3,
                                      njs_date_magic2(NJS_DATE_YR, 3, 1)),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("setUTCFullYear"),
        .value = njs_native_function2(njs_date_prototype_set_fields, 3,
                                      njs_date_magic2(NJS_DATE_YR, 3, 0)),
        .writable = 1,
        .configurable = 1,
    },
};


const njs_object_init_t  njs_date_prototype_init = {
    njs_date_prototype_properties,
    njs_nitems(njs_date_prototype_properties),
};


const njs_object_type_init_t  njs_date_type_init = {
   .constructor = njs_native_ctor(njs_date_constructor, 7, 0),
   .constructor_props = &njs_date_constructor_init,
   .prototype_props = &njs_date_prototype_init,
   .prototype_value = { .object = { .type = NJS_OBJECT } },
};

