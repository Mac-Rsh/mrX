[**中文**](kernel_fifo.md) | [**English**](kernel_fifo_en.md)

---

# FIFO 环形缓冲区

`mr_fifo` 模块提供固定容量的环形 FIFO 缓冲区。缓冲区以元素为单位读写，内部使用原子读写索引 `ridx` 和 `widx`
记录读写位置，并通过容量掩码完成回绕。

FIFO 支持调用者提供外部缓冲区，也支持通过 `mr_fifo_alloc()` 动态分配缓冲区。普通写入在空间不足时只写入可用部分，覆盖写入会丢弃最旧数据以保留最新数据。

## 核心数据结构和宏

### FIFO 控制块

```c
typedef struct mr_fifo {
    mr_atomic_t ridx, widx;
    mr_uint32_t elemsz;
    mr_uint32_t mask;
    mr_uint8_t *buf;
} mr_fifo_t;
```

字段含义如下：

| 字段       | 含义                                |
|----------|-----------------------------------|
| `ridx`   | 读索引，弹出、读取、跳过数据时递增。                |
| `widx`   | 写索引，推入、写入、写完成时递增。                 |
| `elemsz` | 单个元素大小，单位为字节。                     |
| `mask`   | 容量掩码，实际容量为 `mask + 1`，容量按 2 的幂对齐。 |
| `buf`    | 数据缓冲区指针。为 `MR_NULL` 时表示未初始化或已释放。  |

### 容量对齐

源码中使用两个内部宏处理容量：

```c
#define FIFO_ALIGN_POW2_DOWN(_size)     (1U << (mr_fls32(_size) - 1))
#define FIFO_ALIGN_POW2_UP(_size) \
    (((_size) & ((_size) - 1U)) ? (1U << mr_fls32(_size)) : (_size))
```

`mr_fifo_init()` 使用向下对齐，外部缓冲区可容纳的元素数会被截断为不超过原始容量的 2 的幂。`mr_fifo_alloc()` 使用向上对齐，请求元素数不是
2 的幂时会分配到更大的 2 的幂容量。

## 模块接口

### 使用外部缓冲区初始化

```c
mr_uint32_t mr_fifo_init(mr_fifo_t *fifo, void *buf, mr_uint32_t size,
                         mr_uint32_t elemsz);
```

`size` 是缓冲区字节数，`elemsz` 是元素字节数。成功时返回实际元素容量，失败时返回 `0`。当 `buf == MR_NULL`、`elemsz == 0` 或
`size < elemsz` 时初始化失败。

### 动态分配和释放

```c
mr_uint32_t mr_fifo_alloc(mr_fifo_t *fifo, mr_uint32_t num,
                          mr_uint32_t elemsz);
void mr_fifo_free(mr_fifo_t *fifo);
```

`mr_fifo_alloc()` 按元素数量和元素大小分配缓冲区，实际容量向上对齐到 2 的幂。参数非法、乘法溢出或内存不足时返回 `0`。
`mr_fifo_free()` 释放 `fifo->buf` 并将其置为 `MR_NULL`；若 `fifo->buf` 已为空则直接返回。

### 单元素读写

```c
mr_uint32_t mr_fifo_push(mr_fifo_t *fifo, const void *data);
mr_uint32_t mr_fifo_pop(mr_fifo_t *fifo, void *data);
```

`mr_fifo_push()` 在 FIFO 未满时写入一个元素，成功返回 `1`，未初始化或已满返回 `0`。`mr_fifo_pop()` 在 FIFO
非空时读取并移除一个元素，成功返回 `1`，未初始化或为空返回 `0`。

### 批量读写

```c
mr_uint32_t mr_fifo_write(mr_fifo_t *fifo, const void *buf, mr_uint32_t num);
mr_uint32_t mr_fifo_read(mr_fifo_t *fifo, void *buf, mr_uint32_t num);
mr_uint32_t mr_fifo_peek(mr_fifo_t *fifo, void *buf, mr_uint32_t num);
```

`mr_fifo_write()` 最多写入当前可用空间数量的元素，并返回实际写入数量。`mr_fifo_peek()` 最多复制当前已有数量的元素但不移动读索引。
`mr_fifo_read()` 先执行 `mr_fifo_peek()`，再把读索引增加实际读取数量。

### 覆盖写入

