# 工作队列（Work）

> 工作队列提供可延迟执行的回调（work）调度：立即工作由后续 `execute` 批次处理，
> 延时工作由内嵌软定时器到点转入执行链表。回调最终运行在 `mr_workqueue_execute()`
> 的调用者上下文；模块本身不创建线程。

| 项       | 说明                                                                                    |
|----------|-----------------------------------------------------------------------------------------|
| 功能宏   | `MR_USE_WORK`（默认开启，自动选中 `MR_USE_TIMER`）；`MR_USE_WORKQUEUE_HOOK`（默认关闭） |
| 头文件   | `<mrX/kernel/mr_work.h>`                                                                |
| 依赖模块 | [时钟 Clock](clock.md)、[软定时器 Timer](timer.md)、[对象 object](object.md)            |
| 参考测试 | `mrX/test/kernel/work.c`                                                                |

---

## 用户指南

### 1. 模块概述

工作队列解决的问题是： **把一段回调推迟到"合适的时候"再执行——立即推迟到 下一轮排空，或延迟指定 tick 后执行**。特性：

- 两类对象：工作队列 `mr_workqueue_t`（调度容器）与工作 `mr_work_t`（回调 载体）；一个工作绑定一个队列，一个队列可承载任意多个工作。
- 立即调度 `mr_work_schedule` 与延时调度 `mr_work_delayed_schedule`；延时
  工作按到期时间有序排队，队列内嵌一个 [软定时器](timer.md) 管理。
- 回调执行在 `mr_workqueue_execute` **调用者的上下文**：队列不创建线程， 用户可自由选择主循环轮询或专用线程（配合 hook）驱动。
- 回调允许链式提交新工作，新工作在下一轮 `execute` 执行。
- 可选的唤醒/挂起钩子（`MR_USE_WORKQUEUE_HOOK`）：以原子 `depth` 抑制重复 wakeup，并在 execute 批次结束时按当前状态决定是否调用
  suspend，可用于衔接信号量 + 线程等自定义执行机制。
- 内核内已有使用者：[中断 irq](irq.md) 模块的延迟下半部（`MR_USE_IRQ_DEFER`
  自动选中本模块，`mrX/kernel/irq.c`）。

与 [软定时器 Timer](timer.md) 的分工：定时器回调运行在 `mr_clock_increase()` 调用者上下文；工作队列把真正的 work 回调推迟到
`execute` 调用者上下文。若应用选择在线程中调用 execute，就可以把处理从 ISR/tick 路径移出，但这不是 Work 模块自动创建的上下文切换。

### 2. 配置与依赖

| 配置项                   | 类型 | 默认值 | 说明                                                                          |
|--------------------------|------|--------|-------------------------------------------------------------------------------|
| `MR_USE_WORK`            | bool | y      | 启用工作队列，自动选中 `MR_USE_TIMER`（`mrX/include/mrX/kernel/Kconfig:160`） |
| `MR_USE_WORKQUEUE_HOOK`  | bool | n      | 启用队列唤醒/挂起钩子（`Kconfig:171`）                                        |
| `MR_CFG_TICK_PER_SECOND` | 整型 | 1000   | 时钟模块配置，决定延时调度的粒度                                              |

```c
#include <mrX/kernel/mr_work.h>
```

该头文件自动引入定时器等依赖头。`MR_USE_WORKQUEUE_HOOK` 开启后
`mr_workqueue_hook_set` 与 `mr_workqueue_hook_t` 才可用；[中断 irq](irq.md)
模块的延迟执行功能会自动选中本模块及钩子。

延时以 tick 为单位（毫秒换算用 `MR_TICK_MSEC(ms)`），上限为
`MR_TICK_MAX / 2 - 1` 个 tick（断言校验）。

### 3. 快速上手

一个"提交后由 execute 执行"的最小示例（改编自 `mrX/test/kernel/work.c:116`）：

```c
#include <mrX/kernel/mr_work.h>

static void my_work_entry(struct mr_work *work, void *param) {
    (*(int *)param)++;
    /* Heavy handling runs in the execute() caller context */
}

void work_demo(void) {
    static mr_workqueue_t queue;
    static mr_work_t work;
    int count = 0;

    mr_workqueue_init(&queue);
    mr_work_init(&work, my_work_entry, &count);
    mr_work_bind(&work, &queue);            /* Bind before scheduling */

    mr_work_schedule(&work);                /* Submit: runs nothing yet */
    mr_workqueue_execute(&queue);           /* Drain: my_work_entry runs */
    mr_workqueue_del(&queue);
}
```

