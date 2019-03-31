
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) NGINX, Inc.
 */

#include <njs_core.h>
#include <njs_regexp.h>
#include <string.h>


static njs_parser_node_t *njs_parser_reference(njs_vm_t *vm,
    njs_parser_t *parser, njs_token_t token, nxt_str_t *name, uint32_t hash,
    uint32_t token_line);
static nxt_int_t njs_parser_builtin(njs_vm_t *vm, njs_parser_t *parser,
    njs_parser_node_t *node, njs_value_type_t type, nxt_str_t *name,
    uint32_t hash);
static njs_token_t njs_parser_object(njs_vm_t *vm, njs_parser_t *parser,
    njs_parser_node_t *obj);
static njs_token_t njs_parser_array(njs_vm_t *vm, njs_parser_t *parser,
    njs_parser_node_t *obj);
static njs_token_t njs_parser_escape_string_create(njs_vm_t *vm,
    njs_parser_t *parser, njs_value_t *value);


njs_token_t
njs_parser_terminal(njs_vm_t *vm, njs_parser_t *parser, njs_token_t token)
{
    double             num;
    njs_ret_t          ret;
    njs_parser_node_t  *node;

    if (token == NJS_TOKEN_OPEN_PARENTHESIS) {

        token = njs_parser_token(vm, parser);
        if (nxt_slow_path(token <= NJS_TOKEN_ILLEGAL)) {
            return token;
        }

        token = njs_parser_expression(vm, parser, token);
        if (nxt_slow_path(token <= NJS_TOKEN_ILLEGAL)) {
            return token;
        }

        return njs_parser_match(vm, parser, token, NJS_TOKEN_CLOSE_PARENTHESIS);
    }

    if (token == NJS_TOKEN_FUNCTION) {
        return njs_parser_function_expression(vm, parser);
    }

    switch (token) {

    case NJS_TOKEN_OPEN_BRACE:
        nxt_thread_log_debug("JS: OBJECT");

        node = njs_parser_node_new(vm, parser, NJS_TOKEN_OBJECT);
        if (nxt_slow_path(node == NULL)) {
            return NJS_TOKEN_ERROR;
        }

        parser->node = node;

        token = njs_parser_object(vm, parser, node);

        if (parser->node != node) {
            /* The object is not empty. */
            node->left = parser->node;
            parser->node = node;
        }

        return token;

    case NJS_TOKEN_OPEN_BRACKET:
        nxt_thread_log_debug("JS: ARRAY");

        node = njs_parser_node_new(vm, parser, NJS_TOKEN_ARRAY);
        if (nxt_slow_path(node == NULL)) {
            return NJS_TOKEN_ERROR;
        }

        parser->node = node;

        token = njs_parser_array(vm, parser, node);

        if (parser->node != node) {
            /* The array is not empty. */
            node->left = parser->node;
            parser->node = node;
        }

        return token;

    case NJS_TOKEN_DIVISION:
        node = njs_parser_node_new(vm, parser, NJS_TOKEN_REGEXP);
        if (nxt_slow_path(node == NULL)) {
            return NJS_TOKEN_ERROR;
        }

        token = njs_regexp_literal(vm, parser, &node->u.value);
        if (nxt_slow_path(token <= NJS_TOKEN_ILLEGAL)) {
            return token;
        }

        nxt_thread_log_debug("REGEX: '%V'", njs_parser_text(parser));

        break;

    case NJS_TOKEN_STRING:
        nxt_thread_log_debug("JS: '%V'", njs_parser_text(parser));

        node = njs_parser_node_new(vm, parser, NJS_TOKEN_STRING);
        if (nxt_slow_path(node == NULL)) {
            return NJS_TOKEN_ERROR;
        }

        ret = njs_parser_string_create(vm, &node->u.value);
        if (nxt_slow_path(ret != NXT_OK)) {
            return NJS_TOKEN_ERROR;
        }

        break;

    case NJS_TOKEN_ESCAPE_STRING:
        nxt_thread_log_debug("JS: '%V'", njs_parser_text(parser));

        node = njs_parser_node_new(vm, parser, NJS_TOKEN_STRING);
        if (nxt_slow_path(node == NULL)) {
            return NJS_TOKEN_ERROR;
        }

        ret = njs_parser_escape_string_create(vm, parser, &node->u.value);
        if (nxt_slow_path(ret != NJS_TOKEN_STRING)) {
            return ret;
        }

        break;

    case NJS_TOKEN_UNTERMINATED_STRING:
        njs_parser_syntax_error(vm, parser, "Unterminated string \"%V\"",
                                njs_parser_text(parser));

        return NJS_TOKEN_ILLEGAL;

    case NJS_TOKEN_NUMBER:
        num = njs_parser_number(parser);
        nxt_thread_log_debug("JS: %f", num);

        node = njs_parser_node_new(vm, parser, NJS_TOKEN_NUMBER);
        if (nxt_slow_path(node == NULL)) {
            return NJS_TOKEN_ERROR;
        }

        node->u.value.data.u.number = num;
        node->u.value.type = NJS_NUMBER;
        node->u.value.data.truth = njs_is_number_true(num);

        break;

    case NJS_TOKEN_BOOLEAN:
        num = njs_parser_number(parser);
        nxt_thread_log_debug("JS: boolean: %V", njs_parser_text(parser));

        node = njs_parser_node_new(vm, parser, NJS_TOKEN_BOOLEAN);
        if (nxt_slow_path(node == NULL)) {
            return NJS_TOKEN_ERROR;
        }

        if (num == 0) {
            node->u.value = njs_value_false;

        } else {
            node->u.value = njs_value_true;
        }

        break;

    default:
        node = njs_parser_reference(vm, parser, token,
                                    njs_parser_text(parser),
                                    njs_parser_key_hash(parser),
                                    njs_parser_token_line(parser));

        if (nxt_slow_path(node == NULL)) {
            return NJS_TOKEN_ERROR;
        }

        break;
    }

    parser->node = node;

    return njs_parser_token(vm, parser);
}


