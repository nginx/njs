/// <reference path="../njs_core.d.ts" />

declare module "xml" {

    type XMLTagName =
        | `_${string}`
        | `a${string}`
        | `b${string}`
        | `c${string}`
        | `d${string}`
        | `e${string}`
        | `f${string}`
        | `g${string}`
        | `h${string}`
        | `i${string}`
        | `j${string}`
        | `k${string}`
        | `l${string}`
        | `m${string}`
        | `n${string}`
        | `o${string}`
        | `p${string}`
        | `q${string}`
        | `r${string}`
        | `s${string}`
        | `t${string}`
        | `u${string}`
        | `v${string}`
        | `w${string}`
        | `x${string}`
        | `y${string}`
        | `z${string}`
        | `A${string}`
        | `B${string}`
        | `C${string}`
        | `D${string}`
        | `E${string}`
        | `F${string}`
        | `G${string}`
        | `H${string}`
        | `I${string}`
        | `J${string}`
        | `K${string}`
        | `L${string}`
        | `M${string}`
        | `N${string}`
        | `O${string}`
        | `P${string}`
        | `Q${string}`
        | `R${string}`
        | `S${string}`
        | `T${string}`
        | `U${string}`
        | `V${string}`
        | `W${string}`
        | `X${string}`
        | `Y${string}`
        | `Z${string}`;

    export interface XMLDoc {
        /**
         * The doc's root node.
         */
        readonly $root: XMLNode;

        /**
         * The doc's root by its name or undefined.
         */
        readonly [rootTagName: XMLTagName]: XMLNode | undefined;
    }

    export interface XMLNode {
        /**
         * node.$attr$xxx - the node's attribute value of "xxx".
         */
        readonly [key: `$attr$${string}`]: string | undefined;

        /**
         * node.$attrs - an XMLAttr wrapper object for all the attributes
         * of the node.
         */
        readonly $attrs: XMLAttr;

        /**
         * node.$tag$xxx - the node's first child tag named "xxx".
         */
        readonly [key: `$tag$${string}`]: XMLNode | undefined;

        /**
         * node.$tags$xxx - all children tags named "xxx" of the node.
         */
        readonly [key: `$tags$${string}`]: XMLNode[] | undefined;

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
         */
        readonly $text: string;

        /**
         * node.$tags - all the node's children tags.
         */
        readonly $tags: XMLNode[] | undefined;

        /**
         * node.xxx is the same as node.$tag$xxx.
         */
        readonly [key: XMLTagName]: XMLNode | undefined;
    }

    export interface XMLAttr {
        /**
         * attr.xxx is the attribute value of "xxx".
         */
        readonly [key: string]: string | undefined;
    }

    interface Xml {
        /**
         * Parses src buffer for an XML document and returns a wrapper object.
         *
         * @param src a string or a buffer with an XML document.
         * @return A XMLDoc wrapper object representing the parsed XML document.
         */
        parse(src: NjsStringLike): XMLDoc;

        /**
         * Canonicalizes root_node and its children according to
         * https://www.w3.org/TR/xml-exc-c14n/.
         *
         * @param root - XMLDoc or XMLNode.
         * @param excluding_node - allows to omit from the output a part of the
         * document corresponding to the excluding_node and its children.
         * @param withComments - a boolean (false by default). When withComments
         * is true canonicalization corresponds to
         * http://www.w3.org/2001/10/xml-exc-c14n#WithComments.
         * @param prefix_list - an optional string with a space separated namespace
         * prefixes for namespaces that should also be included into the output.
         * @return Buffer object containing canonicalized output.
         */
        exclusiveC14n(root: XMLDoc | XMLNode, excluding_node?: XMLNode | null | undefined,
                      withComments?: boolean, prefix_list?: string): Buffer;
    }

    const xml: Xml;

    export default xml;
}
