
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) Dmitry Volyntsev
 * Copyright (C) NGINX, Inc.
 */


#include <njs_main.h>


typedef struct {
    enum {
       NJS_BUILTIN_TRAVERSE_KEYS,
       NJS_BUILTIN_TRAVERSE_MATCH,
    }                          type;

    njs_function_t             *func;

    njs_lvlhsh_t               keys;
    njs_str_t                  match;
} njs_builtin_traverse_t;


static njs_int_t njs_global_this_prop_handler(njs_vm_t *vm,
    njs_object_prop_t *self, njs_value_t *global, njs_value_t *setval,
    njs_value_t *retval);
static njs_arr_t *njs_vm_expression_completions(njs_vm_t *vm,
    njs_str_t *expression);
static njs_arr_t *njs_object_completions(njs_vm_t *vm, njs_value_t *object);
static njs_int_t njs_env_hash_init(njs_vm_t *vm, njs_lvlhsh_t *hash,
    char **environment);


static const njs_object_init_t  njs_global_this_init;
static const njs_object_init_t  njs_njs_object_init;
static const njs_object_init_t  njs_process_object_init;
#ifdef NJS_TEST262
static const njs_object_init_t  njs_262_object_init;
#endif


static const njs_object_init_t  *njs_object_init[] = {
    &njs_global_this_init,
    &njs_njs_object_init,
    &njs_process_object_init,
    &njs_math_object_init,
    &njs_json_object_init,
#ifdef NJS_TEST262
    &njs_262_object_init,
#endif
    NULL
};


static const njs_object_type_init_t *const
    njs_object_type_init[NJS_OBJ_TYPE_MAX] =
{
    /* Global types. */

    &njs_obj_type_init,
    &njs_array_type_init,
    &njs_boolean_type_init,
    &njs_number_type_init,
    &njs_symbol_type_init,
    &njs_string_type_init,
    &njs_function_type_init,
    &njs_async_function_type_init,
    &njs_regexp_type_init,
    &njs_date_type_init,
    &njs_promise_type_init,
    &njs_array_buffer_type_init,
    &njs_data_view_type_init,
    &njs_text_decoder_type_init,
    &njs_text_encoder_type_init,
    &njs_buffer_type_init,

    /* Hidden types. */

    &njs_iterator_type_init,
    &njs_array_iterator_type_init,
    &njs_typed_array_type_init,

    /* TypedArray types. */

    &njs_typed_array_u8_type_init,
    &njs_typed_array_u8clamped_type_init,
    &njs_typed_array_i8_type_init,
    &njs_typed_array_u16_type_init,
    &njs_typed_array_i16_type_init,
    &njs_typed_array_u32_type_init,
    &njs_typed_array_i32_type_init,
    &njs_typed_array_f32_type_init,
    &njs_typed_array_f64_type_init,

    /* Error types. */
    &njs_error_type_init,
    &njs_eval_error_type_init,
    &njs_internal_error_type_init,
    &njs_range_error_type_init,
    &njs_reference_error_type_init,
    &njs_syntax_error_type_init,
    &njs_type_error_type_init,
    &njs_uri_error_type_init,
    &njs_memory_error_type_init,
    &njs_aggregate_error_type_init,
};


njs_inline njs_int_t
njs_object_hash_init(njs_vm_t *vm, njs_lvlhsh_t *hash,
    const njs_object_init_t *init)
{
    return njs_object_hash_create(vm, hash, init->properties, init->items);
}


njs_int_t
njs_builtin_objects_create(njs_vm_t *vm)
{
    njs_int_t                  ret;
    njs_uint_t                 i;
    njs_object_t               *object, *string_object;
    njs_function_t             *constructor;
    njs_vm_shared_t            *shared;
    njs_regexp_pattern_t       *pattern;
    njs_object_prototype_t     *prototype;
    const njs_object_init_t    *obj, **p;

    shared = njs_mp_zalloc(vm->mem_pool, sizeof(njs_vm_shared_t));
    if (njs_slow_path(shared == NULL)) {
        return NJS_ERROR;
    }

    njs_lvlhsh_init(&shared->keywords_hash);
    njs_lvlhsh_init(&shared->values_hash);

    pattern = njs_regexp_pattern_create(vm, (u_char *) "(?:)",
                                        njs_length("(?:)"), 0);
    if (njs_slow_path(pattern == NULL)) {
        return NJS_ERROR;
    }

    shared->empty_regexp_pattern = pattern;

    ret = njs_object_hash_init(vm, &shared->array_instance_hash,
                               &njs_array_instance_init);
    if (njs_slow_path(ret != NJS_OK)) {
        return NJS_ERROR;
    }

    ret = njs_object_hash_init(vm, &shared->string_instance_hash,
                               &njs_string_instance_init);
    if (njs_slow_path(ret != NJS_OK)) {
        return NJS_ERROR;
    }

    ret = njs_object_hash_init(vm, &shared->function_instance_hash,
                               &njs_function_instance_init);
    if (njs_slow_path(ret != NJS_OK)) {
        return NJS_ERROR;
    }

    ret = njs_object_hash_init(vm, &shared->async_function_instance_hash,
                               &njs_async_function_instance_init);
    if (njs_slow_path(ret != NJS_OK)) {
        return NJS_ERROR;
    }

    ret = njs_object_hash_init(vm, &shared->arrow_instance_hash,
                               &njs_arrow_instance_init);
    if (njs_slow_path(ret != NJS_OK)) {
        return NJS_ERROR;
    }

    ret = njs_object_hash_init(vm, &shared->arguments_object_instance_hash,
                               &njs_arguments_object_instance_init);
    if (njs_slow_path(ret != NJS_OK)) {
        return NJS_ERROR;
    }

    ret = njs_object_hash_init(vm, &shared->regexp_instance_hash,
                               &njs_regexp_instance_init);
    if (njs_slow_path(ret != NJS_OK)) {
        return NJS_ERROR;
    }

    object = shared->objects;

    for (p = njs_object_init; *p != NULL; p++) {
        obj = *p;

        ret = njs_object_hash_init(vm, &object->shared_hash, obj);
        if (njs_slow_path(ret != NJS_OK)) {
            return NJS_ERROR;
        }

        object->type = NJS_OBJECT;
        object->shared = 1;
        object->extensible = 1;

        object++;
    }

    ret = njs_env_hash_init(vm, &shared->env_hash, environ);
    if (njs_slow_path(ret != NJS_OK)) {
        return NJS_ERROR;
    }

    prototype = shared->prototypes;

    for (i = NJS_OBJ_TYPE_OBJECT; i < NJS_OBJ_TYPE_MAX; i++) {
        prototype[i] = njs_object_type_init[i]->prototype_value;

        ret = njs_object_hash_init(vm, &prototype[i].object.shared_hash,
                                   njs_object_type_init[i]->prototype_props);
        if (njs_slow_path(ret != NJS_OK)) {
            return NJS_ERROR;
        }

        prototype[i].object.extensible = 1;
    }

    shared->prototypes[NJS_OBJ_TYPE_REGEXP].regexp.pattern =
                                              shared->empty_regexp_pattern;

    constructor = shared->constructors;

    for (i = NJS_OBJ_TYPE_OBJECT; i < NJS_OBJ_TYPE_MAX; i++) {
        if (njs_object_type_init[i]->constructor_props == NULL) {
            continue;
        }

        constructor[i] = njs_object_type_init[i]->constructor;
        constructor[i].object.shared = 0;

        ret = njs_object_hash_init(vm, &constructor[i].object.shared_hash,
                                   njs_object_type_init[i]->constructor_props);
        if (njs_slow_path(ret != NJS_OK)) {
            return NJS_ERROR;
        }
    }

    shared->global_slots.prop_handler = njs_global_this_prop_handler;
    shared->global_slots.writable = 1;
    shared->global_slots.configurable = 1;
    shared->global_slots.enumerable = 1;

    shared->objects[0].slots = &shared->global_slots;

    vm->global_object = shared->objects[0];
    vm->global_object.shared = 0;

    njs_set_object(&vm->global_value, &vm->global_object);

    string_object = &shared->string_object;
    njs_lvlhsh_init(&string_object->hash);
    string_object->shared_hash = shared->string_instance_hash;
    string_object->type = NJS_OBJECT_VALUE;
    string_object->shared = 1;
    string_object->extensible = 0;

    njs_lvlhsh_init(&shared->modules_hash);

    vm->shared = shared;

    return NJS_OK;
}


