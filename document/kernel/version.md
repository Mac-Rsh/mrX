[**中文**](kernel_version.md) | [**English**](kernel_version_en.md)

---

# 内核版本

内核版本头文件提供 mrX 版本号常量、版本编码宏和版本匹配宏。该模块没有对应的 C 源文件，所有能力都由
`include/mrX/kernel/mr_version.h` 中的预处理宏提供。

本文对应 `include/mrX/kernel/mr_version.h`。

## 模块用途

版本模块用于在编译期或运行时代码中识别当前 mrX 版本。调用者可以直接读取主版本、次版本、补丁版本，也可以使用统一的版本编码进行比较。

当前头文件定义的版本为 `1.0.0`。

## 核心宏

### 版本号常量

```c
#define MR_VERSION_MAJOR                (1U)
#define MR_VERSION_MINOR                (0U)
#define MR_VERSION_PATCH                (0U)
```

三个宏分别表示主版本、次版本和补丁版本。它们均为无符号整数常量。

### 版本编码

```c
#define MR_VERSION_CODE(_major, _minor, _patch)                                \
    (((_major) << 16) | ((_minor) << 8) | (_patch))
```

`MR_VERSION_CODE()` 将三段版本号编码为一个整数：主版本位于 bit 16 到 bit 31，次版本位于 bit 8 到 bit 15，补丁版本位于 bit 0
到 bit 7。

### 当前版本编码

```c
#define MR_VERSION                                                             \
    MR_VERSION_CODE(MR_VERSION_MAJOR, MR_VERSION_MINOR, MR_VERSION_PATCH)
```

`MR_VERSION` 是当前 mrX 版本的编码值。以 `1.0.0` 为例，编码结果为 `0x00010000`。

### 版本匹配

```c
#define MR_VERSION_IS(_major, _minor, _patch)                                  \
    (MR_VERSION == MR_VERSION_CODE(_major, _minor, _patch))
```

`MR_VERSION_IS()` 用于判断当前版本是否等于指定版本。该宏执行精确匹配，不做大于、小于或兼容性判断。

## 使用示例

```c
#include <mrX/kernel/mr_version.h>

#if MR_VERSION_IS(1, 0, 0)
/* Code for mrX 1.0.0 */
#endif

unsigned int version_code(void) {
    return MR_VERSION;
}
```

也可以直接构造目标版本编码：

```c
if (MR_VERSION == MR_VERSION_CODE(1U, 0U, 0U)) {
    /* The current version is 1.0.0. */
}
```

## 注意事项

- `MR_VERSION_CODE()` 没有对输入范围做掩码限制；调用者应保证次版本和补丁版本在 8 位范围内，避免字段互相覆盖。
- `MR_VERSION_IS()` 是精确版本比较，不表示 API 兼容性。
- 版本头文件被 `include/mrX.h` 纳入内核公共头文件集合，包含 `<mrX.h>` 后即可使用这些宏。
