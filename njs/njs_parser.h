
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) NGINX, Inc.
 */

#ifndef _NJS_PARSER_H_INCLUDED_
#define _NJS_PARSER_H_INCLUDED_


typedef enum {
    NJS_TOKEN_AGAIN = -2,
    NJS_TOKEN_ERROR = -1,
    NJS_TOKEN_ILLEGAL = 0,

    NJS_TOKEN_END,
    NJS_TOKEN_CALL,
    NJS_TOKEN_SPACE,
    NJS_TOKEN_LINE_END,

    NJS_TOKEN_DOUBLE_QUOTE,
    NJS_TOKEN_SINGLE_QUOTE,

    NJS_TOKEN_OPEN_PARENTHESIS,
    NJS_TOKEN_CLOSE_PARENTHESIS,
    NJS_TOKEN_OPEN_BRACKET,
    NJS_TOKEN_CLOSE_BRACKET,
    NJS_TOKEN_OPEN_BRACE,
    NJS_TOKEN_CLOSE_BRACE,

    NJS_TOKEN_COMMA,
    NJS_TOKEN_DOT,
    NJS_TOKEN_SEMICOLON,

#define NJS_TOKEN_FIRST_OPERATOR    NJS_TOKEN_COLON

    NJS_TOKEN_COLON,
    NJS_TOKEN_CONDITIONAL,

    NJS_TOKEN_ASSIGNMENT,
    NJS_TOKEN_ADDITION_ASSIGNMENT,
    NJS_TOKEN_SUBSTRACTION_ASSIGNMENT,
    NJS_TOKEN_MULTIPLICATION_ASSIGNMENT,
    NJS_TOKEN_DIVISION_ASSIGNMENT,
    NJS_TOKEN_REMAINDER_ASSIGNMENT,
    NJS_TOKEN_LEFT_SHIFT_ASSIGNMENT,
    NJS_TOKEN_RIGHT_SHIFT_ASSIGNMENT,
    NJS_TOKEN_UNSIGNED_RIGHT_SHIFT_ASSIGNMENT,
    NJS_TOKEN_BITWISE_OR_ASSIGNMENT,
    NJS_TOKEN_BITWISE_XOR_ASSIGNMENT,
    NJS_TOKEN_BITWISE_AND_ASSIGNMENT,

#define NJS_TOKEN_LAST_ASSIGNMENT   NJS_TOKEN_BITWISE_AND_ASSIGNMENT

    NJS_TOKEN_EQUAL,
    NJS_TOKEN_STRICT_EQUAL,
    NJS_TOKEN_NOT_EQUAL,
    NJS_TOKEN_STRICT_NOT_EQUAL,

    NJS_TOKEN_ADDITION,
    NJS_TOKEN_UNARY_PLUS,
    NJS_TOKEN_INCREMENT,
    NJS_TOKEN_POST_INCREMENT,

    NJS_TOKEN_SUBSTRACTION,
    NJS_TOKEN_UNARY_NEGATION,
    NJS_TOKEN_DECREMENT,
    NJS_TOKEN_POST_DECREMENT,

    NJS_TOKEN_MULTIPLICATION,

    NJS_TOKEN_DIVISION,

    NJS_TOKEN_REMAINDER,

    NJS_TOKEN_LESS,
    NJS_TOKEN_LESS_OR_EQUAL,
    NJS_TOKEN_LEFT_SHIFT,

    NJS_TOKEN_GREATER,
    NJS_TOKEN_GREATER_OR_EQUAL,
    NJS_TOKEN_RIGHT_SHIFT,
    NJS_TOKEN_UNSIGNED_RIGHT_SHIFT,

    NJS_TOKEN_BITWISE_OR,
    NJS_TOKEN_LOGICAL_OR,

    NJS_TOKEN_BITWISE_XOR,

    NJS_TOKEN_BITWISE_AND,
    NJS_TOKEN_LOGICAL_AND,

    NJS_TOKEN_BITWISE_NOT,
    NJS_TOKEN_LOGICAL_NOT,

    NJS_TOKEN_IN,
    NJS_TOKEN_INSTANCEOF,
    NJS_TOKEN_TYPEOF,
    NJS_TOKEN_VOID,
    NJS_TOKEN_NEW,
    NJS_TOKEN_DELETE,
    NJS_TOKEN_YIELD,

#define NJS_TOKEN_LAST_OPERATOR   NJS_TOKEN_YIELD

    NJS_TOKEN_DIGIT,
    NJS_TOKEN_LETTER,

#define NJS_TOKEN_FIRST_CONST     NJS_TOKEN_UNDEFINED

    NJS_TOKEN_UNDEFINED,
    NJS_TOKEN_NULL,
    NJS_TOKEN_NUMBER,
    NJS_TOKEN_BOOLEAN,
    NJS_TOKEN_STRING,

#define NJS_TOKEN_LAST_CONST      NJS_TOKEN_STRING

    NJS_TOKEN_ESCAPE_STRING,
    NJS_TOKEN_NAME,

    NJS_TOKEN_OBJECT,
    NJS_TOKEN_OBJECT_VALUE,
    NJS_TOKEN_PROPERTY,
    NJS_TOKEN_PROPERTY_DELETE,

    NJS_TOKEN_ARRAY,

    NJS_TOKEN_FUNCTION,
    NJS_TOKEN_FUNCTION_EXPRESSION,
    NJS_TOKEN_FUNCTION_CALL,
    NJS_TOKEN_METHOD_CALL,
    NJS_TOKEN_ARGUMENT,
    NJS_TOKEN_RETURN,

    NJS_TOKEN_REGEXP,

    NJS_TOKEN_EXTERNAL,

    NJS_TOKEN_STATEMENT,
    NJS_TOKEN_VAR,
    NJS_TOKEN_IF,
    NJS_TOKEN_ELSE,
    NJS_TOKEN_BRANCHING,
    NJS_TOKEN_WHILE,
    NJS_TOKEN_DO,
    NJS_TOKEN_FOR,
    NJS_TOKEN_FOR_IN,
    NJS_TOKEN_BREAK,
    NJS_TOKEN_CONTINUE,
    NJS_TOKEN_SWITCH,
    NJS_TOKEN_CASE,
    NJS_TOKEN_DEFAULT,
    NJS_TOKEN_WITH,
    NJS_TOKEN_TRY,
    NJS_TOKEN_CATCH,
    NJS_TOKEN_FINALLY,
    NJS_TOKEN_THROW,

    NJS_TOKEN_THIS,

#define NJS_TOKEN_FIRST_OBJECT     NJS_TOKEN_MATH

    NJS_TOKEN_MATH,

    NJS_TOKEN_OBJECT_CONSTRUCTOR,
    NJS_TOKEN_ARRAY_CONSTRUCTOR,
    NJS_TOKEN_BOOLEAN_CONSTRUCTOR,
    NJS_TOKEN_NUMBER_CONSTRUCTOR,
    NJS_TOKEN_STRING_CONSTRUCTOR,
    NJS_TOKEN_FUNCTION_CONSTRUCTOR,
    NJS_TOKEN_REGEXP_CONSTRUCTOR,
    NJS_TOKEN_DATE_CONSTRUCTOR,
    NJS_TOKEN_EVAL,

    NJS_TOKEN_RESERVED,
} njs_token_t;


