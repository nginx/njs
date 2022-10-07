
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) Dmitry Volyntsev
 * Copyright (C) NGINX, Inc.
 */


#include <njs_main.h>


static njs_declaration_t *njs_variable_scope_function_add(njs_parser_t *parser,
    njs_parser_scope_t *scope);
static njs_parser_scope_t *njs_variable_scope_find(njs_parser_t *parser,
     njs_parser_scope_t *scope, uintptr_t unique_id, njs_variable_type_t type);
static njs_variable_t *njs_variable_alloc(njs_vm_t *vm, uintptr_t unique_id,
    njs_variable_type_t type);


njs_variable_t *
njs_variable_add(njs_parser_t *parser, njs_parser_scope_t *scope,
    uintptr_t unique_id, njs_variable_type_t type)
{
    njs_parser_scope_t  *root;

    root = njs_variable_scope_find(parser, scope, unique_id, type);
    if (njs_slow_path(root == NULL)) {
        njs_parser_ref_error(parser, "scope not found");
        return NULL;
    }

    return njs_variable_scope_add(parser, root, scope, unique_id, type,
                                  NJS_INDEX_NONE);
}


njs_variable_t *
njs_variable_function_add(njs_parser_t *parser, njs_parser_scope_t *scope,
    uintptr_t unique_id, njs_variable_type_t type)
{
    njs_bool_t             ctor;
    njs_variable_t         *var;
    njs_declaration_t      *declr;
    njs_parser_scope_t     *root;
    njs_function_lambda_t  *lambda;

    root = njs_variable_scope_find(parser, scope, unique_id, type);
    if (njs_slow_path(root == NULL)) {
        njs_parser_ref_error(parser, "scope not found");
        return NULL;
    }

    var = njs_variable_scope_add(parser, root, scope, unique_id, type,
                                 NJS_INDEX_ERROR);
    if (njs_slow_path(var == NULL)) {
        return NULL;
    }

    root = njs_function_scope(scope);
    if (njs_slow_path(scope == NULL)) {
        return NULL;
    }

    ctor = parser->node->token_type != NJS_TOKEN_ASYNC_FUNCTION_DECLARATION;

    lambda = njs_function_lambda_alloc(parser->vm, ctor);
    if (lambda == NULL) {
        return NULL;
    }

    njs_set_invalid(&var->value);
    var->value.data.u.lambda = lambda;

    declr = njs_variable_scope_function_add(parser, root);
    if (njs_slow_path(declr == NULL)) {
        return NULL;
    }

    var->index = njs_scope_index(root->type, root->items, NJS_LEVEL_LOCAL,
                                 type);

    declr->value = &var->value;
    declr->index = var->index;

    root->items++;

    var->type = NJS_VARIABLE_FUNCTION;
    var->function = 1;

    return var;
}


static njs_declaration_t *
njs_variable_scope_function_add(njs_parser_t *parser, njs_parser_scope_t *scope)
{
    if (scope->declarations == NULL) {
        scope->declarations = njs_arr_create(parser->vm->mem_pool, 1,
                                             sizeof(njs_declaration_t));
        if (njs_slow_path(scope->declarations == NULL)) {
            return NULL;
        }
    }

    return njs_arr_add(scope->declarations);
}


static njs_parser_scope_t *
njs_variable_scope(njs_parser_scope_t *scope, uintptr_t unique_id,
    njs_variable_t **retvar, njs_variable_type_t type)
{
    njs_variable_t       *var;
    njs_rbtree_node_t    *node;
    njs_variable_node_t  var_node;

    *retvar = NULL;

    var_node.key = unique_id;

    do {
        node = njs_rbtree_find(&scope->variables, &var_node.node);

        if (node != NULL) {
            var = ((njs_variable_node_t *) node)->variable;

            if (var->type != NJS_VARIABLE_CATCH || type != NJS_VARIABLE_VAR) {
                *retvar = var;
                return scope;
            }
        }

        if (scope->type == NJS_SCOPE_GLOBAL
            || scope->type == NJS_SCOPE_FUNCTION)
        {
            return scope;
        }

        scope = scope->parent;

    } while (scope != NULL);

    return NULL;
}


