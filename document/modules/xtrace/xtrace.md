# 可扩展跟踪模块（XTrace）

基于“协议编码器 + 异步环形缓冲 + 拉取通知式输出适配器”架构的系统跟踪组件。接收来自内核 [Trace](../../kernel/trace.md) 端口的事件，支持 CTF 1.8、SEGGER SystemView 或自定义协议编码，以及运行期热切换编码器与传输通道。

| 项目 | 说明 |
|---|---|
| 功能宏 | `MR_USE_MODULE_XTRACE`（自动选中 `MR_USE_TRACE`） |
| 头文件 | `<mrX/modules/xtrace/mr_xtrace.h>`、`<mrX/modules/xtrace/proto/mr_xtrace_ctf.h>`、`<mrX/modules/xtrace/proto/mr_xtrace_sysview.h>` |
| 依赖 | [跟踪](../../kernel/trace.md)、[对象系统](../../kernel/object.md)、[环形缓冲](../../kernel/fifo.md)、[平台移植层](../../platform/platform.md) |

## 配置

| 配置项 | 类型 | 默认值 | 说明 |
|---|---|---|---|
| `MR_USE_MODULE_XTRACE` | bool | n | 启用 XTrace 模块，自动选中 `MR_USE_TRACE` |
| `MR_USE_MODULE_XTRACE_AUTO_START` | bool | y | 模块初始化阶段自动开启采集；输出适配器注册前的事件先缓存在 FIFO 中 |
| `MR_CFG_XTRACE_BUF_SIZE` | int | 4096 | 内部异步 FIFO 缓冲区大小（字节），范围 256~65536 |
| `MR_CFG_XTRACE_APP_NAME` | string | `"mrX"` | 系统元数据中报告的默认应用名 |
| `MR_CFG_XTRACE_DEVICE_NAME` | string | `""` | 系统元数据中报告的默认设备或目标板名 |
| `MR_USE_MODULE_XTRACE_PROTO_NONE` | bool | n | 不启用内置协议，由应用通过 `mr_xtrace_register()` 注册自定义协议 |
| `MR_USE_MODULE_XTRACE_PROTO_CTF` | bool | y | 启用 Common Trace Format (CTF 1.8) 协议编码器（默认选项） |
| `MR_CFG_XTRACE_CTF_PACKET_SIZE` | int | 256 | 单个 CTF 数据包提交至 FIFO 前的最大字节数，范围 256~4096 |
| `MR_USE_MODULE_XTRACE_PROTO_SYSVIEW` | bool | n | 启用 SEGGER SystemView 二进制协议编码器 |
| `MR_CFG_SYSVIEW_MARKERS` | int | 32 | SystemView 模式下缓存的不同 `BEGIN`/`END` marker 名称上限，范围 8~256 |

## 快速上手

### 1. 使用内置协议与平台输出

启用 `MR_USE_MODULE_XTRACE` 与内置协议（如 CTF 或 SystemView）后，协议编码器会在 `MR_INIT_LEVEL_MODULE` 阶段自动注册。可在应用初始化时补充目标板与时钟信息：

```c
#include <mrX/modules/xtrace/mr_xtrace.h>

void app_trace_setup(void) {
    mr_xtrace_sysinfo_t info = {
        .app_name = "motor_ctrl",
        .device_name = "stm32h743",
        .core_name = "Cortex-M7",
        .cpu_freq = 480000000U,
    };

    mr_xtrace_sysinfo_set(&info);
}
```

### 2. 实现自定义输出适配器（Pull + Notify 模型）

输出适配器只需实现 `mr_xtrace_output_ops_t`：在 `notify()` 中唤醒后台线程/DMA/工作队列，在后台上下文中通过 `mr_xtrace_read_claim()` 与 `mr_xtrace_read_finish()` 零拷贝排空 FIFO：

```c
#include <mrX/modules/xtrace/mr_xtrace.h>

static void uart_notify(void *priv) {
    /* 唤醒异步发送任务，禁止在 notify() 内阻塞或直接调用 mr_xtrace_read* */
    trigger_tx_worker(priv);
}

static void uart_tx_worker(void *priv) {
    mr_size_t len;
    void *ptr;

    while ((len = mr_xtrace_read_claim(&ptr, 256U)) > 0U) {
        mr_size_t sent = uart_send_chunk(priv, ptr, len);
        mr_xtrace_read_finish(sent);
        if (sent < len) {
            break;
        }
    }
}

static const mr_xtrace_output_ops_t uart_out_ops = {
    .notify = uart_notify,
    .init = MR_NULL,
    .deinit = MR_NULL,
    .read = MR_NULL,
};
static mr_xtrace_output_t uart_out = MR_XTRACE_OUTPUT_INIT(&uart_out_ops, MR_NULL);

void register_uart_trace(void) {
    mr_xtrace_output_register(&uart_out);
}
```

