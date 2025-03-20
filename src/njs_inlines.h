#ifndef _NJS_VALUE_PROP_H_INCLUDED_
#define _NJS_VALUE_PROP_H_INCLUDED_


njs_inline NJS_NOSANITIZE("float-cast-overflow") njs_bool_t
njs_number_is_small_index(double num)
{
    uint32_t  u32;

    u32 = num;

    return (u32 == num && u32 < 0x80000000);
}


/*
 * value is always key: string or number or symbol.
 *
 * symbol always contians atom_id by construction. do nothing;
 * number if short number it is atomized by "| 0x80000000";
 * string if represents short number it is atomized by "| 0x80000000";
 *
 * for string and symbol atom_ids common range is uint32_t < 0x80000000.
 */
njs_inline njs_int_t
njs_atom_atomize_key(njs_vm_t *vm, njs_value_t *value)
{
    double             num;
    uint32_t           hash_id;
    njs_int_t          ret;
    njs_value_t        val_str;
    const njs_value_t  *entry;

    switch (value->type) {
    case NJS_NUMBER:
        num = value->data.u.number;
        if (njs_fast_path(njs_number_is_small_index(num)))
        {
            value->atom_id = ((uint32_t) num) | 0x80000000;
            return NJS_OK;

        } else {
            /* convert num to string, and atomize it. */
            ret = njs_number_to_string(vm, &val_str, value);
            if (ret != NJS_OK) {
                return ret;
            }

            if (val_str.atom_id == 0) {
                hash_id = njs_djb_hash(val_str.string.data->start,
                                       val_str.string.data->size);

                entry = njs_lexer_keyword_find(vm, val_str.string.data->start,
                                               val_str.string.data->size,
                                               val_str.string.data->length,
                                               hash_id);
                if (njs_slow_path(entry == NULL)) {
                    return NJS_ERROR;
                }

                njs_mp_free(vm->mem_pool, val_str.string.data->start);

                value->atom_id = entry->atom_id;

            } else {
                value->atom_id = val_str.atom_id;
            }
        }
        break;
    case NJS_STRING:
        num = njs_key_to_index(value);
        if (njs_fast_path(njs_key_is_integer_index(num, value)) &&
            ((uint32_t) num) < 0x80000000)
        {
            value->atom_id = ((uint32_t) num) | 0x80000000;

        } else {
            hash_id = njs_djb_hash(value->string.data->start,
                                   value->string.data->size);

            entry = njs_lexer_keyword_find(vm, value->string.data->start,
                                           value->string.data->size,
                                           value->string.data->length,
                                           hash_id);
            if (njs_slow_path(entry == NULL)) {
                return NJS_ERROR;
            }

            if (value->string.data->size != 0) {
                njs_mp_free(vm->mem_pool, value->string.data->start);
            }

            *value = *entry;
        }
        break;

    default:
        /* NJS_SYMBOL: do nothing. */
        ;
    }

    return NJS_OK;
}


njs_inline njs_int_t
njs_atom_atomize_key_s(njs_vm_t *vm, njs_value_t *value)
{
    njs_int_t            ret;
    njs_flathsh_query_t  lhq;

    lhq.replace = 0;
    lhq.proto = &njs_lexer_hash_proto; // &njs_atom_hash_proto;
    lhq.pool = vm->atom_hash_mem_pool;


    value->string.atom_id = (*vm->atom_hash_atom_id)++;

    if (value->type == NJS_SYMBOL) {
        lhq.key_hash = value->string.atom_id;

        lhq.value = (void *) value;

        ret = njs_flathsh_insert(vm->atom_hash, &lhq);
        if (njs_slow_path(ret != NJS_OK)) {
            njs_internal_error(vm, "flathsh insert/replace failed");
            return NJS_ERROR;
        }
    }


    return NJS_OK;
}


njs_inline njs_int_t
njs_flathsh_obj_find1(const njs_flathsh_obj_t *fh, njs_flathsh_obj_query_t *fhq)
{
    njs_int_t            cell_num, elt_num;
    njs_flathsh_elt_t    *e, *elts;
    njs_flathsh_descr_t  *h;
    h = fh->slot;
    if (njs_slow_path(h == NULL)) {
        return NJS_DECLINED;
    }
    cell_num = fhq->key_hash & h->hash_mask;
    elt_num = njs_hash_cells_end(h)[-cell_num - 1];
    elts = njs_hash_elts(h);
    while (elt_num != 0) {
        e = &elts[elt_num - 1];
        if (njs_fast_path(e->key_hash == fhq->key_hash)) {
            fhq->value = (njs_value_t *)e->value;
            return NJS_OK;
        }
        elt_num = e->next_elt;
    }
    return NJS_DECLINED;
}


