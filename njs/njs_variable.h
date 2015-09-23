
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) NGINX, Inc.
 */

#ifndef _NJS_VARIABLE_H_INCLUDED_
#define _NJS_VARIABLE_H_INCLUDED_


typedef enum {
    NJS_VARIABLE_CREATED = 0,
    NJS_VARIABLE_PENDING,
    NJS_VARIABLE_USED,
    NJS_VARIABLE_SET,
    NJS_VARIABLE_DECLARED,
} njs_variable_state_t;


typedef struct {
    u_char                *name_start;
    uint16_t              name_len;
    njs_variable_state_t  state:8;  /* 3 bits */

    njs_index_t           index;
} njs_variable_t;


njs_variable_t *njs_parser_variable(njs_vm_t *vm, njs_parser_t *parser,
    nxt_uint_t *level);
njs_value_t *njs_variable_value(njs_parser_t *parser, njs_index_t index);


#endif /* _NJS_VARIABLE_H_INCLUDED_ */
