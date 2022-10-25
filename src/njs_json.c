
/*
 * Copyright (C) Dmitry Volyntsev
 * Copyright (C) NGINX, Inc.
 */


#include <njs_main.h>


typedef struct {
    njs_vm_t                   *vm;
    njs_mp_t                   *pool;
    njs_uint_t                 depth;
    const u_char               *start;
    const u_char               *end;
} njs_json_parse_ctx_t;


typedef struct {
    njs_value_t                value;

    uint8_t                    written;       /* 1 bit */
    uint8_t                    array;         /* 1 bit */
    uint8_t                    fast_array;    /* 1 bit */

    int64_t                    index;
    int64_t                    length;
    njs_array_t                *keys;
    njs_value_t                *key;
    njs_value_t                prop;
} njs_json_state_t;


typedef struct {
    njs_value_t                retval;

    njs_vm_t                   *vm;

    njs_uint_t                 depth;
#define NJS_JSON_MAX_DEPTH     32
    njs_json_state_t           states[NJS_JSON_MAX_DEPTH];

    njs_value_t                replacer;
    njs_str_t                  space;
    u_char                     space_buf[16];
    njs_object_enum_type_t     keys_type;
} njs_json_stringify_t;


static const u_char *njs_json_parse_value(njs_json_parse_ctx_t *ctx,
    njs_value_t *value, const u_char *p);
static const u_char *njs_json_parse_object(njs_json_parse_ctx_t *ctx,
    njs_value_t *value, const u_char *p);
static const u_char *njs_json_parse_array(njs_json_parse_ctx_t *ctx,
    njs_value_t *value, const u_char *p);
static const u_char *njs_json_parse_string(njs_json_parse_ctx_t *ctx,
    njs_value_t *value, const u_char *p);
static const u_char *njs_json_parse_number(njs_json_parse_ctx_t *ctx,
    njs_value_t *value, const u_char *p);
njs_inline uint32_t njs_json_unicode(const u_char *p);
static const u_char *njs_json_skip_space(const u_char *start,
    const u_char *end);

static njs_int_t njs_json_internalize_property(njs_vm_t *vm,
    njs_function_t *reviver, njs_value_t *holder, njs_value_t *name,
    njs_int_t depth, njs_value_t *retval);
static void njs_json_parse_exception(njs_json_parse_ctx_t *ctx,
    const char *msg, const u_char *pos);

static njs_int_t njs_json_stringify_iterator(njs_vm_t *vm,
    njs_json_stringify_t *stringify, njs_value_t *value);
static njs_function_t *njs_object_to_json_function(njs_vm_t *vm,
    njs_value_t *value);
static njs_int_t njs_json_stringify_to_json(njs_json_stringify_t* stringify,
    njs_json_state_t *state, njs_value_t *key, njs_value_t *value);
static njs_int_t njs_json_stringify_replacer(njs_json_stringify_t* stringify,
    njs_json_state_t  *state, njs_value_t *key, njs_value_t *value);
static njs_int_t njs_json_stringify_array(njs_vm_t *vm,
    njs_json_stringify_t *stringify);

static njs_int_t njs_json_append_value(njs_vm_t *vm, njs_chb_t *chain,
    njs_value_t *value);
static void njs_json_append_string(njs_chb_t *chain, const njs_value_t *value,
    char quote);
static void njs_json_append_number(njs_chb_t *chain, const njs_value_t *value);

static njs_object_t *njs_json_wrap_value(njs_vm_t *vm, njs_value_t *wrapper,
    const njs_value_t *value);


static const njs_object_prop_t  njs_json_object_properties[];


static njs_int_t
njs_json_parse(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t unused)
{
    njs_int_t             ret;
    njs_value_t           *text, value, lvalue, wrapper;
    njs_object_t          *obj;
    const u_char          *p, *end;
    const njs_value_t     *reviver;
    njs_string_prop_t     string;
    njs_json_parse_ctx_t  ctx;

    text = njs_lvalue_arg(&lvalue, args, nargs, 1);

    if (njs_slow_path(!njs_is_string(text))) {
        ret = njs_value_to_string(vm, text, text);
        if (njs_slow_path(ret != NJS_OK)) {
            return ret;
        }
    }

    (void) njs_string_prop(&string, text);

    p = string.start;
    end = p + string.size;

    ctx.vm = vm;
    ctx.pool = vm->mem_pool;
    ctx.depth = NJS_JSON_MAX_DEPTH;
    ctx.start = string.start;
    ctx.end = end;

    p = njs_json_skip_space(p, end);
    if (njs_slow_path(p == end)) {
        njs_json_parse_exception(&ctx, "Unexpected end of input", p);
        return NJS_ERROR;
    }

    p = njs_json_parse_value(&ctx, &value, p);
    if (njs_slow_path(p == NULL)) {
        return NJS_ERROR;
    }

    p = njs_json_skip_space(p, end);
    if (njs_slow_path(p != end)) {
        njs_json_parse_exception(&ctx, "Unexpected token", p);
        return NJS_ERROR;
    }

    reviver = njs_arg(args, nargs, 2);

    if (njs_slow_path(njs_is_function(reviver))) {
        obj = njs_json_wrap_value(vm, &wrapper, &value);
        if (njs_slow_path(obj == NULL)) {
            return NJS_ERROR;
        }

        return njs_json_internalize_property(vm, njs_function(reviver),
                                             &wrapper,
                                             njs_value_arg(&njs_string_empty),
                                             0, &vm->retval);
    }

    vm->retval = value;

    return NJS_OK;
}


njs_int_t
njs_vm_json_parse(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs)
{
    njs_function_t  *parse;

    parse = njs_function(&njs_json_object_properties[1].u.value);

    return njs_vm_call(vm, parse, args, nargs);
}


