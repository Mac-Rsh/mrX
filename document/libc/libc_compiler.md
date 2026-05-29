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

```c
#include <mrX/libc/mr_compiler.h>

static int MR_ALIGN(MR_ALIGN_SIZE) aligned_value;

MR_WEAK void board_hook(void)
{
}

static void sample(int unused_arg)
{
    MR_IGNORE(unused_arg);
}
```

## 注意事项

- `MR_ALIGN_SIZE` 是指针宽度，不一定等于缓存行大小。
- `MR_SECTION(_x)` 的段名必须符合链接脚本约定，否则对象可能无法被正确放置或保留。
- `MR_USED` 只能要求编译器保留符号，链接阶段是否保留还受链接器脚本和垃圾回收选项影响。
- 非 GCC/Clang/ARMCC 兼容编译器当前不被支持。
