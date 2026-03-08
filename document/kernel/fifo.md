# fifo - 循环缓冲区

## 1. 模块概述

### 1.1 功能介绍

fifo 模块实现了一个高效的循环缓冲区（Circular Buffer），也称 FIFO（First In First Out）队列。它使用原子索引实现无锁并发访问，专为多线程和中断上下文设计。

fifo 模块的核心功能：

- **循环缓冲**：自动回绕，无需手动处理边界
- **原子操作**：使用原子索引，支持多线程并发读写
- **灵活初始化**：支持静态缓冲区和动态分配
- **批量操作**：支持单元素和批量读写
- **覆盖模式**：支持满时自动覆盖旧数据
- **零拷贝预取**：claim/finish 机制实现零拷贝访问

### 1.2 主要特性

- **无锁设计**：使用原子操作的读写索引
- **容量对齐**：自动对齐到 2 的幂次，使用位运算实现快速取模
- **类型灵活**：支持任意大小的元素（通过 elemsz 参数）
- **状态查询**：提供长度、可用空间、空/满状态等查询
- **零拷贝访问**：通过 claim 机制直接访问内部缓冲区
- **覆盖写入**：overwrite 系列接口支持满时覆盖旧数据

### 1.3 适用场景

fifo 模块适用于以下场景：

- **生产者-消费者**：多线程数据传递
- **中断通信**：中断与主线程之间的数据交换
- **数据缓冲**：串口、网络等数据流缓冲
- **音频处理**：音频采样缓冲区
- **日志缓冲**：日志数据的临时存储
- **传感器数据**：传感器采样数据的缓存

**不适用场景**：

- **需要随机访问**：FIFO 只支持顺序访问
- **需要优先级**：FIFO 是先进先出，无优先级
- **超大容量**：容量需要对齐到 2 的幂次，可能浪费内存

### 1.4 使用注意事项

⚠️ **重要警告**：

1. **容量对齐**：初始化时容量会自动对齐到 2 的幂次（向下取整）

2. **原子索引溢出**：读写索引使用 `mr_atomic_t`，溢出后会自动回绕

3. **覆盖模式**：`overwrite` 系列接口会自动丢弃最旧的数据

4. **线程安全**：单个生产者或单个消费者时完全无锁；多生产者/多消费者时需外部同步

5. **claim/finish 配对**：使用 claim 后必须调用对应的 finish，否则索引不会更新

---

## 2. 使用指南

### 2.1 核心数据结构

#### mr_fifo_t

```c
typedef struct mr_fifo {
    mr_atomic_t ridx, widx;    /* 读索引和写索引 */
    mr_uint32_t elemsz;        /* 元素大小（字节） */
    mr_uint32_t mask;          /* 容量掩码（capacity - 1） */
    mr_uint8_t *buf;           /* 缓冲区指针 */
} mr_fifo_t;
```

**字段说明**：

- `ridx`：读索引（原子类型），指向下一个要读取的元素
- `widx`：写索引（原子类型），指向下一个要写入的位置
- `elemsz`：每个元素的大小（字节）
- `mask`：容量掩码，用于快速取模 `index & mask`
- `buf`：指向数据缓冲区的指针

**内存布局**：

```
fifo 内存布局 (capacity = 8, elemsz = 4):

缓冲区 buf (32 字节):
+------+------+------+------+------+------+------+------+
|  0   |  1   |  2   |  3   |  4   |  5   |  6   |  7   |
+------+------+------+------+------+------+------+------+
  ^                                    ^    ^
  |                                    |    |
  ridx = 0                          widx = 6  mask = 7

已用: 6 个元素 (widx - ridx = 6)
可用: 2 个元素 (mask + 1 - len = 2)
```

**索引回绕示例**：

```
初始状态: ridx = 0, widx = 0
写入 8 个元素后: ridx = 0, widx = 8
读取 3 个元素后: ridx = 3, widx = 8
再写入 2 个元素: ridx = 3, widx = 10

实际位置:
- ridx 实际位置 = 3 & 7 = 3
- widx 实际位置 = 10 & 7 = 2 (回绕!)
```

