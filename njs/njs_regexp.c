
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
#include <nxt_utf8.h>
#include <nxt_djb_hash.h>
#include <nxt_array.h>
#include <nxt_lvlhsh.h>
#include <nxt_random.h>
#include <nxt_pcre.h>
#include <nxt_malloc.h>
#include <nxt_mem_cache_pool.h>
#include <njscript.h>
#include <njs_vm.h>
#include <njs_string.h>
#include <njs_object.h>
#include <njs_object_hash.h>
#include <njs_array.h>
#include <njs_function.h>
#include <njs_error.h>
#include <njs_variable.h>
#include <njs_parser.h>
#include <njs_regexp.h>
#include <njs_regexp_pattern.h>
#include <string.h>
#include <stdio.h>


static void *njs_regexp_malloc(size_t size, void *memory_data);
static void njs_regexp_free(void *p, void *memory_data);
static njs_regexp_flags_t njs_regexp_flags(u_char **start, u_char *end,
    nxt_bool_t bound);
static int njs_regexp_pattern_compile(njs_vm_t *vm, nxt_regex_t *regex,
    u_char *source, int options);
static u_char *njs_regexp_compile_trace_handler(nxt_trace_t *trace,
    nxt_trace_data_t *td, u_char *start);
static u_char *njs_regexp_match_trace_handler(nxt_trace_t *trace,
    nxt_trace_data_t *td, u_char *start);
static njs_ret_t njs_regexp_exec_result(njs_vm_t *vm, njs_regexp_t *regexp,
    njs_utf8_t utf8, u_char *string, nxt_regex_match_data_t *match_data);
static njs_ret_t njs_regexp_string_create(njs_vm_t *vm, njs_value_t *value,
    u_char *start, uint32_t size, int32_t length);


njs_ret_t
njs_regexp_init(njs_vm_t *vm)
{
    vm->regex_context = nxt_regex_context_create(njs_regexp_malloc,
                                          njs_regexp_free, vm->mem_cache_pool);
    if (nxt_slow_path(vm->regex_context == NULL)) {
        return NXT_ERROR;
    }

    vm->single_match_data = nxt_regex_match_data(NULL, vm->regex_context);
    if (nxt_slow_path(vm->single_match_data == NULL)) {
        return NXT_ERROR;
    }

    vm->regex_context->trace = &vm->trace;

    return NXT_OK;
}


static void *
njs_regexp_malloc(size_t size, void *memory_data)
{
    return nxt_mem_cache_alloc(memory_data, size);
}


static void
njs_regexp_free(void *p, void *memory_data)
{
    nxt_mem_cache_free(memory_data, p);
}


njs_ret_t
njs_regexp_constructor(njs_vm_t *vm, njs_value_t *args, nxt_uint_t nargs,
    njs_index_t unused)
{
    nxt_str_t           string;
    njs_regexp_flags_t  flags;

    flags = 0;

    switch (nargs) {

    case 1:
        string.start = NULL;
        string.length = 0;
        break;

    default:
        njs_string_get(&args[2], &string);

        flags = njs_regexp_flags(&string.start, string.start + string.length,
                                 1);
        if (nxt_slow_path(flags < 0)) {
            return NXT_ERROR;
        }

        /* Fall through. */

    case 2:
        njs_string_get(&args[1], &string);
        break;
    }

    return njs_regexp_create(vm, &vm->retval, string.start, string.length,
                             flags);
}


nxt_int_t
njs_regexp_create(njs_vm_t *vm, njs_value_t *value, u_char *start,
    size_t length, njs_regexp_flags_t flags)
{
    njs_regexp_t          *regexp;
    njs_regexp_pattern_t  *pattern;

    if (length != 0) {
        pattern = njs_regexp_pattern_create(vm, start, length, flags);
        if (nxt_slow_path(pattern == NULL)) {
            return NXT_ERROR;
        }

    } else {
        pattern = vm->shared->empty_regexp_pattern;
    }

    regexp = njs_regexp_alloc(vm, pattern);

    if (nxt_fast_path(regexp != NULL)) {
        value->data.u.regexp = regexp;
        value->type = NJS_REGEXP;
        value->data.truth = 1;

        return NXT_OK;
    }

    return NXT_ERROR;
}


