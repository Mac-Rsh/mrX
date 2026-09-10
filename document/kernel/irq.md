# 中断管理（Irq）

> 中断管理基于中断号描述符表提供跨平台的中断注册、分发与控制能力：中断控制器驱动以
> 统一操作集接入，用户以中断号 + 回调 + 所有者的方式申请中断，框架负责触发类型、
> 优先级、使能嵌套、共享中断与可选的下半部（defer）延迟执行。

| 项       | 说明                                                                                 |
|----------|--------------------------------------------------------------------------------------|
| 功能宏   | `MR_USE_IRQ`（默认开启；`MR_USE_IRQ_DEFER` 默认开启并自动选中 `MR_USE_WORK`）        |
| 头文件   | `<mrX/kernel/mr_irq.h>`                                                              |
| 依赖模块 | [对象系统 Object](object.md)、[工作队列 Work](work.md)（启用 `MR_USE_IRQ_DEFER` 时） |
| 参考测试 | `mrX/test/kernel/irq.c`                                                              |

---

## 用户指南

### 1. 模块概述

中断管理解决的问题是： **把"某中断号发生时执行这段代码"这件事从具体中断控制器中抽象 出来**
，让驱动与应用面向中断号编程，控制器差异收敛到一组操作集回调中。

特性：

- 中断控制器抽象：控制器驱动注册一段连续中断号区间与 6 个硬件操作（使能/屏蔽、 类型/优先级设置），框架在其上维护描述符表。
- 多动作注册与共享：同一中断号可由多个所有者分别注册回调；置 `MR_IRQ_SHARED` 后 可共享。
- 使能嵌套：`enable`/`disable` 以深度计数实现，多层 disable 只在最外层操作硬件。
- 中断分发：用户在中断服务程序入口调用 `mr_irq_handle`，框架按序分发到已注册回调， 并以返回值区分"已处理 / 未处理 /
  转下半部"。
- 下半部（defer）：回调返回 `MR_IRQ_DEFERRED` 时，耗时处理被投递到内部工作队列， 在进程上下文执行（需 `MR_USE_IRQ_DEFER`）。
- 生命周期安全：控制器与动作均有引用/pending 跟踪，删除不会使分发流程悬空。

不适用于：直接操作具体外设的中断寄存器（属于控制器 `ops` 与 `priv` 的职责）、 需要比中断上下文更低延迟的零拷贝路径（defer
会引入一次上下文切换）。

### 2. 配置与依赖

| 配置项                  | 类型 | 默认值 | 说明                                                                                 |
|-------------------------|------|--------|--------------------------------------------------------------------------------------|
| `MR_USE_IRQ`            | bool | y      | 启用中断管理模块（`mrX/include/mrX/kernel/Kconfig:184`）                             |
| `MR_CFG_IRQ_TABLE_SIZE` | 整型 | 16     | 中断描述符表大小，即可管理的最大中断号 + 1，范围 2~1024（`Kconfig:191`）             |
| `MR_USE_IRQ_DEFER`      | bool | y      | 启用下半部延迟执行，自动选中 `MR_USE_WORK`（`Kconfig:201`）                          |
| `MR_USE_IRQ_DEFER_HOOK` | bool | n      | 启用下半部工作队列的挂起/唤醒钩子，自动选中 `MR_USE_WORKQUEUE_HOOK`（`Kconfig:210`） |

启用 `MR_USE_IRQ` 并重新生成 `mr_config.h` 后，包含头文件即可使用（启用
`MR_USE_IRQ_DEFER` 时会自动引入工作队列依赖）：

```c
#include <mrX/kernel/mr_irq.h>
```

### 3. 快速上手

以一个 GPIO 控制器接入中断框架并在中断中分发为例（改编自 `mrX/test/kernel/irq.c`）：