njs_int_t
njs_builtin_objects_clone(njs_vm_t *vm, njs_value_t *global)
{
    size_t        size;
    njs_uint_t    i;
    njs_object_t  *object_prototype, *function_prototype,
                  *typed_array_prototype, *error_prototype, *async_prototype,
                  *typed_array_ctor, *error_ctor;

    /*
     * Copy both prototypes and constructors arrays by one memcpy()
     * because they are stored together.
     */
    size = (sizeof(njs_object_prototype_t) + sizeof(njs_function_t))
           * NJS_OBJ_TYPE_MAX;

    memcpy(vm->prototypes, vm->shared->prototypes, size);

    object_prototype = &vm->prototypes[NJS_OBJ_TYPE_OBJECT].object;

    for (i = NJS_OBJ_TYPE_ARRAY; i < NJS_OBJ_TYPE_NORMAL_MAX; i++) {
        vm->prototypes[i].object.__proto__ = object_prototype;
    }

    typed_array_prototype = &vm->prototypes[NJS_OBJ_TYPE_TYPED_ARRAY].object;

    for (i = NJS_OBJ_TYPE_TYPED_ARRAY_MIN;
         i < NJS_OBJ_TYPE_TYPED_ARRAY_MAX;
         i++)
    {
        vm->prototypes[i].object.__proto__ = typed_array_prototype;
    }

    vm->prototypes[NJS_OBJ_TYPE_ARRAY_ITERATOR].object.__proto__ =
                              &vm->prototypes[NJS_OBJ_TYPE_ITERATOR].object;

    vm->prototypes[NJS_OBJ_TYPE_BUFFER].object.__proto__ =
                              &vm->prototypes[NJS_OBJ_TYPE_UINT8_ARRAY].object;

    error_prototype = &vm->prototypes[NJS_OBJ_TYPE_ERROR].object;
    error_prototype->__proto__ = object_prototype;

    for (i = NJS_OBJ_TYPE_EVAL_ERROR; i < NJS_OBJ_TYPE_MAX; i++) {
        vm->prototypes[i].object.__proto__ = error_prototype;
    }

    function_prototype = &vm->prototypes[NJS_OBJ_TYPE_FUNCTION].object;

    async_prototype = &vm->prototypes[NJS_OBJ_TYPE_ASYNC_FUNCTION].object;
    async_prototype->__proto__ = function_prototype;

    for (i = NJS_OBJ_TYPE_OBJECT; i < NJS_OBJ_TYPE_NORMAL_MAX; i++) {
        vm->constructors[i].object.__proto__ = function_prototype;
    }

    typed_array_ctor = &vm->constructors[NJS_OBJ_TYPE_TYPED_ARRAY].object;

    for (i = NJS_OBJ_TYPE_TYPED_ARRAY_MIN;
         i < NJS_OBJ_TYPE_TYPED_ARRAY_MAX;
         i++)
    {
        vm->constructors[i].object.__proto__ = typed_array_ctor;
    }

    error_ctor = &vm->constructors[NJS_OBJ_TYPE_ERROR].object;
    error_ctor->__proto__ = function_prototype;

    for (i = NJS_OBJ_TYPE_EVAL_ERROR; i < NJS_OBJ_TYPE_MAX; i++) {
        vm->constructors[i].object.__proto__ = error_ctor;
    }

    vm->global_object.__proto__ = object_prototype;

    njs_set_undefined(global);
    njs_set_object(global, &vm->global_object);

    vm->string_object = vm->shared->string_object;
    vm->string_object.__proto__ = &vm->prototypes[NJS_OBJ_TYPE_STRING].object;

    return NJS_OK;
}


static njs_int_t
njs_builtin_traverse(njs_vm_t *vm, njs_traverse_t *traverse, void *data)
{
    size_t                  len;
    u_char                  *p, *start, *end;
    njs_int_t               ret, n;
    njs_str_t               name;
    njs_bool_t              symbol;
    njs_value_t             key, *value;
    njs_function_t          *func, *target;
    njs_object_prop_t       *prop;
    njs_lvlhsh_query_t      lhq;
    njs_builtin_traverse_t  *ctx;
    njs_traverse_t          *path[NJS_TRAVERSE_MAX_DEPTH];
    u_char                  buf[256];

    ctx = data;

    if (ctx->type == NJS_BUILTIN_TRAVERSE_MATCH) {
        prop = traverse->prop;
        func = ctx->func;

        if (njs_is_accessor_descriptor(prop)) {
            target = njs_prop_getter(prop);

        } else {
            value = njs_prop_value(prop);
            target = (njs_is_function(value) && njs_function(value)->native)
                            ? njs_function(value)
                            : NULL;
        }

        if (target == NULL
            || !njs_native_function_same(target, func))
        {
            return NJS_OK;
        }
    }

    if (traverse == NULL) {
        njs_type_error(vm, "njs_builtin_traverse() traverse arg is NULL");
        return NJS_ERROR;
    }

    n = 0;

    while (traverse != NULL) {
        path[n++] = traverse;
        traverse = traverse->parent;
    }

    n--;

    p = buf;
    end = buf + sizeof(buf);

    do {
        symbol = 0;
        key = path[n]->prop->name;

        if (njs_slow_path(njs_is_symbol(&key))) {
            symbol = 1;
            key = *njs_symbol_description(&key);
            if (njs_is_undefined(&key)) {
                key = njs_string_empty;
            }
        }

        if (njs_slow_path(!njs_is_string(&key))) {
            /* Skipping special properties (e.g. array index properties). */
            return NJS_OK;
        }

        njs_string_get(&key, &name);

        if (njs_slow_path((p + name.length + 3) > end)) {
            njs_type_error(vm, "njs_builtin_traverse() key is too long");
            return NJS_ERROR;
        }

        if (symbol) {
            *p++ = '[';

        } else if (p != buf) {
            *p++ = '.';
        }

        p = njs_cpymem(p, name.start, name.length);

        if (symbol) {
            *p++ = ']';
        }

    } while (n-- > 0);

    if (ctx->type == NJS_BUILTIN_TRAVERSE_MATCH) {
        len = ctx->match.length;
        start = njs_mp_alloc(vm->mem_pool, len + (p - buf) + (len != 0));
        if (njs_slow_path(start == NULL)) {
            njs_memory_error(vm);
            return NJS_ERROR;
        }

        if (len != 0) {
            memcpy(start, ctx->match.start, len);
            start[len++] = '.';
        }

        memcpy(start + len, buf, p - buf);
        ctx->match.length = len + p - buf;
        ctx->match.start = start;

        return NJS_DONE;
    }

    /* NJS_BUILTIN_TRAVERSE_KEYS. */

    prop = njs_object_prop_alloc(vm, &njs_value_undefined, &njs_value_null, 0);
    if (njs_slow_path(prop == NULL)) {
        return NJS_ERROR;
    }

    ret = njs_string_new(vm, &prop->name, buf, p - buf, 0);
    if (njs_slow_path(ret != NJS_OK)) {
        return ret;
    }

    lhq.value = prop;
    njs_string_get(&prop->name, &lhq.key);
    lhq.key_hash = njs_djb_hash(lhq.key.start, lhq.key.length);
    lhq.replace = 1;
    lhq.pool = vm->mem_pool;
    lhq.proto = &njs_object_hash_proto;

    ret = njs_lvlhsh_insert(&ctx->keys, &lhq);
    if (njs_slow_path(ret != NJS_OK)) {
        njs_internal_error(vm, "lvlhsh insert/replace failed");
        return NJS_ERROR;
    }

    return NJS_OK;
}


