
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) NGINX, Inc.
 */

#include <nxt_auto_config.h>
#include <nxt_types.h>
#include <nxt_clang.h>
#include <nxt_stub.h>
#include <nxt_array.h>
#include <nxt_lvlhsh.h>
#include <nxt_random.h>
#include <nxt_mem_cache_pool.h>
#include <njscript.h>
#include <njs_vm.h>
#include <njs_number.h>
#include <njs_object.h>
#include <njs_regexp.h>
#include <njs_variable.h>
#include <njs_parser.h>
#include <string.h>


typedef nxt_int_t (*njs_parser_operation_t)(njs_vm_t *vm,
    njs_parser_t *parser, njs_token_t token, const void *data);

typedef nxt_int_t (*njs_parser_stack_operation_t)(njs_vm_t *vm,
    njs_parser_t *parser, const void *data);


#define NJS_TOKEN_ANY    NJS_TOKEN_ILLEGAL
#define NJS_PARSER_NODE  ((void *) -1)
#define NJS_PARSER_VOID  ((void *) -2)


typedef struct {
    njs_token_t                  token;
    njs_parser_operation_t       operation;
    const void                   *data;
    const void                   *primed;
} njs_parser_terminal_t;


#define NJS_PARSER_IGNORE_LINE_END  0
#define NJS_PARSER_TEST_LINE_END    1


typedef struct {
    uint8_t                      take_line_end;  /* 1 bit */
    uint8_t                      count;
#if (NXT_SUNC)
    /*
     * SunC supports C99 flexible array members but does not allow
     * static struct's initialization with arbitrary number of members.
     */
    const njs_parser_terminal_t  terminal[9];
#else
    const njs_parser_terminal_t  terminal[];
#endif
} njs_parser_switch_t;


njs_token_t njs_parser_token(njs_parser_t *parser);
static void *njs_parser_stack_pop(njs_parser_t *parser);
static nxt_int_t njs_parser_stack_push(njs_vm_t *vm, njs_parser_t *parser,
    const void *data);

static const void *const  njs_parser_statement[];
static const void *const  njs_parser_expression0[];


/* STUB */
static nxt_int_t                top = -1;
static void                     *stack[1024];
/**/


njs_parser_node_t *
njs_nonrecursive_parser(njs_vm_t *vm, njs_parser_t *parser)
{
    nxt_int_t                     ret;
    njs_token_t                   token;
    njs_parser_stack_operation_t  operation;

    if (top < 0) {
        njs_parser_stack_push(vm, parser, njs_parser_statement);
    }

    token = njs_parser_token(parser);
    if (nxt_slow_path(token <= NJS_TOKEN_ILLEGAL)) {
        /* TODO: NJS_TOKEN_AGAIN */
        return NULL;
    }

    do {
        operation = (njs_parser_stack_operation_t) njs_parser_stack_pop(parser);

        if (operation == NULL) {

            if (parser->lexer->token == NJS_TOKEN_END) {
                return parser->node;
            }

            break;
        }

        ret = operation(vm, parser, njs_parser_stack_pop(parser));

    } while (ret == NXT_OK);

    nxt_thread_log_error(NXT_LOG_ERR, "unexpected token");

    return NULL;
}


njs_token_t
njs_parser_token(njs_parser_t *parser)
{
    njs_token_t  token;

    do {
        token = njs_lexer_token(parser->lexer);

        if (nxt_slow_path(token <= NJS_TOKEN_ILLEGAL)) {
            return token;
        }

    } while (nxt_slow_path(token == NJS_TOKEN_LINE_END));

    return token;
}


static void *
njs_parser_stack_pop(njs_parser_t *parser)
{
    if (top < 0) {
        return NULL;
    }

    return stack[top--];
}


static nxt_int_t
njs_parser_stack_push(njs_vm_t *vm, njs_parser_t *parser, const void *data)
{
    void *const  *next;

    next = data;

    if (next != NULL) {

        do {
            top++;

            if (*next != NJS_PARSER_NODE) {
                stack[top] = *next;

            } else {
                stack[top] = parser->node;
            }

            next++;

        } while (*next != NULL);
    }

    return NXT_OK;
}


static nxt_int_t
njs_parser_switch(njs_vm_t *vm, njs_parser_t *parser, void *data)
{
    nxt_int_t                    ret;
    nxt_uint_t                   n;
    njs_token_t                  token;
    njs_parser_switch_t          *swtch;
    const njs_parser_terminal_t  *term;

    swtch = data;
    token = parser->lexer->token;

    n = swtch->count;
    term = swtch->terminal;

    do {
        if (token == term->token || term->token == NJS_TOKEN_ANY) {
            ret = term->operation(vm, parser, token, term->data);
            if (nxt_slow_path(ret != NXT_OK)) {
                return NXT_ERROR;
            }

            ret = njs_parser_stack_push(vm, parser, term->primed);
            if (nxt_slow_path(ret != NXT_OK)) {
                return NXT_ERROR;
            }

            if (term->token != NJS_TOKEN_ANY) {
                token = njs_parser_token(parser);
                if (nxt_slow_path(token <= NJS_TOKEN_ILLEGAL)) {
                    /* TODO: NJS_TOKEN_AGAIN */
                    return NXT_ERROR;
                }
            }

            return NXT_OK;
        }

        term++;
        n--;

    } while (n != 0);

    return NXT_OK;
}


static nxt_int_t
njs_parser_statement_semicolon(njs_vm_t *vm, njs_parser_t *parser,
    void *data)
{
    njs_token_t        token;
    njs_parser_node_t  *node;

    node = data;

    switch (parser->lexer->token) {

    case NJS_TOKEN_SEMICOLON:
        token = njs_parser_token(parser);
        if (nxt_slow_path(token <= NJS_TOKEN_ILLEGAL)) {
            /* TODO: NJS_TOKEN_AGAIN */
            return NXT_ERROR;
        }

        /* Fall through. */

    case NJS_TOKEN_END:

        node->right = parser->node;
        parser->node = node;

        return NXT_OK;

    default:
        break;
    }

    return NXT_ERROR;
}


