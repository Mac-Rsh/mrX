# 等待队列（Wait）

> 等待队列提供"事件源（waitable）+ 等待者（waiter）"的回调式事件通知机制，
> 支持事件位匹配、电平/边沿两种触发方式，用于驱动与组件向关注者分发
> 可读/可写/错误等事件。

| 项       | 说明                                           |
|----------|------------------------------------------------|
| 功能宏   | `MR_USE_WAIT`（默认开启）                      |
| 头文件   | `<mrX/kernel/mr_wait.h>`                       |
| 依赖模块 | [对象 object](object.md)（对象基类与引用计数） |
| 参考测试 | `mrX/test/kernel/wait.c`                       |

---

## 用户指南

### 1. 模块概述

等待队列解决的问题是： **某个事件发生时，通知所有关心该事件的对象，且不阻塞 事件发起方**。特性：

- 事件源 `mr_waitable_t` 可挂接任意数量的等待者 `mr_waiter_t`。
- 事件为 32 位位图：高 4 位为类型与触发方式标志，低 28 位为自定义事件位。
- 每个等待者独立声明关注的事件位集合，支持电平（level）与边沿（edge）两种 触发语义。
- 通知通过用户回调完成，`raise` 调用方不被阻塞；等待是 **持续**的，触发后 等待者仍留在等待列表，可反复接收事件，直到 `cancel`
  或 `del`。
- 回调中允许取消自身/他人、挂接新等待者、递归 `raise`、删除事件源本身， 全部由实现保证安全。

与相邻模块的分工：阻塞式"等待并睡眠"语义不属于本模块，本模块是回调式分发。 内核内已有使用者：[ADC 驱动](../drivers/adc.md) 与
[DAC 驱动](../drivers/dac.md)（`mrX/drivers/adc/adc.c`、
`mrX/drivers/dac/dac.c`）以 waitable 向异步调用者通知完成/错误事件。

### 2. 配置与依赖

| 配置项        | 类型 | 默认值 | 说明                                                     |
|---------------|------|--------|----------------------------------------------------------|
| `MR_USE_WAIT` | bool | y      | 启用等待队列模块（`mrX/include/mrX/kernel/Kconfig:224`） |

```c
#include <mrX/kernel/mr_wait.h>
```

未启用 `MR_USE_WAIT` 时该头文件为空，所有类型与 API 均不声明。头文件自动 引入对象与链表等依赖头。

事件位约定（`mrX/include/mrX/kernel/mr_wait.h:29`）：

| 宏                   | 值           | 含义                     |
|----------------------|--------------|--------------------------|
| `MR_WAIT_IN`         | `1U << 28`   | 可读类事件               |
| `MR_WAIT_OUT`        | `1U << 29`   | 可写类事件               |
| `MR_WAIT_ERR`        | `1U << 30`   | 错误类事件               |
| `MR_WAIT_EDGE`       | `1U << 31`   | 等待者标志：按边沿触发   |
| `MR_WAIT_TYPE_MASK`  | IN\|OUT\|ERR | 事件类型掩码             |
| `MR_WAIT_EVENT_MASK` | `0x0fffffff` | 低 28 位自定义事件位掩码 |

### 3. 快速上手

一个事件源、一个等待者、一次触发（改编自 `mrX/test/kernel/wait.c:215`）：

```c
#include <mrX/kernel/mr_wait.h>

static int count;

static void on_event(mr_waiter_t *waiter, void *param,
                     mr_wait_event_t event, const void *data) {
    (*(int *)param)++;      /* event carries matched bits, data is payload */
}

void wait_demo(void) {
    static mr_waitable_t able;
    static mr_waiter_t waiter;
    static const int data = 0x12345678;

    mr_waitable_init(&able);
    mr_waiter_init(&waiter, on_event, &count, MR_WAIT_IN);
    mr_waiter_wait(&waiter, &able);          /* Attach to the waitable */

    mr_waitable_raise(&able, MR_WAIT_IN, &data);   /* on_event runs here */
    mr_waiter_del(&waiter);
    mr_waitable_del(&able);
}
```

要点：

- `waiter` 与 `able` 在等待期间被事件分发引用，须用静态存储或保证生命周期。
- `mr_waiter_wait` 之后才算挂接；`raise` 的事件位与等待者的关注位有交集时 回调被触发。

