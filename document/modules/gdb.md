# GDB 桩（Gdb）

> XVM GDB 桩是 GDB 远程串行协议（RSP）适配器：宿主提供字节收发传输，桩把 RSP
> 命令包映射到 XVM 的调试与执行接口，使 GDB 可以直接调试运行中的虚拟机。

| 项       | 说明                                                      |
|----------|-----------------------------------------------------------|
| 功能宏   | `MR_USE_MODULE_XVM_GDB`（依赖 `MR_USE_MODULE_XVM_DEBUG`） |
| 头文件   | `<mrX/modules/xvm/mr_xvm_gdb.h>`                          |
| 依赖模块 | [虚拟机 XVM](xvm.md)、[调试器 Dbg](dbg.md)                |
| 参考测试 | `mrX/test/modules/xvm/gdb.c`                              |

---

## 用户指南

### 1. 模块概述

GDB 桩解决的问题是： **让标准 GDB 通过串口/TCP 等任意字节流通道调试 XVM**，无需 在目标板上实现完整的 RSP 协议栈。

特性：

- 协议会话与传输分离：目标板只实现 `send` 字节发送，接收侧把字节喂给
  `mr_xvm_gdb_recv`，支持短写（send 返回 0 时挂起，待 `flush` 续传）。
- 断点（`Z0`/`Z1`）、单步/继续（`s`/`c`/`vCont;s`/`vCont;c`）、Ctrl-C（0x03） 中断。
- 寄存器读写（`g`/`G`/`p`/`P`，小端十六进制编码，布局由 ISA target 决定）。
- 内存读写（`m`/`M`，经 XVM 地址翻译，镜像区只读）。
- 目标描述 XML 分片读取（`qXfer:features:read:target.xml`），GDB 自动获知寄存器 布局，无需手工维护 `set architecture`。
- `QStartNoAckMode` 无应答模式、`qSupported` 能力协商、单线程模型（thread 1）。
- 会话生命周期状态：DETACHED/ATTACHED/RUNNING/CLOSED，`D`/`k` 结束会话并回调。

不适用：多线程/多核调试（单线程模型）；文件 I/O（`F` 应答）与 `vRun` 等扩展包。

### 2. 配置与依赖

| 配置项                       | 类型 | 默认值 | 说明                                                                       |
|------------------------------|------|--------|----------------------------------------------------------------------------|
| `MR_USE_MODULE_XVM_GDB`      | bool | n      | 启用 GDB RSP 适配器（依赖 `MR_USE_MODULE_XVM_DEBUG`）                      |
| `MR_CFG_XVM_GDB_PACKET_SIZE` | 整型 | 512    | 接收包缓冲大小（字节），下限 272；作为 `PacketSize` 上报，除非发送缓冲更小 |
| `MR_CFG_XVM_GDB_REPLY_SIZE`  | 整型 | 512    | 发送应答缓冲大小（字节，含 `$`/`#xx` 框架），下限 272                      |

下限 272 的原因：GDB 首个 `qSupported` 查询约 200 字节，RV32 的 `g`/`G`
全寄存器包为 265 字节载荷加框架。XML 与内存传输按缓冲大小分片，无需更大缓冲； RAM 紧张时也应优先保持 512。

启用后包含头文件即可使用：

```c
#include <mrX/modules/xvm/mr_xvm_gdb.h>
```

该头文件会自动引入 `mr_xvm.h`。GDB 桩还需要 ISA 提供目标描述：内置 RISCV32 后端 在 `MR_USE_MODULE_XVM_DEBUG` 下自动提供（架构
`riscv:rv32`，x0~x31 + pc 共 33 个 32 位寄存器）。

### 3. 快速上手

以环形缓冲模拟传输通道的最小会话（取材自 `mrX/test/modules/xvm/gdb.c` 的
`test_gdb_io_t` 模式）：

