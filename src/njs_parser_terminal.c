
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) NGINX, Inc.
 */


#include <njs_main.h>


static njs_parser_node_t *njs_parser_reference(njs_vm_t *vm,
    njs_parser_t *parser, njs_token_type_t type, njs_str_t *name,
    uintptr_t hash, uint32_t token_line);
static njs_token_type_t njs_parser_object(njs_vm_t *vm, njs_parser_t *parser,
    njs_parser_node_t *obj);
static njs_int_t njs_parser_object_property(njs_vm_t *vm, njs_parser_t *parser,
    njs_parser_node_t *parent, njs_parser_node_t *property,
    njs_parser_node_t *value, njs_bool_t proto_init);
static njs_int_t njs_parser_property_accessor(njs_vm_t *vm,
    njs_parser_t *parser, njs_parser_node_t *parent,
    njs_parser_node_t *property, njs_parser_node_t *value,
    njs_token_type_t accessor);
static njs_token_type_t njs_parser_array(njs_vm_t *vm, njs_parser_t *parser,
    njs_parser_node_t *array);
static njs_int_t njs_parser_array_item(njs_vm_t *vm, njs_parser_t *parser,
    njs_parser_node_t *array, njs_parser_node_t *value);
static njs_int_t njs_parser_template_expression(njs_vm_t *vm,
    njs_parser_t *parser);
static njs_int_t njs_parser_template_string(njs_vm_t *vm,
    njs_parser_t *parser);
static njs_int_t njs_parser_escape_string_calc_length(njs_vm_t *vm,
    njs_parser_t *parser, size_t *out_size, size_t *out_length);
static njs_token_type_t njs_parser_escape_string_create(njs_vm_t *vm,
    njs_parser_t *parser, njs_value_t *value);


