
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) NGINX, Inc.
 */


#include <njs_main.h>


typedef struct {
    NJS_RBTREE_NODE  (node);
    uint32_t         key;
} njs_rbtree_test_t;


static intptr_t rbtree_unit_test_comparison(njs_rbtree_node_t *node1,
    njs_rbtree_node_t *node2);
static njs_int_t rbtree_unit_test_compare(uint32_t key1, uint32_t key2);
static int njs_cdecl rbtree_unit_test_sort_cmp(const void *one,
    const void *two, void *ctx);


static njs_int_t
rbtree_unit_test(njs_uint_t n)
{
    void               *mark;
    uint32_t           key, *keys;
    njs_uint_t         i;
    njs_rbtree_t       tree;
    njs_rbtree_node_t  *node;
    njs_rbtree_test_t  *items, *item;

    njs_printf("rbtree unit test started: %l nodes\n", (long) n);

    njs_rbtree_init(&tree, rbtree_unit_test_comparison);

    mark = tree.sentinel.right;

    items = malloc(n * sizeof(njs_rbtree_test_t));
    if (items == NULL) {
        return NJS_ERROR;
    }

    keys = malloc(n * sizeof(uint32_t));
    if (keys == NULL) {
        free(items);
        return NJS_ERROR;
    }

    key = 0;

    for (i = 0; i < n; i++) {
        key = njs_murmur_hash2(&key, sizeof(uint32_t));
        keys[i] = key;
        items[i].key = key;
    }

    njs_qsort(keys, n, sizeof(uint32_t), rbtree_unit_test_sort_cmp, NULL);

    for (i = 0; i < n; i++) {
        njs_rbtree_insert(&tree, &items[i].node);
    }

    for (i = 0; i < n; i++) {
        node = njs_rbtree_find(&tree, &items[i].node);

        if (node != (njs_rbtree_node_t *) &items[i].node) {
            njs_printf("rbtree unit test failed: %08uXD not found\n",
                       items[i].key);
            goto fail;
        }
    }

    i = 0;
    node = njs_rbtree_min(&tree);

    while (njs_rbtree_is_there_successor(&tree, node)) {

        item = (njs_rbtree_test_t *) node;

        if (keys[i] != item->key) {
            njs_printf("rbtree unit test failed: %l: %08uXD %08uXD\n",
                       (long) i, keys[i], item->key);
            goto fail;
        }

        i++;
        node = njs_rbtree_node_successor(&tree, node);
    }

    if (i != n) {
        njs_printf("rbtree unit test failed: %l\n", (long) i);
        goto fail;
    }

    for (i = 0; i < n; i++) {
        njs_rbtree_delete(&tree, &items[i].node);
        njs_memset(&items[i], 0xA5, sizeof(njs_rbtree_test_t));
    }

    if (!njs_rbtree_is_empty(&tree)) {
        njs_printf("rbtree unit test failed: tree is not empty\n");
        goto fail;
    }

    /* Check that the sentinel callback was not modified. */

    if (mark != tree.sentinel.right) {
        njs_printf("rbtree sentinel unit test failed\n");
        goto fail;
    }

    free(keys);
    free(items);

    njs_printf("rbtree unit test passed\n");

    return NJS_OK;

fail:

    free(keys);
    free(items);

    return NJS_ERROR;
}


static intptr_t
rbtree_unit_test_comparison(njs_rbtree_node_t *node1, njs_rbtree_node_t *node2)
{
    njs_rbtree_test_t  *item1, *item2;

    item1 = (njs_rbtree_test_t *) node1;
    item2 = (njs_rbtree_test_t *) node2;

    return rbtree_unit_test_compare(item1->key, item2->key);
}


/*
 * Subtraction cannot be used in these comparison functions because
 * the key values are spread uniform in whole 0 .. 2^32 range but are
 * not grouped around some value as timeout values are.
 */

static njs_int_t
rbtree_unit_test_compare(uint32_t key1, uint32_t key2)
{
    if (key1 < key2) {
        return -1;
    }

    if (key1 == key2) {
        return 0;
    }

    return 1;
}


static int njs_cdecl
rbtree_unit_test_sort_cmp(const void *one, const void *two, void *ctx)
{
    const uint32_t  *first, *second;

    first = one;
    second = two;

    if (*first < *second) {
        return -1;
    }

    if (*first == *second) {
        return 0;
    }

    return 1;
}


int
main(void)
{
    return rbtree_unit_test(1000 * 1000);
}