static njs_parser_node_t *
njs_parser_reference(njs_vm_t *vm, njs_parser_t *parser, njs_token_t token,
    nxt_str_t *name, uint32_t hash, uint32_t token_line)
{
    njs_ret_t           ret;
    njs_value_t         *ext;
    njs_parser_node_t   *node;
    njs_parser_scope_t  *scope;

    node = njs_parser_node_new(vm, parser, token);
    if (nxt_slow_path(node == NULL)) {
        return NULL;
    }

    switch (token) {

    case NJS_TOKEN_NULL:
        nxt_thread_log_debug("JS: null");

        node->u.value = njs_value_null;
        break;

    case NJS_TOKEN_UNDEFINED:
        nxt_thread_log_debug("JS: undefined");

        node->u.value = njs_value_undefined;
        break;

    case NJS_TOKEN_THIS:
        nxt_thread_log_debug("JS: this");

        scope = parser->scope;

        while (scope->type != NJS_SCOPE_GLOBAL) {
            if (scope->type == NJS_SCOPE_FUNCTION) {
                node->index = NJS_INDEX_THIS;
                break;
            }

            scope = scope->parent;
        }

        if (node->index == NJS_INDEX_THIS) {
            break;
        }

        node->token = NJS_TOKEN_GLOBAL_THIS;

        /* Fall through. */

    case NJS_TOKEN_NJS:
    case NJS_TOKEN_MATH:
    case NJS_TOKEN_JSON:
        ret = njs_parser_builtin(vm, parser, node, NJS_OBJECT, name, hash);
        if (nxt_slow_path(ret != NXT_OK)) {
            return NULL;
        }

        break;

    case NJS_TOKEN_ARGUMENTS:
        nxt_thread_log_debug("JS: arguments");

        if (parser->scope->type <= NJS_SCOPE_GLOBAL) {
            njs_parser_syntax_error(vm, parser, "\"%V\" object "
                                    "in global scope", name);

            return NULL;
        }

        parser->scope->arguments_object = 1;

        break;

    case NJS_TOKEN_OBJECT_CONSTRUCTOR:
        node->index = NJS_INDEX_OBJECT;
        break;

    case NJS_TOKEN_ARRAY_CONSTRUCTOR:
        node->index = NJS_INDEX_ARRAY;
        break;

    case NJS_TOKEN_BOOLEAN_CONSTRUCTOR:
        node->index = NJS_INDEX_BOOLEAN;
        break;

    case NJS_TOKEN_NUMBER_CONSTRUCTOR:
        node->index = NJS_INDEX_NUMBER;
        break;

    case NJS_TOKEN_STRING_CONSTRUCTOR:
        node->index = NJS_INDEX_STRING;
        break;

    case NJS_TOKEN_FUNCTION_CONSTRUCTOR:
        node->index = NJS_INDEX_FUNCTION;
        break;

    case NJS_TOKEN_REGEXP_CONSTRUCTOR:
        node->index = NJS_INDEX_REGEXP;
        break;

    case NJS_TOKEN_DATE_CONSTRUCTOR:
        node->index = NJS_INDEX_DATE;
        break;

    case NJS_TOKEN_ERROR_CONSTRUCTOR:
        node->index = NJS_INDEX_OBJECT_ERROR;
        break;

    case NJS_TOKEN_EVAL_ERROR_CONSTRUCTOR:
        node->index = NJS_INDEX_OBJECT_EVAL_ERROR;
        break;

    case NJS_TOKEN_INTERNAL_ERROR_CONSTRUCTOR:
        node->index = NJS_INDEX_OBJECT_INTERNAL_ERROR;
        break;

    case NJS_TOKEN_RANGE_ERROR_CONSTRUCTOR:
        node->index = NJS_INDEX_OBJECT_RANGE_ERROR;
        break;

    case NJS_TOKEN_REF_ERROR_CONSTRUCTOR:
        node->index = NJS_INDEX_OBJECT_REF_ERROR;
        break;

    case NJS_TOKEN_SYNTAX_ERROR_CONSTRUCTOR:
        node->index = NJS_INDEX_OBJECT_SYNTAX_ERROR;
        break;

    case NJS_TOKEN_TYPE_ERROR_CONSTRUCTOR:
        node->index = NJS_INDEX_OBJECT_TYPE_ERROR;
        break;

    case NJS_TOKEN_URI_ERROR_CONSTRUCTOR:
        node->index = NJS_INDEX_OBJECT_URI_ERROR;
        break;

    case NJS_TOKEN_MEMORY_ERROR_CONSTRUCTOR:
        node->index = NJS_INDEX_OBJECT_MEMORY_ERROR;
        break;

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
        ret = njs_parser_builtin(vm, parser, node, NJS_FUNCTION, name, hash);
        if (nxt_slow_path(ret != NXT_OK)) {
            return NULL;
        }

        break;

    case NJS_TOKEN_NAME:
        nxt_thread_log_debug("JS: %V", name);

        node->token_line = token_line;

        ext = njs_external_lookup(vm, name, hash);

        if (ext != NULL) {
            node->token = NJS_TOKEN_EXTERNAL;
            node->u.value = *ext;
            node->index = (njs_index_t) ext;
            break;
        }

        ret = njs_variable_reference(vm, parser->scope, node, name, hash,
                                     NJS_REFERENCE);
        if (nxt_slow_path(ret != NXT_OK)) {
            return NULL;
        }

        break;

    default:
        (void) njs_parser_unexpected_token(vm, parser, token);
        return NULL;
    }

    return node;
}


