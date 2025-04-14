
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) NGINX, Inc.
 */


#include <njs_main.h>


struct njs_regexp_group_s {
    njs_str_t  name;
    uint32_t   hash;
    uint32_t   capture;
};


static void *njs_regexp_malloc(size_t size, void *memory_data);
static void njs_regexp_free(void *p, void *memory_data);
static njs_int_t njs_regexp_prototype_source(njs_vm_t *vm, njs_value_t *args,
    njs_uint_t nargs, njs_index_t unused, njs_value_t *retval);
static int njs_regexp_pattern_compile(njs_vm_t *vm, njs_regex_t *regex,
    u_char *source, size_t len, njs_regex_flags_t flags);
static u_char *njs_regexp_compile_trace_handler(njs_trace_t *trace,
    njs_trace_data_t *td, u_char *start);
static u_char *njs_regexp_match_trace_handler(njs_trace_t *trace,
    njs_trace_data_t *td, u_char *start);
#define NJS_REGEXP_FLAG_TEST           1
static njs_int_t njs_regexp_exec(njs_vm_t *vm, njs_value_t *r, njs_value_t *s,
    unsigned flags, njs_value_t *retval);
static njs_array_t *njs_regexp_exec_result(njs_vm_t *vm, njs_value_t *r,
    njs_utf8_t utf8, njs_string_prop_t *string, njs_regex_match_data_t *data);


njs_int_t
njs_regexp_init(njs_vm_t *vm)
{
    vm->regex_generic_ctx = njs_regex_generic_ctx_create(njs_regexp_malloc,
                                                         njs_regexp_free,
                                                         vm->mem_pool);
    if (njs_slow_path(vm->regex_generic_ctx == NULL)) {
        njs_memory_error(vm);
        return NJS_ERROR;
    }

    vm->regex_compile_ctx = njs_regex_compile_ctx_create(vm->regex_generic_ctx);
    if (njs_slow_path(vm->regex_compile_ctx == NULL)) {
        njs_memory_error(vm);
        return NJS_ERROR;
    }

    vm->single_match_data = njs_regex_match_data(NULL, vm->regex_generic_ctx);
    if (njs_slow_path(vm->single_match_data == NULL)) {
        njs_memory_error(vm);
        return NJS_ERROR;
    }

    return NJS_OK;
}


static void *
njs_regexp_malloc(size_t size, void *memory_data)
{
    return njs_mp_alloc(memory_data, size);
}


static void
njs_regexp_free(void *p, void *memory_data)
{
    njs_mp_free(memory_data, p);
}


static njs_regex_flags_t
njs_regexp_value_flags(njs_vm_t *vm, const njs_value_t *regexp)
{
    njs_regex_flags_t     flags;
    njs_regexp_pattern_t  *pattern;

    flags = 0;

    pattern = njs_regexp_pattern(regexp);

    if (pattern->global) {
        flags |= NJS_REGEX_GLOBAL;
    }

    if (pattern->ignore_case) {
        flags |= NJS_REGEX_IGNORE_CASE;
    }

    if (pattern->multiline) {
        flags |= NJS_REGEX_MULTILINE;
    }

    if (pattern->sticky) {
        flags |= NJS_REGEX_STICKY;
    }

    return flags;
}


static njs_int_t
njs_regexp_constructor(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t unused, njs_value_t *retval)
{
    u_char              *start;
    njs_int_t           ret;
    njs_str_t           string;
    njs_value_t         source, *pattern, *flags;
    njs_regex_flags_t   re_flags;

    pattern = njs_arg(args, nargs, 1);

    if (njs_is_regexp(pattern)) {
        ret = njs_regexp_prototype_source(vm, pattern, 1, 0, &source);
        if (njs_slow_path(ret != NJS_OK)) {
            return ret;
        }

        re_flags = njs_regexp_value_flags(vm, pattern);

        pattern = &source;

    } else {
        if (njs_is_defined(pattern)) {
            ret = njs_value_to_string(vm, pattern, pattern);
            if (njs_slow_path(ret != NJS_OK)) {
                return ret;
            }
        }

        re_flags = 0;
    }

    flags = njs_arg(args, nargs, 2);

    if (njs_is_defined(flags)) {
        ret = njs_value_to_string(vm, flags, flags);
        if (njs_slow_path(ret != NJS_OK)) {
            return ret;
        }

        njs_string_get(vm, flags, &string);

        start = string.start;

        re_flags = njs_regexp_flags(&start, start + string.length);
        if (njs_slow_path(re_flags < 0
                          || (size_t) (start - string.start) != string.length))
        {
            njs_syntax_error(vm, "Invalid RegExp flags \"%V\"", &string);
            return NJS_ERROR;
        }
    }

    if (njs_is_defined(pattern)) {
        njs_string_get(vm, pattern, &string);

    } else {
        string = njs_str_value("");
    }

    return njs_regexp_create(vm, retval, string.start, string.length,
                             re_flags);
}


njs_int_t
njs_regexp_create(njs_vm_t *vm, njs_value_t *value, u_char *start,
    size_t length, njs_regex_flags_t flags)
{
    njs_regexp_t          *regexp;
    njs_regexp_pattern_t  *pattern;

    if (length != 0 || flags != 0) {
        if (length == 0) {
            start = (u_char *) "(?:)";
            length = njs_length("(?:)");
        }

        pattern = njs_regexp_pattern_create(vm, start, length, flags);
        if (njs_slow_path(pattern == NULL)) {
            return NJS_ERROR;
        }

    } else {
        pattern = vm->shared->empty_regexp_pattern;
    }

    regexp = njs_regexp_alloc(vm, pattern);

    if (njs_fast_path(regexp != NULL)) {
        njs_set_regexp(value, regexp);

        return NJS_OK;
    }

    return NJS_ERROR;
}


njs_regex_flags_t
njs_regexp_flags(u_char **start, u_char *end)
{
    u_char             *p;
    njs_regex_flags_t  flags, flag;

    flags = NJS_REGEX_NO_FLAGS;

    for (p = *start; p < end; p++) {
        switch (*p) {
        case 'g':
            flag = NJS_REGEX_GLOBAL;
            break;

        case 'i':
            flag = NJS_REGEX_IGNORE_CASE;
            break;

        case 'm':
            flag = NJS_REGEX_MULTILINE;
            break;

        case 'y':
            flag = NJS_REGEX_STICKY;
            break;

        default:
            if (*p >= 'a' && *p <= 'z') {
                goto invalid;
            }

            goto done;
        }

        if (njs_slow_path((flags & flag) != 0)) {
            goto invalid;
        }

        flags |= flag;
    }

done:

    *start = p;

    return flags;

invalid:

    *start = p + 1;

    return NJS_REGEX_INVALID_FLAG;
}