### 4. 使用说明

**事件模型**

- `mr_waitable_raise(able, event, data)` 的 `event` 是本次发生的事件位 （`MR_WAIT_EDGE` 位在分发时被忽略）；`data` 是附加载荷指针。
- 等待者的 `event` 参数声明关注位（`MR_WAIT_IN | MR_WAIT_OUT | ...`，可含 低 28 位自定义位），按位或 `MR_WAIT_EDGE`
  表示该等待者按边沿触发。
- 电平触发（默认）：`raise` 的事件与关注位有交集即触发。
- 边沿触发（`MR_WAIT_EDGE`）：只有本次事件相对 **上一次 raise 的事件**出现了 新的关注位才触发，连续相同的 raise 不重复触发；全
  0 的 raise 会把边沿基准 清零，使下一次 raise 的同一位重新成为"新边沿"
  （`mrX/test/kernel/wait.c:531` 覆盖该行为）。

**运行期行为约定**

| 操作                                    | 行为                                      |
|-----------------------------------------|-------------------------------------------|
| 已在等待的 waiter 再次 `mr_waiter_wait` | 返回 `-MR_EBUSY`，不重复挂接              |
| 触发后的 waiter                         | 仍在等待列表，可继续接收后续事件          |
| 对空闲 waiter `mr_waiter_cancel`        | 安全，返回 0                              |
| `mr_waiter_event_set`                   | 修改关注位并取消当前等待（需重新 `wait`） |
| `raise` 时无等待者或无人匹配            | 安全，返回 0，载荷被忽略                  |
| `mr_waitable_del`                       | 自动取消该事件源上的全部等待者            |
| `mr_waiter_del`                         | 自动从事件源摘除（挂接中删除安全）        |

**回调上下文**：回调运行在 `mr_waitable_raise` 调用者的上下文——驱动在 ISR 中上报事件时即为中断上下文，线程代码 raise
时即为线程上下文。回调内禁止 阻塞与耗时操作；回调内允许调用本模块全部 API（`cancel`/`wait`/`raise`/
`del` 等），分发过程对删除与递归有专门保护。

**载荷借用语义**：`data` 指针由所有被触发的回调共享，必须按只读对待，且仅在
`mr_waitable_raise` 返回前有效；需要更长生命周期时由回调自行复制。

### 5. API 参考

事件源（waitable）：

| API                                            | 说明                                |
|------------------------------------------------|-------------------------------------|
| `MR_WAITABLE_INIT` / `MR_WAITABLE_DERIVE_INIT` | 静态初始化宏（派生版见第 12 节）    |
| `mr_waitable_init` / `mr_waitable_create`      | 运行时初始化静态对象 / 堆上动态创建 |
| `mr_waitable_del`                              | 删除 waitable（自动取消全部等待者） |
| `mr_waitable_raise`                            | 在事件源上分发一次事件              |
| `mr_waitable_cancel_all`                       | 取消该事件源上的全部等待者          |
| `mr_waitable_get` / `mr_waitable_put`          | 引用计数加一 / 减一                 |

等待者（waiter）：

| API                                        | 说明                                 |
|--------------------------------------------|--------------------------------------|
| `MR_WAITER_INIT` / `MR_WAITER_DERIVE_INIT` | 静态初始化宏                         |
| `mr_waiter_init` / `mr_waiter_create`      | 初始化静态对象 / 堆上动态创建        |
| `mr_waiter_del`                            | 删除 waiter（自动摘除挂接）          |
| `mr_waiter_wait`                           | 挂接到某个 waitable                  |
| `mr_waiter_cancel`                         | 从 waitable 摘除                     |
| `mr_waiter_event_set` / `mr_waiter_event`  | 修改关注位（并取消等待）/ 读取关注位 |
| `MR_WAITER_IS_WAITING`                     | 宏：查询是否处于等待                 |
| `mr_waiter_get` / `mr_waiter_put`          | 引用计数加一 / 减一                  |

**`mr_waitable_init` / `mr_waitable_create` / `mr_waitable_del`**

```c
mr_err_t mr_waitable_init(mr_waitable_t *able);
mr_waitable_t *mr_waitable_create(void);
mr_err_t mr_waitable_del(mr_waitable_t *able);
```

