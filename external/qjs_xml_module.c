
/*
 * Copyright (C) Dmitry Volyntsev
 * Copyright (C) F5, Inc.
 */

#include <qjs.h>
#include <njs_sprintf.h>
#include <libxml/parser.h>
#include <libxml/tree.h>
#include <libxml/c14n.h>
#include <libxml/xpathInternals.h>


typedef struct {
    xmlDoc         *doc;
    xmlParserCtxt  *ctx;
    xmlNode        *free;
    int            ref_count;
} qjs_xml_doc_t;


typedef struct {
    xmlNode        *node;
    qjs_xml_doc_t  *doc;
} qjs_xml_node_t;


typedef struct {
    xmlNode        *node;
    qjs_xml_doc_t  *doc;
} qjs_xml_attr_t;


typedef enum {
    XML_NSET_TREE = 0,
    XML_NSET_TREE_NO_COMMENTS,
    XML_NSET_TREE_INVERT,
} qjs_xml_nset_type_t;


typedef struct qjs_xml_nset_s  qjs_xml_nset_t;

struct qjs_xml_nset_s {
    xmlNodeSet           *nodes;
    xmlDoc               *doc;
    qjs_xml_nset_type_t  type;
    qjs_xml_nset_t       *next;
    qjs_xml_nset_t       *prev;
};


static JSValue qjs_xml_parse(JSContext *cx, JSValueConst this_val,
    int argc, JSValueConst *argv);
static JSValue qjs_xml_canonicalization(JSContext *cx, JSValueConst this_val,
    int argc, JSValueConst *argv, int magic);

static int qjs_xml_doc_get_own_property(JSContext *cx,
    JSPropertyDescriptor *pdesc, JSValueConst obj, JSAtom prop);
static int qjs_xml_doc_get_own_property_names(JSContext *cx,
    JSPropertyEnum **ptab, uint32_t *plen, JSValueConst obj);
static void qjs_xml_doc_finalizer(JSRuntime *rt, JSValue val);
static void qjs_xml_doc_free(JSRuntime *rt, qjs_xml_doc_t *current);

static JSValue qjs_xml_node_make(JSContext *cx, qjs_xml_doc_t *doc,
    xmlNode *node);
static int qjs_xml_node_get_own_property(JSContext *cx,
    JSPropertyDescriptor *pdesc, JSValueConst obj, JSAtom prop);
static int qjs_xml_node_get_own_property_names(JSContext *cx,
    JSPropertyEnum **ptab, uint32_t *plen, JSValueConst obj);
static int qjs_xml_node_set_property(JSContext *cx, JSValueConst obj,
    JSAtom atom, JSValueConst value, JSValueConst receiver, int flags);
static int qjs_xml_node_delete_property(JSContext *cx, JSValueConst obj,
    JSAtom prop);
static int qjs_xml_node_define_own_property(JSContext *cx, JSValueConst obj,
    JSAtom atom, JSValueConst value, JSValueConst getter, JSValueConst setter,
    int flags);
static JSValue qjs_xml_node_add_child(JSContext *cx, JSValueConst this_val,
    int argc, JSValueConst *argv);
static JSValue qjs_xml_node_remove_children(JSContext *cx,
    JSValueConst this_val, int argc, JSValueConst *argv);
static JSValue qjs_xml_node_set_attribute(JSContext *cx, JSValueConst this_val,
    int argc, JSValueConst *argv);
static JSValue qjs_xml_node_remove_attribute(JSContext *cx,
    JSValueConst this_val, int argc, JSValueConst *argv);
static JSValue qjs_xml_node_remove_all_attributes(JSContext *cx,
    JSValueConst this_val, int argc, JSValueConst *argv);
static JSValue qjs_xml_node_set_text(JSContext *cx, JSValueConst this_val,
    int argc, JSValueConst *argv);
static JSValue qjs_xml_node_remove_text(JSContext *cx, JSValueConst this_val,
    int argc, JSValueConst *argv);
static void qjs_xml_node_finalizer(JSRuntime *rt, JSValue val);
static xmlNode *qjs_xml_node(JSContext *cx, JSValueConst val, xmlDoc **doc);

static JSValue qjs_xml_attr_make(JSContext *cx, qjs_xml_doc_t *doc,
    xmlNode *node);
static int qjs_xml_attr_get_own_property(JSContext *cx,
    JSPropertyDescriptor *pdesc, JSValueConst obj, JSAtom prop);
static int qjs_xml_attr_get_own_property_names(JSContext *cx,
    JSPropertyEnum **ptab, uint32_t *plen, JSValueConst obj);
static void qjs_xml_attr_finalizer(JSRuntime *rt, JSValue val);

static u_char **qjs_xml_parse_ns_list(JSContext *cx, u_char *src);
static int qjs_xml_c14n_visibility_cb(void *user_data, xmlNode *node,
    xmlNode *parent);
static int qjs_xml_buf_write_cb(void *context, const char *buffer, int len);
static qjs_xml_nset_t *qjs_xml_nset_create(JSContext *cx, xmlDoc *doc,
    xmlNode *current, qjs_xml_nset_type_t type);
static qjs_xml_nset_t *qjs_xml_nset_add(qjs_xml_nset_t *nset,
    qjs_xml_nset_t *add);
static void qjs_xml_nset_free(JSContext *cx, qjs_xml_nset_t *nset);
static int qjs_xml_encode_special_chars(JSContext *cx, njs_str_t *src,
    njs_str_t *out);
static void qjs_xml_replace_node(JSContext *cx, qjs_xml_node_t *node,
    xmlNode *current);

static void qjs_xml_error(JSContext *cx, qjs_xml_doc_t *current,
    const char *fmt, ...);
static JSModuleDef *qjs_xml_init(JSContext *ctx, const char *name);


static const JSCFunctionListEntry qjs_xml_export[] = {
    JS_CFUNC_DEF("parse", 2, qjs_xml_parse),
    JS_CFUNC_MAGIC_DEF("c14n", 4, qjs_xml_canonicalization, 0),
    JS_CFUNC_MAGIC_DEF("exclusiveC14n", 4, qjs_xml_canonicalization, 1),
    JS_CFUNC_MAGIC_DEF("serialize", 4, qjs_xml_canonicalization, 0),
    JS_CFUNC_MAGIC_DEF("serializeToString", 4, qjs_xml_canonicalization, 2),
};


static const JSCFunctionListEntry qjs_xml_doc_proto[] = {
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "XMLDoc", JS_PROP_CONFIGURABLE),
};


static const JSCFunctionListEntry qjs_xml_node_proto[] = {
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "XMLNode", JS_PROP_CONFIGURABLE),
    JS_CFUNC_DEF("addChild", 1, qjs_xml_node_add_child),
    JS_CFUNC_DEF("removeChildren", 1, qjs_xml_node_remove_children),
    JS_CFUNC_DEF("setAttribute", 2, qjs_xml_node_set_attribute),
    JS_CFUNC_DEF("removeAttribute", 1, qjs_xml_node_remove_attribute),
    JS_CFUNC_DEF("removeAllAttributes", 0, qjs_xml_node_remove_all_attributes),
    JS_CFUNC_DEF("setText", 1, qjs_xml_node_set_text),
    JS_CFUNC_DEF("removeText", 0, qjs_xml_node_remove_text),
};


