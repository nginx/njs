
/*
 * Copyright (C) Dmitry Volyntsev
 * Copyright (C) NGINX, Inc.
 */


#include <njs_main.h>


typedef struct {
    int                 fd;
    njs_str_t           name;
    njs_str_t           file;
} njs_module_info_t;


typedef struct {
    njs_str_t           text;
    njs_module_info_t   info;
    njs_lexer_t         *prev;
    njs_lexer_t         lexer;
} njs_module_temp_t;


static njs_int_t njs_parser_module_lambda_after(njs_parser_t *parser,
    njs_lexer_token_t *token, njs_queue_link_t *current);
static njs_int_t njs_parser_module_after(njs_parser_t *parser,
    njs_lexer_token_t *token, njs_queue_link_t *current);

static njs_int_t njs_module_lookup(njs_vm_t *vm, const njs_str_t *cwd,
    njs_module_info_t *info);
static njs_int_t njs_module_relative_path(njs_vm_t *vm,
    const njs_str_t *dir, njs_module_info_t *info);
static njs_int_t njs_module_absolute_path(njs_vm_t *vm,
    njs_module_info_t *info);
static njs_bool_t njs_module_realpath_equal(const njs_str_t *path1,
    const njs_str_t *path2);
static njs_int_t njs_module_read(njs_vm_t *vm, int fd, njs_str_t *body);
static njs_module_t *njs_module_find(njs_vm_t *vm, njs_str_t *name,
    njs_bool_t local);
static njs_module_t *njs_module_add(njs_vm_t *vm, njs_str_t *name);
static njs_int_t njs_module_insert(njs_parser_t *parser, njs_module_t *module);


njs_int_t
njs_module_load(njs_vm_t *vm)
{
    njs_int_t     ret;
    njs_uint_t    i;
    njs_value_t   *value;
    njs_module_t  **item, *module;

    if (vm->modules == NULL) {
        return NJS_OK;
    }

    item = vm->modules->start;

    for (i = 0; i < vm->modules->items; i++) {
        module = *item;

        if (module->function.native) {
            value = njs_scope_valid_value(vm, module->index);
            njs_set_object(value, &module->object);

        } else {
            ret = njs_vm_invoke(vm, &module->function, NULL, 0,
                                njs_scope_valid_value(vm, module->index));
            if (ret == NJS_ERROR) {
                return ret;
            }
        }

        item++;
    }

    return NJS_OK;
}


void
njs_module_reset(njs_vm_t *vm)
{
    njs_uint_t          i;
    njs_module_t        **item, *module;
    njs_lvlhsh_query_t  lhq;

    if (vm->modules == NULL) {
        return;
    }

    item = vm->modules->start;

    for (i = 0; i < vm->modules->items; i++) {
        module = *item;

        if (!module->function.native) {
            lhq.key = module->name;
            lhq.key_hash = njs_djb_hash(lhq.key.start, lhq.key.length);
            lhq.proto = &njs_modules_hash_proto;
            lhq.pool = vm->mem_pool;

            (void) njs_lvlhsh_delete(&vm->modules_hash, &lhq);
        }

        item++;
    }

    njs_arr_reset(vm->modules);
}


njs_int_t
njs_parser_module(njs_parser_t *parser, njs_lexer_token_t *token,
    njs_queue_link_t *current)
{
    njs_int_t          ret;
    njs_str_t          name, text;
    njs_module_t       *module;
    njs_module_temp_t  *temp;
    njs_module_info_t  info;

    name = token->text;

    parser->node = NULL;

    module = njs_module_find(parser->vm, &name, 0);
    if (module != NULL && module->function.native) {
        njs_lexer_consume_token(parser->lexer, 1);

        parser->target = (njs_parser_node_t *) module;

        return njs_parser_module_after(parser, token, current);
    }

    njs_memzero(&text, sizeof(njs_str_t));

    if (parser->vm->options.sandbox || name.length == 0) {
        njs_parser_syntax_error(parser, "Cannot find module \"%V\"", &name);
        goto fail;
    }

    /* Non-native module. */

    njs_memzero(&info, sizeof(njs_module_info_t));

    info.name = name;

    ret = njs_module_lookup(parser->vm, &parser->scope->cwd, &info);
    if (njs_slow_path(ret != NJS_OK)) {
        njs_parser_syntax_error(parser, "Cannot find module \"%V\"", &name);
        goto fail;
    }

    module = njs_module_find(parser->vm, &info.file, 0);
    if (module != NULL) {
        (void) close(info.fd);
        njs_lexer_consume_token(parser->lexer, 1);

        parser->target = (njs_parser_node_t *) module;

        return njs_parser_module_after(parser, token, current);
    }

    ret = njs_module_read(parser->vm, info.fd, &text);

    (void) close(info.fd);

    if (njs_slow_path(ret != NJS_OK)) {
        njs_internal_error(parser->vm, "while reading \"%V\" module",
                           &info.file);
        goto fail;
    }

    if (njs_module_realpath_equal(&parser->lexer->file, &info.file)) {
        njs_parser_syntax_error(parser, "Cannot import itself \"%V\"",
                                &info.file);
        goto fail;
    }

    temp = njs_mp_alloc(parser->vm->mem_pool, sizeof(njs_module_temp_t));
    if (njs_slow_path(temp == NULL)) {
        return NJS_ERROR;
    }

    ret = njs_lexer_init(parser->vm, &temp->lexer, &info.file, text.start,
                         text.start + text.length, 0);
    if (njs_slow_path(ret != NJS_OK)) {
        return NJS_ERROR;
    }

    njs_lexer_consume_token(parser->lexer, 1);

    temp->prev = parser->lexer;
    temp->info = info;
    temp->text = text;

    parser->lexer = &temp->lexer;

    njs_parser_next(parser, njs_parser_module_lambda);

    return njs_parser_after(parser, current, temp, 0,
                            njs_parser_module_lambda_after);

fail:

    if (text.start != NULL) {
        njs_mp_free(parser->vm->mem_pool, text.start);
    }

    return NJS_ERROR;
}


