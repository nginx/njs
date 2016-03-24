
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) NGINX, Inc.
 */

#include <nxt_auto_config.h>
#include <nxt_types.h>
#include <nxt_clang.h>
#include <nxt_alignment.h>
#include <nxt_stub.h>
#include <nxt_utf8.h>
#include <nxt_djb_hash.h>
#include <nxt_array.h>
#include <nxt_lvlhsh.h>
#include <nxt_random.h>
#include <nxt_malloc.h>
#include <nxt_mem_cache_pool.h>
#include <njscript.h>
#include <njs_vm.h>
#include <njs_string.h>
#include <njs_object.h>
#include <njs_object_hash.h>
#include <njs_array.h>
#include <njs_function.h>
#include <njs_regexp.h>
#include <njs_regexp_pattern.h>
#include <njs_variable.h>
#include <njs_parser.h>
#include <string.h>


static njs_regexp_flags_t njs_regexp_flags(u_char **start, u_char *end,
    nxt_bool_t bound);
static int njs_regexp_pattern_compile(pcre **code, pcre_extra **extra,
    u_char *source, int options);
static njs_ret_t njs_regexp_exec_result(njs_vm_t *vm, njs_regexp_t *regexp,
    u_char *string, int *captures, nxt_uint_t utf8);
static njs_ret_t njs_regexp_string_create(njs_vm_t *vm, njs_value_t *value,
    u_char *start, uint32_t size, int32_t length);


njs_ret_t
njs_regexp_constructor(njs_vm_t *vm, njs_value_t *args, nxt_uint_t nargs,
    njs_index_t unused)
{
    size_t                length;
    njs_regexp_t          *regexp;
    njs_string_prop_t     string;
    njs_regexp_flags_t    flags;
    njs_regexp_pattern_t  *pattern;

    flags = 0;

    switch (nargs) {

    default:
        length = njs_string_prop(&string, &args[2]);

        flags = njs_regexp_flags(&string.start, string.start + length, 1);
        if (nxt_slow_path(flags < 0)) {
            return NXT_ERROR;
        }

        /* Fall through. */

    case 2:
        string.length = njs_string_prop(&string, &args[1]);

        if (string.length != 0) {
            break;
        }

        /* Fall through. */

    case 1:
        string.start = (u_char *) "(?:)";
        string.length = sizeof("(?:)") - 1;
        break;
    }

    pattern = njs_regexp_pattern_create(vm, string.start, string.length, flags);

    if (nxt_fast_path(pattern != NULL)) {

        regexp = njs_regexp_alloc(vm, pattern);

        if (nxt_fast_path(regexp != NULL)) {
            vm->retval.data.u.regexp = regexp;
            vm->retval.type = NJS_REGEXP;
            vm->retval.data.truth = 1;

            return NXT_OK;
        }
    }

    return NXT_ERROR;
}


nxt_int_t
njs_regexp_literal(njs_vm_t *vm, njs_parser_t *parser, njs_value_t *value)
{
    u_char                *p;
    njs_lexer_t           *lexer;
    njs_regexp_flags_t    flags;
    njs_regexp_pattern_t  *pattern;

    lexer = parser->lexer;

    for (p = lexer->start; p < lexer->end; p++) {

        if (*p == '\\') {
            p++;
            continue;
        }

        if (*p == '/') {
            lexer->text.data = lexer->start;
            lexer->text.len = p - lexer->text.data;
            p++;

            flags = njs_regexp_flags(&p, lexer->end, 0);

            if (nxt_slow_path(flags < 0)) {
                return NXT_ERROR;
            }

            lexer->start = p;

            pattern = njs_regexp_pattern_create(vm, lexer->text.data,
                                                lexer->text.len, flags);
            if (nxt_slow_path(pattern == NULL)) {
                return NXT_ERROR;
            }

            value->data.u.data = pattern;

            return NXT_OK;
        }
    }

    return NXT_ERROR;
}


