# 字符串（String）

> 字符串模块提供面向嵌入式的内存与字符串操作原语（`mr_memcpy` / `mr_memset` /
> `mr_strcmp` 等）以及 De Bruijn 位操作（`mr_ffs32` / `mr_fls64` 等），是整个
> mrX 的基础工具层。

| 项       | 说明                                                                                                                                                                     |
|----------|--------------------------------------------------------------------------------------------------------------------------------------------------------------------------|
| 功能宏   | `MR_USE_STRING`（默认开启；可选 `MR_USE_STRING_STD` 转发标准库、`MR_USE_STRING_3RD` 接入第三方库）；可选安全开关 `MR_USE_STRING_SAFE`（默认 n）                          |
| 头文件   | `<mrX/libc/mr_string.h>`                                                                                                                                                 |
| 依赖模块 | 头文件工具集（`mr_compiler.h` / `mr_types.h` / `mr_errno.h`，见附录 14）；[内存分配 Malloc](malloc.md) 与本模块为上下游关系（malloc 内部使用 `mr_memcpy` / `mr_memset`） |
| 参考测试 | `mrX/test/libc/string.c`                                                                                                                                                 |

---

## 用户指南

### 1. 模块概述

字符串模块解决的问题是： **为目标板提供一套可控、可裁剪、行为明确的内存与字符串 原语，避免不同工具链 C 库实现的差异**。

特性：

- 覆盖常用内存操作：`memccpy`、`memcmp`、`memcpy`、`memmove`、`memset`、`memchr`。
- 覆盖常用字符串操作：`strcmp`、`strcpy`、`strerror`、`strlen`、`strnlen`、
  `strncmp`、`strncpy`、`strchr`。
- `memcpy` / `memmove` / `memset` 按机器字长做对齐字访问加速，对齐不匹配时 自动退化为逐字节。
- `mr_strerror` 将 `MR_E*` 错误码映射为固定英文描述字符串。
- 头文件内置 `mr_ffs32` / `mr_ffs64` / `mr_fls32` / `mr_fls64` 位查找原语 （`MR_INLINE`，始终编译，不受 `MR_USE_STRING`
  控制）。
- 可选 `MR_USE_STRING_SAFE`：对 `MR_NULL` 参数返回安全值而非崩溃。

### 2. 配置与依赖

| 配置项               | 类型           | 默认值 | 说明                                                                           |
|----------------------|----------------|--------|--------------------------------------------------------------------------------|
| `MR_USE_STRING`      | bool（choice） | y      | 使用内置实现（`mrX/include/mrX/libc/Kconfig:150`）                             |
| `MR_USE_STRING_STD`  | bool（choice） | n      | 宏转发标准库 `string.h` 同名函数                                               |
| `MR_USE_STRING_3RD`  | bool（choice） | n      | 接入第三方字符串库                                                             |
| `MR_USE_STRING_SAFE` | bool           | n      | 空指针安全检查开关，依赖 `MR_USE_STRING`（`mrX/include/mrX/libc/Kconfig:157`） |

选择 `MR_USE_STRING` 并重新生成 `mr_config.h` 后，包含头文件即可使用：

```c
#include <mrX/libc/mr_string.h>
```

选择 `MR_USE_STRING_STD` 时全部内存/字符串函数宏转发标准库（位查找原语仍为 内置实现）。`MR_USE_STRING_SAFE` 开启后各函数入口检查
`MR_NULL` 参数： 内存/查找类返回 `MR_NULL`，比较/长度类返回 `0`（行为表见第 4 节）。

### 3. 快速上手

```c
#include <mrX/libc/mr_string.h>

void example(void) {
    char src[16];
    char dst[16];
    mr_size_t len;

    mr_strcpy(src, "hello");
    mr_memcpy(dst, src, mr_strlen(src) + 1);

    len = mr_strlen(dst);           /* len == 5 */
    if (mr_strcmp(dst, "hello") == 0) {
        /* Match */
    }
}
```

### 4. 使用说明

**内存函数语义**

