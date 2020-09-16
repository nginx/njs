
/*
 * Copyright (C) Alexander Borisov
 * Copyright (C) NGINX, Inc.
 */

#ifndef _NJS_BUFFER_H_INCLUDED_
#define _NJS_BUFFER_H_INCLUDED_


njs_int_t njs_buffer_set(njs_vm_t *vm, njs_value_t *value, const u_char *start,
    uint32_t size);


extern const njs_object_type_init_t  njs_buffer_type_init;
extern const njs_object_init_t  njs_buffer_object_init;


#endif /* _NJS_BUFFER_H_INCLUDED_ */
