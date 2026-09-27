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

- 协议会话与传输分离：目标板只实现 `send` 字节发送，接收侧把字节喂给 `mr_xvm_gdb_recv`。正数短写会在同一次 kick 中继续发送；
  `send` 返回 0 时保留未发送部分，待 `mr_xvm_gdb_flush` 续传。
- 断点（`Z0`/`Z1`）、单步/继续（`s`/`c`/`vCont;s`/`vCont;c`）、Ctrl-C（0x03） 中断。
- 寄存器读写（`g`/`G`/`p`/`P`，小端十六进制编码，布局由 ISA target 决定）。
- 内存读写（`m`/`M`，经 XVM `mem_map` 分段映射，并尊重各映射区域的 writable 属性；具体哪些区域只读取决于 ISA）。
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

下限 272 的原因：GDB 首个 `qSupported` 查询约 200 字节，RV32 的 `g`/`G` 全寄存器包约 265 字节载荷加框架。XML 通过 `qXfer` 的
offset/length 由 GDB 分片拉取；普通 `m` 读取则必须在 **单个 reply** 的 payload 容量内，过大的读取请求返回 `E01`。

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
    mr_xvm_gdb_attach(&gdb, xvm);           /* Holds the VM via debug probe */

    while (uart_readable(uart)) {
        uart_read(uart, &byte, 1);
        mr_xvm_gdb_recv(&gdb, &byte, 1);    /* Feed RSP bytes one by one */
    }
    /* Drive the VM; stops are reported to GDB automatically */
    mr_xvm_execute(xvm, &(mr_xvm_event_t){0}, 100);
}
```

要点：

- `mr_xvm_gdb_attach` 附加调试探针并把门控置 HOLD；它不会异步抢占已经在途的 ISA 执行，但后续 `mr_xvm_execute` 入口会被
  HOLD 门控跳过。
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

1. `attach`：GDB 桩挂接 probe、持有 XVM 引用并把 gate 置 HOLD； **attach 本身不发送停止包**。GDB 后续发 `?` 时，未结束的 XVM
   固定回复 `S05`。
2. GDB 查询：`qSupported`、`qXfer:features:read:target.xml`、`?`、`g` 等。
3. 设断点：`Z0,addr,4`（`Z1` 硬件断点与 `Z0` 共用同一地址表）。
4. 继续：`c`/`vCont;c`（或 `s`/`vCont;s` 单步），桩先跳过当前 PC 处断点再开门； 受理后状态变为 RUNNING，不再立即应答。
5. 停机：断点命中/单步完成/任意被 debug probe 捕获的异常时，probe 回调发送 `Snn` 停止包，GDB 状态回到 ATTACHED；debug core
   在回调返回后把 gate 置 HOLD。Ctrl-C 在 `recv` 中立即把 gate 写为 HOLD、状态置 ATTACHED 并排入 `S02`；它不会异步中断一个已经进入
   ISA 的 `mr_xvm_execute` 批次。
6. 结束：`D` 先排入/尝试发送 `OK`，随后立即 detach、置 CLOSED 并触发 `ops->closed`；若发送发生短写/阻塞，CLOSED 状态下仍可显式调用
   `mr_xvm_gdb_flush` 发送剩余 reply。`k` 不回包，直接关闭。

**传输契约**

- `ops->send(priv, buf, len)`：正常应返回 `0..len` 范围内本次已接受的字节数。正数但小于 `len` 时 `gdb_reply_kick`
  会立刻继续调用；返回 0 表示暂时不可写并保留剩余部分；返回负数时当前 kick 返回 `-MR_EIO` 且 pending reply 保留。`send` 必填。
- `ops->flush(priv)`：可选，挂起字节全部交付后调用（如触发底层发送）。
- `ops->closed(priv)`：可选，会话被 `D`/`k` 关闭后调用，用于释放传输资源。
- `mr_xvm_gdb_recv` 可按任意长度分片喂入（半个包也合法）；输入侧 `+`/`-` ACK 字节被忽略。0x03 在有已附加 XVM 时设置 HOLD、状态
  ATTACHED 并发送 `S02`，未附加时静默。由于 GDB 会话本身无锁，不能靠另一个线程的 0x03 与正在执行的 `mr_xvm_execute` 做异步抢占。
- 无应答模式（`QStartNoAckMode`）开启后，桩不再发送 `+`/`-`。

**回调上下文**：probe 停机回调运行在 `mr_xvm_execute` 调用者上下文中，并会直接进入 `ops->send`，reply 完整交给 send 后还可能调用
`ops->flush`。普通命令应答的 send/flush 则运行在 `recv` 或显式 `mr_xvm_gdb_flush` 的调用者上下文；`closed` 由处理 `D/k` 的
`recv` 路径同步调用。传输回调是否允许阻塞取决于这些调用环境，GDB core 不另建线程。

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

`ops` 只保存指针、不复制操作表，因此其存储必须覆盖 GDB 会话生命周期；`priv` 同样由调用方管理。

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

逐字节解析：空闲态识别 `$`（包开始）、`+`/`-`（忽略的输入 ACK）、0x03（中断）；数据态收集载荷直到 `#`，为末尾 NUL 预留空间，因此
payload 达到接收缓冲上限时会 NAK 并丢弃；校验态核对 checksum，错误 NAK，正确时先排入 ACK 再分发命令。会话 CLOSED 时返回
0。当前实现入口本身不传播内部 `send` 错误，正常返回已消费的输入字节数；若处理 `D/k` 过程中转 CLOSED，会在该字节后停止继续消费本次剩余输入。