static njs_int_t
njs_json_stringify(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t unused)
{
    size_t                length;
    int64_t               i64;
    njs_int_t             i;
    njs_int_t             ret;
    njs_value_t           *replacer, *space;
    const u_char          *p;
    njs_string_prop_t     prop;
    njs_json_stringify_t  *stringify, json_stringify;

    stringify = &json_stringify;

    stringify->vm = vm;
    stringify->depth = 0;
    stringify->keys_type = NJS_ENUM_STRING;

    replacer = njs_arg(args, nargs, 2);

    if (njs_is_function(replacer) || njs_is_array(replacer)) {
        stringify->replacer = *replacer;
        if (njs_is_array(replacer)) {
            ret = njs_json_stringify_array(vm, stringify);
            if (njs_slow_path(ret != NJS_OK)) {
                goto memory_error;
            }
        }

    } else {
        njs_set_undefined(&stringify->replacer);
    }

    space = njs_arg(args, nargs, 3);

    if (njs_is_object(space)) {
        if (njs_is_object_number(space)) {
            ret = njs_value_to_numeric(vm, space, space);
            if (njs_slow_path(ret != NJS_OK)) {
                return ret;
            }

        } else if (njs_is_object_string(space)) {
            ret = njs_value_to_string(vm, space, space);
            if (njs_slow_path(ret != NJS_OK)) {
                return ret;
            }
        }
    }

    switch (space->type) {
    case NJS_STRING:
        length = njs_string_prop(&prop, space);

        if (njs_is_byte_string(&prop)) {
            njs_internal_error(vm, "space argument cannot be"
                               " a byte string");
            return NJS_ERROR;
        }

        if (length > 10) {
            p = njs_string_offset(prop.start, prop.start + prop.size, 10);

        } else {
            p = prop.start + prop.size;
        }

        stringify->space.start = prop.start;
        stringify->space.length = p - prop.start;

        break;

    case NJS_NUMBER:
        i64 = njs_min(njs_number_to_integer(njs_number(space)), 10);

        if (i64 > 0) {
            stringify->space.length = i64;
            stringify->space.start = stringify->space_buf;

            for (i = 0; i < i64; i++) {
                stringify->space.start[i] = ' ';
            }

            break;
        }

        /* Fall through. */

    default:
        stringify->space.length = 0;

        break;
     }

    return njs_json_stringify_iterator(vm, stringify, njs_arg(args, nargs, 1));

memory_error:

    njs_memory_error(vm);

    return NJS_ERROR;
}


njs_int_t
njs_vm_json_stringify(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs)
{
    njs_function_t  *stringify;

    stringify = njs_function(&njs_json_object_properties[2].u.value);

    return njs_vm_call(vm, stringify, args, nargs);
}


static const u_char *
njs_json_parse_value(njs_json_parse_ctx_t *ctx, njs_value_t *value,
    const u_char *p)
{
    switch (*p) {
    case '{':
        return njs_json_parse_object(ctx, value, p);

    case '[':
        return njs_json_parse_array(ctx, value, p);

    case '"':
        return njs_json_parse_string(ctx, value, p);

    case 't':
        if (njs_fast_path(ctx->end - p >= 4 && memcmp(p, "true", 4) == 0)) {
            *value = njs_value_true;

            return p + 4;
        }

        goto error;

    case 'f':
        if (njs_fast_path(ctx->end - p >= 5 && memcmp(p, "false", 5) == 0)) {
            *value = njs_value_false;

            return p + 5;
        }

        goto error;

    case 'n':
        if (njs_fast_path(ctx->end - p >= 4 && memcmp(p, "null", 4) == 0)) {
            *value = njs_value_null;

            return p + 4;
        }

        goto error;
    }

    if (njs_fast_path(*p == '-' || (*p - '0') <= 9)) {
        return njs_json_parse_number(ctx, value, p);
    }

error:

    njs_json_parse_exception(ctx, "Unexpected token", p);

    return NULL;
}


static const u_char *
njs_json_parse_object(njs_json_parse_ctx_t *ctx, njs_value_t *value,
    const u_char *p)
{
    njs_int_t           ret;
    njs_object_t        *object;
    njs_value_t         prop_name, prop_value;
    njs_object_prop_t   *prop;
    njs_lvlhsh_query_t  lhq;

    if (njs_slow_path(--ctx->depth == 0)) {
        njs_json_parse_exception(ctx, "Nested too deep", p);
        return NULL;
    }

    object = njs_object_alloc(ctx->vm);
    if (njs_slow_path(object == NULL)) {
        goto memory_error;
    }

    prop = NULL;

    for ( ;; ) {
        p = njs_json_skip_space(p + 1, ctx->end);
        if (njs_slow_path(p == ctx->end)) {
            goto error_end;
        }

        if (*p != '"') {
            if (njs_fast_path(*p == '}')) {
                if (njs_slow_path(prop != NULL)) {
                    njs_json_parse_exception(ctx, "Trailing comma", p - 1);
                    return NULL;
                }

                break;
            }

            goto error_token;
        }

        p = njs_json_parse_string(ctx, &prop_name, p);
        if (njs_slow_path(p == NULL)) {
            /* The exception is set by the called function. */
            return NULL;
        }

        p = njs_json_skip_space(p, ctx->end);
        if (njs_slow_path(p == ctx->end || *p != ':')) {
            goto error_token;
        }

        p = njs_json_skip_space(p + 1, ctx->end);
        if (njs_slow_path(p == ctx->end)) {
            goto error_end;
        }

        p = njs_json_parse_value(ctx, &prop_value, p);
        if (njs_slow_path(p == NULL)) {
            /* The exception is set by the called function. */
            return NULL;
        }

        prop = njs_object_prop_alloc(ctx->vm, &prop_name, &prop_value, 1);
        if (njs_slow_path(prop == NULL)) {
            goto memory_error;
        }

        njs_string_get(&prop_name, &lhq.key);
        lhq.key_hash = njs_djb_hash(lhq.key.start, lhq.key.length);
        lhq.value = prop;
        lhq.replace = 1;
        lhq.pool = ctx->pool;
        lhq.proto = &njs_object_hash_proto;

        ret = njs_lvlhsh_insert(&object->hash, &lhq);
        if (njs_slow_path(ret != NJS_OK)) {
            njs_internal_error(ctx->vm, "lvlhsh insert/replace failed");
            return NULL;
        }

        p = njs_json_skip_space(p, ctx->end);
        if (njs_slow_path(p == ctx->end)) {
            goto error_end;
        }

        if (*p != ',') {
            if (njs_fast_path(*p == '}')) {
                break;
            }

            goto error_token;
        }
    }

    njs_set_object(value, object);

    ctx->depth++;

    return p + 1;

error_token:

    njs_json_parse_exception(ctx, "Unexpected token", p);

    return NULL;

error_end:

    njs_json_parse_exception(ctx, "Unexpected end of input", p);

    return NULL;

memory_error:

    njs_memory_error(ctx->vm);

    return NULL;
}


