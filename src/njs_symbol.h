
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) NGINX, Inc.
 */

#ifndef _NJS_SYMBOL_H_INCLUDED_
#define _NJS_SYMBOL_H_INCLUDED_

typedef enum {
    NJS_SYMBOL_INVALID                  = 0,
    NJS_SYMBOL_ASYNC_ITERATOR           = 1,
    NJS_SYMBOL_HAS_INSTANCE             = 2,
    NJS_SYMBOL_IS_CONCAT_SPREADABLE     = 3,
    NJS_SYMBOL_ITERATOR                 = 4,
    NJS_SYMBOL_MATCH                    = 5,
    NJS_SYMBOL_MATCH_ALL                = 6,
    NJS_SYMBOL_REPLACE                  = 7,
    NJS_SYMBOL_SEARCH                   = 8,
    NJS_SYMBOL_SPECIES                  = 9,
    NJS_SYMBOL_SPLIT                    = 10,
    NJS_SYMBOL_TO_PRIMITIVE             = 11,
    NJS_SYMBOL_TO_STRING_TAG            = 12,
    NJS_SYMBOL_UNSCOPABLES              = 13,
#define NJS_SYMBOL_KNOWN_MAX (NJS_SYMBOL_UNSCOPABLES + 1)
} njs_wellknown_symbol_t;


njs_int_t njs_symbol_to_string(njs_vm_t *vm, njs_value_t *dst,
    const njs_value_t *value);


extern const njs_object_type_init_t  njs_symbol_type_init;


#endif /* _NJS_SYMBOL_H_INCLUDED_ */
