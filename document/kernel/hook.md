# 钩子（Hook）

> 钩子提供一个容量固定、加锁追加的通用指针表；使用者可以把条目解释为函数指针形成回调链。时钟子系统用它保存 clock hook。

| 项       | 说明                                                         |
|----------|--------------------------------------------------------------|
| 功能宏   | 无（始终编译）                                               |
| 头文件   | `<mrX/kernel/mr_hook.h>`                                     |
| 依赖模块 | 自旋锁（`mr_spinlock`）；主要使用方为 [时钟 Clock](clock.md) |
| 参考测试 | `mrX/test/kernel/hook.c`                                     |

---

## 用户指南

### 1. 模块概述

钩子解决的问题是： **同一个执行点上可能挂多个回调，且回调数量由各模块运行期决定**。
`mr_hook_t` 用一块调用者提供的函数指针数组做容器，`mr_hook_add` 把新回调追加到表尾，
`MR_HOOK_EACH` 按注册顺序遍历执行。

特性：

- 零分配：数组由使用者提供，钩子本体不引用堆。
- 加锁追加：`mr_hook_add` 用一个全局 `mr_spinlock_lock_irqsave` 串行化所有 hook 实例的追加操作。
- 顺序保存：条目按成功 `add` 的先后写入数组；同一指针可以重复注册，没有去重。

不适用于：需要移除回调的场合（本模块只提供 `add`，无 remove）；回调数量不确定且 可能很大的场合（容量固定，超出返回
`-MR_ENOMEM`）。

**与时钟的关系**：本模块是通用容器，时钟内部用一个 `mr_hook_t` 保存 `mr_clock_hook_t`，`mr_clock_hook_add` 是对
`mr_hook_add` 的封装。clock 在每次 `mr_clock_increase()` 和每次 `mr_clock_tickless()`
计算时遍历该表；详见 [时钟](clock.md)。

### 2. 配置与依赖

本模块自身无 Kconfig 选项，始终编译。时钟钩子表的容量由时钟模块的配置决定：

| 配置项                   | 类型 | 默认值 | 说明                                                                                                        |
|--------------------------|------|--------|-------------------------------------------------------------------------------------------------------------|
| `MR_CFG_CLOCK_HOOK_SIZE` | 整型 | 2      | 时钟钩子表容量，范围 2~128（`mrX/include/mrX/kernel/Kconfig:26`）；由时钟模块经 `MR_HOOK_INIT` 传入钩子容器 |

包含头文件即可使用：

```c
#include <mrX/kernel/mr_hook.h>
```

### 3. 快速上手

建立一个回调链并依次执行：

```c
#include <mrX/kernel/mr_hook.h>

typedef void(my_hook_fn_t)(void);

static int sum;

static void add_one(void) { sum += 1; }
static void add_two(void) { sum += 2; }

void my_hook_example(void) {
    mr_ptr_t buf[4];                        /* 调用者提供的存储区 */
    my_hook_fn_t *entry;
    mr_uint32_t i;

    mr_hook_t hook = MR_HOOK_INIT(buf, 4);  /* 静态初始化 */

    mr_hook_add(&hook, (mr_ptr_t)add_one);
    mr_hook_add(&hook, (mr_ptr_t)add_two);

    MR_HOOK_EACH(i, entry, my_hook_fn_t, &hook) {
        entry();                            /* 循环结束后 sum == 3 */
    }
}
```

### 4. 使用说明

**生命周期**

- 定义 `mr_hook_t` 与一块 `mr_ptr_t` 数组（容量即数组长度），用 `MR_HOOK_INIT`
  静态初始化或运行时调用 `mr_hook_init`。
- 之后随时可 `mr_hook_add` 追加回调，直至容量用满。
- 没有删除/反初始化接口：容器生命周期与所在作用域一致，通常为全局或组件内静态 变量。

**行为约定**

| 操作                                  | 行为                                   |
|---------------------------------------|----------------------------------------|
| `mr_hook_add` 成功                    | 回调追加到表尾，`0` 返回               |
| 重复添加同一个 `entry`                | 允许，占用新的槽位并在遍历时再次出现   |
| 容量已满时 `mr_hook_add`              | 返回 `-MR_ENOMEM`，表内容不变          |
| `entry` 为 `MR_NULL` 时 `mr_hook_add` | 非法，断言开启时被 `MR_ASSERT` 拦截    |
| 容量为 0（`entry` 数组为 `MR_NULL`）  | 合法构造，所有 `add` 返回 `-MR_ENOMEM` |
| `MR_HOOK_EACH` 遍历                   | 按注册顺序访问 0 ~ 已注册数-1 的条目   |

