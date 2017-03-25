
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) NGINX, Inc.
 */

#include <nxt_auto_config.h>
#include <nxt_types.h>
#include <nxt_clang.h>
#include <nxt_alignment.h>
#include <nxt_string.h>
#include <nxt_stub.h>
#include <nxt_utf8.h>
#include <nxt_djb_hash.h>
#include <nxt_array.h>
#include <nxt_lvlhsh.h>
#include <nxt_random.h>
#include <nxt_mem_cache_pool.h>
#include <njscript.h>
#include <njs_vm.h>
#include <njs_variable.h>
#include <njs_parser.h>
#include <string.h>


static njs_variable_t *njs_variable_alloc(njs_vm_t *vm, nxt_str_t *name,
    njs_variable_type_t type);


static nxt_int_t
njs_variables_hash_test(nxt_lvlhsh_query_t *lhq, void *data)
{
    njs_variable_t  *var;

    var = data;

    if (nxt_strstr_eq(&lhq->key, &var->name)) {
        return NXT_OK;
    }

    return NXT_DECLINED;
}


static const nxt_lvlhsh_proto_t  njs_variables_hash_proto
    nxt_aligned(64) =
{
    NXT_LVLHSH_DEFAULT,
    0,
    njs_variables_hash_test,
    njs_lvlhsh_alloc,
    njs_lvlhsh_free,
};


njs_variable_t *
njs_builtin_add(njs_vm_t *vm, njs_parser_t *parser)
{
    nxt_int_t           ret;
    njs_variable_t      *var;
    njs_parser_scope_t  *scope;
    nxt_lvlhsh_query_t  lhq;

    lhq.key_hash = parser->lexer->key_hash;
    lhq.key = parser->lexer->text;
    lhq.proto = &njs_variables_hash_proto;

    scope = parser->scope;

    while (scope->type != NJS_SCOPE_GLOBAL) {
        scope = scope->parent;
    }

    if (nxt_lvlhsh_find(&scope->variables, &lhq) == NXT_OK) {
        var = lhq.value;

        return var;
    }

    var = njs_variable_alloc(vm, &lhq.key, NJS_VARIABLE_VAR);
    if (nxt_slow_path(var == NULL)) {
        return var;
    }

    lhq.replace = 0;
    lhq.value = var;
    lhq.pool = vm->mem_cache_pool;

    ret = nxt_lvlhsh_insert(&scope->variables, &lhq);

    if (nxt_fast_path(ret == NXT_OK)) {
        return var;
    }

    nxt_mem_cache_free(vm->mem_cache_pool, var->name.start);
    nxt_mem_cache_free(vm->mem_cache_pool, var);

    return NULL;
}


njs_variable_t *
njs_variable_add(njs_vm_t *vm, njs_parser_t *parser, njs_variable_type_t type)
{
    nxt_int_t           ret;
    njs_variable_t      *var;
    njs_parser_scope_t  *scope;
    nxt_lvlhsh_query_t  lhq;

    lhq.key_hash = parser->lexer->key_hash;
    lhq.key = parser->lexer->text;
    lhq.proto = &njs_variables_hash_proto;

    scope = parser->scope;

    if (type >= NJS_VARIABLE_VAR) {
        /*
         * A "var" and "function" declarations are
         * stored in function or global scope.
         */
        while (scope->type == NJS_SCOPE_BLOCK) {
            scope = scope->parent;
        }
    }

    var = njs_variable_alloc(vm, &lhq.key, type);
    if (nxt_slow_path(var == NULL)) {
        return var;
    }

    lhq.replace = 0;
    lhq.value = var;
    lhq.pool = vm->mem_cache_pool;

    ret = nxt_lvlhsh_insert(&scope->variables, &lhq);

    if (nxt_fast_path(ret == NXT_OK)) {
        return var;
    }

    nxt_mem_cache_free(vm->mem_cache_pool, var->name.start);
    nxt_mem_cache_free(vm->mem_cache_pool, var);

    if (ret == NXT_ERROR) {
        return NULL;
    }

    /* ret == NXT_DECLINED. */

    nxt_alert(&vm->trace, NXT_LEVEL_ERROR,
              "SyntaxError: Identifier \"%.*s\" has already been declared",
              (int) lhq.key.length, lhq.key.start);

    return NULL;
}


njs_ret_t
njs_variable_reference(njs_vm_t *vm, njs_parser_t *parser,
    njs_parser_node_t *node)
{
    njs_ret_t  ret;

    ret = njs_name_copy(vm, &node->u.variable_name, &parser->lexer->text);

    if (nxt_fast_path(ret == NXT_OK)) {
        node->variable_name_hash = parser->lexer->key_hash;
        node->scope = parser->scope;
    }

    return ret;
}