要点：

- work 入队只是建立链表关系，队列不会自动增加 work 的对象引用；调用者必须保证 work 至少存活到取消或回调执行完成。
- `mr_work_bind` 必须在 `mr_work_schedule` 之前调用，未绑定的工作调度返回
  `-MR_ENOENT`。
- `mr_workqueue_execute` 是执行点：不调用它，回调永远不会运行。

### 4. 使用说明

**生命周期**：静态方式定义对象后 `*_init` 初始化（work 亦可用 `MR_WORK_INIT`），动态方式用 `*_create`。`mr_work_del` 会先取消该
work，再执行一次对象 `del`；动态内存只在最后一个引用释放时归还。`mr_workqueue_del` 会先置 `dying`，真正停用内嵌 timer、摘除队列中的
work 和释放动态内存同样发生在 workqueue 的最后一个引用释放路径。

**运行期行为约定**

| 操作                                                             | 行为                                           |
|------------------------------------------------------------------|------------------------------------------------|
| 未 `bind` 的工作 `mr_work_schedule` / `mr_work_delayed_schedule` | 返回 `-MR_ENOENT`                              |
| 已排队（立即或延时）的工作再次调度                               | 返回 `-MR_EBUSY`，不重复排队                   |
| `MR_WORK_IS_RUNNING`                                             | 已入队即返回真（含延时等待期，回调运行中为假） |
| `mr_work_delayed_schedule` 的 `tick` 为 0                        | 等价立即调度                                   |
| 提交时观察到队列 `dying`                                         | 返回 `-MR_ENOENT`                              |
| `mr_work_cancel`                                                 | 把工作从排队/延时链表摘除；对空闲工作安全      |
| 回调内 `mr_work_schedule` 新工作                                 | 支持，新工作在下一轮 `execute` 执行            |
| 回调内 `mr_work_cancel` 自身                                     | 无效果且安全（回调开始时工作已出队）           |

`MR_WORK_IS_RUNNING` 实际只表示 `work->list` 是否挂链：work 被 execute 从批次链摘下、即将进入或正在执行回调时它已经为假，因此不能把该宏当作“回调正在运行”的状态位。

**回调上下文**：回调运行在 `mr_workqueue_execute` 调用者上下文——由集成方决定它是主循环、专用线程还是其他上下文。Work
层本身不授予“可以阻塞”的能力；回调可做什么仍取决于 execute 的调用环境。回调期间不持 `__work_lock`，因此可重新
schedule/cancel，但对象最终释放仍需遵守生命周期约束。

**钩子集成模式**（`MR_USE_WORKQUEUE_HOOK`）：推荐用初值为 0 的信号量作钩子 （`mrX/include/mrX/kernel/mr_work.h:125` 注释）——
`wakeup` 中释放信号量唤醒 执行线程，线程循环 `mr_workqueue_execute`；`suspend` 中等待下一次唤醒。
`queue_wakeup` 仅在 `depth` 的 `fetch_add` 观察到旧值为 0 时调用 `wakeup`；execute 开始时会把 depth 重置为
1，批次结束且没有下一批立即工作时再通过 `queue_suspend` 做一次 `fetch_sub`，只有旧值为 1 才调用 `suspend`
。因此它是通知抑制机制，不应解释成精确的待处理 work 数量。

### 5. API 参考

工作队列：

| API                                               | 说明                                            |
|---------------------------------------------------|-------------------------------------------------|
| `mr_workqueue_init` / `mr_workqueue_create`       | 初始化静态队列 / 堆上动态创建                   |
| `mr_workqueue_del`                                | 删除队列（摘除其上全部工作）                    |
| `mr_workqueue_execute`                            | 排空队列，逐个执行待处理工作的回调              |
| `mr_workqueue_work` / `mr_workqueue_delayed_work` | 向队列提交立即 / 延时工作                       |
| `mr_workqueue_hook_set`                           | 设置唤醒/挂起钩子（需 `MR_USE_WORKQUEUE_HOOK`） |
| `mr_workqueue_get` / `mr_workqueue_put`           | 引用计数加一 / 减一                             |