**遍历期间的限制**：`MR_HOOK_EACH` 是无锁 `for` 宏，每轮条件都会读取当前 `hook->next`
。因此遍历过程中若有追加，新条目是否被当前遍历看到取决于追加发生的时机，而且遍历端与写端之间没有共同的读写锁契约。推荐在进入稳定分发阶段前完成注册。若条目被解释为回调，回调会由调用者按数组顺序同步执行。

### 5. API 参考

初始化：

| API            | 说明                 |
|----------------|----------------------|
| `MR_HOOK_INIT` | 静态初始化宏         |
| `mr_hook_init` | 运行时初始化钩子容器 |

注册与遍历：

| API            | 说明                       |
|----------------|----------------------------|
| `mr_hook_add`  | 追加一个回调条目           |
| `MR_HOOK_EACH` | 宏：按注册顺序遍历执行条目 |

**`mr_hook_init` / `MR_HOOK_INIT`**

```c
MR_INLINE void mr_hook_init(mr_hook_t *hook, mr_ptr_t *entry, mr_uint16_t size);
#define MR_HOOK_INIT(_entry, _size) {.entry = (_entry), .size = (_size), .next = 0}
```

- `hook`：钩子容器，由调用者分配存储。
- `entry`：`mr_ptr_t` 数组（函数指针存储区），容量为 0 时可传 `MR_NULL`。
- `size`：数组容量（可容纳的回调个数）。

**`mr_hook_add`**

```c
mr_err_t mr_hook_add(mr_hook_t *hook, mr_ptr_t entry);
```

- `hook`：已初始化的钩子容器。
- `entry`：回调函数指针（以 `mr_ptr_t` 传递，如 `(mr_ptr_t)my_func`）。
- 返回：成功 `0`；容量已满返回 `-MR_ENOMEM`；参数非法在断言开启时由 `MR_ASSERT` 拦截。

**`MR_HOOK_EACH`**

```c
#define MR_HOOK_EACH(_i, _entry, _type, _hook)                                 \
    for ((_i) = 0; ((_i) < (_hook)->next)                                      \
                   && (((_entry) = (_type *)(_hook)->entry[(_i)]), 1);         \
         (_i)++)
```

- `_i`：循环索引变量（无符号整型）。
- `_entry`：条目指针变量，每次迭代被赋值为按 `_type` 转型后的回调指针。
- `_type`：回调函数类型（typedef 的函数类型，如 `my_hook_fn_t`）。
- `_hook`：钩子容器指针。
- 用法为普通 `for` 循环体：在循环体内调用 `entry()`。

### 6. 完整示例

示例 1：带返回值聚合的回调链（回调返回剩余量，取最小正值——即时钟 tickless 的 聚合模式，改编自 `mrX/kernel/clock.c:34-53`
的用法）：

```c
typedef mr_tick_t(poll_fn_t)(void);

static mr_tick_t poll_a(void) { return 5; }
static mr_tick_t poll_b(void) { return 20; }

mr_tick_t poll_min(poll_fn_t **entries, mr_uint16_t count) {
    mr_hook_t hook = MR_HOOK_INIT(entries, count);
    poll_fn_t *entry;
    mr_tick_t less, min = MR_TICK_MAX;
    mr_uint32_t i;

    while (count--) {
        mr_hook_add(&hook, (mr_ptr_t)*entries++);
    }
    MR_HOOK_EACH(i, entry, poll_fn_t, &hook) {
        less = entry();
        if ((less > 0) && (less < min)) {
            min = less;
        }
    }
    return min;
}
```

示例 2：容量耗尽的处理（改编自 `mrX/test/kernel/hook.c:45-54`）：

```c
void full_example(void) {
    mr_ptr_t buf[2];
    mr_hook_t hook;

    mr_hook_init(&hook, buf, 2);
    if (mr_hook_add(&hook, (mr_ptr_t)full_example) != 0) {
        return;
    }
    /* 2 槽钩子第三次 add 返回 -MR_ENOMEM */
}
```

更多行为（零容量、遍历顺序、重复注册）见 `mrX/test/kernel/hook.c`。

### 7. 常见问题（FAQ）

**Q：想注册"每 tick 执行一次"的回调，该用哪个 API？**