```c
#include <mrX/kernel/mr_irq.h>

/* Controller hardware operations (mock here, all six members are required) */
static const mr_irq_ops_t mock_ops = { /* fill with mock_* functions */ };

static mr_irq_return_t button_handler(mr_uint32_t irq, void *owner) {
    /* Handle button press here */
    return MR_IRQ_HANDLED;
}

static int irq_setup(void) {
    static mr_irq_t gpio_irq;
    mr_err_t ret;

    /* Register controller: irq line 0 of this controller */
    ret = mr_irq_init(&gpio_irq, 0, 0, &mock_ops, MR_NULL);
    if (ret != 0) {
        return ret;
    }

    /* Request line 0 with a rising-edge handler */
    ret = mr_irq_request(0, button_handler, &gpio_irq, MR_IRQ_EDGE_RISING);
    if (ret != 0) {
        return ret;
    }
    return 0;
}

/* In the controller's top-level ISR */
void gpio_isr(void) {
    mr_irq_handle(0);
}
```

要点：

- `mr_irq_init` 的中断号区间必须落在 `MR_CFG_IRQ_TABLE_SIZE` 之内，且区间不能与 已注册控制器重叠。
- `mr_irq_request` 的 `flags` 不能为 0，至少包含一种触发类型。
- `mr_irq_handle` 必须由用户在中断上下文中调用，框架本身不挂接向量表。

### 4. 使用说明

**控制器与中断号**

- 控制器以 `mr_irq_init`（静态）或 `mr_irq_create`（动态）注册一段连续中断号
  `[irq_start, irq_end]`，用完 `mr_irq_del` 删除。删除会自动释放其区间上所有动作 并关闭硬件。
- 中断号是全局编址的：描述符表大小为 `MR_CFG_IRQ_TABLE_SIZE`，所有控制器共享。

**申请与释放**

- `mr_irq_request(irq, entry, owner, flags)` 以 `owner`（任意非空指针，通常是拥有 该中断的对象）为唯一标识申请动作；同一中断号上同一
  `owner` 重复申请返回
  `-MR_EEXIST`。
- `mr_irq_free(irq, owner)` 按所有者释放动作；释放时回调正在执行或已挂下半部的， 框架延迟到安全点再释放，调用方无需关心。
- 第一个申请者决定触发类型与默认优先级（0）；后续申请者必须携带完全相同的 `flags`。

**共享中断**：首个申请者携带 `MR_IRQ_SHARED` 后，其他所有者可继续申请；分发时按 注册顺序依次调用，直到某回调返回
`MR_IRQ_HANDLED` 为止。未置共享位的中断号再被 申请返回 `-MR_EBUSY`。

**使能嵌套**：`mr_irq_enable` / `mr_irq_disable` 按深度计数；申请动作隐含一次 enable，释放最后一个动作隐含一次 disable。重复
enable/disable 在已使能/已关闭 状态下是安全的，不会重复操作硬件。

**回调约定**

| 事项                                                      | 约定                                                                                                    |
|-----------------------------------------------------------|---------------------------------------------------------------------------------------------------------|
| 回调上下文                                                | `mr_irq_handle` 的调用者上下文，即中断上下文（或宿主模拟中调用它的线程）                                |
| 返回值                                                    | `MR_IRQ_HANDLED` 已处理（停止分发）；`MR_IRQ_NONE` 未处理（继续下一个动作）；`MR_IRQ_DEFERRED` 转下半部 |
| 回调内调用 Irq API / 释放自己                             | 均支持，动作在本轮回调返回后释放                                                                        |
| 返回 `MR_IRQ_DEFERRED` 但未用 `mr_irq_defer_request` 申请 | 按已处理对待（需 `MR_USE_IRQ_DEFER`）                                                                   |

**下半部（defer）**

- 用 `mr_irq_defer_request` 申请动作并额外提供 `defer_entry`：顶半部回调返回
  `MR_IRQ_DEFERRED` 后，`defer_entry` 被投递到框架内部工作队列，在进程上下文由
  `mr_irq_defer_execute` 驱动执行。