工作：

| API                                             | 说明                             |
|-------------------------------------------------|----------------------------------|
| `MR_WORK_INIT` / `MR_WORK_DERIVE_INIT`          | 静态初始化宏（派生版见第 12 节） |
| `mr_work_init` / `mr_work_create`               | 初始化静态工作 / 堆上动态创建    |
| `mr_work_del`                                   | 删除工作（自动取消）             |
| `mr_work_bind`                                  | 绑定工作到队列                   |
| `mr_work_schedule` / `mr_work_delayed_schedule` | 提交立即 / 延时工作到绑定的队列  |
| `mr_work_cancel`                                | 取消排队中的工作                 |
| `MR_WORK_IS_RUNNING`                            | 宏：查询工作是否已入队           |
| `mr_work_get` / `mr_work_put`                   | 引用计数加一 / 减一              |

**`mr_workqueue_init` / `mr_workqueue_create` / `mr_workqueue_del`**

```c
mr_err_t mr_workqueue_init(mr_workqueue_t *queue);
mr_workqueue_t *mr_workqueue_create(void);
mr_err_t mr_workqueue_del(mr_workqueue_t *queue);
```

`init` 初始化调用者分配的存储（含内嵌 timer）；`create` 堆分配（内存不足返回 `MR_NULL`）。`del` 立即把 `dying` 置真并执行一次对象
`del`；只有引用计数真正到 0 时，release 才停用内嵌 timer 并摘除 `tlist/list` 上的 work。

**`mr_workqueue_execute`**

```c
mr_err_t mr_workqueue_execute(mr_workqueue_t *queue);
```

把调用开始时的 `queue->list` 整体换到局部批次并逐个执行，返回 `0`。执行期间新提交到 `queue->list` 的 work 留给下一次
`execute`。调用者必须保证 queue 在整个 execute 期间存活；函数自身不会额外 get queue。

**`mr_workqueue_work` / `mr_workqueue_delayed_work`**

```c
mr_err_t mr_workqueue_work(mr_workqueue_t *queue, mr_work_t *work);
mr_err_t mr_workqueue_delayed_work(mr_workqueue_t *queue, mr_work_t *work,
                                   mr_tick_t tick);
```

面向"持有队列指针"的提交方式；`tick` 为 0 时等价立即提交。工作已排队返回
`-MR_EBUSY`，队列 dying 返回 `-MR_ENOENT`。

**`mr_work_init` / `mr_work_create` / `mr_work_bind`**

```c
mr_err_t mr_work_init(mr_work_t *work, mr_work_entry_t *entry, void *param);
mr_work_t *mr_work_create(mr_work_entry_t *entry, void *param);
mr_err_t mr_work_bind(mr_work_t *work, mr_workqueue_t *queue);
```

- `entry`：回调，类型 `void (*)(struct mr_work *work, void *param)`，不可为
  `MR_NULL`；`param`：透传给回调的用户参数。
- `bind` 只是直接覆盖 `work->queue` 指针，不持有 queue 引用、也不检查 work 是否正在挂链。应只在 work 空闲且目标 queue
  生命周期明确时绑定/重新绑定。

**`mr_work_schedule` / `mr_work_delayed_schedule` / `mr_work_cancel`**

```c
mr_err_t mr_work_schedule(mr_work_t *work);
mr_err_t mr_work_delayed_schedule(mr_work_t *work, mr_tick_t tick);
mr_err_t mr_work_cancel(mr_work_t *work);
#define MR_WORK_IS_RUNNING(_work)       (!mr_list_is_empty(&(_work)->list))
```

向 `work->queue` 指向的队列提交；未绑定、绑定对象已不再能成功 `get`，或提交路径观察到 `dying` 时返回 `-MR_ENOENT`，重复挂链返回
`-MR_EBUSY`。`cancel` 只负责从当前链表摘除，不清空 `work->queue`；`MR_WORK_IS_RUNNING` 仅在“节点仍挂在立即/延时链表”期间为真。

**`mr_workqueue_hook_set`**（需 `MR_USE_WORKQUEUE_HOOK`）

```c
mr_err_t mr_workqueue_hook_set(mr_workqueue_t *queue,
                               const mr_workqueue_hook_t *hook);
```

