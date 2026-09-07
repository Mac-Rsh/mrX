# 先进先出队列（FIFO）

> FIFO 模块提供基于环形缓冲的定长元素先进先出队列，单写单读两侧均以原子操作
> 实现无锁并发访问，适用于中断与线程之间的数据传递（串口收发、日志环、
> 生产者消费者管道等）。

| 项       | 说明                                                                             |
|----------|----------------------------------------------------------------------------------|
| 功能宏   | 无（始终编译）                                                                   |
| 头文件   | `<mrX/kernel/mr_fifo.h>`                                                         |
| 依赖模块 | [动态内存 malloc](../libc/malloc.md)（仅 `mr_fifo_alloc` / `mr_fifo_free` 使用） |
| 参考测试 | `mrX/test/kernel/fifo.c`                                                         |

---

## 用户指南

### 1. 模块概述

FIFO 解决的问题是： **在两个执行流之间传递定长元素，且不加锁**。特性：

- 元素长度固定（`elemsz` 字节），容量按元素个数计，缓冲区由用户提供或堆分配。
- 单元素接口 `mr_fifo_push` / `mr_fifo_pop`；批量接口 `mr_fifo_write` /
  `mr_fifo_read` / `mr_fifo_peek` / `mr_fifo_skip` / `mr_fifo_overwrite`。
- 零拷贝接口 `mr_fifo_*_claim` / `mr_fifo_*_finish`，避免中间缓冲搬运。
- 全部操作基于原子读写游标，单写单读（SPSC）场景无需任何锁。
- 所有操作以"实际完成元素数"为返回值，参数非法、未初始化、队满、队空统一 返回 0，不产生错误码、不崩溃。
- 内核内已有使用者：[根文件系统 ramfs](../fs/ramfs.md) 与 RPC 组件 （`mrX/fs/ramfs/ramfs.c`、`mrX/modules/rpc/rpc.c`）。

不适用于：变长消息队列（请自行在元素内携带长度字段）、多生产者或多消费者 并发（见第 7 节 FAQ）。

### 2. 配置与依赖

FIFO 模块没有任何 `MR_USE_*` 功能宏门控，也没有专属 `MR_CFG_*` 配置项： 头文件 `mrX/include/mrX/kernel/mr_fifo.h` 不引用
`mr_config.h`，始终参与编译。

```c
#include <mrX/kernel/mr_fifo.h>
```

唯一的外部依赖是动态内存：`mr_fifo_alloc` 通过 `mr_malloc` 分配缓冲、
`mr_fifo_free` 通过 `mr_free` 归还（`mrX/kernel/fifo.c:58`、`fifo.c:75`）； 使用静态初始化（`mr_fifo_init` +
用户自备缓冲）则完全不依赖堆。实现内部有 一个容量硬上限 `FIFO_CAPACITY_MAX = MR_INT32_MAX / 2 + 1`
（`mrX/kernel/fifo.c:15`），超出时初始化失败。

### 3. 快速上手

一个静态缓冲的整型元素队列：

```c
#include <mrX/kernel/mr_fifo.h>

typedef struct {
    int id;
} sample_t;

void fifo_demo(void) {
    static mr_uint8_t buf[8 * sizeof(sample_t)];
    mr_fifo_t fifo;
    sample_t in = {.id = 42};
    sample_t out;

    if (mr_fifo_init(&fifo, buf, sizeof(buf), sizeof(sample_t)) == 0) {
        return;                     /* Invalid parameters */
    }

    if (mr_fifo_push(&fifo, &in) == 1) {
        if (mr_fifo_pop(&fifo, &out) == 1) {
            /* out.id == 42 */
        }
    }
}
```

要点：

- `mr_fifo_init` 的 `size` 是缓冲区字节数，容量 = `size / elemsz` 向下取整； 返回值是容量（元素个数），返回 0 表示参数非法。
- `fifo` 与 `buf` 的生命周期必须覆盖使用期，不能放在栈上逃出作用域。

