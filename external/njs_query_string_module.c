
/*
 * Copyright (C) Alexander Borisov
 * Copyright (C) Dmitry Volyntsev
 * Copyright (C) NGINX, Inc.
 */


#include <njs.h>
#include <njs_string.h>


static njs_int_t njs_query_string_parser(njs_vm_t *vm, u_char *query,
    u_char *end, const njs_str_t *sep, const njs_str_t *eq,
    njs_function_t *decode, njs_uint_t max_keys, njs_value_t *retval);
static njs_int_t njs_query_string_parse(njs_vm_t *vm, njs_value_t *args,
    njs_uint_t nargs, njs_index_t unused, njs_value_t *retval);
static njs_int_t njs_query_string_stringify(njs_vm_t *vm, njs_value_t *args,
    njs_uint_t nargs, njs_index_t unused, njs_value_t *retval);
static njs_int_t njs_query_string_escape(njs_vm_t *vm, njs_value_t *args,
    njs_uint_t nargs, njs_index_t unused, njs_value_t *retval);
static njs_int_t njs_query_string_unescape(njs_vm_t *vm, njs_value_t *args,
    njs_uint_t nargs, njs_index_t unused, njs_value_t *retval);

static njs_int_t njs_query_string_init(njs_vm_t *vm);


static njs_external_t  njs_ext_query_string[] = {

    {
        .flags = NJS_EXTERN_PROPERTY | NJS_EXTERN_SYMBOL,
        .name.symbol = NJS_SYMBOL_TO_STRING_TAG,
        .u.property = {
            .value = "querystring",
        }
    },

    {
        .flags = NJS_EXTERN_METHOD,
        .name.string = njs_str("parse"),
        .writable = 1,
        .configurable = 1,
        .u.method = {
            .native = njs_query_string_parse,
            .magic8 = 0,
        }
    },

    {
        .flags = NJS_EXTERN_METHOD,
        .name.string = njs_str("stringify"),
        .writable = 1,
        .configurable = 1,
        .u.method = {
            .native = njs_query_string_stringify,
            .magic8 = 0,
        }
    },

    {
        .flags = NJS_EXTERN_METHOD,
        .name.string = njs_str("decode"),
        .writable = 1,
        .configurable = 1,
        .u.method = {
            .native = njs_query_string_parse,
            .magic8 = 0,
        }
    },

    {
        .flags = NJS_EXTERN_METHOD,
        .name.string = njs_str("encode"),
        .writable = 1,
        .configurable = 1,
        .u.method = {
            .native = njs_query_string_stringify,
            .magic8 = 0,
        }
    },

    {
        .flags = NJS_EXTERN_METHOD,
        .name.string = njs_str("escape"),
        .writable = 1,
        .configurable = 1,
        .u.method = {
            .native = njs_query_string_escape,
            .magic8 = 0,
        }
    },

    {
        .flags = NJS_EXTERN_METHOD,
        .name.string = njs_str("unescape"),
        .writable = 1,
        .configurable = 1,
        .u.method = {
            .native = njs_query_string_unescape,
            .magic8 = 0,
        }
    },
};


njs_module_t  njs_query_string_module = {
    .name = njs_str("querystring"),
    .preinit = NULL,
    .init = njs_query_string_init,
};


static const njs_str_t  njs_escape_str = njs_str("escape");
static const njs_str_t  njs_unescape_str = njs_str("unescape");
static const njs_str_t  njs_encode_uri_str = njs_str("encodeURIComponent");
static const njs_str_t  njs_decode_uri_str = njs_str("decodeURIComponent");
static const njs_str_t  njs_max_keys_str = njs_str("maxKeys");

static const njs_str_t njs_sep_default = njs_str("&");
static const njs_str_t njs_eq_default = njs_str("=");


