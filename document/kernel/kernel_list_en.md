[**中文**](kernel_list.md) | [**English**](kernel_list_en.md)

---

# Doubly Linked List

The `mr_list` module provides a generic kernel-intrusive doubly circular linked list. Linked list nodes are embedded by
users into business structures; the list module only maintains the `next` and `prev` pointers between nodes and does not
handle memory allocation, deallocation, or concurrency protection for business objects.

This module is suitable for implementing basic containers such as queues, object collections, and hash bucket chains. An
empty list's head node points to itself, and a normal node removed from a list is re-initialized to a self-loop state.

## Core Data Structures and Macros

### List Node

```c
typedef struct mr_list {
    struct mr_list *next;
    struct mr_list *prev;
} mr_list_t;
```

`next` points to the successor node, `prev` points to the predecessor node. The list head is also a `mr_list_t`; an
empty list satisfies `head.next == &head` and `head.prev == &head`.

### Static Initialization

```c
#define MR_LIST_INIT(_list) \
    { \
        .next = (_list), \
        .prev = (_list), \
    }
```

`MR_LIST_INIT()` is used to statically initialize a single list head or node. In tests, a static list head is
initialized as follows:

```c
static mr_list_t head = MR_LIST_INIT(&head);
```

### Array Initialization

```c
#define MR_LISTS_INIT(_list, _list_num) \
    {MR_LISTIFY(_list_num, (, ), __MR_LIST_ARRAY_INIT_IMPL, _list)}

#define MR_LIST_ARRAY_INIT(_list) \
    MR_LISTS_INIT(_list, MR_ARRAY_SIZE(_list))
```

`MR_LISTS_INIT()` and `MR_LIST_ARRAY_INIT()` are used to statically initialize arrays of linked lists. They rely on
`MR_LISTIFY()` to expand initialization expressions, so the array count is limited by the expansion capacity of
`MR_LISTIFY`.

### Container Entry

```c
#define MR_LIST_ENTRY(_ptr, _type, _member) \
    MR_CONTAINER_OF(_ptr, _type, _member)
```

`MR_LIST_ENTRY()` obtains the outer structure pointer from an embedded list node pointer.

### Traversal Macros

```c
#define MR_LIST_FOR_EACH(_pos, _head) \
    for ((_pos) = (_head)->next; (_pos) != (_head); (_pos) = (_pos)->next)

#define MR_LIST_FOR_EACH_SAFE(_pos, _n, _head) \
    for ((_pos) = (_head)->next, (_n) = (_pos)->next; (_pos) != (_head); \
         (_pos) = (_n), (_n) = (_pos)->next)
```

`MR_LIST_FOR_EACH()` is used for normal traversal. `MR_LIST_FOR_EACH_SAFE()` saves the next node in advance, suitable
for deleting the current node within the loop body.

## Module Interfaces

### Initialization

```c
MR_INLINE void mr_list_init(mr_list_t *entry);
```

`mr_list_init()` sets both `entry->next` and `entry->prev` to `entry` itself. Both list heads and nodes not yet added to
a list can use this interface for initialization.

### Tail Append

```c
MR_INLINE void mr_list_add(mr_list_t *head, mr_list_t *entry);
```

`mr_list_add()` inserts `entry` at the tail of the list, i.e., between `head->prev` and `head`. In tests, after
successively appending `1, 2, 3`, the forward traversal order remains `1, 2, 3`.

### Head Insert

```c
MR_INLINE void mr_list_insert(mr_list_t *head, mr_list_t *entry);
```

`mr_list_insert()` inserts `entry` at the head of the list, i.e., between `head` and `head->next`. In tests, after
inserting `1` followed by `2`, the forward traversal order is `2, 1`.

### Remove Node

```c
MR_INLINE void mr_list_del(mr_list_t *entry);
```

`mr_list_del()` removes the node from its list and resets the node to a self-loop state. This interface does not free
the memory of the outer business object.

### Replace Node

```c
MR_INLINE void mr_list_replace(mr_list_t *new, mr_list_t *old);
```

`mr_list_replace()` replaces `old` in the list with `new`, and resets `old` to a self-loop state. After replacement,
`new` inherits the original predecessor and successor positions of `old`.

### Move Node

```c
MR_INLINE void mr_list_move(mr_list_t *entry, mr_list_t *head);
```

`mr_list_move()` first removes `entry` from its original list, then appends it to the tail of the list pointed to by
`head`. This interface can be used to move nodes between two lists.

### Check Empty

```c
MR_INLINE mr_bool_t mr_list_is_empty(mr_list_t *head);
```

`mr_list_is_empty()` returns `MR_TRUE` when `head->next == head`, otherwise returns `MR_FALSE`.

## Usage Example

The following example is from test usage, demonstrating embedding a list node in a business node, tail appending, and
traversing to retrieve the outer structure:

```c
#include <mrX/kernel/mr_list.h>

typedef struct sample_node {
    mr_list_t link;
    int id;
} sample_node_t;

void sample_list_usage(void)
{
    sample_node_t n1, n2;
    mr_list_t head;
    mr_list_t *pos;

    mr_list_init(&head);
    n1.id = 1;
    n2.id = 2;

    mr_list_add(&head, &n1.link);
    mr_list_add(&head, &n2.link);

    MR_LIST_FOR_EACH(pos, &head) {
        sample_node_t *node = MR_LIST_ENTRY(pos, sample_node_t, link);
        (void)node->id;
    }
}
```

When deleting the current node during traversal, use the safe traversal macro:

```c
MR_LIST_FOR_EACH_SAFE(pos, next, &head) {
    sample_node_t *node = MR_LIST_ENTRY(pos, sample_node_t, link);
    if ((node->id % 2) == 0) {
        mr_list_del(pos);
    }
}
```

## Notes

- Nodes should be in an initialized state before being added to a list to avoid corrupting existing list pointers.
- The same `mr_list_t` node can only belong to one list at a time.
- `mr_list_del()`, `mr_list_replace()`, and `mr_list_move()` require valid pointer relationships; the module does not
  perform null pointer checks internally.
- Deleting the current node during normal traversal will corrupt subsequent traversal; use `MR_LIST_FOR_EACH_SAFE()`
  instead.
- The list module does not provide locking; when multiple execution contexts modify the same list concurrently, the
  caller must lock appropriately.
