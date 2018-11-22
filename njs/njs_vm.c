
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) NGINX, Inc.
 */

#include <njs_core.h>
#include <njs_regexp.h>
#include <string.h>
#include <stdio.h>



struct njs_property_next_s {
    int32_t                        index;
    nxt_lvlhsh_each_t              lhe;
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
static njs_native_frame_t *
    njs_function_previous_frame(njs_native_frame_t *frame);
static njs_ret_t njs_function_frame_free(njs_vm_t *vm,
    njs_native_frame_t *frame);

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
const njs_value_t  njs_value_void =         njs_value(NJS_VOID, 0, NAN);
const njs_value_t  njs_value_false =        njs_value(NJS_BOOLEAN, 0, 0.0);
const njs_value_t  njs_value_true =         njs_value(NJS_BOOLEAN, 1, 1.0);
const njs_value_t  njs_value_zero =         njs_value(NJS_NUMBER, 0, 0.0);
const njs_value_t  njs_value_nan =          njs_value(NJS_NUMBER, 0, NAN);
const njs_value_t  njs_value_invalid =      njs_value(NJS_INVALID, 0, 0.0);


const njs_value_t  njs_string_empty =       njs_string("");
const njs_value_t  njs_string_comma =       njs_string(",");
const njs_value_t  njs_string_null =        njs_string("null");
const njs_value_t  njs_string_void =        njs_string("undefined");
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
         *   njs_vmcode_try_next(),
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
            //njs_release(vm, retval);
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
                nxt_mem_cache_free(vm->mem_cache_pool, frame);
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
                        nxt_memcache_pool_free(vm->mem_cache_pool,
                                               string->start);
                    }

                    nxt_memcache_pool_free(vm->mem_cache_pool, string);
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
    size_t                 size;
    nxt_uint_t             n, nesting;
    njs_function_t         *function;
    njs_function_lambda_t  *lambda;
    njs_vmcode_function_t  *code;

    code = (njs_vmcode_function_t *) vm->current;
    lambda = code->lambda;
    nesting = lambda->nesting;

    size = sizeof(njs_function_t) + nesting * sizeof(njs_closure_t *);

    function = nxt_mem_cache_zalloc(vm->mem_cache_pool, size);
    if (nxt_slow_path(function == NULL)) {
        njs_memory_error(vm);
        return NXT_ERROR;
    }

    function->u.lambda = lambda;
    function->object.shared_hash = vm->shared->function_prototype_hash;
    function->object.__proto__ = &vm->prototypes[NJS_PROTOTYPE_FUNCTION].object;
    function->object.extensible = 1;
    function->args_offset = 1;

    if (nesting != 0) {
        function->closure = 1;

        n = 0;

        do {
            /* GC: retain closure. */
            function->closures[n] = vm->active_frame->closures[n];
            n++;
        } while (n < nesting);
    }

    vm->retval.data.u.function = function;
    vm->retval.type = NJS_FUNCTION;
    vm->retval.data.truth = 1;

    return sizeof(njs_vmcode_function_t);
}


njs_ret_t
njs_vmcode_arguments(njs_vm_t *vm, njs_value_t *invld1, njs_value_t *invld2)
{
    njs_ret_t    ret;
    njs_frame_t  *frame;

    frame = (njs_frame_t *) vm->active_frame;

    if (frame->native.arguments_object == NULL) {
        ret = njs_function_arguments_object_init(vm, &frame->native);
        if (nxt_slow_path(ret != NXT_OK)) {
            return NXT_ERROR;
        }
    }

    vm->retval.data.u.object = frame->native.arguments_object;
    vm->retval.type = NJS_OBJECT;
    vm->retval.data.truth = 1;

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
    njs_ret_t  ret;

    ret = njs_value_property(vm, object, property, &vm->retval);
    if (ret == NXT_OK || ret == NXT_DECLINED) {
        return sizeof(njs_vmcode_prop_get_t);
    }

    return ret;
}