njs_token_type_t
njs_parser_terminal(njs_vm_t *vm, njs_parser_t *parser, njs_token_type_t type)
{
    double             num;
    njs_int_t          ret;
    njs_parser_node_t  *node;

    ret = njs_parser_match_arrow_expression(vm, parser, type);
    if (ret == NJS_OK) {
        return njs_parser_arrow_expression(vm, parser, type);
    }

    if (type == NJS_TOKEN_OPEN_PARENTHESIS) {

        type = njs_parser_token(vm, parser);
        if (njs_slow_path(type <= NJS_TOKEN_ILLEGAL)) {
            return type;
        }

        type = njs_parser_expression(vm, parser, type);
        if (njs_slow_path(type <= NJS_TOKEN_ILLEGAL)) {
            return type;
        }

        return njs_parser_match(vm, parser, type, NJS_TOKEN_CLOSE_PARENTHESIS);
    }

    if (type == NJS_TOKEN_FUNCTION) {
        return njs_parser_function_expression(vm, parser);
    }

    switch (type) {

    case NJS_TOKEN_OPEN_BRACE:
        njs_thread_log_debug("JS: OBJECT");

        node = njs_parser_node_new(vm, parser, NJS_TOKEN_OBJECT);
        if (njs_slow_path(node == NULL)) {
            return NJS_TOKEN_ERROR;
        }

        return njs_parser_object(vm, parser, node);

    case NJS_TOKEN_OPEN_BRACKET:
        njs_thread_log_debug("JS: ARRAY");

        node = njs_parser_node_new(vm, parser, NJS_TOKEN_ARRAY);
        if (njs_slow_path(node == NULL)) {
            return NJS_TOKEN_ERROR;
        }

        return njs_parser_array(vm, parser, node);

    case NJS_TOKEN_GRAVE:
        njs_thread_log_debug("JS: TEMPLATE LITERAL");

        node = njs_parser_node_new(vm, parser, NJS_TOKEN_TEMPLATE_LITERAL);
        if (njs_slow_path(node == NULL)) {
            return NJS_TOKEN_ERROR;
        }

        return njs_parser_template_literal(vm, parser, node);

    case NJS_TOKEN_DIVISION:
        node = njs_parser_node_new(vm, parser, NJS_TOKEN_REGEXP);
        if (njs_slow_path(node == NULL)) {
            return NJS_TOKEN_ERROR;
        }

        type = njs_regexp_literal(vm, parser, &node->u.value);
        if (njs_slow_path(type <= NJS_TOKEN_ILLEGAL)) {
            return type;
        }

        njs_thread_log_debug("REGEX: '%V'", njs_parser_text(parser));

        break;

    case NJS_TOKEN_STRING:
        njs_thread_log_debug("JS: '%V'", njs_parser_text(parser));

        node = njs_parser_node_new(vm, parser, NJS_TOKEN_STRING);
        if (njs_slow_path(node == NULL)) {
            return NJS_TOKEN_ERROR;
        }

        ret = njs_parser_string_create(vm, &node->u.value);
        if (njs_slow_path(ret != NJS_OK)) {
            return NJS_TOKEN_ERROR;
        }

        break;

    case NJS_TOKEN_ESCAPE_STRING:
        njs_thread_log_debug("JS: '%V'", njs_parser_text(parser));

        node = njs_parser_node_new(vm, parser, NJS_TOKEN_STRING);
        if (njs_slow_path(node == NULL)) {
            return NJS_TOKEN_ERROR;
        }

        ret = njs_parser_escape_string_create(vm, parser, &node->u.value);
        if (njs_slow_path(ret != NJS_TOKEN_STRING)) {
            return ret;
        }

        break;

    case NJS_TOKEN_UNTERMINATED_STRING:
        njs_parser_syntax_error(vm, parser, "Unterminated string \"%V\"",
                                njs_parser_text(parser));

        return NJS_TOKEN_ILLEGAL;

    case NJS_TOKEN_NUMBER:
        num = njs_parser_number(parser);
        njs_thread_log_debug("JS: %f", num);

        node = njs_parser_node_new(vm, parser, NJS_TOKEN_NUMBER);
        if (njs_slow_path(node == NULL)) {
            return NJS_TOKEN_ERROR;
        }

        njs_set_number(&node->u.value, num);

        break;

    case NJS_TOKEN_TRUE:
    case NJS_TOKEN_FALSE:
        njs_thread_log_debug("JS: boolean: %V", njs_parser_text(parser));

        node = njs_parser_node_new(vm, parser, parser->lexer->token->type);
        if (njs_slow_path(node == NULL)) {
            return NJS_TOKEN_ERROR;
        }

        if (parser->lexer->token->type == NJS_TOKEN_FALSE) {
            node->u.value = njs_value_false;

        } else {
            node->u.value = njs_value_true;
        }

        break;

    default:
        node = njs_parser_reference(vm, parser, type, njs_parser_text(parser),
                                    njs_parser_key_hash(parser),
                                    njs_parser_token_line(parser));
        if (njs_slow_path(node == NULL)) {
            return NJS_TOKEN_ERROR;
        }

        break;
    }

    parser->node = node;

    return njs_parser_token(vm, parser);
}


