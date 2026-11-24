# 跟踪（Trace）

轻量级内核事件跟踪接口，通过统一宏采集区间（Begin/End）、瞬时事件（Event）和计数器（Counter）数据，经平台端口 `mr_port_trace()` 转发给上层记录器（如 [XTrace](../modules/xtrace/xtrace.md)），关闭时编译期零开销裁剪。

| 项目 | 说明 |
|---|---|
| 功能宏 | `MR_USE_TRACE` |
| 头文件 | `<mrX/kernel/mr_trace.h>`、`<mrX/port/mr_port_trace.h>` |
| 依赖 | [时钟](clock.md)、[平台移植层](../platform/platform.md) |

## 配置

| 配置项 | 类型 | 默认值 | 说明 |
|---|---|---|---|
| `MR_USE_TRACE` | bool | n | 启用内核跟踪框架；关闭时所有 `MR_TRACE_*` 宏展开为 `((void)0)` |
| `MR_CFG_TRACE_ARGS_MAX` | int | 8 | 单条跟踪事件携带的最大参数个数，范围 4~8 |
| `MR_USE_INITCALL_TRACE` | bool | n | 为用户 `initcall` 执行生成跟踪事件 |
| `MR_USE_CLOCK_TRACE` | bool | n | 为时钟忙等待与 tickless 空闲采样生成跟踪事件 |
| `MR_USE_TIMER_TRACE` | bool | n | 为软件定时器启停与超时回调生成跟踪事件 |
| `MR_USE_WORK_TRACE` | bool | n | 为工作队列提交、取消与回调执行生成跟踪事件 |
| `MR_USE_IRQ_TRACE` | bool | n | 为中断处理、延迟执行与开关状态变更生成跟踪事件 |
| `MR_USE_WAIT_TRACE` | bool | n | 为等待队列事件触发、通知与取消生成跟踪事件 |

## 快速上手

每个编译单元需在包含 `<mrX/kernel/mr_trace.h>` 前定义 `MR_TRACE_TAG` 字符串宏，否则该文件内的 `MR_TRACE_*` 宏均展开为空语句：

```c
#define MR_TRACE_TAG                    "driver"
#include <mrX/kernel/mr_trace.h>

void spi_transfer(void *dev, mr_size_t len) {
    MR_TRACE_BEGIN("xfer", dev, len);
    /* 执行传输 */
    MR_TRACE_EVENT("dma_done", dev);
    MR_TRACE_COUNTER("tx_bytes", len);
    MR_TRACE_END("xfer", dev, 0);
}
```

运行期可通过掩码按事件类型动态开关跟踪：

```c
/* 仅保留 BEGIN 与 END 区间事件，屏蔽 EVENT 与 COUNTER */
mr_trace_mask_set(MR_TRACE_MASK(MR_TRACE_TYPE_BEGIN)
                  | MR_TRACE_MASK(MR_TRACE_TYPE_END));
```

## 核心机制与约束

- **编译期与运行期双重过滤**：未启用 `MR_USE_TRACE` 或当前编译单元未定义 `MR_TRACE_TAG` 时，`MR_TRACE_*` 宏在预处理阶段直接替换为 `((void)0)`，不产生任何指令或字符串常量。运行期通过 32 位原子掩码 `__trace_mask`（默认 `0xffffffffU`，全开）按 `type` 过滤；直接调用 `mr_trace()` 仅受运行期掩码控制。
- **参数数量与类型**：`MR_TRACE_BEGIN`、`MR_TRACE_END`、`MR_TRACE_EVENT` 最多接受 8 个参数，每个参数在宏内强转为 `mr_uintptr_t`；若传入 `argc` 超过 `MR_CFG_TRACE_ARGS_MAX`，`mr_trace()` 会截断至 `MR_CFG_TRACE_ARGS_MAX`。`MR_TRACE_COUNTER` 固定传入 1 个参数（先转 `mr_intptr_t` 再转 `mr_uintptr_t`）。
- **时间戳来源**：启用 `MR_USE_CLOCK_HIRES` 时读取 `mr_clock_hires_tick()`，否则读取系统节拍 `mr_clock_tick()`。
- **平台端口契约**：`mr_trace()` 最终调用平台实现的 `mr_port_trace(timestamp, type, tag, name, args, argc)`。由于跟踪宏可能在中断或持锁上下文中调用，`mr_port_trace()` 必须保证非阻塞且中断安全；`args` 指针指向 `mr_trace()` 栈上临时数组，端口层不得在返回后异步引用该指针。
- **异常输入**：`mr_trace()` 遇到 `type >= 32`、`tag` 或 `name` 为 `MR_NULL` 或空字符串 `""` 时直接忽略。

## API 速查

### 事件类型与跟踪宏

事件类型常量：`MR_TRACE_TYPE_BEGIN` (0)、`MR_TRACE_TYPE_END` (1)、`MR_TRACE_TYPE_EVENT` (2)、`MR_TRACE_TYPE_COUNTER` (3)。

| 宏 | 说明 |
|---|---|
| `MR_TRACE_BEGIN(_name, ...)` | 记录区间开始事件，支持 0~8 个附加参数 |
| `MR_TRACE_END(_name, ...)` | 记录区间结束事件，支持 0~8 个附加参数 |
| `MR_TRACE_EVENT(_name, ...)` | 记录瞬时事件，支持 0~8 个附加参数 |
| `MR_TRACE_COUNTER(_name, _val)` | 记录计数器采样值 |
| `MR_TRACE_MASK(_type)` | 生成对应类型的位掩码 `1U << (_type)`（`_type < 32`） |

### 函数接口与数据结构

| API / 类型 | 说明 |
|---|---|
| `mr_trace_event_t` | 事件结构体，字段依次为 `timestamp`、`type`、`tag`、`name`、`args`、`argc` |
| `void mr_trace(mr_uint32_t type, const char *tag, const char *name, mr_size_t argc, ...)` | 发送指定类型的跟踪事件（受运行期掩码过滤） |
| `void mr_trace_mask_set(mr_uint32_t mask)` | 设置运行期 32 位事件类型掩码（对应位置 1 开启，置 0 关闭） |
| `mr_uint32_t mr_trace_mask(void)` | 获取当前运行期事件类型掩码 |
| `void mr_port_trace(mr_uint64_t timestamp, mr_uint32_t type, const char *tag, const char *name, const mr_uintptr_t *args, mr_size_t argc)` | 平台移植端口（`<mrX/port/mr_port_trace.h>`），接收并转发事件 |
