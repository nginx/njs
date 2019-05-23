
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) NGINX, Inc.
 */

#include <njs_core.h>
#include <njs_regexp.h>
#include <njs_regexp_pattern.h>

#include <string.h>


struct njs_regexp_group_s {
    nxt_str_t  name;
    uint32_t   hash;
    uint32_t   capture;
};


static void *njs_regexp_malloc(size_t size, void *memory_data);
static void njs_regexp_free(void *p, void *memory_data);
static njs_regexp_flags_t njs_regexp_flags(u_char **start, u_char *end,
    nxt_bool_t bound);
static njs_ret_t njs_regexp_prototype_source(njs_vm_t *vm, njs_value_t *value,
    njs_value_t *setval, njs_value_t *retval);
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
                                          njs_regexp_free, vm->mem_pool);
    if (nxt_slow_path(vm->regex_context == NULL)) {
        njs_memory_error(vm);
        return NXT_ERROR;
    }

    vm->single_match_data = nxt_regex_match_data(NULL, vm->regex_context);
    if (nxt_slow_path(vm->single_match_data == NULL)) {
        njs_memory_error(vm);
        return NXT_ERROR;
    }

    vm->regex_context->trace = &vm->trace;

    return NXT_OK;
}


static void *
njs_regexp_malloc(size_t size, void *memory_data)
{
    return nxt_mp_alloc(memory_data, size);
}


static void
njs_regexp_free(void *p, void *memory_data)
{
    nxt_mp_free(memory_data, p);
}


static njs_regexp_flags_t
njs_regexp_value_flags(njs_vm_t *vm, const njs_value_t *regexp)
{
    njs_regexp_flags_t    flags;
    njs_regexp_pattern_t  *pattern;

    flags = 0;

    pattern = regexp->data.u.regexp->pattern;

    if (pattern->global) {
        flags |= NJS_REGEXP_GLOBAL;
    }

    if (pattern->ignore_case) {
        flags |= NJS_REGEXP_IGNORE_CASE;
    }

    if (pattern->multiline) {
        flags |= NJS_REGEXP_MULTILINE;
    }

    return flags;
}