`init` 初始化调用者分配的存储；`create` 堆分配（内存不足返回 `MR_NULL`）；
`del` 先取消全部等待者再删除对象，静态对象可重新 `init`，动态对象内存被 释放。

**`mr_waitable_raise`**

```c
mr_err_t mr_waitable_raise(mr_waitable_t *able, mr_wait_event_t event,
                           const void *data);
```

- `event`：本次事件位（`MR_WAIT_EDGE` 位被忽略）；`data`：载荷指针，所有被 触发的回调共享、只读、`raise` 返回后失效，可为
  `MR_NULL`。
- 返回：成功 `0`。同步执行全部匹配回调后返回。

**`mr_waiter_init` / `mr_waiter_create`**

```c
mr_err_t mr_waiter_init(mr_waiter_t *wait, mr_waiter_entry_t *entry,
                        void *param, mr_wait_event_t event);
mr_waiter_t *mr_waiter_create(mr_waiter_entry_t *entry, void *param,
                              mr_wait_event_t event);
```

- `entry`：回调，类型 `void (*)(mr_waiter_t *waiter, void *param,
  mr_wait_event_t event, const void *data)`，不可为 `MR_NULL`。
- `param`：透传给回调的用户参数；`event`：关注位，可按位或 `MR_WAIT_EDGE`。

**`mr_waiter_wait` / `mr_waiter_cancel`**

```c
mr_err_t mr_waiter_wait(mr_waiter_t *wait, mr_waitable_t *able);
mr_err_t mr_waiter_cancel(mr_waiter_t *wait);
```

`wait` 把等待者挂到 `able` 上，已在等待时返回 `-MR_EBUSY`，挂接会复位边沿 基准；`cancel` 从事件源摘除，对空闲等待者安全。

**`mr_waiter_event_set` / `mr_waiter_event` / `MR_WAITER_IS_WAITING`**

```c
mr_err_t mr_waiter_event_set(mr_waiter_t *wait, mr_wait_event_t event);
mr_wait_event_t mr_waiter_event(mr_waiter_t *wait);
#define MR_WAITER_IS_WAITING(_waiter)   (!mr_list_is_empty(&(_waiter)->list))
```

`event_set` 修改关注位，若正在等待会被取消，需重新 `wait`；`event` 返回当前 关注位（含 `MR_WAIT_EDGE` 标志）；
`MR_WAITER_IS_WAITING` 正在等待返回
`MR_TRUE`。

**`mr_waitable_cancel_all` 与引用管理**

```c
mr_err_t mr_waitable_cancel_all(mr_waitable_t *able);
```

摘除该事件源上的全部等待者，等待者对象本身不被销毁，可再次 `wait`。
`*_get` / `*_put` 用于跨模块持有指针时增减引用，`put` 返回 `MR_TRUE` 表示 引用归零、生命周期结束；一般应用代码不需要使用。

### 6. 完整示例

示例 1：一个事件源、多种关注位（改编自 `mrX/test/kernel/wait.c:242`）：

```c
static void on_wait_event(mr_waiter_t *waiter, void *param,
                          mr_wait_event_t event, const void *data) {
    (*(int *)param)++;
}

void multi_waiter_demo(void) {
    static mr_waitable_t able;
    static mr_waiter_t in_w, out_w, err_w;
    int count = 0;

    mr_waitable_init(&able);
    mr_waiter_init(&in_w, on_wait_event, &count, MR_WAIT_IN);
    mr_waiter_init(&out_w, on_wait_event, &count, MR_WAIT_OUT);
    mr_waiter_init(&err_w, on_wait_event, &count, MR_WAIT_ERR);
    mr_waiter_wait(&in_w, &able);
    mr_waiter_wait(&out_w, &able);
    mr_waiter_wait(&err_w, &able);

    mr_waitable_raise(&able, MR_WAIT_IN | MR_WAIT_OUT, MR_NULL);
    /* count == 2: err waiter not matched */

    mr_waiter_del(&in_w);
    mr_waiter_del(&out_w);
    mr_waiter_del(&err_w);
    mr_waitable_del(&able);
}
```

示例 2：边沿与电平触发对比（改编自 `mrX/test/kernel/wait.c:270`）：

