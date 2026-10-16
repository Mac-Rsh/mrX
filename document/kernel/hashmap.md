# 哈希表（Hashmap）

> 哈希表提供键值（key-value）容器能力：以 32 位整数为键，通过乘法散列 + 桶链
> 实现 O (1) 均摊的查找/插入/删除，节点内嵌于用户结构，无动态内存开销。

| 项       | 说明                                                                      |
|----------|---------------------------------------------------------------------------|
| 功能宏   | 无（始终编译）                                                            |
| 头文件   | `<mrX/kernel/mr_hashmap.h>`                                               |
| 依赖模块 | 无（仅依赖 `mr_list.h` 链表容器与 `mr_errno.h` 错误码，均为头文件级依赖） |
| 参考测试 | `mrX/test/kernel/hashmap.c`                                               |

---

## 用户指南

### 1. 模块概述

哈希表解决的问题是： **在大量键值对中按整数键快速定位数据**，比线性链表查找（O (n)）快得多。特性：

- 节点（`mr_hashnode_t`）内嵌于用户结构体，容器本身零分配——桶数组与节点存储 全部由调用者提供。
- 键为 `mr_uint32_t`，值为 `void *`，经 Knuth 乘法散列分布到桶。
- 同键重复添加、未添加即删除等错误场景有明确错误码。
- 提供遍历宏及"遍历中安全删除当前节点"的安全版本。

适用场景：内部组件的句柄/ID 映射（如会话表、资源表）。不适用：需要字符串键 （需自行哈希成 32 位）、需要自动扩容（桶数固定，创建时确定）。

### 2. 配置与依赖

无功能宏：`mr_hashmap.h` 不引用 `mr_config.h`，任何配置下都参与编译。仅依赖 libc 的 `mr_errno.h`（错误码）与内核的
`mr_list.h`（链表容器），无初始化接线， 直接包含头文件即可使用：

```c
#include <mrX/kernel/mr_hashmap.h>
```

桶数没有内置上限，由调用者按预期容量与内存预算权衡（见第 7 节 FAQ）。

### 3. 快速上手

```c
#include <mrX/kernel/mr_hashmap.h>

#define BUCKET_NUM  16

static void hashmap_example(void) {
    mr_hashmap_t map;
    mr_list_t buckets[BUCKET_NUM];
    mr_hashnode_t node;
    int value = 42;

    /* 在调用者提供的桶数组上创建哈希表 */
    mr_hashmap_init(&map, buckets, BUCKET_NUM);

    /* 添加键 7 -> &value */
    mr_hashnode_init(&node, 7, &value);
    mr_hashmap_add(&map, &node);

    /* 按键查找 */
    if (mr_hashmap_find(&map, 7) == &node) {
        /* 命中：mr_hashnode_value(&node) == &value */
    }

    /* 删除 */
    mr_hashmap_del(&node);
}
```

要点：

- 桶数组 `buckets` 必须与 `map` 同生命周期（通常同为静态或同属一个外层结构）。
- 节点用 `mr_hashnode_init`（或 `MR_HASHNODE_INIT` 宏）初始化后才能 `add`。
- `find` 返回 `mr_hashnode_t *`，用 `MR_CONTAINER_OF` 取回外层结构或直接读
  `value` 字段接口。

### 4. 使用说明

**节点生命周期**：节点归调用者所有，哈希表只维护其链入状态。

| 操作                           | 行为                                      |
|--------------------------------|-------------------------------------------|
| 未初始化的节点 `add`           | 未定义（必须先 `mr_hashnode_init`）       |
| 已在表中的节点再次 `add`       | 拒绝，返回 `-MR_EBUSY`                    |
| 添加与表中已有节点相同键的节点 | 拒绝，返回 `-MR_EEXIST`                   |
| 删除不在表中的节点             | 拒绝，返回 `-MR_ENOENT`                   |
| 修改已在表中的节点的 `key`     | 禁止（会破坏散列定位），只能先 `del` 再改 |

**键值访问**：键经 `mr_hashnode_key` 读取（只读约定）；值经
`mr_hashnode_value_set` / `mr_hashnode_value` 读写，可在节点在表期间修改。

