# initcall - 分级初始化调用

## 1. 模块概述

### 1.1 功能介绍

initcall 模块实现了一个**分级初始化机制**（Hierarchical
Initialization），用于管理系统的启动和初始化流程。在嵌入式系统和操作系统中，不同的组件和模块需要按照特定的顺序进行初始化，initcall
提供了一种声明式、自动化的方式来管理这个过程。

传统初始化方式的缺点：

- **手动调用**：需要在主函数中手动调用每个初始化函数
- **顺序依赖**：必须仔细管理初始化顺序，容易出错
- **耦合性强**：新增模块需要修改启动代码
- **分散管理**：初始化代码分散在各个文件中，难以维护

initcall 模块通过**链接器段（Linker Section）**技术实现了自动化的分级初始化：

- **声明式注册**：模块通过宏声明自己的初始化函数和级别
- **自动收集**：链接器自动将所有初始化函数收集到特定段
- **顺序执行**：按照段名称的字典序自动排序执行
- **解耦设计**：模块之间相互独立，无需修改启动代码

### 1.2 主要特性

- **分级初始化**：支持多个初始化级别，自动按顺序执行
- **声明式注册**：使用宏自动注册初始化函数
- **链接器驱动**：利用链接器自动收集和排序
- **双向初始化**：区分内核初始化和用户初始化
- **可扩展**：用户可以自定义初始化级别
- **零运行时开销**：注册过程在编译期完成

### 1.3 适用场景

initcall 模块适用于以下场景：

- **系统启动**：操作系统或嵌入式系统的启动流程
- **模块初始化**：驱动、服务、子系统的初始化
- **板级支持**：不同硬件平台的初始化差异
- **组件加载**：按顺序加载依赖的组件

**典型初始化顺序**：

```
0.end     → 用户初始化标记（开始）
1.0       → IRQ 初始化
1.1       → 驱动初始化
8.0-8.9   → 用户自定义级别
9.end     → 用户初始化标记（结束）
```

---

## 2. 使用指南

### 2.1 核心数据结构

#### mr_initcall_entry_t

```c
typedef void(mr_initcall_entry_t)(void);
```

初始化函数的函数指针类型。

**签名**：

```c
void init_function(void);
```

**要求**：

- 无参数
- 无返回值
- 不应该阻塞或睡眠

#### mr_initcall_t

```c
typedef struct mr_initcall {
    const char *name;                  /* 初始化函数名称 */
    mr_initcall_entry_t *entry;        /* 初始化函数指针 */
} mr_initcall_t;
```

初始化调用条目结构。

**成员说明**：

- `name`：函数名称字符串，用于调试和日志
- `entry`：函数指针，指向实际的初始化函数

### 2.2 初始化级别定义

initcall 模块预定义了以下初始化级别：

| 级别   | 宏定义                              | 用途               |
|------|----------------------------------|------------------|
| 开始标记 | `"0.end"`                        | 用户初始化的起始标记       |
| IRQ  | `MR_INIT_LEVEL_IRQ` (`"1.0"`)    | 中断管理初始化          |
| 驱动   | `MR_INIT_LEVEL_DRIVER` (`"1.1"`) | 设备驱动初始化          |
| 用户级别 | `MR_INIT_LEVEL_USER(level)`      | 用户自定义级别（8.0-8.9） |
| 结束标记 | `"9.end"`                        | 用户初始化的结束标记       |

**级别命名规则**：

- 使用字符串表示，如 `"1.0"`、`"8.5"`
- 链接器按字典序排序，所以 `"1.0"` < `"1.1"` < `"8.0"`
- 建议使用 `主级别.子级别` 的格式

### 2.3 主要接口说明

#### MR_INIT_EXPORT()

```c
#define MR_INIT_EXPORT(_entry, _level)
```

**功能**：导出一个初始化函数到指定的初始化级别

**参数**：

