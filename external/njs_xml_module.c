
/*
 * Copyright (C) Dmitry Volyntsev
 * Copyright (C) NGINX, Inc.
 */


#include <njs.h>
#include <string.h>
#include <libxml/parser.h>
#include <libxml/tree.h>
#include <libxml/c14n.h>
#include <libxml/xpathInternals.h>


typedef struct {
    xmlDoc         *doc;
    xmlParserCtxt  *ctx;
} njs_xml_doc_t;


typedef enum {
    XML_NSET_TREE = 0,
    XML_NSET_TREE_NO_COMMENTS,
    XML_NSET_TREE_INVERT,
} njs_xml_nset_type_t;


typedef struct njs_xml_nset_s  njs_xml_nset_t;

struct njs_xml_nset_s {
    xmlNodeSet           *nodes;
    xmlDoc               *doc;
    njs_xml_nset_type_t  type;
    njs_xml_nset_t       *next;
    njs_xml_nset_t       *prev;
};

static njs_int_t njs_xml_ext_parse(njs_vm_t *vm, njs_value_t *args,
    njs_uint_t nargs, njs_index_t unused, njs_value_t *retval);
static njs_int_t njs_xml_ext_canonicalization(njs_vm_t *vm, njs_value_t *args,
    njs_uint_t nargs, njs_index_t magic, njs_value_t *retval);
static njs_int_t njs_xml_doc_ext_prop_keys(njs_vm_t *vm, njs_value_t *value,
    njs_value_t *keys);
static njs_int_t njs_xml_doc_ext_root(njs_vm_t *vm, njs_object_prop_t *prop,
    uint32_t atom_id, njs_value_t *value, njs_value_t *unused,
    njs_value_t *retval);
static njs_int_t njs_xml_node_ext_prop_keys(njs_vm_t *vm, njs_value_t *value,
    njs_value_t *keys);
static njs_int_t njs_xml_node_ext_prop_handler(njs_vm_t *vm,
    njs_object_prop_t *prop, uint32_t atom_id, njs_value_t *value,
    njs_value_t *unused, njs_value_t *retval);
static njs_int_t njs_xml_attr_ext_prop_keys(njs_vm_t *vm, njs_value_t *value,
    njs_value_t *keys);
static njs_int_t njs_xml_attr_ext_prop_handler(njs_vm_t *vm,
    njs_object_prop_t *prop, uint32_t atom_id, njs_value_t *value,
    njs_value_t *unused, njs_value_t *retval);
static njs_int_t njs_xml_node_ext_add_child(njs_vm_t *vm, njs_value_t *args,
    njs_uint_t nargs, njs_index_t unused, njs_value_t *retval);
static njs_int_t njs_xml_node_ext_attrs(njs_vm_t *vm, njs_object_prop_t *prop,
    uint32_t unused, njs_value_t *value, njs_value_t *setval,
    njs_value_t *retval);
static njs_int_t njs_xml_node_ext_name(njs_vm_t *vm, njs_object_prop_t *prop,
    uint32_t unused, njs_value_t *value, njs_value_t *setval,
    njs_value_t *retval);
static njs_int_t njs_xml_node_ext_ns(njs_vm_t *vm, njs_object_prop_t *prop,
    uint32_t unused, njs_value_t *value, njs_value_t *setval,
    njs_value_t *retval);
static njs_int_t njs_xml_node_ext_parent(njs_vm_t *vm, njs_object_prop_t *prop,
    uint32_t unused, njs_value_t *value, njs_value_t *setval,
    njs_value_t *retval);
static njs_int_t njs_xml_node_ext_remove_all_attributes(njs_vm_t *vm,
    njs_value_t *args, njs_uint_t nargs, njs_index_t unused,
    njs_value_t *retval);
static njs_int_t njs_xml_node_ext_remove_attribute(njs_vm_t *vm,
    njs_value_t *args, njs_uint_t nargs, njs_index_t unused,
    njs_value_t *retval);
static njs_int_t njs_xml_node_ext_remove_children(njs_vm_t *vm,
    njs_value_t *args, njs_uint_t nargs, njs_index_t unused,
    njs_value_t *retval);
static njs_int_t njs_xml_node_ext_remove_text(njs_vm_t *vm,
    njs_value_t *args, njs_uint_t nargs, njs_index_t unused,
    njs_value_t *retval);
static njs_int_t njs_xml_node_ext_set_attribute(njs_vm_t *vm,
    njs_value_t *args, njs_uint_t nargs, njs_index_t unused,
    njs_value_t *retval);
static njs_int_t njs_xml_node_ext_set_text(njs_vm_t *vm,
    njs_value_t *args, njs_uint_t nargs, njs_index_t unused,
    njs_value_t *retval);
static njs_int_t njs_xml_node_ext_tags(njs_vm_t *vm, njs_object_prop_t *prop,
    uint32_t unused, njs_value_t *value, njs_value_t *setval,
    njs_value_t *retval);
static njs_int_t njs_xml_node_ext_text(njs_vm_t *vm, njs_object_prop_t *prop,
    uint32_t unused, njs_value_t *value, njs_value_t *setval,
    njs_value_t *retval);

static njs_int_t njs_xml_node_attr_handler(njs_vm_t *vm, xmlNode *current,
    njs_str_t *name, njs_value_t *setval, njs_value_t *retval);
static njs_int_t njs_xml_node_tag_remove(njs_vm_t *vm, njs_value_t *value,
    njs_str_t *name);
static njs_int_t njs_xml_node_tag_handler(njs_vm_t *vm, njs_value_t *value,
    njs_str_t *name, njs_value_t *setval, njs_value_t *retval);
static njs_int_t njs_xml_node_tags_handler(njs_vm_t *vm, njs_value_t *value,
    njs_str_t *name, njs_value_t *setval, njs_value_t *retval);

static xmlNode *njs_xml_external_node(njs_vm_t *vm, njs_value_t *value);
static const u_char *njs_xml_string_to_c_string(njs_vm_t *vm, njs_str_t *str,
    u_char *dst, size_t size);
static njs_bool_t njs_xml_node_is_live(xmlNode *node);
static xmlNode *njs_xml_list_detach(xmlNode *parent, njs_str_t *name);
static njs_int_t njs_xml_list_retire(njs_vm_t *vm, xmlNode *parent,
    njs_str_t *name);
static void njs_xml_list_append(xmlNode *parent, xmlNode *node);
static njs_bool_t njs_xml_tree_has_namespaces(xmlNode *node);
static void njs_xml_node_cleanup(void *data);
static void njs_xml_doc_cleanup(void *data);

static njs_xml_nset_t *njs_xml_nset_create(njs_vm_t *vm, xmlDoc *doc,
    xmlNode *current, njs_xml_nset_type_t type);
static njs_xml_nset_t *njs_xml_nset_add(njs_xml_nset_t *nset,
    njs_xml_nset_t *add);
static void njs_xml_nset_cleanup(void *data);
static void njs_xml_error(njs_vm_t *vm, njs_xml_doc_t *tree, const char *fmt,
    ...);
static njs_int_t njs_xml_init(njs_vm_t *vm);


