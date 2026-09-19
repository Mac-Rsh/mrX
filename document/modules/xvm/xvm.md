# 虚拟机（XVM）

> XVM（eXtensible Virtual Machine）是一个指令步进调度的可扩展虚拟机：宿主以
> `mr_xvm_execute` 按步数驱动客户机执行，客户机通过系统调用与异常事件与宿主交互，
> 指令集架构（ISA）以运行期注册的方式插拔。

| 项       | 说明                                                                                                              |
|----------|-------------------------------------------------------------------------------------------------------------------|
| 功能宏   | `MR_USE_MODULE_XVM`（RISCV32 后端为 `MR_USE_MODULE_XVM_RISCV32`）                                                 |
| 头文件   | `<mrX/modules/xvm/mr_xvm.h>`                                                                                      |
| 依赖模块 | [内存分配](../../libc/malloc.md)（仅 `mr_xvm_create` 动态方式）、[调试器](dbg.md)（可选，`MR_USE_MODULE_XVM_DEBUG`） |
| 参考测试 | `mrX/test/modules/xvm/xvm.c`                                                                                      |

---

## 用户指南

### 1. 模块概述

XVM 解决的问题是：**在 MCU 上以受控地址映射和指令解释执行一段独立二进制代码**。当前后端把客户机访问限制在其实现显式映射的镜像、RAM/MMIO 区域，并把无法由客户机 trap 处理的非法访问/指令等上报给宿主。它可以作为隔离执行机制的一部分，但当前实现并未声明或验证成面向恶意代码的安全沙箱，不能仅凭“虚拟机”这一层就假定任意不可信输入都具备安全边界。

特性：

- 二进制镜像与数据内存分开描述；当前 RISCV32 后端把镜像映射为只读、RAM 映射为可写。镜像只是宿主提供的 `const` 缓冲指针，不要求物理上一定来自 Flash。
- 步进预算调度：每次 `mr_xvm_execute` 把 `step` 交给 ISA 作为本批执行预算；对当前 RISCV32，普通指令每轮消耗一个 step，但 trap/外部中断入口也可能消耗循环预算而没有退休一条客户机指令。
- 系统调用事件：客户机通过 `ECALL` 请求宿主服务，宿主写回结果后继续执行。
- 异常事件：非法指令、访存越界、地址不对齐、除零路径等以异常号上报。
- 暂停/恢复/状态查询，运行状态原子可查。
- 通用寄存器与 PC 的读写接口（寄存器编号由 ISA 定义）。
- 数据内存按地址读写与直接映射，可用于宿主与客户机共享数据。
- 中断注入：宿主可向客户机断言/撤销外部中断（ISA 支持时）。
- ISA 可插拔：运行期按名字注册 ISA 实现，创建虚拟机时按名字选择。

不适用于：需要与宿主同地址空间、零开销调用的场景（直接函数调用更合适）；需要完整 OS 语义（进程、虚拟内存）的场景。XVM 核心本身不规定必须是哪一种 CPU 模型；当前内置 RISCV32 后端实现的是单 hart、机器模式风格的 RV32 执行环境。

当前内置后端为 RISCV32（`MR_USE_MODULE_XVM_RISCV32`，注册名 `"riscv32"`），实现 RV32I + M 扩展、机器模式 CSR 与 trap、外部中断注入。

### 2. 配置与依赖

| 配置项                         | 类型 | 默认值 | 说明                                                                    |
|--------------------------------|------|--------|-------------------------------------------------------------------------|
| `MR_USE_MODULE_XVM`            | bool | n      | 启用 XVM 核心（依赖 `MR_USE_MODULE`）                                   |
| `MR_CFG_XVM_SYSCALL_PARAM_NUM` | 整型 | 7      | 系统调用事件中的参数槽数量，范围 1~64；每槽持有指向一个 64 位参数的指针 |
| `MR_USE_MODULE_XVM_RISCV32`    | bool | n      | 启用 RISCV32 ISA 后端（依赖 `MR_USE_MODULE_XVM`），注册名为 `"riscv32"` |

启用后包含头文件即可使用：

```c
#include <mrX/modules/xvm/mr_xvm.h>
```
内置 RISCV32 不是在 `mr_xvm_init()` 时临时注册，而是通过 `MR_INIT_LEVEL_MODULE` 的 user-initcall 注册。因此正常启动需先执行 `mr_user_init()`；否则即使配置已启用，`mr_xvm_init(..., "riscv32", ...)` 仍会因 ISA 尚未进入注册表而返回 `-MR_ENOENT`。

该头文件会自动引入 ISA 抽象头 `mr_xvm_isa.h`；启用调试时再引入 `mr_xvm_dbg.h`。

内存布局约定（由 ISA 后端实现，RISCV32 如下）：

- 镜像区 `[0, image_size)`：RISCV32 `mem_map` 将其映射为只读，取指也从该镜像区完成。
- 客户机 RAM 从 `entry` 开始，容量为 `data_size - sizeof(core_t)`，即逻辑范围约为 `[entry, entry + mem_size)`；`entry >= image_size` 由 `mr_xvm_load` 的断言约束，用于避免当前镜像/RAM 布局重叠。
- `data_size` 描述的是整个 ISA 数据区。RISCV32 把开头 `sizeof(core_t)` 字节作为 CPU/CSR/中断/系统调用影子状态，后续 `core->mem[]` 才映射为客户机 RAM；最小尺寸在 `mr_xvm_load` 的 RISCV32 `load` 回调中检查，而不是在 `mr_xvm_init` 中检查。

