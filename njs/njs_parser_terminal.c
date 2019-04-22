
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
static nxt_int_t njs_parser_object_property(njs_vm_t *vm, njs_parser_t *parser,
    njs_parser_node_t *parent, njs_parser_node_t *property,
    njs_parser_node_t *value);
static njs_token_t njs_parser_array(njs_vm_t *vm, njs_parser_t *parser,
    njs_parser_node_t *array);
static nxt_int_t njs_parser_array_item(njs_vm_t *vm, njs_parser_t *parser,
    njs_parser_node_t *array, njs_parser_node_t *value);
static nxt_int_t njs_parser_template_expression(njs_vm_t *vm,
    njs_parser_t *parser);
static nxt_int_t njs_parser_template_string(njs_vm_t *vm,
    njs_parser_t *parser);
static njs_token_t njs_parser_escape_string_create(njs_vm_t *vm,
    njs_parser_t *parser, njs_value_t *value);


njs_token_t
njs_parser_terminal(njs_vm_t *vm, njs_parser_t *parser, njs_token_t token)
{
    double             num;
    njs_ret_t          ret;
    njs_parser_node_t  *node;

    ret = njs_parser_match_arrow_expression(vm, parser, token);
    if (ret == NXT_OK) {
        return njs_parser_arrow_expression(vm, parser, token);
    }

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

        return njs_parser_object(vm, parser, node);

    case NJS_TOKEN_OPEN_BRACKET:
        nxt_thread_log_debug("JS: ARRAY");

        node = njs_parser_node_new(vm, parser, NJS_TOKEN_ARRAY);
        if (nxt_slow_path(node == NULL)) {
            return NJS_TOKEN_ERROR;
        }

        return njs_parser_array(vm, parser, node);

    case NJS_TOKEN_GRAVE:
        nxt_thread_log_debug("JS: TEMPLATE LITERAL");

        node = njs_parser_node_new(vm, parser, NJS_TOKEN_TEMPLATE_LITERAL);
        if (nxt_slow_path(node == NULL)) {
            return NJS_TOKEN_ERROR;
        }

        return njs_parser_template_literal(vm, parser, node);

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
    njs_variable_t      *var;
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

        scope = njs_function_scope(parser->scope, 0);

        if (scope != NULL) {
            if (scope == njs_function_scope(parser->scope, 1)) {
                node->index = NJS_INDEX_THIS;

            } else {
                node->token = NJS_TOKEN_NON_LOCAL_THIS;

                node->token_line = token_line;

                ret = njs_variable_reference(vm, scope, node, name, hash,
                                             NJS_REFERENCE);
                if (nxt_slow_path(ret != NXT_OK)) {
                    return NULL;
                }

                var = njs_variable_add(vm, scope, name, hash, NJS_VARIABLE_VAR);
                if (nxt_slow_path(var == NULL)) {
                    return NULL;
                }

                var->this_object = 1;
            }

            break;
        }

        node->token = NJS_TOKEN_GLOBAL_THIS;

        if (vm->options.module) {
            node->u.value = njs_value_undefined;
            break;
        }

        /* Fall through. */

    case NJS_TOKEN_NJS:
    case NJS_TOKEN_MATH:
    case NJS_TOKEN_JSON:
        ret = njs_parser_builtin(vm, parser, node, NJS_OBJECT, name, hash);
        if (nxt_slow_path(ret != NXT_OK)) {
            return NULL;
        }

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

    case NJS_TOKEN_ARGUMENTS:
        nxt_thread_log_debug("JS: arguments");

        scope = njs_function_scope(parser->scope, 0);

        if (scope == NULL) {
            njs_parser_syntax_error(vm, parser, "\"%V\" object "
                                    "in global scope", name);

            return NULL;
        }

        node->token_line = token_line;

        ret = njs_variable_reference(vm, scope, node, name, hash,
                                     NJS_REFERENCE);
        if (nxt_slow_path(ret != NXT_OK)) {
            return NULL;
        }

        var = njs_variable_add(vm, scope, name, hash, NJS_VARIABLE_VAR);
        if (nxt_slow_path(var == NULL)) {
            return NULL;
        }

        var->arguments_object = 1;

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
    nxt_int_t          ret;
    nxt_str_t          name;
    njs_token_t        token;
    njs_lexer_t        *lexer;
    njs_parser_node_t  *object, *property, *expression;

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

        if (token == NJS_TOKEN_CLOSE_BRACE) {
            break;
        }

        name.start = NULL;

        switch (token) {

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

        property = parser->node;

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

        ret = njs_parser_object_property(vm, parser, obj, property, expression);
        if (nxt_slow_path(ret != NXT_OK)) {
            return NJS_TOKEN_ERROR;
        }

        if (token == NJS_TOKEN_CLOSE_BRACE) {
            break;
        }

        if (nxt_slow_path(token != NJS_TOKEN_COMMA)) {
            return NJS_TOKEN_ILLEGAL;
        }
    }

    parser->node = obj;

    return njs_parser_token(vm, parser);
}


