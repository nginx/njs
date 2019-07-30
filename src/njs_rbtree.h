
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) NGINX, Inc.
 */

#ifndef _NJS_RBTREE_H_INCLUDED_
#define _NJS_RBTREE_H_INCLUDED_


typedef struct njs_rbtree_node_s  njs_rbtree_node_t;

struct njs_rbtree_node_s {
    njs_rbtree_node_t         *left;
    njs_rbtree_node_t         *right;
    njs_rbtree_node_t         *parent;

    uint8_t                   color;
};


typedef struct {
    njs_rbtree_node_t         *left;
    njs_rbtree_node_t         *right;
    njs_rbtree_node_t         *parent;
} njs_rbtree_part_t;


#define NJS_RBTREE_NODE(node)                                                 \
    njs_rbtree_part_t         node;                                           \
    uint8_t                   node##_color


#define NJS_RBTREE_NODE_INIT  { NULL, NULL, NULL }, 0


typedef struct {
    njs_rbtree_node_t         sentinel;
} njs_rbtree_t;


typedef intptr_t (*njs_rbtree_compare_t)(njs_rbtree_node_t *node1,
    njs_rbtree_node_t *node2);


#define njs_rbtree_root(tree)                                                 \
    ((tree)->sentinel.left)


#define njs_rbtree_sentinel(tree)                                             \
    (&(tree)->sentinel)


#define njs_rbtree_is_empty(tree)                                             \
    (njs_rbtree_root(tree) == njs_rbtree_sentinel(tree))


#define njs_rbtree_min(tree)                                                  \
    njs_rbtree_branch_min(tree, &(tree)->sentinel)


njs_inline njs_rbtree_node_t *
njs_rbtree_branch_min(njs_rbtree_t *tree, njs_rbtree_node_t *node)
{
    while (node->left != njs_rbtree_sentinel(tree)) {
        node = node->left;
    }

    return node;
}


#define njs_rbtree_is_there_successor(tree, node)                             \
    ((node) != njs_rbtree_sentinel(tree))


njs_inline njs_rbtree_node_t *
njs_rbtree_node_successor(njs_rbtree_t *tree, njs_rbtree_node_t *node)
{
    njs_rbtree_node_t  *parent;

    if (node->right != njs_rbtree_sentinel(tree)) {
        return njs_rbtree_branch_min(tree, node->right);
    }

    for ( ;; ) {
        parent = node->parent;

        /*
         * Explicit test for a root node is not required here, because
         * the root node is always the left child of the sentinel.
         */
        if (node == parent->left) {
            return parent;
        }

        node = parent;
    }
}


NJS_EXPORT void njs_rbtree_init(njs_rbtree_t *tree,
    njs_rbtree_compare_t compare);
NJS_EXPORT void njs_rbtree_insert(njs_rbtree_t *tree, njs_rbtree_part_t *node);
NJS_EXPORT njs_rbtree_node_t *njs_rbtree_find(njs_rbtree_t *tree,
    njs_rbtree_part_t *node);
NJS_EXPORT njs_rbtree_node_t *njs_rbtree_find_less_or_equal(njs_rbtree_t *tree,
    njs_rbtree_part_t *node);
NJS_EXPORT njs_rbtree_node_t
    *njs_rbtree_find_greater_or_equal(njs_rbtree_t *tree,
    njs_rbtree_part_t *node);
NJS_EXPORT void njs_rbtree_delete(njs_rbtree_t *tree, njs_rbtree_part_t *node);

/*
 * njs_rbtree_destroy_next() is iterator to use only while rbtree destruction.
 * It deletes a node from rbtree and returns the node.  The rbtree is not
 * rebalanced after deletion.  At the beginning the "next" parameter should
 * be equal to rbtree root.  The iterator should be called in loop until
 * the "next" parameter will be equal to the rbtree sentinel.  No other
 * operations must be performed on the rbtree while destruction.
 */
NJS_EXPORT njs_rbtree_node_t *njs_rbtree_destroy_next(njs_rbtree_t *tree,
    njs_rbtree_node_t **next);


#endif /* _NJS_RBTREE_H_INCLUDED_ */