njs_ret_t
njs_regexp_constructor(njs_vm_t *vm, njs_value_t *args, nxt_uint_t nargs,
    njs_index_t unused)
{
    u_char              *start;
    njs_ret_t           ret;
    nxt_str_t           string;
    njs_value_t         source, flags_string;
    const njs_value_t   *pattern, *flags;
    njs_regexp_flags_t  re_flags;

    pattern = njs_arg(args, nargs, 1);

    if (!njs_is_regexp(pattern) && !njs_is_primitive(pattern)) {
        njs_vm_trap_value(vm, &args[1]);

        return njs_trap(vm, NJS_TRAP_STRING_ARG);
    }

    flags = njs_arg(args, nargs, 2);

    if (!njs_is_primitive(flags)) {
        njs_vm_trap_value(vm, &args[2]);

        return njs_trap(vm, NJS_TRAP_STRING_ARG);
    }

    re_flags = 0;

    if (njs_is_regexp(pattern)) {
        ret = njs_regexp_prototype_source(vm, (njs_value_t *) pattern, NULL,
                                          &source);
        if (nxt_slow_path(ret != NXT_OK)) {
            return ret;
        }

        re_flags = njs_regexp_value_flags(vm, pattern);

        pattern = &source;

    } else {
        if (njs_is_undefined(pattern)) {
            pattern = &njs_string_empty;
        }

        ret = njs_primitive_value_to_string(vm, &source, pattern);
        if (nxt_slow_path(ret != NXT_OK)) {
            return ret;
        }

        pattern = &source;
    }

    if (!njs_is_undefined(flags)) {
        ret = njs_primitive_value_to_string(vm, &flags_string, flags);
        if (nxt_slow_path(ret != NXT_OK)) {
            return ret;
        }

        njs_string_get(&flags_string, &string);

        start = string.start;

        re_flags = njs_regexp_flags(&start, start + string.length, 1);
        if (nxt_slow_path(re_flags < 0)) {
            njs_syntax_error(vm, "Invalid RegExp flags \"%V\"", &string);
            return NXT_ERROR;
        }
    }

    njs_string_get(pattern, &string);

    return njs_regexp_create(vm, &vm->retval, string.start, string.length,
                             re_flags);
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


nxt_inline njs_ret_t
njs_regexp_escape_bracket(njs_vm_t *vm, nxt_str_t *text, size_t count)
{
    size_t  length, diff;
    u_char  *p, *dst, *start, *end;

    length = text->length + count;

    dst = nxt_mp_alloc(vm->mem_pool, length);
    if (nxt_slow_path(dst == NULL)) {
        njs_memory_error(vm);
        return NJS_ERROR;
    }

    start = text->start;
    end = text->start + text->length;

    for (p = start; p < end; p++) {

        switch (*p) {
        case '[':
            while (++p < end && *p != ']') {
                if (*p == '\\') {
                    p++;
                }
            }

            break;

        case ']':
            diff = p - start;
            dst = nxt_cpymem(dst, start, diff);
            dst = nxt_cpymem(dst, "\\]", 2);

            start = p + 1;
            break;

        case '\\':
            p++;
            break;
        }
    }

    diff = p - start;
    memcpy(dst, start, diff);

    text->start = dst - (length - diff);
    text->length = length;

    return NJS_OK;
}


njs_token_t
njs_regexp_literal(njs_vm_t *vm, njs_parser_t *parser, njs_value_t *value)
{
    u_char                *p;
    size_t                closing_brackets;
    nxt_str_t             text;
    njs_ret_t             ret;
    njs_lexer_t           *lexer;
    njs_regexp_flags_t    flags;
    njs_regexp_pattern_t  *pattern;

    closing_brackets = 0;
    lexer = parser->lexer;

    for (p = lexer->start; p < lexer->end; p++) {

        switch (*p) {
        case '\n':
        case '\r':
            goto failed;

        case '[':
            while (++p < lexer->end && *p != ']') {
                switch (*p) {
                case '\n':
                case '\r':
                    goto failed;

                case '\\':
                    if (++p < lexer->end && (*p == '\n' || *p == '\r')) {
                        goto failed;
                    }

                    break;
                }
            }

            break;

        case ']':
            closing_brackets++;
            break;

        case '\\':
            if (++p < lexer->end && (*p == '\n' || *p == '\r')) {
                goto failed;
            }

            break;

        case '/':
            text.start = lexer->start;
            text.length = p - text.start;
            p++;
            lexer->start = p;

            flags = njs_regexp_flags(&p, lexer->end, 0);

            if (nxt_slow_path(flags < 0)) {
                njs_parser_syntax_error(vm, parser,
                                        "Invalid RegExp flags \"%*s\"",
                                        p - lexer->start, lexer->start);

                return NJS_TOKEN_ILLEGAL;
            }

            lexer->start = p;

            if (closing_brackets != 0) {
                /*
                 * PCRE with PCRE_JAVASCRIPT_COMPAT flag rejects regexps with
                 * lone closing square brackets as invalid.  Whereas according
                 * to ES6: 11.8.5 it is a valid regexp expression.
                 *
                 * Escaping it here as a workaround.
                 */

                ret = njs_regexp_escape_bracket(vm, &text, closing_brackets);
                if (nxt_slow_path(ret != NXT_OK)) {
                    return NJS_TOKEN_ILLEGAL;
                }
            }

            pattern = njs_regexp_pattern_create(vm, text.start, text.length,
                                                flags);

            if (closing_brackets != 0) {
                nxt_mp_free(vm->mem_pool, text.start);
            }

            if (nxt_slow_path(pattern == NULL)) {
                return NJS_TOKEN_ILLEGAL;
            }

            value->data.u.data = pattern;

            return NJS_TOKEN_REGEXP;
        }
    }

failed:

    njs_parser_syntax_error(vm, parser, "Unterminated RegExp \"%*s\"",
                            p - (lexer->start - 1), lexer->start - 1);

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
    nxt_uint_t            n;
    nxt_regex_t           *regex;
    njs_regexp_group_t    *group;
    njs_regexp_pattern_t  *pattern;

    size = 1;  /* A trailing "/". */
    size += ((flags & NJS_REGEXP_GLOBAL) != 0);
    size += ((flags & NJS_REGEXP_IGNORE_CASE) != 0);
    size += ((flags & NJS_REGEXP_MULTILINE) != 0);

    pattern = nxt_mp_zalloc(vm->mem_pool, sizeof(njs_regexp_pattern_t) + 1
                                          + length + size + 1);
    if (nxt_slow_path(pattern == NULL)) {
        njs_memory_error(vm);
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

    if (nxt_fast_path(ret >= 0)) {
        pattern->ncaptures = ret;

    } else if (ret < 0 && ret != NXT_DECLINED) {
        goto fail;
    }

    ret = njs_regexp_pattern_compile(vm, &pattern->regex[1],
                                     &pattern->source[1], options | PCRE_UTF8);
    if (nxt_fast_path(ret >= 0)) {

        if (nxt_slow_path(nxt_regex_is_valid(&pattern->regex[0])
                          && (u_int) ret != pattern->ncaptures))
        {
            njs_internal_error(vm, "regexp pattern compile failed");
            goto fail;
        }

        pattern->ncaptures = ret;

    } else if (ret != NXT_DECLINED) {
        goto fail;
    }

    if (nxt_regex_is_valid(&pattern->regex[0])) {
        regex = &pattern->regex[0];

    } else if (nxt_regex_is_valid(&pattern->regex[1])) {
        regex = &pattern->regex[1];

    } else {
        goto fail;
    }

    *end = '/';

    pattern->ngroups = nxt_regex_named_captures(regex, NULL, 0);

    if (pattern->ngroups != 0) {
        size = sizeof(njs_regexp_group_t) * pattern->ngroups;

        pattern->groups = nxt_mp_alloc(vm->mem_pool, size);
        if (nxt_slow_path(pattern->groups == NULL)) {
            njs_memory_error(vm);
            return NULL;
        }

        n = 0;

        do {
            group = &pattern->groups[n];

            group->capture = nxt_regex_named_captures(regex, &group->name, n);
            group->hash = nxt_djb_hash(group->name.start, group->name.length);

            n++;

        } while (n != pattern->ngroups);
    }

    return pattern;

fail:

    nxt_mp_free(vm->mem_pool, pattern);
    return NULL;
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
    njs_vm_t  *vm;

    vm = trace->data;

    trace = trace->next;
    p = trace->handler(trace, td, start);

    if (vm->parser != NULL && vm->parser->lexer != NULL) {
        njs_syntax_error(vm, "%*s in %uD", p - start, start,
                         vm->parser->lexer->line);

    } else {
        njs_syntax_error(vm, "%*s", p - start, start);
    }

    return p;
}


nxt_int_t
njs_regexp_match(njs_vm_t *vm, nxt_regex_t *regex, const u_char *subject,
    size_t len, nxt_regex_match_data_t *match_data)
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
    njs_vm_t  *vm;

    vm = trace->data;

    trace = trace->next;
    p = trace->handler(trace, td, start);

    njs_internal_error(vm, (const char *) start);

    return p;
}


njs_regexp_t *
njs_regexp_alloc(njs_vm_t *vm, njs_regexp_pattern_t *pattern)
{
    njs_regexp_t  *regexp;

    regexp = nxt_mp_alloc(vm->mem_pool, sizeof(njs_regexp_t));

    if (nxt_fast_path(regexp != NULL)) {
        nxt_lvlhsh_init(&regexp->object.hash);
        nxt_lvlhsh_init(&regexp->object.shared_hash);
        regexp->object.__proto__ = &vm->prototypes[NJS_PROTOTYPE_REGEXP].object;
        regexp->object.type = NJS_REGEXP;
        regexp->object.shared = 0;
        regexp->object.extensible = 1;
        regexp->last_index = 0;
        regexp->pattern = pattern;
        return regexp;
    }

    njs_memory_error(vm);

    return NULL;
}


static njs_ret_t
njs_regexp_prototype_last_index(njs_vm_t *vm, njs_value_t *value,
    njs_value_t *setval, njs_value_t *retval)
{
    uint32_t           index;
    njs_regexp_t       *regexp;
    njs_string_prop_t  string;

    njs_release(vm, value);

    regexp = value->data.u.regexp;

    (void) njs_string_prop(&string, &regexp->string);

    index = njs_string_index(&string, regexp->last_index);
    njs_value_number_set(retval, index);

    return NXT_OK;
}


static njs_ret_t
njs_regexp_prototype_global(njs_vm_t *vm, njs_value_t *value,
    njs_value_t *setval, njs_value_t *retval)
{
    njs_regexp_pattern_t  *pattern;

    pattern = value->data.u.regexp->pattern;
    *retval = pattern->global ? njs_value_true : njs_value_false;
    njs_release(vm, value);

    return NXT_OK;
}


static njs_ret_t
njs_regexp_prototype_ignore_case(njs_vm_t *vm, njs_value_t *value,
    njs_value_t *setval, njs_value_t *retval)
{
    njs_regexp_pattern_t  *pattern;

    pattern = value->data.u.regexp->pattern;
    *retval = pattern->ignore_case ? njs_value_true : njs_value_false;
    njs_release(vm, value);

    return NXT_OK;
}


static njs_ret_t
njs_regexp_prototype_multiline(njs_vm_t *vm, njs_value_t *value,
    njs_value_t *setval, njs_value_t *retval)
{
    njs_regexp_pattern_t  *pattern;

    pattern = value->data.u.regexp->pattern;
    *retval = pattern->multiline ? njs_value_true : njs_value_false;
    njs_release(vm, value);

    return NXT_OK;
}


static njs_ret_t
njs_regexp_prototype_source(njs_vm_t *vm, njs_value_t *value,
    njs_value_t *setval, njs_value_t *retval)
{
    u_char                *source;
    int32_t               length;
    uint32_t              size;
    njs_regexp_pattern_t  *pattern;

    pattern = value->data.u.regexp->pattern;
    /* Skip starting "/". */
    source = pattern->source + 1;

    size = nxt_strlen(source) - pattern->flags;
    length = nxt_utf8_length(source, size);

    return njs_regexp_string_create(vm, retval, source, size, length);
}


static njs_ret_t
njs_regexp_prototype_to_string(njs_vm_t *vm, njs_value_t *args,
    nxt_uint_t nargs, njs_index_t unused)
{
    if (njs_is_regexp(njs_arg(args, nargs, 0))) {
        return njs_regexp_to_string(vm, &vm->retval, &args[0]);
    }

    njs_type_error(vm, "\"this\" argument is not a regexp");

    return NXT_ERROR;
}


njs_ret_t
njs_regexp_to_string(njs_vm_t *vm, njs_value_t *retval,
    const njs_value_t *value)
{
    u_char                *source;
    int32_t               length;
    uint32_t              size;
    njs_regexp_pattern_t  *pattern;

    pattern = value->data.u.regexp->pattern;
    source = pattern->source;

    size = nxt_strlen(source);
    length = nxt_utf8_length(source, size);

    return njs_regexp_string_create(vm, retval, source, size, length);
}


static njs_ret_t
njs_regexp_prototype_test(njs_vm_t *vm, njs_value_t *args, nxt_uint_t nargs,
    njs_index_t unused)
{
    njs_ret_t             ret;
    nxt_uint_t            n;
    const njs_value_t     *value, *retval;
    njs_string_prop_t     string;
    njs_regexp_pattern_t  *pattern;

    if (!njs_is_regexp(njs_arg(args, nargs, 0))) {
        njs_type_error(vm, "\"this\" argument is not a regexp");
        return NXT_ERROR;
    }

    retval = &njs_value_false;

    value = njs_arg(args, nargs, 1);
    if (njs_is_undefined(value)) {
        value = &njs_string_undefined;
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
    njs_regexp_t            *regexp;
    njs_string_prop_t       string;
    njs_regexp_utf8_t       type;
    const njs_value_t       *value;
    njs_regexp_pattern_t    *pattern;
    nxt_regex_match_data_t  *match_data;

    if (!njs_is_regexp(njs_arg(args, nargs, 0))) {
        njs_type_error(vm, "\"this\" argument is not a regexp");
        return NXT_ERROR;
    }

    value = njs_arg(args, nargs, 1);
    if (njs_is_undefined(value)) {
        value = &njs_string_undefined;
    }

    regexp = args[0].data.u.regexp;
    regexp->string = *value;

    (void) njs_string_prop(&string, value);

    if (string.size >= regexp->last_index) {
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
                njs_memory_error(vm);
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
    njs_value_t         name;
    njs_object_t        *groups;
    njs_object_prop_t   *prop;
    njs_regexp_group_t  *group;
    nxt_lvlhsh_query_t  lhq;

    static const njs_value_t  string_index = njs_string("index");
    static const njs_value_t  string_input = njs_string("input");
    static const njs_value_t  string_groups = njs_string("groups");

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

            length = njs_string_calc_length(utf8, start, size);

            ret = njs_regexp_string_create(vm, &array->start[i], start, size,
                                           length);
            if (nxt_slow_path(ret != NXT_OK)) {
                goto fail;
            }

        } else {
            array->start[i] = njs_value_undefined;
        }
    }

    prop = njs_object_prop_alloc(vm, &string_index, &njs_value_undefined, 1);
    if (nxt_slow_path(prop == NULL)) {
        goto fail;
    }

    /* TODO: Non UTF-8 position */

    njs_value_number_set(&prop->value, regexp->last_index + captures[0]);

    if (regexp->pattern->global) {
        regexp->last_index += captures[1];
    }

    lhq.key_hash = NJS_INDEX_HASH;
    lhq.key = nxt_string_value("index");
    lhq.replace = 0;
    lhq.value = prop;
    lhq.pool = vm->mem_pool;
    lhq.proto = &njs_object_hash_proto;

    ret = nxt_lvlhsh_insert(&array->object.hash, &lhq);
    if (nxt_slow_path(ret != NXT_OK)) {
        goto insert_fail;
    }

    prop = njs_object_prop_alloc(vm, &string_input, &regexp->string, 1);
    if (nxt_slow_path(prop == NULL)) {
        goto fail;
    }

    lhq.key_hash = NJS_INPUT_HASH;
    lhq.key = nxt_string_value("input");
    lhq.value = prop;

    ret = nxt_lvlhsh_insert(&array->object.hash, &lhq);
    if (nxt_slow_path(ret != NXT_OK)) {
        goto insert_fail;
    }

    prop = njs_object_prop_alloc(vm, &string_groups, &njs_value_undefined, 1);
    if (nxt_slow_path(prop == NULL)) {
        goto fail;
    }

    lhq.key_hash = NJS_GROUPS_HASH;
    lhq.key = nxt_string_value("groups");
    lhq.value = prop;

    ret = nxt_lvlhsh_insert(&array->object.hash, &lhq);
    if (nxt_slow_path(ret != NXT_OK)) {
        goto insert_fail;
    }

    if (regexp->pattern->ngroups != 0) {
        groups = njs_object_alloc(vm);
        if (nxt_slow_path(groups == NULL)) {
            goto fail;
        }

        prop->value.data.u.object = groups;
        prop->value.type = NJS_OBJECT;
        prop->value.data.truth = 1;

        i = 0;

        do {
            group = &regexp->pattern->groups[i];

            ret = njs_string_set(vm, &name, group->name.start,
                                 group->name.length);
            if (nxt_slow_path(ret != NXT_OK)) {
                goto fail;
            }

            prop = njs_object_prop_alloc(vm, &name,
                                         &array->start[group->capture], 1);
            if (nxt_slow_path(prop == NULL)) {
                goto fail;
            }

            lhq.key_hash = group->hash;
            lhq.key = group->name;
            lhq.value = prop;

            ret = nxt_lvlhsh_insert(&groups->hash, &lhq);
            if (nxt_slow_path(ret != NXT_OK)) {
                goto insert_fail;
            }

            i++;

        } while (i < regexp->pattern->ngroups);
    }

    vm->retval.data.u.array = array;
    vm->retval.type = NJS_ARRAY;
    vm->retval.data.truth = 1;

    ret = NXT_OK;
    goto done;

insert_fail:

    njs_internal_error(vm, "lvlhsh insert failed");

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

    return njs_string_new(vm, value, start, size, length);
}


static const njs_object_prop_t  njs_regexp_constructor_properties[] =
{
    /* RegExp.name == "RegExp". */
    {
        .type = NJS_PROPERTY,
        .name = njs_string("name"),
        .value = njs_string("RegExp"),
        .configurable = 1,
    },

    /* RegExp.length == 2. */
    {
        .type = NJS_PROPERTY,
        .name = njs_string("length"),
        .value = njs_value(NJS_NUMBER, 1, 2.0),
        .configurable = 1,
    },

    /* RegExp.prototype. */
    {
        .type = NJS_PROPERTY_HANDLER,
        .name = njs_string("prototype"),
        .value = njs_prop_handler(njs_object_prototype_create),
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
        .type = NJS_PROPERTY_HANDLER,
        .name = njs_string("constructor"),
        .value = njs_prop_handler(njs_object_prototype_create_constructor),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY_HANDLER,
        .name = njs_string("lastIndex"),
        .value = njs_prop_handler(njs_regexp_prototype_last_index),
    },

    {
        .type = NJS_PROPERTY_HANDLER,
        .name = njs_string("global"),
        .value = njs_prop_handler(njs_regexp_prototype_global),
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY_HANDLER,
        .name = njs_string("ignoreCase"),
        .value = njs_prop_handler(njs_regexp_prototype_ignore_case),
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY_HANDLER,
        .name = njs_string("multiline"),
        .value = njs_prop_handler(njs_regexp_prototype_multiline),
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY_HANDLER,
        .name = njs_string("source"),
        .value = njs_prop_handler(njs_regexp_prototype_source),
        .configurable = 1,
    },

    {
        .type = NJS_METHOD,
        .name = njs_string("toString"),
        .value = njs_native_function(njs_regexp_prototype_to_string, 0, 0),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_METHOD,
        .name = njs_string("test"),
        .value = njs_native_function(njs_regexp_prototype_test, 0,
                     NJS_OBJECT_ARG, NJS_STRING_ARG),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_METHOD,
        .name = njs_string("exec"),
        .value = njs_native_function(njs_regexp_prototype_exec, 0,
                     NJS_OBJECT_ARG, NJS_STRING_ARG),
        .writable = 1,
        .configurable = 1,
    },
};


const njs_object_init_t  njs_regexp_prototype_init = {
    nxt_string("RegExp"),
    njs_regexp_prototype_properties,
    nxt_nitems(njs_regexp_prototype_properties),
};
