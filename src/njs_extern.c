
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) NGINX, Inc.
 */


#include <njs_main.h>


static njs_int_t njs_external_prop_handler(njs_vm_t *vm,
    njs_object_prop_t *self, uint32_t atom_id, njs_value_t *value,
    njs_value_t *setval, njs_value_t *retval);


static njs_int_t
njs_external_add(njs_vm_t *vm, njs_arr_t *protos,
    const njs_external_t *external, njs_uint_t n)
{
    size_t                size;
    njs_int_t             ret;
    njs_value_t           prop_name;
    const u_char          *start;
    njs_flathsh_t         *hash;
    njs_function_t        *function;
    njs_object_prop_t     *prop;
    njs_exotic_slots_t    *slot, *next;
    njs_flathsh_query_t   lhq;
    const njs_external_t  *end;

    slot = njs_arr_add(protos);
    njs_memzero(slot, sizeof(njs_exotic_slots_t));

    hash = &slot->external_shared_hash;
    njs_lvlhsh_init(hash);

    if (n == 0) {
        return NJS_OK;
    }

    lhq.replace = 0;
    lhq.proto = &njs_object_hash_proto;
    lhq.pool = vm->mem_pool;

    end = external + n;

    while (external < end) {
        if ((external->flags & NJS_EXTERN_TYPE_MASK) == NJS_EXTERN_SELF) {
            slot->writable = external->u.object.writable;
            slot->configurable = external->u.object.configurable;
            slot->enumerable = external->u.object.enumerable;
            slot->prop_handler = external->u.object.prop_handler;
            slot->magic32 = external->u.object.magic32;
            slot->keys = external->u.object.keys;

            external++;
            continue;
        }

        prop = njs_object_prop_alloc(vm, &njs_value_invalid, 1);
        if (njs_slow_path(prop == NULL)) {
            goto memory_error;
        }

        prop->writable = external->writable;
        prop->configurable = external->configurable;
        prop->enumerable = external->enumerable;

        if (external->flags & NJS_EXTERN_SYMBOL) {
            lhq.key_hash = external->name.symbol;

        } else {
            ret = njs_atom_string_create(vm, &prop_name,
                                         external->name.string.start,
                                         external->name.string.length);
            if (njs_slow_path(ret != NJS_OK)) {
                return NJS_ERROR;
            }

            lhq.key_hash = prop_name.atom_id;
        }

        lhq.value = prop;

        switch (external->flags & NJS_EXTERN_TYPE_MASK) {
        case NJS_EXTERN_METHOD:
            function = njs_mp_zalloc(vm->mem_pool, sizeof(njs_function_t));
            if (njs_slow_path(function == NULL)) {
                goto memory_error;
            }

            function->object.shared_hash = vm->shared->arrow_instance_hash;
            function->object.type = NJS_FUNCTION;
            function->object.shared = 1;
            function->object.extensible = 1;
            function->native = 1;
            function->u.native = external->u.method.native;
            function->magic8 = external->u.method.magic8;
            function->ctor = external->u.method.ctor;

            njs_set_function(njs_prop_value(prop), function);

            break;

        case NJS_EXTERN_PROPERTY:
            if (external->u.property.handler != NULL) {
                prop->type = NJS_PROPERTY_HANDLER;
                prop->u.value.type = NJS_INVALID;
                prop->u.value.data.truth = 1;
                njs_prop_magic16(prop) = external->u.property.magic16;
                njs_prop_magic32(prop) = external->u.property.magic32;
                njs_prop_handler(prop) = external->u.property.handler;

            } else {
                start = (u_char *) external->u.property.value;
                size = njs_strlen(start);

                ret = njs_string_create(vm, &prop->u.value, start, size);
                if (njs_slow_path(ret != NJS_OK)) {
                    return NJS_ERROR;
                }
            }

            break;

        case NJS_EXTERN_OBJECT:
        default:
            next = njs_arr_item(protos, protos->items);

            ret = njs_external_add(vm, protos, external->u.object.properties,
                                   external->u.object.nproperties);
            if (njs_slow_path(ret != NJS_OK)) {
                return ret;
            }

            prop->type = NJS_PROPERTY_HANDLER;
            prop->u.value.type = NJS_INVALID;
            prop->u.value.data.truth = 1;
            njs_prop_magic16(prop) = next - slot;
            njs_prop_magic32(prop) = lhq.key_hash;
            njs_prop_handler(prop) = njs_external_prop_handler;

            if (external->u.object.prop_handler) {
                if (next->prop_handler) {
                    njs_internal_error(vm, "overwritten self prop_handler");
                    return NJS_ERROR;
                }

                next->writable = external->u.object.writable;
                next->configurable = external->u.object.configurable;
                next->enumerable = external->u.object.enumerable;

                next->prop_handler = external->u.object.prop_handler;
                next->magic32 = external->u.object.magic32;
            }

            if (external->u.object.keys) {
                if (next->keys) {
                    njs_internal_error(vm, "overwritten self keys");
                    return NJS_ERROR;
                }

                next->keys = external->u.object.keys;
            }

            break;
        }

        ret = njs_flathsh_unique_insert(hash, &lhq);
        if (njs_slow_path(ret != NJS_OK)) {
            njs_internal_error(vm, "lvlhsh insert failed");
            return NJS_ERROR;
        }

        external++;
    }

    return NJS_OK;

memory_error:

    njs_memory_error(vm);

    return NJS_ERROR;
}