static njs_parser_node_t *
njs_parser_reference(njs_vm_t *vm, njs_parser_t *parser, njs_token_type_t type,
    njs_str_t *name, uintptr_t unique_id, uint32_t token_line)
{
    njs_int_t           ret;
    njs_variable_t      *var;
    njs_parser_node_t   *node;
    njs_parser_scope_t  *scope;

    node = njs_parser_node_new(vm, parser, type);
    if (njs_slow_path(node == NULL)) {
        return NULL;
    }

    switch (type) {

    case NJS_TOKEN_NULL:
        njs_thread_log_debug("JS: null");

        node->u.value = njs_value_null;
        break;

    case NJS_TOKEN_THIS:
        njs_thread_log_debug("JS: this");

        scope = njs_function_scope(parser->scope, 0);

        if (scope != NULL) {
            if (scope == njs_function_scope(parser->scope, 1)) {
                node->index = NJS_INDEX_THIS;

            } else {
                node->token_type = NJS_TOKEN_NON_LOCAL_THIS;

                node->token_line = token_line;

                ret = njs_variable_reference(vm, scope, node, unique_id,
                                             NJS_REFERENCE);
                if (njs_slow_path(ret != NJS_OK)) {
                    return NULL;
                }

                var = njs_variable_add(vm, scope, unique_id, NJS_VARIABLE_VAR);
                if (njs_slow_path(var == NULL)) {
                    return NULL;
                }

                var->this_object = 1;
            }

            break;
        }

        node->token_type = NJS_TOKEN_GLOBAL_OBJECT;

        break;

    case NJS_TOKEN_ARGUMENTS:
        njs_thread_log_debug("JS: arguments");

        scope = njs_function_scope(parser->scope, 0);

        if (scope == NULL) {
            njs_parser_syntax_error(vm, parser, "\"%V\" object "
                                    "in global scope", name);

            return NULL;
        }

        node->token_line = token_line;

        ret = njs_variable_reference(vm, scope, node, unique_id, NJS_REFERENCE);
        if (njs_slow_path(ret != NJS_OK)) {
            return NULL;
        }

        var = njs_variable_add(vm, scope, unique_id, NJS_VARIABLE_VAR);
        if (njs_slow_path(var == NULL)) {
            return NULL;
        }

        var->arguments_object = 1;

        break;

    case NJS_TOKEN_NAME:
    case NJS_TOKEN_EVAL:
        njs_thread_log_debug("JS: %V", name);

        node->token_line = token_line;

        ret = njs_variable_reference(vm, parser->scope, node, unique_id,
                                     NJS_REFERENCE);
        if (njs_slow_path(ret != NJS_OK)) {
            return NULL;
        }

        break;

    default:
        (void) njs_parser_unexpected_token(vm, parser, type);
        return NULL;
    }

    return node;
}


