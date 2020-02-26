
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) NGINX, Inc.
 */

#ifndef _NJS_VM_H_INCLUDED_
#define _NJS_VM_H_INCLUDED_


#define NJS_MAX_STACK_SIZE       (256 * 1024)


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
    njs_str_t                         name;
    njs_str_t                         file;
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
 * that is 11.  The 8 is reasonable limit.
 */
#define NJS_MAX_NESTING        8

#define NJS_SCOPES             (NJS_SCOPE_CLOSURE + NJS_MAX_NESTING)

#define NJS_SCOPE_SHIFT        4
#define NJS_SCOPE_MASK         ((uintptr_t) ((1 << NJS_SCOPE_SHIFT) - 1))

#define NJS_INDEX_NONE         ((njs_index_t) 0)
#define NJS_INDEX_ERROR        ((njs_index_t) -1)
#define NJS_INDEX_THIS         ((njs_index_t) (0 | NJS_SCOPE_ARGUMENTS))

#define njs_scope_type(index)                                                 \
     ((uintptr_t) (index) & NJS_SCOPE_MASK)

#define njs_is_callee_argument_index(index)                                   \
    (((index) & NJS_SCOPE_CALLEE_ARGUMENTS) == NJS_SCOPE_CALLEE_ARGUMENTS)


typedef enum {
    NJS_OBJ_TYPE_OBJECT = 0,
    NJS_OBJ_TYPE_ARRAY,
    NJS_OBJ_TYPE_BOOLEAN,
    NJS_OBJ_TYPE_NUMBER,
    NJS_OBJ_TYPE_SYMBOL,
    NJS_OBJ_TYPE_STRING,
    NJS_OBJ_TYPE_FUNCTION,
    NJS_OBJ_TYPE_REGEXP,
    NJS_OBJ_TYPE_DATE,
    NJS_OBJ_TYPE_PROMISE,
    NJS_OBJ_TYPE_ARRAY_BUFFER,

    NJS_OBJ_TYPE_CRYPTO_HASH,
#define NJS_OBJ_TYPE_HIDDEN_MIN    (NJS_OBJ_TYPE_CRYPTO_HASH)
    NJS_OBJ_TYPE_CRYPTO_HMAC,
    NJS_OBJ_TYPE_TYPED_ARRAY,
#define NJS_OBJ_TYPE_HIDDEN_MAX    (NJS_OBJ_TYPE_TYPED_ARRAY + 1)
#define NJS_OBJ_TYPE_NORMAL_MAX    (NJS_OBJ_TYPE_HIDDEN_MAX)

#define NJS_OBJ_TYPE_TYPED_ARRAY_MIN    (NJS_OBJ_TYPE_UINT8_ARRAY)
#define njs_typed_array_index(type)     (type - NJS_OBJ_TYPE_TYPED_ARRAY_MIN)
    NJS_OBJ_TYPE_UINT8_ARRAY,
    NJS_OBJ_TYPE_UINT8_CLAMPED_ARRAY,
    NJS_OBJ_TYPE_INT8_ARRAY,
    NJS_OBJ_TYPE_UINT16_ARRAY,
    NJS_OBJ_TYPE_INT16_ARRAY,
    NJS_OBJ_TYPE_UINT32_ARRAY,
    NJS_OBJ_TYPE_INT32_ARRAY,
    NJS_OBJ_TYPE_FLOAT32_ARRAY,
    NJS_OBJ_TYPE_FLOAT64_ARRAY,
#define NJS_OBJ_TYPE_TYPED_ARRAY_MAX    (NJS_OBJ_TYPE_FLOAT64_ARRAY + 1)
#define NJS_OBJ_TYPE_TYPED_ARRAY_SIZE   (NJS_OBJ_TYPE_TYPED_ARRAY_MAX         \
                                         - NJS_OBJ_TYPE_TYPED_ARRAY_MIN)
    NJS_OBJ_TYPE_ERROR,
    NJS_OBJ_TYPE_EVAL_ERROR,
    NJS_OBJ_TYPE_INTERNAL_ERROR,
    NJS_OBJ_TYPE_RANGE_ERROR,
    NJS_OBJ_TYPE_REF_ERROR,
    NJS_OBJ_TYPE_SYNTAX_ERROR,
    NJS_OBJ_TYPE_TYPE_ERROR,
    NJS_OBJ_TYPE_URI_ERROR,
    NJS_OBJ_TYPE_MEMORY_ERROR,
    NJS_OBJ_TYPE_MAX,
} njs_object_type_t;


#define njs_primitive_prototype_index(type)                                   \
    (NJS_OBJ_TYPE_BOOLEAN + ((type) - NJS_BOOLEAN))


#define njs_prototype_type(index)                                             \
    (index + NJS_OBJECT)


enum njs_object_e {
    NJS_OBJECT_THIS = 0,
    NJS_OBJECT_NJS,
    NJS_OBJECT_PROCESS,
    NJS_OBJECT_MATH,
    NJS_OBJECT_JSON,
#define NJS_OBJECT_MAX         (NJS_OBJECT_JSON + 1)
};


#define njs_scope_index(value, type)                                          \
    ((njs_index_t) (((value) << NJS_SCOPE_SHIFT) | (type)))


#define njs_global_scope_index(value)                                         \
    (njs_scope_index(value, NJS_SCOPE_GLOBAL))


#define NJS_INDEX_GLOBAL_OBJECT  njs_global_scope_index(0)
#define NJS_INDEX_GLOBAL_OBJECT_OFFSET  njs_scope_index(0, 0)