static njs_int_t
njs_query_string_decode(njs_vm_t *vm, njs_value_t *value, const u_char *start,
    size_t size)
{
    u_char                *dst;
    uint32_t              cp;
    njs_int_t             ret;
    njs_chb_t             chain;
    const u_char          *p, *end;
    njs_unicode_decode_t  ctx;

    static const int8_t  hex[256]
        njs_aligned(32) =
    {
        -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
        -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
        -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
         0,  1,  2,  3,  4,  5,  6,  7,  8,  9, -1, -1, -1, -1, -1, -1,
        -1, 10, 11, 12, 13, 14, 15, -1, -1, -1, -1, -1, -1, -1, -1, -1,
        -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
        -1, 10, 11, 12, 13, 14, 15, -1, -1, -1, -1, -1, -1, -1, -1, -1,
        -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
        -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
        -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
        -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
        -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
        -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
        -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
        -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
        -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    };

    NJS_CHB_MP_INIT(&chain, vm);
    njs_utf8_decode_init(&ctx);

    cp = 0;

    p = start;
    end = p + size;

    while (p < end) {
        if (*p == '%' && end - p > 2 && hex[p[1]] >= 0 && hex[p[2]] >= 0) {
            cp = njs_utf8_consume(&ctx, (hex[p[1]] << 4) | hex[p[2]]);
            p += 3;

        } else {
            if (*p == '+') {
                cp = ' ';
                p++;

            } else {
                cp = njs_utf8_decode(&ctx, &p, end);
            }
        }

        if (cp > NJS_UNICODE_MAX_CODEPOINT) {
            if (cp == NJS_UNICODE_CONTINUE) {
                continue;
            }

            cp = NJS_UNICODE_REPLACEMENT;
        }

        dst = njs_chb_reserve(&chain, 4);
        if (njs_slow_path(dst == NULL)) {
            return NJS_ERROR;
        }

        njs_chb_written(&chain, njs_utf8_encode(dst, cp) - dst);
    }

    if (njs_slow_path(cp == NJS_UNICODE_CONTINUE)) {
        dst = njs_chb_reserve(&chain, 3);
        if (njs_slow_path(dst == NULL)) {
            return NJS_ERROR;
        }

        njs_chb_written(&chain,
                        njs_utf8_encode(dst, NJS_UNICODE_REPLACEMENT) - dst);
    }

    ret = njs_vm_value_string_create_chb(vm, value, &chain);

    njs_chb_destroy(&chain);

    return ret;
}


njs_inline njs_bool_t
njs_query_string_is_native_decoder(njs_function_t *decoder)
{
    njs_opaque_value_t     function;
    njs_function_native_t  native;

    if (decoder == NULL) {
        return 1;
    }

    njs_value_function_set(njs_value_arg(&function), decoder);

    native = njs_value_native_function(njs_value_arg(&function));

    return native == njs_query_string_unescape;
}


njs_inline njs_int_t
njs_query_string_append(njs_vm_t *vm, njs_value_t *object, const u_char *key,
    size_t key_size, const u_char *val, size_t val_size,
    njs_function_t *decoder)
{
    njs_int_t           ret;
    njs_value_t         *push;
    njs_opaque_value_t  array, name, value, retval;

    if (njs_query_string_is_native_decoder(decoder)) {
        ret = njs_query_string_decode(vm, njs_value_arg(&name), key, key_size);
        if (njs_slow_path(ret != NJS_OK)) {
            return ret;
        }

        ret = njs_query_string_decode(vm, njs_value_arg(&value), val, val_size);
        if (njs_slow_path(ret != NJS_OK)) {
            return ret;
        }

    } else {

        ret = njs_vm_value_string_create(vm, njs_value_arg(&name), key,
                                         key_size);
        if (njs_slow_path(ret != NJS_OK)) {
            return ret;
        }

        if (key_size > 0) {
            ret = njs_vm_invoke(vm, decoder, njs_value_arg(&name), 1,
                                njs_value_arg(&name));
            if (njs_slow_path(ret != NJS_OK)) {
                return ret;
            }

            if (!njs_value_is_string(njs_value_arg(&name))) {
                ret = njs_value_to_string(vm, njs_value_arg(&name),
                                          njs_value_arg(&name));
                if (njs_slow_path(ret != NJS_OK)) {
                    return ret;
                }
            }
        }

        ret = njs_vm_value_string_create(vm, njs_value_arg(&value), val,
                                         val_size);
        if (njs_slow_path(ret != NJS_OK)) {
            return ret;
        }

        if (val_size > 0) {
            ret = njs_vm_invoke(vm, decoder, njs_value_arg(&value), 1,
                                njs_value_arg(&value));
            if (njs_slow_path(ret != NJS_OK)) {
                return ret;
            }

            if (!njs_value_is_string(njs_value_arg(&value))) {
                ret = njs_value_to_string(vm, njs_value_arg(&value),
                                          njs_value_arg(&value));
                if (njs_slow_path(ret != NJS_OK)) {
                    return ret;
                }
            }
        }
    }

    ret = njs_value_property_val(vm, object, njs_value_arg(&name),
                                 njs_value_arg(&retval));

    if (ret == NJS_OK) {
        if (njs_value_is_array(njs_value_arg(&retval))) {
            push = njs_vm_array_push(vm, njs_value_arg(&retval));
            if (njs_slow_path(push == NULL)) {
                return NJS_ERROR;
            }

            njs_value_assign(push, njs_value_arg(&value));

            return NJS_OK;
        }

        ret = njs_vm_array_alloc(vm, njs_value_arg(&array), 2);
        if (njs_slow_path(ret != NJS_OK)) {
            return ret;
        }

        push = njs_vm_array_push(vm, njs_value_arg(&array));
        if (njs_slow_path(push == NULL)) {
            return NJS_ERROR;
        }

        njs_value_assign(push, njs_value_arg(&retval));

        push = njs_vm_array_push(vm, njs_value_arg(&array));
        if (njs_slow_path(push == NULL)) {
            return NJS_ERROR;
        }

        njs_value_assign(push, njs_value_arg(&value));

        njs_value_assign(&value, &array);
    }

    return njs_value_property_val_set(vm, object, njs_value_arg(&name),
                                      njs_value_arg(&value));
}