static njs_external_t  njs_ext_xml[] = {

    {
        .flags = NJS_EXTERN_PROPERTY | NJS_EXTERN_SYMBOL,
        .name.symbol = NJS_SYMBOL_TO_STRING_TAG,
        .u.property = {
            .value = "xml",
        }
    },

    {
        .flags = NJS_EXTERN_METHOD,
        .name.string = njs_str("parse"),
        .writable = 1,
        .configurable = 1,
        .u.method = {
            .native = njs_xml_ext_parse,
            .magic8 = 0,
        }
    },

    {
        .flags = NJS_EXTERN_METHOD,
        .name.string = njs_str("c14n"),
        .writable = 1,
        .configurable = 1,
        .enumerable = 1,
        .u.method = {
            .native = njs_xml_ext_canonicalization,
        }
    },

    {
        .flags = NJS_EXTERN_METHOD,
        .name.string = njs_str("exclusiveC14n"),
        .writable = 1,
        .configurable = 1,
        .enumerable = 1,
        .u.method = {
            .native = njs_xml_ext_canonicalization,
            .magic8 = 1,
        }
    },

    {
        .flags = NJS_EXTERN_METHOD,
        .name.string = njs_str("serialize"),
        .writable = 1,
        .configurable = 1,
        .enumerable = 1,
        .u.method = {
            .native = njs_xml_ext_canonicalization,
        }
    },

    {
        .flags = NJS_EXTERN_METHOD,
        .name.string = njs_str("serializeToString"),
        .writable = 1,
        .configurable = 1,
        .enumerable = 1,
        .u.method = {
            .native = njs_xml_ext_canonicalization,
            .magic8 = 2,
        }
    },

};


static njs_external_t  njs_ext_xml_doc[] = {

    {
        .flags = NJS_EXTERN_PROPERTY | NJS_EXTERN_SYMBOL,
        .name.symbol = NJS_SYMBOL_TO_STRING_TAG,
        .u.property = {
            .value = "XMLDoc",
        }
    },

    {
        .flags = NJS_EXTERN_SELF,
        .u.object = {
            .enumerable = 1,
            .prop_handler = njs_xml_doc_ext_root,
            .keys = njs_xml_doc_ext_prop_keys,
        }
    },

    {
        .flags = NJS_EXTERN_PROPERTY,
        .name.string = njs_str("$root"),
        .enumerable = 1,
        .u.property = {
            .handler = njs_xml_doc_ext_root,
            .magic32 = 1,
        }
    },

};


static njs_external_t  njs_ext_xml_node[] = {

    {
        .flags = NJS_EXTERN_PROPERTY | NJS_EXTERN_SYMBOL,
        .name.symbol = NJS_SYMBOL_TO_STRING_TAG,
        .u.property = {
            .value = "XMLNode",
        }
    },

    {
        .flags = NJS_EXTERN_SELF,
        .u.object = {
            .enumerable = 1,
            .writable = 1,
            .configurable = 1,
            .prop_handler = njs_xml_node_ext_prop_handler,
            .keys = njs_xml_node_ext_prop_keys,
        }
    },

    {
        .flags = NJS_EXTERN_METHOD,
        .name.string = njs_str("addChild"),
        .writable = 1,
        .configurable = 1,
        .enumerable = 1,
        .u.method = {
            .native = njs_xml_node_ext_add_child,
        }
    },

    {
        .flags = NJS_EXTERN_PROPERTY,
        .name.string = njs_str("$attrs"),
        .enumerable = 1,
        .u.property = {
            .handler = njs_xml_node_ext_attrs,
        }
    },

    {
        .flags = NJS_EXTERN_PROPERTY,
        .name.string = njs_str("$name"),
        .enumerable = 1,
        .u.property = {
            .handler = njs_xml_node_ext_name,
        }
    },

    {
        .flags = NJS_EXTERN_PROPERTY,
        .name.string = njs_str("$ns"),
        .enumerable = 1,
        .u.property = {
            .handler = njs_xml_node_ext_ns,
        }
    },

    {
        .flags = NJS_EXTERN_PROPERTY,
        .name.string = njs_str("$parent"),
        .enumerable = 1,
        .u.property = {
            .handler = njs_xml_node_ext_parent,
        }
    },

    {
        .flags = NJS_EXTERN_METHOD,
        .name.string = njs_str("removeAllAttributes"),
        .writable = 1,
        .configurable = 1,
        .enumerable = 1,
        .u.method = {
            .native = njs_xml_node_ext_remove_all_attributes,
        }
    },

    {
        .flags = NJS_EXTERN_METHOD,
        .name.string = njs_str("removeAttribute"),
        .writable = 1,
        .configurable = 1,
        .enumerable = 1,
        .u.method = {
            .native = njs_xml_node_ext_remove_attribute,
        }
    },

    {
        .flags = NJS_EXTERN_METHOD,
        .name.string = njs_str("removeChildren"),
        .writable = 1,
        .configurable = 1,
        .enumerable = 1,
        .u.method = {
            .native = njs_xml_node_ext_remove_children,
        }
    },

    {
        .flags = NJS_EXTERN_METHOD,
        .name.string = njs_str("removeText"),
        .writable = 1,
        .configurable = 1,
        .enumerable = 1,
        .u.method = {
            .native = njs_xml_node_ext_remove_text,
        }
    },

    {
        .flags = NJS_EXTERN_METHOD,
        .name.string = njs_str("setAttribute"),
        .writable = 1,
        .configurable = 1,
        .enumerable = 1,
        .u.method = {
            .native = njs_xml_node_ext_set_attribute,
        }
    },

    {
        .flags = NJS_EXTERN_METHOD,
        .name.string = njs_str("setText"),
        .writable = 1,
        .configurable = 1,
        .enumerable = 1,
        .u.method = {
            .native = njs_xml_node_ext_set_text,
        }
    },

    {
        .flags = NJS_EXTERN_PROPERTY,
        .name.string = njs_str("$tags"),
        .enumerable = 1,
        .writable = 1,
        .configurable = 1,
        .u.property = {
            .handler = njs_xml_node_ext_tags,
        }
    },

    {
        .flags = NJS_EXTERN_PROPERTY,
        .name.string = njs_str("$text"),
        .enumerable = 1,
        .writable = 1,
        .configurable = 1,
        .u.property = {
            .handler = njs_xml_node_ext_text,
        }
    },

};


static njs_external_t  njs_ext_xml_attr[] = {

    {
        .flags = NJS_EXTERN_PROPERTY | NJS_EXTERN_SYMBOL,
        .name.symbol = NJS_SYMBOL_TO_STRING_TAG,
        .u.property = {
            .value = "XMLAttr",
        }
    },

    {
        .flags = NJS_EXTERN_SELF,
        .u.object = {
            .enumerable = 1,
            .prop_handler = njs_xml_attr_ext_prop_handler,
            .keys = njs_xml_attr_ext_prop_keys,
        }
    },

};


njs_module_t  njs_xml_module = {
    .name = njs_str("xml"),
    .preinit = NULL,
    .init = njs_xml_init,
};


static njs_int_t    njs_xml_doc_proto_id;
static njs_int_t    njs_xml_node_proto_id;
static njs_int_t    njs_xml_attr_proto_id;


static njs_int_t
njs_xml_ext_parse(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t unused, njs_value_t *retval)
{
    njs_int_t         ret;
    njs_str_t         data;
    njs_xml_doc_t     *tree;
    njs_mp_cleanup_t  *cln;

    ret = njs_vm_value_to_bytes(vm, &data, njs_arg(args, nargs, 1));
    if (njs_slow_path(ret != NJS_OK)) {
        return NJS_ERROR;
    }

    tree = njs_mp_zalloc(njs_vm_memory_pool(vm), sizeof(njs_xml_doc_t));
    if (njs_slow_path(tree == NULL)) {
        njs_vm_memory_error(vm);
        return NJS_ERROR;
    }

    tree->ctx = xmlNewParserCtxt();
    if (njs_slow_path(tree->ctx == NULL)) {
        njs_vm_internal_error(vm, "xmlNewParserCtxt() failed");
        return NJS_ERROR;
    }

    tree->doc = xmlCtxtReadMemory(tree->ctx, (char *) data.start, data.length,
                                  NULL, NULL, XML_PARSE_NOWARNING
                                              | XML_PARSE_NOERROR);
    if (njs_slow_path(tree->doc == NULL)) {
        njs_xml_error(vm, tree, "failed to parse XML");
        return NJS_ERROR;
    }

    cln = njs_mp_cleanup_add(njs_vm_memory_pool(vm), 0);
    if (njs_slow_path(cln == NULL)) {
        njs_vm_memory_error(vm);
        return NJS_ERROR;
    }

    cln->handler = njs_xml_doc_cleanup;
    cln->data = tree;

    return njs_vm_external_create(vm, retval, njs_xml_doc_proto_id,
                                  tree, 0);
}


