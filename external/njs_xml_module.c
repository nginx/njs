
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
    njs_uint_t nargs, njs_index_t unused);
static njs_int_t njs_xml_ext_canonicalization(njs_vm_t *vm, njs_value_t *args,
    njs_uint_t nargs, njs_index_t unused);
static njs_int_t njs_xml_doc_ext_prop_keys(njs_vm_t *vm, njs_value_t *value,
    njs_value_t *keys);
static njs_int_t njs_xml_doc_ext_root(njs_vm_t *vm, njs_object_prop_t *prop,
     njs_value_t *value, njs_value_t *unused, njs_value_t *retval);
static void njs_xml_doc_cleanup(void *data);
static njs_int_t njs_xml_node_ext_prop_keys(njs_vm_t *vm, njs_value_t *value,
    njs_value_t *keys);
static njs_int_t njs_xml_node_ext_prop_handler(njs_vm_t *vm,
    njs_object_prop_t *prop, njs_value_t *value, njs_value_t *unused,
    njs_value_t *retval);
static njs_int_t njs_xml_attr_ext_prop_keys(njs_vm_t *vm, njs_value_t *value,
    njs_value_t *keys);
static njs_int_t njs_xml_attr_ext_prop_handler(njs_vm_t *vm,
    njs_object_prop_t *prop, njs_value_t *value, njs_value_t *unused,
    njs_value_t *retval);
static njs_int_t njs_xml_node_ext_attrs(njs_vm_t *vm, njs_object_prop_t *prop,
    njs_value_t *value, njs_value_t *setval, njs_value_t *retval);
static njs_int_t njs_xml_node_ext_name(njs_vm_t *vm, njs_object_prop_t *prop,
    njs_value_t *value, njs_value_t *setval, njs_value_t *retval);
static njs_int_t njs_xml_node_ext_ns(njs_vm_t *vm, njs_object_prop_t *prop,
    njs_value_t *value, njs_value_t *setval, njs_value_t *retval);
static njs_int_t njs_xml_node_ext_parent(njs_vm_t *vm, njs_object_prop_t *prop,
     njs_value_t *value, njs_value_t *setval, njs_value_t *retval);
static njs_int_t njs_xml_node_ext_tags(njs_vm_t *vm, njs_object_prop_t *prop,
     njs_value_t *value, njs_value_t *setval, njs_value_t *retval);
static njs_int_t njs_xml_node_ext_text(njs_vm_t *vm, njs_object_prop_t *prop,
    njs_value_t *value, njs_value_t *setval, njs_value_t *retval);

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
            .prop_handler = njs_xml_node_ext_prop_handler,
            .keys = njs_xml_node_ext_prop_keys,
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
        .flags = NJS_EXTERN_PROPERTY,
        .name.string = njs_str("$tags"),
        .enumerable = 1,
        .u.property = {
            .handler = njs_xml_node_ext_tags,
        }
    },

    {
        .flags = NJS_EXTERN_PROPERTY,
        .name.string = njs_str("$text"),
        .enumerable = 1,
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
    .init = njs_xml_init,
};


static njs_int_t    njs_xml_doc_proto_id;
static njs_int_t    njs_xml_node_proto_id;
static njs_int_t    njs_xml_attr_proto_id;