- 宿主测试中直接轮询调用 `mr_irq_defer_execute`；目标系统上推荐配合
  `mr_irq_defer_hook_set` 用信号量唤醒专用线程（见头文件注释）。
- 若在下半部执行前动作已被 `free` 或控制器被 `del`，下半部自动作废，不再执行。

### 5. API 参考

控制器生命周期：

| API                         | 说明                                                       |
|-----------------------------|------------------------------------------------------------|
| `mr_irq_init`               | 初始化一个静态中断控制器，接管 `[irq_start, irq_end]` 区间 |
| `mr_irq_create`             | 在堆上动态创建中断控制器                                   |
| `mr_irq_del`                | 删除控制器（静态对象反初始化，动态对象释放内存）           |
| `mr_irq_get` / `mr_irq_put` | 控制器引用计数加一 / 减一（对接对象系统）                  |

中断动作：

| API                    | 说明                                            |
|------------------------|-------------------------------------------------|
| `mr_irq_request`       | 申请中断动作                                    |
| `mr_irq_defer_request` | 申请带下半部的中断动作（需 `MR_USE_IRQ_DEFER`） |
| `mr_irq_free`          | 按所有者释放中断动作                            |

控制与查询：

| API                                | 说明                               |
|------------------------------------|------------------------------------|
| `mr_irq_enable` / `mr_irq_disable` | 使能 / 关闭中断（深度嵌套）        |
| `mr_irq_type`                      | 读取中断触发类型                   |
| `mr_irq_priority_set`              | 设置中断优先级（0 最高，255 最低） |
| `mr_irq_priority`                  | 读取中断优先级                     |

分发与下半部：

| API                     | 说明                                                            |
|-------------------------|-----------------------------------------------------------------|
| `mr_irq_handle`         | 分发一次中断，返回处理结果                                      |
| `mr_irq_defer_execute`  | 在进程上下文执行已投递的下半部（需 `MR_USE_IRQ_DEFER`）         |
| `mr_irq_defer_hook_set` | 设置下半部工作队列的挂起/唤醒钩子（需 `MR_USE_IRQ_DEFER_HOOK`） |

**`mr_irq_init` / `mr_irq_create` / `mr_irq_del`**

```c
mr_err_t mr_irq_init(mr_irq_t *irq, mr_uint32_t irq_start, mr_uint32_t irq_end,
                     const mr_irq_ops_t *ops, void *priv);
mr_irq_t *mr_irq_create(mr_uint32_t irq_start, mr_uint32_t irq_end,
                        const mr_irq_ops_t *ops, void *priv);
mr_err_t mr_irq_del(mr_irq_t *irq);
```

- `irq_start` / `irq_end`：接管的连续中断号区间，均须小于 `MR_CFG_IRQ_TABLE_SIZE`
  且 `irq_start <= irq_end`；区间冲突返回 `-MR_EEXIST`。
- `ops`：控制器操作集（见下文），不可为 `MR_NULL`；`priv` 为透传给操作回调的 私有数据，可为 `MR_NULL`。
- 返回：`mr_irq_init` 成功 `0`；`mr_irq_create` 成功返回控制器指针，失败返回
  `MR_NULL`。
- `mr_irq_del`：标记控制器为 dying 并删除对象，区间上全部动作被移除、每条中断线 被 disable +
  mask；静态对象可复用，动态对象内存释放。分发中删除是安全的 （分发持有引用计数）。

**`mr_irq_request` / `mr_irq_defer_request`**

```c
mr_err_t mr_irq_request(mr_uint32_t irq, mr_irq_entry_t *entry, void *owner,
                        mr_uint16_t flags);
mr_err_t mr_irq_defer_request(mr_uint32_t irq, mr_irq_entry_t *entry,
                              void *owner, mr_uint16_t flags,
                              mr_irq_entry_t *defer_entry);
```