njs_regexp_pattern_t *
njs_regexp_pattern_create(njs_vm_t *vm, u_char *start, size_t length,
    njs_regex_flags_t flags)
{
    int                   ret;
    u_char                *p, *end;
    size_t                size;
    njs_str_t             text;
    njs_bool_t            in;
    njs_uint_t            n;
    njs_regex_t           *regex;
    njs_regexp_group_t    *group;
    njs_regexp_pattern_t  *pattern;

    text.start = start;
    text.length = length;

    in = 0;
    end = start + length;

    for (p = start; p < end; p++) {

        switch (*p) {
        case '[':
            in = 1;
            break;

        case ']':
            in = 0;
            break;

        case '\\':
            p++;
            break;

        case '+':
            if (njs_slow_path(!in
                              && (p - 1 > start)
                              && (p[-1] == '+'|| p[-1] == '*' || p[-1] == '?'))
                              && (p - 2 >= start && p[-2] != '\\'))
            {
                /**
                 * PCRE possessive quantifiers `++`, `*+`, `?+`
                 * are not allowed in JavaScript. Whereas `[++]` or `\?+` are
                 * allowed.
                 */
                goto nothing_to_repeat;
            }

            break;
        }
    }

    ret = njs_regex_escape(vm->mem_pool, &text);
    if (njs_slow_path(ret != NJS_OK)) {
        njs_memory_error(vm);
        return NULL;
    }

    pattern = njs_mp_alloc(vm->mem_pool, sizeof(njs_regexp_pattern_t)
                                          + text.length + 1);
    if (njs_slow_path(pattern == NULL)) {
        njs_memory_error(vm);
        return NULL;
    }

    njs_memzero(pattern, sizeof(njs_regexp_pattern_t));

    p = (u_char *) pattern + sizeof(njs_regexp_pattern_t);
    pattern->source = p;

    p = njs_cpymem(p, text.start, text.length);
    *p++ = '\0';

    pattern->global = ((flags & NJS_REGEX_GLOBAL) != 0);
    pattern->ignore_case = ((flags & NJS_REGEX_IGNORE_CASE) != 0);
    pattern->multiline = ((flags & NJS_REGEX_MULTILINE) != 0);
    pattern->sticky = ((flags & NJS_REGEX_STICKY) != 0);

    ret = njs_regexp_pattern_compile(vm, &pattern->regex[0],
                                     &pattern->source[0], text.length, flags);

    if (njs_fast_path(ret >= 0)) {
        pattern->ncaptures = ret;

    } else if (ret < 0 && ret != NJS_DECLINED) {
        goto fail;
    }

    njs_set_invalid(&vm->exception);

    ret = njs_regexp_pattern_compile(vm, &pattern->regex[1],
                                  &pattern->source[0], text.length,
                                  flags | NJS_REGEX_UTF8);
    if (njs_fast_path(ret >= 0)) {

        if (njs_slow_path(njs_regex_is_valid(&pattern->regex[0])
                          && (u_int) ret != pattern->ncaptures))
        {
            njs_internal_error(vm, "regexp pattern compile failed");
            goto fail;
        }

        pattern->ncaptures = ret;

    } else if (ret != NJS_DECLINED) {
        goto fail;
    }

    if (njs_regex_is_valid(&pattern->regex[0])) {
        regex = &pattern->regex[0];

    } else if (njs_regex_is_valid(&pattern->regex[1])) {
        regex = &pattern->regex[1];

    } else {
        goto fail;
    }

    pattern->ngroups = njs_regex_named_captures(regex, NULL, 0);

    if (pattern->ngroups != 0) {
        size = sizeof(njs_regexp_group_t) * pattern->ngroups;

        pattern->groups = njs_mp_alloc(vm->mem_pool, size);
        if (njs_slow_path(pattern->groups == NULL)) {
            njs_memory_error(vm);
            return NULL;
        }

        n = 0;

        do {
            group = &pattern->groups[n];

            group->capture = njs_regex_named_captures(regex, &group->name, n);
            group->hash = njs_djb_hash(group->name.start, group->name.length);

            n++;

        } while (n != pattern->ngroups);
    }

    return pattern;

fail:

    njs_mp_free(vm->mem_pool, pattern);
    return NULL;

nothing_to_repeat:

    njs_syntax_error(vm, "Invalid regular expression \"%V\" nothing to repeat",
                     &text);
    return NULL;
}


static int
njs_regexp_pattern_compile(njs_vm_t *vm, njs_regex_t *regex, u_char *source,
    size_t len, njs_regex_flags_t flags)
{
    njs_int_t            ret;
    njs_trace_handler_t  handler;

    handler = vm->trace.handler;
    vm->trace.handler = njs_regexp_compile_trace_handler;

    ret = njs_regex_compile(regex, source, len, flags, vm->regex_compile_ctx,
                            &vm->trace);

    vm->trace.handler = handler;

    if (njs_fast_path(ret == NJS_OK)) {
        return regex->ncaptures;
    }

    return ret;
}


static u_char *
njs_regexp_compile_trace_handler(njs_trace_t *trace, njs_trace_data_t *td,
    u_char *start)
{
    u_char    *p;
    njs_vm_t  *vm;

    vm = trace->data;

    trace = trace->next;
    p = trace->handler(trace, td, start);

    njs_syntax_error(vm, "%*s", p - start, start);

    return p;
}


njs_int_t
njs_regexp_match(njs_vm_t *vm, njs_regex_t *regex, const u_char *subject,
    size_t off, size_t len, njs_regex_match_data_t *match_data)
{
    njs_int_t            ret;
    njs_trace_handler_t  handler;

    handler = vm->trace.handler;
    vm->trace.handler = njs_regexp_match_trace_handler;

    ret = njs_regex_match(regex, subject, off, len, match_data, &vm->trace);

    vm->trace.handler = handler;

    return ret;
}


static u_char *
njs_regexp_match_trace_handler(njs_trace_t *trace, njs_trace_data_t *td,
    u_char *start)
{
    u_char    *p;
    njs_vm_t  *vm;

    vm = trace->data;

    trace = trace->next;
    p = trace->handler(trace, td, start);

    njs_internal_error(vm, "%*s", p - start, start);

    return p;
}


njs_regexp_t *
njs_regexp_alloc(njs_vm_t *vm, njs_regexp_pattern_t *pattern)
{
    njs_regexp_t  *regexp;

    regexp = njs_mp_alloc(vm->mem_pool, sizeof(njs_regexp_t));

    if (njs_fast_path(regexp != NULL)) {
        njs_lvlhsh_init(&regexp->object.hash);
        regexp->object.shared_hash = vm->shared->regexp_instance_hash;
        regexp->object.__proto__ = njs_vm_proto(vm, NJS_OBJ_TYPE_REGEXP);
        regexp->object.slots = NULL;
        regexp->object.type = NJS_REGEXP;
        regexp->object.shared = 0;
        regexp->object.extensible = 1;
        regexp->object.fast_array = 0;
        regexp->object.error_data = 0;
        njs_set_number(&regexp->last_index, 0);
        regexp->pattern = pattern;
        njs_set_empty_string(vm, &regexp->string);
        return regexp;
    }

    njs_memory_error(vm);

    return NULL;
}


