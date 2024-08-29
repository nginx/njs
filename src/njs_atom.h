
/*
 * Copyright (C) Vadim Zhestkov
 * Copyright (C) F5, Inc.
 */


#ifndef _NJS_ATOM_H_INCLUDED_
#define _NJS_ATOM_H_INCLUDED_

#define NJS_ATOM_SYMBOL_KNOWN_MAX  14
#define NJS_ATOM_HASH_MASK         0x3FF
#define NJS_ATOM_SIZE              505


#ifdef NJS_DEF_VW
    #undef NJS_DEF_VW
    #undef NJS_DEF_VS
#endif

#define NJS_DEF_VW(name) \
    njs_value_t vw_ ## name;

#define NJS_DEF_VS(name, str, flags, token) \
    njs_value_t vs_ ## name;

typedef struct {
    #include <njs_atom_defs.h>
} njs_atom_values_t;


#ifdef NJS_DEF_VW
    #undef NJS_DEF_VW
    #undef NJS_DEF_VS
#endif

#define NJS_DEF_VW(name) \
    njs_atom_vw_ ## name,

#define NJS_DEF_VS(name, str, flags, token) \
    njs_atom_vs_ ## name,

enum {
    #include <njs_atom_defs.h>
};


void njs_atom_hash_init(void);
njs_int_t njs_atom_atomize_key(njs_vm_t *vm, njs_value_t *value);
njs_int_t njs_atom_atomize_key_s(njs_vm_t *vm, njs_value_t *value);


extern njs_atom_values_t  njs_atom;
extern njs_flathsh_t      njs_atom_hash;
extern uint32_t           njs_atom_hash_atom_id;
extern const njs_flathsh_proto_t  njs_atom_hash_proto;

njs_inline njs_int_t
njs_get_prop_name_by_atom_id(njs_vm_t *vm, njs_value_t *prop_name, uint32_t atom_id)
{
    double num;

    if (atom_id & 0x80000000) {
        num = atom_id & 0x7FFFFFFF;

        size_t size;
        u_char buf[128];

        size = njs_dtoa(num, (char *) buf);
        njs_int_t ret = njs_string_new(vm, prop_name, buf, size, size);
        if (ret != NJS_OK) {
            return NJS_ERROR;
        }
        return NJS_OK;

    }

    if (atom_id < vm->atom_hash_atom_id_shared_cell) {
        *prop_name = *((njs_value_t *)(njs_hash_elts(
                      (&vm->atom_hash_shared_cell)->slot))[atom_id].value);
    } else {
        *prop_name = *((njs_value_t *)(njs_hash_elts(vm->atom_hash->slot))[
                      atom_id - vm->atom_hash_atom_id_shared_cell].value);
    }
    return NJS_OK;
}

#endif /* _NJS_ATOM_H_INCLUDED_ */