static u_char *
njs_query_string_match(u_char *p, u_char *end, const njs_str_t *v)
{
    size_t  length;

    length = v->length;

    if (njs_fast_path(length == 1)) {
        p = njs_strlchr(p, end, v->start[0]);

        if (p == NULL) {
            p = end;
        }

        return p;
    }

    while (p <= (end - length)) {
        if (memcmp(p, v->start, length) == 0) {
            return p;
        }

        p++;
    }

    return end;
}


static njs_int_t
njs_query_string_parse(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t unused, njs_value_t *retval)
{
    int64_t             max_keys;
    njs_int_t           ret;
    njs_str_t           str, sep, eq;
    njs_value_t         *this, *string, *options, *arg, *val;
    njs_function_t      *decode;
    njs_opaque_value_t  value, val_sep, val_eq;

    decode = NULL;
    max_keys = 1000;

    this = njs_argument(args, 0);
    string = njs_arg(args, nargs, 1);

    if (njs_value_is_string(string)) {
        njs_value_string_get(vm, string, &str);

    } else {
        str = njs_str_value("");
    }

    sep = njs_sep_default;
    eq = njs_eq_default;

    arg = njs_arg(args, nargs, 2);
    if (!njs_value_is_null_or_undefined(arg)) {
        ret = njs_value_to_string(vm, njs_value_arg(&val_sep), arg);
        if (njs_slow_path(ret != NJS_OK)) {
            return ret;
        }

        if (njs_value_string_length(vm, njs_value_arg(&val_sep)) != 0) {
            njs_value_string_get(vm, njs_value_arg(&val_sep), &sep);
        }
    }

    arg = njs_arg(args, nargs, 3);
    if (!njs_value_is_null_or_undefined(arg)) {
        ret = njs_value_to_string(vm, njs_value_arg(&val_eq), arg);
        if (njs_slow_path(ret != NJS_OK)) {
            return ret;
        }

        if (njs_value_string_length(vm, njs_value_arg(&val_eq)) != 0) {
            njs_value_string_get(vm, njs_value_arg(&val_eq), &eq);
        }
    }

    options = njs_arg(args, nargs, 4);

    if (njs_value_is_object(options)) {
        val = njs_vm_object_prop(vm, options, &njs_max_keys_str, &value);

        if (val != NULL) {
            if (!njs_value_is_valid_number(val)) {
                njs_vm_type_error(vm, "is not a number");
                return NJS_ERROR;
            }

            max_keys = njs_value_number(val);

            if (max_keys == 0) {
                max_keys = INT64_MAX;
            }
        }

        val = njs_vm_object_prop(vm, options, &njs_decode_uri_str, &value);

        if (val != NULL) {
            if (njs_slow_path(!njs_value_is_function(val))) {
                njs_vm_type_error(vm, "option decodeURIComponent is not "
                                  "a function");
                return NJS_ERROR;
            }

            decode = njs_value_function(val);
        }
    }

    if (decode == NULL) {
        val = njs_vm_object_prop(vm, this, &njs_unescape_str, &value);

        if (val == NULL || !njs_value_is_function(val)) {
            njs_vm_type_error(vm, "QueryString.unescape is not a function");
            return NJS_ERROR;
        }

        decode = njs_value_function(val);
    }

    return njs_query_string_parser(vm, str.start, str.start + str.length,
                                   &sep, &eq, decode, max_keys, retval);
}