static const u_char *
njs_json_parse_array(njs_json_parse_ctx_t *ctx, njs_value_t *value,
    const u_char *p)
{
    njs_int_t    ret;
    njs_bool_t   empty;
    njs_array_t  *array;
    njs_value_t  element;

    if (njs_slow_path(--ctx->depth == 0)) {
        njs_json_parse_exception(ctx, "Nested too deep", p);
        return NULL;
    }

    array = njs_array_alloc(ctx->vm, 0, 0, NJS_ARRAY_SPARE);
    if (njs_slow_path(array == NULL)) {
        return NULL;
    }

    empty = 1;

    for ( ;; ) {
        p = njs_json_skip_space(p + 1, ctx->end);
        if (njs_slow_path(p == ctx->end)) {
            goto error_end;
        }

        if (*p == ']') {
            if (njs_slow_path(!empty)) {
                njs_json_parse_exception(ctx, "Trailing comma", p - 1);
                return NULL;
            }

            break;
        }

        p = njs_json_parse_value(ctx, &element, p);
        if (njs_slow_path(p == NULL)) {
            return NULL;
        }

        ret = njs_array_add(ctx->vm, array, &element);
        if (njs_slow_path(ret != NJS_OK)) {
            return NULL;
        }

        empty = 0;

        p = njs_json_skip_space(p, ctx->end);
        if (njs_slow_path(p == ctx->end)) {
            goto error_end;
        }

        if (*p != ',') {
            if (njs_fast_path(*p == ']')) {
                break;
            }

            goto error_token;
        }
    }

    njs_set_array(value, array);

    ctx->depth++;

    return p + 1;

error_token:

    njs_json_parse_exception(ctx, "Unexpected token", p);

    return NULL;

error_end:

    njs_json_parse_exception(ctx, "Unexpected end of input", p);

    return NULL;
}


static const u_char *
njs_json_parse_string(njs_json_parse_ctx_t *ctx, njs_value_t *value,
    const u_char *p)
{
    u_char        ch, *s, *dst;
    size_t        size, surplus;
    ssize_t       length;
    uint32_t      utf, utf_low;
    njs_int_t     ret;
    const u_char  *start, *last;

    enum {
        sw_usual = 0,
        sw_escape,
        sw_encoded1,
        sw_encoded2,
        sw_encoded3,
        sw_encoded4,
    } state;

    start = p + 1;

    dst = NULL;
    state = 0;
    surplus = 0;

    for (p = start; p < ctx->end; p++) {
        ch = *p;

        switch (state) {

        case sw_usual:

            if (ch == '"') {
                break;
            }

            if (ch == '\\') {
                state = sw_escape;
                continue;
            }

            if (njs_fast_path(ch >= ' ')) {
                continue;
            }

            njs_json_parse_exception(ctx, "Forbidden source char", p);

            return NULL;

        case sw_escape:

            switch (ch) {
            case '"':
            case '\\':
            case '/':
            case 'n':
            case 'r':
            case 't':
            case 'b':
            case 'f':
                surplus++;
                state = sw_usual;
                continue;

            case 'u':
                /*
                 * Basic unicode 6 bytes "\uXXXX" in JSON
                 * and up to 3 bytes in UTF-8.
                 *
                 * Surrogate pair: 12 bytes "\uXXXX\uXXXX" in JSON
                 * and 3 or 4 bytes in UTF-8.
                 */
                surplus += 3;
                state = sw_encoded1;
                continue;
            }

            njs_json_parse_exception(ctx, "Unknown escape char", p);

            return NULL;

        case sw_encoded1:
        case sw_encoded2:
        case sw_encoded3:
        case sw_encoded4:

            if (njs_fast_path((ch >= '0' && ch <= '9')
                              || (ch >= 'A' && ch <= 'F')
                              || (ch >= 'a' && ch <= 'f')))
            {
                state = (state == sw_encoded4) ? sw_usual : state + 1;
                continue;
            }

            njs_json_parse_exception(ctx, "Invalid Unicode escape sequence", p);

            return NULL;
        }

        break;
    }

    if (njs_slow_path(p == ctx->end)) {
        njs_json_parse_exception(ctx, "Unexpected end of input", p);
        return NULL;
    }

    /* Points to the ending quote mark. */
    last = p;

    size = last - start - surplus;

    if (surplus != 0) {
        p = start;

        dst = njs_mp_alloc(ctx->pool, size);
        if (njs_slow_path(dst == NULL)) {
            njs_memory_error(ctx->vm);;
            return NULL;
        }

        s = dst;

        do {
            ch = *p++;

            if (ch != '\\') {
                *s++ = ch;
                continue;
            }

            ch = *p++;

            switch (ch) {
            case '"':
            case '\\':
            case '/':
                *s++ = ch;
                continue;

            case 'n':
                *s++ = '\n';
                continue;

            case 'r':
                *s++ = '\r';
                continue;

            case 't':
                *s++ = '\t';
                continue;

            case 'b':
                *s++ = '\b';
                continue;

            case 'f':
                *s++ = '\f';
                continue;
            }

            /* "\uXXXX": Unicode escape sequence. */

            utf = njs_json_unicode(p);
            p += 4;

            if (njs_surrogate_any(utf)) {

                if (utf > 0xdbff || p[0] != '\\' || p[1] != 'u') {
                    s = njs_utf8_encode(s, NJS_UNICODE_REPLACEMENT);
                    continue;
                }

                p += 2;

                utf_low = njs_json_unicode(p);
                p += 4;

                if (njs_fast_path(njs_surrogate_trailing(utf_low))) {
                    utf = njs_surrogate_pair(utf, utf_low);

                } else if (njs_surrogate_leading(utf_low)) {
                    utf = NJS_UNICODE_REPLACEMENT;
                    s = njs_utf8_encode(s, NJS_UNICODE_REPLACEMENT);

                } else {
                    utf = utf_low;
                    s = njs_utf8_encode(s, NJS_UNICODE_REPLACEMENT);
                }
            }

            s = njs_utf8_encode(s, utf);

        } while (p != last);

        size = s - dst;
        start = dst;
    }

    length = njs_utf8_length(start, size);
    if (njs_slow_path(length < 0)) {
        length = 0;
    }

    ret = njs_string_new(ctx->vm, value, (u_char *) start, size, length);
    if (njs_slow_path(ret != NJS_OK)) {
        return NULL;
    }

    if (dst != NULL) {
        njs_mp_free(ctx->pool, dst);
    }

    return last + 1;
}


static const u_char *
njs_json_parse_number(njs_json_parse_ctx_t *ctx, njs_value_t *value,
    const u_char *p)
{
    double        num;
    njs_int_t     sign;
    const u_char  *start;

    sign = 1;

    if (*p == '-') {
        if (p + 1 == ctx->end) {
            goto error;
        }

        p++;
        sign = -1;
    }

    start = p;
    num = njs_number_dec_parse(&p, ctx->end, 0);
    if (p != start) {
        njs_set_number(value, sign * num);
        return p;
    }

error:

    njs_json_parse_exception(ctx, "Unexpected number", p);

    return NULL;
}


njs_inline uint32_t
njs_json_unicode(const u_char *p)
{
    u_char      c;
    uint32_t    utf;
    njs_uint_t  i;

    utf = 0;

    for (i = 0; i < 4; i++) {
        utf <<= 4;
        c = p[i] | 0x20;
        c -= '0';
        if (c > 9) {
            c += '0' - 'a' + 10;
        }

        utf |= c;
    }

    return utf;
}


