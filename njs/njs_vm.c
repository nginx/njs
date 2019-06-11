
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) NGINX, Inc.
 */

#include <njs_core.h>
#include <njs_regexp.h>
#include <string.h>


struct njs_property_next_s {
    uint32_t     index;
    njs_array_t  *array;
};


/*
 * These functions are forbidden to inline to minimize JavaScript VM
 * interpreter memory footprint.  The size is less than 8K on AMD64
 * and should fit in CPU L1 instruction cache.
 */

static nxt_noinline njs_ret_t njs_string_concat(njs_vm_t *vm,
    njs_value_t *val1, njs_value_t *val2);
static nxt_noinline njs_ret_t njs_values_equal(njs_vm_t *vm,
    const njs_value_t *val1, const njs_value_t *val2);
static nxt_noinline njs_ret_t njs_values_compare(njs_vm_t *vm,
    const njs_value_t *val1, const njs_value_t *val2);
static njs_ret_t njs_function_frame_create(njs_vm_t *vm, njs_value_t *value,
    const njs_value_t *this, uintptr_t nargs, nxt_bool_t ctor);
static njs_object_t *njs_function_new_object(njs_vm_t *vm, njs_value_t *value);
static void njs_vm_scopes_restore(njs_vm_t *vm, njs_frame_t *frame,
    njs_native_frame_t *previous);
static njs_ret_t njs_vmcode_continuation(njs_vm_t *vm, njs_value_t *invld1,
    njs_value_t *invld2);

static void njs_vm_trap(njs_vm_t *vm, njs_trap_t trap, njs_value_t *value1,
    njs_value_t *value2);
static void njs_vm_trap_argument(njs_vm_t *vm, njs_trap_t trap);
static njs_ret_t njs_vmcode_number_primitive(njs_vm_t *vm, njs_value_t *invld,
    njs_value_t *narg);
static njs_ret_t njs_vmcode_string_primitive(njs_vm_t *vm, njs_value_t *invld,
    njs_value_t *narg);
static njs_ret_t njs_vmcode_addition_primitive(njs_vm_t *vm, njs_value_t *invld,
    njs_value_t *narg);
static njs_ret_t njs_vmcode_comparison_primitive(njs_vm_t *vm,
    njs_value_t *invld, njs_value_t *narg);
static njs_ret_t njs_vmcode_number_argument(njs_vm_t *vm, njs_value_t *invld1,
    njs_value_t *inlvd2);
static njs_ret_t njs_vmcode_string_argument(njs_vm_t *vm, njs_value_t *invld1,
    njs_value_t *inlvd2);
static njs_ret_t njs_vmcode_primitive_argument(njs_vm_t *vm,
    njs_value_t *invld1, njs_value_t *inlvd2);
static njs_ret_t njs_primitive_value(njs_vm_t *vm, njs_value_t *value,
    nxt_uint_t hint);
static njs_ret_t njs_vmcode_restart(njs_vm_t *vm, njs_value_t *invld1,
    njs_value_t *invld2);
static njs_ret_t njs_object_value_to_string(njs_vm_t *vm, njs_value_t *value);
static njs_ret_t njs_vmcode_value_to_string(njs_vm_t *vm, njs_value_t *invld1,
    njs_value_t *invld2);

static njs_ret_t njs_vm_add_backtrace_entry(njs_vm_t *vm, njs_frame_t *frame);

void njs_debug(njs_index_t index, njs_value_t *value);


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


const nxt_str_t  njs_entry_main =           nxt_string("main");
const nxt_str_t  njs_entry_module =         nxt_string("module");
const nxt_str_t  njs_entry_native =         nxt_string("native");
const nxt_str_t  njs_entry_unknown =        nxt_string("unknown");
const nxt_str_t  njs_entry_anonymous =      nxt_string("anonymous");


/*
 * The nJSVM is optimized for an ABIs where the first several arguments
 * are passed in registers (AMD64, ARM32/64): two pointers to the operand
 * values is passed as arguments although they are not always used.
 */

nxt_noinline nxt_int_t
njs_vmcode_interpreter(njs_vm_t *vm)
{
    u_char                *catch;
    njs_ret_t             ret;
    njs_trap_t            trap;
    njs_value_t           *retval, *value1, *value2;
    njs_frame_t           *frame;
    njs_native_frame_t    *previous;
    njs_vmcode_generic_t  *vmcode;

start:

    for ( ;; ) {

        vmcode = (njs_vmcode_generic_t *) vm->current;

        /*
         * The first operand is passed as is in value2 to
         *   njs_vmcode_jump(),
         *   njs_vmcode_if_true_jump(),
         *   njs_vmcode_if_false_jump(),
         *   njs_vmcode_validate(),
         *   njs_vmcode_function_frame(),
         *   njs_vmcode_function_call(),
         *   njs_vmcode_return(),
         *   njs_vmcode_try_start(),
         *   njs_vmcode_try_continue(),
         *   njs_vmcode_try_break(),
         *   njs_vmcode_try_end(),
         *   njs_vmcode_catch().
         *   njs_vmcode_throw().
         *   njs_vmcode_stop().
         */
        value2 = (njs_value_t *) vmcode->operand1;
        value1 = NULL;

        switch (vmcode->code.operands) {

        case NJS_VMCODE_3OPERANDS:
            value2 = njs_vmcode_operand(vm, vmcode->operand3);

            /* Fall through. */

        case NJS_VMCODE_2OPERANDS:
            value1 = njs_vmcode_operand(vm, vmcode->operand2);
        }

        ret = vmcode->code.operation(vm, value1, value2);

        /*
         * On success an operation returns size of the bytecode,
         * a jump offset or zero after the call or return operations.
         * Jumps can return a negative offset.  Compilers can generate
         *    (ret < 0 && ret >= NJS_PREEMPT)
         * as a single unsigned comparision.
         */

        if (nxt_slow_path(ret < 0 && ret >= NJS_PREEMPT)) {
            break;
        }

        vm->current += ret;

        if (vmcode->code.retval) {
            retval = njs_vmcode_operand(vm, vmcode->operand1);
            njs_release(vm, retval);
            *retval = vm->retval;
        }
    }

    if (ret == NJS_TRAP) {
        trap = vm->trap;

        switch (trap) {

        case NJS_TRAP_NUMBER:
            value2 = value1;

            /* Fall through. */

        case NJS_TRAP_NUMBERS:
        case NJS_TRAP_ADDITION:
        case NJS_TRAP_COMPARISON:
        case NJS_TRAP_INCDEC:
        case NJS_TRAP_PROPERTY:

            njs_vm_trap(vm, trap, value1, value2);

            goto start;

        case NJS_TRAP_NUMBER_ARG:
        case NJS_TRAP_STRING_ARG:
        case NJS_TRAP_PRIMITIVE_ARG:

            njs_vm_trap_argument(vm, trap);

            goto start;

        default:
            ret = NXT_ERROR;
            break;
        }
    }

    if (ret == NXT_ERROR) {

        for ( ;; ) {
            frame = (njs_frame_t *) vm->top_frame;
            catch = frame->native.exception.catch;

            if (catch != NULL) {
                vm->current = catch;

                if (vm->debug != NULL) {
                    nxt_array_reset(vm->backtrace);
                }

                goto start;
            }

            if (vm->debug != NULL
                && njs_vm_add_backtrace_entry(vm, frame) != NXT_OK)
            {
                return NXT_ERROR;
            }

            previous = frame->native.previous;
            if (previous == NULL) {
                return NXT_ERROR;
            }

            njs_vm_scopes_restore(vm, frame, previous);

            if (frame->native.size != 0) {
                vm->stack_size -= frame->native.size;
                nxt_mp_free(vm->mem_pool, frame);
            }
        }
    }

    /* NXT_AGAIN, NJS_STOP. */

    return ret;
}


nxt_noinline void
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


nxt_noinline void
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


njs_ret_t
njs_vmcode_object(njs_vm_t *vm, njs_value_t *invld1, njs_value_t *invld2)
{
    njs_object_t  *object;

    object = njs_object_alloc(vm);

    if (nxt_fast_path(object != NULL)) {
        vm->retval.data.u.object = object;
        vm->retval.type = NJS_OBJECT;
        vm->retval.data.truth = 1;

        return sizeof(njs_vmcode_object_t);
    }

    return NXT_ERROR;
}


njs_ret_t
njs_vmcode_array(njs_vm_t *vm, njs_value_t *invld1, njs_value_t *invld2)
{
    uint32_t            length;
    njs_array_t         *array;
    njs_value_t         *value;
    njs_vmcode_array_t  *code;

    code = (njs_vmcode_array_t *) vm->current;

    array = njs_array_alloc(vm, code->length, NJS_ARRAY_SPARE);

    if (nxt_fast_path(array != NULL)) {

        if (code->code.ctor) {
            /* Array of the form [,,,], [1,,]. */
            value = array->start;
            length = array->length;

            do {
                njs_set_invalid(value);
                value++;
                length--;
            } while (length != 0);

        } else {
            /* Array of the form [], [,,1], [1,2,3]. */
            array->length = 0;
        }

        vm->retval.data.u.array = array;
        vm->retval.type = NJS_ARRAY;
        vm->retval.data.truth = 1;

        return sizeof(njs_vmcode_array_t);
    }

    return NXT_ERROR;
}


njs_ret_t
njs_vmcode_function(njs_vm_t *vm, njs_value_t *invld1, njs_value_t *invld2)
{
    njs_function_t         *function;
    njs_function_lambda_t  *lambda;
    njs_vmcode_function_t  *code;

    code = (njs_vmcode_function_t *) vm->current;
    lambda = code->lambda;

    function = njs_function_alloc(vm, lambda, vm->active_frame->closures, 0);
    if (nxt_slow_path(function == NULL)) {
        return NXT_ERROR;
    }

    vm->retval.data.u.function = function;
    vm->retval.type = NJS_FUNCTION;
    vm->retval.data.truth = 1;

    return sizeof(njs_vmcode_function_t);
}


njs_ret_t
njs_vmcode_this(njs_vm_t *vm, njs_value_t *invld1, njs_value_t *invld2)
{
    njs_frame_t        *frame;
    njs_value_t        *value;
    njs_vmcode_this_t  *code;

    frame = (njs_frame_t *) vm->active_frame;
    code = (njs_vmcode_this_t *) vm->current;

    value = njs_vmcode_operand(vm, code->dst);
    *value = frame->native.arguments[0];

    return sizeof(njs_vmcode_this_t);
}


njs_ret_t
njs_vmcode_arguments(njs_vm_t *vm, njs_value_t *invld1, njs_value_t *invld2)
{
    nxt_int_t               ret;
    njs_frame_t             *frame;
    njs_value_t             *value;
    njs_vmcode_arguments_t  *code;

    frame = (njs_frame_t *) vm->active_frame;

    if (frame->native.arguments_object == NULL) {
        ret = njs_function_arguments_object_init(vm, &frame->native);
        if (nxt_slow_path(ret != NXT_OK)) {
            return NXT_ERROR;
        }
    }

    code = (njs_vmcode_arguments_t *) vm->current;

    value = njs_vmcode_operand(vm, code->dst);
    value->data.u.object = frame->native.arguments_object;
    value->type = NJS_OBJECT;
    value->data.truth = 1;

    return sizeof(njs_vmcode_arguments_t);
}