**`mr_xvm_gdb_flush` / `mr_xvm_gdb_want_send`**

```c
mr_err_t mr_xvm_gdb_flush(mr_xvm_gdb_t *gdb);
mr_bool_t mr_xvm_gdb_want_send(const mr_xvm_gdb_t *gdb);
```

`send` 返回 0 造成挂起后，宿主在传输可写时调用 `flush` 续传；正数短写则会由 kick 在同一次调用中继续尝试。全部 pending 字节交给
`send` 后，reply 缓冲先复位，再调用可选 `ops->flush`；其返回值由显式 `mr_xvm_gdb_flush` 透传。`want_send` 只反映
`reply.off < reply.len`，不表示底层 transport 自己的发送队列已经物理发完。

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

默认 512 足以覆盖当前测试中的 RV32 全寄存器包和常规查询，但不是任意内存请求的无限上限。超长接收 payload 会被 NAK
丢弃；target.xml 由 GDB 通过 `qXfer` 窗口分片读取，而 `m addr,len` 的单次返回若超过 reply payload 容量会直接 `E01`
，不会自动拆成多个 RSP 包。

**Q：`send` 返回负数后会话还能用吗？**

负数会使当前 `gdb_reply_kick` 返回 `-MR_EIO`，且未交付部分保持 pending。自动 kick（例如 `recv` 分发或 probe stop
reply）目前不会把这个错误继续返回给它的外层入口，所以宿主可通过 `mr_xvm_gdb_want_send()` 发现仍有数据，并在 transport 恢复后调用
`mr_xvm_gdb_flush()` 重试；显式 flush 会把后续 `-MR_EIO` 返回给调用者。是否最终重建会话由传输错误性质决定。

**Q：会话关闭后 XVM 还能继续用吗？**

能。`D`/`k` 会 detach probe、打开 debug gate、清空 debug 断点并归还 GDB 持有的 XVM 引用，但不会删除 XVM 或清除其寄存器/数据内存。
`del` 删除的只是会话对象（同样先 detach）。

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
                             mr_xvm_reg_*   mr_xvm_datamem_*  mr_xvm_debug_*
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

