# 工作队列

Workqueue 模块用于把工作项提交到队列中，并在调用者显式执行队列时运行工作回调。模块支持立即工作和延迟工作：立即工作进入运行链表，延迟工作先进入按超时 tick 排序的定时链表，到期后由内部定时器移动到运行链表。

工作队列和工作项都以内嵌 `mr_object_t` 管理生命周期。队列内部使用自旋锁保护链表，使用原子计数记录待执行深度，并可在启用 `MR_USE_WORKQUEUE_HOOK` 时通过 hook 通知外部执行上下文唤醒或挂起。

## 核心数据结构和宏

### 工作队列控制块

```c
typedef struct mr_workqueue {
    mr_object_t parent;
    mr_atomic_t depth;
    mr_atomic_t dying;
    mr_timer_t timer;
    mr_list_t tlist;
    mr_list_t list;
#if defined(MR_USE_WORKQUEUE_HOOK)
    const mr_workqueue_hook_t *hook;
#endif
} mr_workqueue_t;
```

字段含义如下：

| 字段     | 含义 |
|----------|------|
| `parent` | 对象控制块。 |
| `depth`  | 队列待执行深度，用于控制 wakeup/suspend hook 调用。 |
| `dying`  | 删除标记；置位后拒绝新的工作提交。 |
| `timer`  | 内部定时器，用于把延迟工作移动到运行链表。 |
| `tlist`  | 延迟工作链表，按到期 tick 排序。 |
| `list`   | 待执行工作链表。 |
| `hook`   | 可选的唤醒/挂起回调。 |

### 工作项控制块

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

字段含义如下：

| 字段     | 含义 |
|----------|------|
| `parent` | 对象控制块。 |
| `list`   | 工作链表节点；非空表示工作正在队列或延迟链表中。 |
| `queue`  | 绑定或最近提交到的工作队列。 |
| `tick`   | 延迟工作到期的绝对 tick。 |
| `entry`  | 工作回调，类型为 `mr_work_entry_t *`。 |
| `param`  | 传给工作回调的参数。 |

```c
typedef void(mr_work_entry_t)(struct mr_work *work, void *param);
#define MR_WORK_IS_RUNNING(_work) (!mr_list_is_empty(&(_work)->list))
```

`MR_WORK_IS_RUNNING()` 判断工作项是否已在队列链表中。它同时覆盖立即待执行和延迟等待两种状态。

### Hook 类型

```c
typedef struct mr_workqueue_hook {
    void (*suspend)(struct mr_workqueue *queue, void *param);
    void (*wakeup)(struct mr_workqueue *queue, void *param);
    void *param;
} mr_workqueue_hook_t;
```

Hook 仅在 `MR_USE_WORKQUEUE_HOOK` 启用时存在。首次从空队列提交工作时调用 `wakeup`；执行队列并使深度回落时调用 `suspend`。

## 模块接口

### 工作队列初始化、创建和删除

```c
mr_err_t mr_workqueue_init(mr_workqueue_t *queue);
mr_workqueue_t *mr_workqueue_create(void);
mr_err_t mr_workqueue_del(mr_workqueue_t *queue);
```

`mr_workqueue_init()` 初始化静态队列，并初始化内部定时器。`mr_workqueue_create()` 从堆中创建动态队列。`mr_workqueue_del()` 将队列标记为 dying，释放对象引用；释放路径会删除内部定时器，并清空立即工作链表和延迟工作链表。

### 执行队列

```c
mr_err_t mr_workqueue_execute(mr_workqueue_t *queue);
```

`mr_workqueue_execute()` 将当前待执行链表替换到局部操作链表，逐个移除工作项并在释放内部锁后调用工作回调。执行结束后调用内部 suspend 逻辑。

该接口不会自动循环等待新工作。外部调度器通常在收到 hook wakeup 后调用它。

### 向指定队列提交工作

```c
mr_err_t mr_workqueue_work(mr_workqueue_t *queue, mr_work_t *work);
mr_err_t mr_workqueue_delayed_work(mr_workqueue_t *queue, mr_work_t *work,
                                   mr_tick_t tick);
```