njs_ret_t
njs_vmcode_regexp(njs_vm_t *vm, njs_value_t *invld1, njs_value_t *invld2)
{
    njs_regexp_t         *regexp;
    njs_vmcode_regexp_t  *code;

    code = (njs_vmcode_regexp_t *) vm->current;

    regexp = njs_regexp_alloc(vm, code->pattern);

    if (nxt_fast_path(regexp != NULL)) {
        vm->retval.data.u.regexp = regexp;
        vm->retval.type = NJS_REGEXP;
        vm->retval.data.truth = 1;

        return sizeof(njs_vmcode_regexp_t);
    }

    return NXT_ERROR;
}


njs_ret_t
njs_vmcode_template_literal(njs_vm_t *vm, njs_value_t *invld1,
    njs_value_t *retval)
{
    nxt_int_t    ret;
    njs_array_t  *array;
    njs_value_t  *value;

    static const njs_function_t  concat = {
          .native = 1,
          .args_offset = 1,
          .u.native = njs_string_prototype_concat
    };

    value = njs_vmcode_operand(vm, retval);

    if (!njs_is_primitive(value)) {
        array = value->data.u.array;

        ret = njs_function_activate(vm, (njs_function_t *) &concat,
                                    &njs_string_empty, array->start,
                                    array->length, (njs_index_t) retval, 0);
        if (ret == NJS_APPLIED) {
            return 0;
        }

        return NXT_ERROR;
    }

    return sizeof(njs_vmcode_template_literal_t);
}


njs_ret_t
njs_vmcode_object_copy(njs_vm_t *vm, njs_value_t *value, njs_value_t *invld)
{
    njs_object_t    *object;
    njs_function_t  *function;

    switch (value->type) {

    case NJS_OBJECT:
        object = njs_object_value_copy(vm, value);
        if (nxt_slow_path(object == NULL)) {
            return NXT_ERROR;
        }

        break;

    case NJS_FUNCTION:
        function = njs_function_value_copy(vm, value);
        if (nxt_slow_path(function == NULL)) {
            return NXT_ERROR;
        }

        break;

    default:
        break;
    }

    vm->retval = *value;

    njs_retain(value);

    return sizeof(njs_vmcode_object_copy_t);
}


njs_ret_t
njs_vmcode_property_get(njs_vm_t *vm, njs_value_t *object,
    njs_value_t *property)
{
    njs_ret_t              ret;
    njs_value_t            *retval;
    njs_vmcode_prop_get_t  *code;

    code = (njs_vmcode_prop_get_t *) vm->current;
    retval = njs_vmcode_operand(vm, code->value);

    ret = njs_value_property(vm, object, property, retval,
                             sizeof(njs_vmcode_prop_get_t));
    if (ret == NXT_OK || ret == NXT_DECLINED) {
        vm->retval = *retval;
        return sizeof(njs_vmcode_prop_get_t);
    }

    return (ret == NJS_APPLIED) ? 0 : ret;
}


njs_ret_t
njs_vmcode_property_init(njs_vm_t *vm, njs_value_t *object,
    njs_value_t *property)
{
    uint32_t               index, size;
    njs_ret_t              ret;
    njs_array_t            *array;
    njs_value_t            *init, *value, name;
    njs_object_t           *obj;
    njs_object_prop_t      *prop;
    nxt_lvlhsh_query_t     lhq;
    njs_vmcode_prop_set_t  *code;

    code = (njs_vmcode_prop_set_t *) vm->current;
    init = njs_vmcode_operand(vm, code->value);

    switch (object->type) {
    case NJS_ARRAY:
        index = njs_value_to_index(property);
        if (nxt_slow_path(index == NJS_ARRAY_INVALID_INDEX)) {
            njs_internal_error(vm,
                               "invalid index while property initialization");
            return NXT_ERROR;
        }

        array = object->data.u.array;

        if (index >= array->length) {
            size = index - array->length;

            ret = njs_array_expand(vm, array, 0, size + 1);
            if (nxt_slow_path(ret != NXT_OK)) {
                return ret;
            }

            value = &array->start[array->length];

            while (size != 0) {
                njs_set_invalid(value);
                value++;
                size--;
            }

            array->length = index + 1;
        }

        /* GC: retain. */
        array->start[index] = *init;

        break;

    case NJS_OBJECT:
        ret = njs_primitive_value_to_string(vm, &name, property);
        if (nxt_slow_path(ret != NXT_OK)) {
            njs_internal_error(vm, "failed conversion of type \"%s\" "
                               "to string while property initialization",
                               njs_type_string(property->type));
            return NXT_ERROR;
        }

        njs_string_get(&name, &lhq.key);
        lhq.key_hash = nxt_djb_hash(lhq.key.start, lhq.key.length);
        lhq.proto = &njs_object_hash_proto;
        lhq.pool = vm->mem_pool;

        obj = object->data.u.object;

        ret = nxt_lvlhsh_find(&obj->__proto__->shared_hash, &lhq);
        if (ret == NXT_OK) {
            prop = lhq.value;

            if (prop->type == NJS_PROPERTY_HANDLER) {
                ret = prop->value.data.u.prop_handler(vm, object, init,
                                                      &vm->retval);
                if (nxt_slow_path(ret != NXT_OK)) {
                    return ret;
                }

                break;
            }
        }

        prop = njs_object_prop_alloc(vm, &name, init, 1);
        if (nxt_slow_path(prop == NULL)) {
            return NXT_ERROR;
        }

        lhq.value = prop;
        lhq.replace = 1;

        ret = nxt_lvlhsh_insert(&obj->hash, &lhq);
        if (nxt_slow_path(ret != NXT_OK)) {
            njs_internal_error(vm, "lvlhsh insert/replace failed");
            return NXT_ERROR;
        }

        break;

    default:
        njs_internal_error(vm, "unexpected object type \"%s\" "
                           "while property initialization",
                           njs_type_string(object->type));

        return NXT_ERROR;
    }

    return sizeof(njs_vmcode_prop_set_t);
}


njs_ret_t
njs_vmcode_property_set(njs_vm_t *vm, njs_value_t *object,
    njs_value_t *property)
{
    njs_ret_t              ret;
    njs_value_t            *value;
    njs_vmcode_prop_set_t  *code;

    code = (njs_vmcode_prop_set_t *) vm->current;
    value = njs_vmcode_operand(vm, code->value);

    ret = njs_value_property_set(vm, object, property, value,
                                 sizeof(njs_vmcode_prop_set_t));
    if (ret == NXT_OK) {
        return sizeof(njs_vmcode_prop_set_t);
    }

    return (ret == NJS_APPLIED) ? 0 : ret;
}


njs_ret_t
njs_vmcode_property_in(njs_vm_t *vm, njs_value_t *object, njs_value_t *property)
{
    njs_ret_t             ret;
    njs_object_prop_t     *prop;
    const njs_value_t     *retval;
    njs_property_query_t  pq;

    retval = &njs_value_false;

    njs_property_query_init(&pq, NJS_PROPERTY_QUERY_GET, 0);

    ret = njs_property_query(vm, &pq, object, property);

    switch (ret) {

    case NXT_OK:
        prop = pq.lhq.value;

        if (!njs_is_valid(&prop->value)) {
            break;
        }

        retval = &njs_value_true;
        break;

    case NXT_DECLINED:
        if (!njs_is_object(object) && !njs_is_external(object)) {
            njs_type_error(vm, "property in on a primitive value");

            return NXT_ERROR;
        }

        break;

    case NJS_TRAP:
    case NXT_ERROR:
    default:

        return ret;
    }

    vm->retval = *retval;

    return sizeof(njs_vmcode_3addr_t);
}


njs_ret_t
njs_vmcode_property_delete(njs_vm_t *vm, njs_value_t *object,
    njs_value_t *property)
{
    njs_ret_t             ret;
    njs_object_prop_t     *prop, *whipeout;
    njs_property_query_t  pq;

    njs_property_query_init(&pq, NJS_PROPERTY_QUERY_DELETE, 1);

    ret = njs_property_query(vm, &pq, object, property);

    switch (ret) {

    case NXT_OK:
        prop = pq.lhq.value;

        if (nxt_slow_path(!prop->configurable)) {
            njs_type_error(vm, "Cannot delete property \"%V\" of %s",
                           &pq.lhq.key, njs_type_string(object->type));
            return NXT_ERROR;
        }

        if (nxt_slow_path(pq.shared)) {
            whipeout = nxt_mp_align(vm->mem_pool, sizeof(njs_value_t),
                                    sizeof(njs_object_prop_t));
            if (nxt_slow_path(whipeout == NULL)) {
                njs_memory_error(vm);
                return NXT_ERROR;
            }

            njs_set_invalid(&whipeout->value);
            whipeout->name = prop->name;
            whipeout->type = NJS_WHITEOUT;

            pq.lhq.replace = 0;
            pq.lhq.value = whipeout;
            pq.lhq.pool = vm->mem_pool;

            ret = nxt_lvlhsh_insert(&pq.prototype->hash, &pq.lhq);
            if (nxt_slow_path(ret != NXT_OK)) {
                njs_internal_error(vm, "lvlhsh insert failed");
                return NXT_ERROR;
            }

            break;
        }

        switch (prop->type) {
        case NJS_PROPERTY:
        case NJS_METHOD:
            break;

        case NJS_PROPERTY_REF:
            njs_set_invalid(prop->value.data.u.value);
            goto done;

        case NJS_PROPERTY_HANDLER:
            ret = prop->value.data.u.prop_handler(vm, object, NULL, NULL);
            if (nxt_slow_path(ret != NXT_OK)) {
                return ret;
            }

            goto done;

        default:
            njs_internal_error(vm, "unexpected property type \"%s\" "
                               "while deleting",
                               njs_prop_type_string(prop->type));

            return NXT_ERROR;
        }

        /* GC: release value. */
        prop->type = NJS_WHITEOUT;
        njs_set_invalid(&prop->value);

        break;

    case NXT_DECLINED:
        break;

    case NJS_TRAP:
    case NXT_ERROR:
    default:

        return ret;
    }

done:

    vm->retval = njs_value_true;

    return sizeof(njs_vmcode_3addr_t);
}


njs_ret_t
njs_vmcode_property_foreach(njs_vm_t *vm, njs_value_t *object,
    njs_value_t *invld)
{
    void                       *obj;
    njs_ret_t                  ret;
    njs_property_next_t        *next;
    const njs_extern_t         *ext_proto;
    njs_vmcode_prop_foreach_t  *code;

    if (njs_is_external(object)) {
        ext_proto = object->external.proto;

        if (ext_proto->foreach != NULL) {
            obj = njs_extern_object(vm, object);

            ret = ext_proto->foreach(vm, obj, &vm->retval);
            if (nxt_slow_path(ret != NXT_OK)) {
                return ret;
            }
        }

        goto done;
    }

    next = nxt_mp_alloc(vm->mem_pool, sizeof(njs_property_next_t));
    if (nxt_slow_path(next == NULL)) {
        njs_memory_error(vm);
        return NXT_ERROR;
    }

    next->index = 0;
    next->array = njs_value_enumerate(vm, object, NJS_ENUM_KEYS, 0);
    if (nxt_slow_path(next->array == NULL)) {
        njs_memory_error(vm);
        return NXT_ERROR;
    }

    vm->retval.data.u.next = next;

done:

    code = (njs_vmcode_prop_foreach_t *) vm->current;

    return code->offset;
}