njs_ret_t
njs_vmcode_property_set(njs_vm_t *vm, njs_value_t *object,
    njs_value_t *property)
{
    njs_ret_t              ret;
    njs_value_t            *value;
    njs_object_prop_t      *prop;
    njs_property_query_t   pq;
    njs_vmcode_prop_set_t  *code;

    if (njs_is_primitive(object)) {
        njs_type_error(vm, "property set on primitive %s type",
                       njs_type_string(object->type));
        return NXT_ERROR;
    }

    code = (njs_vmcode_prop_set_t *) vm->current;
    value = njs_vmcode_operand(vm, code->value);

    njs_property_query_init(&pq, NJS_PROPERTY_QUERY_SET, 0);

    ret = njs_property_query(vm, &pq, object, property);

    switch (ret) {

    case NXT_OK:
        prop = pq.lhq.value;

        switch (prop->type) {
        case NJS_PROPERTY:
            break;

        case NJS_PROPERTY_REF:
            *prop->value.data.u.value = *value;
            return sizeof(njs_vmcode_prop_set_t);

        case NJS_PROPERTY_HANDLER:
            if (prop->writable) {
                ret = prop->value.data.u.prop_handler(vm, object, value,
                                                      &vm->retval);
                if (nxt_slow_path(ret != NXT_OK)) {
                    return ret;
                }

                return sizeof(njs_vmcode_prop_set_t);
            }

            break;

        default:
            njs_internal_error(vm, "unexpected property type '%s' "
                               "while setting",
                               njs_prop_type_string(prop->type));

            return NXT_ERROR;
        }

        break;

    case NXT_DECLINED:
        if (nxt_slow_path(!object->data.u.object->extensible)) {
            njs_type_error(vm, "Cannot add property '%.*s', "
                           "object is not extensible", pq.lhq.key.length,
                           pq.lhq.key.start);
            return NXT_ERROR;
        }

        if (nxt_slow_path(pq.lhq.value != NULL)) {
            prop = pq.lhq.value;

            if (nxt_slow_path(prop->type == NJS_WHITEOUT)) {
                /* Previously deleted property.  */
                prop->type = NJS_PROPERTY;
                prop->enumerable = 1;
                prop->configurable = 1;
                prop->writable = 1;
                break;
            }
        }

        prop = njs_object_prop_alloc(vm, &pq.value, &njs_value_void, 1);
        if (nxt_slow_path(prop == NULL)) {
            return NXT_ERROR;
        }

        pq.lhq.replace = 0;
        pq.lhq.value = prop;
        pq.lhq.pool = vm->mem_cache_pool;

        ret = nxt_lvlhsh_insert(&object->data.u.object->hash, &pq.lhq);
        if (nxt_slow_path(ret != NXT_OK)) {
            njs_internal_error(vm, "lvlhsh insert failed");
            return NXT_ERROR;
        }

        break;

    case NJS_TRAP:
    case NXT_ERROR:
    default:

        return ret;
    }

    if (nxt_slow_path(!prop->writable)) {
        njs_type_error(vm, "Cannot assign to read-only property '%.*s' of %s",
                       pq.lhq.key.length, pq.lhq.key.start,
                       njs_type_string(object->type));
        return NXT_ERROR;
    }

    prop->value = *value;

    return sizeof(njs_vmcode_prop_set_t);
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
    const njs_value_t     *retval;
    njs_object_prop_t     *prop;
    njs_property_query_t  pq;

    retval = &njs_value_false;

    njs_property_query_init(&pq, NJS_PROPERTY_QUERY_DELETE, 1);

    ret = njs_property_query(vm, &pq, object, property);

    switch (ret) {

    case NXT_OK:
        prop = pq.lhq.value;

        switch (prop->type) {
        case NJS_PROPERTY:
        case NJS_METHOD:
            break;

        case NJS_PROPERTY_REF:
            njs_set_invalid(prop->value.data.u.value);
            retval = &njs_value_true;
            goto done;

        case NJS_PROPERTY_HANDLER:
            if (prop->configurable) {
                ret = prop->value.data.u.prop_handler(vm, object, NULL, NULL);
                if (nxt_slow_path(ret != NXT_OK)) {
                    return ret;
                }

                retval = &njs_value_true;
                goto done;
            }

            break;

        default:
            njs_internal_error(vm, "unexpected property type '%s' "
                               "while deleting",
                               njs_prop_type_string(prop->type));

            return NXT_ERROR;
        }

        if (nxt_slow_path(!prop->configurable)) {
            njs_type_error(vm, "Cannot delete property '%.*s' of %s",
                           pq.lhq.key.length, pq.lhq.key.start,
                           njs_type_string(object->type));
            return NXT_ERROR;
        }

        /* GC: release value. */
        prop->type = NJS_WHITEOUT;
        njs_set_invalid(&prop->value);

        retval = &njs_value_true;

        break;

    case NXT_DECLINED:
        break;

    case NJS_TRAP:
    case NXT_ERROR:
    default:

        return ret;
    }

done:

    vm->retval = *retval;

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

    if (njs_is_object(object)) {
        next = nxt_mem_cache_alloc(vm->mem_cache_pool,
                                   sizeof(njs_property_next_t));
        if (nxt_slow_path(next == NULL)) {
            njs_memory_error(vm);
            return NXT_ERROR;
        }

        vm->retval.data.u.next = next;

        nxt_lvlhsh_each_init(&next->lhe, &njs_object_hash_proto);
        next->index = -1;

        if (njs_is_array(object) && object->data.u.array->length != 0) {
            next->index = 0;
        }

    } else if (njs_is_external(object)) {
        ext_proto = object->external.proto;

        if (ext_proto->foreach != NULL) {
            obj = njs_extern_object(vm, object);

            ret = ext_proto->foreach(vm, obj, &vm->retval);
            if (nxt_slow_path(ret != NXT_OK)) {
                return ret;
            }
        }
    }

    code = (njs_vmcode_prop_foreach_t *) vm->current;

    return code->offset;
}


