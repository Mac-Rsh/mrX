# 哈希表（Hashmap）

基于 32 位整数键的侵入式链表哈希表，采用 Knuth
乘法散列与桶链解决冲突，桶数组与节点内存均由调用者提供，零动态内存开销。

| 项目   | 说明                                                       |
|--------|------------------------------------------------------------|
| 功能宏 | 无（始终编译）                                             |
| 头文件 | `<mrX/kernel/mr_hashmap.h>`                                |
| 依赖   | [内核基础工具（Foundation）](foundation.md)（`mr_list.h`） |
| 测试   | `test/kernel/hashmap.c`、`test/kernel/hashmap_deep.c`      |

## 快速上手

```c
#include <mrX/kernel/mr_hashmap.h>

#define BUCKET_NUM 16

typedef struct {
    mr_hashnode_t node;
    int payload;
} item_t;

void hashmap_demo(void) {
    mr_hashmap_t map;
    mr_list_t buckets[BUCKET_NUM];
    item_t item = {.payload = 42};
    mr_hashnode_t *found;

    mr_hashmap_init(&map, buckets, BUCKET_NUM);
    mr_hashnode_init(&item.node, 100, &item.payload);
    mr_hashmap_add(&map, &item.node);

    found = mr_hashmap_find(&map, 100);
    if (found != MR_NULL) {
        item_t *owner = MR_CONTAINER_OF(found, item_t, node);
        (void)owner;
    }

    mr_hashmap_del(&item.node);
}
```

## 核心机制与约束

- **内存与生命周期**：哈希表不拥有桶数组和节点内存，调用者须确保 `buckets` 生命周期覆盖
  `map`。`bucket_num == 0` 时允许 `buckets == MR_NULL`，此时 `find()` 恒返回
  `MR_NULL`，`add()` 返回 `-MR_EINVAL`。
- **节点状态与键不可变**：每个 `mr_hashnode_t` 仅含一个 `mr_list_t`
  节点，同一时刻只能链入一张表；重复添加到任意表会返回 `-MR_EBUSY`，键冲突会返回
  `-MR_EEXIST`。节点在表期间禁止修改 `key`（会破坏散列定位），须先
  `mr_hashmap_del()` 再修改；`value` 可随时通过 `mr_hashnode_value_set()` 更新。
- **遍历中安全删除**：使用 `MR_HASHMAP_FOR_EACH()` 遍历时禁止删除当前节点；需在循环体中删除时必须改用
  `MR_HASHMAP_FOR_EACH_SAFE()`。
- **并发与前置条件**：哈希表完全无锁，多线程或中断与线程共享时须由调用者在外部加锁保护。公共接口假定
  `map` 与 `node` 指针有效，不做空指针校验。

## API 速查

### 容器操作

| API                                                                                   | 说明                                                                                               |
|---------------------------------------------------------------------------------------|----------------------------------------------------------------------------------------------------|
| `void mr_hashmap_init(mr_hashmap_t *map, mr_list_t *buckets, mr_uint32_t bucket_num)` | 绑定并初始化桶数组                                                                                 |
| `mr_err_t mr_hashmap_add(mr_hashmap_t *map, mr_hashnode_t *node)`                     | 添加节点；成功返回 `0`，未初始化返回 `-MR_EINVAL`，已链入返回 `-MR_EBUSY`，键重复返回 `-MR_EEXIST` |
| `mr_err_t mr_hashmap_del(mr_hashnode_t *node)`                                        | 从所属表中摘除节点；未链入返回 `-MR_ENOENT`                                                        |
| `mr_hashnode_t *mr_hashmap_find(mr_hashmap_t *map, mr_uint32_t key)`                  | 按键查找节点，未命中或空表返回 `MR_NULL`                                                           |
| `mr_bool_t mr_hashmap_is_empty(mr_hashmap_t *map)`                                    | 查询所有桶是否均为空                                                                               |

### 节点与遍历

| API / 宏                                                         | 说明                                 |
|------------------------------------------------------------------|--------------------------------------|
| `MR_HASHNODE_INIT(_node, _key, _value)` / `mr_hashnode_init`     | 静态 / 运行期初始化节点              |
| `MR_HASHNODE_IS_ADDED(_node)`                                    | 判断节点当前是否已链入某张表         |
| `mr_hashnode_key(node)`                                          | 读取节点键                           |
| `mr_hashnode_value_set(node, value)` / `mr_hashnode_value(node)` | 设置 / 读取节点值                    |
| `MR_HASHMAP_FOR_EACH(_node, _bi, _map)`                          | 按桶序遍历全部节点                   |
| `MR_HASHMAP_FOR_EACH_SAFE(_node, _next, _bi, _map)`              | 允许在循环体中删除当前节点的安全遍历 |

## 内部实现

核心实现位于 `kernel/hashmap.c`：

- **散列与定位**：`hashmap_hash()` 使用 Knuth 乘法散列 `key * 2654435761U`，再对
  `map->bucket_num` 取模定位到对应桶链表。
- **冲突处理**：桶内采用 `mr_list_add()` 尾插，查找与重复键检查在目标桶内线性扫描。
- **无依赖删除**：`mr_hashmap_del()` 直接判断 `MR_HASHNODE_IS_ADDED(node)` 并执行
  `mr_list_del(&node->list)`，无需传入 `map` 指针。