| API          | 行为要点                                                             |
|--------------|----------------------------------------------------------------------|
| `mr_memcpy`  | 区间不得重叠（重叠请用 `mr_memmove`）；按字长对齐加速                |
| `mr_memmove` | 支持任意重叠区间，结果与"先拷到临时区"一致                           |
| `mr_memset`  | 以字节值填充，区间任意对齐                                           |
| `mr_memcmp`  | 无符号字节序比较（`0x80 > 0x7F`），返回首个差异字节的差值            |
| `mr_memchr`  | 在 `size` 字节内查找字节值，找不到返回 `MR_NULL`                     |
| `mr_memccpy` | 拷贝至遇到 `c`（含该字节）为止，返回其后继指针；未遇到返回 `MR_NULL` |
| 零长度       | `size == 0` 的内存操作均为无操作，返回 `dst`（查找类返回 `MR_NULL`） |

**字符串函数语义**

| API                        | 行为要点                                                               |
|----------------------------|------------------------------------------------------------------------|
| `mr_strcmp` / `mr_strncmp` | 无符号字节序比较；`strncmp` 在 `\0` 处或 `size` 用尽即停               |
| `mr_strcpy`                | 连 `\0` 一起拷贝，返回 `dst` 起始指针（非拷贝终点）                    |
| `mr_strncpy`               | 最多拷贝 `size` 字节；源串较短时剩余空间补 `\0`；源串超长时不补 `\0`   |
| `mr_strlen` / `mr_strnlen` | 前者数到 `\0`，后者上限 `size`                                         |
| `mr_strchr`                | 查找范围为串长 + 1（可查找 `\0` 终结符），返回首个匹配指针或 `MR_NULL` |
| `mr_strerror`              | 返回静态字符串，仅识别 `MR_E*` 错误码，未知码返回 `"Unknown error"`    |

**`MR_USE_STRING_SAFE` 开启后的空指针行为**

| 函数                                                                  | `MR_NULL` 输入时返回 |
|-----------------------------------------------------------------------|----------------------|
| `mr_memcpy` / `mr_memmove` / `mr_memset` / `mr_memccpy` / `mr_memchr` | `MR_NULL`            |
| `mr_strcmp` / `mr_memcmp` / `mr_strncmp`                              | `0`                  |
| `mr_strcpy`                                                           | `MR_NULL`            |
| `mr_strlen` / `mr_strnlen`                                            | `0`                  |

`mr_strncpy` / `mr_strchr` 自身不检查，但其内部依赖的 `mr_memccpy` /
`mr_strlen` / `mr_memchr` 已被保护，等效安全。该开关有少量运行时开销， 生产环境可按需开启。

### 5. API 参考

内存操作：

| API          | 说明                                                       |
|--------------|------------------------------------------------------------|
| `mr_memcpy`  | 拷贝 `size` 字节（区间不可重叠），返回 `dst`               |
| `mr_memmove` | 拷贝 `size` 字节（区间可重叠），返回 `dst`                 |
| `mr_memset`  | 将 `dst` 起 `size` 字节设为 `c`（取低字节），返回 `dst`    |
| `mr_memcmp`  | 比较 `size` 字节，返回首个差异字节的无符号差值，相等为 `0` |
| `mr_memchr`  | 在 `size` 字节内查找 `c`，返回匹配指针或 `MR_NULL`         |
| `mr_memccpy` | 拷贝直到写入字节 `c`，返回其后继指针，未遇到返回 `MR_NULL` |

字符串操作：

| API           | 说明                                                   |
|---------------|--------------------------------------------------------|
| `mr_strcmp`   | 比较两串，返回首个差异字节的无符号差值，相等为 `0`     |
| `mr_strncmp`  | 最多比较 `size` 字节                                   |
| `mr_strcpy`   | 拷贝含终结符的字符串，返回 `dst`                       |
| `mr_strncpy`  | 至多拷贝 `size` 字节并补 `\0` 填充，返回 `dst`         |
| `mr_strlen`   | 返回串长（不含 `\0`）                                  |
| `mr_strnlen`  | 返回串长，上限 `size`                                  |
| `mr_strchr`   | 查找首个匹配字符（含终结符位置），返回指针或 `MR_NULL` |
| `mr_strerror` | 错误码转静态描述字符串                                 |

位操作（`MR_INLINE`，始终编译）：

| API                     | 说明                                            |
|-------------------------|-------------------------------------------------|
| `mr_ffs32` / `mr_ffs64` | 首个置位位的序号（1 起计），入参为 0 返回 0     |
| `mr_fls32` / `mr_fls64` | 最后一个置位位的序号（1 起计），入参为 0 返回 0 |