static nxt_int_t
njs_parser_object_property(njs_vm_t *vm, njs_parser_t *parser,
    njs_parser_node_t *parent, njs_parser_node_t *property,
    njs_parser_node_t *value)
{
    njs_parser_node_t  *stmt, *assign, *object, *propref;

    object = njs_parser_node_new(vm, parser, NJS_TOKEN_OBJECT_VALUE);
    if (nxt_slow_path(object == NULL)) {
        return NJS_TOKEN_ERROR;
    }

    object->u.object = parent;

    propref = njs_parser_node_new(vm, parser, NJS_TOKEN_PROPERTY);
    if (nxt_slow_path(propref == NULL)) {
        return NXT_ERROR;
    }

    propref->left = object;
    propref->right = property;

    assign = njs_parser_node_new(vm, parser, NJS_TOKEN_ASSIGNMENT);
    if (nxt_slow_path(assign == NULL)) {
        return NXT_ERROR;
    }

    assign->u.operation = njs_vmcode_move;
    assign->left = propref;
    assign->right = value;

    stmt = njs_parser_node_new(vm, parser, NJS_TOKEN_STATEMENT);
    if (nxt_slow_path(stmt == NULL)) {
        return NXT_ERROR;
    }

    stmt->right = assign;
    stmt->left = parent->left;
    parent->left = stmt;

    return NXT_OK;
}


static njs_token_t
njs_parser_array(njs_vm_t *vm, njs_parser_t *parser, njs_parser_node_t *array)
{
    nxt_int_t    ret;
    njs_token_t  token;

    for ( ;; ) {
        token = njs_parser_token(vm, parser);
        if (nxt_slow_path(token <= NJS_TOKEN_ILLEGAL)) {
            return token;
        }

        if (token == NJS_TOKEN_CLOSE_BRACKET) {
            break;
        }

        if (token == NJS_TOKEN_COMMA) {
            array->ctor = 1;
            array->u.length++;
            continue;
        }

        token = njs_parser_assignment_expression(vm, parser, token);
        if (nxt_slow_path(token <= NJS_TOKEN_ILLEGAL)) {
            return token;
        }

        ret = njs_parser_array_item(vm, parser, array, parser->node);
        if (nxt_slow_path(ret != NXT_OK)) {
            return NJS_TOKEN_ERROR;
        }

        if (token == NJS_TOKEN_CLOSE_BRACKET) {
            break;
        }

        if (nxt_slow_path(token != NJS_TOKEN_COMMA)) {
            return NJS_TOKEN_ILLEGAL;
        }
    }

    parser->node = array;

    return njs_parser_token(vm, parser);
}


static nxt_int_t
njs_parser_array_item(njs_vm_t *vm, njs_parser_t *parser,
    njs_parser_node_t *array, njs_parser_node_t *value)
{
    nxt_int_t          ret;
    njs_parser_node_t  *number;

    number = njs_parser_node_new(vm, parser, NJS_TOKEN_NUMBER);
    if (nxt_slow_path(number == NULL)) {
        return NXT_ERROR;
    }

    number->u.value.data.u.number = array->u.length;
    number->u.value.type = NJS_NUMBER;
    number->u.value.data.truth = (array->u.length != 0);

    ret = njs_parser_object_property(vm, parser, array, number, value);
    if (nxt_slow_path(ret != NXT_OK)) {
        return NXT_ERROR;
    }

    array->ctor = 0;
    array->u.length++;

    return NXT_OK;
}


