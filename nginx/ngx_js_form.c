/*
 * Copyright (C) Dmitry Volyntsev
 * Copyright (C) F5, Inc.
 */


#include <ngx_config.h>
#include <ngx_core.h>
#include "ngx_js_form.h"


#define NGX_JS_FORM_URLENCODED  1
#define NGX_JS_FORM_MULTIPART   2

/*
 * RFC 2046, section 5.1.1 limits boundary to 70 characters; we allow up
 * to 200 to tolerate non-conforming clients while bounding allocation.
 */
#define NGX_JS_FORM_MAX_BOUNDARY_LEN     200
#define NGX_JS_FORM_MAX_PART_HEADERS     32
#define NGX_JS_FORM_MAX_PART_HEADER_LINE 4096
#define NGX_JS_FORM_MAX_PART_HEADER_SIZE 16384


typedef struct {
    ngx_str_t  boundary;
    ngx_uint_t type;
} ngx_js_form_content_type_t;


static ngx_int_t ngx_js_form_parse_content_type(ngx_pool_t *pool,
    ngx_str_t *content_type, ngx_js_form_content_type_t *ct,
    ngx_str_t *error);
static ngx_int_t ngx_js_form_parse_urlencoded(ngx_pool_t *pool, u_char *body,
    size_t len, ngx_uint_t max_keys, ngx_js_form_t *form, ngx_str_t *error);
static ngx_int_t ngx_js_form_parse_multipart(ngx_pool_t *pool, u_char *body,
    size_t len, ngx_str_t *boundary, ngx_uint_t max_keys, ngx_js_form_t *form,
    ngx_str_t *error);
static ngx_int_t ngx_js_form_add_entry(ngx_js_form_t *form,
    ngx_pool_t *pool, ngx_str_t *name, ngx_str_t *value, ngx_uint_t *count,
    ngx_uint_t max_keys, ngx_str_t *filename, ngx_flag_t is_file,
    ngx_str_t *error);
static ngx_int_t ngx_js_form_decode_urlencoded(ngx_pool_t *pool, u_char *start,
    u_char *end, ngx_str_t *dst, ngx_str_t *error);
static ngx_int_t ngx_js_form_copy(ngx_pool_t *pool, u_char *start,
    u_char *end, ngx_str_t *dst);
static ngx_int_t ngx_js_form_copy_quoted(ngx_pool_t *pool, u_char *start,
    u_char *end, ngx_str_t *dst);
static void ngx_js_form_error(ngx_str_t *error, const char *text);
static u_char *ngx_js_form_skip_ows(u_char *p, u_char *end);
static u_char *ngx_js_form_find(u_char *start, u_char *end, u_char *pattern,
    size_t len);
static ngx_uint_t ngx_js_form_is_ows(u_char ch);
static ngx_int_t ngx_js_form_parse_part_headers(ngx_pool_t *pool,
    u_char *start, u_char *end, ngx_str_t *name, ngx_flag_t *is_file,
    ngx_str_t *filename, ngx_str_t *error);
static ngx_int_t ngx_js_form_parse_disposition(ngx_pool_t *pool,
    ngx_str_t *value, ngx_str_t *name, ngx_flag_t *is_file,
    ngx_str_t *filename, ngx_str_t *error);
static ngx_int_t ngx_js_form_parse_param(ngx_pool_t *pool, u_char **pp,
    u_char *end, ngx_str_t *param, ngx_str_t *value, ngx_flag_t *quoted,
    ngx_str_t *error);