static njs_parser_scope_t *
njs_variable_scope_find(njs_parser_t *parser, njs_parser_scope_t *scope,
     uintptr_t unique_id, njs_variable_type_t type)
{
    njs_bool_t               module;
    njs_variable_t           *var;
    njs_parser_scope_t       *root;
    const njs_lexer_entry_t  *entry;

    root = njs_variable_scope(scope, unique_id, &var, type);
    if (njs_slow_path(root == NULL)) {
        return NULL;
    }

    switch (type) {
    case NJS_VARIABLE_CONST:
    case NJS_VARIABLE_LET:
        if (scope->type == NJS_SCOPE_GLOBAL
            && parser->undefined_id == unique_id)
        {
            goto failed;
        }

        if (root != scope) {
            return scope;
        }

        if (var != NULL && var->scope == root) {
            if (var->self) {
                var->function = 0;
                return scope;
            }

            goto failed;
        }

        return scope;

    case NJS_VARIABLE_VAR:
    case NJS_VARIABLE_FUNCTION:
        break;

    default:
        return scope;
    }

    if (type == NJS_VARIABLE_FUNCTION) {
        root = scope;
    }

    if (var == NULL) {
        return root;
    }

    if (var->type == NJS_VARIABLE_LET || var->type == NJS_VARIABLE_CONST) {
        goto failed;
    }

    if (var->original->type == NJS_SCOPE_BLOCK) {
        if (type == NJS_VARIABLE_FUNCTION
            || var->type == NJS_VARIABLE_FUNCTION)
        {
            if (var->original == root) {
                goto failed;
            }
        }
    }

    if (type != NJS_VARIABLE_FUNCTION
        && var->type != NJS_VARIABLE_FUNCTION)
    {
        return var->scope;
    }

    if (root != scope) {
        return root;
    }

    if (scope->parent == NULL) {
        module = parser->vm->options.module || parser->module;

        if (module) {
            if (type == NJS_VARIABLE_FUNCTION
                || var->type == NJS_VARIABLE_FUNCTION)
            {
                goto failed;
            }
        }

    }

    return root;

failed:

    entry = njs_lexer_entry(unique_id);

    njs_parser_syntax_error(parser, "\"%V\" has already been declared",
                            &entry->name);
    return NULL;
}


njs_variable_t *
njs_variable_scope_add(njs_parser_t *parser, njs_parser_scope_t *scope,
    njs_parser_scope_t *original, uintptr_t unique_id,
    njs_variable_type_t type, njs_index_t index)
{
    njs_variable_t       *var;
    njs_rbtree_node_t    *node;
    njs_parser_scope_t   *root;
    njs_variable_node_t  var_node, *var_node_new;

    var_node.key = unique_id;

    node = njs_rbtree_find(&scope->variables, &var_node.node);

    if (node != NULL) {
        return ((njs_variable_node_t *) node)->variable;
    }

    var = njs_variable_alloc(parser->vm, unique_id, type);
    if (njs_slow_path(var == NULL)) {
        goto memory_error;
    }

    var->scope = scope;
    var->index = index;
    var->original = original;

    if (index == NJS_INDEX_NONE) {
        root = njs_function_scope(scope);
        if (njs_slow_path(scope == NULL)) {
            return NULL;
        }

        var->index = njs_scope_index(root->type, root->items, NJS_LEVEL_LOCAL,
                                     type);
        root->items++;
    }

    var_node_new = njs_variable_node_alloc(parser->vm, var, unique_id);
    if (njs_slow_path(var_node_new == NULL)) {
        goto memory_error;
    }

    njs_rbtree_insert(&scope->variables, &var_node_new->node);

    return var;

memory_error:

    njs_memory_error(parser->vm);

    return NULL;
}


