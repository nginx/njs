
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) NGINX, Inc.
 */

#ifndef _NJS_OBJECT_HASH_H_INCLUDED_
#define _NJS_OBJECT_HASH_H_INCLUDED_


#define NJS___PROTO___HASH                                                    \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(NJS_DJB_HASH_INIT,                                       \
        '_'), '_'), 'p'), 'r'), 'o'), 't'), 'o'), '_'), '_')


#define NJS_ARRAY_HASH                                                        \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(NJS_DJB_HASH_INIT,                                       \
        'A'), 'r'), 'r'), 'a'), 'y')


#define NJS_ARGV_HASH                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(NJS_DJB_HASH_INIT,                                       \
        'a'), 'r'), 'g'), 'v')


#define NJS_BOOLEAN_HASH                                                      \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(NJS_DJB_HASH_INIT,                                       \
        'B'), 'o'), 'o'), 'l'), 'e'), 'a'), 'n')


#define NJS_CONFIGURABLE_HASH                                                 \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(NJS_DJB_HASH_INIT,                                       \
        'c'), 'o'), 'n'), 'f'), 'i'), 'g'), 'u'), 'r'), 'a'), 'b'), 'l'), 'e')


#define NJS_CONSTRUCTOR_HASH                                                  \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(NJS_DJB_HASH_INIT,                                       \
        'c'), 'o'), 'n'), 's'), 't'), 'r'), 'u'), 'c'), 't'), 'o'), 'r')


#define NJS_DATE_HASH                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(NJS_DJB_HASH_INIT,                                       \
        'D'), 'a'), 't'), 'e')


#define NJS_PROMISE_HASH                                                      \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(NJS_DJB_HASH_INIT,                                       \
        'P'), 'r'), 'o'), 'm'), 'i'), 's'), 'e')


#define NJS_ENUMERABLE_HASH                                                   \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(NJS_DJB_HASH_INIT,                                       \
        'e'), 'n'), 'u'), 'm'), 'e'), 'r'), 'a'), 'b'), 'l'), 'e')


#define NJS_ERRNO_HASH                                                        \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(NJS_DJB_HASH_INIT,                                       \
        'e'), 'r'), 'r'), 'n'), 'o')


#define NJS_ERROR_HASH                                                        \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(NJS_DJB_HASH_INIT,                                       \
        'E'), 'r'), 'r'), 'o'), 'r')


#define NJS_ENCODING_HASH                                                     \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(NJS_DJB_HASH_INIT,                                       \
        'e'), 'n'), 'c'), 'o'), 'd'), 'i'), 'n'), 'g')


#define NJS_ENV_HASH                                                          \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(NJS_DJB_HASH_INIT,                                       \
        'e'), 'n'), 'v')


#define NJS_EVAL_ERROR_HASH                                                   \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(NJS_DJB_HASH_INIT,                                       \
        'E'), 'v'), 'a'), 'l'), 'E'), 'r'), 'r'), 'o'), 'r')


#define NJS_FLAG_HASH                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(NJS_DJB_HASH_INIT,                                       \
        'f'), 'l'), 'a'), 'g')


#define NJS_GET_HASH                                                          \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(NJS_DJB_HASH_INIT,                                       \
        'g'), 'e'), 't')


#define NJS_GLOBAL_HASH                                                       \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(NJS_DJB_HASH_INIT,                                       \
        'g'), 'l'), 'o'), 'b'), 'a'), 'l')


#define NJS_GLOBAL_THIS_HASH                                                  \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(NJS_DJB_HASH_INIT,                                       \
        'g'), 'l'), 'o'), 'b'), 'a'), 'l'), 'T'), 'h'), 'i'), 's')


#define NJS_FUNCTION_HASH                                                     \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(NJS_DJB_HASH_INIT,                                       \
        'F'), 'u'), 'n'), 'c'), 't'), 'i'), 'o'), 'n')


#define NJS_INDEX_HASH                                                        \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(NJS_DJB_HASH_INIT,                                       \
        'i'), 'n'), 'd'), 'e'), 'x')


#define NJS_INPUT_HASH                                                        \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(NJS_DJB_HASH_INIT,                                       \
        'i'), 'n'), 'p'), 'u'), 't')


#define NJS_INTERNAL_ERROR_HASH                                               \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(NJS_DJB_HASH_INIT,                                       \
        'I'), 'n'), 't'), 'e'), 'r'), 'n'), 'a'), 'l'),                       \
        'E'), 'r'), 'r'), 'o'), 'r')


#define NJS_GROUPS_HASH                                                       \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(NJS_DJB_HASH_INIT,                                       \
        'g'), 'r'), 'o'), 'u'), 'p'), 's')


#define NJS_JOIN_HASH                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(NJS_DJB_HASH_INIT,                                       \
        'j'), 'o'), 'i'), 'n')


#define NJS_JSON_HASH                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(NJS_DJB_HASH_INIT,                                       \
        'J'), 'S'), 'O'), 'N')


