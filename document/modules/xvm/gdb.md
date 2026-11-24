# GDB 桩（Gdb）

XVM 的 GDB 远程串行协议（RSP）适配器。宿主仅需注入字节收发回调，桩将标准 GDB
命令映射到底层 [调试器](dbg.md) 与 [虚拟机](xvm.md) 接口，支持单线程远程调试。

| 项目   | 说明                                                                                           |
|--------|------------------------------------------------------------------------------------------------|
| 功能宏 | `MR_USE_MODULE_XVM_GDB`（依赖 `MR_USE_MODULE_XVM_DEBUG`）                                      |
| 头文件 | `<mrX/modules/xvm/mr_xvm_gdb.h>`（自动引入 `mr_xvm.h`）                                        |
| 依赖   | [虚拟机](xvm.md)、[调试器](dbg.md)、[内存分配](../../libc/malloc.md)（仅 `mr_xvm_gdb_create`） |
| 测试   | `test/modules/xvm/gdb.c`                                                                       |

## 配置

| 配置项                       | 类型 | 默认值 | 说明                                                                      |
|------------------------------|------|--------|---------------------------------------------------------------------------|
| `MR_USE_MODULE_XVM_GDB`      | bool | n      | 启用 GDB RSP 适配器                                                       |
| `MR_CFG_XVM_GDB_PACKET_SIZE` | int  | 512    | 接收包缓冲字节数，范围 272~8192；作为 `PacketSize` 上报（受发送缓冲限制） |
| `MR_CFG_XVM_GDB_REPLY_SIZE`  | int  | 512    | 发送应答缓冲字节数（含 `$` / `#xx` 框架），范围 272~8192                  |

下限 272 字节用于保证容纳 GDB 首个 `qSupported` 查询以及 RV32 的 `g`/`G` 全寄存器包。

## 快速上手

```c
#include <mrX/modules/xvm/mr_xvm_gdb.h>

static mr_xvm_gdb_t gdb;

static mr_ssize_t gdb_send(void *priv, const void *buf, mr_size_t len) {
    /* 返回 0..len 表示已接受字节数；返回 0 表示忙（保留未发送部分等待 flush），负数表示错误 */
    return transport_write(priv, buf, len);
}

static const mr_xvm_gdb_ops_t gdb_ops = {
    .send = gdb_send,
    .flush = MR_NULL,
    .closed = MR_NULL,
};

mr_err_t gdb_start(mr_xvm_t *xvm, void *transport) {
    mr_err_t ret = mr_xvm_gdb_init(&gdb, &gdb_ops, transport);
    if (ret != 0) {
        return ret;
    }
    ret = mr_xvm_gdb_attach(&gdb, xvm); /* 挂载探针并置 HOLD，本身不发停止包 */
    if (ret != 0) {
        (void)mr_xvm_gdb_del(&gdb);
    }
    return ret;
}

/* 宿主主循环：喂入字节、续传挂起应答，并独立驱动 mr_xvm_execute() */
mr_ssize_t gdb_poll(const void *rx_buf, mr_size_t rx_len) {
    mr_ssize_t consumed = mr_xvm_gdb_recv(&gdb, rx_buf, rx_len);
    if (consumed < 0) {
        return consumed;
    }
    if (mr_xvm_gdb_want_send(&gdb)) {
        (void)mr_xvm_gdb_flush(&gdb);
    }
    return consumed;
}
```

## 核心机制与约束

### 会话状态与 RSP 命令支持

| 会话状态              | 含义                   | 触发迁移                                           |
|-----------------------|------------------------|----------------------------------------------------|
| `MR_XVM_GDB_DETACHED` | 未连接任何 XVM         | `init`、`detach`、`del`                            |
| `MR_XVM_GDB_ATTACHED` | 已连接且目标处于停机态 | `attach`、调试停机、Ctrl-C（`0x03`）               |
| `MR_XVM_GDB_RUNNING`  | 已放行执行             | 受理 `c`/`s`/`vCont` 命令                          |
| `MR_XVM_GDB_CLOSED`   | 会话已关闭             | 收到 `D` 或 `k`；此后 `recv` 返回 `0` 不再消费输入 |