njs_variable_t *
njs_label_add(njs_vm_t *vm, njs_parser_scope_t *scope, uintptr_t unique_id)
{
    njs_variable_t       *label;
    njs_rbtree_node_t    *node;
    njs_variable_node_t  var_node, *var_node_new;

    var_node.key = unique_id;

    node = njs_rbtree_find(&scope->labels, &var_node.node);

    if (node != NULL) {
        return ((njs_variable_node_t *) node)->variable;
    }

    label = njs_variable_alloc(vm, unique_id, NJS_VARIABLE_CONST);
    if (njs_slow_path(label == NULL)) {
        goto memory_error;
    }

    var_node_new = njs_variable_node_alloc(vm, label, unique_id);
    if (njs_slow_path(var_node_new == NULL)) {
        goto memory_error;
    }

    njs_rbtree_insert(&scope->labels, &var_node_new->node);

    return label;

memory_error:

    njs_memory_error(vm);

    return NULL;
}


njs_int_t
njs_label_remove(njs_vm_t *vm, njs_parser_scope_t *scope, uintptr_t unique_id)
{
    njs_rbtree_node_t    *node;
    njs_variable_node_t  var_node;

    var_node.key = unique_id;

    node = njs_rbtree_find(&scope->labels, &var_node.node);
    if (njs_slow_path(node == NULL)) {
        njs_internal_error(vm, "failed to find label while removing");
        return NJS_ERROR;
    }

    njs_rbtree_delete(&scope->labels, (njs_rbtree_part_t *) node);
    njs_variable_node_free(vm, (njs_variable_node_t *) node);

    return NJS_OK;
}


njs_bool_t
njs_variable_closure_test(njs_parser_scope_t *root, njs_parser_scope_t *scope)
{
    if (root == scope) {
        return 0;
    }

    do {
        if (root->type == NJS_SCOPE_FUNCTION) {
            return 1;
        }

        root = root->parent;

    } while (root != scope);

    return 0;
}


njs_variable_t *
njs_variable_resolve(njs_vm_t *vm, njs_parser_node_t *node)
{
    njs_rbtree_node_t         *rb_node;
    njs_parser_scope_t        *scope;
    njs_variable_node_t       var_node;
    njs_variable_reference_t  *ref;

    ref = &node->u.reference;
    scope = node->scope;

    var_node.key = ref->unique_id;

    do {
        rb_node = njs_rbtree_find(&scope->variables, &var_node.node);

        if (rb_node != NULL) {
            return ((njs_variable_node_t *) rb_node)->variable;
        }

        scope = scope->parent;

    } while (scope != NULL);

    return NULL;
}


static njs_index_t
njs_variable_closure(njs_vm_t *vm, njs_variable_t *var,
    njs_parser_scope_t *scope)
{
    njs_index_t               index, prev_index, *idx;
    njs_level_type_t          type;
    njs_rbtree_node_t         *rb_node;
    njs_parser_scope_t        **p;
    njs_parser_rbtree_node_t  *parse_node, ref_node;
#define NJS_VAR_MAX_DEPTH     32
    njs_parser_scope_t        *list[NJS_VAR_MAX_DEPTH];

    ref_node.key = var->unique_id;

    p = list;

    do {
        if (njs_slow_path(p == &list[NJS_VAR_MAX_DEPTH - 1])) {
            njs_error(vm, "maximum depth of nested functions is reached");
            return NJS_INDEX_ERROR;
        }

        if (scope->type == NJS_SCOPE_FUNCTION) {
            *p++ = scope;
        }

        scope = scope->parent;

    } while (scope != var->scope && scope->type != NJS_SCOPE_GLOBAL);

    prev_index = var->index;

    while (p != list) {
        p--;

        scope = *p;

        rb_node = njs_rbtree_find(&scope->references, &ref_node.node);

        parse_node = ((njs_parser_rbtree_node_t *) rb_node);

        type = NJS_LEVEL_LOCAL;

        if (parse_node != NULL) {
            type = njs_scope_index_type(parse_node->index);

            if (p != list && parse_node->index != 0) {
                prev_index = parse_node->index;
                continue;
            }
        }

        if (type != NJS_LEVEL_CLOSURE) {
            /* Create new closure for scope. */

            index = njs_scope_index(scope->type, scope->closures->items,
                                    NJS_LEVEL_CLOSURE, var->type);
            if (njs_slow_path(index == NJS_INDEX_ERROR)) {
                return NJS_INDEX_ERROR;
            }

            idx = njs_arr_add(scope->closures);
            if (njs_slow_path(idx == NULL)) {
                return NJS_INDEX_ERROR;
            }

            *idx = prev_index;

            if (parse_node == NULL) {
                /* Create new reference for closure. */

                parse_node = njs_mp_alloc(vm->mem_pool,
                                          sizeof(njs_parser_rbtree_node_t));
                if (njs_slow_path(parse_node == NULL)) {
                    return NJS_INDEX_ERROR;
                }

                parse_node->key = var->unique_id;

                njs_rbtree_insert(&scope->references, &parse_node->node);
            }

            parse_node->index = index;
        }

        prev_index = parse_node->index;
    }

    return prev_index;
}