static njs_arr_t *
njs_builtin_completions(njs_vm_t *vm)
{
    njs_arr_t                *array;
    njs_str_t                *completion;
    njs_int_t                ret;
    njs_lvlhsh_each_t        lhe;
    njs_builtin_traverse_t   ctx;
    const njs_object_prop_t  *prop;

    array = njs_arr_create(vm->mem_pool, 64, sizeof(njs_str_t));
    if (njs_slow_path(array == NULL)) {
        return NULL;
    }

    ret = njs_lexer_keywords(array);
    if (njs_slow_path(ret != NJS_OK)) {
        return NULL;
    }

    /* Global object completions. */

    ctx.type = NJS_BUILTIN_TRAVERSE_KEYS;
    njs_lvlhsh_init(&ctx.keys);

    ret = njs_object_traverse(vm, &vm->global_object, &ctx,
                              njs_builtin_traverse);
    if (njs_slow_path(ret != NJS_OK)) {
        return NULL;
    }

    njs_lvlhsh_each_init(&lhe, &njs_object_hash_proto);

    for ( ;; ) {
        prop = njs_lvlhsh_each(&ctx.keys, &lhe);

        if (prop == NULL) {
            break;
        }

        completion = njs_arr_add(array);
        if (njs_slow_path(completion == NULL)) {
            return NULL;
        }

        njs_string_get(&prop->name, completion);
    }

    return array;
}


njs_arr_t *
njs_vm_completions(njs_vm_t *vm, njs_str_t *expression)
{
    if (expression == NULL) {
        return njs_builtin_completions(vm);
    }

    return njs_vm_expression_completions(vm, expression);
}


static njs_arr_t *
njs_vm_expression_completions(njs_vm_t *vm, njs_str_t *expression)
{
    u_char               *p, *end;
    njs_int_t            ret;
    njs_value_t          *value;
    njs_variable_t       *var;
    njs_rbtree_node_t    *node;
    njs_object_prop_t    *prop;
    njs_lvlhsh_query_t   lhq;
    njs_variable_node_t  var_node;

    p = expression->start;
    end = p + expression->length;

    lhq.key.start = p;

    while (p < end && *p != '.') { p++; }

    lhq.proto = &njs_lexer_hash_proto;
    lhq.key.length = p - lhq.key.start;
    lhq.key_hash = njs_djb_hash(lhq.key.start, lhq.key.length);

    ret = njs_lvlhsh_find(&vm->shared->keywords_hash, &lhq);
    if (njs_slow_path(ret != NJS_OK)) {
        return NULL;
    }

    var_node.key = (uintptr_t) lhq.value;

    node = njs_rbtree_find(vm->variables_hash, &var_node.node);
    if (njs_slow_path(node == NULL)) {
        return NULL;
    }

    var = ((njs_variable_node_t *) node)->variable;
    value = njs_scope_value(vm, var->index);

    if (!njs_is_object(value)) {
        return NULL;
    }

    lhq.proto = &njs_object_hash_proto;

    for ( ;; ) {

        if (p == end) {
            break;
        }

        lhq.key.start = ++p;

        while (p < end && *p != '.') { p++; }

        lhq.key.length = p - lhq.key.start;
        lhq.key_hash = njs_djb_hash(lhq.key.start, lhq.key.length);

        ret = njs_lvlhsh_find(njs_object_hash(value), &lhq);
        if (njs_slow_path(ret != NJS_OK)) {
            return NULL;
        }

        prop = lhq.value;

        if (njs_is_accessor_descriptor(prop) ||
            !njs_is_object(njs_prop_value(prop)))
        {
            return NULL;
        }

        value = njs_prop_value(prop);
    }

    return njs_object_completions(vm, value);
}


static njs_arr_t *
njs_object_completions(njs_vm_t *vm, njs_value_t *object)
{
    double            num;
    njs_arr_t         *array;
    njs_str_t         *completion;
    njs_uint_t        n;
    njs_array_t       *keys;
    njs_value_type_t  type;

    array = NULL;
    type = object->type;

    if (type == NJS_ARRAY || type == NJS_TYPED_ARRAY) {
        object->type = NJS_OBJECT;
    }

    keys = njs_value_enumerate(vm, object, NJS_ENUM_KEYS, NJS_ENUM_STRING, 1);
    if (njs_slow_path(keys == NULL)) {
        goto done;
    }

    array = njs_arr_create(vm->mem_pool, 8, sizeof(njs_str_t));
    if (njs_slow_path(array == NULL)) {
        goto done;
    }

    for (n = 0; n < keys->length; n++) {
        num = njs_key_to_index(&keys->start[n]);

        if (!njs_key_is_integer_index(num, &keys->start[n])) {
            completion = njs_arr_add(array);
            if (njs_slow_path(completion == NULL)) {
                njs_arr_destroy(array);
                array = NULL;
                goto done;
            }

            njs_string_get(&keys->start[n], completion);
        }
    }

done:

    if (type == NJS_ARRAY || type == NJS_TYPED_ARRAY) {
        object->type = type;
    }

    return array;
}


typedef struct {
    njs_str_t               name;
    njs_function_native_t   native;
    uint8_t                 magic8;
} njs_function_name_t;