static nxt_int_t
njs_parser_test_token(njs_vm_t *vm, njs_parser_t *parser, void *data)
{
    njs_token_t  token;

    token = (njs_token_t) data;

    if (parser->lexer->token == token) {
        token = njs_parser_token(parser);
        if (nxt_slow_path(token <= NJS_TOKEN_ILLEGAL)) {
            /* TODO: NJS_TOKEN_AGAIN */
            return NXT_ERROR;
        }

        return NXT_OK;
    }

    vm->exception = &njs_exception_syntax_error;

    return NXT_ERROR;
}


static nxt_int_t
njs_parser_node(njs_vm_t *vm, njs_parser_t *parser, njs_token_t token,
    const void *data)
{
    njs_parser_node_t  *node;

    token = (njs_token_t) data;

    node = njs_parser_node_alloc(vm);

    if (nxt_fast_path(node != NULL)) {
        node->token = token;
        node->left = parser->node;
        parser->node = node;

        return NXT_OK;
    }

    return NXT_ERROR;
}


static nxt_int_t
njs_parser_noop(njs_vm_t *vm, njs_parser_t *parser, njs_token_t token,
    const void *data)
{
    return NXT_OK;
}


static nxt_int_t
njs_parser_link_left(njs_vm_t *vm, njs_parser_t *parser, const void *data)
{
    njs_parser_node_t  *node;

    node = (njs_parser_node_t *) data;

    node->left = parser->node;
    parser->node = node;

    return NXT_OK;
}


static nxt_int_t
njs_parser_link_right(njs_vm_t *vm, njs_parser_t *parser, const void *data)
{
    njs_parser_node_t  *node;

    node = (njs_parser_node_t *) data;

    node->right = parser->node;
    parser->node = node;

    return NXT_OK;
}


static nxt_int_t
njs_parser_condition_expression(njs_vm_t *vm, njs_parser_t *parser,
    njs_token_t token, const void *data)
{
    njs_parser_node_t  *node;

    node = njs_parser_node_alloc(vm);

    if (nxt_fast_path(node != NULL)) {
        node->token = token;
        node->left = parser->node;
        parser->node = node;
        parser->code_size += sizeof(njs_vmcode_cond_jump_t)
                             + sizeof(njs_vmcode_move_t)
                             + sizeof(njs_vmcode_jump_t)
                             + sizeof(njs_vmcode_move_t);
        parser->branch = 1;

        return NXT_OK;
    }

    return NXT_ERROR;
}


static nxt_int_t
njs_parser_binary_expression(njs_vm_t *vm, njs_parser_t *parser,
    njs_token_t token, const void *data)
{
    njs_parser_node_t       *node;
    njs_vmcode_operation_t  operation;

    operation = (njs_vmcode_operation_t) data;

    node = njs_parser_node_alloc(vm);

    if (nxt_fast_path(node != NULL)) {
        node->token = token;
        node->u.operation = operation;
        node->left = parser->node;
        parser->node = node;
        parser->code_size += sizeof(njs_vmcode_3addr_t);

        return NXT_OK;
    }

    return NXT_ERROR;
}


static nxt_int_t
njs_parser_post_unary_expression(njs_vm_t *vm, njs_parser_t *parser,
    njs_token_t token, const void *data)
{
    njs_parser_node_t       *node;
    njs_vmcode_operation_t  operation;

    operation = (njs_vmcode_operation_t) data;

    node = njs_parser_node_alloc(vm);

    if (nxt_fast_path(node != NULL)) {
        node->token = token;
        node->u.operation = operation;
        node->left = parser->node;
        parser->node = node;
        parser->code_size += sizeof(njs_vmcode_3addr_t);

        return NXT_OK;
    }

    return NXT_ERROR;
}


static nxt_int_t
njs_parser_unary_expression0(njs_vm_t *vm, njs_parser_t *parser,
    njs_token_t token, const void *data)
{
    njs_parser_node_t       *node;
    njs_vmcode_operation_t  operation;

    operation = (njs_vmcode_operation_t) data;

    node = njs_parser_node_alloc(vm);

    if (nxt_fast_path(node != NULL)) {
        node->token = token;
        node->u.operation = operation;
        parser->node = node;
        parser->code_size += sizeof(njs_vmcode_3addr_t);

        return NXT_OK;
    }

    return NXT_ERROR;
}


static nxt_int_t
njs_parser_unary_expression(njs_vm_t *vm, njs_parser_t *parser,
    njs_token_t token, const void *data)
{
    njs_parser_node_t       *node;
    njs_vmcode_operation_t  operation;

    operation = (njs_vmcode_operation_t) data;

    node = njs_parser_node_alloc(vm);

    if (nxt_fast_path(node != NULL)) {
        node->token = token;
        node->u.operation = operation;
        parser->node = node;
        parser->code_size += sizeof(njs_vmcode_2addr_t);

        return NXT_OK;
    }

    return NXT_ERROR;
}


static nxt_int_t
njs_parser_unary_plus_expression(njs_vm_t *vm, njs_parser_t *parser,
    njs_token_t token, const void *data)
{
    return njs_parser_unary_expression(vm, parser,
                                       NJS_TOKEN_UNARY_PLUS, data);
}


static nxt_int_t
njs_parser_unary_plus_link(njs_vm_t *vm, njs_parser_t *parser,
    const void *data)
{
    njs_parser_node_t  *node;

    node = (njs_parser_node_t *) data;

    /* Skip the unary plus of number. */

    if (parser->node->token != NJS_TOKEN_NUMBER) {
        node->left = parser->node;
        parser->node = node;
    }

    return NXT_OK;
}


static nxt_int_t
njs_parser_unary_negation_expression(njs_vm_t *vm, njs_parser_t *parser,
    njs_token_t token, const void *data)
{
    return njs_parser_unary_expression(vm, parser,
                                       NJS_TOKEN_UNARY_NEGATION, data);
}