static njs_token_type_t
njs_parser_object(njs_vm_t *vm, njs_parser_t *parser, njs_parser_node_t *obj)
{
    uintptr_t              unique_id;
    uint32_t               token_line;
    njs_int_t              ret, __proto__;
    njs_str_t              name;
    njs_bool_t             computed, proto_init;
    njs_token_type_t       type, accessor;
    njs_lexer_t            *lexer;
    njs_parser_node_t      *object, *property, *expression;
    njs_function_lambda_t  *lambda;

    const njs_str_t proto_string = njs_str("__proto__");

    lexer = parser->lexer;

    /* GCC and Clang complain about uninitialized values. */
    unique_id = 0;
    token_line = 0;
    __proto__ = 0;
    property = NULL;

    object = njs_parser_node_new(vm, parser, NJS_TOKEN_OBJECT_VALUE);
    if (njs_slow_path(object == NULL)) {
        return NJS_TOKEN_ERROR;
    }

    object->u.object = obj;

    for ( ;; ) {
        type = njs_parser_token(vm, parser);
        if (njs_slow_path(type <= NJS_TOKEN_ILLEGAL)) {
            return type;
        }

        accessor = 0;
        computed = 0;
        proto_init = 0;
        njs_memzero(&name, sizeof(njs_str_t));

        if (type == NJS_TOKEN_NAME || lexer->keyword) {
            name = *njs_parser_text(parser);
            unique_id = njs_parser_key_hash(parser);
            token_line = njs_parser_token_line(parser);

            property = njs_parser_node_string(vm, parser);
            if (njs_slow_path(property == NULL)) {
                return NJS_TOKEN_ERROR;
            }

            if (type == NJS_TOKEN_NAME && name.length == 3
                && (memcmp(name.start, "get", 3) == 0
                    || memcmp(name.start, "set", 3) == 0))
            {
                accessor = (name.start[0] == 'g') ? NJS_TOKEN_PROPERTY_GETTER
                                                  : NJS_TOKEN_PROPERTY_SETTER;

                type = njs_parser_token(vm, parser);
                if (njs_slow_path(type <= NJS_TOKEN_ILLEGAL)) {
                    return type;
                }
            }
        }

        switch (type) {

        case NJS_TOKEN_CLOSE_BRACE:
            if (accessor) {
                accessor = 0;
                break;
            }

            goto done;

        case NJS_TOKEN_OPEN_BRACKET:
            type = njs_parser_token(vm, parser);
            if (njs_slow_path(type <= NJS_TOKEN_ILLEGAL)) {
                return type;
            }

            if (type == NJS_TOKEN_CLOSE_BRACKET) {
                return NJS_TOKEN_ILLEGAL;
            }

            type = njs_parser_assignment_expression(vm, parser, type);
            if (njs_slow_path(type <= NJS_TOKEN_ILLEGAL)) {
                return type;
            }

            property = parser->node;

            type = njs_parser_match(vm, parser, type, NJS_TOKEN_CLOSE_BRACKET);

            computed = 1;

            break;

        case NJS_TOKEN_NUMBER:
        case NJS_TOKEN_STRING:
        case NJS_TOKEN_ESCAPE_STRING:
            type = njs_parser_terminal(vm, parser, type);
            if (njs_slow_path(type <= NJS_TOKEN_ILLEGAL)) {
                return type;
            }

            property = parser->node;
            break;

        default:
            if (type != NJS_TOKEN_NAME && !lexer->keyword) {
                if (name.length == 0) {
                    return NJS_TOKEN_ILLEGAL;
                }

                accessor = 0;
                break;
            }

            if (accessor) {
                property = njs_parser_node_string(vm, parser);
                if (njs_slow_path(property == NULL)) {
                    return NJS_TOKEN_ERROR;
                }
            }

            type = njs_parser_token(vm, parser);
            break;
        }

        if (accessor) {
            expression = njs_parser_node_new(vm, parser,
                                             NJS_TOKEN_FUNCTION_EXPRESSION);
            if (njs_slow_path(expression == NULL)) {
                return NJS_TOKEN_ERROR;
            }

            expression->token_line = njs_parser_token_line(parser);
            parser->node = expression;

            lambda = njs_mp_zalloc(vm->mem_pool, sizeof(njs_function_lambda_t));
            if (njs_slow_path(lambda == NULL)) {
                return NJS_TOKEN_ERROR;
            }

            expression->u.value.data.u.lambda = lambda;

            type = njs_parser_function_lambda(vm, parser, lambda, type);
            if (njs_slow_path(type <= NJS_TOKEN_ILLEGAL)) {
                return type;
            }

            if (accessor == NJS_TOKEN_PROPERTY_GETTER) {
                if (lambda->nargs != 0) {
                    njs_parser_syntax_error(vm, parser,
                                  "Getter must not have any formal parameters");
                    return NJS_TOKEN_ILLEGAL;
                }

            } else {
                if (lambda->nargs != 1) {
                    njs_parser_syntax_error(vm, parser,
                               "Setter must have exactly one formal parameter");
                    return NJS_TOKEN_ILLEGAL;
                }
            }

            ret = njs_parser_property_accessor(vm, parser, obj, property,
                                               expression, accessor);
            if (njs_slow_path(ret != NJS_OK)) {
                return NJS_TOKEN_ERROR;
            }

        } else {
            switch (type) {

            case NJS_TOKEN_COMMA:
            case NJS_TOKEN_CLOSE_BRACE:

                if (name.length == 0
                    || lexer->prev_type == NJS_TOKEN_THIS
                    || lexer->prev_type == NJS_TOKEN_GLOBAL_OBJECT)
                {
                    return NJS_TOKEN_ILLEGAL;
                }

                expression = njs_parser_reference(vm, parser, lexer->prev_type,
                                                  &name, unique_id, token_line);
                if (njs_slow_path(expression == NULL)) {
                    return NJS_TOKEN_ERROR;
                }

                break;

            case NJS_TOKEN_COLON:
                if (!computed) {
                    if (njs_is_string(&property->u.value)) {
                        njs_string_get(&property->u.value, &name);
                    }

                    if (njs_slow_path(njs_strstr_eq(&name, &proto_string))) {
                        if (++__proto__ > 1) {
                            njs_parser_syntax_error(vm, parser,
                                "Duplicate __proto__ fields are not allowed "
                                "in object literals");
                            return NJS_TOKEN_ILLEGAL;
                        }

                        proto_init = 1;
                    }
                }

                type = njs_parser_token(vm, parser);
                if (njs_slow_path(type <= NJS_TOKEN_ILLEGAL)) {
                    return type;
                }

                type = njs_parser_assignment_expression(vm, parser, type);
                if (njs_slow_path(type <= NJS_TOKEN_ILLEGAL)) {
                    return type;
                }

                expression = parser->node;
                break;

            case NJS_TOKEN_OPEN_PARENTHESIS:
                expression = njs_parser_node_new(vm, parser,
                                                 NJS_TOKEN_FUNCTION_EXPRESSION);
                if (njs_slow_path(expression == NULL)) {
                    return NJS_TOKEN_ERROR;
                }

                expression->token_line = njs_parser_token_line(parser);
                parser->node = expression;

                lambda = njs_function_lambda_alloc(vm, 0);
                if (njs_slow_path(lambda == NULL)) {
                    return NJS_TOKEN_ERROR;
                }

                expression->u.value.data.u.lambda = lambda;

                type = njs_parser_function_lambda(vm, parser, lambda, type);
                if (njs_slow_path(type <= NJS_TOKEN_ILLEGAL)) {
                    return type;
                }

                break;

            default:
                return NJS_TOKEN_ILLEGAL;
            }

            ret = njs_parser_object_property(vm, parser, obj, property,
                                             expression, proto_init);
            if (njs_slow_path(ret != NJS_OK)) {
                return NJS_TOKEN_ERROR;
            }
        }

        if (type == NJS_TOKEN_CLOSE_BRACE) {
            break;
        }

        if (njs_slow_path(type != NJS_TOKEN_COMMA)) {
            return NJS_TOKEN_ILLEGAL;
        }
    }

done:

    parser->node = obj;

    return njs_parser_token(vm, parser);
}