- `entry`：顶半部回调，类型 `mr_irq_return_t (mr_uint32_t irq, void *owner)`。
- `owner`：所有者标识，不可为 `MR_NULL`；同一中断号上必须唯一。
- `flags`：五种触发类型之一按位或可选的 `MR_IRQ_SHARED`，不可为 0。
- `defer_entry`：下半部回调，签名同 `entry`，仅 `mr_irq_defer_request` 需要。
- 返回：成功 `0`；`-MR_ENOENT`（无控制器或控制器删除中）、`-MR_EBUSY`（非共享 中断号被二次申请）、`-MR_EINVAL`（flags 与已注册不一致）、
  `-MR_EEXIST`
  （所有者重复）、`-MR_ENOMEM`（内存不足）。

**`mr_irq_free`**

```c
mr_err_t mr_irq_free(mr_uint32_t irq, void *owner);
```

按所有者删除动作；动作正在执行或已有下半部排队时延迟释放。成功 `0`，无匹配动作 或控制器不存在返回 `-MR_ENOENT`。

**`mr_irq_enable` / `mr_irq_disable` / `mr_irq_type` / `mr_irq_priority_set` / `mr_irq_priority`**

```c
mr_err_t mr_irq_enable(mr_uint32_t irq);
mr_err_t mr_irq_disable(mr_uint32_t irq);
int mr_irq_type(mr_uint32_t irq);
mr_err_t mr_irq_priority_set(mr_uint32_t irq, mr_uint8_t priority);
int mr_irq_priority(mr_uint32_t irq);
```

`enable`/`disable` 以深度计数控制：首个 enable 硬件 unmask + enable，首个 disable 硬件 disable + mask，重复调用只调整计数。
`type` 返回申请时的类型位，`priority`
返回当前优先级，`priority_set` 同步调用 `ops->priority_set`。控制器不存在时 返回 `-MR_ENOENT`（查询 API 返回负数错误码）。

**`mr_irq_handle`**

```c
mr_irq_return_t mr_irq_handle(mr_uint32_t irq);
```

分发一次中断：先 mask 硬件，按注册顺序调用各动作回调；某回调返回
`MR_IRQ_HANDLED` 即停止，全部返回 `MR_IRQ_NONE` 则结果为未处理。分发结束时若中断 仍处于使能状态则重新 unmask。中断号无控制器返回
`MR_IRQ_NONE`。

**`mr_irq_defer_hook_set`**（需 `MR_USE_IRQ_DEFER_HOOK`）

```c
mr_err_t mr_irq_defer_hook_set(const mr_irq_defer_hook_t *hook);
```

`hook` 含 `suspend` / `wakeup` 两个回调与 `param`：下半部工作队列从空闲转为有工作时 调用 `wakeup`，回到空闲时调用
`suspend`。头文件注释建议用"初始值为 0 的信号量"充当 钩子，以唤醒专职执行 `mr_irq_defer_execute` 的线程。

**控制器操作集 `mr_irq_ops_t`**

| 成员                                       | 职责                            |
|--------------------------------------------|---------------------------------|
| `type_set(irq, type, priv)`                | 设置触发类型                    |
| `priority_set(irq, priority, priv)`        | 设置优先级                      |
| `enable(irq, priv)` / `disable(irq, priv)` | 使能 / 关闭该中断号（控制器级） |
| `unmask(irq, priv)` / `mask(irq, priv)`    | 解除 / 施加该中断号的屏蔽       |

由控制器驱动实现全部六个成员；框架在自旋锁内（关中断）调用它们（见开发者篇）。

### 6. 完整示例

示例 1：共享中断（改编自 `mrX/test/kernel/irq.c` 的 `shared` 用例）：

