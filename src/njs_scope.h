
/*
 * Copyright (C) Alexander Borisov
 * Copyright (C) NGINX, Inc.
 */

#ifndef _NJS_SCOPE_H_INCLUDED_
#define _NJS_SCOPE_H_INCLUDED_


#define NJS_SCOPE_VAR_SIZE      4
#define NJS_SCOPE_TYPE_OFFSET   (NJS_SCOPE_VAR_SIZE + 4)
#define NJS_SCOPE_VALUE_OFFSET  (NJS_SCOPE_TYPE_OFFSET + 1)
#define NJS_SCOPE_VALUE_MAX     ((1 << (32 - NJS_SCOPE_VALUE_OFFSET)) - 1)
#define NJS_SCOPE_TYPE_MASK     ((NJS_SCOPE_VALUE_MAX) << NJS_SCOPE_VAR_SIZE)

#define NJS_INDEX_NONE          ((njs_index_t) 0)
#define NJS_INDEX_ERROR         ((njs_index_t) -1)


njs_index_t njs_scope_temp_index(njs_parser_scope_t *scope);
njs_value_t *njs_scope_create_index_value(njs_vm_t *vm, njs_index_t index);
njs_value_t **njs_scope_make(njs_vm_t *vm, uint32_t count);
njs_index_t njs_scope_global_index(njs_vm_t *vm, const njs_value_t *src,
    njs_uint_t runtime);
njs_value_t *njs_scope_value_get(njs_vm_t *vm, njs_index_t index);


njs_inline njs_index_t
njs_scope_index(njs_scope_t scope, njs_index_t index, njs_level_type_t type,
                njs_variable_type_t var_type)
{
    njs_assert(type < NJS_LEVEL_MAX);
    njs_assert(scope == NJS_SCOPE_GLOBAL || scope == NJS_SCOPE_FUNCTION);

    if (index > NJS_SCOPE_VALUE_MAX) {
        return NJS_INDEX_ERROR;
    }

    if (scope == NJS_SCOPE_GLOBAL && type == NJS_LEVEL_LOCAL) {
        type = NJS_LEVEL_GLOBAL;
    }

    return (index << NJS_SCOPE_VALUE_OFFSET) | (type << NJS_SCOPE_VAR_SIZE)
            | var_type;
}


njs_inline njs_variable_type_t
njs_scope_index_var(njs_index_t index)
{
    return (njs_variable_type_t) (index & ~NJS_SCOPE_TYPE_MASK);
}


njs_inline njs_level_type_t
njs_scope_index_type(njs_index_t index)
{
    return (njs_level_type_t) ((index >> NJS_SCOPE_VAR_SIZE)
                               & ~NJS_SCOPE_TYPE_MASK);
}


njs_inline uint32_t
njs_scope_index_value(njs_index_t index)
{
    return (uint32_t) (index >> NJS_SCOPE_VALUE_OFFSET);
}


njs_inline njs_value_t *
njs_scope_value(njs_vm_t *vm, njs_index_t index)
{
    return vm->levels[njs_scope_index_type(index)]
                     [njs_scope_index_value(index)];
}


njs_inline njs_value_t *
njs_scope_valid_value(njs_vm_t *vm, njs_index_t index)
{
    njs_value_t  *value;

    value = njs_scope_value(vm, index);

    if (!njs_is_valid(value)) {
        if (njs_scope_index_var(index) <= NJS_VARIABLE_LET) {
            njs_reference_error(vm, "cannot access variable "
                                    "before initialization");
            return NULL;
        }

        njs_set_undefined(value);
    }

    return value;
}


njs_inline void
njs_scope_value_set(njs_vm_t *vm, njs_index_t index, njs_value_t *value)
{
    vm->levels[njs_scope_index_type(index)]
              [njs_scope_index_value(index)] = value;
}


njs_inline njs_value_t *
njs_scope_value_clone(njs_vm_t *vm, njs_index_t index, njs_value_t *value)
{
    njs_value_t  *newval;

    newval = njs_mp_alloc(vm->mem_pool, sizeof(njs_value_t));
    if (njs_slow_path(newval == NULL)) {
        njs_memory_error(vm);
        return NULL;
    }

    *newval = *value;

    njs_scope_value_set(vm, index, newval);

    return newval;
}


njs_inline njs_index_t
njs_scope_undefined_index(njs_vm_t *vm, njs_uint_t runtime)
{
    return njs_scope_global_index(vm, &njs_value_undefined, runtime);
}


njs_inline njs_index_t
njs_scope_global_this_index()
{
    return njs_scope_index(NJS_SCOPE_GLOBAL, 0, NJS_LEVEL_LOCAL,
                           NJS_VARIABLE_VAR);
}


#endif /* _NJS_PARSER_H_INCLUDED_ */