```c
#include <mrX/modules/xvm/mr_xvm_gdb.h>

static mr_ssize_t my_send(void *priv, const void *buf, mr_size_t len) {
    return uart_write(priv, buf, len);      /* < 0: error, 0: would block */
}

static const mr_xvm_gdb_ops_t ops = { .send = my_send };

void gdb_service(uart_t *uart, mr_xvm_t *xvm) {
    static mr_xvm_gdb_t gdb;
    char byte;

    mr_xvm_gdb_init(&gdb, &ops, uart);
    mr_xvm_gdb_attach(&gdb, xvm);           /* Holds the VM via dbg probe */

    while (uart_readable(uart)) {
        uart_read(uart, &byte, 1);
        mr_xvm_gdb_recv(&gdb, &byte, 1);    /* Feed RSP bytes one by one */
    }
    /* Drive the VM; stops are reported to GDB automatically */
    mr_xvm_execute(xvm, &(mr_xvm_event_t){0}, 100);
}
```

要点：

- `mr_xvm_gdb_attach` 附加调试探针并把门控置 HOLD，虚拟机立即停住等待 GDB 命令。
- 会话状态与停止应答（`S05` 等）都由桩内部处理，宿主只负责搬运字节与驱动执行。
- 结束时调用 `mr_xvm_gdb_del`，桩会自动从 XVM 摘除探针并归还引用。

### 4. 使用说明

**会话生命周期**

| 状态                  | 含义                     | 迁移条件                                                      |
|-----------------------|--------------------------|---------------------------------------------------------------|
| `MR_XVM_GDB_DETACHED` | 未连接任何 XVM           | `init`/`detach`/`del`                                         |
| `MR_XVM_GDB_ATTACHED` | 已连接且停住             | `attach`、调试停机、Ctrl-C                                    |
| `MR_XVM_GDB_RUNNING`  | 已放行（`c`/`s` 已受理） | GDB 发出继续/单步                                             |
| `MR_XVM_GDB_CLOSED`   | 会话已关闭               | GDB 发出 `D`/`k`；CLOSED 后 `recv` 直接返回 0（不再消费字节） |

**典型交互流程**

1. `attach`：GDB 桩挂接探针，回复停止包（`S05`）。
2. GDB 查询：`qSupported`、`qXfer:features:read:target.xml`、`?`、`g` 等。
3. 设断点：`Z0,addr,4`（`Z1` 硬件断点与 `Z0` 共用同一地址表）。
4. 继续：`c`/`vCont;c`（或 `s`/`vCont;s` 单步），桩先跳过当前 PC 处断点再开门； 受理后状态变为 RUNNING，不再立即应答。
5. 停机：断点命中/单步完成/异常时，探针回调发送 `Snn` 停止包，状态回到 ATTACHED， 执行再次被 HOLD；Ctrl-C 则由桩在 `recv`
   中直接置 HOLD 并应答 `S02`。
6. 结束：`D` 回 OK 后关闭并触发 `ops->closed`；`k` 直接关闭。

**传输契约**

- `ops->send(priv, buf, len)`：发送字节，返回已接受字节数；返回 0 表示暂时不可写 （桩保留未发送部分，宿主稍后调用
  `mr_xvm_gdb_flush` 续传）；返回负数视为 I/O 错误（`-MR_EIO`）。`send` 必填。
- `ops->flush(priv)`：可选，挂起字节全部交付后调用（如触发底层发送）。
- `ops->closed(priv)`：可选，会话被 `D`/`k` 关闭后调用，用于释放传输资源。
- `mr_xvm_gdb_recv` 可按任意长度分片喂入（半个包也合法）；`+`/`-` ACK 字节被 忽略；0x03 字节在 ATTACHED/RUNNING 状态触发中断停机
  （应答 `S02`），未附加时静默。
- 无应答模式（`QStartNoAckMode`）开启后，桩不再发送 `+`/`-`。

**回调上下文**：探针回调（停止应答）运行在 `mr_xvm_execute` 调用者上下文中；
`send`/`flush`/`closed` 运行在触发它们的 `recv`/`flush` 调用者上下文中。回调内 不应阻塞。

### 5. API 参考