```c
static mr_waitable_t able;
static int hits;

static void count_cb(mr_waiter_t *waiter, void *param,
                     mr_wait_event_t event, const void *data) {
    hits++;
}

void edge_level_demo(void) {
    static mr_waiter_t edge_w, level_w;

    mr_waitable_init(&able);
    mr_waiter_init(&edge_w, count_cb, MR_NULL, MR_WAIT_IN | MR_WAIT_EDGE);
    mr_waiter_init(&level_w, count_cb, MR_NULL, MR_WAIT_IN);
    mr_waiter_wait(&edge_w, &able);
    mr_waiter_wait(&level_w, &able);

    mr_waitable_raise(&able, MR_WAIT_IN, MR_NULL);  /* hits += 2 */
    mr_waitable_raise(&able, MR_WAIT_IN, MR_NULL);  /* hits += 1: level only */
    mr_waitable_raise(&able, 0, MR_NULL);           /* no hit, clears edge base */
    mr_waitable_raise(&able, MR_WAIT_IN, MR_NULL);  /* hits += 2: new edge */
}
```

示例 3：回调中取消其他等待者（改编自 `mrX/test/kernel/wait.c:351`）：

```c
static mr_waiter_t *target_w;

static void remove_cb(mr_waiter_t *waiter, void *param,
                      mr_wait_event_t event, const void *data) {
    mr_waiter_cancel(waiter);           /* Cancel self */
    mr_waiter_cancel(target_w);         /* Cancel another waiter */
}
/* After raise: canceled waiters leave the list, remaining ones still fire. */
```

### 7. 常见问题（FAQ）

**Q：等待者被触发一次后还需要重新 `wait` 吗？**

不需要。等待是持续订阅，触发后仍留在等待列表，后续匹配的 raise 会继续触发； 要停止接收请调用 `mr_waiter_cancel` 或
`mr_waiter_del`。

**Q：`raise` 传入的 `data` 在回调里能用多久？**

只在本次 `mr_waitable_raise` 返回前有效，且被多个回调共享，必须只读；回调 需要保留数据时应复制内容。

**Q：怎么定义自己的事件位？**

使用低 28 位（`MR_WAIT_EVENT_MASK` 范围内）自定义；高 4 位中 IN/OUT/ERR 是 约定的类型位，`MR_WAIT_EDGE` 仅用于等待者的触发方式声明。

**Q：为什么电平等待者对连续两次相同的 raise 都触发，而边沿等待者只触发一次？**

电平语义是"事件发生即通知"；边沿语义是"相对上一次事件有新变化才通知"。 注意全 0 的 raise 会重置边沿基准，此后同样的 raise
位会再次构成新边沿 （见第 4 节与测试 `mrX/test/kernel/wait.c:531`）。

**Q：`mr_waiter_wait` 返回 `-MR_EBUSY`？**

该等待者已经在某个事件源上等待。一个 waiter 同一时刻只能挂接一个 waitable； 先 `cancel` 或 `event_set`（会取消等待）后再重新
`wait`。

**Q：能在等待者回调里删除事件源吗？**

可以。`mr_waitable_del` 会先取消全部等待者再删除对象，分发过程对"回调中删除 自身事件源"有专门保护（
`mrX/test/kernel/wait.c:434` 验证）；但删除后不得再 使用该 waitable 指针。

**Q：静态和动态方式怎么选？**

数量固定、生命周期明确（如驱动内部的完成通知）用静态对象 + `MR_WAITER_INIT`； 运行期才确定数量或需要销毁的（如每个连接一个等待者）用
`*_create` /
`*_del`。

---

## 开发者指南

### 8. 架构总览

等待队列是内核服务层模块，构建在对象系统与双向链表之上：

```
   +------------------------------+
   | 事件发起方（驱动 ISR / 组件）  |
   |   mr_waitable_raise()        |
   +---------------+--------------+
                   | waitable_handle()
   +---------------v--------------+
   | 等待队列 Wait                 |----> able->list    等待者链表
   |                              |----> able->cursors 分发游标链表
   +------+-----------------------+
          | 逐个回调（锁外执行）
   +------v-----------------------+
   | mr_waiter_t 回调              |  可调本模块全部 API
   +------------------------------+
        依赖：对象系统(类型检查/引用计数/静态动态双释放通道)、
              双向链表(容器)、自旋锁(并发保护)、malloc(仅动态方式)
```