### 4. 使用说明

**生命周期**：静态方式用 `mr_fifo_init` 绑定调用者缓冲，不需要释放；动态方式 用 `mr_fifo_alloc` 堆分配、`mr_fifo_free`
释放（仅释放缓冲区，`mr_fifo_t`
本身的存储归调用者）。`free` 后可再次 `init` / `alloc` 复用同一对象。

**失败语义**：未初始化、参数非法、队满（写入类）、队空（读取类）一律返回 0， 不区分具体原因；需要区分时先调用 `mr_fifo_is_full` /
`mr_fifo_is_empty`。 初始化失败时 `mr_fifo_t` 保持调用前的内容不变（`mrX/test/kernel/fifo.c:63`
验证）。

**读写行为**

| 操作                           | 行为                                                                 |
|--------------------------------|----------------------------------------------------------------------|
| `mr_fifo_write`                | 尽量写入，超出剩余空间自动截断，返回实际写入数                       |
| `mr_fifo_read`                 | 尽量读出并消费，超出已有元素自动截断                                 |
| `mr_fifo_peek`                 | 只复制不消费，读后 `len` 不变                                        |
| `mr_fifo_skip`                 | 丢弃最多 `num` 个最旧元素                                            |
| `mr_fifo_overwrite`            | 空间不足时挤掉最旧元素；`num` 超过容量时只保留最新 `capacity` 个元素 |
| `mr_fifo_reset`                | 读写游标清零，缓冲内容不擦除                                         |
| `mr_fifo_push` / `mr_fifo_pop` | 单元素版写读，成功返回 1                                             |

**零拷贝**：`mr_fifo_write_claim(off, &buf, want)` 返回一段 **连续**可写区域 （遇到缓冲区物理边界会截断，长度可能小于
`want`），调用者直接写入后以
`mr_fifo_write_finish(num)` 提交；`off` 为相对当前写游标的偏移，支持分段填充 （`mrX/test/kernel/fifo.c:338`）。读侧
`mr_fifo_read_claim` /
`mr_fifo_read_finish` 对称；`mr_fifo_overwrite_claim` /
`mr_fifo_overwrite_finish` 允许超越当前填充量覆盖旧数据，finish 时自动丢弃 被覆盖的最旧元素。

### 5. API 参考

初始化与释放：

| API             | 说明                                       |
|-----------------|--------------------------------------------|
| `mr_fifo_init`  | 用外部缓冲初始化，返回容量（失败返回 0）   |
| `mr_fifo_alloc` | 堆分配缓冲并初始化，返回容量（失败返回 0） |
| `mr_fifo_free`  | 释放 `alloc` 分配的缓冲                    |

单元素：

| API            | 说明                      |
|----------------|---------------------------|
| `mr_fifo_push` | 压入 1 个元素，满则返回 0 |
| `mr_fifo_pop`  | 弹出 1 个元素，空则返回 0 |

批量：

| API                 | 说明                                 |
|---------------------|--------------------------------------|
| `mr_fifo_write`     | 批量写入，返回实际写入元素数         |
| `mr_fifo_read`      | 批量读出并消费，返回实际读出元素数   |
| `mr_fifo_peek`      | 批量预览不消费，返回实际预览元素数   |
| `mr_fifo_skip`      | 丢弃元素，返回实际丢弃元素数         |
| `mr_fifo_overwrite` | 带挤占的批量写入，返回实际写入元素数 |

状态查询（均为内联函数）：

| API                                              | 说明                             |
|--------------------------------------------------|----------------------------------|
| `mr_fifo_len` / `mr_fifo_avail` / `mr_fifo_size` | 当前元素数 / 剩余可写数 / 总容量 |
| `mr_fifo_is_empty` / `mr_fifo_is_full`           | 是否为空 / 是否已满              |
| `mr_fifo_reset`                                  | 清零读写游标                     |