static njs_int_t
njs_regexp_prototype_last_index(njs_vm_t *vm, njs_object_prop_t *unused,
    uint32_t unused2, njs_value_t *value, njs_value_t *setval,
    njs_value_t *retval)
{
    njs_regexp_t  *regexp;

    regexp = njs_object_proto_lookup(njs_object(value), NJS_REGEXP,
                                     njs_regexp_t);
    if (njs_slow_path(regexp == NULL)) {
        njs_set_undefined(retval);
        return NJS_DECLINED;
    }

    if (setval != NULL) {
        regexp->last_index = *setval;
        njs_value_assign(retval, setval);

        return NJS_OK;
    }

    njs_value_assign(retval, &regexp->last_index);
    return NJS_OK;
}


static njs_int_t
njs_regexp_prototype_flags(njs_vm_t *vm, njs_value_t *args,
    njs_uint_t nargs, njs_index_t unused, njs_value_t *retval)
{
    u_char       *p;
    njs_int_t    ret;
    njs_value_t  *this, value;
    u_char       dst[4];

    this = njs_argument(args, 0);
    if (njs_slow_path(!njs_is_object(this))) {
        njs_type_error(vm, "\"this\" argument is not an object");
        return NJS_ERROR;
    }

    p = &dst[0];

    ret = njs_value_property(vm, this, NJS_ATOM_STRING_global, &value);
    if (njs_slow_path(ret == NJS_ERROR)) {
        return NJS_ERROR;
    }

    if (njs_bool(&value)) {
        *p++ = 'g';
    }

    ret = njs_value_property(vm, this, NJS_ATOM_STRING_ignoreCase, &value);
    if (njs_slow_path(ret == NJS_ERROR)) {
        return NJS_ERROR;
    }

    if (njs_bool(&value)) {
        *p++ = 'i';
    }

    ret = njs_value_property(vm, this, NJS_ATOM_STRING_multiline, &value);
    if (njs_slow_path(ret == NJS_ERROR)) {
        return NJS_ERROR;
    }

    if (njs_bool(&value)) {
        *p++ = 'm';
    }

    ret = njs_value_property(vm, this, NJS_ATOM_STRING_sticky, &value);
    if (njs_slow_path(ret == NJS_ERROR)) {
        return NJS_ERROR;
    }

    if (njs_bool(&value)) {
        *p++ = 'y';
    }

    return njs_string_new(vm, retval, dst, p - dst, p - dst);
}


static njs_int_t
njs_regexp_prototype_flag(njs_vm_t *vm, njs_value_t *args,
    njs_uint_t nargs, njs_index_t flag, njs_value_t *retval)
{
    unsigned              yn;
    njs_value_t           *this;
    njs_regexp_pattern_t  *pattern;

    this = njs_argument(args, 0);
    if (njs_slow_path(!njs_is_object(this))) {
        njs_type_error(vm, "\"this\" argument is not an object");
        return NJS_ERROR;
    }

    if (njs_slow_path(!njs_is_regexp(this))) {
        if (njs_object(this) == njs_vm_proto(vm, NJS_OBJ_TYPE_REGEXP)) {
            njs_set_undefined(retval);
            return NJS_OK;
        }

        njs_type_error(vm, "\"this\" argument is not a regexp");
        return NJS_ERROR;
    }

    pattern = njs_regexp_pattern(this);

    switch (flag) {
    case NJS_REGEX_GLOBAL:
        yn = pattern->global;
        break;

    case NJS_REGEX_IGNORE_CASE:
        yn = pattern->ignore_case;
        break;

    case NJS_REGEX_MULTILINE:
        yn = pattern->multiline;
        break;

    case NJS_REGEX_STICKY:
    default:
        yn = pattern->sticky;
        break;
    }

    njs_set_boolean(retval, yn);

    return NJS_OK;
}


static njs_int_t
njs_regexp_prototype_source(njs_vm_t *vm, njs_value_t *args,
    njs_uint_t nargs, njs_index_t unused, njs_value_t *retval)
{
    njs_str_t             src;
    njs_value_t           *this;
    njs_regexp_pattern_t  *pattern;

    this = njs_argument(args, 0);
    if (njs_slow_path(!njs_is_object(this))) {
        njs_type_error(vm, "\"this\" argument is not an object");
        return NJS_ERROR;
    }

    if (njs_slow_path(!njs_is_regexp(this))) {
        if (njs_object(this) == njs_vm_proto(vm, NJS_OBJ_TYPE_REGEXP)) {
            njs_atom_to_value(vm, retval, NJS_ATOM_STRING_spec_EMPTY_REGEXP);
            return NJS_OK;
        }

        njs_type_error(vm, "\"this\" argument is not a regexp");
        return NJS_ERROR;
    }

    pattern = njs_regexp_pattern(this);

    src.start = pattern->source;
    src.length = njs_strlen(pattern->source);

    return njs_string_decode_utf8(vm, retval, &src);
}


static njs_int_t
njs_regexp_prototype_to_string(njs_vm_t *vm, njs_value_t *args,
    njs_uint_t nargs, njs_index_t unused, njs_value_t *retval)
{
    u_char             *p;
    size_t             size, length;
    njs_int_t          ret;
    njs_value_t        *r, source, flags;
    njs_string_prop_t  source_string, flags_string;

    r = njs_argument(args, 0);

    if (njs_slow_path(!njs_is_object(r))) {
        njs_type_error(vm, "\"this\" argument is not an object");
        return NJS_ERROR;
    }

    ret = njs_value_property(vm, r, NJS_ATOM_STRING_source, &source);
    if (njs_slow_path(ret == NJS_ERROR)) {
        return NJS_ERROR;
    }

    ret = njs_value_to_string(vm, &source, &source);
    if (njs_slow_path(ret != NJS_OK)) {
        return NJS_ERROR;
    }

    ret = njs_value_property(vm, r, NJS_ATOM_STRING_flags, &flags);
    if (njs_slow_path(ret == NJS_ERROR)) {
        return NJS_ERROR;
    }

    ret = njs_value_to_string(vm, &flags, &flags);
    if (njs_slow_path(ret != NJS_OK)) {
        return NJS_ERROR;
    }

    (void) njs_string_prop(vm, &source_string, &source);
    (void) njs_string_prop(vm, &flags_string, &flags);

    size = source_string.size + flags_string.size + njs_length("//");
    length = source_string.length + flags_string.length + njs_length("//");

    p = njs_string_alloc(vm, retval, size, length);
    if (njs_slow_path(p == NULL)) {
        return NJS_ERROR;
    }

    *p++ = '/';
    p = njs_cpymem(p, source_string.start, source_string.size);
    *p++ = '/';
    memcpy(p, flags_string.start, flags_string.size);

    return NJS_OK;
}