生命周期：

| API                 | 说明                      |
|---------------------|---------------------------|
| `mr_xvm_gdb_init`   | 初始化静态 GDB 会话       |
| `mr_xvm_gdb_create` | 堆上创建 GDB 会话         |
| `mr_xvm_gdb_del`    | 删除会话（先自动 detach） |

会话控制：

| API                 | 说明                            |
|---------------------|---------------------------------|
| `mr_xvm_gdb_attach` | 附加到 XVM（挂接探针，置 HOLD） |
| `mr_xvm_gdb_detach` | 从 XVM 摘除（不销毁 XVM）       |
| `mr_xvm_gdb_state`  | 查询会话状态                    |

传输：

| API                                 | 说明                           |
|-------------------------------------|--------------------------------|
| `mr_xvm_gdb_recv`                   | 喂入接收字节，返回消费的字节数 |
| `mr_xvm_gdb_flush`                  | 续传挂起的应答                 |
| `mr_xvm_gdb_want_send`              | 查询是否有未发完的应答         |
| `mr_xvm_gdb_get` / `mr_xvm_gdb_put` | 引用计数加一 / 减一            |

**`mr_xvm_gdb_init` / `mr_xvm_gdb_create`**

```c
mr_err_t mr_xvm_gdb_init(mr_xvm_gdb_t *gdb, const mr_xvm_gdb_ops_t *ops,
                         void *priv);
mr_xvm_gdb_t *mr_xvm_gdb_create(const mr_xvm_gdb_ops_t *ops, void *priv);
```

`ops->send` 不可为 `MR_NULL`（断言拦截）。`init` 成功返回 0，失败负数错误码；
`create` 成功返回会话指针，失败返回 `MR_NULL`。

**`mr_xvm_gdb_attach` / `mr_xvm_gdb_detach`**

```c
mr_err_t mr_xvm_gdb_attach(mr_xvm_gdb_t *gdb, mr_xvm_t *xvm);
void mr_xvm_gdb_detach(mr_xvm_gdb_t *gdb);
```

- `attach`：一个会话同一时刻只能连接一个 XVM（`-MR_EBUSY`）；成功后解析 ISA target 中的 PC 寄存器编号（按寄存器名 `"pc"`
  查找），供 `c`/`s` 带地址变体与 断点跳过使用；无 target 或无 `"pc"` 寄存器时继续/单步命令回 `E01`。
- `detach`：摘除探针（清断点、开门）并归还 XVM 引用；XVM 保持可用。

**`mr_xvm_gdb_recv`**

```c
mr_ssize_t mr_xvm_gdb_recv(mr_xvm_gdb_t *gdb, const void *buf, mr_size_t len);
```

逐字节解析：空闲态识别 `$`（包开始）、`+`/`-`（ACK）、0x03（中断）；数据态收集 载荷直到 `#`，超长包 NAK（`-`）并丢弃；校验态核对校验和，错误
NAK，正确 ACK 后 分发命令。会话 CLOSED 时直接返回 0。返回消费的字节数（错误时为负数错误码）。

**`mr_xvm_gdb_flush` / `mr_xvm_gdb_want_send`**

```c
mr_err_t mr_xvm_gdb_flush(mr_xvm_gdb_t *gdb);
mr_bool_t mr_xvm_gdb_want_send(const mr_xvm_gdb_t *gdb);
```

`send` 返回 0 造成挂起后，宿主在传输可写时调用 `flush` 续传；挂起字节全部交付 后调用 `ops->flush`（如有）。`want_send` 为
`MR_TRUE` 表示仍有未发完的应答。

### 6. 完整示例

示例 1：短写传输——`send` 每次只接受 1 字节，用 `flush` 续传（对应测试
`short_send`）：

