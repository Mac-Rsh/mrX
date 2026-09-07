# 调试器（Dbg）

> XVM 调试单元为虚拟机提供断点表与探针（probe）门控两套机制：断点在取指前按地址
> 命中停机，探针通过步进门控实现单步、暂停与继续，并把调试停机对运行时隐藏。

| 项       | 说明                                                                                |
|----------|-------------------------------------------------------------------------------------|
| 功能宏   | `MR_USE_MODULE_XVM_DEBUG`（依赖 `MR_USE_MODULE_XVM`）                               |
| 头文件   | `<mrX/modules/xvm/mr_xvm_dbg.h>`                                                    |
| 依赖模块 | [虚拟机 XVM](xvm.md)（调试单元内嵌于 `mr_xvm_t`）                                   |
| 参考测试 | 无独立测试（断点与门控行为由 `mrX/test/modules/xvm/gdb.c` 经 [GDB 桩](gdb.md)覆盖） |

---

## 用户指南

### 1. 模块概述

调试单元解决的问题是： **在不修改客户机代码的前提下观察与控制 XVM 的执行**。

特性：

- 地址比较断点：ISA 在取指前上报 PC，命中即停机，无需软件插桩。
- 探针（probe）：每个 XVM 至多一个调试代理，通过回调获知每次停机原因。
- 步进门控（step gate）：暂停（HOLD）、单步（STEP）、放开（OPEN）三档。
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
| `MR_XVM_DBG_GATE_OPEN`（0xFFFFFFFF） | 放开：不限制步数，直到断点/异常/系统调用                              |

**调试停机对运行时隐藏**：断点命中在 ISA 内表现为 `MR_XVM_EXCEPT`（异常号
`MR_XVM_EXCEPT_NR_BREAK`），单步完成表现为 `MR_XVM_NONE`；探针回调触发后，
`mr_xvm_execute` 的返回值一律被改写为 `MR_XVM_NONE`、状态恢复 RUNNING、门控置
HOLD。宿主调度循环因此可以无差别地轮询执行，无需感知调试停机。系统调用 （`MR_XVM_SYSCALL`）与客户机退出（`MR_XVM_EXIT`
）不属于调试停机，原样透出。

**断点表**

- 插入重复地址是幂等的（返回 0）；表满返回 `-MR_ENOMEM`。
- 移除不存在的地址返回 `-MR_ENOENT`。
- `mr_xvm_dbg_break_skip(pc)` 设置一次性跳过：下一次在且仅在 `pc` 处的命中不 停机并被消费。继续/单步前应先跳过当前
  PC，否则断点立即再次命中。
- `mr_xvm_dbg_break_clear` 清空断点表与未消费的跳过标记；`mr_xvm_load` 会自动 清空断点。

**回调上下文**：探针回调在 `mr_xvm_execute` 调用者上下文中同步执行，此时虚拟机 状态已恢复 RUNNING、门控为 HOLD。回调内可以调用本模块全部
API（设门控、增删 断点），也可以读写寄存器/数据内存，但不应重入 `mr_xvm_execute`。

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

`gate` 取 `MR_XVM_DBG_GATE_HOLD`/`MR_XVM_DBG_GATE_STEP`/`MR_XVM_DBG_GATE_OPEN`
三者之一。

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
    mr_xvm_execute(xvm, &event, 100);   /* Executes exactly one instruction */
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

两种来源：客户机执行了 `EBREAK` 指令，或断点命中（ISA 在取指前上报）。有探针时 后者会被调试单元截获并隐藏，透出的 BREAK
异常即为客户机 `EBREAK`。

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
| `step_gate`      | 步进门控：`MR_XVM_DBG_GATE_HOLD`(0)/`GATE_STEP`(1)/`GATE_OPEN`(0xFFFFFFFF)   |
| `entry` / `priv` | 探针回调与私有指针；`entry` 为 `MR_NULL` 表示无探针，此时 enter/leave 均直通 |

目标描述复用 ISA 的 `mr_xvm_isa_target_t`（架构名、feature 名、寄存器描述符表）， RISCV32 仅在 `MR_USE_MODULE_XVM_DEBUG` 下提供（
`riscv32.c:34-80`）。

### 10. 核心流程

**门控钳制（probe_enter，`mrX/modules/xvm/dbg.c:82`）**：无探针直通。有探针时
`step` 被钳到 `step_gate` 与 `step` 的较小值；钳制后仍非 0 则放行 ISA；为 0 （HOLD）则 `event->none.step = 0` 并让
`mr_xvm_execute` 整体跳过 ISA。

**停机回收（probe_leave，`dbg.c:112`）**：无探针直通。有探针时仅两类结果算调试 停机：`MR_XVM_EXCEPT`（断点命中等）或"门控为 STEP
且结果为 `MR_XVM_NONE`"（单步 完成）。判定为停机后：调用探针回调 → 状态原子写回 RUNNING → 门控置 HOLD → 返回
`MR_XVM_NONE` 把停机从运行时视角抹掉。`MR_XVM_SYSCALL`/`MR_XVM_EXIT` 不是调试 停机，透传且不触发回调。

