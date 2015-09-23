
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) NGINX, Inc.
 */

#include <nxt_types.h>
#include <nxt_clang.h>
#include <nxt_alignment.h>
#include <nxt_stub.h>
#include <nxt_utf8.h>
#include <nxt_djb_hash.h>
#include <nxt_array.h>
#include <nxt_lvlhsh.h>
#include <nxt_malloc.h>
#include <nxt_mem_cache_pool.h>
#include <njscript.h>
#include <njs_vm.h>
#include <njs_regexp.h>
#include <njs_regexp_pattern.h>
#include <string.h>


static int njs_regexp_pattern_compile(pcre **code, pcre_extra **extra,
    u_char *source, int options);
static njs_ret_t njs_regexp_exec_result(njs_vm_t *vm, njs_regexp_t *regexp,
    u_char *string, int *captures, nxt_uint_t utf8);


njs_regexp_t *
njs_regexp_alloc(njs_vm_t *vm, njs_regexp_pattern_t *pattern)
{
    njs_regexp_t  *regexp;

    regexp = nxt_mem_cache_align(vm->mem_cache_pool, sizeof(njs_value_t),
                                 sizeof(njs_regexp_t));

    if (nxt_fast_path(regexp != NULL)) {
        nxt_lvlhsh_init(&regexp->object.hash);
        nxt_lvlhsh_init(&regexp->object.shared_hash);
        regexp->object.__proto__ = &vm->prototypes[NJS_PROTOTYPE_REGEXP];
        regexp->last_index = 0;
        regexp->pattern = pattern;
    }

    return regexp;
}


njs_regexp_pattern_t *
njs_regexp_pattern_create(njs_vm_t *vm, nxt_str_t *source,
    njs_regexp_flags_t flags)
{
    int                   options, ret;
    u_char                *p;
    njs_regexp_pattern_t  *pattern;

    /* TODO: pcre_malloc */

    pattern = nxt_mem_cache_alloc(vm->mem_cache_pool,
                            sizeof(njs_regexp_pattern_t) + source->len + 1);
    if (nxt_slow_path(pattern == NULL)) {
        return NULL;
    }

    p = (u_char *) pattern + sizeof(njs_regexp_pattern_t);
    pattern->source = p;

    p = memcpy(p, source->data, source->len);
    p += source->len;
    *p = '\0';

    pattern->ncaptures = 0;

    pattern->global = ((flags & NJS_REGEXP_GLOBAL) != 0);

#ifdef PCRE_JAVASCRIPT_COMPAT
    /* JavaScript compatibility has been introduced in PCRE-7.7. */
    options = PCRE_JAVASCRIPT_COMPAT;
#else
    options = 0;
#endif

    if ((flags & NJS_REGEXP_IGNORE_CASE) != 0) {
         pattern->ignore_case = 1;
         options |= PCRE_CASELESS;
    }

    if ((flags & NJS_REGEXP_MULTILINE) != 0) {
         pattern->multiline = 1;
         options |= PCRE_MULTILINE;
    }

    ret = njs_regexp_pattern_compile(&pattern->code[0], &pattern->extra[0],
                                     pattern->source, options);

    if (nxt_slow_path(ret < 0)) {
        return NULL;
    }

    pattern->ncaptures = ret;

    ret = njs_regexp_pattern_compile(&pattern->code[1], &pattern->extra[1],
                                     pattern->source, options | PCRE_UTF8);

    if (nxt_slow_path(ret < 0)) {

        if (ret == NXT_DECLINED) {
            return pattern;
        }

        return NULL;
    }

    if (nxt_fast_path((unsigned) ret == pattern->ncaptures)) {
        return pattern;
    }

    nxt_thread_log_error(NXT_LOG_ERR, "numbers of byte and UTF-8 captures "
                         "in RegExp \"%s\" vary: %d vs %d",
                         pattern->source, pattern->ncaptures, ret);

    return NULL;
}


