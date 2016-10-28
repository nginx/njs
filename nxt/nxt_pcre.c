
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) NGINX, Inc.
 */

#include <nxt_auto_config.h>
#include <nxt_types.h>
#include <nxt_clang.h>
#include <nxt_stub.h>
#include <nxt_trace.h>
#include <nxt_regex.h>
#include <nxt_pcre.h>
#include <string.h>


static void *nxt_pcre_malloc(size_t size);
static void nxt_pcre_free(void *p);
static void *nxt_pcre_default_malloc(size_t size, void *memory_data);
static void nxt_pcre_default_free(void *p, void *memory_data);


static nxt_regex_context_t  *regex_context;


nxt_regex_context_t *
nxt_regex_context_create(nxt_pcre_malloc_t private_malloc,
    nxt_pcre_free_t private_free, void *memory_data)
{
    nxt_regex_context_t  *ctx;

    if (private_malloc == NULL) {
        private_malloc = nxt_pcre_default_malloc;
        private_free = nxt_pcre_default_free;
    }

    ctx = private_malloc(sizeof(nxt_regex_context_t), memory_data);

    if (nxt_fast_path(ctx != NULL)) {
        ctx->private_malloc = private_malloc;
        ctx->private_free = private_free;
        ctx->memory_data = memory_data;
    }

    return ctx;
}


nxt_int_t
nxt_regex_compile(nxt_regex_t *regex, u_char *source, size_t len,
    nxt_uint_t options, nxt_regex_context_t *ctx)
{
    int         ret, err, erroff;
    char        *pattern, *error;
    void        *(*saved_malloc)(size_t size);
    void        (*saved_free)(void *p);
    const char  *errstr;

    ret = NXT_ERROR;

    saved_malloc = pcre_malloc;
    pcre_malloc = nxt_pcre_malloc;
    saved_free = pcre_free;
    pcre_free = nxt_pcre_free;
    regex_context = ctx;

    if (len == 0) {
        pattern = (char *) source;

    } else {
        pattern = ctx->private_malloc(len + 1, ctx->memory_data);
        if (nxt_slow_path(pattern == NULL)) {
            goto done;
        }

        memcpy(pattern, source, len);
        pattern[len] = '\0';
    }

    regex->code = pcre_compile(pattern, options, &errstr, &erroff, NULL);

    if (nxt_slow_path(regex->code == NULL)) {
        error = pattern + erroff;

        if (*error != '\0') {
            nxt_alert(ctx->trace, NXT_LEVEL_ERROR,
                      "pcre_compile(\"%s\") failed: %s at \"%s\"",
                      pattern, errstr, error);

        } else {
            nxt_alert(ctx->trace, NXT_LEVEL_ERROR,
                      "pcre_compile(\"%s\") failed: %s", pattern, errstr);
        }

        goto done;
    }

    regex->extra = pcre_study(regex->code, 0, &errstr);

    if (nxt_slow_path(errstr != NULL)) {
        nxt_alert(ctx->trace, NXT_LEVEL_ERROR,
                  "pcre_study(\"%s\") failed: %s", pattern, errstr);

        goto done;
    }

    err = pcre_fullinfo(regex->code, NULL, PCRE_INFO_CAPTURECOUNT,
                        &regex->ncaptures);

    if (nxt_slow_path(err < 0)) {
        nxt_alert(ctx->trace, NXT_LEVEL_ERROR,
                  "pcre_fullinfo(\"%s\", PCRE_INFO_CAPTURECOUNT) failed: %d",
                  pattern, err);

        goto done;
    }

    /* Reserve additional elements for the first "$0" capture. */
    regex->ncaptures++;

    ret = NXT_OK;

done:

    pcre_malloc = saved_malloc;
    pcre_free = saved_free;
    regex_context = NULL;

    return ret;
}


nxt_bool_t
nxt_regex_is_valid(nxt_regex_t *regex)
{
    return (regex->code != NULL);
}


nxt_uint_t
nxt_regex_ncaptures(nxt_regex_t *regex)
{
    return regex->ncaptures;
}


nxt_regex_match_data_t *
nxt_regex_match_data(nxt_regex_t *regex, nxt_regex_context_t *ctx)
{
    size_t                  size;
    nxt_uint_t              ncaptures;
    nxt_regex_match_data_t  *match_data;

    if (regex != NULL) {
        ncaptures = regex->ncaptures - 1;

    } else {
        ncaptures = 0;
    }

    /* Each capture is stored in 3 "int" vector elements. */
    ncaptures *= 3;
    size = sizeof(nxt_regex_match_data_t) + ncaptures * sizeof(int);

    match_data = ctx->private_malloc(size, ctx->memory_data);

    if (nxt_fast_path(match_data != NULL)) {
        match_data->ncaptures = ncaptures + 3;
    }

    return match_data;
}


void
nxt_regex_match_data_free(nxt_regex_match_data_t *match_data,
    nxt_regex_context_t *ctx)
{
    ctx->private_free(match_data, ctx->memory_data);
}


static void *
nxt_pcre_malloc(size_t size)
{
    return regex_context->private_malloc(size, regex_context->memory_data);
}


static void
nxt_pcre_free(void *p)
{
    regex_context->private_free(p, regex_context->memory_data);
}


static void *
nxt_pcre_default_malloc(size_t size, void *memory_data)
{
    return malloc(size);
}


static void
nxt_pcre_default_free(void *p, void *memory_data)
{
    free(p);
}


nxt_int_t
nxt_regex_match(nxt_regex_t *regex, u_char *subject, size_t len,
    nxt_regex_match_data_t *match_data, nxt_regex_context_t *ctx)
{
    int  ret;

    ret = pcre_exec(regex->code, regex->extra, (char *) subject, len, 0, 0,
                    match_data->captures, match_data->ncaptures);

    /* PCRE_ERROR_NOMATCH is -1. */

    if (nxt_slow_path(ret < PCRE_ERROR_NOMATCH)) {
        nxt_alert(ctx->trace, NXT_LEVEL_ERROR, "pcre_exec() failed: %d", ret);
    }

    return ret;
}


int *
nxt_regex_captures(nxt_regex_match_data_t *match_data)
{
    return match_data->captures;
}
