# 调试器（Dbg）

> XVM 调试单元为虚拟机提供断点表与探针（probe）门控两套机制：断点在取指前按地址
> 命中停机，探针通过步进门控实现单步、暂停与继续，并把调试停机对运行时隐藏。

| 项       | 说明                                                                                |
|----------|-------------------------------------------------------------------------------------|
| 功能宏   | `MR_USE_MODULE_XVM_DEBUG`（依赖 `MR_USE_MODULE_XVM`）                               |
| 头文件   | `<mrX/modules/xvm/mr_xvm_dbg.h>`                                                    |
| 依赖模块 | [虚拟机 XVM](xvm.md)（调试单元内嵌于 `mr_xvm_t`）                                  |
| 参考测试 | 无独立测试（断点与门控行为由 `mrX/test/modules/xvm/gdb.c` 经 [GDB 桩](gdb.md)覆盖） |

---

## 用户指南

### 1. 模块概述

调试单元解决的问题是： **在不修改客户机代码的前提下观察与控制 XVM 的执行**。

特性：

- 地址比较断点：ISA 在取指前上报 PC，命中即停机，无需软件插桩。
- 探针（probe）：每个 XVM 至多一个调试代理，通过回调获知每次停机原因。
- 步进门控（step gate）：公开常量定义暂停（HOLD）、单步（STEP）、放开（OPEN）；底层字段实际是 `mr_uint32_t` 上限值，setter 不校验只能取这三档。
- 跳过（skip）：从断点处继续/单步时只消耗同地址的下一次命中，避免原地打转。
- 目标描述（target）：向调试代理透出 ISA 的架构名与寄存器表。

分工边界：本模块是被动的机制层，不涉及任何通信协议；将其映射到 GDB 远程串行协议 的是 [GDB 桩](gdb.md)。

### 2. 配置与依赖

| 配置项                      | 类型 | 默认值 | 说明                                                                          |
|-----------------------------|------|--------|-------------------------------------------------------------------------------|
| `MR_USE_MODULE_XVM_DEBUG`   | bool | n      | 启用 XVM 调试单元（依赖 `MR_USE_MODULE_XVM`）；关闭时 `mr_xvm_t` 不含调试字段 |
| `MR_CFG_XVM_BREAKPOINT_NUM` | 整型 | 16     | 每个 XVM 的断点槽数量，范围 1~64；空表保持取指快速路径                        |

启用后包含头文件即可使用：

```c
#include <mrX/modules/xvm/mr_xvm_dbg.h>
```

注意：断点能否生效取决于 ISA 后端是否在取指前调用 `mr_xvm_dbg_break_hit`。 内置 RISCV32 后端已支持（[虚拟机 XVM](xvm.md) 第
12 节）。

### 3. 快速上手

一个最小调试代理：附加探针、设断点、单步观察：

```c
#include <mrX/modules/xvm/mr_xvm_dbg.h>

static void my_probe(void *priv, mr_xvm_return_t exec_ret,
                     mr_xvm_event_t *event) {
    /* Called on every debug stop: breakpoint hit or completed single-step */
    (void)priv;
    (void)exec_ret;
    (void)event;
}

void debug_once(mr_xvm_t *xvm) {
    mr_xvm_dbg_probe_attach(xvm, my_probe, MR_NULL);
    mr_xvm_dbg_break_insert(xvm, 0x104);            /* Break at address */

    mr_xvm_dbg_probe_set_gate(xvm, MR_XVM_DBG_GATE_OPEN);
    /* Each mr_xvm_execute() now stops at the breakpoint; the probe fires and
     * execution is held afterwards */

    mr_xvm_dbg_probe_detach(xvm);                   /* Restore free running */
}
```

要点：

- `mr_xvm_dbg_probe_attach` 成功后门控立即变为 HOLD，客户机停住等宿主放行。
- 断点命中或单步完成都会触发探针回调，随后执行再次被 HOLD。
- `mr_xvm_dbg_probe_detach` 打开门控并清空全部断点。

### 4. 使用说明

**步进门控**：探针附上后，每次 `mr_xvm_execute` 的步数会被门控钳制：

| 门控值                               | 语义                                                                  |
|--------------------------------------|-----------------------------------------------------------------------|
| `MR_XVM_DBG_GATE_HOLD`（0）          | 暂停：本次 `execute` 不执行任何指令                                   |
| `MR_XVM_DBG_GATE_STEP`（1）          | 单步：本次最多执行 1 条；若正常跑完（返回 `MR_XVM_NONE`）视为调试停机 |
| `MR_XVM_DBG_GATE_OPEN`（0xFFFFFFFF） | 放开：对传入 step 不再额外收紧，由 ISA 自己决定何时停止                |