```c
static mr_ssize_t limited_send(void *priv, const void *buf, mr_size_t len) {
    test_io_t *io = priv;

    if (io->stall) {
        return 0;                       /* Would block */
    }
    io->limit = 1;                      /* Accept one byte then stall */
    io->stall = MR_TRUE;
    return uart_write(io->uart, buf, MR_MIN(len, 1));
}

void polled_gdb(test_io_t *io, mr_xvm_gdb_t *gdb) {
    if (mr_xvm_gdb_want_send(gdb)) {
        mr_xvm_gdb_flush(gdb);          /* Drain the pending reply */
    }
}
```

示例 2：接收分片——RSP 包可以按任意块到达（对应测试
`query_edges_and_errors` 末尾）：

```c
/* A full "$qC#xx" packet arrives in three pieces */
mr_xvm_gdb_recv(&gdb, "$q", 2);
mr_xvm_gdb_recv(&gdb, "C#", 2);
mr_xvm_gdb_recv(&gdb, "1a", 2);         /* Checksum digits */
/* Reply "QC1" has been queued and sent via ops->send */
```

更多场景（寄存器/内存读写、断点继续、Ctrl-C、无应答模式、`D`/`k` 关闭）见
`mrX/test/modules/xvm/gdb.c`。

### 7. 常见问题（FAQ）

**Q：GDB 连接后没有任何输出？**

按顺序排查：确认 `MR_USE_MODULE_XVM_GDB`（及其依赖 `MR_USE_MODULE_XVM_DEBUG`、
`MR_USE_MODULE_XVM`）已启用；确认宿主在循环调用 `mr_xvm_gdb_recv` 喂入字节并驱动
`mr_xvm_execute`；确认 `ops->send` 已实现且返回值符合契约。

**Q：GDB 报告寄存器布局不对？**

ISA 未提供 target 或寄存器表不完整。内置 RISCV32 需同时启用
`MR_USE_MODULE_XVM_DEBUG`；`qSupported` 中的 `qXfer:features:read+` 只有存在 target 时才上报，GDB 依赖它拉取 target.xml。

**Q：`c` 命令后 GDB 收到 `E01`？**

ISA target 中没有名为 `"pc"` 的寄存器（桩无法实现"跳过当前断点 + 开门"）。 自定义 ISA 需在寄存器表中包含 `"pc"`。

**Q：继续之后 GDB 一直收不到停止包？**

宿主没有驱动执行。GDB 桩不主动运行虚拟机：`c`/`s` 受理后宿主必须继续调用
`mr_xvm_execute`，停机时桩才通过探针回调发送 `Snn`。

**Q：包内容超过缓冲上限怎么办？**

`MR_CFG_XVM_GDB_PACKET_SIZE`/`MR_CFG_XVM_GDB_REPLY_SIZE` 保持默认 512 即可覆盖 RV32 的全部命令；超长接收包会被 NAK
丢弃，超长应答（如整份 XML）由桩自动分片， 无需增大缓冲。

**Q：`send` 返回负数后会话还能用吗？**

不能可靠恢复：桩立即停止发送并返回 `-MR_EIO`，未交付部分仍滞留在应答缓冲中， 时序已不可预期，建议宿主重建会话 （
`mr_xvm_gdb_del` 后重新 `init`/`attach`）。

**Q：会话关闭后 XVM 还能继续用吗？**

能。`D`/`k` 只摘除探针并归还 XVM 引用，XVM 状态与数据不受影响；`del` 销毁的只是 会话对象。

---

## 开发者指南

### 8. 架构总览

GDB 桩是一个独立对象（非 XVM 内嵌组件），向下复用[调试器 Dbg](dbg.md) 的探针与 断点表，向上只依赖宿主提供的字节传输：

```
   GDB (host)                                  目标板
      |  RSP: "$m0,4#..."  /  0x03               |
      v                                          v
   传输层（串口/TCP，宿主实现）----recv()----> mr_xvm_gdb_recv()
                                                   | 解析/校验/ACK
                                               gdb_command() 分发
                                                   | 映射到 XVM API
                                    +--------------+---------------+
                                    |              |               |
                             mr_xvm_reg_*   mr_xvm_datamem_*  mr_xvm_dbg_*
                             （寄存器）      （内存）           （断点/门控/skip）
                                    |              |               |
                                    +--------------+---------------+
                                                   v
                                          探针回调 gdb_probe_entry()
                                                   | 停止包 "$S05#xx"
                                                   v
                                            ops->send() ----> GDB
```