njs_int_t
njs_builtin_match_native_function(njs_vm_t *vm, njs_function_t *function,
    njs_str_t *name)
{
    uint8_t                 magic8;
    njs_int_t               ret;
    njs_arr_t               **pprotos;
    njs_mod_t               *module;
    njs_uint_t              i, n;
    njs_value_t             value, tag;
    njs_object_t            object;
    njs_lvlhsh_each_t       lhe;
    njs_exotic_slots_t      *slots;
    njs_function_name_t     *fn;
    njs_function_native_t   native;
    njs_builtin_traverse_t  ctx;

    if (vm->functions_name_cache != NULL) {
        n = vm->functions_name_cache->items;
        fn = vm->functions_name_cache->start;

        magic8 = function->magic8;
        native = function->u.native;

        while (n != 0) {
            if (fn->native == native && fn->magic8 == magic8) {
                *name = fn->name;
                return NJS_OK;
            }

            fn++;
            n--;
        }
    }

    ctx.type = NJS_BUILTIN_TRAVERSE_MATCH;
    ctx.func = function;

    /* Global object. */

    ctx.match = njs_str_value("");

    ret = njs_object_traverse(vm, &vm->global_object, &ctx,
                              njs_builtin_traverse);

    if (ret == NJS_DONE) {
        goto found;
    }

    /* Constructor from built-in modules (not-mapped to global object). */

    for (i = NJS_OBJ_TYPE_HIDDEN_MIN; i < NJS_OBJ_TYPE_HIDDEN_MAX; i++) {
        njs_set_object(&value, &vm->constructors[i].object);

        ret = njs_value_property(vm, &value, njs_value_arg(&njs_string_name),
                                 &tag);

        if (ret == NJS_OK && njs_is_string(&tag)) {
            njs_string_get(&tag, &ctx.match);
        }

        ret = njs_object_traverse(vm, &vm->constructors[i].object, &ctx,
                                  njs_builtin_traverse);

        if (ret == NJS_DONE) {
            goto found;
        }
    }

    /* Modules. */

    njs_lvlhsh_each_init(&lhe, &njs_modules_hash_proto);

    for ( ;; ) {
        module = njs_lvlhsh_each(&vm->modules_hash, &lhe);

        if (module == NULL) {
            break;
        }

        if (njs_is_object(&module->value)
            && !njs_object(&module->value)->shared)
        {
            ctx.match = module->name;

            ret = njs_object_traverse(vm, njs_object(&module->value), &ctx,
                                      njs_builtin_traverse);

            if (ret == NJS_DONE) {
                goto found;
            }
        }
    }

    /* External prototypes (not mapped to global object). */

    ctx.match = njs_str_value("");

    for (i = 0; i< vm->protos->items; i++) {
        njs_memzero(&object, sizeof(njs_object_t));

        pprotos = njs_arr_item(vm->protos, i);
        slots = (*pprotos)->start;

        object.shared_hash = slots->external_shared_hash;
        object.slots = slots;

        njs_set_object(&value, &object);

        ret = njs_object_string_tag(vm, &value, &tag);
        if (ret == NJS_OK && njs_is_string(&tag)) {
            njs_string_get(&tag, &ctx.match);
        }

        ret = njs_object_traverse(vm, njs_object(&value), &ctx,
                                  njs_builtin_traverse);

        if (ret == NJS_DONE) {
            goto found;
        }
    }

    return NJS_DECLINED;

found:

    if (vm->functions_name_cache == NULL) {
        vm->functions_name_cache = njs_arr_create(vm->mem_pool, 4,
                                                  sizeof(njs_function_name_t));
        if (njs_slow_path(vm->functions_name_cache == NULL)) {
            return NJS_ERROR;
        }
    }

    fn = njs_arr_add(vm->functions_name_cache);
    if (njs_slow_path(fn == NULL)) {
        njs_memory_error(vm);
        return NJS_ERROR;
    }

    fn->name = ctx.match;
    fn->native = function->u.native;
    fn->magic8 = function->magic8;

    *name = fn->name;

    return NJS_OK;
}


static njs_int_t
njs_ext_dump(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t unused)
{
    uint32_t     n;
    njs_int_t    ret;
    njs_str_t    str;
    njs_value_t  *value, *indent;

    value = njs_arg(args, nargs, 1);
    indent = njs_arg(args, nargs, 2);

    ret = njs_value_to_uint32(vm, indent, &n);
    if (njs_slow_path(ret != NJS_OK)) {
        return ret;
    }

    n = njs_min(n, 5);

    if (njs_vm_value_dump(vm, &str, value, 1, n) != NJS_OK) {
        return NJS_ERROR;
    }

    return njs_string_new(vm, &vm->retval, str.start, str.length, 0);
}


static njs_int_t
njs_ext_on(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t unused)
{
    njs_str_t    type;
    njs_uint_t   i, n;
    njs_value_t  *value;

    static const struct {
        njs_str_t   name;
        njs_uint_t  id;
    } hooks[] = {
        {
            njs_str("exit"),
            NJS_HOOK_EXIT
        },
    };

    value = njs_arg(args, nargs, 1);

    if (njs_slow_path(!njs_is_string(value))) {
        njs_type_error(vm, "hook type is not a string");
        return NJS_ERROR;
    }

    njs_string_get(value, &type);

    i = 0;
    n = sizeof(hooks) / sizeof(hooks[0]);

    while (i < n) {
        if (njs_strstr_eq(&type, &hooks[i].name)) {
            break;
        }

        i++;
    }

    if (i == n) {
        njs_type_error(vm, "unknown hook type \"%V\"", &type);
        return NJS_ERROR;
    }

    value = njs_arg(args, nargs, 2);

    if (njs_slow_path(!njs_is_function(value) && !njs_is_null(value))) {
        njs_type_error(vm, "callback is not a function or null");
        return NJS_ERROR;
    }

    vm->hooks[i] = njs_is_function(value) ? njs_function(value) : NULL;

    return NJS_OK;
}


static njs_int_t
njs_ext_memory_stats(njs_vm_t *vm, njs_object_prop_t *prop,
    njs_value_t *unused, njs_value_t *unused2, njs_value_t *retval)
{
    njs_int_t      ret;
    njs_value_t    object, value;
    njs_object_t   *stat;
    njs_mp_stat_t  mp_stat;

    static const njs_value_t  size_string = njs_string("size");
    static const njs_value_t  nblocks_string = njs_string("nblocks");
    static const njs_value_t  page_string = njs_string("page_size");
    static const njs_value_t  cluster_string = njs_string("cluster_size");

    stat = njs_object_alloc(vm);
    if (njs_slow_path(stat == NULL)) {
        return NJS_ERROR;
    }

    njs_set_object(&object, stat);

    njs_mp_stat(vm->mem_pool, &mp_stat);

    njs_set_number(&value, mp_stat.size);

    ret = njs_value_property_set(vm, &object, njs_value_arg(&size_string),
                                 &value);
    if (njs_slow_path(ret != NJS_OK)) {
        return NJS_ERROR;
    }

    njs_set_number(&value, mp_stat.nblocks);

    ret = njs_value_property_set(vm, &object, njs_value_arg(&nblocks_string),
                                 &value);
    if (njs_slow_path(ret != NJS_OK)) {
        return NJS_ERROR;
    }

    njs_set_number(&value, mp_stat.cluster_size);

    ret = njs_value_property_set(vm, &object, njs_value_arg(&cluster_string),
                                 &value);
    if (njs_slow_path(ret != NJS_OK)) {
        return NJS_ERROR;
    }

    njs_set_number(&value, mp_stat.page_size);

    ret = njs_value_property_set(vm, &object, njs_value_arg(&page_string),
                                 &value);
    if (njs_slow_path(ret != NJS_OK)) {
        return NJS_ERROR;
    }

    njs_set_object(retval, stat);

    return NJS_OK;
}




