
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) Dmitry Volyntsev
 * Copyright (C) NGINX, Inc.
 */


#include <njs_main.h>


static njs_variable_t *njs_variable_scope_add(njs_vm_t *vm,
    njs_parser_scope_t *scope, uintptr_t unique_id, njs_variable_type_t type);
static njs_int_t njs_variable_reference_resolve(njs_vm_t *vm,
    njs_variable_reference_t *vr, njs_parser_scope_t *node_scope);
static njs_variable_t *njs_variable_alloc(njs_vm_t *vm, uintptr_t unique_id,
    njs_variable_type_t type);


njs_variable_t *
njs_variable_add(njs_vm_t *vm, njs_parser_scope_t *scope, uintptr_t unique_id,
    njs_variable_type_t type)
{
    njs_variable_t  *var;

    var = njs_variable_scope_add(vm, scope, unique_id, type);
    if (njs_slow_path(var == NULL)) {
        return NULL;
    }

    if (type == NJS_VARIABLE_VAR && scope->type == NJS_SCOPE_BLOCK) {
        /* A "var" declaration is stored in function or global scope. */
        do {
            scope = scope->parent;

            var = njs_variable_scope_add(vm, scope, unique_id, type);
            if (njs_slow_path(var == NULL)) {
                return NULL;
            }

        } while (scope->type == NJS_SCOPE_BLOCK);
    }

    if (type == NJS_VARIABLE_FUNCTION) {
        var->type = type;
    }

    return var;
}


njs_int_t
njs_variables_copy(njs_vm_t *vm, njs_rbtree_t *variables,
    njs_rbtree_t *prev_variables)
{
    njs_rbtree_node_t    *node;
    njs_variable_node_t  *var_node;

    node = njs_rbtree_min(prev_variables);

    while (njs_rbtree_is_there_successor(prev_variables, node)) {
        var_node = (njs_variable_node_t *) node;

        var_node = njs_variable_node_alloc(vm, var_node->variable,
                                           var_node->key);
        if (njs_slow_path(var_node == NULL)) {
            njs_memory_error(vm);
            return NJS_ERROR;
        }

        njs_rbtree_insert(variables, &var_node->node);

        node = njs_rbtree_node_successor(prev_variables, node);
    }

    return NJS_OK;
}


