[**中文**](kernel_log.md) | [**English**](kernel_log_en.md)

---

# 内核日志

内核日志模块提供带时间戳、等级和标签的日志输出能力。模块基于 `mr_printf` 输出文本，通过全局等级掩码控制运行时输出范围，并通过编译期配置和
`MR_LOG_TAG` 控制各级日志宏是否展开。

本文对应 `include/mrX/kernel/mr_log.h`、`kernel/log.c` 和 `test/kernel/log.c`。

## 模块用途

`mr_log` 模块用于在内核和组件中输出结构化调试信息。调用日志宏时，宏会生成包含等级和模块标签的格式字符串；`mr_log()`
在实际输出前打印时间戳，并根据等级掩码决定是否输出该日志。

模块依赖 `MR_USE_LOG`。在 Kconfig 中，`MR_USE_LOG` 会 `select MR_USE_PRINTF`，因此日志输出总是基于 printf 模块。

## 核心宏和配置

### 日志等级

```c
#define MR_LOG_LEVEL_ERR                (0)
#define MR_LOG_LEVEL_WRN                (1)
#define MR_LOG_LEVEL_INF                (2)
#define MR_LOG_LEVEL_DBG                (3)
#define MR_LOG_LEVEL_TRC                (4)
```

日志等级使用从 0 到 4 的整数表示。数值越大，信息越详细。`mr_log()` 本身接受 0 到 31 的等级值，但头文件提供的标准宏只定义了上述五个等级。

### 等级掩码

```c
#define MR_LOG_MASK(_level)             (1U << (_level))
```

等级掩码用于运行时过滤日志。`mr_log()` 只在 `(1U << level) & mask` 非零时输出。源码中的默认掩码为 `0xffffffffU`，表示所有 0
到 31 的等级默认开启。

### 默认编译期等级

```c
#if !defined(MR_CFG_LOG_LEVEL)
#define MR_CFG_LOG_LEVEL                MR_LOG_LEVEL_TRC
#endif
```

`MR_CFG_LOG_LEVEL` 控制日志宏的编译期展开上限。Kconfig 中该配置依赖 `MR_USE_LOG`，默认值为 4，取值范围为 0 到
4。若源码包含日志宏的位置未定义 `MR_LOG_TAG`，对应日志宏会展开为空操作。

### 日志格式宏

```c
#define __MR_LOG_FORMAT(_level, _fmt)                                          \
    (" [" _level "/" MR_LOG_TAG "] " _fmt "\r\n")
```

日志宏会将等级、`MR_LOG_TAG` 和用户格式串拼接为最终格式串。时间戳不在该宏中生成，而是在 `mr_log()` 内部先行输出。

### 日志输出宏

```c
#define MR_LOG_ERR(_fmt, ...) mr_log(MR_LOG_LEVEL_ERR, ...)
#define MR_LOG_WRN(_fmt, ...) mr_log(MR_LOG_LEVEL_WRN, ...)
#define MR_LOG_INF(_fmt, ...) mr_log(MR_LOG_LEVEL_INF, ...)
#define MR_LOG_DBG(_fmt, ...) mr_log(MR_LOG_LEVEL_DBG, ...)
#define MR_LOG_TRC(_fmt, ...) mr_log(MR_LOG_LEVEL_TRC, ...)
```

各宏只有在定义了 `MR_LOG_TAG` 且 `MR_CFG_LOG_LEVEL` 大于等于对应等级时才会展开为 `mr_log()` 调用，否则展开为 `((void)0)`。

## 模块接口

### 输出日志

```c
void mr_log(mr_uint32_t level, const char *fmt, ...);
```

`mr_log()` 根据等级掩码过滤日志。参数合法且等级启用时，它先输出时间戳，再将 `fmt` 和可变参数传给 `mr_vprintf()`。

无返回值。以下情况会直接返回且不输出：

| 条件               | 行为                  |
|------------------|---------------------|
| `level >= 32U`   | 等级超出 32 位掩码范围，直接忽略。 |
| `fmt == MR_NULL` | 格式串无效，直接忽略。         |
| `fmt[0] == '\0'` | 空格式串，直接忽略。          |
| 当前等级未被掩码启用       | 输出被过滤。              |

时间戳格式由时钟配置决定：

| 配置                       | 时间戳来源和格式                                                                                   |
|--------------------------|--------------------------------------------------------------------------------------------|
| `MR_USE_CLOCK_HIRES`     | 使用 `mr_clock_hires_tick()` 和 `mr_hires_tick2timestamp()`，输出 `[YYYY-MM-DD-HH:MM:SS:ms:us]`。 |
| 未启用 `MR_USE_CLOCK_HIRES` | 使用 `mr_clock_uptime()`，输出 `[seconds.millisecond]`。                                         |

### 设置日志掩码

```c
void mr_log_mask_set(mr_uint32_t mask);
```

`mr_log_mask_set()` 将全局日志等级掩码设置为 `mask`。例如，`MR_LOG_MASK(MR_LOG_LEVEL_ERR)` 只开启错误日志，`0U` 会关闭所有等级输出。

### 获取日志掩码

```c
mr_uint32_t mr_log_mask(void);
```

`mr_log_mask()` 返回当前全局日志等级掩码。测试用例覆盖了默认掩码、设置和读取掩码、非法等级、无效格式串以及掩码过滤行为。

## 使用示例

```c
#define MR_LOG_TAG "demo"
#include <mrX/kernel/mr_log.h>

void example_log(void) {
    mr_log_mask_set(MR_LOG_MASK(MR_LOG_LEVEL_ERR) |
                    MR_LOG_MASK(MR_LOG_LEVEL_INF));

    MR_LOG_ERR("open failed: %d", -1);
    MR_LOG_INF("device ready: %s", "uart0");
    MR_LOG_DBG("this message depends on mask and compile level");
}
```

也可以直接调用 `mr_log()`：

```c
mr_log(MR_LOG_LEVEL_WRN, " [WRN/manual] value=%u\r\n", value);
```

直接调用时，调用者需要自行组织等级和标签文本；日志宏已经内置了这一格式。

## 注意事项

- 日志宏依赖 `MR_LOG_TAG`，未定义标签时宏不会输出。
- `MR_CFG_LOG_LEVEL` 是编译期过滤，等级高于该值的宏不会生成 `mr_log()` 调用。
- `mr_log_mask_set()` 是运行时过滤，不能恢复已经被编译期过滤掉的日志宏。
- 全局掩码 `__log_level` 在当前实现中没有单独加锁；多执行上下文同时读写掩码时需要调用者自行约束。
- `mr_log()` 使用 `mr_printf()` 和 `mr_vprintf()` 输出，printf 模块的缓冲区大小、安全模式和平台输出限制同样适用于日志模块。
- 启用高精度时钟后，时间戳使用 `MR_CFG_LOG_TIME_ZONE`，未配置时源码默认使用 8。