njs_ret_t
njs_vmcode_property_next(njs_vm_t *vm, njs_value_t *object, njs_value_t *value)
{
    void                    *obj;
    njs_ret_t               ret;
    nxt_uint_t              n;
    njs_value_t             *retval;
    njs_array_t             *array;
    njs_object_prop_t       *prop;
    njs_property_next_t     *next;
    const njs_extern_t      *ext_proto;
    njs_vmcode_prop_next_t  *code;

    code = (njs_vmcode_prop_next_t *) vm->current;
    retval = njs_vmcode_operand(vm, code->retval);

    if (njs_is_object(object)) {
        next = value->data.u.next;

        if (next->index >= 0) {
            array = object->data.u.array;

            while ((uint32_t) next->index < array->length) {
                n = next->index++;

                if (njs_is_valid(&array->start[n])) {
                    njs_value_number_set(retval, n);

                    return code->offset;
                }
            }

            next->index = -1;
        }

        for ( ;; ) {
            prop = nxt_lvlhsh_each(&object->data.u.object->hash, &next->lhe);

            if (prop == NULL) {
                break;
            }

            if (prop->type != NJS_WHITEOUT && prop->enumerable) {
                *retval = prop->name;

                return code->offset;
            }
        }

        nxt_mem_cache_free(vm->mem_cache_pool, next);

    } else if (njs_is_external(object)) {
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
    }

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
        value = njs_value_void;
        ret = njs_value_property(vm, constructor, &prototype_string, &value);

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
    nxt_uint_t  type;

    /* ECMAScript 5.1: null, array and regexp are objects. */

    static const njs_value_t  *types[NJS_TYPE_MAX] = {
        &njs_string_object,
        &njs_string_void,
        &njs_string_boolean,
        &njs_string_number,
        &njs_string_string,
        &njs_string_void,
        &njs_string_void,
        &njs_string_void,
        &njs_string_void,
        &njs_string_void,
        &njs_string_void,
        &njs_string_void,
        &njs_string_void,
        &njs_string_void,
        &njs_string_void,
        &njs_string_void,

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

    /* A zero index means non-declared variable. */
    type = (value != NULL) ? value->type : NJS_VOID;

    vm->retval = *types[type];

    return sizeof(njs_vmcode_2addr_t);
}


njs_ret_t
njs_vmcode_void(njs_vm_t *vm, njs_value_t *invld1, njs_value_t *invld2)
{
    vm->retval = njs_value_void;

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

        num1 = njs_number_to_integer(val1->data.u.number);
        num2 = njs_number_to_integer(val2->data.u.number);
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

        num1 = njs_number_to_integer(val1->data.u.number);
        num2 = njs_number_to_integer(val2->data.u.number);
        njs_value_number_set(&vm->retval, num1 >> (num2 & 0x1f));

        return sizeof(njs_vmcode_3addr_t);
    }

    return njs_trap(vm, NJS_TRAP_NUMBERS);
}


