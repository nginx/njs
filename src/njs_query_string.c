
/*
 * Copyright (C) Alexander Borisov
 * Copyright (C) Dmitry Volyntsev
 * Copyright (C) NGINX, Inc.
 */


#include <njs_main.h>


static const njs_value_t  njs_escape_str = njs_string("escape");
static const njs_value_t  njs_unescape_str = njs_string("unescape");
static const njs_value_t  njs_encode_uri_str =
                                         njs_long_string("encodeURIComponent");
static const njs_value_t  njs_decode_uri_str =
                                         njs_long_string("decodeURIComponent");
static const njs_value_t  njs_max_keys_str = njs_string("maxKeys");


static njs_int_t njs_query_string_escape(njs_vm_t *vm, njs_value_t *args,
    njs_uint_t nargs, njs_index_t unused);
static njs_int_t njs_query_string_unescape(njs_vm_t *vm, njs_value_t *args,
    njs_uint_t nargs, njs_index_t unused);


static njs_object_t *
njs_query_string_object_alloc(njs_vm_t *vm)
{
    njs_object_t  *obj;

    obj = njs_mp_alloc(vm->mem_pool, sizeof(njs_object_t));

    if (njs_fast_path(obj != NULL)) {
        njs_lvlhsh_init(&obj->hash);
        njs_lvlhsh_init(&obj->shared_hash);
        obj->type = NJS_OBJECT;
        obj->shared = 0;
        obj->extensible = 1;
        obj->error_data = 0;
        obj->fast_array = 0;

        obj->__proto__ = NULL;
        obj->slots = NULL;

        return obj;
    }

    njs_memory_error(vm);

    return NULL;
}


static njs_int_t
njs_query_string_decode(njs_vm_t *vm, njs_value_t *value, const u_char *start,
    size_t size)
{
    u_char                *dst;
    size_t                length;
    ssize_t               str_size;
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

    njs_chb_init(&chain, vm->mem_pool);
    njs_utf8_decode_init(&ctx);

    cp = 0;
    length = 0;
    ret = NJS_ERROR;

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

        length++;
    }

    if (njs_slow_path(cp == NJS_UNICODE_CONTINUE)) {
        dst = njs_chb_reserve(&chain, 3);
        if (njs_slow_path(dst == NULL)) {
            return NJS_ERROR;
        }

        njs_chb_written(&chain,
                        njs_utf8_encode(dst, NJS_UNICODE_REPLACEMENT) - dst);

        length++;
    }

    str_size = njs_chb_size(&chain);
    if (njs_slow_path(str_size < 0)) {
        goto failed;
    }

    dst = njs_string_alloc(vm, value, str_size, length);
    if (njs_slow_path(dst == NULL)) {
        goto failed;
    }

    njs_chb_join_to(&chain, dst);

    ret = NJS_OK;

failed:

    njs_chb_destroy(&chain);

    return ret;
}


njs_inline njs_bool_t
njs_query_string_is_native_decoder(njs_function_t *decoder)
{
    return decoder->native && decoder->u.native == njs_query_string_unescape;
}


