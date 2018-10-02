
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) NGINX, Inc.
 */

#include <nxt_auto_config.h>
#include <nxt_types.h>
#include <nxt_clang.h>
#include <nxt_stub.h>
#include <nxt_string.h>
#include <nxt_rbtree.h>
#include <nxt_murmur_hash.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>


typedef struct {
    NXT_RBTREE_NODE  (node);
    uint32_t         key;
} nxt_rbtree_test_t;


static intptr_t rbtree_unit_test_comparison(nxt_rbtree_node_t *node1,
    nxt_rbtree_node_t *node2);
static nxt_int_t rbtree_unit_test_compare(uint32_t key1, uint32_t key2);
static int nxt_cdecl rbtree_unit_test_sort_cmp(const void *one,
    const void *two);


static nxt_int_t
rbtree_unit_test(nxt_uint_t n)
{
    void               *mark;
    uint32_t           key, *keys;
    nxt_uint_t         i;
    nxt_rbtree_t       tree;
    nxt_rbtree_node_t  *node;
    nxt_rbtree_test_t  *items, *item;

    printf("rbtree unit test started: %ld nodes\n", (long) n);

    nxt_rbtree_init(&tree, rbtree_unit_test_comparison);

    mark = tree.sentinel.right;

    items = malloc(n * sizeof(nxt_rbtree_test_t));
    if (items == NULL) {
        return NXT_ERROR;
    }

    keys = malloc(n * sizeof(uint32_t));
    if (keys == NULL) {
        free(items);
        return NXT_ERROR;
    }

    key = 0;

    for (i = 0; i < n; i++) {
        key = nxt_murmur_hash2(&key, sizeof(uint32_t));
        keys[i] = key;
        items[i].key = key;
    }

    qsort(keys, n, sizeof(uint32_t), rbtree_unit_test_sort_cmp);

    for (i = 0; i < n; i++) {
        nxt_rbtree_insert(&tree, &items[i].node);
    }

    for (i = 0; i < n; i++) {
        node = nxt_rbtree_find(&tree, &items[i].node);

        if (node != (nxt_rbtree_node_t *) &items[i].node) {
            printf("rbtree unit test failed: %08X not found\n", items[i].key);
            goto fail;
        }
    }

    i = 0;
    node = nxt_rbtree_min(&tree);

    while (nxt_rbtree_is_there_successor(&tree, node)) {

        item = (nxt_rbtree_test_t *) node;

        if (keys[i] != item->key) {
            printf("rbtree unit test failed: %ld: %08X %08X\n",
                   (long) i, keys[i], item->key);
            goto fail;
        }

        i++;
        node = nxt_rbtree_node_successor(&tree, node);
    }

    if (i != n) {
        printf("rbtree unit test failed: %ld\n", (long) i);
        goto fail;
    }

    for (i = 0; i < n; i++) {
        nxt_rbtree_delete(&tree, &items[i].node);
        nxt_memset(&items[i], 0xA5, sizeof(nxt_rbtree_test_t));
    }

    if (!nxt_rbtree_is_empty(&tree)) {
        printf("rbtree unit test failed: tree is not empty\n");
        goto fail;
    }

    /* Check that the sentinel callback was not modified. */

    if (mark != tree.sentinel.right) {
        printf("rbtree sentinel unit test failed\n");
        goto fail;
    }

    free(keys);
    free(items);

    printf("rbtree unit test passed\n");

    return NXT_OK;

fail:

    free(keys);
    free(items);

    return NXT_ERROR;
}


static intptr_t
rbtree_unit_test_comparison(nxt_rbtree_node_t *node1, nxt_rbtree_node_t *node2)
{
    nxt_rbtree_test_t  *item1, *item2;

    item1 = (nxt_rbtree_test_t *) node1;
    item2 = (nxt_rbtree_test_t *) node2;

    return rbtree_unit_test_compare(item1->key, item2->key);
}


/*
 * Subtraction cannot be used in these comparison functions because
 * the key values are spread uniform in whole 0 .. 2^32 range but are
 * not grouped around some value as timeout values are.
 */

static nxt_int_t
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


static int nxt_cdecl
rbtree_unit_test_sort_cmp(const void *one, const void *two)
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