### 3. 快速上手

最小示例：创建虚拟机、加载镜像、执行到系统调用并应答（取材自
`mrX/test/modules/xvm/xvm.c`）：

```c
#include <mrX/modules/xvm/mr_xvm.h>

static void run(void) {
    mr_uint8_t data[512];
    /* ADDI a7, x0, 1; ECALL with nr = 1 */
    mr_uint8_t image[8] = {
        0x93, 0x08, 0x10, 0x00,        /* ADDI a7, x0, 1 */
        0x73, 0x00, 0x00, 0x00,        /* ECALL          */
    };
    mr_xvm_t xvm;
    mr_xvm_event_t event = {0};
    mr_err_t ret;

    ret = mr_xvm_init(&xvm, "riscv32", data, sizeof(data));
    if (ret != 0) {
        return;
    }
    ret = mr_xvm_load(&xvm, image, sizeof(image), 0x100);
    if (ret != 0) {
        mr_xvm_del(&xvm);
        return;
    }

    for (;;) {
        mr_xvm_return_t er = mr_xvm_execute(&xvm, &event, 100);
        if (er == MR_XVM_SYSCALL) {
            /* Handle syscall: nr = event.syscall.nr, args in param[0..n] */
            *event.syscall.ret = 0;
            mr_xvm_resume(&xvm);
            continue;
        }
        break;  /* MR_XVM_NONE / MR_XVM_EXCEPT / MR_XVM_EXIT */
    }
    mr_xvm_del(&xvm);
}
```

要点：

- `"riscv32"` 是 ISA 注册名，要求 `MR_USE_MODULE_XVM_RISCV32` 已启用。
- `mr_xvm_load` 成功后虚拟机进入 `MR_XVM_STATUS_RUNNING`。当前 RISCV32 在 load 前由核心清零整个 `datamem`，因此 PC 初值为 0；`mr_xvm_load` 的 `entry` 参数在该后端表示客户机 RAM 基址，并不会写入 PC。可用 `mr_xvm_reg_store(&xvm, 32, addr)` 显式修改 PC（32 是 RISCV32 的 PC 寄存器编号）。
- `mr_xvm_execute` 返回 `MR_XVM_SYSCALL` 时虚拟机自动暂停，宿主写
  `*event.syscall.ret` 并 `mr_xvm_resume` 后，结果会在下次执行时写回客户机的 A0。

### 4. 使用说明

**生命周期**

- 静态方式：定义 `mr_xvm_t` 与 RAM 缓冲，调用 `mr_xvm_init`；用完调用 `mr_xvm_del` 归还对象的初始引用。若没有其他引用，ISA 引用等收尾会同步完成；若仍有 `mr_xvm_get()` 引用，最终反初始化延后到最后一次 `put`。
- 动态方式：`mr_xvm_create(isa_name, data_size)` 一次性分配对象与 RAM；`mr_xvm_del` 先置 DYING 并归还初始引用，动态内存只在最后一个对象引用释放时真正 `mr_free`。

**状态机**

| 状态                    | 进入条件                       | 离开方式                       |
|-------------------------|--------------------------------|--------------------------------|
| `MR_XVM_STATUS_INIT`    | `init` 完成                    | `mr_xvm_load` 成功             |
| `MR_XVM_STATUS_RUNNING` | `load` 成功、`resume` 成功     | `execute` 返回非 NONE、`pause` |
| `MR_XVM_STATUS_PAUSED`  | `pause`、系统调用返回          | `resume`                       |
| `MR_XVM_STATUS_EXCEPT`  | 未被调试 probe 隐藏的 `MR_XVM_EXCEPT` | `resume`（恢复 RUNNING）       |
| `MR_XVM_STATUS_ENDED`   | `execute` 返回 `MR_XVM_EXIT`   | 仅重新 `mr_xvm_load`           |

**执行与事件**

- `mr_xvm_execute(xvm, event, step)` 把 `step` 作为 ISA 本次执行预算。当前 RISCV32 在正常跑满预算时返回 `MR_XVM_NONE`，`event->none.step` 是其执行循环完成的 step 数；外部中断/trap 入口可占用一次循环预算，因此它不应被泛化为严格的“退休指令数”。
- 返回 `MR_XVM_SYSCALL`：`event->syscall.nr` 为系统调用号（RISCV32 取自 A7），
  `event->syscall.param[i]` 指向第 i 个 64 位参数（RISCV32 取自 A0~A6，
  `MR_CFG_XVM_SYSCALL_PARAM_NUM` 超过 7 的槽位为无效填充），`event->syscall.ret`
  指向返回值槽。系统调用号 0 被 ISA 约定为"结束"，返回 `MR_XVM_EXIT`。
