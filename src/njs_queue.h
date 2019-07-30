
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) NGINX, Inc.
 */

#ifndef _NJS_QUEUE_H_INCLUDED_
#define _NJS_QUEUE_H_INCLUDED_


typedef struct njs_queue_link_s  njs_queue_link_t;

struct njs_queue_link_s {
    njs_queue_link_t  *prev;
    njs_queue_link_t  *next;
};


typedef struct {
    njs_queue_link_t  head;
} njs_queue_t;


#define njs_queue_init(queue)                                                 \
    do {                                                                      \
        (queue)->head.prev = &(queue)->head;                                  \
        (queue)->head.next = &(queue)->head;                                  \
    } while (0)


#define njs_queue_sentinel(link)                                              \
    do {                                                                      \
        (link)->prev = (link);                                                \
        (link)->next = (link);                                                \
    } while (0)


/*
 * Short-circuit a queue link to itself to allow once remove safely it
 * using njs_queue_remove().
 */

#define njs_queue_self(link)                                                  \
    njs_queue_sentinel(link)


#define njs_queue_is_empty(queue)                                             \
    (&(queue)->head == (queue)->head.prev)

/*
 * A loop to iterate all queue links starting from head:
 *
 *      njs_queue_link_t  link;
 *  } njs_type_t  *tp;
 *
 *
 *  for (lnk = njs_queue_first(queue);
 *       lnk != njs_queue_tail(queue);
 *       lnk = njs_queue_next(lnk))
 *  {
 *      tp = njs_queue_link_data(lnk, njs_type_t, link);
 *
 * or starting from tail:
 *
 *  for (lnk = njs_queue_last(queue);
 *       lnk != njs_queue_head(queue);
 *       lnk = njs_queue_next(lnk))
 *  {
 *      tp = njs_queue_link_data(lnk, njs_type_t, link);
 */

#define njs_queue_first(queue)                                                \
    (queue)->head.next


#define njs_queue_last(queue)                                                 \
    (queue)->head.prev


#define njs_queue_head(queue)                                                 \
    (&(queue)->head)


#define njs_queue_tail(queue)                                                 \
    (&(queue)->head)


#define njs_queue_next(link)                                                  \
    (link)->next


#define njs_queue_prev(link)                                                  \
    (link)->prev


#define njs_queue_insert_head(queue, link)                                    \
    do {                                                                      \
        (link)->next = (queue)->head.next;                                    \
        (link)->next->prev = (link);                                          \
        (link)->prev = &(queue)->head;                                        \
        (queue)->head.next = (link);                                          \
    } while (0)


#define njs_queue_insert_tail(queue, link)                                    \
    do {                                                                      \
        (link)->prev = (queue)->head.prev;                                    \
        (link)->prev->next = (link);                                          \
        (link)->next = &(queue)->head;                                        \
        (queue)->head.prev = (link);                                          \
    } while (0)


#define njs_queue_insert_after(target, link)                                  \
    do {                                                                      \
        (link)->next = (target)->next;                                        \
        (link)->next->prev = (link);                                          \
        (link)->prev = (target);                                              \
        (target)->next = (link);                                              \
    } while (0)


#define njs_queue_insert_before(target, link)                                 \
    do {                                                                      \
        (link)->next = (target);                                              \
        (link)->prev = (target)->prev;                                        \
        (target)->prev = (link);                                              \
        (link)->prev->next = (link);                                          \
    } while (0)


#if (NJS_DEBUG)

#define njs_queue_remove(link)                                                \
    do {                                                                      \
        (link)->next->prev = (link)->prev;                                    \
        (link)->prev->next = (link)->next;                                    \
        (link)->prev = NULL;                                                  \
        (link)->next = NULL;                                                  \
    } while (0)

#else

#define njs_queue_remove(link)                                                \
    do {                                                                      \
        (link)->next->prev = (link)->prev;                                    \
        (link)->prev->next = (link)->next;                                    \
    } while (0)

#endif


/*
 * Split the queue "queue" starting at the element "link",
 * the "tail" is the new tail queue.
 */

#define njs_queue_split(queue, link, tail)                                    \
    do {                                                                      \
        (tail)->head.prev = (queue)->head.prev;                               \
        (tail)->head.prev->next = &(tail)->head;                              \
        (tail)->head.next = (link);                                           \
        (queue)->head.prev = (link)->prev;                                    \
        (queue)->head.prev->next = &(queue)->head;                            \
        (link)->prev = &(tail)->head;                                         \
    } while (0)


/* Truncate the queue "queue" starting at element "link". */

#define njs_queue_truncate(queue, link)                                       \
    do {                                                                      \
        (queue)->head.prev = (link)->prev;                                    \
        (queue)->head.prev->next = &(queue)->head;                            \
    } while (0)


/* Add the queue "tail" to the queue "queue". */

#define njs_queue_add(queue, tail)                                            \
    do {                                                                      \
        (queue)->head.prev->next = (tail)->head.next;                         \
        (tail)->head.next->prev = (queue)->head.prev;                         \
        (queue)->head.prev = (tail)->head.prev;                               \
        (queue)->head.prev->next = &(queue)->head;                            \
    } while (0)


#define njs_queue_link_data(lnk, type, link)                                  \
    njs_container_of(lnk, type, link)


NJS_EXPORT njs_queue_link_t *njs_queue_middle(njs_queue_t *queue);
NJS_EXPORT void njs_queue_sort(njs_queue_t *queue,
    njs_int_t (*compare)(const void *, const njs_queue_link_t *,
    const njs_queue_link_t *), const void *data);


#endif /* _NJS_QUEUE_H_INCLUDED_ */
