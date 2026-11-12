# 内核基础工具（Foundation）

> 本文集中说明没有独立 `.c` 实现、但会被应用和其他模块直接使用的链表、引用计数、自旋锁、工具宏和版本宏。

| 项目     | 说明                                                                                                  |
|----------|-------------------------------------------------------------------------------------------------------|
| 功能宏   | 无（头文件级基础能力始终可用）                                                                        |
| 头文件   | `<mrX/kernel/mr_list.h>`、`mr_listify.h`、`mr_ref.h`、`mr_service.h`、`mr_spinlock.h`、`mr_version.h` |
| 依赖模块 | 原子操作、编译器封装、基础类型与平台 IRQ 接口                                                         |

---

## 用户指南

### 1. 侵入式双向链表

`mr_list_t` 既可作为链表头，也可内嵌到业务对象中作为节点。空链表头的 `next/prev`
都指向自身。

```c
typedef struct item {
    int value;
    mr_list_t node;
} item_t;

static mr_list_t items = MR_LIST_INIT(&items);

static void item_add(item_t *item) {
    mr_list_init(&item->node);
    mr_list_add(&items, &item->node);       /* 尾插。 */
}
```

| API / 宏                               | 说明                                   |
|----------------------------------------|----------------------------------------|
| `MR_LIST_INIT` / `mr_list_init`        | 静态 / 运行期初始化                    |
| `MR_LISTS_INIT` / `MR_LIST_ARRAY_INIT` | 初始化固定长度链表头数组               |
| `mr_list_add`                          | 尾插                                   |
| `mr_list_insert`                       | 头插                                   |
| `mr_list_del`                          | 摘链并把节点重置为自指空链             |
| `mr_list_replace`                      | 用新节点替换旧节点，旧节点变为自指     |
| `mr_list_move`                         | 摘下节点并移到另一链表尾部             |
| `mr_list_is_empty`                     | 判断链表头是否为空                     |
| `MR_LIST_ENTRY`                        | 由节点指针取回外层对象                 |
| `MR_LIST_FOR_EACH`                     | 普通遍历                               |
| `MR_LIST_FOR_EACH_SAFE`                | 预存下一节点，允许在遍历中删除当前节点 |

链表函数不加锁。同一链表可能被多个上下文修改时，调用方必须在外部串行化。

### 2. 自旋锁

```c
static mr_spinlock_t lock = MR_SPINLOCK_INIT();

int mask = mr_spinlock_lock_irqsave(&lock);
/* 短临界区。 */
mr_spinlock_unlock_irqrestore(&lock, mask);
```

`mr_spinlock_lock` 会忙等，`mr_spinlock_try_lock` 只尝试一次。`irqsave`
变体先保存并关闭本地中断，解锁时必须把原 mask 传回
`mr_spinlock_unlock_irqrestore`。不要在自旋锁临界区中阻塞、等待事件或执行无界耗时操作。

### 3. 引用计数

`mr_ref_t` 把 magic 和计数存在同一原子值中。`mr_ref_init` 以计数 1 开始生命周期；
`mr_ref_get` 仅在 magic 有效且计数非零时成功；
`mr_ref_put` 在最后一个引用上先清除 magic，再同步调用可选 `release`。

`MR_USE_REF_LONG_COUNT` 关闭时计数字段为 8 位，开启时为 16
位。计数溢出会使引用失效并触发断言，不应把引用计数当作任意大的持有者计数器。完整对象生命周期请见 [对象系统](object.md)。

### 4. 通用工具宏与奇偶校验

`mr_service.h` 提供 `MR_CONTAINER_OF`、`MR_ARRAY_SIZE`、`MR_MIN/MAX`、
`MR_ALIGN_UP/DOWN`、`MR_ROUND_DIV`、`MR_MAKE_LOCAL`
、`MR_BSWAP16/32/64`、参数计数、拼接及条件展开等宏，以及 `mr_parity_even/odd`。

`MR_MIN`、`MR_MAX` 可能多次求值参数，不得传入带自增、函数调用等副作用的表达式。
`MR_ALIGN_UP/DOWN` 使用位运算，对齐值应为 2
的幂。`MR_BSWAP16/32/64` 分别反转 16/32/64 位值的字节顺序，与当前 CPU
字节序无关；参数会多次求值，不得传入带副作用的表达式。

### 5. 预处理展开与版本

`MR_LISTIFY(n, separator, fn, ...)` 在预处理阶段展开 `fn(0, ...)` 到
`fn(n - 1, ...)`，支持的 n 上限为 4095；
`MR_LISTIFY_NEST` 是用于避免同名递归展开冲突的辅助变体，上限为
255。这些宏主要用于静态初始化和代码生成，不产生运行期循环。

`mr_version.h` 提供 `MR_VERSION_MAJOR/MINOR/PATCH`、`MR_VERSION`、
`MR_VERSION_CODE`、`MR_VERSION_IS`。`MR_VERSION` 编码为
`major << 16 | minor << 8 | patch`。

---

## 开发者指南

### 6. 并发和生命周期边界

- list/service/version/listify 都不提供自动并发保护。
- spinlock 的真实跨核和 IRQ 语义依赖 `mr_atomic_*` 与 `mr_port_irq_*` 的平台实现。
- ref 的计数更新为原子操作，但最后一次 put 会在当前调用上下文同步执行
  release；release 能否阻塞或在 ISR 执行由上层决定。
- 上述工具都不跟踪业务对象所有权，不会防止误用已释放容器或节点。

### 7. 源码导读

| 位置                                   | 职责                        |
|----------------------------------------|-----------------------------|
| `mrX/include/mrX/kernel/mr_list.h`     | 侵入式双向链表              |
| `mrX/include/mrX/kernel/mr_listify.h`  | 预处理序列展开              |
| `mrX/include/mrX/kernel/mr_ref.h`      | magic + count 原子引用计数  |
| `mrX/include/mrX/kernel/mr_service.h`  | 通用 C/预处理工具与奇偶校验 |
| `mrX/include/mrX/kernel/mr_spinlock.h` | 原子自旋锁与 irqsave 包装   |
| `mrX/include/mrX/kernel/mr_version.h`  | 版本常量与编码宏            |