njs_int_t
njs_regexp_to_string(njs_vm_t *vm, njs_value_t *retval,
    const njs_value_t *value)
{
    u_char                *p, *start;
    size_t                size, extra;
    int32_t               length;
    njs_str_t             s;
    njs_regexp_pattern_t  *pattern;
    njs_unicode_decode_t  ctx;

    pattern = njs_regexp_pattern(value);

    s.start = pattern->source;
    s.length = njs_strlen(pattern->source);

    length = njs_decode_utf8_length(&s, &size);

    extra = njs_length("//");
    extra += (pattern->global != 0);
    extra += (pattern->ignore_case != 0);
    extra += (pattern->multiline != 0);
    extra += (pattern->sticky != 0);

    size += extra;

    length = (length >= 0) ? (length + extra) : 0;

    start = njs_string_alloc(vm, retval, size, length);
    if (njs_slow_path(start == NULL)) {
        return NJS_ERROR;
    }

    njs_utf8_decode_init(&ctx);

    p = start;

    *p++ = '/';

    p = njs_utf8_stream_encode(&ctx, s.start, &s.start[s.length], p, 1, 0);

    *p++ = '/';

    if (pattern->global) {
        *p++ = 'g';
    }

    if (pattern->ignore_case) {
        *p++ = 'i';
    }

    if (pattern->multiline) {
        *p++ = 'm';
    }

    if (pattern->sticky) {
        *p++ = 'y';
    }

    return NJS_OK;
}


static njs_int_t
njs_regexp_prototype_test(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t unused, njs_value_t *retval)
{
    njs_int_t    ret;
    njs_value_t  *r, *string, lvalue, value;

    r = njs_argument(args, 0);

    if (njs_slow_path(!njs_is_object(r))) {
        njs_type_error(vm, "\"this\" argument is not an object");
        return NJS_ERROR;
    }

    string = njs_lvalue_arg(&lvalue, args, nargs, 1);

    ret = njs_value_to_string(vm, string, string);
    if (njs_slow_path(ret != NJS_OK)) {
        return NJS_ERROR;
    }

    ret = njs_regexp_exec(vm, r, string, NJS_REGEXP_FLAG_TEST, &value);
    if (njs_slow_path(ret != NJS_OK)) {
        return NJS_ERROR;
    }

    njs_set_boolean(retval, !njs_is_null(&value));

    return NJS_OK;
}


/**
 * TODO: unicode flags.
 */
static njs_int_t
njs_regexp_builtin_exec(njs_vm_t *vm, njs_value_t *r, njs_value_t *s,
    unsigned flags, njs_value_t *retval)
{
    size_t                  c, length, offset;
    int64_t                 last_index;
    uint32_t                index;
    njs_int_t               ret;
    njs_utf8_t              utf8;
    njs_value_t             value;
    njs_array_t             *result;
    njs_regexp_t            *regexp;
    njs_string_prop_t       string;
    njs_regexp_utf8_t       type;
    njs_regexp_pattern_t    *pattern;
    njs_regex_match_data_t  *match_data;

    regexp = njs_regexp(r);
    regexp->string = *s;
    pattern = regexp->pattern;

    ret = njs_value_property(vm, r, NJS_ATOM_STRING_lastIndex, &value);
    if (njs_slow_path(ret == NJS_ERROR)) {
        return NJS_ERROR;
    }

    ret = njs_value_to_length(vm, &value, &last_index);
    if (njs_slow_path(ret != NJS_OK)) {
        return NJS_ERROR;
    }

    if (!pattern->global && !pattern->sticky) {
        last_index = 0;
    }

    length = njs_string_prop(vm, &string, s);

    if (njs_slow_path((size_t) last_index > length)) {
        goto not_found;
    }

    utf8 = NJS_STRING_ASCII;
    type = NJS_REGEXP_BYTE;

    if (string.length != 0) {
        type = NJS_REGEXP_UTF8;

        if (!njs_is_ascii_string(&string)) {
            utf8 = NJS_STRING_UTF8;
        }
    }

    pattern = regexp->pattern;

    if (njs_slow_path(!njs_regex_is_valid(&pattern->regex[type]))) {
        goto not_found;
    }

    match_data = njs_regex_match_data(&pattern->regex[type],
                                      vm->regex_generic_ctx);
    if (njs_slow_path(match_data == NULL)) {
        njs_memory_error(vm);
        return NJS_ERROR;
    }

    if (utf8 != NJS_STRING_UTF8) {
        offset = last_index;

    } else {
        if ((size_t) last_index < string.length) {
            offset = njs_string_utf8_offset(string.start,
                                            string.start + string.size,
                                            last_index)
                     - string.start;
        } else {
            offset = string.size;
        }
    }

    ret = njs_regexp_match(vm, &pattern->regex[type], string.start, offset,
                           string.size, match_data);
    if (ret >= 0) {
        if (pattern->global || pattern->sticky) {
            c = njs_regex_capture(match_data, 1);

            if (utf8 == NJS_STRING_UTF8) {
                index = njs_string_index(&string, c);

            } else {
                index = c;
            }

            njs_set_number(&value, index);
            ret = njs_value_property_set(vm, r, NJS_ATOM_STRING_lastIndex,
                                         &value);
            if (njs_slow_path(ret != NJS_OK)) {
                njs_regex_match_data_free(match_data, vm->regex_generic_ctx);
                return NJS_ERROR;
            }
        }

        if (flags & NJS_REGEXP_FLAG_TEST) {
            njs_regex_match_data_free(match_data, vm->regex_generic_ctx);
            njs_set_boolean(retval, 1);
            return NJS_OK;
        }

        result = njs_regexp_exec_result(vm, r, utf8, &string, match_data);

        njs_regex_match_data_free(match_data, vm->regex_generic_ctx);

        if (njs_slow_path(result == NULL)) {
            return NJS_ERROR;
        }

        njs_set_array(retval, result);
        return NJS_OK;
    }

    njs_regex_match_data_free(match_data, vm->regex_generic_ctx);

    if (njs_slow_path(ret == NJS_ERROR)) {
        return NJS_ERROR;
    }

not_found:

    if (pattern->global || pattern->sticky) {
        njs_set_number(&value, 0);
        ret = njs_value_property_set(vm, r, NJS_ATOM_STRING_lastIndex, &value);
        if (njs_slow_path(ret != NJS_OK)) {
            return NJS_ERROR;
        }
    }

    njs_set_null(retval);

    return NJS_OK;
}


