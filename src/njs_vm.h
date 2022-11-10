
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) NGINX, Inc.
 */

#ifndef _NJS_VM_H_INCLUDED_
#define _NJS_VM_H_INCLUDED_


#define NJS_MAX_STACK_SIZE       (64 * 1024)


typedef struct njs_frame_s            njs_frame_t;
typedef struct njs_native_frame_s     njs_native_frame_t;
typedef struct njs_parser_s           njs_parser_t;
typedef struct njs_parser_scope_s     njs_parser_scope_t;
typedef struct njs_parser_node_s      njs_parser_node_t;
typedef struct njs_generator_s        njs_generator_t;


typedef enum {
    NJS_SCOPE_GLOBAL = 0,
    NJS_SCOPE_FUNCTION,
    NJS_SCOPE_BLOCK
} njs_scope_t;


typedef enum {
    NJS_OBJ_TYPE_OBJECT = 0,
    NJS_OBJ_TYPE_ARRAY,
    NJS_OBJ_TYPE_BOOLEAN,
    NJS_OBJ_TYPE_NUMBER,
    NJS_OBJ_TYPE_SYMBOL,
    NJS_OBJ_TYPE_STRING,
    NJS_OBJ_TYPE_FUNCTION,
    NJS_OBJ_TYPE_ASYNC_FUNCTION,
    NJS_OBJ_TYPE_REGEXP,
    NJS_OBJ_TYPE_DATE,
    NJS_OBJ_TYPE_PROMISE,
    NJS_OBJ_TYPE_ARRAY_BUFFER,
    NJS_OBJ_TYPE_DATA_VIEW,
    NJS_OBJ_TYPE_TEXT_DECODER,
    NJS_OBJ_TYPE_TEXT_ENCODER,
    NJS_OBJ_TYPE_BUFFER,

#define NJS_OBJ_TYPE_HIDDEN_MIN    (NJS_OBJ_TYPE_ITERATOR)
    NJS_OBJ_TYPE_ITERATOR,
    NJS_OBJ_TYPE_ARRAY_ITERATOR,
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
    NJS_OBJ_TYPE_AGGREGATE_ERROR,

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
#ifdef NJS_TEST262
    NJS_OBJECT_262,
#endif
    NJS_OBJECT_MAX
};


enum njs_hook_e {
    NJS_HOOK_EXIT = 0,
    NJS_HOOK_MAX
};


typedef enum {
    NJS_LEVEL_LOCAL = 0,
    NJS_LEVEL_CLOSURE,
    NJS_LEVEL_GLOBAL,
    NJS_LEVEL_STATIC,
    NJS_LEVEL_MAX
} njs_level_type_t;


struct njs_vm_s {
    /* njs_vm_t must be aligned to njs_value_t due to scratch value. */
    njs_value_t              retval;

    njs_arr_t                *paths;
    njs_arr_t                *protos;

    njs_arr_t                *scope_absolute;
    njs_value_t              **levels[NJS_LEVEL_MAX];
    size_t                   global_items;

    njs_external_ptr_t       external;

    njs_native_frame_t       *top_frame;
    njs_frame_t              *active_frame;

    njs_rbtree_t             *variables_hash;
    njs_lvlhsh_t             keywords_hash;
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

    njs_function_t           *hooks[NJS_HOOK_MAX];

    njs_mp_t                 *mem_pool;

    u_char                   *start;
    size_t                   spare_stack_size;

    njs_vm_shared_t          *shared;

    njs_regex_generic_ctx_t  *regex_generic_ctx;
    njs_regex_compile_ctx_t  *regex_compile_ctx;
    njs_regex_match_data_t   *single_match_data;

    njs_array_t              *promise_reason;

    njs_parser_scope_t       *global_scope;

    /*
     * MemoryError is statically allocated immutable Error object
     * with the InternalError prototype.
     */
    njs_object_t             memory_error_object;

    njs_object_t             string_object;
    njs_object_t             global_object;
    njs_value_t              global_value;

    njs_arr_t                *codes;  /* of njs_vm_code_t */
    njs_arr_t                *functions_name_cache;

    njs_trace_t              trace;
    njs_random_t             random;

    njs_rbtree_t             global_symbols;
    uint64_t                 symbol_generator;
};


typedef struct {
    uint32_t                 offset;
    uint32_t                 line;
} njs_vm_line_num_t;


typedef struct {
    u_char                   *start;
    u_char                   *end;
    njs_str_t                file;
    njs_str_t                name;
    njs_arr_t                *lines;  /* of njs_vm_line_num_t */
} njs_vm_code_t;


struct njs_vm_shared_s {
    njs_lvlhsh_t             keywords_hash;
    njs_lvlhsh_t             values_hash;

    njs_lvlhsh_t             array_instance_hash;
    njs_lvlhsh_t             string_instance_hash;
    njs_lvlhsh_t             function_instance_hash;
    njs_lvlhsh_t             async_function_instance_hash;
    njs_lvlhsh_t             arrow_instance_hash;
    njs_lvlhsh_t             arguments_object_instance_hash;
    njs_lvlhsh_t             regexp_instance_hash;

    size_t                   module_items;
    njs_lvlhsh_t             modules_hash;

    njs_lvlhsh_t             env_hash;

    njs_object_t             string_object;
    njs_object_t             objects[NJS_OBJECT_MAX];

    njs_exotic_slots_t       global_slots;

    /*
     * The prototypes and constructors arrays must be togther because they are
     * copied to njs_vm_t by single memcpy() in njs_builtin_objects_clone().
     */
    njs_object_prototype_t   prototypes[NJS_OBJ_TYPE_MAX];
    njs_function_t           constructors[NJS_OBJ_TYPE_MAX];

    njs_regexp_pattern_t     *empty_regexp_pattern;
};


void njs_vm_scopes_restore(njs_vm_t *vm, njs_native_frame_t *frame,
    njs_native_frame_t *previous);

njs_int_t njs_builtin_objects_create(njs_vm_t *vm);
njs_int_t njs_builtin_objects_clone(njs_vm_t *vm, njs_value_t *global);
njs_int_t njs_builtin_match_native_function(njs_vm_t *vm,
    njs_function_t *function, njs_str_t *name);

void njs_disassemble(u_char *start, u_char *end, njs_int_t count,
    njs_arr_t *lines);

njs_arr_t *njs_vm_completions(njs_vm_t *vm, njs_str_t *expression);

void *njs_lvlhsh_alloc(void *data, size_t size);
void njs_lvlhsh_free(void *data, void *p, size_t size);


extern const njs_str_t    njs_entry_empty;
extern const njs_str_t    njs_entry_main;
extern const njs_str_t    njs_entry_module;
extern const njs_str_t    njs_entry_native;
extern const njs_str_t    njs_entry_unknown;
extern const njs_str_t    njs_entry_anonymous;

extern const njs_lvlhsh_proto_t  njs_object_hash_proto;


#endif /* _NJS_VM_H_INCLUDED_ */
