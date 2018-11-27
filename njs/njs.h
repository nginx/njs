
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) NGINX, Inc.
 *
 * njs public header.
 */

#ifndef _NJS_H_INCLUDED_
#define _NJS_H_INCLUDED_

#include <nxt_auto_config.h>

#define NJS_VERSION                 "0.2.7"


#include <nxt_types.h>
#include <nxt_clang.h>
#include <nxt_string.h>
#include <nxt_stub.h>
#include <nxt_array.h>
#include <nxt_lvlhsh.h>


typedef intptr_t                    njs_ret_t;
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
    (njs_value_t *) ((u_char *) args + n * 16)


extern const njs_value_t            njs_value_void;

#define njs_arg(args, nargs, n)                                               \
    ((n < nargs) ? njs_argument(args, n) : &njs_value_void)

#define njs_value_assign(dst, src)                                            \
    *((njs_opaque_value_t *) dst) = *((njs_opaque_value_t *) src);

#define njs_value_arg(val) ((njs_value_t *) val)


#define njs_vm_error(vm, fmt, ...)                                            \
    njs_value_error_set(vm, njs_vm_retval(vm), fmt, ##__VA_ARGS__)


typedef njs_ret_t (*njs_extern_get_t)(njs_vm_t *vm, njs_value_t *value,
    void *obj, uintptr_t data);
typedef njs_ret_t (*njs_extern_set_t)(njs_vm_t *vm, void *obj, uintptr_t data,
    nxt_str_t *value);
typedef njs_ret_t (*njs_extern_find_t)(njs_vm_t *vm, void *obj, uintptr_t data,
    nxt_bool_t delete);
typedef njs_ret_t (*njs_extern_foreach_t)(njs_vm_t *vm, void *obj, void *next);
typedef njs_ret_t (*njs_extern_next_t)(njs_vm_t *vm, njs_value_t *value,
    void *obj, void *next);
typedef njs_ret_t (*njs_extern_method_t)(njs_vm_t *vm, njs_value_t *args,
    nxt_uint_t nargs, njs_index_t unused);


typedef struct njs_external_s       njs_external_t;

struct njs_external_s {
    nxt_str_t                       name;

#define NJS_EXTERN_PROPERTY         0x00
#define NJS_EXTERN_METHOD           0x01
#define NJS_EXTERN_OBJECT           0x80
#define NJS_EXTERN_CASELESS_OBJECT  0x81

    uintptr_t                       type;

    njs_external_t                  *properties;
    nxt_uint_t                      nproperties;

    njs_extern_get_t                get;
    njs_extern_set_t                set;
    njs_extern_find_t               find;

    njs_extern_foreach_t            foreach;
    njs_extern_next_t               next;

    njs_extern_method_t             method;

    uintptr_t                       data;
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

typedef njs_host_event_t (*njs_set_timer)(njs_external_ptr_t external,
    uint64_t delay, njs_vm_event_t vm_event);
typedef void (*njs_event_destructor)(njs_external_ptr_t external,
    njs_host_event_t event);


typedef struct {
    njs_set_timer                   set_timer;
    njs_event_destructor            clear_timer;
} njs_vm_ops_t;


typedef struct {
    njs_external_ptr_t              external;
    njs_vm_shared_t                 *shared;
    njs_vm_ops_t                    *ops;

    uint8_t                         trailer;         /* 1 bit */
    uint8_t                         init;            /* 1 bit */
    uint8_t                         accumulative;    /* 1 bit */
    uint8_t                         backtrace;       /* 1 bit */
    uint8_t                         sandbox;         /* 1 bit */
} njs_vm_opt_t;


#define NJS_OK                      NXT_OK
#define NJS_ERROR                   NXT_ERROR
#define NJS_AGAIN                   NXT_AGAIN
#define NJS_DECLINED                NXT_DECLINED
#define NJS_DONE                    NXT_DONE


NXT_EXPORT njs_vm_t *njs_vm_create(njs_vm_opt_t *options);
NXT_EXPORT void njs_vm_destroy(njs_vm_t *vm);

NXT_EXPORT nxt_int_t njs_vm_compile(njs_vm_t *vm, u_char **start, u_char *end);
NXT_EXPORT njs_vm_t *njs_vm_clone(njs_vm_t *vm, njs_external_ptr_t external);
NXT_EXPORT nxt_int_t njs_vm_call(njs_vm_t *vm, njs_function_t *function,
    const njs_value_t *args, nxt_uint_t nargs);

NXT_EXPORT njs_vm_event_t njs_vm_add_event(njs_vm_t *vm,
    njs_function_t *function, nxt_uint_t once, njs_host_event_t host_ev,
    njs_event_destructor destructor);
NXT_EXPORT void njs_vm_del_event(njs_vm_t *vm, njs_vm_event_t vm_event);
NXT_EXPORT nxt_int_t njs_vm_pending(njs_vm_t *vm);
NXT_EXPORT nxt_int_t njs_vm_post_event(njs_vm_t *vm, njs_vm_event_t vm_event,
    const njs_value_t *args, nxt_uint_t nargs);

NXT_EXPORT nxt_int_t njs_vm_run(njs_vm_t *vm);

NXT_EXPORT const njs_extern_t *njs_vm_external_prototype(njs_vm_t *vm,
    njs_external_t *external);
NXT_EXPORT nxt_int_t njs_vm_external_create(njs_vm_t *vm,
    njs_value_t *value, const njs_extern_t *proto, njs_external_ptr_t object);
NXT_EXPORT nxt_int_t njs_vm_external_bind(njs_vm_t *vm,
    const nxt_str_t *var_name, const njs_value_t *value);
NXT_EXPORT njs_external_ptr_t njs_vm_external(njs_vm_t *vm,
    const njs_value_t *value);

NXT_EXPORT void njs_disassembler(njs_vm_t *vm);
NXT_EXPORT nxt_array_t *njs_vm_completions(njs_vm_t *vm, nxt_str_t *expression);

NXT_EXPORT njs_function_t *njs_vm_function(njs_vm_t *vm, nxt_str_t *name);
NXT_EXPORT njs_value_t *njs_vm_retval(njs_vm_t *vm);
NXT_EXPORT void njs_vm_retval_set(njs_vm_t *vm, const njs_value_t *value);

NXT_EXPORT u_char * njs_string_alloc(njs_vm_t *vm, njs_value_t *value,
    uint32_t size, uint32_t length);
NXT_EXPORT njs_ret_t njs_string_create(njs_vm_t *vm, njs_value_t *value,
    u_char *start, uint32_t size, uint32_t length);

NXT_EXPORT nxt_int_t njs_value_string_copy(njs_vm_t *vm, nxt_str_t *retval,
    const njs_value_t *value, uintptr_t *next);

NXT_EXPORT njs_ret_t njs_vm_value_to_ext_string(njs_vm_t *vm, nxt_str_t *dst,
    const njs_value_t *src, nxt_uint_t handle_exception);
NXT_EXPORT njs_ret_t njs_vm_retval_to_ext_string(njs_vm_t *vm, nxt_str_t *dst);

NXT_EXPORT njs_ret_t njs_vm_value_dump(njs_vm_t *vm, nxt_str_t *dst,
    const njs_value_t *value, nxt_uint_t indent);
NXT_EXPORT njs_ret_t njs_vm_retval_dump(njs_vm_t *vm, nxt_str_t *dst,
    nxt_uint_t indent);

NXT_EXPORT void njs_vm_memory_error(njs_vm_t *vm);

NXT_EXPORT void njs_value_void_set(njs_value_t *value);
NXT_EXPORT void njs_value_boolean_set(njs_value_t *value, int yn);
NXT_EXPORT void njs_value_number_set(njs_value_t *value, double num);
NXT_EXPORT void njs_value_data_set(njs_value_t *value, void *data);
NXT_EXPORT void njs_value_error_set(njs_vm_t *vm, njs_value_t *value,
    const char *fmt, ...);

NXT_EXPORT uint8_t njs_value_bool(const njs_value_t *value);
NXT_EXPORT double njs_value_number(const njs_value_t *value);
NXT_EXPORT void *njs_value_data(const njs_value_t *value);
NXT_EXPORT njs_function_t *njs_value_function(const njs_value_t *value);

NXT_EXPORT nxt_int_t njs_value_is_null(const njs_value_t *value);
NXT_EXPORT nxt_int_t njs_value_is_void(const njs_value_t *value);
NXT_EXPORT nxt_int_t njs_value_is_boolean(const njs_value_t *value);
NXT_EXPORT nxt_int_t njs_value_is_number(const njs_value_t *value);
NXT_EXPORT nxt_int_t njs_value_is_valid_number(const njs_value_t *value);
NXT_EXPORT nxt_int_t njs_value_is_string(const njs_value_t *value);
NXT_EXPORT nxt_int_t njs_value_is_object(const njs_value_t *value);
NXT_EXPORT nxt_int_t njs_value_is_function(const njs_value_t *value);

NXT_EXPORT njs_ret_t njs_vm_object_alloc(njs_vm_t *vm, njs_value_t *retval,
    ...);
NXT_EXPORT njs_value_t *njs_vm_object_prop(njs_vm_t *vm,
    const njs_value_t *value, const nxt_str_t *key);

extern const nxt_mem_proto_t  njs_vm_mem_cache_pool_proto;

#endif /* _NJS_H_INCLUDED_ */