`mr_xvm_dbg_probe_set_gate()` 不验证取值：任何较小的非零值都会作为本次 `execute` 的 step 上限参与 `min(gate, step)`。但只有 gate **恰好等于** `MR_XVM_DBG_GATE_STEP` 时，正常执行完该批 step 的 `MR_XVM_NONE` 才被当成“单步完成”调试停机。因此应用层应优先使用三个公开常量。

**调试停机对运行时隐藏**：挂有 probe 时，`probe_leave` 会把**任何 `MR_XVM_EXCEPT`** 都视为调试停机，不只地址断点；单步 gate 下正常返回 `MR_XVM_NONE` 也算停机。它先把原始 `exec_ret/event` 交给 probe 回调，回调返回后再把 XVM 状态写回 RUNNING、gate 置 HOLD，并向 `mr_xvm_execute` 返回 `MR_XVM_NONE`。`MR_XVM_SYSCALL` 与 `MR_XVM_EXIT` 不被该层截获，原样透出。

**断点表**

- 插入重复地址是幂等的（返回 0）；表满返回 `-MR_ENOMEM`。
- 移除不存在的地址返回 `-MR_ENOENT`。
- `mr_xvm_dbg_break_skip(pc)` 设置一次性跳过：下一次在且仅在 `pc` 处的命中不 停机并被消费。继续/单步前应先跳过当前
  PC，否则断点立即再次命中。
- `mr_xvm_dbg_break_clear` 清空断点表与未消费的跳过标记；`mr_xvm_load` 会自动 清空断点。

**回调上下文**：probe 回调在 `mr_xvm_execute` 调用者上下文中同步执行，且此时 XVM 的 execute lock 仍然持有。回调发生在恢复 RUNNING / 置 HOLD **之前**：异常停机时 `status` 已是 EXCEPT，单步完成时通常仍是 RUNNING，`step_gate` 也仍是触发本次停机的原值。回调内可直接使用不再获取 execute lock 的调试/寄存器接口；不要重入 `mr_xvm_execute`，也不要让其他上下文同时修改 debug 状态。

### 5. API 参考

探针管理：

| API                         | 说明                                         |
|-----------------------------|----------------------------------------------|
| `mr_xvm_dbg_probe_attach`   | 附加探针（每个 XVM 一个），成功后门控置 HOLD |
| `mr_xvm_dbg_probe_detach`   | 摘除探针，开门并清空断点                     |
| `mr_xvm_dbg_probe_set_gate` | 设置步进门控                                 |
| `mr_xvm_dbg_target`         | 获取 ISA 目标描述（可能为 `MR_NULL`）        |

断点管理：

| API                       | 说明                                        |
|---------------------------|---------------------------------------------|
| `mr_xvm_dbg_break_insert` | 插入地址断点（重复地址幂等）                |
| `mr_xvm_dbg_break_remove` | 移除地址断点                                |
| `mr_xvm_dbg_break_clear`  | 清空断点表与跳过标记                        |
| `mr_xvm_dbg_break_skip`   | 设置一次性跳过的 PC                         |
| `mr_xvm_dbg_break_hit`    | 查询 PC 是否命中断点（由 ISA 在取指前调用） |

内部挂钩（由 [虚拟机 XVM](xvm.md) 调用，应用一般不直接使用）：

| API                      | 说明                             |
|--------------------------|----------------------------------|
| `mr_xvm_dbg_init`        | XVM 初始化时初始化调试单元       |
| `mr_xvm_dbg_probe_enter` | ISA 执行前按门控钳制 step        |
| `mr_xvm_dbg_probe_leave` | ISA 执行后回收调试停机、通知探针 |

**`mr_xvm_dbg_probe_attach`**

```c
mr_err_t mr_xvm_dbg_probe_attach(mr_xvm_t *xvm,
                                 mr_xvm_dbg_probe_entry_t *entry, void *priv);
```

- `entry`：探针回调，类型 `void (*)(void *priv, mr_xvm_return_t exec_ret,
  mr_xvm_event_t *event)`，不可为 `MR_NULL`；已有探针时返回 `-MR_EBUSY`。
- `priv`：透传给回调的私有指针。
- 返回：成功 `0`，失败负数错误码。

**`mr_xvm_dbg_probe_set_gate`**

```c
void mr_xvm_dbg_probe_set_gate(mr_xvm_t *xvm, mr_uint32_t gate);
```