关键接线：

- `mr_xvm_gdb_attach` 调用 `mr_xvm_dbg_probe_attach(xvm, gdb_probe_entry, gdb)`
  （`mrX/modules/xvm/gdb.c:311`），并以 `mr_xvm_get` 持有 XVM 引用直至 detach。
- 继续命令通过 `mr_xvm_dbg_break_skip` + `mr_xvm_dbg_probe_set_gate` 落地 （`gdb.c:856-887` 的 `gdb_run`）。
- 停止应答由探针回调 `gdb_probe_entry`（`gdb.c:250`）在调试停机时发出。

### 9. 数据结构

会话对象（`mrX/include/mrX/modules/xvm/mr_xvm_gdb.h:74`）：

| 字段           | 说明                                                                                |
|----------------|-------------------------------------------------------------------------------------|
| `parent`       | 对象基类，静态/动态双 clazz（`gdb.c:18`）                                           |
| `state`        | 会话状态机（DETACHED/ATTACHED/RUNNING/CLOSED）                                      |
| `parse`        | 接收解析器：`buf[MR_CFG_XVM_GDB_PACKET_SIZE]`、状态机三态、载荷长度、2 字节校验缓冲 |
| `reply`        | 发送缓冲：`buf[MR_CFG_XVM_GDB_REPLY_SIZE]`、`len`（已写入）、`off`（已交付）        |
| `no_ack`       | 无应答模式标志（`QStartNoAckMode` 后为真）                                          |
| `xvm`          | 已连接的 XVM（持有引用）；`MR_NULL` 表示未连接                                      |
| `pc_nr`        | target 中 PC 寄存器编号；无 PC 时为 `GDB_PC_NONE`（`MR_UINT32_MAX`）                |
| `ops` / `priv` | 传输操作表与私有指针                                                                |

`parse.state` 三态（`mr_xvm_gdb.h:36`）：IDLE（识别 `$`/ACK/0x03）、DATA（收集 载荷）、CKSUM（校验 2 个十六进制位）。

### 10. 核心流程

**接收状态机（`gdb_parse_byte`，`gdb.c:1159`）**：IDLE 态中 0x03 触发中断（HOLD 门控、应答 `S02`、状态回 ATTACHED）；`$` 进入
DATA；ACK 字节忽略。DATA 态溢出 （达到 `parse.buf` 容量）NAK 并复位。CKSUM 态校验和不匹配或数字非法 NAK 并丢弃； 通过则 ACK +
分发。

**命令分发（`gdb_command`，`gdb.c:972`）**按首字符路由，返回四类动作：
`GDB_CMD_REPLY`（应答已就绪）、`GDB_CMD_SILENT`（`c`/`s` 受理，暂不应答）、
`GDB_CMD_CLOSE`（`D`：回 OK 后关闭会话）、`GDB_CMD_KILL`（`k`：直接关闭）。 支持的命令：`?`（停止原因；ENDED 时回 `W00`）、`g`/`G`/
`p`/`P`（寄存器）、
`m`/`M`（内存）、`Z/z`（断点）、`c`/`s` 及 `vCont;c`/`vCont;s`、`qSupported`/
`qXfer:features:read:target.xml`/`qSymbol`/`qC`/`qfThreadInfo`/`qsThreadInfo`/
`qAttached`、`QStartNoAckMode`、`H`/`T`（线程相关，固定 OK）、`D`/`k`。 未识别的包回空载荷。

**寄存器与内存编码**：寄存器按 target 表顺序、小端十六进制编码；`g`/`G` 覆盖全部 表项（长度不匹配回 `E01`），`p`/`P`
按编号访问。内存访问经
`mr_xvm_datamem_map` 分段推进（`gdb_datamem_span`，`gdb.c:746`），跨镜像/RAM 边界 自动分段；`M` 写只读区直接 `E01`。