njs_variable_t *
njs_variable_reference(njs_vm_t *vm, njs_parser_node_t *node)
{
    njs_bool_t                closure;
    njs_rbtree_node_t         *rb_node;
    njs_parser_scope_t        *scope;
    njs_parser_rbtree_node_t  *parse_node, ref_node;
    njs_variable_reference_t  *ref;

    ref = &node->u.reference;
    scope = node->scope;

    if (ref->variable == NULL) {
        ref->variable = njs_variable_resolve(vm, node);
        if (njs_slow_path(ref->variable == NULL)) {
            ref->not_defined = 1;

            return NULL;
        }
    }

    closure = njs_variable_closure_test(node->scope, ref->variable->scope);
    ref->scope = node->scope;

    ref_node.key = ref->unique_id;

    rb_node = njs_rbtree_find(&scope->references, &ref_node.node);
    if (njs_slow_path(rb_node == NULL)) {
        return NULL;
    }

    parse_node = ((njs_parser_rbtree_node_t *) rb_node);

    if (parse_node->index != NJS_INDEX_NONE) {
        node->index = parse_node->index;

        return ref->variable;
    }

    if (!closure) {
        node->index = ref->variable->index;

        return ref->variable;
    }

    ref->variable->closure = closure;

    node->index = njs_variable_closure(vm, ref->variable, scope);
    if (njs_slow_path(node->index == NJS_INDEX_ERROR)) {
        return NULL;
    }

    return ref->variable;
}


njs_variable_t *
njs_label_find(njs_vm_t *vm, njs_parser_scope_t *scope, uintptr_t unique_id)
{
    njs_rbtree_node_t    *node;
    njs_variable_node_t  var_node;

    var_node.key = unique_id;

    do {
        node = njs_rbtree_find(&scope->labels, &var_node.node);

        if (node != NULL) {
            return ((njs_variable_node_t *) node)->variable;
        }

        scope = scope->parent;

    } while (scope != NULL);

    return NULL;
}


static njs_variable_t *
njs_variable_alloc(njs_vm_t *vm, uintptr_t unique_id, njs_variable_type_t type)
{
    njs_variable_t  *var;

    var = njs_mp_zalloc(vm->mem_pool, sizeof(njs_variable_t));
    if (njs_slow_path(var == NULL)) {
        njs_memory_error(vm);
        return NULL;
    }

    var->unique_id = unique_id;
    var->type = type;

    return var;
}


njs_int_t
njs_name_copy(njs_vm_t *vm, njs_str_t *dst, const njs_str_t *src)
{
    dst->length = src->length;

    dst->start = njs_mp_alloc(vm->mem_pool, src->length);

    if (njs_fast_path(dst->start != NULL)) {
        (void) memcpy(dst->start, src->start, src->length);

        return NJS_OK;
    }

    njs_memory_error(vm);

    return NJS_ERROR;
}