- `mr_xvm_gdb_attach` 调用 `mr_xvm_debug_probe_attach(xvm, gdb_probe_entry, gdb)`
  （`mrX/modules/xvm/gdb.c:311`），并以 `mr_xvm_get` 持有 XVM 引用直至 detach。
- 继续命令通过 `mr_xvm_debug_break_skip` + `mr_xvm_debug_probe_set_gate` 落地 （`gdb.c:856-887` 的 `gdb_run`）。
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

**接收状态机（`gdb_parse_byte`，`gdb.c:1159`）**：IDLE 态中 0x03 在已附加时设置 HOLD、排入 `S02`、状态 ATTACHED；`$` 进入
DATA；输入 ACK 字节忽略。DATA 为后续 NUL 保留一字节，若再收一个数据字节会使 `len + 1 >= sizeof(buf)`，则 NAK 并复位。CKSUM
态校验失败 NAK；通过则先排入 ACK 再分发。0x03 只是 gate 控制，不会中途抢占已开始的 ISA execute。

**命令分发（`gdb_command`，`gdb.c:972`）**按首字符路由，返回四类动作：
`GDB_CMD_REPLY`（应答已就绪）、`GDB_CMD_SILENT`（`c`/`s` 受理，暂不应答）、
`GDB_CMD_CLOSE`（`D`：回 OK 后关闭会话）、`GDB_CMD_KILL`（`k`：直接关闭）。 支持的命令：`?`（停止原因；ENDED 时回 `W00`）、`g`/`G`/
`p`/`P`（寄存器）、
`m`/`M`（内存）、`Z/z`（断点）、`c`/`s` 及 `vCont;c`/`vCont;s`、`qSupported`/
`qXfer:features:read:target.xml`/`qSymbol`/`qC`/`qfThreadInfo`/`qsThreadInfo`/
`qAttached`、`QStartNoAckMode`、`H`/`T`（线程相关，固定 OK）、`D`/`k`。 未识别的包回空载荷。

**寄存器与内存编码**：寄存器按 target 表顺序、小端十六进制编码；`g`/`G` 覆盖全部表项（长度不匹配回 `E01`），`p`/`P` 按编号访问。
`G` 和 `M` 都是逐项/逐映射段执行，并非事务式回滚：若后半段解析或写入失败，前面已经成功写入的内容不会撤销。一次 `m`/`M`
命令内部可经 `mr_xvm_datamem_map` 跨多个映射段推进；这里的“分段”只是单条命令内部的地址映射分段，不会自动拆成多个 RSP
回复包。遇到不可写映射时 `M` 回 `E01`。

**继续/单步（`gdb_run`，`gdb.c:856`）**：可选起始地址写入 PC（解析失败回
`E01`）→ 读当前 PC → `mr_xvm_debug_break_skip(pc)` 使当前断点不再立即命中 → 按 `step` 设置门控 STEP/OPEN → 状态 RUNNING →
静默返回。此后宿主驱动
`mr_xvm_execute`，调试停机时探针回调发停止包并把状态拉回 ATTACHED。

**应答发送**：命令 payload 直接在持久 `reply.buf` 中构造 `$...#xx`；中断/probe 的 `Snn` payload 虽先在栈上生成，但
`gdb_reply_packet` 会把完整 RSP 包复制进同一个持久 reply 缓冲后再发送，因此短写后仍可续传。`gdb_reply_kick` 对正数短写继续循环，0
表示暂停，负数返回 `-MR_EIO`。异常映射：BREAK→`S05`、ILLEGAL→`S04`、DIV0→`S08`、ACCESS/ALIGN→`S11`、其他→`S06`。

**会话关闭（`gdb_close_session`，`gdb.c:1053`）**：先保存 `closed`/`priv`，再 `gdb_detach`（摘 probe、清断点、开 gate、归还 XVM
引用），随后把仍有效的 GDB 对象状态置 CLOSED 并同步调用可选 `closed`。`D` 的 OK 包是在关闭前先 kick 的，但短写/0 并不会阻止立即进入
CLOSED；pending reply 仍保存在 GDB 对象中，可由显式 flush 继续。`del` 与最终对象 release 也都会先 detach，但不会调用
`closed`。