static nxt_int_t
njs_parser_unary_negative_link(njs_vm_t *vm, njs_parser_t *parser,
    void *data)
{
    double             num;
    njs_parser_node_t  *node;

    node = data;

    if (parser->node->token == NJS_TOKEN_NUMBER) {
        /* Optimization of common negative number. */
        node = parser->node;
        num = -node->u.value.data.u.number;
        node->u.value.data.u.number = num;
        node->u.value.data.truth = njs_is_number_true(num);

    } else {
        node->left = parser->node;
        parser->node = node;
    }

    return NXT_OK;
}


static nxt_int_t
njs_parser_name_expression(njs_vm_t *vm, njs_parser_t *parser,
    njs_token_t token, const void *data)
{
    nxt_uint_t         level;
    njs_extern_t       *ext;
    njs_variable_t     *var;
    njs_parser_node_t  *node;

    node = njs_parser_node_alloc(vm);

    if (nxt_fast_path(node != NULL)) {
        ext = njs_parser_external(vm, parser);

        if (ext != NULL) {
            node->token = NJS_TOKEN_EXTERNAL;
            node->u.value.type = NJS_EXTERNAL;
            node->u.value.data.truth = 1;
            node->index = (njs_index_t) ext;

        } else {
            node->token = token;

            var = njs_parser_variable(vm, parser, &level);
            if (nxt_slow_path(var == NULL)) {
                return NJS_TOKEN_ERROR;
            }

            switch (var->state) {

            case NJS_VARIABLE_CREATED:
                var->state = NJS_VARIABLE_PENDING;
                parser->code_size += sizeof(njs_vmcode_1addr_t);
                break;

            case NJS_VARIABLE_PENDING:
                var->state = NJS_VARIABLE_USED;
                parser->code_size += sizeof(njs_vmcode_1addr_t);
                break;

            case NJS_VARIABLE_USED:
                parser->code_size += sizeof(njs_vmcode_1addr_t);
                break;

            case NJS_VARIABLE_SET:
            case NJS_VARIABLE_DECLARED:
                break;
            }

            node->lvalue = NJS_LVALUE_ENABLED;
            node->u.variable = var;
        }
    }

    parser->node = node;

    return NXT_OK;
}


static nxt_int_t
njs_parser_var_name(njs_vm_t *vm, njs_parser_t *parser, void *data)
{
    /* TODO disable NJS_TOKEN_EXTERNAL */
    return njs_parser_name_expression(vm, parser, NJS_TOKEN_NAME, data);
}


static nxt_int_t
njs_parser_this_expression(njs_vm_t *vm, njs_parser_t *parser,
    njs_token_t token, const void *data)
{
    njs_parser_node_t  *node;

    node = njs_parser_node_alloc(vm);

    if (nxt_fast_path(node != NULL)) {
        node->token = token;
        node->index = NJS_INDEX_THIS;
        parser->node = node;

        return NXT_OK;
    }

    return NXT_ERROR;
}


static nxt_int_t
njs_parser_string_expression(njs_vm_t *vm, njs_parser_t *parser,
    njs_token_t token, const void *data)
{
    nxt_int_t          ret;
    njs_parser_node_t  *node;

    node = njs_parser_node_alloc(vm);

    if (nxt_fast_path(node != NULL)) {
        node->token = token;

        ret = njs_parser_string_create(vm, &node->u.value);
        if (nxt_slow_path(ret != NXT_OK)) {
            return NJS_TOKEN_ERROR;
        }

        parser->node = node;

        return NXT_OK;
    }

    return NXT_ERROR;
}


static nxt_int_t
njs_parser_number_expression(njs_vm_t *vm, njs_parser_t *parser,
    njs_token_t token, const void *data)
{
    double             num;
    njs_parser_node_t  *node;

    node = njs_parser_node_alloc(vm);

    if (nxt_fast_path(node != NULL)) {
        node->token = token;
        num = parser->lexer->number;
        node->u.value.data.u.number = num;
        node->u.value.type = NJS_NUMBER;
        node->u.value.data.truth = njs_is_number_true(num);
        parser->node = node;

        return NXT_OK;
    }

    return NXT_ERROR;
}


static nxt_int_t
njs_parser_boolean_expression(njs_vm_t *vm, njs_parser_t *parser,
    njs_token_t token, const void *data)
{
    njs_parser_node_t  *node;

    node = njs_parser_node_alloc(vm);

    if (nxt_fast_path(node != NULL)) {
        node->token = token;
        node->u.value = (parser->lexer->number == 0.0) ? njs_value_false:
                                                         njs_value_true;
        parser->node = node;

        return NXT_OK;
    }

    return NXT_ERROR;
}


static nxt_int_t
njs_parser_undefined_expression(njs_vm_t *vm, njs_parser_t *parser,
    njs_token_t token, const void *data)
{
    njs_parser_node_t  *node;

    node = njs_parser_node_alloc(vm);

    if (nxt_fast_path(node != NULL)) {
        node->token = token;
        node->u.value = njs_value_void;
        parser->node = node;

        return NXT_OK;
    }

    return NXT_ERROR;
}


static nxt_int_t
njs_parser_null_expression(njs_vm_t *vm, njs_parser_t *parser,
    njs_token_t token, const void *data)
{
    njs_parser_node_t  *node;

    node = njs_parser_node_alloc(vm);

    if (nxt_fast_path(node != NULL)) {
        node->token = token;
        node->u.value = njs_value_null;
        parser->node = node;

        return NXT_OK;
    }

    return NXT_ERROR;
}


static nxt_int_t
njs_parser_syntax_error(njs_vm_t *vm, njs_parser_t *parser,
    njs_token_t token, const void *data)
{
    vm->exception = &njs_exception_syntax_error;

    return NXT_ERROR;
}


