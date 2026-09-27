# 软定时器（Timer）

> 软定时器基于系统时钟节拍（tick）提供回调式的延时与周期触发能力，用于在指定 tick
> 数后执行一次或周期性执行用户回调，无需为每个定时任务占用独立的硬件定时器或线程。

| 项       | 说明                                                   |
|----------|--------------------------------------------------------|
| 功能宏   | `MR_USE_TIMER`（默认开启，`MR_USE_WORK` 会自动选中它） |
| 头文件   | `<mrX/kernel/mr_timer.h>`                              |
| 依赖模块 | [时钟 Clock](clock.md)（提供 tick 与到期检查驱动）     |
| 参考测试 | `mrX/test/kernel/timer.c`                              |

---

## 用户指南

### 1. 模块概述

软定时器解决的问题是： **在未来的某个 tick 时刻执行一段代码，且不阻塞当前执行流**。

特性：

- 单次触发与周期触发两种模式，由周期参数中的一个标志位区分。
- 支持静态初始化（栈上/全局对象）与动态创建（堆分配）两种生命周期管理方式。
- 运行中的定时器按到期点有序排列；普通 tick 只需检查表头直到遇到第一个未到期项，但 `start`
  的有序插入最坏需遍历现有定时器，同一时刻到期的多个回调也会逐个处理。
- 回调中允许重新启动、停止自身，便于实现"自重启"与"条件停止"模式。
- 到期检查作为时钟钩子运行，返回距下次到期的剩余 tick 数，可供低功耗 tickless 调度使用。

不适用于：微秒级高精度定时（粒度为 1 个 tick，高精度需求请使用时钟模块的 busy-wait 或硬件定时器驱动）、需要在独立线程上下文中执行的延时任务（请使用
[工作队列 Work](work.md)）。

### 2. 配置与依赖

| 配置项                   | 类型 | 默认值 | 说明                                                     |
|--------------------------|------|--------|----------------------------------------------------------|
| `MR_USE_TIMER`           | bool | y      | 启用软定时器模块（`mrX/include/mrX/kernel/Kconfig:149`） |
| `MR_CFG_TICK_PER_SECOND` | 整型 | 1000   | 每秒 tick 数，属于时钟模块配置，决定定时的最小粒度       |

启用 `MR_USE_TIMER` 并重新生成 `mr_config.h` 后，包含头文件即可使用：

```c
#include <mrX/kernel/mr_timer.h>
```

该头文件会自动引入对象、时钟与链表等依赖头，无需手动包含。

时间换算：周期以 tick 为单位，毫秒换算使用时钟模块的 `MR_TICK_MSEC(ms)` 宏 （默认配置下 1 tick = 1 ms）。

### 3. 快速上手

一个每 500 ms 触发一次的周期定时器：

```c
#include <mrX/kernel/mr_timer.h>

static void led_timer_entry(mr_timer_t *timer, void *param) {
    /* Toggle LED here every 500 ms */
}

static int led_timer_setup(void) {
    static mr_timer_t led_timer;
    mr_err_t ret;

    ret = mr_timer_init(&led_timer, led_timer_entry, MR_NULL,
                        MR_TICK_MSEC(500) | MR_TIMER_PERIODIC);
    if (ret != 0) {
        return ret;
    }

    ret = mr_timer_start(&led_timer);
    if (ret != 0) {
        return ret;
    }
    return 0;
}
```

要点：

- 定时器对象 `led_timer` 声明为 `static`：定时器在 `start` 之后由内核异步引用， 不允许放在栈上逃出作用域。
- 周期值 `MR_TICK_MSEC(500) | MR_TIMER_PERIODIC`：按位或 `MR_TIMER_PERIODIC`
  表示周期模式，不带该标志则为单次模式。
- `mr_timer_init` 之后必须调用 `mr_timer_start`，定时器才开始计时。

### 4. 使用说明

**触发模式**