njs_inline njs_int_t
njs_flathsh_obj_insert2(njs_flathsh_obj_t *fh, njs_flathsh_obj_query_t *fhq)
{
    njs_int_t                cell_num;
    njs_flathsh_obj_elt_t    *elt, *elts;
    njs_flathsh_obj_descr_t  *h;

    h = fh->slot;
    if (njs_slow_path(h == NULL)) {
        h = njs_flathsh_obj_new(fhq);
        if (h == NULL) {
            return NJS_ERROR;
        }
        fh->slot = h;
    }

    /* below is optimized njs_flathsh_add_elt(fh, fhq); */

    if (njs_slow_path(h->elts_count == h->elts_size)) {
        h = njs_flathsh_obj_expand_elts(fhq, h);
        if (njs_slow_path(h == NULL)) {
            return NJS_ERROR;
        }

        fh->slot = h;
    }

    elts = njs_flathsh_obj_hash_elts(h);
    elt = &elts[h->elts_count++];

    elt->value = fhq->value;
    elt->key_hash = fhq->key_hash;

    cell_num = fhq->key_hash & h->hash_mask;
    uint32_t *p = &(njs_flathsh_obj_hash_cells_end(h)[-cell_num - 1]);
    elt->next_elt = *p;
    *p = h->elts_count;

    return NJS_OK;
}


/*
 * ES5.1, 8.12.1: [[GetOwnProperty]], [[GetProperty]].
 * The njs_property_query() returns values
 *   NJS_OK               property has been found in object,
 *     retval of type njs_object_prop_t * is in pq->lhq.value.
 *     in NJS_PROPERTY_QUERY_GET
 *       prop->type is NJS_PROPERTY or NJS_PROPERTY_HANDLER.
 *     in NJS_PROPERTY_QUERY_SET, NJS_PROPERTY_QUERY_DELETE
 *       prop->type is NJS_PROPERTY, NJS_PROPERTY_REF, NJS_PROPERTY_PLACE_REF,
 *       NJS_PROPERTY_TYPED_ARRAY_REF or
 *       NJS_PROPERTY_HANDLER.
 *   NJS_DECLINED         property was not found in object,
 *     if pq->lhq.value != NULL it contains retval of type
 *     njs_object_prop_t * where prop->type is NJS_WHITEOUT
 *   NJS_ERROR            exception has been thrown.
 */
njs_inline njs_int_t
njs_property_query(njs_vm_t *vm, njs_property_query_t *pq, njs_value_t *value,
    njs_value_t *key)
{
    double          num;
    uint32_t        index;
    njs_int_t       ret;
    njs_object_t    *obj;
    njs_function_t  *function;

    njs_assert(njs_is_index_or_key(key));

    switch (value->type) {
    case NJS_BOOLEAN:
    case NJS_NUMBER:
    case NJS_SYMBOL:
        index = njs_primitive_prototype_index(value->type);
        obj = njs_vm_proto(vm, index);
        break;

    case NJS_STRING:
        if (njs_fast_path(!njs_is_null_or_undefined_or_boolean(key))) {
            num = njs_key_to_index(key);
            if (njs_fast_path(njs_key_is_integer_index(num, key))) {
                return njs_string_property_query(vm, pq, value, num);
            }
        }

        obj = &vm->string_object;
        break;

    case NJS_OBJECT:
    case NJS_ARRAY:
    case NJS_ARRAY_BUFFER:
    case NJS_DATA_VIEW:
    case NJS_TYPED_ARRAY:
    case NJS_REGEXP:
    case NJS_DATE:
    case NJS_PROMISE:
    case NJS_OBJECT_VALUE:
        obj = njs_object(value);
        break;

    case NJS_FUNCTION:
        function = njs_function_value_copy(vm, value);
        if (njs_slow_path(function == NULL)) {
            return NJS_ERROR;
        }

        obj = &function->object;
        break;

    case NJS_UNDEFINED:
    case NJS_NULL:
    default:
        ret = njs_primitive_value_to_string(vm, &pq->key, key);

        if (njs_fast_path(ret == NJS_OK)) {
            njs_string_get(&pq->key, &pq->lhq.key);
            njs_type_error(vm, "cannot get property \"%V\" of %s",
                           &pq->lhq.key, njs_is_null(value) ? "null"
                                                            : "undefined");
            return NJS_ERROR;
        }

        njs_type_error(vm, "cannot get property \"unknown\" of %s",
                       njs_is_null(value) ? "null" : "undefined");

        return NJS_ERROR;
    }

    ret = njs_primitive_value_to_key(vm, &pq->key, key);

    if (njs_fast_path(ret == NJS_OK)) {
        if (njs_is_symbol(key)) {
            pq->lhq.key_hash = njs_symbol_key(key);

        } else {
            njs_string_get(&pq->key, &pq->lhq.key);

            if (key->atom_id == 0) {
                ret = njs_atom_atomize_key(vm, key);
                if (ret != NJS_OK) {
                    return ret;
                }
            }
            pq->lhq.key_hash = key->atom_id;
        }

        ret = njs_object_property_query(vm, pq, obj, key);

        if (njs_slow_path(ret == NJS_DECLINED && obj->slots != NULL)) {
            njs_int_t ret = njs_external_property_query(vm, pq, value);
            return ret;
        }
    }

    return ret;
}