/*
 * The variables and literal values.
 *
 * VALUE = "(" EXPRESSION ")"
 *         [ NAME create_node ]
 *         [ "this" create_node ]
 *         [ STRING create_node ]
 *         [ NUMBER create_node ]
 *         [ BOOLEAN create_node ]
 *         [ "undefined" create_node ]
 *         [ "null" create_node ]
 *         ERROR
 */

static const void *const  njs_parser_grouping_expression[] = {
    (void *) NJS_TOKEN_CLOSE_PARENTHESIS, (void *) njs_parser_test_token,
    &njs_parser_expression0, (void *) njs_parser_stack_push,
    NULL,
};


static const njs_parser_switch_t  njs_parser_value_expression_switch = {
    NJS_PARSER_IGNORE_LINE_END,
    9, {
        { NJS_TOKEN_OPEN_PARENTHESIS, njs_parser_noop, NULL,
          &njs_parser_grouping_expression },

        { NJS_TOKEN_NAME, njs_parser_name_expression, NULL, NULL },
        { NJS_TOKEN_THIS, njs_parser_this_expression, NULL, NULL },
        { NJS_TOKEN_STRING, njs_parser_string_expression, NULL, NULL },
        { NJS_TOKEN_NUMBER, njs_parser_number_expression, NULL, NULL },
        { NJS_TOKEN_BOOLEAN, njs_parser_boolean_expression, NULL, NULL },

        { NJS_TOKEN_UNDEFINED,
          njs_parser_undefined_expression, NULL, NULL },
        { NJS_TOKEN_NULL, njs_parser_null_expression, NULL, NULL },

        { NJS_TOKEN_ANY, njs_parser_syntax_error, NULL, NULL },
    }
};


#if 0

static const void *const  njs_parser_value_expression[] = {
    &njs_parser_value_expression_switch, (void *) njs_parser_switch,
    NULL,
};

#endif


/*
 * The postfix increment and decrement operations.
 *
 * POSTFIX_INC_DEC = VALUE [ "++" create_node ]
 *                   VALUE [ "--" create_node ]
 *                   <>
 */


static const njs_parser_switch_t  njs_parser_post_inc_dec_expression_switch = {
    NJS_PARSER_IGNORE_LINE_END,
    2, {
        { NJS_TOKEN_INCREMENT,
          njs_parser_post_unary_expression,
          (void *) njs_vmcode_post_increment, NULL },

        { NJS_TOKEN_DECREMENT,
          njs_parser_post_unary_expression,
          (void *) njs_vmcode_post_decrement, NULL },
    }
};


static const void *const  njs_parser_post_inc_dec_expression[] = {
    &njs_parser_post_inc_dec_expression_switch, (void *) njs_parser_switch,
    &njs_parser_value_expression_switch, (void *) njs_parser_switch,
    NULL,
};


/*
 * The prefix increment and decrement operations.
 *
 * PREFIX_INC_DEC = [ "++" create_node ] POSTFIX_INC_DEC link_left
 *                  [ "--" create_node ] POSTFIX_INC_DEC link_left
 *                  <>                   POSTFIX_INC_DEC
 */

static const void *const  njs_parser_inc_dec_expression_primed[] = {
    NJS_PARSER_NODE, (void *) njs_parser_link_left,
    &njs_parser_value_expression_switch, (void *) njs_parser_switch,
    NULL,
};


static const njs_parser_switch_t  njs_parser_inc_dec_expression_switch = {
    NJS_PARSER_IGNORE_LINE_END,
    3, {
        { NJS_TOKEN_INCREMENT,
          njs_parser_unary_expression0, (void *) njs_vmcode_increment,
          njs_parser_inc_dec_expression_primed },

        { NJS_TOKEN_DECREMENT,
          njs_parser_unary_expression0, (void *) njs_vmcode_decrement,
          njs_parser_inc_dec_expression_primed },

        { NJS_TOKEN_ANY, njs_parser_noop, NULL,
          njs_parser_post_inc_dec_expression },
    }
};


static const void *const  njs_parser_inc_dec_expression[] = {
    &njs_parser_inc_dec_expression_switch, (void *) njs_parser_switch,
    NULL,
};


/*
 * The unary operations.
 *
 * UNARY = [ "+"      create_node ] UNARY   plus_link_left
 *         [ "-"      create_node ] UNARY   negation_link_left
 *         [ "!"      create_node ] UNARY   link_left
 *         [ "~"      create_node ] UNARY   link_left
 *         [ "typeof" create_node ] UNARY   link_left
 *         [ "void"   create_node ] UNARY   link_left
 *         [ "delete" create_node ] UNARY   link_left
 *         <>                       INC_DEC
 */

static const njs_parser_switch_t  njs_parser_unary_expression_switch;

static const void *const  njs_parser_unary_plus_expression_primed[] = {
    NJS_PARSER_NODE, (void *) njs_parser_unary_plus_link,
    &njs_parser_unary_expression_switch, (void *) njs_parser_switch,
    NULL,
};


static const void *const  njs_parser_unary_negative_expression_primed[] = {
    NJS_PARSER_NODE, (void *) njs_parser_unary_negative_link,
    &njs_parser_unary_expression_switch, (void *) njs_parser_switch,
    NULL,
};


static const void *const  njs_parser_unary_expression_primed[] = {
    NJS_PARSER_NODE, (void *) njs_parser_link_left,
    &njs_parser_unary_expression_switch, (void *) njs_parser_switch,
    NULL,
};