- `_entry`：初始化函数名称（函数标识符）
- `_level`：初始化级别（字符串字面量）

**示例**：

```c
static void my_module_init(void) {
    /* 初始化代码 */
    printf("My module initialized\n");
}
MR_INIT_EXPORT(my_module_init, MR_INIT_LEVEL_DRIVER);
```

**宏展开结果**：

```c
MR_USED const mr_initcall_t __mr_initcall_my_module_init
    __attribute__((section("mr_initcall.1.1")))
    __attribute__((aligned(sizeof(void *))))
    = { .name = "my_module_init", .entry = my_module_init };
```

**工作原理**：

1. 创建一个 `mr_initcall_t` 类型的常量
2. 使用 `section` 属性将变量放置到特定的段
3. 使用 `aligned` 属性确保对齐
4. 初始化结构体，记录函数名和指针

**注意事项**：

- 函数必须符合 `mr_initcall_entry_t` 签名
- `_level` 必须是字符串字面量，不能是变量
- 导出的函数会在系统启动时自动调用

#### mr_kernel_init()

```c
void mr_kernel_init(void);
```

**功能**：执行内核级别的初始化

**调用时机**：

- 在 `mr_user_init()` 中自动调用
- 用户通常不需要手动调用

**初始化内容**：

- 平台初始化（如果定义了 `MR_USE_PLATFORM`）
- 内存管理初始化（如果定义了 `MR_USE_MALLOC`）
- 定时器初始化（如果定义了 `MR_USE_TIMER`）
- 中断管理初始化（如果定义了 `MR_USE_IRQ`）

**注意事项**：

- 该函数只执行一次，重复调用会直接返回
- 在所有用户初始化函数之前执行

#### mr_user_init()

```c
void mr_user_init(void);
```

**功能**：执行用户级别的初始化，遍历并调用所有通过 `MR_INIT_EXPORT` 注册的函数

**调用时机**：

- 系统启动时由启动代码调用
- 通常在 `main()` 函数开始时调用

**执行流程**：

1. 调用 `mr_kernel_init()` 完成内核初始化
2. 获取初始化函数数组的首尾地址
3. 按顺序遍历并调用所有初始化函数

**注意事项**：

- 该函数只执行一次，重复调用会直接返回
- 必须在使用任何需要初始化的模块之前调用

### 2.4 典型应用案例

#### 案例 1：基础用法 - 模块初始化

```c
#include <mrX/kernel/mr_initcall.h>
#include <stdio.h>

/* 定义一个模块的初始化函数 */
static void uart_init(void) {
    printf("UART driver initialized\n");
    /* 初始化 UART 硬件... */
}
MR_INIT_EXPORT(uart_init, MR_INIT_LEVEL_DRIVER);

/* 定义另一个模块的初始化函数 */
static void timer_init(void) {
    printf("Timer module initialized\n");
    /* 初始化定时器... */
}
MR_INIT_EXPORT(timer_init, MR_INIT_LEVEL_IRQ);

/* 主程序 */
int main(void) {
    printf("System starting...\n");

    /* 执行所有初始化 */
    mr_user_init();

    printf("System ready\n");

    /* 主循环 */
    while (1) {
        /* 应用程序逻辑 */
    }

    return 0;
}
```

**预期输出**：

```
System starting...
Platform initialized
Memory allocator initialized
[内核初始化完成]
Timer module initialized     ← 1.0 级别
UART driver initialized      ← 1.1 级别
System ready
```

#### 案例 2：自定义初始化级别