ngx_int_t
ngx_js_parse_form(ngx_pool_t *pool, ngx_str_t *content_type, u_char *body,
    size_t len, ngx_uint_t max_keys, ngx_js_form_t **form,
    ngx_str_t *error)
{
    ngx_int_t                    rc;
    ngx_js_form_t               *f;
    ngx_js_form_content_type_t   ct;

    rc = ngx_js_form_parse_content_type(pool, content_type, &ct, error);
    if (rc != NGX_JS_FORM_OK) {
        return rc;
    }

    f = ngx_pcalloc(pool, sizeof(ngx_js_form_t));
    if (f == NULL) {
        return NGX_ERROR;
    }

    if (ngx_array_init(&f->entries, pool, 4, sizeof(ngx_js_form_entry_t))
        != NGX_OK)
    {
        return NGX_ERROR;
    }

    switch (ct.type) {
    case NGX_JS_FORM_URLENCODED:
        rc = ngx_js_form_parse_urlencoded(pool, body, len, max_keys, f, error);
        break;

    case NGX_JS_FORM_MULTIPART:
        rc = ngx_js_form_parse_multipart(pool, body, len, &ct.boundary,
                                         max_keys, f, error);
        break;

    default:
        ngx_js_form_error(error, "unsupported content type");
        return NGX_JS_FORM_TYPE_ERROR;
    }

    if (rc != NGX_JS_FORM_OK) {
        return rc;
    }

    *form = f;

    return NGX_JS_FORM_OK;
}


static ngx_int_t
ngx_js_form_parse_content_type(ngx_pool_t *pool, ngx_str_t *content_type,
    ngx_js_form_content_type_t *ct, ngx_str_t *error)
{
    u_char      *p, *end, *last, *value_start;
    ngx_int_t    rc;
    ngx_str_t    param, value;
    ngx_flag_t   quoted;

    if (content_type == NULL || content_type->len == 0) {
        ngx_js_form_error(error, "request content type is required");
        return NGX_JS_FORM_TYPE_ERROR;
    }

    ct->type = 0;
    ct->boundary.len = 0;
    ct->boundary.data = NULL;

    p = content_type->data;
    end = p + content_type->len;

    last = p;

    while (last < end && *last != ';') {
        last++;
    }

    value_start = ngx_js_form_skip_ows(p, last);
    p = last;

    while (last > value_start && ngx_js_form_is_ows(last[-1])) {
        last--;
    }

    if ((size_t) (last - value_start)
        == sizeof("application/x-www-form-urlencoded") - 1
        && ngx_strncasecmp(value_start,
                           (u_char *) "application/x-www-form-urlencoded",
                           last - value_start)
           == 0)
    {
        ct->type = NGX_JS_FORM_URLENCODED;
    }

    if ((size_t) (last - value_start) == sizeof("multipart/form-data") - 1
        && ngx_strncasecmp(value_start, (u_char *) "multipart/form-data",
                           last - value_start)
           == 0)
    {
        ct->type = NGX_JS_FORM_MULTIPART;
    }

    if (ct->type == 0) {
        ngx_js_form_error(error, "unsupported content type");
        return NGX_JS_FORM_TYPE_ERROR;
    }

    while (p < end) {
        if (*p++ != ';') {
            ngx_js_form_error(error, "malformed content type");
            return NGX_JS_FORM_PARSE_ERROR;
        }

        p = ngx_js_form_skip_ows(p, end);

        rc = ngx_js_form_parse_param(pool, &p, end, &param, &value, &quoted,
                                     error);
        if (rc != NGX_JS_FORM_OK) {
            return rc;
        }

        if (param.len == sizeof("boundary") - 1
            && ngx_strncasecmp(param.data, (u_char *) "boundary", param.len)
               == 0)
        {
            if (ct->boundary.data != NULL) {
                ngx_js_form_error(error, "duplicate boundary parameter");
                return NGX_JS_FORM_PARSE_ERROR;
            }

            if (value.len == 0 || value.len > NGX_JS_FORM_MAX_BOUNDARY_LEN) {
                ngx_js_form_error(error, "invalid multipart boundary");
                return NGX_JS_FORM_PARSE_ERROR;
            }

            ct->boundary = value;
        }

        p = ngx_js_form_skip_ows(p, end);

        if (p == end) {
            break;
        }
    }

    if (ct->type == NGX_JS_FORM_MULTIPART && ct->boundary.data == NULL) {
        ngx_js_form_error(error, "multipart boundary is required");
        return NGX_JS_FORM_TYPE_ERROR;
    }

    return NGX_JS_FORM_OK;
}