### 2.2 主要接口说明

#### 初始化接口

##### mr_fifo_init()

```c
mr_uint32_t mr_fifo_init(mr_fifo_t *fifo,
                         void *buf,
                         mr_uint32_t size,
                         mr_uint32_t elemsz);
```

**功能**：使用用户提供的缓冲区初始化 fifo

**参数**：

- `fifo`：fifo 对象指针
- `buf`：缓冲区指针
- `size`：缓冲区大小（字节）
- `elemsz`：元素大小（字节）

**返回值**：实际容量（元素数量），会向下对齐到 2 的幂次

**示例**：

```c
#define FIFO_BUF_SIZE  128
#define ELEM_SIZE      4

mr_uint8_t fifo_buf[FIFO_BUF_SIZE];
mr_fifo_t fifo;

/* 初始化 fifo */
mr_uint32_t capacity = mr_fifo_init(&fifo,
                                    fifo_buf,
                                    sizeof(fifo_buf),
                                    ELEM_SIZE);
/* capacity = 32 (128 / 4 = 32，且 32 是 2 的幂次) */

/* 如果请求 100 字节，实际容量是 24 个元素（96 字节） */
capacity = mr_fifo_init(&fifo, fifo_buf, 100, 4);
/* capacity = 24 (96 / 4 = 24，向下对齐到 2 的幂次) */
```

##### mr_fifo_alloc()

```c
mr_uint32_t mr_fifo_alloc(mr_fifo_t *fifo,
                          mr_uint32_t num,
                          mr_uint32_t elemsz);
```

**功能**：动态分配缓冲区并初始化 fifo

**参数**：

- `fifo`：fifo 对象指针
- `num`：期望的元素数量
- `elemsz`：元素大小（字节）

**返回值**：实际容量（元素数量），会向上对齐到 2 的幂次

**示例**：

```c
mr_fifo_t fifo;

/* 分配 16 个 int 元素的 fifo */
mr_uint32_t capacity = mr_fifo_alloc(&fifo, 16, sizeof(int));
/* capacity = 16 */

/* 分配 20 个 int 元素的 fifo */
capacity = mr_fifo_alloc(&fifo, 20, sizeof(int));
/* capacity = 32 (向上对齐到 2 的幂次) */

/* 使用后释放 */
mr_fifo_free(&fifo);
```

##### mr_fifo_free()

```c
void mr_fifo_free(mr_fifo_t *fifo);
```

**功能**：释放动态分配的 fifo 缓冲区

**参数**：

- `fifo`：fifo 对象指针

**注意**：只释放通过 `mr_fifo_alloc` 分配的缓冲区

#### 单元素操作

##### mr_fifo_push()

```c
mr_uint32_t mr_fifo_push(mr_fifo_t *fifo, const void *data);
```

**功能**：向 fifo 写入一个元素

**参数**：

- `fifo`：fifo 对象指针
- `data`：要写入的数据指针

**返回值**：成功返回 1，fifo 满时返回 0

##### mr_fifo_pop()

```c
mr_uint32_t mr_fifo_pop(mr_fifo_t *fifo, void *data);
```

**功能**：从 fifo 读取一个元素

**参数**：

- `fifo`：fifo 对象指针
- `data`：接收数据的缓冲区指针

**返回值**：成功返回 1，fifo 空时返回 0

**示例**：

```c
int value = 42;

/* 写入 */
if (mr_fifo_push(&fifo, &value) == 0) {
    printf("FIFO is full!\n");
}

/* 读取 */
if (mr_fifo_pop(&fifo, &value) == 1) {
    printf("Read value: %d\n", value);
} else {
    printf("FIFO is empty!\n");
}
```

#### 批量操作

##### mr_fifo_write()

```c
mr_uint32_t mr_fifo_write(mr_fifo_t *fifo,
                          const void *buf,
                          mr_uint32_t num);
```

**功能**：向 fifo 写入多个元素

**参数**：