njs_ret_t
njs_vmcode_property_next(njs_vm_t *vm, njs_value_t *object, njs_value_t *value)
{
    void                    *obj;
    njs_ret_t               ret;
    njs_value_t             *retval;
    njs_property_next_t     *next;
    const njs_extern_t      *ext_proto;
    njs_vmcode_prop_next_t  *code;

    code = (njs_vmcode_prop_next_t *) vm->current;
    retval = njs_vmcode_operand(vm, code->retval);

    if (njs_is_external(object)) {
        ext_proto = object->external.proto;

        if (ext_proto->next != NULL) {
            obj = njs_extern_object(vm, object);

            ret = ext_proto->next(vm, retval, obj, value);

            if (ret == NXT_OK) {
                return code->offset;
            }

            if (nxt_slow_path(ret == NXT_ERROR)) {
                return ret;
            }

            /* ret == NJS_DONE. */
        }

        return sizeof(njs_vmcode_prop_next_t);
    }

    next = value->data.u.next;

    if (next->index < next->array->length) {
        *retval = next->array->data[next->index++];

        return code->offset;
    }

    nxt_mp_free(vm->mem_pool, next);

    return sizeof(njs_vmcode_prop_next_t);
}


njs_ret_t
njs_vmcode_instance_of(njs_vm_t *vm, njs_value_t *object,
    njs_value_t *constructor)
{
    nxt_int_t          ret;
    njs_value_t        value;
    njs_object_t       *prototype, *proto;
    const njs_value_t  *retval;

    static njs_value_t prototype_string = njs_string("prototype");

    if (!njs_is_function(constructor)) {
        njs_type_error(vm, "right argument is not a function");
        return NXT_ERROR;
    }

    retval = &njs_value_false;

    if (njs_is_object(object)) {
        value = njs_value_undefined;
        ret = njs_value_property(vm, constructor, &prototype_string, &value, 0);

        if (nxt_slow_path(ret == NJS_APPLIED)) {
            njs_internal_error(vm, "getter is not supported in instanceof");
            return NXT_ERROR;
        }

        if (nxt_fast_path(ret == NXT_OK)) {

            if (nxt_slow_path(!njs_is_object(&value))) {
                njs_internal_error(vm, "prototype is not an object");
                return NXT_ERROR;
            }

            prototype = value.data.u.object;
            proto = object->data.u.object;

            do {
                proto = proto->__proto__;

                if (proto == prototype) {
                    retval = &njs_value_true;
                    break;
                }

            } while (proto != NULL);
        }
    }

    vm->retval = *retval;

    return sizeof(njs_vmcode_instance_of_t);
}


/*
 * The increment and decrement operations require only one value parameter.
 * However, if the value is not numeric, then the trap is generated and
 * value parameter points to a trap frame value converted to a numeric.
 * So the additional reference parameter points to the original value.
 */

njs_ret_t
njs_vmcode_increment(njs_vm_t *vm, njs_value_t *reference, njs_value_t *value)
{
    double  num;

    if (nxt_fast_path(njs_is_numeric(value))) {
        num = value->data.u.number + 1.0;

        njs_release(vm, reference);

        njs_value_number_set(reference, num);
        vm->retval = *reference;

        return sizeof(njs_vmcode_3addr_t);
    }

    return njs_trap(vm, NJS_TRAP_INCDEC);
}


njs_ret_t
njs_vmcode_decrement(njs_vm_t *vm, njs_value_t *reference, njs_value_t *value)
{
    double  num;

    if (nxt_fast_path(njs_is_numeric(value))) {
        num = value->data.u.number - 1.0;

        njs_release(vm, reference);

        njs_value_number_set(reference, num);
        vm->retval = *reference;

        return sizeof(njs_vmcode_3addr_t);
    }

    return njs_trap(vm, NJS_TRAP_INCDEC);
}


njs_ret_t
njs_vmcode_post_increment(njs_vm_t *vm, njs_value_t *reference,
    njs_value_t *value)
{
    double  num;

    if (nxt_fast_path(njs_is_numeric(value))) {
        num = value->data.u.number;

        njs_release(vm, reference);

        njs_value_number_set(reference, num + 1.0);
        njs_value_number_set(&vm->retval, num);

        return sizeof(njs_vmcode_3addr_t);
    }

    return njs_trap(vm, NJS_TRAP_INCDEC);
}


njs_ret_t
njs_vmcode_post_decrement(njs_vm_t *vm, njs_value_t *reference,
    njs_value_t *value)
{
    double  num;

    if (nxt_fast_path(njs_is_numeric(value))) {
        num = value->data.u.number;

        njs_release(vm, reference);

        njs_value_number_set(reference, num - 1.0);
        njs_value_number_set(&vm->retval, num);

        return sizeof(njs_vmcode_3addr_t);
    }

    return njs_trap(vm, NJS_TRAP_INCDEC);
}


njs_ret_t
njs_vmcode_typeof(njs_vm_t *vm, njs_value_t *value, njs_value_t *invld)
{
    /* ECMAScript 5.1: null, array and regexp are objects. */

    static const njs_value_t  *types[NJS_TYPE_MAX] = {
        &njs_string_object,
        &njs_string_undefined,
        &njs_string_boolean,
        &njs_string_number,
        &njs_string_string,
        &njs_string_undefined,
        &njs_string_undefined,
        &njs_string_undefined,
        &njs_string_undefined,
        &njs_string_undefined,
        &njs_string_undefined,
        &njs_string_undefined,
        &njs_string_undefined,
        &njs_string_undefined,
        &njs_string_undefined,
        &njs_string_undefined,

        &njs_string_object,
        &njs_string_object,
        &njs_string_object,
        &njs_string_object,
        &njs_string_object,
        &njs_string_function,
        &njs_string_object,
        &njs_string_object,
        &njs_string_object,
        &njs_string_object,
        &njs_string_object,
        &njs_string_object,
        &njs_string_object,
        &njs_string_object,
        &njs_string_object,
        &njs_string_object,
        &njs_string_object,
    };

    vm->retval = *types[value->type];

    return sizeof(njs_vmcode_2addr_t);
}


njs_ret_t
njs_vmcode_void(njs_vm_t *vm, njs_value_t *invld1, njs_value_t *invld2)
{
    vm->retval = njs_value_undefined;

    return sizeof(njs_vmcode_2addr_t);
}


njs_ret_t
njs_vmcode_delete(njs_vm_t *vm, njs_value_t *value, njs_value_t *invld)
{
    njs_release(vm, value);

    vm->retval = njs_value_true;

    return sizeof(njs_vmcode_2addr_t);
}


njs_ret_t
njs_vmcode_unary_plus(njs_vm_t *vm, njs_value_t *value, njs_value_t *invld)
{
    if (nxt_fast_path(njs_is_numeric(value))) {
        njs_value_number_set(&vm->retval, value->data.u.number);
        return sizeof(njs_vmcode_2addr_t);
    }

    return njs_trap(vm, NJS_TRAP_NUMBER);
}


njs_ret_t
njs_vmcode_unary_negation(njs_vm_t *vm, njs_value_t *value, njs_value_t *invld)
{
    if (nxt_fast_path(njs_is_numeric(value))) {
        njs_value_number_set(&vm->retval, - value->data.u.number);
        return sizeof(njs_vmcode_2addr_t);
    }

    return njs_trap(vm, NJS_TRAP_NUMBER);
}


njs_ret_t
njs_vmcode_addition(njs_vm_t *vm, njs_value_t *val1, njs_value_t *val2)
{
    double       num;
    njs_ret_t    ret;
    njs_value_t  *s1, *s2, *src, dst;

    if (nxt_fast_path(njs_is_numeric(val1) && njs_is_numeric(val2))) {

        num = val1->data.u.number + val2->data.u.number;
        njs_value_number_set(&vm->retval, num);

        return sizeof(njs_vmcode_3addr_t);
    }

    if (nxt_fast_path(njs_is_string(val1) && njs_is_string(val2))) {
        return njs_string_concat(vm, val1, val2);
    }

    if (nxt_fast_path(njs_is_primitive(val1) && njs_is_primitive(val2))) {

        if (njs_is_string(val1)) {
            s1 = val1;
            s2 = &dst;
            src = val2;

        } else {
            s1 = &dst;
            s2 = val2;
            src = val1;
        }

        ret = njs_primitive_value_to_string(vm, &dst, src);

        if (nxt_fast_path(ret == NXT_OK)) {
            return njs_string_concat(vm, s1, s2);
        }

        return ret;
    }

    return njs_trap(vm, NJS_TRAP_ADDITION);
}


static nxt_noinline njs_ret_t
njs_string_concat(njs_vm_t *vm, njs_value_t *val1, njs_value_t *val2)
{
    u_char             *start;
    size_t             size, length;
    njs_string_prop_t  string1, string2;

    (void) njs_string_prop(&string1, val1);
    (void) njs_string_prop(&string2, val2);

    /*
     * A result of concatenation of Byte and ASCII or UTF-8 strings
     * is a Byte string.
     */
    if ((string1.length != 0 || string1.size == 0)
        && (string2.length != 0 || string2.size == 0))
    {
        length = string1.length + string2.length;

    } else {
        length = 0;
    }

    size = string1.size + string2.size;

    start = njs_string_alloc(vm, &vm->retval, size, length);

    if (nxt_slow_path(start == NULL)) {
        return NXT_ERROR;
    }

    (void) memcpy(start, string1.start, string1.size);
    (void) memcpy(start + string1.size, string2.start, string2.size);

    return sizeof(njs_vmcode_3addr_t);
}


njs_ret_t
njs_vmcode_substraction(njs_vm_t *vm, njs_value_t *val1, njs_value_t *val2)
{
    double  num;

    if (nxt_fast_path(njs_is_numeric(val1) && njs_is_numeric(val2))) {

        num = val1->data.u.number - val2->data.u.number;
        njs_value_number_set(&vm->retval, num);

        return sizeof(njs_vmcode_3addr_t);
    }

    return njs_trap(vm, NJS_TRAP_NUMBERS);
}


njs_ret_t
njs_vmcode_multiplication(njs_vm_t *vm, njs_value_t *val1, njs_value_t *val2)
{
    double  num;

    if (nxt_fast_path(njs_is_numeric(val1) && njs_is_numeric(val2))) {

        num = val1->data.u.number * val2->data.u.number;
        njs_value_number_set(&vm->retval, num);

        return sizeof(njs_vmcode_3addr_t);
    }

    return njs_trap(vm, NJS_TRAP_NUMBERS);
}


