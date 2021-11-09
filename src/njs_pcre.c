
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) NGINX, Inc.
 */


#include <njs_main.h>


static void *njs_pcre_malloc(size_t size);
static void njs_pcre_free(void *p);


static njs_regex_context_t  *regex_context;


njs_regex_context_t *
njs_regex_context_create(njs_pcre_malloc_t private_malloc,
    njs_pcre_free_t private_free, void *memory_data)
{
    njs_regex_context_t  *ctx;

    ctx = private_malloc(sizeof(njs_regex_context_t), memory_data);

    if (njs_fast_path(ctx != NULL)) {
        ctx->private_malloc = private_malloc;
        ctx->private_free = private_free;
        ctx->memory_data = memory_data;
    }

    return ctx;
}


njs_int_t
njs_regex_compile(njs_regex_t *regex, u_char *source, size_t len,
    njs_uint_t options, njs_regex_context_t *ctx)
{
    int         ret, err, erroff;
    char        *pattern, *error;
    void        *(*saved_malloc)(size_t size);
    void        (*saved_free)(void *p);
    const char  *errstr;

    ret = NJS_ERROR;

    saved_malloc = pcre_malloc;
    pcre_malloc = njs_pcre_malloc;
    saved_free = pcre_free;
    pcre_free = njs_pcre_free;
    regex_context = ctx;

    if (len == 0) {
        pattern = (char *) source;

    } else {
        pattern = ctx->private_malloc(len + 1, ctx->memory_data);
        if (njs_slow_path(pattern == NULL)) {
            goto done;
        }

        memcpy(pattern, source, len);
        pattern[len] = '\0';
    }

    regex->code = pcre_compile(pattern, options, &errstr, &erroff, NULL);

    if (njs_slow_path(regex->code == NULL)) {
        error = pattern + erroff;

        if (*error != '\0') {
            njs_alert(ctx->trace, NJS_LEVEL_ERROR,
                      "pcre_compile(\"%s\") failed: %s at \"%s\"",
                      pattern, errstr, error);

        } else {
            njs_alert(ctx->trace, NJS_LEVEL_ERROR,
                      "pcre_compile(\"%s\") failed: %s", pattern, errstr);
        }

        ret = NJS_DECLINED;

        goto done;
    }

    regex->extra = pcre_study(regex->code, 0, &errstr);

    if (njs_slow_path(errstr != NULL)) {
        njs_alert(ctx->trace, NJS_LEVEL_WARN,
                  "pcre_study(\"%s\") failed: %s", pattern, errstr);
    }

    err = pcre_fullinfo(regex->code, NULL, PCRE_INFO_CAPTURECOUNT,
                        &regex->ncaptures);

    if (njs_slow_path(err < 0)) {
        njs_alert(ctx->trace, NJS_LEVEL_ERROR,
                  "pcre_fullinfo(\"%s\", PCRE_INFO_CAPTURECOUNT) failed: %d",
                  pattern, err);

        goto done;
    }

    err = pcre_fullinfo(regex->code, NULL, PCRE_INFO_BACKREFMAX,
                        &regex->backrefmax);

    if (njs_slow_path(err < 0)) {
        njs_alert(ctx->trace, NJS_LEVEL_ERROR,
                  "pcre_fullinfo(\"%s\", PCRE_INFO_BACKREFMAX) failed: %d",
                  pattern, err);

        goto done;
    }

    /* Reserve additional elements for the first "$0" capture. */
    regex->ncaptures++;

    if (regex->ncaptures > 1) {
        err = pcre_fullinfo(regex->code, NULL, PCRE_INFO_NAMECOUNT,
                            &regex->nentries);

        if (njs_slow_path(err < 0)) {
            njs_alert(ctx->trace, NJS_LEVEL_ERROR,
                      "pcre_fullinfo(\"%s\", PCRE_INFO_NAMECOUNT) failed: %d",
                      pattern, err);

            goto done;
        }

        if (regex->nentries != 0) {
            err = pcre_fullinfo(regex->code, NULL, PCRE_INFO_NAMEENTRYSIZE,
                                &regex->entry_size);

            if (njs_slow_path(err < 0)) {
                njs_alert(ctx->trace, NJS_LEVEL_ERROR, "pcre_fullinfo(\"%s\", "
                          "PCRE_INFO_NAMEENTRYSIZE) failed: %d", pattern, err);

                goto done;
            }

            err = pcre_fullinfo(regex->code, NULL, PCRE_INFO_NAMETABLE,
                                &regex->entries);

            if (njs_slow_path(err < 0)) {
                njs_alert(ctx->trace, NJS_LEVEL_ERROR, "pcre_fullinfo(\"%s\", "
                          "PCRE_INFO_NAMETABLE) failed: %d", pattern, err);

                goto done;
            }
        }
    }

    ret = NJS_OK;

done:

    pcre_malloc = saved_malloc;
    pcre_free = saved_free;
    regex_context = NULL;

    return ret;
}


njs_bool_t
njs_regex_is_valid(njs_regex_t *regex)
{
    return (regex->code != NULL);
}


njs_int_t
njs_regex_named_captures(njs_regex_t *regex, njs_str_t *name, int n)
{
    char  *entry;

    if (name == NULL) {
        return regex->nentries;
    }

    if (n >= regex->nentries) {
        return NJS_ERROR;
    }

    entry = regex->entries + regex->entry_size * n;

    name->start = (u_char *) entry + 2;
    name->length = njs_strlen(name->start);

    return (entry[0] << 8) + entry[1];
}


njs_regex_match_data_t *
njs_regex_match_data(njs_regex_t *regex, njs_regex_context_t *ctx)
{
    size_t                  size;
    njs_uint_t              ncaptures;
    njs_regex_match_data_t  *match_data;

    if (regex != NULL) {
        ncaptures = regex->ncaptures - 1;

    } else {
        ncaptures = 0;
    }

    /* Each capture is stored in 3 "int" vector elements. */
    ncaptures *= 3;
    size = sizeof(njs_regex_match_data_t) + ncaptures * sizeof(int);

    match_data = ctx->private_malloc(size, ctx->memory_data);

    if (njs_fast_path(match_data != NULL)) {
        match_data->ncaptures = ncaptures + 3;
    }

    return match_data;
}


void
njs_regex_match_data_free(njs_regex_match_data_t *match_data,
    njs_regex_context_t *ctx)
{
    ctx->private_free(match_data, ctx->memory_data);
}


static void *
njs_pcre_malloc(size_t size)
{
    return regex_context->private_malloc(size, regex_context->memory_data);
}


static void
njs_pcre_free(void *p)
{
    regex_context->private_free(p, regex_context->memory_data);
}


njs_int_t
njs_regex_match(njs_regex_t *regex, const u_char *subject, size_t off,
    size_t len, njs_regex_match_data_t *match_data, njs_regex_context_t *ctx)
{
    int  ret;

    ret = pcre_exec(regex->code, regex->extra, (const char *) subject, len,
                    off, 0, match_data->captures, match_data->ncaptures);

    /* PCRE_ERROR_NOMATCH is -1. */

    if (njs_slow_path(ret < PCRE_ERROR_NOMATCH)) {
        njs_alert(ctx->trace, NJS_LEVEL_ERROR, "pcre_exec() failed: %d", ret);
    }

    return ret;
}


int *
njs_regex_captures(njs_regex_match_data_t *match_data)
{
    return match_data->captures;
}