static nxt_int_t
njs_parser_builtin(njs_vm_t *vm, njs_parser_t *parser, njs_parser_node_t *node,
    njs_value_type_t type, nxt_str_t *name, uint32_t hash)
{
    njs_ret_t           ret;
    nxt_uint_t          index;
    njs_variable_t      *var;
    njs_parser_scope_t  *scope;

    scope = njs_parser_global_scope(vm);

    var = njs_variable_add(vm, scope, name, hash, NJS_VARIABLE_VAR);
    if (nxt_slow_path(var == NULL)) {
        return NXT_ERROR;
    }

    /* TODO: once */
    switch (type) {
    case NJS_OBJECT:
        index = node->token - NJS_TOKEN_FIRST_OBJECT;
        var->value.data.u.object = &vm->shared->objects[index];
        break;

    case NJS_FUNCTION:
        index = node->token - NJS_TOKEN_FIRST_FUNCTION;
        var->value.data.u.function = &vm->shared->functions[index];
        break;

    default:
        return NXT_ERROR;
    }

    var->value.type = type;
    var->value.data.truth = 1;

    ret = njs_variable_reference(vm, scope, node, name, hash, NJS_REFERENCE);
    if (nxt_slow_path(ret != NXT_OK)) {
        return NXT_ERROR;
    }

    return NXT_OK;
}


