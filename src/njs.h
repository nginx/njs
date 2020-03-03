
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) NGINX, Inc.
 *
 * njs public header.
 */

#ifndef _NJS_H_INCLUDED_
#define _NJS_H_INCLUDED_

#include <njs_auto_config.h>

#define NJS_VERSION                 "0.3.10"


#include <unistd.h>                 /* STDOUT_FILENO, STDERR_FILENO */
#include <njs_types.h>
#include <njs_clang.h>
#include <njs_str.h>
#include <njs_lvlhsh.h>
#include <njs_sprintf.h>


typedef uintptr_t                   njs_index_t;
typedef struct njs_vm_s             njs_vm_t;
typedef union  njs_value_s          njs_value_t;
typedef struct njs_extern_s         njs_extern_t;
typedef struct njs_function_s       njs_function_t;
typedef struct njs_vm_shared_s      njs_vm_shared_t;

/*
 * njs_opaque_value_t is the external storage type for native njs_value_t type.
 * sizeof(njs_opaque_value_t) == sizeof(njs_value_t).
 */

typedef struct {
    uint64_t                        filler[2];
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

#define njs_lvalue_arg(lvalue, args, nargs, n)                                \
    ((n < nargs) ? njs_argument(args, n)                                      \
                 : (njs_value_assign(lvalue, &njs_value_undefined), lvalue))

#define njs_vm_error(vm, fmt, ...)                                            \
    njs_vm_value_error_set(vm, njs_vm_retval(vm), fmt, ##__VA_ARGS__)


typedef njs_int_t (*njs_extern_get_t)(njs_vm_t *vm, njs_value_t *value,
    void *obj, uintptr_t data);
typedef njs_int_t (*njs_extern_set_t)(njs_vm_t *vm, void *obj, uintptr_t data,
    njs_str_t *value);
typedef njs_int_t (*njs_extern_find_t)(njs_vm_t *vm, void *obj, uintptr_t data,
    njs_bool_t delete);
typedef njs_int_t (*njs_extern_keys_t)(njs_vm_t *vm, void *obj,
    njs_value_t *keys);
typedef njs_int_t (*njs_extern_method_t)(njs_vm_t *vm, njs_value_t *args,
    njs_uint_t nargs, njs_index_t unused);


typedef struct njs_external_s       njs_external_t;

struct njs_external_s {
    njs_str_t                       name;

#define NJS_EXTERN_PROPERTY         0x00
#define NJS_EXTERN_METHOD           0x01
#define NJS_EXTERN_OBJECT           0x80
#define NJS_EXTERN_CASELESS_OBJECT  0x81

    uintptr_t                       type;

    njs_external_t                  *properties;
    njs_uint_t                      nproperties;

    njs_extern_get_t                get;
    njs_extern_set_t                set;
    njs_extern_find_t               find;

    njs_extern_keys_t               keys;

    njs_extern_method_t             method;

    uintptr_t                       data;
};


typedef njs_int_t (*njs_function_native_t) (njs_vm_t *vm, njs_value_t *args,
    njs_uint_t nargs, njs_index_t retval);

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


typedef struct {
    njs_set_timer_t                 set_timer;
    njs_event_destructor_t          clear_timer;
} njs_vm_ops_t;


typedef struct {
    njs_external_ptr_t              external;
    njs_vm_shared_t                 *shared;
    njs_vm_ops_t                    *ops;
    njs_str_t                       file;

    char                            **argv;
    njs_uint_t                      argc;

/*
 * accumulative - enables "accumulative" mode to support incremental compiling.
 *  (REPL). Allows starting parent VM without cloning.
 * disassemble  - enables disassemble.
 * backtrace    - enables backtraces.
 * quiet        - removes filenames from backtraces. To produce comparable
    test262 diffs.
 * sandbox      - "sandbox" mode. Disables file access.
 * unsafe       - enables unsafe language features:
 *   - Function constructors.
 * module       - ES6 "module" mode. Script mode is default.
 */

    uint8_t                         trailer;         /* 1 bit */
    uint8_t                         init;            /* 1 bit */
    uint8_t                         accumulative;    /* 1 bit */
    uint8_t                         disassemble;     /* 1 bit */
    uint8_t                         backtrace;       /* 1 bit */
    uint8_t                         quiet;           /* 1 bit */
    uint8_t                         sandbox;         /* 1 bit */
    uint8_t                         unsafe;          /* 1 bit */
    uint8_t                         module;          /* 1 bit */
} njs_vm_opt_t;


NJS_EXPORT njs_vm_t *njs_vm_create(njs_vm_opt_t *options);
NJS_EXPORT void njs_vm_destroy(njs_vm_t *vm);

NJS_EXPORT njs_int_t njs_vm_compile(njs_vm_t *vm, u_char **start, u_char *end);
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
    const njs_value_t *args, njs_uint_t nargs, njs_index_t retval);

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

NJS_EXPORT const njs_extern_t *njs_vm_external_prototype(njs_vm_t *vm,
    njs_external_t *external);
NJS_EXPORT njs_int_t njs_vm_external_create(njs_vm_t *vm,
    njs_value_t *value, const njs_extern_t *proto, njs_external_ptr_t object);
NJS_EXPORT njs_external_ptr_t njs_vm_external(njs_vm_t *vm,
    const njs_value_t *value);

NJS_EXPORT njs_function_t *njs_vm_function_alloc(njs_vm_t *vm,
    njs_function_native_t native);

NJS_EXPORT void njs_disassembler(njs_vm_t *vm);
NJS_EXPORT void njs_disassemble(u_char *start, u_char *end);

NJS_EXPORT njs_int_t njs_vm_bind(njs_vm_t *vm, const njs_str_t *var_name,
    const njs_value_t *value, njs_bool_t shared);
NJS_EXPORT const njs_value_t *njs_vm_value(njs_vm_t *vm, const njs_str_t *name);
NJS_EXPORT njs_function_t *njs_vm_function(njs_vm_t *vm, const njs_str_t *name);

NJS_EXPORT njs_value_t *njs_vm_retval(njs_vm_t *vm);
NJS_EXPORT void njs_vm_retval_set(njs_vm_t *vm, const njs_value_t *value);

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

NJS_EXPORT void njs_value_undefined_set(njs_value_t *value);
NJS_EXPORT void njs_value_boolean_set(njs_value_t *value, int yn);
NJS_EXPORT void njs_value_number_set(njs_value_t *value, double num);
NJS_EXPORT void njs_value_data_set(njs_value_t *value, void *data);

NJS_EXPORT uint8_t njs_value_bool(const njs_value_t *value);
NJS_EXPORT double njs_value_number(const njs_value_t *value);
NJS_EXPORT void *njs_value_data(const njs_value_t *value);
NJS_EXPORT njs_function_t *njs_value_function(const njs_value_t *value);

NJS_EXPORT njs_int_t njs_value_is_null(const njs_value_t *value);
NJS_EXPORT njs_int_t njs_value_is_undefined(const njs_value_t *value);
NJS_EXPORT njs_int_t njs_value_is_null_or_undefined(const njs_value_t *value);
NJS_EXPORT njs_int_t njs_value_is_boolean(const njs_value_t *value);
NJS_EXPORT njs_int_t njs_value_is_number(const njs_value_t *value);
NJS_EXPORT njs_int_t njs_value_is_valid_number(const njs_value_t *value);
NJS_EXPORT njs_int_t njs_value_is_string(const njs_value_t *value);
NJS_EXPORT njs_int_t njs_value_is_object(const njs_value_t *value);
NJS_EXPORT njs_int_t njs_value_is_function(const njs_value_t *value);

NJS_EXPORT njs_int_t njs_vm_object_alloc(njs_vm_t *vm, njs_value_t *retval,
    ...);
NJS_EXPORT njs_value_t *njs_vm_object_prop(njs_vm_t *vm,
    const njs_value_t *value, const njs_str_t *key);

NJS_EXPORT njs_int_t njs_vm_array_alloc(njs_vm_t *vm, njs_value_t *retval,
    uint32_t spare);
NJS_EXPORT njs_value_t *njs_vm_array_push(njs_vm_t *vm, njs_value_t *value);

NJS_EXPORT njs_int_t njs_vm_json_parse(njs_vm_t *vm, njs_value_t *args,
    njs_uint_t nargs);
NJS_EXPORT njs_int_t njs_vm_json_stringify(njs_vm_t *vm, njs_value_t *args,
    njs_uint_t nargs);

NJS_EXPORT njs_int_t njs_vm_promise_create(njs_vm_t *vm, njs_value_t *retval,
    njs_value_t *callbacks);


#endif /* _NJS_H_INCLUDED_ */