- 单次模式：周期参数不带 `MR_TIMER_PERIODIC`，到期触发一次回调后自动停止 （`MR_TIMER_IS_TIMING` 变为假）。
- 周期模式：周期参数按位或 `MR_TIMER_PERIODIC`。若回调没有移动/停止/删除该定时器，回调返回后会以 **当时的当前 tick**
  为基准重新计算下一次到期点；因此回调耗时或 tick 推进会影响相位，并非固定绝对时间轴追赶。

**生命周期**

- 静态方式：定义 `mr_timer_t` 对象，用 `MR_TIMER_INIT` 初始化或运行时调用
  `mr_timer_init`，用完调用 `mr_timer_del` 反初始化（不释放内存）。
- 动态方式：`mr_timer_create` 在堆上创建，用完调用 `mr_timer_del` 销毁并释放内存。

**运行期行为约定**

| 操作                                 | 行为                                            |
|--------------------------------------|-------------------------------------------------|
| 对运行中的定时器 `mr_timer_start`    | 重新装载：从当前时刻重新计时（不是"忽略"）      |
| 对未启动的定时器 `mr_timer_stop`     | 安全，返回 0                                    |
| 对运行中的定时器 `mr_timer_tick_set` | 修改周期并停止定时器，需重新 `start` 才继续计时 |
| 在回调中调用 `mr_timer_start`        | 支持，等价于单次模式的自重启                    |
| 在回调中调用 `mr_timer_stop`         | 支持，周期定时器不再重新装载                    |
| 周期参数（去掉标志位）为 0           | 非法，断言开启时被 `MR_ASSERT` 拦截             |

**回调上下文**：回调由时钟 hook 同步执行，因此处于调用 `mr_clock_increase()` 的同一上下文；目标板常见实现是在 tick ISR
中，宿主也可能在线程中推进时钟。为避免阻塞整个时钟 hook 链，回调应尽量短；若平台在 ISR 中推进
tick，则其中尤其不能执行阻塞操作。耗时工作可转交 [工作队列 Work](work.md)。

### 5. API 参考

初始化与销毁：

| API                    | 说明                                             |
|------------------------|--------------------------------------------------|
| `MR_TIMER_INIT`        | 静态初始化宏（编译期初始化静态定时器对象）       |
| `MR_TIMER_DERIVE_INIT` | 派生定时器静态初始化宏（见开发者篇第 12 节）     |
| `mr_timer_init`        | 运行时初始化一个静态定时器                       |
| `mr_timer_create`      | 在堆上动态创建一个定时器                         |
| `mr_timer_del`         | 删除定时器（静态对象反初始化，动态对象释放内存） |

控制：

| API                  | 说明                             |
|----------------------|----------------------------------|
| `mr_timer_start`     | 启动定时器；运行中调用则重新装载 |
| `mr_timer_stop`      | 停止定时器；未启动时安全         |
| `MR_TIMER_IS_TIMING` | 宏：查询定时器是否正在计时       |

参数与查询：

| API                             | 说明                                |
|---------------------------------|-------------------------------------|
| `mr_timer_tick_set`             | 设置定时周期                        |
| `mr_timer_tick`                 | 读取定时周期                        |
| `mr_timer_get` / `mr_timer_put` | 引用计数加一 / 减一（对接对象系统） |

**`mr_timer_init`**

```c
mr_err_t mr_timer_init(mr_timer_t *timer, mr_timer_entry_t *entry, void *param,
                       mr_tick_t tick);
```

- `timer`：定时器对象，由调用者分配存储。
- `entry`：回调函数，类型 `void (*)(mr_timer_t *timer, void *param)`，不可为 `MR_NULL`。
- `param`：透传给回调的用户参数，可为 `MR_NULL`。
- `tick`：定时周期，`tick | MR_TIMER_PERIODIC` 为周期模式；有效周期（去掉标志位） 必须大于 0。
- 返回：成功 `0`，失败负数错误码。

