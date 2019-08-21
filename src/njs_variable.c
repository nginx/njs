
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) Dmitry Volyntsev
 * Copyright (C) NGINX, Inc.
 */


#include <njs_main.h>


static njs_variable_t *njs_variable_scope_add(njs_vm_t *vm,
    njs_parser_scope_t *scope, njs_lvlhsh_query_t *lhq,
    njs_variable_type_t type);
static njs_int_t njs_variable_reference_resolve(njs_vm_t *vm,
    njs_variable_reference_t *vr, njs_parser_scope_t *node_scope);
static njs_variable_t *njs_variable_alloc(njs_vm_t *vm, njs_str_t *name,
    njs_variable_type_t type);


static njs_int_t
njs_variables_hash_test(njs_lvlhsh_query_t *lhq, void *data)
{
    njs_variable_t  *var;

    var = data;

    if (njs_strstr_eq(&lhq->key, &var->name)) {
        return NJS_OK;
    }

    return NJS_DECLINED;
}


const njs_lvlhsh_proto_t  njs_variables_hash_proto
    njs_aligned(64) =
{
    NJS_LVLHSH_DEFAULT,
    njs_variables_hash_test,
    njs_lvlhsh_alloc,
    njs_lvlhsh_free,
};


njs_variable_t *
njs_variable_add(njs_vm_t *vm, njs_parser_scope_t *scope, njs_str_t *name,
    uint32_t hash, njs_variable_type_t type)
{
    njs_variable_t      *var;
    njs_lvlhsh_query_t  lhq;

    lhq.key_hash = hash;
    lhq.key = *name;
    lhq.proto = &njs_variables_hash_proto;

    var = njs_variable_scope_add(vm, scope, &lhq, type);
    if (njs_slow_path(var == NULL)) {
        return NULL;
    }

    if (type == NJS_VARIABLE_VAR && scope->type == NJS_SCOPE_BLOCK) {
        /* A "var" declaration is stored in function or global scope. */
        do {
            scope = scope->parent;

            var = njs_variable_scope_add(vm, scope, &lhq, type);
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
njs_variables_copy(njs_vm_t *vm, njs_lvlhsh_t *variables,
    njs_lvlhsh_t *prev_variables)
{
    njs_int_t           ret;
    njs_variable_t      *var;
    njs_lvlhsh_each_t   lhe;
    njs_lvlhsh_query_t  lhq;

    njs_lvlhsh_each_init(&lhe, &njs_variables_hash_proto);

    lhq.proto = &njs_variables_hash_proto;
    lhq.replace = 0;
    lhq.pool = vm->mem_pool;

    for ( ;; ) {
        var = njs_lvlhsh_each(prev_variables, &lhe);

        if (var == NULL) {
            break;
        }

        lhq.value = var;
        lhq.key = var->name;
        lhq.key_hash = njs_djb_hash(var->name.start, var->name.length);

        ret = njs_lvlhsh_insert(variables, &lhq);
        if (njs_slow_path(ret != NJS_OK)) {
            return NJS_ERROR;
        }
    }

    return NJS_OK;
}


static njs_variable_t *
njs_variable_scope_add(njs_vm_t *vm, njs_parser_scope_t *scope,
    njs_lvlhsh_query_t *lhq, njs_variable_type_t type)
{
    njs_int_t       ret;
    njs_variable_t  *var;

    if (njs_lvlhsh_find(&scope->variables, lhq) == NJS_OK) {
        var = lhq->value;

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

    var = njs_variable_alloc(vm, &lhq->key, type);
    if (njs_slow_path(var == NULL)) {
        return NULL;
    }

    lhq->replace = 0;
    lhq->value = var;
    lhq->pool = vm->mem_pool;

    ret = njs_lvlhsh_insert(&scope->variables, lhq);

    if (njs_fast_path(ret == NJS_OK)) {
        return var;
    }

    njs_mp_free(vm->mem_pool, var->name.start);
    njs_mp_free(vm->mem_pool, var);

    njs_type_error(vm, "lvlhsh insert failed");

    return NULL;

fail:

    njs_parser_syntax_error(vm, vm->parser,
                            "\"%V\" has already been declared",
                            &lhq->key);
    return NULL;
}


njs_variable_t *
njs_label_add(njs_vm_t *vm, njs_parser_scope_t *scope, njs_str_t *name,
    uint32_t hash)
{
    njs_int_t           ret;
    njs_variable_t      *label;
    njs_lvlhsh_query_t  lhq;

    lhq.key_hash = hash;
    lhq.key = *name;
    lhq.proto = &njs_variables_hash_proto;

    if (njs_lvlhsh_find(&scope->labels, &lhq) == NJS_OK) {
        return lhq.value;
    }

    label = njs_variable_alloc(vm, &lhq.key, NJS_VARIABLE_CONST);
    if (njs_slow_path(label == NULL)) {
        return label;
    }

    lhq.replace = 0;
    lhq.value = label;
    lhq.pool = vm->mem_pool;

    ret = njs_lvlhsh_insert(&scope->labels, &lhq);

    if (njs_fast_path(ret == NJS_OK)) {
        return label;
    }

    njs_mp_free(vm->mem_pool, label->name.start);
    njs_mp_free(vm->mem_pool, label);

    njs_internal_error(vm, "lvlhsh insert failed");

    return NULL;
}


njs_int_t
njs_label_remove(njs_vm_t *vm, njs_parser_scope_t *scope, njs_str_t *name,
    uint32_t hash)
{
    njs_int_t           ret;
    njs_variable_t      *label;
    njs_lvlhsh_query_t  lhq;

    lhq.key_hash = hash;
    lhq.key = *name;
    lhq.proto = &njs_variables_hash_proto;
    lhq.pool = vm->mem_pool;

    ret = njs_lvlhsh_delete(&scope->labels, &lhq);

    if (njs_fast_path(ret == NJS_OK)) {
        label = lhq.value;
        njs_mp_free(vm->mem_pool, label->name.start);
        njs_mp_free(vm->mem_pool, label);

    } else {
        njs_internal_error(vm, "lvlhsh delete failed");
    }

    return ret;
}


static njs_int_t
njs_reference_hash_test(njs_lvlhsh_query_t *lhq, void *data)
{
    njs_parser_node_t  *node;

    node = data;

    if (njs_strstr_eq(&lhq->key, &node->u.reference.name)) {
        return NJS_OK;
    }

    return NJS_DECLINED;
}


const njs_lvlhsh_proto_t  njs_references_hash_proto
    njs_aligned(64) =
{
    NJS_LVLHSH_DEFAULT,
    njs_reference_hash_test,
    njs_lvlhsh_alloc,
    njs_lvlhsh_free,
};


njs_int_t
njs_variable_reference(njs_vm_t *vm, njs_parser_scope_t *scope,
    njs_parser_node_t *node, njs_str_t *name, uint32_t hash,
    njs_reference_type_t type)
{
    njs_int_t                 ret;
    njs_lvlhsh_query_t        lhq;
    njs_variable_reference_t  *vr;

    vr = &node->u.reference;

    ret = njs_name_copy(vm, &vr->name, name);

    if (njs_fast_path(ret == NJS_OK)) {
        vr->hash = hash;
        vr->type = type;

        lhq.key_hash = hash;
        lhq.key = vr->name;
        lhq.proto = &njs_references_hash_proto;
        lhq.replace = 0;
        lhq.value = node;
        lhq.pool = vm->mem_pool;

        ret = njs_lvlhsh_insert(&scope->references, &lhq);

        if (njs_fast_path(ret != NJS_ERROR)) {
            ret = NJS_OK;
        }
    }

    return ret;
}


static njs_int_t
njs_variables_scope_resolve(njs_vm_t *vm, njs_parser_scope_t *scope,
    njs_bool_t closure)
{
    njs_int_t                 ret;
    njs_queue_t               *nested;
    njs_queue_link_t          *lnk;
    njs_parser_node_t         *node;
    njs_lvlhsh_each_t         lhe;
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

        njs_lvlhsh_each_init(&lhe, &njs_variables_hash_proto);

        for ( ;; ) {
            node = njs_lvlhsh_each(&scope->references, &lhe);

            if (node == NULL) {
                break;
            }

            vr = &node->u.reference;

            if (closure) {
                ret = njs_variable_reference_resolve(vm, vr, node->scope);
                if (njs_slow_path(ret != NJS_OK)) {
                    continue;
                }

                if (vr->scope_index == NJS_SCOPE_INDEX_LOCAL) {
                    continue;
                }
            }

            (void) njs_variable_resolve(vm, node);
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
njs_variable_typeof(njs_vm_t *vm, njs_parser_node_t *node)
{
    njs_int_t                 ret;
    njs_variable_reference_t  *vr;

    if (node->index != NJS_INDEX_NONE) {
        return node->index;
    }

    vr = &node->u.reference;

    ret = njs_variable_reference_resolve(vm, vr, node->scope);

    if (njs_fast_path(ret == NJS_OK)) {
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
njs_label_find(njs_vm_t *vm, njs_parser_scope_t *scope, njs_str_t *name,
    uint32_t hash)
{
    njs_lvlhsh_query_t  lhq;

    lhq.key_hash = hash;
    lhq.key = *name;
    lhq.proto = &njs_variables_hash_proto;

    for ( ;; ) {
        if (njs_lvlhsh_find(&scope->labels, &lhq) == NJS_OK) {
            return lhq.value;
        }

        scope = scope->parent;

        if (scope == NULL) {
            return NULL;
        }
    }
}


static njs_int_t
njs_variable_reference_resolve(njs_vm_t *vm, njs_variable_reference_t *vr,
    njs_parser_scope_t *node_scope)
{
    njs_lvlhsh_query_t  lhq;
    njs_parser_scope_t  *scope, *previous;

    lhq.key_hash = vr->hash;
    lhq.key = vr->name;
    lhq.proto = &njs_variables_hash_proto;

    scope = node_scope;
    previous = NULL;

    for ( ;; ) {
        if (njs_lvlhsh_find(&scope->variables, &lhq) == NJS_OK) {
            vr->variable = lhq.value;

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
njs_variable_alloc(njs_vm_t *vm, njs_str_t *name, njs_variable_type_t type)
{
    njs_int_t       ret;
    njs_variable_t  *var;

    var = njs_mp_zalloc(vm->mem_pool, sizeof(njs_variable_t));
    if (njs_slow_path(var == NULL)) {
        njs_memory_error(vm);
        return NULL;
    }

    var->type = type;

    ret = njs_name_copy(vm, &var->name, name);

    if (njs_fast_path(ret == NJS_OK)) {
        return var;
    }

    njs_mp_free(vm->mem_pool, var);

    njs_memory_error(vm);

    return NULL;
}


njs_int_t
njs_name_copy(njs_vm_t *vm, njs_str_t *dst, njs_str_t *src)
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


const njs_value_t *
njs_vm_value(njs_vm_t *vm, const njs_str_t *name)
{
    njs_lvlhsh_query_t  lhq;

    lhq.key_hash = njs_djb_hash(name->start, name->length);
    lhq.key = *name;
    lhq.proto = &njs_variables_hash_proto;

    if (njs_lvlhsh_find(&vm->variables_hash, &lhq) == NJS_OK) {
        return njs_vmcode_operand(vm, ((njs_variable_t *) lhq.value)->index);
    }

    lhq.proto = &njs_extern_value_hash_proto;

    if (njs_lvlhsh_find(&vm->externals_hash, &lhq) == NJS_OK) {
        return &((njs_extern_value_t *) lhq.value)->value;
    }

    return &njs_value_undefined;
}


njs_function_t *
njs_vm_function(njs_vm_t *vm, const njs_str_t *name)
{
    const njs_value_t  *value;

    value = njs_vm_value(vm, name);

    if (njs_is_function(value)) {
        return njs_function(value);
    }

    return NULL;
}