njs_int_t
njs_vm_query_string_parse(njs_vm_t *vm, u_char *start, u_char *end,
    njs_value_t *retval)
{
    return njs_query_string_parser(vm, start, end, &njs_sep_default,
                                   &njs_eq_default, NULL, 1000, retval);
}


static njs_int_t
njs_query_string_parser(njs_vm_t *vm, u_char *query, u_char *end,
    const njs_str_t *sep, const njs_str_t *eq, njs_function_t *decode,
    njs_uint_t max_keys, njs_value_t *retval)
{
    size_t      size;
    u_char      *part, *key, *val;
    njs_int_t   ret;
    njs_uint_t  count;

    ret = njs_vm_object_alloc(vm, retval, NULL);
    if (njs_slow_path(ret != NJS_OK)) {
        return NJS_ERROR;
    }

    count = 0;

    key = query;

    while (key < end) {
        if (count++ == max_keys) {
            break;
        }

        part = njs_query_string_match(key, end, sep);

        if (part == key) {
            goto next;
        }

        val = njs_query_string_match(key, part, eq);

        size = val - key;

        if (val != part) {
            val += eq->length;
        }

        ret = njs_query_string_append(vm, retval, key, size, val, part - val,
                                      decode);
        if (njs_slow_path(ret != NJS_OK)) {
            return ret;
        }

    next:

        key = part + sep->length;
    }

    return NJS_OK;
}


njs_inline njs_int_t
njs_query_string_encode(njs_chb_t *chain, njs_str_t *str)
{
    size_t  size;
    u_char  *p, *start, *end;

    static const uint32_t  escape[] = {
        0xffffffff,  /* 1111 1111 1111 1111  1111 1111 1111 1111 */

                     /* ?>=< ;:98 7654 3210  /.-, +*)( '&%$ #"!  */
        0xfc00987d,  /* 1111 1100 0000 0000  1001 1000 0111 1101 */

                     /* _^]\ [ZYX WVUT SRQP  ONML KJIH GFED CBA@ */
        0x78000001,  /* 0111 1000 0000 0000  0000 0000 0000 0001 */

                     /*  ~}| {zyx wvut srqp  onml kjih gfed cba` */
        0xb8000001,  /* 1011 1000 0000 0000  0000 0000 0000 0001 */

        0xffffffff,  /* 1111 1111 1111 1111  1111 1111 1111 1111 */
        0xffffffff,  /* 1111 1111 1111 1111  1111 1111 1111 1111 */
        0xffffffff,  /* 1111 1111 1111 1111  1111 1111 1111 1111 */
        0xffffffff,  /* 1111 1111 1111 1111  1111 1111 1111 1111 */
    };

    if (chain->error) {
        return NJS_ERROR;
    }

    if (str->length == 0) {
        return NJS_OK;
    }

    p = str->start;
    end = p + str->length;
    size = str->length;

    while (p < end) {
        if (njs_need_escape(escape, *p++)) {
            size += 2;
        }
    }

    start = njs_chb_reserve(chain, size);
    if (njs_slow_path(start == NULL)) {
        return NJS_ERROR;
    }

    if (size == str->length) {
        memcpy(start, str->start, str->length);
        njs_chb_written(chain, str->length);
        return NJS_OK;
    }

    (void) njs_string_encode(escape, str->length, str->start, start);

    njs_chb_written(chain, size);

    return NJS_OK;
}


njs_inline njs_bool_t
njs_query_string_is_native_encoder(njs_function_t *encoder)
{
    njs_opaque_value_t  function;

    njs_value_function_set(njs_value_arg(&function), encoder);

    return njs_value_native_function(njs_value_arg(&function))
                                                   == njs_query_string_escape;
}