njs_inline njs_int_t
njs_object_property_query(njs_vm_t *vm, njs_property_query_t *pq,
    njs_object_t *object, njs_value_t *key)
{
    double              num;
    njs_int_t           ret;
    njs_bool_t          own;
    njs_array_t         *array;
    njs_object_t        *proto;
    njs_object_prop_t   *prop;
    njs_typed_array_t   *tarray;
    njs_object_value_t  *ov;

    pq->lhq.proto = &njs_object_hash_proto;

    own = pq->own;
    pq->own = 1;

    proto = object;

    do {
        switch (proto->type) {
        case NJS_ARRAY:
            array = (njs_array_t *) proto;
            num = njs_key_to_index(key);

            if (njs_fast_path(njs_key_is_integer_index(num, key))) {
                ret = njs_array_property_query(vm, pq, array, num);
                if (njs_fast_path(ret != NJS_DECLINED)) {
                    return (ret == NJS_DONE) ? NJS_DECLINED : ret;
                }
            }

            break;

        case NJS_TYPED_ARRAY:
            num = njs_key_to_index(key);
            if (njs_fast_path(njs_key_is_integer_index(num, key))) {
                tarray = (njs_typed_array_t *) proto;
                return njs_typed_array_property_query(vm, pq, tarray, num);
            }

            if (!isnan(num)) {
                return NJS_DECLINED;
            }

            break;

        case NJS_OBJECT_VALUE:
            ov = (njs_object_value_t *) proto;
            if (!njs_is_string(&ov->value)) {
                break;
            }

            num = njs_key_to_index(key);
            if (njs_fast_path(njs_key_is_integer_index(num, key))) {
                ov = (njs_object_value_t *) proto;
                ret = njs_string_property_query(vm, pq, &ov->value, num);
                if (njs_fast_path(ret != NJS_DECLINED)) {
                    return ret;
                }
            }

            break;

        default:
            break;
        }

        if (key->atom_id == 0) {
            ret = njs_atom_atomize_key(vm, key);
            if (ret != NJS_OK) {
                return ret;
            }
        }
        pq->lhq.key_hash = key->atom_id;

        ret = njs_flathsh_obj_find1(&proto->hash, &pq->lhq);

        if (ret == NJS_OK) {
            prop = pq->lhq.value;

            if (prop->type != NJS_WHITEOUT) {
                return ret;
            }

            if (pq->own) {
                pq->own_whiteout = &proto->hash;
            }

        } else {
            ret = njs_flathsh_obj_find1(&proto->shared_hash, &pq->lhq);
            if (ret == NJS_OK) {
                return njs_prop_private_copy(vm, pq, proto);
            }
        }

        if (own) {
            return NJS_DECLINED;
        }

        pq->own = 0;
        proto = proto->__proto__;

    } while (proto != NULL);

    return NJS_DECLINED;
}


#endif /* _NJS_VALUE_PROP_H_INCLUDED_ */