```c
static mr_irq_return_t handler_a(mr_uint32_t irq, void *owner) {
    if (/* not mine */) {
        return MR_IRQ_NONE;         /* Let next handler run */
    }
    return MR_IRQ_HANDLED;
}

static mr_irq_return_t handler_b(mr_uint32_t irq, void *owner) {
    return MR_IRQ_HANDLED;
}

void shared_example(void) {
    static mr_irq_t irq;

    mr_irq_init(&irq, 3, 3, &mock_ops, MR_NULL);
    /* First requester decides the flags, must carry MR_IRQ_SHARED */
    mr_irq_request(3, handler_a, handler_a, MR_IRQ_SHARED | MR_IRQ_EDGE_RISING);
    mr_irq_request(3, handler_b, handler_b, MR_IRQ_SHARED | MR_IRQ_EDGE_RISING);

    mr_irq_handle(3);               /* Dispatches A then B */

    mr_irq_free(3, handler_a);
    mr_irq_free(3, handler_b);
    mr_irq_del(&irq);
}
```

示例 2：顶半部 + 下半部（需 `MR_USE_IRQ_DEFER`，改编自测试 `defer_request` 用例）：

```c
static volatile int work_done = 0;

static mr_irq_return_t top_half(mr_uint32_t irq, void *owner) {
    /* Ack hardware, grab data, keep it short */
    return MR_IRQ_DEFERRED;         /* Hand over to bottom half */
}

static mr_irq_return_t bottom_half(mr_uint32_t irq, void *owner) {
    work_done++;                    /* Lengthy processing runs here */
    return MR_IRQ_HANDLED;
}

void defer_example(void) {
    static mr_irq_t irq;

    mr_irq_init(&irq, 4, 4, &mock_ops, MR_NULL);
    mr_irq_defer_request(4, top_half, &irq, MR_IRQ_EDGE_RISING, bottom_half);
    mr_irq_handle(4);               /* In ISR: runs top_half only */
    mr_irq_defer_execute();         /* In thread: runs bottom_half */
    mr_irq_free(4, &irq);
    mr_irq_del(&irq);
}
```

更多行为参考（共享冲突、pending 释放、深度嵌套边界）见 `mrX/test/kernel/irq.c`。

### 7. 常见问题（FAQ）

**Q：`mr_irq_request` 返回 `-MR_ENOENT`？**

该中断号上没有已注册的控制器，或控制器正在被删除。先 `mr_irq_init`/`mr_irq_create`
覆盖该中断号，再申请。

**Q：第二个申请者返回 `-MR_EBUSY` 或 `-MR_EINVAL`？**

`-MR_EBUSY`：中断号已被占用且首个申请者未携带 `MR_IRQ_SHARED`。`-MR_EINVAL`： 中断号是共享的，但你的 `flags`
与首个申请者不完全一致（共享要求所有申请者的 flags 逐位相同，包括类型位）。

**Q：连续调用两次 `mr_irq_disable`，需要同样调用两次 `mr_irq_enable` 吗？**

深度计数保证配对；多余的 disable/enable 在"已关闭/已使能"状态下被钳位，不会 叠加到硬件层。注意申请/释放动作各自隐含一次
enable/disable，勿与之手动重复配对。

**Q：回调返回 `MR_IRQ_DEFERRED` 但下半部没执行？**

确认 `MR_USE_IRQ_DEFER` 已启用；确认动作是用 `mr_irq_defer_request`（而非
`mr_irq_request`）申请的，否则框架按已处理对待；确认进程上下文中有人调用
`mr_irq_defer_execute` 驱动执行。

**Q：在中断回调里 `mr_irq_free` 自己安全吗？回调内能调 Irq API 吗？**

都安全。框架为每个动作维护 pending 计数：回调执行期间或下半部排队期间只解除 绑定，待计数归零后才真正释放内存；回调在锁外执行，任何
Irq API 均可调用。

**Q：`mr_irq_handle` 会自动清中断吗？**

不会。mask/unmask 由框架在分发前后调用，但中断 pending 清除属于控制器职责，应在 顶半部回调或 `ops` 中完成。

---

## 开发者指南

### 8. 架构总览