- `fifo`：fifo 对象指针
- `buf`：要写入的数据缓冲区
- `num`：期望写入的元素数量

**返回值**：实际写入的元素数量（可能小于 num）

##### mr_fifo_read()

```c
mr_uint32_t mr_fifo_read(mr_fifo_t *fifo,
                         void *buf,
                         mr_uint32_t num);
```

**功能**：从 fifo 读取多个元素

**参数**：

- `fifo`：fifo 对象指针
- `buf`：接收数据的缓冲区
- `num`：期望读取的元素数量

**返回值**：实际读取的元素数量（可能小于 num）

**示例**：

```c
int data[10];

/* 写入 10 个元素 */
mr_uint32_t written = mr_fifo_write(&fifo, data, 10);
printf("Written: %u elements\n", written);

/* 读取最多 5 个元素 */
mr_uint32_t read = mr_fifo_read(&fifo, data, 5);
printf("Read: %u elements\n", read);
```

#### 覆盖模式

##### mr_fifo_overwrite()

```c
mr_uint32_t mr_fifo_overwrite(mr_fifo_t *fifo,
                              const void *buf,
                              mr_uint32_t num);
```

**功能**：覆盖模式写入，fifo 满时自动丢弃最旧的数据

**参数**：

- `fifo`：fifo 对象指针
- `buf`：要写入的数据缓冲区
- `num`：期望写入的元素数量

**返回值**：实际写入的元素数量

**示例**：

```c
/* 容量为 16 的 fifo */
int data[20];

/* 尝试写入 20 个元素，会自动丢弃最旧的 4 个 */
mr_uint32_t written = mr_fifo_overwrite(&fifo, data, 20);
/* written = 16 (fifo 容量)，最旧的 4 个元素被丢弃 */
```

#### 状态查询

##### mr_fifo_len()

```c
mr_uint32_t mr_fifo_len(mr_fifo_t *fifo);
```

**功能**：获取 fifo 中当前元素数量

**返回值**：当前元素数量

##### mr_fifo_avail()

```c
mr_uint32_t mr_fifo_avail(mr_fifo_t *fifo);
```

**功能**：获取 fifo 可用空间（元素数量）

**返回值**：可写入的元素数量

##### mr_fifo_size()

```c
mr_uint32_t mr_fifo_size(mr_fifo_t *fifo);
```

**功能**：获取 fifo 总容量

**返回值**：总容量（元素数量）

##### mr_fifo_is_empty()

```c
mr_bool_t mr_fifo_is_empty(mr_fifo_t *fifo);
```

**功能**：判断 fifo 是否为空

**返回值**：`MR_TRUE` 表示空，`MR_FALSE` 表示非空

##### mr_fifo_is_full()

```c
mr_bool_t mr_fifo_is_full(mr_fifo_t *fifo);
```

**功能**：判断 fifo 是否已满

**返回值**：`MR_TRUE` 表示满，`MR_FALSE` 表示未满

**示例**：

```c
/* 状态查询 */
printf("Length: %u / %u\n", mr_fifo_len(&fifo), mr_fifo_size(&fifo));
printf("Available: %u\n", mr_fifo_avail(&fifo));

/* 状态判断 */
if (mr_fifo_is_empty(&fifo)) {
    printf("FIFO is empty\n");
}

if (mr_fifo_is_full(&fifo)) {
    printf("FIFO is full\n");
}
```

#### 其他操作

##### mr_fifo_peek()

```c
mr_uint32_t mr_fifo_peek(mr_fifo_t *fifo,
                         void *buf,
                         mr_uint32_t num);
```

**功能**：查看数据但不消费（不更新读索引）

**参数**：

- `fifo`：fifo 对象指针
- `buf`：接收数据的缓冲区
- `num`：期望查看的元素数量

**返回值**：实际查看的元素数量

##### mr_fifo_skip()

```c
mr_uint32_t mr_fifo_skip(mr_fifo_t *fifo,
                         mr_uint32_t num);
```

**功能**：跳过指定数量的元素（不读取数据）

**参数**：