```c
mr_uint32_t mr_fifo_overwrite(mr_fifo_t *fifo, const void *buf,
                              mr_uint32_t num);
```

`mr_fifo_overwrite()` 写入时允许覆盖旧数据。若 `num` 大于 FIFO 容量，只保留输入缓冲区最后 `capacity`
个元素；若空间不足，会先跳过最旧元素，再写入新数据。

### 状态查询和控制

```c
MR_INLINE mr_uint32_t mr_fifo_len(mr_fifo_t *fifo);
MR_INLINE mr_uint32_t mr_fifo_avail(mr_fifo_t *fifo);
MR_INLINE mr_uint32_t mr_fifo_size(mr_fifo_t *fifo);
MR_INLINE mr_uint32_t mr_fifo_skip(mr_fifo_t *fifo, mr_uint32_t num);
MR_INLINE void mr_fifo_reset(mr_fifo_t *fifo);
MR_INLINE mr_bool_t mr_fifo_is_empty(mr_fifo_t *fifo);
MR_INLINE mr_bool_t mr_fifo_is_full(mr_fifo_t *fifo);
```

`mr_fifo_len()` 返回已有元素数，`mr_fifo_avail()` 返回可写空间，`mr_fifo_size()` 返回容量。`mr_fifo_skip()` 丢弃最多 `num`
个已有元素并返回实际跳过数量。`mr_fifo_reset()` 将读写索引清零，不清除缓冲区内容。

### 零拷贝声明和完成

```c
mr_uint32_t mr_fifo_write_claim(mr_fifo_t *fifo, mr_uint32_t off, void **buf,
                                mr_uint32_t want);
MR_INLINE mr_uint32_t mr_fifo_write_finish(mr_fifo_t *fifo, mr_uint32_t num);

mr_uint32_t mr_fifo_overwrite_claim(mr_fifo_t *fifo, mr_uint32_t off,
                                    void **buf, mr_uint32_t want);
MR_INLINE mr_uint32_t mr_fifo_overwrite_finish(mr_fifo_t *fifo,
                                               mr_uint32_t num);

mr_uint32_t mr_fifo_read_claim(mr_fifo_t *fifo, mr_uint32_t off, void **buf,
                               mr_uint32_t want);
MR_INLINE void mr_fifo_read_finish(mr_fifo_t *fifo, mr_uint32_t num);
```

`claim` 接口返回从当前读写位置加 `off` 后开始的一段连续内存，返回值为本次可连续访问的元素数。由于环形缓冲区可能回绕，返回值可能小于请求值。写入或读取完成后，调用者需要调用对应的
`finish` 接口推进索引。

## 使用示例

### 外部缓冲区读写

```c
#include <mrX/kernel/mr_fifo.h>

typedef struct sample_elem {
    int id;
} sample_elem_t;

void sample_fifo_usage(void)
{
    mr_uint8_t buf[128];
    mr_fifo_t fifo;
    sample_elem_t in = {.id = 42};
    sample_elem_t out;

    if (mr_fifo_init(&fifo, buf, sizeof(buf), sizeof(sample_elem_t)) == 0) {
        return;
    }

    mr_fifo_push(&fifo, &in);
    mr_fifo_pop(&fifo, &out);
}
```

### 声明连续写缓冲区

```c
void sample_fifo_claim(mr_fifo_t *fifo)
{
    void *ptr;
    mr_uint32_t n;

    n = mr_fifo_write_claim(fifo, 0, &ptr, 4);
    if (n > 0) {
        /* 调用者在 ptr 指向的连续区域写入 n 个元素。 */
        mr_fifo_write_finish(fifo, n);
    }
}
```

## 注意事项

- `mr_fifo_init()` 不清理参数非法时的 `fifo` 内容；测试中使用零初始化控制块检查失败路径。
- `mr_fifo_alloc()` 分配的缓冲区必须通过 `mr_fifo_free()` 释放；外部缓冲区初始化的 FIFO 不应调用 `mr_fifo_free()` 释放外部内存。
- 批量读写会按可读或可写数量截断，返回值才是实际处理的元素数量。
- `peek` 不推进读索引，`read` 会推进读索引。
- `claim` 返回的是连续片段，不保证一次覆盖跨回绕的全部请求；需要处理返回值小于请求值的情况。
- 模块使用原子索引操作，但未提供完整的多生产者、多消费者同步协议；复杂并发场景需要调用者额外约束访问模型或加锁。