**`mr_timer_create`**

```c
mr_timer_t *mr_timer_create(mr_timer_entry_t *entry, void *param, mr_tick_t tick);
```

参数含义同 `mr_timer_init`。成功返回定时器指针，内存不足或初始化失败返回 `MR_NULL`。

**`mr_timer_del`**

```c
mr_err_t mr_timer_del(mr_timer_t *timer);
```

先把定时器从当前链表摘除，再对对象执行 `mr_object_del`。若这是最后一个引用，静态对象进入生命周期终点且可在适当时机重新
`init`；动态对象由 release 释放内存。若仍有额外引用，对象生命周期会继续，因此 `del` 不是强制立即销毁。回调内自删除由当前到期处理流程显式兼容。

**`mr_timer_start` / `mr_timer_stop`**

```c
mr_err_t mr_timer_start(mr_timer_t *timer);
mr_err_t mr_timer_stop(mr_timer_t *timer);
```

`start` 以当前时刻为基准重新计算到期点，因此对运行中的定时器调用等同于"重新计时"。
`stop` 仅将定时器摘出计时链表，不修改周期参数。

**`mr_timer_tick_set` / `mr_timer_tick`**

```c
mr_err_t mr_timer_tick_set(mr_timer_t *timer, mr_tick_t tick);
mr_tick_t mr_timer_tick(mr_timer_t *timer);
```

`tick_set` 修改周期；若定时器正在计时会被停止，需重新 `start`。`tick` 读取当前 周期参数（含 `MR_TIMER_PERIODIC` 标志位）。

**`MR_TIMER_IS_TIMING`**

```c
#define MR_TIMER_IS_TIMING(_timer)      (!mr_list_is_empty(&(_timer)->list))
```

正在计时返回 `MR_TRUE`，否则返回 `MR_FALSE`。

**`mr_timer_get` / `mr_timer_put`**

```c
mr_timer_t *mr_timer_get(mr_timer_t *timer);
mr_bool_t mr_timer_put(mr_timer_t *timer);
```

跨模块持有定时器指针时可用 `get`/`put` 管理额外引用；`mr_timer_del` 只是归还组件持有的一次对象引用，真正的生命周期终点仍由最后一次
`put` 决定。一般应用代码不需要直接使用这些接口。

### 6. 完整示例

示例 1：单次定时 + 回调内自重启（延迟重试场景，回调中携带计数参数）：

```c
static void retry_timer_entry(mr_timer_t *timer, void *param) {
    int *count = param;

    (*count)++;
    if (*count < 3) {
        /* Re-arm by itself: fire again one period later */
        mr_timer_start(timer);
    }
}

void retry_example(void) {
    static mr_timer_t retry_timer;
    int count = 0;

    mr_timer_init(&retry_timer, retry_timer_entry, &count, MR_TICK_MSEC(100));
    mr_timer_start(&retry_timer);
}
```

示例 2：动态创建周期定时器，并在回调内满足条件后自停止：

```c
static void poll_timer_entry(mr_timer_t *timer, void *param) {
    /* Poll hardware here */
    if (/* done */) {
        mr_timer_stop(timer);   /* Stop periodic re-arm from inside */
    }
}

void poll_example(void) {
    mr_timer_t *timer;

    timer = mr_timer_create(poll_timer_entry, MR_NULL,
                            MR_TICK_MSEC(10) | MR_TIMER_PERIODIC);
    if (timer == MR_NULL) {
        return;
    }
    mr_timer_start(timer);
    /* mr_timer_del(timer) when no longer needed */
}
```

更多行为参考（启动/停止/重装载/周期语义）见 `mrX/test/kernel/timer.c`。

### 7. 常见问题（FAQ）

**Q：定时器回调迟迟不执行？**