- 返回 `MR_XVM_EXCEPT`：`event->except.nr` 为异常号（`BREAK`/`OVERFLOW`/`ILLEGAL`/`ACCESS`/`ALIGN`/`DIV0`）。当前 RISCV32 对多数取指/执行异常会先尝试进入客户机 trap；`mtvec` 无有效向量时才上报宿主。`BREAK` 是例外：当前后端显式跳过 trap 进入并直接向宿主上报。启用 debug probe 后，[调试器](dbg.md) 当前还会把所有 `MR_XVM_EXCEPT` 当作调试停机回收成 `MR_XVM_NONE`。
- 返回 `MR_XVM_EXIT`：客户机请求退出，此后 `execute` 不再执行指令。

**运行期行为约定**

| 操作                                    | 行为                                                 |
|-----------------------------------------|------------------------------------------------------|
| 非 RUNNING 状态调用 `mr_xvm_execute`    | 不执行，`event->none.step = 0`，返回 `MR_XVM_NONE`   |
| RUNNING 状态调用 `mr_xvm_pause`         | 切换到 PAUSED；其他状态调用无效果（仍返回 0）        |
| PAUSED/EXCEPT 调用 `mr_xvm_resume`      | 恢复 RUNNING；其他状态返回 `-MR_EPERM`               |
| 系统调用后不写 `*ret` 直接 `resume`     | 合法，A0 收到的是影子寄存器的旧值                    |
| `mr_xvm_load` 成功重复调用              | 清零整个 datamem、重建 ISA 状态、清空调试断点并置 RUNNING；失败不回滚已清零内存/镜像描述符 |
| INIT/ENDED 状态调用 `mr_xvm_interrupt`  | 返回 `-MR_EPERM`                                     |
| 同一时刻两个上下文调用 `execute`/`load` | 共享同一 `xvm_lock`：后到的 `load` 返回 `-MR_EBUSY`；后到的 `execute` 把忙翻译为 `MR_XVM_NONE` 且 step=0 |

**数据内存访问**：`mr_xvm_datamem_load`/`mr_xvm_datamem_store` 按客户机地址读写， 会正确落在镜像区（只读，store 返回
`-MR_EPERM`）或 RAM 区；`mr_xvm_datamem_map`
直接给出宿主侧指针，用于零拷贝共享，跨区访问需按返回的映射长度分段。

### 5. API 参考

初始化与销毁：

| API             | 说明                                     |
|-----------------|------------------------------------------|
| `mr_xvm_init`   | 初始化静态虚拟机（调用者提供 RAM 缓冲）  |
| `mr_xvm_create` | 堆上创建虚拟机（对象与 RAM 一次分配）    |
| `mr_xvm_del`    | 删除虚拟机（静态反初始化，动态释放内存） |

执行与控制：

| API                | 说明                                         |
|--------------------|----------------------------------------------|
| `mr_xvm_load`      | 加载二进制镜像并复位进入 RUNNING             |
| `mr_xvm_execute`   | 执行最多 step 条指令，返回停机原因并填充事件 |
| `mr_xvm_pause`     | 暂停（仅 RUNNING 状态生效）                  |
| `mr_xvm_resume`    | 从 PAUSED/EXCEPT 恢复                        |
| `mr_xvm_status`    | 查询当前状态                                 |
| `mr_xvm_interrupt` | 断言/撤销外部中断（ISA 支持时）              |

寄存器与数据内存：

| API                             | 说明                                             |
|---------------------------------|--------------------------------------------------|
| `mr_xvm_reg_load`               | 读一个寄存器（编号含义由 ISA 定义）              |
| `mr_xvm_reg_store`              | 写一个寄存器                                     |
| `mr_xvm_datamem_load`           | 从客户机地址空间读数据                           |
| `mr_xvm_datamem_store`          | 向客户机地址空间写数据（只读区返回 `-MR_EPERM`） |
| `mr_xvm_datamem_map`            | 映射客户机地址到宿主指针，并给出可写属性         |
| `MR_XVM_DATAMEM` / `MR_XVM_BIN` | 宏：取数据内存/镜像描述符                        |

引用管理：

| API                         | 说明                                |
|-----------------------------|-------------------------------------|
| `mr_xvm_get` / `mr_xvm_put` | 引用计数加一 / 减一（对接对象系统） |

**`mr_xvm_init`**

```c
mr_err_t mr_xvm_init(mr_xvm_t *xvm, const char *isa_name, void *data,
                     mr_size_t data_size);
```

- `xvm`：虚拟机对象，由调用者分配存储。
- `isa_name`：ISA 注册名（内置后端为 `"riscv32"`）；未注册时返回 `-MR_ENOENT`。
- `data`：ISA 数据区缓冲，不可为 `MR_NULL`；核心层只断言 `data_size > 0`。是否足以容纳 ISA 私有核心状态由具体后端在 `load` 等阶段验证；RISCV32 在 `mr_xvm_load` 时检查 `data_size >= sizeof(core_t)`。
- 返回：成功 `0`，失败负数错误码。

**`mr_xvm_create`**

```c
mr_xvm_t *mr_xvm_create(const char *isa_name, mr_size_t data_size);
```

一次性分配 `sizeof(mr_xvm_t) + data_size`，RAM 位于对象之后。成功返回虚拟机指针， 内存不足或初始化失败返回 `MR_NULL`。

**`mr_xvm_del`**

```c
mr_err_t mr_xvm_del(mr_xvm_t *xvm);
```