#define NJS_INDEX_GLOBAL_RETVAL  njs_global_scope_index(1)
#define NJS_INDEX_GLOBAL_OFFSET  njs_scope_index(1, 0)


#define njs_scope_offset(index)                                               \
    ((uintptr_t) (index) & ~NJS_SCOPE_MASK)


#define njs_vmcode_operand(vm, index)                                         \
    ((njs_value_t *)                                                          \
     ((u_char *) vm->scopes[(uintptr_t) (index) & NJS_SCOPE_MASK]             \
      + njs_scope_offset(index)))


typedef struct {
    uint32_t                  line;
    njs_str_t                 file;
    njs_str_t                 name;
    njs_function_lambda_t     *lambda;
} njs_function_debug_t;


struct njs_vm_s {
    /* njs_vm_t must be aligned to njs_value_t due to scratch value. */
    njs_value_t              retval;

    njs_arr_t                *paths;

    u_char                   *start;

    njs_value_t              *scopes[NJS_SCOPES];

    njs_external_ptr_t       external;

    njs_native_frame_t       *top_frame;
    njs_frame_t              *active_frame;

    njs_arr_t                *external_objects; /* of njs_external_ptr_t */

    njs_lvlhsh_t             external_prototypes_hash;

    njs_rbtree_t             *variables_hash;
    njs_lvlhsh_t             values_hash;

    njs_arr_t                *modules;
    njs_lvlhsh_t             modules_hash;

    uint32_t                 event_id;
    njs_lvlhsh_t             events_hash;
    njs_queue_t              posted_events;
    njs_queue_t              promise_events;

    njs_vm_opt_t             options;

    /*
     * The prototypes and constructors arrays must be together because
     * they are copied from njs_vm_shared_t by single memcpy()
     * in njs_builtin_objects_clone().
     */
    njs_object_prototype_t   prototypes[NJS_OBJ_TYPE_MAX];
    njs_function_t           constructors[NJS_OBJ_TYPE_MAX];

    njs_mp_t                 *mem_pool;

    njs_value_t              *global_scope;
    size_t                   scope_size;
    size_t                   stack_size;

    njs_vm_shared_t          *shared;
    njs_parser_t             *parser;

    njs_regex_context_t      *regex_context;
    njs_regex_match_data_t   *single_match_data;

    /*
     * MemoryError is statically allocated immutable Error object
     * with the InternalError prototype.
     */
    njs_object_t             memory_error_object;

    njs_object_t             string_object;
    njs_object_t             global_object;

    njs_arr_t                *codes;  /* of njs_vm_code_t */

    njs_trace_t              trace;
    njs_random_t             random;

    njs_arr_t                *debug;

    /*
     * njs_property_query() uses it to store reference to a temporary
     * PROPERTY_HANDLERs for NJS_EXTERNAL values in NJS_PROPERTY_QUERY_SET
     * and NJS_PROPERTY_QUERY_DELETE modes.
     */
    uintptr_t                stash; /* njs_property_query_t * */

    uint64_t                 symbol_generator;
};


typedef struct {
    u_char                   *start;
    u_char                   *end;
    njs_str_t                file;
    njs_str_t                name;
} njs_vm_code_t;


struct njs_vm_shared_s {
    njs_lvlhsh_t             keywords_hash;
    njs_lvlhsh_t             values_hash;

    njs_lvlhsh_t             array_instance_hash;
    njs_lvlhsh_t             string_instance_hash;
    njs_lvlhsh_t             function_instance_hash;
    njs_lvlhsh_t             arrow_instance_hash;
    njs_lvlhsh_t             arguments_object_instance_hash;
    njs_lvlhsh_t             regexp_instance_hash;

    njs_lvlhsh_t             modules_hash;

    njs_lvlhsh_t             env_hash;

    njs_object_t             string_object;
    njs_object_t             objects[NJS_OBJECT_MAX];

    /*
     * The prototypes and constructors arrays must be togther because they are
     * copied to njs_vm_t by single memcpy() in njs_builtin_objects_clone().
     */
    njs_object_prototype_t   prototypes[NJS_OBJ_TYPE_MAX];
    njs_function_t           constructors[NJS_OBJ_TYPE_MAX];

    njs_regexp_pattern_t     *empty_regexp_pattern;
};


void njs_vm_scopes_restore(njs_vm_t *vm, njs_frame_t *frame,
    njs_native_frame_t *previous);
njs_int_t njs_vm_add_backtrace_entry(njs_vm_t *vm, njs_arr_t *stack,
    njs_native_frame_t *native_frame);
njs_int_t njs_vm_backtrace_to_string(njs_vm_t *vm, njs_arr_t *stack,
    njs_str_t *dst);

njs_int_t njs_builtin_objects_create(njs_vm_t *vm);
njs_int_t njs_builtin_objects_clone(njs_vm_t *vm, njs_value_t *global);
njs_int_t njs_builtin_match_native_function(njs_vm_t *vm,
    njs_function_t *function, njs_str_t *name);

njs_arr_t *njs_vm_completions(njs_vm_t *vm, njs_str_t *expression);

void *njs_lvlhsh_alloc(void *data, size_t size);
void njs_lvlhsh_free(void *data, void *p, size_t size);


extern const njs_str_t    njs_entry_main;
extern const njs_str_t    njs_entry_module;
extern const njs_str_t    njs_entry_native;
extern const njs_str_t    njs_entry_unknown;
extern const njs_str_t    njs_entry_anonymous;

extern const njs_lvlhsh_proto_t  njs_object_hash_proto;


#endif /* _NJS_VM_H_INCLUDED_ */
