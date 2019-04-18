
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) Dmitry Volyntsev
 * Copyright (C) NGINX, Inc.
 */

#include <njs_core.h>
#include <string.h>


static njs_ret_t njs_variable_reference_resolve(njs_vm_t *vm,
    njs_variable_reference_t *vr, njs_parser_scope_t *node_scope);
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
njs_variable_add(njs_vm_t *vm, njs_parser_scope_t *scope, nxt_str_t *name,
    uint32_t hash, njs_variable_type_t type)
{
    nxt_int_t           ret;
    njs_variable_t      *var;
    nxt_lvlhsh_query_t  lhq;

    lhq.key_hash = hash;
    lhq.key = *name;
    lhq.proto = &njs_variables_hash_proto;

    if (type >= NJS_VARIABLE_VAR) {
        /*
         * A "var" and "function" declarations are
         * stored in function or global scope.
         */
        while (scope->type == NJS_SCOPE_BLOCK) {
            scope = scope->parent;
        }
    }

    if (nxt_lvlhsh_find(&scope->variables, &lhq) == NXT_OK) {
        var = lhq.value;

        if (type == NJS_VARIABLE_FUNCTION) {
            var->type = type;
        }

        return var;
    }

    var = njs_variable_alloc(vm, &lhq.key, type);
    if (nxt_slow_path(var == NULL)) {
        return var;
    }

    lhq.replace = 0;
    lhq.value = var;
    lhq.pool = vm->mem_pool;

    ret = nxt_lvlhsh_insert(&scope->variables, &lhq);

    if (nxt_fast_path(ret == NXT_OK)) {
        return var;
    }

    nxt_mp_free(vm->mem_pool, var->name.start);
    nxt_mp_free(vm->mem_pool, var);

    njs_type_error(vm, "lvlhsh insert failed");

    return NULL;
}


njs_variable_t *
njs_label_add(njs_vm_t *vm, njs_parser_scope_t *scope, nxt_str_t *name,
    uint32_t hash)
{
    nxt_int_t           ret;
    njs_variable_t      *label;
    nxt_lvlhsh_query_t  lhq;

    lhq.key_hash = hash;
    lhq.key = *name;
    lhq.proto = &njs_variables_hash_proto;

    if (nxt_lvlhsh_find(&scope->labels, &lhq) == NXT_OK) {
        return lhq.value;
    }

    label = njs_variable_alloc(vm, &lhq.key, NJS_VARIABLE_CONST);
    if (nxt_slow_path(label == NULL)) {
        return label;
    }

    lhq.replace = 0;
    lhq.value = label;
    lhq.pool = vm->mem_pool;

    ret = nxt_lvlhsh_insert(&scope->labels, &lhq);

    if (nxt_fast_path(ret == NXT_OK)) {
        return label;
    }

    nxt_mp_free(vm->mem_pool, label->name.start);
    nxt_mp_free(vm->mem_pool, label);

    njs_internal_error(vm, "lvlhsh insert failed");

    return NULL;
}


njs_ret_t
njs_label_remove(njs_vm_t *vm, njs_parser_scope_t *scope, nxt_str_t *name,
    uint32_t hash)
{
    nxt_int_t           ret;
    njs_variable_t      *label;
    nxt_lvlhsh_query_t  lhq;

    lhq.key_hash = hash;
    lhq.key = *name;
    lhq.proto = &njs_variables_hash_proto;
    lhq.pool = vm->mem_pool;

    ret = nxt_lvlhsh_delete(&scope->labels, &lhq);

    if (nxt_fast_path(ret == NXT_OK)) {
        label = lhq.value;
        nxt_mp_free(vm->mem_pool, label->name.start);
        nxt_mp_free(vm->mem_pool, label);

    } else {
        njs_internal_error(vm, "lvlhsh delete failed");
    }

    return ret;
}


static nxt_int_t
njs_reference_hash_test(nxt_lvlhsh_query_t *lhq, void *data)
{
    njs_parser_node_t  *node;

    node = data;

    if (nxt_strstr_eq(&lhq->key, &node->u.reference.name)) {
        return NXT_OK;
    }

    return NXT_DECLINED;
}


const nxt_lvlhsh_proto_t  njs_references_hash_proto
    nxt_aligned(64) =
{
    NXT_LVLHSH_DEFAULT,
    0,
    njs_reference_hash_test,
    njs_lvlhsh_alloc,
    njs_lvlhsh_free,
};