调用时把 `MR_XVM_FLAG_DYING` 置位并通过对象系统归还初始引用。若这是最后引用，release 会归还绑定 ISA 引用，动态对象随后释放内存；若仍有 `mr_xvm_get()` 引用，最终释放延后。**`execute`/`load` 本身不会自动 get XVM**，因此要让操作跨越另一个上下文可能调用 `mr_xvm_del` 的生命周期边界，调用方必须预先持有对象引用或显式串行化删除。仅有 DYING 标记不足以防止无引用的在途调用发生生命周期竞态。

**`mr_xvm_load`**

```c
mr_err_t mr_xvm_load(mr_xvm_t *xvm, const void *image, mr_size_t image_size,
                     mr_uint64_t entry);
```

- `image`：二进制镜像（flash），宿主需保证其生命周期覆盖虚拟机运行期，XVM 不拷贝。
- `entry`：由 ISA 解释的镜像相关地址参数；当前 RISCV32 把它作为客户机 RAM 基址，而不是 PC 入口。核心断言 `entry >= image_size`；当前 RISCV32 的 PC 因 load 前清零 datamem 而从 0 开始。
- 行为：清零 RAM、调用 ISA 的 load 复位核心、清除全部断点、状态置 RUNNING。
- 返回：成功 `0`；RAM 不足以容纳核心状态时 RISCV32 返回 `-MR_ENOMEM`。

**`mr_xvm_execute`**

```c
mr_xvm_return_t mr_xvm_execute(mr_xvm_t *xvm, mr_xvm_event_t *event,
                               mr_uint32_t step);
```

- `event`：事件输出参数，不可为 `MR_NULL`。普通 `MR_XVM_NONE` 路径填 `none.step`，SYSCALL/EXCEPT 由 ISA 填对应成员。DYING 导致的早退直接返回 `MR_XVM_EXIT`，不会额外填充 `event`。
- `step`：本次调用交给 ISA 的最大执行预算，不保证等价于退休指令数。
- 返回：`MR_XVM_NONE`/`SYSCALL`/`EXCEPT`/`EXIT`。xvm_lock 因 DYING 失败时返回 `MR_XVM_EXIT`；因已有执行者而忙时返回 `MR_XVM_NONE` 且 `event->none.step = 0`。启用 debug probe 后，异常/单步还可能在返回前被调试层改写为 `MR_XVM_NONE`。

**`mr_xvm_pause` / `mr_xvm_resume` / `mr_xvm_status`**

```c
mr_err_t mr_xvm_pause(mr_xvm_t *xvm);
mr_err_t mr_xvm_resume(mr_xvm_t *xvm);
mr_xvm_status_t mr_xvm_status(mr_xvm_t *xvm);
```

`pause` 尝试以 CAS 把 RUNNING 改为 PAUSED；若状态本来不是 RUNNING 或并发时 CAS 失败，函数仍返回 0。`resume` 先检查状态必须为 PAUSED/EXCEPT，再以 CAS 尝试改为 RUNNING；当前实现同样不检查最终 CAS 是否成功，因此这些接口提供原子字段更新，但不应被理解为与其他并发状态迁移完全线性化的控制事务。

**`mr_xvm_reg_load` / `mr_xvm_reg_store`**

```c
mr_err_t mr_xvm_reg_load(mr_xvm_t *xvm, mr_uint32_t nr, mr_uint64_t *val);
mr_err_t mr_xvm_reg_store(mr_xvm_t *xvm, mr_uint32_t nr, mr_uint64_t val);
```

寄存器编号由 ISA 定义：RISCV32 中 0~31 为 x0~x31（x0 恒 0，写入被忽略），32 为 PC， 其余编号返回 `-MR_EINVAL`。注意 CSR
不在寄存器表内，只能由客户机代码访问。

**`mr_xvm_datamem_load` / `mr_xvm_datamem_store` / `mr_xvm_datamem_map`**

```c
mr_ssize_t mr_xvm_datamem_load(mr_xvm_t *xvm, mr_uint64_t addr, void *datamem,
                               mr_size_t size);
mr_ssize_t mr_xvm_datamem_store(mr_xvm_t *xvm, mr_uint64_t addr,
                                const void *datamem, mr_size_t size);
mr_ssize_t mr_xvm_datamem_map(mr_xvm_t *xvm, mr_uint64_t addr, void **datamem,
                              mr_bool_t *writable);
```

地址不落在任何映射区时返回 `-MR_EINVAL`；落区时传输长度被截断到区末尾，返回实际 字节数。`store` 遇到只读区（镜像）返回
`-MR_EPERM`。

**`mr_xvm_interrupt`**

```c
mr_err_t mr_xvm_interrupt(mr_xvm_t *xvm, mr_uint32_t irq, mr_bool_t active);
```

INIT/ENDED 状态返回 `-MR_EPERM`；ISA 未实现中断返回 `-MR_ENOSYS`；RISCV32 中
`irq` 为 0~31 的位编号，否则返回 `-MR_EINVAL`。中断是否被客户机响应还取决于客户机 对 `mstatus.MIE`、`mie.MEIE` 的配置。

**`mr_xvm_get` / `mr_xvm_put`**

```c
mr_xvm_t *mr_xvm_get(mr_xvm_t *xvm);
mr_bool_t mr_xvm_put(mr_xvm_t *xvm);
```