static njs_int_t
njs_global_this_prop_handler(njs_vm_t *vm, njs_object_prop_t *prop,
    njs_value_t *global, njs_value_t *setval, njs_value_t *retval)
{
    njs_int_t            ret;
    njs_value_t          *value;
    njs_variable_t       *var;
    njs_function_t       *function;
    njs_rbtree_node_t    *rb_node;
    njs_lvlhsh_query_t   lhq;
    njs_variable_node_t  *node, var_node;

    if (retval == NULL) {
        return NJS_DECLINED;
    }

    njs_string_get(&prop->name, &lhq.key);
    lhq.key_hash = njs_djb_hash(lhq.key.start, lhq.key.length);
    lhq.proto = &njs_lexer_hash_proto;

    ret = njs_lvlhsh_find(&vm->shared->keywords_hash, &lhq);

    if (njs_slow_path(ret != NJS_OK || lhq.value == NULL)) {
        return NJS_DECLINED;
    }

    var_node.key = (uintptr_t) lhq.value;

    rb_node = njs_rbtree_find(vm->variables_hash, &var_node.node);
    if (rb_node == NULL) {
        return NJS_DECLINED;
    }

    node = (njs_variable_node_t *) rb_node;

    var = node->variable;

    if (var->type == NJS_VARIABLE_LET || var->type == NJS_VARIABLE_CONST) {
        return NJS_DECLINED;
    }

    value = njs_scope_valid_value(vm, var->index);

    if (var->type == NJS_VARIABLE_FUNCTION && njs_is_undefined(value)) {
        njs_value_assign(value, &var->value);

        function = njs_function_value_copy(vm, value);
        if (njs_slow_path(function == NULL)) {
            return NJS_ERROR;
        }
    }

    if (setval != NULL) {
        njs_value_assign(value, setval);
    }

    njs_value_assign(retval, value);

    return NJS_OK;
}


static njs_int_t
njs_global_this_object(njs_vm_t *vm, njs_object_prop_t *self,
    njs_value_t *global, njs_value_t *setval, njs_value_t *retval)
{
    njs_int_t           ret;
    njs_object_prop_t   *prop;
    njs_lvlhsh_query_t  lhq;

    njs_value_assign(retval, global);

    if (njs_slow_path(setval != NULL)) {
        njs_value_assign(retval, setval);

    } else if (njs_slow_path(retval == NULL)) {
        return NJS_DECLINED;
    }

    prop = njs_object_prop_alloc(vm, &self->name, retval, 1);
    if (njs_slow_path(prop == NULL)) {
        return NJS_ERROR;
    }

    njs_value_assign(njs_prop_value(prop), retval);
    prop->enumerable = self->enumerable;

    lhq.value = prop;
    njs_string_get(&self->name, &lhq.key);
    lhq.key_hash = njs_prop_magic32(self);
    lhq.replace = 1;
    lhq.pool = vm->mem_pool;
    lhq.proto = &njs_object_hash_proto;

    ret = njs_lvlhsh_insert(njs_object_hash(global), &lhq);
    if (njs_slow_path(ret != NJS_OK)) {
        njs_internal_error(vm, "lvlhsh insert/replace failed");
        return NJS_ERROR;
    }

    return NJS_OK;
}


static njs_int_t
njs_top_level_object(njs_vm_t *vm, njs_object_prop_t *self,
    njs_value_t *global, njs_value_t *setval, njs_value_t *retval)
{
    njs_int_t           ret;
    njs_object_t        *object;
    njs_object_prop_t   *prop;
    njs_lvlhsh_query_t  lhq;

    if (njs_slow_path(setval != NULL)) {
        njs_value_assign(retval, setval);

    } else {
        if (njs_slow_path(retval == NULL)) {
            return NJS_DECLINED;
        }

        njs_set_object(retval, &vm->shared->objects[njs_prop_magic16(self)]);

        object = njs_object_value_copy(vm, retval);
        if (njs_slow_path(object == NULL)) {
            return NJS_ERROR;
        }
    }

    prop = njs_object_prop_alloc(vm, &self->name, retval, 1);
    if (njs_slow_path(prop == NULL)) {
        return NJS_ERROR;
    }

    njs_value_assign(njs_prop_value(prop), retval);
    prop->enumerable = self->enumerable;

    lhq.value = prop;
    njs_string_get(&self->name, &lhq.key);
    lhq.key_hash = njs_prop_magic32(self);
    lhq.replace = 1;
    lhq.pool = vm->mem_pool;
    lhq.proto = &njs_object_hash_proto;

    ret = njs_lvlhsh_insert(njs_object_hash(global), &lhq);
    if (njs_slow_path(ret != NJS_OK)) {
        njs_internal_error(vm, "lvlhsh insert/replace failed");
        return NJS_ERROR;
    }

    return NJS_OK;
}


static njs_int_t
njs_top_level_constructor(njs_vm_t *vm, njs_object_prop_t *self,
    njs_value_t *global, njs_value_t *setval, njs_value_t *retval)
{
    njs_int_t           ret;
    njs_function_t      *ctor;
    njs_object_prop_t   *prop;
    njs_lvlhsh_query_t  lhq;

    if (njs_slow_path(setval != NULL)) {
        njs_value_assign(retval, setval);

    } else {
        if (njs_slow_path(retval == NULL)) {
            return NJS_DECLINED;
        }

        ctor = &vm->constructors[njs_prop_magic16(self)];

        njs_set_function(retval, ctor);
    }

    prop = njs_object_prop_alloc(vm, &self->name, retval, 1);
    if (njs_slow_path(prop == NULL)) {
        return NJS_ERROR;
    }

    njs_value_assign(njs_prop_value(prop), retval);
    prop->enumerable = 0;

    lhq.value = prop;
    njs_string_get(&self->name, &lhq.key);
    lhq.key_hash = njs_prop_magic32(self);
    lhq.replace = 1;
    lhq.pool = vm->mem_pool;
    lhq.proto = &njs_object_hash_proto;

    ret = njs_lvlhsh_insert(njs_object_hash(global), &lhq);
    if (njs_slow_path(ret != NJS_OK)) {
        njs_internal_error(vm, "lvlhsh insert/replace failed");
        return NJS_ERROR;
    }

    return NJS_OK;
}