- `fifo`：fifo 对象指针
- `num`：期望跳过的元素数量

**返回值**：实际跳过的元素数量

##### mr_fifo_reset()

```c
void mr_fifo_reset(mr_fifo_t *fifo);
```

**功能**：重置 fifo（清空所有数据）

**参数**：

- `fifo`：fifo 对象指针

#### 零拷贝操作（Claim/Finish）

##### mr_fifo_write_claim() / mr_fifo_write_finish()

```c
mr_uint32_t mr_fifo_write_claim(mr_fifo_t *fifo,
                                mr_uint32_t off,
                                void **buf,
                                mr_uint32_t want);
mr_uint32_t mr_fifo_write_finish(mr_fifo_t *fifo,
                                 mr_uint32_t num);
```

**功能**：获取写缓冲区指针，直接操作内部内存，零拷贝

**参数**：

- `fifo`：fifo 对象指针
- `off`：偏移量（用于多次 claim）
- `buf`：输出参数，指向内部缓冲区
- `want`：期望的元素数量

**返回值**：实际可写入的元素数量

**示例**：

```c
void *buf;
mr_uint32_t n;

/* Claim 最多 10 个元素的缓冲区 */
n = mr_fifo_write_claim(&fifo, 0, &buf, 10);
if (n > 0) {
    /* 直接写入内部缓冲区，无需 memcpy */
    memcpy(buf, my_data, n * elemsz);

    /* 完成写入，更新写索引 */
    mr_fifo_write_finish(&fifo, n);
}
```

##### mr_fifo_read_claim() / mr_fifo_read_finish()

```c
mr_uint32_t mr_fifo_read_claim(mr_fifo_t *fifo,
                               mr_uint32_t off,
                               void **buf,
                               mr_uint32_t want);
void mr_fifo_read_finish(mr_fifo_t *fifo,
                         mr_uint32_t num);
```

**功能**：获取读缓冲区指针，直接读取内部内存

**示例**：

```c
void *buf;
mr_uint32_t n;

/* Claim 最多 10 个元素的缓冲区 */
n = mr_fifo_read_claim(&fifo, 0, &buf, 10);
if (n > 0) {
    /* 直接从内部缓冲区读取 */
    process_data(buf, n);

    /* 完成读取，更新读索引 */
    mr_fifo_read_finish(&fifo, n);
}
```

##### mr_fifo_overwrite_claim() / mr_fifo_overwrite_finish()

```c
mr_uint32_t mr_fifo_overwrite_claim(mr_fifo_t *fifo,
                                    mr_uint32_t off,
                                    void **buf,
                                    mr_uint32_t want);
mr_uint32_t mr_fifo_overwrite_finish(mr_fifo_t *fifo,
                                     mr_uint32_t num);
```

**功能**：覆盖模式的 claim 操作

### 2.3 典型应用案例

#### 案例 1：基础用法 - 串口数据缓冲

```c
#include <mrX/kernel/mr_fifo.h>

#define UART_BUF_SIZE  256

static mr_uint8_t uart_rx_buf[UART_BUF_SIZE];
static mr_fifo_t uart_rx_fifo;

/* 初始化 */
void uart_buffer_init(void) {
    mr_fifo_init(&uart_rx_fifo,
                 uart_rx_buf,
                 sizeof(uart_rx_buf),
                 sizeof(mr_uint8_t));
}

/* 串口中断：接收数据 */
void UART_IRQHandler(void) {
    mr_uint8_t data;

    while (UART_GetDataAvailable()) {
        data = UART_ReadByte();
        mr_fifo_push(&uart_rx_fifo, &data);
    }
}

/* 主线程：处理接收到的数据 */
void uart_process_data(void) {
    mr_uint8_t data;

    while (!mr_fifo_is_empty(&uart_rx_fifo)) {
        mr_fifo_pop(&uart_rx_fifo, &data);
        /* 处理数据 */
        handle_uart_byte(data);
    }
}
```

#### 案例 2：批量操作 - 音频数据流