/*
 * ES6: 12.2.6 Object Initializer
 * Supported syntax:
 *   PropertyDefinition:
 *     PropertyName : AssignmentExpression
 *     IdentifierReference
 *   PropertyName:
 *    IdentifierName, StringLiteral, NumericLiteral.
 */
static njs_token_t
njs_parser_object(njs_vm_t *vm, njs_parser_t *parser, njs_parser_node_t *obj)
{
    uint32_t           hash, token_line;
    nxt_str_t          name;
    njs_token_t        token;
    njs_lexer_t        *lexer;
    njs_parser_node_t  *stmt, *assign, *object, *propref, *left, *expression;

    left = NULL;
    lexer = parser->lexer;

    /* GCC and Clang complain about uninitialized hash. */
    hash = 0;
    token_line = 0;

    object = njs_parser_node_new(vm, parser, NJS_TOKEN_OBJECT_VALUE);
    if (nxt_slow_path(object == NULL)) {
        return NJS_TOKEN_ERROR;
    }

    object->u.object = obj;

    for ( ;; ) {
        token = njs_parser_property_token(vm, parser);

        name.start = NULL;

        switch (token) {

        case NJS_TOKEN_CLOSE_BRACE:
            return njs_parser_token(vm, parser);

        case NJS_TOKEN_NAME:
            name = *njs_parser_text(parser);

            hash = njs_parser_key_hash(parser);
            token_line = njs_parser_token_line(parser);

            token = njs_parser_token(vm, parser);
            break;

        case NJS_TOKEN_NUMBER:
        case NJS_TOKEN_STRING:
        case NJS_TOKEN_ESCAPE_STRING:
            token = njs_parser_terminal(vm, parser, token);
            break;

        default:
            return NJS_TOKEN_ILLEGAL;
        }

        if (nxt_slow_path(token <= NJS_TOKEN_ILLEGAL)) {
            return token;
        }

        propref = njs_parser_node_new(vm, parser, NJS_TOKEN_PROPERTY);
        if (nxt_slow_path(propref == NULL)) {
            return NJS_TOKEN_ERROR;
        }

        propref->left = object;
        propref->right = parser->node;

        if (name.start != NULL
            && (token == NJS_TOKEN_COMMA || token == NJS_TOKEN_CLOSE_BRACE)
            && lexer->property_token != NJS_TOKEN_THIS
            && lexer->property_token != NJS_TOKEN_GLOBAL_THIS)
        {
            expression = njs_parser_reference(vm, parser, lexer->property_token,
                                              &name, hash, token_line);
            if (nxt_slow_path(expression == NULL)) {
                return NJS_TOKEN_ERROR;
            }

        } else {
            token = njs_parser_match(vm, parser, token, NJS_TOKEN_COLON);
            if (nxt_slow_path(token <= NJS_TOKEN_ILLEGAL)) {
                return token;
            }

            token = njs_parser_assignment_expression(vm, parser, token);
            if (nxt_slow_path(token <= NJS_TOKEN_ILLEGAL)) {
                return token;
            }

            expression = parser->node;
        }

        assign = njs_parser_node_new(vm, parser, NJS_TOKEN_ASSIGNMENT);
        if (nxt_slow_path(assign == NULL)) {
            return NJS_TOKEN_ERROR;
        }

        assign->u.operation = njs_vmcode_move;
        assign->left = propref;
        assign->right = expression;

        stmt = njs_parser_node_new(vm, parser, NJS_TOKEN_STATEMENT);
        if (nxt_slow_path(stmt == NULL)) {
            return NJS_TOKEN_ERROR;
        }

        stmt->left = left;
        stmt->right = assign;

        parser->node = stmt;

        left = stmt;

        if (token == NJS_TOKEN_CLOSE_BRACE) {
            return njs_parser_token(vm, parser);
        }

        if (nxt_slow_path(token != NJS_TOKEN_COMMA)) {
            return NJS_TOKEN_ILLEGAL;
        }
    }
}


