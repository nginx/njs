
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) NGINX, Inc.
 *
 * njs public header.
 */

#ifndef _NJS_H_INCLUDED_
#define _NJS_H_INCLUDED_

#include <njs_auto_config.h>

#define NJS_VERSION                 "0.9.5"
#define NJS_VERSION_NUMBER          0x000905


#include <string.h>
#include <njs_types.h>
#include <njs_clang.h>
#include <njs_str.h>
#include <njs_unicode.h>
#include <njs_utf8.h>
#include <njs_mp.h>
#include <njs_chb.h>
#include <njs_sprintf.h>


typedef uintptr_t                     njs_index_t;
typedef struct njs_vm_s               njs_vm_t;
typedef struct njs_mod_s              njs_mod_t;
typedef union  njs_value_s            njs_value_t;
typedef struct njs_function_s         njs_function_t;
typedef struct njs_vm_shared_s        njs_vm_shared_t;
typedef struct njs_object_init_s      njs_object_init_t;
typedef struct njs_object_prop_s      njs_object_prop_t;
typedef struct njs_promise_data_s     njs_promise_data_t;
typedef struct njs_object_prop_init_s njs_object_prop_init_t;
typedef struct njs_object_type_init_s njs_object_type_init_t;
typedef struct njs_external_s         njs_external_t;

/*
 * njs_opaque_value_t is the external storage type for native njs_value_t type.
 * sizeof(njs_opaque_value_t) == sizeof(njs_value_t).
 */

typedef struct {
    uint32_t                        filler[4];
} njs_opaque_value_t;

/* sizeof(njs_value_t) is 16 bytes. */
#define njs_argument(args, n)                                                 \
    (njs_value_t *) ((u_char *) args + (n) * 16)


extern const njs_value_t            njs_value_undefined;

#define njs_arg(args, nargs, n)                                               \
    ((n < nargs) ? njs_argument(args, n)                                      \
                 : (njs_value_t *) &njs_value_undefined)

#define njs_value_assign(dst, src)                                            \
    memcpy(dst, src, sizeof(njs_opaque_value_t))

#define njs_value_arg(val) ((njs_value_t *) val)
#define njs_value_atom(val) (((njs_opaque_value_t *) (val))->filler[0])

#define njs_atom_is_number(atom_id) ((atom_id) & 0x80000000)
#define njs_atom_number(atom_id) ((atom_id) & 0x7FFFFFFF)
#define njs_number_atom(n) ((n) | 0x80000000)

#define njs_lvalue_arg(lvalue, args, nargs, n)                                \
    ((n < nargs) ? njs_argument(args, n)                                      \
                 : (njs_value_assign(lvalue, &njs_value_undefined), lvalue))