njs_inline njs_int_t
njs_query_string_encoder_call(njs_vm_t *vm, njs_chb_t *chain,
    njs_function_t *encoder, njs_value_t *string)
{
    njs_str_t           str;
    njs_int_t           ret;
    njs_opaque_value_t  retval;

    if (njs_slow_path(!njs_value_is_string(string))) {
        ret = njs_value_to_string(vm, string, string);
        if (njs_slow_path(ret != NJS_OK)) {
            return NJS_ERROR;
        }
    }

    if (njs_fast_path(njs_query_string_is_native_encoder(encoder))) {
        njs_value_string_get(vm, string, &str);
        return njs_query_string_encode(chain, &str);
    }

    ret = njs_vm_invoke(vm, encoder, string, 1, njs_value_arg(&retval));
    if (njs_slow_path(ret != NJS_OK)) {
        return ret;
    }

    if (njs_slow_path(!njs_value_is_string(njs_value_arg(&retval)))) {
        ret = njs_value_to_string(vm, njs_value_arg(&retval),
                                  njs_value_arg(&retval));
        if (njs_slow_path(ret != NJS_OK)) {
            return NJS_ERROR;
        }
    }

    njs_value_string_get(vm, njs_value_arg(&retval), &str);

    njs_chb_append_str(chain, &str);

    return NJS_OK;
}


njs_inline njs_int_t
njs_query_string_push(njs_vm_t *vm, njs_chb_t *chain, njs_value_t *key,
    njs_value_t *value, njs_str_t *eq, njs_function_t *encoder)
{
    njs_int_t  ret;

    ret = njs_query_string_encoder_call(vm, chain, encoder, key);
    if (njs_slow_path(ret != NJS_OK)) {
        return NJS_ERROR;
    }

    njs_chb_append(chain, eq->start, eq->length);

    if (njs_value_is_valid_number(value)
        || njs_value_is_boolean(value)
        || njs_value_is_string(value))
    {
        if (!njs_value_is_string(value)) {
            ret = njs_value_to_string(vm, value, value);
            if (njs_slow_path(ret != NJS_OK)) {
                return NJS_ERROR;
            }
        }

        ret = njs_query_string_encoder_call(vm, chain, encoder, value);
        if (njs_slow_path(ret < 0)) {
            return NJS_ERROR;
        }
    }

    return NJS_OK;
}


static njs_int_t
njs_query_string_stringify(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t unused, njs_value_t *retval)
{
    int64_t             len, keys_length;
    uint32_t            n, i;
    njs_int_t           ret;
    njs_str_t           sep, eq;
    njs_chb_t           chain;
    njs_value_t         *this, *object, *arg, *options, *val, *keys;
    njs_function_t      *encode;
    njs_opaque_value_t  value, result, key, *string;

    encode = NULL;
    sep = njs_sep_default;
    eq = njs_eq_default;

    this = njs_argument(args, 0);
    object = njs_arg(args, nargs, 1);

    if (njs_slow_path(!njs_value_is_object(object))) {
        njs_vm_value_string_create(vm, retval, (u_char *) "", 0);
        return NJS_OK;
    }

    arg = njs_arg(args, nargs, 2);
    if (!njs_value_is_null_or_undefined(arg)) {
        ret = njs_value_to_string(vm, arg, arg);
        if (njs_slow_path(ret != NJS_OK)) {
            return ret;
        }

        if (njs_value_string_length(vm, arg) > 0) {
            njs_value_string_get(vm, arg, &sep);
        }
    }

    arg = njs_arg(args, nargs, 3);
    if (!njs_value_is_null_or_undefined(arg)) {
        ret = njs_value_to_string(vm, arg, arg);
        if (njs_slow_path(ret != NJS_OK)) {
            return ret;
        }

        if (njs_value_string_length(vm, arg) > 0) {
            njs_value_string_get(vm, arg, &eq);
        }
    }

    options = njs_arg(args, nargs, 4);

    if (njs_value_is_object(options)) {
        val = njs_vm_object_prop(vm, options, &njs_encode_uri_str, &value);

        if (val != NULL) {
            if (njs_slow_path(!njs_value_is_function(val))) {
                njs_vm_type_error(vm, "option encodeURIComponent is not "
                                  "a function");
                return NJS_ERROR;
            }

            encode = njs_value_function(val);
        }
    }

    if (encode == NULL) {
        val = njs_vm_object_prop(vm, this, &njs_escape_str, &value);

        if (val == NULL || !njs_value_is_function(val)) {
            njs_vm_type_error(vm, "QueryString.escape is not a function");
            return NJS_ERROR;
        }

        encode = njs_value_function(val);
    }

    NJS_CHB_MP_INIT(&chain, vm);

    keys = njs_vm_object_keys(vm, object, njs_value_arg(&value));
    if (njs_slow_path(keys == NULL)) {
        return NJS_ERROR;
    }

    (void) njs_vm_array_length(vm, keys, &keys_length);

    string = (njs_opaque_value_t *) njs_vm_array_start(vm, keys);
    if (njs_slow_path(string == NULL)) {
        return NJS_ERROR;
    }

    for (n = 0; n < keys_length; n++, string++) {
        ret = njs_value_property_val(vm, object, njs_value_arg(string),
                                     njs_value_arg(&value));
        if (njs_slow_path(ret == NJS_ERROR)) {
            goto failed;
        }

        if (njs_value_is_array(njs_value_arg(&value))) {
            (void) njs_vm_array_length(vm, njs_value_arg(&value), &len);

            for (i = 0; i < len; i++) {
                njs_value_number_set(njs_value_arg(&key), i);
                ret = njs_value_property_val(vm, njs_value_arg(&value),
                                             njs_value_arg(&key),
                                             njs_value_arg(&result));
                if (njs_slow_path(ret == NJS_ERROR)) {
                    goto failed;
                }

                if (chain.last != NULL) {
                    njs_chb_append(&chain, sep.start, sep.length);
                }

                ret = njs_query_string_push(vm, &chain, njs_value_arg(string),
                                            njs_value_arg(&result), &eq,
                                            encode);
                if (njs_slow_path(ret != NJS_OK)) {
                    goto failed;
                }
            }

            continue;
        }

        if (n != 0) {
            njs_chb_append(&chain, sep.start, sep.length);
        }

        ret = njs_query_string_push(vm, &chain, njs_value_arg(string),
                                    njs_value_arg(&value), &eq, encode);
        if (njs_slow_path(ret != NJS_OK)) {
            goto failed;
        }
    }

    ret = njs_vm_value_string_create_chb(vm, retval, &chain);

failed:

    njs_chb_destroy(&chain);

    return ret;
}