#define NJS_LENGTH_HASH                                                       \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(NJS_DJB_HASH_INIT,                                       \
        'l'), 'e'), 'n'), 'g'), 't'), 'h')


#define NJS_NAME_HASH                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(NJS_DJB_HASH_INIT,                                       \
        'n'), 'a'), 'm'), 'e')


#define NJS_NJS_HASH                                                          \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(NJS_DJB_HASH_INIT,                                       \
        'n'), 'j'), 's')


#define NJS_262_HASH                                                          \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(NJS_DJB_HASH_INIT,                                       \
        '$'), '2'), '6'), '2')


#define NJS_NUMBER_HASH                                                       \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(NJS_DJB_HASH_INIT,                                       \
        'N'), 'u'), 'm'), 'b'), 'e'), 'r')


#define NJS_MATH_HASH                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(NJS_DJB_HASH_INIT,                                       \
        'M'), 'a'), 't'), 'h')


#define NJS_MEMORY_ERROR_HASH                                                 \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(NJS_DJB_HASH_INIT,                                       \
        'M'), 'e'), 'm'), 'o'), 'r'), 'y'),                                   \
        'E'), 'r'), 'r'), 'o'), 'r')


#define NJS_AGGREGATE_ERROR_HASH                                              \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(NJS_DJB_HASH_INIT,                                       \
        'A'), 'g'), 'g'), 'r'), 'e'), 'g'), 'a'), 't'), 'e'),                 \
        'E'), 'r'), 'r'), 'o'), 'r')


#define NJS_MESSAGE_HASH                                                      \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(NJS_DJB_HASH_INIT,                                       \
        'm'), 'e'), 's'), 's'), 'a'), 'g'), 'e')


#define NJS_ERRORS_HASH                                                       \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(NJS_DJB_HASH_INIT,                                       \
        'e'), 'r'), 'r'), 'o'), 'r'), 's')


#define NJS_MODE_HASH                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(NJS_DJB_HASH_INIT,                                       \
        'm'), 'o'), 'd'), 'e')


#define NJS_OBJECT_HASH                                                       \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(NJS_DJB_HASH_INIT,                                       \
        'O'), 'b'), 'j'), 'e'), 'c'), 't')


#define NJS_PATH_HASH                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(NJS_DJB_HASH_INIT,                                       \
        'p'), 'a'), 't'), 'h')


#define NJS_PROCESS_HASH                                                      \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(NJS_DJB_HASH_INIT,                                       \
        'p'), 'r'), 'o'), 'c'), 'e'), 's'), 's')


#define NJS_PROTOTYPE_HASH                                                    \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(NJS_DJB_HASH_INIT,                                       \
        'p'), 'r'), 'o'), 't'), 'o'), 't'), 'y'), 'p'), 'e')


#define NJS_RANGE_ERROR_HASH                                                  \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(NJS_DJB_HASH_INIT,                                       \
        'R'), 'a'), 'n'), 'g'), 'e'), 'E'), 'r'), 'r'), 'o'), 'r')


#define NJS_REF_ERROR_HASH                                                    \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(NJS_DJB_HASH_INIT,                                       \
        'R'), 'e'), 'f'), 'e'), 'r'), 'e'), 'n'), 'c'), 'e'),                 \
        'E'), 'r'), 'r'), 'o'), 'r')


#define NJS_REGEXP_HASH                                                       \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(NJS_DJB_HASH_INIT,                                       \
        'R'), 'e'), 'g'), 'E'), 'x'), 'p')


#define NJS_SET_HASH                                                          \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(NJS_DJB_HASH_INIT,                                       \
        's'), 'e'), 't')


#define NJS_STACK_HASH                                                        \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(NJS_DJB_HASH_INIT,                                       \
        's'), 't'), 'a'), 'c'), 'k')


#define NJS_STRING_HASH                                                       \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(NJS_DJB_HASH_INIT,                                       \
        'S'), 't'), 'r'), 'i'), 'n'), 'g')


#define NJS_SYMBOL_HASH                                                       \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(NJS_DJB_HASH_INIT,                                       \
        'S'), 'y'), 'm'), 'b'), 'o'), 'l')


#define NJS_SYNTAX_ERROR_HASH                                                 \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(NJS_DJB_HASH_INIT,                                       \
        'S'), 'y'), 'n'), 't'), 'a'), 'x'),                                   \
        'E'), 'r'), 'r'), 'o'), 'r')


#define NJS_SYSCALL_HASH                                                      \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(NJS_DJB_HASH_INIT,                                       \
        's'), 'y'), 's'), 'c'), 'a'), 'l'), 'l')


#define NJS_TO_JSON_HASH                                                      \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(NJS_DJB_HASH_INIT,                                       \
        't'), 'o'), 'J'), 'S'), 'O'), 'N')


#define NJS_TO_STRING_HASH                                                    \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(NJS_DJB_HASH_INIT,                                       \
        't'), 'o'), 'S'), 't'), 'r'), 'i'), 'n'), 'g')