```c
#include <mrX/kernel/mr_initcall.h>
#include <stdio.h>

/* 定义自定义初始化级别 */
#define MY_INIT_LEVEL_EARLY  "8.0"  /* 早期用户初始化 */
#define MY_INIT_LEVEL_NORMAL "8.5"  /* 正常用户初始化 */
#define MY_INIT_LEVEL_LATE   "8.9"  /* 晚期用户初始化 */

/* 早期初始化：基础服务 */
static void logger_init(void) {
    printf("Logger service initialized\n");
}
MR_INIT_EXPORT(logger_init, MY_INIT_LEVEL_EARLY);

/* 正常初始化：应用模块 */
static void network_init(void) {
    printf("Network stack initialized\n");
}
MR_INIT_EXPORT(network_init, MY_INIT_LEVEL_NORMAL);

/* 晚期初始化：依赖其他模块的服务 */
static void web_server_init(void) {
    printf("Web server started\n");
}
MR_INIT_EXPORT(web_server_init, MY_INIT_LEVEL_LATE);

/* 主程序 */
int main(void) {
    mr_user_init();

    /* 所有模块已按顺序初始化 */
    return 0;
}
```

**预期输出**：

```
Platform initialized
[...内核初始化...]
Logger service initialized      ← 8.0
Network stack initialized       ← 8.5
Web server started             ← 8.9
```

#### 案例 3：条件初始化

```c
#include <mrX/kernel/mr_initcall.h>
#include <stdio.h>

/* 根据配置决定是否注册初始化函数 */
#if defined(CONFIG_USE_SPI)
static void spi_init(void) {
    printf("SPI driver initialized\n");
}
MR_INIT_EXPORT(spi_init, MR_INIT_LEVEL_DRIVER);
#endif

#if defined(CONFIG_USE_I2C)
static void i2c_init(void) {
    printf("I2C driver initialized\n");
}
MR_INIT_EXPORT(i2c_init, MR_INIT_LEVEL_DRIVER);
#endif

int main(void) {
    mr_user_init();
    return 0;
}
```

**说明**：

- 如果定义了 `CONFIG_USE_SPI`，则 SPI 驱动会被初始化
- 如果定义了 `CONFIG_USE_I2C`，则 I2C 驱动会被初始化
- 未定义的模块不会被编译和注册

#### 案例 4：板级支持包（BSP）初始化

```c
/* board_a.c */
#include <mrX/kernel/mr_initcall.h>

static void board_a_init(void) {
    /* 初始化板 A 特定的硬件 */
    init_board_a_clock();
    init_board_a_gpio();
    printf("Board A initialized\n");
}
MR_INIT_EXPORT(board_a_init, MR_INIT_LEVEL_USER("0"));

/* ========== */

/* board_b.c */
#include <mrX/kernel/mr_initcall.h>

static void board_b_init(void) {
    /* 初始化板 B 特定的硬件 */
    init_board_b_clock();
    init_board_b_gpio();
    printf("Board B initialized\n");
}
MR_INIT_EXPORT(board_b_init, MR_INIT_LEVEL_USER("0"));

/* ========== */

/* 主程序 - 根据编译时选择不同的板级支持 */
int main(void) {
    mr_user_init();
    /* 根据链接的文件，自动调用对应板的初始化 */
    return 0;
}
```

**说明**：

- 不同板的初始化代码相互独立
- 编译时只需链接对应板的源文件
- 无需修改启动代码

#### 案例 5：依赖关系管理

```c
#include <mrX/kernel/mr_initcall.h>
#include <stdio.h>

/* 基础层：硬件抽象 */
static void gpio_init(void) {
    printf("GPIO initialized\n");
}
MR_INIT_EXPORT(gpio_init, "2.0");

/* 中间层：设备驱动 */
static void spi_init(void) {
    printf("SPI driver initialized\n");
}
MR_INIT_EXPORT(spi_init, "2.1");

/* 应用层：传感器驱动 */
static void sensor_init(void) {
    printf("Temperature sensor initialized\n");
}
MR_INIT_EXPORT(sensor_init, "2.2");

int main(void) {
    mr_user_init();
    return 0;
}
```

**说明**：

- 通过级别编号控制依赖关系
- 低编号的级别先执行，高编号的后执行
- GPIO (2.0) → SPI (2.1) → Sensor (2.2)

