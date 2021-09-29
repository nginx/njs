
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) NGINX, Inc.
 */

#ifndef _NJS_LEXER_H_INCLUDED_
#define _NJS_LEXER_H_INCLUDED_


typedef enum {
    NJS_TOKEN_ERROR = -1,
    NJS_TOKEN_ILLEGAL = 0,

    NJS_TOKEN_END,
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
    NJS_TOKEN_ELLIPSIS,
    NJS_TOKEN_SEMICOLON,

    NJS_TOKEN_COLON,
    NJS_TOKEN_CONDITIONAL,

    NJS_TOKEN_COMMENT,

    NJS_TOKEN_ASSIGNMENT,
    NJS_TOKEN_ARROW,
    NJS_TOKEN_ADDITION_ASSIGNMENT,
    NJS_TOKEN_SUBSTRACTION_ASSIGNMENT,
    NJS_TOKEN_MULTIPLICATION_ASSIGNMENT,
    NJS_TOKEN_EXPONENTIATION_ASSIGNMENT,
    NJS_TOKEN_DIVISION_ASSIGNMENT,
    NJS_TOKEN_REMAINDER_ASSIGNMENT,
    NJS_TOKEN_LEFT_SHIFT_ASSIGNMENT,
    NJS_TOKEN_RIGHT_SHIFT_ASSIGNMENT,
    NJS_TOKEN_UNSIGNED_RIGHT_SHIFT_ASSIGNMENT,
    NJS_TOKEN_BITWISE_OR_ASSIGNMENT,
    NJS_TOKEN_BITWISE_XOR_ASSIGNMENT,
    NJS_TOKEN_BITWISE_AND_ASSIGNMENT,

    NJS_TOKEN_INCREMENT,
    NJS_TOKEN_DECREMENT,
    NJS_TOKEN_POST_INCREMENT,
    NJS_TOKEN_POST_DECREMENT,

#define NJS_TOKEN_LAST_ASSIGNMENT   NJS_TOKEN_POST_DECREMENT

    NJS_TOKEN_EQUAL,
    NJS_TOKEN_STRICT_EQUAL,
    NJS_TOKEN_NOT_EQUAL,
    NJS_TOKEN_STRICT_NOT_EQUAL,

    NJS_TOKEN_ADDITION,
    NJS_TOKEN_UNARY_PLUS,

    NJS_TOKEN_SUBSTRACTION,
    NJS_TOKEN_UNARY_NEGATION,

    NJS_TOKEN_MULTIPLICATION,

    NJS_TOKEN_EXPONENTIATION,

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

    NJS_TOKEN_COALESCE,

    NJS_TOKEN_IN,
    NJS_TOKEN_OF,
    NJS_TOKEN_INSTANCEOF,
    NJS_TOKEN_TYPEOF,
    NJS_TOKEN_VOID,
    NJS_TOKEN_NEW,
    NJS_TOKEN_DELETE,
    NJS_TOKEN_YIELD,

    NJS_TOKEN_DIGIT,
    NJS_TOKEN_LETTER,

#define NJS_TOKEN_FIRST_CONST     NJS_TOKEN_NULL

    NJS_TOKEN_NULL,
    NJS_TOKEN_NUMBER,
    NJS_TOKEN_TRUE,
    NJS_TOKEN_UNDEFINED,
    NJS_TOKEN_FALSE,
    NJS_TOKEN_STRING,

#define NJS_TOKEN_LAST_CONST      NJS_TOKEN_STRING

    NJS_TOKEN_ESCAPE_STRING,
    NJS_TOKEN_UNTERMINATED_STRING,
    NJS_TOKEN_NAME,

    NJS_TOKEN_OBJECT,
    NJS_TOKEN_OBJECT_VALUE,
    NJS_TOKEN_PROPERTY,
    NJS_TOKEN_PROPERTY_INIT,
    NJS_TOKEN_PROPERTY_DELETE,
    NJS_TOKEN_PROPERTY_GETTER,
    NJS_TOKEN_PROPERTY_SETTER,
    NJS_TOKEN_PROTO_INIT,

    NJS_TOKEN_ARRAY,

    NJS_TOKEN_GRAVE,
    NJS_TOKEN_TEMPLATE_LITERAL,

    NJS_TOKEN_FUNCTION,
    NJS_TOKEN_FUNCTION_DECLARATION,
    NJS_TOKEN_FUNCTION_EXPRESSION,
    NJS_TOKEN_FUNCTION_CALL,
    NJS_TOKEN_METHOD_CALL,
    NJS_TOKEN_ARGUMENT,
    NJS_TOKEN_RETURN,

    NJS_TOKEN_ASYNC_FUNCTION,
    NJS_TOKEN_ASYNC_FUNCTION_DECLARATION,
    NJS_TOKEN_ASYNC_FUNCTION_EXPRESSION,

    NJS_TOKEN_REGEXP,

    NJS_TOKEN_EXTERNAL,

    NJS_TOKEN_STATEMENT,
    NJS_TOKEN_BLOCK,
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
    NJS_TOKEN_ARGUMENTS,
    NJS_TOKEN_EVAL,

    NJS_TOKEN_IMPORT,
    NJS_TOKEN_EXPORT,

    NJS_TOKEN_TARGET,

    NJS_TOKEN_FROM,

    NJS_TOKEN_META,

    NJS_TOKEN_AWAIT,
    NJS_TOKEN_ASYNC,
    NJS_TOKEN_CLASS,
    NJS_TOKEN_CONST,
    NJS_TOKEN_DEBUGGER,
    NJS_TOKEN_ENUM,
    NJS_TOKEN_EXTENDS,
    NJS_TOKEN_IMPLEMENTS,
    NJS_TOKEN_INTERFACE,
    NJS_TOKEN_LET,
    NJS_TOKEN_PACKAGE,
    NJS_TOKEN_PRIVATE,
    NJS_TOKEN_PROTECTED,
    NJS_TOKEN_PUBLIC,
    NJS_TOKEN_STATIC,
    NJS_TOKEN_SUPER,

    NJS_TOKEN_RESERVED,
} njs_token_type_t;