static const u_char *
njs_json_skip_space(const u_char *start, const u_char *end)
{
    const u_char  *p;

    for (p = start; njs_fast_path(p != end); p++) {

        switch (*p) {
        case ' ':
        case '\t':
        case '\r':
        case '\n':
            continue;
        }

        break;
    }

    return p;
}


static njs_int_t
njs_json_internalize_property(njs_vm_t *vm, njs_function_t *reviver,
    njs_value_t *holder, njs_value_t *name, njs_int_t depth,
    njs_value_t *retval)
{
    int64_t       k, length;
    njs_int_t     ret;
    njs_value_t   val, new_elem, index;
    njs_value_t   arguments[3];
    njs_array_t   *keys;

    if (njs_slow_path(depth++ >= NJS_JSON_MAX_DEPTH)) {
        njs_type_error(vm, "Nested too deep or a cyclic structure");
        return NJS_ERROR;
    }

    ret = njs_value_property(vm, holder, name, &val);
    if (njs_slow_path(ret == NJS_ERROR)) {
        return NJS_ERROR;
    }

    keys = NULL;

    if (njs_is_object(&val)) {
        if (!njs_is_array(&val)) {
            keys = njs_array_keys(vm, &val, 0);
            if (njs_slow_path(keys == NULL)) {
                return NJS_ERROR;
            }

            for (k = 0; k < keys->length; k++) {
                ret = njs_json_internalize_property(vm, reviver, &val,
                                                    &keys->start[k], depth,
                                                    &new_elem);

                if (njs_slow_path(ret != NJS_OK)) {
                    goto done;
                }

                if (njs_is_undefined(&new_elem)) {
                    ret = njs_value_property_delete(vm, &val, &keys->start[k],
                                                    NULL, 0);

                } else {
                    ret = njs_value_property_set(vm, &val, &keys->start[k],
                                                 &new_elem);
                }

                if (njs_slow_path(ret == NJS_ERROR)) {
                    goto done;
                }
            }

        } else {

            ret = njs_object_length(vm, &val, &length);
            if (njs_slow_path(ret == NJS_ERROR)) {
                return NJS_ERROR;
            }

            for (k = 0; k < length; k++) {
                ret = njs_int64_to_string(vm, &index, k);
                if (njs_slow_path(ret != NJS_OK)) {
                    return NJS_ERROR;
                }

                ret = njs_json_internalize_property(vm, reviver, &val, &index,
                                                    depth, &new_elem);

                if (njs_slow_path(ret != NJS_OK)) {
                    return NJS_ERROR;
                }

                if (njs_is_undefined(&new_elem)) {
                    ret = njs_value_property_delete(vm, &val, &index, NULL, 0);

                } else {
                    ret = njs_value_property_set(vm, &val, &index, &new_elem);
                }

                if (njs_slow_path(ret == NJS_ERROR)) {
                    return NJS_ERROR;
                }
            }
        }
    }

    njs_value_assign(&arguments[0], holder);
    njs_value_assign(&arguments[1], name);
    njs_value_assign(&arguments[2], &val);

    ret = njs_function_apply(vm, reviver, arguments, 3, retval);

done:

    if (keys != NULL) {
        njs_array_destroy(vm, keys);
    }

    return ret;
}


static void
njs_json_parse_exception(njs_json_parse_ctx_t *ctx, const char *msg,
    const u_char *pos)
{
    ssize_t  length;

    length = njs_utf8_length(ctx->start, pos - ctx->start);
    if (njs_slow_path(length < 0)) {
        length = 0;
    }

    njs_syntax_error(ctx->vm, "%s at position %z", msg, length);
}


static njs_json_state_t *
njs_json_push_stringify_state(njs_vm_t *vm, njs_json_stringify_t *stringify,
    njs_value_t *value)
{
    njs_int_t         ret;
    njs_json_state_t  *state;

    if (njs_slow_path(stringify->depth >= NJS_JSON_MAX_DEPTH)) {
        njs_type_error(vm, "Nested too deep or a cyclic structure");
        return NULL;
    }

    state = &stringify->states[stringify->depth++];
    state->value = *value;
    state->array = njs_is_array(value);
    state->fast_array = njs_is_fast_array(value);
    state->index = 0;
    state->written = 0;
    state->keys = NULL;
    state->key = NULL;

    if (state->fast_array) {
        state->length = njs_array_len(value);
    }

    if (njs_is_array(&stringify->replacer)) {
        state->keys = njs_array(&stringify->replacer);

    } else if (state->array) {
        state->keys = njs_array_keys(vm, value, 1);
        if (njs_slow_path(state->keys == NULL)) {
            return NULL;
        }

        ret = njs_object_length(vm, &state->value, &state->length);
        if (njs_slow_path(ret == NJS_ERROR)) {
            return NULL;
        }

    } else {
        state->keys = njs_value_own_enumerate(vm, value, NJS_ENUM_KEYS,
                                              stringify->keys_type, 0);

        if (njs_slow_path(state->keys == NULL)) {
            return NULL;
        }
    }

    return state;
}


njs_inline njs_json_state_t *
njs_json_pop_stringify_state(njs_json_stringify_t *stringify)
{
    njs_json_state_t  *state;

    state = &stringify->states[stringify->depth - 1];
    if (!njs_is_array(&stringify->replacer) && state->keys != NULL) {
        njs_array_destroy(stringify->vm, state->keys);
        state->keys = NULL;
    }

    if (stringify->depth > 1) {
        stringify->depth--;
        state = &stringify->states[stringify->depth - 1];
        state->written = 1;
        return state;
    }

    return NULL;
}


njs_inline njs_bool_t
njs_json_is_object(const njs_value_t *value)
{
    if (!njs_is_object(value)) {
        return 0;
    }

    if (njs_is_function(value)) {
        return 0;
    }

    if (njs_is_object_value(value)) {
        switch (njs_object_value(value)->type) {
        case NJS_BOOLEAN:
        case NJS_NUMBER:
        case NJS_STRING:
            return 0;

        default:
            break;
        }
    }

    return 1;
}


njs_inline void
njs_json_stringify_indent(njs_json_stringify_t *stringify, njs_chb_t *chain,
    njs_int_t times)
{
    njs_int_t  i;

    if (stringify->space.length != 0) {
        times += stringify->depth;
        njs_chb_append(chain,"\n", 1);
        for (i = 0; i < (times - 1); i++) {
            njs_chb_append_str(chain, &stringify->space);
        }
    }
}


njs_inline njs_bool_t
njs_json_stringify_done(njs_json_state_t *state, njs_bool_t array)
{
    return array ? state->index >= state->length
                 : state->index >= state->keys->length;
}


