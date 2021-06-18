
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) Dmitry Volyntsev
 * Copyright (C) Alexander Borisov
 * Copyright (C) NGINX, Inc.
 */


#include <njs_main.h>


typedef struct njs_generator_patch_s   njs_generator_patch_t;

struct njs_generator_patch_s {
    /*
     * The jump_offset field points to jump offset field which contains a small
     * adjustment and the adjustment should be added as (njs_jump_off_t *)
     * because pointer to u_char accesses only one byte so this does not
     * work on big endian platforms.
     */
    njs_jump_off_t                  jump_offset;
    njs_generator_patch_t           *next;

    njs_str_t                       label;
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
    njs_str_t                       label;

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


static njs_int_t njs_generator(njs_vm_t *vm, njs_generator_t *generator,
    njs_parser_node_t *node);
static u_char *njs_generate_reserve(njs_vm_t *vm, njs_generator_t *generator,
    size_t size);
static njs_int_t njs_generate_code_map(njs_vm_t *vm, njs_generator_t *generator,
    njs_parser_node_t *node, u_char *code);
static njs_int_t njs_generate_name(njs_vm_t *vm, njs_generator_t *generator,
    njs_parser_node_t *node);
static njs_int_t njs_generate_variable(njs_vm_t *vm, njs_generator_t *generator,
    njs_parser_node_t *node, njs_reference_type_t type,
    njs_variable_t **retvar);
static njs_int_t njs_generate_var_statement(njs_vm_t *vm,
    njs_generator_t *generator, njs_parser_node_t *node);
static njs_int_t njs_generate_let(njs_vm_t *vm, njs_generator_t *generator,
    njs_parser_node_t *node, njs_variable_t *var);
static njs_int_t njs_generate_if_statement(njs_vm_t *vm,
    njs_generator_t *generator, njs_parser_node_t *node);
static njs_int_t njs_generate_cond_expression(njs_vm_t *vm,
    njs_generator_t *generator, njs_parser_node_t *node);
static njs_int_t njs_generate_switch_statement(njs_vm_t *vm,
    njs_generator_t *generator, njs_parser_node_t *node);
static njs_int_t njs_generate_while_statement(njs_vm_t *vm,
    njs_generator_t *generator, njs_parser_node_t *node);
static njs_int_t njs_generate_do_while_statement(njs_vm_t *vm,
    njs_generator_t *generator, njs_parser_node_t *node);
static njs_int_t njs_generate_for_statement(njs_vm_t *vm,
    njs_generator_t *generator, njs_parser_node_t *node);
static njs_int_t njs_generate_for_let_update(njs_vm_t *vm,
    njs_generator_t *generator, njs_parser_node_t *node, size_t depth);
static njs_int_t njs_generate_for_resolve_closure(njs_vm_t *vm,
    njs_parser_node_t *node, size_t depth);
static njs_int_t njs_generate_for_in_statement(njs_vm_t *vm,
    njs_generator_t *generator, njs_parser_node_t *node);
static njs_int_t njs_generate_start_block(njs_vm_t *vm,
    njs_generator_t *generator, njs_generator_block_type_t type,
    const njs_str_t *label);
static njs_generator_block_t *njs_generate_lookup_block(
    njs_generator_block_t *block, uint32_t mask, const njs_str_t *label);
static njs_generator_block_t *njs_generate_find_block(
    njs_generator_block_t *block, uint32_t mask, const njs_str_t *label);
static void njs_generate_patch_block(njs_vm_t *vm, njs_generator_t *generator,
    njs_generator_patch_t *list);
static njs_generator_patch_t *njs_generate_make_continuation_patch(njs_vm_t *vm,
    njs_generator_block_t *block, const njs_str_t *label,
    njs_jump_off_t offset);
static njs_generator_patch_t *njs_generate_make_exit_patch(njs_vm_t *vm,
    njs_generator_block_t *block, const njs_str_t *label,
    njs_jump_off_t offset);
static void njs_generate_patch_block_exit(njs_vm_t *vm,
    njs_generator_t *generator);
static const njs_str_t *njs_generate_jump_destination(njs_vm_t *vm,
    njs_generator_block_t *block, const char *inst_type, uint32_t mask,
    const njs_str_t *label1, const njs_str_t *label2);
static njs_int_t njs_generate_continue_statement(njs_vm_t *vm,
    njs_generator_t *generator, njs_parser_node_t *node);
static njs_int_t njs_generate_break_statement(njs_vm_t *vm,
    njs_generator_t *generator, njs_parser_node_t *node);
static njs_int_t njs_generate_debugger_statement(njs_vm_t *vm,
    njs_generator_t *generator, njs_parser_node_t *node);
static njs_int_t njs_generate_statement(njs_vm_t *vm,
    njs_generator_t *generator, njs_parser_node_t *node);
static njs_int_t njs_generate_block_statement(njs_vm_t *vm,
    njs_generator_t *generator, njs_parser_node_t *node);
static njs_int_t njs_generate_children(njs_vm_t *vm, njs_generator_t *generator,
    njs_parser_node_t *node);
static njs_int_t njs_generate_stop_statement(njs_vm_t *vm,
    njs_generator_t *generator, njs_parser_node_t *node);
static njs_int_t njs_generate_comma_expression(njs_vm_t *vm,
    njs_generator_t *generator, njs_parser_node_t *node);
static njs_int_t njs_generate_assignment(njs_vm_t *vm,
    njs_generator_t *generator, njs_parser_node_t *node);
static njs_int_t njs_generate_operation_assignment(njs_vm_t *vm,
    njs_generator_t *generator, njs_parser_node_t *node);
static njs_int_t njs_generate_object(njs_vm_t *vm, njs_generator_t *generator,
    njs_parser_node_t *node);
static njs_int_t njs_generate_property_accessor(njs_vm_t *vm,
    njs_generator_t *generator, njs_parser_node_t *node);
static njs_int_t njs_generate_array(njs_vm_t *vm, njs_generator_t *generator,
    njs_parser_node_t *node);
static njs_int_t njs_generate_function_expression(njs_vm_t *vm,
    njs_generator_t *generator, njs_parser_node_t *node);
static njs_int_t njs_generate_function(njs_vm_t *vm, njs_generator_t *generator,
    njs_parser_node_t *node);
static njs_int_t njs_generate_regexp(njs_vm_t *vm, njs_generator_t *generator,
    njs_parser_node_t *node);
static njs_int_t njs_generate_template_literal(njs_vm_t *vm,
    njs_generator_t *generator, njs_parser_node_t *node);
static njs_int_t njs_generate_test_jump_expression(njs_vm_t *vm,
    njs_generator_t *generator, njs_parser_node_t *node);
static njs_int_t njs_generate_3addr_operation(njs_vm_t *vm,
    njs_generator_t *generator, njs_parser_node_t *node, njs_bool_t swap);
static njs_int_t njs_generate_2addr_operation(njs_vm_t *vm,
    njs_generator_t *generator, njs_parser_node_t *node);
static njs_int_t njs_generate_typeof_operation(njs_vm_t *vm,
    njs_generator_t *generator, njs_parser_node_t *node);
static njs_int_t njs_generate_inc_dec_operation(njs_vm_t *vm,
    njs_generator_t *generator, njs_parser_node_t *node, njs_bool_t post);
static njs_int_t njs_generate_function_declaration(njs_vm_t *vm,
    njs_generator_t *generator, njs_parser_node_t *node);
static njs_int_t njs_generate_function_scope(njs_vm_t *vm,
    njs_function_lambda_t *lambda, njs_parser_node_t *node,
    const njs_str_t *name);
static int64_t njs_generate_lambda_variables(njs_vm_t *vm,
    njs_generator_t *generator, njs_parser_node_t *node);
static njs_int_t njs_generate_return_statement(njs_vm_t *vm,
    njs_generator_t *generator, njs_parser_node_t *node);
static njs_int_t njs_generate_function_call(njs_vm_t *vm,
    njs_generator_t *generator, njs_parser_node_t *node);
static njs_int_t njs_generate_method_call(njs_vm_t *vm,
    njs_generator_t *generator, njs_parser_node_t *node);
static njs_int_t njs_generate_call(njs_vm_t *vm, njs_generator_t *generator,
    njs_parser_node_t *node);
static njs_int_t njs_generate_move_arguments(njs_vm_t *vm,
    njs_generator_t *generator, njs_parser_node_t *node);
static njs_int_t njs_generate_try_statement(njs_vm_t *vm,
    njs_generator_t *generator, njs_parser_node_t *node);
static njs_int_t njs_generate_throw_statement(njs_vm_t *vm,
    njs_generator_t *generator, njs_parser_node_t *node);
static njs_int_t njs_generate_import_statement(njs_vm_t *vm,
    njs_generator_t *generator, njs_parser_node_t *node);
static njs_int_t njs_generate_export_statement(njs_vm_t *vm,
    njs_generator_t *generator, njs_parser_node_t *node);
static njs_index_t njs_generate_dest_index(njs_vm_t *vm,
    njs_generator_t *generator, njs_parser_node_t *node);
static njs_index_t njs_generate_object_dest_index(njs_vm_t *vm,
    njs_generator_t *generator, njs_parser_node_t *node);
static njs_index_t njs_generate_node_temp_index_get(njs_vm_t *vm,
    njs_generator_t *generator, njs_parser_node_t *node);
static njs_index_t njs_generate_temp_index_get(njs_vm_t *vm,
    njs_generator_t *generator, njs_parser_node_t *node);
static njs_int_t njs_generate_children_indexes_release(njs_vm_t *vm,
    njs_generator_t *generator, njs_parser_node_t *node);
static njs_int_t njs_generate_node_index_release(njs_vm_t *vm,
    njs_generator_t *generator, njs_parser_node_t *node);
static njs_int_t njs_generate_index_release(njs_vm_t *vm,
    njs_generator_t *generator, njs_index_t index);
static njs_int_t njs_generate_global_reference(njs_vm_t *vm,
    njs_generator_t *generator, njs_parser_node_t *node, njs_bool_t exception);
static njs_int_t njs_generate_reference_error(njs_vm_t *vm,
    njs_generator_t *generator, njs_parser_node_t *node);


#define njs_generate_code(generator, type, _code, _op, nargs, nd)             \
    do {                                                                      \
        _code = (type *) njs_generate_reserve(vm, generator, sizeof(type));   \
        if (njs_slow_path(_code == NULL)) {                                   \
            return NJS_ERROR;                                                 \
        }                                                                     \
                                                                              \
        if (njs_generate_code_map(vm, generator, nd, (u_char *) _code)        \
            != NJS_OK)                                                        \
        {                                                                     \
            return NJS_ERROR;                                                 \
        }                                                                     \
                                                                              \
        generator->code_end += sizeof(type);                                  \
                                                                              \
        _code->code.operation = _op;                                          \
        _code->code.operands = 3 - nargs;                                     \
    } while (0)


#define njs_generate_code_jump(generator, _code, _offset)                     \
    do {                                                                      \
        njs_generate_code(generator, njs_vmcode_jump_t, _code,                \
                          NJS_VMCODE_JUMP, 0, NULL);                          \
        _code->offset = _offset;                                              \
    } while (0)


#define njs_generate_code_move(generator, _code, _dst, _src, node)            \
    do {                                                                      \
        njs_generate_code(generator, njs_vmcode_move_t, _code,                \
                          NJS_VMCODE_MOVE, 2, node);                          \
        _code->dst = _dst;                                                    \
        _code->src = _src;                                                    \
    } while (0)


#define njs_code_offset(generator, code)                                      \
    ((u_char *) code - generator->code_start)


#define njs_code_ptr(generator, type, offset)                                 \
    (type *) (generator->code_start + offset)


#define njs_code_jump_ptr(generator, offset)                                  \
    (njs_jump_off_t *) (generator->code_start + offset)


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
    njs_parser_node_error(vm, node, NJS_OBJ_TYPE_SYNTAX_ERROR, fmt,           \
                          ##__VA_ARGS__)


#define NJS_GENERATE_MAX_DEPTH  4096


static const njs_str_t  no_label     = njs_str("");
static const njs_str_t  return_label = njs_str("@return");
/* GCC and Clang complain about NULL argument passed to memcmp(). */
static const njs_str_t  undef_label  = { 0xffffffff, (u_char *) "" };


static njs_int_t
njs_generate(njs_vm_t *vm, njs_generator_t *generator, njs_parser_node_t *node)
{
    if (node == NULL) {
        return NJS_OK;
    }

    switch (node->token_type) {

    case NJS_TOKEN_VAR:
    case NJS_TOKEN_LET:
    case NJS_TOKEN_CONST:
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

    case NJS_TOKEN_DEBUGGER:
        return njs_generate_debugger_statement(vm, generator, node);

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
    case NJS_TOKEN_COALESCE:
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

    case NJS_TOKEN_NULL:
    case NJS_TOKEN_TRUE:
    case NJS_TOKEN_FALSE:
    case NJS_TOKEN_NUMBER:
    case NJS_TOKEN_STRING:
        node->index = njs_scope_global_index(vm, &node->u.value,
                                             generator->runtime);
        if (njs_slow_path(node->index == NJS_INDEX_ERROR)) {
            return NJS_ERROR;
        }

        return NJS_OK;

    case NJS_TOKEN_OBJECT_VALUE:
        node->index = node->u.object->index;
        return NJS_OK;

    case NJS_TOKEN_OBJECT:
        return njs_generate_object(vm, generator, node);

    case NJS_TOKEN_PROPERTY_GETTER:
    case NJS_TOKEN_PROPERTY_SETTER:
        return njs_generate_property_accessor(vm, generator, node);

    case NJS_TOKEN_ARRAY:
        return njs_generate_array(vm, generator, node);

    case NJS_TOKEN_FUNCTION_EXPRESSION:
        return njs_generate_function_expression(vm, generator, node);

    case NJS_TOKEN_FUNCTION:
        return njs_generate_function(vm, generator, node);

    case NJS_TOKEN_REGEXP:
        return njs_generate_regexp(vm, generator, node);

    case NJS_TOKEN_TEMPLATE_LITERAL:
        return njs_generate_template_literal(vm, generator, node);

    case NJS_TOKEN_EXTERNAL:
        return NJS_OK;

    case NJS_TOKEN_NAME:
    case NJS_TOKEN_ARGUMENTS:
    case NJS_TOKEN_EVAL:
    case NJS_TOKEN_THIS:
        return njs_generate_name(vm, generator, node);

    case NJS_TOKEN_FUNCTION_DECLARATION:
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

    case NJS_TOKEN_IMPORT:
        return njs_generate_import_statement(vm, generator, node);

    case NJS_TOKEN_EXPORT:
        return njs_generate_export_statement(vm, generator, node);

    default:
        njs_thread_log_debug("unknown token: %d", node->token);
        njs_internal_error(vm, "Generator failed: unknown token");

        return NJS_ERROR;
    }
}


njs_inline njs_int_t
njs_generator(njs_vm_t *vm, njs_generator_t *generator, njs_parser_node_t *node)
{
    njs_int_t  ret;

    if (njs_slow_path(generator->count++ > NJS_GENERATE_MAX_DEPTH)) {
        njs_range_error(vm, "Maximum call stack size exceeded");
        return NJS_ERROR;
    }

    ret = njs_generate(vm, generator, node);

    generator->count--;

    return ret;
}


static njs_int_t
njs_generate_wo_dest(njs_vm_t *vm, njs_generator_t *generator,
    njs_parser_node_t *node)
{
    njs_int_t           ret;
    njs_parser_scope_t  *scope;

    scope = njs_function_scope(node->scope);

    scope->dest_disable = 1;

    ret = njs_generator(vm, generator, node);

    scope->dest_disable = 0;

    return ret;
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

    size = njs_max(generator->code_end - generator->code_start + size,
                   generator->code_size);

    if (size < 1024) {
        size *= 2;

    } else {
        size += size / 2;
    }

    p = njs_mp_alloc(vm->mem_pool, size);
    if (njs_slow_path(p == NULL)) {
        njs_memory_error(vm);
        return NULL;
    }

    generator->code_size = size;

    size = generator->code_end - generator->code_start;
    memcpy(p, generator->code_start, size);

    njs_mp_free(vm->mem_pool, generator->code_start);

    generator->code_start = p;
    generator->code_end = p + size;

    return generator->code_end;
}


static njs_int_t
njs_generate_code_map(njs_vm_t *vm, njs_generator_t *generator,
    njs_parser_node_t *node, u_char *code)
{
    njs_arr_t          *map;
    njs_vm_line_num_t  *last;

    map = generator->lines;

    if (map != NULL && node != NULL) {
        last = (map->items != 0) ? njs_arr_last(map) : NULL;
        if (last == NULL || (node->token_line != last->line)) {
            last = njs_arr_add(map);
            if (njs_slow_path(last == NULL)) {
                return NJS_ERROR;
            }

            last->line = node->token_line;
            last->offset = njs_code_offset(generator, code);
        }
    }

    return NJS_OK;
}


uint32_t
njs_lookup_line(njs_vm_code_t *code, uint32_t offset)
{
    njs_uint_t         n;
    njs_vm_line_num_t  *map;

    n = 0;
    map = NULL;

    if (code->lines != NULL) {
        n = code->lines->items;
        map = (njs_vm_line_num_t *) code->lines->start;
    }

    while (n != 0) {
        if (offset >= map->offset && (n == 1 || offset < map[1].offset)) {
            return map->line;
        }

        map++;
        n--;
    }

    return 0;
}


static njs_int_t
njs_generate_name(njs_vm_t *vm, njs_generator_t *generator,
    njs_parser_node_t *node)
{
    njs_variable_t              *var;
    njs_parser_scope_t          *scope;
    njs_vmcode_variable_t       *variable;
    njs_vmcode_function_copy_t  *copy;

    var = njs_variable_reference(vm, node);
    if (njs_slow_path(var == NULL)) {
        return njs_generate_global_reference(vm, generator, node, 1);
    }

    if (var->function && var->type == NJS_VARIABLE_FUNCTION) {
        njs_generate_code(generator, njs_vmcode_function_copy_t, copy,
                          NJS_VMCODE_FUNCTION_COPY, 0, node);
        copy->function = &var->value;
        copy->retval = node->index;
    }

    if (var->init) {
        return NJS_OK;
    }

    if (var->type == NJS_VARIABLE_LET || var->type == NJS_VARIABLE_CONST) {
        scope = njs_function_scope(node->scope);

        if (scope->dest_disable) {
            njs_generate_code(generator, njs_vmcode_variable_t, variable,
                              NJS_VMCODE_NOT_INITIALIZED, 1, node);
            variable->dst = node->index;
        }
    }

    return NJS_OK;
}


static njs_int_t
njs_generate_variable(njs_vm_t *vm, njs_generator_t *generator,
    njs_parser_node_t *node, njs_reference_type_t type, njs_variable_t **retvar)
{
    njs_variable_t              *var;
    njs_parser_scope_t          *scope;
    njs_vmcode_variable_t       *variable;
    njs_vmcode_function_copy_t  *copy;

    var = njs_variable_reference(vm, node);

    if (retvar != NULL) {
        *retvar = var;
    }

    if (njs_slow_path(var == NULL)) {
        switch (type) {
        case NJS_DECLARATION:
            return njs_generate_reference_error(vm, generator, node);

        case NJS_REFERENCE:
        case NJS_TYPEOF:
            return njs_generate_global_reference(vm, generator, node,
                                                 type == NJS_REFERENCE);
        }
    }

    if (var->function && var->type == NJS_VARIABLE_FUNCTION) {
        njs_generate_code(generator, njs_vmcode_function_copy_t, copy,
                          NJS_VMCODE_FUNCTION_COPY, 0, node);
        copy->function = &var->value;
        copy->retval = node->index;
    }

    if (var->init) {
        return NJS_OK;
    }

    if (var->type == NJS_VARIABLE_LET || var->type == NJS_VARIABLE_CONST) {
        scope = njs_function_scope(node->scope);

        if ((!scope->dest_disable && njs_function_scope(var->scope) == scope)) {
            njs_generate_code(generator, njs_vmcode_variable_t, variable,
                              NJS_VMCODE_NOT_INITIALIZED, 1, node);
            variable->dst = node->index;
        }
    }

    return NJS_OK;
}


static njs_int_t
njs_generate_variable_wo_dest(njs_vm_t *vm, njs_generator_t *generator,
    njs_parser_node_t *node, njs_reference_type_t type, njs_variable_t **retvar)
{
    njs_int_t           ret;
    njs_parser_scope_t  *scope;

    scope = njs_function_scope(node->scope);

    scope->dest_disable = 1;

    ret = njs_generate_variable(vm, generator, node, type, retvar);

    scope->dest_disable = 0;

    return ret;
}


static njs_int_t
njs_generate_var_statement(njs_vm_t *vm, njs_generator_t *generator,
    njs_parser_node_t *node)
{
    njs_int_t          ret;
    njs_variable_t     *var;
    njs_parser_node_t  *lvalue, *expr;
    njs_vmcode_move_t  *move;

    lvalue = node->left;

    ret = njs_generate_variable_wo_dest(vm, generator, lvalue,
                                        NJS_DECLARATION, &var);
    if (njs_slow_path(ret != NJS_OK)) {
        return NJS_ERROR;
    }

    expr = node->right;

    if (expr == NULL) {
        /* Variable is only declared. */

        if (var->type == NJS_VARIABLE_CONST) {
            njs_syntax_error(vm, "missing initializer in const declaration");
            return NJS_ERROR;
        }

        if (var->type == NJS_VARIABLE_LET) {
            ret = njs_generate_let(vm, generator, node, var);
            if (njs_slow_path(ret != NJS_OK)) {
                return ret;
            }
        }

        var->init = 1;

        return NJS_OK;
    }

    if (var->type == NJS_VARIABLE_LET || var->type == NJS_VARIABLE_CONST) {
        ret = njs_generate_wo_dest(vm, generator, expr);
        if (njs_slow_path(ret != NJS_OK)) {
            return ret;
        }

        ret = njs_generate_let(vm, generator, node, var);
        if (njs_slow_path(ret != NJS_OK)) {
            return ret;
        }

    } else {
        expr->dest = lvalue;

        ret = njs_generator(vm, generator, expr);
        if (njs_slow_path(ret != NJS_OK)) {
            return ret;
        }
    }

    var->init = 1;

    /*
     * lvalue and expression indexes are equal if the expression is an
     * empty object or expression result is stored directly in variable.
     */
    if (lvalue->index != expr->index) {
        njs_generate_code_move(generator, move, lvalue->index, expr->index,
                               lvalue);
    }

    node->index = expr->index;
    node->temporary = expr->temporary;

    return NJS_OK;
}


static njs_int_t
njs_generate_let(njs_vm_t *vm, njs_generator_t *generator,
    njs_parser_node_t *node, njs_variable_t *var)
{
    njs_vmcode_variable_t  *code;

    njs_generate_code(generator, njs_vmcode_variable_t, code,
                      NJS_VMCODE_LET, 0, node);
    code->dst = var->index;

    return NJS_OK;
}


static njs_int_t
njs_generate_if_statement(njs_vm_t *vm, njs_generator_t *generator,
    njs_parser_node_t *node)
{
    njs_int_t               ret;
    njs_jump_off_t          jump_offset, label_offset;
    njs_vmcode_jump_t       *jump;
    njs_vmcode_cond_jump_t  *cond_jump;

    /* The condition expression. */

    ret = njs_generator(vm, generator, node->left);
    if (njs_slow_path(ret != NJS_OK)) {
        return ret;
    }

    njs_generate_code(generator, njs_vmcode_cond_jump_t, cond_jump,
                      NJS_VMCODE_IF_FALSE_JUMP, 2, node);
    cond_jump->cond = node->left->index;

    ret = njs_generate_node_index_release(vm, generator, node->left);
    if (njs_slow_path(ret != NJS_OK)) {
        return ret;
    }

    jump_offset = njs_code_offset(generator, cond_jump);
    label_offset = jump_offset + offsetof(njs_vmcode_cond_jump_t, offset);

    if (node->right != NULL && node->right->token_type == NJS_TOKEN_BRANCHING) {

        /* The "then" branch in a case of "if/then/else" statement. */

        node = node->right;

        ret = njs_generator(vm, generator, node->left);
        if (njs_slow_path(ret != NJS_OK)) {
            return ret;
        }

        ret = njs_generate_node_index_release(vm, generator, node->left);
        if (njs_slow_path(ret != NJS_OK)) {
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
    if (njs_slow_path(ret != NJS_OK)) {
        return ret;
    }

    ret = njs_generate_node_index_release(vm, generator, node->right);
    if (njs_slow_path(ret != NJS_OK)) {
        return ret;
    }

    njs_code_set_offset(generator, label_offset, jump_offset);

    return NJS_OK;
}


static njs_int_t
njs_generate_cond_expression(njs_vm_t *vm, njs_generator_t *generator,
    njs_parser_node_t *node)
{
    njs_int_t               ret;
    njs_jump_off_t          jump_offset, cond_jump_offset;
    njs_parser_node_t       *branch;
    njs_vmcode_move_t       *move;
    njs_vmcode_jump_t       *jump;
    njs_vmcode_cond_jump_t  *cond_jump;

    /* The condition expression. */

    ret = njs_generator(vm, generator, node->left);
    if (njs_slow_path(ret != NJS_OK)) {
        return ret;
    }

    njs_generate_code(generator, njs_vmcode_cond_jump_t, cond_jump,
                      NJS_VMCODE_IF_FALSE_JUMP, 2, node);

    cond_jump_offset = njs_code_offset(generator, cond_jump);
    cond_jump->cond = node->left->index;

    node->index = njs_generate_dest_index(vm, generator, node);
    if (njs_slow_path(node->index == NJS_INDEX_ERROR)) {
        return node->index;
    }

    branch = node->right;

    /* The "true" branch. */

    ret = njs_generator(vm, generator, branch->left);
    if (njs_slow_path(ret != NJS_OK)) {
        return ret;
    }

    /*
     * Branches usually uses node->index as destination, however,
     * if branch expression is a literal, variable or assignment,
     * then a MOVE operation is required.
     */

    if (node->index != branch->left->index) {
        njs_generate_code_move(generator, move, node->index,
                               branch->left->index, node);
    }

    ret = njs_generate_node_index_release(vm, generator, branch->left);
    if (njs_slow_path(ret != NJS_OK)) {
        return ret;
    }

    njs_generate_code_jump(generator, jump, 0);
    jump_offset = njs_code_offset(generator, jump);

    njs_code_set_jump_offset(generator, njs_vmcode_cond_jump_t,
                             cond_jump_offset);

    /* The "false" branch. */

    ret = njs_generator(vm, generator, branch->right);
    if (njs_slow_path(ret != NJS_OK)) {
        return ret;
    }

    if (node->index != branch->right->index) {
        njs_generate_code_move(generator, move, node->index,
                               branch->right->index, node);
    }

    njs_code_set_jump_offset(generator, njs_vmcode_cond_jump_t, jump_offset);

    ret = njs_generate_node_index_release(vm, generator, branch->right);
    if (njs_slow_path(ret != NJS_OK)) {
        return ret;
    }

    return NJS_OK;
}


static njs_int_t
njs_generate_switch_statement(njs_vm_t *vm, njs_generator_t *generator,
    njs_parser_node_t *swtch)
{
    njs_int_t                ret;
    njs_jump_off_t           jump_offset;
    njs_index_t              index;
    njs_parser_node_t        *node, *expr, *branch;
    njs_vmcode_move_t        *move;
    njs_vmcode_jump_t        *jump;
    njs_generator_patch_t    *patch, *next, *patches, **last;
    njs_vmcode_equal_jump_t  *equal;

    /* The "switch" expression. */

    expr = swtch->left;

    ret = njs_generator(vm, generator, expr);
    if (njs_slow_path(ret != NJS_OK)) {
        return ret;
    }

    index = expr->index;

    if (!expr->temporary) {
        index = njs_generate_temp_index_get(vm, generator, swtch);
        if (njs_slow_path(index == NJS_INDEX_ERROR)) {
            return NJS_ERROR;
        }

        njs_generate_code_move(generator, move, index, expr->index, swtch);
    }

    ret = njs_generate_start_block(vm, generator, NJS_GENERATOR_SWITCH,
                                   &swtch->name);
    if (njs_slow_path(ret != NJS_OK)) {
        return ret;
    }

    patches = NULL;
    last = &patches;

    for (branch = swtch->right; branch != NULL; branch = branch->left) {

        if (branch->token_type != NJS_TOKEN_DEFAULT) {

            /* The "case" expression. */

            node = branch->right;

            ret = njs_generator(vm, generator, node->left);
            if (njs_slow_path(ret != NJS_OK)) {
                return ret;
            }

            njs_generate_code(generator, njs_vmcode_equal_jump_t, equal,
                              NJS_VMCODE_IF_EQUAL_JUMP, 3, branch);
            equal->offset = offsetof(njs_vmcode_equal_jump_t, offset);
            equal->value1 = index;
            equal->value2 = node->left->index;

            ret = njs_generate_node_index_release(vm, generator, node->left);
            if (njs_slow_path(ret != NJS_OK)) {
                return ret;
            }

            patch = njs_mp_alloc(vm->mem_pool, sizeof(njs_generator_patch_t));
            if (njs_slow_path(patch == NULL)) {
                return NJS_ERROR;
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
    if (njs_slow_path(ret != NJS_OK)) {
        return ret;
    }

    njs_generate_code_jump(generator, jump,
                           offsetof(njs_vmcode_jump_t, offset));

    jump_offset = njs_code_offset(generator, jump);

    patch = patches;

    for (branch = swtch->right; branch != NULL; branch = branch->left) {

        if (branch->token_type == NJS_TOKEN_DEFAULT) {
            njs_code_set_jump_offset(generator, njs_vmcode_jump_t, jump_offset);
            jump = NULL;
            node = branch;

        } else {
            njs_code_update_offset(generator, patch);

            next = patch->next;

            njs_mp_free(vm->mem_pool, patch);

            patch = next;
            node = branch->right;
        }

        /* The "case/default" statements. */

        ret = njs_generator(vm, generator, node->right);
        if (njs_slow_path(ret != NJS_OK)) {
            return ret;
        }
    }

    if (jump != NULL) {
        /* A "switch" without default case. */
        njs_code_set_jump_offset(generator, njs_vmcode_jump_t, jump_offset);
    }

    /* Patch "break" statements offsets. */
    njs_generate_patch_block_exit(vm, generator);

    return NJS_OK;
}


static njs_int_t
njs_generate_while_statement(njs_vm_t *vm, njs_generator_t *generator,
    njs_parser_node_t *node)
{
    njs_int_t               ret;
    njs_jump_off_t          jump_offset, loop_offset;
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
                                   &node->name);
    if (njs_slow_path(ret != NJS_OK)) {
        return ret;
    }

    loop_offset = njs_code_offset(generator, generator->code_end);

    ret = njs_generator(vm, generator, node->left);
    if (njs_slow_path(ret != NJS_OK)) {
        return ret;
    }

    /* The loop condition. */

    njs_generate_patch_block(vm, generator, generator->block->continuation);

    njs_code_set_jump_offset(generator, njs_vmcode_jump_t, jump_offset);

    condition = node->right;

    ret = njs_generator(vm, generator, condition);
    if (njs_slow_path(ret != NJS_OK)) {
        return ret;
    }

    njs_generate_code(generator, njs_vmcode_cond_jump_t, cond_jump,
                      NJS_VMCODE_IF_TRUE_JUMP, 2, condition);
    cond_jump->offset = loop_offset - njs_code_offset(generator, cond_jump);
    cond_jump->cond = condition->index;

    njs_generate_patch_block_exit(vm, generator);

    return njs_generate_node_index_release(vm, generator, condition);
}


static njs_int_t
njs_generate_do_while_statement(njs_vm_t *vm, njs_generator_t *generator,
    njs_parser_node_t *node)
{
    njs_int_t               ret;
    njs_jump_off_t          loop_offset;
    njs_parser_node_t       *condition;
    njs_vmcode_cond_jump_t  *cond_jump;

    /* The loop body. */

    ret = njs_generate_start_block(vm, generator, NJS_GENERATOR_LOOP,
                                   &node->name);
    if (njs_slow_path(ret != NJS_OK)) {
        return ret;
    }

    loop_offset = njs_code_offset(generator, generator->code_end);

    ret = njs_generator(vm, generator, node->left);
    if (njs_slow_path(ret != NJS_OK)) {
        return ret;
    }

    /* The loop condition. */

    njs_generate_patch_block(vm, generator, generator->block->continuation);

    condition = node->right;

    ret = njs_generator(vm, generator, condition);
    if (njs_slow_path(ret != NJS_OK)) {
        return ret;
    }

    njs_generate_code(generator, njs_vmcode_cond_jump_t, cond_jump,
                      NJS_VMCODE_IF_TRUE_JUMP, 2, condition);
    cond_jump->offset = loop_offset - njs_code_offset(generator, cond_jump);
    cond_jump->cond = condition->index;

    njs_generate_patch_block_exit(vm, generator);

    return njs_generate_node_index_release(vm, generator, condition);
}


static njs_int_t
njs_generate_for_statement(njs_vm_t *vm, njs_generator_t *generator,
    njs_parser_node_t *node)
{
    njs_int_t               ret;
    njs_jump_off_t          jump_offset, loop_offset;
    njs_parser_node_t       *condition, *update, *init;
    njs_vmcode_jump_t       *jump;
    njs_vmcode_cond_jump_t  *cond_jump;

    ret = njs_generate_start_block(vm, generator, NJS_GENERATOR_LOOP,
                                   &node->name);
    if (njs_slow_path(ret != NJS_OK)) {
        return ret;
    }

    jump = NULL;

    /* The loop initialization. */

    ret = njs_generator(vm, generator, node->left);
    if (njs_slow_path(ret != NJS_OK)) {
        return ret;
    }

    ret = njs_generate_node_index_release(vm, generator, node->left);
    if (njs_slow_path(ret != NJS_OK)) {
        return ret;
    }

    init = node->left;
    node = node->right;
    condition = node->left;

    /*
     * Closures can occur in conditional and loop updates.  This must be
     * foreseen in order to generate optimized code for let updates.
     */

    ret = njs_generate_for_resolve_closure(vm, condition, generator->count);
    if (njs_slow_path(ret != NJS_OK)) {
        return ret;
    }

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
    if (njs_slow_path(ret != NJS_OK)) {
        return ret;
    }

    /* The loop update. */

    update = node->right;

    ret = njs_generate_for_resolve_closure(vm, update, generator->count);
    if (njs_slow_path(ret != NJS_OK)) {
        return ret;
    }

    ret = njs_generate_for_let_update(vm, generator, init, generator->count);
    if (njs_slow_path(ret != NJS_OK)) {
        return ret;
    }

    njs_generate_patch_block(vm, generator, generator->block->continuation);

    ret = njs_generator(vm, generator, update);
    if (njs_slow_path(ret != NJS_OK)) {
        return ret;
    }

    ret = njs_generate_node_index_release(vm, generator, update);
    if (njs_slow_path(ret != NJS_OK)) {
        return ret;
    }

    /* The loop condition. */

    if (condition != NULL) {
        njs_code_set_jump_offset(generator, njs_vmcode_jump_t, jump_offset);

        ret = njs_generator(vm, generator, condition);
        if (njs_slow_path(ret != NJS_OK)) {
            return ret;
        }

        njs_generate_code(generator, njs_vmcode_cond_jump_t, cond_jump,
                          NJS_VMCODE_IF_TRUE_JUMP, 2, condition);
        cond_jump->offset = loop_offset - njs_code_offset(generator, cond_jump);
        cond_jump->cond = condition->index;

        njs_generate_patch_block_exit(vm, generator);

        return njs_generate_node_index_release(vm, generator, condition);
    }

    njs_generate_code_jump(generator, jump,
                           loop_offset - njs_code_offset(generator, jump));

    njs_generate_patch_block_exit(vm, generator);

    return NJS_OK;
}


static njs_int_t
njs_generate_for_let_update(njs_vm_t *vm, njs_generator_t *generator,
    njs_parser_node_t *node, size_t depth)
{
    njs_parser_node_t         *let;
    njs_vmcode_variable_t     *code_var;
    njs_variable_reference_t  *ref;

    if (node == NULL) {
        return NJS_OK;
    }

    if (depth >= NJS_GENERATE_MAX_DEPTH) {
        return NJS_ERROR;
    }

    if (node->token_type != NJS_TOKEN_STATEMENT) {
        return NJS_OK;
    }

    let = node->right;

    if (let->token_type != NJS_TOKEN_LET
        && let->token_type != NJS_TOKEN_CONST)
    {
        return NJS_OK;
    }

    ref = &let->left->u.reference;

    if (ref->variable->closure) {
        njs_generate_code(generator, njs_vmcode_variable_t, code_var,
                          NJS_VMCODE_LET_UPDATE, 0, let);
        code_var->dst = let->left->index;
    }

    return njs_generate_for_let_update(vm, generator, node->left, depth + 1);
}


static njs_int_t
njs_generate_for_resolve_closure(njs_vm_t *vm, njs_parser_node_t *node,
    size_t depth)
{
    njs_int_t       ret;
    njs_bool_t      closure;
    njs_variable_t  *var;

    if (node == NULL) {
        return NJS_OK;
    }

    if (node->token_type == NJS_TOKEN_NAME) {
        var = njs_variable_resolve(vm, node);

        if (njs_fast_path(var != NULL)) {
            closure = njs_variable_closure_test(node->scope, var->scope);

            if (closure) {
                var->closure = 1;
            }
        }
    }

    if (depth >= NJS_GENERATE_MAX_DEPTH) {
        njs_range_error(vm, "Maximum call stack size exceeded");
        return NJS_ERROR;
    }

    ret = njs_generate_for_resolve_closure(vm, node->left, depth + 1);
    if (njs_slow_path(ret != NJS_OK)) {
        return ret;
    }

    ret = njs_generate_for_resolve_closure(vm, node->right, depth + 1);
    if (njs_slow_path(ret != NJS_OK)) {
        return ret;
    }

    return NJS_OK;
}


static njs_int_t
njs_generate_for_in_statement(njs_vm_t *vm, njs_generator_t *generator,
    njs_parser_node_t *node)
{
    njs_int_t                  ret;
    njs_index_t                index;
    njs_variable_t             *var;
    njs_jump_off_t             loop_offset, prop_offset;
    njs_parser_node_t          *foreach, *name;
    njs_vmcode_prop_next_t     *prop_next;
    njs_vmcode_prop_foreach_t  *prop_foreach;

    ret = njs_generate_start_block(vm, generator, NJS_GENERATOR_LOOP,
                                   &node->name);
    if (njs_slow_path(ret != NJS_OK)) {
        return ret;
    }

    /* The object. */

    foreach = node->left;
    name = foreach->left->right;

    if (name != NULL) {
        name = name->left;

        ret = njs_generate_variable_wo_dest(vm, generator, name,
                                            NJS_DECLARATION, &var);
        if (njs_slow_path(ret != NJS_OK)) {
            return NJS_ERROR;
        }

        foreach->left->index = name->index;

        ret = njs_generator(vm, generator, foreach->right);
        if (njs_slow_path(ret != NJS_OK)) {
            return ret;
        }

        var->init = 1;

    } else {
        ret = njs_generator(vm, generator, foreach->left);
        if (njs_slow_path(ret != NJS_OK)) {
            return ret;
        }

        ret = njs_generator(vm, generator, foreach->right);
        if (njs_slow_path(ret != NJS_OK)) {
            return ret;
        }
    }

    njs_generate_code(generator, njs_vmcode_prop_foreach_t, prop_foreach,
                      NJS_VMCODE_PROPERTY_FOREACH, 2, foreach);
    prop_offset = njs_code_offset(generator, prop_foreach);
    prop_foreach->object = foreach->right->index;

    index = njs_generate_temp_index_get(vm, generator, foreach->right);
    if (njs_slow_path(index == NJS_INDEX_ERROR)) {
        return NJS_ERROR;
    }

    prop_foreach->next = index;

    /* The loop body. */

    loop_offset = njs_code_offset(generator, generator->code_end);

    ret = njs_generator(vm, generator, node->right);
    if (njs_slow_path(ret != NJS_OK)) {
        return ret;
    }

    /* The loop iterator. */

    if (name != NULL) {
        ret = njs_generate_for_let_update(vm, generator, foreach->left,
                                          generator->count);
        if (njs_slow_path(ret != NJS_OK)) {
            return ret;
        }
    }

    njs_generate_patch_block(vm, generator, generator->block->continuation);

    njs_code_set_jump_offset(generator, njs_vmcode_prop_foreach_t, prop_offset);

    njs_generate_code(generator, njs_vmcode_prop_next_t, prop_next,
                      NJS_VMCODE_PROPERTY_NEXT, 3, node->left->left);
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
    if (njs_slow_path(ret != NJS_OK)) {
        return ret;
    }

    return njs_generate_index_release(vm, generator, index);
}


static njs_int_t
njs_generate_start_block(njs_vm_t *vm, njs_generator_t *generator,
    njs_generator_block_type_t type, const njs_str_t *label)
{
    njs_generator_block_t  *block;

    block = njs_mp_alloc(vm->mem_pool, sizeof(njs_generator_block_t));

    if (njs_fast_path(block != NULL)) {
        block->next = generator->block;
        generator->block = block;

        block->type = type;
        block->label = *label;
        block->continuation = NULL;
        block->exit = NULL;

        block->index = 0;

        return NJS_OK;
    }

    return NJS_ERROR;
}


static njs_generator_block_t *
njs_generate_lookup_block(njs_generator_block_t *block, uint32_t mask,
    const njs_str_t *label)
{
    if (njs_strstr_eq(label, &return_label)) {
        mask = NJS_GENERATOR_TRY;
        label = &no_label;
    }

    while (block != NULL) {
        if ((block->type & mask) != 0
            && (label->length == 0 || njs_strstr_eq(&block->label, label)))
        {
            return block;
        }

        block = block->next;
    }

    return NULL;
}


static njs_generator_block_t *
njs_generate_find_block(njs_generator_block_t *block, uint32_t mask,
    const njs_str_t *label)
{
    njs_generator_block_t  *dest_block;

    /*
     * ES5.1: 12.8 The break Statement
     * "break" without a label is valid only from within
     * loop or switch statement.
     */
    if ((mask & NJS_GENERATOR_ALL) == NJS_GENERATOR_ALL
        && label->length != 0)
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
    const njs_str_t *label, njs_jump_off_t offset)
{
    njs_generator_patch_t  *patch;

    patch = njs_mp_alloc(vm->mem_pool, sizeof(njs_generator_patch_t));
    if (njs_slow_path(patch == NULL)) {
        njs_memory_error(vm);
        return NULL;
    }

    patch->next = block->continuation;
    block->continuation = patch;

    patch->jump_offset = offset;

    patch->label = *label;

    return patch;
}


static void
njs_generate_patch_block(njs_vm_t *vm, njs_generator_t *generator,
    njs_generator_patch_t *list)
{
    njs_generator_patch_t  *patch, *next;

    for (patch = list; patch != NULL; patch = next) {
        njs_code_update_offset(generator, patch);
        next = patch->next;

        njs_mp_free(vm->mem_pool, patch);
    }
}


static njs_generator_patch_t *
njs_generate_make_exit_patch(njs_vm_t *vm, njs_generator_block_t *block,
    const njs_str_t *label, njs_jump_off_t offset)
{
    njs_generator_patch_t  *patch;

    patch = njs_mp_alloc(vm->mem_pool, sizeof(njs_generator_patch_t));
    if (njs_slow_path(patch == NULL)) {
        njs_memory_error(vm);
        return NULL;
    }

    patch->next = block->exit;
    block->exit = patch;

    patch->jump_offset = offset;

    patch->label = *label;

    return patch;
}


static void
njs_generate_patch_block_exit(njs_vm_t *vm, njs_generator_t *generator)
{
    njs_generator_block_t  *block;

    block = generator->block;
    generator->block = block->next;

    njs_generate_patch_block(vm, generator, block->exit);

    njs_mp_free(vm->mem_pool, block);
}


/*
 * TODO: support multiple destination points from within try-catch block.
 */
static const njs_str_t *
njs_generate_jump_destination(njs_vm_t *vm, njs_generator_block_t *block,
    const char *inst_type, uint32_t mask, const njs_str_t *label1,
    const njs_str_t *label2)
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


static njs_int_t
njs_generate_continue_statement(njs_vm_t *vm, njs_generator_t *generator,
    njs_parser_node_t *node)
{
    const njs_str_t        *label, *dest;
    njs_vmcode_jump_t      *jump;
    njs_generator_patch_t  *patch;
    njs_generator_block_t  *block;

    label = &node->name;

    block = njs_generate_find_block(generator->block, NJS_GENERATOR_LOOP,
                                    label);

    if (njs_slow_path(block == NULL)) {
        goto syntax_error;
    }

    if (block->type == NJS_GENERATOR_TRY && block->continuation != NULL) {
        dest = njs_generate_jump_destination(vm, block->next, "continue",
                                             NJS_GENERATOR_LOOP,
                                             &block->continuation->label,
                                             label);
        if (njs_slow_path(dest == NULL)) {
            return NJS_ERROR;
        }
    }

    njs_generate_code_jump(generator, jump,
                           offsetof(njs_vmcode_jump_t, offset));

    patch = njs_generate_make_continuation_patch(vm, block, label,
                                         njs_code_offset(generator, jump)
                                         + offsetof(njs_vmcode_jump_t, offset));
    if (njs_slow_path(patch == NULL)) {
        return NJS_ERROR;
    }

    return NJS_OK;

syntax_error:

    njs_generate_syntax_error(vm, node, "Illegal continue statement");

    return NJS_ERROR;
}


static njs_int_t
njs_generate_break_statement(njs_vm_t *vm, njs_generator_t *generator,
    njs_parser_node_t *node)
{
    const njs_str_t        *label, *dest;
    njs_vmcode_jump_t      *jump;
    njs_generator_patch_t  *patch;
    njs_generator_block_t  *block;

    label = &node->name;

    block = njs_generate_find_block(generator->block, NJS_GENERATOR_ALL, label);
    if (njs_slow_path(block == NULL)) {
        goto syntax_error;
     }

    if (block->type == NJS_GENERATOR_TRY && block->exit != NULL) {
        dest = njs_generate_jump_destination(vm, block->next, "break/return",
                                             NJS_GENERATOR_ALL,
                                             &block->exit->label, label);
        if (njs_slow_path(dest == NULL)) {
            return NJS_ERROR;
        }
    }

    njs_generate_code_jump(generator, jump,
                           offsetof(njs_vmcode_jump_t, offset));

    patch = njs_generate_make_exit_patch(vm, block, label,
                                         njs_code_offset(generator, jump)
                                         + offsetof(njs_vmcode_jump_t, offset));
    if (njs_slow_path(patch == NULL)) {
        return NJS_ERROR;
    }

    return NJS_OK;

syntax_error:

    njs_generate_syntax_error(vm, node, "Illegal break statement");

    return NJS_ERROR;
}


static njs_int_t
njs_generate_debugger_statement(njs_vm_t *vm, njs_generator_t *generator,
    njs_parser_node_t *node)
{
    njs_vmcode_debugger_t  *debugger;

    njs_generate_code(generator, njs_vmcode_debugger_t, debugger,
                      NJS_VMCODE_DEBUGGER, 0, node);

    debugger->retval = njs_generate_dest_index(vm, generator, node);
    if (njs_slow_path(debugger->retval == NJS_INDEX_ERROR)) {
        return debugger->retval;
    }

    return NJS_OK;
}


static njs_int_t
njs_generate_statement(njs_vm_t *vm, njs_generator_t *generator,
    njs_parser_node_t *node)
{
    njs_int_t              ret;
    njs_variable_t         *var;
    njs_parser_node_t      *right;
    njs_vmcode_variable_t  *code;

    right = node->right;

    if (right != NULL && right->token_type == NJS_TOKEN_NAME) {
        var = njs_variable_reference(vm, right);
        if (njs_slow_path(var == NULL)) {
            goto statement;
        }

        if (!var->init && (var->type == NJS_VARIABLE_LET
            || var->type == NJS_VARIABLE_CONST))
        {
            njs_generate_code(generator, njs_vmcode_variable_t, code,
                              NJS_VMCODE_INITIALIZATION_TEST, 0, right);
            code->dst = right->index;
        }

        if (node->left == NULL) {
            return NJS_OK;
        }

        node = node->left;
    }

statement:

    ret = njs_generate_children(vm, generator, node);

    if (njs_fast_path(ret == NJS_OK)) {
        return njs_generate_node_index_release(vm, generator, right);
    }

    return ret;
}


static njs_int_t
njs_generate_block_statement(njs_vm_t *vm, njs_generator_t *generator,
    njs_parser_node_t *node)
{
    njs_int_t  ret;

    ret = njs_generate_start_block(vm, generator, NJS_GENERATOR_BLOCK,
                                   &node->name);
    if (njs_slow_path(ret != NJS_OK)) {
        return ret;
    }

    ret = njs_generate_statement(vm, generator, node);
    if (njs_slow_path(ret != NJS_OK)) {
        return ret;
    }

    njs_generate_patch_block_exit(vm, generator);

    return ret;
}


static njs_int_t
njs_generate_children(njs_vm_t *vm, njs_generator_t *generator,
    njs_parser_node_t *node)
{
    njs_int_t  ret;

    ret = njs_generator(vm, generator, node->left);
    if (njs_slow_path(ret != NJS_OK)) {
        return ret;
    }

    ret = njs_generate_node_index_release(vm, generator, node->left);
    if (njs_slow_path(ret != NJS_OK)) {
        return ret;
    }

    return njs_generator(vm, generator, node->right);
}


static njs_int_t
njs_generate_stop_statement(njs_vm_t *vm, njs_generator_t *generator,
    njs_parser_node_t *node)
{
    njs_int_t          ret;
    njs_index_t        index;
    njs_vmcode_stop_t  *stop;

    ret = njs_generate_children(vm, generator, node);

    if (njs_fast_path(ret == NJS_OK)) {
        njs_generate_code(generator, njs_vmcode_stop_t, stop,
                          NJS_VMCODE_STOP, 1, node);

        index = njs_scope_undefined_index(vm, 0);
        node = node->right;

        if (node != NULL) {
            if ((node->index != NJS_INDEX_NONE
                 && node->token_type != NJS_TOKEN_FUNCTION_DECLARATION)
                || node->token_type == NJS_TOKEN_THIS)
            {
                index = node->index;
            }
        }

        stop->retval = index;
    }

    return ret;
}


static njs_int_t
njs_generate_comma_expression(njs_vm_t *vm, njs_generator_t *generator,
    njs_parser_node_t *node)
{
    njs_int_t  ret;

    ret = njs_generate_children(vm, generator, node);

    if (njs_fast_path(ret == NJS_OK)) {
        node->index = node->right->index;
    }

    return ret;
}


static njs_int_t
njs_generate_assignment(njs_vm_t *vm, njs_generator_t *generator,
    njs_parser_node_t *node)
{
    njs_int_t              ret;
    njs_index_t            index, src;
    njs_variable_t         *var;
    njs_parser_node_t      *lvalue, *expr, *object, *property;
    njs_vmcode_move_t      *move;
    njs_vmcode_variable_t  *var_code;
    njs_vmcode_prop_set_t  *prop_set;

    lvalue = node->left;
    expr = node->right;
    expr->dest = NULL;

    if (lvalue->token_type == NJS_TOKEN_NAME) {

        ret = njs_generate_variable(vm, generator, lvalue, NJS_DECLARATION,
                                    &var);
        if (njs_slow_path(ret != NJS_OK)) {
            return ret;
        }

        if (var != NULL && var->type == NJS_VARIABLE_CONST) {
            njs_generate_code(generator, njs_vmcode_variable_t, var_code,
                              NJS_VMCODE_ASSIGNMENT_ERROR, 0, node);
            var_code->dst = var->index;

            return NJS_OK;
        }

        expr->dest = lvalue;

        ret = njs_generator(vm, generator, expr);
        if (njs_slow_path(ret != NJS_OK)) {
            return ret;
        }

        /*
         * lvalue and expression indexes are equal if the expression is an
         * empty object or expression result is stored directly in variable.
         */
        if (lvalue->index != expr->index) {
            njs_generate_code_move(generator, move, lvalue->index, expr->index,
                                   expr);
        }

        node->index = expr->index;
        node->temporary = expr->temporary;

        return NJS_OK;
    }

    /* lvalue->token == NJS_TOKEN_PROPERTY(_INIT) */

    /* Object. */

    object = lvalue->left;

    ret = njs_generator(vm, generator, object);
    if (njs_slow_path(ret != NJS_OK)) {
        return ret;
    }

    /* Property. */

    property = lvalue->right;

    ret = njs_generator(vm, generator, property);
    if (njs_slow_path(ret != NJS_OK)) {
        return ret;
    }

    if (njs_slow_path(njs_parser_has_side_effect(expr))) {
        /*
         * Preserve object and property values stored in variables in a case
         * if the variables can be changed by side effects in expression.
         */
        if (object->token_type == NJS_TOKEN_NAME) {
            src = object->index;

            index = njs_generate_node_temp_index_get(vm, generator, object);
            if (njs_slow_path(index == NJS_INDEX_ERROR)) {
                return NJS_ERROR;
            }

            njs_generate_code_move(generator, move, index, src, object);
        }

        if (property->token_type == NJS_TOKEN_NAME) {
            src = property->index;

            index = njs_generate_node_temp_index_get(vm, generator, property);
            if (njs_slow_path(index == NJS_INDEX_ERROR)) {
                return NJS_ERROR;
            }

            njs_generate_code_move(generator, move, index, src, property);
        }
    }

    ret = njs_generator(vm, generator, expr);
    if (njs_slow_path(ret != NJS_OK)) {
        return ret;
    }

    switch (lvalue->token_type) {
    case NJS_TOKEN_PROPERTY_INIT:
        njs_generate_code(generator, njs_vmcode_prop_set_t, prop_set,
                          NJS_VMCODE_PROPERTY_INIT, 3, expr);
        break;

    case NJS_TOKEN_PROTO_INIT:
        njs_generate_code(generator, njs_vmcode_prop_set_t, prop_set,
                          NJS_VMCODE_PROTO_INIT, 3, expr);
        break;

    default:
        /* NJS_VMCODE_PROPERTY_SET */
        njs_generate_code(generator, njs_vmcode_prop_set_t, prop_set,
                          NJS_VMCODE_PROPERTY_SET, 3, expr);
    }

    prop_set->value = expr->index;
    prop_set->object = object->index;
    prop_set->property = property->index;

    node->index = expr->index;
    node->temporary = expr->temporary;

    return njs_generate_children_indexes_release(vm, generator, lvalue);
}


static njs_int_t
njs_generate_operation_assignment(njs_vm_t *vm, njs_generator_t *generator,
    njs_parser_node_t *node)
{
    njs_int_t              ret;
    njs_index_t            index, src;
    njs_variable_t         *var;
    njs_parser_node_t      *lvalue, *expr, *object, *property;
    njs_vmcode_move_t      *move;
    njs_vmcode_3addr_t     *code;
    njs_vmcode_variable_t  *var_code;
    njs_vmcode_prop_get_t  *prop_get;
    njs_vmcode_prop_set_t  *prop_set;

    lvalue = node->left;

    if (lvalue->token_type == NJS_TOKEN_NAME) {

        ret = njs_generate_variable(vm, generator, lvalue, NJS_DECLARATION,
                                    &var);
        if (njs_slow_path(ret != NJS_OK)) {
            return ret;
        }

        if (var != NULL && var->type == NJS_VARIABLE_CONST) {
            njs_generate_code(generator, njs_vmcode_variable_t, var_code,
                              NJS_VMCODE_ASSIGNMENT_ERROR, 0, node);
            var_code->dst = var->index;

            return NJS_OK;
        }

        index = lvalue->index;
        expr = node->right;

        if (njs_slow_path(njs_parser_has_side_effect(expr))) {
            /* Preserve variable value if it may be changed by expression. */

            njs_generate_code(generator, njs_vmcode_move_t, move,
                              NJS_VMCODE_MOVE, 2, expr);
            move->src = lvalue->index;

            index = njs_generate_temp_index_get(vm, generator, expr);
            if (njs_slow_path(index == NJS_INDEX_ERROR)) {
                return NJS_ERROR;
            }

            move->dst = index;
        }

        ret = njs_generator(vm, generator, expr);
        if (njs_slow_path(ret != NJS_OK)) {
            return ret;
        }

        njs_generate_code(generator, njs_vmcode_3addr_t, code,
                          node->u.operation, 3, expr);
        code->dst = lvalue->index;
        code->src1 = index;
        code->src2 = expr->index;

        node->index = lvalue->index;

        if (lvalue->index != index) {
            ret = njs_generate_index_release(vm, generator, index);
            if (njs_slow_path(ret != NJS_OK)) {
                return ret;
            }
        }

        return njs_generate_node_index_release(vm, generator, expr);
    }

    /* lvalue->token == NJS_TOKEN_PROPERTY */

    /* Object. */

    object = lvalue->left;

    ret = njs_generator(vm, generator, object);
    if (njs_slow_path(ret != NJS_OK)) {
        return ret;
    }

    /* Property. */

    property = lvalue->right;

    ret = njs_generator(vm, generator, property);
    if (njs_slow_path(ret != NJS_OK)) {
        return ret;
    }

    if (njs_slow_path(njs_parser_has_side_effect(node->right))) {
        /*
         * Preserve object and property values stored in variables in a case
         * if the variables can be changed by side effects in expression.
         */
        if (object->token_type == NJS_TOKEN_NAME) {
            src = object->index;

            index = njs_generate_node_temp_index_get(vm, generator, object);
            if (njs_slow_path(index == NJS_INDEX_ERROR)) {
                return NJS_ERROR;
            }

            njs_generate_code_move(generator, move, index, src, object);
        }

        if (property->token_type == NJS_TOKEN_NAME) {
            src = property->index;

            index = njs_generate_node_temp_index_get(vm, generator, property);
            if (njs_slow_path(index == NJS_INDEX_ERROR)) {
                return NJS_ERROR;
            }

            njs_generate_code_move(generator, move, index, src, property);
        }
    }

    index = njs_generate_node_temp_index_get(vm, generator, node);
    if (njs_slow_path(index == NJS_INDEX_ERROR)) {
        return NJS_ERROR;
    }

    njs_generate_code(generator, njs_vmcode_prop_get_t, prop_get,
                      NJS_VMCODE_PROPERTY_GET, 3, property);
    prop_get->value = index;
    prop_get->object = object->index;
    prop_get->property = property->index;

    expr = node->right;

    ret = njs_generator(vm, generator, expr);
    if (njs_slow_path(ret != NJS_OK)) {
        return ret;
    }

    njs_generate_code(generator, njs_vmcode_3addr_t, code,
                      node->u.operation, 3, expr);
    code->dst = node->index;
    code->src1 = node->index;
    code->src2 = expr->index;

    njs_generate_code(generator, njs_vmcode_prop_set_t, prop_set,
                      NJS_VMCODE_PROPERTY_SET, 3, expr);
    prop_set->value = node->index;
    prop_set->object = object->index;
    prop_set->property = property->index;

    ret = njs_generate_children_indexes_release(vm, generator, lvalue);
    if (njs_slow_path(ret != NJS_OK)) {
        return ret;
    }

    return njs_generate_node_index_release(vm, generator, expr);
}


static njs_int_t
njs_generate_object(njs_vm_t *vm, njs_generator_t *generator,
    njs_parser_node_t *node)
{
    njs_vmcode_object_t  *object;

    node->index = njs_generate_object_dest_index(vm, generator, node);
    if (njs_slow_path(node->index == NJS_INDEX_ERROR)) {
        return NJS_ERROR;
    }

    njs_generate_code(generator, njs_vmcode_object_t, object,
                      NJS_VMCODE_OBJECT, 1, node);
    object->retval = node->index;

    /* Initialize object. */
    return njs_generator(vm, generator, node->left);
}


static njs_int_t
njs_generate_property_accessor(njs_vm_t *vm, njs_generator_t *generator,
    njs_parser_node_t *node)
{
    njs_int_t                   ret;
    njs_parser_node_t           *lvalue, *function, *object, *property;
    njs_vmcode_prop_accessor_t  *accessor;

    lvalue = node->left;
    function = node->right;

    object = lvalue->left;

    ret = njs_generator(vm, generator, object);
    if (njs_slow_path(ret != NJS_OK)) {
        return ret;
    }

    property = lvalue->right;

    ret = njs_generator(vm, generator, property);
    if (njs_slow_path(ret != NJS_OK)) {
        return ret;
    }

    ret = njs_generator(vm, generator, function);
    if (njs_slow_path(ret != NJS_OK)) {
        return ret;
    }

    njs_generate_code(generator, njs_vmcode_prop_accessor_t, accessor,
                      NJS_VMCODE_PROPERTY_ACCESSOR, 3, function);

    accessor->value = function->index;
    accessor->object = object->index;
    accessor->property = property->index;
    accessor->type = (node->token_type == NJS_TOKEN_PROPERTY_GETTER)
                     ? NJS_OBJECT_PROP_GETTER : NJS_OBJECT_PROP_SETTER;

    return NJS_OK;
}


static njs_int_t
njs_generate_array(njs_vm_t *vm, njs_generator_t *generator,
    njs_parser_node_t *node)
{
    njs_vmcode_array_t  *array;

    node->index = njs_generate_object_dest_index(vm, generator, node);
    if (njs_slow_path(node->index == NJS_INDEX_ERROR)) {
        return NJS_ERROR;
    }

    njs_generate_code(generator, njs_vmcode_array_t, array,
                      NJS_VMCODE_ARRAY, 1, node);
    array->ctor = node->ctor;
    array->retval = node->index;
    array->length = node->u.length;

    /* Initialize array. */
    return njs_generator(vm, generator, node->left);
}


static njs_int_t
njs_generate_function_expression(njs_vm_t *vm, njs_generator_t *generator,
    njs_parser_node_t *node)
{
    njs_int_t                ret;
    njs_variable_t           *var;
    njs_function_lambda_t    *lambda;
    njs_vmcode_function_t    *function;
    const njs_lexer_entry_t  *lex_entry;

    var = njs_variable_reference(vm, node->left);
    if (njs_slow_path(var == NULL)) {
        return njs_generate_reference_error(vm, generator, node->left);
    }

    lambda = node->u.value.data.u.lambda;

    lex_entry = njs_lexer_entry(var->unique_id);
    if (njs_slow_path(lex_entry == NULL)) {
        return NJS_ERROR;
    }

    ret = njs_generate_function_scope(vm, lambda, node, &lex_entry->name);
    if (njs_slow_path(ret != NJS_OK)) {
        return ret;
    }

    njs_generate_code(generator, njs_vmcode_function_t, function,
                      NJS_VMCODE_FUNCTION, 1, node);
    function->lambda = lambda;

    node->index = njs_generate_object_dest_index(vm, generator, node);
    if (njs_slow_path(node->index == NJS_INDEX_ERROR)) {
        return NJS_ERROR;
    }

    function->retval = node->index;

    return NJS_OK;
}


static njs_int_t
njs_generate_function(njs_vm_t *vm, njs_generator_t *generator,
    njs_parser_node_t *node)
{
    njs_int_t              ret;
    njs_bool_t             module;
    const njs_str_t        *name;
    njs_function_lambda_t  *lambda;
    njs_vmcode_function_t  *function;

    lambda = node->u.value.data.u.lambda;
    module = node->right->scope->module;

    name = module ? &njs_entry_module : &njs_entry_anonymous;

    ret = njs_generate_function_scope(vm, lambda, node, name);
    if (njs_slow_path(ret != NJS_OK)) {
        return ret;
    }

    njs_generate_code(generator, njs_vmcode_function_t, function,
                      NJS_VMCODE_FUNCTION, 1, node);
    function->lambda = lambda;

    node->index = njs_generate_object_dest_index(vm, generator, node);
    if (njs_slow_path(node->index == NJS_INDEX_ERROR)) {
        return NJS_ERROR;
    }

    function->retval = node->index;

    return NJS_OK;
}


static njs_int_t
njs_generate_regexp(njs_vm_t *vm, njs_generator_t *generator,
    njs_parser_node_t *node)
{
    njs_vmcode_regexp_t  *regexp;

    node->index = njs_generate_object_dest_index(vm, generator, node);
    if (njs_slow_path(node->index == NJS_INDEX_ERROR)) {
        return NJS_ERROR;
    }

    njs_generate_code(generator, njs_vmcode_regexp_t, regexp,
                      NJS_VMCODE_REGEXP, 1, node);
    regexp->retval = node->index;
    regexp->pattern = node->u.value.data.u.data;

    return NJS_OK;
}


static njs_int_t
njs_generate_template_literal(njs_vm_t *vm, njs_generator_t *generator,
    njs_parser_node_t *node)
{
    njs_int_t                      ret;
    njs_vmcode_template_literal_t  *code;

    ret = njs_generator(vm, generator, node->left);
    if (njs_slow_path(ret != NJS_OK)) {
        return ret;
    }

    njs_generate_code(generator, njs_vmcode_template_literal_t, code,
                      NJS_VMCODE_TEMPLATE_LITERAL, 1, node);
    code->retval = node->left->index;

    node->index = node->left->index;

    return NJS_OK;
}


static njs_int_t
njs_generate_test_jump_expression(njs_vm_t *vm, njs_generator_t *generator,
    njs_parser_node_t *node)
{
    njs_int_t               ret;
    njs_jump_off_t          jump_offset;
    njs_vmcode_move_t       *move;
    njs_vmcode_test_jump_t  *test_jump;

    ret = njs_generator(vm, generator, node->left);
    if (njs_slow_path(ret != NJS_OK)) {
        return ret;
    }

    njs_generate_code(generator, njs_vmcode_test_jump_t, test_jump,
                      node->u.operation, 2, node);
    jump_offset = njs_code_offset(generator, test_jump);
    test_jump->value = node->left->index;

    node->index = njs_generate_node_temp_index_get(vm, generator, node);
    if (njs_slow_path(node->index == NJS_INDEX_ERROR)) {
        return node->index;
    }

    test_jump->retval = node->index;

    ret = njs_generator(vm, generator, node->right);
    if (njs_slow_path(ret != NJS_OK)) {
        return ret;
    }

    /*
     * The right expression usually uses node->index as destination,
     * however, if the expression is a literal, variable or assignment,
     * then a MOVE operation is required.
     */

    if (node->index != node->right->index) {
        njs_generate_code_move(generator, move, node->index,
                               node->right->index, node);
    }

    njs_code_set_jump_offset(generator, njs_vmcode_test_jump_t, jump_offset);

    return njs_generate_children_indexes_release(vm, generator, node);
}


static njs_int_t
njs_generate_3addr_operation(njs_vm_t *vm, njs_generator_t *generator,
    njs_parser_node_t *node, njs_bool_t swap)
{
    njs_int_t           ret;
    njs_index_t         index;
    njs_parser_node_t   *left, *right;
    njs_vmcode_move_t   *move;
    njs_vmcode_3addr_t  *code;

    left = node->left;

    ret = njs_generator(vm, generator, left);
    if (njs_slow_path(ret != NJS_OK)) {
        return ret;
    }

    right = node->right;

    if (left->token_type == NJS_TOKEN_NAME) {

        if (njs_slow_path(njs_parser_has_side_effect(right))) {
            njs_generate_code(generator, njs_vmcode_move_t, move,
                              NJS_VMCODE_MOVE, 2, node);
            move->src = left->index;

            index = njs_generate_node_temp_index_get(vm, generator, left);
            if (njs_slow_path(index == NJS_INDEX_ERROR)) {
                return NJS_ERROR;
            }

            move->dst = index;
        }
    }

    ret = njs_generator(vm, generator, right);
    if (njs_slow_path(ret != NJS_OK)) {
        return ret;
    }

    njs_generate_code(generator, njs_vmcode_3addr_t, code,
                      node->u.operation, 3, node);

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
    if (njs_slow_path(node->index == NJS_INDEX_ERROR)) {
        return node->index;
    }

    code->dst = node->index;

    njs_thread_log_debug("CODE3  %p, %p, %p",
                         code->dst, code->src1, code->src2);

    return NJS_OK;
}


static njs_int_t
njs_generate_2addr_operation(njs_vm_t *vm, njs_generator_t *generator,
    njs_parser_node_t *node)
{
    njs_int_t           ret;
    njs_vmcode_2addr_t  *code;

    ret = njs_generator(vm, generator, node->left);
    if (njs_slow_path(ret != NJS_OK)) {
        return ret;
    }

    njs_generate_code(generator, njs_vmcode_2addr_t, code,
                      node->u.operation, 2, node);
    code->src = node->left->index;

    node->index = njs_generate_dest_index(vm, generator, node);
    if (njs_slow_path(node->index == NJS_INDEX_ERROR)) {
        return node->index;
    }

    code->dst = node->index;

    njs_thread_log_debug("CODE2  %p, %p", code->dst, code->src);

    return NJS_OK;
}


static njs_int_t
njs_generate_typeof_operation(njs_vm_t *vm, njs_generator_t *generator,
    njs_parser_node_t *node)
{
    njs_int_t           ret;
    njs_parser_node_t   *expr;
    njs_vmcode_2addr_t  *code;

    expr = node->left;

    if (expr->token_type == NJS_TOKEN_NAME) {
        ret = njs_generate_variable(vm, generator, expr, NJS_TYPEOF, NULL);
        if (njs_slow_path(ret != NJS_OK)) {
            return NJS_ERROR;
        }

    } else {
        ret = njs_generator(vm, generator, node->left);
        if (njs_slow_path(ret != NJS_OK)) {
            return ret;
        }
    }

    njs_generate_code(generator, njs_vmcode_2addr_t, code,
                      node->u.operation, 2, expr);
    code->src = node->left->index;

    node->index = njs_generate_dest_index(vm, generator, node);
    if (njs_slow_path(node->index == NJS_INDEX_ERROR)) {
        return node->index;
    }

    code->dst = node->index;

    njs_thread_log_debug("CODE2  %p, %p", code->dst, code->src);

    return NJS_OK;
}


static njs_int_t
njs_generate_inc_dec_operation(njs_vm_t *vm, njs_generator_t *generator,
    njs_parser_node_t *node, njs_bool_t post)
{
    njs_int_t              ret;
    njs_index_t            index, dest_index;
    njs_variable_t         *var;
    njs_parser_node_t      *lvalue;
    njs_vmcode_3addr_t     *code;
    njs_vmcode_variable_t  *var_code;
    njs_vmcode_prop_get_t  *prop_get;
    njs_vmcode_prop_set_t  *prop_set;

    lvalue = node->left;

    if (lvalue->token_type == NJS_TOKEN_NAME) {

        ret = njs_generate_variable(vm, generator, lvalue, NJS_DECLARATION,
                                    &var);
        if (njs_slow_path(ret != NJS_OK)) {
            return ret;
        }

        if (var != NULL && var->type == NJS_VARIABLE_CONST) {
            njs_generate_code(generator, njs_vmcode_variable_t, var_code,
                              NJS_VMCODE_ASSIGNMENT_ERROR, 0, node);
            var_code->dst = var->index;

            return NJS_OK;
        }

        index = njs_generate_dest_index(vm, generator, node);
        if (njs_slow_path(index == NJS_INDEX_ERROR)) {
            return index;
        }

        node->index = index;

        njs_generate_code(generator, njs_vmcode_3addr_t, code,
                          node->u.operation, 3, node);
        code->dst = index;
        code->src1 = lvalue->index;
        code->src2 = lvalue->index;

        return NJS_OK;
    }

    /* lvalue->token == NJS_TOKEN_PROPERTY */

    /* Object. */

    ret = njs_generator(vm, generator, lvalue->left);
    if (njs_slow_path(ret != NJS_OK)) {
        return ret;
    }

    /* Property. */

    ret = njs_generator(vm, generator, lvalue->right);
    if (njs_slow_path(ret != NJS_OK)) {
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

    if (njs_slow_path(index == NJS_INDEX_ERROR)) {
        return NJS_ERROR;
    }

    njs_generate_code(generator, njs_vmcode_prop_get_t, prop_get,
                      NJS_VMCODE_PROPERTY_GET, 3, node);
    prop_get->value = index;
    prop_get->object = lvalue->left->index;
    prop_get->property = lvalue->right->index;

    njs_generate_code(generator, njs_vmcode_3addr_t, code,
                      node->u.operation, 3, node);
    code->dst = dest_index;
    code->src1 = index;
    code->src2 = index;

    njs_generate_code(generator, njs_vmcode_prop_set_t, prop_set,
                      NJS_VMCODE_PROPERTY_SET, 3, node);
    prop_set->value = index;
    prop_set->object = lvalue->left->index;
    prop_set->property = lvalue->right->index;

    if (post) {
        ret = njs_generate_index_release(vm, generator, index);
        if (njs_slow_path(ret != NJS_OK)) {
            return ret;
        }
    }

    return njs_generate_children_indexes_release(vm, generator, lvalue);
}


static njs_int_t
njs_generate_function_declaration(njs_vm_t *vm, njs_generator_t *generator,
    njs_parser_node_t *node)
{
    njs_int_t                ret;
    njs_variable_t           *var;
    njs_function_t           *function;
    njs_function_lambda_t    *lambda;
    const njs_lexer_entry_t  *lex_entry;

    var = njs_variable_reference(vm, node);
    if (njs_slow_path(var == NULL)) {
        return njs_generate_reference_error(vm, generator, node);
    }

    if (njs_is_function(&var->value)) {
        lambda = njs_function(&var->value)->u.lambda;
    } else {
        lambda = var->value.data.u.lambda;
    }

    lex_entry = njs_lexer_entry(node->u.reference.unique_id);
    if (njs_slow_path(lex_entry == NULL)) {
        return NJS_ERROR;
    }

    ret = njs_generate_function_scope(vm, lambda, node, &lex_entry->name);
    if (njs_slow_path(ret != NJS_OK)) {
        return ret;
    }

    function = njs_function_alloc(vm, lambda);
    if (njs_slow_path(function == NULL)) {
        return NJS_ERROR;
    }

    function->global = njs_function_scope(var->scope)->type == NJS_SCOPE_GLOBAL;
    function->object.shared = 1;
    function->args_count = lambda->nargs - lambda->rest_parameters;

    njs_set_function(&var->value, function);

    return ret;
}


static njs_int_t
njs_generate_function_scope(njs_vm_t *vm, njs_function_lambda_t *lambda,
    njs_parser_node_t *node, const njs_str_t *name)
{
    njs_arr_t          *arr;
    njs_bool_t         module;
    njs_vm_code_t      *code;
    njs_generator_t    generator;
    njs_parser_node_t  *file_node;

    njs_memzero(&generator, sizeof(njs_generator_t));

    node = node->right;

    code = njs_generate_scope(vm, &generator, node->scope, name);
    if (njs_slow_path(code == NULL)) {
        if (!njs_is_error(&vm->retval)) {
            njs_internal_error(vm, "njs_generate_scope() failed");
        }

        return NJS_ERROR;
    }

    module = node->right->scope->module;
    file_node = module ? node->right : node;

    code->file = file_node->scope->file;

    lambda->start = generator.code_start;
    lambda->closures = generator.closures->start;
    lambda->nclosures = generator.closures->items;
    lambda->nlocal = node->scope->items;
    lambda->temp = node->scope->temp;

    if (node->scope->declarations != NULL) {
        arr = node->scope->declarations;
        lambda->declarations = arr->start;
        lambda->ndeclarations = arr->items;
    }

    return NJS_OK;
}


njs_vm_code_t *
njs_generate_scope(njs_vm_t *vm, njs_generator_t *generator,
    njs_parser_scope_t *scope, const njs_str_t *name)
{
    u_char         *p;
    int64_t        nargs;
    njs_uint_t     index;
    njs_vm_code_t  *code;

    generator->code_size = 128;

    p = njs_mp_alloc(vm->mem_pool, generator->code_size);
    if (njs_slow_path(p == NULL)) {
        njs_memory_error(vm);
        return NULL;
    }

    generator->code_start = p;
    generator->code_end = p;

    nargs = njs_generate_lambda_variables(vm, generator, scope->top);
    if (njs_slow_path(nargs < NJS_OK)) {
        return NULL;
    }

    if (vm->codes == NULL) {
        vm->codes = njs_arr_create(vm->mem_pool, 4, sizeof(njs_vm_code_t));
        if (njs_slow_path(vm->codes == NULL)) {
            return NULL;
        }
    }

    index = vm->codes->items;
    code = njs_arr_add(vm->codes);
    if (njs_slow_path(code == NULL)) {
        njs_memory_error(vm);
        return NULL;
    }

    code->lines = NULL;

    if (vm->options.backtrace) {
        code->lines = njs_arr_create(vm->mem_pool, 4,
                                     sizeof(njs_vm_line_num_t));
        if (njs_slow_path(code->lines == NULL)) {
            njs_memory_error(vm);
            return NULL;
        }

        generator->lines = code->lines;
    }

    generator->closures = njs_arr_create(vm->mem_pool, 4, sizeof(njs_index_t));
    if (njs_slow_path(generator->closures == NULL)) {
        return NULL;
    }

    scope->closures = generator->closures;

    if (njs_slow_path(njs_generator(vm, generator, scope->top) != NJS_OK)) {
        return NULL;
    }

    code = njs_arr_item(vm->codes, index);
    code->start = generator->code_start;
    code->end = generator->code_end;
    code->file = scope->file;
    code->name = *name;

    generator->code_size = generator->code_end - generator->code_start;

    return code;
}


static int64_t
njs_generate_lambda_variables(njs_vm_t *vm, njs_generator_t *generator,
    njs_parser_node_t *node)
{
    int64_t                 nargs;
    njs_variable_t          *var;
    njs_rbtree_node_t       *rb_node;
    njs_variable_node_t     *var_node;
    njs_vmcode_arguments_t  *arguments;

    nargs = 0;

    rb_node = njs_rbtree_min(&node->scope->variables);

    while (njs_rbtree_is_there_successor(&node->scope->variables, rb_node)) {
        var_node = (njs_variable_node_t *) rb_node;
        var = var_node->variable;

        if (var == NULL) {
            break;
        }

        if (var->argument) {
            nargs++;
        }

        if (var->arguments_object) {
            njs_generate_code(generator, njs_vmcode_arguments_t, arguments,
                              NJS_VMCODE_ARGUMENTS, 1, NULL);
            arguments->dst = var->index;
        }

        rb_node = njs_rbtree_node_successor(&node->scope->variables, rb_node);
    }

    return nargs;
}


static njs_int_t
njs_generate_return_statement(njs_vm_t *vm, njs_generator_t *generator,
    njs_parser_node_t *node)
{
    njs_int_t                ret;
    njs_index_t              index;
    const njs_str_t          *dest;
    njs_vmcode_return_t      *code;
    njs_generator_patch_t    *patch;
    njs_generator_block_t    *block, *immediate, *top;
    njs_vmcode_try_return_t  *try_return;

    ret = njs_generator(vm, generator, node->right);
    if (njs_slow_path(ret != NJS_OK)) {
        return ret;
    }

    if (node->right != NULL) {
        index = node->right->index;

    } else {
        index = njs_scope_global_index(vm, &njs_value_undefined,
                                       generator->runtime);
    }

    if (njs_slow_path(index == NJS_INDEX_ERROR)) {
        return NJS_ERROR;
    }

    immediate = njs_generate_lookup_block(generator->block, NJS_GENERATOR_TRY,
                                          &no_label);

    if (njs_fast_path(immediate == NULL)) {
        njs_generate_code(generator, njs_vmcode_return_t, code,
                          NJS_VMCODE_RETURN, 1, node);
        code->retval = index;
        node->index = index;

        return NJS_OK;
    }

    if (immediate->type == NJS_GENERATOR_TRY && immediate->exit != NULL) {
        dest = njs_generate_jump_destination(vm, immediate->next,
                                             "break/return",
                                             NJS_GENERATOR_ALL,
                                             &immediate->exit->label,
                                             &return_label);
        if (njs_slow_path(dest == NULL)) {
            return NJS_ERROR;
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
                      NJS_VMCODE_TRY_RETURN, 2, node);
    try_return->retval = index;
    try_return->save = top->index;
    try_return->offset = offsetof(njs_vmcode_try_return_t, offset);

    patch = njs_generate_make_exit_patch(vm, immediate, &return_label,
                                         njs_code_offset(generator, try_return)
                                         + offsetof(njs_vmcode_try_return_t,
                                                    offset));
    if (njs_slow_path(patch == NULL)) {
        return NJS_ERROR;
    }

    return NJS_OK;
}


static njs_int_t
njs_generate_function_call(njs_vm_t *vm, njs_generator_t *generator,
    njs_parser_node_t *node)
{
    njs_int_t                    ret, nargs;
    njs_jump_off_t               func_offset;
    njs_variable_t               *var;
    njs_parser_node_t            *name;
    njs_vmcode_function_frame_t  *func;

    var = NULL;

    if (node->left != NULL) {
        /* Generate function code in function expression. */
        ret = njs_generator(vm, generator, node->left);
        if (njs_slow_path(ret != NJS_OK)) {
            return ret;
        }

        name = node->left;

    } else {
        ret = njs_generate_variable(vm, generator, node, NJS_REFERENCE, &var);
        if (njs_slow_path(ret != NJS_OK)) {
            return ret;
        }

        name = node;
    }

    njs_generate_code(generator, njs_vmcode_function_frame_t, func,
                      NJS_VMCODE_FUNCTION_FRAME, 2, node);
    func_offset = njs_code_offset(generator, func);
    func->ctor = node->ctor;
    func->name = name->index;

    nargs = njs_generate_move_arguments(vm, generator, node);
    if (njs_slow_path(nargs < 0)) {
        return nargs;
    }

    func = njs_code_ptr(generator, njs_vmcode_function_frame_t, func_offset);
    func->nargs = nargs;

    ret = njs_generate_call(vm, generator, node);
    if (njs_fast_path(ret != NJS_OK)) {
        return ret;
    }

    return NJS_OK;
}


static njs_int_t
njs_generate_method_call(njs_vm_t *vm, njs_generator_t *generator,
    njs_parser_node_t *node)
{
    njs_int_t                  ret, nargs;
    njs_jump_off_t             method_offset;
    njs_parser_node_t          *prop;
    njs_vmcode_method_frame_t  *method;

    prop = node->left;

    /* Object. */

    ret = njs_generator(vm, generator, prop->left);
    if (njs_slow_path(ret != NJS_OK)) {
        return ret;
    }

    /* Method name. */

    ret = njs_generator(vm, generator, prop->right);
    if (njs_slow_path(ret != NJS_OK)) {
        return ret;
    }

    njs_generate_code(generator, njs_vmcode_method_frame_t, method,
                      NJS_VMCODE_METHOD_FRAME, 3, prop);
    method_offset = njs_code_offset(generator, method);
    method->ctor = node->ctor;
    method->object = prop->left->index;
    method->method = prop->right->index;

    nargs = njs_generate_move_arguments(vm, generator, node);
    if (njs_slow_path(nargs < 0)) {
        return nargs;
    }

    method = njs_code_ptr(generator, njs_vmcode_method_frame_t, method_offset);
    method->nargs = nargs;

    ret = njs_generate_call(vm, generator, node);
    if (njs_fast_path(ret != NJS_OK)) {
        return ret;
    }

    return NJS_OK;
}


static njs_int_t
njs_generate_call(njs_vm_t *vm, njs_generator_t *generator,
    njs_parser_node_t *node)
{
    njs_index_t                 retval;
    njs_vmcode_function_call_t  *call;

    retval = njs_generate_dest_index(vm, generator, node);
    if (njs_slow_path(retval == NJS_INDEX_ERROR)) {
        return retval;
    }

    node->index = retval;

    njs_generate_code(generator, njs_vmcode_function_call_t, call,
                      NJS_VMCODE_FUNCTION_CALL, 1, node);
    call->retval = retval;

    return NJS_OK;
}


static njs_int_t
njs_generate_move_arguments(njs_vm_t *vm, njs_generator_t *generator,
    njs_parser_node_t *node)
{
    njs_int_t              ret;
    njs_uint_t             nargs;
    njs_parser_node_t      *arg;
    njs_vmcode_move_arg_t  *move_arg;

    nargs = 0;

    for (arg = node->right; arg != NULL; arg = arg->right) {
        ret = njs_generator(vm, generator, arg->left);
        if (njs_slow_path(ret != NJS_OK)) {
            return ret;
        }

        njs_generate_code(generator, njs_vmcode_move_arg_t, move_arg,
                          NJS_VMCODE_MOVE_ARG, 0, node);
        move_arg->src = arg->left->index;
        move_arg->dst = nargs;

        nargs++;
    }

    return nargs;
}


#define njs_generate_code_catch(generator, _code, _exception, node)           \
    do {                                                                      \
            njs_generate_code(generator, njs_vmcode_catch_t, _code,           \
                              NJS_VMCODE_CATCH, 2, node);                     \
            _code->offset = sizeof(njs_vmcode_catch_t);                       \
            _code->exception = _exception;                                    \
    } while (0)


#define njs_generate_code_finally(generator, _code, _retval, _exit, node)     \
    do {                                                                      \
            njs_generate_code(generator, njs_vmcode_finally_t, _code,         \
                              NJS_VMCODE_FINALLY, 1, node);                   \
            _code->retval = _retval;                                          \
            _code->exit_value = _exit;                                        \
            _code->continue_offset = offsetof(njs_vmcode_finally_t,           \
                                              continue_offset);               \
            _code->break_offset = offsetof(njs_vmcode_finally_t,              \
                                           break_offset);                     \
    } while (0)


static njs_int_t
njs_generate_try_statement(njs_vm_t *vm, njs_generator_t *generator,
    njs_parser_node_t *node)
{
    njs_int_t                    ret;
    njs_str_t                    try_cont_label, try_exit_label,
                                 catch_cont_label, catch_exit_label;
    njs_index_t                  exception_index, exit_index, catch_index;
    njs_jump_off_t               try_offset, try_end_offset, catch_offset,
                                 catch_end_offset;
    njs_variable_t               *var;
    const njs_str_t              *dest_label;
    njs_vmcode_catch_t           *catch;
    njs_vmcode_finally_t         *finally;
    njs_vmcode_try_end_t         *try_end, *catch_end;
    njs_generator_patch_t        *patch;
    njs_generator_block_t        *block, *try_block, *catch_block;
    njs_vmcode_try_start_t       *try_start;
    njs_vmcode_try_trampoline_t  *try_break, *try_continue;

    njs_generate_code(generator, njs_vmcode_try_start_t, try_start,
                      NJS_VMCODE_TRY_START, 2, node);
    try_offset = njs_code_offset(generator, try_start);

    exception_index = njs_generate_temp_index_get(vm, generator, node);
    if (njs_slow_path(exception_index == NJS_INDEX_ERROR)) {
        return NJS_ERROR;
    }

    try_start->exception_value = exception_index;

    /*
     * exit_value is used in njs_vmcode_finally to make a decision
     * which way to go after "break", "continue" and "return" instruction
     * inside "try" or "catch" blocks.
     */

    exit_index = njs_generate_temp_index_get(vm, generator, node);
    if (njs_slow_path(exit_index == NJS_INDEX_ERROR)) {
        return NJS_ERROR;
    }

    try_start->exit_value = exit_index;

    ret = njs_generate_start_block(vm, generator, NJS_GENERATOR_TRY, &no_label);
    if (njs_slow_path(ret != NJS_OK)) {
        return ret;
    }

    try_block = generator->block;
    try_block->index = exit_index;

    ret = njs_generator(vm, generator, node->left);
    if (njs_slow_path(ret != NJS_OK)) {
        return ret;
    }

    try_exit_label = undef_label;
    try_cont_label = undef_label;

    njs_generate_code(generator, njs_vmcode_try_end_t, try_end,
                      NJS_VMCODE_TRY_END, 0, NULL);
    try_end_offset = njs_code_offset(generator, try_end);

    if (try_block->exit != NULL) {
        try_exit_label = try_block->exit->label;

        njs_generate_patch_block(vm, generator, try_block->exit);

        njs_generate_code(generator, njs_vmcode_try_trampoline_t, try_break,
                          NJS_VMCODE_TRY_BREAK, 1, NULL);
        try_break->exit_value = exit_index;

        try_break->offset = -sizeof(njs_vmcode_try_end_t);

    } else {
        try_break = NULL;
    }

    if (try_block->continuation != NULL) {
        try_cont_label = try_block->continuation->label;

        njs_generate_patch_block(vm, generator, try_block->continuation);

        njs_generate_code(generator, njs_vmcode_try_trampoline_t, try_continue,
                          NJS_VMCODE_TRY_CONTINUE, 1, NULL);
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

    if (node->token_type == NJS_TOKEN_CATCH) {
        /* A "try/catch" case. */

        var = njs_variable_reference(vm, node->left);
        if (njs_slow_path(var == NULL)) {
            return NJS_ERROR;
        }

        catch_index = node->left->index;

        njs_generate_code_catch(generator, catch, catch_index, node);

        ret = njs_generator(vm, generator, node->right);
        if (njs_slow_path(ret != NJS_OK)) {
            return ret;
        }

        njs_code_set_jump_offset(generator, njs_vmcode_try_end_t, try_offset);

        if (try_block->continuation != NULL || try_block->exit != NULL) {
            njs_generate_code_finally(generator, finally, exception_index,
                                      exit_index, NULL);

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
                if (njs_slow_path(patch == NULL)) {
                    return NJS_ERROR;
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
                    if (njs_slow_path(patch == NULL)) {
                        return NJS_ERROR;
                    }
                }
            }
        }

        /* TODO: release exception variable index. */

    } else {
        if (node->left != NULL) {
            /* A try/catch/finally case. */

            var = njs_variable_reference(vm, node->left->left);
            if (njs_slow_path(var == NULL)) {
                return NJS_ERROR;
            }

            catch_index = node->left->left->index;

            njs_generate_code_catch(generator, catch, catch_index, node);
            catch_offset = njs_code_offset(generator, catch);

            ret = njs_generate_start_block(vm, generator, NJS_GENERATOR_TRY,
                                           &no_label);
            if (njs_slow_path(ret != NJS_OK)) {
                return ret;
            }

            catch_block = generator->block;
            catch_block->index = exit_index;

            ret = njs_generator(vm, generator, node->left->right);
            if (njs_slow_path(ret != NJS_OK)) {
                return ret;
            }

            njs_generate_code(generator, njs_vmcode_try_end_t, catch_end,
                              NJS_VMCODE_TRY_END, 0, node->left->right);
            catch_end_offset = njs_code_offset(generator, catch_end);

            if (catch_block->exit != NULL) {
                catch_exit_label = catch_block->exit->label;

                njs_generate_patch_block(vm, generator, catch_block->exit);

                njs_generate_code(generator, njs_vmcode_try_trampoline_t,
                                  try_break, NJS_VMCODE_TRY_BREAK, 1, NULL);

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
                                  try_continue, NJS_VMCODE_TRY_CONTINUE, 1,
                                  NULL);

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

            njs_generate_code_catch(generator, catch, exception_index, NULL);

            njs_code_set_jump_offset(generator, njs_vmcode_try_end_t,
                                     catch_end_offset);

        } else {
            /* A try/finally case. */

            njs_generate_code_catch(generator, catch, exception_index, NULL);

            catch_block = NULL;
        }

        njs_code_set_jump_offset(generator, njs_vmcode_try_end_t, try_offset);

        ret = njs_generator(vm, generator, node->right);
        if (njs_slow_path(ret != NJS_OK)) {
            return ret;
        }

        njs_generate_code_finally(generator, finally, exception_index,
                                  exit_index, node);

        if (try_block->continuation != NULL
            || (catch_block && catch_block->continuation != NULL))
        {
            dest_label = njs_generate_jump_destination(vm, generator->block,
                                                       "try continue",
                                                       NJS_GENERATOR_LOOP,
                                                       &try_cont_label,
                                                       &catch_cont_label);
            if (njs_slow_path(dest_label == NULL)) {
                return NJS_ERROR;
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
            if (njs_slow_path(patch == NULL)) {
                return NJS_ERROR;
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
            if (njs_slow_path(dest_label == NULL)) {
                return NJS_ERROR;
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
                if (njs_slow_path(patch == NULL)) {
                    return NJS_ERROR;
                }
            }
        }
    }

    return njs_generate_index_release(vm, generator, exception_index);
}


static njs_int_t
njs_generate_throw_statement(njs_vm_t *vm, njs_generator_t *generator,
    njs_parser_node_t *node)
{
    njs_int_t           ret;
    njs_vmcode_throw_t  *throw;

    ret = njs_generator(vm, generator, node->right);

    if (njs_fast_path(ret == NJS_OK)) {
        njs_generate_code(generator, njs_vmcode_throw_t, throw,
                          NJS_VMCODE_THROW, 1, node);

        node->index = node->right->index;
        throw->retval = node->index;
    }

    return ret;
}


static njs_int_t
njs_generate_import_statement(njs_vm_t *vm, njs_generator_t *generator,
    njs_parser_node_t *node)
{
    njs_int_t                 ret;
    njs_index_t               index;
    njs_module_t              *module;
    njs_variable_t            *var;
    njs_parser_node_t         *lvalue, *expr;
    njs_vmcode_object_copy_t  *copy;

    lvalue = node->left;
    expr = node->right;

    var = njs_variable_reference(vm, lvalue);
    if (njs_slow_path(var == NULL)) {
        return NJS_ERROR;
    }

    index = lvalue->index;

    if (expr->left != NULL) {
        ret = njs_generator(vm, generator, expr->left);
        if (njs_slow_path(ret != NJS_OK)) {
            return ret;
        }
    }

    module = (njs_module_t *) expr->index;

    njs_generate_code(generator, njs_vmcode_object_copy_t, copy,
                      NJS_VMCODE_OBJECT_COPY, 2, node);
    copy->retval = index;
    copy->object = module->index;

    return NJS_OK;
}


static njs_int_t
njs_generate_export_statement(njs_vm_t *vm, njs_generator_t *generator,
    njs_parser_node_t *node)
{
    njs_int_t            ret;
    njs_parser_node_t    *obj;
    njs_vmcode_return_t  *code;

    obj = node->right;

    ret = njs_generator(vm, generator, obj);
    if (njs_slow_path(ret != NJS_OK)) {
        return ret;
    }

    njs_generate_code(generator, njs_vmcode_return_t, code,
                      NJS_VMCODE_RETURN, 1, NULL);
    code->retval = obj->index;
    node->index = obj->index;

    return NJS_OK;
}


static njs_index_t
njs_generate_dest_index(njs_vm_t *vm, njs_generator_t *generator,
    njs_parser_node_t *node)
{
    njs_index_t         ret;
    njs_parser_node_t   *dest;
    njs_parser_scope_t  *scope;

    ret = njs_generate_children_indexes_release(vm, generator, node);
    if (njs_slow_path(ret != NJS_OK)) {
        return ret;
    }

    dest = node->dest;

    if (dest != NULL && dest->index != NJS_INDEX_NONE) {
        scope = njs_function_scope(node->scope);

        if (!scope->dest_disable) {
            return dest->index;
        }
    }

    return njs_generate_node_temp_index_get(vm, generator, node);
}


static njs_index_t
njs_generate_object_dest_index(njs_vm_t *vm, njs_generator_t *generator,
    njs_parser_node_t *node)
{
    njs_index_t        index;
    njs_parser_node_t  *dest;

    dest = node->dest;

    if (dest != NULL && dest->index != NJS_INDEX_NONE) {
        index = dest->index;

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


static njs_index_t
njs_generate_temp_index_get(njs_vm_t *vm, njs_generator_t *generator,
    njs_parser_node_t *node)
{
    njs_arr_t           *cache;
    njs_index_t         *last;
    njs_parser_scope_t  *scope;

    cache = generator->index_cache;

    if (cache != NULL && cache->items != 0) {
        last = njs_arr_remove_last(cache);

        njs_thread_log_debug("CACHE %p", *last);

        return *last;
    }

    scope = njs_function_scope(node->scope);
    if (njs_slow_path(scope == NULL)) {
        return NJS_ERROR;
    }

    return njs_scope_index(scope->type, scope->temp++, NJS_LEVEL_TEMP,
                           NJS_VARIABLE_VAR);
}


static njs_int_t
njs_generate_children_indexes_release(njs_vm_t *vm, njs_generator_t *generator,
    njs_parser_node_t *node)
{
    njs_int_t  ret;

    ret = njs_generate_node_index_release(vm, generator, node->left);

    if (njs_fast_path(ret == NJS_OK)) {
        return njs_generate_node_index_release(vm, generator, node->right);
    }

    return ret;
}


static njs_int_t
njs_generate_node_index_release(njs_vm_t *vm, njs_generator_t *generator,
    njs_parser_node_t *node)
{
    if (node != NULL && node->temporary) {
        return njs_generate_index_release(vm, generator, node->index);
    }

    return NJS_OK;
}


static njs_int_t
njs_generate_index_release(njs_vm_t *vm, njs_generator_t *generator,
    njs_index_t index)
{
    njs_arr_t    *cache;
    njs_index_t  *last;

    njs_thread_log_debug("RELEASE %p", index);

    cache = generator->index_cache;

    if (cache == NULL) {
        cache = njs_arr_create(vm->mem_pool, 4, sizeof(njs_value_t *));
        if (njs_slow_path(cache == NULL)) {
            return NJS_ERROR;
        }

        generator->index_cache = cache;
    }

    last = njs_arr_add(cache);
    if (njs_fast_path(last != NULL)) {
        *last = index;
        return NJS_OK;
    }

    return NJS_ERROR;
}


static njs_int_t
njs_generate_global_reference(njs_vm_t *vm, njs_generator_t *generator,
    njs_parser_node_t *node, njs_bool_t exception)
{
    njs_int_t                ret;
    njs_index_t              index;
    njs_value_t              property;
    njs_vmcode_prop_get_t    *prop_get;
    const njs_lexer_entry_t  *lex_entry;

    index = njs_generate_temp_index_get(vm, generator, node);
    if (njs_slow_path(index == NJS_INDEX_ERROR)) {
        return NJS_ERROR;
    }

    njs_generate_code(generator, njs_vmcode_prop_get_t, prop_get,
                 exception ? NJS_VMCODE_GLOBAL_GET: NJS_VMCODE_PROPERTY_GET,
                 3, node);

    prop_get->value = index;

    prop_get->object = njs_scope_global_this_index();
    if (njs_slow_path(prop_get->object == NJS_INDEX_ERROR)) {
        return NJS_ERROR;
    }

    lex_entry = njs_lexer_entry(node->u.reference.unique_id);
    if (njs_slow_path(lex_entry == NULL)) {
        return NJS_ERROR;
    }

    ret = njs_string_set(vm, &property, lex_entry->name.start,
                         lex_entry->name.length);
    if (njs_slow_path(ret != NJS_OK)) {
        return NJS_ERROR;
    }

    prop_get->property = njs_scope_global_index(vm, &property,
                                                generator->runtime);
    if (njs_slow_path(prop_get->property == NJS_INDEX_ERROR)) {
        return NJS_ERROR;
    }

    node->index = index;

    if (!exception) {
        return NJS_OK;
    }

    return njs_generate_reference_error(vm, generator, node);
}


static njs_int_t
njs_generate_reference_error(njs_vm_t *vm, njs_generator_t *generator,
    njs_parser_node_t *node)
{
    njs_vmcode_error_t       *ref_err;
    const njs_lexer_entry_t  *lex_entry;

    if (njs_slow_path(!node->u.reference.not_defined)) {
        njs_internal_error(vm, "variable is not defined but not_defined "
                               "is not set");
        return NJS_ERROR;
    }

    njs_generate_code(generator, njs_vmcode_error_t, ref_err, NJS_VMCODE_ERROR,
                      0, NULL);

    ref_err->type = NJS_OBJ_TYPE_REF_ERROR;
    lex_entry = njs_lexer_entry(node->u.reference.unique_id);
    if (njs_slow_path(lex_entry == NULL)) {
        return NJS_ERROR;
    }

    return njs_name_copy(vm, &ref_err->u.name, &lex_entry->name);
}
