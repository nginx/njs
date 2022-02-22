
/*
 * Copyright (C) Dmitry Volyntsev
 * Copyright (C) NGINX, Inc.
 */

#ifndef _NJS_GENERATOR_H_INCLUDED_
#define _NJS_GENERATOR_H_INCLUDED_


typedef struct njs_generator_block_s   njs_generator_block_t;


typedef njs_int_t (*njs_generator_state_func_t)(njs_vm_t *vm,
    njs_generator_t *generator, njs_parser_node_t *node);


struct njs_generator_s {
    njs_generator_state_func_t      state;
    njs_queue_t                     stack;
    njs_parser_node_t               *node;
    void                            *context;

    njs_value_t                     *local_scope;

    njs_generator_block_t           *block;
    njs_arr_t                       *index_cache;
    njs_arr_t                       *closures;

    njs_str_t                       file;
    njs_arr_t                       *lines;

    size_t                          code_size;
    u_char                          *code_start;
    u_char                          *code_end;

    /* Parsing Function() or eval(). */
    uint8_t                         runtime;           /* 1 bit */

    njs_uint_t                      depth;
};


njs_int_t njs_generator_init(njs_generator_t *generator, njs_str_t *file,
    njs_int_t depth, njs_bool_t runtime);
njs_vm_code_t *njs_generate_scope(njs_vm_t *vm, njs_generator_t *generator,
    njs_parser_scope_t *scope, const njs_str_t *name);
njs_vm_code_t *njs_lookup_code(njs_vm_t *vm, u_char *pc);
uint32_t njs_lookup_line(njs_arr_t *lines, uint32_t offset);


#endif /* _NJS_GENERATOR_H_INCLUDED_ */