**典型原型**

```c
void *mr_memcpy(void *dst, const void *src, mr_size_t size);
void *mr_memmove(void *dst, const void *src, mr_size_t size);
void *mr_memset(void *dst, int c, mr_size_t size);
int mr_memcmp(const void *m1, const void *m2, mr_size_t size);
void *mr_memchr(const void *s, int c, mr_size_t size);
void *mr_memccpy(void *dst, const void *src, int c, mr_size_t size);
int mr_strcmp(const char *s1, const char *s2);
char *mr_strcpy(char *dst, const char *src);
char *mr_strerror(int errnum);
mr_size_t mr_strlen(const char *s);
```

返回值约定：拷贝/填充类成功返回 `dst`，安全模式空指针失败返回 `MR_NULL`； 比较类相等返回 `0`，不等返回非 0 差值；查找类未命中返回
`MR_NULL`。

### 6. 完整示例

示例 1：重叠区间安全搬运（取材自 `mrX/test/libc/string.c` 的 memmove 用例）：

```c
void move_example(void) {
    mr_uint8_t buf[16];
    /* buf filled with data ... */

    /* Shift left by 5 bytes: overlapping range */
    mr_memmove(buf, buf + 5, 8);
}
```

示例 2：`strncpy` 的截断与补零（取材自 `mrX/test/libc/string.c`）：

```c
void copy_example(void) {
    char buf[8];

    mr_strncpy(buf, "hi", sizeof(buf));
    /* buf == "hi" followed by 6 '\0' */

    mr_strncpy(buf, "long_string", sizeof(buf));
    /* buf == "long_st", no terminator appended */
    buf[sizeof(buf) - 1] = '\0';
}
```

示例 3：错误码转文本输出：

```c
#include <mrX/libc/mr_string.h>
#include <mrX/libc/mr_errno.h>

void strerror_example(int err) {
    if (err == -MR_ENOMEM) {
        /* "Out of memory" */
        (void)mr_strerror(MR_ENOMEM);
    }
    /* Unknown code: "Unknown error" */
    (void)mr_strerror(-99999);
}
```

更多行为（对齐矩阵、高位字节比较、位查找全位遍历）见
`mrX/test/libc/string.c`。

### 7. 常见问题（FAQ）

**Q：`mr_memcpy` 拷贝重叠区间会怎样？**

结果未定义（对齐字加速会放大覆盖）。任何可能重叠的场景一律使用 `mr_memmove`。

**Q：为什么 `mr_memcmp(0x80, 0x01)` 返回"大于"？**

比较按无符号字节进行：`0x80`（128）大于 `0x01`（1），与 `char` 的符号性无关， 已由 `mrX/test/libc/string.c` 的
`memcmp_high_bit_order` 固定。

**Q：`mr_strncpy` 源串比 `size` 长时，目标串有 `\0` 吗？**

没有，行为与标准 `strncpy` 一致：只拷贝前 `size` 字节、不追加终结符。 调用方应自行保证终结（如 `buf[size - 1] = '\0'`）。

**Q：`mr_strerror` 的返回值需要释放吗？**

不需要，返回的是只读静态字符串，生命周期贯穿程序全程。

**Q：`mr_ffs32` 与 `mr_fls32` 的序号从几开始？**

从 1 开始：bit0 置位返回 1，bit31 置位返回 32；入参为 0 返回 0（无置位位）。 内核位图（如 [内核 Slab](../kernel/slab.md)
的空闲位查找）即使用该原语。

---

## 开发者指南

### 8. 架构总览

字符串模块位于最底层，被 libc 与内核普遍依赖，自身只依赖头文件工具集：

```
        +------------------------------------------+
        | libc 各模块（malloc 用 memcpy/memset）      |
        | 内核各模块（slab 位图、对象名、hashmap 等）  |
        +---------------------+--------------------+
                              | mr_memcpy() / mr_strcmp() / ...
        +---------------------v--------------------+
        | 字符串 String                              |
        |  内存原语 | 字符串原语 | strerror | 位查找  |
        +---------------------+--------------------+
             依赖：mr_compiler.h（MR_INLINE）、mr_types.h、
                   mr_errno.h（strerror 错误码表）
```

