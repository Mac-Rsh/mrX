# 先进先出队列（FIFO）

提供基于环形缓冲的定长元素先进先出队列，支持单元素、批量、覆盖写及零拷贝操作。普通读写各自使用独立的原子游标，适合单生产者/单消费者（SPSC）无锁场景。

| 项目   | 说明                                                                                |
|--------|-------------------------------------------------------------------------------------|
| 功能宏 | 无（始终编译）                                                                      |
| 头文件 | `<mrX/kernel/mr_fifo.h>`                                                            |
| 依赖   | [动态内存（Malloc）](../libc/malloc.md)（仅 `mr_fifo_alloc` / `mr_fifo_free` 使用） |
| 测试   | `test/kernel/fifo.c`、`test/kernel/fifo_deep.c`                                     |

## 快速上手

```c
#include <mrX/kernel/mr_fifo.h>

typedef struct {
    int id;
} sample_t;

void fifo_demo(void) {
    static mr_uint8_t buf[8 * sizeof(sample_t)];
    mr_fifo_t fifo;
    sample_t in = {.id = 42}, out;

    /* 容量 = sizeof(buf) / sizeof(sample_t) = 8；返回 0 表示参数非法 */
    if (mr_fifo_init(&fifo, buf, sizeof(buf), sizeof(sample_t)) == 0) {
        return;
    }

    if (mr_fifo_push(&fifo, &in) == 1) {
        if (mr_fifo_pop(&fifo, &out) == 1) {
            /* out.id == 42 */
        }
    }
}
```

## 核心机制与约束

- **SPSC 并发模型**：普通写接口（`push`/`write`/`write_claim`/`write_finish`）仅推进
  `widx`，普通读接口（`pop`/`read`/`skip`/`read_claim`/`read_finish`）仅推进 `ridx`
  ，适合一写一读无锁并发。多生产者或多消费者不受支持（CAS 只保护游标提交，无法保护多执行流对同一槽位的
  `memcpy`）。
- **覆盖写的并发限制**：`mr_fifo_overwrite` 与 `mr_fifo_overwrite_finish`
  在空间不足时会从写侧推进 `ridx` 丢弃最旧元素，打破了“读游标只由消费者修改”的分工，若存在并发消费者需在外部加锁同步。
- **生命周期管理**：静态缓冲通过 `mr_fifo_init()` 绑定，不可调用 `mr_fifo_free()`
  ；动态缓冲通过 `mr_fifo_alloc()` 分配，使用完毕后调用 `mr_fifo_free()` 释放。
  `mr_fifo_free()` 仅释放并置空 `buf`，不会清零 `capacity`、`elemsz` 或游标，复用前需重新
  `init` 或 `alloc`。
- **零拷贝分段访问**：`mr_fifo_*_claim()` 仅返回物理连续的内存段。临近缓冲区物理末尾时返回长度可能小于
  `want`，需结合 `*_finish()` 提交后再对剩余部分发起下一次 `claim`。
- **前置条件与返回值**：数据路径在队满、队空或 `fifo->buf == MR_NULL` 时返回 `0`
  （表示完成 0 个元素），但接口本身假定 `fifo` 与数据指针有效，不做通用空指针校验。容量上限为
  `FIFO_CAPACITY_MAX`（`MR_INT32_MAX / 2 + 1`）。

## API 速查

### 初始化与释放

| API                                                                                          | 说明                                                         |
|----------------------------------------------------------------------------------------------|--------------------------------------------------------------|
| `mr_uint32_t mr_fifo_init(mr_fifo_t *fifo, void *buf, mr_uint32_t size, mr_uint32_t elemsz)` | 绑定外部缓冲并初始化，返回元素容量（失败返回 `0`）           |
| `mr_uint32_t mr_fifo_alloc(mr_fifo_t *fifo, mr_uint32_t num, mr_uint32_t elemsz)`            | 堆分配 `num * elemsz` 缓冲并初始化，返回容量（失败返回 `0`） |
| `void mr_fifo_free(mr_fifo_t *fifo)`                                                         | 释放由 `mr_fifo_alloc()` 分配的缓冲并将 `buf` 置空           |

### 单元素与批量读写