typedef enum {
    NJS_KEYWORD_TYPE_UNDEF    = 0,
    NJS_KEYWORD_TYPE_RESERVED = 1,
    NJS_KEYWORD_TYPE_KEYWORD  = 2
} njs_keyword_type_t;


typedef struct {
    njs_str_t                       name;
} njs_lexer_entry_t;


typedef struct {
    njs_lexer_entry_t               entry;
    njs_token_type_t                type;
    njs_bool_t                      reserved;
} njs_keyword_t;


typedef struct {
    const char                      *key;
    const njs_keyword_t             *value;

    size_t                          length;
    size_t                          next;
} njs_lexer_keyword_entry_t;


typedef struct {
    njs_token_type_t                type:16;
    njs_keyword_type_t              keyword_type;
    uint32_t                        line;
    uintptr_t                       unique_id;
    njs_str_t                       text;
    double                          number;
    njs_queue_link_t                link;
} njs_lexer_token_t;


typedef struct {
    njs_lexer_token_t               *token;
    njs_queue_t                     preread; /*  of njs_lexer_token_t */

    u_char                          *prev_start;
    njs_token_type_t                prev_type:16;
    njs_token_type_t                last_type:16;

    uint32_t                        line;
    njs_str_t                       file;

    njs_lvlhsh_t                    *keywords_hash;

    njs_mp_t                        *mem_pool;

    u_char                          *start;
    u_char                          *end;
} njs_lexer_t;


njs_int_t njs_lexer_init(njs_vm_t *vm, njs_lexer_t *lexer, njs_str_t *file,
    u_char *start, u_char *end, njs_uint_t runtime);

njs_lexer_token_t *njs_lexer_token(njs_lexer_t *lexer,
    njs_bool_t with_end_line);
njs_lexer_token_t *njs_lexer_peek_token(njs_lexer_t *lexer,
    njs_lexer_token_t *current, njs_bool_t with_end_line);
void njs_lexer_consume_token(njs_lexer_t *lexer, unsigned length);
njs_int_t njs_lexer_make_token(njs_lexer_t *lexer, njs_lexer_token_t *token);

const njs_lexer_keyword_entry_t *njs_lexer_keyword(const u_char *key,
    size_t length);
njs_int_t njs_lexer_keywords(njs_arr_t *array);


njs_inline const njs_lexer_entry_t *
njs_lexer_entry(uintptr_t unique_id)
{
    return (const njs_lexer_entry_t *) unique_id;
}


njs_inline njs_bool_t
njs_lexer_token_is_keyword(njs_lexer_token_t *token)
{
    return token->keyword_type & NJS_KEYWORD_TYPE_KEYWORD;
}


njs_inline njs_bool_t
njs_lexer_token_is_reserved(njs_lexer_token_t *token)
{
    return token->keyword_type & NJS_KEYWORD_TYPE_RESERVED;
}


njs_inline njs_bool_t
njs_lexer_token_is_name(njs_lexer_token_t *token)
{
    return token->type == NJS_TOKEN_NAME
           || (!njs_lexer_token_is_reserved(token)
               && njs_lexer_token_is_keyword(token));
}


njs_inline njs_bool_t
njs_lexer_token_is_identifier_name(njs_lexer_token_t *token)
{
    return token->type == NJS_TOKEN_NAME || njs_lexer_token_is_keyword(token);
}


njs_inline njs_bool_t
njs_lexer_token_is_binding_identifier(njs_lexer_token_t *token)
{
    switch (token->type) {
    case NJS_TOKEN_NAME:
    case NJS_TOKEN_YIELD:
    case NJS_TOKEN_AWAIT:
        return 1;

    default:
        return (!njs_lexer_token_is_reserved(token)
                && njs_lexer_token_is_keyword(token));
    };
}


njs_inline njs_bool_t
njs_lexer_token_is_label_identifier(njs_lexer_token_t *token)
{
    return njs_lexer_token_is_binding_identifier(token);
}


njs_inline njs_bool_t
njs_lexer_token_is_identifier_reference(njs_lexer_token_t *token)
{
    return njs_lexer_token_is_binding_identifier(token);
}


extern const njs_lvlhsh_proto_t  njs_lexer_hash_proto;


#endif /* _NJS_LEXER_H_INCLUDED_ */