njs_ret_t
njs_vmcode_exponentiation(njs_vm_t *vm, njs_value_t *val1, njs_value_t *val2)
{
    double      num, base, exponent;
    nxt_bool_t  valid;

    if (nxt_fast_path(njs_is_numeric(val1) && njs_is_numeric(val2))) {
        base = val1->data.u.number;
        exponent = val2->data.u.number;

        /*
         * According to ES7:
         *  1. If exponent is NaN, the result should be NaN;
         *  2. The result of +/-1 ** +/-Infinity should be NaN.
         */
        valid = nxt_expect(1, fabs(base) != 1
                              || (!isnan(exponent) && !isinf(exponent)));

        if (valid) {
            num = pow(base, exponent);

        } else {
            num = NAN;
        }

        njs_value_number_set(&vm->retval, num);

        return sizeof(njs_vmcode_3addr_t);
    }

    return njs_trap(vm, NJS_TRAP_NUMBERS);
}


njs_ret_t
njs_vmcode_division(njs_vm_t *vm, njs_value_t *val1, njs_value_t *val2)
{
    double  num;

    if (nxt_fast_path(njs_is_numeric(val1) && njs_is_numeric(val2))) {

        num = val1->data.u.number / val2->data.u.number;
        njs_value_number_set(&vm->retval, num);

        return sizeof(njs_vmcode_3addr_t);
    }

    return njs_trap(vm, NJS_TRAP_NUMBERS);
}


njs_ret_t
njs_vmcode_remainder(njs_vm_t *vm, njs_value_t *val1, njs_value_t *val2)
{
    double  num;

    if (nxt_fast_path(njs_is_numeric(val1) && njs_is_numeric(val2))) {

        num = fmod(val1->data.u.number, val2->data.u.number);
        njs_value_number_set(&vm->retval, num);

        return sizeof(njs_vmcode_3addr_t);
    }

    return njs_trap(vm, NJS_TRAP_NUMBERS);
}


njs_ret_t
njs_vmcode_left_shift(njs_vm_t *vm, njs_value_t *val1, njs_value_t *val2)
{
    int32_t   num1;
    uint32_t  num2;

    if (nxt_fast_path(njs_is_numeric(val1) && njs_is_numeric(val2))) {

        num1 = njs_number_to_int32(val1->data.u.number);
        num2 = njs_number_to_uint32(val2->data.u.number);
        njs_value_number_set(&vm->retval, num1 << (num2 & 0x1f));

        return sizeof(njs_vmcode_3addr_t);
    }

    return njs_trap(vm, NJS_TRAP_NUMBERS);
}


njs_ret_t
njs_vmcode_right_shift(njs_vm_t *vm, njs_value_t *val1, njs_value_t *val2)
{
    int32_t   num1;
    uint32_t  num2;

    if (nxt_fast_path(njs_is_numeric(val1) && njs_is_numeric(val2))) {

        num1 = njs_number_to_int32(val1->data.u.number);
        num2 = njs_number_to_uint32(val2->data.u.number);
        njs_value_number_set(&vm->retval, num1 >> (num2 & 0x1f));

        return sizeof(njs_vmcode_3addr_t);
    }

    return njs_trap(vm, NJS_TRAP_NUMBERS);
}


njs_ret_t
njs_vmcode_unsigned_right_shift(njs_vm_t *vm, njs_value_t *val1,
    njs_value_t *val2)
{
    uint32_t  num1, num2;

    if (nxt_fast_path(njs_is_numeric(val1) && njs_is_numeric(val2))) {

        num1 = njs_number_to_uint32(val1->data.u.number);
        num2 = njs_number_to_uint32(val2->data.u.number);
        njs_value_number_set(&vm->retval, num1 >> (num2 & 0x1f));

        return sizeof(njs_vmcode_3addr_t);
    }

    return njs_trap(vm, NJS_TRAP_NUMBERS);
}


njs_ret_t
njs_vmcode_logical_not(njs_vm_t *vm, njs_value_t *value, njs_value_t *inlvd)
{
    const njs_value_t  *retval;

    if (njs_is_true(value)) {
        retval = &njs_value_false;

    } else {
        retval = &njs_value_true;
    }

    vm->retval = *retval;

    return sizeof(njs_vmcode_2addr_t);
}


njs_ret_t
njs_vmcode_test_if_true(njs_vm_t *vm, njs_value_t *value, njs_value_t *invld)
{
    njs_vmcode_test_jump_t  *test_jump;

    vm->retval = *value;

    if (njs_is_true(value)) {
        test_jump = (njs_vmcode_test_jump_t *) vm->current;
        return test_jump->offset;
    }

    return sizeof(njs_vmcode_3addr_t);
}


njs_ret_t
njs_vmcode_test_if_false(njs_vm_t *vm, njs_value_t *value, njs_value_t *invld)
{
    njs_vmcode_test_jump_t  *test_jump;

    vm->retval = *value;

    if (!njs_is_true(value)) {
        test_jump = (njs_vmcode_test_jump_t *) vm->current;
        return test_jump->offset;
    }

    return sizeof(njs_vmcode_3addr_t);
}


njs_ret_t
njs_vmcode_bitwise_not(njs_vm_t *vm, njs_value_t *value, njs_value_t *invld)
{
    int32_t  num;

    if (nxt_fast_path(njs_is_numeric(value))) {
        num = njs_number_to_integer(value->data.u.number);
        njs_value_number_set(&vm->retval, ~num);

        return sizeof(njs_vmcode_2addr_t);
    }

    return njs_trap(vm, NJS_TRAP_NUMBER);
}


njs_ret_t
njs_vmcode_bitwise_and(njs_vm_t *vm, njs_value_t *val1, njs_value_t *val2)
{
    int32_t  num1, num2;

    if (nxt_fast_path(njs_is_numeric(val1) && njs_is_numeric(val2))) {

        num1 = njs_number_to_integer(val1->data.u.number);
        num2 = njs_number_to_integer(val2->data.u.number);
        njs_value_number_set(&vm->retval, num1 & num2);

        return sizeof(njs_vmcode_3addr_t);
    }

    return njs_trap(vm, NJS_TRAP_NUMBERS);
}


njs_ret_t
njs_vmcode_bitwise_xor(njs_vm_t *vm, njs_value_t *val1, njs_value_t *val2)
{
    int32_t  num1, num2;

    if (nxt_fast_path(njs_is_numeric(val1) && njs_is_numeric(val2))) {

        num1 = njs_number_to_integer(val1->data.u.number);
        num2 = njs_number_to_integer(val2->data.u.number);
        njs_value_number_set(&vm->retval, num1 ^ num2);

        return sizeof(njs_vmcode_3addr_t);
    }

    return njs_trap(vm, NJS_TRAP_NUMBERS);
}


njs_ret_t
njs_vmcode_bitwise_or(njs_vm_t *vm, njs_value_t *val1, njs_value_t *val2)
{
    int32_t  num1, num2;

    if (nxt_fast_path(njs_is_numeric(val1) && njs_is_numeric(val2))) {

        num1 = njs_number_to_uint32(val1->data.u.number);
        num2 = njs_number_to_uint32(val2->data.u.number);
        njs_value_number_set(&vm->retval, num1 | num2);

        return sizeof(njs_vmcode_3addr_t);
    }

    return njs_trap(vm, NJS_TRAP_NUMBERS);
}


njs_ret_t
njs_vmcode_equal(njs_vm_t *vm, njs_value_t *val1, njs_value_t *val2)
{
    njs_ret_t          ret;
    const njs_value_t  *retval;

    ret = njs_values_equal(vm, val1, val2);

    if (nxt_fast_path(ret >= 0)) {

        retval = (ret != 0) ? &njs_value_true : &njs_value_false;
        vm->retval = *retval;

        return sizeof(njs_vmcode_3addr_t);
    }

    return ret;
}


njs_ret_t
njs_vmcode_not_equal(njs_vm_t *vm, njs_value_t *val1, njs_value_t *val2)
{
    njs_ret_t          ret;
    const njs_value_t  *retval;

    ret = njs_values_equal(vm, val1, val2);

    if (nxt_fast_path(ret >= 0)) {

        retval = (ret == 0) ? &njs_value_true : &njs_value_false;
        vm->retval = *retval;

        return sizeof(njs_vmcode_3addr_t);
    }

    return ret;
}


static nxt_noinline njs_ret_t
njs_values_equal(njs_vm_t *vm, const njs_value_t *val1, const njs_value_t *val2)
{
    nxt_bool_t         nv1, nv2;
    const njs_value_t  *hv, *lv;

    nv1 = njs_is_null_or_undefined(val1);
    nv2 = njs_is_null_or_undefined(val2);

    /* Void and null are equal and not comparable with anything else. */
    if (nv1 || nv2) {
        return (nv1 && nv2);
    }

    if (njs_is_numeric(val1) && njs_is_numeric(val2)) {
        /* NaNs and Infinities are handled correctly by comparision. */
        return (val1->data.u.number == val2->data.u.number);
    }

    if (val1->type == val2->type) {

        if (njs_is_string(val1)) {
            return njs_string_eq(val1, val2);
        }

        return (val1->data.u.object == val2->data.u.object);
    }

    /* Sort values as: numeric < string < objects. */

    if (val1->type > val2->type) {
        hv = val1;
        lv = val2;

    } else {
        hv = val2;
        lv = val1;
    }

    /* If "lv" is an object then "hv" can only be another object. */
    if (njs_is_object(lv)) {
        return 0;
    }

    /* If "hv" is a string then "lv" can only be a numeric. */
    if (njs_is_string(hv)) {
        return (lv->data.u.number == njs_string_to_number(hv, 0));
    }

    /* "hv" is an object and "lv" is either a string or a numeric. */
    return njs_trap(vm, NJS_TRAP_COMPARISON);
}


nxt_noinline njs_ret_t
njs_vmcode_less(njs_vm_t *vm, njs_value_t *val1, njs_value_t *val2)
{
    njs_ret_t          ret;
    const njs_value_t  *retval;

    ret = njs_values_compare(vm, val1, val2);

    if (nxt_fast_path(ret >= -1)) {

        retval = (ret > 0) ? &njs_value_true : &njs_value_false;
        vm->retval = *retval;

        return sizeof(njs_vmcode_3addr_t);
    }

    return ret;
}


njs_ret_t
njs_vmcode_greater(njs_vm_t *vm, njs_value_t *val1, njs_value_t *val2)
{
    return njs_vmcode_less(vm, val2, val1);
}


njs_ret_t
njs_vmcode_less_or_equal(njs_vm_t *vm, njs_value_t *val1, njs_value_t *val2)
{
    return njs_vmcode_greater_or_equal(vm, val2, val1);
}


nxt_noinline njs_ret_t
njs_vmcode_greater_or_equal(njs_vm_t *vm, njs_value_t *val1, njs_value_t *val2)
{
    njs_ret_t          ret;
    const njs_value_t  *retval;

    ret = njs_values_compare(vm, val1, val2);

    if (nxt_fast_path(ret >= -1)) {

        retval = (ret == 0) ? &njs_value_true : &njs_value_false;
        vm->retval = *retval;

        return sizeof(njs_vmcode_3addr_t);
    }

    return ret;
}


/*
 * ECMAScript 5.1: 11.8.5
 * njs_values_compare() returns
 *   1 if val1 is less than val2,
 *   0 if val1 is greater than or equal to val2,
 *  -1 if the values are not comparable,
 *  or negative trap number if convertion to primitive is required.
 */