static njs_int_t
njs_parser_object_property(njs_vm_t *vm, njs_parser_t *parser,
    njs_parser_node_t *parent, njs_parser_node_t *property,
    njs_parser_node_t *value, njs_bool_t proto_init)
{
    njs_token_type_t   type;
    njs_parser_node_t  *stmt, *assign, *object, *propref;

    object = njs_parser_node_new(vm, parser, NJS_TOKEN_OBJECT_VALUE);
    if (njs_slow_path(object == NULL)) {
        return NJS_TOKEN_ERROR;
    }

    object->u.object = parent;

    type = proto_init ? NJS_TOKEN_PROTO_INIT : NJS_TOKEN_PROPERTY_INIT;

    propref = njs_parser_node_new(vm, parser, type);
    if (njs_slow_path(propref == NULL)) {
        return NJS_ERROR;
    }

    propref->left = object;
    propref->right = property;

    assign = njs_parser_node_new(vm, parser, NJS_TOKEN_ASSIGNMENT);
    if (njs_slow_path(assign == NULL)) {
        return NJS_ERROR;
    }

    assign->u.operation = NJS_VMCODE_MOVE;
    assign->left = propref;
    assign->right = value;

    stmt = njs_parser_node_new(vm, parser, NJS_TOKEN_STATEMENT);
    if (njs_slow_path(stmt == NULL)) {
        return NJS_ERROR;
    }

    stmt->right = assign;
    stmt->left = parent->left;
    parent->left = stmt;

    return NJS_OK;
}


static njs_int_t
njs_parser_property_accessor(njs_vm_t *vm, njs_parser_t *parser,
    njs_parser_node_t *parent, njs_parser_node_t *property,
    njs_parser_node_t *value, njs_token_type_t accessor)
{
    njs_parser_node_t  *node, *stmt, *object, *propref;

    object = njs_parser_node_new(vm, parser, NJS_TOKEN_OBJECT_VALUE);
    if (njs_slow_path(object == NULL)) {
        return NJS_TOKEN_ERROR;
    }

    object->u.object = parent;

    propref = njs_parser_node_new(vm, parser, 0);
    if (njs_slow_path(propref == NULL)) {
        return NJS_ERROR;
    }

    propref->left = object;
    propref->right = property;

    node = njs_parser_node_new(vm, parser, accessor);
    if (njs_slow_path(node == NULL)) {
        return NJS_ERROR;
    }

    node->left = propref;
    node->right = value;

    stmt = njs_parser_node_new(vm, parser, NJS_TOKEN_STATEMENT);
    if (njs_slow_path(stmt == NULL)) {
        return NJS_ERROR;
    }

    stmt->right = node;
    stmt->left = parent->left;
    parent->left = stmt;

    return NJS_OK;
}