```c
#include <mrX/kernel/mr_fifo.h>

#define AUDIO_BUF_SIZE  4096
#define SAMPLE_SIZE     sizeof(int16_t)

static mr_uint8_t audio_buf[AUDIO_BUF_SIZE];
static mr_fifo_t audio_fifo;

/* 初始化音频 fifo */
void audio_init(void) {
    mr_fifo_init(&audio_fifo,
                 audio_buf,
                 sizeof(audio_buf),
                 SAMPLE_SIZE);
}

/* 音频中断：采集音频采样 */
void DAC_IRQHandler(void) {
    int16_t samples[128];

    /* 从 ADC 采集 128 个采样 */
    ADC_ReadSamples(samples, 128);

    /* 批量写入 fifo */
    mr_fifo_write(&audio_fifo, samples, 128);
}

/* 主线程：处理音频数据 */
void audio_process(void) {
    int16_t buffer[256];
    mr_uint32_t n;

    /* 读取最多 256 个采样 */
    n = mr_fifo_read(&audio_fifo, buffer, 256);
    if (n > 0) {
        /* 处理音频数据 */
        audio_filter(buffer, n);
        audio_output(buffer, n);
    }
}
```

#### 案例 3：覆盖模式 - 传感器数据

```c
#include <mrX/kernel/mr_fifo.h>

#define SENSOR_BUF_SIZE  64

typedef struct {
    mr_uint32_t timestamp;
    float value;
} sensor_data_t;

static mr_uint8_t sensor_buf[SENSOR_BUF_SIZE];
static mr_fifo_t sensor_fifo;

/* 初始化 */
void sensor_init(void) {
    mr_fifo_init(&sensor_fifo,
                 sensor_buf,
                 sizeof(sensor_buf),
                 sizeof(sensor_data_t));
}

/* 传感器中断：高频采集 */
void sensor_irq_handler(void) {
    sensor_data_t data;

    data.timestamp = get_timestamp();
    data.value = read_sensor();

    /* 使用覆盖模式：fifo 满时自动丢弃最旧的数据 */
    mr_fifo_overwrite(&sensor_fifo, &data, 1);
}

/* 主线程：获取最新数据 */
void sensor_process(void) {
    sensor_data_t latest;

    /* 总是读取最新的数据 */
    while (mr_fifo_len(&sensor_fifo) > 1) {
        sensor_data_t temp;
        mr_fifo_pop(&sensor_fifo, &temp);
    }

    if (mr_fifo_pop(&sensor_fifo, &latest) == 1) {
        printf("Sensor: %.2f @ %u\n", latest.value, latest.timestamp);
    }
}
```

#### 案例 4：零拷贝操作 - DMA 数据传输

```c
#include <mrX/kernel/mr_fifo.h>

#define DMA_BUF_SIZE  1024

static mr_uint8_t dma_buf[DMA_BUF_SIZE];
static mr_fifo_t dma_fifo;

/* 初始化 */
void dma_buffer_init(void) {
    mr_fifo_init(&dma_fifo,
                 dma_buf,
                 sizeof(dma_buf),
                 sizeof(mr_uint8_t));
}

/* DMA 完成：零拷贝传输 */
void DMA_IRQHandler(void) {
    void *dst;
    mr_uint32_t n;

    /* Claim 可用的缓冲区空间 */
    n = mr_fifo_write_claim(&dma_fifo, 0, &dst, 256);
    if (n >= 128) {
        /* 直接将 DMA 目标设置为 fifo 内部缓冲区 */
        DMA_SetDestination(dst);
        DMA_StartTransfer(128);

        /* DMA 完成后更新写索引 */
        mr_fifo_write_finish(&dma_fifo, 128);
    }
}

/* 数据处理：零拷贝读取 */
void process_dma_data(void) {
    void *src;
    mr_uint32_t n;

    /* Claim 可读的数据 */
    n = mr_fifo_read_claim(&dma_fifo, 0, &src, 512);
    if (n > 0) {
        /* 直接处理 fifo 内部数据，无需 memcpy */
        network_parse_packet(src, n);

        /* 完成读取 */
        mr_fifo_read_finish(&dma_fifo, n);
    }
}
```