static nxt_noinline njs_ret_t
njs_values_compare(njs_vm_t *vm, const njs_value_t *val1,
    const njs_value_t *val2)
{
    double  num1, num2;

    if (nxt_fast_path(njs_is_primitive(val1) && njs_is_primitive(val2))) {

        if (nxt_fast_path(njs_is_numeric(val1))) {
            num1 = val1->data.u.number;

            if (nxt_fast_path(njs_is_numeric(val2))) {
                num2 = val2->data.u.number;

            } else {
                num2 = njs_string_to_number(val2, 0);
            }

        } else if (njs_is_numeric(val2)) {
            num1 = njs_string_to_number(val1, 0);
            num2 = val2->data.u.number;

        } else {
            return (njs_string_cmp(val1, val2) < 0) ? 1 : 0;
        }

        /* NaN and void values are not comparable with anything. */
        if (isnan(num1) || isnan(num2)) {
            return -1;
        }

        /* Infinities are handled correctly by comparision. */
        return (num1 < num2);
    }

    return njs_trap(vm, NJS_TRAP_COMPARISON);
}


njs_ret_t
njs_vmcode_strict_equal(njs_vm_t *vm, njs_value_t *val1, njs_value_t *val2)
{
    const njs_value_t  *retval;

    if (njs_values_strict_equal(val1, val2)) {
        retval = &njs_value_true;

    } else {
        retval = &njs_value_false;
    }

    vm->retval = *retval;

    return sizeof(njs_vmcode_3addr_t);
}


njs_ret_t
njs_vmcode_strict_not_equal(njs_vm_t *vm, njs_value_t *val1, njs_value_t *val2)
{
    const njs_value_t  *retval;

    if (njs_values_strict_equal(val1, val2)) {
        retval = &njs_value_false;

    } else {
        retval = &njs_value_true;
    }

    vm->retval = *retval;

    return sizeof(njs_vmcode_3addr_t);
}


nxt_noinline nxt_bool_t
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


njs_ret_t
njs_vmcode_move(njs_vm_t *vm, njs_value_t *value, njs_value_t *invld)
{
    vm->retval = *value;

    njs_retain(value);

    return sizeof(njs_vmcode_move_t);
}


njs_ret_t
njs_vmcode_jump(njs_vm_t *vm, njs_value_t *invld, njs_value_t *offset)
{
    return (njs_ret_t) offset;
}


njs_ret_t
njs_vmcode_if_true_jump(njs_vm_t *vm, njs_value_t *cond, njs_value_t *offset)
{
    if (njs_is_true(cond)) {
        return (njs_ret_t) offset;
    }

    return sizeof(njs_vmcode_cond_jump_t);
}


njs_ret_t
njs_vmcode_if_false_jump(njs_vm_t *vm, njs_value_t *cond, njs_value_t *offset)
{
    if (njs_is_true(cond)) {
        return sizeof(njs_vmcode_cond_jump_t);
    }

    return (njs_ret_t) offset;
}


njs_ret_t
njs_vmcode_if_equal_jump(njs_vm_t *vm, njs_value_t *val1, njs_value_t *val2)
{
    njs_vmcode_equal_jump_t  *jump;

    if (njs_values_strict_equal(val1, val2)) {
        jump = (njs_vmcode_equal_jump_t *) vm->current;
        return jump->offset;
    }

    return sizeof(njs_vmcode_3addr_t);
}


njs_ret_t
njs_vmcode_function_frame(njs_vm_t *vm, njs_value_t *value, njs_value_t *nargs)
{
    njs_ret_t                    ret;
    njs_vmcode_function_frame_t  *function;

    function = (njs_vmcode_function_frame_t *) vm->current;

    /* TODO: external object instead of void this. */

    ret = njs_function_frame_create(vm, value, &njs_value_undefined,
                                    (uintptr_t) nargs, function->code.ctor);

    if (nxt_fast_path(ret == NXT_OK)) {
        return sizeof(njs_vmcode_function_frame_t);
    }

    return ret;
}


static njs_ret_t
njs_function_frame_create(njs_vm_t *vm, njs_value_t *value,
    const njs_value_t *this, uintptr_t nargs, nxt_bool_t ctor)
{
    njs_value_t     val;
    njs_object_t    *object;
    njs_function_t  *function;

    if (nxt_fast_path(njs_is_function(value))) {

        function = value->data.u.function;

        if (ctor) {
            if (!function->ctor) {
                njs_type_error(vm, "%s is not a constructor",
                               njs_type_string(value->type));
                return NXT_ERROR;
            }

            if (!function->native) {
                object = njs_function_new_object(vm, value);
                if (nxt_slow_path(object == NULL)) {
                    return NXT_ERROR;
                }

                val.data.u.object = object;
                val.type = NJS_OBJECT;
                val.data.truth = 1;
                this = &val;
            }
        }

        return njs_function_frame(vm, function, this, NULL, nargs, 0, ctor);
    }

    njs_type_error(vm, "%s is not a function", njs_type_string(value->type));

    return NXT_ERROR;
}


static njs_object_t *
njs_function_new_object(njs_vm_t *vm, njs_value_t *value)
{
    nxt_int_t           ret;
    njs_value_t         *proto;
    njs_object_t        *object;
    njs_function_t      *function;
    njs_object_prop_t   *prop;
    nxt_lvlhsh_query_t  lhq;

    object = njs_object_alloc(vm);

    if (nxt_fast_path(object != NULL)) {

        lhq.key_hash = NJS_PROTOTYPE_HASH;
        lhq.key = nxt_string_value("prototype");
        lhq.proto = &njs_object_hash_proto;
        function = value->data.u.function;

        ret = nxt_lvlhsh_find(&function->object.hash, &lhq);

        if (ret == NXT_OK) {
            prop = lhq.value;
            proto = &prop->value;

        } else {
            proto = njs_function_property_prototype_create(vm, value);
        }

        if (nxt_fast_path(proto != NULL)) {
            object->__proto__ = proto->data.u.object;
            return object;
        }
   }

   return NULL;
}


njs_ret_t
njs_vmcode_method_frame(njs_vm_t *vm, njs_value_t *object, njs_value_t *name)
{
    njs_ret_t                  ret;
    nxt_str_t                  string;
    njs_value_t                *value;
    njs_object_prop_t          *prop;
    njs_property_query_t       pq;
    njs_vmcode_method_frame_t  *method;

    value = NULL;
    method = (njs_vmcode_method_frame_t *) vm->current;

    njs_property_query_init(&pq, NJS_PROPERTY_QUERY_GET, 0);

    ret = njs_property_query(vm, &pq, object, name);

    switch (ret) {

    case NXT_OK:
        prop = pq.lhq.value;

        switch (prop->type) {
        case NJS_PROPERTY:
        case NJS_METHOD:
            break;

        case NJS_PROPERTY_HANDLER:
            pq.scratch = *prop;
            prop = &pq.scratch;
            ret = prop->value.data.u.prop_handler(vm, object, NULL,
                                                  &prop->value);
            if (nxt_slow_path(ret != NXT_OK)) {
                return ret;
            }

            break;

        default:
            njs_internal_error(vm, "unexpected property type \"%s\" "
                               "while getting method",
                               njs_prop_type_string(prop->type));

            return NXT_ERROR;
        }

        value = &prop->value;

        break;

    case NXT_DECLINED:
        break;

    case NJS_TRAP:
    case NXT_ERROR:
    default:

        return ret;
    }

    if (value == NULL || !njs_is_function(value)) {
        njs_string_get(name, &string);
        njs_type_error(vm, "(intermediate value)[\"%V\"] is not a function",
                       &string);
        return NXT_ERROR;
    }

    ret = njs_function_frame_create(vm, value, object, method->nargs,
                                    method->code.ctor);

    if (nxt_fast_path(ret == NXT_OK)) {
        return sizeof(njs_vmcode_method_frame_t);
    }

    return ret;
}