static njs_int_t
njs_json_stringify_iterator(njs_vm_t *vm, njs_json_stringify_t *stringify,
    njs_value_t *object)
{
    u_char            *p;
    int64_t           size, length;
    njs_int_t         ret;
    njs_chb_t         chain;
    njs_value_t       *key, *value, index, wrapper;
    njs_object_t      *obj;
    njs_json_state_t  *state;

    obj = njs_json_wrap_value(vm, &wrapper, object);
    if (njs_slow_path(obj == NULL)) {
        goto memory_error;
    }

    state = njs_json_push_stringify_state(vm, stringify, &wrapper);
    if (njs_slow_path(state == NULL)) {
        goto memory_error;
    }

    njs_chb_init(&chain, vm->mem_pool);

    for ( ;; ) {
        if (state->index == 0) {
            njs_chb_append(&chain, state->array ? "[" : "{", 1);
            njs_json_stringify_indent(stringify, &chain, 0);
        }

        if (njs_json_stringify_done(state, state->array)) {
            njs_json_stringify_indent(stringify, &chain, -1);
            njs_chb_append(&chain, state->array ? "]" : "}", 1);

            state = njs_json_pop_stringify_state(stringify);
            if (state == NULL) {
                goto done;
            }

            continue;
        }

        value = &stringify->retval;

        if (state->array) {
            njs_set_number(&index, state->index);
            key = &index;

        } else {
            key = &state->keys->start[state->index];
        }

        ret = njs_value_property(vm, &state->value, key, value);
        if (njs_slow_path(ret == NJS_ERROR)) {
            return ret;
        }

        if (state->array && ret == NJS_DECLINED) {
            njs_set_null(value);
        }

        ret = njs_json_stringify_to_json(stringify, state, key, value);
        if (njs_slow_path(ret != NJS_OK)) {
            return ret;
        }

        ret = njs_json_stringify_replacer(stringify, state, key, value);
        if (njs_slow_path(ret != NJS_OK)) {
            return ret;
        }

        state->index++;

        if (!state->array
            && (njs_is_undefined(value)
                || njs_is_symbol(value)
                || njs_is_function(value)
                || !njs_is_valid(value)))
        {
            continue;
        }

        if (state->written) {
            njs_chb_append_literal(&chain,",");
            njs_json_stringify_indent(stringify, &chain, 0);
        }

        state->written = 1;

        if (!state->array) {
            njs_json_append_string(&chain, key, '\"');
            njs_chb_append_literal(&chain,":");
            if (stringify->space.length != 0) {
                njs_chb_append_literal(&chain," ");
            }
        }

        if (njs_json_is_object(value)) {
            state = njs_json_push_stringify_state(vm, stringify, value);
            if (njs_slow_path(state == NULL)) {
                return NJS_ERROR;
            }

            continue;
        }

        ret = njs_json_append_value(vm, &chain, value);
        if (njs_slow_path(ret != NJS_OK)) {
            return ret;
        }
    }

done:

    /*
     * The value to stringify is wrapped as '{"": value}'.
     * Stripping the wrapper's data.
     */

    njs_chb_drain(&chain, njs_length("{\"\":"));
    njs_chb_drop(&chain, njs_length("}"));

    if (stringify->space.length != 0) {
        njs_chb_drain(&chain, njs_length("\n "));
        njs_chb_drop(&chain, njs_length("\n"));
    }

    size = njs_chb_size(&chain);
    if (njs_slow_path(size < 0)) {
        njs_chb_destroy(&chain);
        goto memory_error;
    }

    if (size == 0) {
        njs_set_undefined(&vm->retval);
        goto release;
    }

    length = njs_chb_utf8_length(&chain);

    p = njs_string_alloc(vm, &vm->retval, size, length);
    if (njs_slow_path(p == NULL)) {
        njs_chb_destroy(&chain);
        goto memory_error;
    }

    njs_chb_join_to(&chain, p);

release:

    njs_chb_destroy(&chain);

    return NJS_OK;

memory_error:

    njs_memory_error(vm);

    return NJS_ERROR;
}


static njs_function_t *
njs_object_to_json_function(njs_vm_t *vm, njs_value_t *value)
{
    njs_int_t           ret;
    njs_value_t         retval;
    njs_lvlhsh_query_t  lhq;

    static const njs_value_t  to_json_string = njs_string("toJSON");

    njs_object_property_init(&lhq, &to_json_string, NJS_TO_JSON_HASH);

    ret = njs_object_property(vm, value, &lhq, &retval);

    if (njs_slow_path(ret == NJS_ERROR)) {
        return NULL;
    }

    return njs_is_function(&retval) ? njs_function(&retval) : NULL;
}


static njs_int_t
njs_json_stringify_to_json(njs_json_stringify_t* stringify,
    njs_json_state_t *state, njs_value_t *key, njs_value_t *value)
{
    njs_value_t     arguments[2];
    njs_function_t  *to_json;

    if (!njs_is_object(value)) {
        return NJS_OK;
    }

    to_json = njs_object_to_json_function(stringify->vm, value);
    if (to_json == NULL) {
        return NJS_OK;
    }

    arguments[0] = *value;

    if (!state->array) {
        arguments[1] = *key;

    } else {
        njs_uint32_to_string(&arguments[1], state->index);
    }

    return njs_function_apply(stringify->vm, to_json, arguments, 2,
                              &stringify->retval);
}


static njs_int_t
njs_json_stringify_replacer(njs_json_stringify_t* stringify,
    njs_json_state_t *state, njs_value_t *key, njs_value_t *value)
{
    njs_value_t  arguments[3];

    if (!njs_is_function(&stringify->replacer)) {
        return NJS_OK;
    }

    arguments[0] = state->value;
    arguments[2] = *value;

    if (!state->array) {
        arguments[1] = *key;

    } else {
        njs_uint32_to_string(&arguments[1], state->index);
    }

    return njs_function_apply(stringify->vm, njs_function(&stringify->replacer),
                              arguments, 3, &stringify->retval);
}


