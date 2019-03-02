
/*
 * Copyright (C) Dmitry Volyntsev
 * Copyright (C) NGINX, Inc.
 */

#include <njs_core.h>
#include <njs_module.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/param.h>


typedef struct {
    int                 fd;
    nxt_str_t           name;
    nxt_str_t           file;
} njs_module_info_t;


static nxt_int_t njs_module_lookup(njs_vm_t *vm, const nxt_str_t *cwd,
    njs_module_info_t *info);
static nxt_noinline nxt_int_t njs_module_relative_path(njs_vm_t *vm,
    const nxt_str_t *dir, njs_module_info_t *info);
static nxt_int_t njs_module_absolute_path(njs_vm_t *vm,
    njs_module_info_t *info);
static nxt_bool_t njs_module_realpath_equal(const nxt_str_t *path1,
    const nxt_str_t *path2);
static nxt_int_t njs_module_read(njs_vm_t *vm, int fd, nxt_str_t *body);
static njs_module_t *njs_module_find(njs_vm_t *vm, nxt_str_t *name);
static njs_module_t *njs_module_add(njs_vm_t *vm, nxt_str_t *name);
static nxt_int_t njs_module_insert(njs_vm_t *vm, njs_module_t *module);


nxt_int_t
njs_module_load(njs_vm_t *vm)
{
    nxt_int_t     ret;
    nxt_uint_t    i;
    njs_value_t   *value;
    njs_module_t  **item, *module;

    if (vm->modules == NULL) {
        return NXT_OK;
    }

    item = vm->modules->start;

    for (i = 0; i < vm->modules->items; i++) {
        module = *item;

        if (module->function.native) {
            value = njs_vmcode_operand(vm, module->index);
            value->data.u.object = &module->object;
            value->type = NJS_OBJECT;
            value->data.truth = 1;

        } else {
            ret = njs_vm_invoke(vm, &module->function, NULL, 0, module->index);
            if (ret == NXT_ERROR) {
                goto done;
            }
        }

        item++;
    }

    ret = NXT_OK;

done:

    if (vm->options.accumulative) {
        nxt_array_reset(vm->modules);
    }

    return ret;
}


nxt_int_t
njs_parser_module(njs_vm_t *vm, njs_parser_t *parser)
{
    nxt_int_t          ret;
    nxt_str_t          name, text;
    njs_lexer_t        *prev, lexer;
    njs_token_t        token;
    njs_module_t       *module;
    njs_parser_node_t  *node;
    njs_module_info_t  info;

    name = *njs_parser_text(parser);

    parser->node = NULL;

    module = njs_module_find(vm, &name);
    if (module != NULL) {
        goto found;
    }

    prev = parser->lexer;

    nxt_memzero(&text, sizeof(nxt_str_t));

    if (vm->options.sandbox || name.length == 0) {
        njs_parser_syntax_error(vm, parser, "Cannot find module \"%V\"", &name);
        goto fail;
    }

    /* Non-native module. */

    nxt_memzero(&info, sizeof(njs_module_info_t));

    info.name = name;

    ret = njs_module_lookup(vm, &parser->scope->cwd, &info);
    if (nxt_slow_path(ret != NXT_OK)) {
        njs_parser_syntax_error(vm, parser, "Cannot find module \"%V\"", &name);
        goto fail;
    }

    ret = njs_module_read(vm, info.fd, &text);

    close(info.fd);

    if (nxt_slow_path(ret != NXT_OK)) {
        njs_internal_error(vm, "while reading \"%V\" module", &name);
        goto fail;
    }

    if (njs_module_realpath_equal(&prev->file, &info.file)) {
        njs_parser_syntax_error(vm, parser, "Cannot import itself \"%V\"",
                                &name);
        goto fail;
    }

    ret = njs_lexer_init(vm, &lexer, &info.file, text.start,
                         text.start + text.length);
    if (nxt_slow_path(ret != NXT_OK)) {
        return NJS_ERROR;
    }

    parser->lexer = &lexer;

    token = njs_parser_token(vm, parser);
    if (nxt_slow_path(token <= NJS_TOKEN_ILLEGAL)) {
        goto fail;
    }

    token = njs_parser_module_lambda(vm, parser);
    if (nxt_slow_path(token <= NJS_TOKEN_ILLEGAL)) {
        goto fail;
    }

    module = njs_module_add(vm, &name);
    if (nxt_slow_path(module == NULL)) {
        goto fail;
    }

    module->function.u.lambda = parser->node->u.value.data.u.lambda;

    nxt_mp_free(vm->mem_pool, text.start);

    parser->lexer = prev;

found:

    node = njs_parser_node_new(vm, parser, 0);
    if (nxt_slow_path(node == NULL)) {
       return NXT_ERROR;
    }

    node->left = parser->node;

    if (module->index == 0) {
        ret = njs_module_insert(vm, module);
        if (nxt_slow_path(ret != NXT_OK)) {
            return NXT_ERROR;
        }
    }

    node->index = (njs_index_t) module;

    parser->node = node;

    return NXT_OK;

fail:

    parser->lexer = prev;

    if (text.start != NULL) {
        nxt_mp_free(vm->mem_pool, text.start);
    }

    return NXT_ERROR;
}