`mr_workqueue_hook_t` 含 `suspend`、`wakeup` 两个回调与 `param`，成员回调可为 `MR_NULL`。`mr_workqueue_hook_set` 只保存传入
hook 指针，不复制结构体，因此 hook 存储必须覆盖 queue 使用期；设置过程也没有并发同步，建议在队列开始工作前配置。

**`*_get` / `*_put`**：跨模块持有 queue/work 指针时先 `get` 后 `put`；
`put` 返回 `MR_TRUE` 表示引用归零。一般应用代码不需要使用。

### 6. 完整示例

示例 1：延时工作——10 ms 后执行一次（改编自 `mrX/test/kernel/work.c:151`）：

```c
static void delayed_entry(struct mr_work *work, void *param) {
    (*(int *)param)++;
}

void delayed_demo(void) {
    static mr_workqueue_t queue;
    static mr_work_t work;
    int count = 0;

    mr_workqueue_init(&queue);
    mr_work_init(&work, delayed_entry, &count);
    mr_work_bind(&work, &queue);

    mr_work_delayed_schedule(&work, MR_TICK_MSEC(10));
    /* ...10 ticks later, in the main loop: */
    mr_workqueue_execute(&queue);           /* count == 1 after due */

    mr_workqueue_del(&queue);
}
```

示例 2：钩子衔接专用线程（需 `MR_USE_WORKQUEUE_HOOK`，改编自
`mrX/test/kernel/work.c:519`）：

```c
static void queue_wakeup_cb(struct mr_workqueue *queue, void *param) {
    sem_post((sem_t *)param);               /* Release the worker thread */
}

void threaded_setup(void) {
    static mr_workqueue_t queue;
    static sem_t sem;

    sem_init(&sem, 0, 0);
    mr_workqueue_init(&queue);
    static mr_workqueue_hook_t hook;

    hook.suspend = MR_NULL;
    hook.wakeup = queue_wakeup_cb;
    hook.param = &sem;
    mr_workqueue_hook_set(&queue, &hook);

    /* Worker thread: wait(sem); mr_workqueue_execute(&queue); in a loop */
}
```

示例 3：回调链式提交——一轮一件（改编自 `mrX/test/kernel/work.c:322`）：

```c
static mr_work_t step_a, step_b;
static mr_work_t *chain_next = &step_b;

static void chained_entry(struct mr_work *work, void *param) {
    /* Process one step here */
    if (chain_next != MR_NULL) {
        mr_work_schedule(chain_next);       /* Queued for the NEXT execute */
        chain_next = MR_NULL;
    }
}

void chain_demo(mr_workqueue_t *queue) {
    mr_work_init(&step_a, chained_entry, MR_NULL);
    mr_work_init(&step_b, chained_entry, MR_NULL);
    mr_work_bind(&step_a, queue);
    mr_work_bind(&step_b, queue);
    mr_work_schedule(&step_a);
    mr_workqueue_execute(queue);            /* runs step_a, queues step_b */
    mr_workqueue_execute(queue);            /* runs step_b */
}
```

### 7. 常见问题（FAQ）

**Q：调用了 `mr_work_schedule` 但回调没执行？**

`execute` 才是执行点：确认主循环或工作线程在周期性调用
`mr_workqueue_execute`。延时工作还需等待到期——到期由内嵌定时器在 tick 中 搬运，tick 源不动则延时工作永远不到期。

**Q：`mr_work_schedule` 返回 `-MR_ENOENT` 或 `-MR_EBUSY`？**

`-MR_ENOENT`：工作没有 `mr_work_bind` 到任何队列，或绑定的队列已被删除；
`-MR_EBUSY`：该工作已在队列中等待（立即或延时）。一个工作同一时刻只能有 一份待处理实例，先 `mr_work_cancel` 或等回调执行完再重新调度。

**Q：延时精度如何？最长能延时多久？**

粒度为 1 个 tick，实际执行最晚滞后约 1 个 tick 加一次 `execute` 的调度 延迟。延时上限为 `MR_TICK_MAX / 2 - 1` 个
tick（断言开启时超出直接失败）， 默认 1000 tick/s 下约 24.8 天。

**Q：`wakeup` 为什么只调用了一次？**

