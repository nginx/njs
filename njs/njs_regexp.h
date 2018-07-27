
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) NGINX, Inc.
 */

#ifndef _NJS_REGEXP_H_INCLUDED_
#define _NJS_REGEXP_H_INCLUDED_


typedef enum {
    NJS_REGEXP_INVALID_FLAG = -1,
    NJS_REGEXP_GLOBAL       =  1,
    NJS_REGEXP_IGNORE_CASE  =  2,
    NJS_REGEXP_MULTILINE    =  4,
} njs_regexp_flags_t;


njs_ret_t njs_regexp_init(njs_vm_t *vm);
njs_ret_t njs_regexp_constructor(njs_vm_t *vm, njs_value_t *args,
    nxt_uint_t nargs, njs_index_t unused);
nxt_int_t njs_regexp_create(njs_vm_t *vm, njs_value_t *value, u_char *start,
    size_t length, njs_regexp_flags_t flags);
njs_token_t njs_regexp_literal(njs_vm_t *vm, njs_parser_t *parser,
    njs_value_t *value);
njs_regexp_pattern_t *njs_regexp_pattern_create(njs_vm_t *vm,
    u_char *string, size_t length, njs_regexp_flags_t flags);
nxt_int_t njs_regexp_match(njs_vm_t *vm, nxt_regex_t *regex, u_char *subject,
    size_t len, nxt_regex_match_data_t *match_data);
njs_regexp_t *njs_regexp_alloc(njs_vm_t *vm, njs_regexp_pattern_t *pattern);
njs_ret_t njs_regexp_prototype_exec(njs_vm_t *vm, njs_value_t *args,
    nxt_uint_t nargs, njs_index_t unused);

njs_ret_t njs_regexp_to_string(njs_vm_t *vm, njs_value_t *retval,
    const njs_value_t *regexp);

extern const njs_object_init_t  njs_regexp_constructor_init;
extern const njs_object_init_t  njs_regexp_prototype_init;


#endif /* _NJS_REGEXP_H_INCLUDED_ */