static const njs_object_prop_t  njs_global_this_object_properties[] =
{
    {
        .type = NJS_PROPERTY,
        .name = njs_wellknown_symbol(NJS_SYMBOL_TO_STRING_TAG),
        .u.value = njs_string("global"),
        .configurable = 1,
    },

    /* Global aliases. */

    NJS_DECLARE_PROP_HANDLER("global", njs_global_this_object, 0,
                             NJS_GLOBAL_HASH, NJS_OBJECT_PROP_VALUE_ECW),

    NJS_DECLARE_PROP_HANDLER("globalThis", njs_global_this_object, 0,
                             NJS_GLOBAL_THIS_HASH, NJS_OBJECT_PROP_VALUE_CW),

    /* Global constants. */

    NJS_DECLARE_PROP_VALUE("NaN",  njs_value(NJS_NUMBER, 0, NAN), 0),

    NJS_DECLARE_PROP_VALUE("Infinity",  njs_value(NJS_NUMBER, 1, INFINITY), 0),

    NJS_DECLARE_PROP_VALUE("undefined",  njs_value(NJS_UNDEFINED, 0, NAN), 0),

    /* Global functions. */

    NJS_DECLARE_PROP_NATIVE("isFinite", njs_number_global_is_finite, 1, 0),

    NJS_DECLARE_PROP_NATIVE("isNaN", njs_number_global_is_nan, 1, 0),

    NJS_DECLARE_PROP_NATIVE("parseFloat", njs_number_parse_float, 1, 0),

    NJS_DECLARE_PROP_NATIVE("parseInt", njs_number_parse_int, 2, 0),

    NJS_DECLARE_PROP_NATIVE("toString", njs_object_prototype_to_string, 0, 0),

    NJS_DECLARE_PROP_NATIVE("encodeURI", njs_string_encode_uri, 1, 0),

    NJS_DECLARE_PROP_LNATIVE("encodeURIComponent", njs_string_encode_uri, 1, 1),

    NJS_DECLARE_PROP_NATIVE("decodeURI", njs_string_decode_uri, 1, 0),

    NJS_DECLARE_PROP_LNATIVE("decodeURIComponent", njs_string_decode_uri, 1, 1),

    NJS_DECLARE_PROP_NATIVE("atob", njs_string_atob, 1, 0),

    NJS_DECLARE_PROP_NATIVE("btoa", njs_string_btoa, 1, 0),

    NJS_DECLARE_PROP_NATIVE("eval", njs_eval_function, 1, 0),

    NJS_DECLARE_PROP_NATIVE("setTimeout", njs_set_timeout, 2, 0),

    NJS_DECLARE_PROP_NATIVE("setImmediate", njs_set_immediate, 4, 0),

    NJS_DECLARE_PROP_NATIVE("clearTimeout", njs_clear_timeout, 1, 0),

    NJS_DECLARE_PROP_NATIVE("require", njs_module_require, 1, 0),

    /* Global objects. */

    NJS_DECLARE_PROP_HANDLER("njs", njs_top_level_object, NJS_OBJECT_NJS,
                             NJS_NJS_HASH, NJS_OBJECT_PROP_VALUE_ECW),

    NJS_DECLARE_PROP_HANDLER("process", njs_top_level_object,
                             NJS_OBJECT_PROCESS, NJS_PROCESS_HASH,
                             NJS_OBJECT_PROP_VALUE_ECW),

    NJS_DECLARE_PROP_HANDLER("Math", njs_top_level_object,
                             NJS_OBJECT_MATH, NJS_MATH_HASH,
                             NJS_OBJECT_PROP_VALUE_CW),

    NJS_DECLARE_PROP_HANDLER("JSON", njs_top_level_object,
                             NJS_OBJECT_JSON, NJS_JSON_HASH,
                             NJS_OBJECT_PROP_VALUE_CW),


#ifdef NJS_TEST262
    NJS_DECLARE_PROP_HANDLER("$262", njs_top_level_object,
                             NJS_OBJECT_262, NJS_262_HASH,
                             NJS_OBJECT_PROP_VALUE_ECW),
#endif

    /* Global constructors. */

    NJS_DECLARE_PROP_HANDLER("Object", njs_top_level_constructor,
                             NJS_OBJ_TYPE_OBJECT, NJS_OBJECT_HASH,
                             NJS_OBJECT_PROP_VALUE_CW),

    NJS_DECLARE_PROP_HANDLER("Array", njs_top_level_constructor,
                             NJS_OBJ_TYPE_ARRAY, NJS_ARRAY_HASH,
                             NJS_OBJECT_PROP_VALUE_CW),

    NJS_DECLARE_PROP_HANDLER("ArrayBuffer", njs_top_level_constructor,
                             NJS_OBJ_TYPE_ARRAY_BUFFER, NJS_ARRAY_BUFFER_HASH,
                             NJS_OBJECT_PROP_VALUE_CW),

    NJS_DECLARE_PROP_HANDLER("DataView", njs_top_level_constructor,
                             NJS_OBJ_TYPE_DATA_VIEW, NJS_DATA_VIEW_HASH,
                             NJS_OBJECT_PROP_VALUE_CW),

    NJS_DECLARE_PROP_HANDLER("TextDecoder", njs_top_level_constructor,
                             NJS_OBJ_TYPE_TEXT_DECODER, NJS_TEXT_DECODER_HASH,
                             NJS_OBJECT_PROP_VALUE_CW),

    NJS_DECLARE_PROP_HANDLER("TextEncoder", njs_top_level_constructor,
                             NJS_OBJ_TYPE_TEXT_ENCODER, NJS_TEXT_ENCODER_HASH,
                             NJS_OBJECT_PROP_VALUE_CW),

    NJS_DECLARE_PROP_HANDLER("Buffer", njs_top_level_constructor,
                             NJS_OBJ_TYPE_BUFFER, NJS_BUFFER_HASH,
                             NJS_OBJECT_PROP_VALUE_CW),

    NJS_DECLARE_PROP_HANDLER("Uint8Array", njs_top_level_constructor,
                             NJS_OBJ_TYPE_UINT8_ARRAY, NJS_UINT8ARRAY_HASH,
                             NJS_OBJECT_PROP_VALUE_CW),

    NJS_DECLARE_PROP_HANDLER("Uint16Array", njs_top_level_constructor,
                             NJS_OBJ_TYPE_UINT16_ARRAY, NJS_UINT16ARRAY_HASH,
                             NJS_OBJECT_PROP_VALUE_CW),

    NJS_DECLARE_PROP_HANDLER("Uint32Array", njs_top_level_constructor,
                             NJS_OBJ_TYPE_UINT32_ARRAY, NJS_UINT32ARRAY_HASH,
                             NJS_OBJECT_PROP_VALUE_CW),

    NJS_DECLARE_PROP_HANDLER("Int8Array", njs_top_level_constructor,
                             NJS_OBJ_TYPE_INT8_ARRAY, NJS_INT8ARRAY_HASH,
                             NJS_OBJECT_PROP_VALUE_CW),

    NJS_DECLARE_PROP_HANDLER("Int16Array", njs_top_level_constructor,
                             NJS_OBJ_TYPE_INT16_ARRAY, NJS_INT16ARRAY_HASH,
                             NJS_OBJECT_PROP_VALUE_CW),

    NJS_DECLARE_PROP_HANDLER("Int32Array", njs_top_level_constructor,
                             NJS_OBJ_TYPE_INT32_ARRAY, NJS_INT32ARRAY_HASH,
                             NJS_OBJECT_PROP_VALUE_CW),

    NJS_DECLARE_PROP_HANDLER("Float32Array", njs_top_level_constructor,
                             NJS_OBJ_TYPE_FLOAT32_ARRAY, NJS_FLOAT32ARRAY_HASH,
                             NJS_OBJECT_PROP_VALUE_CW),

    NJS_DECLARE_PROP_HANDLER("Float64Array", njs_top_level_constructor,
                             NJS_OBJ_TYPE_FLOAT64_ARRAY, NJS_FLOAT64ARRAY_HASH,
                             NJS_OBJECT_PROP_VALUE_CW),

    {
        .type = NJS_PROPERTY_HANDLER,
        .name = njs_long_string("Uint8ClampedArray"),
        .u.value = njs_prop_handler2(njs_top_level_constructor,
                                   NJS_OBJ_TYPE_UINT8_CLAMPED_ARRAY,
                                   NJS_UINT8CLAMPEDARRAY_HASH),
        .writable = 1,
        .configurable = 1,
    },

    NJS_DECLARE_PROP_HANDLER("Boolean", njs_top_level_constructor,
                             NJS_OBJ_TYPE_BOOLEAN, NJS_BOOLEAN_HASH,
                             NJS_OBJECT_PROP_VALUE_CW),

    NJS_DECLARE_PROP_HANDLER("Number", njs_top_level_constructor,
                             NJS_OBJ_TYPE_NUMBER, NJS_NUMBER_HASH,
                             NJS_OBJECT_PROP_VALUE_CW),

    NJS_DECLARE_PROP_HANDLER("Symbol", njs_top_level_constructor,
                             NJS_OBJ_TYPE_SYMBOL, NJS_SYMBOL_HASH,
                             NJS_OBJECT_PROP_VALUE_CW),

    NJS_DECLARE_PROP_HANDLER("String", njs_top_level_constructor,
                             NJS_OBJ_TYPE_STRING, NJS_STRING_HASH,
                             NJS_OBJECT_PROP_VALUE_CW),

    NJS_DECLARE_PROP_HANDLER("Function", njs_top_level_constructor,
                             NJS_OBJ_TYPE_FUNCTION, NJS_FUNCTION_HASH,
                             NJS_OBJECT_PROP_VALUE_CW),

    NJS_DECLARE_PROP_HANDLER("RegExp", njs_top_level_constructor,
                             NJS_OBJ_TYPE_REGEXP, NJS_REGEXP_HASH,
                             NJS_OBJECT_PROP_VALUE_CW),

    NJS_DECLARE_PROP_HANDLER("Date", njs_top_level_constructor,
                             NJS_OBJ_TYPE_DATE, NJS_DATE_HASH,
                             NJS_OBJECT_PROP_VALUE_CW),

    NJS_DECLARE_PROP_HANDLER("Promise", njs_top_level_constructor,
                             NJS_OBJ_TYPE_PROMISE, NJS_PROMISE_HASH,
                             NJS_OBJECT_PROP_VALUE_CW),

    NJS_DECLARE_PROP_HANDLER("Error", njs_top_level_constructor,
                             NJS_OBJ_TYPE_ERROR, NJS_ERROR_HASH,
                             NJS_OBJECT_PROP_VALUE_CW),

    NJS_DECLARE_PROP_HANDLER("EvalError", njs_top_level_constructor,
                             NJS_OBJ_TYPE_EVAL_ERROR, NJS_EVAL_ERROR_HASH,
                             NJS_OBJECT_PROP_VALUE_CW),

    NJS_DECLARE_PROP_HANDLER("InternalError", njs_top_level_constructor,
                             NJS_OBJ_TYPE_INTERNAL_ERROR,
                             NJS_INTERNAL_ERROR_HASH,
                             NJS_OBJECT_PROP_VALUE_CW),

    NJS_DECLARE_PROP_HANDLER("RangeError", njs_top_level_constructor,
                             NJS_OBJ_TYPE_RANGE_ERROR, NJS_RANGE_ERROR_HASH,
                             NJS_OBJECT_PROP_VALUE_CW),

    NJS_DECLARE_PROP_HANDLER("ReferenceError", njs_top_level_constructor,
                             NJS_OBJ_TYPE_REF_ERROR, NJS_REF_ERROR_HASH,
                             NJS_OBJECT_PROP_VALUE_CW),

    NJS_DECLARE_PROP_HANDLER("SyntaxError", njs_top_level_constructor,
                             NJS_OBJ_TYPE_SYNTAX_ERROR, NJS_SYNTAX_ERROR_HASH,
                             NJS_OBJECT_PROP_VALUE_CW),

    NJS_DECLARE_PROP_HANDLER("TypeError", njs_top_level_constructor,
                             NJS_OBJ_TYPE_TYPE_ERROR, NJS_TYPE_ERROR_HASH,
                             NJS_OBJECT_PROP_VALUE_CW),

    NJS_DECLARE_PROP_HANDLER("URIError", njs_top_level_constructor,
                             NJS_OBJ_TYPE_URI_ERROR, NJS_URI_ERROR_HASH,
                             NJS_OBJECT_PROP_VALUE_CW),

    NJS_DECLARE_PROP_HANDLER("MemoryError", njs_top_level_constructor,
                             NJS_OBJ_TYPE_MEMORY_ERROR, NJS_MEMORY_ERROR_HASH,
                             NJS_OBJECT_PROP_VALUE_CW),

    NJS_DECLARE_PROP_HANDLER("AggregateError", njs_top_level_constructor,
                             NJS_OBJ_TYPE_AGGREGATE_ERROR,
                             NJS_AGGREGATE_ERROR_HASH,
                             NJS_OBJECT_PROP_VALUE_CW),
};