static const JSCFunctionListEntry qjs_xml_attr_proto[] = {
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "XMLAttr", JS_PROP_CONFIGURABLE),
};


static JSClassDef qjs_xml_doc_class = {
    "XMLDoc",
    .finalizer = qjs_xml_doc_finalizer,
    .exotic = & (JSClassExoticMethods) {
        .get_own_property = qjs_xml_doc_get_own_property,
        .get_own_property_names = qjs_xml_doc_get_own_property_names,
    },
};


static JSClassDef qjs_xml_node_class = {
    "XMLNode",
    .finalizer = qjs_xml_node_finalizer,
    .exotic = & (JSClassExoticMethods) {
        .get_own_property = qjs_xml_node_get_own_property,
        .get_own_property_names = qjs_xml_node_get_own_property_names,
        .set_property = qjs_xml_node_set_property,
        .delete_property = qjs_xml_node_delete_property,
        .define_own_property = qjs_xml_node_define_own_property,
    },
};


static JSClassDef qjs_xml_attr_class = {
    "XMLAttr",
    .finalizer = qjs_xml_attr_finalizer,
    .exotic = & (JSClassExoticMethods) {
        .get_own_property = qjs_xml_attr_get_own_property,
        .get_own_property_names = qjs_xml_attr_get_own_property_names,
    },
};


qjs_module_t  qjs_xml_module = {
    .name = "xml",
    .init = qjs_xml_init,
};


static JSValue
qjs_xml_parse(JSContext *cx, JSValueConst this_val, int argc,
    JSValueConst *argv)
{
    JSValue        ret;
    qjs_bytes_t    data;
    qjs_xml_doc_t  *tree;

    if (qjs_to_bytes(cx, &data, argv[0]) < 0) {
        return JS_EXCEPTION;
    }

    tree = js_mallocz(cx, sizeof(qjs_xml_doc_t));
    if (tree == NULL) {
        qjs_bytes_free(cx, &data);
        JS_ThrowOutOfMemory(cx);
        return JS_EXCEPTION;
    }

    tree->ref_count = 1;

    tree->ctx = xmlNewParserCtxt();
    if (tree->ctx == NULL) {
        qjs_bytes_free(cx, &data);
        JS_ThrowInternalError(cx, "xmlNewParserCtxt() failed");
        qjs_xml_doc_free(JS_GetRuntime(cx), tree);
        return JS_EXCEPTION;
    }

    tree->doc = xmlCtxtReadMemory(tree->ctx, (char *) data.start, data.length,
                                  NULL, NULL, XML_PARSE_NOWARNING
                                              | XML_PARSE_NOERROR);
    qjs_bytes_free(cx, &data);
    if (tree->doc == NULL) {
        qjs_xml_error(cx, tree, "failed to parse XML");
        qjs_xml_doc_free(JS_GetRuntime(cx), tree);
        return JS_EXCEPTION;
    }

    ret = JS_NewObjectClass(cx, QJS_CORE_CLASS_ID_XML_DOC);
    if (JS_IsException(ret)) {
        qjs_xml_doc_free(JS_GetRuntime(cx), tree);
        return ret;
    }

    JS_SetOpaque(ret, tree);

    return ret;
}


static JSValue
qjs_xml_canonicalization(JSContext *cx, JSValueConst this_val, int argc,
    JSValueConst *argv, int magic)
{
    int              comments;
    u_char           **prefix_list, *pref;
    xmlDoc           *doc;
    xmlNode          *node;
    ssize_t          size;
    JSValue          excluding, prefixes, ret;
    njs_chb_t        chain;
    qjs_xml_node_t   *nd;
    qjs_xml_nset_t   *nset, *children;
    xmlOutputBuffer  *buf;

    node = qjs_xml_node(cx, argv[0], &doc);
    if (node == NULL) {
        return JS_EXCEPTION;
    }

    comments = JS_ToBool(cx, argv[2]);
    if (comments < 0) {
        return JS_EXCEPTION;
    }

    buf = NULL;
    nset = NULL;
    children = NULL;

    excluding = argv[1];
    if (!JS_IsNullOrUndefined(excluding)) {
        nd = JS_GetOpaque(excluding, QJS_CORE_CLASS_ID_XML_NODE);
        if (nd == NULL) {
            JS_ThrowTypeError(cx, "\"excluding\" argument is not a XMLNode "
                              "object");
            return JS_EXCEPTION;
        }

        nset = qjs_xml_nset_create(cx, doc, node, XML_NSET_TREE_NO_COMMENTS);
        if (nset == NULL) {
            return JS_ThrowOutOfMemory(cx);
        }

        children = qjs_xml_nset_create(cx, nd->doc->doc, nd->node,
                                       XML_NSET_TREE_INVERT);
        if (children == NULL) {
            qjs_xml_nset_free(cx, nset);
            return JS_ThrowOutOfMemory(cx);
        }

        nset = qjs_xml_nset_add(nset, children);

    } else {
        nset = qjs_xml_nset_create(cx, doc, node,
                                   comments ? XML_NSET_TREE
                                            : XML_NSET_TREE_NO_COMMENTS);
        if (nset == NULL) {
            return JS_ThrowOutOfMemory(cx);
        }
    }

    prefix_list = NULL;
    prefixes = argv[3];
    if (!JS_IsNullOrUndefined(prefixes)) {
        if (!JS_IsString(prefixes)) {
            JS_ThrowTypeError(cx, "\"prefixes\" argument is not a string");
            goto fail;
        }

        pref = (u_char *) JS_ToCString(cx, prefixes);
        if (pref == NULL) {
            JS_ThrowOutOfMemory(cx);
            goto fail;
        }

        prefix_list = qjs_xml_parse_ns_list(cx, pref);
        if (prefix_list == NULL) {
            goto fail;
        }
    }

    NJS_CHB_CTX_INIT(&chain, cx);

    buf = xmlOutputBufferCreateIO(qjs_xml_buf_write_cb, NULL,
                                  &chain, NULL);
    if (buf == NULL) {
        JS_ThrowInternalError(cx, "xmlOutputBufferCreateIO() failed");
        goto fail;
    }

    size = xmlC14NExecute(doc, qjs_xml_c14n_visibility_cb, nset,
                          magic & 0x1 ? XML_C14N_EXCLUSIVE_1_0 : XML_C14N_1_0,
                          prefix_list, comments, buf);

    if (size < 0) {
        njs_chb_destroy(&chain);
        (void) xmlOutputBufferClose(buf);
        JS_ThrowInternalError(cx, "xmlC14NExecute() failed");
        goto fail;
    }

    if (magic & 0x2) {
        ret = qjs_string_create_chb(cx, &chain);

    } else {
        ret = qjs_buffer_chb_alloc(cx, &chain);
        njs_chb_destroy(&chain);
    }

    (void) xmlOutputBufferClose(buf);

    qjs_xml_nset_free(cx, nset);
    qjs_xml_nset_free(cx, children);

    if (prefix_list != NULL) {
        js_free(cx, prefix_list);
    }

    return ret;

fail:

    qjs_xml_nset_free(cx, nset);
    qjs_xml_nset_free(cx, children);

    if (prefix_list != NULL) {
        js_free(cx, prefix_list);
    }

    return JS_EXCEPTION;
}