**继续/单步（`gdb_run`，`gdb.c:856`）**：可选起始地址写入 PC（解析失败回
`E01`）→ 读当前 PC → `mr_xvm_dbg_break_skip(pc)` 使当前断点不再立即命中 → 按 `step` 设置门控 STEP/OPEN → 状态 RUNNING →
静默返回。此后宿主驱动
`mr_xvm_execute`，调试停机时探针回调发停止包并把状态拉回 ATTACHED。

**应答发送**：命令应答直接写入 `reply.buf` 的 `$...#xx` 框架 （`gdb_reply_begin`/`gdb_reply_end`，`gdb.c:1073-1104`），
`gdb_reply_kick`
（`gdb.c:225`）循环调用 `ops->send`；`send` 返回 0 时保留 `off` 之后的部分挂起， 由 `mr_xvm_gdb_flush`
续传。中断与探针路径的短应答用栈上短缓冲拼装
`$Snn#xx`（`gdb_reply_stop`，`gdb.c:150`），异常号映射： BREAK→`S05`（trap）、ILLEGAL→`S04`、DIV0→`S08`、ACCESS/ALIGN→`S11`、 其他→
`S06`。

**会话关闭（`gdb_close_session`，`gdb.c:1053`）**：先保存 `ops->closed` 与
`priv`，`gdb_detach`（摘探针、置 DETACHED、`mr_xvm_put` 归还引用）后再触发回调， 避免回调期间引用已失效。`del` 与对象释放通道同样先
detach（`gdb.c:137`、
`gdb.c:1297-1310`）。

### 11. 并发与中断安全

- GDB 桩不持锁，串行化假设为"一个会话同一时刻只被一个上下文服务"。`recv`、
  `flush` 与探针回调操作同一缓冲，不得并发调用。
- `mr_xvm_gdb_attach`/`detach`/`del` 会操作 XVM 的探针与断点表，应与
  `mr_xvm_execute` 互斥（或由服务线程统一调度）。
- 探针回调运行在 `mr_xvm_execute` 调用者上下文（见[调试器 Dbg](dbg.md) 第 11 节），回调内的 `ops->send`
  因此也在该上下文执行；传输实现不应阻塞。
- 与 XVM 的交互通过 `mr_xvm_get`/`mr_xvm_put` 引用计数保证：会话存活期间 XVM 对象 不会被引用归零释放。

### 12. 扩展与派生

本模块无派生机制。可扩展点在传输层：实现 `mr_xvm_gdb_ops_t` 即可对接任意通道 （UART、USB CDC、TCP socket 等）；`flush` 用于支持
DMA/中断驱动的发送队列，
`closed` 用于会话结束时的资源归还。协议层面的新 RSP 命令需修改 `gdb_command`
分发器，不属于对外扩展点。

### 13. 源码导读

| 位置                                       | 职责                                                  |
|--------------------------------------------|-------------------------------------------------------|
| `mrX/include/mrX/modules/xvm/mr_xvm_gdb.h` | 状态机、parse/reply 缓冲、ops 表与 API 声明           |
| `mrX/modules/xvm/gdb.c:67`                 | 会话初始化公共路径（状态/缓冲/引用）                  |
| `mrX/modules/xvm/gdb.c:118`                | detach 公共路径（摘探针、归还 XVM 引用）              |
| `mrX/modules/xvm/gdb.c:250`                | 探针回调：停止包发送与状态回 ATTACHED                 |
| `mrX/modules/xvm/gdb.c:292`                | attach：挂探针、持引用、解析 PC 寄存器编号            |
| `mrX/modules/xvm/gdb.c:505`                | target XML 生成与 qXfer 分片（窗口滑切）              |
| `mrX/modules/xvm/gdb.c:629`                | `g`/`G`/`p`/`P` 寄存器编解码（target 表序、小端 hex） |
| `mrX/modules/xvm/gdb.c:759`                | `m`/`M` 内存读写（分段映射、只读检查）                |
| `mrX/modules/xvm/gdb.c:856`                | `c`/`s`/`vCont`：断点 skip + 门控设置                 |
| `mrX/modules/xvm/gdb.c:972`                | RSP 命令分发表                                        |
| `mrX/modules/xvm/gdb.c:1106`               | 应答封装与分发收尾（REPLY/SILENT/CLOSE/KILL）         |
| `mrX/modules/xvm/gdb.c:1159`               | 逐字节接收状态机（ACK/NAK/校验）                      |
| `mrX/modules/xvm/gdb.c:1244`               | `mr_xvm_gdb_recv` 入口与 CLOSED 处理                  |
| `mrX/test/modules/xvm/gdb.c`               | 全部协议行为的端到端测试（含短写、分片、错误包）      |