static njs_int_t
njs_xml_doc_ext_prop_keys(njs_vm_t *vm, njs_value_t *value, njs_value_t *keys)
{
    xmlNode        *node;
    njs_int_t      ret;
    njs_value_t    *push;
    njs_xml_doc_t  *tree;

    tree = njs_vm_external(vm, njs_xml_doc_proto_id, value);
    if (njs_slow_path(tree == NULL)) {
        njs_value_undefined_set(keys);
        return NJS_DECLINED;
    }

    ret = njs_vm_array_alloc(vm, keys, 2);
    if (njs_slow_path(ret != NJS_OK)) {
        return NJS_ERROR;
    }

    for (node = xmlDocGetRootElement(tree->doc);
         node != NULL;
         node = node->next)
    {
        if (node->type != XML_ELEMENT_NODE) {
            continue;
        }

        push = njs_vm_array_push(vm, keys);
        if (njs_slow_path(push == NULL)) {
            return NJS_ERROR;
        }

        ret = njs_vm_value_string_create(vm, push, node->name,
                                         njs_strlen(node->name));
        if (njs_slow_path(ret != NJS_OK)) {
            return NJS_ERROR;
        }
    }

    return NJS_OK;
}


static njs_int_t
njs_xml_doc_ext_root(njs_vm_t *vm, njs_object_prop_t *prop, uint32_t atom_id,
     njs_value_t *value, njs_value_t *unused, njs_value_t *retval)
{
    xmlNode        *node;
    njs_int_t      ret;
    njs_str_t      name;
    njs_bool_t     any;
    njs_xml_doc_t  *tree;

    tree = njs_vm_external(vm, njs_xml_doc_proto_id, value);
    if (njs_slow_path(tree == NULL)) {
        njs_value_undefined_set(retval);
        return NJS_DECLINED;
    }

    any = njs_vm_prop_magic32(prop);

    if (!any) {
        ret = njs_vm_prop_name(vm, atom_id, &name);
        if (njs_slow_path(ret != NJS_OK)) {
            njs_value_undefined_set(retval);
            return NJS_DECLINED;
        }

    } else {
        /* To suppress warning. */
        name.length = 0;
        name.start = NULL;
    }

    for (node = xmlDocGetRootElement(tree->doc);
         node != NULL;
         node = node->next)
    {
        if (node->type != XML_ELEMENT_NODE) {
            continue;
        }

        if (!any) {
            if (name.length != njs_strlen(node->name)
                || njs_strncmp(name.start, node->name, name.length) != 0)
            {
                continue;
            }
        }

        return njs_vm_external_create(vm, retval, njs_xml_node_proto_id, node,
                                      0);
    }

    njs_value_undefined_set(retval);

    return NJS_DECLINED;
}


static njs_int_t
njs_xml_node_ext_prop_keys(njs_vm_t *vm, njs_value_t *value, njs_value_t *keys)
{
    xmlNode      *node, *current;
    njs_int_t    ret;
    njs_value_t  *push;

    current = njs_vm_external(vm, njs_xml_node_proto_id, value);
    if (njs_slow_path(current == NULL)) {
        njs_value_undefined_set(keys);
        return NJS_DECLINED;
    }

    ret = njs_vm_array_alloc(vm, keys, 2);
    if (njs_slow_path(ret != NJS_OK)) {
        return NJS_ERROR;
    }

    if (current->name != NULL && current->type == XML_ELEMENT_NODE) {
        push = njs_vm_array_push(vm, keys);
        if (njs_slow_path(push == NULL)) {
            return NJS_ERROR;
        }

        ret = njs_vm_value_string_create(vm, push, (u_char *) "$name",
                                         njs_length("$name"));
        if (njs_slow_path(ret != NJS_OK)) {
            return NJS_ERROR;
        }
    }

    if (current->ns != NULL) {
        push = njs_vm_array_push(vm, keys);
        if (njs_slow_path(push == NULL)) {
            return NJS_ERROR;
        }

        ret = njs_vm_value_string_create(vm, push, (u_char *) "$ns",
                                         njs_length("$ns"));
        if (njs_slow_path(ret != NJS_OK)) {
            return NJS_ERROR;
        }
    }

    if (current->properties != NULL) {
        push = njs_vm_array_push(vm, keys);
        if (njs_slow_path(push == NULL)) {
            return NJS_ERROR;
        }

        ret = njs_vm_value_string_create(vm, push, (u_char *) "$attrs",
                                         njs_length("$attrs"));
        if (njs_slow_path(ret != NJS_OK)) {
            return NJS_ERROR;
        }
    }

    if (current->children != NULL && current->children->content != NULL) {
        push = njs_vm_array_push(vm, keys);
        if (njs_slow_path(push == NULL)) {
            return NJS_ERROR;
        }

        ret = njs_vm_value_string_create(vm, push, (u_char *) "$text",
                                         njs_length("$text"));
        if (njs_slow_path(ret != NJS_OK)) {
            return NJS_ERROR;
        }
    }

    for (node = current->children; node != NULL; node = node->next) {
        if (node->type != XML_ELEMENT_NODE) {
            continue;
        }

        push = njs_vm_array_push(vm, keys);
        if (njs_slow_path(push == NULL)) {
            return NJS_ERROR;
        }

        ret = njs_vm_value_string_create(vm, push, (u_char *) "$tags",
                                         njs_length("$tags"));
        if (njs_slow_path(ret != NJS_OK)) {
            return NJS_ERROR;
        }

        break;
    }

    return NJS_OK;
}


static njs_int_t
njs_xml_node_ext_prop_handler(njs_vm_t *vm, njs_object_prop_t *prop,
    uint32_t atom_id, njs_value_t *value, njs_value_t *setval,
    njs_value_t *retval)
{
    xmlNode    *current;
    njs_int_t  ret;
    njs_str_t  name;

    /*
     * $tag$foo - the first tag child with the name "foo"
     * $tags$foo - the all children with the name "foo" as an array
     * $attr$foo - the attribute with the name "foo"
     * foo - the same as $tag$foo
     */

    current = njs_vm_external(vm, njs_xml_node_proto_id, value);
    if (njs_slow_path(current == NULL)) {
        njs_value_undefined_set(retval);
        return NJS_DECLINED;
    }

    ret = njs_vm_prop_name(vm, atom_id, &name);
    if (njs_slow_path(ret != NJS_OK)) {
        njs_value_undefined_set(retval);
        return NJS_DECLINED;
    }

    if (name.length > 1 && name.start[0] == '$') {
        if (name.length > njs_length("$attr$")
            && njs_strncmp(&name.start[1], "attr$", njs_length("attr$")) == 0)
        {
            name.length -= njs_length("$attr$");
            name.start += njs_length("$attr$");

            return njs_xml_node_attr_handler(vm, current, &name, setval,
                                             retval);
        }

        if (name.length > njs_length("$tag$")
            && njs_strncmp(&name.start[1], "tag$", njs_length("tag$")) == 0)
        {
            name.length -= njs_length("$tag$");
            name.start += njs_length("$tag$");

            return njs_xml_node_tag_handler(vm, value, &name, setval, retval);
        }

        if (name.length >= njs_length("$tags$")
            && njs_strncmp(&name.start[1], "tags$", njs_length("tags$")) == 0)
        {
            name.length -= njs_length("$tags$");
            name.start += njs_length("$tags$");

            return njs_xml_node_tags_handler(vm, value, &name, setval, retval);
        }
    }

    return njs_xml_node_tag_handler(vm, value, &name, setval, retval);
}