关键接线：[内存分配 Malloc](malloc.md) 的 `mr_calloc` / `mr_realloc` 直接调用
`mr_memset` / `mr_memcpy`（`mrX/libc/malloc.c:230`、`malloc.c:253`）；内核 slab 的空闲位查找使用 `mr_ffs64`（
`mrX/kernel/slab.c:32`）。模块不含任何 平台相关代码，唯一的可移植性开关是 `MR_USE_ARCH_64B`（影响 `mr_memset`
的填充模式宽度，`mrX/libc/string.c:181`）。

### 9. 数据结构

本模块不定义持久数据结构。实现要点分布在函数内部：

- **字长访问模式**（`mrX/libc/string.c:59-92`）：`memcpy` / `memmove` 先检测
  `dst` / `src` 按 `sizeof(mr_uintptr_t)` 的对齐位（`align = sizeof(mr_uintptr_t) - 1`）， 对齐匹配时按机器字整块搬运、余量逐字节；不匹配则整段逐字节。
- **`memset` 填充模式**（`string.c:164`）：把字节值扩展为机器字宽的模式字 （32 位扩展 4 次，`MR_USE_ARCH_64B` 时再扩展 4 次到
  64 位），先处理头部 非对齐字节，再整字写入，尾部逐字节。
- **De Bruijn 表**（`mrX/include/mrX/libc/mr_string.h:188`、`mr_string.h:209`）：
  `mr_ffs32` / `mr_ffs64` 以 `x & (-x)` 分离最低置位位后查表得序号；
  `mr_fls*` 先将高位折叠（`x |= x >> n` 级联）化为"仅最低位为原最高位"再复用
  `ffs`。全部为 `MR_INLINE`，无函数调用开销。

### 10. 核心流程

**memmove 的重叠处理**（`mrX/libc/string.c:94`）：

1. 空区间或同址直接返回。
2. `dst` 高于 `src` 且距离小于 `size`（前向重叠）→ 从尾部向前倒序搬运； 否则正向搬运。两个方向内部都先做对齐字搬运、再处理余量。
3. 倒序分支同样要求 `(dst_addr ^ src_addr) & align == 0` 才走字访问 （`string.c:124`），保证对齐假设成立。

**strncpy 的实现**（`string.c:386`）：以 `mr_memccpy(dst, src, '\0', size)`
完成"拷到终结符为止"，命中则用 `mr_memset` 把剩余空间补 `\0`；未命中 （源串超长）则原样返回，不补 `\0`、不越界。`strchr`（
`string.c:398`）以
`mr_memchr(s, c, mr_strlen(s) + 1)` 实现，`+1` 使查找覆盖终结符位置。

**strerror 的错误码表**（`string.c:266`）：`switch` 映射 `0` 与
`MR_EPERM` ~ `MR_ELOOP` 共 13 个码值（12 个 `MR_E*` 错误码加上 `0`）为 静态字符串，默认分支 `"Unknown error"`
；无格式化、无缓冲，返回指针即查表结果。

### 11. 并发与中断安全

- 全部函数无全局状态、无堆、无锁，天然可重入，线程与中断上下文均可调用。
- 不操作目标存储之外的状态，并发安全性由调用者对目标缓冲的所有权保证 （同一缓冲的并发写需调用者串行化，这与标准 C 库一致）。
- 函数耗时与 `size` 成正比且无上界，长拷贝建议移出中断上下文。

### 12. 扩展与派生

扩展面是 **编译期后端 choice + 一个可选安全检查开关**；本模块没有 clazz 与派生机制：

- `MR_USE_STRING_STD` 时头文件把内存/字符串函数整体宏转发标准库（`mrX/include/mrX/libc/mr_string.h:30-44`），
  `MR_USE_STRING` 时使用内置实现（`mr_string.h:45-179`，实现见 `mrX/libc/string.c:10` 起）。
- `MR_USE_STRING_3RD` 已在 choice 中定义（`mrX/include/mrX/libc/Kconfig:145`），但头文件只有上述两个分支，代码中没有任何
  `MR_USE_STRING_3RD` 分支——第三方字符串库的接入点当前并不存在，属于预留选项。
- `MR_USE_STRING_SAFE`（`Kconfig:157`，依赖 `MR_USE_STRING`）在各内置实现函数入口追加 `MR_NULL` 检查（如
  `mrX/libc/string.c:64`、`string.c:100`），把"崩溃"换成返回 `MR_NULL` / `0`；STD 后端不受该开关影响。