内核内使用者：[ADC 驱动](../drivers/adc.md)、[DAC 驱动](../drivers/dac.md)
以 waitable 为设备对象的事件出口。

### 9. 数据结构

waitable（`mrX/include/mrX/kernel/mr_wait.h:41`）：

```c
typedef struct mr_waitable {
    mr_object_t parent;
    mr_list_t cursors;
    mr_list_t list;
} mr_waitable_t;
```

| 字段      | 说明                                                              |
|-----------|-------------------------------------------------------------------|
| `parent`  | 对象基类，提供类型检查与引用计数                                  |
| `list`    | 等待者链表表头，挂接所有 `mr_waiter_wait` 上来的 waiter           |
| `cursors` | 分发游标链表表头，每个进行中的 `raise` 注册一个游标（见第 10 节） |

waiter（`mr_wait.h:49`）：

```c
typedef struct mr_waiter {
    mr_object_t parent;
    mr_wait_event_t event;
    mr_wait_event_t last;
    mr_waitable_t *able;
    mr_list_t list;
    mr_ptr_t entry;
    mr_ptr_t param;
} mr_waiter_t;
```

| 字段              | 说明                                                                    |
|-------------------|-------------------------------------------------------------------------|
| `event`           | 关注位（含 `MR_WAIT_EDGE` 标志）                                        |
| `last`            | 边沿基准：上一次收到的事件位，`wait` 时清零                             |
| `able`            | 当前挂接的事件源；`MR_NULL` 表示空闲                                    |
| `list`            | 挂接 `able->list` 的节点；`MR_WAITER_IS_WAITING` 即判断该节点是否在链上 |
| `entry` / `param` | 回调与参数，初始化后只读                                                |

内部游标结构（`mrX/kernel/wait.c:28`）：`typedef struct wait_cursor
{ mr_list_t list; mr_list_t *next; } wait_cursor_t;`。事件位编码见第 2 节 表格，`MR_WAIT_TYPE_MASK` 与
`MR_WAIT_EVENT_MASK`（`mr_wait.h:33-34`）供 实现区分类型位与自定义位。

### 10. 核心流程

**全局结构**：单一自旋锁 `__wait_lock`（`mrX/kernel/wait.c:25`）保护所有 waitable 的等待者链表、游标链表与 waiter 的挂接状态。

**挂接**（`mr_waiter_wait`，`mrX/kernel/wait.c:310`）：锁内检查未挂接（否则
`-MR_EBUSY`），记录 `wait->able`、清零 `last`，把 `wait->list` 头插到
`able->list`。

**分发**（`waitable_handle`，由 `mr_waitable_raise` 调用，`wait.c:124`）：

1. 清除 raise 事件中的 `MR_WAIT_EDGE` 位。
2. 锁内在 `able->cursors` 上注册一个栈上游标，`cursor.next` 指向等待者链表 的第一个节点。
3. 循环：取 `cursor.next` 指向的 waiter， **先把游标推进到下一个节点再触发 回调**——回调删除当前 waiter 时，
   `waiter_remove_locked` 能据此修复游标。
4. 触发判定：边沿等待者取 `event & ~waiter->last`，电平等待者取 `event`
   本身；随后 `waiter->last = event`。关注位与该值有交集才触发。
5. **解锁后调用回调，再重新加锁**（`wait.c:169-175`）：回调期间不持锁，这是 回调内可调用全部 wait API 的前提。
6. 循环结束摘除游标并解锁。递归 `raise` 只会注册新的游标，互不干扰 （`mrX/test/kernel/wait.c:393` 验证递归场景）。

**游标修复**（`waiter_remove_locked`，`wait.c:72`）：摘除 waiter 前，遍历
`able->cursors`，把所有 `next` 指向该 waiter 的游标改指其后的节点。该机制使
"回调中 `cancel`/`del` 任意等待者"不会破坏进行中的分发，是全部回调内操作 安全性的核心。

**删除**：`mr_waitable_del`（`wait.c:111`）先 `waitable_cancel_all`（锁内 循环摘除全部 waiter，`wait.c:94`
）再走对象删除通道；waitable/waiter 的静态/ 动态双释放通道（`MR_CLAZZES_DEFINE`，`wait.c:17`、`wait.c:22`）决定 `del`
是否 `mr_free`，动态对象引用计数归零路径同样先取消挂接再释放 （`wait.c:207-231`、`wait.c:371-395`）。

