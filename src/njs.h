
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) NGINX, Inc.
 *
 * njs public header.
 */

#ifndef _NJS_H_INCLUDED_
#define _NJS_H_INCLUDED_

#include <njs_auto_config.h>

#define NJS_VERSION                 "0.7.8"
#define NJS_VERSION_NUMBER          0x000708


#include <unistd.h>                 /* STDOUT_FILENO, STDERR_FILENO */
#include <njs_types.h>
#include <njs_clang.h>
#include <njs_str.h>
#include <njs_unicode.h>
#include <njs_utf8.h>
#include <njs_mp.h>
#include <njs_chb.h>
#include <njs_lvlhsh.h>
#include <njs_sprintf.h>


typedef uintptr_t                   njs_index_t;
typedef struct njs_vm_s             njs_vm_t;
typedef struct njs_mod_s            njs_mod_t;
typedef union  njs_value_s          njs_value_t;
typedef struct njs_function_s       njs_function_t;
typedef struct njs_vm_shared_s      njs_vm_shared_t;
typedef struct njs_object_prop_s    njs_object_prop_t;
typedef struct njs_external_s       njs_external_t;

/*
 * njs_opaque_value_t is the external storage type for native njs_value_t type.
 * sizeof(njs_opaque_value_t) == sizeof(njs_value_t).
 */

typedef struct {
    uint64_t                        filler[2];
} njs_opaque_value_t;

typedef enum {
    NJS_LOG_LEVEL_ERROR = 4,
    NJS_LOG_LEVEL_WARN = 5,
    NJS_LOG_LEVEL_INFO = 7,
} njs_log_level_t;

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

#define njs_lvalue_arg(lvalue, args, nargs, n)                                \
    ((n < nargs) ? njs_argument(args, n)                                      \
                 : (njs_value_assign(lvalue, &njs_value_undefined), lvalue))