static const njs_parser_switch_t  njs_parser_unary_expression_switch = {
    NJS_PARSER_TEST_LINE_END,
    8, {
        { NJS_TOKEN_ADDITION,
          njs_parser_unary_plus_expression, (void *) njs_vmcode_unary_plus,
          njs_parser_unary_plus_expression_primed },

        { NJS_TOKEN_SUBSTRACTION,
          njs_parser_unary_negation_expression,
          (void *) njs_vmcode_unary_negation,
          njs_parser_unary_negative_expression_primed },

        { NJS_TOKEN_LOGICAL_NOT,
          njs_parser_unary_expression, (void *) njs_vmcode_logical_not,
          njs_parser_unary_expression_primed },

        { NJS_TOKEN_BITWISE_NOT,
          njs_parser_unary_expression, (void *) njs_vmcode_bitwise_not,
          njs_parser_unary_expression_primed },

        { NJS_TOKEN_TYPEOF,
          njs_parser_unary_expression, (void *) njs_vmcode_typeof,
          njs_parser_unary_expression_primed },

        { NJS_TOKEN_VOID,
          njs_parser_unary_expression, (void *) njs_vmcode_void,
          njs_parser_unary_expression_primed },

        { NJS_TOKEN_DELETE,
          njs_parser_unary_expression, (void *) njs_vmcode_delete,
          njs_parser_unary_expression_primed },

        { NJS_TOKEN_ANY, njs_parser_noop, NULL,
          njs_parser_inc_dec_expression },
    }
};


/*
 * The left associative multiplication, division and remainder operations.
 *
 * MULTIPLICATION  =                     UNARY            MULTIPLICATION'
 * MULTIPLICATION' = [ "*" create_node ] UNARY link_right MULTIPLICATION'
 *                   [ "/" create_node ] UNARY link_right MULTIPLICATION'
 *                   [ "%" create_node ] UNARY link_right MULTIPLICATION'
 *                   <>
 */

static const njs_parser_switch_t  njs_parser_multiplicative_expression_switch;

static const void *const  njs_parser_multiplicative_expression_primed[] = {
    &njs_parser_multiplicative_expression_switch, (void *) njs_parser_switch,
    NJS_PARSER_NODE, (void *) njs_parser_link_right,
    &njs_parser_unary_expression_switch, (void *) njs_parser_switch,
    NULL,
};


static const njs_parser_switch_t
    njs_parser_multiplicative_expression_switch =
{
    NJS_PARSER_TEST_LINE_END,
    3, {
        { NJS_TOKEN_MULTIPLICATION,
          njs_parser_binary_expression, (void *) njs_vmcode_multiplication,
          njs_parser_multiplicative_expression_primed },

        { NJS_TOKEN_DIVISION,
          njs_parser_binary_expression, (void *) njs_vmcode_division,
          njs_parser_multiplicative_expression_primed },

        { NJS_TOKEN_REMAINDER,
          njs_parser_binary_expression, (void *) njs_vmcode_remainder,
          njs_parser_multiplicative_expression_primed },
    }
};


static const void *const  njs_parser_multiplicative_expression[] = {
    &njs_parser_multiplicative_expression_switch, (void *) njs_parser_switch,
    &njs_parser_unary_expression_switch, (void *) njs_parser_switch,
    NULL,
};


/*
 * The left associative addition and substraction operations.
 *
 * ADDITION  =                     MULTIPLICATION            ADDITION'
 * ADDITION' = [ "+" create_node ] MULTIPLICATION link_right ADDITION'
 *             [ "-" create_node ] MULTIPLICATION link_right ADDITION'
 *             <>
 */

static const njs_parser_switch_t  njs_parser_additive_expression_switch;

static const void *const  njs_parser_additive_expression_primed[] = {
    &njs_parser_additive_expression_switch, (void *) njs_parser_switch,
    NJS_PARSER_NODE, (void *) njs_parser_link_right,
    njs_parser_multiplicative_expression, (void *) njs_parser_stack_push,
    NULL,
};


static const njs_parser_switch_t  njs_parser_additive_expression_switch = {
    NJS_PARSER_TEST_LINE_END,
    2, {
        { NJS_TOKEN_ADDITION,
          njs_parser_binary_expression, (void *) njs_vmcode_addition,
          njs_parser_additive_expression_primed },

        { NJS_TOKEN_SUBSTRACTION,
          njs_parser_binary_expression, (void *) njs_vmcode_substraction,
          njs_parser_additive_expression_primed },
    }
};


static const void *const  njs_parser_additive_expression[] = {
    &njs_parser_additive_expression_switch, (void *) njs_parser_switch,
    njs_parser_multiplicative_expression, (void *) njs_parser_stack_push,
    NULL,
};


/*
 * The left associative bitwise shift operations.
 *
 * BITWISE_SHIFT  =                       ADDITION            BITWISE_SHIFT'
 * BITWISE_SHIFT' = [ "<<"  create_node ] ADDITION link_right BITWISE_SHIFT'
 *                  [ ">>"  create_node ] ADDITION link_right BITWISE_SHIFT'
 *                  [ ">>>" create_node ] ADDITION link_right BITWISE_SHIFT'
 *                  <>
 */

static const njs_parser_switch_t  njs_parser_bitwise_shift_expression_switch;

static const void *const  njs_parser_bitwise_shift_expression_primed[] = {
    &njs_parser_bitwise_shift_expression_switch, (void *) njs_parser_switch,
    NJS_PARSER_NODE, (void *) njs_parser_link_right,
    &njs_parser_additive_expression, (void *) njs_parser_stack_push,
    NULL,
};


static const njs_parser_switch_t  njs_parser_bitwise_shift_expression_switch = {
    NJS_PARSER_TEST_LINE_END,
    3, {
        { NJS_TOKEN_LEFT_SHIFT,
          njs_parser_binary_expression, (void *) njs_vmcode_left_shift,
          njs_parser_bitwise_shift_expression_primed },

        { NJS_TOKEN_RIGHT_SHIFT,
          njs_parser_binary_expression, (void *) njs_vmcode_right_shift,
          njs_parser_bitwise_shift_expression_primed },

        { NJS_TOKEN_UNSIGNED_RIGHT_SHIFT,
          njs_parser_binary_expression,
          (void *) njs_vmcode_unsigned_right_shift,
          njs_parser_bitwise_shift_expression_primed },
    }
};


static const void *const  njs_parser_bitwise_shift_expression[] = {
    &njs_parser_bitwise_shift_expression_switch, (void *) njs_parser_switch,
    njs_parser_additive_expression, (void *) njs_parser_stack_push,
    NULL,
};