跨模块或跨线程保存虚拟机指针、尤其可能与 `mr_xvm_del` 并发时，应先 `get` 增加引用，用完 `put` 归还；最后一个引用释放时才到达对象生命周期终点。`mr_xvm_execute`/`load` 不会替调用者自动保活。

### 6. 完整示例

示例 1：共享内存往返——宿主通过数据内存接口与客户机交换数据（改编自
`mrX/test/modules/xvm/xvm.c` 的 `memory_permissions_and_bounds`）：

```c
mr_uint8_t data[512];
mr_uint8_t image[4] = {0x13, 0x00, 0x00, 0x00};    /* NOP */
mr_uint8_t source[8] = {1, 2, 3, 4, 5, 6, 7, 8};
mr_uint8_t target[8] = {0};
mr_xvm_t xvm;

mr_xvm_init(&xvm, "riscv32", data, sizeof(data));
mr_xvm_load(&xvm, image, sizeof(image), 0x100);

/* RAM region at 0x100 is writable */
mr_xvm_datamem_store(&xvm, 0x100, source, sizeof(source));
mr_xvm_datamem_load(&xvm, 0x100, target, sizeof(target));

/* Image region at 0 is read-only: returns -MR_EPERM */
mr_xvm_datamem_store(&xvm, 0, source, sizeof(source));

mr_xvm_del(&xvm);
```

示例 2：异常处理——非法指令触发异常后恢复或终止（改编自
`exception_pause_resume_and_interrupt`）：

```c
mr_uint8_t image[4] = {0xff, 0xff, 0xff, 0xff};    /* Illegal instruction */
mr_xvm_event_t event = {0};

test_xvm_prepare(&xvm, data, image, sizeof(image));
mr_xvm_interrupt(&xvm, 1, MR_TRUE);                /* Assert irq 1 */

if (mr_xvm_execute(&xvm, &event, 1) == MR_XVM_EXCEPT) {
    /* event.except.nr == MR_XVM_EXCEPT_NR_ILLEGAL here */
    switch (event.except.nr) {
        case MR_XVM_EXCEPT_NR_ILLEGAL:
            mr_xvm_del(&xvm);                      /* Terminate */
            break;
        default:
            mr_xvm_resume(&xvm);                   /* Clear and continue */
            break;
    }
}
```

更多行为参考（指令集覆盖、trap/中断/MMIO 语义）见 `mrX/test/modules/xvm/xvm.c`。

### 7. 常见问题（FAQ）

**Q：`mr_xvm_init` 返回 `-MR_ENOENT`？**

ISA 名字当前不在注册表。内置 RISCV32 除了要启用 `MR_USE_MODULE_XVM_RISCV32`，还必须已经执行到 `MR_INIT_LEVEL_MODULE` user-initcall（正常路径即先调用 `mr_user_init()`）；自定义 ISA 则需确认已经调用 `mr_xvm_isa_register` 且注册未失败。

**Q：`mr_xvm_execute` 一直返回 `MR_XVM_NONE` 且 step 为 0？**

虚拟机不在 RUNNING 状态：检查是否忘记 `mr_xvm_load`、是否被 `pause` 后未 `resume`、 或上一次异常后未处理。

**Q：客户机写入的数据丢了？**

写入地址落在镜像区（只读）。RISCV32 的 RAM 区从 `entry` 开始，镜像区从 0 开始；
`mr_xvm_datamem_store` 对镜像区返回 `-MR_EPERM` 而不是静默丢弃。

**Q：系统调用参数最多能传几个？**

RISCV32 寄存器约定为 A0~A6 共 7 个；`MR_CFG_XVM_SYSCALL_PARAM_NUM` 增大只会增加 无效填充槽，不会带来更多寄存器参数。

**Q：异常号 `MR_XVM_EXCEPT_NR_DIV0` 什么时候出现？**

RISCV32 的除零按 RISC-V 规范返回定义值而不触发异常，因此当前后端不会上报该异常号； 它预留给除零会陷入的 ISA 后端。

**Q：`execute` 返回 `MR_XVM_EXIT` 后还能继续跑吗？**

不能。ENDED 是终态，重新执行需要 `mr_xvm_load` 复位。

**Q：静态和动态方式怎么选？**

RAM 位置固定（如片上 SRAM 的一段）用静态对象 + 自备缓冲；数量运行期才确定用
`mr_xvm_create`。动态方式 RAM 与对象一体分配，注意堆要够大。

---

## 开发者指南

### 8. 架构总览

XVM 由核心调度层（xvm.c）、ISA 注册表（isa.c）与 ISA 后端（isa/riscv32）三层组成：

```
        +--------------------------------------+
        |  宿主应用                             |
        +------------------+-------------------+
                           | mr_xvm_execute() / load / reg / datamem
        +------------------v-------------------+
        |  XVM 核心 xvm.c                      |
        |  状态机 + 单执行者锁 + 事件封装        |<--- mr_xvm_dbg_*（可选，dbg.c）
        +----------+---------------------------+
                   | isa->ops->{load,execute,mem_map,reg_*}
        +----------v---------------------------+
        |  ISA 注册表 isa.c                    |----> 按名字查找 + 引用计数
        +----------+---------------------------+
                   | "riscv32"
        +----------v---------------------------+
        |  RISCV32 后端 riscv32.c              |
        |  取指/译码/执行 + CSR/trap/MMIO       |
        +--------------------------------------+
             依赖：对象系统（生命周期/引用计数）、自旋锁（ISA 表）、
                   原子操作（状态/中断挂起）、malloc（仅动态创建）
```

