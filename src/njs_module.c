
/*
 * Copyright (C) Dmitry Volyntsev
 * Copyright (C) NGINX, Inc.
 */


#include <njs_main.h>


typedef struct {
    int                 fd;
    njs_str_t           name;
    njs_str_t           file;
    char                path[NJS_MAX_PATH + 1];
} njs_module_info_t;


static njs_int_t njs_module_lookup(njs_vm_t *vm, const njs_str_t *cwd,
    njs_module_info_t *info);
static njs_int_t njs_module_path(njs_vm_t *vm, const njs_str_t *dir,
    njs_module_info_t *info);
static njs_int_t njs_module_read(njs_vm_t *vm, int fd, njs_str_t *body);
static njs_mod_t *njs_default_module_loader(njs_vm_t *vm,
    njs_external_ptr_t external, njs_str_t *name);


njs_mod_t *
njs_parser_module(njs_parser_t *parser, njs_str_t *name)
{
    njs_mod_t            *module;
    njs_vm_t             *vm;
    njs_external_ptr_t   external;
    njs_module_loader_t  loader;

    vm = parser->vm;

    if (name->length == 0) {
        njs_parser_syntax_error(parser, "Cannot find module \"%V\"", name);
        return NULL;
    }

    module = njs_module_find(vm, name, 1);
    if (module != NULL) {
        goto done;
    }

    external = parser;
    loader = njs_default_module_loader;

    if (vm->options.ops != NULL && vm->options.ops->module_loader != NULL) {
        loader = vm->options.ops->module_loader;
        external = vm->external;
    }

    module = loader(vm, external, name);
    if (module == NULL) {
        njs_parser_syntax_error(parser, "Cannot find module \"%V\"", name);
        return NULL;
    }

done:

    if (module->index == 0) {
        module->index = vm->shared->module_items++;
    }

    return module;
}


static njs_int_t
njs_module_lookup(njs_vm_t *vm, const njs_str_t *cwd, njs_module_info_t *info)
{
    njs_int_t   ret;
    njs_str_t   *path;
    njs_uint_t  i;

    if (info->name.start[0] == '/') {
        return njs_module_path(vm, NULL, info);
    }

    ret = njs_module_path(vm, cwd, info);

    if (ret != NJS_DECLINED) {
        return ret;
    }

    if (vm->paths == NULL) {
        return NJS_DECLINED;
    }

    path = vm->paths->start;

    for (i = 0; i < vm->paths->items; i++) {
        ret = njs_module_path(vm, path, info);

        if (ret != NJS_DECLINED) {
            return ret;
        }

        path++;
    }

    return NJS_DECLINED;
}


static njs_int_t
njs_module_path(njs_vm_t *vm, const njs_str_t *dir, njs_module_info_t *info)
{
    char        *p;
    size_t      length;
    njs_bool_t  trail;
    char        src[NJS_MAX_PATH + 1];

    trail = 0;
    length = info->name.length;

    if (dir != NULL) {
        length += dir->length;

        if (length == 0) {
            return NJS_DECLINED;
        }

        trail = (dir->start[dir->length - 1] != '/');

        if (trail) {
            length++;
        }
    }

    if (njs_slow_path(length > NJS_MAX_PATH)) {
        return NJS_ERROR;
    }

    p = &src[0];

    if (dir != NULL) {
        p = (char *) njs_cpymem(p, dir->start, dir->length);

        if (trail) {
            *p++ = '/';
        }
    }

    p = (char *) njs_cpymem(p, info->name.start, info->name.length);
    *p = '\0';

    p = realpath(&src[0], &info->path[0]);
    if (p == NULL) {
        return NJS_DECLINED;
    }

    info->fd = open(&info->path[0], O_RDONLY);
    if (info->fd < 0) {
        return NJS_DECLINED;
    }


    info->file.start = (u_char *) &info->path[0];
    info->file.length = njs_strlen(info->file.start);

    return NJS_OK;
}


static njs_int_t
njs_module_read(njs_vm_t *vm, int fd, njs_str_t *text)
{
    ssize_t      n;
    struct stat  sb;

    text->start = NULL;

    if (fstat(fd, &sb) == -1) {
        goto fail;
    }

    if (!S_ISREG(sb.st_mode)) {
        goto fail;
    }

    text->length = sb.st_size;

    text->start = njs_mp_alloc(vm->mem_pool, text->length);
    if (text->start == NULL) {
        goto fail;
    }

    n = read(fd, text->start, sb.st_size);

    if (n < 0 || n != sb.st_size) {
        goto fail;
    }

    return NJS_OK;

fail:

    if (text->start != NULL) {
        njs_mp_free(vm->mem_pool, text->start);
    }

    return NJS_ERROR;
}


static njs_int_t
njs_module_hash_test(njs_lvlhsh_query_t *lhq, void *data)
{
    njs_mod_t  *module;

    module = data;

    if (njs_strstr_eq(&lhq->key, &module->name)) {
        return NJS_OK;
    }

    return NJS_DECLINED;
}