njs_ret_t
njs_variable_reference(njs_vm_t *vm, njs_parser_scope_t *scope,
    njs_parser_node_t *node, nxt_str_t *name, uint32_t hash,
    njs_reference_type_t type)
{
    njs_ret_t                 ret;
    nxt_lvlhsh_query_t        lhq;
    njs_variable_reference_t  *vr;

    vr = &node->u.reference;

    ret = njs_name_copy(vm, &vr->name, name);

    if (nxt_fast_path(ret == NXT_OK)) {
        vr->hash = hash;
        vr->type = type;

        lhq.key_hash = hash;
        lhq.key = vr->name;
        lhq.proto = &njs_references_hash_proto;
        lhq.replace = 0;
        lhq.value = node;
        lhq.pool = vm->mem_pool;

        ret = nxt_lvlhsh_insert(&scope->references, &lhq);

        if (nxt_fast_path(ret != NXT_ERROR)) {
            ret = NXT_OK;
        }
    }

    return ret;
}


static njs_ret_t
njs_variables_scope_resolve(njs_vm_t *vm, njs_parser_scope_t *scope,
    nxt_bool_t closure)
{
    njs_ret_t                 ret;
    nxt_queue_t               *nested;
    njs_variable_t            *var;
    nxt_queue_link_t          *lnk;
    njs_parser_node_t         *node;
    nxt_lvlhsh_each_t         lhe;
    njs_variable_reference_t  *vr;

    nested = &scope->nested;

    for (lnk = nxt_queue_first(nested);
         lnk != nxt_queue_tail(nested);
         lnk = nxt_queue_next(lnk))
    {
        scope = nxt_queue_link_data(lnk, njs_parser_scope_t, link);

        ret = njs_variables_scope_resolve(vm, scope, closure);
        if (nxt_slow_path(ret != NXT_OK)) {
            return NXT_ERROR;
        }

        nxt_lvlhsh_each_init(&lhe, &njs_variables_hash_proto);

        for ( ;; ) {
            node = nxt_lvlhsh_each(&scope->references, &lhe);

            if (node == NULL) {
                break;
            }

            vr = &node->u.reference;

            if (closure) {
                ret = njs_variable_reference_resolve(vm, vr, node->scope);
                if (nxt_slow_path(ret != NXT_OK)) {
                    continue;
                }

                if (vr->scope_index == NJS_SCOPE_INDEX_LOCAL) {
                    continue;
                }
            }

            var = njs_variable_resolve(vm, node);

            if (nxt_slow_path(var == NULL)) {
                if (vr->type != NJS_TYPEOF) {
                    return NXT_ERROR;
                }
            }
        }
    }

    return NXT_OK;
}


njs_ret_t
njs_variables_scope_reference(njs_vm_t *vm, njs_parser_scope_t *scope)
{
    njs_ret_t  ret;

    /*
     * Calculating proper scope types for variables.
     * A variable is considered to be local variable if it is referenced
     * only in the local scope (reference and definition nestings are the same).
     */

    ret = njs_variables_scope_resolve(vm, scope, 1);
    if (nxt_slow_path(ret != NXT_OK)) {
        return NXT_ERROR;
    }

    ret = njs_variables_scope_resolve(vm, scope, 0);
    if (nxt_slow_path(ret != NXT_OK)) {
        return NXT_ERROR;
    }

    return NXT_OK;
}