static njs_int_t
njs_json_stringify_array(njs_vm_t *vm, njs_json_stringify_t *stringify)
{
    njs_int_t    ret;
    int64_t      i, k, length;
    njs_value_t  *value, *item;
    njs_array_t  *properties;

    ret = njs_object_length(vm, &stringify->replacer, &length);
    if (njs_slow_path(ret != NJS_OK)) {
        return ret;
    }

    properties = njs_array_alloc(vm, 1, 0, NJS_ARRAY_SPARE);
    if (njs_slow_path(properties == NULL)) {
        return NJS_ERROR;
    }

    item = njs_array_push(vm, properties);
    njs_value_assign(item, &njs_string_empty);

    for (i = 0; i < length; i++) {
        ret = njs_value_property_i64(vm, &stringify->replacer, i,
                                     &stringify->retval);
        if (njs_slow_path(ret == NJS_ERROR)) {
            return ret;
        }

        value = &stringify->retval;

        switch (value->type) {
        case NJS_STRING:
            break;

        case NJS_NUMBER:
            ret = njs_number_to_string(vm, value, value);
            if (njs_slow_path(ret != NJS_OK)) {
                return NJS_ERROR;
            }

            break;

        case NJS_OBJECT_VALUE:
            switch (njs_object_value(value)->type) {
            case NJS_NUMBER:
            case NJS_STRING:
                ret = njs_value_to_string(vm, value, value);
                if (njs_slow_path(ret != NJS_OK)) {
                    return NJS_ERROR;
                }

                break;

            default:
                continue;
            }

            break;

        default:
            continue;
        }

        for (k = 0; k < properties->length; k++) {
            if (njs_values_strict_equal(value, &properties->start[k]) == 1) {
                break;
            }
        }

        if (k == properties->length) {
            item = njs_array_push(vm, properties);
            if (njs_slow_path(item == NULL)) {
                return NJS_ERROR;
            }

            njs_value_assign(item, value);
        }
    }

    njs_set_array(&stringify->replacer, properties);

    return NJS_OK;
}


static njs_int_t
njs_json_append_value(njs_vm_t *vm, njs_chb_t *chain, njs_value_t *value)
{
    njs_int_t  ret;

    if (njs_is_object_value(value)) {
        switch (njs_object_value(value)->type) {
        case NJS_NUMBER:
            ret = njs_value_to_numeric(vm, value, value);
            if (njs_slow_path(ret != NJS_OK)) {
                return ret;
            }

            break;

        case NJS_BOOLEAN:
            njs_value_assign(value, njs_object_value(value));
            break;

        case NJS_STRING:
            ret = njs_value_to_string(vm, value, value);
             if (njs_slow_path(ret != NJS_OK)) {
                 return ret;
             }

            break;

        default:
            break;
        }
    }

    switch (value->type) {
    case NJS_STRING:
        njs_json_append_string(chain, value, '\"');
        break;

    case NJS_NUMBER:
        njs_json_append_number(chain, value);
        break;

    case NJS_BOOLEAN:
        if (njs_is_true(value)) {
            njs_chb_append_literal(chain, "true");

        } else {
            njs_chb_append_literal(chain, "false");
        }

        break;

    case NJS_UNDEFINED:
    case NJS_NULL:
    case NJS_SYMBOL:
    case NJS_INVALID:
    case NJS_FUNCTION:
    default:
        njs_chb_append_literal(chain, "null");
    }

    return NJS_OK;
}


static void
njs_json_append_string(njs_chb_t *chain, const njs_value_t *value, char quote)
{
    size_t             size;
    u_char             c, *dst, *dst_end;
    njs_bool_t         utf8;
    const u_char       *p, *end;
    njs_string_prop_t  string;

    static char  hex2char[16] = { '0', '1', '2', '3', '4', '5', '6', '7',
                                  '8', '9', 'a', 'b', 'c', 'd', 'e', 'f' };

    (void) njs_string_prop(&string, value);

    p = string.start;
    end = p + string.size;
    utf8 = (string.length != 0 && string.length != string.size);

    size = njs_max(string.size + 2, 7);
    dst = njs_chb_reserve(chain, size);
    if (njs_slow_path(dst == NULL)) {
        return;
    }

    dst_end = dst + size;

    *dst++ = quote;
    njs_chb_written(chain, 1);

    while (p < end) {
        if (njs_slow_path(dst_end <= dst + njs_length("\\uXXXX"))) {
            size = njs_max(end - p + 1, 6);
            dst = njs_chb_reserve(chain, size);
            if (njs_slow_path(dst == NULL)) {
                return;
            }

            dst_end = dst + size;
        }

        if (njs_slow_path(*p < ' '
                          || *p == '\\'
                          || (*p == '\"' && quote == '\"')))
        {
            c = (u_char) *p++;
            *dst++ = '\\';
            njs_chb_written(chain, 2);

            switch (c) {
            case '\\':
                *dst++ = '\\';
                break;
            case '"':
                *dst++ = '\"';
                break;
            case '\r':
                *dst++ = 'r';
                break;
            case '\n':
                *dst++ = 'n';
                break;
            case '\t':
                *dst++ = 't';
                break;
            case '\b':
                *dst++ = 'b';
                break;
            case '\f':
                *dst++ = 'f';
                break;
            default:
                *dst++ = 'u';
                *dst++ = '0';
                *dst++ = '0';
                *dst++ = hex2char[(c & 0xf0) >> 4];
                *dst++ = hex2char[c & 0x0f];
                njs_chb_written(chain, 4);
            }

            continue;
        }

        if (utf8) {
            /* UTF-8 string. */
            dst = njs_utf8_copy(dst, &p, end);

        } else {
            /* Byte or ASCII string. */
            *dst++ = *p++;
        }

        njs_chb_written(chain, dst - chain->last->pos);
    }

    njs_chb_append(chain, &quote, 1);
}


static void
njs_json_append_number(njs_chb_t *chain, const njs_value_t *value)
{
    u_char  *p;
    size_t  size;
    double  num;

    num = njs_number(value);

    if (isnan(num) || isinf(num)) {
        njs_chb_append_literal(chain, "null");

    } else {
        p = njs_chb_reserve(chain, 64);
        if (njs_slow_path(p == NULL)) {
            return;
        }

        size = njs_dtoa(num, (char *) p);

        njs_chb_written(chain, size);
    }
}


/*
 * Wraps a value as '{"": <value>}'.
 */
static njs_object_t *
njs_json_wrap_value(njs_vm_t *vm, njs_value_t *wrapper,
    const njs_value_t *value)
{
    njs_int_t           ret;
    njs_object_prop_t   *prop;
    njs_lvlhsh_query_t  lhq;

    wrapper->data.u.object = njs_object_alloc(vm);
    if (njs_slow_path(njs_object(wrapper) == NULL)) {
        return NULL;
    }

    wrapper->type = NJS_OBJECT;
    wrapper->data.truth = 1;

    lhq.replace = 0;
    lhq.proto = &njs_object_hash_proto;
    lhq.pool = vm->mem_pool;
    lhq.key = njs_str_value("");
    lhq.key_hash = NJS_DJB_HASH_INIT;

    prop = njs_object_prop_alloc(vm, &njs_string_empty, value, 1);
    if (njs_slow_path(prop == NULL)) {
        return NULL;
    }

    lhq.value = prop;

    ret = njs_lvlhsh_insert(njs_object_hash(wrapper), &lhq);
    if (njs_slow_path(ret != NJS_OK)) {
        return NULL;
    }

    return wrapper->data.u.object;
}