## 核心机制与约束

```text
+-------------------+     +------------------+     +--------------------+     +--------------------+
| 内核 mr_trace()   | --> | mr_port_trace()  | --> | mr_xtrace_emit()   | --> | 协议编码器         |
| (MR_TRACE_*)      |     | (平台端口)       |     | (校验/运行态检查)  |     | (CTF/SysView/自定) |
+-------------------+     +------------------+     +--------------------+     +---------+----------+
                                                                                        | mr_xtrace_write()
+-------------------+     +------------------+     +--------------------+               v
| 传输介质          | <-- | 输出适配器       | <-- | 边沿合并 notify()  | <--- [ 内部异步 FIFO ]
| (RTT/UART/TCP/文件)|    | read[_claim]     |     | (armed + pending)  |      (MR_CFG_XTRACE_BUF_SIZE)
+-------------------+     +------------------+     +--------------------+
```

- **异步拉取模型与非阻塞约束**：`mr_xtrace_emit()` 与 `mr_xtrace_write()` 可在中断或持锁上下文调用，仅做快速编码与 FIFO 入队，满时丢弃并递增 `mr_xtrace_dropped_count()`。`notify()` 采用边沿合并（armed/pending）机制：FIFO 从空变非空或仍有残留数据时通知输出适配器；**禁止在 `notify()` 回调内直接调用 `mr_xtrace_read()`、`mr_xtrace_read_claim()` 或 `mr_xtrace_read_finish()`**，否则会被判定为重入或导致死锁，必须交由独立任务/线程/DMA 完成读取。
- **零拷贝读取三态机**：`mr_xtrace_read()` 与 `mr_xtrace_read_claim()`/`mr_xtrace_read_finish()` 面向**单消费者**互斥访问；在 `claim` 尚未 `finish` 期间再次 `claim` 或 `read` 会返回 `0`（若传入 `len > claimed` 给 `finish`，内部会钳位至实际 `claimed` 长度）。`mr_xtrace_clear()` 会跳过正在进行的 `claim` 并返回 `-MR_EBUSY`。
- **对象生命周期与热切换**：协议编码器（`mr_xtrace_t`）与输出适配器（`mr_xtrace_output_t`）均继承自 `mr_object_t`。注册新实例前若已有激活实例，会先自动注销旧实例（若旧实例已处于运行态会先执行 `stop`/`flush`）；切换协议前会自动清空 FIFO 中残留的旧协议字节流。静态对象使用 `MR_XTRACE_INIT` / `MR_XTRACE_OUTPUT_INIT` 宏初始化，动态对象用 `mr_xtrace_init()` / `mr_xtrace_output_init()` 初始化并在注销后调用对应 `*_del()`。
- **系统元数据默认值**：`mr_xtrace_sysinfo_get()` 会快照内部配置并填充派生默认值：未显式设置 `ts_freq` 时，启用 `MR_USE_CLOCK_HIRES` 默认为 `MR_CFG_HIRES_TICK_PER_SECOND`，否则为 `MR_CFG_TICK_PER_SECOND`；`cpu_freq` 为 `0` 时默认等于 `ts_freq`；`anchor_addr` 为 `0` 时默认取 `(mr_uintptr_t)&mr_xtrace_init`，用于主机端校准重定位或 ASLR 地址偏移。
- **内置 CTF 1.8 协议**：支持通过 `mr_xtrace_ctf_metadata()` 生成标准 TSDL metadata 文本，定义 4 类事件 ID（0=`trace_begin`、1=`trace_end`、2=`trace_event`、3=`trace_counter`）。每个包头 `packet.context` 除标准字段（`timestamp_begin`、`timestamp_end`、`content_size`、`packet_size`、`events_discarded`）外，还附带 `seq`、`anchor_addr`、`ts_freq`、`cpu_freq`、`ptr_bits`，便于串口或 TCP 实时接入时自描述解包与时间换算。
- **内置 SEGGER SystemView 协议**：首次发送事件时自动写入 10 字节全零同步头、`TRACE_START`、`INIT` 与 `SYSDESC` 包；时间戳按差值（delta）做 32 位截断编码；`BEGIN`/`END` marker 名称通过静态表动态分配 ID（上限由 `MR_CFG_SYSVIEW_MARKERS` 控制，超出则回落为打印/丢弃）。

## API 速查

### 控制与事件采集

| API | 说明 |
|---|---|
| `void mr_xtrace_start(void)` | 开启跟踪记录，并触发当前协议的 `start()` 回调 |
| `void mr_xtrace_stop(void)` | 停止跟踪记录，并触发当前协议的 `stop()` 回调 |
| `mr_bool_t mr_xtrace_is_running(void)` | 查询当前是否处于运行状态 |
| `void mr_xtrace_emit(const mr_trace_event_t *event)` | 投递一条跟踪事件到当前协议编码器（未运行或未注册协议时忽略） |
| `mr_err_t mr_xtrace_flush(void)` | 请求当前协议编码器将缓存中的未提交数据刷入 FIFO |

