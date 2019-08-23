
/*
 * Copyright (C) Dmitry Volyntsev
 * Copyright (C) NGINX, Inc.
 */

#ifndef _NJS_GENERATOR_H_INCLUDED_
#define _NJS_GENERATOR_H_INCLUDED_


typedef struct njs_generator_block_s   njs_generator_block_t;

struct njs_generator_s {
    njs_value_t                     *local_scope;

    size_t                          scope_size;

    njs_generator_block_t           *block;
    njs_arr_t                       *index_cache;

    size_t                          code_size;
    u_char                          *code_start;
    u_char                          *code_end;

    /* Parsing Function() or eval(). */
    uint8_t                         runtime;           /* 1 bit */

    njs_uint_t                      count;
};


njs_int_t njs_generate_scope(njs_vm_t *vm, njs_generator_t *generator,
    njs_parser_scope_t *scope, const njs_str_t *name);


#endif /* _NJS_GENERATOR_H_INCLUDED_ */