关键接线：

- XVM 对象内嵌调试单元 `mr_xvm_dbg_t`（`mr_xvm.h:111`），`execute` 在 ISA 前后 调用 probe 门控挂钩（
  `mrX/modules/xvm/xvm.c:212`、`xvm.c:235`），断点命中由 ISA 在取指前上报。调试机制见[调试器](dbg.md)。
- RISCV32 后端通过 `MR_INIT_EXPORT(..., MR_INIT_LEVEL_MODULE)` 注册为 user-initcall（`mrX/modules/xvm/isa/riscv32/riscv32.c:1457-1460`）；执行 `mr_user_init()` 后才会真正调用 `mr_xvm_isa_register`。

### 9. 数据结构

虚拟机对象（`mrX/include/mrX/modules/xvm/mr_xvm.h:103`）：

```c
typedef struct mr_xvm {
    mr_object_t parent;         /* Object base: clazz + reference count */
    struct mr_xvm_isa *isa;     /* Bound ISA implementation (held reference) */
    mr_xvm_datamem_t datamem;   /* RAM: data pointer + size */
    mr_xvm_binary_t bin;        /* Flash image: pointer + entry + size */
    mr_atomic_t status;         /* mr_xvm_status_t */
    mr_atomic_t flags;          /* bit0 = LOCK, bit1 = DYING */
    mr_xvm_dbg_t dbg;           /* Debug unit (MR_USE_MODULE_XVM_DEBUG) */
} mr_xvm_t;
```

| 字段      | 说明                                                                                                   |
|-----------|--------------------------------------------------------------------------------------------------------|
| `parent`  | 对象基类，静态/动态双 clazz（`MR_CLAZZES_DEFINE`，`xvm.c:17`）决定 `mr_xvm_del` 的收尾方式             |
| `isa`     | 绑定的 ISA 实现；`xvm_init` 中 `mr_xvm_isa_find` 成功后持有一个引用，对象释放时归还（`xvm.c:309-321`） |
| `datamem` | RAM 缓冲与大小；动态创建时指向对象尾部（`xvm.c:90`）                                                   |
| `bin`     | 镜像描述符；`image` 由宿主持有，XVM 不拷贝                                                             |
| `status`  | 原子状态机，`load`/`execute`/`pause`/`resume` 通过原子写或 CAS 迁移                                    |
| `flags`   | bit0 `MR_XVM_FLAG_LOCK` 单执行者锁，bit1 `MR_XVM_FLAG_DYING` 删除标记                                  |

事件联合（`mr_xvm.h:82`）按返回类型解释：`none.step`（ISA 报告的 step 数）、`syscall`（nr + 参数槽指针 + 返回值槽指针）、`except.nr`（异常号）。RISCV32 的 `syscall.param[]`/`ret` 指向 `core_t` 内的影子存储：宿主要让返回值生效，必须在下一次 `mr_xvm_execute` 的 `syscall_writeback` 前写 `*ret`；这些指针还依赖 XVM/当前 load 的生命周期，不能在重载或最终删除后继续保存。

ISA 抽象（`mr_xvm_isa.h:69`）：`name`（注册名）、`next`（注册表链）、`ops`（六个 操作：`load`/`execute`/`mem_map`/`reg_load`/
`reg_store`/`interrupt`）、`target`
（面向调试器的目标描述：架构名、feature 名与寄存器表，`MR_USE_MODULE_XVM_DEBUG`
下 RISCV32 才提供，`riscv32.c:34-80`）。

### 10. 核心流程

**创建与绑定**：`xvm_init`（`xvm.c:19`）先 `mr_xvm_isa_find(isa_name)` 查表并持有 引用（找不到返回 `-MR_ENOENT`
），随后初始化状态/flags/内存/镜像字段并在启用调试时 调用 `mr_xvm_dbg_init`，最后 `mr_object_init`；对象初始化失败要回滚 ISA
引用。

**加载（load）**：`mr_xvm_load`（`xvm.c:140`）在单执行者锁内完成：清零 RAM → 记录镜像描述符 → 调用 `isa->ops->load`（RISCV32 在
RAM 头部构造
`core_t`，校验 `data_size >= sizeof(core_t)`，见 `riscv32.c:97-117`）→ 清空调试 断点 → 状态置 RUNNING。失败路径会解锁，但 RAM
清零与镜像描述符更新不会回滚。

**执行（execute）**：`mr_xvm_execute`（`xvm.c:181`）：

1. `xvm_lock`（`xvm.c:112`）用 CAS 循环把 `MR_XVM_FLAG_LOCK` 置位：dying 返回
   `-MR_ENOENT`（上层翻译成 `MR_XVM_EXIT`），已被锁返回 `-MR_EBUSY`（上层返回 step=0 的 `MR_XVM_NONE`
   ）。这保证同一时刻只有一个上下文执行客户机代码。