static int
njs_regexp_pattern_compile(pcre **code, pcre_extra **extra, u_char *source,
    int options)
{
    int         ret, erroff, captures;
    u_char      *error;
    const char  *errstr;

    *code = pcre_compile((char *) source, options, &errstr, &erroff, NULL);

    if (nxt_slow_path(*code == NULL)) {

        if ((options & PCRE_UTF8) != 0) {
            return NXT_DECLINED;
        }

        error = source + erroff;

        if (*error != '\0') {
            nxt_thread_log_error(NXT_LOG_ERR,
                                 "pcre_compile(\"%s\") failed: %s at \"%s\"",
                                 source, errstr, error);
        } else {
            nxt_thread_log_error(NXT_LOG_ERR,
                                 "pcre_compile(\"%s\") failed: %s",
                                 source, errstr);
        }

        return NXT_ERROR;
    }

    *extra = pcre_study(*code, 0, &errstr);

    if (nxt_slow_path(errstr != NULL)) {
        nxt_thread_log_error(NXT_LOG_ERR, "pcre_study(\"%s\") failed: %s",
                             source, errstr);
        return NXT_ERROR;
    }

    ret = pcre_fullinfo(*code, NULL, PCRE_INFO_CAPTURECOUNT, &captures);

    if (nxt_fast_path(ret >= 0)) {
        /* Reserve additional elements for the first "$0" capture. */
        return captures + 1;
    }

    nxt_thread_log_error(NXT_LOG_ERR,
                   "pcre_fullinfo(\"%s\", PCRE_INFO_CAPTURECOUNT) failed: %d",
                   source, ret);

    return NXT_ERROR;
}


njs_ret_t
njs_regexp_function(njs_vm_t *vm, njs_param_t *param)
{
    return NXT_ERROR;
}


static njs_ret_t
njs_regexp_prototype_last_index(njs_vm_t *vm, njs_value_t *value)
{
    uint32_t           index;
    njs_regexp_t       *regexp;
    njs_string_prop_t  string;

    njs_release(vm, value);

    regexp = value->data.u.regexp;

    (void) njs_string_prop(&string, &regexp->string);

    index = njs_string_index(&string, regexp->last_index);
    njs_number_set(&vm->retval, index);

    return NXT_OK;
}


static njs_ret_t
njs_regexp_prototype_ignore_case(njs_vm_t *vm, njs_value_t *regexp)
{
    njs_regexp_pattern_t  *pattern;

    pattern = regexp->data.u.regexp->pattern;
    vm->retval = pattern->ignore_case ? njs_value_true : njs_value_false;
    njs_release(vm, regexp);

    return NXT_OK;
}


static njs_ret_t
njs_regexp_prototype_global(njs_vm_t *vm, njs_value_t *regexp)
{
    njs_regexp_pattern_t  *pattern;

    pattern = regexp->data.u.regexp->pattern;
    vm->retval = pattern->global ? njs_value_true : njs_value_false;
    njs_release(vm, regexp);

    return NXT_OK;
}


static njs_ret_t
njs_regexp_prototype_multiline(njs_vm_t *vm, njs_value_t *regexp)
{
    njs_regexp_pattern_t  *pattern;

    pattern = regexp->data.u.regexp->pattern;
    vm->retval = pattern->multiline ? njs_value_true : njs_value_false;
    njs_release(vm, regexp);

    return NXT_OK;
}


static njs_ret_t
njs_regexp_prototype_source(njs_vm_t *vm, njs_value_t *regexp)
{
    size_t                length;
    u_char                *source;
    njs_regexp_pattern_t  *pattern;

    pattern = regexp->data.u.regexp->pattern;

    /*
     * The pattern source is stored not as value but as C string even
     * without length, because retrieving it is very seldom operation.
     */
    source = pattern->source;

    /* TODO: can regexp string be UTF-8? */
    length = strlen((char *) source);

    return njs_string_create(vm, &vm->retval, source, length, length);
}