static ngx_int_t
ngx_js_form_parse_urlencoded(ngx_pool_t *pool, u_char *body, size_t len,
    ngx_uint_t max_keys, ngx_js_form_t *form, ngx_str_t *error)
{
    u_char      *p, *end, *amp, *eq;
    ngx_int_t    rc;
    ngx_str_t    name, value;
    ngx_uint_t   count;

    count = 0;
    p = body;
    end = body + len;

    if (len == 0) {
        return NGX_JS_FORM_OK;
    }

    while (p < end) {
        if (*p == '&') {
            p++;
            continue;
        }

        amp = p;

        while (amp < end && *amp != '&') {
            amp++;
        }

        eq = p;

        while (eq < amp && *eq != '=') {
            eq++;
        }

        rc = ngx_js_form_decode_urlencoded(pool, p, eq, &name, error);
        if (rc != NGX_JS_FORM_OK) {
            return rc;
        }

        if (eq < amp) {
            eq++;
        }

        rc = ngx_js_form_decode_urlencoded(pool, eq, amp, &value, error);
        if (rc != NGX_JS_FORM_OK) {
            return rc;
        }

        rc = ngx_js_form_add_entry(form, pool, &name, &value, &count, max_keys,
                                   NULL, 0, error);
        if (rc != NGX_JS_FORM_OK) {
            return rc;
        }

        if (amp == end) {
            break;
        }

        p = amp + 1;
    }

    return NGX_JS_FORM_OK;
}


static ngx_int_t
ngx_js_form_parse_multipart(ngx_pool_t *pool, u_char *body, size_t len,
    ngx_str_t *boundary, ngx_uint_t max_keys, ngx_js_form_t *form,
    ngx_str_t *error)
{
    size_t       dlen, cdlen;
    u_char      *p, *end, *marker, *next, *headers_end, *part_end, *scan;
    u_char      *delimiter;
    ngx_int_t    rc;
    ngx_str_t    name, value, filename;
    ngx_uint_t   count;
    ngx_flag_t   is_file;

    count = 0;
    end = body + len;
    dlen = boundary->len + 2;
    cdlen = boundary->len + 4;

    delimiter = ngx_pnalloc(pool, cdlen);
    if (delimiter == NULL) {
        return NGX_ERROR;
    }

    delimiter[0] = '-';
    delimiter[1] = '-';
    ngx_memcpy(delimiter + 2, boundary->data, boundary->len);
    delimiter[dlen] = '-';
    delimiter[dlen + 1] = '-';

    /*
     * Validate the body opening: a dash-boundary "--BOUNDARY" must
     * appear, and the close-delimiter "--BOUNDARY--" must not appear
     * before it.  The dash-boundary is a prefix of the close-delimiter,
     * so both searches use the same buffer with different lengths
     * (dlen = "--BOUNDARY", cdlen = "--BOUNDARY--").
     */
    p = ngx_js_form_find(body, end, delimiter, cdlen);
    marker = ngx_js_form_find(body, end, delimiter, dlen);

    if (marker == NULL || (p != NULL && p < marker)) {
        ngx_js_form_error(error, "malformed multipart body");
        return NGX_JS_FORM_PARSE_ERROR;
    }

    p = marker + dlen;

    if (p + 2 <= end && p[0] == '-' && p[1] == '-') {
        return NGX_JS_FORM_OK;
    }

    if (p + 2 > end || p[0] != '\r' || p[1] != '\n') {
        ngx_js_form_error(error, "malformed multipart boundary");
        return NGX_JS_FORM_PARSE_ERROR;
    }

    p += 2;

    for ( ;; ) {
        headers_end = ngx_js_form_find(p, end, (u_char *) "\r\n\r\n", 4);
        if (headers_end == NULL) {
            ngx_js_form_error(error, "missing multipart header separator");
            return NGX_JS_FORM_PARSE_ERROR;
        }

        if ((size_t) (headers_end - p) > NGX_JS_FORM_MAX_PART_HEADER_SIZE) {
            ngx_js_form_error(error, "multipart headers are too large");
            return NGX_JS_FORM_PARSE_ERROR;
        }

        rc = ngx_js_form_parse_part_headers(pool, p, headers_end, &name,
                                            &is_file, &filename, error);
        if (rc != NGX_JS_FORM_OK) {
            return rc;
        }

        p = headers_end + sizeof("\r\n\r\n") - 1;
        scan = p;

        for ( ;; ) {
            next = ngx_js_form_find(scan, end, (u_char *) "\r\n--", 4);
            if (next == NULL) {
                ngx_js_form_error(error, "truncated multipart body");
                return NGX_JS_FORM_PARSE_ERROR;
            }

            if (next + (sizeof("\r\n--") - 1) + boundary->len <= end
                && ngx_memcmp(next + (sizeof("\r\n--") - 1), boundary->data,
                              boundary->len) == 0)
            {
                break;
            }

            scan = next + sizeof("\r\n--") - 1;
        }

        part_end = next;

        if (is_file) {
            value.len = 0;
            value.data = (u_char *) "";
            form->has_files = 1;

        } else {
            if (ngx_js_form_copy(pool, p, part_end, &value) != NGX_OK) {
                return NGX_ERROR;
            }
        }

        rc = ngx_js_form_add_entry(form, pool, &name, &value, &count, max_keys,
                                   &filename, is_file, error);
        if (rc != NGX_JS_FORM_OK) {
            return rc;
        }

        p = next + (sizeof("\r\n--") - 1) + boundary->len;

        if (p + 2 <= end && p[0] == '-' && p[1] == '-') {
            return NGX_JS_FORM_OK;
        }

        if (p + 2 > end || p[0] != '\r' || p[1] != '\n') {
            ngx_js_form_error(error, "malformed multipart boundary");
            return NGX_JS_FORM_PARSE_ERROR;
        }

        p += 2;
    }
}