### 11. 并发与中断安全

- GDB 桩不持锁，串行化假设为“一个会话同一时刻只被一个上下文服务”。`recv`、显式 `flush`、Ctrl-C/命令发送与 probe 停止应答都共享
  `parse/reply/state`，不得并发操作同一会话。
- `mr_xvm_gdb_attach`/`detach`/`del` 以及协议命令中的寄存器、内存、断点、gate 操作并不会统一取得 XVM 的 `xvm_lock`
  ；宿主应把这些控制面操作与 `mr_xvm_execute` 串行化。
- probe 回调运行在 `mr_xvm_execute` 调用者上下文（见[调试器 Dbg](dbg.md) 第 11 节），其 `ops->send/flush` 也在该上下文执行；由
  `recv` 或显式 `flush` 触发的传输则运行在对应调用者上下文。
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

| 命令                                                             | 应答                                                                   | 说明                                                                   |
|------------------------------------------------------------------|------------------------------------------------------------------------|------------------------------------------------------------------------|
| `?`                                                              | `S05` / `W00`                                                          | 非 ENDED 固定 `S05`，ENDED 回 `W00`；不保存/回放真实“最后停止信号”     |
| `g` / `G`                                                        | hex / `OK`/`E01`                                                       | 全寄存器读写（target 表序）                                            |
| `p nr` / `P nr=val`                                              | hex / `OK`/`E01`                                                       | 单寄存器读写                                                           |
| `m addr,len` / `M addr,len:data`                                 | hex / `OK`/`E01`                                                       | 内存读写（小端 hex）                                                   |
| `Z0/Z1,addr,kind` / `z0/z1,...`                                  | `OK`/`E01`/空                                                          | 断点插删（类型非 0/1 或缺逗号时静默；`z` 删不存在的断点回 `OK`）       |
| `c [addr]` / `s [addr]`                                          | 静默，停机时 `Snn`                                                     | 继续/单步，可选起始地址                                                |
| `vCont?` / `vCont;c` / `vCont;s`                                 | `vCont;c;s` / 同 `c`/`s`                                               | vCont 协商与执行                                                       |
| `qSupported`                                                     | `PacketSize=...;qXfer:features:read+;QStartNoAckMode+;vContSupported+` | 能力协商                                                               |
| `qXfer:features:read:target.xml:off,len`                         | `m`/`l` + 分片                                                         | 目标描述 XML 分片读取                                                  |
| `qSymbol` / `qC` / `qfThreadInfo` / `qsThreadInfo` / `qAttached` | `OK` / `QC1` / `m1` / `l` / `1`                                        | 固定应答（单线程模型）                                                 |
| `QStartNoAckMode`                                                | `OK`                                                                   | 进入无应答模式                                                         |
| `H` / `T1`                                                       | `OK`                                                                   | 线程选择/存活（`T` 其他值回空）                                        |
| `D` / `k`                                                        | `OK` / 无                                                              | 关闭会话（触发 `ops->closed`）                                         |
| 0x03 字节                                                        | `S02`                                                                  | Ctrl-C：附加时置 HOLD 并报告停止；未附加时静默，不异步抢占在途 execute |
| 其他                                                             | 空                                                                     | 未识别，空应答                                                         |

**错误码**：`mr_xvm_gdb_attach` 重复连接返回 `-MR_EBUSY`、XVM 引用获取失败返回 `-MR_ENOENT`。内部 reply builder 可产生
`-MR_ENOMEM`，`ops->send` 负数被 kick 映射为 `-MR_EIO`；但 `mr_xvm_gdb_recv` 当前不向外传播这些发送错误，显式
`mr_xvm_gdb_flush` 才直接返回 kick/transport flush 的错误。协议命令自身的解析/目标访问失败通常表现为 `E01` payload。