static njs_ret_t
njs_regexp_prototype_test(njs_vm_t *vm, njs_param_t *param)
{
    nxt_uint_t             n;
    njs_ret_t             ret;
    njs_value_t           val;
    const njs_value_t     *retval;
    njs_string_prop_t     string;
    njs_regexp_pattern_t  *pattern;

    if (param->nargs != 0) {
        ret = njs_value_to_string(vm, &val, &param->args[0]);

        if (nxt_fast_path(ret == NXT_OK)) {
            retval = &njs_value_false;

            (void) njs_string_prop(&string, &val);

            n = (string.length != 0 && string.length != string.size);
            pattern = param->object->data.u.regexp->pattern;

            if (pattern->code[n] != NULL) {
                ret = pcre_exec(pattern->code[n], pattern->extra[n],
                                (char *) string.start, string.size,
                                0, 0, NULL, 0);

                if (ret >= 0) {
                    retval = &njs_value_true;

                } else if (ret != PCRE_ERROR_NOMATCH) {
                    /* TODO: exception */
                    return NXT_ERROR;
                }
            }

            vm->retval = *retval;

            return NXT_OK;
        }
    }

    return NXT_ERROR;
}


njs_ret_t
njs_regexp_prototype_exec(njs_vm_t *vm, njs_param_t *param)
{
    int                   *captures, ncaptures;
    nxt_uint_t             n, utf8;
    njs_ret_t             ret;
    njs_regexp_t          *regexp;
    njs_string_prop_t     string;
    njs_regexp_pattern_t  *pattern;

    if (param->nargs != 0) {
        regexp = param->object->data.u.regexp;

        ret = njs_value_to_string(vm, &regexp->string, &param->args[0]);

        if (nxt_fast_path(ret == NXT_OK)) {
            (void) njs_string_prop(&string, &regexp->string);

            utf8 = 0;
            n = 0;

            if (string.length != 0) {
                utf8 = 1;
                n = 1;

                if (string.length != string.size) {
                    utf8 = 2;
                }
            }

            pattern = regexp->pattern;

            if (pattern->code[n] != NULL) {
                string.start += regexp->last_index;
                string.size -= regexp->last_index;

                /* Each capture is stored in 3 vector elements. */
                ncaptures = pattern->ncaptures * 3;

                captures = alloca(ncaptures * sizeof(int));

                ret = pcre_exec(pattern->code[n], pattern->extra[n],
                                (char *) string.start, string.size,
                                0, 0, captures, ncaptures);

                if (ret >= 0) {
                    return njs_regexp_exec_result(vm, regexp, string.start,
                                                  captures, utf8);
                }

                if (nxt_slow_path(ret != PCRE_ERROR_NOMATCH)) {
                    /* TODO: exception */
                    return NXT_ERROR;
                }
            }

            regexp->last_index = 0;
            vm->retval = njs_value_null;

            return NXT_OK;
        }
    }

    return NXT_ERROR;
}


