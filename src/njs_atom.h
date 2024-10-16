
/*
 * Copyright (C) Vadim Zhestkov
 * Copyright (C) F5, Inc.
 */


#ifndef _NJS_ATOM_H_INCLUDED_
#define _NJS_ATOM_H_INCLUDED_


#define NJS_ATOM_SYMBOL_KNOWN_MAX  14
#define NJS_ATOM_HASH_MASK         0x3FF
#define NJS_ATOM_SIZE              503


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


void njs_atom_hash_init(void);
njs_int_t njs_atom_atomize_key(njs_vm_t *vm, njs_value_t *value);


extern njs_atom_values_t  njs_atom;
extern njs_flathsh_t      njs_atom_hash;
extern uint32_t           njs_atom_hash_atom_id;


#endif /* _NJS_ATOM_H_INCLUDED_ */