static njs_int_t
njs_xml_node_ext_add_child(njs_vm_t *vm, njs_value_t *args,
    njs_uint_t nargs, njs_index_t unused, njs_value_t *retval)
{
    xmlNode  *current, *node;

    current = njs_vm_external(vm, njs_xml_node_proto_id, njs_argument(args, 0));
    if (njs_slow_path(!njs_xml_node_is_live(current))) {
        njs_vm_type_error(vm, "\"this\" is not a XMLNode object");
        return NJS_ERROR;
    }

    node = njs_xml_external_node(vm, njs_arg(args, nargs, 1));
    if (njs_slow_path(node == NULL)) {
        njs_vm_type_error(vm, "node is not a XMLNode object");
        return NJS_ERROR;
    }

    if (njs_slow_path(njs_xml_tree_has_namespaces(node))) {
        njs_vm_type_error(vm, "XMLNode has namespaces");
        return NJS_ERROR;
    }

    node = xmlDocCopyNode(node, current->doc, 1);
    if (njs_slow_path(node == NULL)) {
        njs_vm_internal_error(vm, "xmlDocCopyNode() failed");
        return NJS_ERROR;
    }

    njs_xml_list_append(current, node);

    njs_value_undefined_set(retval);

    return NJS_OK;
}


static njs_int_t
njs_xml_node_ext_attrs(njs_vm_t *vm, njs_object_prop_t *prop, uint32_t unused,
    njs_value_t *value, njs_value_t *setval, njs_value_t *retval)
{
    xmlNode  *current;

    current = njs_vm_external(vm, njs_xml_node_proto_id, value);
    if (njs_slow_path(current == NULL || current->properties == NULL)) {
        njs_value_undefined_set(retval);
        return NJS_DECLINED;
    }

    return njs_vm_external_create(vm, retval, njs_xml_attr_proto_id, current,
                                  0);
}


static njs_int_t
njs_xml_node_ext_name(njs_vm_t *vm, njs_object_prop_t *prop, uint32_t unused,
     njs_value_t *value, njs_value_t *setval, njs_value_t *retval)
{
    xmlNode  *current;

    current = njs_vm_external(vm, njs_xml_node_proto_id, value);
    if (current == NULL || current->type != XML_ELEMENT_NODE) {
        njs_value_undefined_set(retval);
        return NJS_DECLINED;
    }

    return njs_vm_value_string_create(vm, retval, current->name,
                                      njs_strlen(current->name));
}


static njs_int_t
njs_xml_node_ext_ns(njs_vm_t *vm, njs_object_prop_t *prop, uint32_t unused,
    njs_value_t *value, njs_value_t *setval, njs_value_t *retval)
{
    xmlNode  *current;

    current = njs_vm_external(vm, njs_xml_node_proto_id, value);
    if (njs_slow_path(current == NULL || current->ns == NULL)) {
        njs_value_undefined_set(retval);
        return NJS_DECLINED;
    }

    return njs_vm_value_string_create(vm, retval, current->ns->href,
                                      njs_strlen(current->ns->href));
}


static njs_int_t
njs_xml_node_ext_parent(njs_vm_t *vm, njs_object_prop_t *prop, uint32_t unused,
    njs_value_t *value, njs_value_t *setval, njs_value_t *retval)
{
    xmlNode  *current;

    current = njs_vm_external(vm, njs_xml_node_proto_id, value);
    if (njs_slow_path(current == NULL
                      || current->parent == NULL
                      || current->parent->type != XML_ELEMENT_NODE))
    {
        njs_value_undefined_set(retval);
        return NJS_DECLINED;
    }

    return njs_vm_external_create(vm, retval, njs_xml_node_proto_id,
                                  current->parent, 0);
}


static njs_int_t
njs_xml_node_ext_remove_attribute(njs_vm_t *vm, njs_value_t *args,
    njs_uint_t nargs, njs_index_t unused, njs_value_t *retval)
{
    return njs_xml_node_ext_set_attribute(vm, args, nargs, 1, retval);
}


static njs_int_t
njs_xml_node_ext_remove_all_attributes(njs_vm_t *vm,
    njs_value_t *args, njs_uint_t nargs, njs_index_t unused,
    njs_value_t *retval)
{
    xmlNode  *current;

    current = njs_vm_external(vm, njs_xml_node_proto_id, njs_argument(args, 0));
    if (njs_slow_path(!njs_xml_node_is_live(current))) {
        njs_vm_type_error(vm, "\"this\" is not a XMLNode object");
        return NJS_ERROR;
    }

    if (current->properties != NULL) {
        xmlFreePropList(current->properties);
        current->properties = NULL;
    }

    njs_value_undefined_set(retval);

    return NJS_OK;
}


static njs_int_t
njs_xml_node_ext_remove_children(njs_vm_t *vm, njs_value_t *args,
    njs_uint_t nargs, njs_index_t unused, njs_value_t *retval)
{
    xmlNode      *current;
    njs_str_t    name;
    njs_value_t  *selector;

    current = njs_vm_external(vm, njs_xml_node_proto_id, njs_argument(args, 0));
    if (njs_slow_path(!njs_xml_node_is_live(current))) {
        njs_vm_type_error(vm, "\"this\" is not a XMLNode object");
        return NJS_ERROR;
    }

    selector = njs_arg(args, nargs, 1);

    njs_value_undefined_set(retval);

    if (!njs_value_is_null_or_undefined(selector)) {
        if (njs_slow_path(!njs_value_is_string(selector))) {
            njs_vm_type_error(vm, "selector is not a string");
            return NJS_ERROR;
        }

        njs_value_string_get(vm, selector, &name);

        return njs_xml_node_tag_remove(vm, njs_argument(args, 0), &name);
    }

    /* all. */

    if (current->children == NULL) {
        return NJS_OK;
    }

    return njs_xml_list_retire(vm, current, NULL);
}


static njs_int_t
njs_xml_node_ext_remove_text(njs_vm_t *vm, njs_value_t *args,
    njs_uint_t nargs, njs_index_t unused, njs_value_t *retval)
{
    return njs_xml_node_ext_text(vm, NULL, 0, njs_argument(args, 0), NULL, NULL);
}


static njs_int_t
njs_xml_node_ext_set_attribute(njs_vm_t *vm, njs_value_t *args,
    njs_uint_t nargs, njs_index_t remove, njs_value_t *retval)
{
    xmlNode      *current;
    njs_str_t    str;
    njs_value_t  *name;

    current = njs_vm_external(vm, njs_xml_node_proto_id, njs_argument(args, 0));
    if (njs_slow_path(current == NULL)) {
        njs_vm_type_error(vm, "\"this\" is not a XMLNode object");
        return NJS_ERROR;
    }

    name = njs_arg(args, nargs, 1);

    if (njs_slow_path(!njs_value_is_string(name))) {
        njs_vm_type_error(vm, "name is not a string");
        return NJS_ERROR;
    }

    njs_value_string_get(vm, name, &str);

    return njs_xml_node_attr_handler(vm, current, &str, njs_arg(args, nargs, 2),
                                     !remove ? retval : NULL);
}


static njs_int_t
njs_xml_node_ext_set_text(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t unused, njs_value_t *retval)
{
    return njs_xml_node_ext_text(vm, NULL, 0, njs_argument(args, 0),
                                 njs_arg(args, nargs, 1), retval);
}