按顺序排查：确认 `MR_USE_TIMER` 已启用；确认平台 tick 中断已在调用
`mr_clock_increase`（定时器由时钟钩子驱动，tick 源不动则全部定时器静止）； 确认 `start` 已被调用且周期大于 0；确认没有被
`stop` 后遗忘重启。

**Q：定时有误差吗？**

定时判断只在时钟 hook 被执行时发生，因此分辨率受 tick 推进频率影响；若平台稳定地每 1 tick 调用一次 `mr_clock_increase(1)`
，量化误差通常在一个 tick 量级。同一次 hook 中到期的多个回调串行执行，前面回调的耗时会延后后面的回调。若一次
`mr_clock_increase()` 跨过多个 tick，逾期定时器会在该次调用中集中处理。

**Q：最长能定时多久？**

周期参数为 32 位，最高位是周期标志，有效周期上限为 2^31 - 1 个 tick；在默认 1000 tick/s 下约 24.8 天。超长定时可由回调级联（触发后再
`start`）实现。

**Q：为什么调用 `mr_timer_tick_set` 之后定时器停了？**

这是约定行为：修改周期会同时停止计时（避免旧装载值与新周期混用），需要再次
`mr_timer_start`。

**Q：静态和动态方式怎么选？**

数量固定、生命周期明确（如驱动内部的心跳定时器）用静态对象 + `MR_TIMER_INIT`； 数量运行期才确定、或需要被创建/销毁的（如每个连接一个超时定时器）用
`mr_timer_create` / `mr_timer_del`。资源紧张的场合优先静态，不依赖堆。

---

## 开发者指南

### 8. 架构总览

软定时器是内核服务层模块，构建在时钟模块之上，本身不产生时间：

```
        +---------------------+
        |  平台 tick 中断      |
        +----------+----------+
                   | mr_clock_increase()
        +----------v----------+
        |  时钟 Clock         |----> mr_clock_tick()      全局 tick 计数
        |  (mr_clock_hook_add)|----> 到期检查钩子链        每 tick 驱动
        +----------+----------+
                   | timer_timeout_check()（本模块注册）
        +----------v----------+
        |  软定时器 Timer      |----> 有序计时链表 + 回调分发
        +---------------------+
             依赖：对象系统（类型/引用计数）、双向链表（容器）、
                   自旋锁（并发保护）、malloc（仅动态方式）
```

关键接线：

- 内核初始化序列在 `mrX/kernel/initcall.c:59` 调用 `mr_kernel_timer_init()`
  （`mrX/kernel/timer.c:264`），将到期检查函数 `timer_timeout_check` 注册为时钟钩子。
- 钩子返回值 `tless`（距下次到期的剩余 tick 数）汇入 `mr_clock_tickless()`， 供低功耗场景在"下一个到期点之前"休眠，唤醒后补齐
  tick。

### 9. 数据结构

定时器结构（`mrX/include/mrX/kernel/mr_timer.h:34`）：

```c
typedef struct mr_timer {
    mr_object_t parent;     /* Object base: clazz + reference count */
    mr_tick_t   itick;      /* User period, bit31 = MR_TIMER_PERIODIC */
    mr_tick_t   ttick;      /* Absolute timeout tick, valid while timing */
    mr_list_t   list;       /* Node on the global timing list */
    mr_ptr_t    entry;      /* User callback */
    mr_ptr_t    param;      /* User callback parameter */
} mr_timer_t;
```

| 字段              | 说明                                                                                                             |
|-------------------|------------------------------------------------------------------------------------------------------------------|
| `parent`          | 对象基类，提供类型检查（`MR_OBJECT_IS_CLAZZES`）与引用计数                                                       |
| `itick`           | 用户设定值，生命周期内保持不变（`tick_set` 显式修改除外）；bit31 是 `MR_TIMER_PERIODIC` 标志，低 31 位是有效周期 |
| `ttick`           | 启动时由 `itick` 换算的绝对到期 tick（`now + period`），仅运行期有意义                                           |
| `list`            | 挂接全局计时链表的节点；`MR_TIMER_IS_TIMING` 即判断该节点是否在链上                                              |
| `entry` / `param` | 回调与参数，初始化后只读                                                                                         |