static njs_exotic_slots_t njs_regexp_prototype_exotic_slots = {
    .prop_handler = NULL,
    .keys = NULL,
};


static njs_array_t *
njs_regexp_exec_result(njs_vm_t *vm, njs_value_t *r, njs_utf8_t utf8,
    njs_string_prop_t *string, njs_regex_match_data_t *match_data)
{
    u_char                *start;
    size_t                c;
    int32_t               size;
    uint32_t              index;
    njs_int_t             ret;
    njs_uint_t            i, n;
    njs_array_t           *array;
    njs_value_t           name;
    njs_object_t          *groups;
    njs_regexp_t          *regexp;
    njs_object_prop_t     *prop;
    njs_regexp_group_t    *group;
    njs_flathsh_query_t   lhq;
    njs_regexp_pattern_t  *pattern;

    regexp = njs_regexp(r);
    pattern = regexp->pattern;
    array = njs_array_alloc(vm, 0, pattern->ncaptures, 0);
    if (njs_slow_path(array == NULL)) {
        goto fail;
    }

    array->object.slots = &njs_regexp_prototype_exotic_slots;

    for (i = 0; i < pattern->ncaptures; i++) {
        n = 2 * i;
        c = njs_regex_capture(match_data, n);

        if (c != NJS_REGEX_UNSET) {
            start = &string->start[c];
            size = njs_regex_capture(match_data, n + 1) - c;

            ret = njs_string_create(vm, &array->start[i], start, size);
            if (njs_slow_path(ret != NJS_OK)) {
                goto fail;
            }

        } else {
            njs_set_undefined(&array->start[i]);
        }
    }

    /* FIXME: implement fast CreateDataPropertyOrThrow(). */
    prop = njs_object_prop_alloc(vm, &njs_value_undefined, 1);
    if (njs_slow_path(prop == NULL)) {
        goto fail;
    }

    c = njs_regex_capture(match_data, 0);

    if (utf8 == NJS_STRING_UTF8) {
        index = njs_string_index(string, c);

    } else {
        index = c;
    }

    njs_set_number(&prop->u.value, index);

    lhq.key_hash = NJS_ATOM_STRING_index;
    lhq.replace = 0;
    lhq.value = prop;
    lhq.pool = vm->mem_pool;
    lhq.proto = &njs_object_hash_proto;

    ret = njs_flathsh_unique_insert(&array->object.hash, &lhq);
    if (njs_slow_path(ret != NJS_OK)) {
        goto insert_fail;
    }

    prop = njs_object_prop_alloc(vm, &regexp->string, 1);
    if (njs_slow_path(prop == NULL)) {
        goto fail;
    }

    lhq.key_hash = NJS_ATOM_STRING_input;
    lhq.value = prop;

    ret = njs_flathsh_unique_insert(&array->object.hash, &lhq);
    if (njs_slow_path(ret != NJS_OK)) {
        goto insert_fail;
    }

    prop = njs_object_prop_alloc(vm, &njs_value_undefined, 1);
    if (njs_slow_path(prop == NULL)) {
        goto fail;
    }

    lhq.key_hash = NJS_ATOM_STRING_groups;
    lhq.value = prop;

    ret = njs_flathsh_unique_insert(&array->object.hash, &lhq);
    if (njs_slow_path(ret != NJS_OK)) {
        goto insert_fail;
    }

    if (pattern->ngroups != 0) {
        groups = njs_object_alloc(vm);
        if (njs_slow_path(groups == NULL)) {
            goto fail;
        }

        njs_set_object(&prop->u.value, groups);

        i = 0;

        do {
            group = &pattern->groups[i];

            ret = njs_atom_string_create(vm, &name, group->name.start,
                                         group->name.length);
            if (njs_slow_path(ret != NJS_OK)) {
                goto fail;
            }

            prop = njs_object_prop_alloc(vm, &array->start[group->capture], 1);
            if (njs_slow_path(prop == NULL)) {
                goto fail;
            }

            lhq.key_hash = name.atom_id;
            lhq.value = prop;

            ret = njs_flathsh_unique_insert(&groups->hash, &lhq);
            if (njs_slow_path(ret != NJS_OK)) {
                goto insert_fail;
            }

            i++;

        } while (i < pattern->ngroups);
    }

    ret = NJS_OK;
    goto done;

insert_fail:

    njs_internal_error(vm, "lvlhsh insert failed");

fail:

    ret = NJS_ERROR;

done:

    return (ret == NJS_OK) ? array : NULL;
}


static void
njs_regexp_exec_result_free(njs_vm_t *vm, njs_array_t *result)
{
    njs_uint_t           n;
    njs_value_t          *start;
    njs_flathsh_t        *hash;
    njs_object_prop_t    *prop;
    njs_flathsh_elt_t    *elt;
    njs_flathsh_each_t   lhe;
    njs_flathsh_query_t  lhq;

    if (result->object.fast_array) {
        start = result->start;

        for (n = 0; n < result->length; n++) {
            if (start[n].type == NJS_STRING) {
                njs_mp_free(vm->mem_pool, start[n].string.data);
            }
        }
    }

    njs_flathsh_each_init(&lhe, &njs_object_hash_proto);

    hash = &result->object.hash;

    for ( ;; ) {
        elt = njs_flathsh_each(hash, &lhe);
        if (elt == NULL) {
            break;
        }
        prop = elt->value;

        njs_mp_free(vm->mem_pool, prop);
    }


    lhq.pool = vm->mem_pool;
    lhq.proto = &njs_object_hash_proto;

    njs_flathsh_destroy(hash, &lhq);

    njs_array_destroy(vm, result);
}


njs_int_t
njs_regexp_prototype_exec(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t unused, njs_value_t *retval)
{
    unsigned     flags;
    njs_int_t    ret;
    njs_value_t  *r, *s;
    njs_value_t  string_lvalue;

    r = njs_argument(args, 0);

    if (njs_slow_path(!njs_is_regexp(r))) {
        njs_type_error(vm, "\"this\" argument is not a regexp");
        return NJS_ERROR;
    }

    s = njs_lvalue_arg(&string_lvalue, args, nargs, 1);

    ret = njs_value_to_string(vm, s, s);
    if (njs_slow_path(ret != NJS_OK)) {
        return ret;
    }

    if (nargs > 2) {
        flags = njs_number(njs_arg(args, nargs, 2));

    } else {
        flags = 0;
    }

    return njs_regexp_builtin_exec(vm, r, s, flags, retval);
}