njs_variable_t *
njs_variable_get(njs_vm_t *vm, njs_parser_node_t *node,
    njs_name_reference_t reference)
{
    nxt_array_t         *values;
    njs_index_t         index;
    njs_value_t         *value;
    njs_variable_t      *var;
    njs_parser_scope_t  *scope, *parent, *inclusive;
    nxt_lvlhsh_query_t  lhq;

    lhq.key_hash = node->variable_name_hash;
    lhq.key = node->u.variable_name;
    lhq.proto = &njs_variables_hash_proto;

    inclusive = NULL;
    scope = node->scope;

    for ( ;; ) {
        if (nxt_lvlhsh_find(&scope->variables, &lhq) == NXT_OK) {
            var = lhq.value;

            if (scope->type == NJS_SCOPE_SHIM) {
                scope = inclusive;

            } else {
                /*
                 * Variables declared in a block with "let" or "const"
                 * keywords are actually stored in function or global scope.
                 */
                while (scope->type == NJS_SCOPE_BLOCK) {
                    scope = scope->parent;
                }
            }

            goto found;
        }

        parent = scope->parent;

        if (parent == NULL) {
            /* A global scope. */
            break;
        }

        inclusive = scope;
        scope = parent;
    }

    if (reference != NJS_NAME_TYPEOF) {
        goto not_found;
    }

    var = njs_variable_alloc(vm, &lhq.key, NJS_VARIABLE_TYPEOF);
    if (nxt_slow_path(var == NULL)) {
        return NULL;
    }

    var->index = NJS_INDEX_NONE;

    return var;

found:

    if (reference == NJS_NAME_REFERENCE && var->type == NJS_VARIABLE_TYPEOF) {
        goto not_found;
    }

    index = var->index;

    if (index != NJS_INDEX_NONE) {
        node->index = index;
        return var;
    }

    if (reference == NJS_NAME_REFERENCE && var->type <= NJS_VARIABLE_LET) {
        goto not_found;
    }

    values = scope->values;

    if (values == NULL) {
        values = nxt_array_create(4, sizeof(njs_value_t), &njs_array_mem_proto,
                                  vm->mem_cache_pool);
        if (nxt_slow_path(values == NULL)) {
            return NULL;
        }

        scope->values = values;
    }

    value = nxt_array_add(values, &njs_array_mem_proto, vm->mem_cache_pool);
    if (nxt_slow_path(value == NULL)) {
        return NULL;
    }

    if (njs_is_object(&var->value)) {
        *value = var->value;

    } else {
        *value = njs_value_void;
    }

    index = scope->next_index;
    scope->next_index += sizeof(njs_value_t);

    var->index = index;
    node->index = index;

    return var;

not_found:

    nxt_alert(&vm->trace, NXT_LEVEL_ERROR,
              "ReferenceError: \"%.*s\" is not defined",
              (int) lhq.key.length, lhq.key.start);

    return NULL;
}


njs_index_t
njs_variable_index(njs_vm_t *vm, njs_parser_node_t *node,
    njs_name_reference_t reference)
{
    njs_variable_t  *var;

    var = njs_variable_get(vm, node, reference);

    if (nxt_fast_path(var != NULL)) {
        return var->index;
    }

    return NJS_INDEX_ERROR;
}


static njs_variable_t *
njs_variable_alloc(njs_vm_t *vm, nxt_str_t *name, njs_variable_type_t type)
{
    njs_ret_t       ret;
    njs_variable_t  *var;

    var = nxt_mem_cache_zalloc(vm->mem_cache_pool, sizeof(njs_variable_t));
    if (nxt_slow_path(var == NULL)) {
        return NULL;
    }

    var->type = type;

    ret = njs_name_copy(vm, &var->name, name);

    if (nxt_fast_path(ret == NXT_OK)) {
        return var;
    }

    nxt_mem_cache_free(vm->mem_cache_pool, var);

    return NULL;
}


njs_ret_t
njs_name_copy(njs_vm_t *vm, nxt_str_t *dst, nxt_str_t *src)
{
    dst->length = src->length;

    dst->start = nxt_mem_cache_alloc(vm->mem_cache_pool, src->length);

    if (nxt_slow_path(dst->start != NULL)) {
        (void) memcpy(dst->start, src->start, src->length);

        return NXT_OK;
    }

    return NXT_ERROR;
}


nxt_str_t *
njs_vm_export_functions(njs_vm_t *vm)
{
    size_t             n;
    nxt_str_t          *ex, *export;
    njs_value_t        *value;
    njs_variable_t     *var;
    nxt_lvlhsh_each_t  lhe;

    n = 1;

    memset(&lhe, 0, sizeof(nxt_lvlhsh_each_t));
    lhe.proto = &njs_variables_hash_proto;

    for ( ;; ) {
        var = nxt_lvlhsh_each(&vm->variables_hash, &lhe);
        if (var == NULL) {
            break;
        }

        value = njs_global_variable_value(vm, var);

        if (njs_is_function(value) && !value->data.u.function->native) {
            n++;
        }
    }

    export = nxt_mem_cache_alloc(vm->mem_cache_pool, n * sizeof(nxt_str_t));
    if (nxt_slow_path(export == NULL)) {
        return NULL;
    }

    memset(&lhe, 0, sizeof(nxt_lvlhsh_each_t));
    lhe.proto = &njs_variables_hash_proto;

    ex = export;

    for ( ;; ) {
        var = nxt_lvlhsh_each(&vm->variables_hash, &lhe);
        if (var == NULL) {
            break;
        }

        value = njs_global_variable_value(vm, var);

        if (njs_is_function(value) && !value->data.u.function->native) {
            *ex = var->name;
            ex++;
        }
    }

    ex->length = 0;
    ex->start = NULL;

    return export;
}


njs_function_t *
njs_vm_function(njs_vm_t *vm, nxt_str_t *name)
{
    njs_value_t         *value;
    njs_variable_t      *var;
    nxt_lvlhsh_query_t  lhq;

    lhq.key_hash = nxt_djb_hash(name->start, name->length);
    lhq.key = *name;
    lhq.proto = &njs_variables_hash_proto;

    if (nxt_slow_path(nxt_lvlhsh_find(&vm->variables_hash, &lhq) != NXT_OK)) {
        return NULL;
    }

    var = lhq.value;

    value = njs_global_variable_value(vm, var);

    if (njs_is_function(value)) {
        return value->data.u.function;
    }

    return NULL;
}