- 位查找原语 `mr_ffs32` / `mr_ffs64` / `mr_fls32` / `mr_fls64` 固定内联在头文件、始终可用且不受 choice 控制（
  `mr_string.h:187` 起），需要位扫描的内核模块直接继承该实现而非依赖所选后端（用例见 `mrX/kernel/slab.c:32`）。

两种扩展开关都有行为测试固定：`mrX/test/libc/string.c:488`（`MR_USE_STRING_SAFE` 下的空指针拒绝）、`string.c:431`（位查找序号语义）。

### 13. 源码导读

| 位置                                   | 职责                                                     |
|----------------------------------------|----------------------------------------------------------|
| `mrX/include/mrX/libc/mr_string.h:30`  | STD 后端宏转发与内置 API 声明                            |
| `mrX/include/mrX/libc/mr_string.h:187` | `mr_ffs32` / `mr_ffs64` / `mr_fls32` / `mr_fls64` 位查找 |
| `mrX/libc/string.c:13`                 | `mr_memccpy`（`strncpy` 的底层）                         |
| `mrX/libc/string.c:59`                 | `mr_memcpy`（对齐检测 + 字访问加速）                     |
| `mrX/libc/string.c:94`                 | `mr_memmove`（重叠区间的双向搬运）                       |
| `mrX/libc/string.c:164`                | `mr_memset`（模式字填充）                                |
| `mrX/libc/string.c:266`                | `mr_strerror`（错误码表）                                |
| `mrX/libc/string.c:386`                | `mr_strncpy`（memccpy + memset 组合）                    |
| `mrX/test/libc/string.c`               | 内存/字符串/位查找/安全模式的行为级测试                  |

### 14. 附录

**配置宏**

| 宏                                    | 说明                                           |
|---------------------------------------|------------------------------------------------|
| `MR_USE_STRING`                       | 内置实现开关（choice，默认）                   |
| `MR_USE_STRING_STD`                   | 转发标准库 string.h（choice）                  |
| `MR_USE_STRING_3RD`                   | 接入第三方字符串库（choice）                   |
| `MR_USE_STRING_SAFE`                  | 空指针安全检查，默认 n（依赖 `MR_USE_STRING`） |
| `MR_USE_ARCH_32B` / `MR_USE_ARCH_64B` | 架构字长选择，影响 `mr_memset` 模式字宽度      |

**错误码**：比较类函数以返回值差值表达比较结果，无负数错误码路径；
`mr_strerror` 识别的错误码定义于 `mrX/include/mrX/libc/mr_errno.h`
（`MR_EPERM`、`MR_ENOMEM`、`MR_EINVAL` 等，见下表工具集一节）。

**头文件工具集**

`mrX/include/mrX/libc` 下还有若干无独立源文件的纯头文件工具，本文档不单独 成篇，此处列其用途；详细 API 以各头文件的 Doxygen
注释为准。

| 头文件          | 用途                                                                                                                                                    |
|-----------------|---------------------------------------------------------------------------------------------------------------------------------------------------------|
| `mr_assert.h`   | 断言宏 `MR_ASSERT` / `MR_PANIC`（含静态断言），支持 STD / 3RD / 内置 / 关闭四种后端，内置后端经由平台断言接口停机                                       |
| `mr_atomic.h`   | 原子操作原语（`mr_atomic_load` / `mr_atomic_compare_exchange` / `fetch_add` 等），内置后端基于关中断实现，供无锁结构（如内核 slab 位图）使用            |
| `mr_compiler.h` | 编译器属性封装（`MR_ALIGN` / `MR_SECTION` / `MR_WEAK` / `MR_INLINE` / `MR_PACKED` 等）与 `MR_ALIGN_SIZE`、`MR_IGNORE`                                   |
| `mr_errno.h`    | 错误码类型 `mr_err_t` 与 `MR_E*` 错误码定义（POSIX 兼容取值），全模块 API 失败返回值的取值来源                                                          |
| `mr_types.h`    | 定宽类型别名（`mr_int32_t` / `mr_uint32_t` / `mr_size_t` / `mr_ssize_t` / `mr_ptr_t` / `mr_va_list` 等）与 `MR_TRUE` / `MR_FALSE`、`MR_SIZE_MAX` 等常量 |