static njs_int_t
njs_regexp_exec(njs_vm_t *vm, njs_value_t *r, njs_value_t *s, unsigned flags,
    njs_value_t *retval)
{
    njs_int_t    ret;
    njs_value_t  exec;
    njs_value_t  arguments[2];

    ret = njs_value_property(vm, r, NJS_ATOM_STRING_exec, &exec);
    if (njs_slow_path(ret == NJS_ERROR)) {
        return NJS_ERROR;
    }

    if (njs_is_function(&exec)) {
        njs_value_assign(&arguments[0], s);

        if (flags) {
            njs_set_number(&arguments[1], flags);
        }

        ret = njs_function_call(vm, njs_function(&exec), r, arguments,
                                flags ? 2 : 1, retval);
        if (njs_slow_path(ret == NJS_ERROR)) {
            return NJS_ERROR;
        }

        if (njs_slow_path(!njs_is_null(retval)
                          && (flags & NJS_REGEXP_FLAG_TEST
                              && !njs_is_boolean(retval))))
        {
            njs_type_error(vm, "unexpected \"%s\" retval in njs_regexp_exec()",
                           njs_type_string(retval->type));
            return NJS_ERROR;
        }

        if (njs_slow_path(!njs_is_null(retval)
                          && (!flags && !njs_is_object(retval))))
        {
            njs_type_error(vm, "unexpected \"%s\" retval in njs_regexp_exec()",
                           njs_type_string(retval->type));
            return NJS_ERROR;
        }

        return NJS_OK;
    }

    if (njs_slow_path(!njs_is_regexp(r))) {
        njs_type_error(vm, "receiver argument is not a regexp");
        return NJS_ERROR;
    }

    return njs_regexp_builtin_exec(vm, r, s, flags, retval);
}


njs_int_t
njs_regexp_prototype_symbol_replace(njs_vm_t *vm, njs_value_t *args,
    njs_uint_t nargs, njs_index_t unused, njs_value_t *retval)
{
    int64_t            n, last_index, ncaptures, pos, length;
    const u_char       *p, *next;
    njs_str_t          rep, m;
    njs_int_t          ret;
    njs_arr_t          results;
    njs_chb_t          chain;
    njs_uint_t         i;
    njs_bool_t         global;
    njs_array_t        *array;
    njs_value_t        *arguments, *r, *rx, *string, *replace;
    njs_value_t        s_lvalue, r_lvalue, value, matched, groups;
    njs_function_t     *func_replace;
    njs_string_prop_t  s;

    rx = njs_argument(args, 0);

    if (njs_slow_path(!njs_is_object(rx))) {
        njs_type_error(vm, "\"this\" is not object");
        return NJS_ERROR;
    }

    string = njs_lvalue_arg(&s_lvalue, args, nargs, 1);

    ret = njs_value_to_string(vm, string, string);
    if (njs_slow_path(ret != NJS_OK)) {
        return ret;
    }

    length = njs_string_prop(vm, &s, string);

    rep.start = NULL;
    rep.length = 0;

    replace = njs_lvalue_arg(&r_lvalue, args, nargs, 2);
    func_replace = njs_is_function(replace) ? njs_function(replace) : NULL;

    if (!func_replace) {
        ret = njs_value_to_string(vm, replace, replace);
        if (njs_slow_path(ret != NJS_OK)) {
            return ret;
        }
    }

    ret = njs_value_property(vm, rx, NJS_ATOM_STRING_global, &value);
    if (njs_slow_path(ret == NJS_ERROR)) {
        return NJS_ERROR;
    }

    global = njs_bool(&value);

    if (global) {
        njs_set_number(&value, 0);
        ret = njs_value_property_set(vm, rx, NJS_ATOM_STRING_lastIndex, &value);
        if (njs_slow_path(ret != NJS_OK)) {
            return NJS_ERROR;
        }
    }

    NJS_CHB_MP_INIT(&chain, vm);

    results.separate = 0;
    results.pointer = 0;

    r = njs_arr_init(vm->mem_pool, &results, NULL, 4, sizeof(njs_value_t));
    if (njs_slow_path(r == NULL)) {
        return NJS_ERROR;
    }

    for ( ;; ) {
        r = njs_arr_add(&results);
        if (njs_slow_path(r == NULL)) {
            ret = NJS_ERROR;
            goto exception;
        }

        ret = njs_regexp_exec(vm, rx, string, 0, r);
        if (njs_slow_path(ret != NJS_OK)) {
            goto exception;
        }

        if (njs_is_null(r) || !global) {
            break;
        }

        if (njs_fast_path(njs_is_fast_array(r) && njs_array_len(r) != 0)) {
            value = njs_array_start(r)[0];

            if (!njs_is_valid(&value)) {
                njs_set_undefined(&value);
            }

        } else {
            ret = njs_value_property_i64(vm, r, 0, &value);
            if (njs_slow_path(ret == NJS_ERROR)) {
                goto exception;
            }
        }

        ret = njs_value_to_string(vm, &value, &value);
        if (njs_slow_path(ret != NJS_OK)) {
            goto exception;
        }

        njs_string_get(vm, &value, &m);

        if (m.length != 0) {
            continue;
        }

        ret = njs_value_property(vm, rx, NJS_ATOM_STRING_lastIndex, &value);
        if (njs_slow_path(ret == NJS_ERROR)) {
            goto exception;
        }

        ret = njs_value_to_length(vm, &value, &last_index);
        if (njs_slow_path(ret != NJS_OK)) {
            goto exception;
        }

        njs_set_number(&value, last_index + 1);
        ret = njs_value_property_set(vm, rx, NJS_ATOM_STRING_lastIndex, &value);
        if (njs_slow_path(ret != NJS_OK)) {
            goto exception;
        }
    }

    i = 0;
    p = s.start;
    next = p;

    while (i < results.items) {
        r = njs_arr_item(&results, i++);

        if (njs_slow_path(njs_is_null(r))) {
            break;
        }

        ret = njs_value_property_i64(vm, r, 0, &matched);
        if (njs_slow_path(ret == NJS_ERROR)) {
            goto exception;
        }

        ret = njs_value_to_string(vm, &matched, &matched);
        if (njs_slow_path(ret != NJS_OK)) {
            goto exception;
        }

        ret = njs_value_property(vm, r, NJS_ATOM_STRING_index, &value);
        if (njs_slow_path(ret == NJS_ERROR)) {
            goto exception;
        }

        ret = njs_value_to_integer(vm, &value, &pos);
        if (njs_slow_path(ret != NJS_OK)) {
            goto exception;
        }

        pos = njs_max(njs_min(pos, (int64_t) length), 0);

        ret = njs_object_length(vm, r, &ncaptures);
        if (njs_slow_path(ret != NJS_OK)) {
            goto exception;
        }

        ncaptures = njs_min(njs_max(ncaptures - 1, 0), 99);

        array = njs_array_alloc(vm, 1, ncaptures + 1, 0);
        if (njs_slow_path(array == NULL)) {
            goto exception;
        }

        arguments = array->start;
        arguments[0] = matched;

        for (n = 1; n <= ncaptures; n++) {
            ret = njs_value_property_i64(vm, r, n, &arguments[n]);
            if (njs_slow_path(ret == NJS_ERROR)) {
                goto exception;
            }

            if (njs_is_undefined(&arguments[n])) {
                continue;
            }

            ret = njs_value_to_string(vm, &arguments[n], &arguments[n]);
            if (njs_slow_path(ret == NJS_ERROR)) {
                goto exception;
            }
        }

        ret = njs_value_property(vm, r, NJS_ATOM_STRING_groups, &groups);
        if (njs_slow_path(ret == NJS_ERROR)) {
            goto exception;
        }

        if (!func_replace) {
            if (njs_is_defined(&groups)) {
                ret = njs_value_to_object(vm, &groups);
                if (njs_slow_path(ret != NJS_OK)) {
                    return ret;
                }
            }

            ret = njs_string_get_substitution(vm, &matched, string, pos,
                                              arguments, ncaptures, &groups,
                                              replace, retval);

        } else {
            ret = njs_array_expand(vm, array, 0,
                                   njs_is_defined(&groups) ? 3 : 2);
            if (njs_slow_path(ret != NJS_OK)) {
                goto exception;
            }

            arguments = array->start;
            njs_set_number(&arguments[n++], pos);
            arguments[n++] = *string;

            if (njs_is_defined(&groups)) {
                arguments[n++] = groups;
            }

            ret = njs_function_call(vm, func_replace,
                                    njs_value_arg(&njs_value_undefined),
                                    arguments, n, retval);
        }

        njs_array_destroy(vm, array);

        if (njs_slow_path(ret == NJS_ERROR)) {
            return NJS_ERROR;
        }

        ret = njs_value_to_string(vm, retval, retval);
        if (njs_slow_path(ret != NJS_OK)) {
            goto exception;
        }

        p = njs_string_offset(&s, pos);

        if (p >= next) {
            njs_chb_append(&chain, next, p - next);

            njs_string_get(vm, retval, &rep);
            njs_chb_append_str(&chain, &rep);

            njs_string_get(vm, &matched, &m);

            next = p + m.length;
        }

        if (!func_replace && njs_object_slots(r)) {
            /*
              * Doing free here ONLY for non-function replace, because
              * otherwise we cannot be certain the result of match
              * was not stored elsewhere.
              */
            njs_regexp_exec_result_free(vm, njs_array(r));
        }
    }

    if (next < s.start + s.size) {
        njs_chb_append(&chain, next, s.start + s.size - next);
    }

    ret = njs_string_create_chb(vm, retval, &chain);
    if (njs_slow_path(ret != NJS_OK)) {
        ret = NJS_ERROR;
        goto exception;
    }

    ret = NJS_OK;

exception:

    njs_chb_destroy(&chain);
    njs_arr_destroy(&results);

    return ret;
}


