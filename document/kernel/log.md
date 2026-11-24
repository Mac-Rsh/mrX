# 日志（Log）

基于格式化输出提供带时间戳、模块标签与五级过滤的调试日志能力，支持编译期零开销裁剪与运行期掩码动态开关。

| 项目   | 说明                                          |
|--------|-----------------------------------------------|
| 功能宏 | `MR_USE_LOG`（自动选中 `MR_USE_PRINTF`）      |
| 头文件 | `<mrX/kernel/mr_log.h>`                       |
| 依赖   | [格式化输出](printf.md)、[时钟](clock.md)     |
| 测试   | `test/kernel/log.c`、`test/kernel/log_deep.c` |

## 配置

| 配置项                 | 类型 | 默认值 | 说明                                                                             |
|------------------------|------|--------|----------------------------------------------------------------------------------|
| `MR_USE_LOG`           | bool | y      | 启用日志模块，自动选中 `MR_USE_PRINTF`                                           |
| `MR_CFG_LOG_LEVEL`     | int  | 4      | 编译期最高输出级别，范围 0~4，对应 `ERR`(0) ~ `TRC`(4)（以 `mr_log.h` 定义为准） |
| `MR_CFG_LOG_TIME_ZONE` | int  | 8      | 高分辨率时间戳的 UTC 时区偏移（-12~12），仅启用 `MR_USE_CLOCK_HIRES` 时生效      |

## 快速上手

每个编译单元需在包含 `<mrX/kernel/mr_log.h>` 前定义 `MR_LOG_TAG` 字符串宏，否则所有
`MR_LOG_xxx` 宏均展开为空语句：

```c
#define MR_LOG_TAG                      "sensor"
#include <mrX/kernel/mr_log.h>

void sensor_check(int val) {
    MR_LOG_INF("raw sample: %d", val);
    if (val < 0) {
        MR_LOG_ERR("invalid sample");
    }
}
```

未启用高分辨率时钟时，输出格式如下（宏已自动附加 `\r\n`）：

```text
[   1.234] [INF/sensor] raw sample: -1
[   1.235] [ERR/sensor] invalid sample
```

## 核心机制与约束

- **编译期与运行期双重过滤**：`MR_CFG_LOG_LEVEL` 低于某宏级别（或未定义
  `MR_LOG_TAG`）时，宏在预处理阶段直接替换为 `((void)0)`；运行期通过 32 位位掩码（初始值
  `0xffffffffU`，全开）控制 `mr_log()` 是否实际输出。直接调用 `mr_log()` 不受
  `MR_CFG_LOG_LEVEL` 和 `MR_LOG_TAG` 约束，仅受运行期掩码控制。
- **时间戳格式**：未启用 `MR_USE_CLOCK_HIRES` 时输出 `[秒.毫秒]`（来自
  `mr_clock_uptime()`）；启用后通过 `mr_hires_tick2timestamp()` 按
  `MR_CFG_LOG_TIME_ZONE` 换算并输出 `[YYYY-MM-DD-HH:MM:SS:mmm:uuu]`。
- **并发与中断安全**：时间戳与日志正文分两次调用 `mr_printf` / `mr_vprintf`
  输出，且全局掩码 `__log_level` 无锁保护。多上下文并发打印可能出现交错，需启用
  `MR_USE_PRINTF_SAFE` 并保证底层 `mr_port_output()` 线程/中断安全。
- **异常输入**：`mr_log()` 遇到 `level >= 32`、`fmt == MR_NULL` 或空字符串 `""`
  时静默忽略。

## API 速查

### 日志宏与级别常量

级别常量：`MR_LOG_LEVEL_ERR` (0)、`MR_LOG_LEVEL_WRN` (1)、`MR_LOG_LEVEL_INF` (2)、
`MR_LOG_LEVEL_DBG` (3)、`MR_LOG_LEVEL_TRC` (4)。

| 宏                      | 说明                                                                |
|-------------------------|---------------------------------------------------------------------|
| `MR_LOG_ERR(_fmt, ...)` | 输出 `ERR` 级别日志（自动带标签与 `\r\n`）                          |
| `MR_LOG_WRN(_fmt, ...)` | 输出 `WRN` 级别日志                                                 |
| `MR_LOG_INF(_fmt, ...)` | 输出 `INF` 级别日志                                                 |
| `MR_LOG_DBG(_fmt, ...)` | 输出 `DBG` 级别日志                                                 |
| `MR_LOG_TRC(_fmt, ...)` | 输出 `TRC` 级别日志                                                 |
| `MR_LOG_MASK(_level)`   | 生成对应级别的位掩码 `1U << (_level)`（调用方需保证 `_level < 32`） |

### 函数接口

| API                                                    | 说明                                                   |
|--------------------------------------------------------|--------------------------------------------------------|
| `void mr_log(mr_uint32_t level, const char *fmt, ...)` | 按指定级别输出格式化日志（受运行期掩码过滤）           |
| `void mr_log_mask_set(mr_uint32_t mask)`               | 设置运行期 32 位级别掩码（对应位置 1 开启，置 0 关闭） |
| `mr_uint32_t mr_log_mask(void)`                        | 获取当前运行期级别掩码                                 |