static int
qjs_xml_doc_get_own_property(JSContext *cx, JSPropertyDescriptor *pdesc,
    JSValueConst obj, JSAtom prop)
{
    int            any;
    xmlNode        *node;
    njs_str_t      name;
    qjs_xml_doc_t  *tree;

    tree = JS_GetOpaque(obj, QJS_CORE_CLASS_ID_XML_DOC);
    if (tree == NULL) {
        (void) JS_ThrowInternalError(cx, "\"this\" is not an XMLDoc");
        return -1;
    }

    name.start = (u_char *) JS_AtomToCString(cx, prop);
    if (name.start == NULL) {
        return -1;
    }

    name.length = njs_strlen(name.start);

    any = (name.length == njs_strlen("$root")
           && njs_strncmp(name.start, "$root", name.length) == 0);

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

        JS_FreeCString(cx, (char *) name.start);

        if (pdesc != NULL) {
            pdesc->flags = JS_PROP_ENUMERABLE;
            pdesc->getter = JS_UNDEFINED;
            pdesc->setter = JS_UNDEFINED;
            pdesc->value  = qjs_xml_node_make(cx, tree, node);
            if (JS_IsException(pdesc->value)) {
                return -1;
            }
        }

        return 1;
    }

    JS_FreeCString(cx, (char *) name.start);

    return 0;
}


static int
qjs_xml_push_string(JSContext *cx, JSValue obj, const char *start)
{
    JSAtom  key;

    key = JS_NewAtomLen(cx, start, njs_strlen(start));
    if (key == JS_ATOM_NULL) {
        return -1;
    }

    if (JS_DefinePropertyValue(cx, obj, key, JS_UNDEFINED,
                               JS_PROP_ENUMERABLE) < 0)
    {
        JS_FreeAtom(cx, key);
        return -1;
    }

    JS_FreeAtom(cx, key);

    return 0;
}


static int
qjs_xml_doc_get_own_property_names(JSContext *cx, JSPropertyEnum **ptab,
    uint32_t *plen, JSValueConst obj)
{
    int            rc;
    JSValue        keys;
    xmlNode        *node;
    qjs_xml_doc_t  *tree;

    tree = JS_GetOpaque(obj, QJS_CORE_CLASS_ID_XML_DOC);
    if (tree == NULL) {
        (void) JS_ThrowInternalError(cx, "\"this\" is not an XMLDoc");
        return -1;
    }

    keys = JS_NewObject(cx);
    if (JS_IsException(keys)) {
        return -1;
    }

    for (node = xmlDocGetRootElement(tree->doc);
         node != NULL;
         node = node->next)
    {
        if (node->type != XML_ELEMENT_NODE) {
            continue;
        }

        if (qjs_xml_push_string(cx, keys, (char *) node->name) < 0) {
            JS_FreeValue(cx, keys);
            return -1;
        }
    }

    rc = JS_GetOwnPropertyNames(cx, ptab, plen, keys, JS_GPN_STRING_MASK);

    JS_FreeValue(cx, keys);

    return rc;
}


static void
qjs_xml_doc_free(JSRuntime *rt, qjs_xml_doc_t *current)
{
    xmlNode  *node, *next;

    if (--current->ref_count > 0) {
        return;
    }

    node = current->free;

    while (node != NULL) {
        next = node->next;
        xmlFreeNode(node);
        node = next;
    }

    if (current->doc != NULL) {
        xmlFreeDoc(current->doc);
    }

    if (current->ctx != NULL) {
        xmlFreeParserCtxt(current->ctx);
    }

    js_free_rt(rt, current);
}


static void
qjs_xml_doc_finalizer(JSRuntime *rt, JSValue val)
{
    qjs_xml_doc_t  *current;

    current = JS_GetOpaque(val, QJS_CORE_CLASS_ID_XML_DOC);

    qjs_xml_doc_free(rt, current);
}


static JSValue
qjs_xml_node_make(JSContext *cx, qjs_xml_doc_t *doc, xmlNode *node)
{
    JSValue         ret;
    qjs_xml_node_t  *current;

    current = js_malloc(cx, sizeof(qjs_xml_node_t));
    if (current == NULL) {
        JS_ThrowOutOfMemory(cx);
        return JS_EXCEPTION;
    }

    current->node = node;
    current->doc = doc;
    doc->ref_count++;

    ret = JS_NewObjectClass(cx, QJS_CORE_CLASS_ID_XML_NODE);
    if (JS_IsException(ret)) {
        js_free(cx, current);
        return ret;
    }

    JS_SetOpaque(ret, current);

    return ret;
}


static JSValue
qjs_xml_node_tag_handler(JSContext *cx, qjs_xml_node_t *current,
    njs_str_t *name)
{
    size_t   size;
    xmlNode  *node;

    node = current->node;

    for (node = node->children; node != NULL; node = node->next) {
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

        return qjs_xml_node_make(cx, current->doc, node);
    }

    return JS_UNDEFINED;
}


static JSValue
qjs_xml_node_tags_handler(JSContext *cx, qjs_xml_node_t *current,
    njs_str_t *name)
{
    size_t   size;
    int32_t  i;
    xmlNode  *node;
    JSValue  arr, ret;

    arr = JS_NewArray(cx);
    if (JS_IsException(arr)) {
        return arr;
    }

    i = 0;
    node = current->node;

    for (node = node->children; node != NULL; node = node->next) {
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

        ret = qjs_xml_node_make(cx, current->doc, node);
        if (JS_IsException(ret)) {
            JS_FreeValue(cx, arr);
            return JS_EXCEPTION;
        }

        if (JS_SetPropertyUint32(cx, arr, i++, ret) < 0) {
            JS_FreeValue(cx, arr);
            JS_FreeValue(cx, ret);
            return JS_EXCEPTION;
        }
    }

    return arr;
}


static JSValue
qjs_xml_node_attr_handler(JSContext *cx, qjs_xml_node_t *current,
    njs_str_t *name)
{
    size_t   size;
    xmlAttr  *attr;
    xmlNode  *node;

    node = current->node;

    for (attr = node->properties; attr != NULL; attr = attr->next) {
        if (attr->type != XML_ATTRIBUTE_NODE) {
            continue;
        }

        size = njs_strlen(attr->name);

        if (name->length > 0
            && (name->length != size
                || njs_strncmp(name->start, attr->name, size) != 0))
        {
            continue;
        }

        if (attr->children != NULL
            && attr->children->next == NULL
            && attr->children->type == XML_TEXT_NODE
            && attr->children->content != NULL)
        {
            return JS_NewString(cx, (char *) attr->children->content);
        }
    }

    return JS_UNDEFINED;
}