**修改关注位**：`mr_waiter_event_set`（`wait.c:350`）锁内先摘除再改 `event`，
"改位即取消等待"是结构性结果。

### 11. 并发与中断安全

- `__wait_lock` 为关中断自旋锁（`mr_spinlock_lock_irqsave`），保护等待者 链表、游标链表与 `able`/`last` 字段；临界区内只有链表搬移与字段读写。
- 全部 API 可在中断与线程上下文调用；`raise` 与 `wait`/`cancel` 并发安全。
- 回调运行在 `raise` 调用者上下文（目标板驱动常为中断）；回调内允许调用本 模块
  API（游标修复与"先推进后触发"保证），但不允许阻塞——阻塞会卡住
  `raise` 方与其他等待者的分发。
- 同一次 raise 内回调串行执行；递归 raise 时内层回调先于外层剩余部分执行， 语义上相当于函数调用嵌套。

### 12. 扩展与派生

waitable 与 waiter 均通过对象 clazz 支持派生：在自定义结构体首成员放置
`mr_waitable_t` / `mr_waiter_t`，用 `MR_WAITABLE_DERIVE_INIT` /
`MR_WAITER_DERIVE_INIT` 以自定义 clazz 初始化，即可携带附加成员并通过
`MR_CONTAINER_OF` 取回外层结构。参考用例：`mrX/test/kernel/wait.c:125`
（`test_waitable_clazz` 继承 waitable 静态 clazz）、`wait.c:132`（waiter
同理）。派生模块可注册自己的释放函数，在"取消挂接"之外完成资源归还。典型
派生方向：把设备对象（如 [ADC 驱动](../drivers/adc.md)）内嵌 waitable，向 异步调用者暴露事件订阅接口。

### 13. 源码导读

| 位置                                  | 职责                                   |
|---------------------------------------|----------------------------------------|
| `mrX/include/mrX/kernel/mr_wait.h:29` | 事件位编码与掩码                       |
| `mrX/include/mrX/kernel/mr_wait.h:41` | waitable/waiter 结构与静态初始化宏     |
| `mrX/kernel/wait.c:17`                | waitable/waiter 的静态与动态释放通道   |
| `mrX/kernel/wait.c:25`                | 全局自旋锁                             |
| `mrX/kernel/wait.c:28`                | 分发游标结构                           |
| `mrX/kernel/wait.c:72`                | 游标修复（回调内删除安全性的核心）     |
| `mrX/kernel/wait.c:94`                | 取消全部等待者（del 与释放的公共路径） |
| `mrX/kernel/wait.c:124`               | 事件分发主流程                         |
| `mrX/kernel/wait.c:310`               | 挂接（含 `-MR_EBUSY` 判定）            |
| `mrX/kernel/wait.c:350`               | 修改关注位（摘除 + 更新）              |
| `mrX/test/kernel/wait.c`              | 全部用户可见行为的行为级测试           |

### 14. 附录：配置宏与错误码

**配置宏**

| 宏                                           | 说明                                  |
|----------------------------------------------|---------------------------------------|
| `MR_USE_WAIT`                                | 模块开关，默认 y（`Kconfig:224`）     |
| `MR_WAIT_IN` / `MR_WAIT_OUT` / `MR_WAIT_ERR` | 约定事件类型位（bit28~30）            |
| `MR_WAIT_EDGE`                               | 等待者边沿触发标志（bit31）           |
| `MR_WAIT_TYPE_MASK` / `MR_WAIT_EVENT_MASK`   | 类型位掩码 / 自定义位掩码（低 28 位） |

**错误码**：本模块 API 失败返回负数错误码（见
`mrX/include/mrX/libc/mr_errno.h`）：`-MR_EBUSY`，仅由 `mr_waiter_wait`
在等待者已在等待时返回（`wait.c:325`）。传入错误类型指针、空回调等参数问题 在断言开启时由 `MR_ASSERT` 拦截，而非返回错误码；
`mr_waitable_create` / `mr_waiter_create` 内存不足返回 `MR_NULL`。
