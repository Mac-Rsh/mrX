[**中文**](kernel_hashmap.md) | [**English**](kernel_hashmap_en.md)

---

# 哈希表

`mr_hashmap` 模块提供基于固定桶数组和双向链表的侵入式哈希表。哈希表本身不分配桶数组和节点内存，调用者需要提供桶数组，并将
`mr_hashnode_t` 嵌入到业务对象中。

该模块使用 32 位无符号整数作为键，使用 `void *` 保存值。键通过乘法哈希函数映射到桶，桶内使用 `mr_list` 链表处理冲突。

## 核心数据结构和宏

### 哈希表控制块

```c
typedef struct mr_hashmap {
    mr_list_t *buckets;
    mr_uint32_t bucket_num;
} mr_hashmap_t;
```

`buckets` 指向调用者提供的桶数组，每个桶是一个 `mr_list_t` 链表头。`bucket_num` 表示桶数量，查找和添加时使用
`hash % bucket_num` 定位桶。

### 哈希节点

```c
typedef struct mr_hashnode {
    mr_list_t list;
    mr_uint32_t key;
    void *value;
} mr_hashnode_t;
```

`list` 是嵌入式链表节点，`key` 是 32 位键，`value` 是调用者保存的值指针。

### 节点静态初始化

```c
#define MR_HASHNODE_INIT(_node, _key, _value) \
    { \
        .list = MR_LIST_INIT(&(_node)->list), \
        .key = (_key), \
        .value = (_value), \
    }
```

该宏用于静态初始化哈希节点，使节点初始处于未加入哈希表的自环状态。

### 节点状态判断

```c
#define MR_HASHNODE_IS_ADDED(_node)     (!mr_list_is_empty(&(_node)->list))
```

节点加入任意哈希表后，`list` 不再为空，因此该宏返回 `MR_TRUE`。节点删除后，`mr_list_del()` 会把节点恢复为自环状态，该宏返回
`MR_FALSE`。

### 遍历宏

```c
#define MR_HASHMAP_FOR_EACH(_node, _bi, _map) ...
#define MR_HASHMAP_FOR_EACH_SAFE(_node, _next, _bi, _map) ...
```

两个宏按桶索引从 `0` 到 `bucket_num - 1` 遍历所有节点。`MR_HASHMAP_FOR_EACH_SAFE()` 预取下一节点，适合遍历过程中删除当前节点。

## 模块接口

### 初始化哈希表

```c
MR_INLINE void mr_hashmap_init(mr_hashmap_t *map, mr_list_t *buckets,
                               mr_uint32_t bucket_num);
```

`mr_hashmap_init()` 会初始化 `bucket_num` 个桶链表，并保存桶数组指针和桶数量。桶数组生命周期必须覆盖哈希表使用周期。

### 添加节点

```c
mr_err_t mr_hashmap_add(mr_hashmap_t *map, mr_hashnode_t *node);
```

`mr_hashmap_add()` 根据 `node->key` 计算桶位置，并将节点加入对应桶尾部。

返回值如下：

| 返回值          | 含义                       |
|--------------|--------------------------|
| `0`          | 添加成功。                    |
| `-MR_EINVAL` | `map->bucket_num` 为 `0`。 |
| `-MR_EBUSY`  | `node` 已经加入某个哈希表或链表。     |
| `-MR_EEXIST` | 目标哈希表中已经存在相同键。           |

### 删除节点

```c
mr_err_t mr_hashmap_del(mr_hashnode_t *node);
```

`mr_hashmap_del()` 将节点从当前桶链表中删除。

返回值如下：

| 返回值          | 含义        |
|--------------|-----------|
| `0`          | 删除成功。     |
| `-MR_ENOENT` | 节点未加入哈希表。 |

### 查找节点

```c
mr_hashnode_t *mr_hashmap_find(mr_hashmap_t *map, mr_uint32_t key);
```

`mr_hashmap_find()` 使用与添加相同的哈希规则定位桶，并在桶链表中按键比较。找到时返回节点指针，未找到或 `bucket_num` 为 `0`
时返回 `MR_NULL`。

### 判空

```c
MR_INLINE mr_bool_t mr_hashmap_is_empty(mr_hashmap_t *map);
```

该接口检查所有桶链表。全部为空时返回 `MR_TRUE`，任一桶非空时返回 `MR_FALSE`。

### 节点访问

```c
MR_INLINE void mr_hashnode_init(mr_hashnode_t *node, mr_uint32_t key,
                                void *value);
MR_INLINE mr_uint32_t mr_hashnode_key(mr_hashnode_t *node);
MR_INLINE void mr_hashnode_value_set(mr_hashnode_t *node, void *value);
MR_INLINE void *mr_hashnode_value(mr_hashnode_t *node);
```

`mr_hashnode_init()` 初始化节点、键和值。`mr_hashnode_key()` 返回键；已经加入哈希表的节点不应直接修改键。
`mr_hashnode_value_set()` 和 `mr_hashnode_value()` 用于设置和读取值指针。

## 使用示例

下面示例演示初始化、添加、查找和删除节点：

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

## 注意事项

- 哈希表不拥有桶数组和节点内存，调用者负责它们的生命周期管理。
- `bucket_num` 为 `0` 的哈希表不能添加节点，查找总是失败。
- 同一节点不能同时加入两个哈希表；再次添加会返回 `-MR_EBUSY`。
- 同一哈希表不允许重复键；重复键添加返回 `-MR_EEXIST`。
- 已加入哈希表的节点不要直接修改 `key`，否则节点所在桶和查找规则会不一致。
- 模块未提供锁；并发添加、删除、查找时调用者需要自行同步。