`wakeup` 只在 `queue_wakeup()` 看到 depth 旧值为 0 时触发；后续提交会继续增加 depth 但不重复 wakeup。execute 会把 depth
重置为 1，并在没有下一批立即工作时尝试 suspend。这个字段用于合并通知，不是严格的 queue
length；集成线程应按自身循环/信号量协议使用，而不要依赖它表示精确工作数。

**Q：回调执行中 `mr_work_cancel` 它自己有用吗？**

没有且安全：回调开始前工作已被摘出队列（此时 `MR_WORK_IS_RUNNING` 为假），
`cancel` 无法中止正在运行的回调；需要协作式中止请在回调内检查取消标志。

**Q：队列删除时，挂接在它上面的工作怎么办？**

当 `mr_workqueue_del` 最终导致 queue 引用归零时，release 会停用内嵌 timer 并摘除全部立即/延时 work；work 对象本身不会被销毁，但其
`work->queue` 指针 **不会自动清空**。若要复用，必须先显式 `mr_work_bind` 到仍然有效的新队列。若 queue 还有额外引用，`del`
只先置 dying 并归还一次引用，实际清理推迟到最后一个引用释放。

**Q：静态和动态方式怎么选？**

队列通常全局唯一，用静态对象 + `mr_workqueue_init`；工作数量运行期才确定 （如每连接一个超时工作）用 `mr_work_create` /
`mr_work_del`。

---

## 开发者指南

### 8. 架构总览

工作队列是内核服务层模块，构建在时钟与软定时器之上：

```
   +-------------------------------------------+
   | 生产者（ISR / 线程）                        |
   |  mr_work_schedule()        立即路径          |
   |    -> work 入 queue->list, depth++          |
   |    -> queue_wakeup() (hook.wakeup 一次)     |
   |  mr_work_delayed_schedule() 延时路径         |
   |    -> work 有序入 queue->tlist, 内嵌定时器装载 |
   +-------------------+-----------------------+
                       | 到期 tick
   +-------------------v-----------------------+
   | queue_timer_entry()（定时器回调）            |
   |  tlist 中到期工作搬运至 queue->list          |
   |  重装/停止定时器, queue_wakeup()             |
   +-------------------+-----------------------+
                       | mr_workqueue_execute()
   +-------------------v-----------------------+
   | 执行者上下文（主循环 / 专用线程）              |
   |  整批交换至 oplist, 解锁逐个执行回调           |
   |  排空后 queue_suspend() (hook.suspend 一次)  |
   +-------------------------------------------+
        依赖：时钟(tick)、软定时器(每队列一个内嵌定时器)、
              对象系统(类型/引用计数/双释放通道)、自旋锁、malloc(动态方式)
```

- 内嵌定时器在 `workqueue_init` 中以回调 `queue_timer_entry` 初始化 （`mrX/kernel/work.c:108`），仅存在延时工作时才启动。
- [中断 irq](irq.md) 的延迟下半部（`MR_USE_IRQ_DEFER`）以工作队列承载延迟 执行（`mrX/kernel/irq.c`）。

### 9. 数据结构

工作队列（`mrX/include/mrX/kernel/mr_work.h:40`）：

```c
typedef struct mr_workqueue {
    mr_object_t parent;
    mr_atomic_t dying;
    mr_timer_t timer;
    mr_list_t tlist;
    mr_list_t list;
    /* The following fields exist only with MR_USE_WORKQUEUE_HOOK */
    const mr_workqueue_hook_t *hook;
    mr_atomic_t depth;
} mr_workqueue_t;
```

| 字段     | 说明                                                                                                                               |
|----------|------------------------------------------------------------------------------------------------------------------------------------|
| `parent` | 对象基类，提供类型检查与引用计数                                                                                                   |
| `dying`  | 原子删除标记：置位后拒绝新提交（`-MR_ENOENT`）                                                                                     |
| `timer`  | 内嵌软定时器（每队列一个），驱动延时工作到期                                                                                       |
| `tlist`  | 延时工作有序链表（按到期 tick 升序，表头最早到期）                                                                                 |
| `list`   | 待处理（立即）工作链表                                                                                                             |
| `depth`  | Hook 通知抑制计数：`queue_wakeup` 加一且仅旧值 0 时 wakeup；execute 会重置为 1，随后视批次状态尝试减一/suspend；不是实际 work 数量 |
| `hook`   | 唤醒/挂起钩子（仅 `MR_USE_WORKQUEUE_HOOK` 时存在）                                                                                 |