static njs_int_t
njs_regexp_prototype_symbol_split(njs_vm_t *vm, njs_value_t *args,
    njs_uint_t nargs, njs_index_t unused, njs_value_t *retval)
{
    u_char             *dst;
    int64_t            e, i, p, q, ncaptures, length;
    ssize_t            len;
    uint32_t           limit;
    njs_int_t          ret;
    njs_bool_t         sticky;
    njs_utf8_t         utf8;
    njs_array_t        *array;
    njs_value_t        *rx, *string, *value;
    njs_value_t        r, z, this, s_lvalue, setval, constructor;
    njs_object_t       *object;
    const u_char       *start, *end;
    njs_string_prop_t  s, sv;
    njs_value_t        arguments[2];

    rx = njs_argument(args, 0);

    if (njs_slow_path(!njs_is_object(rx))) {
        njs_type_error(vm, "\"this\" is not object");
        return NJS_ERROR;
    }

    string = njs_lvalue_arg(&s_lvalue, args, nargs, 1);

    ret = njs_value_to_string(vm, string, string);
    if (njs_slow_path(ret != NJS_OK)) {
        return ret;
    }

    njs_set_function(&constructor, &njs_vm_ctor(vm, NJS_OBJ_TYPE_REGEXP));

    ret = njs_value_species_constructor(vm, rx, &constructor, &constructor);
    if (njs_slow_path(ret != NJS_OK)) {
        return ret;
    }

    ret = njs_value_property(vm, rx, NJS_ATOM_STRING_flags, retval);
    if (njs_slow_path(ret == NJS_ERROR)) {
        return NJS_ERROR;
    }

    ret = njs_value_to_string(vm, retval, retval);
    if (njs_slow_path(ret != NJS_OK)) {
        return ret;
    }

    (void) njs_string_prop(vm, &s, retval);

    sticky = memchr(s.start, 'y', s.size) != NULL;

    object = njs_function_new_object(vm, &constructor);
    if (njs_slow_path(object == NULL)) {
        return NJS_ERROR;
    }

    njs_set_object(&this, object);

    arguments[0] = *rx;

    if (!sticky) {
        length = s.length + 1;

        dst = njs_string_alloc(vm, &arguments[1], s.size + 1, length);
        if (njs_slow_path(dst == NULL)) {
            return NJS_ERROR;
        }

        dst = njs_cpymem(dst, s.start, s.size);
        *dst++ = 'y';

    } else {
        njs_value_assign(&arguments[1], retval);
    }

    ret = njs_function_call2(vm, njs_function(&constructor), &this,
                             njs_value_arg(&arguments), 2, &r, 1);
    if (njs_slow_path(ret != NJS_OK)) {
        return NJS_ERROR;
    }

    rx = &r;

    array = njs_array_alloc(vm, 0, 0, NJS_ARRAY_SPARE);
    if (njs_slow_path(array == NULL)) {
        return NJS_ERROR;
    }

    value = njs_arg(args, nargs, 2);
    limit = UINT32_MAX;

    if (njs_is_defined(value)) {
        ret = njs_value_to_uint32(vm, value, &limit);
        if (njs_slow_path(ret != NJS_OK)) {
            return ret;
        }
    }

    if (njs_slow_path(limit == 0)) {
        goto done;
    }

    length = njs_string_prop(vm, &s, string);

    if (njs_slow_path(s.size == 0)) {
        ret = njs_regexp_exec(vm, rx, string, NJS_REGEXP_FLAG_TEST, &z);
        if (njs_slow_path(ret != NJS_OK)) {
            return NJS_ERROR;
        }

        if (!njs_is_null(&z)) {
            goto done;
        }

        goto single;
    }

    utf8 = NJS_STRING_ASCII;

    if (!njs_is_ascii_string(&s)) {
        utf8 = NJS_STRING_UTF8;
    }

    p = 0;
    q = 0;

    while (q < length) {
        njs_set_number(&setval, q);
        ret = njs_value_property_set(vm, rx, NJS_ATOM_STRING_lastIndex, &setval);
        if (njs_slow_path(ret != NJS_OK)) {
            return NJS_ERROR;
        }

        ret = njs_regexp_exec(vm, rx, string, 0, &z);
        if (njs_slow_path(ret != NJS_OK)) {
            return NJS_ERROR;
        }

        if (njs_is_null(&z)) {
            q = q + 1;
            continue;
        }

        ret = njs_value_property(vm, rx, NJS_ATOM_STRING_lastIndex, retval);
        if (njs_slow_path(ret == NJS_ERROR)) {
            return NJS_ERROR;
        }

        ret = njs_value_to_length(vm, retval, &e);
        if (njs_slow_path(ret != NJS_OK)) {
            return NJS_ERROR;
        }

        e = njs_min(e, length);

        if (e == p) {
            if (njs_object_slots(&z)) {
                njs_regexp_exec_result_free(vm, njs_array(&z));
            }

            q = q + 1;
            continue;
        }

        if (utf8 == NJS_STRING_UTF8) {
            start = njs_string_utf8_offset(s.start, s.start + s.size, p);
            end = njs_string_utf8_offset(s.start, s.start + s.size, q);

        } else {
            start = &s.start[p];
            end = &s.start[q];
        }

        len = njs_string_calc_length(utf8, start, end - start);

        ret = njs_array_string_add(vm, array, start, end - start, len);
        if (njs_slow_path(ret != NJS_OK)) {
            return ret;
        }

        if (array->length == limit) {
            if (njs_object_slots(&z)) {
                njs_regexp_exec_result_free(vm, njs_array(&z));
            }

            goto done;
        }

        p = e;

        ret = njs_object_length(vm, &z, &ncaptures);
        if (njs_slow_path(ret != NJS_OK)) {
            return NJS_ERROR;
        }

        ncaptures = njs_max(ncaptures - 1, 0);

        for (i = 1; i <= ncaptures; i++) {
            ret = njs_value_property_i64(vm, &z, i, retval);
            if (njs_slow_path(ret == NJS_ERROR)) {
                return NJS_ERROR;
            }

            (void) njs_string_prop(vm, &sv, retval);

            ret = njs_array_string_add(vm, array, sv.start, sv.size,
                                       sv.length);
            if (njs_slow_path(ret != NJS_OK)) {
                return ret;
            }

            if (array->length == limit) {
                if (njs_object_slots(&z)) {
                    njs_regexp_exec_result_free(vm, njs_array(&z));
                }

                goto done;
            }
        }

        if (njs_object_slots(&z)) {
            njs_regexp_exec_result_free(vm, njs_array(&z));
        }

        q = p;
    }

    end = &s.start[s.size];

    if (utf8 == NJS_STRING_UTF8) {
        start = (p < length) ? njs_string_utf8_offset(s.start, s.start + s.size,
                                                      p)
                             : end;

    } else {
        start = &s.start[p];
    }

    len = njs_string_calc_length(utf8, start, end - start);

    ret = njs_array_string_add(vm, array, start, end - start, len);
    if (njs_slow_path(ret != NJS_OK)) {
        return ret;
    }

    goto done;

single:

    value = njs_array_push(vm, array);
    if (njs_slow_path(value == NULL)) {
        return NJS_ERROR;
    }

    *value = *string;

done:

    njs_set_array(retval, array);

    return NJS_OK;
}