#define njs_vm_error(vm, fmt, ...)                                            \
    njs_vm_error2(vm, 0, fmt, ##__VA_ARGS__)
#define njs_vm_internal_error(vm, fmt, ...)                                   \
    njs_vm_error2(vm, 2, fmt, ##__VA_ARGS__)
#define njs_vm_range_error(vm, fmt, ...)                                      \
    njs_vm_error2(vm, 3, fmt, ##__VA_ARGS__)
#define njs_vm_ref_error(vm, fmt, ...)                                        \
    njs_vm_error2(vm, 4, fmt, ##__VA_ARGS__)
#define njs_vm_syntax_error(vm, fmt, ...)                                     \
    njs_vm_error2(vm, 5, fmt, ##__VA_ARGS__)
#define njs_vm_type_error(vm, fmt, ...)                                       \
    njs_vm_error2(vm, 6, fmt, ##__VA_ARGS__)

#define njs_deprecated(vm, text)                                             \
    do {                                                                     \
        static njs_bool_t  reported;                                         \
                                                                             \
        if (!reported) {                                                     \
            njs_vm_warn(vm, text " is deprecated "                           \
                        "and will be removed in the future");                \
            reported = 1;                                                    \
        }                                                                    \
    } while(0)

/*
 * njs_prop_handler_t operates as a property getter/setter or delete handler.
 * - retval != NULL && setval == NULL - GET context.
 * - retval != NULL && setval != NULL - SET context.
 * - retval == NULL - DELETE context.
 *
 * njs_prop_handler_t is expected to return:
 *   NJS_OK - handler executed successfully;
 *   NJS_DECLINED - handler was applied to inappropriate object, retval
 *   contains undefined value;
 *   NJS_ERROR - some error, njs_vm_exception_get(vm) can be used to get
 *   the exception value.
 */
typedef njs_int_t (*njs_prop_handler_t) (njs_vm_t *vm, njs_object_prop_t *prop,
    uint32_t atom_id, njs_value_t *value, njs_value_t *setval,
    njs_value_t *retval);
typedef njs_int_t (*njs_exotic_keys_t)(njs_vm_t *vm, njs_value_t *value,
    njs_value_t *retval);
typedef njs_int_t (*njs_function_native_t) (njs_vm_t *vm, njs_value_t *args,
    njs_uint_t nargs, njs_index_t magic8, njs_value_t *retval);


typedef enum {
    NJS_SYMBOL_INVALID,
    NJS_SYMBOL_ASYNC_ITERATOR,
    NJS_SYMBOL_HAS_INSTANCE,
    NJS_SYMBOL_IS_CONCAT_SPREADABLE,
    NJS_SYMBOL_ITERATOR,
    NJS_SYMBOL_MATCH,
    NJS_SYMBOL_MATCH_ALL,
    NJS_SYMBOL_REPLACE,
    NJS_SYMBOL_SEARCH,
    NJS_SYMBOL_SPECIES,
    NJS_SYMBOL_SPLIT,
    NJS_SYMBOL_TO_PRIMITIVE,
    NJS_SYMBOL_TO_STRING_TAG,
    NJS_SYMBOL_UNSCOPABLES,
    NJS_SYMBOL_KNOWN_MAX,
} njs_wellknown_symbol_t;


typedef enum {
    NJS_PROMISE_PENDING = 0,
    NJS_PROMISE_FULFILL,
    NJS_PROMISE_REJECTED
} njs_promise_type_t;


typedef enum {
#define njs_object_enum_kind(flags) (flags & 7)
    NJS_ENUM_KEYS = 1,
    NJS_ENUM_VALUES = 2,
    NJS_ENUM_BOTH = 4,
#define njs_object_enum(flags) (flags & (NJS_ENUM_STRING | NJS_ENUM_SYMBOL))
    NJS_ENUM_STRING = 8,
    NJS_ENUM_SYMBOL = 16,
    NJS_ENUM_ENUMERABLE_ONLY = 32,
    NJS_ENUM_NON_SHARED_ONLY = 64,
} njs_object_enum_t;


typedef enum {
    /*
     * Extern property type.
     */
    NJS_EXTERN_PROPERTY = 0,
    NJS_EXTERN_METHOD = 1,
    NJS_EXTERN_OBJECT = 2,
    NJS_EXTERN_SELF = 3,
#define NJS_EXTERN_TYPE_MASK    3
    /*
     * Extern property flags.
     */
    NJS_EXTERN_SYMBOL = 4,
} njs_extern_flag_t;


typedef enum {
    NJS_EXTERN_TYPE_INT = 0,
    NJS_EXTERN_TYPE_UINT = 1,
    NJS_EXTERN_TYPE_VALUE = 2,
} njs_extern_type_t;


struct njs_external_s {
    njs_extern_flag_t               flags;

    union {
        njs_str_t                   string;
        uint32_t                    symbol;
    } name;

    unsigned                        writable;
    unsigned                        configurable;
    unsigned                        enumerable;

    union {
        struct {
            const char              *value;
            njs_prop_handler_t      handler;
            uint16_t                magic16;
            uint32_t                magic32;
        } property;

        struct {
            njs_function_native_t   native;
            uint8_t                 magic8;
            uint8_t                 ctor;
        } method;

        struct {
            njs_external_t          *properties;
            njs_uint_t              nproperties;

            unsigned                writable;
            unsigned                configurable;
            unsigned                enumerable;
            njs_prop_handler_t      prop_handler;
            uint32_t                magic32;
            njs_exotic_keys_t       keys;
        } object;
    } u;
};


typedef void *                      njs_external_ptr_t;

typedef njs_mod_t *(*njs_module_loader_t)(njs_vm_t *vm,
    njs_external_ptr_t external, njs_str_t *name);
typedef void (*njs_rejection_tracker_t)(njs_vm_t *vm,
    njs_external_ptr_t external, njs_bool_t is_handled, njs_value_t *promise,
    njs_value_t *reason);


typedef struct {
    size_t                          size;
    uintptr_t                       *values;
} njs_vm_meta_t;


typedef njs_int_t (*njs_addon_init_pt)(njs_vm_t *vm);

typedef struct {
    njs_str_t                       name;
    njs_addon_init_pt               preinit;
    njs_addon_init_pt               init;
} njs_module_t;


typedef struct {
    njs_external_ptr_t              external;
    njs_vm_shared_t                 *shared;
    njs_vm_meta_t                   *metas;
    njs_module_t                    **addons;
    njs_str_t                       file;

    char                            **argv;
    njs_uint_t                      argc;

    njs_uint_t                      max_stack_size;

/*
 * interactive  - enables "interactive" mode.
 *  (REPL). Allows starting parent VM without cloning.
 * disassemble   - enables disassemble.
 * backtrace     - enables backtraces.
 * quiet         - removes filenames from backtraces. To produce comparable
    test262 diffs.
 * sandbox       - "sandbox" mode. Disables file access.
 * unsafe        - enables unsafe language features:
 *   - Function constructors.
 * module        - ES6 "module" mode. Script mode is default.
 * ast           - print AST.
 */
    uint8_t                         interactive;     /* 1 bit */
    uint8_t                         trailer;         /* 1 bit */
    uint8_t                         init;            /* 1 bit */
    uint8_t                         disassemble;     /* 1 bit */
    uint8_t                         backtrace;       /* 1 bit */
    uint8_t                         quiet;           /* 1 bit */
    uint8_t                         sandbox;         /* 1 bit */
    uint8_t                         unsafe;          /* 1 bit */
    uint8_t                         module;          /* 1 bit */
    uint8_t                         ast;             /* 1 bit */
#ifdef NJS_DEBUG_OPCODE
    uint8_t                         opcode_debug;    /* 1 bit */
#endif
#ifdef NJS_DEBUG_GENERATOR
    uint8_t                         generator_debug; /* 1 bit */
#endif
} njs_vm_opt_t;


typedef struct {
    njs_function_t      *function;
    njs_opaque_value_t  argument;
    njs_opaque_value_t  value;

    void                *data;

    int64_t             from;
    int64_t             to;
} njs_iterator_args_t;


typedef njs_int_t (*njs_iterator_handler_t)(njs_vm_t *vm,
    njs_iterator_args_t *args, njs_value_t *entry, int64_t n,
    njs_value_t *retval);


NJS_EXPORT void njs_vm_opt_init(njs_vm_opt_t *options);
NJS_EXPORT njs_vm_t *njs_vm_create(njs_vm_opt_t *options);
NJS_EXPORT void njs_vm_destroy(njs_vm_t *vm);
NJS_EXPORT njs_int_t njs_vm_call_exit_hook(njs_vm_t *vm);

/* Input must be null-terminated. */
NJS_EXPORT njs_int_t njs_vm_compile(njs_vm_t *vm, u_char **start, u_char *end);
NJS_EXPORT void njs_vm_set_module_loader(njs_vm_t *vm,
    njs_module_loader_t module_loader, void *opaque);
NJS_EXPORT njs_mod_t *njs_vm_add_module(njs_vm_t *vm, njs_str_t *name,
    njs_value_t *value);
/* Input must be null-terminated. */
NJS_EXPORT njs_mod_t *njs_vm_compile_module(njs_vm_t *vm, njs_str_t *name,
    u_char **start, u_char *end);
NJS_EXPORT njs_int_t njs_vm_reuse(njs_vm_t *vm);
NJS_EXPORT njs_vm_t *njs_vm_clone(njs_vm_t *vm, njs_external_ptr_t external);

NJS_EXPORT njs_int_t njs_vm_enqueue_job(njs_vm_t *vm, njs_function_t *function,
    const njs_value_t *args, njs_uint_t nargs);
/*
 * Executes a single pending job.
 *  1 successful run.
 *  NJS_OK pending job was not found.
 *  NJS_ERROR some exception or internal error happens.
 */
NJS_EXPORT njs_int_t njs_vm_execute_pending_job(njs_vm_t *vm);
NJS_EXPORT njs_int_t njs_vm_pending(njs_vm_t *vm);

NJS_EXPORT void njs_vm_set_rejection_tracker(njs_vm_t *vm,
    njs_rejection_tracker_t rejection_tracker, void *opaque);

/*
 * Runs the specified function with provided arguments.
 *  NJS_OK successful run.
 *  NJS_ERROR some exception or internal error happens.
 *
 *  njs_vm_exception_get(vm) can be used to get the exception value.
 */
NJS_EXPORT njs_int_t njs_vm_call(njs_vm_t *vm, njs_function_t *function,
    const njs_value_t *args, njs_uint_t nargs);
NJS_EXPORT njs_int_t njs_vm_invoke(njs_vm_t *vm, njs_function_t *function,
    const njs_value_t *args, njs_uint_t nargs, njs_value_t *retval);

/*
 * Runs the global code.
 *   NJS_OK successful run.
 *   NJS_ERROR some exception or internal error happens.
 *
 *   njs_vm_exception_get(vm) can be used to get the exception value.
 */
NJS_EXPORT njs_int_t njs_vm_start(njs_vm_t *vm, njs_value_t *retval);

#define NJS_PROTO_ID_ANY    (-1)

NJS_EXPORT njs_int_t njs_vm_external_prototype(njs_vm_t *vm,
    const njs_external_t *definition, njs_uint_t n);
NJS_EXPORT njs_int_t njs_vm_external_constructor(njs_vm_t *vm,
    const njs_str_t *name, njs_function_native_t native,
    const njs_external_t *ctor_props, njs_uint_t ctor_nprops,
    const njs_external_t *proto_props, njs_uint_t proto_nprops);
NJS_EXPORT njs_int_t njs_vm_external_create(njs_vm_t *vm, njs_value_t *value,
    njs_int_t proto_id, njs_external_ptr_t external, njs_bool_t shared);
NJS_EXPORT njs_external_ptr_t njs_vm_external(njs_vm_t *vm,
    njs_int_t proto_id, const njs_value_t *value);
NJS_EXPORT njs_int_t njs_external_property(njs_vm_t *vm,
    njs_object_prop_t *prop, uint32_t unused, njs_value_t *value,
    njs_value_t *setval, njs_value_t *retval);
NJS_EXPORT njs_int_t njs_atom_atomize_key(njs_vm_t *vm, njs_value_t *value);
NJS_EXPORT njs_int_t njs_value_property(njs_vm_t *vm, njs_value_t *value,
    uint32_t atom_id, njs_value_t *retval);
NJS_EXPORT njs_int_t njs_value_property_set(njs_vm_t *vm, njs_value_t *value,
    uint32_t atom_id, njs_value_t *setval);
NJS_EXPORT uintptr_t njs_vm_meta(njs_vm_t *vm, njs_uint_t index);
NJS_EXPORT njs_vm_opt_t *njs_vm_options(njs_vm_t *vm);

NJS_EXPORT njs_int_t njs_error_constructor(njs_vm_t *vm, njs_value_t *args,
    njs_uint_t nargs, njs_index_t type, njs_value_t *retval);
NJS_EXPORT njs_int_t njs_object_prototype_create_constructor(njs_vm_t *vm,
    njs_object_prop_t *prop, uint32_t unused, njs_value_t *value,
    njs_value_t *setval, njs_value_t *retval);
NJS_EXPORT njs_int_t njs_object_prototype_create(njs_vm_t *vm,
    njs_object_prop_t *prop, uint32_t unused, njs_value_t *value,
    njs_value_t *setval, njs_value_t *retval);

NJS_EXPORT njs_function_t *njs_vm_function_alloc(njs_vm_t *vm,
    njs_function_native_t native, njs_bool_t shared, njs_bool_t ctor);

NJS_EXPORT void njs_disassembler(njs_vm_t *vm);

NJS_EXPORT njs_int_t njs_vm_bind(njs_vm_t *vm, const njs_str_t *var_name,
    const njs_value_t *value, njs_bool_t shared);
njs_int_t njs_vm_bind_handler(njs_vm_t *vm, const njs_str_t *var_name,
    njs_prop_handler_t handler, uint16_t magic16, uint32_t magic32,
    njs_bool_t shared);
NJS_EXPORT njs_int_t njs_vm_global(njs_vm_t *vm, njs_value_t *retval);
NJS_EXPORT njs_int_t njs_vm_value(njs_vm_t *vm, const njs_str_t *path,
    njs_value_t *retval);
NJS_EXPORT njs_function_t *njs_vm_function(njs_vm_t *vm, const njs_str_t *name);
NJS_EXPORT njs_bool_t njs_vm_constructor(njs_vm_t *vm);
NJS_EXPORT njs_int_t njs_vm_prototype(njs_vm_t *vm, njs_value_t *value,
    njs_value_t *retval);

NJS_EXPORT void njs_vm_throw(njs_vm_t *vm, const njs_value_t *value);
NJS_EXPORT void njs_vm_error2(njs_vm_t *vm, unsigned error_type,
    const char *fmt, ...);
NJS_EXPORT void njs_vm_error3(njs_vm_t *vm, unsigned type, const char *fmt,
    ...);
NJS_EXPORT void njs_vm_exception_get(njs_vm_t *vm, njs_value_t *retval);
NJS_EXPORT njs_mp_t *njs_vm_memory_pool(njs_vm_t *vm);
NJS_EXPORT njs_external_ptr_t njs_vm_external_ptr(njs_vm_t *vm);

NJS_EXPORT njs_int_t njs_value_to_integer(njs_vm_t *vm, njs_value_t *value,
    int64_t *dst);

/*  Gets string value, no copy. */
NJS_EXPORT void njs_value_string_get(njs_vm_t *vm, njs_value_t *value,
    njs_str_t *dst);
NJS_EXPORT njs_int_t njs_vm_value_string_create(njs_vm_t *vm,
    njs_value_t *value, const u_char *start, uint32_t size);
NJS_EXPORT njs_int_t njs_vm_value_string_create_chb(njs_vm_t *vm,
    njs_value_t *value, njs_chb_t *chain);
NJS_EXPORT njs_int_t njs_vm_string_compare(njs_vm_t *vm, const njs_value_t *v1,
    const njs_value_t *v2);

NJS_EXPORT njs_int_t njs_vm_value_array_buffer_set2(njs_vm_t *vm,
    njs_value_t *value, u_char *start, uint32_t size, njs_bool_t shared);
#define njs_vm_value_array_buffer_set(vm, value, start, size)               \
    njs_vm_value_array_buffer_set2(vm, value, start, size, 0)

NJS_EXPORT njs_int_t njs_value_buffer_get(njs_vm_t *vm, njs_value_t *value,
    njs_str_t *dst);
/*
 * Sets a Buffer value.
 *   start data is not copied and should not be freed.
 */
NJS_EXPORT njs_int_t njs_vm_value_buffer_set(njs_vm_t *vm, njs_value_t *value,
    const u_char *start, uint32_t size);

NJS_EXPORT njs_int_t njs_value_to_string(njs_vm_t *vm, njs_value_t *dst,
    njs_value_t *value);
/*
 * Converts a value to bytes.
 */
NJS_EXPORT njs_int_t njs_vm_value_to_bytes(njs_vm_t *vm, njs_str_t *dst,
    njs_value_t *src);

/*
 * Converts a value to string.
 */
NJS_EXPORT njs_int_t njs_vm_value_to_string(njs_vm_t *vm, njs_str_t *dst,
    njs_value_t *src);

/*
 * Calls njs_vm_value_to_string(), if exception was thrown adds backtrace.
 */
NJS_EXPORT njs_int_t njs_vm_value_string(njs_vm_t *vm, njs_str_t *dst,
    njs_value_t *src);
/*
 * If string value is null-terminated the corresponding C string
 * is returned as is, otherwise the new copy is allocated with
 * the terminating zero byte.
 */
NJS_EXPORT const char *njs_vm_value_to_c_string(njs_vm_t *vm,
    njs_value_t *value);
NJS_EXPORT njs_int_t njs_vm_exception_string(njs_vm_t *vm, njs_str_t *dst);

NJS_EXPORT njs_int_t njs_vm_value_dump(njs_vm_t *vm, njs_str_t *dst,
    njs_value_t *value, njs_uint_t console, njs_uint_t indent);

NJS_EXPORT void njs_vm_memory_error(njs_vm_t *vm);

NJS_EXPORT void njs_value_undefined_set(njs_value_t *value);
NJS_EXPORT void njs_value_null_set(njs_value_t *value);
NJS_EXPORT void njs_value_invalid_set(njs_value_t *value);
NJS_EXPORT void njs_value_boolean_set(njs_value_t *value, int yn);
NJS_EXPORT void njs_value_number_set(njs_value_t *value, double num);
NJS_EXPORT void njs_value_function_set(njs_value_t *value,
    njs_function_t *function);
NJS_EXPORT void njs_value_external_set(njs_value_t *value,
    njs_external_ptr_t external);

NJS_EXPORT uint8_t njs_value_bool(const njs_value_t *value);
NJS_EXPORT double njs_value_number(const njs_value_t *value);
NJS_EXPORT njs_function_t *njs_value_function(const njs_value_t *value);
NJS_EXPORT njs_function_native_t njs_value_native_function(
    const njs_value_t *value);
NJS_EXPORT void *njs_value_ptr(const njs_value_t *value);
njs_external_ptr_t njs_value_external(const njs_value_t *value);
NJS_EXPORT njs_int_t njs_value_external_tag(const njs_value_t *value);

NJS_EXPORT uint16_t njs_vm_prop_magic16(njs_object_prop_t *prop);
NJS_EXPORT uint32_t njs_vm_prop_magic32(njs_object_prop_t *prop);
NJS_EXPORT njs_int_t njs_vm_prop_name(njs_vm_t *vm, uint32_t atom_id,
    njs_str_t *dst);

NJS_EXPORT njs_int_t njs_value_is_null(const njs_value_t *value);
NJS_EXPORT njs_int_t njs_value_is_undefined(const njs_value_t *value);
NJS_EXPORT njs_int_t njs_value_is_null_or_undefined(const njs_value_t *value);
NJS_EXPORT njs_int_t njs_value_is_valid(const njs_value_t *value);
NJS_EXPORT njs_int_t njs_value_is_boolean(const njs_value_t *value);
NJS_EXPORT njs_int_t njs_value_is_number(const njs_value_t *value);
NJS_EXPORT njs_int_t njs_value_is_valid_number(const njs_value_t *value);
NJS_EXPORT njs_int_t njs_value_is_string(const njs_value_t *value);
NJS_EXPORT njs_int_t njs_value_is_object(const njs_value_t *value);
NJS_EXPORT njs_int_t njs_value_is_error(const njs_value_t *value);
NJS_EXPORT njs_int_t njs_value_is_external(const njs_value_t *value,
    njs_int_t proto_id);
NJS_EXPORT njs_int_t njs_value_is_array(const njs_value_t *value);
NJS_EXPORT njs_int_t njs_value_is_function(const njs_value_t *value);
NJS_EXPORT njs_int_t njs_value_is_buffer(const njs_value_t *value);
NJS_EXPORT njs_int_t njs_value_is_array_buffer(const njs_value_t *value);
NJS_EXPORT njs_int_t njs_value_is_shared_array_buffer(const njs_value_t *value);
NJS_EXPORT njs_int_t njs_value_is_data_view(const njs_value_t *value);
NJS_EXPORT njs_int_t njs_value_is_promise(const njs_value_t *value);
NJS_EXPORT njs_promise_type_t njs_promise_state(const njs_value_t *value);
NJS_EXPORT njs_value_t *njs_promise_result(const njs_value_t *value);

NJS_EXPORT njs_int_t njs_vm_object_alloc(njs_vm_t *vm, njs_value_t *retval,
    ...);
NJS_EXPORT njs_value_t *njs_vm_object_keys(njs_vm_t *vm, njs_value_t *value,
    njs_value_t *retval);
NJS_EXPORT njs_value_t *njs_vm_value_enumerate(njs_vm_t *vm, njs_value_t *value,
    uint32_t flags, njs_value_t *retval);
NJS_EXPORT njs_value_t *njs_vm_value_own_enumerate(njs_vm_t *vm,
    njs_value_t *value, uint32_t flags, njs_value_t *retval);
NJS_EXPORT njs_value_t *njs_vm_object_prop(njs_vm_t *vm,
    njs_value_t *value, const njs_str_t *key, njs_opaque_value_t *retval);
NJS_EXPORT njs_int_t njs_vm_object_prop_set(njs_vm_t *vm, njs_value_t *value,
    const njs_str_t *prop, njs_opaque_value_t *setval);
NJS_EXPORT njs_int_t njs_vm_object_iterate(njs_vm_t *vm,
    njs_iterator_args_t *args, njs_iterator_handler_t handler,
    njs_value_t *retval);

NJS_EXPORT njs_int_t njs_vm_array_alloc(njs_vm_t *vm, njs_value_t *retval,
    uint32_t spare);
NJS_EXPORT njs_int_t njs_vm_array_length(njs_vm_t *vm, njs_value_t *value,
    int64_t *length);
NJS_EXPORT njs_value_t *njs_vm_array_start(njs_vm_t *vm, njs_value_t *value);
NJS_EXPORT njs_value_t *njs_vm_array_prop(njs_vm_t *vm,
    njs_value_t *value, int64_t index, njs_opaque_value_t *retval);
NJS_EXPORT njs_value_t *njs_vm_array_push(njs_vm_t *vm, njs_value_t *value);
NJS_EXPORT njs_int_t njs_vm_date_alloc(njs_vm_t *vm, njs_value_t *retval,
    double time);

NJS_EXPORT njs_int_t njs_vm_json_parse(njs_vm_t *vm, njs_value_t *args,
    njs_uint_t nargs, njs_value_t *retval);
NJS_EXPORT njs_int_t njs_vm_json_stringify(njs_vm_t *vm, njs_value_t *args,
    njs_uint_t nargs, njs_value_t *retval);

NJS_EXPORT njs_int_t njs_vm_query_string_parse(njs_vm_t *vm, u_char *start,
    u_char *end, njs_value_t *retval);

NJS_EXPORT njs_int_t njs_vm_promise_create(njs_vm_t *vm, njs_value_t *retval,
    njs_value_t *callbacks);


typedef struct {
    void *(*sab_alloc)(void *opaque, size_t size);
    void (*sab_free)(void *opaque, void *ptr);
    void (*sab_dup)(void *opaque, void *ptr);
    void *sab_opaque;
} njs_sab_functions_t;

NJS_EXPORT void njs_vm_set_sab_functions(njs_vm_t *vm,
    const njs_sab_functions_t *functions);


njs_inline njs_int_t
njs_value_property_val(njs_vm_t *vm, njs_value_t *value, njs_value_t *key,
    njs_value_t *retval)
{
    njs_int_t  ret;

    if (njs_value_atom(key) == 0 /* NJS_ATOM_STRING_unknown */) {
        ret = njs_atom_atomize_key(vm, key);
        if (ret != NJS_OK) {
            return ret;
        }
    }

    return njs_value_property(vm, value, njs_value_atom(key), retval);
}


njs_inline njs_int_t
njs_value_property_val_set(njs_vm_t *vm, njs_value_t *value, njs_value_t *key,
    njs_value_t *setval)
{
    njs_int_t  ret;

    if (njs_value_atom(key) == 0 /* NJS_ATOM_STRING_unknown */) {
        ret = njs_atom_atomize_key(vm, key);
        if (ret != NJS_OK) {
            return ret;
        }
    }

    return njs_value_property_set(vm, value, njs_value_atom(key), setval);
}


njs_inline size_t
njs_value_string_length(njs_vm_t *vm, njs_value_t *value)
{
    njs_str_t  str;

    njs_value_string_get(vm, value, &str);

    return str.length;
}


#endif /* _NJS_H_INCLUDED_ */
