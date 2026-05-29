[**中文**](kernel_hashmap.md) | [**English**](kernel_hashmap_en.md)

---

# Hash Table

The `mr_hashmap` module provides an intrusive hash table based on a fixed bucket array and doubly linked lists. The hash
table itself does not allocate memory for the bucket array or nodes; the caller must provide the bucket array and embed
`mr_hashnode_t` into the business object.

This module uses a 32-bit unsigned integer as the key and `void *` to store the value. Keys are mapped to buckets via a
multiplicative hash function, and collisions within a bucket are handled using a `mr_list` linked list.

## Core Data Structures and Macros

### Hash Table Control Block

```c
typedef struct mr_hashmap {
    mr_list_t *buckets;
    mr_uint32_t bucket_num;
} mr_hashmap_t;
```

`buckets` points to the bucket array provided by the caller; each bucket is a `mr_list_t` list head. `bucket_num`
specifies the number of buckets. Lookup and insertion use `hash % bucket_num` to locate the bucket.

### Hash Node

```c
typedef struct mr_hashnode {
    mr_list_t list;
    mr_uint32_t key;
    void *value;
} mr_hashnode_t;
```

`list` is an embedded linked list node, `key` is a 32-bit key, and `value` is a pointer to the value saved by the
caller.

### Node Static Initialization

```c
#define MR_HASHNODE_INIT(_node, _key, _value) \
    { \
        .list = MR_LIST_INIT(&(_node)->list), \
        .key = (_key), \
        .value = (_value), \
    }
```

### Node Status Check

```c
#define MR_HASHNODE_IS_ADDED(_node)     (!mr_list_is_empty(&(_node)->list))
```

### Iteration Macros

```c
#define MR_HASHMAP_FOR_EACH(_node, _bi, _map) ...
#define MR_HASHMAP_FOR_EACH_SAFE(_node, _next, _bi, _map) ...
```

## Module Interface

### Initialize Hash Table

```c
MR_INLINE void mr_hashmap_init(mr_hashmap_t *map, mr_list_t *buckets,
                               mr_uint32_t bucket_num);
```

`mr_hashmap_init()` initializes `bucket_num` bucket lists and stores the bucket array pointer and bucket count.

### Add Node

```c
mr_err_t mr_hashmap_add(mr_hashmap_t *map, mr_hashnode_t *node);
```

Return values:

| Return Value | Meaning                                                           |
|--------------|-------------------------------------------------------------------|
| `0`          | Addition succeeded.                                               |
| `-MR_EINVAL` | `map->bucket_num` is `0`.                                         |
| `-MR_EBUSY`  | `node` is already added to a hash table or linked list.           |
| `-MR_EEXIST` | A node with the same key already exists in the target hash table. |

### Delete Node

```c
mr_err_t mr_hashmap_del(mr_hashnode_t *node);
```

| Return Value | Meaning                          |
|--------------|----------------------------------|
| `0`          | Deletion succeeded.              |
| `-MR_ENOENT` | The node is not in a hash table. |

### Find Node

```c
mr_hashnode_t *mr_hashmap_find(mr_hashmap_t *map, mr_uint32_t key);
```

### Check Empty

```c
MR_INLINE mr_bool_t mr_hashmap_is_empty(mr_hashmap_t *map);
```

### Node Access

```c
MR_INLINE void mr_hashnode_init(mr_hashnode_t *node, mr_uint32_t key, void *value);
MR_INLINE mr_uint32_t mr_hashnode_key(mr_hashnode_t *node);
MR_INLINE void mr_hashnode_value_set(mr_hashnode_t *node, void *value);
MR_INLINE void *mr_hashnode_value(mr_hashnode_t *node);
```

## Usage Example

```c
#include <mrX/kernel/mr_hashmap.h>

#define SAMPLE_BUCKET_NUM 4

typedef struct sample_item {
    mr_hashnode_t node;
    int value;
} sample_item_t;

void sample_hashmap_usage(void)
{
    mr_list_t buckets[SAMPLE_BUCKET_NUM];
    mr_hashmap_t map;
    sample_item_t item;
    mr_hashnode_t *node;

    mr_hashmap_init(&map, buckets, SAMPLE_BUCKET_NUM);
    item.value = 70;
    mr_hashnode_init(&item.node, 7, &item.value);

    if (mr_hashmap_add(&map, &item.node) != 0) {
        return;
    }

    node = mr_hashmap_find(&map, 7);
    if (node != MR_NULL) {
        int *value = (int *)mr_hashnode_value(node);
        (void)value;
    }

    mr_hashmap_del(&item.node);
}
```

## Notes

- The hash table does not own the bucket array or node memory; the caller is responsible for their lifecycle management.
- A hash table with `bucket_num` of `0` cannot add nodes, and lookups will always fail.
- The same node cannot be added to two hash tables simultaneously; re-adding returns `-MR_EBUSY`.
- Duplicate keys are not allowed in the same hash table; adding a duplicate key returns `-MR_EEXIST`.
- Do not directly modify the `key` of a node that has already been added to a hash table, otherwise the node's bucket
  and lookup rules will become inconsistent.
- The module does not provide locking; callers must synchronize concurrent add, delete, and find operations themselves.