static int
qjs_xml_node_attr_modify(JSContext *cx, JSValue current, const u_char *name,
    JSValue setval)
{
    xmlAttr         *attr;
    const u_char    *value;
    qjs_xml_node_t  *node;

    node = JS_GetOpaque(current, QJS_CORE_CLASS_ID_XML_NODE);
    if (node == NULL) {
        return -1;
    }

    if (xmlValidateQName(name, 0) != 0) {
        JS_ThrowTypeError(cx, "attribute name \"%s\" is not valid", name);
        return -1;
    }

    if (JS_IsNullOrUndefined(setval)) {
        attr = xmlHasProp(node->node, name);

        if (attr != NULL) {
            xmlRemoveProp(attr);
        }

        return 1;
    }

    value = (const u_char *) JS_ToCString(cx, setval);
    if (value == NULL) {
        return -1;
    }

    attr = xmlSetProp(node->node, name, value);
    JS_FreeCString(cx, (char *) value);
    if (attr == NULL) {
        JS_ThrowInternalError(cx, "xmlSetProp() failed");
        return -1;
    }

    return 1;
}


static int
qjs_xml_node_tag_modify(JSContext *cx, JSValue obj, njs_str_t *name,
    JSValue setval)
{
    size_t          size;
    xmlNode         *node, *next, *copy;
    qjs_xml_node_t  *current;

    current = JS_GetOpaque(obj, QJS_CORE_CLASS_ID_XML_NODE);
    if (current == NULL) {
        return -1;
    }

    if (!JS_IsNullOrUndefined(setval)) {
        JS_ThrowInternalError(cx, "XMLNode.$tag$xxx is not assignable, "
                           "use addChild() or node.$tags = [node1, node2, ..] "
                           "syntax");
        return -1;
    }

    copy = xmlDocCopyNode(current->node, current->doc->doc, 1);
    if (copy == NULL) {
        JS_ThrowInternalError(cx, "xmlDocCopyNode() failed");
        return -1;
    }

    for (node = copy->children; node != NULL; node = next) {
        next = node->next;

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

        xmlUnlinkNode(node);

        node->next = current->doc->free;
        current->doc->free = node;
    }

    qjs_xml_replace_node(cx, current, copy);

    return 1;
}


static int
qjs_xml_node_tags_modify(JSContext *cx, JSValue obj, njs_str_t *name,
    JSValue setval)
{
    int32_t         len, i;
    xmlNode         *node, *rnode, *copy;
    JSValue         length, v;
    qjs_xml_node_t  *current;

    current = JS_GetOpaque(obj, QJS_CORE_CLASS_ID_XML_NODE);
    if (current == NULL) {
        return -1;
    }

    if (!qjs_is_array(cx, setval)) {
        JS_ThrowTypeError(cx, "setval is not an array");
        return -1;
    }

    length = JS_GetPropertyStr(cx, setval, "length");
    if (JS_IsException(length)) {
        return -1;
    }

    if (JS_ToInt32(cx, &len, length) < 0) {
        return -1;
    }

    copy = xmlDocCopyNode(current->node, current->doc->doc,
                          2 /* copy properties and namespaces */);
    if (copy == NULL) {
        JS_ThrowInternalError(cx, "xmlDocCopyNode() failed");
        return -1;
    }

    for (i = 0; i < len; i++) {
        v = JS_GetPropertyUint32(cx, setval, i);
        if (JS_IsException(v)) {
            goto error;
        }

        node = qjs_xml_node(cx, v, NULL);
        JS_FreeValue(cx, v);
        if (node == NULL) {
            goto error;
        }

        node = xmlDocCopyNode(node, current->doc->doc, 1);
        if (node == NULL) {
            JS_ThrowInternalError(cx, "xmlDocCopyNode() failed");
            goto error;
        }

        rnode = xmlAddChild(copy, node);
        if (rnode == NULL) {
            xmlFreeNode(node);
            JS_ThrowInternalError(cx, "xmlAddChild() failed");
            goto error;
        }
    }

    if (xmlReconciliateNs(current->doc->doc, copy) == -1) {
        JS_ThrowInternalError(cx, "xmlReconciliateNs() failed");
        goto error;
    }

    qjs_xml_replace_node(cx, current, copy);

    return 1;

error:

    xmlFreeNode(copy);

    return -1;
}


static int
qjs_xml_node_text_handler(JSContext *cx, JSValue current, JSValue setval)
{
    xmlNode         *copy;
    njs_str_t       content, enc;
    qjs_xml_node_t  *node;

    enc.start = NULL;
    enc.length = 0;

    node = JS_GetOpaque(current, QJS_CORE_CLASS_ID_XML_NODE);
    if (node == NULL) {
        return -1;
    }

    if (!JS_IsNullOrUndefined(setval)) {
        content.start = (u_char *) JS_ToCStringLen(cx, &content.length, setval);
        if (content.start == NULL) {
            return -1;
        }

        if (qjs_xml_encode_special_chars(cx, &content, &enc) < 0) {
            JS_FreeCString(cx, (char *) content.start);
            return -1;
        }

        JS_FreeCString(cx, (char *) content.start);
    }

    copy = xmlDocCopyNode(node->node, node->doc->doc, 1);
    if (copy == NULL) {
        if (enc.start != NULL) {
            js_free(cx, enc.start);
        }

        JS_ThrowInternalError(cx, "xmlDocCopyNode() failed");
        return -1;
    }

    xmlNodeSetContentLen(copy, enc.start, enc.length);

    if (enc.start != NULL) {
        js_free(cx, enc.start);
    }

    qjs_xml_replace_node(cx, node, copy);

    return 1;
}