njs_inline njs_int_t
njs_query_string_append(njs_vm_t *vm, njs_value_t *object, const u_char *key,
    size_t key_size, const u_char *val, size_t val_size,
    njs_function_t *decoder)
{
    uint32_t     key_length, val_length;
    njs_int_t    ret;
    njs_array_t  *array;
    njs_value_t  name, value, retval;

    if (njs_query_string_is_native_decoder(decoder)) {
        ret = njs_query_string_decode(vm, &name, key, key_size);
        if (njs_slow_path(ret != NJS_OK)) {
            return ret;
        }

        ret = njs_query_string_decode(vm, &value, val, val_size);
        if (njs_slow_path(ret != NJS_OK)) {
            return ret;
        }

    } else {

        key_length = njs_max(njs_utf8_length(key, key_size), 0);
        ret = njs_string_new(vm, &name, key, key_size, key_length);
        if (njs_slow_path(ret != NJS_OK)) {
            return ret;
        }

        if (key_size > 0) {
            ret = njs_function_call(vm, decoder, &njs_value_undefined, &name, 1,
                                    &name);
            if (njs_slow_path(ret != NJS_OK)) {
                return ret;
            }

            if (!njs_is_string(&name)) {
                njs_value_to_string(vm, &name, &name);
            }
        }

        val_length = njs_max(njs_utf8_length(val, val_size), 0);
        ret = njs_string_new(vm, &value, val, val_size, val_length);
        if (njs_slow_path(ret != NJS_OK)) {
            return ret;
        }

        if (val_size > 0) {
            ret = njs_function_call(vm, decoder, &njs_value_undefined, &value,
                                    1, &value);
            if (njs_slow_path(ret != NJS_OK)) {
                return ret;
            }

            if (!njs_is_string(&value)) {
                njs_value_to_string(vm, &value, &value);
            }
        }
    }

    ret = njs_value_property(vm, object, &name, &retval);

    if (ret == NJS_OK) {
        if (njs_is_array(&retval)) {
            return njs_array_add(vm, njs_array(&retval), &value);
        }

        array = njs_array_alloc(vm, 1, 2, 0);
        if (njs_slow_path(array == NULL)) {
            return NJS_ERROR;
        }

        array->start[0] = retval;
        array->start[1] = value;

        njs_set_array(&value, array);
    }

    return njs_value_property_set(vm, object, &name, &value);
}


static u_char *
njs_query_string_match(u_char *p, u_char *end, njs_str_t *v)
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
    njs_index_t unused)
{
    size_t          size;
    u_char          *end, *part, *key, *val;
    int64_t         max_keys, count;
    njs_int_t       ret;
    njs_str_t       str;
    njs_value_t     obj, value, *this, *string, *options, *arg;
    njs_value_t     val_sep, val_eq;
    njs_object_t    *object;
    njs_function_t  *decode;

    njs_str_t  sep = njs_str("&");
    njs_str_t  eq = njs_str("=");

    count = 0;
    decode = NULL;
    max_keys = 1000;

    object = njs_query_string_object_alloc(vm);
    if (njs_slow_path(object == NULL)) {
        return NJS_ERROR;
    }

    njs_set_object(&obj, object);

    this = njs_arg(args, nargs, 0);
    string = njs_arg(args, nargs, 1);

    if (njs_slow_path(!njs_is_string(string)
                      || njs_string_length(string) == 0))
    {
        goto done;
    }

    njs_string_get(string, &str);

    arg = njs_arg(args, nargs, 2);
    if (!njs_is_null_or_undefined(arg)) {
        ret = njs_value_to_string(vm, &val_sep, arg);
        if (njs_slow_path(ret != NJS_OK)) {
            return ret;
        }

        if (njs_string_length(&val_sep) != 0) {
            njs_string_get(&val_sep, &sep);
        }
    }

    arg = njs_arg(args, nargs, 3);
    if (!njs_is_null_or_undefined(arg)) {
        ret = njs_value_to_string(vm, &val_eq, arg);
        if (njs_slow_path(ret != NJS_OK)) {
            return ret;
        }

        if (njs_string_length(&val_eq) != 0) {
            njs_string_get(&val_eq, &eq);
        }
    }

    options = njs_arg(args, nargs, 4);

    if (njs_is_object(options)) {
        ret = njs_value_property(vm, options, njs_value_arg(&njs_max_keys_str),
                                 &value);
        if (njs_slow_path(ret == NJS_ERROR)) {
            return ret;
        }

        ret = njs_value_to_integer(vm, &value, &max_keys);
        if (njs_slow_path(ret != NJS_OK)) {
            return ret;
        }

        if (max_keys == 0) {
            max_keys = INT64_MAX;
        }

        ret = njs_value_property(vm, options,
                                 njs_value_arg(&njs_decode_uri_str), &value);

        if (ret == NJS_OK) {
            if (njs_slow_path(!njs_is_function(&value))) {
                njs_type_error(vm,
                               "option decodeURIComponent is not a function");
                return NJS_ERROR;
            }

            decode = njs_function(&value);
        }
    }

    if (decode == NULL) {
        ret = njs_value_property(vm, this, njs_value_arg(&njs_unescape_str),
                                 &value);

        if (ret != NJS_OK || !njs_is_function(&value)) {
            njs_type_error(vm, "QueryString.unescape is not a function");
            return NJS_ERROR;
        }

        decode = njs_function(&value);
    }

    key = str.start;
    end = str.start + str.length;

    do {
        if (count++ == max_keys) {
            break;
        }

        part = njs_query_string_match(key, end, &sep);

        if (part == key) {
            goto next;
        }

        val = njs_query_string_match(key, part, &eq);

        size = val - key;

        if (val != end) {
            val += eq.length;
        }

        ret = njs_query_string_append(vm, &obj, key, size, val, part - val,
                                      decode);
        if (njs_slow_path(ret != NJS_OK)) {
            return ret;
        }

    next:

        key = part + sep.length;

    } while (key < end);

done:

    njs_set_object(&vm->retval, object);

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
        return 0;
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
        return str->length;
    }

    (void) njs_string_encode(escape, str->length, str->start, start);

    njs_chb_written(chain, size);

    return size;
}