#define NJS_TO_ISO_STRING_HASH                                                \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(NJS_DJB_HASH_INIT,                                       \
        't'), 'o'), 'I'), 'S'), 'O'), 'S'), 't'), 'r'), 'i'), 'n'), 'g')


#define NJS_TYPE_ERROR_HASH                                                   \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(NJS_DJB_HASH_INIT,                                       \
        'T'), 'y'), 'p'), 'e'), 'E'), 'r'), 'r'), 'o'), 'r')


#define NJS_VALUE_HASH                                                        \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(NJS_DJB_HASH_INIT,                                       \
        'v'), 'a'), 'l'), 'u'), 'e')


#define NJS_VALUE_OF_HASH                                                     \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(NJS_DJB_HASH_INIT,                                       \
        'v'), 'a'), 'l'), 'u'), 'e'), 'O'), 'f')


#define NJS_WRITABABLE_HASH                                                   \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(NJS_DJB_HASH_INIT,                                       \
        'w'), 'r'), 'i'), 't'), 'a'), 'b'), 'l'), 'e')


#define NJS_URI_ERROR_HASH                                                    \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(NJS_DJB_HASH_INIT,                                       \
        'U'), 'R'), 'I'), 'E'), 'r'), 'r'), 'o'), 'r')


#define NJS_ARRAY_BUFFER_HASH                                                 \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(NJS_DJB_HASH_INIT,                                       \
        'A'), 'r'), 'r'), 'a'), 'y'), 'B'), 'u'), 'f'), 'f'), 'e'), 'r')


#define NJS_DATA_VIEW_HASH                                                    \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(NJS_DJB_HASH_INIT,                                       \
        'D'), 'a'), 't'), 'a'), 'V'), 'i'), 'e'), 'w')


#define NJS_UINT8ARRAY_HASH                                                   \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(NJS_DJB_HASH_INIT,                                       \
        'U'), 'i'), 'n'), 't'), '8'), 'A'), 'r'), 'r'), 'a'), 'y')


#define NJS_UINT16ARRAY_HASH                                                  \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(NJS_DJB_HASH_INIT,                                       \
        'U'), 'i'), 'n'), 't'), '1'), '6'), 'A'), 'r'), 'r'), 'a'), 'y')


#define NJS_UINT32ARRAY_HASH                                                  \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(NJS_DJB_HASH_INIT,                                       \
        'U'), 'i'), 'n'), 't'), '3'), '2'), 'A'), 'r'), 'r'), 'a'), 'y')


#define NJS_INT8ARRAY_HASH                                                    \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(NJS_DJB_HASH_INIT,                                       \
        'I'), 'n'), 't'), '8'), 'A'), 'r'), 'r'), 'a'), 'y')


#define NJS_INT16ARRAY_HASH                                                   \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(NJS_DJB_HASH_INIT,                                       \
        'I'), 'n'), 't'), '1'), '6'), 'A'), 'r'), 'r'), 'a'), 'y')


#define NJS_INT32ARRAY_HASH                                                   \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(NJS_DJB_HASH_INIT,                                       \
        'I'), 'n'), 't'), '3'), '2'), 'A'), 'r'), 'r'), 'a'), 'y')


#define NJS_FLOAT32ARRAY_HASH                                                 \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(NJS_DJB_HASH_INIT,                                       \
        'F'), 'l'), 'o'), 'a'), 't'), '3'), '2'), 'A'), 'r'), 'r'), 'a'), 'y')


#define NJS_FLOAT64ARRAY_HASH                                                 \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(NJS_DJB_HASH_INIT,                                       \
        'F'), 'l'), 'o'), 'a'), 't'), '6'), '4'), 'A'), 'r'), 'r'), 'a'), 'y')


#define NJS_UINT8CLAMPEDARRAY_HASH                                            \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(NJS_DJB_HASH_INIT,                                       \
        'U'), 'i'), 'n'), 't'), '8'), 'C'), 'l'), 'a'), 'm'), 'p'), 'e'),     \
        'd'), 'A'), 'r'), 'r'), 'a'), 'y')


#define NJS_TEXT_DECODER_HASH                                                 \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(NJS_DJB_HASH_INIT,                                       \
        'T'), 'e'), 'x'), 't'), 'D'), 'e'), 'c'), 'o'), 'd'), 'e'), 'r')


#define NJS_TEXT_ENCODER_HASH                                                 \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(NJS_DJB_HASH_INIT,                                       \
        'T'), 'e'), 'x'), 't'), 'E'), 'n'), 'c'), 'o'), 'd'), 'e'), 'r')


#define NJS_BUFFER_HASH                                                       \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(                                                         \
    njs_djb_hash_add(NJS_DJB_HASH_INIT,                                       \
        'B'), 'u'), 'f'), 'f'), 'e'), 'r')


#endif /* _NJS_OBJECT_HASH_H_INCLUDED_ */