使用 [时钟](clock.md) 的 `mr_clock_hook_add`。时钟钩子表容量由 `MR_CFG_CLOCK_HOOK_SIZE` 决定（默认 2）；具体启用了哪些内核模块、注册了多少
clock hook，决定剩余槽位。

**Q：`mr_hook_add` 返回 `-MR_ENOMEM` 怎么办？**

表已满。调大容器数组（自定义钩子）或调大对应配置项（时钟钩子为
`MR_CFG_CLOCK_HOOK_SIZE`）。追加失败不会破坏已有内容。

**Q：能移除已注册的回调吗？**

不能。本模块只提供追加。需要"注销"语义的场合请在回调内加使能标志，或重新设计 容器生命周期。

**Q：回调的执行顺序有保证吗？**

有，按 `mr_hook_add` 的注册顺序串行执行。

**Q：钩子容器能放栈上吗？**

仅当容器生命周期覆盖全部使用过程、注册与遍历都发生在同一作用域内时才可以。跨上下文使用 （如中断注册、线程遍历）应使用全局或静态存储。

---

## 开发者指南

### 8. 架构总览

钩子是内核基础设施层的小容器，无业务语义，向上支撑"一个事件点、多个观察者"的 分发模式。内核中的使用关系：

```
        +---------------------------------------+
        |  时钟 Clock（每 tick 事件源）           |
        |  __clock_hook : mr_hook_t              |
        |  __clock_entry[MR_CFG_CLOCK_HOOK_SIZE] |
        +-------------------+-------------------+
                            | mr_hook_add（注册）
        +-------------------v-------------------+
        |  钩子 Hook                              |
        |  固定容量函数指针表 + 自旋锁追加         |
        +-------------------+-------------------+
                            | MR_HOOK_EACH（分发）
        +-------------------v-------------------+
        |  各 tick 回调：软定时器到期检查等        |
        +---------------------------------------+
```

关键接线：时钟在 `mrX/kernel/clock.c:23-26` 以 `MR_HOOK_INIT(__clock_entry,
MR_CFG_CLOCK_HOOK_SIZE)` 构造钩子表；`mr_clock_hook_add`（`clock.c:85`）封装
`mr_hook_add`；每个 tick 的 `mr_clock_increase`（`clock.c:55`）末尾调用
`clock_hook()`（`clock.c:34`）以 `MR_HOOK_EACH` 分发全部回调并聚合最小正值作为 tickless。软定时器的到期检查（
`timer_timeout_check`）即通过该链每 tick 被驱动， 见 [定时器](timer.md) 与 [时钟](clock.md)。

### 9. 数据结构

```c
typedef struct mr_hook {                /* mrX/include/mrX/kernel/mr_hook.h:26 */
    mr_ptr_t *entry;                    /* 函数指针数组（归调用者所有） */
    mr_uint16_t size;                   /* 容量（槽位数） */
    mr_uint16_t next;                   /* 已注册条目数 */
} mr_hook_t;
```

| 字段    | 说明                                                                   |
|---------|------------------------------------------------------------------------|
| `entry` | 指向调用者提供的 `mr_ptr_t` 数组；钩子本体不做任何分配                 |
| `size`  | 容量上限；为 0 时允许 `entry` 为 `MR_NULL`（占位容器）                 |
| `next`  | 已注册数量，`mr_hook_add` 成功后自增；同时是 `MR_HOOK_EACH` 的遍历上界 |

条目以 `mr_ptr_t`（通用指针）存储，读取时由 `MR_HOOK_EACH` 按 `_type` 转型， 类型安全由使用者保证。

### 10. 核心流程

**追加**：`mr_hook_add`（`mrX/kernel/hook.c:16`）：

1. 断言检查：容器非空、（数组非空或容量为 0）、条目非空（`hook.c:21-23`）。
2. 以 `mr_spinlock_lock_irqsave` 获取全局自旋锁 `__hook_lock`（`hook.c:14`、
   `hook.c:26`）。
3. `next < size` 则写入 `entry[next++]`，返回 0；否则返回 `-MR_ENOMEM`（`hook.c:29-35`）。
4. 解锁（`hook.c:38`）。

**锁的实际范围**：实现只有一把全局 `__hook_lock`，所有 hook 实例的 `mr_hook_add` 都经过它。锁内只检查容量、写 `entry[next]`
并递增 `next`。代码没有为 `MR_HOOK_EACH` 提供配套锁，因此这把锁只串行化多个 add 调用，不构成 add 与遍历之间的完整互斥。