static const njs_object_init_t  njs_global_this_init = {
    njs_global_this_object_properties,
    njs_nitems(njs_global_this_object_properties)
};


static const njs_object_prop_t  njs_njs_object_properties[] =
{
    {
        .type = NJS_PROPERTY,
        .name = njs_wellknown_symbol(NJS_SYMBOL_TO_STRING_TAG),
        .u.value = njs_string("njs"),
        .configurable = 1,
    },

    NJS_DECLARE_PROP_VALUE("version", njs_string(NJS_VERSION),
                           NJS_OBJECT_PROP_VALUE_EC),

    NJS_DECLARE_PROP_VALUE("version_number",
                           njs_value(NJS_NUMBER, 1, NJS_VERSION_NUMBER),
                           NJS_OBJECT_PROP_VALUE_EC),

    NJS_DECLARE_PROP_NATIVE("dump", njs_ext_dump, 0, 0),

    NJS_DECLARE_PROP_NATIVE("on", njs_ext_on, 0, 0),

    NJS_DECLARE_PROP_HANDLER("memoryStats", njs_ext_memory_stats, 0, 0,
                             NJS_OBJECT_PROP_VALUE_EC),

};


static const njs_object_init_t  njs_njs_object_init = {
    njs_njs_object_properties,
    njs_nitems(njs_njs_object_properties),
};


static njs_int_t
njs_process_object_argv(njs_vm_t *vm, njs_object_prop_t *pr,
    njs_value_t *process, njs_value_t *unused, njs_value_t *retval)
{
    char                **arg;
    njs_int_t           ret;
    njs_uint_t          i;
    njs_array_t         *argv;
    njs_object_prop_t   *prop;
    njs_lvlhsh_query_t  lhq;

    static const njs_value_t  argv_string = njs_string("argv");

    argv = njs_array_alloc(vm, 1, vm->options.argc, 0);
    if (njs_slow_path(argv == NULL)) {
        return NJS_ERROR;
    }

    i = 0;

    for (arg = vm->options.argv; i < vm->options.argc; arg++) {
        njs_string_set(vm, &argv->start[i++], (u_char *) *arg,
                       njs_strlen(*arg));
    }

    prop = njs_object_prop_alloc(vm, &argv_string, &njs_value_undefined, 1);
    if (njs_slow_path(prop == NULL)) {
        return NJS_ERROR;
    }

    njs_set_array(njs_prop_value(prop), argv);

    lhq.value = prop;
    lhq.key_hash = NJS_ARGV_HASH;
    lhq.key = njs_str_value("argv");
    lhq.replace = 1;
    lhq.pool = vm->mem_pool;
    lhq.proto = &njs_object_hash_proto;

    ret = njs_lvlhsh_insert(njs_object_hash(process), &lhq);

    if (njs_fast_path(ret == NJS_OK)) {
        njs_value_assign(retval, njs_prop_value(prop));
        return NJS_OK;
    }

    njs_internal_error(vm, "lvlhsh insert failed");

    return NJS_ERROR;
}


