
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) Dmitry Volyntsev
 * Copyright (C) NGINX, Inc.
 */

#include <njs_core.h>
#include <string.h>


typedef struct njs_generator_patch_s   njs_generator_patch_t;

struct njs_generator_patch_s {
    /*
     * The jump_offset field points to jump offset field which contains a small
     * adjustment and the adjustment should be added as (njs_ret_t *) because
     * pointer to u_char accesses only one byte so this does not work on big
     * endian platforms.
     */
    njs_ret_t                       jump_offset;
    njs_generator_patch_t           *next;

    nxt_str_t                       label;
};


typedef enum {
    NJS_GENERATOR_LOOP = 1,
    NJS_GENERATOR_SWITCH = 2,
    NJS_GENERATOR_BLOCK = 4,
    NJS_GENERATOR_TRY = 8,
#define NJS_GENERATOR_ALL          (NJS_GENERATOR_LOOP | NJS_GENERATOR_SWITCH)
} njs_generator_block_type_t;


struct njs_generator_block_s {
    njs_generator_block_type_t      type;    /* 4 bits */
    nxt_str_t                       label;

    /* list of "continue" instruction offsets to be patched. */
    njs_generator_patch_t           *continuation;
    /*
     * list of "return" from try-catch block and "break"
     * instruction offsets to be patched.
     */
    njs_generator_patch_t           *exit;

    njs_generator_block_t           *next;

    /* exit value index, used only for NJS_GENERATOR_TRY blocks. */
    njs_index_t                     index;
};


static nxt_int_t njs_generator(njs_vm_t *vm, njs_generator_t *generator,
    njs_parser_node_t *node);
static u_char *njs_generate_reserve(njs_vm_t *vm, njs_generator_t *generator,
    size_t size);
static nxt_int_t njs_generate_name(njs_vm_t *vm, njs_generator_t *generator,
    njs_parser_node_t *node);
static nxt_int_t njs_generate_builtin_object(njs_vm_t *vm,
    njs_generator_t *generator, njs_parser_node_t *node);
static nxt_int_t njs_generate_arguments_object(njs_vm_t *vm,
    njs_generator_t *generator, njs_parser_node_t *node);
static nxt_int_t njs_generate_variable(njs_vm_t *vm, njs_generator_t *generator,
    njs_parser_node_t *node);
static nxt_int_t njs_generate_var_statement(njs_vm_t *vm,
    njs_generator_t *generator, njs_parser_node_t *node);
static nxt_int_t njs_generate_if_statement(njs_vm_t *vm,
    njs_generator_t *generator, njs_parser_node_t *node);
static nxt_int_t njs_generate_cond_expression(njs_vm_t *vm,
    njs_generator_t *generator, njs_parser_node_t *node);
static nxt_int_t njs_generate_switch_statement(njs_vm_t *vm,
    njs_generator_t *generator, njs_parser_node_t *node);
static nxt_int_t njs_generate_while_statement(njs_vm_t *vm,
    njs_generator_t *generator, njs_parser_node_t *node);
static nxt_int_t njs_generate_do_while_statement(njs_vm_t *vm,
    njs_generator_t *generator, njs_parser_node_t *node);
static nxt_int_t njs_generate_for_statement(njs_vm_t *vm,
    njs_generator_t *generator, njs_parser_node_t *node);
static nxt_int_t njs_generate_for_in_statement(njs_vm_t *vm,
    njs_generator_t *generator, njs_parser_node_t *node);
static nxt_noinline nxt_int_t njs_generate_start_block(njs_vm_t *vm,
    njs_generator_t *generator, njs_generator_block_type_t type,
    const nxt_str_t *label);
static njs_generator_block_t *njs_generate_lookup_block(
    njs_generator_block_t *block, uint32_t mask, const nxt_str_t *label);
static njs_generator_block_t *njs_generate_find_block(
    njs_generator_block_t *block, uint32_t mask, const nxt_str_t *label);
static nxt_noinline void njs_generate_patch_block(njs_vm_t *vm,
    njs_generator_t *generator, njs_generator_patch_t *list);
static njs_generator_patch_t *njs_generate_make_continuation_patch(njs_vm_t *vm,
    njs_generator_block_t *block, const nxt_str_t *label, njs_ret_t offset);
static njs_generator_patch_t *njs_generate_make_exit_patch(njs_vm_t *vm,
    njs_generator_block_t *block, const nxt_str_t *label, njs_ret_t offset);
static nxt_noinline void njs_generate_patch_block_exit(njs_vm_t *vm,
    njs_generator_t *generator);
static const nxt_str_t *njs_generate_jump_destination(njs_vm_t *vm,
    njs_generator_block_t *block, const char *inst_type, uint32_t mask,
    const nxt_str_t *label1, const nxt_str_t *label2);
static nxt_int_t njs_generate_continue_statement(njs_vm_t *vm,
    njs_generator_t *generator, njs_parser_node_t *node);
static nxt_int_t njs_generate_break_statement(njs_vm_t *vm,
    njs_generator_t *generator, njs_parser_node_t *node);
static nxt_int_t njs_generate_statement(njs_vm_t *vm,
    njs_generator_t *generator, njs_parser_node_t *node);
static nxt_int_t njs_generate_block_statement(njs_vm_t *vm,
    njs_generator_t *generator, njs_parser_node_t *node);
static nxt_int_t njs_generate_children(njs_vm_t *vm, njs_generator_t *generator,
    njs_parser_node_t *node);
static nxt_int_t njs_generate_stop_statement(njs_vm_t *vm,
    njs_generator_t *generator, njs_parser_node_t *node);
static nxt_int_t njs_generate_comma_expression(njs_vm_t *vm,
    njs_generator_t *generator, njs_parser_node_t *node);
static nxt_int_t njs_generate_assignment(njs_vm_t *vm,
    njs_generator_t *generator, njs_parser_node_t *node);
static nxt_int_t njs_generate_operation_assignment(njs_vm_t *vm,
    njs_generator_t *generator, njs_parser_node_t *node);
static nxt_int_t njs_generate_object(njs_vm_t *vm, njs_generator_t *generator,
    njs_parser_node_t *node);
static nxt_int_t njs_generate_array(njs_vm_t *vm, njs_generator_t *generator,
    njs_parser_node_t *node);
static nxt_int_t njs_generate_function(njs_vm_t *vm, njs_generator_t *generator,
    njs_parser_node_t *node);
static nxt_int_t njs_generate_regexp(njs_vm_t *vm, njs_generator_t *generator,
    njs_parser_node_t *node);
static nxt_int_t njs_generate_test_jump_expression(njs_vm_t *vm,
    njs_generator_t *generator, njs_parser_node_t *node);
static nxt_int_t njs_generate_3addr_operation(njs_vm_t *vm,
    njs_generator_t *generator, njs_parser_node_t *node, nxt_bool_t swap);
static nxt_int_t njs_generate_2addr_operation(njs_vm_t *vm,
    njs_generator_t *generator, njs_parser_node_t *node);
static nxt_int_t njs_generate_typeof_operation(njs_vm_t *vm,
    njs_generator_t *generator, njs_parser_node_t *node);
static nxt_int_t njs_generate_inc_dec_operation(njs_vm_t *vm,
    njs_generator_t *generator, njs_parser_node_t *node, nxt_bool_t post);
static nxt_int_t njs_generate_function_declaration(njs_vm_t *vm,
    njs_generator_t *generator, njs_parser_node_t *node);
static nxt_int_t njs_generate_function_scope(njs_vm_t *vm,
    njs_function_lambda_t *lambda, njs_parser_node_t *node,
    const nxt_str_t *name);
static nxt_int_t njs_generate_argument_closures(njs_vm_t *vm,
    njs_generator_t *generator, njs_parser_node_t *node);
static nxt_int_t njs_generate_return_statement(njs_vm_t *vm,
    njs_generator_t *generator, njs_parser_node_t *node);
static nxt_int_t njs_generate_function_call(njs_vm_t *vm,
    njs_generator_t *generator, njs_parser_node_t *node);
static nxt_int_t njs_generate_method_call(njs_vm_t *vm,
    njs_generator_t *generator, njs_parser_node_t *node);
static nxt_noinline nxt_int_t njs_generate_call(njs_vm_t *vm,
    njs_generator_t *generator, njs_parser_node_t *node);
static nxt_int_t njs_generate_try_statement(njs_vm_t *vm,
    njs_generator_t *generator, njs_parser_node_t *node);
static nxt_int_t njs_generate_throw_statement(njs_vm_t *vm,
    njs_generator_t *generator, njs_parser_node_t *node);
static nxt_noinline njs_index_t njs_generate_dest_index(njs_vm_t *vm,
    njs_generator_t *generator, njs_parser_node_t *node);
static nxt_noinline njs_index_t
    njs_generate_object_dest_index(njs_vm_t *vm, njs_generator_t *generator,
    njs_parser_node_t *node);
static njs_index_t njs_generate_node_temp_index_get(njs_vm_t *vm,
    njs_generator_t *generator, njs_parser_node_t *node);
static nxt_noinline njs_index_t njs_generate_temp_index_get(njs_vm_t *vm,
    njs_generator_t *generator, njs_parser_node_t *node);
static nxt_noinline nxt_int_t
    njs_generate_children_indexes_release(njs_vm_t *vm,
    njs_generator_t *generator, njs_parser_node_t *node);
static nxt_noinline nxt_int_t njs_generate_node_index_release(njs_vm_t *vm,
    njs_generator_t *generator, njs_parser_node_t *node);
static nxt_noinline nxt_int_t njs_generate_index_release(njs_vm_t *vm,
    njs_generator_t *generator, njs_index_t index);

static nxt_int_t njs_generate_function_debug(njs_vm_t *vm, nxt_str_t *name,
    njs_function_lambda_t *lambda, njs_parser_node_t *node);


#define njs_generate_code(generator, type, _code, _operation, nargs, _retval) \
    do {                                                                      \
        _code = (type *) njs_generate_reserve(vm, generator, sizeof(type));   \
        if (nxt_slow_path(_code == NULL)) {                                   \
            return NXT_ERROR;                                                 \
        }                                                                     \
                                                                              \
        generator->code_end += sizeof(type);                                  \
                                                                              \
        _code->code.operation = _operation;                                   \
        _code->code.operands = 3 - nargs;                                     \
        _code->code.retval = _retval;                                         \
    } while (0)


#define njs_generate_code_jump(generator, _code, _offset)                     \
    do {                                                                      \
        njs_generate_code(generator, njs_vmcode_jump_t, _code,                \
                          njs_vmcode_jump, 0, 0);                             \
        _code->offset = _offset;                                              \
    } while (0)


#define njs_generate_code_move(generator, _code, _dst, _src)                  \
    do {                                                                      \
        njs_generate_code(generator, njs_vmcode_move_t, _code,                \
                          njs_vmcode_move, 2, 1);                             \
        _code->dst = _dst;                                                    \
        _code->src = _src;                                                    \
    } while (0)


#define njs_code_offset(generator, code)                                      \
    ((u_char *) code - generator->code_start)


#define njs_code_ptr(generator, type, offset)                                 \
    (type *) (generator->code_start + offset)


#define njs_code_jump_ptr(generator, offset)                                  \
    (njs_ret_t *) (generator->code_start + offset)


#define njs_code_offset_diff(generator, offset)                               \
    ((generator->code_end - generator->code_start) - offset)


#define njs_code_set_offset(generator, offset, target)                        \
    *(njs_code_jump_ptr(generator, offset))                                   \
        = njs_code_offset_diff(generator, target)


#define njs_code_set_jump_offset(generator, type, code_offset)                \
    *(njs_code_jump_ptr(generator, code_offset + offsetof(type, offset)))     \
        = njs_code_offset_diff(generator, code_offset)


#define njs_code_update_offset(generator, patch)                              \
    *(njs_code_jump_ptr(generator, patch->jump_offset)) +=                    \
        njs_code_offset_diff(generator, patch->jump_offset)