工作（`mr_work.h:54`）：

```c
typedef struct mr_work {
    mr_object_t parent;
    mr_list_t list;
    mr_ptr_t queue;
    mr_tick_t tick;
    mr_ptr_t entry;
    mr_ptr_t param;
} mr_work_t;
```

| 字段              | 说明                                                                                     |
|-------------------|------------------------------------------------------------------------------------------|
| `list`            | 挂接 `queue->list` 或 `queue->tlist` 的节点；`MR_WORK_IS_RUNNING` 即判断该节点是否在链上 |
| `queue`           | 绑定的工作队列；`MR_NULL` 表示未绑定（`schedule` 返回 `-MR_ENOENT` 的依据）              |
| `tick`            | 延时到期绝对 tick，仅延时等待期有意义                                                    |
| `entry` / `param` | 回调与参数，初始化后只读                                                                 |

### 10. 核心流程

**全局结构**：单一自旋锁 `__work_lock`（`mrX/kernel/work.c:26`）保护所有 队列的 `list`/`tlist` 与工作的挂接状态；`dying`
（及启用钩子时的 `depth`） 用原子量无锁访问。

**立即提交**（`mr_workqueue_work`，`mrX/kernel/work.c:268`）：锁外检查 dying 与锁内检查重复排队后，头插 `queue->list`、记录
`work->queue`，解锁后调用
`queue_wakeup`：`depth` 原子加 1，从 0 变 1 时调用 `hook->wakeup`
（`work.c:28-44`）。

**延时提交**（`mr_workqueue_delayed_work`，`work.c:310`）：`tick` 清除标志位 后换算绝对到期点
`work->tick = mr_clock_tick() + tick`；用回绕安全比较
`(work->tick - w->tick) < MR_TICK_MAX / 2` 在 `tlist` 中找到首个不早于自己的 位置插入（与 [软定时器](timer.md) 的
`timer_insert` 同源算法）；若插到表头 则把内嵌定时器重装为 `work->tick - now | MR_TIMER_PERIODIC` 并启动 （
`work.c:364-371`）。

**到期搬运**（`queue_timer_entry`，`work.c:46`）：定时器回调中先
`mr_workqueue_get` 取引用（引用已死则自删定时器，`work.c:53-58`，这是引用 计数存在的理由），锁内扫描 `tlist` 表头：未到期（`(tick - work->tick) >=
MR_TICK_MAX / 2`）则记录剩余 tick 并停止扫描，到期者 `mr_list_move` 至
`queue->list`；随后把定时器重装到下一到期点或停表（`work.c:80-90`），解锁后
`queue_wakeup` 并归还引用。

**排空执行**（`mr_workqueue_execute`，`work.c:192`）：

1. 无锁快速路径：`list` 为空（且无 suspend 钩子）直接返回。
2. 锁内 `mr_list_replace` 把 `queue->list` 整体换入局部 `oplist`——此后 回调内的新提交落在全新的 `queue->list` 上，与遍历互不干扰。
3. `depth` 置 1（重置计数），锁外逐个执行回调（`work.c:241-248`）。
4. 排空后检查 `queue->list`：非空说明回调提交了新一批（不调用 `suspend`）， 否则 `queue_suspend`：`depth` 减 1，降到 0 时调用
   `hook->suspend`
   （`work.c:174-190`）。

**删除**：`mr_workqueue_del` 先置 `dying`，再执行一次 `mr_object_del`；`queue_del` 只在对象 release 阶段运行，因此若还有额外引用，timer
与链表清理会延后。`queue_del` 摘链时不会把各 work 的 `queue` 字段清空。`mr_work_del` 则会立即先 cancel
自己，再归还一次对象引用；动态对象仅在最后引用归零时 `mr_free`。

### 11. 并发与中断安全

- `__work_lock` 为关中断自旋锁，保护 `list`/`tlist` 与工作挂接字段；临界区 内只有链表搬移。
- 提交/取消与 execute 的链表交换由 `__work_lock` 协调；work 回调在锁外执行，因此回调内重新 schedule/cancel 不会递归持有该锁。
- `execute` 从批次链摘下 work 后不会为正在执行的 work 额外持有对象引用，queue 自身也不在 execute 入口自动
  get。调用方必须用更高层生命周期规则保证：不要让另一个上下文在回调即将/正在执行时 final-delete 动态 work，也不要在 execute
  尚未返回时 final-delete queue。
