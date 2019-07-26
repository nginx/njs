
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) NGINX, Inc.
 */

#ifndef _NJS_VM_H_INCLUDED_
#define _NJS_VM_H_INCLUDED_


#define NJS_MAX_STACK_SIZE       (16 * 1024 * 1024)


/*
 * NJS_PROPERTY_QUERY_GET must be less to NJS_PROPERTY_QUERY_SET
 * and NJS_PROPERTY_QUERY_DELETE.
 */
#define NJS_PROPERTY_QUERY_GET     0
#define NJS_PROPERTY_QUERY_SET     1
#define NJS_PROPERTY_QUERY_DELETE  2


typedef struct njs_frame_s            njs_frame_t;
typedef struct njs_native_frame_s     njs_native_frame_t;
typedef struct njs_parser_s           njs_parser_t;
typedef struct njs_parser_scope_s     njs_parser_scope_t;
typedef struct njs_parser_node_s      njs_parser_node_t;
typedef struct njs_generator_s        njs_generator_t;


typedef struct {
    nxt_str_t                         name;
    nxt_str_t                         file;
    uint32_t                          line;
} njs_backtrace_entry_t;


typedef enum {
    NJS_SCOPE_ABSOLUTE = 0,
    NJS_SCOPE_GLOBAL = 1,
    NJS_SCOPE_CALLEE_ARGUMENTS = 2,
    /*
     * The argument and local VM scopes should be separated because a
     * function may be called with any number of arguments.
     */
    NJS_SCOPE_ARGUMENTS = 3,
    NJS_SCOPE_LOCAL = 4,
    NJS_SCOPE_FUNCTION = NJS_SCOPE_LOCAL,

    NJS_SCOPE_CLOSURE = 5,
    /*
     * The block and shim scopes are not really VM scopes.
     * They are used only on parsing phase.
     */
    NJS_SCOPE_BLOCK = 16,
    NJS_SCOPE_SHIM = 17,
} njs_scope_t;


/*
 * The maximum possible function nesting level is (16 - NJS_SCOPE_CLOSURE),
 * that is 11.  The 5 is reasonable limit.
 */
#define NJS_MAX_NESTING        5

#define NJS_SCOPES             (NJS_SCOPE_CLOSURE + NJS_MAX_NESTING)

#define NJS_SCOPE_SHIFT        4
#define NJS_SCOPE_MASK         ((uintptr_t) ((1 << NJS_SCOPE_SHIFT) - 1))

#define NJS_INDEX_CACHE        NJS_SCOPE_GLOBAL

#define NJS_INDEX_NONE         ((njs_index_t) 0)
#define NJS_INDEX_ERROR        ((njs_index_t) -1)
#define NJS_INDEX_THIS         ((njs_index_t) (0 | NJS_SCOPE_ARGUMENTS))

#define njs_scope_type(index)                                                 \
     ((uintptr_t) (index) & NJS_SCOPE_MASK)

#define njs_is_callee_argument_index(index)                                   \
    (((index) & NJS_SCOPE_CALLEE_ARGUMENTS) == NJS_SCOPE_CALLEE_ARGUMENTS)


enum njs_prototypes_e {
    NJS_PROTOTYPE_OBJECT = 0,
    NJS_PROTOTYPE_ARRAY,
    NJS_PROTOTYPE_BOOLEAN,
    NJS_PROTOTYPE_NUMBER,
    NJS_PROTOTYPE_STRING,
    NJS_PROTOTYPE_FUNCTION,
    NJS_PROTOTYPE_REGEXP,
    NJS_PROTOTYPE_DATE,
    NJS_PROTOTYPE_CRYPTO_HASH,
    NJS_PROTOTYPE_CRYPTO_HMAC,
    NJS_PROTOTYPE_ERROR,
    NJS_PROTOTYPE_EVAL_ERROR,
    NJS_PROTOTYPE_INTERNAL_ERROR,
    NJS_PROTOTYPE_RANGE_ERROR,
    NJS_PROTOTYPE_REF_ERROR,
    NJS_PROTOTYPE_SYNTAX_ERROR,
    NJS_PROTOTYPE_TYPE_ERROR,
    NJS_PROTOTYPE_URI_ERROR,
#define NJS_PROTOTYPE_MAX      (NJS_PROTOTYPE_URI_ERROR + 1)
};


