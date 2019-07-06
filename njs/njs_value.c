
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) NGINX, Inc.
 */

#include <njs_core.h>
#include <string.h>


const njs_value_t  njs_value_null =         njs_value(NJS_NULL, 0, 0.0);
const njs_value_t  njs_value_undefined =    njs_value(NJS_UNDEFINED, 0, NAN);
const njs_value_t  njs_value_false =        njs_value(NJS_BOOLEAN, 0, 0.0);
const njs_value_t  njs_value_true =         njs_value(NJS_BOOLEAN, 1, 1.0);
const njs_value_t  njs_value_zero =         njs_value(NJS_NUMBER, 0, 0.0);
const njs_value_t  njs_value_nan =          njs_value(NJS_NUMBER, 0, NAN);
const njs_value_t  njs_value_invalid =      njs_value(NJS_INVALID, 0, 0.0);

const njs_value_t  njs_string_empty =       njs_string("");
const njs_value_t  njs_string_comma =       njs_string(",");
const njs_value_t  njs_string_null =        njs_string("null");
const njs_value_t  njs_string_undefined =   njs_string("undefined");
const njs_value_t  njs_string_boolean =     njs_string("boolean");
const njs_value_t  njs_string_false =       njs_string("false");
const njs_value_t  njs_string_true =        njs_string("true");
const njs_value_t  njs_string_number =      njs_string("number");
const njs_value_t  njs_string_minus_zero =  njs_string("-0");
const njs_value_t  njs_string_minus_infinity =
                                            njs_string("-Infinity");
const njs_value_t  njs_string_plus_infinity =
                                            njs_string("Infinity");
const njs_value_t  njs_string_nan =         njs_string("NaN");
const njs_value_t  njs_string_string =      njs_string("string");
const njs_value_t  njs_string_object =      njs_string("object");
const njs_value_t  njs_string_function =    njs_string("function");
const njs_value_t  njs_string_memory_error = njs_string("MemoryError");


void
njs_value_retain(njs_value_t *value)
{
    njs_string_t  *string;

    if (njs_is_string(value)) {

        if (value->long_string.external != 0xff) {
            string = value->long_string.data;

            nxt_thread_log_debug("retain:%uxD \"%*s\"", string->retain,
                                 value->long_string.size, string->start);

            if (string->retain != 0xffff) {
                string->retain++;
            }
        }
    }
}


void
njs_value_release(njs_vm_t *vm, njs_value_t *value)
{
    njs_string_t  *string;

    if (njs_is_string(value)) {

        if (value->long_string.external != 0xff) {
            string = value->long_string.data;

            nxt_thread_log_debug("release:%uxD \"%*s\"", string->retain,
                                 value->long_string.size, string->start);

            if (string->retain != 0xffff) {
                string->retain--;

#if 0
                if (string->retain == 0) {
                    if ((u_char *) string + sizeof(njs_string_t)
                        != string->start)
                    {
                        nxt_memcache_pool_free(vm->mem_pool,
                                               string->start);
                    }

                    nxt_memcache_pool_free(vm->mem_pool, string);
                }
#endif
            }
        }
    }
}


nxt_bool_t
njs_values_strict_equal(const njs_value_t *val1, const njs_value_t *val2)
{
    size_t        size, length1, length2;
    const u_char  *start1, *start2;

    if (val1->type != val2->type) {
        return 0;
    }

    if (njs_is_numeric(val1)) {

        if (njs_is_undefined(val1)) {
            return 1;
        }

        /* Infinities are handled correctly by comparision. */
        return (val1->data.u.number == val2->data.u.number);
    }

    if (njs_is_string(val1)) {
        size = val1->short_string.size;

        if (size != val2->short_string.size) {
            return 0;
        }

        if (size != NJS_STRING_LONG) {
            length1 = val1->short_string.length;
            length2 = val2->short_string.length;

            /*
             * Using full memcmp() comparison if at least one string
             * is a Byte string.
             */
            if (length1 != 0 && length2 != 0 && length1 != length2) {
                return 0;
            }

            start1 = val1->short_string.start;
            start2 = val2->short_string.start;

        } else {
            size = val1->long_string.size;

            if (size != val2->long_string.size) {
                return 0;
            }

            length1 = val1->long_string.data->length;
            length2 = val2->long_string.data->length;

            /*
             * Using full memcmp() comparison if at least one string
             * is a Byte string.
             */
            if (length1 != 0 && length2 != 0 && length1 != length2) {
                return 0;
            }

            start1 = val1->long_string.data->start;
            start2 = val2->long_string.data->start;
        }

        return (memcmp(start1, start2, size) == 0);
    }

    return (val1->data.u.object == val2->data.u.object);
}


/*
 * A hint value is 0 for numbers and 1 for strings.  The value chooses
 * method calls order specified by ECMAScript 5.1: "valueOf", "toString"
 * for numbers and "toString", "valueOf" for strings.
 */