`MR_TIMER_PERIODIC` 定义为 `1U << 31`（`mr_timer.h:30`）：占用 `mr_tick_t`
（`mr_uint32_t`）的最高位，因此有效周期为 31 位——这也是"最长定时 2^31 - 1 tick"
这一用户约定的来源。

### 10. 核心流程

**全局结构**：所有运行中的定时器挂在一个按到期时间升序排列的循环双链表
`__timer_list` 上（`mrX/kernel/timer.c:20`，表头为最早到期的定时器），由自旋锁
`__timer_lock`（`timer.c:22`）保护。

**启动（有序插入）**：`mr_timer_start` 在锁内调用 `timer_insert`
（`mrX/kernel/timer.c:98`）：

1. 计算 `ttick = mr_clock_tick() + period`。
2. 从表头（最早到期）向后遍历，用回绕安全比较 `(timer->ttick - t->ttick) <
   MR_TICK_MAX / 2` 判断新定时器是否晚于 `t`：是则继续后移，否则插到 `t` 之前 （`mr_list_move`）。
3. 遍历到底仍未插入，说明它最晚到期，插到表尾。

**回绕安全比较**：`mr_tick_t` 会溢出回绕，直接比较绝对值在回绕边界处失效。 利用无符号减法的模运算性质，`a - b`
的差值在回绕下仍正确表示"a 比 b 晚多少"， 以 `MR_TICK_MAX / 2` 为分界即可稳定判定先后与"是否已到期"。到期判定
`(tick - timer->ttick) >= MR_TICK_MAX / 2`（`timer.c:221`）与之同源。 这也是用户篇"最长定时 2^31 - 1 tick"约束的实现依据。

**到期检查（时钟钩子）**：`timer_timeout_check`（`mrX/kernel/timer.c:202`）每 tick 被时钟调用：

1. 建立局部"操作链表" `oplist`。
2. 加锁检查表头：未到期（差值小于 `MR_TICK_MAX / 2`）则记录 `tless = ttick - tick`
   并退出；已到期则将该定时器移入 `oplist`。
3. **恢复进入函数前的中断状态并解锁后调用用户回调，再重新加锁**（`timer.c:235-241`）：Timer 锁本身不跨用户回调持有，这是回调内可重入
   Timer 控制 API 的关键。若调用者原本就在 ISR，回调仍处于 ISR 上下文。
4. 回调返回后检查 `oplist` 是否为空（`timer.c:244`）：为空说明回调内部已对本定时器 执行了 `start`/`stop`/`del`
   ，所有权已转移，本次流程放弃处理。
5. 否则按模式收尾：单次定时器仅从 `oplist` 摘除， **对象本身并未删除**；周期定时器调用 `timer_insert`，以此刻
   `mr_clock_tick()` + period 计算新的 `ttick`。
6. 循环直至表头未到期或链表为空，返回 `tless`。

该流程使同一 tick 内到期的任意多个定时器在一次钩子调用中被串行处理，且每一步 链表操作都在锁内、每一处用户回调都在锁外——临界区内没有任何不可预期的用户代码。

**停止与删除**：`mr_timer_stop` 调用 `timer_remove`（锁内 `mr_list_del`，自指空链也可删除）。`mr_timer_del` 先摘链再执行一次
`mr_object_del`；只有该次 put 把引用计数降到 0 时才进入 timer release。release 又会再次执行一次安全摘链；动态通道随后
`mr_free`（`mrX/kernel/timer.c:176-200`）。

**修改周期**：`mr_timer_tick_set` 在锁内摘链（等效停止）后更新 `itick`
（`mrX/kernel/timer.c:154`），因此"改周期即停止、需重新 start"是结构性结果， 不是附加逻辑。