| RSP 命令                                                          | 应答                 | 说明                                                                    |
|-------------------------------------------------------------------|----------------------|-------------------------------------------------------------------------|
| `?`                                                               | `S05` / `W00`        | 未结束固定回 `S05`，`ENDED` 状态回 `W00`                                |
| `g` / `G`、`p` / `P`                                              | hex / `OK` / `E01`   | 读写全部或单个寄存器（小端 hex，顺序由 ISA `target` 决定）              |
| `m` / `M`                                                         | hex / `OK` / `E01`   | 经 `mem_map` 读写内存（只读区返回 `E01`；单次 `m` 不能超过 reply 缓冲） |
| `Z0/Z1` / `z0/z1`                                                 | `OK` / `E01` / 空    | 插入/移除地址断点（`Z0`/`Z1` 共用断点表）                               |
| `c` / `s` / `vCont;c` / `vCont;s`                                 | 静默，停机后发 `Snn` | 自动 skip 当前 PC 断点后开门；要求 ISA `target` 含 `"pc"` 寄存器        |
| `qSupported` / `qXfer:features:read:target.xml`                   | 能力串 / XML 分片    | 自动向 GDB 上报 `riscv:rv32` 等寄存器布局 XML                           |
| `QStartNoAckMode`                                                 | `OK`                 | 关闭 `+`/`-` ACK 应答                                                   |
| `qC` / `qfThreadInfo` / `qsThreadInfo` / `qAttached` / `H` / `T1` | 单线程固定应答       | 仅支持单线程模型（thread 1），不支持多核、文件 I/O（`F`）及 `vRun`      |
| `0x03`（Ctrl-C）                                                  | `S02`                | 已附加时置 `HOLD` 并上报 `S02`；未附加时静默                            |
| `D` / `k`                                                         | `OK` / 无            | `detach` XVM（不销毁 XVM），置 `CLOSED` 并触发 `ops->closed`            |

### 传输契约与并发约束

- **短写与续传**：`ops->send()` 为必需回调；返回正数短写时桩在同一次发送尝试中继续推进，返回
  `0` 时将剩余数据留在 `reply` 缓冲中待 `mr_xvm_gdb_flush()` 续传，返回负数时内部转换为
  `-MR_EIO`（仅显式 `flush` 会返回该错误码，可通过 `mr_xvm_gdb_want_send()`
  检测并重试）。
- **无锁串行化**：GDB 会话与调试单元均无内部锁，`mr_xvm_gdb_recv()`、
  `mr_xvm_gdb_flush()`、`attach`/`detach` 以及 `mr_xvm_execute()` 必须由宿主串行驱动；
  `0x03`（Ctrl-C）仅通过设置 `HOLD` 门控生效，不能在多线程下异步抢占正在执行中的
  `mr_xvm_execute()` 批次。
- **回调上下文**：命令应答的 `send`/`flush` 运行在 `recv`/`flush`
  调用者上下文中，而断点/单步停机包 `Snn` 通过调试探针在 `mr_xvm_execute()`
  上下文中同步发送。

## API 速查

| 分组     | API                                           | 说明                                                   |
|----------|-----------------------------------------------|--------------------------------------------------------|
| 生命周期 | `mr_xvm_gdb_init(gdb, ops, priv)`             | 初始化静态 GDB 会话（`ops->send` 不可为 `MR_NULL`）    |
| 生命周期 | `mr_xvm_gdb_create(ops, priv)`                | 在堆上创建 GDB 会话                                    |
| 生命周期 | `mr_xvm_gdb_del(gdb)`                         | 自动 `detach` 并销毁/释放会话                          |
| 生命周期 | `mr_xvm_gdb_get(gdb)` / `mr_xvm_gdb_put(gdb)` | 增加 / 释放对象引用计数                                |
| 会话控制 | `mr_xvm_gdb_attach(gdb, xvm)`                 | 附加到 XVM，挂载探针并解析 `"pc"` 寄存器编号           |
| 会话控制 | `mr_xvm_gdb_detach(gdb)`                      | 摘除探针、恢复运行门控并归还 XVM 引用（保留 XVM 实例） |
| 会话控制 | `mr_xvm_gdb_state(gdb)`                       | 查询当前会话状态                                       |
| 数据收发 | `mr_xvm_gdb_recv(gdb, buf, len)`              | 喂入任意分片的 RSP 字节流，返回已消费字节数            |
| 数据收发 | `mr_xvm_gdb_flush(gdb)`                       | 续传挂起应答，全部交付后调用可选的 `ops->flush`        |
| 数据收发 | `mr_xvm_gdb_want_send(gdb)`                   | 查询 `reply` 缓冲中是否仍有待发送数据                  |

## 内部实现

核心实现位于 `modules/xvm/gdb.c`：

- **逐字节解析器**：`parse.state` 在 `IDLE`（识别 `$` / `0x03` / 忽略输入 ACK）、
  `DATA`（收集载荷并预留 NUL 结尾空间，超限回 `-` NAK）与 `CKSUM`（核对 2
  位十六进制校验和）三态间流转。
- **异常信号映射**：探针回调 `gdb_probe_entry()` 将 XVM 异常映射为 GDB 信号码发出：
  `BREAK` → `S05`、`ILLEGAL` → `S04`、`DIV0` → `S08`、`ACCESS`/`ALIGN` → `S11`
  、其余 → `S06`。