static njs_token_type_t
njs_parser_array(njs_vm_t *vm, njs_parser_t *parser, njs_parser_node_t *array)
{
    njs_int_t         ret;
    njs_token_type_t  type;

    for ( ;; ) {
        type = njs_parser_token(vm, parser);
        if (njs_slow_path(type <= NJS_TOKEN_ILLEGAL)) {
            return type;
        }

        if (type == NJS_TOKEN_CLOSE_BRACKET) {
            break;
        }

        if (type == NJS_TOKEN_COMMA) {
            array->ctor = 1;
            array->u.length++;
            continue;
        }

        type = njs_parser_assignment_expression(vm, parser, type);
        if (njs_slow_path(type <= NJS_TOKEN_ILLEGAL)) {
            return type;
        }

        ret = njs_parser_array_item(vm, parser, array, parser->node);
        if (njs_slow_path(ret != NJS_OK)) {
            return NJS_TOKEN_ERROR;
        }

        if (type == NJS_TOKEN_CLOSE_BRACKET) {
            break;
        }

        if (njs_slow_path(type != NJS_TOKEN_COMMA)) {
            return NJS_TOKEN_ILLEGAL;
        }
    }

    parser->node = array;

    return njs_parser_token(vm, parser);
}


static njs_int_t
njs_parser_array_item(njs_vm_t *vm, njs_parser_t *parser,
    njs_parser_node_t *array, njs_parser_node_t *value)
{
    njs_int_t          ret;
    njs_parser_node_t  *number;

    number = njs_parser_node_new(vm, parser, NJS_TOKEN_NUMBER);
    if (njs_slow_path(number == NULL)) {
        return NJS_ERROR;
    }

    njs_set_number(&number->u.value, array->u.length);

    ret = njs_parser_object_property(vm, parser, array, number, value, 0);
    if (njs_slow_path(ret != NJS_OK)) {
        return NJS_ERROR;
    }

    array->ctor = 0;
    array->u.length++;

    return NJS_OK;
}