static njs_regexp_flags_t
njs_regexp_flags(u_char **start, u_char *end, nxt_bool_t bound)
{
    u_char              *p;
    njs_regexp_flags_t  flags, flag;

    flags = 0;

    for (p = *start; p < end; p++) {

        switch (*p) {

        case 'g':
            flag = NJS_REGEXP_GLOBAL;
            break;

        case 'i':
            flag = NJS_REGEXP_IGNORE_CASE;
            break;

        case 'm':
            flag = NJS_REGEXP_MULTILINE;
            break;

        default:
            if (bound) {
                return NJS_REGEXP_INVALID_FLAG;
            }

            goto done;
        }

        if (nxt_slow_path((flags & flag) != 0)) {
            return NJS_REGEXP_INVALID_FLAG;
        }

        flags |= flag;
    }

done:

    *start = p;

    return flags;
}


njs_regexp_pattern_t *
njs_regexp_pattern_create(njs_vm_t *vm, u_char *start, size_t length,
    njs_regexp_flags_t flags)
{
    int                   options, ret;
    u_char                *p, *end;
    size_t                size;
    njs_regexp_pattern_t  *pattern;

    size = 1;  /* A trailing "/". */
    size += ((flags & NJS_REGEXP_GLOBAL) != 0);
    size += ((flags & NJS_REGEXP_IGNORE_CASE) != 0);
    size += ((flags & NJS_REGEXP_MULTILINE) != 0);

    pattern = nxt_mem_cache_alloc(vm->mem_cache_pool,
                                  sizeof(njs_regexp_pattern_t)
                                  + 1 + length + size + 1);
    if (nxt_slow_path(pattern == NULL)) {
        return NULL;
    }

    pattern->flags = size;
    pattern->code[0] = NULL;
    pattern->code[1] = NULL;
    pattern->extra[0] = NULL;
    pattern->extra[1] = NULL;
    pattern->next = NULL;

    p = (u_char *) pattern + sizeof(njs_regexp_pattern_t);
    pattern->source = p;

    *p++ = '/';
    p = memcpy(p, start, length);
    p += length;
    end = p;
    *p++ = '\0';

    pattern->global = ((flags & NJS_REGEXP_GLOBAL) != 0);
    if (pattern->global) {
        *p++ = 'g';
    }

#ifdef PCRE_JAVASCRIPT_COMPAT
    /* JavaScript compatibility has been introduced in PCRE-7.7. */
    options = PCRE_JAVASCRIPT_COMPAT;
#else
    options = 0;
#endif

    pattern->ignore_case = ((flags & NJS_REGEXP_IGNORE_CASE) != 0);
    if (pattern->ignore_case) {
        *p++ = 'i';
         options |= PCRE_CASELESS;
    }

    pattern->multiline = ((flags & NJS_REGEXP_MULTILINE) != 0);
    if (pattern->multiline) {
        *p++ = 'm';
         options |= PCRE_MULTILINE;
    }

    *p++ = '\0';

    ret = njs_regexp_pattern_compile(&pattern->code[0], &pattern->extra[0],
                                     &pattern->source[1], options);

    if (nxt_slow_path(ret < 0)) {
        return NULL;
    }

    pattern->ncaptures = ret;

    ret = njs_regexp_pattern_compile(&pattern->code[1], &pattern->extra[1],
                                     &pattern->source[1], options | PCRE_UTF8);

    if (nxt_fast_path(ret >= 0)) {

        if (nxt_slow_path((u_int) ret != pattern->ncaptures)) {
            nxt_thread_log_error(NXT_LOG_ERR, "numbers of captures in byte "
                           "and UTF-8 versions of RegExp \"%s\" vary: %d vs %d",
                           &pattern->source[1], pattern->ncaptures, ret);

            njs_regexp_pattern_free(pattern);
            return NULL;
        }

    } else if (ret != NXT_DECLINED) {
        njs_regexp_pattern_free(pattern);
        return NULL;
    }

    *end = '/';

    pattern->next = vm->pattern;
    vm->pattern = pattern;

    return pattern;
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


njs_regexp_t *
njs_regexp_alloc(njs_vm_t *vm, njs_regexp_pattern_t *pattern)
{
    njs_regexp_t  *regexp;

    regexp = nxt_mem_cache_alloc(vm->mem_cache_pool, sizeof(njs_regexp_t));

    if (nxt_fast_path(regexp != NULL)) {
        nxt_lvlhsh_init(&regexp->object.hash);
        nxt_lvlhsh_init(&regexp->object.shared_hash);
        regexp->object.__proto__ = &vm->prototypes[NJS_PROTOTYPE_REGEXP];
        regexp->object.shared = 0;
        regexp->last_index = 0;
        regexp->pattern = pattern;
    }

    return regexp;
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
njs_regexp_prototype_global(njs_vm_t *vm, njs_value_t *value)
{
    njs_regexp_pattern_t  *pattern;

    pattern = value->data.u.regexp->pattern;
    vm->retval = pattern->global ? njs_value_true : njs_value_false;
    njs_release(vm, value);

    return NXT_OK;
}


static njs_ret_t
njs_regexp_prototype_ignore_case(njs_vm_t *vm, njs_value_t *value)
{
    njs_regexp_pattern_t  *pattern;

    pattern = value->data.u.regexp->pattern;
    vm->retval = pattern->ignore_case ? njs_value_true : njs_value_false;
    njs_release(vm, value);

    return NXT_OK;
}


static njs_ret_t
njs_regexp_prototype_multiline(njs_vm_t *vm, njs_value_t *value)
{
    njs_regexp_pattern_t  *pattern;

    pattern = value->data.u.regexp->pattern;
    vm->retval = pattern->multiline ? njs_value_true : njs_value_false;
    njs_release(vm, value);

    return NXT_OK;
}


static njs_ret_t
njs_regexp_prototype_source(njs_vm_t *vm, njs_value_t *value)
{
    u_char                *source;
    size_t                length, size;
    njs_regexp_pattern_t  *pattern;

    pattern = value->data.u.regexp->pattern;
    /* Skip starting "/". */
    source = pattern->source + 1;

    size = strlen((char *) source) - pattern->flags;
    length = nxt_utf8_length(source, size);

    return njs_regexp_string_create(vm, &vm->retval, source, size, length);
}


static njs_ret_t
njs_regexp_prototype_to_string(njs_vm_t *vm, njs_value_t *args,
    nxt_uint_t nargs, njs_index_t unused)
{
    u_char                *source;
    size_t                length, size;
    njs_regexp_pattern_t  *pattern;

    pattern = args[0].data.u.regexp->pattern;
    source = pattern->source;

    size = strlen((char *) source);
    length = nxt_utf8_length(source, size);

    return njs_regexp_string_create(vm, &vm->retval, source, size, length);
}


static njs_ret_t
njs_regexp_prototype_test(njs_vm_t *vm, njs_value_t *args, nxt_uint_t nargs,
    njs_index_t unused)
{
    njs_ret_t             ret;
    nxt_uint_t            n;
    njs_value_t           *value;
    const njs_value_t     *retval;
    njs_string_prop_t     string;
    njs_regexp_pattern_t  *pattern;

    if (!njs_is_regexp(&args[0])) {
        vm->exception = &njs_exception_type_error;
        return NXT_ERROR;
    }

    retval = &njs_value_false;

    if (nargs > 1) {
        value = &args[1];

    } else {
        value = (njs_value_t *) &njs_string_void;
    }

    (void) njs_string_prop(&string, value);

    n = (string.length != 0 && string.length != string.size);

    pattern = args[0].data.u.regexp->pattern;

    if (pattern->code[n] != NULL) {
        ret = pcre_exec(pattern->code[n], pattern->extra[n],
                        (char *) string.start, string.size, 0, 0, NULL, 0);

        if (ret >= 0) {
            retval = &njs_value_true;

        } else if (ret != PCRE_ERROR_NOMATCH) {
            vm->exception = &njs_exception_internal_error;
            return NXT_ERROR;
        }
    }

    vm->retval = *retval;

    return NXT_OK;
}


njs_ret_t
njs_regexp_prototype_exec(njs_vm_t *vm, njs_value_t *args, nxt_uint_t nargs,
    njs_index_t unused)
{
    int                   *captures, ncaptures;
    njs_ret_t             ret;
    nxt_uint_t            n, utf8;
    njs_value_t           *value;
    njs_regexp_t          *regexp;
    njs_string_prop_t     string;
    njs_regexp_pattern_t  *pattern;

    if (!njs_is_regexp(&args[0])) {
        vm->exception = &njs_exception_type_error;
        return NXT_ERROR;
    }

    if (nargs > 1) {
        value = &args[1];

    } else {
        value = (njs_value_t *) &njs_string_void;
    }

    regexp = args[0].data.u.regexp;
    regexp->string = *value;

    (void) njs_string_prop(&string, value);

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
            vm->exception = &njs_exception_internal_error;
            return NXT_ERROR;
        }
    }

    regexp->last_index = 0;
    vm->retval = njs_value_null;

    return NXT_OK;
}