#define njs_primitive_prototype_index(type)                                   \
    (NJS_PROTOTYPE_BOOLEAN + ((type) - NJS_BOOLEAN))


#define njs_error_prototype_index(type)                                       \
    (NJS_PROTOTYPE_ERROR + ((type) - NJS_OBJECT_ERROR))


#define njs_prototype_type(index)                                             \
    (index + NJS_OBJECT)


enum njs_constructor_e {
    NJS_CONSTRUCTOR_OBJECT =         NJS_PROTOTYPE_OBJECT,
    NJS_CONSTRUCTOR_ARRAY =          NJS_PROTOTYPE_ARRAY,
    NJS_CONSTRUCTOR_BOOLEAN =        NJS_PROTOTYPE_BOOLEAN,
    NJS_CONSTRUCTOR_NUMBER =         NJS_PROTOTYPE_NUMBER,
    NJS_CONSTRUCTOR_STRING =         NJS_PROTOTYPE_STRING,
    NJS_CONSTRUCTOR_FUNCTION =       NJS_PROTOTYPE_FUNCTION,
    NJS_CONSTRUCTOR_REGEXP =         NJS_PROTOTYPE_REGEXP,
    NJS_CONSTRUCTOR_DATE =           NJS_PROTOTYPE_DATE,
    NJS_CONSTRUCTOR_CRYPTO_HASH =    NJS_PROTOTYPE_CRYPTO_HASH,
    NJS_CONSTRUCTOR_CRYPTO_HMAC =    NJS_PROTOTYPE_CRYPTO_HMAC,
    NJS_CONSTRUCTOR_ERROR =          NJS_PROTOTYPE_ERROR,
    NJS_CONSTRUCTOR_EVAL_ERROR =     NJS_PROTOTYPE_EVAL_ERROR,
    NJS_CONSTRUCTOR_INTERNAL_ERROR = NJS_PROTOTYPE_INTERNAL_ERROR,
    NJS_CONSTRUCTOR_RANGE_ERROR =    NJS_PROTOTYPE_RANGE_ERROR,
    NJS_CONSTRUCTOR_REF_ERROR =      NJS_PROTOTYPE_REF_ERROR,
    NJS_CONSTRUCTOR_SYNTAX_ERROR =   NJS_PROTOTYPE_SYNTAX_ERROR,
    NJS_CONSTRUCTOR_TYPE_ERROR =     NJS_PROTOTYPE_TYPE_ERROR,
    NJS_CONSTRUCTOR_URI_ERROR =      NJS_PROTOTYPE_URI_ERROR,
    /* MemoryError has no its own prototype. */
    NJS_CONSTRUCTOR_MEMORY_ERROR,
#define NJS_CONSTRUCTOR_MAX    (NJS_CONSTRUCTOR_MEMORY_ERROR + 1)
};


enum njs_object_e {
    NJS_OBJECT_THIS = 0,
    NJS_OBJECT_NJS,
    NJS_OBJECT_MATH,
    NJS_OBJECT_JSON,
#define NJS_OBJECT_MAX         (NJS_OBJECT_JSON + 1)
};


enum njs_function_e {
    NJS_FUNCTION_EVAL = 0,
    NJS_FUNCTION_TO_STRING,
    NJS_FUNCTION_IS_NAN,
    NJS_FUNCTION_IS_FINITE,
    NJS_FUNCTION_PARSE_INT,
    NJS_FUNCTION_PARSE_FLOAT,
    NJS_FUNCTION_STRING_ENCODE_URI,
    NJS_FUNCTION_STRING_ENCODE_URI_COMPONENT,
    NJS_FUNCTION_STRING_DECODE_URI,
    NJS_FUNCTION_STRING_DECODE_URI_COMPONENT,
    NJS_FUNCTION_REQUIRE,
    NJS_FUNCTION_SET_TIMEOUT,
    NJS_FUNCTION_SET_IMMEDIATE,
    NJS_FUNCTION_CLEAR_TIMEOUT,
#define NJS_FUNCTION_MAX       (NJS_FUNCTION_CLEAR_TIMEOUT + 1)
};