#### 案例 5：多线程生产者-消费者

```c
#include <mrX/kernel/mr_fifo.h>

#define MSG_BUF_SIZE  512

typedef struct {
    int type;
    int value;
} message_t;

static mr_uint8_t msg_buf[MSG_BUF_SIZE];
static mr_fifo_t msg_queue;

/* 初始化 */
void message_queue_init(void) {
    mr_fifo_init(&msg_queue,
                 msg_buf,
                 sizeof(msg_buf),
                 sizeof(message_t));
}

/* 生产者线程：发送消息 */
void producer_thread(void) {
    message_t msg;

    while (1) {
        /* 准备消息 */
        msg.type = MSG_TYPE_DATA;
        msg.value = rand();

        /* 尝试发送 */
        if (mr_fifo_push(&msg_queue, &msg) == 0) {
            /* 队列满，等待或丢弃 */
            mr_sleep(1);
        } else {
            printf("Sent message: %d\n", msg.value);
        }
    }
}

/* 消费者线程：接收消息 */
void consumer_thread(void) {
    message_t msg;

    while (1) {
        /* 尝试接收 */
        if (mr_fifo_pop(&msg_queue, &msg) == 1) {
            printf("Received message: %d\n", msg.value);
            /* 处理消息 */
            handle_message(&msg);
        } else {
            /* 队列空，等待 */
            mr_sleep(1);
        }
    }
}
```

#### 案例 6：正确使用示例与常见陷阱

```c
#include <mrX/kernel/mr_fifo.h>

/* ✓ 正确：检查返回值 */
static void correct_push_check(void) {
    int value = 42;
    mr_uint32_t ret = mr_fifo_push(&fifo, &value);
    if (ret == 0) {
        printf("FIFO is full!\n");
    }
}

/* ✓ 正确：批量写入时检查实际写入数量 */
static void correct_batch_write(void) {
    int data[10];
    mr_uint32_t written = mr_fifo_write(&fifo, data, 10);
    printf("Written %u elements\n", written);
}

/* ✗ 错误：未检查返回值 */
static void wrong_no_check(void) {
    int value = 42;
    mr_fifo_push(&fifo, &value);  /* 可能失败 */
}

/* ✓ 正确：处理部分写入 */
static void correct_partial_write(void) {
    int data[100];
    mr_uint32_t written = 0;
    mr_uint32_t total = 100;

    while (written < total) {
        mr_uint32_t n = mr_fifo_write(&fifo,
                                       data + written,
                                       total - written);
        if (n == 0) {
            break;  /* fifo 满了 */
        }
        written += n;
    }
}

/* ✓ 正确：使用 peek 查看数据但不消费 */
static void correct_peek(void) {
    int data;
    mr_fifo_peek(&fifo, &data, 1);
    /* 数据仍在 fifo 中 */
    printf("Next value: %d\n", data);
    /* 稍后真正读取 */
    mr_fifo_pop(&fifo, &data);
}

/* ✗ 错误：忘记调用 finish */
static void wrong_claim_no_finish(void) {
    void *buf;
    mr_uint32_t n = mr_fifo_write_claim(&fifo, 0, &buf, 10);
    if (n > 0) {
        memcpy(buf, data, n * elemsz);
        /* 错误：忘记调用 mr_fifo_write_finish */
        /* 写索引不会更新，下次 claim 会返回同一位置 */
    }
}

/* ✓ 正确：claim/finish 配对 */
static void correct_claim_finish(void) {
    void *buf;
    mr_uint32_t n = mr_fifo_write_claim(&fifo, 0, &buf, 10);
    if (n > 0) {
        memcpy(buf, data, n * elemsz);
        mr_fifo_write_finish(&fifo, n);  /* 更新写索引 */
    }
}

/* ✓ 正确：容量会自动对齐到 2 的幂次 */
static void correct_alignment(void) {
    mr_uint8_t buf[100];
    mr_fifo_t fifo;

    /* 100 字节，元素大小 4 */
    mr_uint32_t cap = mr_fifo_init(&fifo, buf, 100, 4);
    /* cap = 24 (96 字节 / 4 = 24，向下对齐到 2 的幂次) */
}

/* ✗ 错误：假设容量不会被修改 */
static void wrong_capacity_assume(void) {
    mr_fifo_t fifo;
    mr_fifo_alloc(&fifo, 100, 4);

    /* 错误：假设容量是 100 */
    /* 实际容量是 128（向上对齐到 2 的幂次） */
    for (int i = 0; i < 100; i++) {
        int value = i;
        mr_fifo_push(&fifo, &value);  /* 前 100 个成功 */
    }
    /* fifo 没有满，还能再装 28 个 */
}

/* ✓ 正确：使用 mr_fifo_size 获取实际容量 */
static void correct_size_check(void) {
    mr_fifo_t fifo;
    mr_fifo_alloc(&fifo, 100, 4);

    mr_uint32_t size = mr_fifo_size(&fifo);  /* 128 */
    printf("Actual capacity: %u\n", size);
}
```

