[**中文**](libc_compiler.md) | [**English**](libc_compiler_en.md)

---

# libc 编译器适配

`mr_compiler.h` 封装编译器属性和常用编译期辅助宏。该模块用于统一对齐、段放置、弱符号、内联和未使用标记等写法。

## 核心宏

当前实现支持 `__GNUC__`、`__clang__` 或 `__ARMCC_VERSION` 编译器环境，否则直接触发编译错误。

| 宏                      | 展开形式                                     | 用途              |
|------------------------|------------------------------------------|-----------------|
| `MR_ALIGN(_x)`         | `__attribute__((aligned(_x)))`           | 指定对象对齐。         |
| `MR_SECTION(_x)`       | `__attribute__((section(_x)))`           | 指定对象或函数所在段。     |
| `MR_UNUSED`            | `__attribute__((unused))`                | 标记可能未使用的符号。     |
| `MR_USED`              | `__attribute__((used))`                  | 要求编译器保留符号。      |
| `MR_WEAK`              | `__attribute__((weak))`                  | 定义弱符号。          |
| `MR_INLINE`            | `static __inline`                        | 定义头文件内联函数。      |
| `MR_ALIGN_SIZE`        | `sizeof(void *)`                         | mrX 默认机器字对齐大小。  |
| `MR_SECTION_ALIGN(_x)` | `MR_ALIGN(MR_ALIGN_SIZE) MR_SECTION(_x)` | 按默认对齐放入指定段。     |
| `MR_IGNORE(_x)`        | `(void)(_x)`                             | 显式忽略变量，抑制未使用告警。 |

## 主要接口

该模块不提供运行时函数，所有能力通过宏在编译期生效。

## 使用示例

### 对齐对象

`MR_ALIGN()` 可用于要求对象按指定字节数对齐。常见场景包括 DMA 缓冲区、硬件描述符、栈空间或需要机器字对齐的数据。

```c
#include <mrX/libc/mr_compiler.h>

static unsigned char MR_ALIGN(32) dma_buffer[128];
static int MR_ALIGN(MR_ALIGN_SIZE) aligned_value;
```

### 放入指定段

`MR_SECTION()` 可把对象或函数放入链接脚本约定的段中。段名需要和工程链接脚本保持一致。

```c
#include <mrX/libc/mr_compiler.h>

static const char MR_SECTION(".rodata.board") board_name[] = "demo-board";

void MR_SECTION(".fast_text") fast_irq_handler(void)
{
    /* 需要放入快速执行区域的代码。 */
}
```

### 按默认对齐放入指定段

`MR_SECTION_ALIGN()` 等价于 `MR_ALIGN(MR_ALIGN_SIZE) MR_SECTION(_x)`，适合把表项、描述符或自动注册对象放入指定段，并保持 mrX 默认机器字对齐。

```c
#include <mrX/libc/mr_compiler.h>

struct command_entry {
    const char *name;
    void (*handler)(void);
};

static void reboot_handler(void)
{
}

static const struct command_entry MR_USED MR_SECTION_ALIGN(".mr_cmd") reboot_cmd = {
    "reboot",
    reboot_handler,
};
```

### 定义可覆盖的弱钩子

`MR_WEAK` 可定义默认实现，板级或应用层可以提供同名强符号覆盖它。

```c
#include <mrX/libc/mr_compiler.h>

MR_WEAK void board_hook(void)
{
    /* 默认不做任何处理。 */
}
```

### 保留静态符号

`MR_USED` 用于要求编译器不要因为符号未被 C 代码直接引用而删除它，常和 `MR_SECTION()` 或 `MR_SECTION_ALIGN()` 配合使用。

```c
#include <mrX/libc/mr_compiler.h>

static const char MR_USED MR_SECTION(".mr_version") build_version[] = "1.0.0";
```

### 定义头文件内联函数

`MR_INLINE` 用于在头文件中定义 `static inline` 风格的小函数，避免多个源文件包含同一头文件时产生重复外部定义。

```c
#include <mrX/libc/mr_compiler.h>

MR_INLINE int mr_max_int(int a, int b)
{
    return (a > b) ? a : b;
}
```

### 标记或忽略未使用项

`MR_IGNORE()` 用于在函数体内显式忽略参数或局部变量。`MR_UNUSED` 可直接标记可能不被引用的函数、变量或参数。

```c
#include <mrX/libc/mr_compiler.h>

static void sample(int unused_arg, void *context MR_UNUSED)
{
    MR_IGNORE(unused_arg);
}

static void debug_dump(void) MR_UNUSED;

static void debug_dump(void)
{
}
```

## 注意事项

- `MR_ALIGN_SIZE` 是指针宽度，不一定等于缓存行大小。
- `MR_SECTION(_x)` 的段名必须符合链接脚本约定，否则对象可能无法被正确放置或保留。
- `MR_USED` 只能要求编译器保留符号，链接阶段是否保留还受链接器脚本和垃圾回收选项影响。
- 非 GCC/Clang/ARMCC 兼容编译器当前不被支持。