#define njs_scope_index(value, type)                                          \
    ((njs_index_t) (((value) << NJS_SCOPE_SHIFT) | (type)))

#define njs_global_scope_index(value)                                         \
    ((njs_index_t) (((value) << NJS_SCOPE_SHIFT) | NJS_SCOPE_GLOBAL))


#define NJS_INDEX_OBJECT         njs_global_scope_index(NJS_CONSTRUCTOR_OBJECT)
#define NJS_INDEX_ARRAY          njs_global_scope_index(NJS_CONSTRUCTOR_ARRAY)
#define NJS_INDEX_BOOLEAN        njs_global_scope_index(NJS_CONSTRUCTOR_BOOLEAN)
#define NJS_INDEX_NUMBER         njs_global_scope_index(NJS_CONSTRUCTOR_NUMBER)
#define NJS_INDEX_STRING         njs_global_scope_index(NJS_CONSTRUCTOR_STRING)
#define NJS_INDEX_FUNCTION                                                    \
    njs_global_scope_index(NJS_CONSTRUCTOR_FUNCTION)
#define NJS_INDEX_REGEXP         njs_global_scope_index(NJS_CONSTRUCTOR_REGEXP)
#define NJS_INDEX_DATE           njs_global_scope_index(NJS_CONSTRUCTOR_DATE)
#define NJS_INDEX_OBJECT_ERROR   njs_global_scope_index(NJS_CONSTRUCTOR_ERROR)
#define NJS_INDEX_OBJECT_EVAL_ERROR                                           \
    njs_global_scope_index(NJS_CONSTRUCTOR_EVAL_ERROR)
#define NJS_INDEX_OBJECT_INTERNAL_ERROR                                       \
    njs_global_scope_index(NJS_CONSTRUCTOR_INTERNAL_ERROR)
#define NJS_INDEX_OBJECT_RANGE_ERROR                                          \
    njs_global_scope_index(NJS_CONSTRUCTOR_RANGE_ERROR)
#define NJS_INDEX_OBJECT_REF_ERROR                                            \
    njs_global_scope_index(NJS_CONSTRUCTOR_REF_ERROR)
#define NJS_INDEX_OBJECT_SYNTAX_ERROR                                         \
    njs_global_scope_index(NJS_CONSTRUCTOR_SYNTAX_ERROR)
#define NJS_INDEX_OBJECT_TYPE_ERROR                                           \
    njs_global_scope_index(NJS_CONSTRUCTOR_TYPE_ERROR)
#define NJS_INDEX_OBJECT_URI_ERROR                                            \
    njs_global_scope_index(NJS_CONSTRUCTOR_URI_ERROR)
#define NJS_INDEX_OBJECT_MEMORY_ERROR                                         \
    njs_global_scope_index(NJS_CONSTRUCTOR_MEMORY_ERROR)

#define NJS_INDEX_GLOBAL_RETVAL  njs_global_scope_index(NJS_CONSTRUCTOR_MAX)
#define NJS_INDEX_GLOBAL_OFFSET  njs_scope_index(NJS_CONSTRUCTOR_MAX + 1, 0)


#define njs_scope_offset(index)                                               \
    ((uintptr_t) (index) & ~NJS_SCOPE_MASK)


#define njs_vmcode_operand(vm, index)                                         \
    ((njs_value_t *)                                                          \
     ((u_char *) vm->scopes[(uintptr_t) (index) & NJS_SCOPE_MASK]             \
      + njs_scope_offset(index)))


typedef struct {
    uint32_t                  line;
    nxt_str_t                 file;
    nxt_str_t                 name;
    njs_function_lambda_t     *lambda;
} njs_function_debug_t;


struct njs_vm_s {
    /* njs_vm_t must be aligned to njs_value_t due to scratch value. */
    njs_value_t              retval;

    nxt_uint_t               count;

    nxt_array_t              *paths;

    u_char                   *start;

    njs_value_t              *scopes[NJS_SCOPES];

    njs_external_ptr_t       external;

    njs_native_frame_t       *top_frame;
    njs_frame_t              *active_frame;

    nxt_array_t              *external_objects; /* of njs_external_ptr_t */

    nxt_lvlhsh_t             externals_hash;
    nxt_lvlhsh_t             external_prototypes_hash;

