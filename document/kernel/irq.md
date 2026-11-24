# 中断管理（Irq）

基于全局中断描述符表提供跨平台的中断控制器接入、动作申请、共享中断分发、使能深度控制与可选的下半部（defer）延迟执行。

| 项目   | 说明                                                                                          |
|--------|-----------------------------------------------------------------------------------------------|
| 功能宏 | `MR_USE_IRQ`（默认开启）、`MR_USE_IRQ_DEFER`（默认开启）、`MR_USE_IRQ_DEFER_HOOK`（默认关闭） |
| 头文件 | `<mrX/kernel/mr_irq.h>`                                                                       |
| 依赖   | [对象系统（Object）](object.md)、[工作队列（Work）](work.md)（启用 `MR_USE_IRQ_DEFER` 时）    |
| 测试   | `test/kernel/irq.c`、`test/kernel/irq_deep.c`                                                 |

## 配置

| 配置项                  | 类型 | 默认值 | 说明                                                                   |
|-------------------------|------|--------|------------------------------------------------------------------------|
| `MR_USE_IRQ`            | bool | y      | 启用中断管理模块                                                       |
| `MR_CFG_IRQ_TABLE_SIZE` | int  | 16     | 全局中断描述符表大小（范围 2~1024；未定义时 `kernel/irq.c` 兜底为 32） |
| `MR_USE_IRQ_DEFER`      | bool | y      | 启用中断下半部延迟执行（自动选中 `MR_USE_WORK`）                       |
| `MR_USE_IRQ_DEFER_HOOK` | bool | n      | 启用下半部工作队列的挂起/唤醒钩子（自动选中 `MR_USE_WORKQUEUE_HOOK`）  |

## 快速上手

```c
#include <mrX/kernel/mr_irq.h>

/* 控制器后端需实现全部 6 个硬件操作 */
static const mr_irq_ops_t gpio_irq_ops = {
    .priority_set = /* ... */,
    .type_set     = /* ... */,
    .enable       = /* ... */,
    .unmask       = /* ... */,
    .disable      = /* ... */,
    .mask         = /* ... */,
};

static mr_irq_return_t button_handler(mr_uint32_t irq, void *owner) {
    /* 顶半部：应答硬件并快速处理 */
    return MR_IRQ_HANDLED;
}

int irq_demo(void) {
    static mr_irq_t gpio_irq;

    /* 注册接管 [0, 0] 号中断的控制器 */
    if (mr_irq_init(&gpio_irq, 0, 0, &gpio_irq_ops, MR_NULL) != 0) {
        return -1;
    }
    /* 申请 0 号中断，以上升沿触发 */
    return mr_irq_request(0, button_handler, &gpio_irq, MR_IRQ_EDGE_RISING);
}

/* 平台顶层 ISR 入口 */
void gpio_isr(void) {
    mr_irq_handle(0);
}
```

## 核心机制与约束

- **控制器区间与唯一性**：控制器通过 `mr_irq_init()` 或 `mr_irq_create()`
  接管连续全局中断号 `[irq_start, irq_end]`，不可超出 `MR_CFG_IRQ_TABLE_SIZE`
  且不可与已注册控制器重叠（冲突返回 `-MR_EEXIST`）。调用 `mr_irq_del()`
  会自动关闭硬件并释放该区间上的所有动作。
- **共享中断与 flags 约束**：首个申请者通过 `flags`（触发类型位 + 可选
  `MR_IRQ_SHARED`）配置硬件触发类型和默认优先级（`0`）。若未携带 `MR_IRQ_SHARED`
  ，后续申请返回 `-MR_EBUSY`；若已共享但后续申请的 `flags` 不完全一致，返回
  `-MR_EINVAL`。同一中断号上同一 `owner` 重复申请返回 `-MR_EEXIST`。
- **使能深度嵌套**：`mr_irq_enable()` / `mr_irq_disable()` 维护原子深度计数器
  `depth`（初始为 `1` 关闭态）。仅在 `1 -> 0` 跃迁时调用硬件 `unmask + enable`，
  `0 -> 1` 跃迁时调用 `disable + mask`。申请动作隐含一次 `enable`，释放最后一个动作隐含一次
  `disable`；无动作时多余的 `disable` 会将 `depth` 钳回 `1`。
- **中断分发与自释放**：`mr_irq_handle()` 先调用 `ops->mask`，再按注册顺序调用动作回调。返回
  `MR_IRQ_HANDLED` 即停止分发；返回 `MR_IRQ_NONE` 则继续下一动作；分发结束时若
  `depth == 0` 则 `ops->unmask`。动作回调在锁外执行，支持在回调内部调用
  `mr_irq_free()` 释放自身（框架通过 `pending` 计数延迟回收内存）。
- **下半部（defer）**：使用 `mr_irq_defer_request()` 注册 `defer_entry`，当顶半部返回
  `MR_IRQ_DEFERRED` 时动作被投递到内部工作队列，并在调用 `mr_irq_defer_execute()`
  的上下文中同步执行（框架不自动创建线程；可配合 `mr_irq_defer_hook_set()`
  唤醒专用线程）。若用普通 `mr_irq_request()` 申请却返回 `MR_IRQ_DEFERRED`，将按
  `MR_IRQ_HANDLED` 对待。