njs_ret_t
njs_value_to_primitive(njs_vm_t *vm, njs_value_t *value, nxt_uint_t hint)
{
    njs_ret_t           ret;
    njs_value_t         *retval;
    njs_function_t      *function;
    njs_object_prop_t   *prop;
    nxt_lvlhsh_query_t  lhq;

    static const uint32_t  hashes[] = {
        NJS_VALUE_OF_HASH,
        NJS_TO_STRING_HASH,
    };

    static const nxt_str_t  names[] = {
        nxt_string("valueOf"),
        nxt_string("toString"),
    };

    if (!njs_is_primitive(value)) {
        retval = &vm->top_frame->trap_scratch;

        if (!njs_is_primitive(retval)) {

            for ( ;; ) {
                ret = NXT_ERROR;

                if (njs_is_object(value) && vm->top_frame->trap_tries < 2) {
                    hint ^= vm->top_frame->trap_tries++;

                    lhq.key_hash = hashes[hint];
                    lhq.key = names[hint];

                    prop = njs_object_property(vm, value->data.u.object, &lhq);

                    if (nxt_fast_path(prop != NULL)) {

                        if (!njs_is_function(&prop->value)) {
                            /* Try the second method. */
                            continue;
                        }

                        function = prop->value.data.u.function;

                        ret = njs_function_apply(vm, function, value, 1,
                                                 (njs_index_t) retval);
                        /*
                         * njs_function_apply() can return
                         *   NXT_OK, NJS_APPLIED, NXT_ERROR, NXT_AGAIN.
                         */
                        if (nxt_fast_path(ret == NXT_OK)) {

                            if (njs_is_primitive(&vm->retval)) {
                                retval = &vm->retval;
                                break;
                            }

                            /* Try the second method. */
                            continue;
                        }

                        if (ret == NJS_APPLIED) {
                            /*
                             * A user-defined method or continuation have
                             * been prepared to run.  The method will return
                             * to the current instruction and will restart it.
                             */
                            ret = 0;
                        }
                    }
                }

                if (ret == NXT_ERROR) {
                    njs_type_error(vm,
                                   "Cannot convert object to primitive value");
                }

                return ret;
            }
        }

        *value = *retval;

        njs_set_invalid(retval);
    }

    vm->top_frame->trap_tries = 0;

    return 1;
}


njs_array_t *
njs_value_enumerate(njs_vm_t *vm, const njs_value_t *value,
    njs_object_enum_t kind, nxt_bool_t all)
{
    njs_object_value_t  obj_val;

    if (njs_is_object(value)) {
        return njs_object_enumerate(vm, value->data.u.object, kind, all);
    }

    if (value->type != NJS_STRING) {
        return njs_array_alloc(vm, 0, NJS_ARRAY_SPARE);
    }

    obj_val.object = vm->string_object;
    obj_val.value = *value;

    return njs_object_enumerate(vm, (njs_object_t *) &obj_val, kind, all);
}


njs_array_t *
njs_value_own_enumerate(njs_vm_t *vm, const njs_value_t *value,
    njs_object_enum_t kind, nxt_bool_t all)
{
    njs_object_value_t  obj_val;

    if (njs_is_object(value)) {
        return njs_object_own_enumerate(vm, value->data.u.object, kind, all);
    }

    if (value->type != NJS_STRING) {
        return njs_array_alloc(vm, 0, NJS_ARRAY_SPARE);
    }

    obj_val.object = vm->string_object;
    obj_val.value = *value;

    return njs_object_own_enumerate(vm, (njs_object_t *) &obj_val, kind, all);
}


const char *
njs_type_string(njs_value_type_t type)
{
    switch (type) {
    case NJS_NULL:
        return "null";

    case NJS_UNDEFINED:
        return "undefined";

    case NJS_BOOLEAN:
        return "boolean";

    case NJS_NUMBER:
        return "number";

    case NJS_STRING:
        return "string";

    case NJS_EXTERNAL:
        return "external";

    case NJS_INVALID:
        return "invalid";

    case NJS_OBJECT:
        return "object";

    case NJS_ARRAY:
        return "array";

    case NJS_OBJECT_BOOLEAN:
        return "object boolean";

    case NJS_OBJECT_NUMBER:
        return "object number";

    case NJS_OBJECT_STRING:
        return "object string";

    case NJS_FUNCTION:
        return "function";

    case NJS_REGEXP:
        return "regexp";

    case NJS_DATE:
        return "date";

    case NJS_OBJECT_ERROR:
        return "error";

    case NJS_OBJECT_EVAL_ERROR:
        return "eval error";

    case NJS_OBJECT_INTERNAL_ERROR:
        return "internal error";

    case NJS_OBJECT_RANGE_ERROR:
        return "range error";

    case NJS_OBJECT_REF_ERROR:
        return "reference error";

    case NJS_OBJECT_SYNTAX_ERROR:
        return "syntax error";

    case NJS_OBJECT_TYPE_ERROR:
        return "type error";

    case NJS_OBJECT_URI_ERROR:
        return "uri error";

    default:
        return NULL;
    }
}


const char *
njs_arg_type_string(uint8_t arg)
{
    switch (arg) {
    case NJS_SKIP_ARG:
        return "skip";

    case NJS_NUMBER_ARG:
        return "number";

    case NJS_INTEGER_ARG:
        return "integer";

    case NJS_STRING_ARG:
        return "string";

    case NJS_OBJECT_ARG:
        return "object";

    case NJS_STRING_OBJECT_ARG:
        return "string object";

    case NJS_FUNCTION_ARG:
        return "function";

    case NJS_REGEXP_ARG:
        return "regexp";

    case NJS_DATE_ARG:
        return "date";

    default:
        return "unknown";
    }
}