static njs_int_t
njs_external_prop_handler(njs_vm_t *vm, njs_object_prop_t *self,
    uint32_t atom_id, njs_value_t *value, njs_value_t *setval,
    njs_value_t *retval)
{
    njs_int_t            ret;
    njs_object_prop_t    *prop;
    njs_external_ptr_t   external;
    njs_object_value_t   *ov;
    njs_exotic_slots_t   *slots;
    njs_flathsh_query_t  lhq;

    if (njs_slow_path(retval == NULL)) {
        return NJS_DECLINED;
    }

    slots = NULL;

    if (njs_slow_path(setval != NULL)) {
        *retval = *setval;

    } else {
        ov = njs_object_value_alloc(vm, NJS_OBJ_TYPE_OBJECT, 0, NULL);
        if (njs_slow_path(ov == NULL)) {
            return NJS_ERROR;
        }

        slots = njs_object(value)->slots + njs_prop_magic16(self);

        ov->object.shared_hash = slots->external_shared_hash;
        ov->object.slots = slots;

        external = njs_vm_external(vm, NJS_PROTO_ID_ANY, value);

        njs_set_data(&ov->value, external, njs_value_external_tag(value));
        njs_set_object_value(retval, ov);
    }

    prop = njs_object_prop_alloc(vm, retval, 1);
    if (njs_slow_path(prop == NULL)) {
        return NJS_ERROR;
    }

    prop->writable = self->writable;
    prop->configurable = self->configurable;
    prop->enumerable = self->enumerable;

    lhq.value = prop;
    lhq.key_hash = atom_id;
    lhq.replace = 1;
    lhq.pool = vm->mem_pool;
    lhq.proto = &njs_object_hash_proto;

    ret = njs_flathsh_unique_insert(njs_object_hash(value), &lhq);
    if (njs_slow_path(ret != NJS_OK)) {
        njs_internal_error(vm, "lvlhsh insert/replace failed");
        return NJS_ERROR;
    }

    return NJS_OK;
}


static njs_uint_t
njs_external_protos(const njs_external_t *external, njs_uint_t size)
{
    njs_uint_t  n;

    n = 1;

    while (size != 0) {
        if ((external->flags & 3) == NJS_EXTERN_OBJECT) {
            n += njs_external_protos(external->u.object.properties,
                                     external->u.object.nproperties);
        }

        size--;
        external++;
    }

    return n;
}


njs_int_t
njs_vm_external_prototype(njs_vm_t *vm, const njs_external_t *definition,
    njs_uint_t n)
{
    njs_arr_t   *protos, **pr;
    njs_int_t   ret;
    njs_uint_t  size;

    size = njs_external_protos(definition, n) + 1;

    protos = njs_arr_create(vm->mem_pool, size, sizeof(njs_exotic_slots_t));
    if (njs_slow_path(protos == NULL)) {
        njs_memory_error(vm);
        return -1;
    }

    ret = njs_external_add(vm, protos, definition, n);
    if (njs_slow_path(ret != NJS_OK)) {
        njs_internal_error(vm, "njs_vm_external_add() failed");
        return -1;
    }

    if (vm->protos == NULL) {
        vm->protos = njs_arr_create(vm->mem_pool, 4, sizeof(njs_arr_t *));
        if (njs_slow_path(vm->protos == NULL)) {
            return -1;
        }
    }

    pr = njs_arr_add(vm->protos);
    if (njs_slow_path(pr == NULL)) {
        return -1;
    }

    *pr = protos;

    return vm->protos->items - 1;
}


static njs_int_t
njs_vm_external_constructor_handler(njs_vm_t *vm, njs_object_prop_t *prop,
    uint32_t atom_id, njs_value_t *value, njs_value_t *setval,
    njs_value_t *retval)
{
    njs_set_function(retval, &njs_vm_ctor(vm, njs_prop_magic32(prop)));

    return NJS_OK;
}


