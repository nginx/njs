
/*
 * Copyright (C) Dmitry Volyntsev
 * Copyright (C) NGINX, Inc.
 */


#include <njs_main.h>

#define PCRE2_CODE_UNIT_WIDTH 8
#include <pcre2.h>


static const u_char* njs_regex_pcre2_error(int errcode, u_char buffer[128]);


njs_regex_generic_ctx_t *
njs_regex_generic_ctx_create(njs_pcre_malloc_t private_malloc,
    njs_pcre_free_t private_free, void *memory_data)
{
    return pcre2_general_context_create(private_malloc, private_free,
                                        memory_data);
}


njs_regex_compile_ctx_t *
njs_regex_compile_ctx_create(njs_regex_generic_ctx_t *ctx)
{
    return pcre2_compile_context_create(ctx);
}


njs_int_t
njs_regex_escape(njs_mp_t *mp, njs_str_t *text)
{
    return NJS_OK;
}


njs_int_t
njs_regex_compile(njs_regex_t *regex, u_char *source, size_t len,
    njs_regex_flags_t flags, njs_regex_compile_ctx_t *ctx, njs_trace_t *trace)
{
    int         ret;
    u_char      *error;
    size_t      erroff;
    njs_uint_t  options;
    u_char      errstr[128];

    options = PCRE2_ALT_BSUX | PCRE2_MATCH_UNSET_BACKREF;

    if ((flags & NJS_REGEX_IGNORE_CASE)) {
         options |= PCRE2_CASELESS;
    }

    if ((flags & NJS_REGEX_MULTILINE)) {
         options |= PCRE2_MULTILINE;
    }

    if ((flags & NJS_REGEX_STICKY)) {
         options |= PCRE2_ANCHORED;
    }

    if ((flags & NJS_REGEX_UTF8)) {
         options |= PCRE2_UTF;
    }

    regex->code = pcre2_compile(source, len, options, &ret, &erroff, ctx);

    if (njs_slow_path(regex->code == NULL)) {
        error = &source[erroff];

        njs_alert(trace, NJS_LEVEL_ERROR,
                  "pcre_compile2(\"%s\") failed: %s at \"%s\"",
                  source, njs_regex_pcre2_error(ret, errstr), error);

        return NJS_DECLINED;
    }

    ret = pcre2_pattern_info(regex->code, PCRE2_INFO_CAPTURECOUNT,
                             &regex->ncaptures);

    if (njs_slow_path(ret < 0)) {
        njs_alert(trace, NJS_LEVEL_ERROR,
               "pcre2_pattern_info(\"%s\", PCRE2_INFO_CAPTURECOUNT) failed: %s",
               source, njs_regex_pcre2_error(ret, errstr));

        return NJS_ERROR;
    }

    ret = pcre2_pattern_info(regex->code, PCRE2_INFO_BACKREFMAX,
                             &regex->backrefmax);

    if (njs_slow_path(ret < 0)) {
        njs_alert(trace, NJS_LEVEL_ERROR,
                 "pcre2_pattern_info(\"%s\", PCRE2_INFO_BACKREFMAX) failed: %s",
                 source, njs_regex_pcre2_error(ret, errstr));

        return NJS_ERROR;
    }

    /* Reserve additional elements for the first "$0" capture. */
    regex->ncaptures++;

    if (regex->ncaptures > 1) {
        ret = pcre2_pattern_info(regex->code, PCRE2_INFO_NAMECOUNT,
                                 &regex->nentries);

        if (njs_slow_path(ret < 0)) {
            njs_alert(trace, NJS_LEVEL_ERROR,
                  "pcre2_pattern_info(\"%s\", PCRE2_INFO_NAMECOUNT) failed: %s",
                   source, njs_regex_pcre2_error(ret, errstr));

            return NJS_ERROR;
        }

        if (regex->nentries != 0) {
            ret = pcre2_pattern_info(regex->code, PCRE2_INFO_NAMEENTRYSIZE,
                                     &regex->entry_size);

            if (njs_slow_path(ret < 0)) {
                njs_alert(trace, NJS_LEVEL_ERROR,
                          "pcre2_pattern_info(\"%s\", PCRE2_INFO_NAMEENTRYSIZE)"
                          " failed: %s", source,
                          njs_regex_pcre2_error(ret, errstr));

                return NJS_ERROR;
            }

            ret = pcre2_pattern_info(regex->code, PCRE2_INFO_NAMETABLE,
                                     &regex->entries);

            if (njs_slow_path(ret < 0)) {
                njs_alert(trace, NJS_LEVEL_ERROR,
                          "pcre2_pattern_info(\"%s\", PCRE2_INFO_NAMETABLE) "
                          "failed: %s", source,
                          njs_regex_pcre2_error(ret, errstr));

                return NJS_ERROR;
            }
        }
    }

    return NJS_OK;
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
njs_regex_match_data(njs_regex_t *regex, njs_regex_generic_ctx_t *ctx)
{
    if (regex != NULL) {
        return pcre2_match_data_create_from_pattern(regex->code, ctx);
    }

    return pcre2_match_data_create(0, ctx);
}


void
njs_regex_match_data_free(njs_regex_match_data_t *match_data,
    njs_regex_generic_ctx_t *unused)
{
    pcre2_match_data_free(match_data);
}


njs_int_t
njs_regex_match(njs_regex_t *regex, const u_char *subject, size_t off,
    size_t len, njs_regex_match_data_t *match_data, njs_trace_t *trace)
{
    int     ret;
    u_char  errstr[128];

    ret = pcre2_match(regex->code, subject, len, off, 0, match_data, NULL);

    if (ret < 0) {
        if (ret == PCRE2_ERROR_NOMATCH) {
            return NJS_DECLINED;
        }

        njs_alert(trace, NJS_LEVEL_ERROR, "pcre2_match() failed: %s",
                  njs_regex_pcre2_error(ret, errstr));
        return NJS_ERROR;
    }

    return ret;
}


size_t
njs_regex_capture(njs_regex_match_data_t *match_data, njs_uint_t n)
{
    size_t  c;

    c = pcre2_get_ovector_pointer(match_data)[n];

    if (c == PCRE2_UNSET) {
        return NJS_REGEX_UNSET;
    }

    return c;
}


static const u_char *
njs_regex_pcre2_error(int errcode, u_char buffer[128])
{
    pcre2_get_error_message(errcode, buffer, 128);

    return buffer;
}