static nxt_int_t
njs_module_lookup(njs_vm_t *vm, const nxt_str_t *cwd, njs_module_info_t *info)
{
    nxt_int_t   ret;
    nxt_str_t   *path;
    nxt_uint_t  i;

    if (info->name.start[0] == '/') {
        return njs_module_absolute_path(vm, info);
    }

    ret = njs_module_relative_path(vm, cwd, info);
    if (ret == NXT_OK) {
        return ret;
    }

    if (vm->paths == NULL) {
        return NXT_DECLINED;
    }

    path = vm->paths->start;

    for (i = 0; i < vm->paths->items; i++) {
        ret = njs_module_relative_path(vm, path, info);
        if (ret == NXT_OK) {
            return ret;
        }

        path++;
    }

    return NXT_DECLINED;
}


static nxt_int_t
njs_module_absolute_path(njs_vm_t *vm, njs_module_info_t *info)
{
    nxt_str_t  file;

    file.length = info->name.length;
    file.start = nxt_mp_alloc(vm->mem_pool, file.length + 1);
    if (nxt_slow_path(file.start == NULL)) {
        return NXT_ERROR;
    }

    memcpy(file.start, info->name.start, file.length);
    file.start[file.length] = '\0';

    info->fd = open((char *) file.start, O_RDONLY);
    if (info->fd < 0) {
        nxt_mp_free(vm->mem_pool, file.start);
        return NXT_DECLINED;
    }

    info->file = file;

    return NXT_OK;
}


static nxt_noinline nxt_int_t
njs_module_relative_path(njs_vm_t *vm, const nxt_str_t *dir,
    njs_module_info_t *info)
{
    u_char      *p;
    nxt_str_t   file;
    nxt_bool_t  trail;

    file.length = dir->length;

    trail = (dir->start[dir->length - 1] != '/');

    if (trail) {
        file.length++;
    }

    file.length += info->name.length;

    file.start = nxt_mp_alloc(vm->mem_pool, file.length + 1);
    if (nxt_slow_path(file.start == NULL)) {
        return NXT_ERROR;
    }

    p = nxt_cpymem(file.start, dir->start, dir->length);

    if (trail) {
        *p++ = '/';
    }

    p = nxt_cpymem(p, info->name.start, info->name.length);
    *p = '\0';

    info->fd = open((char *) file.start, O_RDONLY);
    if (info->fd < 0) {
        nxt_mp_free(vm->mem_pool, file.start);
        return NXT_DECLINED;
    }

    info->file = file;

    return NXT_OK;
}


#define NJS_MODULE_START   "function() {"
#define NJS_MODULE_END     "}"

static nxt_int_t
njs_module_read(njs_vm_t *vm, int fd, nxt_str_t *text)
{
    u_char       *p;
    ssize_t      n;
    struct stat  sb;

    if (fstat(fd, &sb) == -1) {
        goto fail;
    }

    text->length = nxt_length(NJS_MODULE_START);

    if (S_ISREG(sb.st_mode) && sb.st_size) {
        text->length += sb.st_size;
    }

    text->length += nxt_length(NJS_MODULE_END);

    text->start = nxt_mp_alloc(vm->mem_pool, text->length);
    if (text->start == NULL) {
        goto fail;
    }

    p = nxt_cpymem(text->start, NJS_MODULE_START, nxt_length(NJS_MODULE_START));

    n = read(fd, p, sb.st_size);

    if (n < 0) {
        goto fail;
    }

    if (n != sb.st_size) {
        goto fail;
    }

    p += n;

    memcpy(p, NJS_MODULE_END, nxt_length(NJS_MODULE_END));

    return NXT_OK;

fail:

    if (text->start != NULL) {
        nxt_mp_free(vm->mem_pool, text->start);
    }

    return NXT_ERROR;
}


static nxt_bool_t
njs_module_realpath_equal(const nxt_str_t *path1, const nxt_str_t *path2)
{
    char  rpath1[MAXPATHLEN], rpath2[MAXPATHLEN];

    realpath((char *) path1->start, rpath1);
    realpath((char *) path2->start, rpath2);

    return (strcmp(rpath1, rpath2) == 0);
}