njs_int_t
njs_parser_module_lambda_after(njs_parser_t *parser, njs_lexer_token_t *token,
    njs_queue_link_t *current)
{
    njs_module_t       *module;
    njs_module_temp_t  *temp;

    temp = (njs_module_temp_t *) parser->target;

    if (parser->ret != NJS_OK) {
        njs_mp_free(parser->vm->mem_pool, temp->text.start);
        njs_mp_free(parser->vm->mem_pool, temp);

        if (token->type == NJS_TOKEN_END) {
            return njs_parser_stack_pop(parser);
        }

        return njs_parser_failed(parser);
    }

    module = njs_module_add(parser->vm, &temp->info.file);
    if (njs_slow_path(module == NULL)) {
        parser->lexer = temp->prev;

        if (temp->text.start != NULL) {
            njs_mp_free(parser->vm->mem_pool, temp->text.start);
        }
    }

    module->function.args_offset = 1;
    module->function.u.lambda = parser->node->u.value.data.u.lambda;

    njs_mp_free(parser->vm->mem_pool, temp->text.start);

    parser->lexer = temp->prev;
    parser->target = (njs_parser_node_t *) module;

    njs_mp_free(parser->vm->mem_pool, temp);

    return njs_parser_module_after(parser, token, current);
}


njs_int_t
njs_parser_module_after(njs_parser_t *parser, njs_lexer_token_t *token,
    njs_queue_link_t *current)
{
    njs_int_t          ret;
    njs_module_t       *module;
    njs_parser_node_t  *node;

    node = njs_parser_node_new(parser, 0);
    if (njs_slow_path(node == NULL)) {
       return NJS_ERROR;
    }

    node->left = parser->node;

    module = (njs_module_t *) parser->target;

    if (module->index == 0) {
        ret = njs_module_insert(parser, module);
        if (njs_slow_path(ret != NJS_OK)) {
            return NJS_ERROR;
        }
    }

    node->index = (njs_index_t) module;

    parser->node = node;

    return njs_parser_stack_pop(parser);
}


static njs_int_t
njs_module_lookup(njs_vm_t *vm, const njs_str_t *cwd, njs_module_info_t *info)
{
    njs_int_t   ret;
    njs_str_t   *path;
    njs_uint_t  i;

    if (info->name.start[0] == '/') {
        return njs_module_absolute_path(vm, info);
    }

    ret = njs_module_relative_path(vm, cwd, info);

    if (ret != NJS_DECLINED) {
        return ret;
    }

    if (vm->paths == NULL) {
        return NJS_DECLINED;
    }

    path = vm->paths->start;

    for (i = 0; i < vm->paths->items; i++) {
        ret = njs_module_relative_path(vm, path, info);

        if (ret != NJS_DECLINED) {
            return ret;
        }

        path++;
    }

    return NJS_DECLINED;
}


static njs_int_t
njs_module_absolute_path(njs_vm_t *vm, njs_module_info_t *info)
{
    njs_str_t  file;

    file.length = info->name.length;
    file.start = njs_mp_alloc(vm->mem_pool, file.length + 1);
    if (njs_slow_path(file.start == NULL)) {
        return NJS_ERROR;
    }

    memcpy(file.start, info->name.start, file.length);
    file.start[file.length] = '\0';

    info->fd = open((char *) file.start, O_RDONLY);
    if (info->fd < 0) {
        njs_mp_free(vm->mem_pool, file.start);
        return NJS_DECLINED;
    }

    info->file = file;

    return NJS_OK;
}