static int
qjs_xml_node_get_own_property(JSContext *cx, JSPropertyDescriptor *pdesc,
    JSValueConst obj, JSAtom prop)
{
    u_char          *text;
    JSValue         value;
    xmlNode         *node;
    njs_str_t       name, nm;
    qjs_xml_node_t  *current;

    /*
     * $tag$foo - the first tag child with the name "foo"
     * $tags$foo - the all children with the name "foo" as an array
     * $attr$foo - the attribute with the name "foo"
     * foo - the same as $tag$foo
     */

    current = JS_GetOpaque(obj, QJS_CORE_CLASS_ID_XML_NODE);
    if (current == NULL) {
        (void) JS_ThrowInternalError(cx, "\"this\" is not an XMLNode");
        return -1;
    }

    name.start = (u_char *) JS_AtomToCString(cx, prop);
    if (name.start == NULL) {
        return -1;
    }

    name.length = njs_strlen(name.start);
    node = current->node;

    if (name.length > 1 && name.start[0] == '$') {
        if (name.length == njs_length("$attrs")
            && njs_strncmp(&name.start[1], "attrs", njs_length("attrs")) == 0)
        {
            JS_FreeCString(cx, (char *) name.start);

            if (node->properties == NULL) {
                return 0;
            }

            if (pdesc != NULL) {
                pdesc->flags = JS_PROP_ENUMERABLE;
                pdesc->getter = JS_UNDEFINED;
                pdesc->setter = JS_UNDEFINED;
                pdesc->value  = qjs_xml_attr_make(cx, current->doc, node);
                if (JS_IsException(pdesc->value)) {
                    return -1;
                }
            }

            return 1;
        }

        if (name.length > njs_length("$attr$")
            && njs_strncmp(&name.start[1], "attr$", njs_length("attr$")) == 0)
        {
            nm.length = name.length - njs_length("$attr$");
            nm.start = name.start + njs_length("$attr$");

            value = qjs_xml_node_attr_handler(cx, current, &nm);
            JS_FreeCString(cx, (char *) name.start);
            if (JS_IsException(value)) {
                return -1;
            }

            if (JS_IsUndefined(value)) {
                return 0;
            }

            if (pdesc != NULL) {
                pdesc->flags = JS_PROP_ENUMERABLE;
                pdesc->getter = JS_UNDEFINED;
                pdesc->setter = JS_UNDEFINED;
                pdesc->value  = value;
            }

            return 1;
        }

        if (name.length == njs_length("$name")
            && njs_strncmp(&name.start[1], "name", njs_length("name")) == 0)
        {
            JS_FreeCString(cx, (char *) name.start);

            if (node->type != XML_ELEMENT_NODE) {
                return 0;
            }

            if (pdesc != NULL) {
                pdesc->flags = JS_PROP_ENUMERABLE;
                pdesc->getter = JS_UNDEFINED;
                pdesc->setter = JS_UNDEFINED;
                pdesc->value  = JS_NewString(cx, (char *) node->name);
                if (JS_IsException(pdesc->value)) {
                    return -1;
                }
            }

            return 1;
        }

        if (name.length == njs_length("$ns")
            && njs_strncmp(&name.start[1], "ns", njs_length("ns")) == 0)
        {
            JS_FreeCString(cx, (char *) name.start);

            if (node->ns == NULL || node->ns->href == NULL) {
                return 0;
            }

            if (pdesc != NULL) {
                pdesc->flags = JS_PROP_ENUMERABLE;
                pdesc->getter = JS_UNDEFINED;
                pdesc->setter = JS_UNDEFINED;
                pdesc->value  = JS_NewString(cx, (char *) node->ns->href);
                if (JS_IsException(pdesc->value)) {
                    return -1;
                }
            }

            return 1;
        }

        if (name.length == njs_length("$parent")
            && njs_strncmp(&name.start[1], "parent", njs_length("parent")) == 0)
        {
            JS_FreeCString(cx, (char *) name.start);

            if (node->parent == NULL
                || node->parent->type != XML_ELEMENT_NODE)
            {
                return 0;
            }

            if (pdesc != NULL) {
                pdesc->flags = JS_PROP_ENUMERABLE;
                pdesc->getter = JS_UNDEFINED;
                pdesc->setter = JS_UNDEFINED;
                pdesc->value  = qjs_xml_node_make(cx, current->doc,
                                                  node->parent);
                if (JS_IsException(pdesc->value)) {
                    return -1;
                }
            }

            return 1;
        }

        if (name.length == njs_length("$tags")
            && njs_strncmp(&name.start[1], "tags", njs_length("tags")) == 0)
        {
            JS_FreeCString(cx, (char *) name.start);

            if (pdesc != NULL) {
                nm.start = NULL;
                nm.length = 0;

                pdesc->flags = JS_PROP_ENUMERABLE;
                pdesc->getter = JS_UNDEFINED;
                pdesc->setter = JS_UNDEFINED;
                pdesc->value  = qjs_xml_node_tags_handler(cx, current, &nm);
                if (JS_IsException(pdesc->value)) {
                    return -1;
                }
            }

            return 1;
        }

        if (name.length > njs_length("$tags$")
            && njs_strncmp(&name.start[1], "tags$", njs_length("tags$")) == 0)
        {
            nm.start = name.start + njs_length("$tags$");
            nm.length = name.length - njs_length("$tags$");

            value = qjs_xml_node_tags_handler(cx, current, &nm);
            JS_FreeCString(cx, (char *) name.start);
            if (JS_IsException(value)) {
                return -1;
            }

            if (pdesc != NULL) {
                pdesc->flags = JS_PROP_ENUMERABLE;
                pdesc->getter = JS_UNDEFINED;
                pdesc->setter = JS_UNDEFINED;
                pdesc->value = value;
            }

            return 1;
        }

        if (name.length > njs_length("$tag$")
            && njs_strncmp(&name.start[1], "tag$", njs_length("tag$")) == 0)
        {
            nm.length = name.length - njs_length("$tag$");
            nm.start = name.start + njs_length("$tag$");
            goto tag;
        }

        if (name.length == njs_length("$text")
            && njs_strncmp(&name.start[1], "text", njs_length("text")) == 0)
        {
            JS_FreeCString(cx, (char *) name.start);

            text = xmlNodeGetContent(current->node);
            if (text == NULL) {
                return 0;
            }

            if (pdesc != NULL) {
                pdesc->flags = JS_PROP_ENUMERABLE;
                pdesc->getter = JS_UNDEFINED;
                pdesc->setter = JS_UNDEFINED;
                pdesc->value = JS_NewString(cx, (char *) text);
                if (JS_IsException(pdesc->value)) {
                    xmlFree(text);
                    return -1;
                }
            }

            xmlFree(text);

            return 1;
        }
    }

    nm = name;

tag:

    value = qjs_xml_node_tag_handler(cx, current, &nm);
    JS_FreeCString(cx, (char *) name.start);
    if (JS_IsException(value)) {
        return -1;
    }

    if (JS_IsUndefined(value)) {
        return 0;
    }

    if (pdesc != NULL) {
        pdesc->flags = JS_PROP_ENUMERABLE;
        pdesc->getter = JS_UNDEFINED;
        pdesc->setter = JS_UNDEFINED;
        pdesc->value  = value;
    }

    return 1;
}


static int
qjs_xml_node_get_own_property_names(JSContext *cx, JSPropertyEnum **ptab,
    uint32_t *plen, JSValueConst obj)
{
    int             rc;
    JSValue         keys;
    xmlNode         *node, *current;
    qjs_xml_node_t  *tree;

    tree = JS_GetOpaque(obj, QJS_CORE_CLASS_ID_XML_NODE);
    if (tree == NULL) {
        (void) JS_ThrowInternalError(cx, "\"this\" is not an XMLNode");
        return -1;
    }

    keys = JS_NewObject(cx);
    if (JS_IsException(keys)) {
        return -1;
    }

    current = tree->node;

    if (current->name != NULL && current->type == XML_ELEMENT_NODE) {
        if (qjs_xml_push_string(cx, keys, "$name") < 0) {
            goto fail;
        }
    }

    if (current->ns != NULL) {
        if (qjs_xml_push_string(cx, keys, "$ns") < 0) {
            goto fail;
        }
    }

    if (current->properties != NULL) {
        if (qjs_xml_push_string(cx, keys, "$attrs") < 0) {
            goto fail;
        }
    }

    if (current->children != NULL && current->children->content != NULL) {
        if (qjs_xml_push_string(cx, keys, "$text") < 0) {
            goto fail;
        }
    }

    for (node = current->children; node != NULL; node = node->next) {
        if (node->type != XML_ELEMENT_NODE) {
            continue;
        }

        if (qjs_xml_push_string(cx, keys, "$tags") < 0) {
            goto fail;
        }

        break;
    }

    rc = JS_GetOwnPropertyNames(cx, ptab, plen, keys, JS_GPN_STRING_MASK);

    JS_FreeValue(cx, keys);

    return rc;

fail:

    JS_FreeValue(cx, keys);

    return -1;
}

static int
qjs_xml_node_set_property(JSContext *cx, JSValueConst obj, JSAtom atom,
    JSValueConst value, JSValueConst receiver, int flags)
{
    return qjs_xml_node_define_own_property(cx, obj, atom, value,
                                            JS_UNDEFINED, JS_UNDEFINED, flags);
}