零拷贝：

| API                                                    | 说明                      |
|--------------------------------------------------------|---------------------------|
| `mr_fifo_write_claim` / `mr_fifo_write_finish`         | 申请/提交一段连续可写区域 |
| `mr_fifo_overwrite_claim` / `mr_fifo_overwrite_finish` | 申请/提交一段连续覆盖区域 |
| `mr_fifo_read_claim` / `mr_fifo_read_finish`           | 申请/提交一段连续可读区域 |

**`mr_fifo_init`**

```c
mr_uint32_t mr_fifo_init(mr_fifo_t *fifo, void *buf, mr_uint32_t size,
                         mr_uint32_t elemsz);
```

- `buf`：缓冲区，不可为 `MR_NULL`；`size`：缓冲区字节数，须不小于 `elemsz`。
- `elemsz`：单元素字节数，不可为 0。
- 返回：容量（元素个数）；参数非法或容量超过 `FIFO_CAPACITY_MAX` 返回 0。

**`mr_fifo_alloc`**

```c
mr_uint32_t mr_fifo_alloc(mr_fifo_t *fifo, mr_uint32_t num, mr_uint32_t elemsz);
```

- `num`：期望容量，不可为 0 且不得超过 `FIFO_CAPACITY_MAX`，`num * elemsz`
  不得溢出 32 位。
- 返回：容量；参数非法或内存不足返回 0。

**`mr_fifo_push` / `mr_fifo_pop`**

```c
mr_uint32_t mr_fifo_push(mr_fifo_t *fifo, const void *data);
mr_uint32_t mr_fifo_pop(mr_fifo_t *fifo, void *data);
```

复制 `elemsz` 字节，成功返回 1；未初始化、队满（push）或队空（pop）返回 0。

**`mr_fifo_write` / `mr_fifo_read` / `mr_fifo_peek` / `mr_fifo_skip`**

```c
mr_uint32_t mr_fifo_write(mr_fifo_t *fifo, const void *buf, mr_uint32_t num);
mr_uint32_t mr_fifo_read(mr_fifo_t *fifo, void *buf, mr_uint32_t num);
mr_uint32_t mr_fifo_peek(mr_fifo_t *fifo, void *buf, mr_uint32_t num);
mr_uint32_t mr_fifo_skip(mr_fifo_t *fifo, mr_uint32_t num);
```

均以元素个数为单位，跨缓冲区物理边界自动分两段搬运，`num` 超出可写/可读量 时自动截断，返回实际完成的元素数。`read` 即
`peek` + 消费。

**`mr_fifo_overwrite`**

```c
mr_uint32_t mr_fifo_overwrite(mr_fifo_t *fifo, const void *buf, mr_uint32_t num);
```

`num` 大于容量时只取 `buf` 中最新的 `capacity` 个元素；空间不足时先
`mr_fifo_skip` 腾位再写入（`mrX/kernel/fifo.c:204`）。适合"只关心最新数据"
的日志环、采样环。

**`mr_fifo_*_claim` / `mr_fifo_*_finish`**

```c
mr_uint32_t mr_fifo_write_claim(mr_fifo_t *fifo, mr_uint32_t off, void **buf,
                                mr_uint32_t want);
mr_uint32_t mr_fifo_write_finish(mr_fifo_t *fifo, mr_uint32_t num);
mr_uint32_t mr_fifo_overwrite_claim(mr_fifo_t *fifo, mr_uint32_t off, void **buf,
                                    mr_uint32_t want);
mr_uint32_t mr_fifo_overwrite_finish(mr_fifo_t *fifo, mr_uint32_t num);
mr_uint32_t mr_fifo_read_claim(mr_fifo_t *fifo, mr_uint32_t off, void **buf,
                               mr_uint32_t want);
void mr_fifo_read_finish(mr_fifo_t *fifo, mr_uint32_t num);
```