---

## 3. 设计思路

### 3.1 设计目标

initcall 模块的设计目标：

1. **自动化初始化**：无需手动管理初始化函数调用
2. **解耦设计**：模块之间相互独立，降低耦合度
3. **顺序控制**：支持复杂的初始化顺序依赖
4. **可扩展性**：易于添加新的初始化级别和模块
5. **零运行时开销**：注册过程在编译期和链接期完成

### 3.2 链接器段技术

initcall 模块的核心是使用**链接器段（Linker Section）**技术：

**工作原理**：

1. **编译期**：
   ```c
   MR_INIT_EXPORT(my_init, "1.0")
   ```
   展开为：
   ```c
   __attribute__((section("mr_initcall.1.0")))
   const mr_initcall_t __mr_initcall_my_init = { ... };
   ```

2. **链接期**：
    - 链接器收集所有段名为 `mr_initcall.*` 的变量
    - 按段名的字典序排列
    - 存放到连续的内存区域

3. **运行期**：
   ```c
   const mr_initcall_t *start = &__mr_initcall_start;
   const mr_initcall_t *end = &__mr_initcall_end;
   for (call = start; call <= end; call++) {
       call->entry();  /* 调用初始化函数 */
   }
   ```

**内存布局示例**：

```
地址        内容
----------------------
0x1000      __mr_initcall_start (标记)
0x1008      {name="platform_init", entry=0x2000}  ; mr_initcall.0.end
0x1018      {name="irq_init", entry=0x3000}       ; mr_initcall.1.0
0x1028      {name="driver_init", entry=0x4000}    ; mr_initcall.1.1
0x1038      {name="user_init", entry=0x5000}      ; mr_initcall.8.0
0x1048      __mr_initcall_end (标记)
```

### 3.3 级别命名策略

使用字符串作为级别名称的原因：

**优点**：

1. **灵活性**：支持任意数量的子级别
2. **可读性**：`"1.0"` 比 `0x100` 更直观
3. **排序保证**：字符串字典序与数值序一致（同长度）

**示例**：

```
"0.end"    < "1.0" < "1.1" < "1.10" < "2.0" < "8.0" < "8.9" < "9.end"
```

**注意事项**：

- 必须保证相同长度的字符串（如都用 `X.Y` 格式）
- `"1.10"` < `"2.0"`（正确）
- `"1.10"` > `"1.2"`（如果长度不同会出错）

### 3.4 双向初始化机制

initcall 模块区分内核初始化和用户初始化：

**内核初始化（`mr_kernel_init`）**：

- 硬件相关初始化（平台、时钟、内存）
- 基础服务初始化（定时器、中断）
- 必须在其他任何初始化之前完成
- 使用条件编译，根据配置决定初始化内容

**用户初始化（`mr_user_init`）**：

- 遍历所有注册的初始化函数
- 在内核初始化之后执行
- 按级别顺序调用

**设计理由**：

1. **分离关注点**：内核和用户的初始化逻辑分离
2. **灵活性**：用户可以自由注册初始化函数
3. **安全性**：确保内核基础服务先初始化

### 3.5 防止重复初始化

```c
static volatile mr_bool_t __initcall_kstatus = MR_FALSE;
static volatile mr_bool_t __initcall_ustatus = MR_FALSE;

void mr_kernel_init(void) {
    if (__initcall_kstatus) {
        return;  /* 已经初始化，直接返回 */
    }
    __initcall_kstatus = MR_TRUE;
    /* ... 初始化代码 ... */
}
```

**设计考虑**：

- 使用 `volatile` 确保多线程可见性
- 使用简单的布尔标志，避免使用原子操作（此时原子系统可能未初始化）
- 允许多次调用，保证幂等性

---

## 4. 链接器脚本配置

### 4.1 为什么需要修改链接器脚本