njs_int_t
njs_vm_external_constructor(njs_vm_t *vm, const njs_str_t *name,
    const njs_function_native_t native, const njs_external_t *ctor_props,
    njs_uint_t ctor_nprops, const njs_external_t *proto_props,
    njs_uint_t proto_nprops)
{
    njs_int_t               ret, index, proto_id;
    njs_arr_t               **pprotos;
    njs_function_t          *constructor;
    njs_exotic_slots_t      *slots;
    njs_object_prototype_t  *prototype;

    index = njs_vm_ctor_push(vm);
    if (njs_slow_path(index < 0)) {
        njs_internal_error(vm, "njs_vm_ctor_push() failed");
        return -1;
    }

    proto_id = njs_vm_external_prototype(vm, proto_props, proto_nprops);
    if (njs_slow_path(proto_id < 0)) {
        njs_internal_error(vm, "njs_vm_external_prototype(proto_props) failed");
        return -1;
    }

    prototype = njs_shared_prototype(vm->shared, index);
    njs_memzero(prototype, sizeof(njs_object_prototype_t));
    prototype->object.type = NJS_OBJECT;
    prototype->object.extensible = 1;

    pprotos = njs_arr_item(vm->protos, proto_id);
    slots = (*pprotos)->start;
    prototype->object.shared_hash = slots->external_shared_hash;

    proto_id = njs_vm_external_prototype(vm, ctor_props, ctor_nprops);
    if (njs_slow_path(proto_id < 0)) {
        njs_internal_error(vm, "njs_vm_external_prototype(ctor_props) failed");
        return -1;
    }

    constructor = njs_shared_ctor(vm->shared, index);
    njs_memzero(constructor, sizeof(njs_function_t));
    constructor->object.type = NJS_FUNCTION;
    constructor->u.native = native;
    constructor->magic8 = index;
    constructor->native = 1;
    constructor->ctor = 1;

    pprotos = njs_arr_item(vm->protos, proto_id);
    slots = (*pprotos)->start;
    constructor->object.shared_hash = slots->external_shared_hash;

    ret = njs_vm_bind_handler(vm, name, njs_vm_external_constructor_handler, 0,
                              index, 1);
    if (njs_slow_path(ret != NJS_OK)) {
        return NJS_ERROR;
    }

    return index;
}


njs_int_t
njs_vm_external_create(njs_vm_t *vm, njs_value_t *value, njs_int_t proto_id,
    njs_external_ptr_t external, njs_bool_t shared)
{
    njs_arr_t           **pprotos;
    njs_object_value_t  *ov;
    njs_exotic_slots_t  *slots;

    if (vm->protos == NULL || (njs_int_t) vm->protos->items <= proto_id) {
        return NJS_ERROR;
    }

    ov = njs_object_value_alloc(vm, NJS_OBJ_TYPE_OBJECT, 0, NULL);
    if (njs_slow_path(ov == NULL)) {
        return NJS_ERROR;
    }

    pprotos = njs_arr_item(vm->protos, proto_id);
    slots = (*pprotos)->start;

    ov->object.shared_hash = slots->external_shared_hash;
    ov->object.shared = shared;
    ov->object.slots = slots;

    njs_set_object_value(value, ov);
    njs_set_data(&ov->value, external, njs_make_tag(proto_id));

    return NJS_OK;
}


njs_external_ptr_t
njs_vm_external(njs_vm_t *vm, njs_int_t proto_id, const njs_value_t *value)
{
    njs_external_ptr_t  external;

    if (njs_fast_path(njs_is_object_data(value, njs_make_tag(proto_id)))) {
        external = njs_object_data(value);
        if (external == NULL) {
            external = vm->external;
        }

        return external;
    }

    return NULL;
}


njs_int_t
njs_value_external_tag(const njs_value_t *value)
{
    if (njs_is_object_data(value, njs_make_tag(NJS_PROTO_ID_ANY))) {
        return njs_object_value(value)->data.magic32;
    }

    return -1;
}


njs_int_t
njs_external_property(njs_vm_t *vm, njs_object_prop_t *prop, uint32_t unused,
    njs_value_t *value, njs_value_t *setval, njs_value_t *retval)
{
    char        *p;
    njs_int_t   i;
    njs_uint_t  ui;

    p = njs_vm_external(vm, NJS_PROTO_ID_ANY, value);
    if (p == NULL) {
        njs_value_undefined_set(retval);
        return NJS_DECLINED;
    }

    switch (njs_vm_prop_magic16(prop)) {
    case NJS_EXTERN_TYPE_INT:
        i = *(njs_int_t *) (p + njs_vm_prop_magic32(prop));
        njs_value_number_set(retval, i);
        break;

    case NJS_EXTERN_TYPE_UINT:
        ui = *(njs_uint_t *) (p + njs_vm_prop_magic32(prop));
        njs_value_number_set(retval, ui);
        break;

    case NJS_EXTERN_TYPE_VALUE:
    default:
        njs_value_assign(retval,
                         (njs_value_t *) (p + njs_vm_prop_magic32(prop)));

    }

    return NJS_OK;
}