**遍历**：`MR_HOOK_EACH`（`mr_hook.h:75`）只是普通 `for` 宏：索引从 0 开始，每轮检查 `_i < hook->next`，再读取 `entry[_i]` 并按
`_type *` 转型。它本身既不加锁，也不调用条目；是否调用以及调用上下文完全由宏外的循环体决定。稳定用法是在初始化期完成追加、运行期只遍历。

### 11. 并发与中断安全

- `mr_hook_add` 使用 `mr_spinlock_lock_irqsave`；它能否在某个具体中断/多核场景安全使用，取决于平台 spinlock/irqsave
  实现，Hook 层不再额外声明“任意上下文”。
- `MR_HOOK_EACH` 无锁。若与 add 并发，`next` 和新槽位的可见性没有由 Hook 模块建立配套同步；推荐“初始化期 add、运行期只读”。
- 如果使用者在循环体中调用函数条目，函数就在遍历者当前上下文同步执行；Hook 模块不会切换上下文，也不会限制回调是否阻塞。

### 12. 扩展与派生

钩子模块不含对象 clazz，也没有派生宏：`mr_hook_t` 内嵌的是通用指针数组 `mr_ptr_t *entry`（
`mrX/include/mrX/kernel/mr_hook.h:26`，`mr_ptr_t` 即 `void *`），不注册对象类型、不提供 ops
表或注册回调，扩展语义全部集中在"条目如何被解释"：

- 新增条目类型 = 新增一个函数指针 typedef 并复用同一张表：宿主模块在自己的头文件里声明如
  `typedef mr_tick_t(mr_clock_hook_t)(void);`（`mrX/include/mrX/kernel/mr_clock.h:64`），分发处用
  `MR_HOOK_EACH(i, entry, mr_clock_hook_t, &__clock_hook)` 转型调用（`mrX/kernel/clock.c:36`、`:43`）。条目是纯函数指针，注册端与分发端只约定
  `_type`，模块本身不做类型校验。
- 条目也可为数据指针：注册接口接受任意 `mr_ptr_t` 原样保存（`mrX/kernel/hook.c:30`），`MR_HOOK_EACH` 再按 `_type` 取回，
  `mrX/test/kernel/hook_deep.c:184-189` 即以 `deep_hook_data_t *` 条目演示该用法。
- 新增钩子表实例 = `MR_HOOK_INIT` + 自备 `mr_ptr_t` 数组（容量即数组长度，`mr_hook.h:38`）；多个实例共享 `hook.c:14`
  的全局自旋锁，无需也不支持逐实例定制锁或 vtable。

### 13. 源码导读

| 位置                                  | 职责                                        |
|---------------------------------------|---------------------------------------------|
| `mrX/include/mrX/kernel/mr_hook.h:26` | `mr_hook_t` 结构                            |
| `mrX/include/mrX/kernel/mr_hook.h:38` | `MR_HOOK_INIT` 静态初始化宏                 |
| `mrX/include/mrX/kernel/mr_hook.h:75` | `MR_HOOK_EACH` 遍历宏                       |
| `mrX/kernel/hook.c:14`                | 全局自旋锁                                  |
| `mrX/kernel/hook.c:16`                | `mr_hook_add`：断言 + 锁内追加              |
| `mrX/kernel/clock.c:23`               | 时钟钩子表（`MR_CFG_CLOCK_HOOK_SIZE` 容量） |
| `mrX/kernel/clock.c:34`               | clock hook 遍历与 tickless 聚合             |
| `mrX/kernel/clock.c:85`               | `mr_clock_hook_add`（时钟侧封装）           |
| `mrX/test/kernel/hook.c`              | 初始化/追加/容量/遍历行为测试               |

### 14. 附录

**配置宏**

| 宏                       | 说明                                                                                                 |
|--------------------------|------------------------------------------------------------------------------------------------------|
| `MR_CFG_CLOCK_HOOK_SIZE` | 时钟钩子表容量（时钟模块配置，默认 2，范围 2~128），由 `mrX/kernel/clock.c:17-22` 提供缺省与下限校验 |

**错误码**

| 返回值       | 产生点                                                                                   |
|--------------|------------------------------------------------------------------------------------------|
| `-MR_ENOMEM` | 钩子表容量已满（`mrX/kernel/hook.c:34`）                                                 |
| `0`          | 追加成功                                                                                 |
| （断言）     | `hook` 为 `MR_NULL`、`entry` 条目为 `MR_NULL`、（数组为空且容量非 0）时 `MR_ASSERT` 拦截 |