#define njs_vm_error(vm, fmt, ...)                                            \
    njs_vm_value_error_set(vm, njs_vm_retval(vm), fmt, ##__VA_ARGS__)

#define njs_vm_log(vm, fmt, ...)  njs_vm_logger(vm, NJS_LOG_LEVEL_INFO, fmt,  \
                                                ##__VA_ARGS__)
#define njs_vm_warn(vm, fmt, ...)  njs_vm_logger(vm, NJS_LOG_LEVEL_WARN, fmt, \
                                                ##__VA_ARGS__)
#define njs_vm_err(vm, fmt, ...)  njs_vm_logger(vm, NJS_LOG_LEVEL_ERROR, fmt, \
                                                ##__VA_ARGS__)

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
 *   NJS_ERROR - some error, vm->retval contains appropriate exception.
 */
typedef njs_int_t (*njs_prop_handler_t) (njs_vm_t *vm, njs_object_prop_t *prop,
    njs_value_t *value, njs_value_t *setval, njs_value_t *retval);
typedef njs_int_t (*njs_exotic_keys_t)(njs_vm_t *vm, njs_value_t *value,
    njs_value_t *retval);
typedef njs_int_t (*njs_function_native_t) (njs_vm_t *vm, njs_value_t *args,
    njs_uint_t nargs, njs_index_t magic8);


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
    NJS_EXTERN_PROPERTY = 0,
    NJS_EXTERN_METHOD = 1,
    NJS_EXTERN_OBJECT = 2,
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
            const char              value[15]; /* NJS_STRING_SHORT + 1. */
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


/*
 * NJS and event loops.
 *
 * njs_vm_ops_t callbacks are used to interact with the event loop environment.
 *
 * Functions get an external object as the first argument. The external
 * object is provided as the third argument to njs_vm_clone().
 *
 * The callbacks are expected to return to the VM the unique id of an
 * underlying event.  This id will be passed as the second argument to
 * njs_event_destructor() at the moment the VM wants to destroy it.
 *
 * When an underlying events fires njs_vm_post_event() should be invoked with
 * the value provided as vm_event.
 *
 * The events posted by njs_vm_post_event() are processed as soon as
 * njs_vm_run() is invoked. njs_vm_run() returns NJS_AGAIN until pending events
 * are present.
 */

typedef void *                      njs_vm_event_t;
typedef void *                      njs_host_event_t;
typedef void *                      njs_external_ptr_t;

typedef njs_host_event_t (*njs_set_timer_t)(njs_external_ptr_t external,
    uint64_t delay, njs_vm_event_t vm_event);
typedef void (*njs_event_destructor_t)(njs_external_ptr_t external,
    njs_host_event_t event);
typedef njs_mod_t *(*njs_module_loader_t)(njs_vm_t *vm,
    njs_external_ptr_t external, njs_str_t *name);
typedef void (*njs_logger_t)(njs_vm_t *vm, njs_external_ptr_t external,
    njs_log_level_t level, const u_char *start, size_t length);


typedef struct {
    njs_set_timer_t                 set_timer;
    njs_event_destructor_t          clear_timer;
    njs_module_loader_t             module_loader;
    njs_logger_t                    logger;
} njs_vm_ops_t;


typedef struct {
    size_t                          size;
    uintptr_t                       *values;
} njs_vm_meta_t;


typedef njs_int_t (*njs_addon_init_pt)(njs_vm_t *vm);

typedef struct {
    njs_str_t                       name;
    njs_addon_init_pt               init;
} njs_module_t;


typedef struct {
    njs_external_ptr_t              external;
    njs_vm_shared_t                 *shared;
    njs_vm_ops_t                    *ops;
    njs_vm_meta_t                   *metas;
    njs_module_t                    **addons;
    njs_str_t                       file;

    char                            **argv;
    njs_uint_t                      argc;

    njs_log_level_t                 log_level;

#define NJS_VM_OPT_UNHANDLED_REJECTION_IGNORE   0
#define NJS_VM_OPT_UNHANDLED_REJECTION_THROW    1

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
 * unhandled_rejection IGNORE | THROW - tracks unhandled promise rejections:
 *   - throwing inside a Promise without a catch block.
 *   - throwing inside in a finally or catch block.
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
    uint8_t                         unhandled_rejection;
} njs_vm_opt_t;


NJS_EXPORT void njs_vm_opt_init(njs_vm_opt_t *options);
NJS_EXPORT njs_vm_t *njs_vm_create(njs_vm_opt_t *options);
NJS_EXPORT void njs_vm_destroy(njs_vm_t *vm);

NJS_EXPORT njs_int_t njs_vm_compile(njs_vm_t *vm, u_char **start, u_char *end);
NJS_EXPORT njs_mod_t *njs_vm_compile_module(njs_vm_t *vm, njs_str_t *name,
    u_char **start, u_char *end);
NJS_EXPORT njs_vm_t *njs_vm_clone(njs_vm_t *vm, njs_external_ptr_t external);

NJS_EXPORT njs_vm_event_t njs_vm_add_event(njs_vm_t *vm,
    njs_function_t *function, njs_uint_t once, njs_host_event_t host_ev,
    njs_event_destructor_t destructor);
NJS_EXPORT void njs_vm_del_event(njs_vm_t *vm, njs_vm_event_t vm_event);
NJS_EXPORT njs_int_t njs_vm_post_event(njs_vm_t *vm, njs_vm_event_t vm_event,
    const njs_value_t *args, njs_uint_t nargs);

/*
 * Returns 1 if async events are present.
 */
NJS_EXPORT njs_int_t njs_vm_waiting(njs_vm_t *vm);

/*
 * Returns 1 if posted events are ready to be executed.
 */
NJS_EXPORT njs_int_t njs_vm_posted(njs_vm_t *vm);

#define njs_vm_pending(vm)  (njs_vm_waiting(vm) || njs_vm_posted(vm))

#define njs_vm_unhandled_rejection(vm)                                         \
    ((vm)->options.unhandled_rejection == NJS_VM_OPT_UNHANDLED_REJECTION_THROW \
    && (vm)->promise_reason != NULL && (vm)->promise_reason->length != 0)

/*
 * Runs the specified function with provided arguments.
 *  NJS_OK successful run.
 *  NJS_ERROR some exception or internal error happens.
 *
 *  njs_vm_retval(vm) can be used to get the retval or exception value.
 */
NJS_EXPORT njs_int_t njs_vm_call(njs_vm_t *vm, njs_function_t *function,
    const njs_value_t *args, njs_uint_t nargs);
NJS_EXPORT njs_int_t njs_vm_invoke(njs_vm_t *vm, njs_function_t *function,
    const njs_value_t *args, njs_uint_t nargs, njs_value_t *retval);

/*
 * Runs posted events.
 *  NJS_OK successfully processed all posted events, no more events.
 *  NJS_AGAIN successfully processed all events, some posted events are
 *    still pending.
 *  NJS_ERROR some exception or internal error happens.
 *    njs_vm_retval(vm) can be used to get the retval or exception value.
 */
NJS_EXPORT njs_int_t njs_vm_run(njs_vm_t *vm);

/*
 * Runs the global code.
 *   NJS_OK successful run.
 *   NJS_ERROR some exception or internal error happens.
 *
 *   njs_vm_retval(vm) can be used to get the retval or exception value.
 */
NJS_EXPORT njs_int_t njs_vm_start(njs_vm_t *vm);

NJS_EXPORT njs_int_t njs_vm_add_path(njs_vm_t *vm, const njs_str_t *path);

#define NJS_PROTO_ID_ANY    (-1)

NJS_EXPORT njs_int_t njs_vm_external_prototype(njs_vm_t *vm,
    const njs_external_t *definition, njs_uint_t n);
NJS_EXPORT njs_int_t njs_vm_external_create(njs_vm_t *vm, njs_value_t *value,
    njs_int_t proto_id, njs_external_ptr_t external, njs_bool_t shared);
NJS_EXPORT njs_external_ptr_t njs_vm_external(njs_vm_t *vm,
    njs_int_t proto_id, const njs_value_t *value);
NJS_EXPORT njs_int_t njs_external_property(njs_vm_t *vm,
    njs_object_prop_t *prop, njs_value_t *value, njs_value_t *setval,
    njs_value_t *retval);
NJS_EXPORT uintptr_t njs_vm_meta(njs_vm_t *vm, njs_uint_t index);

NJS_EXPORT njs_function_t *njs_vm_function_alloc(njs_vm_t *vm,
    njs_function_native_t native);

NJS_EXPORT void njs_disassembler(njs_vm_t *vm);

NJS_EXPORT njs_int_t njs_vm_bind(njs_vm_t *vm, const njs_str_t *var_name,
    const njs_value_t *value, njs_bool_t shared);
NJS_EXPORT njs_int_t njs_vm_value(njs_vm_t *vm, const njs_str_t *path,
    njs_value_t *retval);
NJS_EXPORT njs_function_t *njs_vm_function(njs_vm_t *vm, const njs_str_t *name);

NJS_EXPORT njs_value_t *njs_vm_retval(njs_vm_t *vm);
NJS_EXPORT void njs_vm_retval_set(njs_vm_t *vm, const njs_value_t *value);
NJS_EXPORT njs_mp_t *njs_vm_memory_pool(njs_vm_t *vm);

/*  Gets string value, no copy. */
NJS_EXPORT void njs_value_string_get(njs_value_t *value, njs_str_t *dst);
/*
 * Sets a byte string value.
 *   start data is not copied and should not be freed.
 */
NJS_EXPORT njs_int_t njs_vm_value_string_set(njs_vm_t *vm, njs_value_t *value,
    const u_char *start, uint32_t size);
NJS_EXPORT u_char *njs_vm_value_string_alloc(njs_vm_t *vm, njs_value_t *value,
    uint32_t size);
NJS_EXPORT njs_int_t njs_vm_value_string_copy(njs_vm_t *vm, njs_str_t *retval,
    njs_value_t *value, uintptr_t *next);

NJS_EXPORT njs_int_t njs_vm_value_array_buffer_set(njs_vm_t *vm,
    njs_value_t *value, const u_char *start, uint32_t size);

/*
 * Sets a Buffer value.
 *   start data is not copied and should not be freed.
 */
NJS_EXPORT njs_int_t njs_vm_value_buffer_set(njs_vm_t *vm, njs_value_t *value,
    const u_char *start, uint32_t size);

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
NJS_EXPORT njs_int_t njs_vm_retval_string(njs_vm_t *vm, njs_str_t *dst);

NJS_EXPORT njs_int_t njs_vm_value_dump(njs_vm_t *vm, njs_str_t *dst,
    njs_value_t *value, njs_uint_t console, njs_uint_t indent);
NJS_EXPORT njs_int_t njs_vm_retval_dump(njs_vm_t *vm, njs_str_t *dst,
    njs_uint_t indent);

NJS_EXPORT void njs_vm_value_error_set(njs_vm_t *vm, njs_value_t *value,
    const char *fmt, ...);
NJS_EXPORT void njs_vm_memory_error(njs_vm_t *vm);

NJS_EXPORT void njs_vm_logger(njs_vm_t *vm, njs_log_level_t level,
    const char *fmt, ...);

NJS_EXPORT void njs_value_undefined_set(njs_value_t *value);
NJS_EXPORT void njs_value_null_set(njs_value_t *value);
NJS_EXPORT void njs_value_invalid_set(njs_value_t *value);
NJS_EXPORT void njs_value_boolean_set(njs_value_t *value, int yn);
NJS_EXPORT void njs_value_number_set(njs_value_t *value, double num);

NJS_EXPORT uint8_t njs_value_bool(const njs_value_t *value);
NJS_EXPORT double njs_value_number(const njs_value_t *value);
NJS_EXPORT njs_function_t *njs_value_function(const njs_value_t *value);
NJS_EXPORT njs_int_t njs_value_external_tag(const njs_value_t *value);

NJS_EXPORT uint16_t njs_vm_prop_magic16(njs_object_prop_t *prop);
NJS_EXPORT uint32_t njs_vm_prop_magic32(njs_object_prop_t *prop);
NJS_EXPORT njs_int_t njs_vm_prop_name(njs_vm_t *vm, njs_object_prop_t *prop,
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
NJS_EXPORT njs_int_t njs_value_is_array(const njs_value_t *value);
NJS_EXPORT njs_int_t njs_value_is_function(const njs_value_t *value);
NJS_EXPORT njs_int_t njs_value_is_buffer(const njs_value_t *value);

NJS_EXPORT njs_int_t njs_vm_object_alloc(njs_vm_t *vm, njs_value_t *retval,
    ...);
NJS_EXPORT njs_value_t *njs_vm_object_keys(njs_vm_t *vm, njs_value_t *value,
    njs_value_t *retval);
NJS_EXPORT njs_value_t *njs_vm_object_prop(njs_vm_t *vm,
    njs_value_t *value, const njs_str_t *key, njs_opaque_value_t *retval);

NJS_EXPORT njs_int_t njs_vm_array_alloc(njs_vm_t *vm, njs_value_t *retval,
    uint32_t spare);
NJS_EXPORT njs_int_t njs_vm_array_length(njs_vm_t *vm, njs_value_t *value,
    int64_t *length);
NJS_EXPORT njs_value_t *njs_vm_array_start(njs_vm_t *vm, njs_value_t *value);
NJS_EXPORT njs_value_t *njs_vm_array_prop(njs_vm_t *vm,
    njs_value_t *value, int64_t index, njs_opaque_value_t *retval);
NJS_EXPORT njs_value_t *njs_vm_array_push(njs_vm_t *vm, njs_value_t *value);

NJS_EXPORT njs_int_t njs_vm_json_parse(njs_vm_t *vm, njs_value_t *args,
    njs_uint_t nargs);
NJS_EXPORT njs_int_t njs_vm_json_stringify(njs_vm_t *vm, njs_value_t *args,
    njs_uint_t nargs);

NJS_EXPORT njs_int_t njs_vm_query_string_parse(njs_vm_t *vm, u_char *start,
    u_char *end, njs_value_t *retval);

NJS_EXPORT njs_int_t njs_vm_promise_create(njs_vm_t *vm, njs_value_t *retval,
    njs_value_t *callbacks);


#endif /* _NJS_H_INCLUDED_ */
