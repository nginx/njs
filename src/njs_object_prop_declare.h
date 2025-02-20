
/*
 * Copyright (C) Dmitry Volyntsev
 * Copyright (C) NGINX, Inc.
 */

#ifndef _NJS_OBJECT_PROP_DECLARE_H_INCLUDED_
#define _NJS_OBJECT_PROP_DECLARE_H_INCLUDED_

#define NJS_DECLARE_PROP_VALUE(_name, _v, _fl)                                \
    {                                                                         \
        .type = NJS_PROPERTY | NJS_PROPERTY_NOT_INIT,                         \
        .atom_id = njs_atom_ ## _name,                                        \
        .u.pvalue = &_v,                                                      \
        .enumerable = !!(_fl & NJS_OBJECT_PROP_ENUMERABLE),                   \
        .configurable = !!(_fl & NJS_OBJECT_PROP_CONFIGURABLE),               \
        .writable = !!(_fl & NJS_OBJECT_PROP_WRITABLE),                       \
    }


#define NJS_DECLARE_PROP_NATIVE(_name, _native, _nargs, _magic)               \
    NJS_DECLARE_PROP_VALUE(_name,                                             \
                           njs_native_function2(_native, _nargs, _magic),     \
                           NJS_OBJECT_PROP_VALUE_CW)


#define NJS_DECLARE_PROP_HANDLER(_name, _native, _m16, _fl)                   \
    {                                                                         \
        .type = NJS_PROPERTY_HANDLER | NJS_PROPERTY_NOT_INIT,                 \
        .atom_id = njs_atom_ ## _name,                                        \
        .u.pvalue = &njs_prop_handler2(_native, _m16),                        \
        .enumerable = !!(_fl & NJS_OBJECT_PROP_ENUMERABLE),                   \
        .configurable = !!(_fl & NJS_OBJECT_PROP_CONFIGURABLE),               \
        .writable = !!(_fl & NJS_OBJECT_PROP_WRITABLE),                       \
    }


#define NJS_DECLARE_PROP_GETTER(_name, _native, _magic)                       \
    {                                                                         \
        .type = NJS_ACCESSOR | NJS_PROPERTY_NOT_INIT,                         \
        .atom_id = njs_atom_ ## _name,                                        \
        .u.accessor = njs_getter(_native, _magic),                            \
        .writable = NJS_ATTRIBUTE_UNSET,                                      \
        .configurable = 1,                                                    \
    }


#define NJS_DECLARE_PROP_NAME(_name)                                          \
    NJS_DECLARE_PROP_VALUE(vs_name, njs_atom. _name, NJS_OBJECT_PROP_VALUE_C)


#define NJS_DECLARE_PROP_LENGTH(_v)                                           \
    NJS_DECLARE_PROP_VALUE(vs_length,                                         \
                           njs_value(NJS_NUMBER, !!(_v), _v),                 \
                           NJS_OBJECT_PROP_VALUE_C)


#endif /* _NJS_OBJECT_PROP_DECLARE_H_INCLUDED_ */