static njs_token_t
njs_parser_array(njs_vm_t *vm, njs_parser_t *parser, njs_parser_node_t *obj)
{
    nxt_uint_t         index;
    njs_token_t        token;
    njs_parser_node_t  *stmt, *assign, *object, *propref, *left, *node;

    index = 0;
    left = NULL;

    for ( ;; ) {
        token = njs_parser_token(vm, parser);
        if (nxt_slow_path(token <= NJS_TOKEN_ILLEGAL)) {
            return token;
        }

        if (token == NJS_TOKEN_CLOSE_BRACKET) {
            break;
        }

        if (token == NJS_TOKEN_COMMA) {
            obj->ctor = 1;
            index++;
            continue;
        }

        node = njs_parser_node_new(vm, parser, NJS_TOKEN_NUMBER);
        if (nxt_slow_path(node == NULL)) {
            return NJS_TOKEN_ERROR;
        }

        node->u.value.data.u.number = index;
        node->u.value.type = NJS_NUMBER;
        node->u.value.data.truth = (index != 0);
        index++;

        object = njs_parser_node_new(vm, parser, NJS_TOKEN_OBJECT_VALUE);
        if (nxt_slow_path(object == NULL)) {
            return NJS_TOKEN_ERROR;
        }

        object->u.object = obj;

        propref = njs_parser_node_new(vm, parser, NJS_TOKEN_PROPERTY);
        if (nxt_slow_path(propref == NULL)) {
            return NJS_TOKEN_ERROR;
        }

        propref->left = object;
        propref->right = node;

        token = njs_parser_assignment_expression(vm, parser, token);
        if (nxt_slow_path(token <= NJS_TOKEN_ILLEGAL)) {
            return token;
        }

        assign = njs_parser_node_new(vm, parser, NJS_TOKEN_ASSIGNMENT);
        if (nxt_slow_path(assign == NULL)) {
            return NJS_TOKEN_ERROR;
        }

        assign->u.operation = njs_vmcode_move;
        assign->left = propref;
        assign->right = parser->node;

        stmt = njs_parser_node_new(vm, parser, NJS_TOKEN_STATEMENT);
        if (nxt_slow_path(stmt == NULL)) {
            return NJS_TOKEN_ERROR;
        }

        stmt->left = left;
        stmt->right = assign;

        parser->node = stmt;
        left = stmt;

        obj->ctor = 0;

        if (token == NJS_TOKEN_CLOSE_BRACKET) {
            break;
        }

        if (nxt_slow_path(token != NJS_TOKEN_COMMA)) {
            return NJS_TOKEN_ILLEGAL;
        }
    }

    obj->u.length = index;

    return njs_parser_token(vm, parser);
}