njs_token_t
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
            lexer->text.start = lexer->start;
            lexer->text.length = p - lexer->text.start;
            p++;
            lexer->start = p;

            flags = njs_regexp_flags(&p, lexer->end, 0);

            if (nxt_slow_path(flags < 0)) {
                njs_parser_syntax_error(vm, parser,
                                        "Invalid RegExp flags \"%.*s\"",
                                        p - lexer->start, lexer->start);

                return NJS_TOKEN_ILLEGAL;
            }

            lexer->start = p;

            pattern = njs_regexp_pattern_create(vm, lexer->text.start,
                                                lexer->text.length, flags);
            if (nxt_slow_path(pattern == NULL)) {
                return NJS_TOKEN_ILLEGAL;
            }

            value->data.u.data = pattern;

            return NJS_TOKEN_REGEXP;
        }
    }

    njs_parser_syntax_error(vm, parser, "Unterminated RegExp \"%.*s\"",
                            p - lexer->start - 1, lexer->start - 1);

    return NJS_TOKEN_ILLEGAL;
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

        case ';':
        case ' ':
        case '\t':
        case '\r':
        case '\n':
        case ',':
        case ')':
        case ']':
        case '}':
        case '.':
            if (!bound) {
                goto done;
            }

            /* Fall through. */

        default:
            goto invalid;
        }

        if (nxt_slow_path((flags & flag) != 0)) {
            goto invalid;
        }

        flags |= flag;
    }

done:

    *start = p;

    return flags;

invalid:

    *start = p + 1;

    return NJS_REGEXP_INVALID_FLAG;
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

    pattern = nxt_mem_cache_zalloc(vm->mem_cache_pool,
                                   sizeof(njs_regexp_pattern_t)
                                   + 1 + length + size + 1);
    if (nxt_slow_path(pattern == NULL)) {
        return NULL;
    }

    pattern->flags = size;

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

    ret = njs_regexp_pattern_compile(vm, &pattern->regex[0],
                                     &pattern->source[1], options);
    if (nxt_slow_path(ret < 0)) {
        return NULL;
    }

    pattern->ncaptures = ret;

    ret = njs_regexp_pattern_compile(vm, &pattern->regex[1],
                                     &pattern->source[1], options | PCRE_UTF8);
    if (nxt_fast_path(ret >= 0)) {

        if (nxt_slow_path((u_int) ret != pattern->ncaptures)) {
            njs_exception_internal_error(vm, NULL, NULL);
            nxt_mem_cache_free(vm->mem_cache_pool, pattern);
            return NULL;
        }

    } else if (ret != NXT_DECLINED) {
        nxt_mem_cache_free(vm->mem_cache_pool, pattern);
        return NULL;
    }

    *end = '/';

    return pattern;
}


static int
njs_regexp_pattern_compile(njs_vm_t *vm, nxt_regex_t *regex, u_char *source,
    int options)
{
    nxt_int_t            ret;
    nxt_trace_handler_t  handler;

    handler = vm->trace.handler;
    vm->trace.handler = njs_regexp_compile_trace_handler;

    /* Zero length means a zero-terminated string. */
    ret = nxt_regex_compile(regex, source, 0, options, vm->regex_context);

    vm->trace.handler = handler;

    if (nxt_fast_path(ret == NXT_OK)) {
        return regex->ncaptures;
    }

    return ret;
}


