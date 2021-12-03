
/*
 * Copyright (C) Dmitry Volyntsev
 * Copyright (C) NGINX, Inc.
 */

#ifndef _NJS_EXTERNALS_TEST_H_INCLUDED_
#define _NJS_EXTERNALS_TEST_H_INCLUDED_


typedef struct {
    njs_value_t             retval;
    njs_queue_t             events;  /* of njs_external_ev_t */
} njs_external_env_t;


typedef struct {
    njs_vm_event_t          vm_event;
    void                    *data;
    njs_uint_t              nargs;
    njs_value_t             args[3];
    njs_value_t             callbacks[2];
    njs_queue_link_t        link;
} njs_external_ev_t;


njs_int_t njs_externals_shared_init(njs_vm_t *vm);
njs_int_t njs_externals_init(njs_vm_t *vm);
njs_int_t njs_external_env_init(njs_external_env_t *env);
njs_int_t njs_external_call(njs_vm_t *vm, const njs_str_t *fname,
    njs_value_t *args, njs_uint_t nargs);
njs_int_t njs_external_process_events(njs_vm_t *vm, njs_external_env_t *env);


#endif /* _NJS_EXTERNALS_TEST_H_INCLUDED_ */