- `off`：相对当前游标的偏移（元素数），越过可用范围返回 0 且不修改 `*buf`。
- `buf`：出参，指向缓冲区内连续区域首地址；`want`：期望元素数。
- 实际返回值受可用量与物理边界限制；`finish` 的 `num` 超量自动截断，
  `overwrite_finish` 空间不足时自动推进读游标丢弃最旧元素（`fifo.c:351`），
  `read_finish` 无返回值。

### 6. 完整示例

示例 1：填满、取空与回绕顺序（改编自 `mrX/test/kernel/fifo.c:349`）：

```c
void wrap_demo(void) {
    static mr_uint8_t buf[8 * sizeof(int)];
    mr_fifo_t fifo;
    int v, cap;

    cap = (int)mr_fifo_init(&fifo, buf, sizeof(buf), sizeof(int));
    for (v = 0; v < cap; v++) {
        mr_fifo_push(&fifo, &v);
    }
    for (v = 0; v < cap / 2; v++) {
        mr_fifo_pop(&fifo, &v);         /* Discard oldest half */
    }
    for (v = cap; v < cap + cap / 2; v++) {
        mr_fifo_push(&fifo, &v);        /* Wraps around the buffer end */
    }
    /* mr_fifo_len(&fifo) == cap, order preserved */
}
```

示例 2：日志环——只保留最新的采样值：

```c
static mr_uint8_t log_buf[32 * sizeof(sample_t)];
static mr_fifo_t log_fifo;

void log_setup(void) {
    mr_fifo_init(&log_fifo, log_buf, sizeof(log_buf), sizeof(sample_t));
}

void log_sample(const sample_t *s) {
    /* Full fifo keeps only the newest capacity samples */
    mr_fifo_overwrite(&log_fifo, s, 1);
}
```

示例 3：零拷贝写入（改编自 `mrX/test/kernel/fifo.c:310`）：

```c
void zero_copy_demo(mr_fifo_t *fifo, const mr_uint8_t *src, mr_uint32_t len) {
    mr_uint8_t *p;
    mr_uint32_t n = 0;

    while (n < len) {
        mr_uint32_t got = mr_fifo_write_claim(fifo, 0, (void **)&p, len - n);
        if (got == 0) {
            break;                      /* No space left */
        }
        mr_memcpy(p, src + n, got);
        mr_fifo_write_finish(fifo, got);
        n += got;
    }
}
```

### 7. 常见问题（FAQ）

**Q：可以在两个线程同时 push 吗？**

不可以。FIFO 只保证 **单写单读**无锁安全：写侧只推进写游标、读侧只推进读 游标（`mrX/kernel/fifo.c:138`、`fifo.c:164`
）。多生产者或多消费者必须在外部 加锁串行化，否则会产生数据竞争——两个写者会向同一物理槽位拷贝。测试
`mrX/test/kernel/fifo.c:554` 的并发用例即严格的一写一读。

**Q：返回 0 到底是满了、空了还是没初始化？**

API 层面不区分。可在操作前查询 `mr_fifo_is_full` / `mr_fifo_is_empty`； 未初始化对象的所有操作都安全返回 0（内部以
`buf == MR_NULL` 判定，
`fifo.c:120`）。

**Q：`elemsz` 不是 1 有什么影响？**

容量与所有计数以元素为单位，内部按 `elemsz` 字节搬运；`elemsz` 为 1 时走 单字节快速路径（`fifo.c:132`）。

**Q：`mr_fifo_overwrite` 写入超过容量的数据会怎样？**

只保留源缓冲中最新 `capacity` 个元素，其余丢弃，返回值恒为容量（测试
`mrX/test/kernel/fifo.c:255`：向容量 32 的队列 overwrite 1024 个元素返回 32）。

**Q：`claim` 拿到的区域为什么比请求的短？**

零拷贝接口只暴露 **物理连续**区域，临近缓冲区末尾时被 `capacity - index`
截断，剩余部分需再次 `claim`（`off` 为 0）。`mrX/test/kernel/fifo.c:418`
覆盖跨边界的分段 claim。