static const njs_object_prop_t  njs_json_object_properties[] =
{
    {
        .type = NJS_PROPERTY,
        .name = njs_wellknown_symbol(NJS_SYMBOL_TO_STRING_TAG),
        .u.value = njs_string("JSON"),
        .configurable = 1,
    },

    NJS_DECLARE_PROP_NATIVE("parse", njs_json_parse, 2, 0),

    NJS_DECLARE_PROP_NATIVE("stringify", njs_json_stringify, 3, 0),
};


const njs_object_init_t  njs_json_object_init = {
    njs_json_object_properties,
    njs_nitems(njs_json_object_properties),
};


static njs_int_t
njs_dump_terminal(njs_json_stringify_t *stringify, njs_chb_t *chain,
    njs_value_t *value, njs_uint_t console)
{
    njs_str_t          str;
    njs_int_t          ret;
    njs_value_t        str_val, tag;
    njs_typed_array_t  *array;
    njs_string_prop_t  string;

    static const njs_value_t  name_string = njs_string("name");

    njs_int_t   (*to_string)(njs_vm_t *, njs_value_t *, const njs_value_t *);

    switch (value->type) {
    case NJS_NULL:
        njs_chb_append_literal(chain, "null");
        break;

    case NJS_UNDEFINED:
        njs_chb_append_literal(chain, "undefined");
        break;

    case NJS_BOOLEAN:
        if (njs_is_true(value)) {
            njs_chb_append_literal(chain, "true");

        } else {
            njs_chb_append_literal(chain, "false");
        }

        break;

    case NJS_STRING:
        njs_string_get(value, &str);

        if (!console || stringify->depth != 0) {
            njs_json_append_string(chain, value, '\'');
            return NJS_OK;
        }

        njs_chb_append_str(chain, &str);

        break;

    case NJS_SYMBOL:
        ret = njs_symbol_descriptive_string(stringify->vm, &str_val, value);
        if (njs_slow_path(ret != NJS_OK)) {
            return NJS_ERROR;
        }

        njs_string_get(&str_val, &str);
        njs_chb_append_str(chain, &str);

        break;

    case NJS_INVALID:
        /* Fall through. */
        break;

    case NJS_OBJECT_VALUE:
        value = njs_object_value(value);

        switch (value->type) {
        case NJS_BOOLEAN:
            if (njs_is_true(value)) {
                njs_chb_append_literal(chain, "[Boolean: true]");

            } else {
                njs_chb_append_literal(chain, "[Boolean: false]");
            }

            break;

        case NJS_NUMBER:
            if (njs_slow_path(njs_number(value) == 0.0
                              && signbit(njs_number(value))))
            {

                njs_chb_append_literal(chain, "[Number: -0]");
                break;
            }

            ret = njs_number_to_string(stringify->vm, &str_val, value);
            if (njs_slow_path(ret != NJS_OK)) {
                return NJS_ERROR;
            }

            njs_string_get(&str_val, &str);
            njs_chb_sprintf(chain, 16 + str.length, "[Number: %V]", &str);
            break;

        case NJS_SYMBOL:
            ret = njs_symbol_descriptive_string(stringify->vm, &str_val, value);
            if (njs_slow_path(ret != NJS_OK)) {
                return NJS_ERROR;
            }

            njs_string_get(&str_val, &str);
            njs_chb_sprintf(chain, 16 + str.length, "[Symbol: %V]", &str);

            break;

        case NJS_STRING:
        default:
            njs_chb_append_literal(chain, "[String: ");
            njs_json_append_string(chain, value, '\'');
            njs_chb_append_literal(chain, "]");
            break;
        }

        break;

    case NJS_FUNCTION:
        ret = njs_value_property(stringify->vm, value,
                                 njs_value_arg(&name_string), &tag);
        if (njs_slow_path(ret == NJS_ERROR)) {
            return ret;
        }

        if (njs_is_string(&tag)) {
            njs_string_get(&tag, &str);

        } else if (njs_function(value)->native) {
            str = njs_str_value("native");

        } else {
            str = njs_str_value("");
        }

        if (str.length != 0) {
            njs_chb_sprintf(chain, 32 + str.length, "[Function: %V]", &str);

        } else {
            njs_chb_append_literal(chain, "[Function]");
        }

        break;

    case NJS_TYPED_ARRAY:
        array = njs_typed_array(value);
        ret = njs_object_string_tag(stringify->vm, value, &tag);
        if (njs_slow_path(ret == NJS_ERROR)) {
            return ret;
        }

        if (ret == NJS_OK) {
            (void) njs_string_prop(&string, &tag);
            njs_chb_append(chain, string.start, string.size);
            njs_chb_append_literal(chain, " ");
        }

        njs_chb_append_literal(chain, "[");

        (void) njs_typed_array_to_chain(stringify->vm, chain, array, NULL);

        njs_chb_append_literal(chain, "]");

        break;

    case NJS_NUMBER:
        if (njs_slow_path(njs_number(value) == 0.0
                          && signbit(njs_number(value))))
        {

            njs_chb_append_literal(chain, "-0");
            break;
        }

        /* Fall through. */

    case NJS_OBJECT:
    case NJS_REGEXP:
    case NJS_DATE:

        switch (value->type) {
        case NJS_NUMBER:
            to_string = njs_number_to_string;
            break;

        case NJS_REGEXP:
            to_string = njs_regexp_to_string;
            break;

        case NJS_DATE:
            to_string = njs_date_to_string;
            break;

        default:
            to_string = njs_error_to_string;
        }

        ret = to_string(stringify->vm, &str_val, value);
        if (njs_slow_path(ret != NJS_OK)) {
            return NJS_ERROR;
        }

        njs_string_get(&str_val, &str);
        njs_chb_append_str(chain, &str);

        break;

    default:

        njs_chb_sprintf(chain, 64, "[Unknown value type:%uD]", value->type);
    }

    return NJS_OK;
}


njs_inline njs_bool_t
njs_dump_is_recursive(const njs_value_t *value)
{
    return (value->type == NJS_OBJECT && !njs_object(value)->error_data)
           || (value->type == NJS_ARRAY)
           || (value->type >= NJS_OBJECT_SPECIAL_MAX
               && !njs_is_object_primitive(value));
}


njs_inline njs_int_t
njs_dump_visited(njs_vm_t *vm, njs_json_stringify_t *stringify,
    const njs_value_t *value)
{
    njs_int_t  depth;

    depth = stringify->depth - 1;

    for (; depth >= 0; depth--) {
        if (njs_values_same(&stringify->states[depth].value, value)) {
            return 1;
        }
    }

    return 0;
}