static njs_int_t
njs_xml_ext_parse(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t unused)
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
        njs_vm_error(vm, "xmlNewParserCtxt() failed");
        return NJS_ERROR;
    }

    tree->doc = xmlCtxtReadMemory(tree->ctx, (char *) data.start, data.length,
                                  NULL, NULL, XML_PARSE_DTDVALID
                                              | XML_PARSE_NOWARNING
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

    return njs_vm_external_create(vm, njs_vm_retval(vm), njs_xml_doc_proto_id,
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
njs_xml_doc_ext_root(njs_vm_t *vm, njs_object_prop_t *prop, njs_value_t *value,
     njs_value_t *unused, njs_value_t *retval)
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
        ret = njs_vm_prop_name(vm, prop, &name);
        if (njs_slow_path(ret != NJS_OK)) {
            njs_value_undefined_set(retval);
            return NJS_DECLINED;
        }
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


static void
njs_xml_doc_cleanup(void *data)
{
    njs_xml_doc_t  *current = data;

    xmlFreeDoc(current->doc);
    xmlFreeParserCtxt(current->ctx);
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

        ret = njs_vm_value_string_set(vm, push, (u_char *) "$name",
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

        ret = njs_vm_value_string_set(vm, push, (u_char *) "$ns",
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

        ret = njs_vm_value_string_set(vm, push, (u_char *) "$attrs",
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

        ret = njs_vm_value_string_set(vm, push, (u_char *) "$text",
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

        ret = njs_vm_value_string_set(vm, push, (u_char *) "$tags",
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
    njs_value_t *value, njs_value_t *unused, njs_value_t *retval)
{
    size_t        size;
    xmlAttr       *attr;
    xmlNode       *node, *current;
    njs_int_t     ret;
    njs_str_t     name;
    njs_value_t   *push;
    const u_char  *content;

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

    ret = njs_vm_prop_name(vm, prop, &name);
    if (njs_slow_path(ret != NJS_OK)) {
        njs_value_undefined_set(retval);
        return NJS_DECLINED;
    }

    if (name.length > 1 && name.start[0] == '$') {
        if (name.length > njs_length("$attr$")
            && njs_strncmp(&name.start[1], "attr$", njs_length("attr$")) == 0)
        {
            for (attr = current->properties; attr != NULL; attr = attr->next) {
                if (attr->type != XML_ATTRIBUTE_NODE) {
                    continue;
                }

                size = njs_strlen(attr->name);

                if (name.length != (size + njs_length("$attr$"))
                    || njs_strncmp(&name.start[njs_length("$attr$")],
                                   attr->name, size) != 0)
                {
                    continue;
                }

                content = (const u_char *) attr->children->content;

                return njs_vm_value_string_create(vm, retval, content,
                                                  njs_strlen(content));
            }
        }

        if (name.length > njs_length("$tag$")
            && njs_strncmp(&name.start[1], "tag$", njs_length("tag$")) == 0)
        {
            for (node = current->children; node != NULL; node = node->next) {
                if (node->type != XML_ELEMENT_NODE) {
                    continue;
                }

                size = njs_strlen(node->name);

                if (name.length != (size + njs_length("$tag$"))
                    || njs_strncmp(&name.start[njs_length("$tag$")],
                                   node->name, size) != 0)
                {
                    continue;
                }

                return njs_vm_external_create(vm, retval, njs_xml_node_proto_id,
                                              node, 0);
            }
        }

        if (name.length >= njs_length("$tags$")
            && njs_strncmp(&name.start[1], "tags$", njs_length("tags$")) == 0)
        {
            ret = njs_vm_array_alloc(vm, retval, 2);
            if (njs_slow_path(ret != NJS_OK)) {
                return NJS_ERROR;
            }

            for (node = current->children; node != NULL; node = node->next) {
                if (node->type != XML_ELEMENT_NODE) {
                    continue;
                }

                size = njs_strlen(node->name);

                if (name.length > njs_length("$tags$")
                    && (name.length != (size + njs_length("$tags$"))
                        || njs_strncmp(&name.start[njs_length("$tags$")],
                                       node->name, size) != 0))
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
    }

    for (node = current->children; node != NULL; node = node->next) {
        if (node->type != XML_ELEMENT_NODE) {
            continue;
        }

        size = njs_strlen(node->name);

        if (name.length != size
            || njs_strncmp(name.start, node->name, size) != 0)
        {
            continue;
        }

        return njs_vm_external_create(vm, retval, njs_xml_node_proto_id,
                                      node, 0);
    }

    njs_value_undefined_set(retval);

    return NJS_DECLINED;
}


static njs_int_t
njs_xml_node_ext_attrs(njs_vm_t *vm, njs_object_prop_t *prop,
    njs_value_t *value, njs_value_t *setval, njs_value_t *retval)
{
    xmlNode  *current;

    current = njs_vm_external(vm, njs_xml_node_proto_id, value);
    if (njs_slow_path(current == NULL || current->properties == NULL)) {
        njs_value_undefined_set(retval);
        return NJS_DECLINED;
    }

    return njs_vm_external_create(vm, retval, njs_xml_attr_proto_id,
                                  current->properties, 0);
}


static njs_int_t
njs_xml_node_ext_name(njs_vm_t *vm, njs_object_prop_t *prop, njs_value_t *value,
     njs_value_t *setval, njs_value_t *retval)
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
njs_xml_node_ext_ns(njs_vm_t *vm, njs_object_prop_t *prop,
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
njs_xml_node_ext_parent(njs_vm_t *vm, njs_object_prop_t *prop,
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
njs_xml_node_ext_tags(njs_vm_t *vm, njs_object_prop_t *prop, njs_value_t *value,
     njs_value_t *setval, njs_value_t *retval)
{
    xmlNode      *node, *current;
    njs_int_t    ret;
    njs_value_t  *push;

    current = njs_vm_external(vm, njs_xml_node_proto_id, value);
    if (njs_slow_path(current == NULL || current->children == NULL)) {
        njs_value_undefined_set(retval);
        return NJS_DECLINED;
    }

    ret = njs_vm_array_alloc(vm, retval, 2);
    if (njs_slow_path(ret != NJS_OK)) {
        return NJS_ERROR;
    }

    for (node = current->children; node != NULL; node = node->next) {
        if (node->type != XML_ELEMENT_NODE) {
            continue;
        }

        push = njs_vm_array_push(vm, retval);
        if (njs_slow_path(push == NULL)) {
            return NJS_ERROR;
        }

        ret = njs_vm_external_create(vm, push, njs_xml_node_proto_id, node, 0);
        if (njs_slow_path(ret != NJS_OK)) {
            return NJS_ERROR;
        }
    }

    return NJS_OK;
}


static njs_int_t
njs_xml_node_ext_text(njs_vm_t *vm, njs_object_prop_t *prop, njs_value_t *value,
     njs_value_t *setval, njs_value_t *retval)
{
    xmlNode    *current, *node;
    njs_int_t  ret;
    njs_chb_t  chain;

    current = njs_vm_external(vm, njs_xml_node_proto_id, value);
    if (njs_slow_path(current == NULL)) {
        njs_value_undefined_set(retval);
        return NJS_DECLINED;
    }

    njs_chb_init(&chain, njs_vm_memory_pool(vm));

    for (node = current->children; node != NULL; node = node->next) {
        if (node->type != XML_TEXT_NODE) {
            continue;
        }

        njs_chb_append(&chain, node->content, njs_strlen(node->content));
    }

    ret = njs_vm_value_string_create_chb(vm, retval, &chain);

    njs_chb_destroy(&chain);

    return ret;
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


static u_char **
njs_xml_parse_ns_list(njs_vm_t *vm, njs_str_t *src)
{
    u_char    *p, **buf, **n, **out;
    size_t  size, idx;

    out = NULL;

    p =  njs_mp_alloc(njs_vm_memory_pool(vm), src->length + 1);
    if (njs_slow_path(p == NULL)) {
        njs_vm_memory_error(vm);
        return NULL;
    }

    memcpy(p, src->start, src->length);
    p[src->length] = '\0';

    size = 8;

    buf = njs_mp_alloc(njs_vm_memory_pool(vm), size * sizeof(char *));
    if (njs_slow_path(buf == NULL)) {
        njs_vm_memory_error(vm);
        return NULL;
    }

    out = buf;

    while (*p != '\0') {
        idx = out - buf;

        if (idx >= size) {
            size *= 2;

            n = njs_mp_alloc(njs_vm_memory_pool(vm), size * sizeof(char *));
            if (njs_slow_path(buf == NULL)) {
                njs_vm_memory_error(vm);
                return NULL;
            }

            memcpy(n, buf, size * sizeof(char *) / 2);
            buf = n;

            out = &buf[idx];
        }

        *out++ = p;

        while (*p != ' ' && *p != '\0') {
            p++;
        }

        if (*p == ' ') {
            *p++ = '\0';
        }
    }

    *out = NULL;

    return buf;
}


static njs_int_t
njs_xml_ext_canonicalization(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t exclusive)
{
    u_char           **prefix_list;
    ssize_t          size;
    xmlNode          *node, *current;
    njs_int_t        ret;
    njs_str_t        data, string;
    njs_chb_t        chain;
    njs_bool_t       comments;
    njs_value_t      *excluding, *prefixes;
    njs_xml_doc_t    *tree;
    njs_xml_nset_t   *nset, *children;
    xmlOutputBuffer  *buf;

    current = njs_vm_external(vm, njs_xml_node_proto_id, njs_argument(args, 1));
    if (njs_slow_path(current == NULL)) {
        tree = njs_vm_external(vm, njs_xml_doc_proto_id, njs_argument(args, 1));
        if (njs_slow_path(tree == NULL)) {
            njs_vm_error(vm, "\"this\" is not a XMLNode object");
            return NJS_ERROR;
        }

        current = xmlDocGetRootElement(tree->doc);
        if (njs_slow_path(current == NULL)) {
            njs_vm_error(vm, "\"this\" is not a XMLNode object");
            return NJS_ERROR;
        }
    }

    comments = njs_value_bool(njs_arg(args, nargs, 3));

    excluding = njs_arg(args, nargs, 2);

    if (!njs_value_is_null_or_undefined(excluding)) {
        node = njs_vm_external(vm, njs_xml_node_proto_id, excluding);
        if (njs_slow_path(node == NULL)) {
            njs_vm_error(vm, "\"excluding\" argument is not a XMLNode object");
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
            njs_vm_error(vm, "\"prefixes\" argument is not a string");
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

    njs_chb_init(&chain, njs_vm_memory_pool(vm));

    buf = xmlOutputBufferCreateIO(njs_xml_buf_write_cb, NULL, &chain, NULL);
    if (njs_slow_path(buf == NULL)) {
        njs_vm_error(vm, "xmlOutputBufferCreateIO() failed");
        return NJS_ERROR;
    }

    ret = xmlC14NExecute(current->doc, njs_xml_c14n_visibility_cb, nset,
                         exclusive ? XML_C14N_EXCLUSIVE_1_0 : XML_C14N_1_0,
                         prefix_list, comments, buf);

    if (njs_slow_path(ret < 0)) {
        njs_vm_error(vm, "xmlC14NExecute() failed");
        ret = NJS_ERROR;
        goto error;
    }

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

    ret = njs_vm_value_buffer_set(vm, njs_vm_retval(vm), data.start,
                                  data.length);

error:

    (void) xmlOutputBufferClose(buf);

    njs_chb_destroy(&chain);

    return ret;
}


static njs_int_t
njs_xml_attr_ext_prop_keys(njs_vm_t *vm, njs_value_t *value, njs_value_t *keys)
{
    xmlAttr      *node, *current;
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

    for (node = current; node != NULL; node = node->next) {
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
    njs_value_t *value, njs_value_t *unused, njs_value_t *retval)
{
    size_t     size;
    xmlAttr    *node, *current;
    njs_int_t  ret;
    njs_str_t  name;

    current = njs_vm_external(vm, njs_xml_attr_proto_id, value);
    if (njs_slow_path(current == NULL)) {
        njs_value_undefined_set(retval);
        return NJS_DECLINED;
    }

    ret = njs_vm_prop_name(vm, prop, &name);
    if (njs_slow_path(ret != NJS_OK)) {
        njs_value_undefined_set(retval);
        return NJS_DECLINED;
    }

    for (node = current; node != NULL; node = node->next) {
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
    u_char         *p, *last;
    va_list        args;
    xmlError       *err;
    u_char         errstr[NJS_MAX_ERROR_STR];

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

    njs_vm_value_error_set(vm, njs_vm_retval(vm), "%*s", p - errstr, errstr);
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