typedef struct {
    njs_token_t                     token:8;
    njs_token_t                     prev_token:8;
    uint32_t                        key_hash;

    nxt_str_t                       text;
    double                          number;

    nxt_lvlhsh_t                    keywords_hash;

    u_char                          *start;
    u_char                          *end;
} njs_lexer_t;


typedef enum {
    NJS_VARIABLE_NORMAL = 0,
    NJS_VARIABLE_FIRST_ASSIGNMENT,
    NJS_VARIABLE_ASSIGNMENT,
    NJS_VARIABLE_TYPEOF,
} njs_variable_node_state_t;


typedef enum {
    NJS_LVALUE_NONE = 0,
    NJS_LVALUE_ENABLED,
    NJS_LVALUE_ASSIGNED,
} njs_lvalue_state_t;


typedef struct njs_parser_node_s    njs_parser_node_t;

struct njs_parser_node_s {
    njs_token_t                     token:8;
    njs_variable_node_state_t       state:8;    /* 2 bits */
    njs_lvalue_state_t              lvalue:2;   /* 2 bits */
    uint8_t                         ctor:1;     /* 1 bit  */
    uint8_t                         temporary;  /* 1 bit  */

    union {
        uint32_t                    length;
        njs_vmcode_operation_t      operation;
        njs_value_t                 value;
        njs_variable_t              *variable;
        njs_parser_node_t           *object;
        njs_extern_t                *external;
    } u;