**遍历**：`MR_HASHMAP_FOR_EACH(node, bi, map)` 逐桶逐节点遍历；需在遍历中删除 当前节点时必须使用 `_SAFE`
版本（预先保存后继，删除后循环仍可继续）。遍历顺序 为桶序 + 桶内链序，无业务含义，不应依赖。

### 5. API 参考

容器操作：

| API                   | 说明                               |
|-----------------------|------------------------------------|
| `mr_hashmap_init`     | 初始化哈希表（绑定桶数组与桶数）   |
| `mr_hashmap_add`      | 添加节点（键冲突或重复链入会拒绝） |
| `mr_hashmap_del`      | 删除节点                           |
| `mr_hashmap_find`     | 按键查找节点                       |
| `mr_hashmap_is_empty` | 查询哈希表是否为空                 |

节点操作：

| API                                           | 说明                          |
|-----------------------------------------------|-------------------------------|
| `MR_HASHNODE_INIT` / `mr_hashnode_init`       | 初始化节点（宏/函数两种形式） |
| `MR_HASHNODE_IS_ADDED`                        | 查询节点是否已链入哈希表      |
| `mr_hashnode_key`                             | 读取节点键                    |
| `mr_hashnode_value_set` / `mr_hashnode_value` | 设置 / 读取节点值             |

遍历：

| API                        | 说明                   |
|----------------------------|------------------------|
| `MR_HASHMAP_FOR_EACH`      | 全表遍历               |
| `MR_HASHMAP_FOR_EACH_SAFE` | 遍历中安全删除当前节点 |

**`mr_hashmap_init`**

```c
void mr_hashmap_init(mr_hashmap_t *map, mr_list_t *buckets,
                     mr_uint32_t bucket_num);
```

`buckets` 为调用者提供的 `mr_list_t` 数组，`bucket_num` 为其长度；函数会逐一初始化每个桶。`map` 必须有效，且
`bucket_num > 0` 时 `buckets` 必须指向至少这么多有效元素，函数本身不做空指针检查。`bucket_num == 0` 时允许
`buckets == MR_NULL`，此后 `find` 返回空、`add` 返回 `-MR_EINVAL`、`is_empty` 返回真。

**`mr_hashmap_add`**

```c
mr_err_t mr_hashmap_add(mr_hashmap_t *map, mr_hashnode_t *node);
```

成功 `0`；`bucket_num` 为 0 返回 `-MR_EINVAL`；节点已链入返回 `-MR_EBUSY`；同键节点已存在返回 `-MR_EEXIST`。`map` 与 `node`
指针本身必须有效，公共实现不做空指针保护。

**`mr_hashmap_del`**

```c
mr_err_t mr_hashmap_del(mr_hashnode_t *node);
```

成功 `0`；节点不在任何表中返回 `-MR_ENOENT`。

**`mr_hashmap_find`**

```c
mr_hashnode_t *mr_hashmap_find(mr_hashmap_t *map, mr_uint32_t key);
```

命中返回节点指针，未命中或 `bucket_num == 0` 返回 `MR_NULL`。`map` 指针必须有效；当 `bucket_num > 0` 时，`buckets`
也必须是有效桶数组。

### 6. 完整示例

示例 1：节点内嵌于外层结构（典型资源表模式，取材自
`mrX/test/kernel/hashmap.c:39` 的 `add_lookup_del` 用例）：

```c
struct my_item {
    mr_hashnode_t node;     /* 内嵌节点 */
    void *payload;
};

static void item_table_example(void) {
    static struct my_item item;
    mr_hashmap_t map;
    mr_list_t buckets[8];
    mr_hashnode_t *found;

    mr_hashmap_init(&map, buckets, 8);
    mr_hashnode_init(&item.node, 100, &item.payload);
    mr_hashmap_add(&map, &item.node);

    found = mr_hashmap_find(&map, 100);
    if (found != MR_NULL) {
        struct my_item *owner = MR_CONTAINER_OF(found, struct my_item, node);
        /* owner == &item */
    }
    mr_hashmap_del(&item.node);
}
```