njs_token_type_t
njs_parser_template_literal(njs_vm_t *vm, njs_parser_t *parser,
    njs_parser_node_t *parent)
{
    uint8_t            tagged_template;
    njs_int_t          ret;
    njs_bool_t         expression;
    njs_index_t        index;
    njs_parser_node_t  *node, *array;

    tagged_template = (parent->token_type != NJS_TOKEN_TEMPLATE_LITERAL);

    index = NJS_SCOPE_CALLEE_ARGUMENTS;

    array = njs_parser_node_new(vm, parser, NJS_TOKEN_ARRAY);
    if (njs_slow_path(array == NULL)) {
        return NJS_TOKEN_ERROR;
    }

    if (tagged_template) {
        node = njs_parser_argument(vm, parser, array, index);
        if (njs_slow_path(node == NULL)) {
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

        if (ret == NJS_ERROR) {
            njs_parser_syntax_error(vm, parser,
                                    "Unterminated template literal");
            return NJS_TOKEN_ILLEGAL;
        }

        node = parser->node;

        if (ret == NJS_DONE) {
            ret = njs_parser_array_item(vm, parser, array, node);
            if (njs_slow_path(ret != NJS_OK)) {
                return NJS_TOKEN_ERROR;
            }

            parser->node = parent;

            return njs_parser_token(vm, parser);
        }

        /* NJS_OK */

        if (tagged_template && expression) {
            node = njs_parser_argument(vm, parser, node, index);
            if (njs_slow_path(node == NULL)) {
                return NJS_TOKEN_ERROR;
            }

            parent->right = node;
            parent = node;

            index += sizeof(njs_value_t);

        } else {
            ret = njs_parser_array_item(vm, parser, array, node);
            if (njs_slow_path(ret != NJS_OK)) {
                return NJS_TOKEN_ERROR;
            }
        }

        expression = !expression;
    }
}


static njs_int_t
njs_parser_template_expression(njs_vm_t *vm, njs_parser_t *parser)
{
    njs_token_type_t  type;

    type = njs_parser_token(vm, parser);
    if (njs_slow_path(type <= NJS_TOKEN_ILLEGAL)) {
        return NJS_ERROR;
    }

    type = njs_parser_expression(vm, parser, type);
    if (njs_slow_path(type <= NJS_TOKEN_ILLEGAL)) {
        return NJS_ERROR;
    }

    if (type != NJS_TOKEN_CLOSE_BRACE) {
        njs_parser_syntax_error(vm, parser,
                                "Missing \"}\" in template expression");
        return NJS_ERROR;
    }

    return NJS_OK;
}


static njs_int_t
njs_parser_template_string(njs_vm_t *vm, njs_parser_t *parser)
{
    u_char             *p, c;
    njs_int_t          ret;
    njs_str_t          *text;
    njs_bool_t         escape;
    njs_lexer_t        *lexer;
    njs_parser_node_t  *node;

    lexer = parser->lexer;
    text = &lexer->token->text;

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

    return NJS_ERROR;

done:

    node = njs_parser_node_new(vm, parser, NJS_TOKEN_STRING);
    if (njs_slow_path(node == NULL)) {
        return NJS_ERROR;
    }

    if (escape) {
        ret = njs_parser_escape_string_create(vm, parser, &node->u.value);
        if (njs_slow_path(ret != NJS_TOKEN_STRING)) {
            return NJS_ERROR;
        }

    } else {
        ret = njs_parser_string_create(vm, &node->u.value);
        if (njs_slow_path(ret != NJS_OK)) {
            return NJS_ERROR;
        }
    }

    lexer->start = p;
    parser->node = node;

    return c == '`' ? NJS_DONE : NJS_OK;
}


njs_int_t
njs_parser_string_create(njs_vm_t *vm, njs_value_t *value)
{
    u_char        *dst;
    ssize_t       size, length;
    uint32_t      cp;
    njs_str_t     *src;
    const u_char  *p, *end;

    src = njs_parser_text(vm->parser);

    length = njs_utf8_safe_length(src->start, src->length, &size);

    dst = njs_string_alloc(vm, value, size, length);
    if (njs_slow_path(dst == NULL)) {
        return NJS_ERROR;
    }

    p = src->start;
    end = src->start + src->length;

    while (p < end) {
        cp = njs_utf8_safe_decode(&p, end);

        dst = njs_utf8_encode(dst, cp);
    }

    if (length > NJS_STRING_MAP_STRIDE && size != length) {
        njs_string_offset_map_init(value->long_string.data->start, size);
    }

    return NJS_OK;
}


static njs_token_type_t
njs_parser_escape_string_create(njs_vm_t *vm, njs_parser_t *parser,
    njs_value_t *value)
{
    u_char        c, *start, *dst;
    size_t        size, length, hex_length;
    uint64_t      cp, cp_pair;
    njs_int_t     ret;
    njs_str_t     *string;
    const u_char  *src, *end, *hex_end;

    ret = njs_parser_escape_string_calc_length(vm, parser, &size, &length);
    if (njs_slow_path(ret != NJS_OK)) {
        return NJS_TOKEN_ILLEGAL;
    }

    start = njs_string_alloc(vm, value, size, length);
    if (njs_slow_path(start == NULL)) {
        return NJS_TOKEN_ERROR;
    }

    dst = start;
    cp_pair = 0;

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
                /*
                 * A character after "u" can be safely tested here
                 * because there is always a closing quote at the
                 * end of string: ...\u".
                 */

                if (*src != '{') {
                    hex_length = 4;
                    goto hex_length;
                }

                src++;
                hex_length = 0;
                hex_end = end;

                goto hex;

            case 'x':
                hex_length = 2;
                goto hex_length;

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
                if (c >= 0x80) {
                    goto utf8_copy;
                }

                break;
            }
        }

        if (c < 0x80) {
            *dst++ = c;

            continue;
        }

    utf8_copy:

        src--;

        cp = njs_utf8_safe_decode2(&src, end);
        dst = njs_utf8_encode(dst, cp);

        continue;

    hex_length:

        hex_end = src + hex_length;

    hex:
        cp = njs_number_hex_parse(&src, hex_end);

        /* Skip '}' character. */

        if (hex_length == 0) {
            src++;
        }

        if (cp_pair != 0) {
            if (njs_fast_path(njs_surrogate_trailing(cp))) {
                cp = njs_string_surrogate_pair(cp_pair, cp);

            } else if (njs_slow_path(njs_surrogate_leading(cp))) {
                cp = NJS_UTF8_REPLACEMENT;

                dst = njs_utf8_encode(dst, (uint32_t) cp);

            } else {
                dst = njs_utf8_encode(dst, NJS_UTF8_REPLACEMENT);
            }

            cp_pair = 0;

        } else if (njs_surrogate_any(cp)) {
            if (cp <= 0xdbff && src[0] == '\\' && src[1] == 'u') {
                cp_pair = cp;
                continue;
            }

            cp = NJS_UTF8_REPLACEMENT;
        }

        dst = njs_utf8_encode(dst, (uint32_t) cp);
        if (njs_slow_path(dst == NULL)) {
            njs_parser_syntax_error(vm, parser,
                                    "Invalid Unicode code point \"%V\"",
                                    njs_parser_text(parser));

            return NJS_TOKEN_ILLEGAL;
        }
    }

    if (length > NJS_STRING_MAP_STRIDE && length != size) {
        njs_string_offset_map_init(start, size);
    }

    return NJS_TOKEN_STRING;
}


