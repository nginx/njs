
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
#include <njs_error.h>
#include <string.h>


typedef struct {
    nxt_lvlhsh_query_t  lhq;
    njs_variable_t      *variable;
    njs_parser_scope_t  *scope;
} njs_variable_scope_t;


static njs_ret_t njs_variable_find(njs_vm_t *vm, njs_parser_node_t *node,
    njs_variable_scope_t *vs);
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


const nxt_lvlhsh_proto_t  njs_variables_hash_proto
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

    lhq.replace = vm->accumulative;
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

    njs_parser_syntax_error(vm, parser, "Identifier \"%.*s\" "
                            "has already been declared",
                            (int) lhq.key.length, lhq.key.start);

    return NULL;
}


static nxt_int_t
njs_reference_hash_test(nxt_lvlhsh_query_t *lhq, void *data)
{
    njs_parser_node_t  *node;

    node = data;

    if (nxt_strstr_eq(&lhq->key, &node->u.variable_name)) {
        return NXT_OK;
    }

    return NXT_DECLINED;
}


const nxt_lvlhsh_proto_t  njs_reference_hash_proto
    nxt_aligned(64) =
{
    NXT_LVLHSH_DEFAULT,
    0,
    njs_reference_hash_test,
    njs_lvlhsh_alloc,
    njs_lvlhsh_free,
};


njs_ret_t
njs_variable_reference(njs_vm_t *vm, njs_parser_t *parser,
    njs_parser_node_t *node, nxt_bool_t reference)
{
    njs_ret_t           ret;
    nxt_lvlhsh_query_t  lhq;

    ret = njs_name_copy(vm, &node->u.variable_name, &parser->lexer->text);

    if (nxt_fast_path(ret == NXT_OK)) {
        node->variable_name_hash = parser->lexer->key_hash;
        node->scope = parser->scope;
        node->reference = reference;

        lhq.key_hash = node->variable_name_hash;
        lhq.key = node->u.variable_name;
        lhq.proto = &njs_reference_hash_proto;
        lhq.replace = 0;
        lhq.value = node;
        lhq.pool = vm->mem_cache_pool;

        ret = nxt_lvlhsh_insert(&parser->scope->references, &lhq);

        if (nxt_slow_path(ret != NXT_ERROR)) {
            ret = NXT_OK;
        }
    }

    return ret;
}


njs_ret_t
njs_variables_scope_reference(njs_vm_t *vm, njs_parser_scope_t *scope)
{
    njs_ret_t           ret;
    nxt_queue_t         *nested;
    njs_variable_t      *var;
    nxt_queue_link_t    *lnk;
    njs_parser_node_t   *node;
    nxt_lvlhsh_each_t   lhe;

    nested = &scope->nested;

    for (lnk = nxt_queue_first(nested);
         lnk != nxt_queue_tail(nested);
         lnk = nxt_queue_next(lnk))
    {
        scope = nxt_queue_link_data(lnk, njs_parser_scope_t, link);

        ret = njs_variables_scope_reference(vm, scope);
        if (nxt_slow_path(ret != NXT_OK)) {
            return NXT_ERROR;
        }

        nxt_lvlhsh_each_init(&lhe, &njs_variables_hash_proto);

        for ( ;; ) {
            node = nxt_lvlhsh_each(&scope->references, &lhe);

            if (node == NULL) {
                break;
            }

            var = njs_variable_get(vm, node);
            if (nxt_slow_path(var == NULL)) {
                return NXT_ERROR;
            }
        }
    }

    return NXT_OK;
}


njs_index_t
njs_variable_typeof(njs_vm_t *vm, njs_parser_node_t *node)
{
    nxt_int_t             ret;
    njs_variable_scope_t  vs;

    if (node->index != NJS_INDEX_NONE) {
        return node->index;
    }

    ret = njs_variable_find(vm, node, &vs);

    if (nxt_fast_path(ret == NXT_OK)) {
        return vs.variable->index;
    }

    return NJS_INDEX_NONE;
}