njs_token_t
njs_parser_template_literal(njs_vm_t *vm, njs_parser_t *parser,
    njs_parser_node_t *parent)
{
    uint8_t            tagged_template;
    nxt_int_t          ret;
    nxt_bool_t         expression;
    njs_index_t        index;
    njs_parser_node_t  *node, *array;

    tagged_template = (parent->token != NJS_TOKEN_TEMPLATE_LITERAL);

    index = NJS_SCOPE_CALLEE_ARGUMENTS;

    array = njs_parser_node_new(vm, parser, NJS_TOKEN_ARRAY);
    if (nxt_slow_path(array == NULL)) {
        return NJS_TOKEN_ERROR;
    }

    if (tagged_template) {
        node = njs_parser_argument(vm, parser, array, index);
        if (nxt_slow_path(node == NULL)) {
            return NJS_TOKEN_ERROR;
        }

        parent->right = node;
        parent = node;

        index += sizeof(njs_value_t);

    } else {
        parent->left = array;
    }

    expression = 0;

    for ( ;; ) {
        ret = expression ? njs_parser_template_expression(vm, parser)
                         : njs_parser_template_string(vm, parser);

        if (ret == NXT_ERROR) {
            njs_parser_syntax_error(vm, parser,
                                    "Unterminated template literal");
            return NJS_TOKEN_ILLEGAL;
        }

        node = parser->node;

        if (ret == NXT_DONE) {
            ret = njs_parser_array_item(vm, parser, array, node);
            if (nxt_slow_path(ret != NXT_OK)) {
                return NJS_TOKEN_ERROR;
            }

            parser->node = parent;

            return njs_parser_token(vm, parser);
        }

        /* NXT_OK */

        if (tagged_template && expression) {
            node = njs_parser_argument(vm, parser, node, index);
            if (nxt_slow_path(node == NULL)) {
                return NJS_TOKEN_ERROR;
            }

            parent->right = node;
            parent = node;

            index += sizeof(njs_value_t);

        } else {
            ret = njs_parser_array_item(vm, parser, array, node);
            if (nxt_slow_path(ret != NXT_OK)) {
                return NJS_TOKEN_ERROR;
            }
        }

        expression = !expression;
    }
}


static nxt_int_t
njs_parser_template_expression(njs_vm_t *vm, njs_parser_t *parser)
{
    njs_token_t  token;

    token = njs_parser_token(vm, parser);
    if (nxt_slow_path(token <= NJS_TOKEN_ILLEGAL)) {
        return NXT_ERROR;
    }

    token = njs_parser_expression(vm, parser, token);
    if (nxt_slow_path(token <= NJS_TOKEN_ILLEGAL)) {
        return NXT_ERROR;
    }

    if (token != NJS_TOKEN_CLOSE_BRACE) {
        njs_parser_syntax_error(vm, parser,
                            "Missing \"}\" in template expression");
        return NXT_ERROR;
    }

    return NXT_OK;
}


static nxt_int_t
njs_parser_template_string(njs_vm_t *vm, njs_parser_t *parser)
{
    u_char             *p, c;
    nxt_int_t          ret;
    nxt_str_t          *text;
    nxt_bool_t         escape;
    njs_lexer_t        *lexer;
    njs_parser_node_t  *node;

    lexer = parser->lexer;
    text = &lexer->lexer_token->text;

    text->start = lexer->start;

    escape = 0;
    p = lexer->start;

    while (p < lexer->end) {

        c = *p++;

        if (c == '\\') {
            if (p == lexer->end) {
                break;
            }

            p++;
            escape = 1;

            continue;
        }

        if (c == '`') {
            text->length = p - text->start - 1;
            goto done;
        }

        if (c == '$') {
            if (p < lexer->end && *p == '{') {
                p++;
                text->length = p - text->start - 2;
                goto done;
            }
        }
    }

    return NXT_ERROR;

done:

    node = njs_parser_node_new(vm, parser, NJS_TOKEN_STRING);
    if (nxt_slow_path(node == NULL)) {
        return NXT_ERROR;
    }

    if (escape) {
        ret = njs_parser_escape_string_create(vm, parser, &node->u.value);
        if (nxt_slow_path(ret != NJS_TOKEN_STRING)) {
            return NXT_ERROR;
        }

    } else {
        ret = njs_parser_string_create(vm, &node->u.value);
        if (nxt_slow_path(ret != NXT_OK)) {
            return NXT_ERROR;
        }
    }

    lexer->start = p;
    parser->node = node;

    return c == '`' ? NXT_DONE : NXT_OK;
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