2. 状态非 RUNNING 直接空转返回。
3. 启用调试时 `mr_xvm_dbg_probe_enter` 按门控钳制 step（可能整体跳过 ISA）。
4. 调用 `isa->ops->execute`；按返回值迁移状态：SYSCALL→PAUSED、EXCEPT→EXCEPT、 EXIT→ENDED、NONE 保持 RUNNING（
   `xvm.c:220-231`）。
5. 启用调试时 `mr_xvm_dbg_probe_leave` 可能把调试停机"隐藏"为 NONE 并恢复 RUNNING；最后解锁返回。

**RISCV32 执行主循环**：`xvm_riscv32_execute`（`riscv32.c:1263`）先在进入 step 循环前执行一次 `syscall_writeback`，因此即使本次 `step == 0` 也能把上次系统调用返回值写回 A0。随后每个循环预算依次检查外部中断（满足 MIE/MEIE 且有挂起中断时进入 trap 并 `continue`）→ 调试断点 → 取指 → 译码 → 执行。普通成功指令才递增 `mcycle/minstret`；外部中断或被客户机 trap 吸收的异常可能消耗一次循环预算但不退休普通指令。`trap_enter_cause` 保存 mepc/mcause/mtval、维护 mstatus 并跳转 mtvec；无有效向量时多数异常直接上报宿主，而 BREAK 当前刻意不进入 guest trap。

**系统调用**：`ECALL` 执行后 `syscall_handle`（`riscv32.c:1237`）把 A7 装入
`event->syscall.nr`（0 号约定为 EXIT）、A0 的影子地址作为 `ret`、A0~A6 影子地址填 入 `param[]`，返回 `MR_XVM_SYSCALL`；宿主写
`*ret` 并 `resume` 后，下一次
`execute` 开头的 `syscall_writeback` 把影子值送回 A0。

**数据内存**：三个 datamem API 都是 `isa->ops->mem_map`（`riscv32.c:1349`）的封装： RISCV32 把地址翻译为镜像区（只读，返回剩余长度）或
RAM 区（读写），核心层据此做 拷贝与权限检查。跨区分段由调用者按返回长度推进。

**删除**：`mr_xvm_del`（`xvm.c:99-110`）置 DYING 后归还对象初始引用；最终 release 才 `mr_xvm_isa_put`，动态对象再 `mr_free`。DYING 会阻止**后续** `xvm_lock` 成功，但不能自动保活已经在途的 `execute/load`；这些入口没有内部 `mr_xvm_get`。因此生命周期并发必须由调用者用对象引用或外部串行化解决，不能把 DYING 当作完整的删除互斥。

### 11. 并发与中断安全

- `flags` bit0 的 CAS 锁只串行化 `mr_xvm_load` 与 `mr_xvm_execute`；后到的 load 得 `-MR_EBUSY`，后到的 execute 得零步 NONE。它不保护 reg/datamem/debug/interrupt 等其他 API，也不提供对象生命周期引用。
- `mr_xvm_execute` 在调用者上下文同步运行整个 ISA 批次；核心没有主动阻塞或切换线程，但一次批次耗时取决于 `step` 和后端，因此是否允许在 ISR 中调用应由平台时延预算决定，文档不把它声明为 ISR-safe。
- `status` 字段使用原子操作，但 `pause/resume` 的 CAS 结果没有完全反馈给调用者；“原子字段访问”不等价于所有并发控制序列都具备强事务语义。`mr_xvm_interrupt` 也直接调用 ISA 后端而不取 xvm_lock：当前 RISCV32 的 `intr.active` 用原子位操作，可与执行侧读取配合，但自定义 ISA 是否支持同样并发由后端负责。
- `reg_load`/`reg_store`/`datamem_*` 不加 xvm_lock：应在虚拟机暂停/门控停止并确认没有在途执行时，或由外部串行化后调用。仅把 gate 写成 HOLD 并不能异步抢占已经进入 ISA 的执行批次。
- ISA 注册表由自旋锁 `__xvm_isa_lock`（`isa.c:19`）保护，`register`/`unregister`/
  `find` 可并发；注册即持有引用，`find` 命中也会持有引用，调用方用完必须 `put`。 已注册的 ISA 不能 `del`（`isa.c:73-86` 返回
  `-MR_EBUSY`）。
- 公开入口大量依赖 `MR_ASSERT` 做对象类型/指针/必需 ops 前置条件检查；例如 load/execute/reg/mem_map 的必需回调缺失属于断言前置条件，而 `interrupt` 是显式可选槽，缺失时返回 `-MR_ENOSYS`。

### 12. 扩展与派生

XVM 的扩展点是 ISA 后端而不是对象派生。实现一个新 ISA：

1. 定义 `mr_xvm_isa_ops_t`：至少提供 `load`/`execute`/`mem_map`，`reg_load`/
   `reg_store` 供调试与 GDB 使用，`interrupt` 可选（缺席时 `mr_xvm_interrupt`
   返回 `-MR_ENOSYS`）。
2. 用 `MR_XVM_ISA_INIT(name, ops, target)` 静态初始化 `mr_xvm_isa_t`（或运行期
   `mr_xvm_isa_init`），调用 `mr_xvm_isa_register` 注册；重名返回 `-MR_EEXIST`。