initcall 模块使用链接器段技术来收集和排序初始化函数。为了让链接器正确地将这些符号放置到内存中，需要在链接器脚本（Linker
Script）中定义相应的段。

**不同平台的差异**：

- **Keil/ARMCC**：通常不需要修改链接器脚本，编译器会自动处理 `__attribute__((section(...)))`
- **GCC/GNU LD**：**必须**修改链接器脚本，明确定义段
- **Clang/LLVM**：与 GCC 类似，需要链接器脚本支持

### 4.2 链接器脚本配置

在链接器脚本（如 `linker.lds` 或 `link.ld`）中，需要在 `.text` 段或适当的位置添加以下配置：

```ld
SECTIONS
{
  .text : {
    /* ... 其他代码段 ... */

    /* mrX framework sections */
    KEEP(*(SORT(mr_initcall.*)))    /* 初始化调用段 */

    /* ... 其他代码段 ... */
  }
}
```

**关键字说明**：

| 关键字             | 含义                          |
|-----------------|-----------------------------|
| `KEEP(...)`     | 告诉链接器即使这些段没有被显式引用，也不要将它们优化掉 |
| `SORT(...)`     | 按段名的字典序排序，确保初始化顺序正确         |
| `mr_initcall.*` | 匹配所有以 `mr_initcall.` 开头的段   |

### 4.3 示例：完整的链接器脚本配置

#### GCC/GNU LD 配置示例

```ld
/* linker.lds */
ENTRY(mr_entry)

SECTIONS
{
  .text 0x00000000 : {
    /* 标准启动代码 */
    KEEP(*(SORT_NONE(.init)))
    *(.text)
    *(.text.*)

    /* ===== mrX 框架段 ===== */
    KEEP(*(SORT(mr_initcall.*)))    /* 必须添加：initcall 模块 */
    /* ========================= */

    KEEP(*(SORT_NONE(.fini)))
  } > FLASH

  .data : {
    *(.data)
    *(.data.*)
  } > RAM

  .bss : {
    __bss_start__ = .;
    *(.bss)
    *(COMMON)
    __bss_end__ = .;
  } > RAM
}
```

#### ARMCC/Keil 配置

对于 Keil 项目，通常使用 **scatter file** (`.sct` 文件) 而不是链接器脚本：

```sct
; scatter.sct
LR_IROM1 0x00000000 0x00080000  {    ; 加载域：Flash
  ER_IROM1 0x00000000 0x00080000  {  ; 执行域：Flash
   *.o (RESET, +First)
   *(InRoot$$Sections)
   .ANY (+RO)

   ; Keil 通常会自动处理 section 属性
   ; 一般不需要特殊配置
  }

  RW_IRAM1 0x20000000 0x00020000  {  ; RAM
   .ANY (+RW +ZI)
  }
}
```

**注意**：Keil 编译器会自动处理 `__attribute__((section(...)))`，通常不需要特殊配置。

### 4.4 验证段配置

配置完成后，可以通过以下方式验证：

**方法 1：使用 nm 查看符号**

```bash
nm your_program.elf | grep mr_initcall
```

预期输出：

```
00001234 a __mr_initcall_platform_init
00001240 a __mr_initcall_irq_init
0000124c a __mr_initcall_driver_init
...
```

**方法 2：使用 objdump 查看段信息**

```bash
objdump -h your_program.elf | grep mr_initcall
```

预期输出：

```
idx name        size      VMA       LMA       File off  Algn
 10 mr_initcall 00000030  00001234  00001234  00001234     2
```

**方法 3：使用 readelf 查看详细信息**

```bash
readelf -S your_program.elf | grep mr_initcall
```

### 4.5 常见问题排查

#### 问题 1：初始化函数没有被调用

**可能原因**：

- 链接器脚本缺少 `mr_initcall.*` 配置
- 段被链接器优化掉了（缺少 `KEEP`）

**解决方法**：

