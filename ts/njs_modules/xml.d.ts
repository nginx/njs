/// <reference path="../njs_core.d.ts" />

declare module "xml" {

    export interface XMLDoc {
        /**
         * The doc's root node.
         */
        readonly $root: XMLNode;

        /**
         * The doc's root by its name or undefined.
         */
        readonly [rootTagName: string]: XMLNode | undefined;
    }

    export interface XMLNode {
        /**
         * Adds a child node. Node is recursively copied before adding.
         * @param node - XMLNode to be added.
         * @since 0.7.11.
         */
        addChild(node: XMLNode): void;

        /**
         * node.$attr$xxx - value of the node's attribute "xxx".
         * @since 0.7.11 the property is writable.
         */
        [key: `$attr$${string}`]: string | undefined;

        /**
         * Removes attribute by name.
         * @param name - name of the attribute to remove.
         * @since 0.7.11.
         */
        removeAttribute(name: string): void;

        /**
         * Removes all the attribute of the node.
         * @since 0.7.11.
         */
        removeAllAttributes(): void;

        /**
         * Removes all the children tags named tag_name.
         * @param tag_name - name of the children's tags to remove.
         * If tag_name is absent all children tags are removed.
         * @since 0.7.11.
         */
        removeChildren(tag_name?:string): void;

        /**
         * Removes the text value of the node.
         * @since 0.7.11.
         */
        removeText(): void;

        /**
         * Sets a value for the attribute.
         * @param attr_name - name of the attribute to set.
         * @param value - value of the attribute to set. When value is null
         * the attribute is removed.
         * @since 0.7.11.
         */
        setAttribute(attr_name: string, value: string | null): void;

        /**
         * Sets a text value for the node.
         * @param text - a value to set as a text. If value is null the
         * node's text is deleted.
         * @since 0.7.11.
         */
        setText(text:string | null): void;

        /**
         * node.$attrs - an XMLAttr wrapper object for all the attributes
         * of the node.
         */
        readonly $attrs: XMLAttr;

        /**
         * node.$tag$xxx - the node's first child tag named "xxx".
         * @since 0.7.11 the property is writable.
         */
        [key: `$tag$${string}`]: XMLNode | undefined;

        /**
         * node.$tags$xxx - all children tags named "xxx" of the node.
         * @since 0.7.11 the property is writable.
         */
        [key: `$tags$${string}`]: XMLNode[] | undefined;

        /**
         * node.$name - the name of the node.
         */
        readonly $name: string;

        /**
         * node.$ns - the namespace of the node.
         */
        readonly $ns: string;

        /**
         * node.$parent - the parent node of the current node.
         */
        readonly $parent: string;

        /**
         * node.$text - the content of the node.
         * @since 0.7.11 the property is writable.
         */
        $text: string;

        /**
         * node.$tags - all the node's children tags.
         */
        $tags: XMLNode[] | undefined;
    }

    export interface XMLAttr {
        /**
         * attr.xxx is the attribute value of "xxx".
         */
        readonly [key: string]: string | undefined;
    }

    interface Xml {
        /**
         * Canonicalizes root_node and its children according to
         * https://www.w3.org/TR/xml-c14n/.
         *
         * @param root - XMLDoc or XMLNode.
         * @return Buffer object containing canonicalized output.
         */
        c14n(root: XMLDoc | XMLNode): Buffer;

        /**
         * Parses src buffer for an XML document and returns a wrapper object.
         *
         * @param src a string or a buffer with an XML document.
         * @return A XMLDoc wrapper object representing the parsed XML document.
         */
        parse(src: NjsStringLike): XMLDoc;

        /**
         * Canonicalizes root_node and its children according to
         * https://www.w3.org/tr/xml-exc-c14n/.
         *
         * @param root - XMLDoc or XMLNode.
         * @param excluding_node - allows to omit from the output a part of the
         * document corresponding to the excluding_node and its children.
         * @param withComments - a boolean (false by default). when withComments
         * is true canonicalization corresponds to
         * http://www.w3.org/2001/10/xml-exc-c14n#WithComments.
         * @param prefix_list - an optional string with a space separated namespace
         * prefixes for namespaces that should also be included into the output.
         * @return buffer object containing canonicalized output.
         */
        exclusiveC14n(root: XMLDoc | XMLNode, excluding_node?: XMLNode | null | undefined,
                      withComments?: boolean, prefix_list?: string): Buffer;

        /**
         * The alias to xml.x14n()
         * @since 0.7.11
         */
        serialize(root: XMLDoc | XMLNode): Buffer;

        /**
         * The same as xml.x14n() but returns the retval as a string.
         * @since 0.7.11
         */
        serializeToString(root: XMLDoc | XMLNode): string;
    }

    const xml: Xml;

    export default xml;
}
