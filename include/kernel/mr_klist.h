/**
 * @copyright (c) 2024, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2024-09-06        MacRsh        First version
 */

#ifndef __MR_KLIST_H__
#define __MR_KLIST_H__

#include <kernel/mr_kservice.h>
#include <libc/mr_compiler.h>
#include <libc/mr_types.h>

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/**
 * @addtogroup Klist
 * @{
 */

/* Double list type */
typedef struct mr_klist {
    struct mr_klist *next;
    struct mr_klist *prev;
} mr_klist_t;

/**
 * @brief This macro function initializes a list.
 *
 * @param _klist The list to initialize.
 */
#define MR_KLIST_INIT(_klist)                                                  \
    { .next = (_klist), .prev = (_klist) }

/**
 * @brief This function initializes a list.
 *
 * @param entry The element to initialize.
 */
MR_INLINE void mr_klist_init(mr_klist_t *entry) {
    entry->next = entry->prev = entry;
}

/**
 * @brief This function adds an element to the list.
 *
 * @param head The list head.
 * @param entry The element to add.
 *
 * @note Adds to the end of the list.
 */
MR_INLINE void mr_klist_add(mr_klist_t *head, mr_klist_t *entry) {
    head->prev->next = entry;
    entry->prev = head->prev;
    head->prev = entry;
    entry->next = head;
}

/**
 * @brief This function inserts an element into the list.
 *
 * @param head The list head.
 * @param entry The element to insert.
 *
 * @note Inserts to the beginning of the list.
 */
MR_INLINE void mr_klist_insert(mr_klist_t *head, mr_klist_t *entry) {
    head->next->prev = entry;
    entry->next = head->next;
    head->next = entry;
    entry->prev = head;
}

/**
 * @brief This function removes an element from the list.
 *
 * @param entry The element to remove.
 */
MR_INLINE void mr_klist_del(mr_klist_t *entry) {
    entry->prev->next = entry->next;
    entry->next->prev = entry->prev;
    entry->next = entry->prev = entry;
}

/**
 * @brief This function replaces an element in the list.
 *
 * @param new The new element.
 * @param old The old element.
 */
MR_INLINE void mr_klist_replace(mr_klist_t *new, mr_klist_t *old) {
    new->next = old->next;
    new->next->prev = new;
    new->prev = old->prev;
    new->prev->next = new;
    old->next = old->prev = old;
}

/**
 * @brief This function swaps two elements in the list.
 *
 * @param entry1 The first element.
 * @param entry2 The second element.
 */
MR_INLINE void mr_klist_swap(mr_klist_t *entry1, mr_klist_t *entry2) {
    mr_klist_t *tmp;

    tmp = entry2->prev;
    mr_klist_del(entry2);
    mr_klist_replace(entry1, entry2);
    if (tmp == entry1) {
        tmp = entry2;
    }
    mr_klist_add(tmp, entry1);
}

/**
 * @brief This function moves an element to the head of the list.
 *
 * @param entry The element to move.
 * @param head The list head.
 *
 * @note Move to the end of the list.
 */
MR_INLINE void mr_klist_move(mr_klist_t *entry, mr_klist_t *head) {
    mr_klist_del(entry);
    mr_klist_add(head, entry);
}

/**
 * @brief This function checks if the list is empty.
 *
 * @param head The list head.
 * @return MR_TRUE on empty, MR_FALSE otherwise.
 */
MR_INLINE mr_bool_t mr_klist_is_empty(mr_klist_t *head) {
    return (head->next == head) ? MR_TRUE : MR_FALSE;
}

/**
 * @brief This macro function gets the container of an element.
 *
 * @param _ptr The pointer to the element.
 * @param _type The type of the element.
 * @param _member The member of the element.
 * @return The container of the element.
 */
#define MR_KLIST_ENTRY(_ptr, _type, _member)                                   \
    MR_CONTAINER_OF(_ptr, _type, _member)

/**
 * @brief This macro function iterates through the list.
 *
 * @param _pos The pointer to the element.
 * @param _head The list head.
 */
#define MR_KLIST_FOR_EACH(_pos, _head)                                         \
    for ((_pos) = (_head)->next; (_pos) != (_head); (_pos) = (_pos)->next)

/**
 * @brief This macro function iterates through the list in safe order.
 *
 * @param _pos The pointer to the element.
 * @param _n The pointer to the next element.
 * @param _head The list head.
 */
#define MR_KLIST_FOR_EACH_SAFE(_pos, _n, _head)                                \
    for ((_pos) = (_head)->next, (_n) = (_pos)->next; (_pos) != (_head);       \
         (_pos) = (_n), (_n) = (_pos)->next)

/** @} */

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __MR_KLIST_H__ */
