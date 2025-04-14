
/*
 * Copyright (C) Vadim Zhestkov
 * Copyright (C) F5, Inc.
 */


#ifndef _NJS_ATOM_H_INCLUDED_
#define _NJS_ATOM_H_INCLUDED_


enum {
#define NJS_DEF_STRING(name, _1, _2, _3) NJS_ATOM_STRING_ ## name,
#define NJS_DEF_SYMBOL(name, str) NJS_ATOM_SYMBOL_ ## name,
#include <njs_atom_defs.h>
    NJS_ATOM_SIZE,
#undef NJS_DEF_SYMBOL
#undef NJS_DEF_STRING
};


uint32_t njs_atom_hash_init(njs_vm_t *vm);
njs_int_t njs_atom_symbol_add(njs_vm_t *vm, njs_value_t *value);
njs_value_t *njs_atom_find_or_add(njs_vm_t *vm, u_char *key, size_t size,
    size_t length, uint32_t hash);


njs_inline njs_int_t
njs_atom_to_value(njs_vm_t *vm, njs_value_t *dst, uint32_t atom_id)
{
    size_t               size;
    double               num;
    njs_flathsh_descr_t  *h;
    u_char               buf[128];

    njs_assert(atom_id != NJS_ATOM_STRING_unknown);

    if (njs_atom_is_number(atom_id)) {
        num = njs_atom_number(atom_id);
        size = njs_dtoa(num, (char *) buf);

        if (njs_string_new(vm, dst, buf, size, size) != NJS_OK) {
            return NJS_ERROR;
        }

        dst->atom_id = atom_id;

        return NJS_OK;
    }

    if (atom_id < vm->shared_atom_count) {
        h = vm->atom_hash_shared.slot;

        njs_assert(atom_id < h->elts_count);

        *dst = *((njs_value_t *) njs_hash_elts(h)[atom_id].value);

    } else {
        h = vm->atom_hash_current->slot;
        atom_id -= vm->shared_atom_count;

        njs_assert(atom_id < h->elts_count);

        *dst = *((njs_value_t *) njs_hash_elts(h)[atom_id].value);
    }

    return NJS_OK;
}

#endif /* _NJS_ATOM_H_INCLUDED_ */