推荐 `gate` 使用 `MR_XVM_DBG_GATE_HOLD` / `MR_XVM_DBG_GATE_STEP` / `MR_XVM_DBG_GATE_OPEN`。实现只是把任意 `mr_uint32_t` 原样写入 `step_gate`，没有合法值检查。

**`mr_xvm_dbg_break_insert` / `mr_xvm_dbg_break_remove`**

```c
mr_err_t mr_xvm_dbg_break_insert(mr_xvm_t *xvm, mr_uint64_t addr);
mr_err_t mr_xvm_dbg_break_remove(mr_xvm_t *xvm, mr_uint64_t addr);
```

断点表为紧凑数组：移除时用末尾条目填补空洞，因此遍历顺序不保证插入顺序。

**`mr_xvm_dbg_break_hit`**

```c
mr_bool_t mr_xvm_dbg_break_hit(mr_xvm_t *xvm, mr_uint64_t pc);
```

表空时直接返回 `MR_FALSE`（快速路径）；存在与 `pc` 匹配的一次性跳过时先消费该 跳过并返回 `MR_FALSE`；否则扫描表项，命中返回
`MR_TRUE`。

### 6. 完整示例

示例 1：断点 + 单步组合调试（门控与跳过配合）：

```c
static void probe(void *priv, mr_xvm_return_t exec_ret, mr_xvm_event_t *event) {
    mr_xvm_t *xvm = priv;
    mr_uint64_t pc = 0;

    mr_xvm_reg_load(xvm, 32, &pc);      /* 32 = PC on riscv32 */
    (void)exec_ret;
    (void)event;
}

void step_over_breakpoint(mr_xvm_t *xvm) {
    mr_xvm_event_t event = {0};

    mr_xvm_dbg_probe_attach(xvm, probe, xvm);
    mr_xvm_dbg_break_insert(xvm, 0x104);
    mr_xvm_dbg_probe_set_gate(xvm, MR_XVM_DBG_GATE_OPEN);

    /* Runs until PC == 0x104; probe fires, gate is HOLD afterwards */
    mr_xvm_execute(xvm, &event, 100);

    /* Single-step: skip the breakpoint at the current PC first */
    mr_xvm_dbg_break_skip(xvm, 0x104);
    mr_xvm_dbg_probe_set_gate(xvm, MR_XVM_DBG_GATE_STEP);
    mr_xvm_execute(xvm, &event, 100);   /* ISA step is clamped to at most one instruction */
}
```

示例 2：释放运行并移除断点（继续执行）：

```c
void continue_running(mr_xvm_t *xvm) {
    mr_xvm_dbg_break_remove(xvm, 0x104);
    mr_xvm_dbg_probe_set_gate(xvm, MR_XVM_DBG_GATE_OPEN);
    /* Next mr_xvm_execute() resumes freely; breakpoint no longer stops */
}
```

### 7. 常见问题（FAQ）

**Q：设置了断点但从不命中？**

按顺序排查：确认 `MR_USE_MODULE_XVM_DEBUG` 与 `MR_CFG_XVM_BREAKPOINT_NUM` 已配置； 确认 ISA 后端支持（内置 RISCV32 支持，自定义
ISA 需在取指前调用
`mr_xvm_dbg_break_hit`）；确认断点地址与实际 PC 一致；确认门控不是 HOLD （HOLD 时指令根本不执行）。

**Q：从断点处继续后立刻又停在同一条指令？**

继续前没有调用 `mr_xvm_dbg_break_skip(pc)`。断点是地址比较式的，PC 未越过断点 地址时必然再次命中；`mr_xvm_load`
会清空断点，无需手动处理该场景。

**Q：探针回调里能把虚拟机跑起来吗？**

不要在回调中重入 `mr_xvm_execute`。回调的正确用法是记录状态、修改门控、增删 断点或读写寄存器，执行驱动仍由宿主调度循环完成。

**Q：`mr_xvm_execute` 返回 `MR_XVM_EXCEPT` 且异常号是 BREAK，是谁触发的？**

两种来源都可能产生同一个 `MR_XVM_EXCEPT_NR_BREAK`：客户机执行 `EBREAK`，或地址断点在取指前命中。当前 debug core **不区分来源**；只要挂有 probe，二者以及其他 `MR_XVM_EXCEPT` 都会先交给 probe，并被 `probe_leave` 对运行时隐藏。因此不能根据“是否透出 BREAK”判断来源。

**Q：一个 XVM 能挂多个调试代理吗？**

