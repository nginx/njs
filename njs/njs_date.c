
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) NGINX, Inc.
 */

#include <nxt_auto_config.h>
#include <nxt_types.h>
#include <nxt_clang.h>
#include <nxt_alignment.h>
#include <nxt_string.h>
#include <nxt_stub.h>
#include <nxt_djb_hash.h>
#include <nxt_array.h>
#include <nxt_lvlhsh.h>
#include <nxt_random.h>
#include <nxt_time.h>
#include <nxt_malloc.h>
#include <nxt_mem_cache_pool.h>
#include <njscript.h>
#include <njs_vm.h>
#include <njs_number.h>
#include <njs_string.h>
#include <njs_object.h>
#include <njs_object_hash.h>
#include <njs_function.h>
#include <njs_date.h>
#include <njs_error.h>
#include <stdio.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>


/*
 * njs_timegm() is used because
 *   Solaris lacks timegm(),
 *   FreeBSD and MacOSX timegm() cannot handle years before 1900.
 */

#define NJS_ISO_DATE_TIME_LEN  sizeof("+001970-09-28T12:00:00.000Z")

#define NJS_DATE_TIME_LEN                                                     \
    sizeof("Mon Sep 28 1970 12:00:00 GMT+0600 (XXXXX)")


static nxt_noinline double njs_date_string_parse(njs_value_t *date);
static double njs_date_rfc2822_string_parse(struct tm *tm, const u_char *p,
    const u_char *end);
static double njs_date_js_string_parse(struct tm *tm, const u_char *p,
    const u_char *end);
static const u_char *njs_date_skip_week_day(const u_char *p, const u_char *end);
static const u_char *njs_date_skip_spaces(const u_char *p, const u_char *end);
static nxt_noinline nxt_int_t njs_date_month_parse(const u_char *p,
    const u_char *end);
static nxt_noinline const u_char *njs_date_time_parse(struct tm *tm,
    const u_char *p, const u_char *end);
static nxt_noinline nxt_int_t njs_date_gmtoff_parse(const u_char *start,
    const u_char *end);
static nxt_noinline const u_char *njs_date_number_parse(int *value,
    const u_char *p, const u_char *end, size_t size);
static int64_t njs_timegm(struct tm *tm);
static nxt_noinline njs_ret_t njs_date_string(njs_vm_t *vm, const char *fmt,
    double time);
static nxt_noinline double njs_date_time(struct tm *tm, int64_t ms);
static double njs_date_utc_time(struct tm *tm, double time);
static njs_ret_t njs_date_prototype_to_json_continuation(njs_vm_t *vm,
    njs_value_t *args, nxt_uint_t nargs, njs_index_t retval);


static const njs_value_t  njs_string_invalid_date = njs_string("Invalid Date");


static nxt_noinline uint64_t
njs_gettime(void)
{
    struct timeval  tv;

    gettimeofday(&tv, NULL);

    return (uint64_t) tv.tv_sec * 1000 + tv.tv_usec / 1000;
}


njs_ret_t
njs_date_constructor(njs_vm_t *vm, njs_value_t *args, nxt_uint_t nargs,
    njs_index_t unused)
{
    double      num, time;
    int64_t     values[8];
    nxt_uint_t  i, n;
    njs_date_t  *date;
    struct tm   tm;

    if (vm->top_frame->ctor) {

        if (nargs == 1) {
            time = njs_gettime();

        } else if (nargs == 2 && njs_is_string(&args[1])) {
            time = njs_date_string_parse(&args[1]);

        } else {
            memset(values, 0, 8 * sizeof(int64_t));
            /* Month. */
            values[2] = 1;

            n = nxt_min(8, nargs);

            for (i = 1; i < n; i++) {
                if (!njs_is_numeric(&args[i])) {
                    njs_vm_trap_value(vm, &args[i]);
                    return NJS_TRAP_NUMBER_ARG;
                }

                num = args[i].data.u.number;

                if (isnan(num)) {
                    time = num;
                    goto done;
                }

                values[i] = num;
            }

            if (nargs > 2) {
                /* Year. */
                if (values[1] > 99) {
                    values[1] -= 1900;
                }

                tm.tm_year = values[1];
                tm.tm_mon = values[2];
                tm.tm_mday = values[3];
                tm.tm_hour = values[4];
                tm.tm_min = values[5];
                tm.tm_sec = values[6];
                tm.tm_isdst = -1;

                time = (int64_t) mktime(&tm) * 1000 + values[7];

            } else {
                time = values[1];
            }
        }

    done:

        date = nxt_mem_cache_alloc(vm->mem_cache_pool, sizeof(njs_date_t));
        if (nxt_slow_path(date == NULL)) {
            return NXT_ERROR;
        }

        nxt_lvlhsh_init(&date->object.hash);
        nxt_lvlhsh_init(&date->object.shared_hash);
        date->object.type = NJS_DATE;
        date->object.shared = 0;
        date->object.extensible = 1;
        date->object.__proto__ = &vm->prototypes[NJS_PROTOTYPE_DATE].object;

        date->time = time;

        vm->retval.data.u.date = date;
        vm->retval.type = NJS_DATE;
        vm->retval.data.truth = 1;

        return NXT_OK;
    }

    return njs_date_string(vm, "%a %b %d %Y %T GMT%z (%Z)", njs_gettime());
}