static u_char *
njs_regexp_compile_trace_handler(nxt_trace_t *trace, nxt_trace_data_t *td,
    u_char *start)
{
    u_char    *p;
    size_t    size;
    njs_vm_t  *vm;

    size = sizeof("SyntaxError: ") - 1;
    memcpy(start, "SyntaxError: ", size);
    p = start + size;

    vm = trace->data;

    trace = trace->next;
    p = trace->handler(trace, td, start);

    if (vm->parser != NULL) {
        njs_exception_syntax_error(vm, "%s in %u", start,
                                   vm->parser->lexer->line);

    } else {
        njs_exception_syntax_error(vm, "%s", start);
    }

    return p;
}


nxt_int_t
njs_regexp_match(njs_vm_t *vm, nxt_regex_t *regex, u_char *subject, size_t len,
    nxt_regex_match_data_t *match_data)
{
    nxt_int_t            ret;
    nxt_trace_handler_t  handler;

    handler = vm->trace.handler;
    vm->trace.handler = njs_regexp_match_trace_handler;

    ret = nxt_regex_match(regex, subject, len, match_data, vm->regex_context);

    vm->trace.handler = handler;

    return ret;
}


static u_char *
njs_regexp_match_trace_handler(nxt_trace_t *trace, nxt_trace_data_t *td,
    u_char *start)
{
    u_char    *p;
    size_t    size;
    njs_vm_t  *vm;

    size = sizeof("InternalError: ") - 1;
    memcpy(start, "InternalError: ", size);
    p = start + size;

    vm = trace->data;

    trace = trace->next;
    p = trace->handler(trace, td, p);

    njs_exception_internal_error(vm, (const char *) start, NULL);

    return p;
}