**断点命中（break_hit，`dbg.c:228`）**：表空直接 `MR_FALSE`；`skip` 有效且
`skip_addr == pc` 时消费跳过（清 `skip`）并 `MR_FALSE`——只在匹配地址上消费， 避免误吞别处的命中；否则线性扫描
`addr[0, num)`。

**断点增删**：`break_insert`（`dbg.c:153`）先查重（幂等），表满返回
`-MR_ENOMEM`，否则尾插。`break_remove`（`dbg.c:180`）用末尾条目填补被删位置， 保持紧凑。`break_clear`（`dbg.c:202`）同时清 `num`
与 `skip`。

**"为什么这样写"**：

- 门控用 `mr_uint32_t` 而非布尔量：HOLD=0 使"附加即暂停"成为自然结果（attach 把 gate 清 0），OPEN 用全 1 保证与任意 step
  的钳制结果不变。
- 停机隐藏把"调试停机"与"客户机异常"解耦：调度循环无需区分两者，断点命中对客户机 程序本身也不产生任何痕迹（PC 未变、无 trap
  进入）。
- skip 只消费同地址命中（`dbg.c:245-248`）：单步跨过断点后，若后续又跳回同一地址 （循环），断点重新生效。

### 11. 并发与中断安全

- 调试单元无独立锁，其并发安全由 XVM 的单执行者锁保证：`probe_enter`/
  `probe_leave`/`break_hit` 都在 `mr_xvm_execute` 的锁临界区内运行，断点增删与
  门控设置应与执行互斥（通常在同一线程，或经 [GDB 桩](gdb.md) 的门控约束）。
- 探针回调运行在 `mr_xvm_execute` 调用者上下文，回调返回前锁尚未释放；回调内 禁止阻塞，禁止再次调用 `mr_xvm_execute`。
- `mr_xvm_dbg_probe_leave` 用原子写恢复 RUNNING，与 `mr_xvm_pause` 等 CAS 操作 并发安全。

### 12. 扩展与派生

本模块无派生机制。二次开发的推荐接入点是探针回调：任何调试协议适配器（RSP、 自定义串口协议等）都可以作为探针挂接，用门控控制执行、用断点表落实断点、用
`mr_xvm_dbg_target` 生成寄存器元信息。[GDB 桩](gdb.md) 即为参考实现。

### 13. 源码导读

| 位置                                         | 职责                                               |
|----------------------------------------------|----------------------------------------------------|
| `mrX/include/mrX/modules/xvm/mr_xvm_dbg.h`   | 门控常量、probe/breaktable/dbg 结构与全部 API 声明 |
| `mrX/modules/xvm/dbg.c:14`                   | `mr_xvm_dbg_init`：缓存 target、开门、清空状态     |
| `mrX/modules/xvm/dbg.c:33`                   | 探针附加/摘除（一 XVM 一探针）                     |
| `mrX/modules/xvm/dbg.c:82`                   | probe_enter：门控钳制                              |
| `mrX/modules/xvm/dbg.c:112`                  | probe_leave：停机判定、回调、隐藏                  |
| `mrX/modules/xvm/dbg.c:153`                  | 断点插入/移除/清空（紧凑数组）                     |
| `mrX/modules/xvm/dbg.c:228`                  | break_hit：快速路径 + skip 消费 + 线性扫描         |
| `mrX/modules/xvm/xvm.c:212`                  | XVM 核心中的 enter/leave 调用点                    |
| `mrX/modules/xvm/isa/riscv32/riscv32.c:1291` | RISCV32 取指前的断点检查                           |
| `mrX/test/modules/xvm/gdb.c`                 | 断点/单步/门控行为经 GDB 桩的端到端覆盖            |

### 14. 附录

**配置宏**

| 宏                          | 说明                                       |
|-----------------------------|--------------------------------------------|
| `MR_USE_MODULE_XVM_DEBUG`   | 模块开关，默认 n，依赖 `MR_USE_MODULE_XVM` |
| `MR_CFG_XVM_BREAKPOINT_NUM` | 断点槽数，默认 16，范围 1~64               |
| `MR_XVM_DBG_GATE_OPEN`      | 门控：放开（0xFFFFFFFF）                   |
| `MR_XVM_DBG_GATE_STEP`      | 门控：单步（1）                            |
| `MR_XVM_DBG_GATE_HOLD`      | 门控：暂停（0）                            |

**错误码**：`mr_xvm_dbg_probe_attach` 重复附加返回 `-MR_EBUSY`；
`mr_xvm_dbg_break_insert` 表满返回 `-MR_ENOMEM`；`mr_xvm_dbg_break_remove` 未找到 返回 `-MR_ENOENT`；其余无返回值。断言开启时空指针与错误类型由
`MR_ASSERT` 拦截。
