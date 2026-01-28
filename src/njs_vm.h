
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) NGINX, Inc.
 */

#ifndef _NJS_VM_H_INCLUDED_
#define _NJS_VM_H_INCLUDED_


#define NJS_MAX_STACK_SIZE       (160 * 1024)


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
#define NJS_OBJ_TYPE_ERROR_MAX         (NJS_OBJ_TYPE_AGGREGATE_ERROR)

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
    njs_value_t              exception;

    njs_arr_t                *protos;

    njs_arr_t                *scope_absolute;
    njs_value_t              **levels[NJS_LEVEL_MAX];

    njs_external_ptr_t       external;

    njs_native_frame_t       *top_frame;
    njs_frame_t              *active_frame;

    njs_flathsh_t            atom_hash_shared;
    njs_flathsh_t            atom_hash;
    njs_flathsh_t            *atom_hash_current;
    uint32_t                 shared_atom_count;
    uint32_t                 atom_id_generator;

    njs_flathsh_t            values_hash;

    njs_arr_t                *modules;
    njs_flathsh_t            modules_hash;

    uint32_t                 event_id;
    njs_queue_t              jobs;

    njs_vm_opt_t             options;

#define njs_vm_proto(vm, index) (&(vm)->prototypes[index].object)
#define njs_vm_ctor(vm, index) ((vm)->constructors[index])

    njs_object_prototype_t   *prototypes;
    njs_function_t           *constructors;
    size_t                   constructors_size;

    njs_function_t           *hooks[NJS_HOOK_MAX];

    njs_mp_t                 *mem_pool;

    u_char                   *start;
    size_t                   spare_stack_size;

    njs_vm_shared_t          *shared;

    njs_regex_generic_ctx_t  *regex_generic_ctx;
    njs_regex_compile_ctx_t  *regex_compile_ctx;
    njs_regex_match_data_t   *single_match_data;

    njs_parser_scope_t       *global_scope;

    /*
     * MemoryError is statically allocated immutable Error object
     * with the InternalError prototype.
     */
    njs_object_value_t       memory_error_object;

    njs_object_t             string_object;
    njs_object_t             global_object;
    njs_value_t              global_value;

    njs_arr_t                *codes;  /* of njs_vm_code_t */
    njs_arr_t                *functions_name_cache;

    njs_trace_t              trace;
    njs_random_t             random;

    njs_rbtree_t             global_symbols;

    njs_module_loader_t      module_loader;
    void                     *module_loader_opaque;
    njs_rejection_tracker_t  rejection_tracker;
    void                     *rejection_tracker_opaque;
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
    njs_flathsh_t            values_hash;

    njs_flathsh_t            array_instance_hash;
    njs_flathsh_t            string_instance_hash;
    njs_flathsh_t            function_instance_hash;
    njs_flathsh_t            async_function_instance_hash;
    njs_flathsh_t            arrow_instance_hash;
    njs_flathsh_t            arguments_object_instance_hash;
    njs_flathsh_t            regexp_instance_hash;

    size_t                   module_items;
    njs_flathsh_t            modules_hash;

    njs_flathsh_t            env_hash;

    njs_object_t             string_object;
    njs_object_t             objects[NJS_OBJECT_MAX];

#define njs_shared_ctor(shared, index)                                       \
    ((njs_function_t *) njs_arr_item((shared)->constructors, index))

#define njs_shared_prototype(shared, index)                                  \
    ((njs_object_prototype_t *) njs_arr_item((shared)->prototypes, index))

    njs_arr_t                *constructors; /* of njs_function_t */
    njs_arr_t                *prototypes; /* of njs_object_prototype_t */

    njs_exotic_slots_t       global_slots;

    njs_regexp_pattern_t     *empty_regexp_pattern;
};


njs_int_t njs_vm_runtime_init(njs_vm_t *vm);
njs_int_t njs_vm_ctor_push(njs_vm_t *vm);
void njs_vm_constructors_init(njs_vm_t *vm);
njs_value_t njs_vm_exception(njs_vm_t *vm);
void njs_vm_scopes_restore(njs_vm_t *vm, njs_native_frame_t *frame);

njs_int_t njs_builtin_objects_create(njs_vm_t *vm);
njs_int_t njs_builtin_match_native_function(njs_vm_t *vm,
    njs_function_t *function, njs_str_t *name);

void njs_disassemble(u_char *start, u_char *end, njs_int_t count,
    njs_arr_t *lines);

void *njs_flathsh_proto_alloc(void *data, size_t size);
void njs_flathsh_proto_free(void *data, void *p, size_t size);


extern const njs_str_t    njs_entry_empty;
extern const njs_str_t    njs_entry_main;
extern const njs_str_t    njs_entry_module;
extern const njs_str_t    njs_entry_unknown;
extern const njs_str_t    njs_entry_anonymous;

extern const njs_flathsh_proto_t  njs_object_hash_proto;


#endif /* _NJS_VM_H_INCLUDED_ */
