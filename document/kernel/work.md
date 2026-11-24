# 工作队列（Work）

提供可延迟执行的回调调度机制：立即工作直接进入执行链表，延时工作由工作队列内嵌软定时器到期后转入执行链表，最终在
`mr_workqueue_execute()` 调用者上下文中执行。

| 项目   | 说明                                                          |
|--------|---------------------------------------------------------------|
| 功能宏 | `MR_USE_WORK`（自动选中 `MR_USE_TIMER`）                      |
| 头文件 | `<mrX/kernel/mr_work.h>`                                      |
| 依赖   | [时钟](clock.md)、[软定时器](timer.md)、[对象系统](object.md) |
| 测试   | `test/kernel/work.c`、`test/kernel/work_deep.c`               |

## 配置

| 配置项                   | 类型 | 默认值 | 说明                                                                     |
|--------------------------|------|--------|--------------------------------------------------------------------------|
| `MR_USE_WORK`            | bool | y      | 启用工作队列模块，自动选中 `MR_USE_TIMER`                                |
| `MR_USE_WORKQUEUE_HOOK`  | bool | n      | 启用 `wakeup` / `suspend` 生命周期钩子（`MR_USE_IRQ_DEFER_HOOK` 会选中） |
| `MR_CFG_TICK_PER_SECOND` | int  | 1000   | 系统 tick 频率（Hz），决定延时调度粒度                                   |

## 快速上手

```c
#include <mrX/kernel/mr_work.h>

static void my_work_cb(struct mr_work *work, void *param) {
    (*(int *)param)++;
}

void work_demo(void) {
    static mr_workqueue_t queue;
    static mr_work_t work;
    int count = 0;

    mr_workqueue_init(&queue);
    mr_work_init(&work, my_work_cb, &count);
    mr_work_bind(&work, &queue);

    mr_work_schedule(&work);         /* 立即入队，此时不执行回调 */
    mr_workqueue_execute(&queue);    /* 在当前上下文排空并执行回调 */

    mr_work_del(&work);
    mr_workqueue_del(&queue);
}
```

## 核心机制与约束

- **执行上下文**：工作队列本身不创建线程，所有 work 回调都在调用
  `mr_workqueue_execute()` 的上下文（主循环或专用线程）中同步执行。是否允许睡眠或阻塞取决于
  `execute()` 所在上下文。
- **立即与延时调度**：`mr_work_delayed_schedule()` /
  `mr_workqueue_delayed_work()` 的延时上限为 `MR_TICK_MAX / 2 - 1` 个
  tick（断言校验）；`tick == 0` 等价于立即调度。
- **状态与重复入队**：`MR_WORK_IS_RUNNING(_work)` 仅检查 `work->list`
  是否非空（即仍在立即链表或延时链表中排队）；进入回调前 work 已出队，因此回调期间该宏为
  `MR_FALSE`，且回调内 `mr_work_cancel()` 安全无效果。对已入队的 work 再次调度返回
  `-MR_EBUSY`；未绑定队列或目标队列正在销毁（`dying`）返回 `-MR_ENOENT`。
- **生命周期与绑定**：入队与 `mr_work_bind()` 均不增加 `work` 的对象引用计数，调用方需保证
  `work` 在取消或执行完毕前存活；`mr_work_bind()` 直接覆写 `work->queue`
  且不检查是否正在排队，应在空闲时调用。`mr_workqueue_del()` 会立即置位 `dying`
  拒绝新任务，并在引用计数归零时停用内嵌定时器、摘除所有未执行 work。
- **钩子集成（`MR_USE_WORKQUEUE_HOOK`）**：`mr_workqueue_hook_set()` 仅保存 `hook`
  指针而不复制结构体。`wakeup` 在有新工作进入空闲队列时触发，`suspend` 在
  `execute()` 排空所有立即工作后触发（通过原子计数器 `depth` 抑制重复通知），典型用法是配合初值为
  0 的信号量唤醒后台工作线程。

## API 速查

### 工作队列（`mr_workqueue_t`）

| API                                                     | 说明                                                       |
|---------------------------------------------------------|------------------------------------------------------------|
| `mr_err_t mr_workqueue_init(mr_workqueue_t *queue)`     | 初始化静态工作队列                                         |
| `mr_workqueue_t *mr_workqueue_create(void)`             | 堆上动态创建工作队列，失败返回 `MR_NULL`                   |
| `mr_err_t mr_workqueue_del(mr_workqueue_t *queue)`      | 标记 `dying` 并归还对象引用                                |
| `mr_err_t mr_workqueue_execute(mr_workqueue_t *queue)`  | 排空当前立即链表并逐个执行回调                             |
| `mr_err_t mr_workqueue_work(queue, work)`               | 向指定队列提交立即工作                                     |
| `mr_err_t mr_workqueue_delayed_work(queue, work, tick)` | 向指定队列提交延时工作                                     |
| `mr_err_t mr_workqueue_hook_set(queue, hook)`           | 设置 `wakeup`/`suspend` 钩子（需 `MR_USE_WORKQUEUE_HOOK`） |
| `mr_workqueue_get(queue)` / `mr_workqueue_put(queue)`   | 增减对象引用计数                                           |

### 工作项（`mr_work_t`）

| API                                             | 说明                                              |
|-------------------------------------------------|---------------------------------------------------|
| `MR_WORK_INIT(_work, _entry, _param)`           | 静态初始化 work（另有 `MR_WORK_DERIVE_INIT`）     |
| `mr_err_t mr_work_init(work, entry, param)`     | 运行时初始化静态 work（`entry` 不可为 `MR_NULL`） |
| `mr_work_t *mr_work_create(entry, param)`       | 堆上动态创建 work，失败返回 `MR_NULL`             |
| `mr_err_t mr_work_del(work)`                    | 取消排队并归还对象引用                            |
| `mr_err_t mr_work_bind(work, queue)`            | 绑定 work 到目标队列                              |
| `mr_err_t mr_work_schedule(work)`               | 立即调度到已绑定的队列                            |
| `mr_err_t mr_work_delayed_schedule(work, tick)` | 延时调度到已绑定的队列                            |
| `mr_err_t mr_work_cancel(work)`                 | 从立即/延时链表摘除                               |
| `MR_WORK_IS_RUNNING(_work)`                     | 判断 work 是否仍在排队中                          |
| `mr_work_get(work)` / `mr_work_put(work)`       | 增减对象引用计数                                  |

## 内部实现

```text
mr_work_delayed_schedule() ──► queue->tlist (按到期 tick 升序)
                                    │
                       内嵌 timer 到期 (queue_timer_entry)
                                    ▼
mr_work_schedule() ──────────► queue->list (立即执行链表)
                                    │
                       mr_workqueue_execute() 换出到局部 oplist
                                    ▼
                           解锁逐个执行 work->entry()
```

`kernel/work.c` 使用全局自旋锁 `__work_lock`（`mr_spinlock_lock_irqsave`）保护链表操作。
`mr_workqueue_execute()` 将当前 `queue->list` 整体换出到局部 `oplist`
后逐个出队并解锁执行回调，因此回调期间提交的新工作会进入下一轮 `execute()`
，不会引发链表迭代冲突。延时链表 `tlist` 由工作队列内嵌的 `queue->timer`
驱动，每次插入队头或到期检查后都会动态调整为距离下一个最近任务的剩余 tick
数（无延时任务时自动停止定时器）。