/*
 * The left associative relational operations.
 *
 * RELATIONAL  =                      BITWISE_SHIFT            RELATIONAL'
 * RELATIONAL' = [ "<   create_node ] BITWISE_SHIFT link_right RELATIONAL'
 *               [ "<=" create_node ] BITWISE_SHIFT link_right RELATIONAL'
 *               [ ">"  create_node ] BITWISE_SHIFT link_right RELATIONAL'
 *               [ ">=" create_node ] BITWISE_SHIFT link_right RELATIONAL'
 *               [ "in" create_node ] BITWISE_SHIFT link_right RELATIONAL'
 *               <>
 */

static const njs_parser_switch_t  njs_parser_relational_expression_switch;

static const void *const  njs_parser_relational_expression_primed[] = {
    &njs_parser_relational_expression_switch, (void *) njs_parser_switch,
    NJS_PARSER_NODE, (void *) njs_parser_link_right,
    &njs_parser_bitwise_shift_expression, (void *) njs_parser_stack_push,
    NULL,
};


static const njs_parser_switch_t  njs_parser_relational_expression_switch = {
    NJS_PARSER_TEST_LINE_END,
    6, {
        { NJS_TOKEN_LESS,
          njs_parser_binary_expression, (void *) njs_vmcode_less,
          njs_parser_relational_expression_primed },

        { NJS_TOKEN_LESS_OR_EQUAL,
          njs_parser_binary_expression, (void *) njs_vmcode_less_or_equal,
          njs_parser_relational_expression_primed },

        { NJS_TOKEN_GREATER,
          njs_parser_binary_expression, (void *) njs_vmcode_greater,
          njs_parser_relational_expression_primed },

        { NJS_TOKEN_GREATER_OR_EQUAL,
          njs_parser_binary_expression, (void *) njs_vmcode_greater_or_equal,
          njs_parser_relational_expression_primed },

        { NJS_TOKEN_IN,
          njs_parser_binary_expression, (void *) njs_vmcode_property_in,
          njs_parser_relational_expression_primed },

        { NJS_TOKEN_INSTANCEOF,
          njs_parser_binary_expression, (void *) njs_vmcode_instance_of,
          njs_parser_relational_expression_primed },
    }
};


static const void *const  njs_parser_relational_expression[] = {
    &njs_parser_relational_expression_switch, (void *) njs_parser_switch,
    njs_parser_bitwise_shift_expression, (void *) njs_parser_stack_push,
    NULL,
};


/*
 * The left associative equality operations.
 *
 * EQUALITY  =                       RELATION            EQUALITY'
 * EQUALITY' = [ "=="  create_node ] RELATION link_right EQUALITY'
 *             [ "!="  create_node ] RELATION link_right EQUALITY'
 *             [ "===" create_node ] RELATION link_right EQUALITY'
 *             [ "!==" create_node ] RELATION link_right EQUALITY'
 *             <>
 */

static const njs_parser_switch_t  njs_parser_equality_expression_switch;

static const void *const  njs_parser_equality_expression_primed[] = {
    &njs_parser_equality_expression_switch, (void *) njs_parser_switch,
    NJS_PARSER_NODE, (void *) njs_parser_link_right,
    &njs_parser_relational_expression, (void *) njs_parser_stack_push,
    NULL,
};


static const njs_parser_switch_t  njs_parser_equality_expression_switch = {
    NJS_PARSER_TEST_LINE_END,
    4, {
        { NJS_TOKEN_EQUAL,
          njs_parser_binary_expression, (void *) njs_vmcode_equal,
          njs_parser_equality_expression_primed },

        { NJS_TOKEN_NOT_EQUAL,
          njs_parser_binary_expression, (void *) njs_vmcode_not_equal,
          njs_parser_equality_expression_primed },

        { NJS_TOKEN_STRICT_EQUAL,
          njs_parser_binary_expression, (void *) njs_vmcode_strict_equal,
          njs_parser_equality_expression_primed },

        { NJS_TOKEN_STRICT_NOT_EQUAL,
          njs_parser_binary_expression, (void *) njs_vmcode_strict_not_equal,
          njs_parser_equality_expression_primed },
    }
};


static const void *const  njs_parser_equality_expression[] = {
    &njs_parser_equality_expression_switch, (void *) njs_parser_switch,
    njs_parser_relational_expression, (void *) njs_parser_stack_push,
    NULL,
};


/*
 * The left associative bitwise AND.
 *
 * BITWISE_AND  =                      EQUALITY            BITWISE_AND'
 * BITWISE_AND' = [ "&"  create_node ] EQUALITY link_right BITWISE_AND'
 *                <>
 */

static const njs_parser_switch_t  njs_parser_bitwise_and_expression_switch;

static const void *const  njs_parser_bitwise_and_expression_primed[] = {
    &njs_parser_bitwise_and_expression_switch, (void *) njs_parser_switch,
    NJS_PARSER_NODE, (void *) njs_parser_link_right,
    &njs_parser_bitwise_shift_expression, (void *) njs_parser_stack_push,
    NULL,
};


static const njs_parser_switch_t  njs_parser_bitwise_and_expression_switch = {
    NJS_PARSER_TEST_LINE_END,
    1, {
        { NJS_TOKEN_BITWISE_AND,
          njs_parser_binary_expression, (void *) njs_vmcode_bitwise_and,
          njs_parser_bitwise_and_expression_primed },
    }
};


static const void *const  njs_parser_bitwise_and_expression[] = {
    &njs_parser_bitwise_and_expression_switch, (void *) njs_parser_switch,
    njs_parser_equality_expression, (void *) njs_parser_stack_push,
    NULL,
};


/*
 * The left associative bitwise XOR.
 *
 * BITWISE_XOR  =                      BITWISE_AND            BITWISE_XOR'
 * BITWISE_XOR' = [ "^"  create_node ] BITWISE_AND link_right BITWISE_XOR'
 *                <>
 */