static const njs_object_prop_init_t  njs_regexp_constructor_properties[] =
{
    NJS_DECLARE_PROP_LENGTH(2),

    NJS_DECLARE_PROP_NAME("RegExp"),

    NJS_DECLARE_PROP_HANDLER(STRING_prototype, njs_object_prototype_create,
                             0, 0),
};


static const njs_object_init_t  njs_regexp_constructor_init = {
    njs_regexp_constructor_properties,
    njs_nitems(njs_regexp_constructor_properties),
};


static const njs_object_prop_init_t  njs_regexp_prototype_properties[] =
{
    NJS_DECLARE_PROP_HANDLER(STRING_constructor,
                             njs_object_prototype_create_constructor, 0,
                             NJS_OBJECT_PROP_VALUE_CW),

    NJS_DECLARE_PROP_GETTER(STRING_flags, njs_regexp_prototype_flags, 0),

    NJS_DECLARE_PROP_GETTER(STRING_global, njs_regexp_prototype_flag,
                            NJS_REGEX_GLOBAL),

    NJS_DECLARE_PROP_GETTER(STRING_ignoreCase, njs_regexp_prototype_flag,
                            NJS_REGEX_IGNORE_CASE),

    NJS_DECLARE_PROP_GETTER(STRING_multiline, njs_regexp_prototype_flag,
                            NJS_REGEX_MULTILINE),

    NJS_DECLARE_PROP_GETTER(STRING_source, njs_regexp_prototype_source, 0),

    NJS_DECLARE_PROP_GETTER(STRING_sticky, njs_regexp_prototype_flag,
                            NJS_REGEX_STICKY),

    NJS_DECLARE_PROP_NATIVE(STRING_toString,
                            njs_regexp_prototype_to_string, 0, 0),

    NJS_DECLARE_PROP_NATIVE(STRING_test, njs_regexp_prototype_test, 1, 0),

    NJS_DECLARE_PROP_NATIVE(STRING_exec, njs_regexp_prototype_exec, 1, 0),

    NJS_DECLARE_PROP_NATIVE(SYMBOL_replace,
                            njs_regexp_prototype_symbol_replace, 2, 0),

    NJS_DECLARE_PROP_NATIVE(SYMBOL_split,
                            njs_regexp_prototype_symbol_split, 2, 0),
};


static const njs_object_prop_init_t  njs_regexp_instance_properties[] =
{
    NJS_DECLARE_PROP_HANDLER(STRING_lastIndex,
                             njs_regexp_prototype_last_index, 0,
                             NJS_OBJECT_PROP_VALUE_W),
};


const njs_object_init_t  njs_regexp_instance_init = {
    njs_regexp_instance_properties,
    njs_nitems(njs_regexp_instance_properties),
};


static const njs_object_init_t  njs_regexp_prototype_init = {
    njs_regexp_prototype_properties,
    njs_nitems(njs_regexp_prototype_properties),
};


const njs_object_type_init_t  njs_regexp_type_init = {
   .constructor = njs_native_ctor(njs_regexp_constructor, 2, 0),
   .constructor_props = &njs_regexp_constructor_init,
   .prototype_props = &njs_regexp_prototype_init,
   .prototype_value = { .object = { .type = NJS_OBJECT } },
};