**Q：`mr_fifo_free` 和 `mr_fifo_reset` 的区别？**

`reset` 只清零游标、缓冲保留、可继续使用；`free` 释放堆缓冲并使队列回到 未初始化状态，静态初始化的队列不需要 `free`。

---

## 开发者指南

### 8. 架构总览

FIFO 是内核中的独立工具模块，不接入对象系统、不依赖调度与时钟，仅依赖原子 整数操作：

```
   +-----------------------------------+
   | 生产者（ISR / 线程）               |
   |  push / write / overwrite_claim   |----仅推进 widx (CAS)
   +-----------------+-----------------+
                     | mr_uint8_t *buf 环形缓冲（capacity 个 elemsz 元素）
   +-----------------v-----------------+
   | 消费者（线程）                     |
   |  pop / read / read_claim          |----仅推进 ridx (CAS)
   +-----------------------------------+
        依赖：原子操作(mr_atomic: load/CAS)、memcpy、
              malloc(仅 alloc/free 路径)
```

内核内使用者：ramfs 与 rpc（`mrX/fs/ramfs/ramfs.c`、`mrX/modules/rpc/rpc.c`）。

### 9. 数据结构

队列结构（`mrX/include/mrX/kernel/mr_fifo.h:24`）：

```c
typedef struct mr_fifo {
    mr_atomic_t ridx, widx;
    mr_uint32_t elemsz;
    mr_uint32_t capacity;
    mr_uint8_t *buf;
} mr_fifo_t;
```

| 字段                  | 说明                                                                               |
|-----------------------|------------------------------------------------------------------------------------|
| `ridx`                | 原子读游标，消费者独占推进                                                         |
| `widx`                | 原子写游标，生产者独占推进                                                         |
| `elemsz` / `capacity` | 元素字节数 / 容量（元素个数），初始化后只读                                        |
| `buf`                 | 缓冲区首地址；为 `MR_NULL` 表示未初始化（所有操作返回 0 的判断依据，`fifo.c:120`） |

**游标编码**：`ridx`/`widx` 取值范围是 `[0, 2 * capacity)`，允许跑满两圈后才 回绕（`fifo_cursor_advance`，`fifo.c:88`
）。这样"满"（`widx - ridx ==
capacity`）与"空"（`widx == ridx`）天然可区分，无需牺牲一个存储槽位。物理 下标由 `fifo_cursor_to_index` 折叠：
`cursor >= capacity` 时减去 `capacity`
（`fifo.c:79`）。容量上限 `FIFO_CAPACITY_MAX` 保证游标的两圈区间不溢出 32 位。

### 10. 核心流程

**游标推进**（`fifo_cursor_add`，`fifo.c:103`）：读当前游标，计算推进后的值 （超过 `2 * capacity` 减去之），用
`mr_atomic_compare_exchange` CAS 循环 提交。CAS 只在多写者竞争同一游标时才会重试，SPSC 下一次即中。

**长度计算**（`mr_fifo_len`，`mr_fifo.h:125`）：`widx >= ridx` 时为
`widx - ridx`，跨圈时为 `2 * capacity - ridx + widx`；`mr_fifo_avail` 与
`mr_fifo_is_full` 由它派生。

**批量搬运**（`mr_fifo_write`，`fifo.c:168`；`mr_fifo_peek`，`fifo.c:230`）： 先按可用量截断 `num`，把游标与长度换算成字节域后，以
`MR_MIN(num, size - index)` 分割为"尾段 + 头段"两次 `mr_memcpy`，最后一次性 CAS 推进游标；`mr_fifo_read` 即 `peek` +
推进读游标（`fifo.c:265`）。