static njs_int_t
njs_query_string_escape(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t unused, njs_value_t *retval)
{
    njs_int_t           ret;
    njs_str_t           str;
    njs_chb_t           chain;
    njs_value_t         *string;
    njs_opaque_value_t  value;

    string = njs_arg(args, nargs, 1);

    if (!njs_value_is_string(string)) {
        ret = njs_value_to_string(vm, njs_value_arg(&value), string);
        if (njs_slow_path(ret != NJS_OK)) {
            return ret;
        }

        string = njs_value_arg(&value);
    }

    njs_value_string_get(vm, string, &str);

    NJS_CHB_MP_INIT(&chain, vm);

    ret = njs_query_string_encode(&chain, &str);
    if (njs_slow_path(ret != NJS_OK)) {
        return NJS_ERROR;
    }

    ret = njs_vm_value_string_create_chb(vm, retval, &chain);

    njs_chb_destroy(&chain);

    return ret;
}


static njs_int_t
njs_query_string_unescape(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t unused, njs_value_t *retval)
{
    njs_int_t           ret;
    njs_str_t           str;
    njs_value_t         *string;
    njs_opaque_value_t  value;

    string = njs_arg(args, nargs, 1);

    if (!njs_value_is_string(string)) {
        ret = njs_value_to_string(vm, njs_value_arg(&value), string);
        if (njs_slow_path(ret != NJS_OK)) {
            return ret;
        }

        string = njs_value_arg(&value);
    }

    njs_value_string_get(vm, string, &str);

    return njs_query_string_decode(vm, retval, str.start, str.length);
}


static njs_int_t
njs_query_string_init(njs_vm_t *vm)
{
    njs_int_t           ret, proto_id;
    njs_mod_t           *module;
    njs_opaque_value_t  value;

    proto_id = njs_vm_external_prototype(vm, njs_ext_query_string,
                                         njs_nitems(njs_ext_query_string));
    if (njs_slow_path(proto_id < 0)) {
        return NJS_ERROR;
    }

    ret = njs_vm_external_create(vm, njs_value_arg(&value), proto_id, NULL, 1);
    if (njs_slow_path(ret != NJS_OK)) {
        return NJS_ERROR;
    }

    module = njs_vm_add_module(vm, &njs_str_value("querystring"),
                               njs_value_arg(&value));
    if (njs_slow_path(module == NULL)) {
        return NJS_ERROR;
    }

    return NJS_OK;
}