static njs_int_t
njs_env_hash_init(njs_vm_t *vm, njs_lvlhsh_t *hash, char **environment)
{
    char                **ep;
    u_char              *dst;
    ssize_t             length;
    uint32_t            cp;
    njs_int_t           ret;
    const u_char        *val, *entry, *s, *end;
    njs_object_prop_t   *prop, *prev;
    njs_string_prop_t   string;
    njs_lvlhsh_query_t  lhq;

    lhq.replace = 0;
    lhq.pool = vm->mem_pool;
    lhq.proto = &njs_object_hash_proto;

    ep = environment;

    while (*ep != NULL) {
        prop = njs_object_prop_alloc(vm, &njs_value_undefined,
                                     &njs_value_undefined, 1);
        if (njs_slow_path(prop == NULL)) {
            return NJS_ERROR;
        }

        entry = (u_char *) *ep++;

        val = njs_strchr(entry, '=');
        if (njs_slow_path(val == NULL)) {
            continue;
        }

        ret = njs_string_create(vm, &prop->name, (char *) entry, val - entry);
        if (njs_slow_path(ret != NJS_OK)) {
            return NJS_ERROR;
        }

        (void) njs_string_prop(&string, &prop->name);

        length = string.length;
        s = string.start;
        end = s + string.size;
        dst = (u_char *) s;

        while (length != 0) {
            cp = njs_utf8_upper_case(&s, end);
            dst = njs_utf8_encode(dst, cp);
            length--;
        }

        val++;

        ret = njs_string_create(vm, njs_prop_value(prop), (char *) val,
                                njs_strlen(val));
        if (njs_slow_path(ret != NJS_OK)) {
            return NJS_ERROR;
        }

        lhq.value = prop;
        njs_string_get(&prop->name, &lhq.key);
        lhq.key_hash = njs_djb_hash(lhq.key.start, lhq.key.length);

        ret = njs_lvlhsh_insert(hash, &lhq);
        if (njs_slow_path(ret != NJS_OK)) {
            if (ret == NJS_ERROR) {
                njs_internal_error(vm, "lvlhsh insert failed");
                return NJS_ERROR;
            }

            /* ret == NJS_DECLINED: entry already exists */

            /*
             * Always using the first element among the duplicates
             * and ignoring the rest.
             */

            prev = lhq.value;

            if (!njs_values_same(njs_prop_value(prop), njs_prop_value(prev))) {
                njs_vm_warn(vm, "environment variable \"%V\" has more than one"
                            " value\n", &lhq.key);
            }
        }
    }

    return NJS_OK;
}


static njs_int_t
njs_process_object_env(njs_vm_t *vm, njs_object_prop_t *pr,
    njs_value_t *process, njs_value_t *unused, njs_value_t *retval)
{
    njs_int_t           ret;
    njs_object_t        *env;
    njs_object_prop_t   *prop;
    njs_lvlhsh_query_t  lhq;

    static const njs_value_t  env_string = njs_string("env");

    env = njs_object_alloc(vm);
    if (njs_slow_path(env == NULL)) {
        return NJS_ERROR;
    }

    env->shared_hash = vm->shared->env_hash;

    prop = njs_object_prop_alloc(vm, &env_string, &njs_value_undefined, 1);
    if (njs_slow_path(prop == NULL)) {
        return NJS_ERROR;
    }

    njs_set_object(njs_prop_value(prop), env);

    lhq.replace = 1;
    lhq.pool = vm->mem_pool;
    lhq.proto = &njs_object_hash_proto;
    lhq.value = prop;
    lhq.key = njs_str_value("env");
    lhq.key_hash = NJS_ENV_HASH;

    ret = njs_lvlhsh_insert(njs_object_hash(process), &lhq);

    if (njs_fast_path(ret == NJS_OK)) {
        njs_value_assign(retval, njs_prop_value(prop));
        return NJS_OK;
    }

    njs_internal_error(vm, "lvlhsh insert failed");

    return NJS_ERROR;
}


static njs_int_t
njs_process_object_pid(njs_vm_t *vm, njs_object_prop_t *prop,
    njs_value_t *unused, njs_value_t *unused2, njs_value_t *retval)
{
    njs_set_number(retval, getpid());

    return NJS_OK;
}


static njs_int_t
njs_process_object_ppid(njs_vm_t *vm, njs_object_prop_t *prop,
    njs_value_t *unused, njs_value_t *unused2, njs_value_t *retval)
{
    njs_set_number(retval, getppid());

    return NJS_OK;
}


static const njs_object_prop_t  njs_process_object_properties[] =
{
    {
        .type = NJS_PROPERTY,
        .name = njs_wellknown_symbol(NJS_SYMBOL_TO_STRING_TAG),
        .u.value = njs_string("process"),
        .configurable = 1,
    },

    NJS_DECLARE_PROP_HANDLER("argv", njs_process_object_argv, 0, 0, 0),

    NJS_DECLARE_PROP_HANDLER("env", njs_process_object_env, 0, 0, 0),

    NJS_DECLARE_PROP_HANDLER("pid", njs_process_object_pid, 0, 0, 0),

    NJS_DECLARE_PROP_HANDLER("ppid", njs_process_object_ppid, 0, 0, 0),
};


static const njs_object_init_t  njs_process_object_init = {
    njs_process_object_properties,
    njs_nitems(njs_process_object_properties),
};


#if (NJS_TEST262)

static njs_int_t
njs_262_detach_array_buffer(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t unused)
{
    njs_value_t         *value;
    njs_array_buffer_t  *buffer;

    value = njs_arg(args, nargs, 1);
    if (njs_slow_path(!njs_is_array_buffer(value))) {
        njs_type_error(vm, "\"this\" is not an ArrayBuffer");
        return NJS_ERROR;
    }

    buffer = njs_array_buffer(value);
    buffer->u.data = NULL;
    buffer->size = 0;

    njs_set_null(&vm->retval);

    return NJS_OK;
}

static const njs_object_prop_t  njs_262_object_properties[] =
{
    {
        .type = NJS_PROPERTY,
        .name = njs_wellknown_symbol(NJS_SYMBOL_TO_STRING_TAG),
        .u.value = njs_string("$262"),
        .configurable = 1,
    },

    NJS_DECLARE_PROP_LNATIVE("detachArrayBuffer", njs_262_detach_array_buffer,
                             2, 0),
};


static const njs_object_init_t  njs_262_object_init = {
    njs_262_object_properties,
    njs_nitems(njs_262_object_properties),
};

#endif
