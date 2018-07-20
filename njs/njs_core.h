
/*
 * Copyright (C) Dmitry Volyntsev
 * Copyright (C) NGINX, Inc.
 */

#ifndef _NJS_CORE_H_INCLUDED_
#define _NJS_CORE_H_INCLUDED_

#include <nxt_auto_config.h>

#include <nxt_unix.h>
#include <nxt_types.h>
#include <nxt_clang.h>
#include <nxt_alignment.h>
#include <nxt_string.h>
#include <nxt_stub.h>
#include <nxt_utf8.h>
#include <nxt_dtoa.h>
#include <nxt_strtod.h>
#include <nxt_djb_hash.h>
#include <nxt_trace.h>
#include <nxt_array.h>
#include <nxt_queue.h>
#include <nxt_lvlhsh.h>
#include <nxt_random.h>
#include <nxt_time.h>
#include <nxt_malloc.h>
#include <nxt_mem_cache_pool.h>

#include <njs.h>
#include <njs_vm.h>
#include <njs_variable.h>
#include <njs_parser.h>
#include <njs_function.h>
#include <njs_boolean.h>
#include <njs_number.h>
#include <njs_string.h>
#include <njs_object.h>
#include <njs_object_hash.h>
#include <njs_array.h>
#include <njs_error.h>

#include <njs_event.h>

#include <njs_extern.h>

#endif /* _NJS_CORE_H_INCLUDED_ */