static const njs_parser_switch_t  njs_parser_bitwise_xor_expression_switch;

static const void *const  njs_parser_bitwise_xor_expression_primed[] = {
    &njs_parser_bitwise_xor_expression_switch, (void *) njs_parser_switch,
    NJS_PARSER_NODE, (void *) njs_parser_link_right,
    &njs_parser_bitwise_and_expression, (void *) njs_parser_stack_push,
    NULL,
};


static const njs_parser_switch_t  njs_parser_bitwise_xor_expression_switch = {
    NJS_PARSER_TEST_LINE_END,
    1, {
        { NJS_TOKEN_BITWISE_XOR,
          njs_parser_binary_expression, (void *) njs_vmcode_bitwise_xor,
          njs_parser_bitwise_xor_expression_primed },
    }
};


static const void *const  njs_parser_bitwise_xor_expression[] = {
    &njs_parser_bitwise_xor_expression_switch, (void *) njs_parser_switch,
    njs_parser_bitwise_and_expression, (void *) njs_parser_stack_push,
    NULL,
};


/*
 * The left associative bitwise OR.
 *
 * BITWISE_OR  =                      BITWISE_XOR            BITWISE_OR'
 * BITWISE_OR' = [ "|"  create_node ] BITWISE_XOR link_right BITWISE_OR'
 *               <>
 */

static const njs_parser_switch_t  njs_parser_bitwise_or_expression_switch;

static const void *const  njs_parser_bitwise_or_expression_primed[] = {
    &njs_parser_bitwise_or_expression_switch, (void *) njs_parser_switch,
    NJS_PARSER_NODE, (void *) njs_parser_link_right,
    &njs_parser_bitwise_xor_expression, (void *) njs_parser_stack_push,
    NULL,
};


static const njs_parser_switch_t  njs_parser_bitwise_or_expression_switch = {
    NJS_PARSER_TEST_LINE_END,
    1, {
        { NJS_TOKEN_BITWISE_OR,
          njs_parser_binary_expression, (void *) njs_vmcode_bitwise_or,
          njs_parser_bitwise_or_expression_primed },
    }
};


static const void *const  njs_parser_bitwise_or_expression[] = {
    &njs_parser_bitwise_or_expression_switch, (void *) njs_parser_switch,
    njs_parser_bitwise_xor_expression, (void *) njs_parser_stack_push,
    NULL,
};


/*
 * The left associative logical AND.
 *
 * LOGICAL_AND  =                       BITWISE_OR            LOGICAL_AND'
 * LOGICAL_AND' = [ "&&"  create_node ] BITWISE_OR link_right LOGICAL_AND'
 *                <>
 */

static const njs_parser_switch_t  njs_parser_logical_and_expression_switch;

static const void *const  njs_parser_logical_and_expression_primed[] = {
    &njs_parser_logical_and_expression_switch, (void *) njs_parser_switch,
    NJS_PARSER_NODE, (void *) njs_parser_link_right,
    &njs_parser_bitwise_or_expression, (void *) njs_parser_stack_push,
    NULL,
};


static const njs_parser_switch_t  njs_parser_logical_and_expression_switch = {
    NJS_PARSER_TEST_LINE_END,
    1, {
        { NJS_TOKEN_LOGICAL_AND,
          njs_parser_binary_expression, (void *) njs_vmcode_test_if_false,
          njs_parser_logical_and_expression_primed },
    }
};


static const void *const  njs_parser_logical_and_expression[] = {
    &njs_parser_logical_and_expression_switch, (void *) njs_parser_switch,
    njs_parser_bitwise_or_expression, (void *) njs_parser_stack_push,
    NULL,
};


/*
 * The left associative logical OR.
 *
 * LOGICAL_OR  =                       LOGICAL_AND            LOGICAL_OR'
 * LOGICAL_OR' = [ "||"  create_node ] LOGICAL_AND link_right LOGICAL_OR'
 *               <>
 */

static const njs_parser_switch_t  njs_parser_logical_or_expression_switch;

static const void *const  njs_parser_logical_or_expression_primed[] = {
    &njs_parser_logical_or_expression_switch, (void *) njs_parser_switch,
    NJS_PARSER_NODE, (void *) njs_parser_link_right,
    &njs_parser_logical_and_expression, (void *) njs_parser_stack_push,
    NULL,
};


static const njs_parser_switch_t  njs_parser_logical_or_expression_switch = {
    NJS_PARSER_TEST_LINE_END,
    1, {
        { NJS_TOKEN_LOGICAL_OR,
          njs_parser_binary_expression, (void *) njs_vmcode_test_if_true,
          njs_parser_logical_or_expression_primed },
    }
};


static const void *const  njs_parser_logical_or_expression[] = {
    &njs_parser_logical_or_expression_switch, (void *) njs_parser_switch,
    njs_parser_logical_and_expression, (void *) njs_parser_stack_push,
    NULL,
};


/*
 * The right associative condition operation.
 *
 * CONDITION  =                     LOGICAL_OR CONDITION'
 * CONDITION' = [ "?" create_node ] ASSIGNMENT COLON      link_right
 *              <>
 * COLON      = [ ":" create_node ] ASSIGNMENT            link_right
 *              ERROR
 */

static const void *const  njs_parser_assignment_expression[];

static const void *const  njs_parser_colon_expression_primed[] = {
    NJS_PARSER_NODE, (void *) njs_parser_link_right,
    &njs_parser_assignment_expression, (void *) njs_parser_stack_push,
    NULL,
};


static const njs_parser_switch_t  njs_parser_colon_expression_switch = {
    NJS_PARSER_IGNORE_LINE_END,
    2, {
        { NJS_TOKEN_COLON, njs_parser_node, (void *) NJS_TOKEN_ELSE,
          njs_parser_colon_expression_primed },

        { NJS_TOKEN_ANY, njs_parser_syntax_error, NULL, NULL },
    }
};