nxt_int_t
njs_parser_string_create(njs_vm_t *vm, njs_value_t *value)
{
    u_char     *p;
    ssize_t    length;
    nxt_str_t  *src;

    src = njs_parser_text(vm->parser);

    length = nxt_utf8_length(src->start, src->length);

    if (nxt_slow_path(length < 0)) {
        length = 0;
    }

    p = njs_string_alloc(vm, value, src->length, length);

    if (nxt_fast_path(p != NULL)) {
        memcpy(p, src->start, src->length);

        if (length > NJS_STRING_MAP_STRIDE && (size_t) length != src->length) {
            njs_string_offset_map_init(p, src->length);
        }

        return NXT_OK;
    }

    return NXT_ERROR;
}


static njs_token_t
njs_parser_escape_string_create(njs_vm_t *vm, njs_parser_t *parser,
    njs_value_t *value)
{
    u_char        c, *start, *dst;
    size_t        size,length, hex_length;
    uint64_t      u;
    nxt_str_t     *string;
    const u_char  *p, *src, *end, *hex_end;

    start = NULL;
    dst = NULL;

    for ( ;; ) {
        /*
         * The loop runs twice: at the first step string size and
         * UTF-8 length are evaluated.  Then the string is allocated
         * and at the second step string content is copied.
         */
        size = 0;
        length = 0;

        string = njs_parser_text(parser);
        src = string->start;
        end = src + string->length;

        while (src < end) {
            c = *src++;

            if (c == '\\') {
                /*
                 * Testing "src == end" is not required here
                 * since this has been already tested by lexer.
                 */
                c = *src++;

                switch (c) {

                case 'u':
                    hex_length = 4;
                    /*
                     * A character after "u" can be safely tested here
                     * because there is always a closing quote at the
                     * end of string: ...\u".
                     */
                    if (*src != '{') {
                        goto hex_length_test;
                    }

                    src++;
                    hex_length = 0;
                    hex_end = end;

                    goto hex;

                case 'x':
                    hex_length = 2;
                    goto hex_length_test;

                case '0':
                    c = '\0';
                    break;

                case 'b':
                    c = '\b';
                    break;

                case 'f':
                    c = '\f';
                    break;

                case 'n':
                    c = '\n';
                    break;

                case 'r':
                    c = '\r';
                    break;

                case 't':
                    c = '\t';
                    break;

                case 'v':
                    c = '\v';
                    break;

                case '\r':
                    /*
                     * A character after "\r" can be safely tested here
                     * because there is always a closing quote at the
                     * end of string: ...\\r".
                     */
                    if (*src == '\n') {
                        src++;
                    }

                    continue;

                case '\n':
                    continue;

                default:
                    break;
                }
            }

            size++;
            length++;

            if (dst != NULL) {
                *dst++ = c;
            }

            continue;

        hex_length_test:

            hex_end = src + hex_length;

            if (hex_end > end) {
                goto invalid;
            }

        hex:

            p = src;
            u = njs_number_hex_parse(&src, hex_end);

            if (hex_length != 0) {
                if (src != hex_end) {
                    goto invalid;
                }

            } else {
                if (src == p || (src - p) > 6) {
                    goto invalid;
                }

                if (src == end || *src++ != '}') {
                    goto invalid;
                }
            }

            size += nxt_utf8_size(u);
            length++;

            if (dst != NULL) {
                dst = nxt_utf8_encode(dst, (uint32_t) u);
                if (dst == NULL) {
                    goto invalid;
                }
            }
        }

        if (start != NULL) {
            if (length > NJS_STRING_MAP_STRIDE && length != size) {
                njs_string_offset_map_init(start, size);
            }

            return NJS_TOKEN_STRING;
        }

        start = njs_string_alloc(vm, value, size, length);
        if (nxt_slow_path(start == NULL)) {
            return NJS_TOKEN_ERROR;
        }

        dst = start;
    }

invalid:

    njs_parser_syntax_error(vm, parser, "Invalid Unicode code point \"%V\"",
                            njs_parser_text(parser));

    return NJS_TOKEN_ILLEGAL;
}