static njs_int_t
njs_xml_node_ext_tags(njs_vm_t *vm, njs_object_prop_t *prop, uint32_t unused,
     njs_value_t *value, njs_value_t *setval, njs_value_t *retval)
{
    xmlNode    *current;
    njs_str_t  name;

    current = njs_vm_external(vm, njs_xml_node_proto_id, value);
    if (njs_slow_path(current == NULL)) {
        njs_value_undefined_set(retval);
        return NJS_DECLINED;
    }

    if (retval != NULL && current->children == NULL && setval == NULL) {
        njs_value_undefined_set(retval);
        return NJS_DECLINED;
    }

    name.start = NULL;
    name.length = 0;

    return njs_xml_node_tags_handler(vm, value, &name, setval, retval);
}


static njs_int_t
njs_xml_node_ext_text(njs_vm_t *vm, njs_object_prop_t *unused, uint32_t unused1,
    njs_value_t *value, njs_value_t *setval, njs_value_t *retval)
{
    xmlNode    *current, *text;
    njs_int_t  ret;
    njs_str_t  content;

    current = njs_vm_external(vm, njs_xml_node_proto_id, value);
    if (njs_slow_path(current == NULL)) {
        njs_value_undefined_set(retval);
        return NJS_DECLINED;
    }

    if (retval != NULL && setval == NULL) {
        content.start = xmlNodeGetContent(current);
        ret = njs_vm_value_string_create(vm, retval, content.start,
                                         njs_strlen(content.start));

        xmlFree(content.start);

        return ret;
    }

    if (njs_slow_path(!njs_xml_node_is_live(current))) {
        njs_vm_type_error(vm, "XMLNode is detached");
        return NJS_ERROR;
    }

    /* set or delete. */

    content.start = NULL;
    content.length = 0;

    if (retval != NULL
        && (setval != NULL && !njs_value_is_null_or_undefined(setval)))
    {
        if (njs_slow_path(!njs_value_is_string(setval))) {
            njs_vm_type_error(vm, "setval is not a string");
            return NJS_ERROR;
        }

        njs_value_string_get(vm, setval, &content);

        if (njs_slow_path(memchr(content.start, '\0', content.length) != NULL)) {
            njs_vm_type_error(vm, "setval contains NUL");
            return NJS_ERROR;
        }
    }

    text = NULL;

    if (retval != NULL && setval != NULL) {
        text = xmlNewDocTextLen(current->doc, content.start, content.length);
        if (njs_slow_path(text == NULL)) {
            njs_vm_internal_error(vm, "xmlNewDocTextLen() failed");
            return NJS_ERROR;
        }
    }

    if (current->children != NULL) {
        if (njs_slow_path(njs_xml_list_retire(vm, current, NULL) != NJS_OK)) {
            xmlFreeNode(text);
            return NJS_ERROR;
        }
    }

    if (text != NULL) {
        njs_xml_list_append(current, text);
    }

    if (retval != NULL) {
        njs_value_undefined_set(retval);
    }

    return NJS_OK;
}


static njs_int_t
njs_xml_node_attr_handler(njs_vm_t *vm, xmlNode *current, njs_str_t *name,
    njs_value_t *setval, njs_value_t *retval)
{
    size_t        size;
    njs_int_t     ret;
    njs_str_t     str;
    xmlAttr       *attr;
    const u_char  *content, *name_c, *value;
    u_char        name_buf[512], value_buf[1024];

    if (retval != NULL && setval == NULL) {
        /* get. */

        for (attr = current->properties; attr != NULL; attr = attr->next) {
            if (attr->type != XML_ATTRIBUTE_NODE) {
                continue;
            }

            size = njs_strlen(attr->name);

            if (name->length != size
                || njs_strncmp(name->start, attr->name, size) != 0)
            {
                continue;
            }

            if (attr->children != NULL
                && attr->children->next == NULL
                && attr->children->type == XML_TEXT_NODE)
            {
                content = (const u_char *) attr->children->content;

                return njs_vm_value_string_create(vm, retval, content,
                                                  njs_strlen(content));
            }
        }

        njs_value_undefined_set(retval);

        return NJS_DECLINED;
    }

    /* set or delete. */

    if (njs_slow_path(!njs_xml_node_is_live(current))) {
        njs_vm_type_error(vm, "XMLNode is detached");
        return NJS_ERROR;
    }

    name_c = njs_xml_string_to_c_string(vm, name, &name_buf[0],
                                        sizeof(name_buf));
    if (njs_slow_path(name_c == NULL)) {
        return NJS_ERROR;
    }

    ret = xmlValidateQName(name_c, 0);
    if (njs_slow_path(ret != 0)) {
        njs_vm_type_error(vm, "attribute name \"%V\" is not valid", name);
        return NJS_ERROR;
    }

    if (retval == NULL
        || (setval != NULL && njs_value_is_null_or_undefined(setval)))
    {
        /* delete. */

        attr = xmlHasProp(current, name_c);

        if (attr != NULL) {
            xmlRemoveProp(attr);
        }

        return NJS_OK;
    }

    if (njs_slow_path(!njs_value_is_string(setval))) {
        njs_vm_type_error(vm, "setval is not a string");
        return NJS_ERROR;
    }

    njs_value_string_get(vm, setval, &str);

    value = njs_xml_string_to_c_string(vm, &str, &value_buf[0],
                                       sizeof(value_buf));
    if (njs_slow_path(value == NULL)) {
        return NJS_ERROR;
    }

    attr = xmlSetProp(current, name_c, value);
    if (njs_slow_path(attr == NULL)) {
        njs_vm_internal_error(vm, "xmlSetProp() failed");
        return NJS_ERROR;
    }

    njs_value_undefined_set(retval);

    return NJS_OK;
}


static njs_int_t
njs_xml_node_tag_remove(njs_vm_t *vm, njs_value_t *value, njs_str_t *name)
{
    xmlNode  *current, *node;

    current = njs_vm_external(vm, njs_xml_node_proto_id, value);

    if (njs_slow_path(!njs_xml_node_is_live(current))) {
        njs_vm_type_error(vm, "XMLNode is detached");
        return NJS_ERROR;
    }

    for (node = current->children; node != NULL; node = node->next) {
        if (node->type == XML_ELEMENT_NODE
            && name->length == njs_strlen(node->name)
            && njs_strncmp(name->start, node->name, name->length) == 0)
        {
            break;
        }
    }

    if (node == NULL) {
        return NJS_OK;
    }

    return njs_xml_list_retire(vm, current, name);
}


static njs_int_t
njs_xml_node_tag_handler(njs_vm_t *vm, njs_value_t *value, njs_str_t *name,
    njs_value_t *setval, njs_value_t *retval)
{
    size_t   size;
    xmlNode  *current, *node;

    current = njs_vm_external(vm, njs_xml_node_proto_id, value);

    if (retval != NULL && setval == NULL) {

        /* get. */

        for (node = current->children; node != NULL; node = node->next) {
            if (node->type != XML_ELEMENT_NODE) {
                continue;
            }

            size = njs_strlen(node->name);

            if (name->length != size
                || njs_strncmp(name->start, node->name, size) != 0)
            {
                continue;
            }

            return njs_vm_external_create(vm, retval, njs_xml_node_proto_id,
                                          node, 0);
        }

        njs_value_undefined_set(retval);

        return NJS_DECLINED;
    }

    if (retval != NULL) {
        njs_vm_type_error(vm, "XMLNode.$tag$xxx is not assignable, "
                          "use addChild() or node.$tags = [node1, node2, ..] "
                          "syntax");
        return NJS_ERROR;
    }

    /* delete. */

    return njs_xml_node_tag_remove(vm, value, name);
}