njs_ret_t
njs_vmcode_unsigned_right_shift(njs_vm_t *vm, njs_value_t *val1,
    njs_value_t *val2)
{
    int32_t   num2;
    uint32_t  num1;

    if (nxt_fast_path(njs_is_numeric(val1) && njs_is_numeric(val2))) {

        num1 = njs_number_to_integer(val1->data.u.number);
        num2 = njs_number_to_integer(val2->data.u.number);
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

        num1 = njs_number_to_integer(val1->data.u.number);
        num2 = njs_number_to_integer(val2->data.u.number);
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

    nv1 = njs_is_null_or_void(val1);
    nv2 = njs_is_null_or_void(val2);

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
    size_t        size;
    const u_char  *start1, *start2;

    if (val1->type != val2->type) {
        return 0;
    }

    if (njs_is_numeric(val1)) {

        if (njs_is_void(val1)) {
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
            if (val1->short_string.length != val2->short_string.length) {
                return 0;
            }

            start1 = val1->short_string.start;
            start2 = val2->short_string.start;

        } else {
            size = val1->long_string.size;

            if (size != val2->long_string.size) {
                return 0;
            }

            if (val1->long_string.data->length
                != val2->long_string.data->length)
            {
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

    ret = njs_function_frame_create(vm, value, &njs_value_void,
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

        if (!function->native) {

            if (ctor) {
                object = njs_function_new_object(vm, value);
                if (nxt_slow_path(object == NULL)) {
                    return NXT_ERROR;
                }

                val.data.u.object = object;
                val.type = NJS_OBJECT;
                val.data.truth = 1;
                this = &val;
            }

            return njs_function_frame(vm, function, this, NULL, nargs, ctor);
        }

        if (!ctor || function->ctor) {
            return njs_function_native_frame(vm, function, this, NULL,
                                             nargs, 0, ctor);
        }

        njs_type_error(vm, "%s is not a constructor",
                       njs_type_string(value->type));

        return NXT_ERROR;
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
            njs_internal_error(vm, "unexpected property type '%s' "
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
        njs_type_error(vm, "'%.*s' is not a function", (int) string.length,
                       string.start);
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
    njs_ret_t           ret;
    nxt_uint_t          nargs;
    njs_value_t         *args;
    njs_function_t      *function;
    njs_continuation_t  *cont;
    njs_native_frame_t  *frame;

    frame = vm->top_frame;
    function = frame->function;

    if (!function->native) {
        ret = njs_function_call(vm, (njs_index_t) retval,
                                sizeof(njs_vmcode_function_call_t));

        if (nxt_fast_path(ret != NJS_ERROR)) {
            return 0;
        }

        return ret;
    }

    args = frame->arguments;
    nargs = frame->nargs;

    ret = njs_normalize_args(vm, args, function->args_types, nargs);
    if (ret != NJS_OK) {
        return ret;
    }

    if (function->continuation_size != 0) {
        cont = njs_vm_continuation(vm);

        cont->function = function->u.native;
        cont->args_types = function->args_types;
        cont->retval = (njs_index_t) retval;

        cont->return_address = vm->current + sizeof(njs_vmcode_function_call_t);
        vm->current = (u_char *) njs_continuation_nexus;

        return 0;
    }

     ret = function->u.native(vm, args, nargs, (njs_index_t) retval);

    /*
     * A native method can return:
     *   NXT_OK on method success;
     *   NJS_APPLIED by Function.apply() and Function.call();
     *   NXT_AGAIN to postpone nJSVM processing;
     *   NXT_ERROR.
     *
     * The callee arguments must be preserved
     * for NJS_APPLIED and NXT_AGAIN cases.
     */
    if (ret == NXT_OK) {
        frame = vm->top_frame;

        vm->top_frame = njs_function_previous_frame(frame);
        (void) njs_function_frame_free(vm, frame);

        /*
         * If a retval is in a callee arguments scope it
         * must be in the previous callee arguments scope.
         */
        vm->scopes[NJS_SCOPE_CALLEE_ARGUMENTS] =
                              vm->top_frame->arguments + function->args_offset;

        retval = njs_vmcode_operand(vm, retval);
        /*
         * GC: value external/internal++ depending
         * on vm->retval and retval type
         */
        *retval = vm->retval;

        ret = sizeof(njs_vmcode_function_call_t);

    } else if (ret == NJS_APPLIED) {
        /* A user-defined method has been prepared to run. */
        ret = 0;
    }

    return ret;
}


njs_ret_t
njs_normalize_args(njs_vm_t *vm, njs_value_t *args, uint8_t *args_types,
    nxt_uint_t nargs)
{
    nxt_uint_t  n;
    njs_trap_t  trap;

    n = nxt_min(nargs, NJS_ARGS_TYPES_MAX);

    while (n != 0) {

        switch (*args_types) {

        case NJS_STRING_OBJECT_ARG:

            if (njs_is_null_or_void(args)) {
                goto type_error;
            }

            /* Fall through. */

        case NJS_STRING_ARG:

            if (njs_is_string(args)) {
                break;
            }

            trap = NJS_TRAP_STRING_ARG;
            goto trap;

        case NJS_NUMBER_ARG:

            if (njs_is_numeric(args)) {
                break;
            }

            trap = NJS_TRAP_NUMBER_ARG;
            goto trap;

        case NJS_INTEGER_ARG:

            if (njs_is_numeric(args)) {

                /* Numbers are truncated to fit in 32-bit integers. */

                if (isnan(args->data.u.number)) {
                    args->data.u.number = 0;

                } else if (args->data.u.number > 2147483647.0) {
                    args->data.u.number = 2147483647.0;

                } else if (args->data.u.number < -2147483648.0) {
                    args->data.u.number = -2147483648.0;
                }

                break;
            }

            trap = NJS_TRAP_NUMBER_ARG;
            goto trap;

        case NJS_FUNCTION_ARG:

            switch (args->type) {
            case NJS_STRING:
            case NJS_FUNCTION:
                break;

            default:
                trap = NJS_TRAP_STRING_ARG;
                goto trap;
            }

            break;

        case NJS_REGEXP_ARG:

            switch (args->type) {
            case NJS_VOID:
            case NJS_STRING:
            case NJS_REGEXP:
                break;

            default:
                trap = NJS_TRAP_STRING_ARG;
                goto trap;
            }

            break;

        case NJS_DATE_ARG:
            if (!njs_is_date(args)) {
                goto type_error;
            }

            break;

        case NJS_OBJECT_ARG:

            if (njs_is_null_or_void(args)) {
                goto type_error;
            }

            break;

        case NJS_SKIP_ARG:
            break;

        case 0:
            return NJS_OK;
        }

        args++;
        args_types++;
        n--;
    }

    return NJS_OK;

trap:

    njs_vm_trap_value(vm, args);

    return njs_trap(vm, trap);

type_error:

    njs_type_error(vm, "cannot convert %s to %s", njs_type_string(args->type),
                   njs_arg_type_string(*args_types));

    return NXT_ERROR;
}


const char *
njs_type_string(njs_value_type_t type)
{
    switch (type) {
    case NJS_NULL:
        return "null";

    case NJS_VOID:
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

    return njs_function_frame_free(vm, &frame->native);
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


const njs_vmcode_1addr_t  njs_continuation_nexus[] = {
    { .code = { .operation = njs_vmcode_continuation,
                .operands =  NJS_VMCODE_NO_OPERAND,
                .retval = NJS_VMCODE_NO_RETVAL } },
};


static njs_ret_t
njs_vmcode_continuation(njs_vm_t *vm, njs_value_t *invld1, njs_value_t *invld2)
{
    njs_ret_t           ret;
    nxt_bool_t          skip;
    njs_value_t         *args, *retval;
    njs_function_t      *function;
    njs_native_frame_t  *frame;
    njs_continuation_t  *cont;

    cont = njs_vm_continuation(vm);
    frame = vm->top_frame;
    args = frame->arguments;

    if (cont->args_types != NULL) {
        ret = njs_normalize_args(vm, args, cont->args_types, frame->nargs);
        if (ret != NJS_OK) {
            return ret;
        }
    }

    ret = cont->function(vm, args, frame->nargs, cont->retval);

    switch (ret) {

    case NXT_OK:

        frame = vm->top_frame;
        skip = frame->skip;

        vm->top_frame = njs_function_previous_frame(frame);

        /*
         * If a retval is in a callee arguments scope it
         * must be in the previous callee arguments scope.
         */
        args = vm->top_frame->arguments;
        function = vm->top_frame->function;

        if (function != NULL) {
            args += function->args_offset;
        }

        vm->scopes[NJS_SCOPE_CALLEE_ARGUMENTS] = args;

        if (!skip) {
            retval = njs_vmcode_operand(vm, cont->retval);
            /*
             * GC: value external/internal++ depending
             * on vm->retval and retval type
             */
            *retval = vm->retval;
        }

        vm->current = cont->return_address;
        (void) njs_function_frame_free(vm, frame);

        return 0;

    case NJS_APPLIED:
        return 0;

    default:
        return ret;
    }
}


static njs_native_frame_t *
njs_function_previous_frame(njs_native_frame_t *frame)
{
    njs_native_frame_t  *previous;

    do {
        previous = frame->previous;
        frame = previous;

    } while (frame->skip);

    return frame;
}


static njs_ret_t
njs_function_frame_free(njs_vm_t *vm, njs_native_frame_t *frame)
{
    njs_native_frame_t  *previous;

    do {
        previous = frame->previous;

        /* GC: free frame->local, etc. */

        if (frame->size != 0) {
            vm->stack_size -= frame->size;
            nxt_mem_cache_free(vm->mem_cache_pool, frame);
        }

        frame = previous;

    } while (frame->skip);

    return 0;
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
njs_vmcode_try_start(njs_vm_t *vm, njs_value_t *value, njs_value_t *offset)
{
    njs_exception_t  *e;

    if (vm->top_frame->exception.catch != NULL) {
        e = nxt_mem_cache_alloc(vm->mem_cache_pool, sizeof(njs_exception_t));
        if (nxt_slow_path(e == NULL)) {
            njs_memory_error(vm);
            return NXT_ERROR;
        }

        *e = vm->top_frame->exception;
        vm->top_frame->exception.next = e;
    }

    vm->top_frame->exception.catch = vm->current + (njs_ret_t) offset;

    njs_set_invalid(value);

    return sizeof(njs_vmcode_try_start_t);
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
        nxt_mem_cache_free(vm->mem_cache_pool, e);
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
 * njs_vmcode_finally() is set on the end of a "finally" block to throw
 * uncaught exception.
 */

njs_ret_t
njs_vmcode_finally(njs_vm_t *vm, njs_value_t *invld, njs_value_t *retval)
{
    njs_value_t  *value;

    value = njs_vmcode_operand(vm, retval);

    if (!njs_is_valid(value)) {
        return sizeof(njs_vmcode_finally_t);
    }

    vm->retval = *value;

    return NXT_ERROR;
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


static const njs_vm_trap_t  njs_vm_traps[] = {
    /* NJS_TRAP_NUMBER     */  { .code = &njs_trap_number[0]       },
    /* NJS_TRAP_NUMBERS    */  { .code = &njs_trap_numbers[0]      },
    /* NJS_TRAP_ADDITION   */  { .code = &njs_trap_addition[0]     },
    /* NJS_TRAP_COMPARISON */  { .code = &njs_trap_comparison[0]   },
    /* NJS_TRAP_INCDEC     */  { .code = &njs_trap_numbers[1],
                                 .reference = 1                    },
    /* NJS_TRAP_PROPERTY   */  { .code = &njs_trap_property[0]     },
    /* NJS_TRAP_NUMBER_ARG */  { .code = &njs_trap_number_argument },
    /* NJS_TRAP_STRING_ARG */  { .code = &njs_trap_string_argument },
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

    if (nxt_slow_path(ret == NJS_TRAP)) {
        /* Trap handlers are not reentrant. */
        njs_internal_error(vm, "trap inside restart instruction");
        return NXT_ERROR;
    }

    retval = njs_vmcode_operand(vm, vmcode->operand1);

    //njs_release(vm, retval);

    *retval = vm->retval;

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


/*
 * ES5.1, 8.12.3: [[Get]].
 *   NXT_OK               property has been found in object,
 *      retval will contain the property's value
 *
 *   NXT_DECLINED         property was not found in object,
 *   NJS_TRAP             the property trap must be called,
 *   NXT_ERROR            exception has been thrown.
 *      retval will contain undefined
 */
njs_ret_t
njs_value_property(njs_vm_t *vm, njs_value_t *value,
    const njs_value_t *property, njs_value_t *retval)
{
    njs_ret_t             ret;
    njs_object_prop_t     *prop;
    njs_property_query_t  pq;

    njs_property_query_init(&pq, NJS_PROPERTY_QUERY_GET, 0);

    ret = njs_property_query(vm, &pq, value, property);

    switch (ret) {

    case NXT_OK:
        prop = pq.lhq.value;

        switch (prop->type) {

        case NJS_METHOD:
            if (pq.shared) {
                ret = njs_method_private_copy(vm, &pq);

                if (nxt_slow_path(ret != NXT_OK)) {
                    return ret;
                }

                prop = pq.lhq.value;
            }

            /* Fall through. */

        case NJS_PROPERTY:
            *retval = prop->value;
            break;

        case NJS_PROPERTY_HANDLER:
            pq.scratch = *prop;
            prop = &pq.scratch;
            ret = prop->value.data.u.prop_handler(vm, value, NULL,
                                                  &prop->value);

            if (nxt_slow_path(ret != NXT_OK)) {
                return ret;
            }

            *retval = prop->value;
            break;

        default:
            njs_internal_error(vm, "unexpected property type '%s' "
                               "while getting",
                               njs_prop_type_string(prop->type));

            return NXT_ERROR;
        }

        break;

    case NXT_DECLINED:
        *retval = njs_value_void;

        return NXT_DECLINED;

    case NJS_TRAP:
    case NXT_ERROR:
    default:

        return ret;
    }

    return NXT_OK;
}


njs_ret_t
njs_vm_value_to_ext_string(njs_vm_t *vm, nxt_str_t *dst, const njs_value_t *src,
    nxt_uint_t handle_exception)
{
    u_char                 *p, *start;
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
                start = nxt_mem_cache_alloc(vm->mem_cache_pool, size);
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
                    if (i != 0 && prev->name.start == be[i].name.start
                        && prev->line == be[i].line)
                    {
                        count++;

                    } else {

                        if (count != 0) {
                            len += sizeof("      repeats  times\n") + 10;
                            count = 0;
                        }

                        if (be[i].line != 0) {
                            len += sizeof("    at  (:)\n") + 10
                                   + be[i].name.length;

                        } else {
                            len += sizeof("    at  (native)\n")
                                   + be[i].name.length;
                        }
                    }

                    prev = &be[i];
                }

                p = nxt_mem_cache_alloc(vm->mem_cache_pool, len);
                if (p == NULL) {
                    njs_memory_error(vm);
                    return NXT_ERROR;
                }

                start = p;

                p = nxt_cpymem(p, dst->start, dst->length);
                *p++ = '\n';

                count = 0;
                prev = NULL;

                for (i = 0; i < backtrace->items; i++) {
                    if (i != 0 && prev->name.start == be[i].name.start
                        && prev->line == be[i].line)
                    {
                        count++;

                    } else {
                        if (count != 0) {
                            p += sprintf((char *) p,
                                         "      repeats %zu times\n", count);
                            count =0;
                        }

                        if (be[i].line != 0) {
                            p += sprintf((char *) p, "    at %.*s (:%u)\n",
                                         (int) be[i].name.length,
                                         be[i].name.start, be[i].line);

                        } else {
                            p += sprintf((char *) p, "    at %.*s (native)\n",
                                         (int) be[i].name.length,
                                         be[i].name.start);
                        }
                    }

                    prev = &be[i];
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
njs_value_void_set(njs_value_t *value)
{
    *value = njs_value_void;
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


void
njs_value_error_set(njs_vm_t *vm, njs_value_t *value, const char *fmt, ...)
{
    size_t        size;
    va_list       args;
    nxt_int_t     ret;
    njs_value_t   string;
    njs_object_t  *error;
    char          buf[256];

    if (fmt != NULL) {
        va_start(args, fmt);
        size = vsnprintf(buf, sizeof(buf), fmt, args);
        va_end(args);

    } else {
        size = 0;
    }

    ret = njs_string_new(vm, &string, (u_char *) buf, size, size);
    if (nxt_slow_path(ret != NXT_OK)) {
        goto memory_error;
    }

    error = njs_error_alloc(vm, NJS_OBJECT_ERROR, NULL, &string);
    if (nxt_slow_path(error == NULL)) {
        goto memory_error;
    }

    value->data.u.object = error;
    value->type = NJS_OBJECT_ERROR;
    value->data.truth = 1;

    return;

memory_error:

    njs_set_memory_error(vm, value);
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
njs_value_is_void(const njs_value_t *value)
{
    return njs_is_void(value);
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
njs_value_string_copy(njs_vm_t *vm, nxt_str_t *retval, const njs_value_t *value,
    uintptr_t *next)
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

    static const nxt_str_t  entry_main =        nxt_string("main");
    static const nxt_str_t  entry_native =      nxt_string("native");
    static const nxt_str_t  entry_unknown =     nxt_string("unknown");
    static const nxt_str_t  entry_anonymous =   nxt_string("anonymous");

    native_frame = &frame->native;
    function = native_frame->function;

    be = nxt_array_add(vm->backtrace, &njs_array_mem_proto, vm->mem_cache_pool);
    if (nxt_slow_path(be == NULL)) {
        return NXT_ERROR;
    }

    be->line = 0;

    if (function == NULL) {
        be->name = entry_main;
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

        be->name = entry_native;

        return NXT_OK;
    }

    lambda = function->u.lambda;
    debug_entry = vm->debug->start;

    for (i = 0; i < vm->debug->items; i++) {
        if (lambda == debug_entry[i].lambda) {
            if (debug_entry[i].name.length != 0) {
                be->name = debug_entry[i].name;

            } else {
                be->name = entry_anonymous;
            }

            be->line = debug_entry[i].line;

            return NXT_OK;
        }
    }

    be->name = entry_unknown;

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

    case NJS_VOID:
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
    return nxt_mem_cache_align(data, size, size);
}


void
njs_lvlhsh_free(void *data, void *p, size_t size)
{
    nxt_mem_cache_free(data, p);
}