static ngx_int_t
ngx_js_form_parse_part_headers(ngx_pool_t *pool, u_char *start,
    u_char *end, ngx_str_t *name, ngx_flag_t *is_file, ngx_str_t *filename,
    ngx_str_t *error)
{
    u_char      *p, *line, *colon, *line_end;
    ngx_int_t    rc;
    ngx_str_t    key, value;
    ngx_uint_t   headers;
    ngx_flag_t   seen_disposition;

    headers = 0;
    seen_disposition = 0;

    name->len = 0;
    name->data = NULL;
    filename->len = 0;
    filename->data = (u_char *) "";

    *is_file = 0;

    for (p = start; p < end; p = line_end + 2) {
        line = p;
        line_end = ngx_js_form_find(p, end, (u_char *) "\r\n", 2);
        if (line_end == NULL) {
            line_end = end;
        }

        if ((size_t) (line_end - line) > NGX_JS_FORM_MAX_PART_HEADER_LINE) {
            ngx_js_form_error(error, "multipart header line is too long");
            return NGX_JS_FORM_PARSE_ERROR;
        }

        if (++headers > NGX_JS_FORM_MAX_PART_HEADERS) {
            ngx_js_form_error(error, "too many multipart headers");
            return NGX_JS_FORM_PARSE_ERROR;
        }

        colon = line;

        while (colon < line_end && *colon != ':') {
            colon++;
        }

        if (colon == line_end) {
            ngx_js_form_error(error, "malformed multipart header");
            return NGX_JS_FORM_PARSE_ERROR;
        }

        key.data = line;
        key.len = colon - line;

        colon++;
        colon = ngx_js_form_skip_ows(colon, line_end);

        value.data = colon;
        value.len = line_end - colon;

        while (value.len > 0
               && ngx_js_form_is_ows(value.data[value.len - 1]))
        {
            value.len--;
        }

        if (key.len == sizeof("Content-Disposition") - 1
            && ngx_strncasecmp(key.data, (u_char *) "Content-Disposition",
                               key.len)
               == 0)
        {
            if (seen_disposition) {
                ngx_js_form_error(error,
                                  "duplicate Content-Disposition header");
                return NGX_JS_FORM_PARSE_ERROR;
            }

            rc = ngx_js_form_parse_disposition(pool, &value, name, is_file,
                                               filename, error);
            if (rc != NGX_JS_FORM_OK) {
                return rc;
            }

            seen_disposition = 1;
        }

        if (line_end == end) {
            break;
        }
    }

    if (!seen_disposition) {
        ngx_js_form_error(error, "missing Content-Disposition header");
        return NGX_JS_FORM_PARSE_ERROR;
    }

    return NGX_JS_FORM_OK;
}


