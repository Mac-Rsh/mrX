# 调试器（Dbg）

内嵌于 XVM
的被动调试机制层，提供硬件地址比较断点表与探针（probe）步进门控，用于在不修改客户机代码的前提下实现断点、单步、暂停与继续。通信协议由上层适配器（如 [GDB 桩](gdb.md)
）实现。

| 项目   | 说明                                                  |
|--------|-------------------------------------------------------|
| 功能宏 | `MR_USE_MODULE_XVM_DEBUG`（依赖 `MR_USE_MODULE_XVM`） |
| 头文件 | `<mrX/modules/xvm/mr_xvm_debug.h>`                    |
| 依赖   | [虚拟机](xvm.md)                                      |
| 测试   | `test/modules/xvm/gdb.c`（经 GDB 桩端到端覆盖）       |

## 配置

| 配置项                      | 类型 | 默认值 | 说明                                                       |
|-----------------------------|------|--------|------------------------------------------------------------|
| `MR_USE_MODULE_XVM_DEBUG`   | bool | n      | 启用 XVM 调试单元；关闭时 `mr_xvm_t` 不包含调试字段        |
| `MR_CFG_XVM_BREAKPOINT_NUM` | int  | 16     | 每个 XVM 的地址断点槽数，范围 1~64；表空时保持取指快速路径 |

## 快速上手

```c
#include <mrX/modules/xvm/mr_xvm_debug.h>

static void my_probe(void *priv, mr_xvm_return_t exec_ret, mr_xvm_event_t *event) {
    mr_xvm_t *xvm = (mr_xvm_t *)priv;
    mr_uint64_t pc = 0;

    mr_xvm_reg_load(xvm, 32, &pc); /* riscv32 上 32 号为 PC */
    (void)exec_ret;
    (void)event;
}

void debug_session(mr_xvm_t *xvm) {
    mr_xvm_event_t event = {0};

    /* 1. 附加探针后门控立即置 HOLD（暂停），客户机等待放行 */
    mr_xvm_debug_probe_attach(xvm, my_probe, xvm);
    mr_xvm_debug_break_insert(xvm, 0x104);

    /* 2. 放开运行，命中 0x104 后触发 my_probe 并将门控恢复为 HOLD */
    mr_xvm_debug_probe_set_gate(xvm, MR_XVM_DEBUG_GATE_OPEN);
    mr_xvm_execute(xvm, &event, 100);

    /* 3. 从断点处单步：须先跳过当前 PC，再设门控为 STEP */
    mr_xvm_debug_break_skip(xvm, 0x104);
    mr_xvm_debug_probe_set_gate(xvm, MR_XVM_DEBUG_GATE_STEP);
    mr_xvm_execute(xvm, &event, 100);

    /* 4. 摘除探针：自动打开门控并清空全部断点 */
    mr_xvm_debug_probe_detach(xvm);
}
```

## 核心机制与约束

### 步进门控（Step Gate）

每个 XVM 至多附加一个探针（重复附加返回 `-MR_EBUSY`）。附加后每次
`mr_xvm_execute()` 传入的 `step` 预算都会与 `step_gate` 取最小值：

| 门控常量                 | 值           | 语义                                                             |
|--------------------------|--------------|------------------------------------------------------------------|
| `MR_XVM_DEBUG_GATE_HOLD` | `0`          | 暂停：`mr_xvm_execute()` 直接跳过 ISA 执行，返回 `MR_XVM_NONE`   |
| `MR_XVM_DEBUG_GATE_STEP` | `1`          | 单步：最多执行 1 条指令；若正常完成（`MR_XVM_NONE`）视为调试停机 |
| `MR_XVM_DEBUG_GATE_OPEN` | `0xFFFFFFFF` | 放开：不额外收紧 `step`，直至命中断点或发生其他事件              |

`mr_xvm_debug_probe_set_gate()` 不校验输入范围，但只有门控 **恰好等于**
`MR_XVM_DEBUG_GATE_STEP` 时正常跑完才算单步停机，建议仅使用上述三个常量。

### 调试停机隐藏与断点表