njs_inline njs_bool_t
njs_query_string_is_native_encoder(njs_function_t *encoder)
{
    return encoder->native && encoder->u.native == njs_query_string_escape;
}


njs_inline njs_int_t
njs_query_string_encoder_call(njs_vm_t *vm, njs_chb_t *chain,
    njs_function_t *encoder, njs_value_t *string)
{
    njs_str_t    str;
    njs_int_t    ret;
    njs_value_t  retval;

    if (njs_slow_path(!njs_is_string(string))) {
        ret = njs_value_to_string(vm, string, string);
        if (njs_slow_path(ret != NJS_OK)) {
            return NJS_ERROR;
        }
    }

    if (njs_fast_path(njs_query_string_is_native_encoder(encoder))) {
        njs_string_get(string, &str);
        return njs_query_string_encode(chain, &str);
    }

    ret = njs_function_call(vm, encoder, &njs_value_undefined, string, 1,
                            &retval);
    if (njs_slow_path(ret != NJS_OK)) {
        return NJS_ERROR;
    }

    if (njs_slow_path(!njs_is_string(&retval))) {
        ret = njs_value_to_string(vm, &retval, &retval);
        if (njs_slow_path(ret != NJS_OK)) {
            return NJS_ERROR;
        }
    }

    njs_string_get(&retval, &str);

    ret = njs_utf8_length(str.start, str.length);
    if (ret < 0) {
        njs_type_error(vm, "got non-UTF8 string from encoder");
        return NJS_ERROR;
    }

    njs_chb_append_str(chain, &str);

    return ret;
}


njs_inline njs_int_t
njs_query_string_push(njs_vm_t *vm, njs_chb_t *chain, njs_value_t *key,
    njs_value_t *value, njs_string_prop_t *eq, njs_function_t *encoder)
{
    double     num;
    njs_int_t  ret, length;

    length = 0;

    ret = njs_query_string_encoder_call(vm, chain, encoder, key);
    if (njs_slow_path(ret < 0)) {
        return NJS_ERROR;
    }

    length += ret;

    njs_chb_append(chain, eq->start, eq->size);
    length += eq->length;

    switch (value->type) {
    case NJS_NUMBER:
        num = njs_number(value);
        if (njs_slow_path(isnan(num) || isinf(num))) {
            break;
        }

        /* Fall through. */

    case NJS_BOOLEAN:
        ret = njs_primitive_value_to_string(vm, value, value);
        if (njs_slow_path(ret != NJS_OK)) {
            return NJS_ERROR;
        }

        /* Fall through. */

    case NJS_STRING:
        ret = njs_query_string_encoder_call(vm, chain, encoder, value);
        if (njs_slow_path(ret < 0)) {
            return NJS_ERROR;
        }

        length += ret;
        break;

    default:
        break;
    }

    return length;
}