---

## 3. 设计思路

### 3.1 设计目标

fifo 模块的设计目标：

1. **无锁并发**：使用原子索引，支持多线程和中断
2. **高效回绕**：使用 2 的幂次容量和掩码实现快速取模
3. **灵活使用**：支持静态和动态分配
4. **零拷贝访问**：提供 claim/finish 机制
5. **覆盖模式**：支持传感器等场景的覆盖写入

### 3.2 为什么容量是 2 的幂次

**使用掩码代替取模运算**：

```c
/* 传统方式：取模运算 */
index = widx % capacity;  /* 慢：需要除法指令 */

/* 使用掩码：位与运算 */
index = widx & mask;      /* 快：单条指令 */
```

**为什么有效**：

- 2 的幂次容量的二进制表示只有一个 1
- 例如：16 = 0b10000，mask = 0b01111 = 15
- `x & 15` 等价于 `x % 16`
- 位与运算比除法快得多

**自动对齐**：

```c
/* 向下对齐：mr_fifo_init */
size = FIFO_ALIGN_POW2_DOWN(size / elemsz);
/* 100 → 64, 60 → 32, 31 → 16 */

/* 向上对齐：mr_fifo_alloc */
size = FIFO_ALIGN_POW2_UP(num * elemsz);
/* 100 → 128, 60 → 64, 31 → 32 */
```

### 3.3 原子索引的巧妙设计

**读写索引分离**：

```c
typedef struct mr_fifo {
    mr_atomic_t ridx;  /* 只被消费者修改 */
    mr_atomic_t widx;  /* 只被生产者修改 */
    /* ... */
} mr_fifo_t;
```

**单生产者/单消费者（SPSC）场景**：

- 生产者只修改 `widx`，消费者只修改 `ridx`
- 无需锁，完全无冲突

**多生产者/多消费者（MPMC）场景**：

- `mr_fifo_push` 和 `mr_fifo_write` 使用 `mr_atomic_fetch_add` 保证原子性
- 但返回值检查与实际写入之间可能有竞态
- 需要外部同步（如自旋锁）

### 3.4 索引溢出处理

**索引类型**：`mr_atomic_t` 通常是 32 位或 64 位

**溢出后的行为**：

```c
/* 假设 mr_atomic_t 是 32 位 */
ridx = 0xFFFFFFFF;  /* 2^32 - 1 */
ridx++;
/* ridx = 0（溢出回绕） */

/* 计算元素数量 */
len = widx - ridx;
/* 如果 widx = 10, ridx = 0xFFFFFFF6 */
/* len = 10 - 0xFFFFFFF6 = 20 (正确!) */
```

**关键点**：

- 索引的差值 `widx - ridx` 始终正确
- 无符号整数的溢出回绕是定义行为
- 只要不在一次操作中写入超过 2^32 个元素，就安全

### 3.5 覆盖模式的实现

**覆盖模式 vs 普通模式**：