static const void *const  njs_parser_conditional_expression_primed[] = {
    NJS_PARSER_NODE, (void *) njs_parser_link_right,
    &njs_parser_colon_expression_switch, (void *) njs_parser_switch,
    &njs_parser_assignment_expression, (void *) njs_parser_stack_push,
    NULL,
};


static const njs_parser_switch_t  njs_parser_conditional_expression_switch = {
    NJS_PARSER_IGNORE_LINE_END,
    1, {
        { NJS_TOKEN_CONDITIONAL,
          njs_parser_condition_expression, (void *) NJS_TOKEN_CONDITIONAL,
          njs_parser_conditional_expression_primed },
    }
};


static const void *const  njs_parser_conditional_expression0[] = {
    &njs_parser_conditional_expression_switch, (void *) njs_parser_switch,
    njs_parser_logical_or_expression, (void *) njs_parser_stack_push,
    NULL,
};


/*
 * The right associative assignment operations.
 *
 * ASSIGNMENT  =                     CONDITION ASSIGNMENT'
 * ASSIGNMENT' = [ "=" create_node ] CONDITION ASSIGNMENT' link_right
 *               <>
 */

static const void *const  njs_parser_assignment_expression_primed[] = {
    NJS_PARSER_NODE, (void *) njs_parser_link_right,
    &njs_parser_assignment_expression, (void *) njs_parser_stack_push,
    NULL,
};


static const njs_parser_switch_t  njs_parser_assignment_expression_switch = {
    NJS_PARSER_TEST_LINE_END,
    1, {
        { NJS_TOKEN_ASSIGNMENT,
          /* STUB */ njs_parser_binary_expression, (void *) njs_vmcode_move,
          njs_parser_assignment_expression_primed },
    }
};


static const void *const  njs_parser_assignment_expression[] = {
    &njs_parser_assignment_expression_switch, (void *) njs_parser_switch,
    njs_parser_conditional_expression0, (void *) njs_parser_stack_push,
    NULL,
};


/*
 * The left associative comma.
 *
 * EXPRESSION  =                      ASSIGNMENT            EXPRESSION'
 * EXPRESSION' = [ ","  create_node ] ASSIGNMENT link_right EXPRESSION'
 *               <>
 */

static const njs_parser_switch_t  njs_parser_comma_expression_switch;

static const void *const  njs_parser_expression_primed[] = {
    &njs_parser_comma_expression_switch, (void *) njs_parser_switch,
    NJS_PARSER_NODE, (void *) njs_parser_link_right,
    &njs_parser_assignment_expression, (void *) njs_parser_stack_push,
    NULL,
};


static const njs_parser_switch_t  njs_parser_comma_expression_switch = {
    NJS_PARSER_TEST_LINE_END,
    1, {
        { NJS_TOKEN_COMMA,
          njs_parser_binary_expression, NULL, njs_parser_expression_primed },
    }
};


static const void *const  njs_parser_expression0[] = {
    &njs_parser_comma_expression_switch, (void *) njs_parser_switch,
    njs_parser_assignment_expression, (void *) njs_parser_stack_push,
    NULL,
};


/*
 * The variable declarations and initializations.
 *
 * VAR      = [ NAME create_node ]              VAR_INIT
 * VAR_INIT = "=" ASSIGNMENT     [ link_right ] VAR_NEXT
 *            "," VAR
 *            <>
 * VAR_NEXT = "," VAR
 *            <>
 */

static const void *const  njs_parser_var_statement[];

static const njs_parser_switch_t  njs_parser_var_next_switch = {
    NJS_PARSER_TEST_LINE_END,
    1, {
        { NJS_TOKEN_COMMA,
          njs_parser_noop, NULL, njs_parser_var_statement },
    }
};


static const void *const  njs_parser_var_init_expression[] = {
    &njs_parser_var_next_switch, (void *) njs_parser_switch,
    NJS_PARSER_NODE, (void *) njs_parser_link_right,
    &njs_parser_assignment_expression, (void *) njs_parser_stack_push,
    NULL,
};


static const njs_parser_switch_t  njs_parser_var_init_switch = {
    NJS_PARSER_TEST_LINE_END,
    2, {
        { NJS_TOKEN_ASSIGNMENT,
          /* TODO */ njs_parser_binary_expression, (void *) njs_vmcode_move,
          njs_parser_var_init_expression },

        { NJS_TOKEN_COMMA, /* TODO: free node */
          njs_parser_noop, NULL, njs_parser_var_statement },
    }
};


static const void *const  njs_parser_var_statement[] = {
    &njs_parser_var_init_switch, (void *) njs_parser_switch,
    NJS_PARSER_VOID, (void *) njs_parser_var_name,
    NULL,
};


/*
 * The statements.
 *
 * STATEMENT = <END>
 *             "var"  VAR
 *             ";"    STATEMENT
 *             <*>    create_node EXPRESSION [ ";" link_right ] STATEMENT
 */

static const void *const  njs_parser_statement[];

static const void *const  njs_parser_expression_statement[] = {
    &njs_parser_statement, (void *) njs_parser_stack_push,
    NJS_PARSER_NODE, (void *) njs_parser_statement_semicolon,
    &njs_parser_expression0, (void *) njs_parser_stack_push,
    NULL,
};


static const njs_parser_switch_t  njs_parser_statement_switch = {
    NJS_PARSER_IGNORE_LINE_END,
//    4, {
    3, {
        { NJS_TOKEN_VAR, njs_parser_noop, NULL, njs_parser_var_statement },
        { NJS_TOKEN_END, njs_parser_noop, NULL, NULL },
#if 0
        { NJS_TOKEN_SEMICOLON,
          njs_parser_node, (void *) NJS_TOKEN_STATEMENT, NULL },
#endif

        { NJS_TOKEN_ANY, njs_parser_node, (void *) NJS_TOKEN_STATEMENT,
          njs_parser_expression_statement },
    }
};


static const void *const  njs_parser_statement[] = {
    &njs_parser_statement_switch, (void *) njs_parser_switch,
    NULL,
};
