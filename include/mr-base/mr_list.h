/**
 * @copyright (c) 2024, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2024-09-06        MacRsh        First version
 */

#ifndef __MR_LIST_H__
#define __MR_LIST_H__

#include <mr-base/mr_compiler.h>
#include <mr-base/mr_service.h>
#include <mr-base/mr_types.h>

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/**
 * @addtogroup List
 * @{
 */

/* Double list type */
typedef struct mr_list {
    struct mr_list *next;
    struct mr_list *prev;
} mr_list_t;

/**
 * @brief This macro function initializes a list.
 *
 * @param _list The list to initialize.
 */
#define MR_LIST_INIT(_list)             {&(_list), &(_list)}

/**
 * @brief This function initializes a list.
 *
 * @param entry The element to initialize.
 */
MR_INLINE void mr_list_init(mr_list_t *entry) {
    entry->next = entry->prev = entry;
}

/**
 * @brief This function adds an element to the list.
 *
 * @param head The list head.
 * @param entry The element to add.
 */
MR_INLINE void mr_list_add(mr_list_t *head, mr_list_t *entry) {
    head->prev->next = entry;
    entry->prev = head->prev;
    head->prev = entry;
    entry->next = head;
}

/**
 * @brief This function adds an element to the head of the list.
 *
 * @param head The list head.
 * @param entry The element to add.
 */
MR_INLINE void mr_list_add_head(mr_list_t *head, mr_list_t *entry) {
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
MR_INLINE void mr_list_del(mr_list_t *entry) {
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
MR_INLINE void mr_list_replace(mr_list_t *new, mr_list_t *old) {
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
MR_INLINE void mr_list_swap(mr_list_t *entry1, mr_list_t *entry2) {
    mr_list_t *tmp;

    tmp = entry2->prev;
    mr_list_del(entry2);
    mr_list_replace(entry1, entry2);
    if (tmp == entry1) {
        tmp = entry2;
    }
    mr_list_add(tmp, entry1);
}

/**
 * @brief This function moves an element to the head of the list.
 *
 * @param entry The element to move.
 * @param head The list head.
 */
MR_INLINE void mr_list_move(mr_list_t *entry, mr_list_t *head) {
    mr_list_del(entry);
    mr_list_add(head, entry);
}

/**
 * @brief This function moves an element to the head of the list.
 *
 * @param entry The element to move.
 * @param head The list head.
 */
MR_INLINE void mr_list_move_head(mr_list_t *entry, mr_list_t *head) {
    mr_list_del(entry);
    mr_list_add_head(head, entry);
}

/**
 * @brief This function checks if the list is empty.
 *
 * @param head The list head.
 * @return MR_TRUE on empty, MR_FALSE otherwise.
 */
MR_INLINE mr_bool_t mr_list_is_empty(mr_list_t *head) {
    return head->next == head ? MR_TRUE : MR_FALSE;
}

/**
 * @brief This function rotates the list to the left.
 *
 * @param head The list head.
 */
MR_INLINE void mr_list_rotate_left(mr_list_t *head) {
    mr_list_t *tmp;

    if (!mr_list_is_empty(head)) {
        tmp = head->next;
        mr_list_move_head(tmp, head);
    }
}

/**
 * @brief This function rotates the list to the right.
 *
 * @param head The list head.
 */
MR_INLINE void mr_list_rotate_right(mr_list_t *head) {
    mr_list_t *tmp;

    if (!mr_list_is_empty(head)) {
        tmp = head->prev;
        mr_list_move(tmp, head);
    }
}

/**
 * @brief This function splices a list into another list.
 *
 * @param list The list to splice.
 * @param head The list head.
 */
MR_INLINE void mr_list_splice(mr_list_t *list, mr_list_t *head) {
    mr_list_t *tmp;

    tmp = head->prev;
    head->prev->next = list;
    head->prev = list->prev;
    list->prev->next = head;
    list->prev = tmp;
}

/**
 * @brief This macro function gets the container of an element.
 *
 * @param _ptr The pointer to the element.
 * @param _type The type of the element.
 * @param _member The member of the element.
 * @return The container of the element.
 */
#define MR_LIST_ENTRY(_ptr, _type, _member)                                    \
    MR_CONTAINER_OF(_ptr, _type, _member)

/**
 * @brief This macro function iterates through the list.
 *
 * @param _pos The pointer to the element.
 * @param _head The list head.
 */
#define MR_LIST_FOR_EACH(_pos, _head)                                          \
    for ((_pos) = (_head)->next; (_pos) != (_head); (_pos) = (_pos)->next)

/**
 * @brief This macro function iterates through the list in safe order.
 *
 * @param _pos The pointer to the element.
 * @param _n The pointer to the next element.
 * @param _head The list head.
 */
#define MR_LIST_FOR_EACH_SAFE(_pos, _n, _head)                                 \
    for ((_pos) = (_head)->next, (_n) = (_pos)->next; (_pos) != (_head);       \
         (_pos) = (_n), (_n) = (_pos)->next)

/** @} */

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __MR_LIST_H__ */