### 14. 附录

**配置宏**

| 宏                           | 说明                                             |
|------------------------------|--------------------------------------------------|
| `MR_USE_MODULE_XVM_GDB`      | 模块开关，默认 n，依赖 `MR_USE_MODULE_XVM_DEBUG` |
| `MR_CFG_XVM_GDB_PACKET_SIZE` | 接收包缓冲，默认 512，范围 272~8192              |
| `MR_CFG_XVM_GDB_REPLY_SIZE`  | 发送应答缓冲，默认 512，范围 272~8192            |

**RSP 命令支持表**

| 命令                                                             | 应答                                                                   | 说明                                                             |
|------------------------------------------------------------------|------------------------------------------------------------------------|------------------------------------------------------------------|
| `?`                                                              | `S05` / `W00`                                                          | 停止原因；XVM ENDED 时回进程退出                                 |
| `g` / `G`                                                        | hex / `OK`/`E01`                                                       | 全寄存器读写（target 表序）                                      |
| `p nr` / `P nr=val`                                              | hex / `OK`/`E01`                                                       | 单寄存器读写                                                     |
| `m addr,len` / `M addr,len:data`                                 | hex / `OK`/`E01`                                                       | 内存读写（小端 hex）                                             |
| `Z0/Z1,addr,kind` / `z0/z1,...`                                  | `OK`/`E01`/空                                                          | 断点插删（类型非 0/1 或缺逗号时静默；`z` 删不存在的断点回 `OK`） |
| `c [addr]` / `s [addr]`                                          | 静默，停机时 `Snn`                                                     | 继续/单步，可选起始地址                                          |
| `vCont?` / `vCont;c` / `vCont;s`                                 | `vCont;c;s` / 同 `c`/`s`                                               | vCont 协商与执行                                                 |
| `qSupported`                                                     | `PacketSize=...;qXfer:features:read+;QStartNoAckMode+;vContSupported+` | 能力协商                                                         |
| `qXfer:features:read:target.xml:off,len`                         | `m`/`l` + 分片                                                         | 目标描述 XML 分片读取                                            |
| `qSymbol` / `qC` / `qfThreadInfo` / `qsThreadInfo` / `qAttached` | `OK` / `QC1` / `m1` / `l` / `1`                                        | 固定应答（单线程模型）                                           |
| `QStartNoAckMode`                                                | `OK`                                                                   | 进入无应答模式                                                   |
| `H` / `T1`                                                       | `OK`                                                                   | 线程选择/存活（`T` 其他值回空）                                  |
| `D` / `k`                                                        | `OK` / 无                                                              | 关闭会话（触发 `ops->closed`）                                   |
| 0x03 字节                                                        | `S02`                                                                  | Ctrl-C 中断（未附加时静默）                                      |
| 其他                                                             | 空                                                                     | 未识别，空应答                                                   |

**错误码**：`mr_xvm_gdb_attach` 重复连接返回 `-MR_EBUSY`、XVM 引用获取失败返回
`-MR_ENOENT`；`ops->send` 返回负数时内部按 `-MR_EIO` 处理；缓冲不足返回
`-MR_ENOMEM`。协议层错误对 GDB 统一表现为 `E01` 应答。