static nxt_int_t
njs_module_hash_test(nxt_lvlhsh_query_t *lhq, void *data)
{
    njs_module_t  *module;

    module = data;

    if (nxt_strstr_eq(&lhq->key, &module->name)) {
        return NXT_OK;
    }

    return NXT_DECLINED;
}


const nxt_lvlhsh_proto_t  njs_modules_hash_proto
    nxt_aligned(64) =
{
    NXT_LVLHSH_DEFAULT,
    0,
    njs_module_hash_test,
    njs_lvlhsh_alloc,
    njs_lvlhsh_free,
};


static njs_module_t *
njs_module_find(njs_vm_t *vm, nxt_str_t *name)
{
    nxt_lvlhsh_query_t  lhq;

    lhq.key = *name;
    lhq.key_hash = nxt_djb_hash(name->start, name->length);
    lhq.proto = &njs_modules_hash_proto;

    if (nxt_lvlhsh_find(&vm->modules_hash, &lhq) == NXT_OK) {
        return lhq.value;
    }

    return NULL;
}


static njs_module_t *
njs_module_add(njs_vm_t *vm, nxt_str_t *name)
{
    nxt_int_t           ret;
    njs_module_t        *module;
    nxt_lvlhsh_query_t  lhq;

    module = nxt_mp_zalloc(vm->mem_pool, sizeof(njs_module_t));
    if (nxt_slow_path(module == NULL)) {
        njs_memory_error(vm);
        return NULL;
    }

    ret = njs_name_copy(vm, &module->name, name);
    if (nxt_slow_path(ret != NXT_OK)) {
        nxt_mp_free(vm->mem_pool, module);
        njs_memory_error(vm);
        return NULL;
    }

    lhq.replace = 0;
    lhq.key = *name;
    lhq.key_hash = nxt_djb_hash(name->start, name->length);
    lhq.value = module;
    lhq.pool = vm->mem_pool;
    lhq.proto = &njs_modules_hash_proto;

    ret = nxt_lvlhsh_insert(&vm->modules_hash, &lhq);

    if (nxt_fast_path(ret == NXT_OK)) {
        return module;
    }

    nxt_mp_free(vm->mem_pool, module->name.start);
    nxt_mp_free(vm->mem_pool, module);

    njs_internal_error(vm, "lvlhsh insert failed");

    return NULL;
}


static nxt_int_t
njs_module_insert(njs_vm_t *vm, njs_module_t *module)
{
    njs_module_t        **value;
    njs_parser_scope_t  *scope;

    scope = njs_parser_global_scope(vm);

    module->index = njs_scope_next_index(vm, scope, NJS_SCOPE_INDEX_LOCAL,
                                         &njs_value_undefined);
    if (nxt_slow_path(module->index == NJS_INDEX_ERROR)) {
        return NXT_ERROR;
    }

    if (vm->modules == NULL) {
        vm->modules = nxt_array_create(4, sizeof(njs_module_t *),
                                       &njs_array_mem_proto, vm->mem_pool);
        if (nxt_slow_path(vm->modules == NULL)) {
            return NXT_ERROR;
        }
    }

    value = nxt_array_add(vm->modules, &njs_array_mem_proto, vm->mem_pool);
    if (nxt_slow_path(value == NULL)) {
        return NXT_ERROR;
    }

    *value = module;

    return NXT_OK;
}


njs_ret_t njs_module_require(njs_vm_t *vm, njs_value_t *args,
    nxt_uint_t nargs, njs_index_t unused)
{
    njs_module_t        *module;
    nxt_lvlhsh_query_t  lhq;

    if (nargs < 2) {
        njs_type_error(vm, "missing path");
        return NJS_ERROR;
    }

    njs_string_get(&args[1], &lhq.key);
    lhq.key_hash = nxt_djb_hash(lhq.key.start, lhq.key.length);
    lhq.proto = &njs_modules_hash_proto;

    if (nxt_lvlhsh_find(&vm->modules_hash, &lhq) == NXT_OK) {
        module = lhq.value;
        module->object.__proto__ = &vm->prototypes[NJS_PROTOTYPE_OBJECT].object;

        vm->retval.data.u.object = &module->object;
        vm->retval.type = NJS_OBJECT;
        vm->retval.data.truth = 1;

        return NXT_OK;
    }

    njs_error(vm, "Cannot find module \"%V\"", &lhq.key);

    return NJS_ERROR;
}


const njs_object_init_t  njs_require_function_init = {
    nxt_string("require"),
    NULL,
    0,
};