    njs_index_t                     index;

    njs_parser_node_t               *left;
    njs_parser_node_t               *right;
    njs_parser_node_t               *dest;
};


#define njs_parser_node_alloc(vm)                                             \
    nxt_mem_cache_zalloc((vm)->mem_cache_pool, sizeof(njs_parser_node_t))


typedef struct njs_parser_patch_s   njs_parser_patch_t;

struct njs_parser_patch_s {
    /*
     * The address field points to jump offset field which contains a small
     * adjustment and the adjustment should be added as (njs_ret_t *) because
     * pointer to u_char accesses only one byte so this does not work on big
     * endian platforms.
     */
    njs_ret_t                       *address;
    njs_parser_patch_t              *next;
};


typedef enum {
    NJS_PARSER_BLOCK = 0,
    NJS_PARSER_LOOP,
    NJS_PARSER_SWITCH,
} njs_parser_block_type_t;

typedef struct njs_parser_block_s   njs_parser_block_t;

struct njs_parser_block_s {
    njs_parser_block_type_t         type;    /* 2 bits */
    nxt_str_t                       label;
    njs_parser_patch_t              *continuation;
    njs_parser_patch_t              *exit;
    njs_parser_block_t              *next;
};


struct njs_parser_s {
    njs_lexer_t                     *lexer;
    njs_parser_node_t               *node;

    /* Vector of njs_variable_t. */
    nxt_array_t                     *arguments;
    njs_parser_block_t              *block;

    nxt_lvlhsh_t                    variables_hash;

    nxt_array_t                     *index_cache;
    njs_index_t                     index[NJS_SCOPES - NJS_INDEX_CACHE];

    nxt_array_t                     *scope_values;

    uint8_t                         scope;        /* 4 bits */
    uint8_t                         branch;       /* 1 bit */

    /* Parsing Function() or eval(). */
    uint8_t                         runtime;      /* 1 bit */

    size_t                          code_size;

    /* Generator. */

    njs_value_t                     *local_scope;
    size_t                          scope_size;
    size_t                          scope_offset;

    u_char                          *code_start;
    u_char                          *code_end;

    njs_parser_t                    *parent;
};


njs_token_t njs_lexer_token(njs_lexer_t *lexer);
nxt_int_t njs_lexer_keywords_init(nxt_mem_cache_pool_t *mcp,
    nxt_lvlhsh_t *hash);
njs_token_t njs_lexer_keyword(njs_lexer_t *lexer);

njs_extern_t *njs_parser_external(njs_vm_t *vm, njs_parser_t *parser);

njs_parser_node_t *njs_parser(njs_vm_t *vm, njs_parser_t *parser);
njs_parser_node_t *njs_nonrecursive_parser(njs_vm_t *vm, njs_parser_t *parser);
njs_token_t njs_parser_arguments(njs_vm_t *vm, njs_parser_t *parser,
    njs_parser_node_t *parent);
njs_token_t njs_parser_expression(njs_vm_t *vm, njs_parser_t *parser,
    njs_token_t token);
njs_token_t njs_parser_var_expression(njs_vm_t *vm, njs_parser_t *parser,
    njs_token_t token);
njs_token_t njs_parser_conditional_expression(njs_vm_t *vm,
    njs_parser_t *parser, njs_token_t token);
njs_token_t njs_parser_terminal(njs_vm_t *vm, njs_parser_t *parser,
    njs_token_t token);
njs_token_t njs_parser_property_name(njs_vm_t *vm, njs_parser_t *parser,
    njs_token_t token);
njs_token_t njs_parser_token(njs_parser_t *parser);
nxt_int_t njs_parser_string_create(njs_vm_t *vm, njs_value_t *value);
njs_index_t njs_parser_index(njs_parser_t *parser, uint32_t scope);
nxt_bool_t njs_parser_has_side_effect(njs_parser_node_t *node);
nxt_int_t njs_generate_scope(njs_vm_t *vm, njs_parser_t *parser,
    njs_parser_node_t *node);


#define njs_generate_code(parser, type, code)                                 \
    do {                                                                      \
        code = (type *) parser->code_end; parser->code_end += sizeof(type);   \
    } while (0)


#endif /* _NJS_PARSER_H_INCLUDED_ */