static njs_int_t
njs_xml_node_tags_handler(njs_vm_t *vm, njs_value_t *value, njs_str_t *name,
    njs_value_t *setval, njs_value_t *retval)
{
    size_t              size;
    xmlNode             *current, *first, *last, *node;
    int64_t             i, length;
    njs_int_t           ret;
    njs_value_t         *push;
    njs_opaque_value_t  *start;

    current = njs_vm_external(vm, njs_xml_node_proto_id, value);

    first = NULL;
    last = NULL;

    if (retval != NULL && setval == NULL) {

        /* get. */

        ret = njs_vm_array_alloc(vm, retval, 2);
        if (njs_slow_path(ret != NJS_OK)) {
            return NJS_ERROR;
        }

        for (node = current->children; node != NULL; node = node->next) {
            if (node->type != XML_ELEMENT_NODE) {
                continue;
            }

            size = njs_strlen(node->name);

            if (name->length > 0
                && (name->length != size
                    || njs_strncmp(name->start, node->name, size) != 0))
            {
                continue;
            }

            push = njs_vm_array_push(vm, retval);
            if (njs_slow_path(push == NULL)) {
                return NJS_ERROR;
            }

            ret = njs_vm_external_create(vm, push, njs_xml_node_proto_id,
                                         node, 0);
            if (njs_slow_path(ret != NJS_OK)) {
                return NJS_ERROR;
            }
        }

        return NJS_OK;
    }

    if (name->length > 0) {
        njs_vm_type_error(vm, "XMLNode $tags$xxx is not assignable, use "
                          "addChild() or node.$tags = [node1, node2, ..] "
                          "syntax");
        return NJS_ERROR;
    }

    /* set or delete. */

    if (retval == NULL) {
        /* delete. */
        if (!njs_xml_node_is_live(current)) {
            njs_vm_type_error(vm, "XMLNode is detached");
            return NJS_ERROR;
        }

        if (current->children == NULL) {
            return NJS_OK;
        }

        if (njs_slow_path(njs_xml_list_retire(vm, current, NULL) != NJS_OK)) {
            return NJS_ERROR;
        }

        return NJS_OK;
    }

    if (!njs_value_is_array(setval)) {
        njs_vm_type_error(vm, "setval is not an array");
        goto error;
    }

    if (njs_slow_path(!njs_xml_node_is_live(current))) {
        njs_vm_type_error(vm, "XMLNode is detached");
        return NJS_ERROR;
    }

    start = (njs_opaque_value_t *) njs_vm_array_start(vm, setval);
    if (njs_slow_path(start == NULL)) {
        goto error;
    }

    (void) njs_vm_array_length(vm, setval, &length);

    for (i = 0; i < length; i++) {
        node = njs_xml_external_node(vm, njs_value_arg(start++));
        if (njs_slow_path(node == NULL)) {
            njs_vm_type_error(vm, "setval[%D] is not a XMLNode object", i);
            goto error;
        }

        if (njs_slow_path(njs_xml_tree_has_namespaces(node))) {
            njs_vm_type_error(vm, "setval[%D] has namespaces", i);
            goto error;
        }

        node = xmlDocCopyNode(node, current->doc, 1);
        if (njs_slow_path(node == NULL)) {
            njs_vm_internal_error(vm, "xmlDocCopyNode() failed");
            goto error;
        }

        node->parent = NULL;
        node->prev = last;
        node->next = NULL;

        if (last != NULL) {
            last->next = node;

        } else {
            first = node;
        }

        last = node;
    }

    if (current->children != NULL) {
        if (njs_slow_path(njs_xml_list_retire(vm, current, NULL) != NJS_OK)) {
            goto error;
        }
    }

    current->children = first;
    current->last = last;

    for (node = first; node != NULL; node = node->next) {
        node->parent = current;
    }

    njs_value_undefined_set(retval);

    return NJS_OK;

error:

    xmlFreeNodeList(first);

    return NJS_ERROR;
}


static xmlNode *
njs_xml_external_node(njs_vm_t *vm, njs_value_t *value)
{
    xmlNode        *current;
    njs_xml_doc_t  *tree;

    current = njs_vm_external(vm, njs_xml_node_proto_id, value);
    if (njs_slow_path(current == NULL)) {
        tree = njs_vm_external(vm, njs_xml_doc_proto_id, value);
        if (njs_slow_path(tree == NULL)) {
            njs_vm_type_error(vm, "\"this\" is not a XMLNode object");
            return NULL;
        }

        current = xmlDocGetRootElement(tree->doc);
        if (njs_slow_path(current == NULL)) {
            njs_vm_type_error(vm, "\"this\" is not a XMLNode object");
            return NULL;
        }
    }

    return current;
}


static const u_char *
njs_xml_string_to_c_string(njs_vm_t *vm, njs_str_t *str, u_char *dst,
    size_t size)
{
    u_char  *p;

    if (str->length >= size) {
        dst = njs_mp_alloc(njs_vm_memory_pool(vm), str->length + 1);
        if (njs_slow_path(dst == NULL)) {
            njs_vm_memory_error(vm);
            return NULL;
        }
    }

    p = njs_cpymem(dst, str->start, str->length);
    *p = '\0';

    return dst;
}


static njs_bool_t
njs_xml_node_is_live(xmlNode *node)
{
    xmlNode  *parent;

    if (node == NULL || node->doc == NULL) {
        return 0;
    }

    for (parent = node->parent; parent != NULL; parent = parent->parent) {
        if (parent == (xmlNode *) node->doc) {
            return 1;
        }
    }

    return 0;
}


static xmlNode *
njs_xml_list_detach(xmlNode *parent, njs_str_t *name)
{
    xmlNode  *node, *next, *first, *last, *keep_first, *keep_last;

    if (name == NULL) {
        first = parent->children;
        parent->children = NULL;
        parent->last = NULL;

        for (node = first; node != NULL; node = node->next) {
            node->parent = NULL;
        }

        return first;
    }

    first = NULL;
    last = NULL;
    keep_first = NULL;
    keep_last = NULL;

    for (node = parent->children; node != NULL; node = next) {
        next = node->next;

        if (node->type == XML_ELEMENT_NODE
            && name->length == njs_strlen(node->name)
            && njs_strncmp(name->start, node->name, name->length) == 0)
        {
            node->parent = NULL;
            node->prev = last;
            node->next = NULL;

            if (last != NULL) {
                last->next = node;

            } else {
                first = node;
            }

            last = node;

        } else {
            node->parent = parent;
            node->prev = keep_last;
            node->next = NULL;

            if (keep_last != NULL) {
                keep_last->next = node;

            } else {
                keep_first = node;
            }

            keep_last = node;
        }
    }

    parent->children = keep_first;
    parent->last = keep_last;

    return first;
}


static njs_int_t
njs_xml_list_retire(njs_vm_t *vm, xmlNode *parent, njs_str_t *name)
{
    njs_mp_cleanup_t  *cln;

    cln = njs_mp_cleanup_add(njs_vm_memory_pool(vm), 0);
    if (njs_slow_path(cln == NULL)) {
        njs_vm_memory_error(vm);
        return NJS_ERROR;
    }

    cln->handler = njs_xml_node_cleanup;
    cln->data = njs_xml_list_detach(parent, name);

    return NJS_OK;
}


static void
njs_xml_list_append(xmlNode *parent, xmlNode *node)
{
    node->parent = parent;
    node->prev = parent->last;
    node->next = NULL;

    if (parent->last != NULL) {
        parent->last->next = node;

    } else {
        parent->children = node;
    }

    parent->last = node;
}


static njs_bool_t
njs_xml_tree_has_namespaces(xmlNode *node)
{
    xmlAttr  *attr;
    xmlNode  *child;

    switch (node->type) {
    case XML_ELEMENT_NODE:
        break;

    case XML_TEXT_NODE:
    case XML_CDATA_SECTION_NODE:
    case XML_PI_NODE:
    case XML_COMMENT_NODE:
        return 0;

    default:
        return 1;
    }

    if (node->ns != NULL || node->nsDef != NULL) {
        return 1;
    }

    for (attr = node->properties; attr != NULL; attr = attr->next) {
        if (attr->ns != NULL) {
            return 1;
        }
    }

    for (child = node->children; child != NULL; child = child->next) {
        if (njs_xml_tree_has_namespaces(child)) {
            return 1;
        }
    }

    return 0;
}