njs_index_t
njs_variable_index(njs_vm_t *vm, njs_parser_node_t *node)
{
    njs_variable_t  *var;

    if (node->index != NJS_INDEX_NONE) {
        return node->index;
    }

    var = njs_variable_get(vm, node);

    if (nxt_fast_path(var != NULL)) {
        return var->index;
    }

    return NJS_INDEX_ERROR;
}


njs_variable_t *
njs_variable_get(njs_vm_t *vm, njs_parser_node_t *node)
{
    nxt_int_t             ret;
    nxt_uint_t            n;
    nxt_array_t           *values;
    njs_index_t           index;
    njs_value_t           *value;
    njs_variable_t        *var;
    njs_variable_scope_t  vs;

    ret = njs_variable_find(vm, node, &vs);

    if (nxt_slow_path(ret != NXT_OK)) {
        goto not_found;
    }

    n = 0;

    if (vs.scope->type > NJS_SCOPE_GLOBAL) {
        n = (node->scope->nesting != vs.scope->nesting);
    }

    var = vs.variable;
    index = var->index;

    if (index != NJS_INDEX_NONE) {

        if (n == 0 || njs_scope_type(index) != NJS_SCOPE_ARGUMENTS) {
            node->index = index;

            return var;
        }

        vs.scope->argument_closures++;
        index = (index >> NJS_SCOPE_SHIFT) + 1;

        if (index > 255 || vs.scope->argument_closures == 0) {
            njs_exception_internal_error(vm, "too many argument closures",
                                         NULL);

            return NULL;
        }

        var->argument = index;
    }

    if (node->reference && var->type <= NJS_VARIABLE_LET) {
        goto not_found;
    }

    if (vm->accumulative && vs.scope->type == NJS_SCOPE_GLOBAL) {
        /*
         * When non-clonable VM runs in accumulative mode all
         * global variables should be allocated in absolute scope
         * to share them among consecutive VM invocations.
         */
        value = nxt_mem_cache_align(vm->mem_cache_pool, sizeof(njs_value_t),
                                    sizeof(njs_value_t));
        if (nxt_slow_path(value == NULL)) {
            return NULL;
        }

        index = (njs_index_t) value;

    } else {
        values = vs.scope->values[n];

        if (values == NULL) {
            values = nxt_array_create(4, sizeof(njs_value_t),
                                      &njs_array_mem_proto, vm->mem_cache_pool);
            if (nxt_slow_path(values == NULL)) {
                return NULL;
            }

            vs.scope->values[n] = values;
        }

        value = nxt_array_add(values, &njs_array_mem_proto, vm->mem_cache_pool);
        if (nxt_slow_path(value == NULL)) {
            return NULL;
        }

        index = vs.scope->next_index[n];
        vs.scope->next_index[n] += sizeof(njs_value_t);
    }

    if (njs_is_object(&var->value)) {
        *value = var->value;

    } else {
        *value = njs_value_void;
    }

    var->index = index;
    node->index = index;

    return var;

not_found:

    njs_parser_ref_error(vm, vm->parser, "\"%.*s\" is not defined",
                         (int) vs.lhq.key.length, vs.lhq.key.start);

    return NULL;
}


static njs_ret_t
njs_variable_find(njs_vm_t *vm, njs_parser_node_t *node,
    njs_variable_scope_t *vs)
{
    njs_parser_scope_t  *scope, *parent, *previous;

    vs->lhq.key_hash = node->variable_name_hash;
    vs->lhq.key = node->u.variable_name;
    vs->lhq.proto = &njs_variables_hash_proto;

    previous = NULL;
    scope = node->scope;

    for ( ;; ) {
        if (nxt_lvlhsh_find(&scope->variables, &vs->lhq) == NXT_OK) {
            vs->variable = vs->lhq.value;

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

            vs->scope = scope;

            return NXT_OK;
        }

        parent = scope->parent;

        if (parent == NULL) {
            /* A global scope. */
            vs->scope = scope;

            return NXT_DECLINED;
        }

        previous = scope;
        scope = parent;
    }
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
