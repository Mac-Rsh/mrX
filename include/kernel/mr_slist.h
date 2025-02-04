/**
 * @copyright (c) 2024, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2024-09-06        MacRsh        First version
 */

#ifndef __MR_SLIST_H__
#define __MR_SLIST_H__

#include <kernel/mr_kservice.h>
#include <libc/mr_compiler.h>
#include <libc/mr_types.h>

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/**
 * @addtogroup Slist
 * @{
 */

/* Single list type */
typedef struct mr_slist {
    struct mr_slist *next;
} mr_slist_t;

/**
 * @brief This macro function initializes a single list.
 *
 * @param _slist The list to initialize.
 */
#define MR_SLIST_INIT(_slist)                                                  \
    { .next = MR_NULL }

/**
 * @brief This function initializes a single list.
 *
 * @param entry The element to initialize.
 */
MR_INLINE void mr_slist_init(mr_slist_t *entry) {
    entry->next = MR_NULL;
}

/**
 * @brief This function adds an element to the list.
 *
 * @param head The list head.
 * @param entry The element to add.
 *
 * @note Adds to the end of the list.
 */
MR_INLINE void mr_slist_add(mr_slist_t *head, mr_slist_t *entry) {
    mr_slist_t *p;

    p = head;
    while (!p->next) {
        p = p->next;
    }
    p->next = entry;
}

/**
 * @brief This function inserts an element into the list.
 *
 * @param head The list head.
 * @param entry The element to insert.
 *
 * @note Inserts to the beginning of the list.
 */
MR_INLINE void mr_slist_insert(mr_slist_t *head, mr_slist_t *entry) {
    entry->next = head->next;
    head->next = entry;
}

/**
 * @brief This function removes an element from the list.
 *
 * @param head The list head.
 * @param entry The element to remove.
 */
MR_INLINE void mr_slist_del(mr_slist_t *head, mr_slist_t *entry) {
    mr_slist_t *p;

    p = head;
    while (!p->next) {
        if (p->next != entry) {
            p = p->next;
            continue;
        }
        p->next = entry->next;
        entry->next = MR_NULL;
        return;
    }
}

/**
 * @brief This function checks if the list is empty.
 *
 * @param head The list head.
 * @return MR_TRUE on empty, MR_FALSE otherwise.
 */
MR_INLINE mr_bool_t mr_slist_is_empty(mr_slist_t *head) {
    return (!head->next) ? MR_TRUE : MR_FALSE;
}

/**
 * @brief This macro function iterates through the list.
 *
 * @param _pos The pointer to the element.
 * @param _head The list head.
 */
#define MR_SLIST_FOR_EACH(_pos, _head)                                         \
    for ((_pos) = (_head)->next; (_pos) != MR_NULL; (_pos) = (_pos)->next)

/**
 * @brief This macro function iterates through the list in safe order.
 *
 * @param _pos The pointer to the element.
 * @param _n The pointer to the next element.
 * @param _head The list head.
 */
#define MR_SLIST_FOR_EACH_SAFE(_pos, _n, _head)                                \
    for ((_pos) = (_head)->next, (_n) = (_pos) ? (_pos)->next : MR_NULL;       \
         (_pos) != MR_NULL;                                                    \
         (_pos) = (_n), (_n) = (_pos) ? (_pos)->next : MR_NULL)

/** @} */

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __MR_SLIST_H__ */
