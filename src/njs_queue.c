
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) NGINX, Inc.
 */


#include <njs_main.h>


/*
 * Find the middle queue element if the queue has odd number of elements,
 * or the first element of the queue's second part otherwise.
 */

njs_queue_link_t *
njs_queue_middle(njs_queue_t *queue)
{
    njs_queue_link_t  *middle, *next;

    middle = njs_queue_first(queue);

    if (middle == njs_queue_last(queue)) {
        return middle;
    }

    next = middle;

    for ( ;; ) {
        middle = njs_queue_next(middle);

        next = njs_queue_next(next);

        if (next == njs_queue_last(queue)) {
            return middle;
        }

        next = njs_queue_next(next);

        if (next == njs_queue_last(queue)) {
            return middle;
        }
    }
}


/*
 * njs_queue_sort() provides a stable sort because it uses the insertion
 * sort algorithm.  Its worst and average computational complexity is O^2.
 */

void
njs_queue_sort(njs_queue_t *queue,
    njs_int_t (*compare)(const void *data, const njs_queue_link_t *,
    const njs_queue_link_t *), const void *data)
{
    njs_queue_link_t  *link, *prev, *next;

    link = njs_queue_first(queue);

    if (link == njs_queue_last(queue)) {
        return;
    }

    for (link = njs_queue_next(link);
         link != njs_queue_tail(queue);
         link = next)
    {
        prev = njs_queue_prev(link);
        next = njs_queue_next(link);

        njs_queue_remove(link);

        do {
            if (compare(data, prev, link) <= 0) {
                break;
            }

            prev = njs_queue_prev(prev);

        } while (prev != njs_queue_head(queue));

        njs_queue_insert_after(prev, link);
    }
}