njs_ret_t
njs_vmcode_function_call(njs_vm_t *vm, njs_value_t *invld, njs_value_t *retval)
{
    u_char              *return_address;
    njs_ret_t           ret;
    njs_function_t      *function;
    njs_continuation_t  *cont;
    njs_native_frame_t  *frame;

    frame = vm->top_frame;
    function = frame->function;

    return_address = vm->current + sizeof(njs_vmcode_function_call_t);

    if (function->native) {
        if (function->continuation_size != 0) {
            cont = njs_vm_continuation(vm);

            cont->function = function->u.native;
            cont->args_types = function->args_types;
            cont->retval = (njs_index_t) retval;
            cont->return_address = return_address;

            vm->current = (u_char *) njs_continuation_nexus;

            ret = NJS_APPLIED;

        } else {
            ret = njs_function_native_call(vm, function->u.native,
                                           frame->arguments,
                                           function->args_types, frame->nargs,
                                           (njs_index_t) retval,
                                           return_address);
        }

    } else {
        ret = njs_function_lambda_call(vm, (njs_index_t) retval,
                                       return_address);
    }

    return (ret == NJS_APPLIED) ? 0 : ret;
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


njs_ret_t
njs_vmcode_return(njs_vm_t *vm, njs_value_t *invld, njs_value_t *retval)
{
    njs_value_t         *value;
    njs_frame_t         *frame;
    njs_native_frame_t  *previous;

    value = njs_vmcode_operand(vm, retval);

    frame = (njs_frame_t *) vm->top_frame;

    if (frame->native.ctor) {
        if (njs_is_object(value)) {
            njs_release(vm, vm->scopes[NJS_SCOPE_ARGUMENTS]);

        } else {
            value = vm->scopes[NJS_SCOPE_ARGUMENTS];
        }
    }

    previous = njs_function_previous_frame(&frame->native);

    njs_vm_scopes_restore(vm, frame, previous);

    /*
     * If a retval is in a callee arguments scope it
     * must be in the previous callee arguments scope.
     */
    retval = njs_vmcode_operand(vm, frame->retval);

    /* GC: value external/internal++ depending on value and retval type */
    *retval = *value;

    vm->current = frame->return_address;

    njs_function_frame_free(vm, &frame->native);

    return 0;
}


static void
njs_vm_scopes_restore(njs_vm_t *vm, njs_frame_t *frame,
    njs_native_frame_t *previous)
{
    nxt_uint_t      n, nesting;
    njs_value_t     *args;
    njs_function_t  *function;

    vm->top_frame = previous;

    args = previous->arguments;
    function = previous->function;

    if (function != NULL) {
        args += function->args_offset;
    }

    vm->scopes[NJS_SCOPE_CALLEE_ARGUMENTS] = args;

    function = frame->native.function;

    if (function->native) {
        return;
    }

    if (function->closure) {
        /* GC: release function closures. */
    }

    frame = frame->previous_active_frame;
    vm->active_frame = frame;

    /* GC: arguments, local, and local block closures. */

    vm->scopes[NJS_SCOPE_ARGUMENTS] = frame->native.arguments;
    vm->scopes[NJS_SCOPE_LOCAL] = frame->local;

    function = frame->native.function;

    nesting = (function != NULL) ? function->u.lambda->nesting : 0;

    for (n = 0; n <= nesting; n++) {
        vm->scopes[NJS_SCOPE_CLOSURE + n] = &frame->closures[n]->u.values;
    }

    while (n < NJS_MAX_NESTING) {
        vm->scopes[NJS_SCOPE_CLOSURE + n] = NULL;
        n++;
    }
}


const njs_vmcode_generic_t  njs_continuation_nexus[] = {
    { .code = { .operation = njs_vmcode_continuation,
                .operands =  NJS_VMCODE_NO_OPERAND,
                .retval = NJS_VMCODE_NO_RETVAL } },

    { .code = { .operation = njs_vmcode_stop,
                .operands =  NJS_VMCODE_1OPERAND,
                .retval = NJS_VMCODE_NO_RETVAL },
      .operand1 = NJS_INDEX_GLOBAL_RETVAL },
};


static njs_ret_t
njs_vmcode_continuation(njs_vm_t *vm, njs_value_t *invld1, njs_value_t *invld2)
{
    njs_ret_t           ret;
    njs_native_frame_t  *frame;
    njs_continuation_t  *cont;

    frame = vm->top_frame;
    cont = njs_vm_continuation(vm);

    ret = njs_function_native_call(vm, cont->function, frame->arguments,
                                   cont->args_types, frame->nargs,
                                   cont->retval, cont->return_address);

    return (ret == NJS_APPLIED) ? 0 : ret;
}


njs_ret_t
njs_vmcode_stop(njs_vm_t *vm, njs_value_t *invld, njs_value_t *retval)
{
    njs_value_t  *value;

    value = njs_vmcode_operand(vm, retval);

    vm->retval = *value;

    return NJS_STOP;
}


/*
 * njs_vmcode_try_start() is set on the start of a "try" block to create
 * a "try" block, to set a catch address to the start of a "catch" or
 * "finally" blocks and to initialize a value to track uncaught exception.
 */

njs_ret_t
njs_vmcode_try_start(njs_vm_t *vm, njs_value_t *exception_value,
    njs_value_t *offset)
{
    njs_value_t             *exit_value;
    njs_exception_t         *e;
    njs_vmcode_try_start_t  *try_start;

    if (vm->top_frame->exception.catch != NULL) {
        e = nxt_mp_alloc(vm->mem_pool, sizeof(njs_exception_t));
        if (nxt_slow_path(e == NULL)) {
            njs_memory_error(vm);
            return NXT_ERROR;
        }

        *e = vm->top_frame->exception;
        vm->top_frame->exception.next = e;
    }

    vm->top_frame->exception.catch = vm->current + (njs_ret_t) offset;

    njs_set_invalid(exception_value);

    try_start = (njs_vmcode_try_start_t *) vm->current;
    exit_value = njs_vmcode_operand(vm, try_start->exit_value);

    njs_set_invalid(exit_value);
    exit_value->data.u.number = 0;

    return sizeof(njs_vmcode_try_start_t);
}


/*
 * njs_vmcode_try_break() sets exit_value to INVALID 1, and jumps to
 * the nearest try_end block. The exit_value is checked by njs_vmcode_finally().
 */

nxt_noinline njs_ret_t
njs_vmcode_try_break(njs_vm_t *vm, njs_value_t *exit_value,
    njs_value_t *offset)
{
    /* exit_value can contain valid value set by vmcode_try_return. */
    if (!njs_is_valid(exit_value)) {
        exit_value->data.u.number = 1;
    }

    return (njs_ret_t) offset;
}


/*
 * njs_vmcode_try_continue() sets exit_value to INVALID -1, and jumps to
 * the nearest try_end block. The exit_value is checked by njs_vmcode_finally().
 */

nxt_noinline njs_ret_t
njs_vmcode_try_continue(njs_vm_t *vm, njs_value_t *exit_value,
    njs_value_t *offset)
{
    exit_value->data.u.number = -1;

    return (njs_ret_t) offset;
}

/*
 * njs_vmcode_try_return() saves a return value to use it later by
 * njs_vmcode_finally(), and jumps to the nearest try_break block.
 */

nxt_noinline njs_ret_t
njs_vmcode_try_return(njs_vm_t *vm, njs_value_t *value, njs_value_t *invld)
{
    njs_vmcode_try_return_t  *try_return;

    vm->retval = *value;

    njs_retain(value);

    try_return = (njs_vmcode_try_return_t *) vm->current;

    return try_return->offset;
}


/*
 * njs_vmcode_try_end() is set on the end of a "try" block to remove the block.
 * It is also set on the end of a "catch" block followed by a "finally" block.
 */

nxt_noinline njs_ret_t
njs_vmcode_try_end(njs_vm_t *vm, njs_value_t *invld, njs_value_t *offset)
{
    njs_exception_t  *e;

    e = vm->top_frame->exception.next;

    if (e == NULL) {
        vm->top_frame->exception.catch = NULL;

    } else {
        vm->top_frame->exception = *e;
        nxt_mp_free(vm->mem_pool, e);
    }

    return (njs_ret_t) offset;
}


njs_ret_t
njs_vmcode_throw(njs_vm_t *vm, njs_value_t *invld, njs_value_t *retval)
{
    njs_value_t  *value;

    value = njs_vmcode_operand(vm, retval);

    vm->retval = *value;

    return NXT_ERROR;
}


/*
 * njs_vmcode_catch() is set on the start of a "catch" block to store
 * exception and to remove a "try" block if there is no "finally" block
 * or to update a catch address to the start of a "finally" block.
 * njs_vmcode_catch() is set on the start of a "finally" block to store
 * uncaught exception and to remove a "try" block.
 */

njs_ret_t
njs_vmcode_catch(njs_vm_t *vm, njs_value_t *exception, njs_value_t *offset)
{
    *exception = vm->retval;

    if ((njs_ret_t) offset == sizeof(njs_vmcode_catch_t)) {
        return njs_vmcode_try_end(vm, exception, offset);
    }

    vm->top_frame->exception.catch = vm->current + (njs_ret_t) offset;

    return sizeof(njs_vmcode_catch_t);
}


/*
 * njs_vmcode_finally() is set on the end of a "finally" or a "catch" block.
 *   1) to throw uncaught exception.
 *   2) to make a jump to an enslosing loop exit if "continue" or "break"
 *      statement was used inside try block.
 *   3) to finalize "return" instruction from "try" block.
 */

njs_ret_t
njs_vmcode_finally(njs_vm_t *vm, njs_value_t *invld, njs_value_t *retval)
{
    njs_value_t           *exception_value, *exit_value;
    njs_vmcode_finally_t  *finally;

    exception_value = njs_vmcode_operand(vm, retval);

    if (njs_is_valid(exception_value)) {
        vm->retval = *exception_value;

        return NXT_ERROR;
    }

    finally = (njs_vmcode_finally_t *) vm->current;
    exit_value = njs_vmcode_operand(vm, finally->exit_value);

    /*
     * exit_value is set by:
     *   vmcode_try_start to INVALID 0
     *   vmcode_try_break to INVALID 1
     *   vmcode_try_continue to INVALID -1
     *   vmcode_try_return to a valid return value
     */

    if (njs_is_valid(exit_value)) {
        return njs_vmcode_return(vm, NULL, exit_value);

    } else if (exit_value->data.u.number != 0) {
        return (njs_ret_t) (exit_value->data.u.number > 0)
                                ? finally->break_offset
                                : finally->continue_offset;
    }

    return sizeof(njs_vmcode_finally_t);
}


njs_ret_t
njs_vmcode_reference_error(njs_vm_t *vm, njs_value_t *invld1,
    njs_value_t *invld2)
{
    nxt_str_t                     *file;
    njs_vmcode_reference_error_t  *ref_err;

    ref_err = (njs_vmcode_reference_error_t *) vm->current;

    file = &ref_err->file;

    if (file->length != 0 && !vm->options.quiet) {
        njs_reference_error(vm, "\"%V\" is not defined in %V:%uD",
                            &ref_err->name, file, ref_err->token_line);

    } else {
        njs_reference_error(vm, "\"%V\" is not defined in %uD", &ref_err->name,
                            ref_err->token_line);
    }

    return NJS_ERROR;
}


static const njs_vmcode_1addr_t  njs_trap_number[] = {
    { .code = { .operation = njs_vmcode_number_primitive,
                .operands =  NJS_VMCODE_1OPERAND,
                .retval = NJS_VMCODE_NO_RETVAL },
      .index = 0 },
    { .code = { .operation = njs_vmcode_restart,
                .operands =  NJS_VMCODE_NO_OPERAND,
                .retval = NJS_VMCODE_NO_RETVAL } },
};


static const njs_vmcode_1addr_t  njs_trap_numbers[] = {
    { .code = { .operation = njs_vmcode_number_primitive,
                .operands =  NJS_VMCODE_1OPERAND,
                .retval = NJS_VMCODE_NO_RETVAL },
      .index = 0 },
    { .code = { .operation = njs_vmcode_number_primitive,
                .operands =  NJS_VMCODE_1OPERAND,
                .retval = NJS_VMCODE_NO_RETVAL },
      .index = 1 },
    { .code = { .operation = njs_vmcode_restart,
                .operands =  NJS_VMCODE_NO_OPERAND,
                .retval = NJS_VMCODE_NO_RETVAL } },
};


static const njs_vmcode_1addr_t  njs_trap_addition[] = {
    { .code = { .operation = njs_vmcode_addition_primitive,
                .operands =  NJS_VMCODE_1OPERAND,
                .retval = NJS_VMCODE_NO_RETVAL },
      .index = 0 },
    { .code = { .operation = njs_vmcode_addition_primitive,
                .operands =  NJS_VMCODE_1OPERAND,
                .retval = NJS_VMCODE_NO_RETVAL },
      .index = 1 },
    { .code = { .operation = njs_vmcode_restart,
                .operands =  NJS_VMCODE_NO_OPERAND,
                .retval = NJS_VMCODE_NO_RETVAL } },
};


static const njs_vmcode_1addr_t  njs_trap_comparison[] = {
    { .code = { .operation = njs_vmcode_comparison_primitive,
                .operands =  NJS_VMCODE_1OPERAND,
                .retval = NJS_VMCODE_NO_RETVAL },
      .index = 0 },
    { .code = { .operation = njs_vmcode_comparison_primitive,
                .operands =  NJS_VMCODE_1OPERAND,
                .retval = NJS_VMCODE_NO_RETVAL },
      .index = 1 },
    { .code = { .operation = njs_vmcode_restart,
                .operands =  NJS_VMCODE_NO_OPERAND,
                .retval = NJS_VMCODE_NO_RETVAL } },
};


static const njs_vmcode_1addr_t  njs_trap_property[] = {
    { .code = { .operation = njs_vmcode_string_primitive,
                .operands =  NJS_VMCODE_1OPERAND,
                .retval = NJS_VMCODE_NO_RETVAL },
      .index = 1 },
    { .code = { .operation = njs_vmcode_restart,
                .operands =  NJS_VMCODE_NO_OPERAND,
                .retval = NJS_VMCODE_NO_RETVAL } },
};


static const njs_vmcode_1addr_t  njs_trap_number_argument = {
    .code = { .operation = njs_vmcode_number_argument,
              .operands =  NJS_VMCODE_NO_OPERAND,
              .retval = NJS_VMCODE_NO_RETVAL }
};


static const njs_vmcode_1addr_t  njs_trap_string_argument = {
    .code = { .operation = njs_vmcode_string_argument,
              .operands =  NJS_VMCODE_NO_OPERAND,
              .retval = NJS_VMCODE_NO_RETVAL }
};


static const njs_vmcode_1addr_t  njs_trap_primitive_argument = {
    .code = { .operation = njs_vmcode_primitive_argument,
              .operands =  NJS_VMCODE_NO_OPERAND,
              .retval = NJS_VMCODE_NO_RETVAL }
};


static const njs_vm_trap_t  njs_vm_traps[] = {
    /* NJS_TRAP_NUMBER     */     { .code = &njs_trap_number[0]       },
    /* NJS_TRAP_NUMBERS    */     { .code = &njs_trap_numbers[0]      },
    /* NJS_TRAP_ADDITION   */     { .code = &njs_trap_addition[0]     },
    /* NJS_TRAP_COMPARISON */     { .code = &njs_trap_comparison[0]   },
    /* NJS_TRAP_INCDEC     */     { .code = &njs_trap_numbers[1],
                                    .reference = 1                    },
    /* NJS_TRAP_PROPERTY   */     { .code = &njs_trap_property[0]     },
    /* NJS_TRAP_NUMBER_ARG */     { .code = &njs_trap_number_argument },
    /* NJS_TRAP_STRING_ARG */     { .code = &njs_trap_string_argument },
    /* NJS_TRAP_PRIMITIVE_ARG */  { .code = &njs_trap_primitive_argument },
};


static void
njs_vm_trap(njs_vm_t *vm, njs_trap_t trap, njs_value_t *value1,
    njs_value_t *value2)
{
    njs_native_frame_t  *frame;

    frame = vm->top_frame;

    /*
     * The trap_scratch value is for results of "valueOf" and "toString"
     * methods.  The trap_values[] are original operand values which will
     * be replaced with primitive values returned by "valueOf" or "toString"
     * methods.  The scratch value is stored separately to preserve the
     * original operand values for the second method call if the first
     * method call will return non-primitive value.
     */
    njs_set_invalid(&frame->trap_scratch);
    frame->trap_values[1] = *value2;
    frame->trap_reference = njs_vm_traps[trap].reference;

    if (njs_vm_traps[trap].reference) {
        frame->trap_values[0].data.u.value = value1;

    } else {
        frame->trap_values[0] = *value1;
    }

    frame->trap_restart = vm->current;
    vm->current = (u_char *) njs_vm_traps[trap].code;
}


static void
njs_vm_trap_argument(njs_vm_t *vm, njs_trap_t trap)
{
    njs_value_t         *value;
    njs_native_frame_t  *frame;

    frame = vm->top_frame;
    value = frame->trap_scratch.data.u.value;
    njs_set_invalid(&frame->trap_scratch);

    frame->trap_values[1].data.u.value = value;
    frame->trap_values[0] = *value;

    frame->trap_restart = vm->current;
    vm->current = (u_char *) njs_vm_traps[trap].code;
}


static njs_ret_t
njs_vmcode_number_primitive(njs_vm_t *vm, njs_value_t *invld, njs_value_t *narg)
{
    double       num;
    njs_ret_t    ret;
    njs_value_t  *value;

    value = &vm->top_frame->trap_values[(uintptr_t) narg];

    ret = njs_primitive_value(vm, value, 0);

    if (nxt_fast_path(ret > 0)) {

        if (!njs_is_numeric(value)) {
            num = NAN;

            if (njs_is_string(value)) {
                num = njs_string_to_number(value, 0);
            }

            njs_value_number_set(value, num);
        }

        ret = sizeof(njs_vmcode_1addr_t);
    }

    return ret;
}


static njs_ret_t
njs_vmcode_string_primitive(njs_vm_t *vm, njs_value_t *invld, njs_value_t *narg)
{
    njs_ret_t    ret;
    njs_value_t  *value;

    value = &vm->top_frame->trap_values[(uintptr_t) narg];

    ret = njs_primitive_value(vm, value, 1);

    if (nxt_fast_path(ret > 0)) {
        ret = njs_primitive_value_to_string(vm, value, value);

        if (nxt_fast_path(ret == NXT_OK)) {
            return sizeof(njs_vmcode_1addr_t);
        }
    }

    return ret;
}


static njs_ret_t
njs_vmcode_addition_primitive(njs_vm_t *vm, njs_value_t *invld,
    njs_value_t *narg)
{
    njs_ret_t    ret;
    nxt_uint_t   hint;
    njs_value_t  *value;

    value = &vm->top_frame->trap_values[(uintptr_t) narg];

    /*
     * ECMAScript 5.1:
     *   Date should return String, other types sould return Number.
     */
    hint = njs_is_date(value);

    ret = njs_primitive_value(vm, value, hint);

    if (nxt_fast_path(ret > 0)) {
        return sizeof(njs_vmcode_1addr_t);
    }

    return ret;
}


static njs_ret_t
njs_vmcode_comparison_primitive(njs_vm_t *vm, njs_value_t *invld,
    njs_value_t *narg)
{
    njs_ret_t    ret;
    njs_value_t  *value;

    value = &vm->top_frame->trap_values[(uintptr_t) narg];

    ret = njs_primitive_value(vm, value, 0);

    if (nxt_fast_path(ret > 0)) {
        return sizeof(njs_vmcode_1addr_t);
    }

    return ret;
}


static njs_ret_t
njs_vmcode_number_argument(njs_vm_t *vm, njs_value_t *invld1,
    njs_value_t *inlvd2)
{
    double       num;
    njs_ret_t    ret;
    njs_value_t  *value;

    value = &vm->top_frame->trap_values[0];

    ret = njs_primitive_value(vm, value, 0);

    if (nxt_fast_path(ret > 0)) {

        if (!njs_is_numeric(value)) {
            num = NAN;

            if (njs_is_string(value)) {
                num = njs_string_to_number(value, 0);
            }

            njs_value_number_set(value, num);
        }

        *vm->top_frame->trap_values[1].data.u.value = *value;

        vm->current = vm->top_frame->trap_restart;
        vm->top_frame->trap_restart = NULL;

        return 0;
    }

    return ret;
}


static njs_ret_t
njs_vmcode_string_argument(njs_vm_t *vm, njs_value_t *invld1,
    njs_value_t *inlvd2)
{
    njs_ret_t    ret;
    njs_value_t  *value;

    value = &vm->top_frame->trap_values[0];

    ret = njs_primitive_value(vm, value, 1);

    if (nxt_fast_path(ret > 0)) {
        ret = njs_primitive_value_to_string(vm, value, value);

        if (nxt_fast_path(ret == NXT_OK)) {
            *vm->top_frame->trap_values[1].data.u.value = *value;

            vm->current = vm->top_frame->trap_restart;
            vm->top_frame->trap_restart = NULL;
        }
    }

    return ret;
}


static njs_ret_t
njs_vmcode_primitive_argument(njs_vm_t *vm, njs_value_t *invld1,
    njs_value_t *inlvd2)
{
    njs_ret_t    ret;
    njs_value_t  *value;

    value = &vm->top_frame->trap_values[0];

    ret = njs_primitive_value(vm, value, 0);

    if (nxt_fast_path(ret > 0)) {
        *vm->top_frame->trap_values[1].data.u.value = *value;

        vm->current = vm->top_frame->trap_restart;
        vm->top_frame->trap_restart = NULL;

        return 0;
    }

    return ret;
}


static njs_ret_t
njs_vmcode_restart(njs_vm_t *vm, njs_value_t *invld1, njs_value_t *invld2)
{
    u_char                *restart;
    njs_ret_t             ret;
    njs_value_t           *retval, *value1;
    njs_native_frame_t    *frame;
    njs_vmcode_generic_t  *vmcode;

    frame = vm->top_frame;
    restart = frame->trap_restart;
    frame->trap_restart = NULL;
    vm->current = restart;
    vmcode = (njs_vmcode_generic_t *) restart;

    value1 = &frame->trap_values[0];

    if (frame->trap_reference) {
        value1 = value1->data.u.value;
    }

    ret = vmcode->code.operation(vm, value1, &frame->trap_values[1]);

    if (nxt_slow_path(ret == NJS_ERROR)) {
        return ret;
    }

    if (nxt_slow_path(ret == NJS_TRAP)) {
        /* Trap handlers are not reentrant. */
        njs_internal_error(vm, "trap inside restart instruction");
        return NXT_ERROR;
    }

    if (vmcode->code.retval) {
        retval = njs_vmcode_operand(vm, vmcode->operand1);
        njs_release(vm, retval);
        *retval = vm->retval;
    }

    return ret;
}


/*
 * A hint value is 0 for numbers and 1 for strings.  The value chooses
 * method calls order specified by ECMAScript 5.1: "valueOf", "toString"
 * for numbers and "toString", "valueOf" for strings.
 */

static nxt_noinline njs_ret_t
njs_primitive_value(njs_vm_t *vm, njs_value_t *value, nxt_uint_t hint)
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


njs_ret_t
njs_vm_value_to_ext_string(njs_vm_t *vm, nxt_str_t *dst, const njs_value_t *src,
    nxt_uint_t handle_exception)
{
    u_char                 *p, *start, *end;
    size_t                 len, size, count;
    njs_ret_t              ret;
    nxt_uint_t             i, exception;
    nxt_array_t            *backtrace;
    njs_value_t            value;
    njs_backtrace_entry_t  *be, *prev;

    exception = handle_exception;

again:

    if (nxt_fast_path(src != NULL)) {

        if (nxt_slow_path(src->type == NJS_OBJECT_INTERNAL_ERROR)) {

            /* MemoryError is a nonextensible internal error. */

            if (!src->data.u.object->extensible) {
                njs_string_get(&njs_string_memory_error, dst);
                return NXT_OK;
            }
        }

        value = *src;

        if (nxt_slow_path(!njs_is_primitive(&value))) {

            ret = njs_object_value_to_string(vm, &value);

            if (nxt_slow_path(ret != NXT_OK)) {
                goto fail;
            }
        }

        if (nxt_slow_path((value.type == NJS_NUMBER
                            && value.data.u.number == 0
                            && signbit(value.data.u.number))))
        {
            value = njs_string_minus_zero;
            ret = NXT_OK;

        } else {
            ret = njs_primitive_value_to_string(vm, &value, &value);
        }

        if (nxt_fast_path(ret == NXT_OK)) {
            size = value.short_string.size;

            if (size != NJS_STRING_LONG) {
                start = nxt_mp_alloc(vm->mem_pool, size);
                if (nxt_slow_path(start == NULL)) {
                    njs_memory_error(vm);
                    return NXT_ERROR;
                }

                memcpy(start, value.short_string.start, size);

            } else {
                size = value.long_string.size;
                start = value.long_string.data->start;
            }

            dst->length = size;
            dst->start = start;

            if (exception && njs_vm_backtrace(vm) != NULL) {

                backtrace = njs_vm_backtrace(vm);

                len = dst->length + 1;

                count = 0;
                prev = NULL;

                be = backtrace->start;

                for (i = 0; i < backtrace->items; i++) {
                    if (i != 0 && prev->name.start == be->name.start
                        && prev->line == be->line)
                    {
                        count++;

                    } else {

                        if (count != 0) {
                            len += nxt_length("      repeats  times\n")
                                   + NXT_INT_T_LEN;
                            count = 0;
                        }

                        len += be->name.length + nxt_length("    at  ()\n");

                        if (be->line != 0) {
                            len += be->file.length + NXT_INT_T_LEN + 1;

                        } else {
                            len += nxt_length("native");
                        }
                    }

                    prev = be;
                    be++;
                }

                p = nxt_mp_alloc(vm->mem_pool, len);
                if (p == NULL) {
                    njs_memory_error(vm);
                    return NXT_ERROR;
                }

                start = p;
                end = start + len;

                p = nxt_cpymem(p, dst->start, dst->length);
                *p++ = '\n';

                count = 0;
                prev = NULL;

                be = backtrace->start;

                for (i = 0; i < backtrace->items; i++) {
                    if (i != 0 && prev->name.start == be->name.start
                        && prev->line == be->line)
                    {
                        count++;

                    } else {
                        if (count != 0) {
                            p = nxt_sprintf(p, end, "      repeats %uz times\n",
                                            count);
                            count = 0;
                        }

                        p = nxt_sprintf(p, end, "    at %V ", &be->name);

                        if (be->line != 0) {
                            p = nxt_sprintf(p, end, "(%V:%uD)\n", &be->file,
                                            be->line);

                        } else {
                            p = nxt_sprintf(p, end, "(native)\n");
                        }
                    }

                    prev = be;
                    be++;
                }

                dst->start = start;
                dst->length = p - dst->start;
            }

            return NXT_OK;
        }
    }

fail:

    if (handle_exception) {
        handle_exception = 0;

        /* value evaluation threw an exception. */

        vm->top_frame->trap_tries = 0;

        src = &vm->retval;
        goto again;
    }

    dst->length = 0;
    dst->start = NULL;

    return NXT_ERROR;
}


static njs_ret_t
njs_object_value_to_string(njs_vm_t *vm, njs_value_t *value)
{
    u_char              *current;
    njs_ret_t           ret;
    njs_native_frame_t  *previous;

    static const njs_vmcode_1addr_t  value_to_string[] = {
        { .code = { .operation = njs_vmcode_value_to_string,
                    .operands =  NJS_VMCODE_NO_OPERAND,
                    .retval = NJS_VMCODE_NO_RETVAL } },
    };

    /*
     * Execute the single njs_vmcode_value_to_string() instruction.
     * The trap_scratch value is for results of "toString" or "valueOf"
     * methods.  The trap_values[0] is an original object value which will
     * be replaced with primitive value returned by "toString" or "valueOf"
     * methods.  The scratch value is stored separately to preserve the
     * original object value for the second "valueOf" method call if the
     * first "toString" method call will return non-primitive value.
     */

    current = vm->current;
    vm->current = (u_char *) value_to_string;

    njs_set_invalid(&vm->top_frame->trap_scratch);
    vm->top_frame->trap_values[0] = *value;

    /*
     * Prevent njs_vmcode_interpreter() to unwind the current frame if
     * an exception happens.  It preserves the current frame state if
     * njs_vm_value_to_ext_string() is called from within njs_vm_run().
     */
    previous = vm->top_frame->previous;
    vm->top_frame->previous = NULL;

    ret = njs_vmcode_interpreter(vm);

    if (ret == NJS_STOP) {
        ret = NXT_OK;
        *value = vm->top_frame->trap_values[0];
    }

    vm->current = current;
    vm->top_frame->previous = previous;

    return ret;
}


static njs_ret_t
njs_vmcode_value_to_string(njs_vm_t *vm, njs_value_t *invld1,
    njs_value_t *invld2)
{
    njs_ret_t  ret;

    ret = njs_primitive_value(vm, &vm->top_frame->trap_values[0], 1);

    if (nxt_fast_path(ret > 0)) {
        return NJS_STOP;
    }

    return ret;
}


nxt_noinline void
njs_value_undefined_set(njs_value_t *value)
{
    *value = njs_value_undefined;
}


nxt_noinline void
njs_value_boolean_set(njs_value_t *value, int yn)
{
    *value = yn ? njs_value_true : njs_value_false;
}


nxt_noinline void
njs_value_number_set(njs_value_t *value, double num)
{
    value->data.u.number = num;
    value->type = NJS_NUMBER;
    value->data.truth = njs_is_number_true(num);
}


nxt_noinline void
njs_value_data_set(njs_value_t *value, void *data)
{
    value->data.u.data = data;
    value->type = NJS_DATA;
    value->data.truth = 1;
}


nxt_noinline njs_ret_t
njs_vm_value_string_set(njs_vm_t *vm, njs_value_t *value, const u_char *start,
    uint32_t size)
{
    return njs_string_set(vm, value, start, size);
}


nxt_noinline u_char *
njs_vm_value_string_alloc(njs_vm_t *vm, njs_value_t *value, uint32_t size)
{
    return njs_string_alloc(vm, value, size, 0);
}


void
njs_vm_value_error_set(njs_vm_t *vm, njs_value_t *value, const char *fmt, ...)
{
    va_list  args;
    u_char   buf[NXT_MAX_ERROR_STR], *p;

    p = buf;

    if (fmt != NULL) {
        va_start(args, fmt);
        p = nxt_vsprintf(buf, buf + sizeof(buf), fmt, args);
        va_end(args);
    }

    njs_error_new(vm, value, NJS_OBJECT_ERROR, buf, p - buf);
}


nxt_noinline uint8_t
njs_value_bool(const njs_value_t *value)
{
    return value->data.truth;
}


nxt_noinline double
njs_value_number(const njs_value_t *value)
{
    return value->data.u.number;
}


nxt_noinline void *
njs_value_data(const njs_value_t *value)
{
    return value->data.u.data;
}


nxt_noinline njs_function_t *
njs_value_function(const njs_value_t *value)
{
    return value->data.u.function;
}


nxt_noinline nxt_int_t
njs_value_is_null(const njs_value_t *value)
{
    return njs_is_null(value);
}


nxt_noinline nxt_int_t
njs_value_is_undefined(const njs_value_t *value)
{
    return njs_is_undefined(value);
}


nxt_noinline nxt_int_t
njs_value_is_null_or_undefined(const njs_value_t *value)
{
    return njs_is_null_or_undefined(value);
}


nxt_noinline nxt_int_t
njs_value_is_boolean(const njs_value_t *value)
{
    return njs_is_boolean(value);
}


nxt_noinline nxt_int_t
njs_value_is_number(const njs_value_t *value)
{
    return njs_is_number(value);
}


nxt_noinline nxt_int_t
njs_value_is_valid_number(const njs_value_t *value)
{
    return njs_is_number(value)
           && !isnan(value->data.u.number)
           && !isinf(value->data.u.number);
}


nxt_noinline nxt_int_t
njs_value_is_string(const njs_value_t *value)
{
    return njs_is_string(value);
}


nxt_noinline nxt_int_t
njs_value_is_object(const njs_value_t *value)
{
    return njs_is_object(value);
}


nxt_noinline nxt_int_t
njs_value_is_function(const njs_value_t *value)
{
    return njs_is_function(value);
}


nxt_int_t
njs_vm_value_string_copy(njs_vm_t *vm, nxt_str_t *retval,
    const njs_value_t *value, uintptr_t *next)
{
    uintptr_t    n;
    njs_array_t  *array;

    switch (value->type) {

    case NJS_STRING:
        if (*next != 0) {
            return NXT_DECLINED;
        }

        *next = 1;
        break;

    case NJS_ARRAY:
        array = value->data.u.array;

        do {
            n = (*next)++;

            if (n == array->length) {
                return NXT_DECLINED;
            }

            value = &array->start[n];

        } while (!njs_is_valid(value));

        break;

    default:
        return NXT_ERROR;
    }

    return njs_vm_value_to_ext_string(vm, retval, value, 0);
}


static njs_ret_t
njs_vm_add_backtrace_entry(njs_vm_t *vm, njs_frame_t *frame)
{
    nxt_int_t              ret;
    nxt_uint_t             i;
    njs_function_t         *function;
    njs_native_frame_t     *native_frame;
    njs_function_debug_t   *debug_entry;
    njs_function_lambda_t  *lambda;
    njs_backtrace_entry_t  *be;

    native_frame = &frame->native;
    function = native_frame->function;

    be = nxt_array_add(vm->backtrace, &njs_array_mem_proto, vm->mem_pool);
    if (nxt_slow_path(be == NULL)) {
        return NXT_ERROR;
    }

    be->line = 0;

    if (function == NULL) {
        be->name = njs_entry_main;
        return NXT_OK;
    }

    if (function->native) {
        ret = njs_builtin_match_native_function(vm, function, &be->name);
        if (ret == NXT_OK) {
            return NXT_OK;
        }

        ret = njs_external_match_native_function(vm, function->u.native,
                                                 &be->name);
        if (ret == NXT_OK) {
            return NXT_OK;
        }

        be->name = njs_entry_native;

        return NXT_OK;
    }

    lambda = function->u.lambda;
    debug_entry = vm->debug->start;

    for (i = 0; i < vm->debug->items; i++) {
        if (lambda == debug_entry[i].lambda) {
            if (debug_entry[i].name.length != 0) {
                be->name = debug_entry[i].name;

            } else {
                be->name = njs_entry_anonymous;
            }

            be->file = debug_entry[i].file;
            be->line = debug_entry[i].line;

            return NXT_OK;
        }
    }

    be->name = njs_entry_unknown;

    return NXT_OK;
}


nxt_array_t *
njs_vm_backtrace(njs_vm_t *vm)
{
    if (vm->backtrace != NULL && !nxt_array_is_empty(vm->backtrace)) {
        return vm->backtrace;
    }

    return NULL;
}


void
njs_debug(njs_index_t index, njs_value_t *value)
{
#if (NXT_DEBUG)
    u_char    *p;
    uint32_t  length;

    switch (value->type) {

    case NJS_NULL:
        nxt_thread_log_debug("%p [null]", index);
        return;

    case NJS_UNDEFINED:
        nxt_thread_log_debug("%p [void]", index);
        return;

    case NJS_BOOLEAN:
        nxt_thread_log_debug("%p [%s]", index,
                             (value->data.u.number == 0.0) ? "false" : "true");
        return;

    case NJS_NUMBER:
        nxt_thread_log_debug("%p [%f]", index, value->data.u.number);
        return;

    case NJS_STRING:
        length = value->short_string.size;
        if (length != NJS_STRING_LONG) {
            p = value->short_string.start;

        } else {
            length = value->long_string.size;
            p = value->long_string.data->start;
        }

        nxt_thread_log_debug("%p [\"%*s\"]", index, length, p);
        return;

    case NJS_ARRAY:
        nxt_thread_log_debug("%p [array]", index);
        return;

    default:
        nxt_thread_log_debug("%p [invalid]", index);
        return;
    }
#endif
}


void *
njs_lvlhsh_alloc(void *data, size_t size, nxt_uint_t nalloc)
{
    return nxt_mp_align(data, size, size);
}


void
njs_lvlhsh_free(void *data, void *p, size_t size)
{
    nxt_mp_free(data, p);
}