```ld
/* 必须使用 KEEP 关键字 */
KEEP(*(SORT(mr_initcall.*)))
```

#### 问题 2：初始化顺序错误

**可能原因**：

- 链接器脚本缺少 `SORT` 关键字
- 级别名称不符合字典序

**解决方法**：

```ld
/* 必须使用 SORT 关键字 */
KEEP(*(SORT(mr_initcall.*)))

/* 确保级别名称格式统一 */
MR_INIT_EXPORT(func1, "1.0");  /* 正确 */
MR_INIT_EXPORT(func2, "10");   /* 错误！应该是 "1.0" */
```

#### 问题 3：链接错误：undefined reference

**可能原因**：

- 缺少开始和结束标记符号
- 链接器脚本配置不完整

**解决方法**：
确保 `initcall.c` 中的标记符号能正确链接：

```c
extern const mr_initcall_t __mr_initcall_start;
extern const mr_initcall_t __mr_initcall_end;
```

### 4.6 不同工具链的配置差异

| 工具链         | 是否需要修改配置 | 配置难度 | 兼容性 |
|-------------|----------|------|-----|
| GCC/GNU LD  | **是**    | 中等   | 最好  |
| Clang/LLVM  | **是**    | 中等   | 最好  |
| ARMCC/Keil  | 否        | 简单   | 好   |
| IAR         | 是        | 较复杂  | 中等  |
| Green Hills | 是        | 较复杂  | 中等  |

**建议**：

- 在项目初期就配置好链接器脚本
- 将链接器脚本纳入版本控制
- 不同平台维护不同的链接器脚本文件

---

## 5. 使用注意事项

### 5.1 初始化函数编写规范

✓ **推荐的写法**：

```c
static void my_init(void) {
    /* 初始化代码 */
    /* 不要阻塞 */
    /* 不要睡眠 */
}
MR_INIT_EXPORT(my_init, MR_INIT_LEVEL_DRIVER);
```

✗ **不推荐的写法**：

```c
/* 错误：有返回值 */
int my_init(void) {  /* 错！必须是 void */
    return 0;
}

/* 错误：有参数 */
void my_init(int param) {  /* 错！必须无参数 */
    /* ... */
}

/* 错误：可能阻塞 */
void my_init(void) {
    while (!device_ready()) {
        /* 忙等待或睡眠 - 错！*/
    }
}
```

### 5.2 初始化级别选择

| 场景   | 推荐级别              | 示例           |
|------|-------------------|--------------|
| 硬件基础 | `"1.0"`           | IRQ、时钟       |
| 设备驱动 | `"1.1"`           | UART、SPI、I2C |
| 中间件  | `"8.0"` - `"8.4"` | 文件系统、网络协议栈   |
| 应用服务 | `"8.5"` - `"8.9"` | Web 服务器、CLI  |

### 5.3 常见问题

**Q: 如何调试初始化顺序？**

A: 可以在 `mr_user_init()` 中添加打印：

```c
for (call = start; call <= end; call++) {
    printf("Calling: %s\n", call->name);
    call->entry();
}
```

**Q: 如何处理初始化失败？**

A: 有几种方案：

1. 使用断言（致命错误）
2. 在初始化函数中记录错误状态
3. 提供一个单独的状态查询接口

**Q: 能否在初始化函数中使用动态内存？**

A: 可以，但要确保：

- 内核初始化已完成
- `mr_kernel_init()` 中包含了 `mr_libc_malloc_init()`

---

## 6. 参考资料

### 6.1 设计参考

- **Linux initcall**：Linux 内核的分级初始化机制
- **Zephyr SYS_INIT**：Zephyr RTOS 的初始化级别系统

### 6.2 延伸阅读

- **链接器脚本**：理解 SECTIONS 和 linker script
- **GCC 属性**：理解 `section`、`aligned`、`used` 等属性
- **嵌入式系统启动**：理解系统启动流程和初始化顺序
