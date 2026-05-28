# 双向链表

`mr_list` 模块提供内核通用的侵入式双向循环链表。链表节点由使用者嵌入到业务结构体中，链表模块只维护节点之间的 `next` 和 `prev` 指针，不负责业务对象内存的申请、释放或并发保护。

该模块适合实现队列、对象集合、哈希桶链等基础容器。空链表的头节点指向自身，普通节点从链表删除后也会被重新初始化为自环状态。

## 核心数据结构和宏

### 链表节点

```c
typedef struct mr_list {
    struct mr_list *next;
    struct mr_list *prev;
} mr_list_t;
```

`next` 指向后继节点，`prev` 指向前驱节点。链表头也是一个 `mr_list_t`，空链表满足 `head.next == &head` 且 `head.prev == &head`。

### 静态初始化

```c
#define MR_LIST_INIT(_list) \
    { \
        .next = (_list), \
        .prev = (_list), \
    }
```

`MR_LIST_INIT()` 用于静态初始化单个链表头或节点。测试中使用如下形式初始化静态链表头：

```c
static mr_list_t head = MR_LIST_INIT(&head);
```

### 数组初始化

```c
#define MR_LISTS_INIT(_list, _list_num) \
    {MR_LISTIFY(_list_num, (, ), __MR_LIST_ARRAY_INIT_IMPL, _list)}

#define MR_LIST_ARRAY_INIT(_list) \
    MR_LISTS_INIT(_list, MR_ARRAY_SIZE(_list))
```

`MR_LISTS_INIT()` 和 `MR_LIST_ARRAY_INIT()` 用于静态初始化链表数组。它们依赖 `MR_LISTIFY()` 展开初始化表达式，因此数组数量受 `MR_LISTIFY` 可展开数量限制。

### 容器换算

```c
#define MR_LIST_ENTRY(_ptr, _type, _member) \
    MR_CONTAINER_OF(_ptr, _type, _member)
```

`MR_LIST_ENTRY()` 根据嵌入的链表节点指针取得外层结构体指针。

### 遍历宏

```c
#define MR_LIST_FOR_EACH(_pos, _head) \
    for ((_pos) = (_head)->next; (_pos) != (_head); (_pos) = (_pos)->next)

#define MR_LIST_FOR_EACH_SAFE(_pos, _n, _head) \
    for ((_pos) = (_head)->next, (_n) = (_pos)->next; (_pos) != (_head); \
         (_pos) = (_n), (_n) = (_pos)->next)
```

`MR_LIST_FOR_EACH()` 用于普通遍历。`MR_LIST_FOR_EACH_SAFE()` 会提前保存下一节点，适合在循环体中删除当前节点。

## 模块接口

### 初始化

```c
MR_INLINE void mr_list_init(mr_list_t *entry);
```

`mr_list_init()` 将 `entry->next` 和 `entry->prev` 都设置为 `entry` 自身。链表头和未加入链表的节点都可以使用该接口初始化。

### 尾部添加

```c
MR_INLINE void mr_list_add(mr_list_t *head, mr_list_t *entry);
```

`mr_list_add()` 将 `entry` 插入到链表尾部，即 `head->prev` 和 `head` 之间。测试中连续添加 `1、2、3` 后，正向遍历顺序保持为 `1、2、3`。

### 头部插入

```c
MR_INLINE void mr_list_insert(mr_list_t *head, mr_list_t *entry);
```

`mr_list_insert()` 将 `entry` 插入到链表头部，即 `head` 和 `head->next` 之间。测试中先插入 `1` 再插入 `2` 后，正向遍历顺序为 `2、1`。

### 删除节点

```c
MR_INLINE void mr_list_del(mr_list_t *entry);
```

`mr_list_del()` 将节点从所在链表摘除，并把该节点重新设置为自环状态。该接口不释放外层业务对象内存。

### 替换节点

```c
MR_INLINE void mr_list_replace(mr_list_t *new, mr_list_t *old);
```

`mr_list_replace()` 用 `new` 替换链表中的 `old`，并将 `old` 重新设置为自环状态。替换后 `new` 继承 `old` 原来的前驱和后继位置。

### 移动节点

```c
MR_INLINE void mr_list_move(mr_list_t *entry, mr_list_t *head);
```

`mr_list_move()` 先从原链表删除 `entry`，再把它添加到 `head` 指向链表的尾部。该接口可用于在两个链表之间移动节点。

### 判空

```c
MR_INLINE mr_bool_t mr_list_is_empty(mr_list_t *head);
```

`mr_list_is_empty()` 在 `head->next == head` 时返回 `MR_TRUE`，否则返回 `MR_FALSE`。

## 使用示例

下面示例来自测试用法，演示业务节点嵌入链表节点、尾部添加和遍历取回外层结构体：

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

删除遍历中的当前节点时，应使用安全遍历宏：

```c
MR_LIST_FOR_EACH_SAFE(pos, next, &head) {
    sample_node_t *node = MR_LIST_ENTRY(pos, sample_node_t, link);
    if ((node->id % 2) == 0) {
        mr_list_del(pos);
    }
}
```

## 注意事项

- 节点加入链表前应处于初始化状态，避免破坏现有链表指针。
- 同一个 `mr_list_t` 节点同一时刻只能加入一个链表。
- `mr_list_del()`、`mr_list_replace()` 和 `mr_list_move()` 要求节点指针关系有效，模块内部不做空指针检查。
- 普通遍历过程中删除当前节点会破坏后续遍历，应使用 `MR_LIST_FOR_EACH_SAFE()`。
- 链表模块不提供锁；多执行上下文同时修改同一链表时，调用者需要自行加锁。