static ngx_int_t
ngx_js_form_parse_disposition(ngx_pool_t *pool, ngx_str_t *value,
    ngx_str_t *name, ngx_flag_t *is_file, ngx_str_t *filename,
    ngx_str_t *error)
{
    u_char      *p, *end;
    ngx_int_t    rc;
    ngx_str_t    param, param_value;
    ngx_flag_t   quoted, seen_name, seen_file;

    p = value->data;
    end = p + value->len;

    while (p < end && *p != ';') {
        p++;
    }

    if ((size_t) (p - value->data) != sizeof("form-data") - 1
        || ngx_strncasecmp(value->data, (u_char *) "form-data",
                           p - value->data)
           != 0)
    {
        ngx_js_form_error(error, "unsupported disposition type");
        return NGX_JS_FORM_PARSE_ERROR;
    }

    seen_name = 0;
    seen_file = 0;

    while (p < end) {
        if (*p++ != ';') {
            ngx_js_form_error(error, "malformed Content-Disposition");
            return NGX_JS_FORM_PARSE_ERROR;
        }

        p = ngx_js_form_skip_ows(p, end);

        rc = ngx_js_form_parse_param(pool, &p, end, &param, &param_value,
                                     &quoted, error);
        if (rc != NGX_JS_FORM_OK) {
            return rc;
        }

        if (param.len == sizeof("name") - 1
            && ngx_strncasecmp(param.data, (u_char *) "name", param.len) == 0)
        {
            if (seen_name) {
                ngx_js_form_error(error, "duplicate name parameter");
                return NGX_JS_FORM_PARSE_ERROR;
            }

            *name = param_value;
            seen_name = 1;

        } else if (param.len == sizeof("filename") - 1
                   && ngx_strncasecmp(param.data, (u_char *) "filename",
                                      param.len)
                      == 0)
        {
            if (seen_file) {
                ngx_js_form_error(error, "duplicate filename parameter");
                return NGX_JS_FORM_PARSE_ERROR;
            }

            *is_file = 1;
            *filename = param_value;
            seen_file = 1;
        }

        p = ngx_js_form_skip_ows(p, end);

        if (p == end) {
            break;
        }
    }

    if (!seen_name) {
        ngx_js_form_error(error, "multipart field name is required");
        return NGX_JS_FORM_PARSE_ERROR;
    }

    return NGX_JS_FORM_OK;
}


static ngx_int_t
ngx_js_form_parse_param(ngx_pool_t *pool, u_char **pp, u_char *end,
    ngx_str_t *param, ngx_str_t *value, ngx_flag_t *quoted, ngx_str_t *error)
{
    u_char  *p, *start;

    p = ngx_js_form_skip_ows(*pp, end);
    start = p;

    while (p < end && *p != '=' && *p != ';' && !ngx_js_form_is_ows(*p)) {
        p++;
    }

    if (p == start) {
        ngx_js_form_error(error, "malformed parameter");
        return NGX_JS_FORM_PARSE_ERROR;
    }

    if (ngx_js_form_copy(pool, start, p, param) != NGX_OK) {
        return NGX_ERROR;
    }

    p = ngx_js_form_skip_ows(p, end);

    if (p == end || *p != '=') {
        ngx_js_form_error(error, "malformed parameter");
        return NGX_JS_FORM_PARSE_ERROR;
    }

    p++;
    p = ngx_js_form_skip_ows(p, end);

    *quoted = 0;

    if (p < end && *p == '"') {
        start = ++p;

        while (p < end && *p != '"') {
            if (*p == '\\' && p + 1 < end) {
                p += 2;
                continue;
            }

            p++;
        }

        if (p == end) {
            ngx_js_form_error(error, "unterminated quoted parameter");
            return NGX_JS_FORM_PARSE_ERROR;
        }

        if (ngx_js_form_copy_quoted(pool, start, p, value) != NGX_OK) {
            return NGX_ERROR;
        }

        *quoted = 1;
        p++;

    } else {
        start = p;

        while (p < end && *p != ';' && !ngx_js_form_is_ows(*p)) {
            p++;
        }

        if (start == p) {
            ngx_js_form_error(error, "empty parameter value");
            return NGX_JS_FORM_PARSE_ERROR;
        }

        if (ngx_js_form_copy(pool, start, p, value) != NGX_OK) {
            return NGX_ERROR;
        }
    }

    *pp = p;

    return NGX_JS_FORM_OK;
}