中断管理位于内核服务层，向下对接具体中断控制器驱动，向上为驱动与应用提供统一 中断号语义：

```
        +---------------------------+
        |  中断向量表 / 顶层 ISR     |  （用户或平台代码）
        +------------+--------------+
                     | mr_irq_handle(irq)
        +------------v--------------+
        |  中断管理 Irq              |----> 描述符表 + 动作链 + 回调分发
        |  (mrX/kernel/irq.c)       |----> [MR_USE_IRQ_DEFER] 内部工作队列（下半部）
        +------------+--------------+
                     | ops->enable/mask/type_set/priority_set ...
        +------------v--------------+
        |  中断控制器驱动             |  （mr_irq_init/create 注册）
        +---------------------------+
             依赖：对象系统（引用计数/clazz）、自旋锁、原子操作、
                   malloc（动作与动态控制器分配）、Work（仅 defer）
```

关键接线：内核初始化序列在 `mrX/kernel/initcall.c:64` 调用 `mr_kernel_irq_init()`
（`mrX/kernel/irq.c:884`），仅完成内部 defer 工作队列的初始化与钩子挂接；描述符表 为静态数组，无需初始化。控制器驱动在运行期通过
`mr_irq_init`/`mr_irq_create`
自行接入。

### 9. 数据结构

控制器对象（`mrX/include/mrX/kernel/mr_irq.h:67`）：`parent`（对象基类，提供类型 检查与引用计数，`mr_irq_handle` 借此防止分发中删除）、
`dying`（原子删除标记，所有 入口在锁内检查）、`istart`/`iend`（接管的中断号区间）、`ops`/`priv`（控制器操作集 与私有数据，初始化后只读）。

中断动作（`mr_irq.h:77`）：

| 字段                    | 说明                                                             |
|-------------------------|------------------------------------------------------------------|
| `next`                  | 单链表节点，挂在描述符的 `action` 链上                           |
| `pending`               | 正在执行 / 已投递下半部的引用计数，延迟释放的依据                |
| `irq`                   | 申请时的中断号（去掉最高位，见 `irq_action_alloc`，`irq.c:302`） |
| `entry`                 | 顶半部回调                                                       |
| `owner`                 | 所有者标识；`MR_NULL` 表示"已释放待回收"                         |
| `defer` / `defer_entry` | 内嵌工作对象与下半部回调（仅 `MR_USE_IRQ_DEFER`）                |

中断号描述符（`mr_irq.h:90`）：

| 字段                 | 说明                                                         |
|----------------------|--------------------------------------------------------------|
| `action`             | 动作单链表头                                                 |
| `priority` / `flags` | 首个动作设置的优先级与 flags（类型位占低 4 位，共享位 0x10） |
| `depth`              | 使能深度，原子访问；1 = 关闭，0 = 使能                       |
| `irq`                | 占用该中断号的控制器指针，`MR_NULL` 表示空闲                 |

flags 编码：低 4 位为触发类型（`IRQ_FLAG_TYPE_MASK`，`irq.c:35`），bit4 为
`MR_IRQ_SHARED`（`mr_irq.h:37`）；共享要求整个 `flags` 逐位一致。

### 10. 核心流程

**控制器注册**：`irq_init`（`mrX/kernel/irq.c:115`）在锁内调用 `irq_table_insert`
（`irq.c:39`）逐号占用描述符：任一号冲突则回滚已占用项并返回 `-MR_EEXIST`；成功后 每个描述符的 `depth` 初始化为
1（关闭态）。对象初始化失败同样回滚。

**申请动作**：`irq_request`（`irq.c:342`）在锁内完成：检查 dying（`-MR_ENOENT`）； 首个动作调用 `ops->type_set` 与
`ops->priority_set`（默认优先级 0）并记录
`desc->flags`；非首个动作要求共享（否则 `-MR_EBUSY`）且 flags 逐位一致（否则
`-MR_EINVAL`）；动作链按 `owner` 查重（重复 `-MR_EEXIST`）后尾插；最后
`irq_depth_enable`（`irq.c:323`）在深度 1→0 时调用 `ops->unmask` + `ops->enable`。