| API                                                                                | 说明                                                      |
|------------------------------------------------------------------------------------|-----------------------------------------------------------|
| `mr_uint32_t mr_fifo_push(mr_fifo_t *fifo, const void *data)`                      | 压入 1 个元素，成功返回 `1`，队满返回 `0`                 |
| `mr_uint32_t mr_fifo_pop(mr_fifo_t *fifo, void *data)`                             | 弹出 1 个元素，成功返回 `1`，队空返回 `0`                 |
| `mr_uint32_t mr_fifo_write(mr_fifo_t *fifo, const void *buf, mr_uint32_t num)`     | 批量写入（超出剩余空间自动截断），返回实际写入数          |
| `mr_uint32_t mr_fifo_overwrite(mr_fifo_t *fifo, const void *buf, mr_uint32_t num)` | 覆盖写入；`num > capacity` 时只保留最新 `capacity` 个元素 |
| `mr_uint32_t mr_fifo_read(mr_fifo_t *fifo, void *buf, mr_uint32_t num)`            | 批量读出并消费，返回实际读出数                            |
| `mr_uint32_t mr_fifo_peek(mr_fifo_t *fifo, void *buf, mr_uint32_t num)`            | 批量预览但不移动读游标，返回实际预览数                    |
| `mr_uint32_t mr_fifo_skip(mr_fifo_t *fifo, mr_uint32_t num)`                       | 丢弃最多 `num` 个最旧元素，返回实际丢弃数                 |

### 零拷贝接口

| API                                                                                                   | 说明                                                   |
|-------------------------------------------------------------------------------------------------------|--------------------------------------------------------|
| `mr_uint32_t mr_fifo_write_claim(mr_fifo_t *fifo, mr_uint32_t off, void **buf, mr_uint32_t want)`     | 申请写游标偏移 `off` 起的连续可写区域，地址写入 `*buf` |
| `mr_uint32_t mr_fifo_write_finish(mr_fifo_t *fifo, mr_uint32_t num)`                                  | 提交已写入的 `num` 个元素                              |
| `mr_uint32_t mr_fifo_overwrite_claim(mr_fifo_t *fifo, mr_uint32_t off, void **buf, mr_uint32_t want)` | 申请连续覆盖写区域（允许越过当前填充量）               |
| `mr_uint32_t mr_fifo_overwrite_finish(mr_fifo_t *fifo, mr_uint32_t num)`                              | 提交覆盖写入；空间不足时自动推进读游标                 |
| `mr_uint32_t mr_fifo_read_claim(mr_fifo_t *fifo, mr_uint32_t off, void **buf, mr_uint32_t want)`      | 申请读游标偏移 `off` 起的连续可读区域                  |
| `void mr_fifo_read_finish(mr_fifo_t *fifo, mr_uint32_t num)`                                          | 提交消费 `num` 个元素                                  |

### 状态查询（内联）

| API                                                                | 说明                                           |
|--------------------------------------------------------------------|------------------------------------------------|
| `mr_fifo_len(fifo)` / `mr_fifo_avail(fifo)` / `mr_fifo_size(fifo)` | 当前元素数 / 剩余可写数 / 总容量               |
| `mr_fifo_is_empty(fifo)` / `mr_fifo_is_full(fifo)`                 | 是否为空 / 是否已满                            |
| `mr_fifo_reset(fifo)`                                              | 将 `ridx` 与 `widx` 原子清零（不擦除缓冲内容） |

## 内部实现

核心实现位于 `kernel/fifo.c`：

- **两圈游标编码**：`ridx` 与 `widx` 取值范围为 `[0, 2 * capacity)`。物理槽位通过
  `cursor >= capacity ? cursor - capacity : cursor` 折叠计算；这样 `widx == ridx`
  表示队空，`|widx - ridx| == capacity` 表示队满，无需牺牲一个存储槽位即可区分满与空。
- **容量上限**：`FIFO_CAPACITY_MAX` 限制为 `MR_INT32_MAX / 2 + 1`，确保
  `2 * capacity` 不会溢出 `uint32_t`。
- **双段搬运**：批量读写跨过缓冲区物理末尾时，通过 `MR_MIN(num, size - index)`
  拆分为尾段与头段两次 `mr_memcpy`，随后通过 `mr_atomic_compare_exchange()`
  一次性提交游标。