- 动态 create/final-release 涉及 malloc/free；hook 也会在触发它的调用者上下文同步执行。因此不能把所有 Work API 统一标为
  ISR-safe，ISR 可用性取决于具体调用路径、原子/锁实现和 hook。
- `dying` 是原子拒绝标志，但“检查 dying”和“把 work 入链”不是与删除组合成同一个锁内事务；并发删除/提交应由调用方的对象引用和更高层同步定义语义，不应把
  dying 理解成完整的删除线性化屏障。

### 12. 扩展与派生

work 通过对象 clazz 支持派生：在自定义结构体首成员放置 `mr_work_t`，用
`MR_WORK_DERIVE_INIT` 以自定义 clazz 初始化，即可携带附加成员并通过
`MR_CONTAINER_OF` 取回外层结构。参考用例：`mrX/test/kernel/work.c:59`
（`test_work_clazz` 继承 work 静态 clazz）。模块级扩展方向：基于
`MR_USE_WORKQUEUE_HOOK` 把队列接入任意执行机制（信号量 + 线程、邮箱、低功耗 唤醒）；[中断 irq](irq.md) 模块的
`MR_USE_IRQ_DEFER` 即以工作队列实现中断 延迟执行。

### 13. 源码导读

| 位置                                  | 职责                                      |
|---------------------------------------|-------------------------------------------|
| `mrX/include/mrX/kernel/mr_work.h:40` | 队列/工作结构、静态初始化宏与 API 声明    |
| `mrX/kernel/work.c:26`                | 全局自旋锁                                |
| `mrX/kernel/work.c:28`                | 深度计数与 wakeup/suspend 边沿通知        |
| `mrX/kernel/work.c:46`                | 内嵌定时器回调（到期搬运与重装）          |
| `mrX/kernel/work.c:104`               | 队列初始化（内嵌定时器装配）              |
| `mrX/kernel/work.c:192`               | 排空执行（批次交换、锁外回调、挂起判定）  |
| `mrX/kernel/work.c:268`               | 立即提交                                  |
| `mrX/kernel/work.c:310`               | 延时提交（有序插入 + 定时器重装）         |
| `mrX/kernel/work.c:382`               | 队列删除（dying + 定时器停用 + 全量摘除） |
| `mrX/kernel/work.c:496`               | `mr_work_schedule`（按绑定队列提交）      |
| `mrX/kernel/work.c:554`               | 工作取消                                  |
| `mrX/test/kernel/work.c`              | 全部用户可见行为的行为级测试              |

### 14. 附录：配置宏与错误码

**配置宏**

| 宏                              | 说明                                                       |
|---------------------------------|------------------------------------------------------------|
| `MR_USE_WORK`                   | 模块开关，默认 y，自动选中 `MR_USE_TIMER`（`Kconfig:160`） |
| `MR_USE_WORKQUEUE_HOOK`         | 队列唤醒/挂起钩子开关，默认 n（`Kconfig:171`）             |
| `MR_CFG_TICK_PER_SECOND`        | tick 频率（时钟模块），决定延时粒度                        |
| `MR_TICK_MSEC` / `MR_TICK_USEC` | 毫秒/微秒转 tick 的换算宏（时钟模块）                      |

**错误码**：本模块 API 失败返回负数错误码（见
`mrX/include/mrX/libc/mr_errno.h`）：

| 错误码       | 触发条件                                                                      |
|--------------|-------------------------------------------------------------------------------|
| `-MR_ENOENT` | 工作未绑定队列即调度；队列已删除（dying）后提交（`work.c:280`、`work.c:332`） |
| `-MR_EBUSY`  | 工作已入队（立即或延时等待）时再次提交（`work.c:288`、`work.c:340`）          |

`MR_NULL` 回调、延时超过 `MR_TICK_MAX / 2 - 1` 等参数问题在断言开启时由
`MR_ASSERT` 拦截；`mr_workqueue_create` / `mr_work_create` 内存不足返回
`MR_NULL`。