### 协议编码器管理

| API / 宏 | 说明 |
|---|---|
| `MR_XTRACE_INIT(_ops, _priv)` | 静态初始化 `mr_xtrace_t` 协议对象 |
| `mr_err_t mr_xtrace_init(mr_xtrace_t *xtrace, const mr_xtrace_ops_t *ops, void *priv)` | 动态初始化协议对象（`ops->emit` 不可为 `MR_NULL`） |
| `void mr_xtrace_del(mr_xtrace_t *xtrace)` | 释放由 `mr_xtrace_init()` 初始化的协议对象 |
| `mr_err_t mr_xtrace_register(mr_xtrace_t *xtrace)` | 注册并激活协议编码器（自动替换旧协议并清空 FIFO） |
| `mr_err_t mr_xtrace_unregister(void)` | 注销当前协议编码器 |

### 输出适配器管理

| API / 宏 | 说明 |
|---|---|
| `MR_XTRACE_OUTPUT_INIT(_ops, _priv)` | 静态初始化 `mr_xtrace_output_t` 输出对象 |
| `mr_err_t mr_xtrace_output_init(mr_xtrace_output_t *output, const mr_xtrace_output_ops_t *ops, void *priv)` | 动态初始化输出适配器（`ops->notify` 不可为 `MR_NULL`） |
| `void mr_xtrace_output_del(mr_xtrace_output_t *output)` | 释放由 `mr_xtrace_output_init()` 初始化的输出对象 |
| `mr_err_t mr_xtrace_output_register(mr_xtrace_output_t *output)` | 注册并激活输出适配器（若 FIFO 已有积压数据会立即触发一次 `notify()`） |
| `mr_err_t mr_xtrace_output_unregister(void)` | 注销当前输出适配器 |
| `mr_ssize_t mr_xtrace_output_read(void *buf, mr_size_t size)` | 从当前输出适配器读取主机下发数据（需适配器实现 `ops->read`） |

### FIFO 读写与系统信息

| API | 说明 |
|---|---|
| `mr_ssize_t mr_xtrace_write(const void *data, mr_size_t size)` | 协议编码器写入编码字节到 FIFO（不足 `size` 时原子拒绝并返回 `-MR_EAGAIN`） |
| `mr_size_t mr_xtrace_read(void *buf, mr_size_t size)` | 输出适配器拷贝读取 FIFO 数据 |
| `mr_size_t mr_xtrace_read_claim(void **buf, mr_size_t want)` | 零拷贝声明一段连续可读 FIFO 区域，返回实际连续字节数 |
| `void mr_xtrace_read_finish(mr_size_t len)` | 归还并确认消费此前 `claim` 区域中的 `len` 字节（若仍有剩余数据会再次武装通知） |
| `mr_size_t mr_xtrace_pending(void)` | 获取 FIFO 当前待读取字节数 |
| `mr_err_t mr_xtrace_clear(void)` | 清空 FIFO 中的未读数据（存在未完成的 `claim` 时返回 `-MR_EBUSY`） |
| `void mr_xtrace_sysinfo_set(const mr_xtrace_sysinfo_t *info)` | 合并更新系统元数据（仅覆盖非空字符串与非零数值字段） |
| `void mr_xtrace_sysinfo_get(mr_xtrace_sysinfo_t *info)` | 获取系统元数据快照并填充派生默认值 |
| `mr_uint32_t mr_xtrace_timestamp_freq(void)` | 获取当前时间戳时钟频率（Hz） |
| `mr_uint32_t mr_xtrace_dropped_count(void)` | 获取累计丢弃的事件/数据包计数 |
| `void mr_xtrace_dropped_inc(void)` | 递增丢弃计数 |

### 内置协议接口

| API | 头文件 | 说明 |
|---|---|---|
| `mr_ssize_t mr_xtrace_ctf_metadata(char *buf, mr_size_t size)` | `<mrX/modules/xtrace/proto/mr_xtrace_ctf.h>` | 格式化输出 CTF 1.8 TSDL metadata 文本（不含末尾 `'\0'`），缓冲不足返回 `-MR_ENOMEM` |
| `mr_err_t mr_xtrace_ctf_register(void)` | `<mrX/modules/xtrace/proto/mr_xtrace_ctf.h>` | 注册内置 CTF 协议编码器 |
| `mr_err_t mr_xtrace_sysview_register(void)` | `<mrX/modules/xtrace/proto/mr_xtrace_sysview.h>` | 注册内置 SEGGER SystemView 协议编码器 |