示例 2：遍历并安全删除全部匹配节点：

```c
static void drop_even_keys(mr_hashmap_t *map) {
    mr_hashnode_t *node, *next;
    mr_uint32_t bi;

    MR_HASHMAP_FOR_EACH_SAFE(node, next, bi, map) {
        if ((mr_hashnode_key(node) & 1U) == 0U) {
            mr_hashmap_del(node);
        }
    }
}
```

### 7. 常见问题（FAQ）

**Q：桶数选多大合适？**

桶数为 0 会使 `find` 恒空、`add` 报 `-MR_EINVAL`；一般取预期最大元素数的 1~2 倍并可用 2 的幂（散列为乘法取模，桶数接近元素数即可保持短链）。内存受限
时宁可小桶 + 长链，也不会额外分配。

**Q：为什么 `add` 返回 `-MR_EBUSY`？**

节点只有 `list` 一个链入位，已在表中的节点无法再次链入。复用节点前先
`mr_hashmap_del`，`MR_HASHNODE_IS_ADDED` 可提前判断。

**Q：能存字符串键吗？**

键类型固定为 `mr_uint32_t`。字符串键需自行用稳定哈希（如 FNV-1a）压成 32 位， 并自行处理哈希碰撞导致的不同字符串同键问题（值为
`void *`，可再比较原串）。

**Q：遍历中能用非 SAFE 宏删除节点吗？**

不能。非 SAFE 宏会在删除后继续访问已脱链节点的 `next`，行为未定义；删除场景 必须用 `MR_HASHMAP_FOR_EACH_SAFE`。

**Q：线程安全吗？**

不安全。所有接口无锁（见第 11 节），多上下文共享时需调用者自行加锁。

---

## 开发者指南

### 8. 架构总览

哈希表是内核纯数据结构组件，位于 libc 容器层（`mr_list`）之上、各业务模块 之下，无初始化接线、无 `MR_USE_*`
门控、无内核内部固定使用方（当前主要供 上层组件按需组合）：

```
   业务代码
      |  key -> bucket
   +--v---------------------------+
   | Hashmap (mr_hashmap_t)       |
   |  buckets[] -> 桶内冲突链      |---- 依赖 mr_list（链表容器）
   +------------------------------+
```

定位上它与 `mr_list` 同级：`mr_list` 解决"有序集合"，`mr_hashmap` 解决
"按键检索"，两者均以内嵌节点方式工作，共同构成内核的无分配容器族。

### 9. 数据结构

```c
typedef struct mr_hashmap {         /* mrX/include/mrX/kernel/mr_hashmap.h:25 */
    mr_list_t  *buckets;            /* 调用者提供的桶数组 */
    mr_uint32_t bucket_num;         /* 桶数 */
} mr_hashmap_t;

typedef struct mr_hashnode {        /* mr_hashmap.h:31 */
    mr_list_t   list;               /* 桶内冲突链节点 */
    mr_uint32_t key;                /* 查找键（链入期间只读） */
    void       *value;              /* 用户 payload */
} mr_hashnode_t;
```

设计要点：

- 表结构只存桶数组指针与桶数， **不拥有任何内存**——桶数组寿命必须覆盖表寿命。
- 结构体 API 普遍假定 `map`/`node` 指针有效；`mr_hashmap_init` 也不会替调用者验证非零 bucket 数对应的 `buckets` 指针。
- 节点以 `list` 字段链入桶的冲突链，因此一个节点同一时刻只能属于一张表；
  `MR_HASHNODE_IS_ADDED` 即"list 不为自指空链"。
- 链入状态与键共同维持不变量：节点的散列桶由 `key` 决定，故链入期间键不可变。

### 10. 核心流程

**散列与定位**：`hashmap_hash`（`mrX/kernel/hashmap.c:11`）使用 Knuth 乘法散列
`key * 2654435761U`（黄金分割常数），再对桶数取模定位桶 （`hashmap.c:15`）。乘法散列让相邻键（如递增 ID）均匀分散，弥补取模对连续键
不敏感的问题。