    nxt_lvlhsh_t             variables_hash;
    nxt_lvlhsh_t             values_hash;

    nxt_array_t              *modules;
    nxt_lvlhsh_t             modules_hash;

    uint32_t                 event_id;
    nxt_lvlhsh_t             events_hash;
    nxt_queue_t              posted_events;

    njs_vm_opt_t             options;

    /*
     * The prototypes and constructors arrays must be together because
     * they are copied from njs_vm_shared_t by single memcpy()
     * in njs_builtin_objects_clone().
     */
    njs_object_prototype_t   prototypes[NJS_PROTOTYPE_MAX];
    njs_function_t           constructors[NJS_CONSTRUCTOR_MAX];

    nxt_mp_t                 *mem_pool;

    njs_value_t              *global_scope;
    size_t                   scope_size;
    size_t                   stack_size;

    njs_vm_shared_t          *shared;
    njs_parser_t             *parser;

    nxt_regex_context_t      *regex_context;
    nxt_regex_match_data_t   *single_match_data;

    /*
     * MemoryError is statically allocated immutable Error object
     * with the generic type NJS_OBJECT_INTERNAL_ERROR.
     */
    njs_object_t             memory_error_object;

    njs_object_t             string_object;

    nxt_array_t              *codes;  /* of njs_vm_code_t */

    nxt_trace_t              trace;
    nxt_random_t             random;

    nxt_array_t              *debug;
    nxt_array_t              *backtrace;

    /*
     * njs_property_query() uses it to store reference to a temporary
     * PROPERTY_HANDLERs for NJS_EXTERNAL values in NJS_PROPERTY_QUERY_SET
     * and NJS_PROPERTY_QUERY_DELETE modes.
     */
    uintptr_t                stash; /* njs_property_query_t * */
};


typedef struct {
    u_char                   *start;
    u_char                   *end;
    nxt_str_t                file;
    nxt_str_t                name;
} njs_vm_code_t;


struct njs_vm_shared_s {
    nxt_lvlhsh_t             keywords_hash;
    nxt_lvlhsh_t             values_hash;
    nxt_lvlhsh_t             array_instance_hash;
    nxt_lvlhsh_t             string_instance_hash;
    nxt_lvlhsh_t             function_instance_hash;
    nxt_lvlhsh_t             arrow_instance_hash;
    nxt_lvlhsh_t             arguments_object_instance_hash;

    nxt_lvlhsh_t             env_hash;

    njs_object_t             string_object;
    njs_object_t             objects[NJS_OBJECT_MAX];
    njs_function_t           functions[NJS_FUNCTION_MAX];

    /*
     * The prototypes and constructors arrays must be togther because they are
     * copied to njs_vm_t by single memcpy() in njs_builtin_objects_clone().
     */
    njs_object_prototype_t   prototypes[NJS_PROTOTYPE_MAX];
    njs_function_t           constructors[NJS_CONSTRUCTOR_MAX];

    njs_regexp_pattern_t     *empty_regexp_pattern;
};


void njs_vm_scopes_restore(njs_vm_t *vm, njs_frame_t *frame,
    njs_native_frame_t *previous);
njs_ret_t njs_vm_add_backtrace_entry(njs_vm_t *vm, njs_frame_t *frame);

nxt_int_t njs_builtin_objects_create(njs_vm_t *vm);
nxt_int_t njs_builtin_objects_clone(njs_vm_t *vm);
nxt_int_t njs_builtin_match_native_function(njs_vm_t *vm,
    njs_function_t *function, nxt_str_t *name);

nxt_array_t *njs_vm_backtrace(njs_vm_t *vm);

void *njs_lvlhsh_alloc(void *data, size_t size);
void njs_lvlhsh_free(void *data, void *p, size_t size);


extern const nxt_str_t    njs_entry_main;
extern const nxt_str_t    njs_entry_module;
extern const nxt_str_t    njs_entry_native;
extern const nxt_str_t    njs_entry_unknown;
extern const nxt_str_t    njs_entry_anonymous;

extern const nxt_mem_proto_t     njs_array_mem_proto;
extern const nxt_lvlhsh_proto_t  njs_object_hash_proto;


#endif /* _NJS_VM_H_INCLUDED_ */