static njs_ret_t
njs_regexp_exec_result(njs_vm_t *vm, njs_regexp_t *regexp, u_char *string,
    int *captures, nxt_uint_t utf8)
{
    u_char              *start;
    int32_t             size, length;
    njs_ret_t           ret;
    nxt_uint_t          i, n;
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

            ret = njs_regexp_string_create(vm, &array->start[i],
                                           start, size, length);
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


static njs_ret_t
njs_regexp_string_create(njs_vm_t *vm, njs_value_t *value, u_char *start,
    uint32_t size, int32_t length)
{
    if (nxt_fast_path(length >= 0)) {
        return njs_string_create(vm, value, start, size, length);
    }

    vm->exception = &njs_exception_internal_error;
    return NXT_ERROR;
}


static const njs_object_prop_t  njs_regexp_constructor_properties[] =
{
    /* RegExp.name == "RegExp". */
    {
        .type = NJS_PROPERTY,
        .name = njs_string("name"),
        .value = njs_string("RegExp"),
    },

    /* RegExp.length == 2. */
    {
        .type = NJS_PROPERTY,
        .name = njs_string("length"),
        .value = njs_value(NJS_NUMBER, 1, 2.0),
    },

    /* RegExp.prototype. */
    {
        .type = NJS_NATIVE_GETTER,
        .name = njs_string("prototype"),
        .value = njs_native_getter(njs_object_prototype_create),
    },
};


const njs_object_init_t  njs_regexp_constructor_init = {
    njs_regexp_constructor_properties,
    nxt_nitems(njs_regexp_constructor_properties),
};


static const njs_object_prop_t  njs_regexp_prototype_properties[] =
{
    {
        .type = NJS_NATIVE_GETTER,
        .name = njs_string("lastIndex"),
        .value = njs_native_getter(njs_regexp_prototype_last_index),
    },

    {
        .type = NJS_NATIVE_GETTER,
        .name = njs_string("global"),
        .value = njs_native_getter(njs_regexp_prototype_global),
    },

    {
        .type = NJS_NATIVE_GETTER,
        .name = njs_string("ignoreCase"),
        .value = njs_native_getter(njs_regexp_prototype_ignore_case),
    },

    {
        .type = NJS_NATIVE_GETTER,
        .name = njs_string("multiline"),
        .value = njs_native_getter(njs_regexp_prototype_multiline),
    },

    {
        .type = NJS_NATIVE_GETTER,
        .name = njs_string("source"),
        .value = njs_native_getter(njs_regexp_prototype_source),
    },

    {
        .type = NJS_METHOD,
        .name = njs_string("toString"),
        .value = njs_native_function(njs_regexp_prototype_to_string, 0, 0),
    },

    {
        .type = NJS_METHOD,
        .name = njs_string("test"),
        .value = njs_native_function(njs_regexp_prototype_test, 0,
                     NJS_OBJECT_ARG, NJS_STRING_ARG),
    },

    {
        .type = NJS_METHOD,
        .name = njs_string("exec"),
        .value = njs_native_function(njs_regexp_prototype_exec, 0,
                     NJS_OBJECT_ARG, NJS_STRING_ARG),
    },
};


const njs_object_init_t  njs_regexp_prototype_init = {
    njs_regexp_prototype_properties,
    nxt_nitems(njs_regexp_prototype_properties),
};


void
njs_regexp_pattern_free(njs_regexp_pattern_t *pattern)
{
    /*
     * pcre_free() is enough to free PCRE extra study data.
     * pcre_free_study() is required for JIT optimization, pcreapi(3):
     *
     *   When you are finished with a pattern,  you can free  the memory used
     *   for the study data by calling  pcre_free_study().  This function was
     *   added to the API for release 8.20.  For earlier versions, the memory
     *   could be freed with pcre_free(), just like the pattern itself.  This
     *   will still work in cases where JIT optimization is not used,  but it
     *   is advisable to change to the new function when convenient.
     */
    while (pattern != NULL) {
        pcre_free(pattern->extra[0]);
        pcre_free(pattern->code[0]);

        pcre_free(pattern->extra[1]);
        pcre_free(pattern->code[1]);

        pattern = pattern->next;
    }
}