static ngx_int_t
ngx_js_form_add_entry(ngx_js_form_t *form, ngx_pool_t *pool, ngx_str_t *name,
    ngx_str_t *value, ngx_uint_t *count, ngx_uint_t max_keys,
    ngx_str_t *filename, ngx_flag_t is_file, ngx_str_t *error)
{
    ngx_js_form_entry_t  *entry;

    if (++(*count) > max_keys) {
        ngx_js_form_error(error, "maxKeys limit exceeded");
        return NGX_JS_FORM_PARSE_ERROR;
    }

    entry = ngx_array_push(&form->entries);
    if (entry == NULL) {
        return NGX_ERROR;
    }

    entry->name = *name;
    entry->value = *value;
    entry->is_file = is_file;

    if (filename != NULL) {
        entry->filename = *filename;

    } else {
        ngx_str_null(&entry->filename);
    }

    return NGX_JS_FORM_OK;
}


static ngx_int_t
ngx_js_form_decode_urlencoded(ngx_pool_t *pool, u_char *start, u_char *end,
    ngx_str_t *dst, ngx_str_t *error)
{
    u_char     *p, *d, *out;
    ngx_int_t   n;

    out = ngx_pnalloc(pool, (end - start) + 1);
    if (out == NULL) {
        return NGX_ERROR;
    }

    d = out;

    for (p = start; p < end; p++) {
        if (*p == '+') {
            *d++ = ' ';
            continue;
        }

        if (*p == '%') {
            if (p + 2 >= end) {
                ngx_js_form_error(error, "malformed percent escape");
                return NGX_JS_FORM_PARSE_ERROR;
            }

            n = ngx_hextoi(p + 1, 2);
            if (n == NGX_ERROR) {
                ngx_js_form_error(error, "malformed percent escape");
                return NGX_JS_FORM_PARSE_ERROR;
            }

            *d++ = (u_char) n;
            p += 2;
            continue;
        }

        *d++ = *p;
    }

    *d = '\0';
    dst->data = out;
    dst->len = d - out;

    return NGX_JS_FORM_OK;
}


static ngx_int_t
ngx_js_form_copy(ngx_pool_t *pool, u_char *start, u_char *end, ngx_str_t *dst)
{
    dst->len = end - start;

    if (dst->len == 0) {
        dst->data = (u_char *) "";
        return NGX_OK;
    }

    dst->data = ngx_pnalloc(pool, dst->len + 1);
    if (dst->data == NULL) {
        return NGX_ERROR;
    }

    ngx_memcpy(dst->data, start, dst->len);
    dst->data[dst->len] = '\0';

    return NGX_OK;
}


static ngx_int_t
ngx_js_form_copy_quoted(ngx_pool_t *pool, u_char *start, u_char *end,
    ngx_str_t *dst)
{
    u_char  *p, *d;

    dst->len = end - start;

    if (dst->len == 0) {
        dst->data = (u_char *) "";
        return NGX_OK;
    }

    dst->data = ngx_pnalloc(pool, dst->len + 1);
    if (dst->data == NULL) {
        return NGX_ERROR;
    }

    d = dst->data;

    for (p = start; p < end; p++) {
        if (*p == '\\' && p + 1 < end) {
            p++;
        }

        *d++ = *p;
    }

    *d = '\0';
    dst->len = d - dst->data;

    return NGX_OK;
}


static void
ngx_js_form_error(ngx_str_t *error, const char *text)
{
    error->data = (u_char *) text;
    error->len = ngx_strlen(text);
}


static u_char *
ngx_js_form_skip_ows(u_char *p, u_char *end)
{
    while (p < end && ngx_js_form_is_ows(*p)) {
        p++;
    }

    return p;
}


static ngx_uint_t
ngx_js_form_is_ows(u_char ch)
{
    return ch == ' ' || ch == '\t';
}


static u_char *
ngx_js_form_find(u_char *start, u_char *end, u_char *pattern, size_t len)
{
    u_char  *p, *last;

    if ((size_t) (end - start) < len) {
        return NULL;
    }

    last = end - len + 1;

    for (p = start; p < last; p++) {
        if (*p == pattern[0] && ngx_memcmp(p, pattern, len) == 0) {
            return p;
        }
    }

    return NULL;
}