static void
njs_xml_node_cleanup(void *data)
{
    xmlFreeNodeList(data);
}


static void
njs_xml_doc_cleanup(void *data)
{
    njs_xml_doc_t  *current;

    current = data;

    xmlFreeDoc(current->doc);
    xmlFreeParserCtxt(current->ctx);
}


static int
njs_xml_buf_write_cb(void *context, const char *buffer, int len)
{
    njs_chb_t  *chain = context;

    njs_chb_append(chain, buffer, len);

    return chain->error ? -1 : len;
}


static int
njs_xml_node_one_contains(njs_xml_nset_t *nset, xmlNode *node, xmlNode *parent)
{
    int    in;
    xmlNs  ns;

    if (nset->type == XML_NSET_TREE_NO_COMMENTS
        && node->type == XML_COMMENT_NODE)
    {
        return 0;
    }

    in = 1;

    if (nset->nodes != NULL) {
        if (node->type != XML_NAMESPACE_DECL) {
            in = xmlXPathNodeSetContains(nset->nodes, node);

        } else {

            memcpy(&ns, node, sizeof(ns));

            /* libxml2 workaround, check xpath.c for details */

            if ((parent != NULL) && (parent->type == XML_ATTRIBUTE_NODE)) {
                ns.next = (xmlNs *) parent->parent;

            } else {
                ns.next = (xmlNs *) parent;
            }

            in = xmlXPathNodeSetContains(nset->nodes, (xmlNode *) &ns);
        }
    }

    switch (nset->type) {
    case XML_NSET_TREE:
    case XML_NSET_TREE_NO_COMMENTS:
        if (in != 0) {
            return 1;
        }

        if ((parent != NULL) && (parent->type == XML_ELEMENT_NODE)) {
            return njs_xml_node_one_contains(nset, parent, parent->parent);
        }

        return 0;

    case XML_NSET_TREE_INVERT:
    default:
        if (in != 0) {
            return 0;
        }

        if ((parent != NULL) && (parent->type == XML_ELEMENT_NODE)) {
            return njs_xml_node_one_contains(nset, parent, parent->parent);
        }
    }

    return 1;
}


static int
njs_xml_c14n_visibility_cb(void *user_data, xmlNode *node, xmlNode *parent)
{
    int             status;
    njs_xml_nset_t  *n, *nset;

    nset = user_data;

    if (nset == NULL) {
        return 1;
    }

    status = 1;

    n = nset;

    do {
        if (status && !njs_xml_node_one_contains(n, node, parent)) {
            status = 0;
        }

        n = n->next;
    } while (n != nset);

    return status;
}


/* the C14N prefix list is a whitespace delimited list */
#define njs_xml_ns_sep(c)                                                     \
    ((c) == ' ' || (c) == '\t' || (c) == '\n' || (c) == '\r')


static u_char **
njs_xml_parse_ns_list(njs_vm_t *vm, njs_str_t *src)
{
    size_t  n;
    u_char  *p, *end, *dst, **buf, **out;

    /*
     * The pointers and the writable copy of the list are allocated as a single
     * block, the copy follows the NULL terminated array of pointers to it.
     * The tokens are counted exactly the way the list is split below.
     */

    n = 0;
    end = src->start + src->length;

    for (p = src->start; p < end && *p != '\0'; /* void */) {
        if (njs_xml_ns_sep(*p)) {
            p++;
            continue;
        }

        n++;

        while (p < end && *p != '\0' && !njs_xml_ns_sep(*p)) {
            p++;
        }
    }

    if (njs_slow_path(n >= (SIZE_MAX - src->length - 1) / sizeof(u_char *))) {
        njs_vm_memory_error(vm);
        return NULL;
    }

    buf = njs_mp_alloc(njs_vm_memory_pool(vm),
                       (n + 1) * sizeof(u_char *) + src->length + 1);
    if (njs_slow_path(buf == NULL)) {
        njs_vm_memory_error(vm);
        return NULL;
    }

    dst = (u_char *) &buf[n + 1];

    memcpy(dst, src->start, src->length);
    dst[src->length] = '\0';

    out = buf;
    p = dst;

    while (*p != '\0') {
        if (njs_xml_ns_sep(*p)) {
            *p++ = '\0';
            continue;
        }

        *out++ = p;

        while (*p != '\0' && !njs_xml_ns_sep(*p)) {
            p++;
        }
    }

    *out = NULL;

    return buf;
}


static njs_int_t
njs_xml_ext_canonicalization(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t magic, njs_value_t *retval)
{
    u_char           **prefix_list;
    ssize_t          size;
    xmlNode          *node, *current;
    njs_int_t        ret;
    njs_str_t        data, string;
    njs_chb_t        chain;
    njs_bool_t       comments;
    njs_value_t      *excluding, *prefixes;
    njs_xml_nset_t   *nset, *children;
    xmlOutputBuffer  *buf;

    current = njs_xml_external_node(vm, njs_argument(args, 1));
    if (njs_slow_path(current == NULL)) {
        return NJS_ERROR;
    }

    if (njs_slow_path(!njs_xml_node_is_live(current))) {
        njs_vm_type_error(vm, "XMLNode is detached");
        return NJS_ERROR;
    }

    comments = njs_value_bool(njs_arg(args, nargs, 3));

    excluding = njs_arg(args, nargs, 2);

    if (!njs_value_is_null_or_undefined(excluding)) {
        node = njs_vm_external(vm, njs_xml_node_proto_id, excluding);
        if (njs_slow_path(node == NULL)) {
            njs_vm_type_error(vm, "\"excluding\" argument is not a XMLNode "
                              "object");
            return NJS_ERROR;
        }

        if (njs_slow_path(node->doc != current->doc
                          || !njs_xml_node_is_live(node)))
        {
            njs_vm_type_error(vm, "\"excluding\" is not in XMLNode tree");
            return NJS_ERROR;
        }

        nset = njs_xml_nset_create(vm, current->doc, current,
                                   XML_NSET_TREE_NO_COMMENTS);
        if (njs_slow_path(nset == NULL)) {
            return NJS_ERROR;
        }

        children = njs_xml_nset_create(vm, node->doc, node,
                                       XML_NSET_TREE_INVERT);
        if (njs_slow_path(children == NULL)) {
            return NJS_ERROR;
        }

        nset = njs_xml_nset_add(nset, children);

    } else {
        nset = njs_xml_nset_create(vm, current->doc, current,
                                   comments ? XML_NSET_TREE
                                            : XML_NSET_TREE_NO_COMMENTS);
        if (njs_slow_path(nset == NULL)) {
            return NJS_ERROR;
        }
    }

    prefix_list = NULL;
    prefixes = njs_arg(args, nargs, 4);

    if (!njs_value_is_null_or_undefined(prefixes)) {
        if (!njs_value_is_string(prefixes)) {
            njs_vm_type_error(vm, "\"prefixes\" argument is not a string");
            return NJS_ERROR;
        }

        ret = njs_vm_value_string(vm, &string, prefixes);
        if (njs_slow_path(ret != NJS_OK)) {
            return NJS_ERROR;
        }

        prefix_list = njs_xml_parse_ns_list(vm, &string);
        if (njs_slow_path(prefix_list == NULL)) {
            return NJS_ERROR;
        }
    }

    NJS_CHB_MP_INIT(&chain, njs_vm_memory_pool(vm));

    buf = xmlOutputBufferCreateIO(njs_xml_buf_write_cb, NULL, &chain, NULL);
    if (njs_slow_path(buf == NULL)) {
        njs_vm_internal_error(vm, "xmlOutputBufferCreateIO() failed");
        return NJS_ERROR;
    }

    ret = xmlC14NExecute(current->doc, njs_xml_c14n_visibility_cb, nset,
                         magic & 0x1 ? XML_C14N_EXCLUSIVE_1_0 : XML_C14N_1_0,
                         prefix_list, comments, buf);

    if (njs_slow_path(ret < 0)) {
        njs_vm_internal_error(vm, "xmlC14NExecute() failed");
        ret = NJS_ERROR;
        goto error;
    }

    if (magic & 0x2) {
        ret = njs_vm_value_string_create_chb(vm, retval, &chain);

    } else {
        size = njs_chb_size(&chain);
        if (njs_slow_path(size < 0)) {
            njs_vm_memory_error(vm);
            ret = NJS_ERROR;
            goto error;
        }

        ret = njs_chb_join(&chain, &data);
        if (njs_slow_path(ret != NJS_OK)) {
            ret = NJS_ERROR;
            goto error;
        }

        ret = njs_vm_value_buffer_set(vm, retval, data.start,
                                      data.length);
    }

error:

    (void) xmlOutputBufferClose(buf);

    njs_chb_destroy(&chain);

    return ret;
}