### 11. 并发与中断安全

- `__timer_lock` 为关中断自旋锁（`mr_spinlock_lock_irqsave`），保护计时链表与
  `ttick` 的一致性；临界区内只做链表搬移与字段读写。
- 所有 API（`init`/`create` 除外）入口通过 `MR_ASSERT` 校验对象已初始化 （`MR_OBJECT_IS_INITED`）且类型为 timer（
  `MR_OBJECT_IS_CLAZZES`），传入错误 类型指针在断言开启时立即失败。
- 回调运行在 `mr_clock_increase` 调用者上下文。Timer 锁在调用用户回调前已经释放，因此回调内的 `start/stop/tick_set/del`
  路径被实现兼容；是否允许阻塞取决于该时钟推进上下文，但长时间回调都会阻塞后续时钟 hooks。
- `start/stop/tick_set` 的共享链表操作由 irqsave 自旋锁保护；但不能据此把所有 API 统一标成 ISR-safe：动态 `create`
  和动态对象最后一次 `del/put` 可能进入 `mr_malloc/mr_free`，其上下文约束由内存实现决定。

### 12. 扩展与派生

定时器通过对象 clazz 支持派生：在自定义结构体首成员放置 `mr_timer_t`（或直接以
`parent` 为首的兼容布局），用 `MR_TIMER_DERIVE_INIT` 以自定义 clazz 初始化，即可 携带附加成员并通过 `MR_CONTAINER_OF`
取回外层结构。参考用例：
`mrX/test/kernel/timer.c:19-53`（`test_timer_clazz` 继承 timer 静态 clazz）。

clazz 的静态/动态双释放通道（`MR_CLAZZES_DEFINE`，`mrX/kernel/timer.c:17`）决定了
`mr_timer_del` 的收尾方式：派生模块可注册自己的释放函数，在摘链之外完成资源归还。 内核组件（如 [工作队列 Work](work.md)
）内部即以派生定时器实现延时调度。

### 13. 源码导读

| 位置                                   | 职责                                           |
|----------------------------------------|------------------------------------------------|
| `mrX/include/mrX/kernel/mr_timer.h:34` | 结构体、周期标志、静态初始化宏与 API 声明      |
| `mrX/include/mrX/kernel/mr_timer.h:33` | timer clazz 导出（对象类型注册）               |
| `mrX/kernel/timer.c:20`                | 全局计时链表与自旋锁                           |
| `mrX/kernel/timer.c:98`                | 有序插入（启动/重装载的公共路径）              |
| `mrX/kernel/timer.c:202`               | 到期检查（时钟钩子，含所有权检查与周期重装载） |
| `mrX/kernel/timer.c:264`               | `mr_kernel_timer_init`：向时钟注册钩子         |
| `mrX/kernel/initcall.c:59`             | 内核初始化序列中的调用点                       |
| `mrX/test/kernel/timer.c`              | 全部用户可见行为的行为级测试                   |

### 14. 附录

**配置宏**

| 宏                              | 说明                                     |
|---------------------------------|------------------------------------------|
| `MR_USE_TIMER`                  | 模块开关，默认 y                         |
| `MR_TIMER_PERIODIC`             | 周期模式标志，`1U << 31`，与周期值按位或 |
| `MR_CFG_TICK_PER_SECOND`        | tick 频率（时钟模块），决定定时粒度      |
| `MR_TICK_MSEC` / `MR_TICK_USEC` | 毫秒/微秒转 tick 的换算宏（时钟模块）    |

**错误约定**：`mr_timer_init/start/stop/tick_set/del` 的正常公共路径当前均返回 0；`mr_timer_create` 可能因内存分配/初始化失败返回
`MR_NULL`。空指针、错误对象类型、有效周期为 0 等由 `MR_ASSERT` 约束，而不是转换成 `mr_errno` 返回值。