**深度嵌套的钳位**：`irq_depth_enable` 用 `fetch_sub` 检测 1→0 跃迁，非跃迁时若深度 已非正则回写 0（`irq.c:327-333`）；
`irq_depth_disable`（`irq.c:71`）对称地用
`fetch_add` 检测 0→1 跃迁，非跃迁且无动作时回写 1。这保证了重复 enable/disable 不重复操作硬件，且无动作时深度回到确定的静态（测试
`depth_saturation_without_action`
覆盖）。

**分发**：`mr_irq_handle`（`irq.c:660`）：

1. 锁内校验中断号有效性，`mr_irq_get` 增加控制器引用（防分发中删除），调用
   `ops->mask` 屏蔽本中断。
2. 遍历动作链：跳过 `owner` 为空（释放中）的动作；对其余动作 `pending++` 后 **解锁调用回调，再重新加锁**（`irq.c:708-714`
   ），回调期间不持锁，因此回调内 可以调用任何 Irq API。
3. 回调返回后 `pending--`；若动作已被释放（`owner == MR_NULL`）且 `pending` 归零， 在锁内就地摘链释放（用栈上副本 `action2`
   维持遍历，`irq.c:719-739`）。
4. 返回 `MR_IRQ_HANDLED` 停止分发；返回 `MR_IRQ_DEFERRED` 且动作带 `defer_entry`
   时投递到内部工作队列并再 `pending++`（`irq.c:747-756`），不支持时按已处理。
5. 结束时若 `depth == 0`（仍使能）调用 `ops->unmask`，释放引用并返回结果。

**释放**：`mr_irq_free`（`irq.c:462`）按 `owner` 匹配动作：无 pending 直接摘链 释放；有 pending 则仅置 `owner = MR_NULL`，交由第
4 步或下半部完成路径 （`irq_action_defer_entry`，`irq.c:226`）回收。最后一个有效动作移除后
`irq_depth_disable` 关闭硬件。

**删除控制器**：`mr_irq_del` 先置 dying，再走对象删除通道 （`irq_statik_release` / `irq_dynamic_release`，`irq.c:869-882`）→
`irq_del`
（`irq.c:842`）在锁内 `irq_table_remove`（`irq.c:90`）逐线释放动作、关闭硬件、 清空描述符。pending 中的动作同样延迟回收。

### 11. 并发与中断安全

- 全局自旋锁 `__irq_lock`（关中断自旋锁，`irq.c:23`）保护描述符表、动作链、
  `pending` 与 `dying` 检查；临界区内只做链表与标志操作。
- 顶半部与下半部回调在锁外执行，回调内可以调用任何 Irq API；控制器 `ops` 则在 自旋锁内（关中断）被调用，`ops` 实现必须短小，且不得再进入
  Irq API。
- 所有 API 可在中断与线程上下文调用；`mr_irq_handle` 通过控制器引用计数与动作 pending 计数实现"分发中删除 /
  回调内释放"两类竞态的安全。
- `mr_irq_defer_hook_set` 仅写一个全局指针（`irq.c:832`），应在系统启动单线程阶段 调用。

### 12. 扩展与派生

中断管理的扩展面是 **控制器操作集**，而非对象派生：控制器驱动实现 `mr_irq_ops_t`
的六个硬件操作（`mr_irq.h:47-54`），连同私有数据 `priv` 一起交给 `mr_irq_init` /
`mr_irq_create`（`mr_irq.h:111-124`），框架只按中断号回调它们；测试中的 mock 集即 最小样例（`mrX/test/kernel/irq.c:77`
）。中断使用者经 `mr_irq_request` /
`mr_irq_defer_request` + 唯一 `owner` 挂接（`mr_irq.h:143`、`mr_irq.h:157`）。