njs_index_t
njs_variable_typeof(njs_vm_t *vm, njs_parser_node_t *node)
{
    nxt_int_t                 ret;
    njs_variable_reference_t  *vr;

    if (node->index != NJS_INDEX_NONE) {
        return node->index;
    }

    vr = &node->u.reference;

    ret = njs_variable_reference_resolve(vm, vr, node->scope);

    if (nxt_fast_path(ret == NXT_OK)) {
        return vr->variable->index;
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

    var = njs_variable_resolve(vm, node);

    if (nxt_fast_path(var != NULL)) {
        return var->index;
    }

    return NJS_INDEX_ERROR;
}


njs_variable_t *
njs_variable_resolve(njs_vm_t *vm, njs_parser_node_t *node)
{
    nxt_int_t                 ret;
    nxt_uint_t                scope_index;
    njs_index_t               index;
    njs_variable_t            *var;
    njs_variable_reference_t  *vr;

    vr = &node->u.reference;

    ret = njs_variable_reference_resolve(vm, vr, node->scope);

    if (nxt_slow_path(ret != NXT_OK)) {
        goto not_found;
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

    if (nxt_slow_path(index == NJS_INDEX_ERROR)) {
        return NULL;
    }

    var->index = index;
    node->index = index;

    return var;

not_found:

    njs_parser_node_error(vm, node, NJS_OBJECT_REF_ERROR,
                          "\"%V\" is not defined", &vr->name);

    return NULL;
}


njs_variable_t *
njs_label_find(njs_vm_t *vm, njs_parser_scope_t *scope, nxt_str_t *name,
    uint32_t hash)
{
    nxt_lvlhsh_query_t  lhq;

    lhq.key_hash = hash;
    lhq.key = *name;
    lhq.proto = &njs_variables_hash_proto;

    for ( ;; ) {
        if (nxt_lvlhsh_find(&scope->labels, &lhq) == NXT_OK) {
            return lhq.value;
        }

        scope = scope->parent;

        if (scope == NULL) {
            return NULL;
        }
    }
}


static njs_ret_t
njs_variable_reference_resolve(njs_vm_t *vm, njs_variable_reference_t *vr,
    njs_parser_scope_t *node_scope)
{
    nxt_lvlhsh_query_t  lhq;
    njs_parser_scope_t  *scope, *previous;

    lhq.key_hash = vr->hash;
    lhq.key = vr->name;
    lhq.proto = &njs_variables_hash_proto;

    scope = node_scope;
    previous = NULL;

    for ( ;; ) {
        if (nxt_lvlhsh_find(&scope->variables, &lhq) == NXT_OK) {
            vr->variable = lhq.value;

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

            return NXT_OK;
        }

        if (scope->parent == NULL) {
            /* A global scope. */
            vr->scope = scope;

            return NXT_DECLINED;
        }

        previous = scope;
        scope = scope->parent;
    }
}


njs_index_t
njs_scope_next_index(njs_vm_t *vm, njs_parser_scope_t *scope,
    nxt_uint_t scope_index, const njs_value_t *default_value)
{
    njs_index_t  index;
    njs_value_t  *value;
    nxt_array_t  *values;

    if (njs_scope_accumulative(vm, scope)) {
        /*
         * When non-clonable VM runs in accumulative mode all
         * global variables should be allocated in absolute scope
         * to share them among consecutive VM invocations.
         */
        value = nxt_mp_align(vm->mem_pool, sizeof(njs_value_t),
                             sizeof(njs_value_t));
        if (nxt_slow_path(value == NULL)) {
            return NJS_INDEX_ERROR;
        }

        index = (njs_index_t) value;

    } else {
        values = scope->values[scope_index];

        if (values == NULL) {
            values = nxt_array_create(4, sizeof(njs_value_t),
                                      &njs_array_mem_proto, vm->mem_pool);
            if (nxt_slow_path(values == NULL)) {
                return NJS_INDEX_ERROR;
            }

            scope->values[scope_index] = values;
        }

        value = nxt_array_add(values, &njs_array_mem_proto, vm->mem_pool);
        if (nxt_slow_path(value == NULL)) {
            return NJS_INDEX_ERROR;
        }

        index = scope->next_index[scope_index];
        scope->next_index[scope_index] += sizeof(njs_value_t);
    }

    *value = *default_value;

    return index;
}


static njs_variable_t *
njs_variable_alloc(njs_vm_t *vm, nxt_str_t *name, njs_variable_type_t type)
{
    njs_ret_t       ret;
    njs_variable_t  *var;

    var = nxt_mp_zalloc(vm->mem_pool, sizeof(njs_variable_t));
    if (nxt_slow_path(var == NULL)) {
        njs_memory_error(vm);
        return NULL;
    }

    var->type = type;

    ret = njs_name_copy(vm, &var->name, name);

    if (nxt_fast_path(ret == NXT_OK)) {
        return var;
    }

    nxt_mp_free(vm->mem_pool, var);

    njs_memory_error(vm);

    return NULL;
}


njs_ret_t
njs_name_copy(njs_vm_t *vm, nxt_str_t *dst, nxt_str_t *src)
{
    dst->length = src->length;

    dst->start = nxt_mp_alloc(vm->mem_pool, src->length);

    if (nxt_fast_path(dst->start != NULL)) {
        (void) memcpy(dst->start, src->start, src->length);

        return NXT_OK;
    }

    njs_memory_error(vm);

    return NXT_ERROR;
}


const njs_value_t *
njs_vm_value(njs_vm_t *vm, const nxt_str_t *name)
{
    nxt_lvlhsh_query_t  lhq;

    lhq.key_hash = nxt_djb_hash(name->start, name->length);
    lhq.key = *name;
    lhq.proto = &njs_variables_hash_proto;

    if (nxt_lvlhsh_find(&vm->variables_hash, &lhq) == NXT_OK) {
        return njs_vmcode_operand(vm, ((njs_variable_t *) lhq.value)->index);
    }

    lhq.proto = &njs_extern_value_hash_proto;

    if (nxt_lvlhsh_find(&vm->externals_hash, &lhq) == NXT_OK) {
        return &((njs_extern_value_t *) lhq.value)->value;
    }

    return &njs_value_undefined;
}


njs_function_t *
njs_vm_function(njs_vm_t *vm, const nxt_str_t *name)
{
    const njs_value_t  *value;

    value = njs_vm_value(vm, name);

    if (njs_is_function(value)) {
        return value->data.u.function;
    }

    return NULL;
}
