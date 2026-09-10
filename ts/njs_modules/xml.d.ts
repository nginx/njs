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
         * Adds a recursive copy of a namespace-free child node.
         * @param node - XMLNode to be added.
         * @since 0.7.11.
         */
        addChild(node: XMLNode | XMLDoc): void;

        /**
         * node.$attr$xxx - value of the node's attribute "xxx".
         * Assigning null or undefined removes the attribute.
         */
        [key: `$attr$${string}`]: string | null | undefined;

        /**
         * Removes attribute by name.
         * @param name - name of the attribute to remove.
         * @since 0.7.11.
         */
        removeAttribute(name: string): void;

        /**
         * Removes all attributes of the node.
         * @since 0.7.11.
         */
        removeAllAttributes(): void;

        /**
         * Removes all the children tags named tag_name.
         * @param tag_name - name of the children's tags to remove.
         * If tag_name is absent all children tags are removed.
         * @since 0.7.11.
         */
        removeChildren(tag_name?: string | null): void;

        /**
         * Removes the text value of the node.
         * @since 0.7.11.
         */
        removeText(): void;

        /**
         * Sets a value for the attribute.
         * @param attr_name - name of the attribute to set.
         * @param value - value of the attribute to set. When value is null or
         * undefined, the attribute is removed.
         * @since 0.7.11.
         */
        setAttribute(attr_name: string, value: string | null | undefined): void;

        /**
         * Sets a text value for the node.
         * @param text - a value to set as text. If value is null or undefined,
         * the node's text is deleted.
         * @since 0.7.11.
         */
        setText(text: string | null | undefined): void;

        /**
         * node.$attrs - an XMLAttr wrapper object for all node attributes,
         * or undefined when the node has no attributes.
         */
        readonly $attrs: XMLAttr | undefined;

        /**
         * node.$tag$xxx - the node's first child tag named "xxx".
         * Assigning a value throws TypeError at runtime. Deleting the property
         * removes matching child tags.
         */
        [key: `$tag$${string}`]: XMLNode | undefined;

        /**
         * node.$tags$xxx - all children tags named "xxx" of the node.
         * Assigning or deleting the property throws TypeError at runtime.
         */
        [key: `$tags$${string}`]: XMLNode[] | undefined;

        /**
         * node.$name - the name of the node.
         */
        readonly $name: string;

        /**
         * node.$ns - the namespace URI of the node, or undefined when the
         * node has no namespace.
         */
        readonly $ns: string | undefined;

        /**
         * node.$parent - the parent node, or undefined for a document root
         * or a detached node.
         */
        readonly $parent: XMLNode | undefined;

        /**
         * node.$text - the content of the node.
         * Assigning null or undefined removes the text.
         */
        get $text(): string;
        set $text(value: string | null | undefined);

        /**
         * node.$tags - all child tags. Assigning an array replaces all child
         * nodes with recursive copies of namespace-free array elements.
         * Assigning null or undefined removes all child nodes.
         */
        get $tags(): XMLNode[] | undefined;
        set $tags(value: Array<XMLNode | XMLDoc> | null | undefined);
    }

    export interface XMLAttr {
        /**
         * attr.xxx is the attribute value of "xxx".
         */
        readonly [key: string]: string | undefined;
    }

    interface Xml {
        /**
         * Canonicalizes root and its children according to
         * https://www.w3.org/TR/xml-c14n/.
         *
         * @param root - XMLDoc or XMLNode.
         * @param excludingNode - a node to omit from the output.
         * @return Buffer object containing canonicalized output.
         */
        c14n(root: XMLDoc | XMLNode,
             excludingNode?: XMLNode | null | undefined): Buffer;

        /**
         * Parses src for an XML document and returns a wrapper object.
         *
         * @param src - a string or Buffer with an XML document.
         * @return An XMLDoc wrapper object representing the parsed XML document.
         */
        parse(src: string | Buffer): XMLDoc;

        /**
         * Canonicalizes root and its children according to
         * https://www.w3.org/TR/xml-exc-c14n/.
         *
         * @param root - XMLDoc or XMLNode.
         * @param excludingNode - allows omitting the node and its children.
         * @param withComments - a boolean (false by default). When withComments
         * is true canonicalization corresponds to
         * http://www.w3.org/2001/10/xml-exc-c14n#WithComments.
         * @param prefixList - an optional string with space-separated namespace
         * prefixes for namespaces that should also be included into the output.
         * @return Buffer object containing canonicalized output.
         */
        exclusiveC14n(root: XMLDoc | XMLNode,
                       excludingNode?: XMLNode | null | undefined,
                       withComments?: boolean, prefixList?: string): Buffer;

        /**
         * Alias for xml.c14n().
         * @since 0.7.11
         */
        serialize(root: XMLDoc | XMLNode,
                  excludingNode?: XMLNode | null | undefined): Buffer;

        /**
         * The same as xml.c14n() but returns the result as a string.
         * @since 0.7.11
         */
        serializeToString(root: XMLDoc | XMLNode,
                          excludingNode?: XMLNode | null | undefined): string;
    }

    const xml: Xml;

    export default xml;
}