njs_inline njs_bool_t
njs_dump_empty(njs_json_stringify_t *stringify, njs_json_state_t *state,
    njs_chb_t *chain, njs_bool_t sep_position)
{
    double   key, prev;
    int64_t  diff;

    if (!state->array) {
        return 0;
    }

    if (sep_position) {
        key = njs_key_to_index(state->key);
        prev = (state->index > 1) ? njs_key_to_index(&state->key[-1]) : -1;

    } else {
        key = state->length;
        if (state->key == NULL) {
            prev = -1;

        } else {
            prev = (state->index > 0) ? njs_key_to_index(state->key) : -1;
        }
    }

    if (isnan(key)) {
        key = state->length;
    }

    diff = key - prev;

    if (diff > 1) {
        if (sep_position == 0 && state->keys->length) {
            if (prev != -1) {
                njs_chb_append_literal(chain, ",");
                njs_json_stringify_indent(stringify, chain, 1);
            }
        }

        if (diff - 1 == 1) {
            njs_chb_sprintf(chain, 64, "<empty>");

        } else {
            njs_chb_sprintf(chain, 64, "<%L empty items>", diff - 1);
        }

        state->written = 1;

        if (sep_position == 1 && state->keys->length) {
            njs_chb_append_literal(chain, ",");
            njs_json_stringify_indent(stringify, chain, 1);
        }
    }

    return 1;
}


static const njs_value_t  string_get = njs_string("[Getter]");
static const njs_value_t  string_set = njs_string("[Setter]");
static const njs_value_t  string_get_set = njs_long_string("[Getter/Setter]");


njs_int_t
njs_vm_value_dump(njs_vm_t *vm, njs_str_t *retval, njs_value_t *value,
    njs_uint_t console, njs_uint_t indent)
{
    njs_int_t             ret;
    njs_chb_t             chain;
    njs_str_t             str;
    njs_value_t           *key, *val, tag;
    njs_json_state_t      *state;
    njs_string_prop_t     string;
    njs_object_prop_t     *prop;
    njs_property_query_t  pq;
    njs_json_stringify_t  *stringify, dump_stringify;

    stringify = &dump_stringify;

    stringify->vm = vm;
    stringify->depth = 0;

    njs_chb_init(&chain, vm->mem_pool);

    if (!njs_dump_is_recursive(value)) {
        ret = njs_dump_terminal(stringify, &chain, value, console);
        if (njs_slow_path(ret != NJS_OK)) {
            goto memory_error;
        }

        goto done;
    }

    njs_set_undefined(&stringify->replacer);
    stringify->keys_type = NJS_ENUM_STRING | NJS_ENUM_SYMBOL;
    indent = njs_min(indent, 10);
    stringify->space.length = indent;
    stringify->space.start = stringify->space_buf;

    njs_memset(stringify->space.start, ' ', indent);

    state = njs_json_push_stringify_state(vm, stringify, value);
    if (njs_slow_path(state == NULL)) {
        goto memory_error;
    }

    for ( ;; ) {
        if (state->index == 0) {
            ret = njs_object_string_tag(vm, &state->value, &tag);
            if (njs_slow_path(ret == NJS_ERROR)) {
                return ret;
            }

            if (ret == NJS_OK) {
                (void) njs_string_prop(&string, &tag);
                njs_chb_append(&chain, string.start, string.size);
                njs_chb_append_literal(&chain, " ");
            }

            njs_chb_append(&chain, state->array ? "[" : "{", 1);
            njs_json_stringify_indent(stringify, &chain, 1);
        }

        if (njs_json_stringify_done(state, 0)) {
            njs_dump_empty(stringify, state, &chain, 0);

            njs_json_stringify_indent(stringify, &chain, 0);
            njs_chb_append(&chain, state->array ? "]" : "}", 1);

            state = njs_json_pop_stringify_state(stringify);
            if (state == NULL) {
                goto done;
            }

            continue;
        }

        njs_property_query_init(&pq, NJS_PROPERTY_QUERY_GET, 0, 0);

        key = &state->keys->start[state->index++];

        if(state->array) {
            if (key->type == NJS_STRING) {
                njs_string_get(key, &str);
                if (str.length == 6 && memcmp(str.start, "length", 6) == 0) {
                    continue;
                }
            }
        }

        state->key = key;

        ret = njs_property_query(vm, &pq, &state->value, key);
        if (njs_slow_path(ret != NJS_OK)) {
            if (ret == NJS_DECLINED) {
                continue;
            }

            goto exception;
        }

        prop = pq.lhq.value;

        if (prop->type == NJS_WHITEOUT || !prop->enumerable) {
            if (!state->array) {
                continue;
            }
        }

        if (state->written) {
            njs_chb_append_literal(&chain, ",");
            njs_json_stringify_indent(stringify, &chain, 1);
        }

        state->written = 1;

        njs_dump_empty(stringify, state, &chain, 1);

        if (!state->array || isnan(njs_key_to_index(key))) {
            njs_key_string_get(vm, key, &pq.lhq.key);
            njs_chb_append(&chain, pq.lhq.key.start, pq.lhq.key.length);
            njs_chb_append_literal(&chain, ":");
            if (stringify->space.length != 0) {
                njs_chb_append_literal(&chain, " ");
            }
        }

        val = njs_prop_value(prop);

        if (!state->fast_array) {
            if (prop->type == NJS_PROPERTY_HANDLER) {
                pq.scratch = *prop;
                prop = &pq.scratch;
                ret = njs_prop_handler(prop)(vm, prop, &state->value, NULL,
                                             njs_prop_value(prop));

                if (njs_slow_path(ret == NJS_ERROR)) {
                    return ret;
                }

                val = njs_prop_value(prop);
            }

            if (njs_is_accessor_descriptor(prop)) {
                if (njs_prop_getter(prop) != NULL) {
                    if (njs_prop_setter(prop) != NULL) {
                        val = njs_value_arg(&string_get_set);

                    } else {
                        val = njs_value_arg(&string_get);
                    }

                } else {
                    val = njs_value_arg(&string_set);
                }
            }
        }

        if (njs_dump_is_recursive(val)) {
            if (njs_slow_path(njs_dump_visited(vm, stringify, val))) {
                njs_chb_append_literal(&chain, "[Circular]");
                continue;
            }

            state = njs_json_push_stringify_state(vm, stringify, val);
            if (njs_slow_path(state == NULL)) {
                goto exception;
            }

            continue;
        }

        ret = njs_dump_terminal(stringify, &chain, val, console);
        if (njs_slow_path(ret != NJS_OK)) {
            if (ret == NJS_DECLINED) {
                goto exception;
            }

            goto memory_error;
        }
    }

done:

    ret = njs_chb_join(&chain, &str);
    if (njs_slow_path(ret != NJS_OK)) {
        goto memory_error;
    }

    njs_chb_destroy(&chain);

    *retval = str;

    return NJS_OK;

memory_error:

    njs_memory_error(vm);

exception:

    njs_vm_value_string(vm, retval, &vm->retval);

    return NJS_OK;
}