static njs_variable_t *
njs_variable_scope_add(njs_vm_t *vm, njs_parser_scope_t *scope,
    uintptr_t unique_id, njs_variable_type_t type)
{
    njs_variable_t           *var;
    njs_rbtree_node_t        *node;
    njs_variable_node_t      var_node, *var_node_new;
    const njs_lexer_entry_t  *entry;

    var_node.key = unique_id;

    node = njs_rbtree_find(&scope->variables, &var_node.node);

    if (node != NULL) {
        var = ((njs_variable_node_t *) node)->variable;

        if (scope->module || scope->type == NJS_SCOPE_BLOCK) {

            if (type == NJS_VARIABLE_FUNCTION
                || var->type == NJS_VARIABLE_FUNCTION)
            {
                goto fail;
            }
        }

        if (scope->type == NJS_SCOPE_GLOBAL) {

            if (vm->options.module) {
                if (type == NJS_VARIABLE_FUNCTION
                    || var->type == NJS_VARIABLE_FUNCTION)
                {
                    goto fail;
                }
            }
        }

        return var;
    }

    var = njs_variable_alloc(vm, unique_id, type);
    if (njs_slow_path(var == NULL)) {
        goto memory_error;
    }

    var_node_new = njs_variable_node_alloc(vm, var, unique_id);
    if (njs_slow_path(var_node_new == NULL)) {
        goto memory_error;
    }

    njs_rbtree_insert(&scope->variables, &var_node_new->node);

    return var;

memory_error:

    njs_memory_error(vm);

    return NULL;

fail:

    entry = njs_lexer_entry(unique_id);

    njs_parser_syntax_error(vm->parser,
                            "\"%V\" has already been declared",
                            &entry->name);
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


njs_int_t
njs_variable_reference(njs_vm_t *vm, njs_parser_scope_t *scope,
    njs_parser_node_t *node, uintptr_t unique_id, njs_reference_type_t type)
{
    njs_variable_reference_t  *vr;
    njs_parser_rbtree_node_t  *rb_node;

    vr = &node->u.reference;

    vr->unique_id = unique_id;
    vr->type = type;

    rb_node = njs_mp_alloc(vm->mem_pool, sizeof(njs_parser_rbtree_node_t));
    if (njs_slow_path(rb_node == NULL)) {
        return NJS_ERROR;
    }

    rb_node->key = unique_id;
    rb_node->parser_node = node;

    njs_rbtree_insert(&scope->references, &rb_node->node);

    return NJS_OK;
}


static njs_int_t
njs_variables_scope_resolve(njs_vm_t *vm, njs_parser_scope_t *scope,
    njs_bool_t closure)
{
    njs_int_t                 ret;
    njs_queue_t               *nested;
    njs_queue_link_t          *lnk;
    njs_rbtree_node_t         *rb_node;
    njs_parser_node_t         *node;
    njs_parser_rbtree_node_t  *parser_rb_node;
    njs_variable_reference_t  *vr;

    nested = &scope->nested;

    for (lnk = njs_queue_first(nested);
         lnk != njs_queue_tail(nested);
         lnk = njs_queue_next(lnk))
    {
        scope = njs_queue_link_data(lnk, njs_parser_scope_t, link);

        ret = njs_variables_scope_resolve(vm, scope, closure);
        if (njs_slow_path(ret != NJS_OK)) {
            return NJS_ERROR;
        }

        rb_node = njs_rbtree_min(&scope->references);

        while (njs_rbtree_is_there_successor(&scope->references, rb_node)) {
            parser_rb_node = (njs_parser_rbtree_node_t *) rb_node;
            node = parser_rb_node->parser_node;

            if (node == NULL) {
                break;
            }

            vr = &node->u.reference;

            if (closure) {
                ret = njs_variable_reference_resolve(vm, vr, node->scope);
                if (njs_slow_path(ret != NJS_OK)) {
                    goto next;
                }

                if (vr->scope_index == NJS_SCOPE_INDEX_LOCAL) {
                    goto next;
                }
            }

            (void) njs_variable_resolve(vm, node);

        next:

            rb_node = njs_rbtree_node_successor(&scope->references, rb_node);
        }
    }

    return NJS_OK;
}


njs_int_t
njs_variables_scope_reference(njs_vm_t *vm, njs_parser_scope_t *scope)
{
    njs_int_t  ret;

    /*
     * Calculating proper scope types for variables.
     * A variable is considered to be local variable if it is referenced
     * only in the local scope (reference and definition nestings are the same).
     */

    ret = njs_variables_scope_resolve(vm, scope, 1);
    if (njs_slow_path(ret != NJS_OK)) {
        return NJS_ERROR;
    }

    ret = njs_variables_scope_resolve(vm, scope, 0);
    if (njs_slow_path(ret != NJS_OK)) {
        return NJS_ERROR;
    }

    return NJS_OK;
}


njs_index_t
njs_variable_index(njs_vm_t *vm, njs_parser_node_t *node)
{
    njs_variable_t  *var;

    if (node->index != NJS_INDEX_NONE) {
        return node->index;
    }

    var = njs_variable_resolve(vm, node);

    if (njs_fast_path(var != NULL)) {
        return var->index;
    }

    return NJS_INDEX_NONE;
}


njs_variable_t *
njs_variable_resolve(njs_vm_t *vm, njs_parser_node_t *node)
{
    njs_int_t                 ret;
    njs_uint_t                scope_index;
    njs_index_t               index;
    njs_variable_t            *var;
    njs_variable_reference_t  *vr;

    vr = &node->u.reference;

    ret = njs_variable_reference_resolve(vm, vr, node->scope);

    if (njs_slow_path(ret != NJS_OK)) {
        node->u.reference.not_defined = 1;
        return NULL;
    }

    scope_index = vr->scope_index;

    var = vr->variable;
    index = var->index;

    if (index != NJS_INDEX_NONE) {

        if (scope_index == NJS_SCOPE_INDEX_LOCAL
            || njs_scope_type(index) != NJS_SCOPE_ARGUMENTS)
        {
            node->index = index;

            return var;
        }

        vr->scope->argument_closures++;
        index = (index >> NJS_SCOPE_SHIFT) + 1;

        if (index > 255 || vr->scope->argument_closures == 0) {
            njs_internal_error(vm, "too many argument closures");

            return NULL;
        }

        var->argument = index;
    }

    index = njs_scope_next_index(vm, vr->scope, scope_index, &var->value);

    if (njs_slow_path(index == NJS_INDEX_ERROR)) {
        return NULL;
    }

    var->index = index;
    node->index = index;

    return var;
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


static njs_int_t
njs_variable_reference_resolve(njs_vm_t *vm, njs_variable_reference_t *vr,
    njs_parser_scope_t *node_scope)
{
    njs_rbtree_node_t    *node;
    njs_parser_scope_t   *scope, *previous;
    njs_variable_node_t  var_node;

    var_node.key = vr->unique_id;

    scope = node_scope;
    previous = NULL;

    for ( ;; ) {
        node = njs_rbtree_find(&scope->variables, &var_node.node);

        if (node != NULL) {
            vr->variable = ((njs_variable_node_t *) node)->variable;

            if (scope->type == NJS_SCOPE_BLOCK
                && vr->variable->type == NJS_VARIABLE_VAR)
            {
                scope = scope->parent;
                continue;
            }

            if (scope->type == NJS_SCOPE_SHIM) {
                scope = previous;

            } else {
                /*
                 * Variables declared in a block with "let" or "const"
                 * keywords are actually stored in function or global scope.
                 */
                while (scope->type == NJS_SCOPE_BLOCK) {
                    scope = scope->parent;
                }
            }

            vr->scope = scope;

            vr->scope_index = NJS_SCOPE_INDEX_LOCAL;

            if (vr->scope->type > NJS_SCOPE_GLOBAL
                && node_scope->nesting != vr->scope->nesting)
            {
                vr->scope_index = NJS_SCOPE_INDEX_CLOSURE;
            }

            return NJS_OK;
        }

        if (scope->parent == NULL) {
            /* A global scope. */
            vr->scope = scope;

            return NJS_DECLINED;
        }

        previous = scope;
        scope = scope->parent;
    }
}


njs_index_t
njs_scope_next_index(njs_vm_t *vm, njs_parser_scope_t *scope,
    njs_uint_t scope_index, const njs_value_t *default_value)
{
    njs_arr_t    *values;
    njs_index_t  index;
    njs_value_t  *value;

    if (njs_scope_accumulative(vm, scope)) {
        /*
         * When non-clonable VM runs in accumulative mode all
         * global variables should be allocated in absolute scope
         * to share them among consecutive VM invocations.
         */
        value = njs_mp_align(vm->mem_pool, sizeof(njs_value_t),
                             sizeof(njs_value_t));
        if (njs_slow_path(value == NULL)) {
            return NJS_INDEX_ERROR;
        }

        index = (njs_index_t) value;

    } else {
        values = scope->values[scope_index];

        if (values == NULL) {
            values = njs_arr_create(vm->mem_pool, 4, sizeof(njs_value_t));
            if (njs_slow_path(values == NULL)) {
                return NJS_INDEX_ERROR;
            }

            scope->values[scope_index] = values;
        }

        value = njs_arr_add(values);
        if (njs_slow_path(value == NULL)) {
            return NJS_INDEX_ERROR;
        }

        index = scope->next_index[scope_index];
        scope->next_index[scope_index] += sizeof(njs_value_t);
    }

    *value = *default_value;

    return index;
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