3. `load` 复位核心状态；`execute` 按步执行，异常以负的 `MR_XVM_EXCEPT_NR_*` 值 内部传递并翻译为事件；`mem_map` 负责地址翻译与权限。
4. 若希望被[调试器](dbg.md)/[GDB 桩](gdb.md)支持：启用 `MR_USE_MODULE_XVM_DEBUG`
   并提供 `target`（寄存器表，编号与 `reg_load`/`reg_store` 一致，含名为 `"pc"`
   的代码指针寄存器）；在取指前调用 `mr_xvm_dbg_break_hit(xvm, pc)` 实现断点 （参考 `riscv32.c:1289-1295`）。
5. 若采用与 RISCV32 相同的自动注册方式，可用 `MR_INIT_EXPORT(..., MR_INIT_LEVEL_MODULE)` 注册 user-initcall；它会在 `mr_user_init()` 的模块级初始化阶段调用注册函数，而不是在 `mr_xvm_init()` 内自动发现。

RISCV32 后端即按此模式实现（`riscv32.c:26-82`），可作为模板参考。

### 13. 源码导读

| 位置                                             | 职责                                                                               |
|--------------------------------------------------|------------------------------------------------------------------------------------|
| `mrX/include/mrX/modules/xvm/mr_xvm.h`           | 对象/状态机/事件类型与核心 API 声明，inline 实现 pause/resume/status/reg/interrupt |
| `mrX/include/mrX/modules/xvm/mr_xvm_isa.h`       | ISA 抽象：ops 表、target 描述、注册表 API                                          |
| `mrX/modules/xvm/xvm.c:19`                       | 创建公共路径（查 ISA、初始化、对象注册）                                           |
| `mrX/modules/xvm/xvm.c:112`                      | 单执行者锁（CAS）与 dying 检查                                                     |
| `mrX/modules/xvm/xvm.c:181`                      | execute 主流程（锁 → 门控 → ISA → 状态迁移）                                       |
| `mrX/modules/xvm/xvm.c:243`                      | datamem 读写/映射（mem_map 封装）                                                  |
| `mrX/modules/xvm/isa.c:88`                       | ISA 表插入/删除/查找（自旋锁 + 引用计数）                                          |
| `mrX/modules/xvm/isa/riscv32/riscv32.c:1263`     | RISCV32 执行主循环（中断 → 断点 → 取指 → 译码 → 执行）                             |
| `mrX/modules/xvm/isa/riscv32/riscv32.c:1237`     | 系统调用事件封装（A7/A0~A6/EXIT 约定）                                             |
| `mrX/modules/xvm/isa/riscv32/riscv32.c:130`      | trap 进入（mstatus 维护、向量跳转）                                                |
| `mrX/include/mrX/modules/xvm/isa/riscv32/core.h` | 核心状态 `core_t`、CSR/地址宏、RAM 布局（flexible array member）                   |
| `mrX/include/mrX/modules/xvm/isa/riscv32/mmio.h` | MMIO 窗口（0x40000000 起）与中断控制器寄存器                                       |
| `mrX/test/modules/xvm/xvm.c`                     | 全部用户可见行为的行为级测试                                                       |

### 14. 附录

**配置宏**

| 宏                                       | 说明                                         |
|------------------------------------------|----------------------------------------------|
| `MR_USE_MODULE_XVM`                      | 模块开关，默认 n，依赖 `MR_USE_MODULE`       |
| `MR_CFG_XVM_SYSCALL_PARAM_NUM`           | 系统调用参数槽数，默认 7，范围 1~64          |
| `MR_USE_MODULE_XVM_RISCV32`              | RISCV32 后端开关，默认 n，注册名 `"riscv32"` |
| `MR_XVM_FLAG_LOCK` / `MR_XVM_FLAG_DYING` | 内部 flags 位（单执行者锁/删除标记）         |
| `MR_XVM_DATAMEM` / `MR_XVM_BIN`          | 取数据内存/镜像描述符的宏                    |

**状态与事件**

| 名称                                                        | 说明                        |
|-------------------------------------------------------------|-----------------------------|
| `MR_XVM_STATUS_INIT/PAUSED/RUNNING/EXCEPT/ENDED`            | 虚拟机状态机五态            |
| `MR_XVM_NONE/SYSCALL/EXCEPT/EXIT`                           | `mr_xvm_execute` 的四种返回 |
| `MR_XVM_EXCEPT_NR_BREAK/OVERFLOW/ILLEGAL/ACCESS/ALIGN/DIV0` | 异常号                      |

**错误码**：本模块 API 失败返回负数错误码（见
`mrX/include/mrX/libc/mr_errno.h`），常见：`-MR_ENOENT`（ISA 未注册）、
`-MR_EINVAL`（寄存器号/地址非法）、`-MR_EPERM`（只读区写入、状态不允许）、
`-MR_ENOMEM`（RISCV32 load 时数据区不足以容纳 `core_t`）、`-MR_ENOSYS`（ISA 未实现中断）。内置 ISA 尚未执行 user-initcall 时按未注册处理为 `-MR_ENOENT`。参数非法（空指针、`entry < image_size`）主要依赖断言开启时的 `MR_ASSERT` 前置条件检查。