static njs_ret_t
njs_regexp_exec_result(njs_vm_t *vm, njs_regexp_t *regexp, u_char *string,
    int *captures, nxt_uint_t utf8)
{
    u_char              *start;
    int32_t             size, length;
    nxt_uint_t           i, n;
    njs_ret_t           ret;
    njs_array_t         *array;
    njs_object_prop_t   *prop;
    nxt_lvlhsh_query_t  lhq;

    static const njs_value_t  njs_string_index = njs_string("index");
    static const njs_value_t  njs_string_input = njs_string("input");

    array = njs_array_alloc(vm, regexp->pattern->ncaptures, 0);
    if (nxt_slow_path(array == NULL)) {
        return NXT_ERROR;
    }

    for (i = 0; i < regexp->pattern->ncaptures; i++) {
        n = 2 * i;

        if (captures[n] != -1) {
            start = &string[captures[n]];
            size = captures[n + 1] - captures[n];

            switch (utf8) {
            case 0:
                length = 0;
                break;
            case 1:
                length = size;
                break;
            default:
                length = nxt_utf8_length(start, size);
                break;
            }

            ret = njs_string_create(vm, &array->start[i], start, size, length);
            if (nxt_slow_path(ret != NXT_OK)) {
                return NXT_ERROR;
            }

        } else {
            array->start[i] = njs_value_void;
        }
    }

    prop = njs_object_prop_alloc(vm, &njs_string_index);
    if (nxt_slow_path(prop == NULL)) {
        return NXT_ERROR;
    }

    /* TODO: Non UTF-8 position */

    njs_number_set(&prop->value, regexp->last_index + captures[0]);

    if (regexp->pattern->global) {
        regexp->last_index += captures[1];
    }

    lhq.key_hash = NJS_INDEX_HASH;
    lhq.key.len = sizeof("index") - 1;
    lhq.key.data = (u_char *) "index";
    lhq.replace = 0;
    lhq.value = prop;
    lhq.pool = vm->mem_cache_pool;
    lhq.proto = &njs_object_hash_proto;

    ret = nxt_lvlhsh_insert(&array->object.hash, &lhq);
    if (nxt_slow_path(ret != NXT_OK)) {
        /* Only NXT_ERROR can be returned here. */
        return ret;
    }

    prop = njs_object_prop_alloc(vm, &njs_string_input);
    if (nxt_slow_path(prop == NULL)) {
        return NXT_ERROR;
    }

    njs_string_copy(&prop->value, &regexp->string);

    lhq.key_hash = NJS_INPUT_HASH;
    lhq.key.len = sizeof("input") - 1;
    lhq.key.data = (u_char *) "input";
    lhq.value = prop;

    ret = nxt_lvlhsh_insert(&array->object.hash, &lhq);
    if (nxt_slow_path(ret != NXT_OK)) {
        /* Only NXT_ERROR can be returned here. */
        return ret;
    }

    vm->retval.data.u.array = array;
    vm->retval.type = NJS_ARRAY;
    vm->retval.data.truth = 1;

    return NXT_OK;
}


static const njs_object_prop_t  njs_regexp_function_properties[] =
{
    { njs_string("RegExp"),
      njs_string("name"),
      NJS_PROPERTY, 0, 0, 0, },

    { njs_value(NJS_NUMBER, 1, 2.0),
      njs_string("length"),
      NJS_PROPERTY, 0, 0, 0, },

    { njs_getter(njs_object_prototype_create_prototype),
      njs_string("prototype"),
      NJS_NATIVE_GETTER, 0, 0, 0, },
};


nxt_int_t
njs_regexp_function_hash(njs_vm_t *vm, nxt_lvlhsh_t *hash)
{
    return njs_object_hash_create(vm, hash, njs_regexp_function_properties,
                                  nxt_nitems(njs_regexp_function_properties));
}


static const njs_object_prop_t  njs_regexp_prototype_properties[] =
{
    { njs_getter(njs_regexp_prototype_last_index),
      njs_string("lastIndex"),
      NJS_NATIVE_GETTER, 0, 0, 0, },

    { njs_getter(njs_regexp_prototype_ignore_case),
      njs_string("ignoreCase"),
      NJS_NATIVE_GETTER, 0, 0, 0, },

    { njs_getter(njs_regexp_prototype_global),
      njs_string("global"),
      NJS_NATIVE_GETTER, 0, 0, 0, },

    { njs_getter(njs_regexp_prototype_multiline),
      njs_string("multiline"),
      NJS_NATIVE_GETTER, 0, 0, 0, },

    { njs_getter(njs_regexp_prototype_source),
      njs_string("source"),
      NJS_NATIVE_GETTER, 0, 0, 0, },

    { njs_native_function(njs_regexp_prototype_test, 0),
      njs_string("test"),
      NJS_METHOD, 0, 0, 0, },

    { njs_native_function(njs_regexp_prototype_exec, 0),
      njs_string("exec"),
      NJS_METHOD, 0, 0, 0, },
};


nxt_int_t
njs_regexp_prototype_hash(njs_vm_t *vm, nxt_lvlhsh_t *hash)
{
    return njs_object_hash_create(vm, hash, njs_regexp_prototype_properties,
                                  nxt_nitems(njs_regexp_prototype_properties));
}