**挤占写入**（`mr_fifo_overwrite`，`fifo.c:204`）：`num > capacity` 时把源 指针前移 `(num - capacity) * elemsz` 只写最新
`capacity` 个；可用量不足时
`mr_fifo_skip(num - avail)` 丢弃最旧元素；复用 `mr_fifo_write` 完成写入。

**零拷贝**（`mr_fifo_write_claim`，`fifo.c:279`）：`off` 先受 `avail - off`
限制（不允许越过未提交区域），再按游标定位物理下标，最后受 `capacity -
index` 限制为连续段；`mr_fifo_read_claim`（`fifo.c:369`）以 `len` 为可用量 逻辑对称，`mr_fifo_overwrite_claim`（`fifo.c:321`
）以整个容量为可用量，允许 越过当前填充量。提交侧：`write_finish` 以 `avail` 截断后推进写游标 （`fifo.c:309`）；
`overwrite_finish` 空间不足时先推进读游标再推进写游标 （`fifo.c:351`）；`read_finish` 以 `len` 截断后推进读游标（
`fifo.c:399`）。

### 11. 并发与中断安全

- 写侧 API（`push`/`write`/`overwrite`/`write_claim`/`write_finish`/
  `overwrite_*`）只读写 `widx` 与只读字段，读侧 API（`pop`/`read`/`peek`/
  `skip`/`read_claim`/`read_finish`）只读写 `ridx` 与只读字段。两侧并发无需 任何锁，均可在中断上下文调用。
- `peek` 不改游标，与写侧并发时读到的是某个瞬时快照；单读侧前提下（消费只有 一个执行流）`read` 的两段拷贝结果完整一致。
- 多生产者、多消费者不受支持：游标虽由 CAS 保护，但两个写者会向同一物理 槽位并发拷贝，产生数据竞争。
- `mr_fifo_init` / `mr_fifo_alloc` / `mr_fifo_free` / `mr_fifo_reset` 不做 并发保护，必须在开始生产/消费之前或两侧都停止之后调用。

### 12. 源码导读

| 位置                                   | 职责                                                  |
|----------------------------------------|-------------------------------------------------------|
| `mrX/include/mrX/kernel/mr_fifo.h:24`  | 队列结构体与全部 API 声明                             |
| `mrX/include/mrX/kernel/mr_fifo.h:125` | `len` / `avail` / `size` / `reset` / 状态查询（内联） |
| `mrX/kernel/fifo.c:15`                 | 容量上限 `FIFO_CAPACITY_MAX`                          |
| `mrX/kernel/fifo.c:17`                 | 静态初始化与参数校验                                  |
| `mrX/kernel/fifo.c:41`                 | 堆分配初始化（含溢出防护）                            |
| `mrX/kernel/fifo.c:79`                 | 游标折叠/推进/CAS 提交三个内联原语                    |
| `mrX/kernel/fifo.c:116`                | 单元素 push / pop                                     |
| `mrX/kernel/fifo.c:168`                | 批量写（双段拷贝）                                    |
| `mrX/kernel/fifo.c:204`                | 挤占写 overwrite                                      |
| `mrX/kernel/fifo.c:230`                | 批量预览 peek（read 的公共路径）                      |
| `mrX/kernel/fifo.c:279`                | 零拷贝 claim/finish 六个接口                          |
| `mrX/test/kernel/fifo.c`               | 边界、回绕、零拷贝与 SPSC 并发行为测试                |

### 13. 附录：配置宏与错误码

**配置宏**：本模块无功能宏、无 `MR_CFG_*` 配置项，始终编译。实现内部常量
`FIFO_CAPACITY_MAX`（`MR_INT32_MAX / 2 + 1`）为容量硬上限。

**错误码**：本模块不使用 `mr_errno` 错误码体系，所有 API 以元素计数为返回 值：返回 0
表示"未完成任何元素"（涵盖未初始化、参数非法、队满、队空四种 情形），失败信息需通过 `mr_fifo_is_full` / `mr_fifo_is_empty`
等查询接口 进一步区分。