static njs_int_t
njs_parser_escape_string_calc_length(njs_vm_t *vm, njs_parser_t *parser,
    size_t *out_size, size_t *out_length)
{
    size_t        size, length, hex_length;
    uint64_t      cp, cp_pair;
    njs_str_t     *string;
    const u_char  *ptr, *src, *end, *hex_end;

    size = 0;
    length = 0;
    cp_pair = 0;

    string = njs_parser_text(parser);
    src = string->start;
    end = src + string->length;

    while (src < end) {

        if (*src == '\\') {
            src++;

            switch (*src) {
            case 'u':
                src++;

                if (*src != '{') {
                    hex_length = 4;
                    goto hex_length;
                }

                src++;
                hex_length = 0;
                hex_end = end;

                goto hex;

            case 'x':
                src++;
                hex_length = 2;
                goto hex_length;

            case '\r':
                src++;

                if (*src == '\n') {
                    src++;
                }

                continue;

            case '\n':
                src++;
                continue;

            default:
                break;
            }
        }

        if (*src >= 0x80) {
            cp = njs_utf8_safe_decode2(&src, end);

            size += njs_utf8_size(cp);
            length++;

            continue;
        }

        src++;
        size++;
        length++;

        continue;

    hex_length:

        hex_end = src + hex_length;

        if (njs_slow_path(hex_end > end)) {
            goto invalid;
        }

    hex:

        ptr = src;
        cp = njs_number_hex_parse(&src, hex_end);

        if (hex_length != 0) {
            if (src != hex_end) {
                goto invalid;
            }

        } else {
            if (src == ptr || (src - ptr) > 6) {
                goto invalid;
            }

            if (src == end || *src++ != '}') {
                goto invalid;
            }
        }

        if (cp_pair != 0) {
            if (njs_fast_path(njs_surrogate_trailing(cp))) {
                cp = njs_string_surrogate_pair(cp_pair, cp);

            } else if (njs_slow_path(njs_surrogate_leading(cp))) {
                cp = NJS_UTF8_REPLACEMENT;

                size += njs_utf8_size(cp);
                length++;

            } else {
                size += njs_utf8_size(NJS_UTF8_REPLACEMENT);
                length++;
            }

            cp_pair = 0;

        } else if (njs_surrogate_any(cp)) {
            if (cp <= 0xdbff && src[0] == '\\' && src[1] == 'u') {
                cp_pair = cp;
                continue;
            }

            cp = NJS_UTF8_REPLACEMENT;
        }

        size += njs_utf8_size(cp);
        length++;
    }

    *out_size = size;
    *out_length = length;

    return NJS_OK;

invalid:

    njs_parser_syntax_error(vm, parser, "Invalid Unicode code point \"%V\"",
                            njs_parser_text(parser));

    return NJS_ERROR;
}