`mr_irq_t` 虽带对象基类与静态/动态 clazz 对（`mr_irq.h:66`、`irq.c:17`），却没有
`MR_IRQ_DERIVE_INIT` 一类宏，`mr_irq_init` 固定传入 `MR_CLAZZES_STATIK(irq)`
（`irq.c:151`），因此不支持自定义 clazz 派生控制器，附加状态请走 `ops` + `priv`。

下半部也不可整体替换：defer 由模块内部工作队列承担（`__irq_defer_queue`，`irq.c:31`）， 执行入口为 `mr_irq_defer_execute`（
`irq.c:784`），平台只能用 `mr_irq_defer_hook_set`
提供 suspend / wakeup 调度钩子（`irq.c:832`、`mr_irq.h:260`，推荐以初值 0 的信号量 充当，`mr_irq.h:258`）；树内没有
`irq_defer.c`。

### 13. 源码导读

| 位置                                 | 职责                                                 |
|--------------------------------------|------------------------------------------------------|
| `mrX/include/mrX/kernel/mr_irq.h:32` | 触发类型/共享标志、返回值枚举、操作集与结构体定义    |
| `mrX/include/mrX/kernel/mr_irq.h:99` | 回调类型 `mr_irq_entry_t`                            |
| `mrX/kernel/irq.c:23`                | 全局自旋锁与描述符表                                 |
| `mrX/kernel/irq.c:39`                | 区间占用与冲突回滚                                   |
| `mrX/kernel/irq.c:71` / `irq.c:323`  | 使能深度 disable/enable（含钳位）                    |
| `mrX/kernel/irq.c:342`               | 动作申请（类型/优先级初始化、共享与查重）            |
| `mrX/kernel/irq.c:462`               | 动作释放（含 pending 延迟释放）                      |
| `mrX/kernel/irq.c:660`               | 中断分发主流程                                       |
| `mrX/kernel/irq.c:784`               | 下半部执行入口 `mr_irq_defer_execute`                |
| `mrX/kernel/irq.c:884`               | `mr_kernel_irq_init`：defer 工作队列初始化与钩子挂接 |
| `mrX/kernel/initcall.c:64`           | 内核初始化序列中的调用点                             |
| `mrX/test/kernel/irq.c`              | 全部用户可见行为的行为级测试（含 defer 与共享场景）  |

### 14. 附录

**配置宏**

| 宏                                                                | 说明                                       |
|-------------------------------------------------------------------|--------------------------------------------|
| `MR_USE_IRQ`                                                      | 模块开关，默认 y                           |
| `MR_CFG_IRQ_TABLE_SIZE`                                           | 描述符表大小，默认 16，范围 2~1024         |
| `MR_USE_IRQ_DEFER`                                                | 下半部开关，默认 y，自动选中 `MR_USE_WORK` |
| `MR_USE_IRQ_DEFER_HOOK`                                           | 下半部工作队列钩子开关，默认 n             |
| `MR_IRQ_EDGE_RISING` / `MR_IRQ_EDGE_FALLING` / `MR_IRQ_EDGE_BOTH` | 边沿触发类型                               |
| `MR_IRQ_LEVEL_HIGH` / `MR_IRQ_LEVEL_LOW`                          | 电平触发类型                               |
| `MR_IRQ_SHARED`                                                   | 共享中断标志（0x10）                       |

**错误码**：`-MR_EEXIST`（区间冲突 / 所有者重复）、`-MR_ENOENT`（无控制器或动作 不存在）、`-MR_EBUSY`（非共享中断号被二次申请）、
`-MR_EINVAL`（共享 flags 不 一致）、`-MR_ENOMEM`（动作或控制器分配失败）；见
`mrX/include/mrX/libc/mr_errno.h`。参数越界（中断号超表、空回调）在断言开启时由
`MR_ASSERT` 拦截，而非返回错误码。`mr_irq_handle` 失败一律返回 `MR_IRQ_NONE`。