static njs_int_t
njs_module_relative_path(njs_vm_t *vm, const njs_str_t *dir,
    njs_module_info_t *info)
{
    u_char      *p;
    njs_str_t   file;
    njs_bool_t  trail;

    file.length = dir->length;

    if (file.length == 0) {
        return NJS_DECLINED;
    }

    trail = (dir->start[dir->length - 1] != '/');

    if (trail) {
        file.length++;
    }

    file.length += info->name.length;

    file.start = njs_mp_alloc(vm->mem_pool, file.length + 1);
    if (njs_slow_path(file.start == NULL)) {
        return NJS_ERROR;
    }

    p = njs_cpymem(file.start, dir->start, dir->length);

    if (trail) {
        *p++ = '/';
    }

    p = njs_cpymem(p, info->name.start, info->name.length);
    *p = '\0';

    info->fd = open((char *) file.start, O_RDONLY);
    if (info->fd < 0) {
        njs_mp_free(vm->mem_pool, file.start);
        return NJS_DECLINED;
    }

    info->file = file;

    return NJS_OK;
}


static njs_int_t
njs_module_read(njs_vm_t *vm, int fd, njs_str_t *text)
{
    ssize_t      n;
    struct stat  sb;

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


static njs_bool_t
njs_module_realpath_equal(const njs_str_t *path1, const njs_str_t *path2)
{
    char  rpath1[MAXPATHLEN], rpath2[MAXPATHLEN];

    realpath((char *) path1->start, rpath1);
    realpath((char *) path2->start, rpath2);

    return (strcmp(rpath1, rpath2) == 0);
}


static njs_int_t
njs_module_hash_test(njs_lvlhsh_query_t *lhq, void *data)
{
    njs_module_t  *module;

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


static njs_module_t *
njs_module_find(njs_vm_t *vm, njs_str_t *name, njs_bool_t local)
{
    njs_int_t           ret;
    njs_module_t        *module, *shared;
    njs_object_t        *object;
    njs_lvlhsh_query_t  lhq;

    lhq.key = *name;
    lhq.key_hash = njs_djb_hash(name->start, name->length);
    lhq.proto = &njs_modules_hash_proto;

    if (njs_lvlhsh_find(&vm->modules_hash, &lhq) == NJS_OK) {
        return lhq.value;
    }

    if (njs_lvlhsh_find(&vm->shared->modules_hash, &lhq) == NJS_OK) {
        shared = lhq.value;

        if (!local) {
            return shared;
        }

        module = njs_mp_alloc(vm->mem_pool, sizeof(njs_module_t));
        if (njs_slow_path(module == NULL)) {
            njs_memory_error(vm);
            return NULL;
        }

        memcpy(module, shared, sizeof(njs_module_t));
        object = &module->object;

        object->__proto__ = &vm->prototypes[NJS_OBJ_TYPE_OBJECT].object;
        object->slots = NULL;
        object->shared = 0;
        object->extensible = 1;
        object->error_data = 0;
        object->fast_array = 0;

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


static njs_module_t *
njs_module_add(njs_vm_t *vm, njs_str_t *name)
{
    njs_int_t           ret;
    njs_module_t        *module;
    njs_lvlhsh_query_t  lhq;

    module = njs_mp_zalloc(vm->mem_pool, sizeof(njs_module_t));
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

    ret = njs_lvlhsh_insert(&vm->modules_hash, &lhq);
    if (njs_fast_path(ret == NJS_OK)) {
        return module;
    }

    njs_mp_free(vm->mem_pool, module->name.start);
    njs_mp_free(vm->mem_pool, module);

    njs_internal_error(vm, "lvlhsh insert failed");

    return NULL;
}


static njs_int_t
njs_module_insert(njs_parser_t *parser, njs_module_t *module)
{
    njs_vm_t            *vm;
    njs_module_t        **value;
    njs_parser_scope_t  *scope;

    scope = njs_parser_global_scope(parser);
    vm = parser->vm;

    module->index = njs_scope_index(scope->type, scope->items, NJS_LEVEL_LOCAL,
                                    NJS_VARIABLE_VAR);
    scope->items++;

    if (vm->modules == NULL) {
        vm->modules = njs_arr_create(vm->mem_pool, 4, sizeof(njs_module_t *));
        if (njs_slow_path(vm->modules == NULL)) {
            return NJS_ERROR;
        }
    }

    value = njs_arr_add(vm->modules);
    if (njs_slow_path(value == NULL)) {
        return NJS_ERROR;
    }

    *value = module;

    return NJS_OK;
}


njs_int_t
njs_module_require(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t unused)
{
    njs_int_t     ret;
    njs_str_t     name;
    njs_value_t   *path;
    njs_module_t  *module;

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

    module = njs_module_find(vm, &name, 1);
    if (njs_slow_path(module == NULL)) {
        njs_error(vm, "Cannot find module \"%V\"", &name);

        return NJS_ERROR;
    }

    njs_set_object(&vm->retval, &module->object);
    return NJS_OK;
}
