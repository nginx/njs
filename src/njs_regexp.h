
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) NGINX, Inc.
 */

#ifndef _NJS_REGEXP_H_INCLUDED_
#define _NJS_REGEXP_H_INCLUDED_


njs_int_t njs_regexp_init(njs_vm_t *vm);
njs_int_t njs_regexp_create(njs_vm_t *vm, njs_value_t *value, u_char *start,
    size_t length, njs_regex_flags_t flags);
njs_regex_flags_t njs_regexp_flags(u_char **start, u_char *end);
njs_regexp_pattern_t *njs_regexp_pattern_create(njs_vm_t *vm,
    u_char *string, size_t length, njs_regex_flags_t flags);
njs_int_t njs_regexp_match(njs_vm_t *vm, njs_regex_t *regex,
    const u_char *subject, size_t off, size_t len, njs_regex_match_data_t *d);
njs_regexp_t *njs_regexp_alloc(njs_vm_t *vm, njs_regexp_pattern_t *pattern);
njs_int_t njs_regexp_exec(njs_vm_t *vm, njs_value_t *r, njs_value_t *s,
    njs_value_t *retval);
njs_int_t njs_regexp_prototype_exec(njs_vm_t *vm, njs_value_t *args,
    njs_uint_t nargs, njs_index_t unused);

njs_int_t njs_regexp_to_string(njs_vm_t *vm, njs_value_t *retval,
    const njs_value_t *regexp);


extern const njs_object_init_t  njs_regexp_instance_init;
extern const njs_object_type_init_t  njs_regexp_type_init;


#endif /* _NJS_REGEXP_H_INCLUDED_ */