static njs_ret_t
njs_date_utc(njs_vm_t *vm, njs_value_t *args, nxt_uint_t nargs,
    njs_index_t unused)
{
    double      num, time;
    struct tm   tm;
    nxt_uint_t  i, n;
    int32_t     values[8];

    time = NAN;

    if (nargs > 2) {
        memset(values, 0, 8 * sizeof(int32_t));

        n = nxt_min(8, nargs);

        for (i = 1; i < n; i++) {
            if (!njs_is_numeric(&args[i])) {
                njs_vm_trap_value(vm, &args[i]);
                return NJS_TRAP_NUMBER_ARG;
            }

            num = args[i].data.u.number;

            if (isnan(num)) {
                goto done;
            }

            values[i] = num;
        }

        /* Year. */
        if (values[1] > 99) {
            values[1] -= 1900;
        }

        tm.tm_year = values[1];
        tm.tm_mon = values[2];
        tm.tm_mday = values[3];
        tm.tm_hour = values[4];
        tm.tm_min = values[5];
        tm.tm_sec = values[6];

        time = njs_timegm(&tm) * 1000 + values[7];
    }

done:

    njs_number_set(&vm->retval, time);

    return NXT_OK;
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


static njs_ret_t
njs_date_now(njs_vm_t *vm, njs_value_t *args, nxt_uint_t nargs,
    njs_index_t unused)
{
    njs_number_set(&vm->retval, njs_gettime());

    return NXT_OK;
}


static njs_ret_t
njs_date_parse(njs_vm_t *vm, njs_value_t *args, nxt_uint_t nargs,
    njs_index_t unused)
{
    double  time;

    if (nargs > 1) {
        time = njs_date_string_parse(&args[1]);

    } else {
        time = NAN;
    }

    njs_number_set(&vm->retval, time);

    return NXT_OK;
}


static nxt_noinline double
njs_date_string_parse(njs_value_t *date)
{
    int                ext, ms, ms_length, skipped;
    double             time;
    nxt_str_t          string;
    struct tm          tm;
    nxt_bool_t         sign, week, utc;
    const u_char       *p, *next, *end;

    njs_string_get(date, &string);

    p = string.start;
    end = p + string.length;

    if (nxt_slow_path(p >= end)) {
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
            if (nxt_slow_path(next == NULL)) {
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
        if (nxt_slow_path(p == NULL)) {
            return NAN;
        }

        tm.tm_mon--;

        if (p == end) {
            goto done;
        }

        if (nxt_slow_path(*p != '-')) {
            return NAN;
        }

        p = njs_date_number_parse(&tm.tm_mday, p + 1, end, 2);
        if (nxt_slow_path(p == NULL)) {
            return NAN;
        }

        if (p == end) {
            goto done;
        }

        if (nxt_slow_path(*p != 'T')) {
            return NAN;
        }

        utc = 1;
        end--;

        if (*end != 'Z') {
           utc = 0;
           end++;
        }

        p = njs_date_time_parse(&tm, p + 1, end);
        if (nxt_slow_path(p == NULL)) {
            return NAN;
        }

        if (p == end) {
            goto done;
        }

        if (nxt_slow_path(p > end || *p != '.')) {
            return NAN;
        }

        p++;

        ms_length = (end - p < 3) ? end - p : 3;

        p = njs_date_number_parse(&ms, p, end, ms_length);
        if (nxt_slow_path(p == NULL)) {
            return NAN;
        }

        if (end > p) {
            p = njs_date_number_parse(&skipped, p, end, end - p);
            if (nxt_slow_path(p == NULL)) {
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
        if (nxt_slow_path(p == NULL)) {
            return NAN;
        }

        p = njs_date_skip_spaces(p, end);
        if (nxt_slow_path(p == NULL)) {
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
    if (nxt_slow_path(p == NULL)) {
        return NAN;
    }

    tm->tm_mon = njs_date_month_parse(p, end);
    if (nxt_slow_path(tm->tm_mon < 0)) {
        return NAN;
    }

    p = njs_date_skip_spaces(p + 3, end);
    if (nxt_slow_path(p == NULL)) {
        return NAN;
    }

    p = njs_date_number_parse(&tm->tm_year, p, end, 4);
    if (nxt_slow_path(p == NULL)) {
        return NAN;
    }

    tm->tm_year -= 1900;

    if (p == end) {
        goto done;
    }

    p = njs_date_skip_spaces(p, end);
    if (nxt_slow_path(p == NULL)) {
        return NAN;
    }

    if (p == end) {
        goto done;
    }

    p = njs_date_time_parse(tm, p, end);
    if (nxt_slow_path(p == NULL)) {
        return NAN;
    }

    if (p == end) {
        goto done;
    }

    p = njs_date_skip_spaces(p, end);
    if (nxt_slow_path(p == NULL)) {
        return NAN;
    }

    if (p == end) {
        goto done;
    }

    if (nxt_slow_path(p + 2 >= end)) {
        return NAN;
    }

    if ((p[0] == 'G' && p[1] == 'M' && p[2] == 'T')
        || (p[0] == 'U' && p[1] == 'T' && p[2] == 'C'))
    {
        gmtoff = 0;

    } else {
        gmtoff = njs_date_gmtoff_parse(p, end);

        if (nxt_slow_path(gmtoff == -1)) {
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
    if (nxt_slow_path(p == NULL)) {
        return NAN;
    }

    p = njs_date_number_parse(&tm->tm_mday, p, end, 2);
    if (nxt_slow_path(p == NULL)) {
        return NAN;
    }

    p = njs_date_skip_spaces(p, end);
    if (nxt_slow_path(p == NULL)) {
        return NAN;
    }

    p = njs_date_number_parse(&tm->tm_year, p, end, 4);
    if (nxt_slow_path(p == NULL)) {
        return NAN;
    }

    tm->tm_year -= 1900;

    if (p == end) {
        goto done;
    }

    p = njs_date_skip_spaces(p, end);
    if (nxt_slow_path(p == NULL)) {
        return NAN;
    }

    if (p == end) {
        goto done;
    }

    p = njs_date_time_parse(tm, p, end);
    if (nxt_slow_path(p == NULL)) {
        return NAN;
    }

    if (p == end) {
        goto done;
    }

    p = njs_date_skip_spaces(p, end);
    if (nxt_slow_path(p == NULL)) {
        return NAN;
    }

    if (p == end) {
        goto done;
    }

    if (p + 2 < end && p[0] == 'G' && p[1] == 'M' && p[2] == 'T') {

        gmtoff = njs_date_gmtoff_parse(&p[3], end);

        if (nxt_fast_path(gmtoff != -1)) {
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


static nxt_noinline nxt_int_t
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


static nxt_noinline const u_char *
njs_date_time_parse(struct tm *tm, const u_char *p, const u_char *end)
{
    p = njs_date_number_parse(&tm->tm_hour, p, end, 2);
    if (nxt_slow_path(p == NULL)) {
        return p;
    }

    if (nxt_slow_path(p >= end || *p != ':')) {
        return NULL;
    }

    p = njs_date_number_parse(&tm->tm_min, p + 1, end, 2);
    if (nxt_slow_path(p == NULL)) {
        return p;
    }

    if (p == end) {
        return p;
    }

    if (nxt_slow_path(*p != ':')) {
        return NULL;
    }

    return njs_date_number_parse(&tm->tm_sec, p + 1, end, 2);
}


static nxt_noinline nxt_int_t
njs_date_gmtoff_parse(const u_char *start, const u_char *end)
{
    int           gmtoff, hour, min;
    const u_char  *p;

    if (nxt_fast_path(start + 4 < end && (*start == '+' || *start == '-'))) {

        p = njs_date_number_parse(&hour, start + 1, end, 2);
        if (nxt_slow_path(p == NULL)) {
            return -1;
        }

        p = njs_date_number_parse(&min, p, end, 2);
        if (nxt_slow_path(p == NULL)) {
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


static nxt_noinline const u_char *
njs_date_number_parse(int *value, const u_char *p, const u_char *end,
    size_t size)
{
    u_char     c;
    nxt_int_t  n;

    n = 0;

    do {
        if (nxt_slow_path(p >= end)) {
            return NULL;
        }

        c = *p++;

        /* Values below '0' become >= 208. */
        c = c - '0';

        if (nxt_slow_path(c > 9)) {
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
    },

    /* Date.length == 7. */
    {
        .type = NJS_PROPERTY,
        .name = njs_string("length"),
        .value = njs_value(NJS_NUMBER, 1, 7.0),
    },

    /* Date.prototype. */
    {
        .type = NJS_NATIVE_GETTER,
        .name = njs_string("prototype"),
        .value = njs_native_getter(njs_object_prototype_create),
    },

    {
        .type = NJS_METHOD,
        .name = njs_string("UTC"),
        .value = njs_native_function(njs_date_utc, 0, 0),
    },

    {
        .type = NJS_METHOD,
        .name = njs_string("now"),
        .value = njs_native_function(njs_date_now, 0, 0),
    },

    {
        .type = NJS_METHOD,
        .name = njs_string("parse"),
        .value = njs_native_function(njs_date_parse, 0,
                     NJS_SKIP_ARG, NJS_STRING_ARG),
    },
};


const njs_object_init_t  njs_date_constructor_init = {
    nxt_string("Date"),
    njs_date_constructor_properties,
    nxt_nitems(njs_date_constructor_properties),
};


static njs_ret_t
njs_date_prototype_value_of(njs_vm_t *vm, njs_value_t *args, nxt_uint_t nargs,
    njs_index_t unused)
{
    njs_number_set(&vm->retval, args[0].data.u.date->time);

    return NXT_OK;
}


static njs_ret_t
njs_date_prototype_to_string(njs_vm_t *vm, njs_value_t *args, nxt_uint_t nargs,
    njs_index_t unused)
{
    return njs_date_string(vm, "%a %b %d %Y %T GMT%z (%Z)",
                           args[0].data.u.date->time);
}


static njs_ret_t
njs_date_prototype_to_date_string(njs_vm_t *vm, njs_value_t *args,
    nxt_uint_t nargs, njs_index_t unused)
{
    return njs_date_string(vm, "%a %b %d %Y", args[0].data.u.date->time);
}


static njs_ret_t
njs_date_prototype_to_time_string(njs_vm_t *vm, njs_value_t *args,
    nxt_uint_t nargs, njs_index_t unused)
{
    return njs_date_string(vm, "%T GMT%z (%Z)", args[0].data.u.date->time);
}


static nxt_noinline njs_ret_t
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

    return NXT_OK;
}


static njs_ret_t
njs_date_prototype_to_utc_string(njs_vm_t *vm, njs_value_t *args,
    nxt_uint_t nargs, njs_index_t unused)
{
    double             time;
    size_t             size;
    time_t             clock;
    u_char             buf[NJS_DATE_TIME_LEN];
    struct tm          tm;

    static const char  *week[] = { "Sun", "Mon", "Tue", "Wed",
                                   "Thu", "Fri", "Sat" };

    static const char  *month[] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                    "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };

    time = args[0].data.u.date->time;

    if (!isnan(time)) {
        clock = time / 1000;
        gmtime_r(&clock, &tm);

        size = snprintf((char *) buf, NJS_DATE_TIME_LEN,
                        "%s %s %02d %4d %02d:%02d:%02d GMT",
                        week[tm.tm_wday], month[tm.tm_mon],
                        tm.tm_mday, tm.tm_year + 1900,
                        tm.tm_hour, tm.tm_min, tm.tm_sec);

        return njs_string_new(vm, &vm->retval, buf, size, size);
    }

    vm->retval = njs_string_invalid_date;

    return NXT_OK;
}


static njs_ret_t
njs_date_prototype_to_iso_string(njs_vm_t *vm, njs_value_t *args,
    nxt_uint_t nargs, njs_index_t unused)
{
    int32_t    year;
    double     time;
    size_t     size;
    time_t     clock;
    u_char     buf[NJS_ISO_DATE_TIME_LEN];
    struct tm  tm;

    time = args[0].data.u.date->time;

    if (!isnan(time)) {
        clock = time / 1000;

        gmtime_r(&clock, &tm);

        year = tm.tm_year + 1900;

        size = snprintf((char *) buf, NJS_ISO_DATE_TIME_LEN,
                        (year < 0) ? "%07d-%02d-%02dT%02d:%02d:%02d.%03dZ"
                                   : "%04d-%02d-%02dT%02d:%02d:%02d.%03dZ",
                        year, tm.tm_mon + 1, tm.tm_mday,
                        tm.tm_hour, tm.tm_min, tm.tm_sec,
                        (int) ((int64_t) time % 1000));

        return njs_string_new(vm, &vm->retval, buf, size, size);
    }

    njs_exception_range_error(vm, NULL, NULL);

    return NXT_ERROR;
}


static njs_ret_t
njs_date_prototype_get_full_year(njs_vm_t *vm, njs_value_t *args,
    nxt_uint_t nargs, njs_index_t unused)
{
    double     value;
    time_t     clock;
    struct tm  tm;

    value = args[0].data.u.date->time;

    if (nxt_fast_path(!isnan(value))) {
        clock = value / 1000;
        localtime_r(&clock, &tm);

        value = tm.tm_year + 1900;
    }

    njs_number_set(&vm->retval, value);

    return NXT_OK;
}


static njs_ret_t
njs_date_prototype_get_utc_full_year(njs_vm_t *vm, njs_value_t *args,
    nxt_uint_t nargs, njs_index_t unused)
{
    double     value;
    time_t     clock;
    struct tm  tm;

    value = args[0].data.u.date->time;

    if (nxt_fast_path(!isnan(value))) {
        clock = value / 1000;
        gmtime_r(&clock, &tm);

        value = tm.tm_year + 1900;
    }

    njs_number_set(&vm->retval, value);

    return NXT_OK;
}


static njs_ret_t
njs_date_prototype_get_month(njs_vm_t *vm, njs_value_t *args, nxt_uint_t nargs,
    njs_index_t unused)
{
    double     value;
    time_t     clock;
    struct tm  tm;

    value = args[0].data.u.date->time;

    if (nxt_fast_path(!isnan(value))) {
        clock = value / 1000;
        localtime_r(&clock, &tm);

        value = tm.tm_mon;
    }

    njs_number_set(&vm->retval, value);

    return NXT_OK;
}


static njs_ret_t
njs_date_prototype_get_utc_month(njs_vm_t *vm, njs_value_t *args,
    nxt_uint_t nargs, njs_index_t unused)
{
    double     value;
    time_t     clock;
    struct tm  tm;

    value = args[0].data.u.date->time;

    if (nxt_fast_path(!isnan(value))) {
        clock = value / 1000;

        gmtime_r(&clock, &tm);

        value = tm.tm_mon;
    }

    njs_number_set(&vm->retval, value);

    return NXT_OK;
}


static njs_ret_t
njs_date_prototype_get_date(njs_vm_t *vm, njs_value_t *args, nxt_uint_t nargs,
    njs_index_t unused)
{
    double     value;
    time_t     clock;
    struct tm  tm;

    value = args[0].data.u.date->time;

    if (nxt_fast_path(!isnan(value))) {
        clock = value / 1000;
        localtime_r(&clock, &tm);

        value = tm.tm_mday;
    }

    njs_number_set(&vm->retval, value);

    return NXT_OK;
}


static njs_ret_t
njs_date_prototype_get_utc_date(njs_vm_t *vm, njs_value_t *args,
    nxt_uint_t nargs, njs_index_t unused)
{
    double     value;
    time_t     clock;
    struct tm  tm;

    value = args[0].data.u.date->time;

    if (nxt_fast_path(!isnan(value))) {
        clock = value / 1000;
        gmtime_r(&clock, &tm);

        value = tm.tm_mday;
    }

    njs_number_set(&vm->retval, value);

    return NXT_OK;
}


static njs_ret_t
njs_date_prototype_get_day(njs_vm_t *vm, njs_value_t *args, nxt_uint_t nargs,
    njs_index_t unused)
{
    double     value;
    time_t     clock;
    struct tm  tm;

    value = args[0].data.u.date->time;

    if (nxt_fast_path(!isnan(value))) {
        clock = value / 1000;
        localtime_r(&clock, &tm);

        value = tm.tm_wday;
    }

    njs_number_set(&vm->retval, value);

    return NXT_OK;
}


static njs_ret_t
njs_date_prototype_get_utc_day(njs_vm_t *vm, njs_value_t *args,
    nxt_uint_t nargs, njs_index_t unused)
{
    double     value;
    time_t     clock;
    struct tm  tm;

    value = args[0].data.u.date->time;

    if (nxt_fast_path(!isnan(value))) {
        clock = value / 1000;
        gmtime_r(&clock, &tm);

        value = tm.tm_wday;
    }

    njs_number_set(&vm->retval, value);

    return NXT_OK;
}


static njs_ret_t
njs_date_prototype_get_hours(njs_vm_t *vm, njs_value_t *args, nxt_uint_t nargs,
    njs_index_t unused)
{
    double     value;
    time_t     clock;
    struct tm  tm;

    value = args[0].data.u.date->time;

    if (nxt_fast_path(!isnan(value))) {
        clock = value / 1000;

        localtime_r(&clock, &tm);

        value = tm.tm_hour;
    }

    njs_number_set(&vm->retval, value);

    return NXT_OK;
}


static njs_ret_t
njs_date_prototype_get_utc_hours(njs_vm_t *vm, njs_value_t *args,
    nxt_uint_t nargs, njs_index_t unused)
{
    double     value;
    time_t     clock;
    struct tm  tm;

    value = args[0].data.u.date->time;

    if (nxt_fast_path(!isnan(value))) {
        clock = value / 1000;
        gmtime_r(&clock, &tm);

        value = tm.tm_hour;
    }

    njs_number_set(&vm->retval, value);

    return NXT_OK;
}


static njs_ret_t
njs_date_prototype_get_minutes(njs_vm_t *vm, njs_value_t *args,
    nxt_uint_t nargs, njs_index_t unused)
{
    double     value;
    time_t     clock;
    struct tm  tm;

    value = args[0].data.u.date->time;

    if (nxt_fast_path(!isnan(value))) {
        clock = value / 1000;

        localtime_r(&clock, &tm);

        value = tm.tm_min;
    }

    njs_number_set(&vm->retval, value);

    return NXT_OK;
}


static njs_ret_t
njs_date_prototype_get_utc_minutes(njs_vm_t *vm, njs_value_t *args,
    nxt_uint_t nargs, njs_index_t unused)
{
    double     value;
    time_t     clock;
    struct tm  tm;

    value = args[0].data.u.date->time;

    if (nxt_fast_path(!isnan(value))) {
        clock = value / 1000;
        gmtime_r(&clock, &tm);

        value = tm.tm_min;
    }

    njs_number_set(&vm->retval, value);

    return NXT_OK;
}


static njs_ret_t
njs_date_prototype_get_seconds(njs_vm_t *vm, njs_value_t *args,
    nxt_uint_t nargs, njs_index_t unused)
{
    double  value;

    value = args[0].data.u.date->time;

    if (nxt_fast_path(!isnan(value))) {
        value = (int64_t) (value / 1000) % 60;
    }

    njs_number_set(&vm->retval, value);

    return NXT_OK;
}


static njs_ret_t
njs_date_prototype_get_milliseconds(njs_vm_t *vm, njs_value_t *args,
    nxt_uint_t nargs, njs_index_t unused)
{
    double  value;

    value = args[0].data.u.date->time;

    if (nxt_fast_path(!isnan(value))) {
        value = (int64_t) value % 1000;
    }

    njs_number_set(&vm->retval, value);

    return NXT_OK;
}


static njs_ret_t
njs_date_prototype_get_timezone_offset(njs_vm_t *vm, njs_value_t *args,
    nxt_uint_t nargs, njs_index_t unused)
{
    double     value;
    time_t     clock;
    struct tm  tm;

    value = args[0].data.u.date->time;

    if (nxt_fast_path(!isnan(value))) {
        clock = value / 1000;
        localtime_r(&clock, &tm);

        value = - nxt_timezone(&tm) / 60;
    }

    njs_number_set(&vm->retval, value);

    return NXT_OK;
}


static njs_ret_t
njs_date_prototype_set_time(njs_vm_t *vm, njs_value_t *args, nxt_uint_t nargs,
    njs_index_t unused)
{
    double  time;

    time = args[0].data.u.date->time;

    if (nxt_fast_path(!isnan(time))) {

        if (nargs > 1) {
            time = args[1].data.u.number;

        } else {
            time = NAN;
        }
    }

    args[0].data.u.date->time = time;
    njs_number_set(&vm->retval, time);

    return NXT_OK;
}


static njs_ret_t
njs_date_prototype_set_milliseconds(njs_vm_t *vm, njs_value_t *args,
    nxt_uint_t nargs, njs_index_t unused)
{
    double  time;

    time = args[0].data.u.date->time;

    if (nxt_fast_path(!isnan(time))) {

        if (nargs > 1) {
            time = (int64_t) (time / 1000) * 1000 + args[1].data.u.number;

        } else {
            time = NAN;
        }
    }

    args[0].data.u.date->time = time;
    njs_number_set(&vm->retval, time);

    return NXT_OK;
}


static njs_ret_t
njs_date_prototype_set_seconds(njs_vm_t *vm, njs_value_t *args,
    nxt_uint_t nargs, njs_index_t unused)
{
    double   time;
    int64_t  sec, ms;

    time = args[0].data.u.date->time;

    if (nxt_fast_path(!isnan(time))) {

        if (nargs > 1) {
            sec = args[1].data.u.number;
            ms = (nargs > 2) ? args[2].data.u.number : (int64_t) time % 1000;

            time = (int64_t) (time / 60000) * 60000 + sec * 1000 + ms;

        } else {
            time = NAN;
        }
    }

    args[0].data.u.date->time = time;
    njs_number_set(&vm->retval, time);

    return NXT_OK;
}


static njs_ret_t
njs_date_prototype_set_minutes(njs_vm_t *vm, njs_value_t *args,
    nxt_uint_t nargs, njs_index_t unused)
{
    double     time;
    time_t     clock;
    int64_t    ms;
    struct tm  tm;

    time = args[0].data.u.date->time;

    if (nxt_fast_path(!isnan(time))) {

        if (nargs > 1) {
            clock = time / 1000;
            localtime_r(&clock, &tm);

            tm.tm_min = args[1].data.u.number;

            if (nargs > 2) {
                tm.tm_sec = args[2].data.u.number;
            }

            ms = (nargs > 3) ? args[3].data.u.number : (int64_t) time % 1000;

            time = njs_date_time(&tm, ms);

        } else {
            time = NAN;
        }
    }

    args[0].data.u.date->time = time;
    njs_number_set(&vm->retval, time);

    return NXT_OK;
}


static njs_ret_t
njs_date_prototype_set_utc_minutes(njs_vm_t *vm, njs_value_t *args,
    nxt_uint_t nargs, njs_index_t unused)
{
    double   time;
    int64_t  clock, min, sec, ms;

    time = args[0].data.u.date->time;

    if (nxt_fast_path(!isnan(time))) {

        if (nargs > 1) {
            clock = time / 1000;

            sec = (nargs > 2) ? args[2].data.u.number : clock % 60;
            min = args[1].data.u.number;

            clock = clock / 3600 * 3600 + min * 60 + sec;

            ms = (nargs > 3) ? args[3].data.u.number : (int64_t) time % 1000;

            time = clock * 1000 + ms;

        } else {
            time = NAN;
        }
    }

    args[0].data.u.date->time = time;
    njs_number_set(&vm->retval, time);

    return NXT_OK;
}


static njs_ret_t
njs_date_prototype_set_hours(njs_vm_t *vm, njs_value_t *args, nxt_uint_t nargs,
    njs_index_t unused)
{
    double     time;
    time_t     clock;
    int64_t    ms;
    struct tm  tm;

    time = args[0].data.u.date->time;

    if (nxt_fast_path(!isnan(time))) {

        if (nargs > 1) {
            clock = time / 1000;
            localtime_r(&clock, &tm);

            tm.tm_hour = args[1].data.u.number;

            if (nargs > 2) {
                tm.tm_min = args[2].data.u.number;
            }

            if (nargs > 3) {
                tm.tm_sec = args[3].data.u.number;
            }

            ms = (nargs > 4) ? args[4].data.u.number : (int64_t) time % 1000;

            time = njs_date_time(&tm, ms);

        } else {
            time = NAN;
        }
    }

    args[0].data.u.date->time = time;
    njs_number_set(&vm->retval, time);

    return NXT_OK;
}


static njs_ret_t
njs_date_prototype_set_utc_hours(njs_vm_t *vm, njs_value_t *args,
    nxt_uint_t nargs, njs_index_t unused)
{
    double   time;
    int64_t  clock, hour, min, sec, ms;

    time = args[0].data.u.date->time;

    if (nxt_fast_path(!isnan(time))) {

        if (nargs > 1) {
            clock = time / 1000;

            sec = (nargs > 3) ? args[3].data.u.number : clock % 60;
            min = (nargs > 2) ? args[2].data.u.number : clock / 60 % 60;
            hour = args[1].data.u.number;

            clock = clock / 86400 * 86400 + hour * 3600 + min * 60 + sec;

            ms = (nargs > 4) ? args[4].data.u.number : (int64_t) time % 1000;

            time = clock * 1000 + ms;

        } else {
            time = NAN;
        }
    }

    args[0].data.u.date->time = time;
    njs_number_set(&vm->retval, time);

    return NXT_OK;
}


static njs_ret_t
njs_date_prototype_set_date(njs_vm_t *vm, njs_value_t *args, nxt_uint_t nargs,
    njs_index_t unused)
{
    double     time;
    time_t     clock;
    struct tm  tm;

    time = args[0].data.u.date->time;

    if (nxt_fast_path(!isnan(time))) {

        if (nargs > 1) {
            clock = time / 1000;
            localtime_r(&clock, &tm);

            tm.tm_mday = args[1].data.u.number;

            time = njs_date_time(&tm, (int64_t) time % 1000);

        } else {
            time = NAN;
        }
    }

    args[0].data.u.date->time = time;
    njs_number_set(&vm->retval, time);

    return NXT_OK;
}


static njs_ret_t
njs_date_prototype_set_utc_date(njs_vm_t *vm, njs_value_t *args,
    nxt_uint_t nargs, njs_index_t unused)
{
    double     time;
    time_t     clock;
    struct tm  tm;

    time = args[0].data.u.date->time;

    if (nxt_fast_path(!isnan(time))) {

        if (nargs > 1) {
            clock = time / 1000;
            gmtime_r(&clock, &tm);

            tm.tm_mday = args[1].data.u.number;

            time = njs_date_utc_time(&tm, time);

        } else {
            time = NAN;
        }
    }

    args[0].data.u.date->time = time;
    njs_number_set(&vm->retval, time);

    return NXT_OK;
}


static njs_ret_t
njs_date_prototype_set_month(njs_vm_t *vm, njs_value_t *args, nxt_uint_t nargs,
    njs_index_t unused)
{
    double     time;
    time_t     clock;
    struct tm  tm;

    time = args[0].data.u.date->time;

    if (nxt_fast_path(!isnan(time))) {

        if (nargs > 1) {
            clock = time / 1000;
            localtime_r(&clock, &tm);

            tm.tm_mon = args[1].data.u.number;

            if (nargs > 2) {
                tm.tm_mday = args[2].data.u.number;
            }

            time = njs_date_time(&tm, (int64_t) time % 1000);

        } else {
            time = NAN;
        }
    }

    args[0].data.u.date->time = time;
    njs_number_set(&vm->retval, time);

    return NXT_OK;
}


static njs_ret_t
njs_date_prototype_set_utc_month(njs_vm_t *vm, njs_value_t *args,
    nxt_uint_t nargs, njs_index_t unused)
{
    double     time;
    time_t     clock;
    struct tm  tm;

    time = args[0].data.u.date->time;

    if (nxt_fast_path(!isnan(time))) {

        if (nargs > 1) {
            clock = time / 1000;
            gmtime_r(&clock, &tm);

            tm.tm_mon = args[1].data.u.number;

            if (nargs > 2) {
                tm.tm_mday = args[2].data.u.number;
            }

            time = njs_date_utc_time(&tm, time);

        } else {
            time = NAN;
        }
    }

    args[0].data.u.date->time = time;
    njs_number_set(&vm->retval, time);

    return NXT_OK;
}


static njs_ret_t
njs_date_prototype_set_full_year(njs_vm_t *vm, njs_value_t *args,
    nxt_uint_t nargs, njs_index_t unused)
{
    double     time;
    time_t     clock;
    struct tm  tm;

    time = args[0].data.u.date->time;

    if (nxt_fast_path(!isnan(time))) {

        if (nargs > 1) {
            clock = time / 1000;
            localtime_r(&clock, &tm);

            tm.tm_year = args[1].data.u.number - 1900;

            if (nargs > 2) {
                tm.tm_mon = args[2].data.u.number;
            }

            if (nargs > 3) {
                tm.tm_mday = args[3].data.u.number;
            }

            time = njs_date_time(&tm, (int64_t) time % 1000);

        } else {
            time = NAN;
        }
    }

    args[0].data.u.date->time = time;
    njs_number_set(&vm->retval, time);

    return NXT_OK;
}


static njs_ret_t
njs_date_prototype_set_utc_full_year(njs_vm_t *vm, njs_value_t *args,
    nxt_uint_t nargs, njs_index_t unused)
{
    double     time;
    time_t     clock;
    struct tm  tm;

    time = args[0].data.u.date->time;

    if (nxt_fast_path(!isnan(time))) {

        if (nargs > 1) {
            clock = time / 1000;
            gmtime_r(&clock, &tm);

            tm.tm_year = args[1].data.u.number - 1900;

            if (nargs > 2) {
                tm.tm_mon = args[2].data.u.number;
            }

            if (nargs > 3) {
                tm.tm_mday = args[3].data.u.number;
            }

            time = njs_date_utc_time(&tm, time);

        } else {
            time = NAN;
        }
    }

    args[0].data.u.date->time = time;
    njs_number_set(&vm->retval, time);

    return NXT_OK;
}


static nxt_noinline double
njs_date_time(struct tm *tm, int64_t ms)
{
    double  time;
    time_t  clock;

    tm->tm_isdst = -1;
    clock = mktime(tm);

    if (nxt_fast_path(clock != -1)) {
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


/*
 * ECMAScript 5.1: call object method "toISOString".
 * Date.toJSON() must be a continuation otherwise it may endlessly
 * call Date.toISOString().
 */

static njs_ret_t
njs_date_prototype_to_json(njs_vm_t *vm, njs_value_t *args, nxt_uint_t nargs,
    njs_index_t retval)
{
    njs_object_prop_t   *prop;
    njs_continuation_t  *cont;
    nxt_lvlhsh_query_t  lhq;

    cont = njs_vm_continuation(vm);
    cont->function = njs_date_prototype_to_json_continuation;

    if (njs_is_object(&args[0])) {
        lhq.key_hash = NJS_TO_ISO_STRING_HASH;
        lhq.key = nxt_string_value("toISOString");

        prop = njs_object_property(vm, args[0].data.u.object, &lhq);

        if (nxt_fast_path(prop != NULL && njs_is_function(&prop->value))) {
            return njs_function_apply(vm, prop->value.data.u.function,
                                      args, nargs, retval);
        }
    }

    njs_exception_type_error(vm, NULL, NULL);

    return NXT_ERROR;
}


static njs_ret_t
njs_date_prototype_to_json_continuation(njs_vm_t *vm, njs_value_t *args,
    nxt_uint_t nargs, njs_index_t retval)
{
    /* Skip retval update. */
    vm->top_frame->skip = 1;

    return NXT_OK;
}


static const njs_object_prop_t  njs_date_prototype_properties[] =
{
    {
        .type = NJS_NATIVE_GETTER,
        .name = njs_string("__proto__"),
        .value = njs_native_getter(njs_primitive_prototype_get_proto),
    },

    {
        .type = NJS_METHOD,
        .name = njs_string("valueOf"),
        .value = njs_native_function(njs_date_prototype_value_of, 0,
                     NJS_DATE_ARG),
    },

    {
        .type = NJS_METHOD,
        .name = njs_string("toString"),
        .value = njs_native_function(njs_date_prototype_to_string, 0,
                     NJS_DATE_ARG),
    },

    {
        .type = NJS_METHOD,
        .name = njs_string("toDateString"),
        .value = njs_native_function(njs_date_prototype_to_date_string, 0,
                     NJS_DATE_ARG),
    },

    {
        .type = NJS_METHOD,
        .name = njs_string("toTimeString"),
        .value = njs_native_function(njs_date_prototype_to_time_string, 0,
                     NJS_DATE_ARG),
    },

    {
        .type = NJS_METHOD,
        .name = njs_string("toLocaleString"),
        .value = njs_native_function(njs_date_prototype_to_string, 0,
                     NJS_DATE_ARG),
    },

    {
        .type = NJS_METHOD,
        .name = njs_long_string("toLocaleDateString"),
        .value = njs_native_function(njs_date_prototype_to_date_string, 0,
                     NJS_DATE_ARG),
    },

    {
        .type = NJS_METHOD,
        .name = njs_long_string("toLocaleTimeString"),
        .value = njs_native_function(njs_date_prototype_to_time_string, 0,
                     NJS_DATE_ARG),
    },

    {
        .type = NJS_METHOD,
        .name = njs_string("toUTCString"),
        .value = njs_native_function(njs_date_prototype_to_utc_string, 0,
                     NJS_DATE_ARG),
    },

    {
        .type = NJS_METHOD,
        .name = njs_string("toISOString"),
        .value = njs_native_function(njs_date_prototype_to_iso_string, 0,
                     NJS_DATE_ARG),
    },

    {
        .type = NJS_METHOD,
        .name = njs_string("getTime"),
        .value = njs_native_function(njs_date_prototype_value_of, 0,
                     NJS_DATE_ARG),
    },

    {
        .type = NJS_METHOD,
        .name = njs_string("getFullYear"),
        .value = njs_native_function(njs_date_prototype_get_full_year, 0,
                     NJS_DATE_ARG),
    },

    {
        .type = NJS_METHOD,
        .name = njs_string("getUTCFullYear"),
        .value = njs_native_function(njs_date_prototype_get_utc_full_year, 0,
                     NJS_DATE_ARG),
    },

    {
        .type = NJS_METHOD,
        .name = njs_string("getMonth"),
        .value = njs_native_function(njs_date_prototype_get_month, 0,
                     NJS_DATE_ARG),
    },

    {
        .type = NJS_METHOD,
        .name = njs_string("getUTCMonth"),
        .value = njs_native_function(njs_date_prototype_get_utc_month, 0,
                     NJS_DATE_ARG),
    },

    {
        .type = NJS_METHOD,
        .name = njs_string("getDate"),
        .value = njs_native_function(njs_date_prototype_get_date, 0,
                     NJS_DATE_ARG),
    },

    {
        .type = NJS_METHOD,
        .name = njs_string("getUTCDate"),
        .value = njs_native_function(njs_date_prototype_get_utc_date, 0,
                     NJS_DATE_ARG),
    },

    {
        .type = NJS_METHOD,
        .name = njs_string("getDay"),
        .value = njs_native_function(njs_date_prototype_get_day, 0,
                     NJS_DATE_ARG),
    },

    {
        .type = NJS_METHOD,
        .name = njs_string("getUTCDay"),
        .value = njs_native_function(njs_date_prototype_get_utc_day, 0,
                     NJS_DATE_ARG),
    },

    {
        .type = NJS_METHOD,
        .name = njs_string("getHours"),
        .value = njs_native_function(njs_date_prototype_get_hours, 0,
                     NJS_DATE_ARG),
    },

    {
        .type = NJS_METHOD,
        .name = njs_string("getUTCHours"),
        .value = njs_native_function(njs_date_prototype_get_utc_hours, 0,
                     NJS_DATE_ARG),
    },

    {
        .type = NJS_METHOD,
        .name = njs_string("getMinutes"),
        .value = njs_native_function(njs_date_prototype_get_minutes, 0,
                     NJS_DATE_ARG),
    },

    {
        .type = NJS_METHOD,
        .name = njs_string("getUTCMinutes"),
        .value = njs_native_function(njs_date_prototype_get_utc_minutes, 0,
                     NJS_DATE_ARG),
    },

    {
        .type = NJS_METHOD,
        .name = njs_string("getSeconds"),
        .value = njs_native_function(njs_date_prototype_get_seconds, 0,
                     NJS_DATE_ARG),
    },

    {
        .type = NJS_METHOD,
        .name = njs_string("getUTCSeconds"),
        .value = njs_native_function(njs_date_prototype_get_seconds, 0,
                     NJS_DATE_ARG),
    },

    {
        .type = NJS_METHOD,
        .name = njs_long_string("getMilliseconds"),
        .value = njs_native_function(njs_date_prototype_get_milliseconds, 0,
                     NJS_DATE_ARG),
    },

    {
        .type = NJS_METHOD,
        .name = njs_long_string("getUTCMilliseconds"),
        .value = njs_native_function(njs_date_prototype_get_milliseconds, 0,
                     NJS_DATE_ARG),
    },

    {
        .type = NJS_METHOD,
        .name = njs_long_string("getTimezoneOffset"),
        .value = njs_native_function(njs_date_prototype_get_timezone_offset, 0,
                     NJS_DATE_ARG),
    },

    {
        .type = NJS_METHOD,
        .name = njs_string("setTime"),
        .value = njs_native_function(njs_date_prototype_set_time, 0,
                     NJS_DATE_ARG, NJS_NUMBER_ARG),
    },

    {
        .type = NJS_METHOD,
        .name = njs_long_string("setMilliseconds"),
        .value = njs_native_function(njs_date_prototype_set_milliseconds, 0,
                     NJS_DATE_ARG, NJS_NUMBER_ARG),
    },

    {
        .type = NJS_METHOD,
        .name = njs_long_string("setUTCMilliseconds"),
        .value = njs_native_function(njs_date_prototype_set_milliseconds, 0,
                     NJS_DATE_ARG, NJS_NUMBER_ARG),
    },

    {
        .type = NJS_METHOD,
        .name = njs_string("setSeconds"),
        .value = njs_native_function(njs_date_prototype_set_seconds, 0,
                     NJS_DATE_ARG, NJS_NUMBER_ARG),
    },

    {
        .type = NJS_METHOD,
        .name = njs_string("setUTCSeconds"),
        .value = njs_native_function(njs_date_prototype_set_seconds, 0,
                     NJS_DATE_ARG, NJS_NUMBER_ARG, NJS_NUMBER_ARG),
    },

    {
        .type = NJS_METHOD,
        .name = njs_string("setMinutes"),
        .value = njs_native_function(njs_date_prototype_set_minutes, 0,
                     NJS_DATE_ARG, NJS_NUMBER_ARG, NJS_NUMBER_ARG,
                     NJS_NUMBER_ARG),
    },

    {
        .type = NJS_METHOD,
        .name = njs_string("setUTCMinutes"),
        .value = njs_native_function(njs_date_prototype_set_utc_minutes, 0,
                     NJS_DATE_ARG, NJS_NUMBER_ARG, NJS_NUMBER_ARG,
                     NJS_NUMBER_ARG),
    },

    {
        .type = NJS_METHOD,
        .name = njs_string("setHours"),
        .value = njs_native_function(njs_date_prototype_set_hours, 0,
                     NJS_DATE_ARG, NJS_NUMBER_ARG, NJS_NUMBER_ARG,
                     NJS_NUMBER_ARG, NJS_NUMBER_ARG),
    },

    {
        .type = NJS_METHOD,
        .name = njs_string("setUTCHours"),
        .value = njs_native_function(njs_date_prototype_set_utc_hours, 0,
                     NJS_DATE_ARG, NJS_NUMBER_ARG, NJS_NUMBER_ARG,
                     NJS_NUMBER_ARG, NJS_NUMBER_ARG),
    },

    {
        .type = NJS_METHOD,
        .name = njs_string("setDate"),
        .value = njs_native_function(njs_date_prototype_set_date, 0,
                     NJS_DATE_ARG, NJS_NUMBER_ARG),
    },

    {
        .type = NJS_METHOD,
        .name = njs_string("setUTCDate"),
        .value = njs_native_function(njs_date_prototype_set_utc_date, 0,
                     NJS_DATE_ARG, NJS_NUMBER_ARG),
    },

    {
        .type = NJS_METHOD,
        .name = njs_string("setMonth"),
        .value = njs_native_function(njs_date_prototype_set_month, 0,
                     NJS_DATE_ARG, NJS_NUMBER_ARG, NJS_NUMBER_ARG),
    },

    {
        .type = NJS_METHOD,
        .name = njs_string("setUTCMonth"),
        .value = njs_native_function(njs_date_prototype_set_utc_month, 0,
                     NJS_DATE_ARG, NJS_NUMBER_ARG, NJS_NUMBER_ARG),
    },

    {
        .type = NJS_METHOD,
        .name = njs_string("setFullYear"),
        .value = njs_native_function(njs_date_prototype_set_full_year, 0,
                     NJS_DATE_ARG, NJS_NUMBER_ARG, NJS_NUMBER_ARG,
                     NJS_NUMBER_ARG),
    },

    {
        .type = NJS_METHOD,
        .name = njs_string("setUTCFullYear"),
        .value = njs_native_function(njs_date_prototype_set_utc_full_year, 0,
                     NJS_DATE_ARG, NJS_NUMBER_ARG, NJS_NUMBER_ARG,
                     NJS_NUMBER_ARG),
    },

    {
        .type = NJS_METHOD,
        .name = njs_string("toJSON"),
        .value = njs_native_function(njs_date_prototype_to_json,
                     NJS_CONTINUATION_SIZE, 0),
    },
};


const njs_object_init_t  njs_date_prototype_init = {
    nxt_string("Date"),
    njs_date_prototype_properties,
    nxt_nitems(njs_date_prototype_properties),
};