static njs_int_t
njs_xml_attr_ext_prop_keys(njs_vm_t *vm, njs_value_t *value, njs_value_t *keys)
{
    xmlAttr      *node;
    xmlNode      *current;
    njs_int_t    ret;
    njs_value_t  *push;

    current = njs_vm_external(vm, njs_xml_attr_proto_id, value);
    if (njs_slow_path(current == NULL)) {
        njs_value_undefined_set(keys);
        return NJS_DECLINED;
    }

    ret = njs_vm_array_alloc(vm, keys, 2);
    if (njs_slow_path(ret != NJS_OK)) {
        return NJS_ERROR;
    }

    for (node = current->properties; node != NULL; node = node->next) {
        if (node->type != XML_ATTRIBUTE_NODE) {
            continue;
        }

        push = njs_vm_array_push(vm, keys);
        if (njs_slow_path(push == NULL)) {
            return NJS_ERROR;
        }

        ret = njs_vm_value_string_create(vm, push, node->name,
                                         njs_strlen(node->name));
        if (njs_slow_path(ret != NJS_OK)) {
            return NJS_ERROR;
        }
    }

    return NJS_OK;
}


static njs_int_t
njs_xml_attr_ext_prop_handler(njs_vm_t *vm, njs_object_prop_t *prop,
    uint32_t atom_id, njs_value_t *value, njs_value_t *unused,
    njs_value_t *retval)
{
    size_t     size;
    xmlAttr    *node;
    xmlNode    *current;
    njs_int_t  ret;
    njs_str_t  name;

    current = njs_vm_external(vm, njs_xml_attr_proto_id, value);
    if (njs_slow_path(current == NULL)) {
        njs_value_undefined_set(retval);
        return NJS_DECLINED;
    }

    ret = njs_vm_prop_name(vm, atom_id, &name);
    if (njs_slow_path(ret != NJS_OK)) {
        njs_value_undefined_set(retval);
        return NJS_DECLINED;
    }

    for (node = current->properties; node != NULL; node = node->next) {
        if (node->type != XML_ATTRIBUTE_NODE) {
            continue;
        }

        size = njs_strlen(node->name);

        if (name.length != size
            || njs_strncmp(name.start, node->name, size) != 0)
        {
            continue;
        }

        return njs_vm_value_string_create(vm, retval, node->children->content,
                                          njs_strlen(node->children->content));
    }

    njs_value_undefined_set(retval);

    return NJS_OK;
}


static njs_xml_nset_t *
njs_xml_nset_create(njs_vm_t *vm, xmlDoc *doc, xmlNode *current,
    njs_xml_nset_type_t type)
{
    xmlNodeSet        *nodes;
    njs_xml_nset_t    *nset;
    njs_mp_cleanup_t  *cln;

    nset = njs_mp_zalloc(njs_vm_memory_pool(vm), sizeof(njs_xml_nset_t));
    if (njs_slow_path(nset == NULL)) {
        njs_vm_memory_error(vm);
        return NULL;
    }

    cln = njs_mp_cleanup_add(njs_vm_memory_pool(vm), 0);
    if (njs_slow_path(cln == NULL)) {
        njs_vm_memory_error(vm);
        return NULL;
    }

    nodes = xmlXPathNodeSetCreate(current);
    if (njs_slow_path(nodes == NULL)) {
        njs_vm_memory_error(vm);
        return NULL;
    }

    cln->handler = njs_xml_nset_cleanup;
    cln->data = nset;

    nset->doc = doc;
    nset->type = type;
    nset->nodes = nodes;
    nset->next = nset->prev = nset;

    return nset;
}


static njs_xml_nset_t *
njs_xml_nset_add(njs_xml_nset_t *nset, njs_xml_nset_t *add)
{
    if (nset == NULL) {
        return add;
    }

    add->next = nset;
    add->prev = nset->prev;
    nset->prev->next = add;
    nset->prev = add;

    return nset;
}


static void
njs_xml_nset_cleanup(void *data)
{
    njs_xml_nset_t  *nset = data;

    if (nset->nodes != NULL) {
        xmlXPathFreeNodeSet(nset->nodes);
    }
}


static void
njs_xml_error(njs_vm_t *vm, njs_xml_doc_t *current, const char *fmt, ...)
{
    u_char          *p, *last;
    va_list         args;
    const xmlError  *err;
    u_char          errstr[NJS_MAX_ERROR_STR];

    last = &errstr[NJS_MAX_ERROR_STR];

    va_start(args, fmt);
    p = njs_vsprintf(errstr, last - 1, fmt, args);
    va_end(args);

    err = xmlCtxtGetLastError(current->ctx);

    if (err != NULL) {
        p = njs_sprintf(p, last - 1, " (libxml2: \"%*s\" at %d:%d)",
                        njs_strlen(err->message) - 1, err->message, err->line,
                        err->int2);
    }

    njs_vm_syntax_error(vm, "%*s", p - errstr, errstr);
}


static njs_int_t
njs_xml_init(njs_vm_t *vm)
{
    njs_int_t           ret, proto_id;
    njs_mod_t           *module;
    njs_opaque_value_t  value;

    xmlInitParser();

    njs_xml_doc_proto_id = njs_vm_external_prototype(vm, njs_ext_xml_doc,
                                                  njs_nitems(njs_ext_xml_doc));
    if (njs_slow_path(njs_xml_doc_proto_id < 0)) {
        return NJS_ERROR;
    }

    njs_xml_node_proto_id = njs_vm_external_prototype(vm, njs_ext_xml_node,
                                                  njs_nitems(njs_ext_xml_node));
    if (njs_slow_path(njs_xml_node_proto_id < 0)) {
        return NJS_ERROR;
    }

    njs_xml_attr_proto_id = njs_vm_external_prototype(vm, njs_ext_xml_attr,
                                                  njs_nitems(njs_ext_xml_attr));
    if (njs_slow_path(njs_xml_attr_proto_id < 0)) {
        return NJS_ERROR;
    }

    proto_id = njs_vm_external_prototype(vm, njs_ext_xml,
                                         njs_nitems(njs_ext_xml));
    if (njs_slow_path(proto_id < 0)) {
        return NJS_ERROR;
    }

    ret = njs_vm_external_create(vm, njs_value_arg(&value), proto_id, NULL, 1);
    if (njs_slow_path(ret != NJS_OK)) {
        return NJS_ERROR;
    }

    module = njs_vm_add_module(vm, &njs_str_value("xml"),
                               njs_value_arg(&value));
    if (njs_slow_path(module == NULL)) {
        return NJS_ERROR;
    }

    return NJS_OK;
}