- **后端 ops 契约**：`mr_irq_ops_t` 的 6 个回调均为必需成员（核心直接解引用，不做空指针检查），且通常在持有全局自旋锁
  `__irq_lock` 期间调用，实现必须短小且避免重入 Irq 接口。

## API 速查

### 控制器管理

| API                                                                                                                    | 说明                                              |
|------------------------------------------------------------------------------------------------------------------------|---------------------------------------------------|
| `mr_err_t mr_irq_init(mr_irq_t *irq, mr_uint32_t irq_start, mr_uint32_t irq_end, const mr_irq_ops_t *ops, void *priv)` | 静态初始化中断控制器并接管 `[irq_start, irq_end]` |
| `mr_irq_t *mr_irq_create(mr_uint32_t irq_start, mr_uint32_t irq_end, const mr_irq_ops_t *ops, void *priv)`             | 动态创建中断控制器（失败返回 `MR_NULL`）          |
| `mr_err_t mr_irq_del(mr_irq_t *irq)`                                                                                   | 标记 `dying` 并删除控制器，释放区间内所有动作     |
| `mr_irq_t *mr_irq_get(mr_irq_t *irq)` / `mr_bool_t mr_irq_put(mr_irq_t *irq)`                                          | 增加 / 减少控制器对象引用计数                     |

### 动作申请、控制与分发

| API                                                                                                                                  | 说明                                                                         |
|--------------------------------------------------------------------------------------------------------------------------------------|------------------------------------------------------------------------------|
| `mr_err_t mr_irq_request(mr_uint32_t irq, mr_irq_entry_t *entry, void *owner, mr_uint16_t flags)`                                    | 申请中断动作（`owner` 不可为 `MR_NULL`，`flags` 不可为 `0`）                 |
| `mr_err_t mr_irq_defer_request(mr_uint32_t irq, mr_irq_entry_t *entry, void *owner, mr_uint16_t flags, mr_irq_entry_t *defer_entry)` | 申请带下半部的中断动作（需 `MR_USE_IRQ_DEFER`）                              |
| `mr_err_t mr_irq_free(mr_uint32_t irq, void *owner)`                                                                                 | 按 `owner` 释放中断动作（若有 `pending` 则延迟回收）                         |
| `mr_err_t mr_irq_enable(mr_uint32_t irq)` / `mr_err_t mr_irq_disable(mr_uint32_t irq)`                                               | 使能 / 关闭指定中断（深度嵌套计数）                                          |
| `int mr_irq_type(mr_uint32_t irq)`                                                                                                   | 读取触发类型位（低 4 位）；无控制器返回 `-MR_ENOENT`                         |
| `mr_err_t mr_irq_priority_set(mr_uint32_t irq, mr_uint8_t priority)` / `int mr_irq_priority(mr_uint32_t irq)`                        | 设置 / 读取优先级（`0` 最高，`255` 最低）                                    |
| `mr_irq_return_t mr_irq_handle(mr_uint32_t irq)`                                                                                     | 在 ISR 中分发指定中断，返回处理状态                                          |
| `void mr_irq_defer_execute(void)`                                                                                                    | 执行内部工作队列中已投递的下半部（需 `MR_USE_IRQ_DEFER`）                    |
| `mr_err_t mr_irq_defer_hook_set(const mr_irq_defer_hook_t *hook)`                                                                    | 设置下半部工作队列的 `wakeup` / `suspend` 钩子（需 `MR_USE_IRQ_DEFER_HOOK`） |

### 标志位与返回值枚举

- **触发类型与共享标志**：`MR_IRQ_EDGE_RISING`（`0x01`）、`MR_IRQ_EDGE_FALLING`（
  `0x02`）、`MR_IRQ_EDGE_BOTH`（`0x03`）、`MR_IRQ_LEVEL_HIGH`（`0x04`）、
  `MR_IRQ_LEVEL_LOW`（`0x08`）、`MR_IRQ_SHARED`（`0x10`）。
- **回调返回值 `mr_irq_return_t`**：`MR_IRQ_NONE`（未处理）、`MR_IRQ_HANDLED`（已处理）、
  `MR_IRQ_DEFERRED`（已转下半部）。

## 内部实现

核心实现位于 `kernel/irq.c`：

- **描述符表与锁边界**：`__irq_table[MR_CFG_IRQ_TABLE_SIZE]` 保存每条中断线的
  `action` 链表、`depth`、`flags`、`priority` 及所属控制器指针。全局关中断自旋锁
  `__irq_lock` 保护描述符表与动作链修改；`mr_irq_handle()` 在持有控制器对象引用后释放
  `__irq_lock` 再执行用户回调，确保回调内可重入 `mr_irq_free()` 等接口。
- **延迟回收（`pending`）**：每次进入顶半部回调或投递 defer 工作队列时
  `action->pending++`，退出时 `pending--`。若期间发生过 `mr_irq_free()` 或
  `mr_irq_del()`，动作的 `owner` 会被置为 `MR_NULL` 并跳过后续分发，直到 `pending`
  归零时才执行 `mr_work_del()` 与 `mr_free()`。