`mr_workqueue_work()` 将工作加入队列运行链表，并唤醒队列。若队列正在删除，返回 `-MR_ENOENT`；若工作已经在某个链表中，返回 `-MR_EBUSY`。

`mr_workqueue_delayed_work()` 将工作加入延迟链表。`tick == 0` 时退化为立即提交。非零延迟要求 `tick < MR_TICK_MAX / 2`。如果新工作是最早到期项，内部定时器会被设置为最优 tick 并启动。

### 设置 Hook 和引用管理

```c
mr_err_t mr_workqueue_hook_set(mr_workqueue_t *queue,
                               const mr_workqueue_hook_t *hook);
mr_workqueue_t *mr_workqueue_get(mr_workqueue_t *queue);
mr_bool_t mr_workqueue_put(mr_workqueue_t *queue);
```

`mr_workqueue_hook_set()` 保存 hook 指针，要求 hook 非空。调用者需要保证 hook 对象在队列使用期间有效。

`mr_workqueue_get()` 和 `mr_workqueue_put()` 是工作队列引用计数封装。

### 工作项初始化、创建和删除

```c
mr_err_t mr_work_init(mr_work_t *work, mr_work_entry_t *entry, void *param);
mr_work_t *mr_work_create(mr_work_entry_t *entry, void *param);
mr_err_t mr_work_del(mr_work_t *work);
```

`mr_work_init()` 初始化静态工作项。`mr_work_create()` 创建动态工作项。两者都要求 `entry != MR_NULL`。`mr_work_del()` 先取消工作，再释放对象引用。

### 绑定、调度和取消工作

```c
mr_err_t mr_work_bind(mr_work_t *work, mr_workqueue_t *queue);
mr_err_t mr_work_schedule(mr_work_t *work);
mr_err_t mr_work_delayed_schedule(mr_work_t *work, mr_tick_t tick);
mr_err_t mr_work_cancel(mr_work_t *work);
mr_work_t *mr_work_get(mr_work_t *work);
mr_bool_t mr_work_put(mr_work_t *work);
```

`mr_work_bind()` 将工作项绑定到工作队列。`mr_work_schedule()` 和 `mr_work_delayed_schedule()` 使用 `work->queue` 指向的队列提交工作；如果工作尚未绑定或队列生命周期已经结束，返回 `-MR_ENOENT`。

`mr_work_cancel()` 从当前链表移除工作项。`mr_work_get()` 和 `mr_work_put()` 是工作项引用计数封装。

## 使用示例

```c
#include <mrX/kernel/mr_workqueue.h>

static void sample_work_entry(struct mr_work *work, void *param)
{
    (void)work;
    (void)param;
}

void sample_workqueue_usage(void)
{
    mr_workqueue_t queue;
    mr_work_t work;

    mr_workqueue_init(&queue);
    mr_work_init(&work, sample_work_entry, MR_NULL);
    mr_work_bind(&work, &queue);

    mr_work_schedule(&work);
    mr_workqueue_execute(&queue);

    mr_work_del(&work);
    mr_workqueue_del(&queue);
}
```

该示例创建一个静态工作队列和一个静态工作项。工作项绑定队列后提交，随后通过 `mr_workqueue_execute()` 执行回调。

## 注意事项

- Workqueue 模块只有在 `MR_USE_WORKQUEUE` 启用时可用，并依赖 Timer 模块处理延迟工作。
- `mr_work_schedule()` 和 `mr_work_delayed_schedule()` 需要工作项已通过 `mr_work_bind()` 或先前提交设置了有效队列。
- 同一个工作项在 `MR_WORK_IS_RUNNING()` 为真时不能再次提交。
- 删除队列会清空待执行和延迟链表，但不会删除工作项对象本身。
- 工作回调在内部锁释放后执行，可以再次提交工作，但需要避免对象删除和回调并发访问同一对象。
- `mr_workqueue_hook_set()` 不复制 hook 内容，hook 指针必须保持有效。