- **停机隐藏**：挂有探针时，`mr_xvm_debug_probe_leave()` 会截获 **任意
  `MR_XVM_EXCEPT`**（不区分是取指前地址断点还是客户机 `EBREAK`）以及 `STEP`
  门控下的正常完成，先同步调用探针回调，再将 XVM 状态恢复为
  `MR_XVM_STATUS_RUNNING`、门控置为 `HOLD`，并向 `mr_xvm_execute()` 返回
  `MR_XVM_NONE`。`MR_XVM_SYSCALL` 与 `MR_XVM_EXIT` 不被截获，原样透出。
- **断点表与 Skip**：插入重复地址幂等返回 `0`，表满返回 `-MR_ENOMEM`，移除不存在的地址返回
  `-MR_ENOENT`。从断点恢复执行或单步前须调用 `mr_xvm_debug_break_skip(xvm, pc)`
  ，否则下次取指仍会立即命中同一地址；该跳过标记仅消耗匹配 `pc` 的下一次命中。
  `mr_xvm_load()` 会自动清空断点表。
- **ISA 支持前提**：断点依赖 ISA 后端在取指前主动调用 `mr_xvm_debug_break_hit()`
  （内置 `riscv32` 已支持）。

### 回调上下文与并发约束

- 探针回调在 `mr_xvm_execute()` 调用者上下文中同步执行，此时 XVM 仍持有
  `xvm_lock`，且 `status` 与 `step_gate` 尚未被复位为 `RUNNING`/`HOLD`
  。回调内可调用调试与寄存器接口，但 **严禁重入 `mr_xvm_execute()`**。
- 调试单元自身无锁，`break_*`、`probe_attach`/`detach`/`set_gate` 均不获取
  `xvm_lock`，禁止与 `mr_xvm_execute()` 并发调用。

## API 速查

| 分组     | API                                                 | 说明                                                       |
|----------|-----------------------------------------------------|------------------------------------------------------------|
| 探针管理 | `mr_xvm_debug_probe_attach(xvm, entry, priv)`       | 附加探针并置门控为 `HOLD`；已有探针返回 `-MR_EBUSY`        |
| 探针管理 | `mr_xvm_debug_probe_detach(xvm)`                    | 摘除探针、打开门控并清空断点                               |
| 探针管理 | `mr_xvm_debug_probe_set_gate(xvm, gate)`            | 设置步进门控值                                             |
| 探针管理 | `mr_xvm_debug_target(xvm)`                          | 获取 ISA 的 `mr_xvm_isa_target_t` 描述（可能为 `MR_NULL`） |
| 断点管理 | `mr_xvm_debug_break_insert(xvm, addr)`              | 插入地址断点（重复幂等，表满返回 `-MR_ENOMEM`）            |
| 断点管理 | `mr_xvm_debug_break_remove(xvm, addr)`              | 移除地址断点（不存在返回 `-MR_ENOENT`）                    |
| 断点管理 | `mr_xvm_debug_break_clear(xvm)`                     | 清空全部断点与待消费的 skip 标记                           |
| 断点管理 | `mr_xvm_debug_break_skip(xvm, pc)`                  | 设置仅生效一次的指定 PC 断点跳过                           |
| 断点管理 | `mr_xvm_debug_break_hit(xvm, pc)`                   | ISA 取指前调用：检查 PC 是否命中断点                       |
| 核心挂钩 | `mr_xvm_debug_init` / `probe_enter` / `probe_leave` | 由 XVM 核心在初始化与 `mr_xvm_execute()` 前后调用          |

## 内部实现

核心实现位于 `modules/xvm/debug.c`：

```text
mr_xvm_execute() -> probe_enter() (按 step_gate 钳制 step，HOLD 则直接跳过)
                 -> isa->ops->execute() -> mr_xvm_debug_break_hit(pc)
                 -> probe_leave() (截获 EXCEPT/STEP -> 调 probe 回调 -> 置 RUNNING/HOLD -> 返回 NONE)
```

断点表采用紧凑数组存储，移除元素时用末尾条目填补空洞以保持连续扫描，表为空（
`num == 0`）时 `mr_xvm_debug_break_hit()` 直接返回 `MR_FALSE`。