**查找**：`mr_hashmap_find`（`mrX/kernel/hashmap.c:34`）先判空表 （`bucket_num == 0` 直接返回 `MR_NULL`
），然后只在目标桶内做链表线性扫描 （`hashmap.c:20`）——冲突链长度决定单次查找代价。

**添加**：`mr_hashmap_add`（`mrX/kernel/hashmap.c:44`）依次校验：空表
`-MR_EINVAL`、节点已链入 `-MR_EBUSY`（`hashmap.c:53`）、桶内同键
`-MR_EEXIST`（`hashmap.c:59`），全部通过后 `mr_list_add` 头插进桶。

**删除**：`mr_hashmap_del`（`mrX/kernel/hashmap.c:68`）以"节点是否链入"为判据 （不依赖 map 指针），未链入返回 `-MR_ENOENT`；删除即
`mr_list_del`，节点随即 可复用。

**遍历**：`MR_HASHMAP_FOR_EACH_SAFE`（`mr_hashmap.h:198`）在进入下一节点前预取
`next`，当前节点被删除后循环从预取点继续——这是"遍历中删除"安全性的全部来源。

### 11. 并发与中断安全

哈希表自身 **完全无锁**，也没有内核级使用方为其建立锁惯例：

- 单一所有者上下文（单线程或始终同一中断级）内使用无需额外保护。
- 跨上下文共享时，调用者必须用自旋锁等原语将 `add`/`del`/`find`/遍历整体置于 临界区；接口内部均为纯链表操作，临界区时长可控。
- `mr_hashnode_value_set` / `mr_hashnode_value` 只触碰节点自身的普通 `void *value`
  字段，确实不会改变散列桶不变量，但它们也没有原子或锁保护；如果多个执行流同时读写 value，仍需由调用者同步以避免数据竞争。

### 12. 扩展与派生

哈希表无对象 clazz 与派生机制，扩展方式是 **组合**而非继承：

- 节点内嵌：任意结构体嵌入 `mr_hashnode_t` 后经 `MR_CONTAINER_OF` 取回外层 （见用户篇示例 1），一个结构体也可嵌入多个节点加入多张表。
- 键策略定制：字符串/多字段键在调用方先行压缩为 32 位键；桶数与再平衡策略 （若需动态扩容）由外层封装实现。

### 13. 源码导读

| 位置                                      | 职责                                               |
|-------------------------------------------|----------------------------------------------------|
| `mrX/include/mrX/kernel/mr_hashmap.h:25`  | 表与节点结构、初始化与节点访问内联函数             |
| `mrX/include/mrX/kernel/mr_hashmap.h:181` | 两个遍历宏（普通/安全删除）                        |
| `mrX/kernel/hashmap.c:11`                 | Knuth 乘法散列与桶定位                             |
| `mrX/kernel/hashmap.c:44`                 | 添加流程与三类错误码                               |
| `mrX/kernel/hashmap.c:68`                 | 删除流程（以链入状态为判据）                       |
| `mrX/test/kernel/hashmap.c`               | init / add_lookup_del / duplicate_key 等行为级测试 |

### 14. 附录

**配置宏**

| 宏                                          | 说明                                   |
|---------------------------------------------|----------------------------------------|
| 无                                          | 模块不受任何 `MR_USE_*` 门控，始终编译 |
| `MR_HASHNODE_INIT` / `MR_HASHNODE_IS_ADDED` | 节点初始化与链入状态查询宏             |
| `MR_HASHMAP_FOR_EACH` / `_SAFE`             | 遍历宏                                 |

**错误码**（见 `mrX/include/mrX/libc/mr_errno.h`）

| 错误码       | 触发条件                                                                   |
|--------------|----------------------------------------------------------------------------|
| `-MR_EINVAL` | `add` 时表未初始化（`bucket_num == 0`）；`find` 时表未初始化返回 `MR_NULL` |
| `-MR_EBUSY`  | 节点已链入其他位置再次 `add`                                               |
| `-MR_EEXIST` | 同键节点已存在于表中                                                       |
| `-MR_ENOENT` | `del` 的节点不在任何表中                                                   |