```c
/* 普通模式：满时拒绝写入 */
mr_uint32_t mr_fifo_write(...) {
    avail = mr_fifo_avail(fifo);
    if (num > avail) {
        num = avail;  /* 只写入可用空间 */
    }
    /* ... */
}

/* 覆盖模式：满时丢弃旧数据 */
mr_uint32_t mr_fifo_overwrite(...) {
    cap = fifo->mask + 1;
    if (num > cap) {
        num = cap;  /* 最多写入容量 */
    }
    avail = mr_fifo_avail(fifo);
    if (num > avail) {
        /* 跳过旧数据，为新数据腾出空间 */
        mr_fifo_skip(fifo, num - avail);
    }
    /* ... */
}
```

**应用场景**：

- **传感器数据**：只关心最新数据
- **音频缓冲**：丢帧比延迟好
- **日志系统**：滚动日志

### 3.6 与其他机制的对比

#### vs 链表队列

| 特性    | fifo    | 链表队列    |
|-------|---------|---------|
| 内存分配  | 静态/一次分配 | 每个节点分配  |
| 内存碎片  | 无       | 有       |
| 访问速度  | O(1)    | O(1)    |
| 缓存友好  | 好（连续内存） | 差（离散节点） |
| 容量限制  | 固定      | 动态      |
| 插入/删除 | 两端      | 灵活      |

#### vs 动态数组

| 特性      | fifo | 动态数组  |
|---------|------|-------|
| FIFO 语义 | 是    | 否     |
| 自动回绕    | 是    | 需手动处理 |
| 线程安全    | 原子索引 | 需要锁   |

#### vs ring_buffer (Linux kfifo)

| 特性   | fifo         | Linux kfifo |
|------|--------------|-------------|
| 接口风格 | 简洁           | 功能丰富        |
| 元素大小 | 任意           | 通常是字节       |
| 覆盖模式 | 支持           | 支持          |
| 零拷贝  | claim/finish | 抽象接口        |

### 3.7 fifo 模块的应用场景

#### 中断与主线程通信

```c
/* 中断上下文：快速写入 */
void UART_IRQHandler(void) {
    uint8_t data = UART_ReadByte();
    mr_fifo_push(&uart_fifo, &data);  /* 极快，无锁 */
}

/* 主线程：批量处理 */
void uart_process(void) {
    uint8_t buffer[32];
    mr_uint32_t n = mr_fifo_read(&uart_fifo, buffer, 32);
    parse_uart_data(buffer, n);
}
```

#### DMA 零拷贝传输

```c
/* 直接将 DMA 目标设为 fifo 内部缓冲区 */
void *buf;
mr_uint32_t n = mr_fifo_write_claim(&dma_fifo, 0, &buf, 256);
DMA_SetDestination(buf);
DMA_Start(n);
mr_fifo_write_finish(&dma_fifo, n);
```

#### 多线程流水线

```c
/* 线程 1：生产数据 */
void producer(void) {
    data_t item = produce();
    mr_fifo_push(&stage1_fifo, &item);
}

/* 线程 2：处理数据 */
void processor(void) {
    data_t item;
    if (mr_fifo_pop(&stage1_fifo, &item)) {
        process(&item);
        mr_fifo_push(&stage2_fifo, &item);
    }
}

/* 线程 3：消费数据 */
void consumer(void) {
    data_t item;
    if (mr_fifo_pop(&stage2_fifo, &item)) {
        consume(&item);
    }
}
```

---

## 4. 参考资料

### 4.1 相关模块

- **atomic**：原子操作模块，提供原子索引支持
- **slab**：固定大小内存池，可用于分配 fifo 缓冲区

### 4.2 设计参考

- **循环缓冲区**：基础数据结构
- **Linux kfifo**：Linux 内核的 FIFO 实现
- **无锁队列**：多线程无锁编程

### 4.3 延伸阅读

- **SPSC 队列**：单生产者单消费者队列
- **MPMC 队列**：多生产者多消费者队列
- **内存屏障**：保证多线程内存访问顺序