static int
qjs_xml_node_delete_property(JSContext *cx, JSValueConst obj, JSAtom prop)
{
    return qjs_xml_node_define_own_property(cx, obj, prop, JS_UNDEFINED,
                                        JS_UNDEFINED, JS_UNDEFINED,
                                        JS_PROP_THROW);
}


static int
qjs_xml_node_define_own_property(JSContext *cx, JSValueConst obj, JSAtom atom,
    JSValueConst value, JSValueConst getter, JSValueConst setter, int flags)
{
    int        rc;
    njs_str_t  name, nm;

    name.start = (u_char *) JS_AtomToCString(cx, atom);
    if (name.start == NULL) {
        return -1;
    }

    name.length = njs_strlen(name.start);

    if (name.length > 1 && name.start[0] == '$') {
        if (name.length > njs_length("$attr$")
            && njs_strncmp(&name.start[1], "attr$", njs_length("attr$")) == 0)
        {
            nm.start = name.start + njs_length("$attr$");

            rc = qjs_xml_node_attr_modify(cx, obj, nm.start, value);

            JS_FreeCString(cx, (char *) name.start);

            return rc;
        }

        if (name.length > njs_length("$tag$")
            && njs_strncmp(&name.start[1], "tag$", njs_length("tag$")) == 0)
        {
            nm.start = name.start + njs_length("$tag$");
            nm.length = name.length - njs_length("$tag$");

            rc = qjs_xml_node_tag_modify(cx, obj, &nm, value);

            JS_FreeCString(cx, (char *) name.start);

            return rc;
        }

        if (name.length >= njs_length("$tags$")
            && njs_strncmp(&name.start[1], "tags$", njs_length("tags$")) == 0)
        {
            nm.start = name.start + njs_length("$tags$");
            nm.length = name.length - njs_length("$tags$");

            rc = qjs_xml_node_tags_modify(cx, obj, &nm, value);

            JS_FreeCString(cx, (char *) name.start);

            return rc;
        }

        if (name.length >= njs_length("$tags")
            && njs_strncmp(&name.start[1], "tags", njs_length("tags")) == 0)
        {
            nm.start = name.start + njs_length("$tags");
            nm.length = name.length - njs_length("$tags");

            rc = qjs_xml_node_tags_modify(cx, obj, &nm, value);

            JS_FreeCString(cx, (char *) name.start);

            return rc;
        }

        if (name.length == njs_length("$text")
            && njs_strncmp(&name.start[1], "text", njs_length("text")) == 0)
        {
            JS_FreeCString(cx, (char *) name.start);

            return qjs_xml_node_text_handler(cx, obj, value);
        }
    }

    rc = qjs_xml_node_tag_modify(cx, obj, &name, value);
    JS_FreeCString(cx, (char *) name.start);

    return rc;
}


static JSValue
qjs_xml_node_add_child(JSContext *cx, JSValueConst this_val,
    int argc, JSValueConst *argv)
{
    xmlNode         *copy, *node, *rnode;
    qjs_xml_node_t  *current;

    current = JS_GetOpaque(this_val, QJS_CORE_CLASS_ID_XML_NODE);
    if (current == NULL) {
        JS_ThrowTypeError(cx, "\"this\" is not a XMLNode object");
        return JS_EXCEPTION;
    }

    node = qjs_xml_node(cx, argv[0], NULL);
    if (node == NULL) {
        return JS_EXCEPTION;
    }

    copy = xmlDocCopyNode(current->node, current->doc->doc, 1);
    if (copy == NULL) {
        JS_ThrowInternalError(cx, "xmlDocCopyNode() failed");
        return JS_EXCEPTION;
    }

    node = xmlDocCopyNode(node, current->doc->doc, 1);
    if (node == NULL) {
        JS_ThrowInternalError(cx, "xmlDocCopyNode() failed");
        goto error;
    }

    rnode = xmlAddChild(copy, node);
    if (rnode == NULL) {
        xmlFreeNode(node);
        JS_ThrowInternalError(cx, "xmlAddChild() failed");
        goto error;
    }

    if (xmlReconciliateNs(current->doc->doc, copy) == -1) {
        JS_ThrowInternalError(cx, "xmlReconciliateNs() failed");
        goto error;
    }

    qjs_xml_replace_node(cx, current, copy);

    return JS_UNDEFINED;

error:

    xmlFreeNode(copy);

    return JS_EXCEPTION;
}


static JSValue
qjs_xml_node_remove_children(JSContext *cx, JSValueConst this_val,
    int argc, JSValueConst *argv)
{
    int             rc;
    njs_str_t       name;
    qjs_xml_node_t  *current;

    current = JS_GetOpaque(this_val, QJS_CORE_CLASS_ID_XML_NODE);
    if (current == NULL) {
        JS_ThrowTypeError(cx, "\"this\" is not a XMLNode object");
        return JS_EXCEPTION;
    }

    if (!JS_IsNullOrUndefined(argv[0])) {
        if (!JS_IsString(argv[0])) {
            JS_ThrowTypeError(cx, "selector is not a string");
            return JS_EXCEPTION;
        }

        name.start = (u_char *) JS_ToCString(cx, argv[0]);
        if (name.start == NULL) {
            return JS_EXCEPTION;
        }

        name.length = njs_strlen(name.start);

    } else {
        name.start = NULL;
        name.length = 0;
    }

    rc = qjs_xml_node_tag_modify(cx, this_val, &name, JS_UNDEFINED);

    if (name.start != NULL) {
        JS_FreeCString(cx, (char *) name.start);
    }

    if (rc < 0) {
        return JS_EXCEPTION;
    }

    return JS_UNDEFINED;
}


static JSValue
qjs_xml_node_set_attribute(JSContext *cx, JSValueConst this_val,
    int argc, JSValueConst *argv)
{
    const u_char  *name;

    if (!JS_IsString(argv[0])) {
        JS_ThrowTypeError(cx, "\"name\" argument is not a string");
        return JS_EXCEPTION;
    }

    name = (const u_char *) JS_ToCString(cx, argv[0]);

    if (qjs_xml_node_attr_modify(cx, this_val, name, argv[1]) < 0) {
        JS_FreeCString(cx, (char *) name);
        return JS_EXCEPTION;
    }

    JS_FreeCString(cx, (char *) name);

    return JS_UNDEFINED;
}


static JSValue
qjs_xml_node_remove_attribute(JSContext *cx, JSValueConst this_val,
    int argc, JSValueConst *argv)
{
    const u_char  *name;

    if (!JS_IsString(argv[0])) {
        JS_ThrowTypeError(cx, "\"name\" argument is not a string");
        return JS_EXCEPTION;
    }

    name = (const u_char *) JS_ToCString(cx, argv[0]);

    if (qjs_xml_node_attr_modify(cx, this_val, name, JS_UNDEFINED) < 0) {
        JS_FreeCString(cx, (char *) name);
        return JS_EXCEPTION;
    }

    JS_FreeCString(cx, (char *) name);

    return JS_UNDEFINED;
}