不能。每个 XVM 只有一个探针槽，重复附加返回 `-MR_EBUSY`；需要级联时由现有代理 （如 [GDB 桩](gdb.md)）自行转发。

---

## 开发者指南

### 8. 架构总览

调试单元内嵌于 XVM 对象（`mr_xvm_t.dbg`，仅 `MR_USE_MODULE_XVM_DEBUG` 时存在）， 位于 XVM 核心与 ISA 之间的挂钩层：

```
   宿主调度循环
        | mr_xvm_execute()
        v
   XVM 核心 xvm.c
        |  probe_enter()（钳制 step / 整体跳过）      xvm.c:212
        v
   ISA 后端 execute()
        |  break_hit(pc)（取指前断点检查）            riscv32.c:1291
        v
   调试单元 dbg.c
        |  probe_leave()（回收停机、通知探针、置 HOLD）xvm.c:235
        v
   探针回调（GDB 桩 / 用户代理）
```

模块自身无独立初始化入口：`mr_xvm_dbg_init` 由 `xvm_init` 在创建时调用 （`mrX/modules/xvm/xvm.c:40-43`），从 ISA 读取 target
指针缓存。

### 9. 数据结构

调试单元（`mrX/include/mrX/modules/xvm/mr_xvm_dbg.h:57`）：

```c
typedef struct mr_xvm_dbg {
    mr_xvm_dbg_breaktable_t breaktable;   /* Address-compare breakpoints */
    mr_xvm_isa_target_t *target;          /* ISA target description (cached) */
    mr_xvm_dbg_probe_t probe;             /* Attached debugger probe */
} mr_xvm_dbg_t;
```

断点表（`mr_xvm_dbg.h:46`）：

| 字段                              | 说明                                         |
|-----------------------------------|----------------------------------------------|
| `addr[MR_CFG_XVM_BREAKPOINT_NUM]` | 断点地址数组，紧凑存放，`[0, num)` 有效      |
| `skip_addr` / `skip`              | 一次性跳过的 PC 与未消费标记                 |
| `num`                             | 当前断点数量；为 0 时 `break_hit` 走快速路径 |

探针（`mr_xvm_dbg.h:39`）：

| 字段             | 说明                                                                         |
|------------------|------------------------------------------------------------------------------|
| `step_gate`      | `mr_uint32_t` step 上限；公开推荐值为 HOLD(0)/STEP(1)/OPEN(0xFFFFFFFF)       |
| `entry` / `priv` | 探针回调与私有指针；`entry` 为 `MR_NULL` 表示无探针，此时 enter/leave 均直通 |

目标描述复用 ISA 的 `mr_xvm_isa_target_t`（架构名、feature 名、寄存器描述符表）， RISCV32 仅在 `MR_USE_MODULE_XVM_DEBUG` 下提供（
`riscv32.c:34-80`）。

### 10. 核心流程

**门控钳制（probe_enter，`mrX/modules/xvm/dbg.c:85`）**：无探针直通。有探针时
`step` 被钳到 `step_gate` 与 `step` 的较小值；钳制后仍非 0 则放行 ISA；为 0 （HOLD）则 `event->none.step = 0` 并让
`mr_xvm_execute` 整体跳过 ISA。

**停机回收（probe_leave，`dbg.c:115`）**：无 probe 直接透传。有 probe 时，两类结果算调试停机：任意 `MR_XVM_EXCEPT`，或 gate 恰为 STEP 且结果为 `MR_XVM_NONE`。判定后先调用 probe 回调；若原始结果是异常，再把 `event->none.step` 清 0；随后状态原子写回 RUNNING、gate 置 HOLD，最后返回 `MR_XVM_NONE`。因此回调观察到的是恢复前状态，而不是最终 HOLD/RUNNING 状态。

**断点命中（break_hit，`dbg.c:231`）**：表空直接 `MR_FALSE`；`skip` 有效且
`skip_addr == pc` 时消费跳过（清 `skip`）并 `MR_FALSE`——只在匹配地址上消费， 避免误吞别处的命中；否则线性扫描
`addr[0, num)`。

**断点增删**：`break_insert`（`dbg.c:156`）先查重（幂等），表满返回
`-MR_ENOMEM`，否则尾插。`break_remove`（`dbg.c:183`）用末尾条目填补被删位置， 保持紧凑。`break_clear`（`dbg.c:205`）同时清 `num`
与 `skip`。

**"为什么这样写"**：

- 门控用 `mr_uint32_t` 而非布尔量：HOLD=0 使"附加即暂停"成为自然结果（attach 把 gate 清 0），OPEN 用全 1 保证与任意 step
  的钳制结果不变。
