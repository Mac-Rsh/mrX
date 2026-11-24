# 内核基础工具（Foundation）

集中提供头文件级的侵入式双向链表、自旋锁、原子引用计数、预处理展开、通用工具宏与版本宏，供内核各模块与应用直接使用。

| 项目   | 说明                                                                                                  |
|--------|-------------------------------------------------------------------------------------------------------|
| 功能宏 | 无（头文件级基础能力始终可用；`MR_USE_REF_LONG_COUNT` 控制引用计数位宽）                              |
| 头文件 | `<mrX/kernel/mr_list.h>`、`mr_listify.h`、`mr_ref.h`、`mr_service.h`、`mr_spinlock.h`、`mr_version.h` |
| 依赖   | 原子操作（`mr_atomic`）、平台 IRQ 接口（`mr_port_irq`）                                               |
| 测试   | `test/kernel/list.c`、`ref.c`、`service.c`、`spinlock.c`（及对应 `*_deep.c`）                         |

## 配置

| 配置项                  | 类型 | 默认值 | 说明                                                   |
|-------------------------|------|--------|--------------------------------------------------------|
| `MR_USE_REF_LONG_COUNT` | bool | n      | 启用 16 位引用计数（默认 8 位，最大支持 255 并发引用） |

## 快速上手

侵入式链表与关中断自旋锁的常见用法：

```c
#include <mrX/kernel/mr_list.h>
#include <mrX/kernel/mr_spinlock.h>

typedef struct {
    int value;
    mr_list_t node;
} item_t;

static mr_list_t items = MR_LIST_INIT(&items);
static mr_spinlock_t items_lock = MR_SPINLOCK_INIT();

void item_push(item_t *item) {
    int mask;

    mr_list_init(&item->node);
    mask = mr_spinlock_lock_irqsave(&items_lock);
    mr_list_add(&items, &item->node);
    mr_spinlock_unlock_irqrestore(&items_lock, mask);
}
```

## 核心机制与约束

- **侵入式链表（`mr_list.h`）**：`mr_list_t` 既作链表头也作节点，空表时 `next`/
  `prev` 均指向自身。链表操作本身不加锁，多执行流访问时需外部同步；遍历过程中删除当前节点必须使用
  `MR_LIST_FOR_EACH_SAFE`。
- **自旋锁（`mr_spinlock.h`）**：`mr_spinlock_lock()` 会忙等，不可在临界区内阻塞、休眠或执行无界耗时操作；
  `irqsave` 变体会保存并关闭本地中断，解锁时必须原样传回 `mask`。
- **引用计数（`mr_ref.h`）**：`mr_ref_t` 将 magic 与计数编码在同一原子字中。
  `mr_ref_init()` 赋予初始计数 1；`mr_ref_get()` 仅在 magic 有效且计数非零时递增；
  `mr_ref_put()` 在最后一个引用归零时清除 magic 并在当前上下文同步调用 `release`
  。计数溢出会覆盖 magic 为 `MR_REF_OVERFLOW` 并触发 `MR_ASSERT`。
- **通用工具宏（`mr_service.h`）**：`MR_MIN`、`MR_MAX`、`MR_BSWAP16/32/64`
  等宏可能对参数多次求值，禁止传入带自增或函数调用等副作用的表达式；
  `MR_ALIGN_UP` / `MR_ALIGN_DOWN` 的对齐参数必须为 2 的幂。
- **预处理展开（`mr_listify.h`）**：`MR_LISTIFY(n, sep, fn, ...)` 在预处理期展开
  `fn(0, ...)` 至 `fn(n - 1, ...)`，上限 `n <= 4095`；嵌套版本 `MR_LISTIFY_NEST`
  上限为 `255`。

## API 速查

### 侵入式链表（`mr_list.h`）

| API / 宏                                           | 说明                                              |
|----------------------------------------------------|---------------------------------------------------|
| `MR_LIST_INIT` / `mr_list_init`                    | 静态 / 运行期初始化链表头或节点                   |
| `MR_LISTS_INIT` / `MR_LIST_ARRAY_INIT`             | 批量初始化固定长度链表头数组（依赖 `MR_LISTIFY`） |
| `mr_list_add` / `mr_list_insert`                   | 尾插 / 头插节点                                   |
| `mr_list_del` / `mr_list_replace` / `mr_list_move` | 摘链（节点重置为自指）/ 替换 / 移动到目标链表尾部 |
| `mr_list_is_empty`                                 | 判断链表是否为空                                  |
| `MR_LIST_ENTRY(_ptr, _type, _member)`              | 由链表节点指针获取外层结构体指针                  |
| `MR_LIST_FOR_EACH` / `MR_LIST_FOR_EACH_SAFE`       | 普通遍历 / 允许在循环体中摘除当前节点的安全遍历   |

### 自旋锁与引用计数（`mr_spinlock.h`、`mr_ref.h`）

| API / 宏                                                           | 说明                                        |
|--------------------------------------------------------------------|---------------------------------------------|
| `MR_SPINLOCK_INIT` / `mr_spinlock_init`                            | 静态 / 运行期初始化自旋锁                   |
| `mr_spinlock_lock` / `mr_spinlock_try_lock` / `mr_spinlock_unlock` | 忙等加锁 / 非阻塞尝试加锁 / 解锁            |
| `mr_spinlock_lock_irqsave` / `mr_spinlock_unlock_irqrestore`       | 关中断加锁并返回掩码 / 解锁并恢复中断       |
| `mr_spinlock_is_locked`                                            | 查询锁当前是否被持有                        |
| `MR_REF_INIT` / `mr_ref_init`                                      | 初始化引用计数（初始值为 1）                |
| `mr_ref_get` / `mr_ref_put`                                        | 安全递增引用 / 递减并在归零时执行 `release` |
| `MR_REF_IS_INITED` / `MR_REF_COUNT`                                | 检查 magic 是否有效 / 读取当前计数值        |

### 工具宏与版本宏（`mr_service.h`、`mr_listify.h`、`mr_version.h`）

| API / 宏                                                                            | 说明                                               |
|-------------------------------------------------------------------------------------|----------------------------------------------------|
| `MR_CONTAINER_OF` / `MR_ARRAY_SIZE` / `MR_MAKE_LOCAL`                               | 结构体成员反推 / 数组元素个数 / 生成唯一局部变量名 |
| `MR_MIN` / `MR_MAX` / `MR_ALIGN_UP` / `MR_ALIGN_DOWN` / `MR_ROUND_DIV`              | 极值、2 的幂对齐与四舍五入除法                     |
| `MR_BSWAP16` / `MR_BSWAP32` / `MR_BSWAP64`                                          | 16 / 32 / 64 位字节序反转                          |
| `mr_parity_even` / `mr_parity_odd`                                                  | 偶校验 / 奇校验计算                                |
| `MR_LISTIFY` / `MR_LISTIFY_NEST`                                                    | 预处理序列展开                                     |
| `MR_VERSION_MAJOR/MINOR/PATCH` / `MR_VERSION` / `MR_VERSION_CODE` / `MR_VERSION_IS` | 版本号常量与比较宏                                 |