static JSValue
qjs_xml_node_remove_all_attributes(JSContext *cx, JSValueConst this_val,
    int argc, JSValueConst *argv)
{
    xmlNode         *node;
    qjs_xml_node_t  *current;

    current = JS_GetOpaque(this_val, QJS_CORE_CLASS_ID_XML_NODE);
    if (current == NULL) {
        JS_ThrowTypeError(cx, "\"this\" is not a XMLNode object");
        return JS_EXCEPTION;
    }

    node = current->node;

    if (node->properties != NULL) {
        xmlFreePropList(node->properties);
        node->properties = NULL;
    }

    return JS_UNDEFINED;
}


static JSValue
qjs_xml_node_set_text(JSContext *cx, JSValueConst this_val,
    int argc, JSValueConst *argv)
{
    if (qjs_xml_node_text_handler(cx, this_val, argv[0]) < 0) {
        return JS_EXCEPTION;
    }

    return JS_UNDEFINED;
}


static JSValue
qjs_xml_node_remove_text(JSContext *cx, JSValueConst this_val,
    int argc, JSValueConst *argv)
{
    if (qjs_xml_node_text_handler(cx, this_val, JS_UNDEFINED) < 0) {
        return JS_EXCEPTION;
    }

    return JS_UNDEFINED;
}


static void
qjs_xml_node_finalizer(JSRuntime *rt, JSValue val)
{
    qjs_xml_node_t  *node;

    node = JS_GetOpaque(val, QJS_CORE_CLASS_ID_XML_NODE);

    qjs_xml_doc_free(rt, node->doc);

    js_free_rt(rt, node);
}


static xmlNode *
qjs_xml_node(JSContext *cx, JSValueConst val, xmlDoc **doc)
{
    qjs_xml_doc_t   *tree;
    qjs_xml_node_t  *current;

    current = JS_GetOpaque(val, QJS_CORE_CLASS_ID_XML_NODE);
    if (current == NULL) {
        tree = JS_GetOpaque(val, QJS_CORE_CLASS_ID_XML_DOC);
        if (tree == NULL) {
            JS_ThrowInternalError(cx, "'this' is not XMLNode or XMLDoc");
            return NULL;
        }

        if (doc != NULL) {
            *doc = tree->doc;
        }

        return xmlDocGetRootElement(tree->doc);
    }

    if (doc != NULL) {
        *doc = current->doc->doc;
    }

    return current->node;
}


static JSValue
qjs_xml_attr_make(JSContext *cx, qjs_xml_doc_t *doc, xmlNode *node)
{
    JSValue         ret;
    qjs_xml_attr_t  *current;

    current = js_malloc(cx, sizeof(qjs_xml_attr_t));
    if (current == NULL) {
        JS_ThrowOutOfMemory(cx);
        return JS_EXCEPTION;
    }

    current->node = node;
    current->doc = doc;
    doc->ref_count++;

    ret = JS_NewObjectClass(cx, QJS_CORE_CLASS_ID_XML_ATTR);
    if (JS_IsException(ret)) {
        js_free(cx, current);
        return ret;
    }

    JS_SetOpaque(ret, current);

    return ret;
}


static int
qjs_xml_attr_get_own_property(JSContext *cx, JSPropertyDescriptor *pdesc,
    JSValueConst obj, JSAtom prop)
{
    size_t          size;
    u_char          *text;
    xmlAttr         *attr;
    njs_str_t       name;
    qjs_xml_attr_t  *current;

    current = JS_GetOpaque(obj, QJS_CORE_CLASS_ID_XML_ATTR);
    if (current == NULL) {
        (void) JS_ThrowInternalError(cx, "\"this\" is not an XMLAttr");
        return -1;
    }

    name.start = (u_char *) JS_AtomToCString(cx, prop);
    if (name.start == NULL) {
        return -1;
    }

    name.length = njs_strlen(name.start);

    for (attr = current->node->properties; attr != NULL; attr = attr->next) {
        if (attr->type != XML_ATTRIBUTE_NODE) {
            continue;
        }

        size = njs_strlen(attr->name);

        if (name.length != size
            || njs_strncmp(name.start, attr->name, size) != 0)
        {
            continue;
        }

        JS_FreeCString(cx, (char *) name.start);

        text = xmlNodeGetContent(attr->children);
        if (text == NULL) {
            return 0;
        }

        if (pdesc != NULL) {
            pdesc->flags = JS_PROP_ENUMERABLE;
            pdesc->getter = JS_UNDEFINED;
            pdesc->setter = JS_UNDEFINED;
            pdesc->value  = JS_NewString(cx, (char *) text);
            if (JS_IsException(pdesc->value)) {
                xmlFree(text);
                return -1;
            }
        }

        xmlFree(text);

        return 1;
    }

    JS_FreeCString(cx, (char *) name.start);

    return 0;
}


static int
qjs_xml_attr_get_own_property_names(JSContext *cx, JSPropertyEnum **ptab,
    uint32_t *plen, JSValueConst obj)
{
    int             rc;
    JSValue         keys;
    xmlAttr         *attr;
    qjs_xml_attr_t  *current;

    current = JS_GetOpaque(obj, QJS_CORE_CLASS_ID_XML_ATTR);
    if (current == NULL) {
        (void) JS_ThrowInternalError(cx, "\"this\" is not an XMLAttr");
        return -1;
    }

    keys = JS_NewObject(cx);
    if (JS_IsException(keys)) {
        return -1;
    }

    for (attr = current->node->properties; attr != NULL; attr = attr->next) {
        if (attr->type != XML_ATTRIBUTE_NODE) {
            continue;
        }

        if (qjs_xml_push_string(cx, keys, (char *) attr->name) < 0) {
            goto fail;
        }
    }

    rc = JS_GetOwnPropertyNames(cx, ptab, plen, keys, JS_GPN_STRING_MASK);

    JS_FreeValue(cx, keys);

    return rc;

fail:

    JS_FreeValue(cx, keys);

    return -1;
}


static void
qjs_xml_attr_finalizer(JSRuntime *rt, JSValue val)
{
    qjs_xml_attr_t  *attr;

    attr = JS_GetOpaque(val, QJS_CORE_CLASS_ID_XML_ATTR);

    qjs_xml_doc_free(rt, attr->doc);

    js_free_rt(rt, attr);
}


static u_char **
qjs_xml_parse_ns_list(JSContext *cx, u_char *src)
{
    u_char    *p, **buf, **out;
    size_t  size, idx;

    size = 8;
    p = src;

    buf = js_mallocz(cx, size * sizeof(char *));
    if (buf == NULL) {
        JS_ThrowOutOfMemory(cx);
        return NULL;
    }

    out = buf;

    while (*p != '\0') {
        idx = out - buf;

        if (idx >= size) {
            size *= 2;

            buf = js_realloc(cx, buf, size * sizeof(char *));
            if (buf == NULL) {
                JS_ThrowOutOfMemory(cx);
                return NULL;
            }

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


static int
qjs_xml_node_one_contains(qjs_xml_nset_t *nset, xmlNode *node, xmlNode *parent)
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
            return qjs_xml_node_one_contains(nset, parent, parent->parent);
        }

        return 0;

    case XML_NSET_TREE_INVERT:
    default:
        if (in != 0) {
            return 0;
        }

        if ((parent != NULL) && (parent->type == XML_ELEMENT_NODE)) {
            return qjs_xml_node_one_contains(nset, parent, parent->parent);
        }
    }

    return 1;
}