- 停机隐藏把“有 probe 时的异常/单步停机”和宿主运行循环解耦：`mr_xvm_execute` 最终返回 `MR_XVM_NONE`。地址断点是在取指前直接制造 BREAK 事件，PC 不推进；而客户机自身产生的异常是否已经进入 ISA trap 逻辑由 ISA 后端决定。
- skip 只消费同地址命中（`dbg.c:245-248`）：单步跨过断点后，若后续又跳回同一地址 （循环），断点重新生效。

### 11. 并发与中断安全

- 调试单元**没有自己的锁**。`probe_enter` / ISA 内的 `break_hit` / `probe_leave` 确实都发生在 `mr_xvm_execute` 持有 XVM execute lock 的期间，但 `break_insert/remove/clear/skip`、`probe_attach/detach/set_gate` 自身不会获取这把锁。外部上下文若与 execute 并发调用这些 API，会直接竞争普通字段；应由调用方串行化。
- probe 回调同样在 execute lock 尚未释放时运行。由于调试 API 不再次获取该锁，回调内可以调整 gate/断点；但不要重入 `mr_xvm_execute`。是否可以阻塞取决于宿主调用上下文，但阻塞会一直占住 XVM execute lock。
- `probe_leave` 最后对 `status` 使用无条件原子 store(RUNNING)。这不构成与 `mr_xvm_pause` 的完整并发协议：单步停机期间若另一个上下文先把 RUNNING 改成 PAUSED，随后该 store 仍可能覆盖它。需要由更高层避免这类并发状态控制。

### 12. 扩展与派生

本模块无派生机制。二次开发的推荐接入点是探针回调：任何调试协议适配器（RSP、 自定义串口协议等）都可以作为探针挂接，用门控控制执行、用断点表落实断点、用
`mr_xvm_dbg_target` 生成寄存器元信息。[GDB 桩](gdb.md) 即为参考实现。

### 13. 源码导读

| 位置                                         | 职责                                               |
|----------------------------------------------|----------------------------------------------------|
| `mrX/include/mrX/modules/xvm/mr_xvm_dbg.h`   | 门控常量、probe/breaktable/dbg 结构与全部 API 声明 |
| `mrX/modules/xvm/dbg.c:14`                   | `mr_xvm_dbg_init`：缓存 target、开门、清空状态     |
| `mrX/modules/xvm/dbg.c:36`                   | 探针附加/摘除（一 XVM 一探针）                     |
| `mrX/modules/xvm/dbg.c:85`                   | probe_enter：门控钳制                              |
| `mrX/modules/xvm/dbg.c:115`                  | probe_leave：停机判定、回调、隐藏                  |
| `mrX/modules/xvm/dbg.c:156`                  | 断点插入/移除/清空（紧凑数组）                     |
| `mrX/modules/xvm/dbg.c:231`                  | break_hit：快速路径 + skip 消费 + 线性扫描         |
| `mrX/modules/xvm/xvm.c:212`                  | XVM 核心中的 enter/leave 调用点                    |
| `mrX/modules/xvm/isa/riscv32/riscv32.c:1291` | RISCV32 取指前的断点检查                           |
| `mrX/test/modules/xvm/gdb.c`                 | 断点/单步/门控行为经 GDB 桩的端到端覆盖            |

### 14. 附录

**配置宏**

| 宏                          | 说明                                       |
|-----------------------------|--------------------------------------------|
| `MR_USE_MODULE_XVM_DEBUG`   | 模块开关，默认 n，依赖 `MR_USE_MODULE_XVM` |
| `MR_CFG_XVM_BREAKPOINT_NUM` | 断点槽数，默认 16，范围 1~64               |
| `MR_XVM_DBG_GATE_OPEN`      | 推荐门控值：不额外限制 step（0xFFFFFFFF）  |
| `MR_XVM_DBG_GATE_STEP`      | 推荐门控值：单步并在正常完成后停住（1）    |
| `MR_XVM_DBG_GATE_HOLD`      | 推荐门控值：暂停、不进入 ISA（0）          |

**错误码**：`mr_xvm_dbg_probe_attach` 重复附加返回 `-MR_EBUSY`；
`mr_xvm_dbg_break_insert` 表满返回 `-MR_ENOMEM`；`mr_xvm_dbg_break_remove` 未找到 返回 `-MR_ENOENT`；其余无返回值。断言开启时空指针与错误类型由
`MR_ASSERT` 拦截。