#define njs_generate_syntax_error(vm, node, fmt, ...)                         \
    njs_parser_node_error(vm, node, NJS_OBJECT_SYNTAX_ERROR, fmt, ##__VA_ARGS__)


static const nxt_str_t  no_label     = nxt_string("");
static const nxt_str_t  return_label = nxt_string("@return");
/* GCC and Clang complain about NULL argument passed to memcmp(). */
static const nxt_str_t  undef_label  = { 0xffffffff, (u_char *) "" };


static nxt_int_t
njs_generator(njs_vm_t *vm, njs_generator_t *generator, njs_parser_node_t *node)
{
    if (node == NULL) {
        return NXT_OK;
    }

    switch (node->token) {

    case NJS_TOKEN_VAR:
        return njs_generate_var_statement(vm, generator, node);

    case NJS_TOKEN_IF:
        return njs_generate_if_statement(vm, generator, node);

    case NJS_TOKEN_CONDITIONAL:
        return njs_generate_cond_expression(vm, generator, node);

    case NJS_TOKEN_SWITCH:
        return njs_generate_switch_statement(vm, generator, node);

    case NJS_TOKEN_WHILE:
        return njs_generate_while_statement(vm, generator, node);

    case NJS_TOKEN_DO:
        return njs_generate_do_while_statement(vm, generator, node);

    case NJS_TOKEN_FOR:
        return njs_generate_for_statement(vm, generator, node);

    case NJS_TOKEN_FOR_IN:
        return njs_generate_for_in_statement(vm, generator, node);

    case NJS_TOKEN_CONTINUE:
        return njs_generate_continue_statement(vm, generator, node);

    case NJS_TOKEN_BREAK:
        return njs_generate_break_statement(vm, generator, node);

    case NJS_TOKEN_STATEMENT:
        return njs_generate_statement(vm, generator, node);

    case NJS_TOKEN_BLOCK:
        return njs_generate_block_statement(vm, generator, node);

    case NJS_TOKEN_END:
        return njs_generate_stop_statement(vm, generator, node);

    case NJS_TOKEN_COMMA:
        return njs_generate_comma_expression(vm, generator, node);

    case NJS_TOKEN_ASSIGNMENT:
        return njs_generate_assignment(vm, generator, node);

    case NJS_TOKEN_BITWISE_OR_ASSIGNMENT:
    case NJS_TOKEN_BITWISE_XOR_ASSIGNMENT:
    case NJS_TOKEN_BITWISE_AND_ASSIGNMENT:
    case NJS_TOKEN_LEFT_SHIFT_ASSIGNMENT:
    case NJS_TOKEN_RIGHT_SHIFT_ASSIGNMENT:
    case NJS_TOKEN_UNSIGNED_RIGHT_SHIFT_ASSIGNMENT:
    case NJS_TOKEN_ADDITION_ASSIGNMENT:
    case NJS_TOKEN_SUBSTRACTION_ASSIGNMENT:
    case NJS_TOKEN_MULTIPLICATION_ASSIGNMENT:
    case NJS_TOKEN_EXPONENTIATION_ASSIGNMENT:
    case NJS_TOKEN_DIVISION_ASSIGNMENT:
    case NJS_TOKEN_REMAINDER_ASSIGNMENT:
        return njs_generate_operation_assignment(vm, generator, node);

    case NJS_TOKEN_BITWISE_OR:
    case NJS_TOKEN_BITWISE_XOR:
    case NJS_TOKEN_BITWISE_AND:
    case NJS_TOKEN_EQUAL:
    case NJS_TOKEN_NOT_EQUAL:
    case NJS_TOKEN_STRICT_EQUAL:
    case NJS_TOKEN_STRICT_NOT_EQUAL:
    case NJS_TOKEN_INSTANCEOF:
    case NJS_TOKEN_LESS:
    case NJS_TOKEN_LESS_OR_EQUAL:
    case NJS_TOKEN_GREATER:
    case NJS_TOKEN_GREATER_OR_EQUAL:
    case NJS_TOKEN_LEFT_SHIFT:
    case NJS_TOKEN_RIGHT_SHIFT:
    case NJS_TOKEN_UNSIGNED_RIGHT_SHIFT:
    case NJS_TOKEN_ADDITION:
    case NJS_TOKEN_SUBSTRACTION:
    case NJS_TOKEN_MULTIPLICATION:
    case NJS_TOKEN_EXPONENTIATION:
    case NJS_TOKEN_DIVISION:
    case NJS_TOKEN_REMAINDER:
    case NJS_TOKEN_PROPERTY_DELETE:
    case NJS_TOKEN_PROPERTY:
        return njs_generate_3addr_operation(vm, generator, node, 0);

    case NJS_TOKEN_IN:
        /*
         * An "in" operation is parsed as standard binary expression
         * by njs_parser_binary_expression().  However, its operands
         * should be swapped to be uniform with other property operations
         * (get/set and delete) to use the property trap.
         */
        return njs_generate_3addr_operation(vm, generator, node, 1);

    case NJS_TOKEN_LOGICAL_AND:
    case NJS_TOKEN_LOGICAL_OR:
        return njs_generate_test_jump_expression(vm, generator, node);

    case NJS_TOKEN_DELETE:
    case NJS_TOKEN_VOID:
    case NJS_TOKEN_UNARY_PLUS:
    case NJS_TOKEN_UNARY_NEGATION:
    case NJS_TOKEN_LOGICAL_NOT:
    case NJS_TOKEN_BITWISE_NOT:
        return njs_generate_2addr_operation(vm, generator, node);

    case NJS_TOKEN_TYPEOF:
        return njs_generate_typeof_operation(vm, generator, node);

    case NJS_TOKEN_INCREMENT:
    case NJS_TOKEN_DECREMENT:
        return njs_generate_inc_dec_operation(vm, generator, node, 0);

    case NJS_TOKEN_POST_INCREMENT:
    case NJS_TOKEN_POST_DECREMENT:
        return njs_generate_inc_dec_operation(vm, generator, node, 1);

    case NJS_TOKEN_UNDEFINED:
    case NJS_TOKEN_NULL:
    case NJS_TOKEN_BOOLEAN:
    case NJS_TOKEN_NUMBER:
    case NJS_TOKEN_STRING:
        node->index = njs_value_index(vm, &node->u.value, generator->runtime);

        if (nxt_fast_path(node->index != NJS_INDEX_NONE)) {
            return NXT_OK;
        }

        return NXT_ERROR;

    case NJS_TOKEN_OBJECT_VALUE:
        node->index = node->u.object->index;
        return NXT_OK;

    case NJS_TOKEN_OBJECT:
        return njs_generate_object(vm, generator, node);

    case NJS_TOKEN_ARRAY:
        return njs_generate_array(vm, generator, node);

    case NJS_TOKEN_FUNCTION_EXPRESSION:
        return njs_generate_function(vm, generator, node);

    case NJS_TOKEN_REGEXP:
        return njs_generate_regexp(vm, generator, node);

    case NJS_TOKEN_THIS:
    case NJS_TOKEN_OBJECT_CONSTRUCTOR:
    case NJS_TOKEN_ARRAY_CONSTRUCTOR:
    case NJS_TOKEN_NUMBER_CONSTRUCTOR:
    case NJS_TOKEN_BOOLEAN_CONSTRUCTOR:
    case NJS_TOKEN_STRING_CONSTRUCTOR:
    case NJS_TOKEN_FUNCTION_CONSTRUCTOR:
    case NJS_TOKEN_REGEXP_CONSTRUCTOR:
    case NJS_TOKEN_DATE_CONSTRUCTOR:
    case NJS_TOKEN_ERROR_CONSTRUCTOR:
    case NJS_TOKEN_EVAL_ERROR_CONSTRUCTOR:
    case NJS_TOKEN_INTERNAL_ERROR_CONSTRUCTOR:
    case NJS_TOKEN_RANGE_ERROR_CONSTRUCTOR:
    case NJS_TOKEN_REF_ERROR_CONSTRUCTOR:
    case NJS_TOKEN_SYNTAX_ERROR_CONSTRUCTOR:
    case NJS_TOKEN_TYPE_ERROR_CONSTRUCTOR:
    case NJS_TOKEN_URI_ERROR_CONSTRUCTOR:
    case NJS_TOKEN_MEMORY_ERROR_CONSTRUCTOR:
    case NJS_TOKEN_EXTERNAL:
        return NXT_OK;

    case NJS_TOKEN_NAME:
        return njs_generate_name(vm, generator, node);

    case NJS_TOKEN_GLOBAL_THIS:
    case NJS_TOKEN_NJS:
    case NJS_TOKEN_MATH:
    case NJS_TOKEN_JSON:
    case NJS_TOKEN_EVAL:
    case NJS_TOKEN_TO_STRING:
    case NJS_TOKEN_IS_NAN:
    case NJS_TOKEN_IS_FINITE:
    case NJS_TOKEN_PARSE_INT:
    case NJS_TOKEN_PARSE_FLOAT:
    case NJS_TOKEN_ENCODE_URI:
    case NJS_TOKEN_ENCODE_URI_COMPONENT:
    case NJS_TOKEN_DECODE_URI:
    case NJS_TOKEN_DECODE_URI_COMPONENT:
    case NJS_TOKEN_REQUIRE:
    case NJS_TOKEN_SET_TIMEOUT:
    case NJS_TOKEN_SET_IMMEDIATE:
    case NJS_TOKEN_CLEAR_TIMEOUT:
        return njs_generate_builtin_object(vm, generator, node);

    case NJS_TOKEN_ARGUMENTS:
        return njs_generate_arguments_object(vm, generator, node);

    case NJS_TOKEN_FUNCTION:
        return njs_generate_function_declaration(vm, generator, node);

    case NJS_TOKEN_FUNCTION_CALL:
        return njs_generate_function_call(vm, generator, node);

    case NJS_TOKEN_RETURN:
        return njs_generate_return_statement(vm, generator, node);

    case NJS_TOKEN_METHOD_CALL:
        return njs_generate_method_call(vm, generator, node);

    case NJS_TOKEN_TRY:
        return njs_generate_try_statement(vm, generator, node);

    case NJS_TOKEN_THROW:
        return njs_generate_throw_statement(vm, generator, node);

    default:
        nxt_thread_log_debug("unknown token: %d", node->token);
        njs_internal_error(vm, "Generator failed: unknown token");

        return NXT_ERROR;
    }
}


static u_char *
njs_generate_reserve(njs_vm_t *vm, njs_generator_t *generator, size_t size)
{
    u_char  *p;

    if (generator->code_end + size <=
        generator->code_start + generator->code_size)
    {
        return generator->code_end;
    }

    size = nxt_max(generator->code_end - generator->code_start + size,
                   generator->code_size);

    if (size < 1024) {
        size *= 2;

    } else {
        size += size / 2;
    }

    p = nxt_mp_alloc(vm->mem_pool, size);
    if (nxt_slow_path(p == NULL)) {
        njs_memory_error(vm);
        return NULL;
    }

    generator->code_size = size;

    size = generator->code_end - generator->code_start;
    memcpy(p, generator->code_start, size);

    nxt_mp_free(vm->mem_pool, generator->code_start);

    generator->code_start = p;
    generator->code_end = p + size;

    return generator->code_end;
}


static nxt_int_t
njs_generate_name(njs_vm_t *vm, njs_generator_t *generator,
    njs_parser_node_t *node)
{
    njs_variable_t            *var;
    njs_vmcode_object_copy_t  *copy;

    var = njs_variable_resolve(vm, node);
    if (nxt_slow_path(var == NULL)) {
        return NXT_ERROR;
    }

    if (var->type == NJS_VARIABLE_FUNCTION) {

        node->index = njs_generate_dest_index(vm, generator, node);
        if (nxt_slow_path(node->index == NJS_INDEX_ERROR)) {
            return node->index;
        }

        njs_generate_code(generator, njs_vmcode_object_copy_t, copy,
                          njs_vmcode_object_copy, 2, 1);
        copy->retval = node->index;
        copy->object = var->index;

        return NXT_OK;
    }

    return njs_generate_variable(vm, generator, node);
}


static nxt_int_t
njs_generate_builtin_object(njs_vm_t *vm, njs_generator_t *generator,
    njs_parser_node_t *node)
{
    njs_index_t               index;
    njs_vmcode_object_copy_t  *copy;

    index = njs_variable_index(vm, node);
    if (nxt_slow_path(index == NJS_INDEX_ERROR)) {
        return NXT_ERROR;
    }

    node->index = njs_generate_dest_index(vm, generator, node);
    if (nxt_slow_path(node->index == NJS_INDEX_ERROR)) {
        return NXT_ERROR;
    }

    njs_generate_code(generator, njs_vmcode_object_copy_t, copy,
                      njs_vmcode_object_copy, 2, 1);
    copy->retval = node->index;
    copy->object = index;

    return NXT_OK;
}


static nxt_int_t
njs_generate_arguments_object(njs_vm_t *vm, njs_generator_t *generator,
    njs_parser_node_t *node)
{
    njs_vmcode_arguments_t  *gen;

    node->index = njs_generate_object_dest_index(vm, generator, node);
    if (nxt_slow_path(node->index == NJS_INDEX_ERROR)) {
        return NXT_ERROR;
    }

    njs_generate_code(generator, njs_vmcode_arguments_t, gen,
                      njs_vmcode_arguments, 1, 1);
    gen->retval = node->index;

    return NXT_OK;
}


static nxt_int_t
njs_generate_variable(njs_vm_t *vm, njs_generator_t *generator,
    njs_parser_node_t *node)
{
    njs_index_t  index;

    index = njs_variable_index(vm, node);
    if (nxt_slow_path(index == NJS_INDEX_ERROR)) {
        return NXT_ERROR;
    }

    node->index = index;

    return NXT_OK;
}


static nxt_int_t
njs_generate_var_statement(njs_vm_t *vm, njs_generator_t *generator,
    njs_parser_node_t *node)
{
    nxt_int_t          ret;
    njs_index_t        index;
    njs_parser_node_t  *lvalue, *expr;
    njs_vmcode_move_t  *move;

    lvalue = node->left;

    index = njs_variable_index(vm, lvalue);
    if (nxt_slow_path(index == NJS_INDEX_ERROR)) {
        return NXT_ERROR;
    }

    lvalue->index = index;

    expr = node->right;

    if (expr == NULL) {
        /* Variable is only declared. */
        return NXT_OK;
    }

    expr->dest = lvalue;

    ret = njs_generator(vm, generator, expr);
    if (nxt_slow_path(ret != NXT_OK)) {
        return ret;
    }

    /*
     * lvalue and expression indexes are equal if the expression is an
     * empty object or expression result is stored directly in variable.
     */
    if (lvalue->index != expr->index) {
        njs_generate_code_move(generator, move, lvalue->index, expr->index);
    }

    node->index = expr->index;
    node->temporary = expr->temporary;

    return NXT_OK;
}


static nxt_int_t
njs_generate_if_statement(njs_vm_t *vm, njs_generator_t *generator,
    njs_parser_node_t *node)
{
    njs_ret_t               jump_offset, label_offset;
    nxt_int_t               ret;
    njs_vmcode_jump_t       *jump;
    njs_vmcode_cond_jump_t  *cond_jump;

    /* The condition expression. */

    ret = njs_generator(vm, generator, node->left);
    if (nxt_slow_path(ret != NXT_OK)) {
        return ret;
    }

    njs_generate_code(generator, njs_vmcode_cond_jump_t, cond_jump,
                      njs_vmcode_if_false_jump, 2, 0);
    cond_jump->cond = node->left->index;

    ret = njs_generate_node_index_release(vm, generator, node->left);
    if (nxt_slow_path(ret != NXT_OK)) {
        return ret;
    }

    jump_offset = njs_code_offset(generator, cond_jump);
    label_offset = jump_offset + offsetof(njs_vmcode_cond_jump_t, offset);

    if (node->right != NULL && node->right->token == NJS_TOKEN_BRANCHING) {

        /* The "then" branch in a case of "if/then/else" statement. */

        node = node->right;

        ret = njs_generator(vm, generator, node->left);
        if (nxt_slow_path(ret != NXT_OK)) {
            return ret;
        }

        ret = njs_generate_node_index_release(vm, generator, node->left);
        if (nxt_slow_path(ret != NXT_OK)) {
            return ret;
        }

        njs_generate_code_jump(generator, jump, 0);

        njs_code_set_offset(generator, label_offset, jump_offset);

        jump_offset = njs_code_offset(generator, jump);
        label_offset = jump_offset + offsetof(njs_vmcode_jump_t, offset);
    }

    /*
     * The "then" branch in a case of "if/then" statement
     * or the "else" branch in a case of "if/then/else" statement.
     */

    ret = njs_generator(vm, generator, node->right);
    if (nxt_slow_path(ret != NXT_OK)) {
        return ret;
    }

    ret = njs_generate_node_index_release(vm, generator, node->right);
    if (nxt_slow_path(ret != NXT_OK)) {
        return ret;
    }

    njs_code_set_offset(generator, label_offset, jump_offset);

    return NXT_OK;
}


static nxt_int_t
njs_generate_cond_expression(njs_vm_t *vm, njs_generator_t *generator,
    njs_parser_node_t *node)
{
    njs_ret_t               jump_offset, cond_jump_offset;
    nxt_int_t               ret;
    njs_parser_node_t       *branch;
    njs_vmcode_move_t       *move;
    njs_vmcode_jump_t       *jump;
    njs_vmcode_cond_jump_t  *cond_jump;

    /* The condition expression. */

    ret = njs_generator(vm, generator, node->left);
    if (nxt_slow_path(ret != NXT_OK)) {
        return ret;
    }

    njs_generate_code(generator, njs_vmcode_cond_jump_t, cond_jump,
                      njs_vmcode_if_false_jump, 2, 0);

    cond_jump_offset = njs_code_offset(generator, cond_jump);
    cond_jump->cond = node->left->index;

    node->index = njs_generate_dest_index(vm, generator, node);
    if (nxt_slow_path(node->index == NJS_INDEX_ERROR)) {
        return node->index;
    }

    branch = node->right;

    /* The "true" branch. */

    ret = njs_generator(vm, generator, branch->left);
    if (nxt_slow_path(ret != NXT_OK)) {
        return ret;
    }

    /*
     * Branches usually uses node->index as destination, however,
     * if branch expression is a literal, variable or assignment,
     * then a MOVE operation is required.
     */

    if (node->index != branch->left->index) {
        njs_generate_code_move(generator, move, node->index,
                               branch->left->index);
    }

    ret = njs_generate_node_index_release(vm, generator, branch->left);
    if (nxt_slow_path(ret != NXT_OK)) {
        return ret;
    }

    njs_generate_code_jump(generator, jump, 0);
    jump_offset = njs_code_offset(generator, jump);

    njs_code_set_jump_offset(generator, njs_vmcode_cond_jump_t,
                             cond_jump_offset);

    /* The "false" branch. */

    ret = njs_generator(vm, generator, branch->right);
    if (nxt_slow_path(ret != NXT_OK)) {
        return ret;
    }

    if (node->index != branch->right->index) {
        njs_generate_code_move(generator, move, node->index,
                               branch->right->index);
    }

    njs_code_set_jump_offset(generator, njs_vmcode_cond_jump_t, jump_offset);

    ret = njs_generate_node_index_release(vm, generator, branch->right);
    if (nxt_slow_path(ret != NXT_OK)) {
        return ret;
    }

    return NXT_OK;
}


static nxt_int_t
njs_generate_switch_statement(njs_vm_t *vm, njs_generator_t *generator,
    njs_parser_node_t *swtch)
{
    njs_ret_t                jump_offset;
    nxt_int_t                ret;
    njs_index_t              index;
    njs_parser_node_t        *node, *expr, *branch;
    njs_vmcode_move_t        *move;
    njs_vmcode_jump_t        *jump;
    njs_generator_patch_t    *patch, *next, *patches, **last;
    njs_vmcode_equal_jump_t  *equal;

    /* The "switch" expression. */

    expr = swtch->left;

    ret = njs_generator(vm, generator, expr);
    if (nxt_slow_path(ret != NXT_OK)) {
        return ret;
    }

    index = expr->index;

    if (!expr->temporary) {
        index = njs_generate_temp_index_get(vm, generator, swtch);
        if (nxt_slow_path(index == NJS_INDEX_ERROR)) {
            return NXT_ERROR;
        }

        njs_generate_code_move(generator, move, index, expr->index);
    }

    ret = njs_generate_start_block(vm, generator, NJS_GENERATOR_SWITCH,
                                   &swtch->label);
    if (nxt_slow_path(ret != NXT_OK)) {
        return ret;
    }

    patches = NULL;
    last = &patches;

    for (branch = swtch->right; branch != NULL; branch = branch->left) {

        if (branch->token != NJS_TOKEN_DEFAULT) {

            /* The "case" expression. */

            node = branch->right;

            ret = njs_generator(vm, generator, node->left);
            if (nxt_slow_path(ret != NXT_OK)) {
                return ret;
            }

            njs_generate_code(generator, njs_vmcode_equal_jump_t, equal,
                              njs_vmcode_if_equal_jump, 3, 0);
            equal->offset = offsetof(njs_vmcode_equal_jump_t, offset);
            equal->value1 = index;
            equal->value2 = node->left->index;

            ret = njs_generate_node_index_release(vm, generator, node->left);
            if (nxt_slow_path(ret != NXT_OK)) {
                return ret;
            }

            patch = nxt_mp_alloc(vm->mem_pool, sizeof(njs_generator_patch_t));
            if (nxt_slow_path(patch == NULL)) {
                return NXT_ERROR;
            }

            patch->jump_offset = njs_code_offset(generator, equal)
                                 + offsetof(njs_vmcode_equal_jump_t, offset);
            patch->label = no_label;

            *last = patch;
            last = &patch->next;
        }
    }

    /* Release either temporary index or temporary expr->index. */
    ret = njs_generate_index_release(vm, generator, index);
    if (nxt_slow_path(ret != NXT_OK)) {
        return ret;
    }

    njs_generate_code_jump(generator, jump,
                           offsetof(njs_vmcode_jump_t, offset));

    jump_offset = njs_code_offset(generator, jump);

    patch = patches;

    for (branch = swtch->right; branch != NULL; branch = branch->left) {

        if (branch->token == NJS_TOKEN_DEFAULT) {
            njs_code_set_jump_offset(generator, njs_vmcode_jump_t, jump_offset);
            jump = NULL;
            node = branch;

        } else {
            njs_code_update_offset(generator, patch);

            next = patch->next;

            nxt_mp_free(vm->mem_pool, patch);

            patch = next;
            node = branch->right;
        }

        /* The "case/default" statements. */

        ret = njs_generator(vm, generator, node->right);
        if (nxt_slow_path(ret != NXT_OK)) {
            return ret;
        }
    }

    if (jump != NULL) {
        /* A "switch" without default case. */
        njs_code_set_jump_offset(generator, njs_vmcode_jump_t, jump_offset);
    }

    /* Patch "break" statements offsets. */
    njs_generate_patch_block_exit(vm, generator);

    return NXT_OK;
}


static nxt_int_t
njs_generate_while_statement(njs_vm_t *vm, njs_generator_t *generator,
    njs_parser_node_t *node)
{
    njs_ret_t               jump_offset, loop_offset;
    nxt_int_t               ret;
    njs_parser_node_t       *condition;
    njs_vmcode_jump_t       *jump;
    njs_vmcode_cond_jump_t  *cond_jump;

    /*
     * Set a jump to the loop condition.  This jump is executed once just on
     * the loop enter and eliminates execution of one additional jump inside
     * the loop per each iteration.
     */

    njs_generate_code_jump(generator, jump, 0);
    jump_offset = njs_code_offset(generator, jump);

    /* The loop body. */

    ret = njs_generate_start_block(vm, generator, NJS_GENERATOR_LOOP,
                                   &node->label);
    if (nxt_slow_path(ret != NXT_OK)) {
        return ret;
    }

    loop_offset = njs_code_offset(generator, generator->code_end);

    ret = njs_generator(vm, generator, node->left);
    if (nxt_slow_path(ret != NXT_OK)) {
        return ret;
    }

    /* The loop condition. */

    njs_generate_patch_block(vm, generator, generator->block->continuation);

    njs_code_set_jump_offset(generator, njs_vmcode_jump_t, jump_offset);

    condition = node->right;

    ret = njs_generator(vm, generator, condition);
    if (nxt_slow_path(ret != NXT_OK)) {
        return ret;
    }

    njs_generate_code(generator, njs_vmcode_cond_jump_t, cond_jump,
                      njs_vmcode_if_true_jump, 2, 0);
    cond_jump->offset = loop_offset - njs_code_offset(generator, cond_jump);
    cond_jump->cond = condition->index;

    njs_generate_patch_block_exit(vm, generator);

    return njs_generate_node_index_release(vm, generator, condition);
}


static nxt_int_t
njs_generate_do_while_statement(njs_vm_t *vm, njs_generator_t *generator,
    njs_parser_node_t *node)
{
    njs_ret_t               loop_offset;
    nxt_int_t               ret;
    njs_parser_node_t       *condition;
    njs_vmcode_cond_jump_t  *cond_jump;

    /* The loop body. */

    ret = njs_generate_start_block(vm, generator, NJS_GENERATOR_LOOP,
                                   &node->label);
    if (nxt_slow_path(ret != NXT_OK)) {
        return ret;
    }

    loop_offset = njs_code_offset(generator, generator->code_end);

    ret = njs_generator(vm, generator, node->left);
    if (nxt_slow_path(ret != NXT_OK)) {
        return ret;
    }

    /* The loop condition. */

    njs_generate_patch_block(vm, generator, generator->block->continuation);

    condition = node->right;

    ret = njs_generator(vm, generator, condition);
    if (nxt_slow_path(ret != NXT_OK)) {
        return ret;
    }

    njs_generate_code(generator, njs_vmcode_cond_jump_t, cond_jump,
                      njs_vmcode_if_true_jump, 2, 0);
    cond_jump->offset = loop_offset - njs_code_offset(generator, cond_jump);
    cond_jump->cond = condition->index;

    njs_generate_patch_block_exit(vm, generator);

    return njs_generate_node_index_release(vm, generator, condition);
}


static nxt_int_t
njs_generate_for_statement(njs_vm_t *vm, njs_generator_t *generator,
    njs_parser_node_t *node)
{
    njs_ret_t               jump_offset, loop_offset;
    nxt_int_t               ret;
    njs_parser_node_t       *condition, *update;
    njs_vmcode_jump_t       *jump;
    njs_vmcode_cond_jump_t  *cond_jump;

    ret = njs_generate_start_block(vm, generator, NJS_GENERATOR_LOOP,
                                   &node->label);
    if (nxt_slow_path(ret != NXT_OK)) {
        return ret;
    }

    jump = NULL;

    /* The loop initialization. */

    ret = njs_generator(vm, generator, node->left);
    if (nxt_slow_path(ret != NXT_OK)) {
        return ret;
    }

    ret = njs_generate_node_index_release(vm, generator, node->left);
    if (nxt_slow_path(ret != NXT_OK)) {
        return ret;
    }

    node = node->right;
    condition = node->left;

    /* GCC complains about uninitialized jump_offset. */
    jump_offset = 0;

    if (condition != NULL) {
        /*
         * The loop condition presents so set a jump to it.  This jump is
         * executed once just after the loop initialization and eliminates
         * execution of one additional jump inside the loop per each iteration.
         */
        njs_generate_code_jump(generator, jump, 0);
        jump_offset = njs_code_offset(generator, jump);
    }

    /* The loop body. */

    loop_offset = njs_code_offset(generator, generator->code_end);

    node = node->right;

    ret = njs_generator(vm, generator, node->left);
    if (nxt_slow_path(ret != NXT_OK)) {
        return ret;
    }

    /* The loop update. */

    njs_generate_patch_block(vm, generator, generator->block->continuation);

    update = node->right;

    ret = njs_generator(vm, generator, update);
    if (nxt_slow_path(ret != NXT_OK)) {
        return ret;
    }

    ret = njs_generate_node_index_release(vm, generator, update);
    if (nxt_slow_path(ret != NXT_OK)) {
        return ret;
    }

    /* The loop condition. */

    if (condition != NULL) {
        njs_code_set_jump_offset(generator, njs_vmcode_jump_t, jump_offset);

        ret = njs_generator(vm, generator, condition);
        if (nxt_slow_path(ret != NXT_OK)) {
            return ret;
        }

        njs_generate_code(generator, njs_vmcode_cond_jump_t, cond_jump,
                          njs_vmcode_if_true_jump, 2, 0);
        cond_jump->offset = loop_offset - njs_code_offset(generator, cond_jump);
        cond_jump->cond = condition->index;

        njs_generate_patch_block_exit(vm, generator);

        return njs_generate_node_index_release(vm, generator, condition);
    }

    njs_generate_code_jump(generator, jump,
                           loop_offset - njs_code_offset(generator, jump));

    njs_generate_patch_block_exit(vm, generator);

    return NXT_OK;
}


static nxt_int_t
njs_generate_for_in_statement(njs_vm_t *vm, njs_generator_t *generator,
    njs_parser_node_t *node)
{
    njs_ret_t                  loop_offset, prop_offset;
    nxt_int_t                  ret;
    njs_index_t                index;
    njs_parser_node_t          *foreach;
    njs_vmcode_prop_next_t     *prop_next;
    njs_vmcode_prop_foreach_t  *prop_foreach;

    ret = njs_generate_start_block(vm, generator, NJS_GENERATOR_LOOP,
                                   &node->label);
    if (nxt_slow_path(ret != NXT_OK)) {
        return ret;
    }

    /* The object. */

    foreach = node->left;

    ret = njs_generator(vm, generator, foreach->right);
    if (nxt_slow_path(ret != NXT_OK)) {
        return ret;
    }

    njs_generate_code(generator, njs_vmcode_prop_foreach_t, prop_foreach,
                      njs_vmcode_property_foreach, 2, 1);
    prop_offset = njs_code_offset(generator, prop_foreach);
    prop_foreach->object = foreach->right->index;

    index = njs_generate_temp_index_get(vm, generator, foreach->right);
    if (nxt_slow_path(index == NJS_INDEX_ERROR)) {
        return NXT_ERROR;
    }

    prop_foreach->next = index;

    /* The loop body. */

    loop_offset = njs_code_offset(generator, generator->code_end);

    ret = njs_generator(vm, generator, node->right);
    if (nxt_slow_path(ret != NXT_OK)) {
        return ret;
    }

    /* The loop iterator. */

    njs_generate_patch_block(vm, generator, generator->block->continuation);

    njs_code_set_jump_offset(generator, njs_vmcode_prop_foreach_t, prop_offset);

    ret = njs_generator(vm, generator, node->left->left);
    if (nxt_slow_path(ret != NXT_OK)) {
        return ret;
    }

    njs_generate_code(generator, njs_vmcode_prop_next_t, prop_next,
                      njs_vmcode_property_next, 3, 0);
    prop_offset = njs_code_offset(generator, prop_next);
    prop_next->retval = foreach->left->index;
    prop_next->object = foreach->right->index;
    prop_next->next = index;
    prop_next->offset = loop_offset - prop_offset;

    njs_generate_patch_block_exit(vm, generator);

    /*
     * Release object and iterator indexes: an object can be a function result
     * or a property of another object and an iterator can be given with "let".
     */
    ret = njs_generate_children_indexes_release(vm, generator, foreach);
    if (nxt_slow_path(ret != NXT_OK)) {
        return ret;
    }

    return njs_generate_index_release(vm, generator, index);
}


static nxt_noinline nxt_int_t
njs_generate_start_block(njs_vm_t *vm, njs_generator_t *generator,
    njs_generator_block_type_t type, const nxt_str_t *label)
{
    njs_generator_block_t  *block;

    block = nxt_mp_alloc(vm->mem_pool, sizeof(njs_generator_block_t));

    if (nxt_fast_path(block != NULL)) {
        block->next = generator->block;
        generator->block = block;

        block->type = type;
        block->label = *label;
        block->continuation = NULL;
        block->exit = NULL;

        block->index = 0;

        return NXT_OK;
    }

    return NXT_ERROR;
}


static njs_generator_block_t *
njs_generate_lookup_block(njs_generator_block_t *block, uint32_t mask,
    const nxt_str_t *label)
{
    if (nxt_strstr_eq(label, &return_label)) {
        mask = NJS_GENERATOR_TRY;
        label = &no_label;
    }

    while (block != NULL) {
        if ((block->type & mask) != 0
            && (label->length == 0 || nxt_strstr_eq(&block->label, label)))
        {
            return block;
        }

        block = block->next;
    }

    return NULL;
}


static njs_generator_block_t *
njs_generate_find_block(njs_generator_block_t *block, uint32_t mask,
    const nxt_str_t *label)
{
    njs_generator_block_t  *dest_block;

    /*
     * ES5.1: 12.8 The break Statement
     * "break" without a label is valid only from within
     * loop or switch statement.
     */
    if ((mask & NJS_GENERATOR_ALL) == NJS_GENERATOR_ALL
        && !nxt_strstr_eq(label, &no_label))
    {
        mask |= NJS_GENERATOR_BLOCK;
    }

    dest_block = njs_generate_lookup_block(block, mask, label);

    if (dest_block != NULL) {

        /*
         * Looking for intermediate try-catch blocks. Before jumping to
         * the destination finally blocks have to be executed.
         */

        while (block != NULL) {
            if (block->type & NJS_GENERATOR_TRY) {
                return block;
            }

            if (block == dest_block) {
                return block;
            }

            block = block->next;
        }
    }

    return dest_block;
}


static njs_generator_patch_t *
njs_generate_make_continuation_patch(njs_vm_t *vm, njs_generator_block_t *block,
    const nxt_str_t *label, njs_ret_t offset)
{
    njs_generator_patch_t  *patch;

    patch = nxt_mp_alloc(vm->mem_pool, sizeof(njs_generator_patch_t));
    if (nxt_slow_path(patch == NULL)) {
        njs_memory_error(vm);
        return NULL;
    }

    patch->next = block->continuation;
    block->continuation = patch;

    patch->jump_offset = offset;

    patch->label = *label;

    return patch;
}


static nxt_noinline void
njs_generate_patch_block(njs_vm_t *vm, njs_generator_t *generator,
    njs_generator_patch_t *list)
{
    njs_generator_patch_t  *patch, *next;

    for (patch = list; patch != NULL; patch = next) {
        njs_code_update_offset(generator, patch);
        next = patch->next;

        nxt_mp_free(vm->mem_pool, patch);
    }
}


static njs_generator_patch_t *
njs_generate_make_exit_patch(njs_vm_t *vm, njs_generator_block_t *block,
    const nxt_str_t *label, njs_ret_t offset)
{
    njs_generator_patch_t  *patch;

    patch = nxt_mp_alloc(vm->mem_pool, sizeof(njs_generator_patch_t));
    if (nxt_slow_path(patch == NULL)) {
        njs_memory_error(vm);
        return NULL;
    }

    patch->next = block->exit;
    block->exit = patch;

    patch->jump_offset = offset;

    patch->label = *label;

    return patch;
}


static nxt_noinline void
njs_generate_patch_block_exit(njs_vm_t *vm, njs_generator_t *generator)
{
    njs_generator_block_t  *block;

    block = generator->block;
    generator->block = block->next;

    njs_generate_patch_block(vm, generator, block->exit);

    nxt_mp_free(vm->mem_pool, block);
}


/*
 * TODO: support multiple destination points from within try-catch block.
 */
static const nxt_str_t *
njs_generate_jump_destination(njs_vm_t *vm, njs_generator_block_t *block,
    const char *inst_type, uint32_t mask, const nxt_str_t *label1,
    const nxt_str_t *label2)
{
    njs_generator_block_t  *block1, *block2;

    if (label1->length == undef_label.length) {
        return label2;
    }

    if (label2->length == undef_label.length) {
        return label1;
    }

    block1 = njs_generate_lookup_block(block, mask, label1);
    block2 = njs_generate_lookup_block(block, mask, label2);

    if (block1 != block2) {
        njs_internal_error(vm, "%s instructions with different labels "
                           "(\"%V\" vs \"%V\") "
                           "from try-catch block are not supported", inst_type,
                            label1, label2);

        return NULL;
    }

    return label1;
}


static nxt_int_t
njs_generate_continue_statement(njs_vm_t *vm, njs_generator_t *generator,
    njs_parser_node_t *node)
{
    const nxt_str_t        *label, *dest;
    njs_vmcode_jump_t      *jump;
    njs_generator_patch_t  *patch;
    njs_generator_block_t  *block;

    label = &node->label;

    block = njs_generate_find_block(generator->block, NJS_GENERATOR_LOOP,
                                    label);

    if (nxt_slow_path(block == NULL)) {
        goto syntax_error;
    }

    if (block->type == NJS_GENERATOR_TRY && block->continuation != NULL) {
        dest = njs_generate_jump_destination(vm, block->next, "continue",
                                             NJS_GENERATOR_LOOP,
                                             &block->continuation->label,
                                             label);
        if (nxt_slow_path(dest == NULL)) {
            return NXT_ERROR;
        }
    }

    njs_generate_code_jump(generator, jump,
                           offsetof(njs_vmcode_jump_t, offset));

    patch = njs_generate_make_continuation_patch(vm, block, label,
                                         njs_code_offset(generator, jump)
                                         + offsetof(njs_vmcode_jump_t, offset));
    if (nxt_slow_path(patch == NULL)) {
        return NXT_ERROR;
    }

    return NXT_OK;

syntax_error:

    njs_generate_syntax_error(vm, node, "Illegal continue statement");

    return NXT_ERROR;
}


static nxt_int_t
njs_generate_break_statement(njs_vm_t *vm, njs_generator_t *generator,
    njs_parser_node_t *node)
{
    const nxt_str_t        *label, *dest;
    njs_vmcode_jump_t      *jump;
    njs_generator_patch_t  *patch;
    njs_generator_block_t  *block;

    label = &node->label;

    block = njs_generate_find_block(generator->block, NJS_GENERATOR_ALL, label);
    if (nxt_slow_path(block == NULL)) {
        goto syntax_error;
     }

    if (block->type == NJS_GENERATOR_TRY && block->exit != NULL) {
        dest = njs_generate_jump_destination(vm, block->next, "break/return",
                                             NJS_GENERATOR_ALL,
                                             &block->exit->label, label);
        if (nxt_slow_path(dest == NULL)) {
            return NXT_ERROR;
        }
    }

    njs_generate_code_jump(generator, jump,
                           offsetof(njs_vmcode_jump_t, offset));

    patch = njs_generate_make_exit_patch(vm, block, label,
                                         njs_code_offset(generator, jump)
                                         + offsetof(njs_vmcode_jump_t, offset));
    if (nxt_slow_path(patch == NULL)) {
        return NXT_ERROR;
    }

    return NXT_OK;

syntax_error:

    njs_generate_syntax_error(vm, node, "Illegal break statement");

    return NXT_ERROR;
}


static nxt_int_t
njs_generate_statement(njs_vm_t *vm, njs_generator_t *generator,
    njs_parser_node_t *node)
{
    nxt_int_t  ret;

    ret = njs_generate_children(vm, generator, node);

    if (nxt_fast_path(ret == NXT_OK)) {
        return njs_generate_node_index_release(vm, generator, node->right);
    }

    return ret;
}


static nxt_int_t
njs_generate_block_statement(njs_vm_t *vm, njs_generator_t *generator,
    njs_parser_node_t *node)
{
    nxt_int_t  ret;

    ret = njs_generate_start_block(vm, generator, NJS_GENERATOR_BLOCK,
                                   &node->label);
    if (nxt_slow_path(ret != NXT_OK)) {
        return ret;
    }

    ret = njs_generate_statement(vm, generator, node->left);
    if (nxt_slow_path(ret != NXT_OK)) {
        return ret;
    }

    njs_generate_patch_block_exit(vm, generator);

    return ret;
}


static nxt_int_t
njs_generate_children(njs_vm_t *vm, njs_generator_t *generator,
    njs_parser_node_t *node)
{
    nxt_int_t  ret;

    ret = njs_generator(vm, generator, node->left);
    if (nxt_slow_path(ret != NXT_OK)) {
        return ret;
    }

    ret = njs_generate_node_index_release(vm, generator, node->left);
    if (nxt_slow_path(ret != NXT_OK)) {
        return ret;
    }

    return njs_generator(vm, generator, node->right);
}


static nxt_int_t
njs_generate_stop_statement(njs_vm_t *vm, njs_generator_t *generator,
    njs_parser_node_t *node)
{
    nxt_int_t          ret;
    njs_index_t        index;
    njs_vmcode_stop_t  *stop;

    ret = njs_generate_children(vm, generator, node);

    if (nxt_fast_path(ret == NXT_OK)) {
        njs_generate_code(generator, njs_vmcode_stop_t, stop,
                          njs_vmcode_stop, 1, 0);

        index = NJS_INDEX_NONE;
        node = node->right;

        if (node != NULL && node->token != NJS_TOKEN_FUNCTION) {
            index = node->index;
        }

        if (index == NJS_INDEX_NONE) {
            index = njs_value_index(vm, &njs_value_void, generator->runtime);
        }

        stop->retval = index;
    }

    return ret;
}


static nxt_int_t
njs_generate_comma_expression(njs_vm_t *vm, njs_generator_t *generator,
    njs_parser_node_t *node)
{
    nxt_int_t  ret;

    ret = njs_generate_children(vm, generator, node);

    if (nxt_fast_path(ret == NXT_OK)) {
        node->index = node->right->index;
    }

    return ret;
}


static nxt_int_t
njs_generate_assignment(njs_vm_t *vm, njs_generator_t *generator,
    njs_parser_node_t *node)
{
    nxt_int_t              ret;
    njs_index_t            index, src;
    njs_parser_node_t      *lvalue, *expr, *object, *property;
    njs_vmcode_move_t      *move;
    njs_vmcode_prop_set_t  *prop_set;

    lvalue = node->left;
    expr = node->right;
    expr->dest = NULL;

    if (lvalue->token == NJS_TOKEN_NAME) {

        index = njs_variable_index(vm, lvalue);
        if (nxt_slow_path(index == NJS_INDEX_ERROR)) {
            return NXT_ERROR;
        }

        lvalue->index = index;

        expr->dest = lvalue;

        ret = njs_generator(vm, generator, expr);
        if (nxt_slow_path(ret != NXT_OK)) {
            return ret;
        }

        /*
         * lvalue and expression indexes are equal if the expression is an
         * empty object or expression result is stored directly in variable.
         */
        if (lvalue->index != expr->index) {
            njs_generate_code_move(generator, move, lvalue->index, expr->index);
        }

        node->index = expr->index;
        node->temporary = expr->temporary;

        return NXT_OK;
    }

    /* lvalue->token == NJS_TOKEN_PROPERTY */

    /* Object. */

    object = lvalue->left;

    ret = njs_generator(vm, generator, object);
    if (nxt_slow_path(ret != NXT_OK)) {
        return ret;
    }

    /* Property. */

    property = lvalue->right;

    ret = njs_generator(vm, generator, property);
    if (nxt_slow_path(ret != NXT_OK)) {
        return ret;
    }

    if (nxt_slow_path(njs_parser_has_side_effect(expr))) {
        /*
         * Preserve object and property values stored in variables in a case
         * if the variables can be changed by side effects in expression.
         */
        if (object->token == NJS_TOKEN_NAME) {
            src = object->index;

            index = njs_generate_node_temp_index_get(vm, generator, object);
            if (nxt_slow_path(index == NJS_INDEX_ERROR)) {
                return NXT_ERROR;
            }

            njs_generate_code_move(generator, move, index, src);
        }

        if (property->token == NJS_TOKEN_NAME) {
            src = property->index;

            index = njs_generate_node_temp_index_get(vm, generator, property);
            if (nxt_slow_path(index == NJS_INDEX_ERROR)) {
                return NXT_ERROR;
            }

            njs_generate_code_move(generator, move, index, src);
        }
    }

    ret = njs_generator(vm, generator, expr);
    if (nxt_slow_path(ret != NXT_OK)) {
        return ret;
    }

    njs_generate_code(generator, njs_vmcode_prop_set_t, prop_set,
                      njs_vmcode_property_set, 3, 0);
    prop_set->value = expr->index;
    prop_set->object = object->index;
    prop_set->property = property->index;

    node->index = expr->index;
    node->temporary = expr->temporary;

    return njs_generate_children_indexes_release(vm, generator, lvalue);
}


static nxt_int_t
njs_generate_operation_assignment(njs_vm_t *vm, njs_generator_t *generator,
    njs_parser_node_t *node)
{
    nxt_int_t              ret;
    njs_index_t            index;
    njs_parser_node_t      *lvalue, *expr, *object, *property;
    njs_vmcode_move_t      *move;
    njs_vmcode_3addr_t     *code;
    njs_vmcode_prop_get_t  *prop_get;
    njs_vmcode_prop_set_t  *prop_set;

    lvalue = node->left;

    if (lvalue->token == NJS_TOKEN_NAME) {
        ret = njs_generate_variable(vm, generator, lvalue);
        if (nxt_slow_path(ret != NXT_OK)) {
            return ret;
        }

        index = lvalue->index;
        expr = node->right;

        if (nxt_slow_path(njs_parser_has_side_effect(expr))) {
            /* Preserve variable value if it may be changed by expression. */

            njs_generate_code(generator, njs_vmcode_move_t, move,
                              njs_vmcode_move, 2, 1);
            move->src = lvalue->index;

            index = njs_generate_temp_index_get(vm, generator, expr);
            if (nxt_slow_path(index == NJS_INDEX_ERROR)) {
                return NXT_ERROR;
            }

            move->dst = index;
        }

        ret = njs_generator(vm, generator, expr);
        if (nxt_slow_path(ret != NXT_OK)) {
            return ret;
        }

        njs_generate_code(generator, njs_vmcode_3addr_t, code,
                          node->u.operation, 3, 1);
        code->dst = lvalue->index;
        code->src1 = index;
        code->src2 = expr->index;

        node->index = lvalue->index;

        if (lvalue->index != index) {
            ret = njs_generate_index_release(vm, generator, index);
            if (nxt_slow_path(ret != NXT_OK)) {
                return ret;
            }
        }

        return njs_generate_node_index_release(vm, generator, expr);
    }

    /* lvalue->token == NJS_TOKEN_PROPERTY */

    /* Object. */

    object = lvalue->left;

    ret = njs_generator(vm, generator, object);
    if (nxt_slow_path(ret != NXT_OK)) {
        return ret;
    }

    /* Property. */

    property = lvalue->right;

    ret = njs_generator(vm, generator, property);
    if (nxt_slow_path(ret != NXT_OK)) {
        return ret;
    }

    index = njs_generate_node_temp_index_get(vm, generator, node);
    if (nxt_slow_path(index == NJS_INDEX_ERROR)) {
        return NXT_ERROR;
    }

    njs_generate_code(generator, njs_vmcode_prop_get_t, prop_get,
                      njs_vmcode_property_get, 3, 1);
    prop_get->value = index;
    prop_get->object = object->index;
    prop_get->property = property->index;

    expr = node->right;

    ret = njs_generator(vm, generator, expr);
    if (nxt_slow_path(ret != NXT_OK)) {
        return ret;
    }

    njs_generate_code(generator, njs_vmcode_3addr_t, code,
                      node->u.operation, 3, 1);
    code->dst = node->index;
    code->src1 = node->index;
    code->src2 = expr->index;

    njs_generate_code(generator, njs_vmcode_prop_set_t, prop_set,
                      njs_vmcode_property_set, 3, 0);
    prop_set->value = node->index;
    prop_set->object = object->index;
    prop_set->property = property->index;

    ret = njs_generate_children_indexes_release(vm, generator, lvalue);
    if (nxt_slow_path(ret != NXT_OK)) {
        return ret;
    }

    return njs_generate_node_index_release(vm, generator, expr);
}


static nxt_int_t
njs_generate_object(njs_vm_t *vm, njs_generator_t *generator,
    njs_parser_node_t *node)
{
    njs_vmcode_object_t  *object;

    node->index = njs_generate_object_dest_index(vm, generator, node);
    if (nxt_slow_path(node->index == NJS_INDEX_ERROR)) {
        return NXT_ERROR;
    }

    njs_generate_code(generator, njs_vmcode_object_t, object,
                      njs_vmcode_object, 1, 1);
    object->retval = node->index;

    /* Initialize object. */
    return njs_generator(vm, generator, node->left);
}


static nxt_int_t
njs_generate_array(njs_vm_t *vm, njs_generator_t *generator,
    njs_parser_node_t *node)
{
    njs_vmcode_array_t  *array;

    node->index = njs_generate_object_dest_index(vm, generator, node);
    if (nxt_slow_path(node->index == NJS_INDEX_ERROR)) {
        return NXT_ERROR;
    }

    njs_generate_code(generator, njs_vmcode_array_t, array,
                      njs_vmcode_array, 1, 1);
    array->code.ctor = node->ctor;
    array->retval = node->index;
    array->length = node->u.length;

    /* Initialize array. */
    return njs_generator(vm, generator, node->left);
}


static nxt_int_t
njs_generate_function(njs_vm_t *vm, njs_generator_t *generator,
    njs_parser_node_t *node)
{
    nxt_int_t              ret;
    njs_function_lambda_t  *lambda;
    njs_vmcode_function_t  *function;

    lambda = node->u.value.data.u.lambda;

    ret = njs_generate_function_scope(vm, lambda, node, &njs_entry_anonymous);

    if (nxt_slow_path(ret != NXT_OK)) {
        return ret;
    }

    if (vm->debug != NULL) {
        ret = njs_generate_function_debug(vm, NULL, lambda, node);
        if (nxt_slow_path(ret != NXT_OK)) {
            return ret;
        }
    }

    njs_generate_code(generator, njs_vmcode_function_t, function,
                      njs_vmcode_function, 1, 1);
    function->lambda = lambda;

    node->index = njs_generate_object_dest_index(vm, generator, node);
    if (nxt_slow_path(node->index == NJS_INDEX_ERROR)) {
        return NXT_ERROR;
    }

    function->retval = node->index;

    return NXT_OK;
}


static nxt_int_t
njs_generate_regexp(njs_vm_t *vm, njs_generator_t *generator,
    njs_parser_node_t *node)
{
    njs_vmcode_regexp_t  *regexp;

    node->index = njs_generate_object_dest_index(vm, generator, node);
    if (nxt_slow_path(node->index == NJS_INDEX_ERROR)) {
        return NXT_ERROR;
    }

    njs_generate_code(generator, njs_vmcode_regexp_t, regexp,
                      njs_vmcode_regexp, 1, 1);
    regexp->retval = node->index;
    regexp->pattern = node->u.value.data.u.data;

    return NXT_OK;
}


static nxt_int_t
njs_generate_test_jump_expression(njs_vm_t *vm, njs_generator_t *generator,
    njs_parser_node_t *node)
{
    njs_ret_t               jump_offset;
    nxt_int_t               ret;
    njs_vmcode_move_t       *move;
    njs_vmcode_test_jump_t  *test_jump;

    ret = njs_generator(vm, generator, node->left);
    if (nxt_slow_path(ret != NXT_OK)) {
        return ret;
    }

    njs_generate_code(generator, njs_vmcode_test_jump_t, test_jump,
                      node->u.operation, 2, 1);
    jump_offset = njs_code_offset(generator, test_jump);
    test_jump->value = node->left->index;

    node->index = njs_generate_node_temp_index_get(vm, generator, node);
    if (nxt_slow_path(node->index == NJS_INDEX_ERROR)) {
        return node->index;
    }

    test_jump->retval = node->index;

    ret = njs_generator(vm, generator, node->right);
    if (nxt_slow_path(ret != NXT_OK)) {
        return ret;
    }

    /*
     * The right expression usually uses node->index as destination,
     * however, if the expression is a literal, variable or assignment,
     * then a MOVE operation is required.
     */

    if (node->index != node->right->index) {
        njs_generate_code_move(generator, move, node->index,
                               node->right->index);
    }

    njs_code_set_jump_offset(generator, njs_vmcode_test_jump_t, jump_offset);

    return njs_generate_children_indexes_release(vm, generator, node);
}


static nxt_int_t
njs_generate_3addr_operation(njs_vm_t *vm, njs_generator_t *generator,
    njs_parser_node_t *node, nxt_bool_t swap)
{
    nxt_int_t           ret;
    njs_index_t         index;
    njs_parser_node_t   *left, *right;
    njs_vmcode_move_t   *move;
    njs_vmcode_3addr_t  *code;

    left = node->left;

    ret = njs_generator(vm, generator, left);
    if (nxt_slow_path(ret != NXT_OK)) {
        return ret;
    }

    right = node->right;

    if (left->token == NJS_TOKEN_NAME) {

        if (nxt_slow_path(njs_parser_has_side_effect(right))) {
            njs_generate_code(generator, njs_vmcode_move_t, move,
                              njs_vmcode_move, 2, 1);
            move->src = left->index;

            index = njs_generate_node_temp_index_get(vm, generator, left);
            if (nxt_slow_path(index == NJS_INDEX_ERROR)) {
                return NXT_ERROR;
            }

            move->dst = index;
        }
    }

    ret = njs_generator(vm, generator, right);
    if (nxt_slow_path(ret != NXT_OK)) {
        return ret;
    }

    njs_generate_code(generator, njs_vmcode_3addr_t, code,
                      node->u.operation, 3, 1);

    if (!swap) {
        code->src1 = left->index;
        code->src2 = right->index;

    } else {
        code->src1 = right->index;
        code->src2 = left->index;
    }

    /*
     * The temporary index of MOVE destination
     * will be released here as index of node->left.
     */
    node->index = njs_generate_dest_index(vm, generator, node);
    if (nxt_slow_path(node->index == NJS_INDEX_ERROR)) {
        return node->index;
    }

    code->dst = node->index;

    nxt_thread_log_debug("CODE3  %p, %p, %p",
                         code->dst, code->src1, code->src2);

    return NXT_OK;
}


static nxt_int_t
njs_generate_2addr_operation(njs_vm_t *vm, njs_generator_t *generator,
    njs_parser_node_t *node)
{
    nxt_int_t           ret;
    njs_vmcode_2addr_t  *code;

    ret = njs_generator(vm, generator, node->left);
    if (nxt_slow_path(ret != NXT_OK)) {
        return ret;
    }

    njs_generate_code(generator, njs_vmcode_2addr_t, code,
                      node->u.operation, 2, 1);
    code->src = node->left->index;

    node->index = njs_generate_dest_index(vm, generator, node);
    if (nxt_slow_path(node->index == NJS_INDEX_ERROR)) {
        return node->index;
    }

    code->dst = node->index;

    nxt_thread_log_debug("CODE2  %p, %p", code->dst, code->src);

    return NXT_OK;
}


static nxt_int_t
njs_generate_typeof_operation(njs_vm_t *vm, njs_generator_t *generator,
    njs_parser_node_t *node)
{
    nxt_int_t           ret;
    njs_parser_node_t   *expr;
    njs_vmcode_2addr_t  *code;

    expr = node->left;

    if (expr->token == NJS_TOKEN_NAME) {
        expr->index = njs_variable_typeof(vm, expr);

    } else {
        ret = njs_generator(vm, generator, node->left);
        if (nxt_slow_path(ret != NXT_OK)) {
            return ret;
        }
    }

    njs_generate_code(generator, njs_vmcode_2addr_t, code,
                      node->u.operation, 2, 1);
    code->src = node->left->index;

    node->index = njs_generate_dest_index(vm, generator, node);
    if (nxt_slow_path(node->index == NJS_INDEX_ERROR)) {
        return node->index;
    }

    code->dst = node->index;

    nxt_thread_log_debug("CODE2  %p, %p", code->dst, code->src);

    return NXT_OK;
}


static nxt_int_t
njs_generate_inc_dec_operation(njs_vm_t *vm, njs_generator_t *generator,
    njs_parser_node_t *node, nxt_bool_t post)
{
    nxt_int_t              ret;
    njs_index_t            index, dest_index;
    njs_parser_node_t      *lvalue;
    njs_vmcode_3addr_t     *code;
    njs_vmcode_prop_get_t  *prop_get;
    njs_vmcode_prop_set_t  *prop_set;

    lvalue = node->left;

    if (lvalue->token == NJS_TOKEN_NAME) {

        ret = njs_generate_variable(vm, generator, lvalue);
        if (nxt_slow_path(ret != NXT_OK)) {
            return ret;
        }

        index = njs_generate_dest_index(vm, generator, node);
        if (nxt_slow_path(index == NJS_INDEX_ERROR)) {
            return index;
        }

        node->index = index;

        njs_generate_code(generator, njs_vmcode_3addr_t, code,
                          node->u.operation, 3, 1);
        code->dst = index;
        code->src1 = lvalue->index;
        code->src2 = lvalue->index;

        return NXT_OK;
    }

    /* lvalue->token == NJS_TOKEN_PROPERTY */

    /* Object. */

    ret = njs_generator(vm, generator, lvalue->left);
    if (nxt_slow_path(ret != NXT_OK)) {
        return ret;
    }

    /* Property. */

    ret = njs_generator(vm, generator, lvalue->right);
    if (nxt_slow_path(ret != NXT_OK)) {
        return ret;
    }

    if (node->dest != NULL) {
        dest_index = node->dest->index;

        if (dest_index != NJS_INDEX_NONE
            && dest_index != lvalue->left->index
            && dest_index != lvalue->right->index)
        {
            node->index = dest_index;
            goto found;
        }
    }

    dest_index = njs_generate_node_temp_index_get(vm, generator, node);

found:

    index = post ? njs_generate_temp_index_get(vm, generator, node)
                 : dest_index;

    if (nxt_slow_path(index == NJS_INDEX_ERROR)) {
        return NXT_ERROR;
    }

    njs_generate_code(generator, njs_vmcode_prop_get_t, prop_get,
                      njs_vmcode_property_get, 3, 1);
    prop_get->value = index;
    prop_get->object = lvalue->left->index;
    prop_get->property = lvalue->right->index;

    njs_generate_code(generator, njs_vmcode_3addr_t, code,
                      node->u.operation, 3, 1);
    code->dst = dest_index;
    code->src1 = index;
    code->src2 = index;

    njs_generate_code(generator, njs_vmcode_prop_set_t, prop_set,
                      njs_vmcode_property_set, 3, 0);
    prop_set->value = index;
    prop_set->object = lvalue->left->index;
    prop_set->property = lvalue->right->index;

    if (post) {
        ret = njs_generate_index_release(vm, generator, index);
        if (nxt_slow_path(ret != NXT_OK)) {
            return ret;
        }
    }

    return njs_generate_children_indexes_release(vm, generator, lvalue);
}


static nxt_int_t
njs_generate_function_declaration(njs_vm_t *vm, njs_generator_t *generator,
    njs_parser_node_t *node)
{
    nxt_int_t              ret;
    njs_variable_t         *var;
    njs_function_lambda_t  *lambda;

    var = njs_variable_resolve(vm, node);
    if (nxt_slow_path(var == NULL)) {
        return NXT_ERROR;
    }

    if (!njs_is_function(&var->value)) {
        /* A variable was declared with the same name. */
        return NXT_OK;
    }

    lambda = var->value.data.u.function->u.lambda;

    ret = njs_generate_function_scope(vm, lambda, node,
                                      &node->u.reference.name);
    if (nxt_slow_path(ret != NXT_OK)) {
        return ret;
    }

    if (vm->debug != NULL) {
        ret = njs_generate_function_debug(vm, &var->name, lambda, node);
    }

    return ret;
}


static nxt_int_t
njs_generate_function_scope(njs_vm_t *vm, njs_function_lambda_t *lambda,
    njs_parser_node_t *node, const nxt_str_t *name)
{
    size_t           size;
    nxt_int_t        ret;
    nxt_array_t      *closure;
    njs_generator_t  generator;

    node = node->right;

    nxt_memzero(&generator, sizeof(njs_generator_t));

    ret = njs_generate_scope(vm, &generator, node->scope, name);

    if (nxt_fast_path(ret == NXT_OK)) {
        size = 0;
        closure = node->scope->values[1];

        if (closure != NULL) {
            lambda->block_closures = 1;
            lambda->closure_scope = closure->start;
            size = (1 + closure->items) * sizeof(njs_value_t);
        }

        lambda->closure_size = size;

        lambda->nesting = node->scope->nesting;
        lambda->arguments_object = node->scope->arguments_object;

        lambda->start = generator.code_start;
        lambda->local_size = generator.scope_size;
        lambda->local_scope = generator.local_scope;
    }

    return ret;
}


nxt_int_t
njs_generate_scope(njs_vm_t *vm, njs_generator_t *generator,
    njs_parser_scope_t *scope, const nxt_str_t *name)
{
    u_char         *p;
    size_t          size;
    uintptr_t       scope_size;
    nxt_int_t       ret;
    nxt_uint_t      n;
    njs_value_t    *value;
    njs_vm_code_t  *code;

    generator->code_size = 128;

    p = nxt_mp_alloc(vm->mem_pool, generator->code_size);
    if (nxt_slow_path(p == NULL)) {
        return NXT_ERROR;
    }

    generator->code_start = p;
    generator->code_end = p;

    ret = njs_generate_argument_closures(vm, generator, scope->top);
    if (nxt_slow_path(ret != NXT_OK)) {
        return NXT_ERROR;
    }

    if (nxt_slow_path(njs_generator(vm, generator, scope->top) != NXT_OK)) {
        return NXT_ERROR;
    }

    generator->code_size = generator->code_end - generator->code_start;

    scope_size = njs_scope_offset(scope->next_index[0]);

    if (scope->type == NJS_SCOPE_GLOBAL) {
        scope_size -= NJS_INDEX_GLOBAL_OFFSET;
    }

    generator->local_scope = nxt_mp_alloc(vm->mem_pool, scope_size);
    if (nxt_slow_path(generator->local_scope == NULL)) {
        return NXT_ERROR;
    }

    generator->scope_size = scope_size;

    size = scope->values[0]->items * sizeof(njs_value_t);

    nxt_thread_log_debug("SCOPE SIZE: %uz %uz", size, scope_size);

    p = memcpy(generator->local_scope, scope->values[0]->start, size);
    value = (njs_value_t *) (p + size);

    for (n = scope_size - size; n != 0; n -= sizeof(njs_value_t)) {
        *value++ = njs_value_void;
    }

    if (vm->code == NULL) {
        vm->code = nxt_array_create(4, sizeof(njs_vm_code_t),
                                    &njs_array_mem_proto, vm->mem_pool);
        if (nxt_slow_path(vm->code == NULL)) {
            return NXT_ERROR;
        }
    }

    code = nxt_array_add(vm->code, &njs_array_mem_proto, vm->mem_pool);
    if (nxt_slow_path(code == NULL)) {
        return NXT_ERROR;
    }

    code->start = generator->code_start;
    code->end = generator->code_end;
    code->file = scope->file;
    code->name = *name;

    return NXT_OK;
}


static nxt_int_t
njs_generate_argument_closures(njs_vm_t *vm, njs_generator_t *generator,
    njs_parser_node_t *node)
{
    nxt_uint_t         n;
    njs_index_t        index;
    njs_variable_t     *var;
    njs_vmcode_move_t  *move;
    nxt_lvlhsh_each_t  lhe;

    n = node->scope->argument_closures;

    if (n == 0) {
        return NXT_OK;
    }

    nxt_lvlhsh_each_init(&lhe, &njs_variables_hash_proto);

    do {
        var = nxt_lvlhsh_each(&node->scope->variables, &lhe);

        if (var->argument != 0) {
            index = njs_scope_index((var->argument - 1), NJS_SCOPE_ARGUMENTS);

            njs_generate_code_move(generator, move, var->index, index);

            n--;
        }

    } while(n != 0);

    return NXT_OK;
}


static nxt_int_t
njs_generate_return_statement(njs_vm_t *vm, njs_generator_t *generator,
    njs_parser_node_t *node)
{
    nxt_int_t                ret;
    njs_index_t              index;
    const nxt_str_t          *dest;
    njs_vmcode_return_t      *code;
    njs_generator_patch_t    *patch;
    njs_generator_block_t    *block, *immediate, *top;
    njs_vmcode_try_return_t  *try_return;

    ret = njs_generator(vm, generator, node->right);

    if (nxt_slow_path(ret != NXT_OK)) {
        return ret;
    }

    if (node->right != NULL) {
        index = node->right->index;

    } else {
        index = njs_value_index(vm, &njs_value_void, generator->runtime);
    }

    immediate = njs_generate_lookup_block(generator->block, NJS_GENERATOR_TRY,
                                          &no_label);

    if (nxt_fast_path(immediate == NULL)) {
        njs_generate_code(generator, njs_vmcode_return_t, code,
                          njs_vmcode_return, 1, 0);
        code->retval = index;
        node->index = index;

        return NXT_OK;
    }

    if (immediate->type == NJS_GENERATOR_TRY && immediate->exit != NULL) {
        dest = njs_generate_jump_destination(vm, immediate->next,
                                             "break/return",
                                             NJS_GENERATOR_ALL,
                                             &immediate->exit->label,
                                             &return_label);
        if (nxt_slow_path(dest == NULL)) {
            return NXT_ERROR;
        }
    }

    top = immediate;
    block = immediate->next;

    while (block != NULL) {
        if (block->type & NJS_GENERATOR_TRY) {
            top = block;
        }

        block = block->next;
    }

    njs_generate_code(generator, njs_vmcode_try_return_t, try_return,
                      njs_vmcode_try_return, 2, 1);
    try_return->retval = index;
    try_return->save = top->index;
    try_return->offset = offsetof(njs_vmcode_try_return_t, offset);

    patch = njs_generate_make_exit_patch(vm, immediate, &return_label,
                                         njs_code_offset(generator, try_return)
                                         + offsetof(njs_vmcode_try_return_t,
                                                    offset));
    if (nxt_slow_path(patch == NULL)) {
        return NXT_ERROR;
    }

    return NXT_OK;
}


static nxt_int_t
njs_generate_function_call(njs_vm_t *vm, njs_generator_t *generator,
    njs_parser_node_t *node)
{
    njs_ret_t                    func_offset;
    njs_ret_t                    ret;
    njs_parser_node_t            *name;
    njs_vmcode_function_frame_t  *func;

    if (node->left != NULL) {
        /* Generate function code in function expression. */
        ret = njs_generator(vm, generator, node->left);
        if (nxt_slow_path(ret != NXT_OK)) {
            return ret;
        }

        name = node->left;

    } else {
        ret = njs_generate_variable(vm, generator, node);
        if (nxt_slow_path(ret != NXT_OK)) {
            return ret;
        }
        name = node;
    }

    njs_generate_code(generator, njs_vmcode_function_frame_t, func,
                      njs_vmcode_function_frame, 2, 0);
    func_offset = njs_code_offset(generator, func);
    func->code.ctor = node->ctor;
    func->name = name->index;

    ret = njs_generate_call(vm, generator, node);

    if (nxt_fast_path(ret >= 0)) {
        func = njs_code_ptr(generator, njs_vmcode_function_frame_t,
                            func_offset);
        func->nargs = ret;
        return NXT_OK;
    }

    return ret;
}


static nxt_int_t
njs_generate_method_call(njs_vm_t *vm, njs_generator_t *generator,
    njs_parser_node_t *node)
{
    njs_ret_t                  method_offset;
    nxt_int_t                  ret;
    njs_parser_node_t          *prop;
    njs_vmcode_method_frame_t  *method;

    prop = node->left;

    /* Object. */

    ret = njs_generator(vm, generator, prop->left);
    if (nxt_slow_path(ret != NXT_OK)) {
        return ret;
    }

    /* Method name. */

    ret = njs_generator(vm, generator, prop->right);
    if (nxt_slow_path(ret != NXT_OK)) {
        return ret;
    }

    njs_generate_code(generator, njs_vmcode_method_frame_t, method,
                      njs_vmcode_method_frame, 3, 0);
    method_offset = njs_code_offset(generator, method);
    method->code.ctor = node->ctor;
    method->object = prop->left->index;
    method->method = prop->right->index;

    ret = njs_generate_children_indexes_release(vm, generator, prop);
    if (nxt_slow_path(ret != NXT_OK)) {
        return ret;
    }

    ret = njs_generate_call(vm, generator, node);

    if (nxt_fast_path(ret >= 0)) {
        method = njs_code_ptr(generator, njs_vmcode_method_frame_t,
                              method_offset);
        method->nargs = ret;
        return NXT_OK;
    }

    return ret;
}


static nxt_noinline nxt_int_t
njs_generate_call(njs_vm_t *vm, njs_generator_t *generator,
                  njs_parser_node_t *node)
{
    nxt_int_t                   ret;
    nxt_uint_t                  nargs;
    njs_index_t                 retval;
    njs_parser_node_t           *arg;
    njs_vmcode_move_t           *move;
    njs_vmcode_function_call_t  *call;

    nargs = 0;

    for (arg = node->right; arg != NULL; arg = arg->right) {
        nargs++;

        ret = njs_generator(vm, generator, arg->left);
        if (nxt_slow_path(ret != NXT_OK)) {
            return ret;
        }

        if (arg->index != arg->left->index) {
            njs_generate_code_move(generator, move, arg->index,
                                   arg->left->index);
        }
    }

    retval = njs_generate_dest_index(vm, generator, node);
    if (nxt_slow_path(retval == NJS_INDEX_ERROR)) {
        return retval;
    }

    node->index = retval;

    njs_generate_code(generator, njs_vmcode_function_call_t, call,
                      njs_vmcode_function_call, 1, 0);
    call->retval = retval;

    return nargs;
}


#define njs_generate_code_catch(generator, _code, _exception)                 \
    do {                                                                      \
            njs_generate_code(generator, njs_vmcode_catch_t, _code,           \
                              njs_vmcode_catch, 2, 0);                        \
            _code->offset = sizeof(njs_vmcode_catch_t);                       \
            _code->exception = _exception;                                    \
    } while (0)


#define njs_generate_code_finally(generator, _code, _retval, _exit)           \
    do {                                                                      \
            njs_generate_code(generator, njs_vmcode_finally_t, _code,         \
                              njs_vmcode_finally, 2, 0);                      \
            _code->retval = _retval;                                          \
            _code->exit_value = _exit;                                        \
            _code->continue_offset = offsetof(njs_vmcode_finally_t,           \
                                              continue_offset);               \
            _code->break_offset = offsetof(njs_vmcode_finally_t,              \
                                           break_offset);                     \
    } while (0)


static nxt_int_t
njs_generate_try_statement(njs_vm_t *vm, njs_generator_t *generator,
    njs_parser_node_t *node)
{
    njs_ret_t                    try_offset, try_end_offset, catch_offset,
                                 catch_end_offset;
    nxt_int_t                    ret;
    njs_index_t                  exception_index, exit_index, catch_index;
    nxt_str_t                    try_cont_label, try_exit_label,
                                 catch_cont_label, catch_exit_label;
    const nxt_str_t              *dest_label;
    njs_vmcode_catch_t           *catch;
    njs_vmcode_finally_t         *finally;
    njs_vmcode_try_end_t         *try_end, *catch_end;
    njs_generator_patch_t        *patch;
    njs_generator_block_t        *block, *try_block, *catch_block;
    njs_vmcode_try_start_t       *try_start;
    njs_vmcode_try_trampoline_t  *try_break, *try_continue;

    njs_generate_code(generator, njs_vmcode_try_start_t, try_start,
                      njs_vmcode_try_start, 2, 0);
    try_offset = njs_code_offset(generator, try_start);

    exception_index = njs_generate_temp_index_get(vm, generator, node);
    if (nxt_slow_path(exception_index == NJS_INDEX_ERROR)) {
        return NXT_ERROR;
    }

    try_start->exception_value = exception_index;

    /*
     * exit_value is used in njs_vmcode_finally to make a decision
     * which way to go after "break", "continue" and "return" instruction
     * inside "try" or "catch" blocks.
     */

    exit_index = njs_generate_temp_index_get(vm, generator, node);
    if (nxt_slow_path(exit_index == NJS_INDEX_ERROR)) {
        return NXT_ERROR;
    }

    try_start->exit_value = exit_index;

    ret = njs_generate_start_block(vm, generator, NJS_GENERATOR_TRY, &no_label);
    if (nxt_slow_path(ret != NXT_OK)) {
        return ret;
    }

    try_block = generator->block;
    try_block->index = exit_index;

    ret = njs_generator(vm, generator, node->left);
    if (nxt_slow_path(ret != NXT_OK)) {
        return ret;
    }

    try_exit_label = undef_label;
    try_cont_label = undef_label;

    njs_generate_code(generator, njs_vmcode_try_end_t, try_end,
                      njs_vmcode_try_end, 0, 0);
    try_end_offset = njs_code_offset(generator, try_end);

    if (try_block->exit != NULL) {
        try_exit_label = try_block->exit->label;

        njs_generate_patch_block(vm, generator, try_block->exit);

        njs_generate_code(generator, njs_vmcode_try_trampoline_t, try_break,
                          njs_vmcode_try_break, 2, 0);
        try_break->exit_value = exit_index;

        try_break->offset = -sizeof(njs_vmcode_try_end_t);

    } else {
        try_break = NULL;
    }

    if (try_block->continuation != NULL) {
        try_cont_label = try_block->continuation->label;

        njs_generate_patch_block(vm, generator, try_block->continuation);

        njs_generate_code(generator, njs_vmcode_try_trampoline_t, try_continue,
                          njs_vmcode_try_continue, 2, 0);
        try_continue->exit_value = exit_index;

        try_continue->offset = -sizeof(njs_vmcode_try_end_t);

        if (try_break != NULL) {
            try_continue->offset -= sizeof(njs_vmcode_try_trampoline_t);
        }
    }

    generator->block = try_block->next;

    njs_code_set_jump_offset(generator, njs_vmcode_try_start_t, try_offset);
    try_offset = try_end_offset;

    node = node->right;

    catch_exit_label = undef_label;
    catch_cont_label = undef_label;

    if (node->token == NJS_TOKEN_CATCH) {
        /* A "try/catch" case. */

        catch_index = njs_variable_index(vm, node->left);
        if (nxt_slow_path(catch_index == NJS_INDEX_ERROR)) {
            return NXT_ERROR;
        }

        njs_generate_code_catch(generator, catch, catch_index);

        ret = njs_generator(vm, generator, node->right);
        if (nxt_slow_path(ret != NXT_OK)) {
            return ret;
        }

        njs_code_set_jump_offset(generator, njs_vmcode_try_end_t, try_offset);

        if (try_block->continuation != NULL || try_block->exit != NULL) {
            njs_generate_code_finally(generator, finally, exception_index,
                                      exit_index);

            if (try_block->continuation != NULL) {
                /*
                 * block != NULL is checked
                 * by njs_generate_continue_statement()
                 */
                block = njs_generate_find_block(generator->block,
                                                NJS_GENERATOR_LOOP,
                                                &try_cont_label);

                patch = njs_generate_make_continuation_patch(vm, block,
                                                             &try_cont_label,
                            njs_code_offset(generator, finally)
                             + offsetof(njs_vmcode_finally_t, continue_offset));
                if (nxt_slow_path(patch == NULL)) {
                    return NXT_ERROR;
                }
            }

            if (try_block->exit != NULL) {
                block = njs_generate_find_block(generator->block,
                                                NJS_GENERATOR_ALL,
                                                &try_exit_label);

                if (block != NULL) {
                    patch = njs_generate_make_exit_patch(vm, block,
                                                         &try_exit_label,
                                njs_code_offset(generator, finally)
                                + offsetof(njs_vmcode_finally_t, break_offset));
                    if (nxt_slow_path(patch == NULL)) {
                        return NXT_ERROR;
                    }
                }
            }
        }

        /* TODO: release exception variable index. */

    } else {
        if (node->left != NULL) {
            /* A try/catch/finally case. */

            catch_index = njs_variable_index(vm, node->left->left);
            if (nxt_slow_path(catch_index == NJS_INDEX_ERROR)) {
                return NXT_ERROR;
            }

            njs_generate_code_catch(generator, catch, catch_index);
            catch_offset = njs_code_offset(generator, catch);

            ret = njs_generate_start_block(vm, generator, NJS_GENERATOR_TRY,
                                           &no_label);
            if (nxt_slow_path(ret != NXT_OK)) {
                return ret;
            }

            catch_block = generator->block;
            catch_block->index = exit_index;

            ret = njs_generator(vm, generator, node->left->right);
            if (nxt_slow_path(ret != NXT_OK)) {
                return ret;
            }

            njs_generate_code(generator, njs_vmcode_try_end_t, catch_end,
                              njs_vmcode_try_end, 0, 0);
            catch_end_offset = njs_code_offset(generator, catch_end);

            if (catch_block->exit != NULL) {
                catch_exit_label = catch_block->exit->label;

                njs_generate_patch_block(vm, generator, catch_block->exit);

                njs_generate_code(generator, njs_vmcode_try_trampoline_t,
                                  try_break, njs_vmcode_try_break, 2, 0);

                try_break->exit_value = exit_index;

                try_break->offset = -sizeof(njs_vmcode_try_end_t);

            } else {
                try_break = NULL;
            }

            if (catch_block->continuation != NULL) {
                catch_cont_label = catch_block->continuation->label;

                njs_generate_patch_block(vm, generator,
                                         catch_block->continuation);

                njs_generate_code(generator, njs_vmcode_try_trampoline_t,
                                  try_continue, njs_vmcode_try_continue, 2, 0);

                try_continue->exit_value = exit_index;

                try_continue->offset = -sizeof(njs_vmcode_try_end_t);

                if (try_break != NULL) {
                    try_continue->offset -= sizeof(njs_vmcode_try_trampoline_t);
                }
            }

            generator->block = catch_block->next;

            njs_code_set_jump_offset(generator, njs_vmcode_catch_t,
                                     catch_offset);

            /* TODO: release exception variable index. */

            njs_generate_code_catch(generator, catch, exception_index);

            njs_code_set_jump_offset(generator, njs_vmcode_try_end_t,
                                     catch_end_offset);

        } else {
            /* A try/finally case. */

            njs_generate_code_catch(generator, catch, exception_index);

            catch_block = NULL;
        }

        njs_code_set_jump_offset(generator, njs_vmcode_try_end_t, try_offset);

        ret = njs_generator(vm, generator, node->right);
        if (nxt_slow_path(ret != NXT_OK)) {
            return ret;
        }

        njs_generate_code_finally(generator, finally, exception_index,
                          exit_index);

        if (try_block->continuation != NULL
            || (catch_block && catch_block->continuation != NULL))
        {
            dest_label = njs_generate_jump_destination(vm, generator->block,
                                                       "try continue",
                                                       NJS_GENERATOR_LOOP,
                                                       &try_cont_label,
                                                       &catch_cont_label);
            if (nxt_slow_path(dest_label == NULL)) {
                return NXT_ERROR;
            }

            /*
             * block != NULL is checked
             * by njs_generate_continue_statement()
             */
            block = njs_generate_find_block(generator->block,
                                            NJS_GENERATOR_LOOP, dest_label);

            patch = njs_generate_make_continuation_patch(vm, block, dest_label,
                             njs_code_offset(generator, finally)
                             + offsetof(njs_vmcode_finally_t, continue_offset));
            if (nxt_slow_path(patch == NULL)) {
                return NXT_ERROR;
            }
        }

        if (try_block->exit != NULL
            || (catch_block != NULL && catch_block->exit != NULL))
        {
            dest_label = njs_generate_jump_destination(vm, generator->block,
                                                       "try break/return",
                                                       NJS_GENERATOR_ALL
                                                       | NJS_GENERATOR_TRY,
                                                       &try_exit_label,
                                                       &catch_exit_label);
            if (nxt_slow_path(dest_label == NULL)) {
                return NXT_ERROR;
            }

            /*
             * block can be NULL for "return" instruction in
             * outermost try-catch block.
             */
            block = njs_generate_find_block(generator->block,
                                            NJS_GENERATOR_ALL
                                            | NJS_GENERATOR_TRY, dest_label);
            if (block != NULL) {
                patch = njs_generate_make_exit_patch(vm, block, dest_label,
                                njs_code_offset(generator, finally)
                                + offsetof(njs_vmcode_finally_t, break_offset));
                if (nxt_slow_path(patch == NULL)) {
                    return NXT_ERROR;
                }
            }
        }
    }

    return njs_generate_index_release(vm, generator, exception_index);
}


static nxt_int_t
njs_generate_throw_statement(njs_vm_t *vm, njs_generator_t *generator,
    njs_parser_node_t *node)
{
    nxt_int_t           ret;
    njs_vmcode_throw_t  *throw;

    ret = njs_generator(vm, generator, node->right);

    if (nxt_fast_path(ret == NXT_OK)) {
        njs_generate_code(generator, njs_vmcode_throw_t, throw,
                          njs_vmcode_throw, 1, 0);

        node->index = node->right->index;
        throw->retval = node->index;
    }

    return ret;
}


static nxt_noinline njs_index_t
njs_generate_dest_index(njs_vm_t *vm, njs_generator_t *generator,
    njs_parser_node_t *node)
{
    njs_index_t        ret;
    njs_parser_node_t  *dest;

    ret = njs_generate_children_indexes_release(vm, generator, node);
    if (nxt_slow_path(ret != NXT_OK)) {
        return ret;
    }

    dest = node->dest;

    if (dest != NULL && dest->index != NJS_INDEX_NONE) {
        return dest->index;
    }

    return njs_generate_node_temp_index_get(vm, generator, node);
}


static nxt_noinline njs_index_t
njs_generate_object_dest_index(njs_vm_t *vm, njs_generator_t *generator,
    njs_parser_node_t *node)
{
    njs_index_t        index;
    njs_parser_node_t  *dest;

    dest = node->dest;

    if (dest != NULL && dest->index != NJS_INDEX_NONE) {
        index = dest->index;

        if (njs_is_callee_argument_index(index)) {
            /* Assgin object directly to a callee argument. */
            return index;
        }

        if (node->left == NULL) {
            /* Assign empty object directly to variable */
            return index;
        }
    }

    return njs_generate_node_temp_index_get(vm, generator, node);
}


static njs_index_t
njs_generate_node_temp_index_get(njs_vm_t *vm, njs_generator_t *generator,
    njs_parser_node_t *node)
{
    node->temporary = 1;

    node->index = njs_generate_temp_index_get(vm, generator, node);

    return node->index;
}


static nxt_noinline njs_index_t
njs_generate_temp_index_get(njs_vm_t *vm, njs_generator_t *generator,
    njs_parser_node_t *node)
{
    nxt_array_t         *cache;
    njs_index_t         *last;
    njs_parser_scope_t  *scope;

    cache = generator->index_cache;

    if (cache != NULL && cache->items != 0) {
        last = nxt_array_remove_last(cache);

        nxt_thread_log_debug("CACHE %p", *last);

        return *last;
    }

    scope = node->scope;

    while (scope->type == NJS_SCOPE_BLOCK) {
         scope = scope->parent;
    }

    return njs_scope_next_index(vm, scope, NJS_SCOPE_INDEX_LOCAL,
                                &njs_value_invalid);
}


static nxt_noinline nxt_int_t
njs_generate_children_indexes_release(njs_vm_t *vm, njs_generator_t *generator,
    njs_parser_node_t *node)
{
    nxt_int_t  ret;

    ret = njs_generate_node_index_release(vm, generator, node->left);

    if (nxt_fast_path(ret == NXT_OK)) {
        return njs_generate_node_index_release(vm, generator, node->right);
    }

    return ret;
}


static nxt_noinline nxt_int_t
njs_generate_node_index_release(njs_vm_t *vm, njs_generator_t *generator,
    njs_parser_node_t *node)
{
    if (node != NULL && node->temporary) {
        return njs_generate_index_release(vm, generator, node->index);
    }

    return NXT_OK;
}


static nxt_noinline nxt_int_t
njs_generate_index_release(njs_vm_t *vm, njs_generator_t *generator,
    njs_index_t index)
{
    njs_index_t  *last;
    nxt_array_t  *cache;

    nxt_thread_log_debug("RELEASE %p", index);

    cache = generator->index_cache;

    if (cache == NULL) {
        cache = nxt_array_create(4, sizeof(njs_value_t *),
                                 &njs_array_mem_proto, vm->mem_pool);
        if (nxt_slow_path(cache == NULL)) {
            return NXT_ERROR;
        }

        generator->index_cache = cache;
    }

    last = nxt_array_add(cache, &njs_array_mem_proto, vm->mem_pool);
    if (nxt_fast_path(last != NULL)) {
        *last = index;
        return NXT_OK;
    }

    return NXT_ERROR;
}


static nxt_int_t
njs_generate_function_debug(njs_vm_t *vm, nxt_str_t *name,
    njs_function_lambda_t *lambda, njs_parser_node_t *node)
{
    njs_function_debug_t  *debug;

    debug = nxt_array_add(vm->debug, &njs_array_mem_proto, vm->mem_pool);
    if (nxt_slow_path(debug == NULL)) {
        return NXT_ERROR;
    }

    if (name != NULL) {
        debug->name = *name;

    } else {
        debug->name = no_label;
    }

    debug->lambda = lambda;
    debug->line = node->token_line;
    debug->file = node->scope->file;

    return NXT_OK;
}