static int
qjs_xml_c14n_visibility_cb(void *user_data, xmlNode *node, xmlNode *parent)
{
    int             status;
    qjs_xml_nset_t  *n, *nset;

    nset = user_data;

    if (nset == NULL) {
        return 1;
    }

    status = 1;

    n = nset;

    do {
        if (status && !qjs_xml_node_one_contains(n, node, parent)) {
            status = 0;
        }

        n = n->next;
    } while (n != nset);

    return status;
}


static int
qjs_xml_buf_write_cb(void *context, const char *buffer, int len)
{
    njs_chb_t  *chain = context;

    njs_chb_append(chain, buffer, len);

    return chain->error ? -1 : len;
}


static qjs_xml_nset_t *
qjs_xml_nset_create(JSContext *cx, xmlDoc *doc, xmlNode *current,
    qjs_xml_nset_type_t type)
{
    xmlNodeSet      *nodes;
    qjs_xml_nset_t  *nset;

    nset = js_mallocz(cx, sizeof(qjs_xml_nset_t));
    if (nset == NULL) {
        JS_ThrowOutOfMemory(cx);
        return NULL;
    }

    nodes = xmlXPathNodeSetCreate(current);
    if (nodes == NULL) {
        js_free(cx, nset);
        JS_ThrowOutOfMemory(cx);
        return NULL;
    }

    nset->doc = doc;
    nset->type = type;
    nset->nodes = nodes;
    nset->next = nset->prev = nset;

    return nset;
}


static qjs_xml_nset_t *
qjs_xml_nset_add(qjs_xml_nset_t *nset, qjs_xml_nset_t *add)
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
qjs_xml_nset_free(JSContext *cx, qjs_xml_nset_t *nset)
{
    if (nset == NULL) {
        return;
    }

    if (nset->nodes != NULL) {
        xmlXPathFreeNodeSet(nset->nodes);
    }

    js_free(cx, nset);
}


static int
qjs_xml_encode_special_chars(JSContext *cx, njs_str_t *src, njs_str_t *out)
{
    size_t  len;
    u_char  *p, *dst, *end;

    len = 0;
    end = src->start + src->length;

    for (p = src->start; p < end; p++) {
        if (*p == '<' || *p == '>') {
            len += njs_length("&lt");
        }

        if (*p == '&' || *p == '\r') {
            len += njs_length("&amp");
        }

        if (*p == '"') {
            len += njs_length("&quot");
        }

        len += 1;
    }

    if (len == 0) {
        out->start = NULL;
        out->length = 0;

        return 0;
    }

    out->start = js_malloc(cx, len);
    if (out->start == NULL) {
        JS_ThrowOutOfMemory(cx);
        return -1;
    }

    dst = out->start;

    for (p = src->start; p < end; p++) {
        if (*p == '<') {
            *dst++ = '&';
            *dst++ = 'l';
            *dst++ = 't';
            *dst++ = ';';

        } else if (*p == '>') {
            *dst++ = '&';
            *dst++ = 'g';
            *dst++ = 't';
            *dst++ = ';';

        } else if (*p == '&') {
            *dst++ = '&';
            *dst++ = 'a';
            *dst++ = 'm';
            *dst++ = 'p';
            *dst++ = ';';

        } else if (*p == '"') {
            *dst++ = '&';
            *dst++ = 'q';
            *dst++ = 'u';
            *dst++ = 'o';
            *dst++ = 't';
            *dst++ = ';';

        } else if (*p == '\r') {
            *dst++ = '&';
            *dst++ = '#';
            *dst++ = '1';
            *dst++ = '3';
            *dst++ = ';';

        } else {
            *dst++ = *p;
        }
    }

    out->length = len;

    return 0;
}


static void
qjs_xml_replace_node(JSContext *cx, qjs_xml_node_t *node, xmlNode *current)
{
    xmlNode  *old;

    old = node->node;

    if (current != NULL) {
        old = xmlReplaceNode(old, current);

    } else {
        xmlUnlinkNode(old);
    }

    node->node = current;

    old->next = node->doc->free;
    node->doc->free = old;
}


static void
qjs_xml_error(JSContext *cx, qjs_xml_doc_t *current, const char *fmt, ...)
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

    JS_ThrowSyntaxError(cx, "%.*s", (int) (p - errstr), errstr);
}


static int
qjs_xml_module_init(JSContext *cx, JSModuleDef *m)
{
    JSValue  proto;

    proto = JS_NewObject(cx);
    if (JS_IsException(proto)) {
        return -1;
    }

    JS_SetPropertyFunctionList(cx, proto, qjs_xml_export,
                               njs_nitems(qjs_xml_export));

    if (JS_SetModuleExport(cx, m, "default", proto) != 0) {
        return -1;
    }

    return JS_SetModuleExportList(cx, m, qjs_xml_export,
                                  njs_nitems(qjs_xml_export));
}


static JSModuleDef *
qjs_xml_init(JSContext *cx, const char *name)
{
    int          rc;
    JSValue      proto;
    JSModuleDef  *m;

    if (!JS_IsRegisteredClass(JS_GetRuntime(cx),
                              QJS_CORE_CLASS_ID_XML_DOC))
    {
        if (JS_NewClass(JS_GetRuntime(cx), QJS_CORE_CLASS_ID_XML_DOC,
                        &qjs_xml_doc_class) < 0)
        {
            return NULL;
        }

        proto = JS_NewObject(cx);
        if (JS_IsException(proto)) {
            return NULL;
        }

        JS_SetPropertyFunctionList(cx, proto, qjs_xml_doc_proto,
                                   njs_nitems(qjs_xml_doc_proto));

        JS_SetClassProto(cx, QJS_CORE_CLASS_ID_XML_DOC, proto);

        if (JS_NewClass(JS_GetRuntime(cx), QJS_CORE_CLASS_ID_XML_NODE,
                        &qjs_xml_node_class) < 0)
        {
            return NULL;
        }

        proto = JS_NewObject(cx);
        if (JS_IsException(proto)) {
            return NULL;
        }

        JS_SetPropertyFunctionList(cx, proto, qjs_xml_node_proto,
                                   njs_nitems(qjs_xml_node_proto));

        JS_SetClassProto(cx, QJS_CORE_CLASS_ID_XML_NODE, proto);

        if (JS_NewClass(JS_GetRuntime(cx), QJS_CORE_CLASS_ID_XML_ATTR,
                        &qjs_xml_attr_class) < 0)
        {
            return NULL;
        }

        proto = JS_NewObject(cx);
        if (JS_IsException(proto)) {
            return NULL;
        }

        JS_SetPropertyFunctionList(cx, proto, qjs_xml_attr_proto,
                                   njs_nitems(qjs_xml_attr_proto));

        JS_SetClassProto(cx, QJS_CORE_CLASS_ID_XML_ATTR, proto);
    }

    m = JS_NewCModule(cx, name, qjs_xml_module_init);
    if (m == NULL) {
        return NULL;
    }

    JS_AddModuleExport(cx, m, "default");
    rc = JS_AddModuleExportList(cx, m, qjs_xml_export,
                                njs_nitems(qjs_xml_export));
    if (rc != 0) {
        return NULL;
    }

    return m;
}