static njs_int_t
njs_query_string_stringify(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t unused)
{
    u_char             *p;
    int64_t            len;
    ssize_t            size;
    uint32_t           n, i;
    uint64_t           length;
    njs_int_t          ret;
    njs_chb_t          chain;
    njs_value_t        value, retval, *string, *this, *object, *arg, *options;
    njs_array_t        *keys, *array;
    njs_function_t     *encode;
    njs_string_prop_t  sep, eq;

    njs_value_t  val_sep = njs_string("&");
    njs_value_t  val_eq = njs_string("=");

    (void) njs_string_prop(&sep, &val_sep);
    (void) njs_string_prop(&eq, &val_eq);

    encode = NULL;
    this = njs_arg(args, nargs, 0);
    object = njs_arg(args, nargs, 1);

    if (njs_slow_path(!njs_is_object(object))) {
        vm->retval = njs_string_empty;
        return NJS_OK;
    }

    arg = njs_arg(args, nargs, 2);
    if (!njs_is_null_or_undefined(arg)) {
        ret = njs_value_to_string(vm, arg, arg);
        if (njs_slow_path(ret != NJS_OK)) {
            return ret;
        }

        if (njs_string_length(arg) > 0) {
            (void) njs_string_prop(&sep, arg);
        }
    }

    arg = njs_arg(args, nargs, 3);
    if (!njs_is_null_or_undefined(arg)) {
        ret = njs_value_to_string(vm, arg, arg);
        if (njs_slow_path(ret != NJS_OK)) {
            return ret;
        }

        if (njs_string_length(arg) > 0) {
            (void) njs_string_prop(&eq, arg);
        }
    }

    options = njs_arg(args, nargs, 4);

    if (njs_is_object(options)) {
        ret = njs_value_property(vm, options,
                                 njs_value_arg(&njs_encode_uri_str), &value);

        if (ret == NJS_OK) {
            if (njs_slow_path(!njs_is_function(&value))) {
                njs_type_error(vm,
                               "option encodeURIComponent is not a function");
                return NJS_ERROR;
            }

            encode = njs_function(&value);
        }
    }

    if (encode == NULL) {
        ret = njs_value_property(vm, this, njs_value_arg(&njs_escape_str),
                                 &value);

        if (ret != NJS_OK || !njs_is_function(&value)) {
            njs_type_error(vm, "QueryString.escape is not a function");
            return NJS_ERROR;
        }

        encode = njs_function(&value);
    }

    njs_chb_init(&chain, vm->mem_pool);

    keys = njs_value_own_enumerate(vm, object, NJS_ENUM_KEYS, NJS_ENUM_STRING,
                                   0);
    if (njs_slow_path(keys == NULL)) {
        return NJS_ERROR;
    }

    for (n = 0, length = 0; n < keys->length; n++) {
        string = &keys->start[n];

        ret = njs_value_property(vm, object, string, &value);
        if (njs_slow_path(ret == NJS_ERROR)) {
            goto failed;
        }

        if (njs_is_array(&value)) {

            if (njs_is_fast_array(&value)) {
                array = njs_array(&value);

                for (i = 0; i < array->length; i++) {
                    if (chain.last != NULL) {
                        njs_chb_append(&chain, sep.start, sep.size);
                        length += sep.length;
                    }

                    ret = njs_query_string_push(vm, &chain, string,
                                                &array->start[i], &eq, encode);
                    if (njs_slow_path(ret < 0)) {
                        ret = NJS_ERROR;
                        goto failed;
                    }

                    length += ret;
                }

                continue;
            }

            ret = njs_object_length(vm, &value, &len);
            if (njs_slow_path(ret == NJS_ERROR)) {
                goto failed;
            }

            for (i = 0; i < len; i++) {
                ret = njs_value_property_i64(vm, &value, i, &retval);
                if (njs_slow_path(ret == NJS_ERROR)) {
                    goto failed;
                }

                if (chain.last != NULL) {
                    njs_chb_append(&chain, sep.start, sep.size);
                    length += sep.length;
                }

                ret = njs_query_string_push(vm, &chain, string, &retval, &eq,
                                            encode);
                if (njs_slow_path(ret < 0)) {
                    ret = NJS_ERROR;
                    goto failed;
                }

                length += ret;
            }

            continue;
        }

        if (n != 0) {
            njs_chb_append(&chain, sep.start, sep.size);
            length += sep.length;
        }

        ret = njs_query_string_push(vm, &chain, string, &value, &eq, encode);
        if (njs_slow_path(ret < 0)) {
            ret = NJS_ERROR;
            goto failed;
        }

        length += ret;
    }

    size = njs_chb_size(&chain);
    if (njs_slow_path(size < 0)) {
        njs_memory_error(vm);
        return NJS_ERROR;
    }

    p = njs_string_alloc(vm, &vm->retval, size, length);
    if (njs_slow_path(p == NULL)) {
        return NJS_ERROR;
    }

    njs_chb_join_to(&chain, p);

    ret = NJS_OK;

failed:

    njs_chb_destroy(&chain);

    return ret;
}