const njs_lvlhsh_proto_t  njs_modules_hash_proto
    njs_aligned(64) =
{
    NJS_LVLHSH_DEFAULT,
    njs_module_hash_test,
    njs_lvlhsh_alloc,
    njs_lvlhsh_free,
};


njs_mod_t *
njs_module_find(njs_vm_t *vm, njs_str_t *name, njs_bool_t shared)
{
    njs_int_t           ret;
    njs_mod_t           *shrd, *module;
    njs_object_t        *object;
    njs_lvlhsh_query_t  lhq;

    lhq.key = *name;
    lhq.key_hash = njs_djb_hash(name->start, name->length);
    lhq.proto = &njs_modules_hash_proto;

    if (njs_lvlhsh_find(&vm->modules_hash, &lhq) == NJS_OK) {
        return lhq.value;
    }

    if (njs_lvlhsh_find(&vm->shared->modules_hash, &lhq) == NJS_OK) {
        shrd = lhq.value;

        if (shared) {
            return shrd;
        }

        module = njs_mp_alloc(vm->mem_pool, sizeof(njs_mod_t));
        if (njs_slow_path(module == NULL)) {
            njs_memory_error(vm);
            return NULL;
        }

        memcpy(module, shrd, sizeof(njs_mod_t));

        object = njs_object_value_copy(vm, &module->value);
        if (njs_slow_path(object == NULL)) {
            return NULL;
        }

        lhq.replace = 0;
        lhq.value = module;
        lhq.pool = vm->mem_pool;

        ret = njs_lvlhsh_insert(&vm->modules_hash, &lhq);
        if (njs_fast_path(ret == NJS_OK)) {
            return module;
        }
    }

    return NULL;
}


njs_mod_t *
njs_module_add(njs_vm_t *vm, njs_str_t *name)
{
    njs_int_t           ret;
    njs_mod_t           *module;
    njs_lvlhsh_query_t  lhq;

    module = njs_mp_zalloc(vm->mem_pool, sizeof(njs_mod_t));
    if (njs_slow_path(module == NULL)) {
        njs_memory_error(vm);
        return NULL;
    }

    ret = njs_name_copy(vm, &module->name, name);
    if (njs_slow_path(ret != NJS_OK)) {
        njs_memory_error(vm);
        return NULL;
    }

    lhq.replace = 0;
    lhq.key = *name;
    lhq.key_hash = njs_djb_hash(name->start, name->length);
    lhq.value = module;
    lhq.pool = vm->mem_pool;
    lhq.proto = &njs_modules_hash_proto;

    ret = njs_lvlhsh_insert(&vm->shared->modules_hash, &lhq);
    if (njs_fast_path(ret == NJS_OK)) {
        return module;
    }

    njs_mp_free(vm->mem_pool, module->name.start);
    njs_mp_free(vm->mem_pool, module);

    njs_internal_error(vm, "lvlhsh insert failed");

    return NULL;
}


njs_int_t
njs_module_require(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t unused)
{
    njs_int_t    ret;
    njs_str_t    name;
    njs_mod_t    *module;
    njs_value_t  *path;

    if (nargs < 2) {
        njs_type_error(vm, "missing path");
        return NJS_ERROR;
    }

    path = njs_argument(args, 1);
    ret = njs_value_to_string(vm, path, path);
    if (njs_slow_path(ret != NJS_OK)) {
        return ret;
    }

    njs_string_get(path, &name);

    module = njs_module_find(vm, &name, 0);
    if (njs_slow_path(module == NULL)) {
        njs_error(vm, "Cannot find module \"%V\"", &name);

        return NJS_ERROR;
    }

    njs_value_assign(&vm->retval, &module->value);

    return NJS_OK;
}


static njs_mod_t *
njs_default_module_loader(njs_vm_t *vm, njs_external_ptr_t external,
    njs_str_t *name)
{
    njs_int_t          ret;
    njs_str_t          cwd, text;
    njs_parser_t       *prev;
    njs_mod_t          *module;
    njs_module_info_t  info;

    prev = external;

    njs_memzero(&info, sizeof(njs_module_info_t));

    info.name = *name;
    njs_file_dirname(&prev->lexer->file, &cwd);

    ret = njs_module_lookup(vm, &cwd, &info);
    if (njs_slow_path(ret != NJS_OK)) {
        return NULL;
    }

    ret = njs_module_read(vm, info.fd, &text);

    (void) close(info.fd);

    if (njs_slow_path(ret != NJS_OK)) {
        njs_internal_error(vm, "while reading \"%V\" module", &info.file);
        return NULL;
    }

    module = njs_vm_compile_module(vm, &info.file, &text.start,
                                   &text.start[text.length]);

    njs_mp_free(vm->mem_pool, text.start);

    return module;
}