njs_regexp_t *
njs_regexp_alloc(njs_vm_t *vm, njs_regexp_pattern_t *pattern)
{
    njs_regexp_t  *regexp;

    regexp = nxt_mem_cache_alloc(vm->mem_cache_pool, sizeof(njs_regexp_t));

    if (nxt_fast_path(regexp != NULL)) {
        nxt_lvlhsh_init(&regexp->object.hash);
        nxt_lvlhsh_init(&regexp->object.shared_hash);
        regexp->object.__proto__ = &vm->prototypes[NJS_PROTOTYPE_REGEXP].object;
        regexp->object.type = NJS_REGEXP;
        regexp->object.shared = 0;
        regexp->object.extensible = 1;
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
    int32_t               length;
    uint32_t              size;
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
    int32_t               length;
    uint32_t              size;
    njs_value_t           *value;
    njs_regexp_pattern_t  *pattern;

    value = &args[0];

    if (njs_is_regexp(value)) {
        pattern = value->data.u.regexp->pattern;
        source = pattern->source;

        size = strlen((char *) source);
        length = nxt_utf8_length(source, size);

        return njs_regexp_string_create(vm, &vm->retval, source, size, length);
    }

    njs_exception_type_error(vm, NULL, NULL);

    return NXT_ERROR;
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
        njs_exception_type_error(vm, NULL, NULL);
        return NXT_ERROR;
    }

    retval = &njs_value_false;

    if (nargs > 1) {
        value = &args[1];

    } else {
        value = (njs_value_t *) &njs_string_void;
    }

    (void) njs_string_prop(&string, value);

    n = (string.length != 0);

    pattern = args[0].data.u.regexp->pattern;

    if (nxt_regex_is_valid(&pattern->regex[n])) {
        ret = njs_regexp_match(vm, &pattern->regex[n], string.start,
                               string.size, vm->single_match_data);
        if (ret >= 0) {
            retval = &njs_value_true;

        } else if (ret != NXT_REGEX_NOMATCH) {
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
    njs_ret_t               ret;
    njs_utf8_t              utf8;
    njs_value_t             *value;
    njs_regexp_t            *regexp;
    njs_string_prop_t       string;
    njs_regexp_utf8_t       type;
    njs_regexp_pattern_t    *pattern;
    nxt_regex_match_data_t  *match_data;

    if (!njs_is_regexp(&args[0])) {
        njs_exception_type_error(vm, NULL, NULL);
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

    utf8 = NJS_STRING_BYTE;
    type = NJS_REGEXP_BYTE;

    if (string.length != 0) {
        utf8 = NJS_STRING_ASCII;
        type = NJS_REGEXP_UTF8;

        if (string.length != string.size) {
            utf8 = NJS_STRING_UTF8;
        }
    }

    pattern = regexp->pattern;

    if (nxt_regex_is_valid(&pattern->regex[type])) {
        string.start += regexp->last_index;
        string.size -= regexp->last_index;

        match_data = nxt_regex_match_data(&pattern->regex[type],
                                          vm->regex_context);
        if (nxt_slow_path(match_data == NULL)) {
            return NXT_ERROR;
        }

        ret = njs_regexp_match(vm, &pattern->regex[type], string.start,
                               string.size, match_data);
        if (ret >= 0) {
            return njs_regexp_exec_result(vm, regexp, utf8, string.start,
                                          match_data);
        }

        if (nxt_slow_path(ret != NXT_REGEX_NOMATCH)) {
            nxt_regex_match_data_free(match_data, vm->regex_context);

            return NXT_ERROR;
        }
    }

    regexp->last_index = 0;
    vm->retval = njs_value_null;

    return NXT_OK;
}


static njs_ret_t
njs_regexp_exec_result(njs_vm_t *vm, njs_regexp_t *regexp, njs_utf8_t utf8,
    u_char *string, nxt_regex_match_data_t *match_data)
{
    int                 *captures;
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
        goto fail;
    }

    captures = nxt_regex_captures(match_data);

    for (i = 0; i < regexp->pattern->ncaptures; i++) {
        n = 2 * i;

        if (captures[n] != -1) {
            start = &string[captures[n]];
            size = captures[n + 1] - captures[n];

            length = njs_string_length(utf8, start, size);

            ret = njs_regexp_string_create(vm, &array->start[i], start, size,
                                           length);
            if (nxt_slow_path(ret != NXT_OK)) {
                goto fail;
            }

        } else {
            array->start[i] = njs_value_void;
        }
    }

    prop = njs_object_prop_alloc(vm, &njs_string_index, &njs_value_void, 1);
    if (nxt_slow_path(prop == NULL)) {
        goto fail;
    }

    /* TODO: Non UTF-8 position */

    njs_number_set(&prop->value, regexp->last_index + captures[0]);

    if (regexp->pattern->global) {
        regexp->last_index += captures[1];
    }

    lhq.key_hash = NJS_INDEX_HASH;
    lhq.key = nxt_string_value("index");
    lhq.replace = 0;
    lhq.value = prop;
    lhq.pool = vm->mem_cache_pool;
    lhq.proto = &njs_object_hash_proto;

    ret = nxt_lvlhsh_insert(&array->object.hash, &lhq);
    if (nxt_slow_path(ret != NXT_OK)) {
        goto fail;
    }

    prop = njs_object_prop_alloc(vm, &njs_string_input, &regexp->string, 1);
    if (nxt_slow_path(prop == NULL)) {
        goto fail;
    }

    lhq.key_hash = NJS_INPUT_HASH;
    lhq.key = nxt_string_value("input");
    lhq.value = prop;

    ret = nxt_lvlhsh_insert(&array->object.hash, &lhq);

    if (nxt_fast_path(ret == NXT_OK)) {
        vm->retval.data.u.array = array;
        vm->retval.type = NJS_ARRAY;
        vm->retval.data.truth = 1;

        ret = NXT_OK;
        goto done;
    }

fail:

    ret = NXT_ERROR;

done:

    nxt_regex_match_data_free(match_data, vm->regex_context);

    return ret;
}


static njs_ret_t
njs_regexp_string_create(njs_vm_t *vm, njs_value_t *value, u_char *start,
    uint32_t size, int32_t length)
{
    length = (length >= 0) ? length : 0;

    return njs_string_create(vm, value, start, size, length);
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
    nxt_string("RegExp"),
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
    nxt_string("RegExp"),
    njs_regexp_prototype_properties,
    nxt_nitems(njs_regexp_prototype_properties),
};