static njs_int_t
njs_query_string_escape(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t unused)
{
    u_char       *p;
    ssize_t      size, length;
    njs_int_t    ret;
    njs_str_t    str;
    njs_chb_t    chain;
    njs_value_t  *string, value;

    string = njs_arg(args, nargs, 1);

    if (!njs_is_string(string)) {
        ret = njs_value_to_string(vm, &value, string);
        if (njs_slow_path(ret != NJS_OK)) {
            return ret;
        }

        string = &value;
    }

    njs_string_get(string, &str);

    njs_chb_init(&chain, vm->mem_pool);

    length = njs_query_string_encode(&chain, &str);
    if (njs_slow_path(length < 0)) {
        return NJS_ERROR;
    }

    size = njs_chb_size(&chain);

    p = njs_string_alloc(vm, &vm->retval, size, length);
    if (njs_slow_path(p == NULL)) {
        return NJS_ERROR;
    }

    njs_chb_join_to(&chain, p);

    njs_chb_destroy(&chain);

    return NJS_OK;
}


static njs_int_t
njs_query_string_unescape(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t unused)
{
    njs_int_t    ret;
    njs_str_t    str;
    njs_value_t  *string, value;

    string = njs_arg(args, nargs, 1);

    if (!njs_is_string(string)) {
        ret = njs_value_to_string(vm, &value, string);
        if (njs_slow_path(ret != NJS_OK)) {
            return ret;
        }

        string = &value;
    }

    njs_string_get(string, &str);

    return njs_query_string_decode(vm, &vm->retval, str.start, str.length);
}


static const njs_object_prop_t  njs_query_string_object_properties[] =
{
    {
        .type = NJS_PROPERTY,
        .name = njs_string("name"),
        .value = njs_string("querystring"),
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("parse"),
        .value = njs_native_function(njs_query_string_parse, 4),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("stringify"),
        .value = njs_native_function(njs_query_string_stringify, 4),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("escape"),
        .value = njs_native_function(njs_query_string_escape, 1),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("unescape"),
        .value = njs_native_function(njs_query_string_unescape, 1),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("decode"),
        .value = njs_native_function(njs_query_string_parse, 4),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("encode"),
        .value = njs_native_function(njs_query_string_stringify, 4),
        .writable = 1,
        .configurable = 1,
    },
};


const njs_object_init_t  njs_query_string_object_init = {
    njs_query_string_object_properties,
    njs_nitems(njs_query_string_object_properties),
};
