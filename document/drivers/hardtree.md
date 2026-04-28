# hardtree - 硬件描述系统

## 1. 模块概述

### 1.1 功能介绍

hardtree 模块（简称 HT）是 mrX 框架的硬件描述系统，提供了一套编译时设备树生成和访问机制。它的设计灵感来源于 Zephyr RTOS 的 Devicetree 系统，但通过纯宏的方式实现，完全在编译期完成设备树的构建和访问，实现了零运行时开销。

hardtree 的核心理念是"编译时即运行时"：所有的硬件描述信息在编译期通过宏展开确定，不需要运行时解析设备树文件。这种方式特别适合资源受限的嵌入式系统，因为：

1. 无需存储和解析复杂的设备树文件
2. 无运行时查找开销
3. 编译期类型检查
4. 代码生成器可以自动生成绑定代码

hardtree 与 driver 模块紧密集成，通过 `MR_HT_INST_DRIVER_INSTALL` 宏可以从硬件描述自动生成驱动安装代码。

### 1.2 主要特性

- **编译时设备树**：所有信息在编译期确定，无运行时开销
- **DTS 输入格式**：使用标准 DTS（Device Tree Source）格式描述硬件
- **Compatible 匹配机制**：通过 `MR_HT_COMPAT` 标识平台特定的驱动类型
- **节点路径操作**：支持通过路径、标签、别名访问节点
- **属性访问**：支持单个属性和数组属性的访问
- **实例管理**：自动管理同类设备的多个实例
- **迭代支持**：支持遍历所有实例或数组元素
- **默认值支持**：属性可以指定默认值
- **与 driver 集成**：自动生成驱动安装代码
- **类型安全**：宏展开时进行类型检查

### 1.3 适用场景

hardtree 模块适用于以下场景：

- **硬件配置描述**：描述设备的硬件连接和配置参数
- **多设备管理**：管理同一类型的多个设备实例
- **板级配置**：不同板型的差异化配置
- **引脚复用**：描述引脚的多功能配置
- **驱动自动安装**：从设备树自动生成驱动代码

**不适用场景**：

- 需要运行时动态修改硬件配置
- 需要热插拔的设备（应使用动态驱动创建）

---

## 2. 使用指南

### 2.1 核心概念

#### 节点（Node）

节点是 hardtree 的基本单位，代表一个硬件设备或设备的一部分。每个节点有：

- **路径**：节点的唯一标识路径
- **属性**：节点的配置信息
- **父节点**：节点的上级节点

#### 实例（Instance）

实例是同一 compatible 类型下的具体设备。例如，系统有 3 个 UART 设备，它们都是同一 compatible 类型，但各自有不同的实例 ID（0、1、2）。

**重要说明**：

- 实例 ID 由 DTS 编译器自动分配，通常是连续的
- ID 的具体值（0、1、2...）由编译器决定，用户无需关心
- `_NUM` 宏（如 `uart_NUM`）表示该 compatible 类型的实例总数

#### Compatible 类型（MR_HT_COMPAT）

`MR_HT_COMPAT` 是平台特定的匹配标识符，定义在平台驱动代码中：

```c
/* WCH CH32V 平台的 GPIO 驱动 */
#define MR_HT_COMPAT wch_ch32v_gpio

/* WCH CH32V 平台的 UART 驱动 */
#define MR_HT_COMPAT wch_ch32v_uart
```

**命名约定**：

```
厂家_芯片系列[_细分型号][_外设前缀]外设
```

| 部分 | 说明 | 示例 |
|------|------|------|
| 厂家 | 芯片厂商 | wch、stm、gd |
| 芯片系列 | 芯片系列或型号 | ch32v、f103、f407 |
| 细分型号（可选） | 更细分的型号 | ch32v003、ch32v203 |
| 外设前缀（可选） | 区分同类型外设的不同变体 | dma、edma、xdma |
| 外设 | 外设类型 | gpio、uart、spi、i2c |

**示例**：

```c
/* 标准 GPIO */
#define MR_HT_COMPAT wch_ch32v_gpio

/* 标准 UART */
#define MR_HT_COMPAT wch_ch32v_uart

/* eDMA（增强型 DMA）*/
#define MR_HT_COMPAT nxp_lpc_edma

/* XDMA（特定 DMA 变体）*/
#define MR_HT_COMPAT xilinx_zynq_xdma
```

#### 属性（Property）

属性是节点的配置信息，分为两类：

- **标准属性**：框架定义的通用属性（如 NAME、ADDR、PARENT）
- **平台属性**：由平台驱动定义，使用厂家前缀命名（如 `wch_gpiox`、`wch_rccx`）

**属性命名约定**：

```
[厂家前缀_]属性名
```

| 属性名 | 类型 | 说明 |
|--------|------|------|
| NAME | 标准属性 | 节点名称 |
| ADDR | 标准属性 | 寄存器基地址 |
| PARENT | 标准属性 | 父节点 |
| INIT_LEVEL | 标准属性 | 初始化级别 |
| `wch_gpiox` | 平台属性 | WCH 平台的 GPIO 寄存器指针 |
| `wch_rccx` | 平台属性 | WCH 平台的 RCC 时钟值 |

### 2.2 节点访问接口

#### 路径访问

##### MR_HT_HAS_PATH()

```c
#define MR_HT_HAS_PATH(...) ...
```

**功能**：检查路径是否存在

**参数**：

- `...`：节点路径（可变参数，用 `/` 或逗号分隔）

**返回值**：

- `MR_TRUE`：路径存在
- `MR_FALSE`：路径不存在

**示例**：

```c
#if MR_HT_HAS_PATH(SOC, UART, 0)
    /* UART0 存在 */
#endif
```

##### MR_HT_PATH()

```c
#define MR_HT_PATH(...) ...
```

**功能**：通过路径获取节点

**参数**：

- `...`：节点路径

**返回值**：节点标识符

**示例**：

```c
#define UART0_NODE MR_HT_PATH(SOC, UART, 0)
```

#### 标签访问

##### MR_HT_HAS_LABEL()

```c
#define MR_HT_HAS_LABEL(_name) ...
```

**功能**：检查标签是否存在

##### MR_HT_LABEL()

```c
#define MR_HT_LABEL(_name) MR_HT_PATH(LABEL, _name)
```

**功能**：通过标签获取节点

**示例**：

```c
#if MR_HT_HAS_LABEL(my_uart)
    #define MY_UART MR_HT_LABEL(my_uart)
#endif
```

#### 别名访问

##### MR_HT_HAS_ALIAS()

```c
#define MR_HT_HAS_ALIAS(_name) ...
```

**功能**：检查别名是否存在

##### MR_HT_ALIAS()

```c
#define MR_HT_ALIAS(_name) MR_HT_PATH(ALIAS, _name)
```

**功能**：通过别名获取节点

**示例**：

```c
#define UART0 MR_HT_ALIAS(uart0)
```

### 2.3 实例管理接口

#### 实例检查和获取

##### MR_HT_HAS_INST()

```c
#define MR_HT_HAS_INST(_compat) ...
```

**功能**：检查是否有指定 compatible 类型的实例

##### MR_HT_INST()

```c
#define MR_HT_INST(_compat, _id) MR_HT_PATH(INST, _compat, _id)
```

**功能**：获取指定 compatible 类型和 ID 的实例节点

**参数**：

- `_compat`：compatible 类型名称
- `_id`：实例 ID

**示例**：

```c
/* 检查是否有 UART 实例 */
#if MR_HT_HAS_INST(uart)
    /* 获取 UART 实例 0 */
    #define UART0 MR_HT_INST(uart, 0)
    /* 获取 UART 实例 1 */
    #define UART1 MR_HT_INST(uart, 1)
#endif
```

#### 实例迭代

##### MR_HT_INST_FOREACH()

```c
#define MR_HT_INST_FOREACH(_compat, _fn, ...)
```

**功能**：遍历指定 compatible 类型的所有实例

**参数**：

- `_compat`：compatible 类型名称
- `_fn`：要调用的函数/宏，签名为 `fn(i, ...)`
- `...`：传递给函数的额外参数

**示例**：

```c
/* 定义遍历每个实例时执行的宏 */
#define SETUP_UART(i) \
    MR_DRIVER_INSTALL(uart_##i, \
                      MR_DRIVER_CLAZZES_STATIK(MR_CLAZZES_FIND(uart_driver)), \
                      MR_DRIVER_TYPE_UART | i, \
                      &uart##i##_config, \
                      &uart##i##_priv, \
                      &uart##i##_ops, \
                      MR_DRIVER_LEVEL_NORMAL)

/* 遍历所有 UART 实例并设置 */
MR_HT_INST_FOREACH(uart, SETUP_UART);
```

### 2.4 属性访问接口

#### 单个属性访问

##### MR_HT_NODE_HAS_PROP()

```c
#define MR_HT_NODE_HAS_PROP(_node, _prop) ...
```

**功能**：检查节点是否有指定属性

##### MR_HT_NODE_PROP()

```c
#define MR_HT_NODE_PROP(_node, _prop) MR_CAT(_node, MR_CAT_SEP(_P_, _prop))
```

**功能**：获取节点的属性值

**示例**：

```c
#define UART0_NODE MR_HT_INST(uart, 0)

#if MR_HT_NODE_HAS_PROP(UART0_NODE, BAUDRATE)
    #define UART0_BAUDRATE MR_HT_NODE_PROP(UART0_NODE, BAUDRATE)
#endif
```

##### MR_HT_NODE_PROP_OR()

```c
#define MR_HT_NODE_PROP_OR(_node, _prop, _default) ...
```

**功能**：获取节点属性值，如果不存在则返回默认值

**示例**：

```c
#define UART_BAUDRATE \
    MR_HT_NODE_PROP_OR(UART0_NODE, BAUDRATE, 115200)
```

#### 数组属性访问

##### MR_HT_NODE_HAS_PROP_ELEM()

```c
#define MR_HT_NODE_HAS_PROP_ELEM(_node, _prop, _elem, _idx) ...
```

**功能**：检查数组属性中指定索引的元素是否存在

##### MR_HT_NODE_PROP_ELEM()

```c
#define MR_HT_NODE_PROP_ELEM(_node, _prop, _elem, _idx) ...
```

**功能**：获取数组属性中指定索引的元素值

**参数**：

- `_node`：节点
- `_prop`：属性名称
- `_elem`：元素名称（用于标识数组类型）
- `_idx`：索引

**示例**：

```c
/* 获取 GPIO 数组的第 0 个元素 */
#define GPIO0 MR_HT_NODE_PROP_ELEM(NODE, GPIOS, gpio, 0)
```

##### MR_HT_NODE_PROP_ELEM_OR()

```c
#define MR_HT_NODE_PROP_ELEM_OR(_node, _prop, _elem, _idx, _default) ...
```

**功能**：获取数组元素值，如果不存在则返回默认值

##### MR_HT_NODE_PROP_ELEM_FOREACH()

```c
#define MR_HT_NODE_PROP_ELEM_FOREACH(_node, _prop, _fn, ...)
```

**功能**：遍历数组属性的所有元素

**示例**：

```c
/* 遍历所有 GPIO */
#define INIT_GPIO(i, node, prop) \
    gpio_init(MR_HT_NODE_PROP_ELEM(node, prop, gpio, i))

MR_HT_NODE_PROP_ELEM_FOREACH(NODE, GPIOS, INIT_GPIO);
```

### 2.5 节点信息接口

#### 节点名称

##### MR_HT_NODE_NAME()

```c
#define MR_HT_NODE_NAME(_node) MR_HT_NODE_PROP(_node, NAME)
```

**功能**：获取节点的名称

#### 父节点访问

##### MR_HT_NODE_HAS_PARENT()

```c
#define MR_HT_NODE_HAS_PARENT(_node) MR_HT_NODE_HAS_PROP(_node, PARENT)
```

**功能**：检查节点是否有父节点

##### MR_HT_NODE_PARENT()

```c
#define MR_HT_NODE_PARENT(_node) MR_HT_NODE_PROP(_node, PARENT)
```

**功能**：获取节点的父节点

#### 地址属性

##### MR_HT_NODE_HAS_ADDR()

```c
#define MR_HT_NODE_HAS_ADDR(_node) MR_HT_NODE_HAS_PROP(_node, ADDR)
```

**功能**：检查节点是否有地址属性

##### MR_HT_NODE_ADDR()

```c
#define MR_HT_NODE_ADDR(_node) MR_HT_NODE_PROP(_node, ADDR)
```

**功能**：获取节点的地址属性

### 2.6 与 driver 模块集成

#### MR_HT_COMPAT 的使用

在平台驱动代码中，首先定义 `MR_HT_COMPAT` 来标识当前驱动支持的硬件类型：

```c
/* arch/wch/32v/platform/drivers/gpio/gpio.c */
#include <mrX/platform/mr_platform.h>

/* 定义当前驱动的 compatible 类型 */
#define MR_HT_COMPAT wch_ch32v_gpio
```

定义后，就可以使用基于 `MR_HT_COMPAT` 的宏来访问实例和属性。

#### 驱动实例宏

##### MR_HT_HAS_INST_DRIVER()

```c
#define MR_HT_HAS_INST_DRIVER() MR_HT_HAS_INST(MR_HT_COMPAT)
```

**功能**：检查是否有当前 compatible 类型的驱动实例

##### MR_HT_INST_DRIVER()

```c
#define MR_HT_INST_DRIVER(_id) MR_HT_INST(MR_HT_COMPAT, _id)
```

**功能**：获取当前 compatible 类型的驱动实例

##### MR_HT_INST_DRIVER_FOREACH()

```c
#define MR_HT_INST_DRIVER_FOREACH(_fn, ...) \
    MR_HT_INST_FOREACH(MR_HT_COMPAT, _fn, ##__VA_ARGS__)
```

**功能**：遍历当前 compatible 类型的所有驱动实例

#### 驱动安装宏

##### MR_HT_INST_DRIVER_INSTALL()

```c
#define MR_HT_INST_DRIVER_INSTALL(_id, _dclazz, _type, _data, _priv, _ops) \
    MR_DRIVER_INSTALL(MR_HT_NODE_NAME(MR_HT_INST_DRIVER(_id)), \
                      _dclazz, _type, _data, _priv, _ops, \
                      MR_HT_NODE_PROP_OR(MR_HT_INST_DRIVER(_id), INIT_LEVEL, \
                                         MR_DRIVER_LEVEL_DEFER))
```

**功能**：通过 hardtree 安装驱动实例

**参数**：

- `_id`：实例 ID
- `_dclazz`：驱动类
- `_type`：驱动类型
- `_data`：驱动数据
- `_priv`：私有数据
- `_ops`：操作函数表

**自动处理**：

- 使用节点名称作为驱动名称
- 从节点属性读取初始化级别，默认为 DEFER

##### MR_HT_DRIVER_FIND()

```c
#define MR_HT_DRIVER_FIND(_node) MR_DRIVER_FIND(MR_HT_NODE_NAME(_node))
```

**功能**：通过节点查找对应的驱动

### 2.7 典型应用案例

#### 案例 1：WCH 平台 GPIO 驱动（真实示例）

这个案例展示了如何在平台驱动中使用 hardtree 自动安装驱动实例。

```c
/* arch/wch/32v/platform/drivers/gpio/gpio.c */

#include <mrX/platform/mr_platform.h>
#include <mrX/drivers/gpio/mr_gpio.h>

/* ========== 定义 Compatible 类型 ========== */
#define MR_HT_COMPAT wch_ch32v_gpio

/* ========== 定义 ops 实现 ========== */
static mr_err_t drv_pin_configure(const mr_driver_t *drv, mr_uint32_t pin,
                                  const mr_gpio_config_t *config) {
    /* ... 实现引脚配置 ... */
    return 0;
}

static const mr_gpio_ops_t __drv_gpio_ops = {
    .pin_configure = drv_pin_configure,
    /* ... 其他 ops ... */
};

/* ========== 私有数据结构 ========== */
typedef struct {
    GPIO_TypeDef *GPIOx;  /* GPIO 寄存器指针 */
    uint32_t RCCx;        /* RCC 时钟值 */
} drv_gpio_priv_t;

/* ========== 使用 HardTree 自动安装驱动 ========== */

#define WCH_HT_GPIO_INST_GPIO_INSTALL(_id, _ops)                               \
    /* 为每个实例定义私有数据 */                                               \
    static drv_gpio_priv_t __drv_gpio_priv_##_id                               \
        = {.GPIOx = MR_HT_NODE_PROP(MR_HT_INST_DRIVER(_id), wch_gpiox),        \
           .RCCx = MR_HT_NODE_PROP(MR_HT_INST_DRIVER(_id), wch_rccx)};         \
    /* 调用 GPIO 驱动框架的安装宏 */                                           \
    MR_HT_INST_GPIO_INSTALL(_id, &__drv_gpio_priv_##_id, _ops)

/* 遍历所有 GPIO 实例并安装 */
MR_HT_INST_DRIVER_FOREACH(WCH_HT_GPIO_INST_GPIO_INSTALL, &__drv_gpio_ops);
```

**代码解析**：

1. **`#define MR_HT_COMPAT wch_ch32v_gpio`**
   - 定义当前驱动支持的 compatible 类型
   - 与 DTS 中的 compatible 字符串对应

2. **`MR_HT_INST_DRIVER(_id)`**
   - 获取当前 compatible 类型的第 `_id` 个实例
   - 展开为 `MR_HT_INST(wch_ch32v_gpio, _id)`

3. **`MR_HT_NODE_PROP(node, wch_gpiox)`**
   - 从节点读取 `wch_gpiox` 属性
   - 这个属性在 DTS 中定义，由代码生成器生成宏

4. **`MR_HT_INST_DRIVER_FOREACH`**
   - 遍历所有 `wch_ch32v_gpio` 类型的实例
   - 为每个实例调用 `WCH_HT_GPIO_INST_GPIO_INSTALL`

**对应的 DTS 描述**：

```dts
/* mrX/board.dts */

/ {
    soc {
        gpioa: gpio@40010800 {
            compatible = "wch,ch32v-gpio";
            reg = <0x40010800>;
            wch_gpiox = <0x40010800>;  /* GPIOA 寄存器地址 */
            wch_rccx = <2>;             /* RCC 时钟值 */
            exist_mask = <0xffff>;
            invert_mask = <0x0000>;
        };

        gpiob: gpio@40010c00 {
            compatible = "wch,ch32v-gpio";
            reg = <0x40010c00>;
            wch_gpiox = <0x40010c00>;  /* GPIOB 寄存器地址 */
            wch_rccx = <3>;
            exist_mask = <0xffff>;
            invert_mask = <0x0000>;
        };
    };
};
```

#### 案例 2：WCH 平台 UART 驱动（带默认值）

```c
/* arch/wch/32v/platform/drivers/uart/uart.c */

#define MR_HT_COMPAT wch_ch32v_uart

typedef struct {
    USART_TypeDef *UARTx;
    uint32_t RCCx;
    GPIO_TypeDef *TX_GPIOx;
    uint32_t TX_GPIOx_RCCx;
    uint32_t TX_GPIOx_PINx;
    /* ... 更多字段 ... */
} drv_uart_priv_t;

/* 使用 MR_HT_NODE_PROP_OR 提供默认值 */
#define WCH_HT_UART_INST_UART_INSTALL(_id, _ops)                               \
    static drv_uart_priv_t __drv_uart_priv_##_id                               \
        = {.UARTx = MR_HT_NODE_PROP(MR_HT_INST_DRIVER(_id), wch_uartx),        \
           .RCCx = MR_HT_NODE_PROP(MR_HT_INST_DRIVER(_id), wch_rccx),          \
           /* 使用默认值 NULL（表示该引脚不存在）*/                             \
           .TX_GPIOx                                                           \
           = MR_HT_NODE_PROP_OR(MR_HT_INST_DRIVER(_id), wch_tx_gpiox, NULL),   \
           .TX_GPIOx_RCCx                                                      \
           = MR_HT_NODE_PROP_OR(MR_HT_INST_DRIVER(_id), wch_tx_gpiox_rccx, 0), \
           .TX_GPIOx_PINx                                                      \
           = MR_HT_NODE_PROP_OR(MR_HT_INST_DRIVER(_id), wch_tx_gpiox_pinx, 0), \
           /* RX 引脚也是可选的 */                                               \
           .RX_GPIOx                                                           \
           = MR_HT_NODE_PROP_OR(MR_HT_INST_DRIVER(_id), wch_rx_gpiox, NULL),   \
           .RX_GPIOx_RCCx                                                      \
           = MR_HT_NODE_PROP_OR(MR_HT_INST_DRIVER(_id), wch_rx_gpiox_rccx, 0), \
           .RX_GPIOx_PINx                                                      \
           = MR_HT_NODE_PROP_OR(MR_HT_INST_DRIVER(_id), wch_rx_gpiox_pinx, 0), \
           /* ... 更多字段 ... */                                               \
          };                                                                   \
    MR_HT_INST_UART_INSTALL(_id, &__drv_uart_priv_##_id, _ops)

MR_HT_INST_DRIVER_FOREACH(WCH_HT_UART_INST_UART_INSTALL, &__drv_uart_ops);
```

**关键点**：

- 使用 `MR_HT_NODE_PROP_OR` 为可选属性提供默认值
- `NULL` 表示该 GPIO 引脚不存在（如只使用 TX 不使用 RX）
- 数值 `0` 表示无效的时钟或引脚值

#### 案例 3：用户代码中查找和使用驱动

```c
/* 应用层代码 */

void example_use_gpio(void) {
    /* 方法 1：通过驱动名称查找（自动生成的名称） */
    mr_driver_t *gpioa = MR_DRIVER_FIND(gpioa);

    /* 方法 2：通过 hardtree 节点查找 */
    #define GPIOA_NODE MR_HT_INST(wch_ch32v_gpio, 0)
    mr_driver_t *gpioa2 = MR_HT_DRIVER_FIND(GPIOA_NODE);

    if (mr_driver_is_ready(gpioa)) {
        /* 配置引脚为输出 */
        mr_gpio_config_t config = {
            .mode = MR_GPIO_MODE_OUTPUT,
            .otype = MR_GPIO_OTYPE_PP,
            .level = MR_GPIO_LEVEL_LOW,
        };
        mr_gpio_configure(gpioa, 5, &config);

        /* 设置引脚值 */
        mr_gpio_write(gpioa, 5, 1);
    }
}

void example_list_all_uarts(void) {
    /* 遍历所有 UART 实例 */
    #define CHECK_UART(i) \
        do { \
            mr_driver_t *drv = MR_HT_DRIVER_FIND(MR_HT_INST(wch_ch32v_uart, i)); \
            if (drv && mr_driver_is_ready(drv)) { \
                printf("UART%d is ready\n", i); \
            } \
        } while (0)

    MR_HT_INST_FOREACH(wch_ch32v_uart, CHECK_UART);
}
```

---

## 3. 设计思路

### 3.1 设计目标

hardtree 模块的设计目标包括：

1. **零运行时开销**：所有操作在编译期完成
2. **类型安全**：编译期类型检查
3. **与 driver 集成**：无缝配合驱动框架
4. **代码生成友好**：方便工具生成绑定代码
5. **简洁的 API**：提供直观的宏接口
6. **灵活性**：支持多种访问方式和默认值

### 3.2 架构设计

#### 代码生成器工作流程

hardtree 采用 DTS（Device Tree Source）作为输入格式，通过代码生成器生成 C 头文件：

```
┌─────────────────┐
│  board.dts      │  (用户编写硬件描述)
│  (DTS 格式)      │
└────────┬────────┘
         │
         ↓
┌─────────────────┐
│  DTS 编译器      │  (mr_hardgen 工具)
│  (解析/生成)     │
└────────┬────────┘
         │
         ↓
┌─────────────────┐
│  mr_hardgen.h   │  (生成的头文件)
│  (宏定义)        │  放在 mrX 根目录
└────────┬────────┘
         │
         ↓
┌─────────────────┐
│  平台驱动代码     │  (使用 MR_HT_* 宏)
│  (编译时展开)     │
└─────────────────┘
```

**DTS 编译器的职责**：

1. 解析 DTS 文件，提取硬件描述信息
2. 为每个节点生成节点符号（`__MR_HT_N_*`）
3. 为每个属性生成属性宏（`*_P_*`）
4. 为每个 compatible 类型生成实例计数（`*_NUM`）
5. 生成实例 ID 分配（连续的 0、1、2...）

**用户代码的职责**：

1. 编写 DTS 文件描述硬件配置
2. 在平台驱动中定义 `MR_HT_COMPAT`
3. 使用 `MR_HT_*` 宏访问硬件信息
4. 使用 `MR_HT_INST_DRIVER_FOREACH` 自动生成驱动代码

#### 编译时设备树的实现

hardtree 不使用运行时数据结构，而是通过宏展开实现设备树：

```
源代码                          展开后的代码
────────────────────────────────────────────────
MR_HT_PATH(SOC, UART, 0)    →  __MR_HT_N_S_S0_UART_S1_0
MR_HT_INST(uart, 0)         →  __MR_HT_N_INST_uart_0
MR_HT_NODE_PROP(node, ADDR) →  node_P_ADDR
```

**工作原理**：

1. 路径组件通过 `_S_` 分隔符连接
2. 属性通过 `_P_` 分隔符连接
3. 数组元素通过 `_E_` 和 `_I_` 标识

#### 命名约定

hardtree 使用严格的命名约定来组织符号：

| 类型 | 前缀 | 分隔符 | 示例 |
|------|------|--------|------|
| 路径节点 | `__MR_HT_N` | `_S_` | `__MR_HT_N_S_SOC_S_UART_S_0` |
| 标签节点 | `__MR_HT_N_LABEL` | `_S_` | `__MR_HT_N_LABEL_S_my_uart` |
| 别名节点 | `__MR_HT_N_ALIAS` | `_S_` | `__MR_HT_N_ALIAS_S_uart0` |
| 实例节点 | `__MR_HT_N_INST` | `_` | `__MR_HT_N_INST_wch_ch32v_gpio_0` |
| 属性 | `{node}_P_` | `_` | `node_P_ADDR` |
| 数组元素 | `{node}_P_{prop}_E_{elem}_I_{idx}` | - | `node_P_GPIOS_E_gpio_I_0` |

**Compatible 类型命名**：

`MR_HT_COMPAT` 遵循 `厂家_芯片系列[_细分型号][_外设前缀]外设` 格式：

```
wch_ch32v_gpio     /* WCH CH32V 系列 GPIO */
wch_ch32v_uart     /* WCH CH32V 系列 UART */
nxp_lpc_edma       /* NXP LPC 系列 eDMA */
xilinx_zynq_xdma   /* Xilinx Zynq 系列 XDMA */
```

**属性命名**：

- **标准属性**：由框架定义（NAME、ADDR、PARENT、INIT_LEVEL 等）
- **平台属性**：使用厂家前缀，如 `wch_gpiox`、`wch_rccx`

### 3.3 实现要点

#### 条件编译的存在性检查

`MR_HT_HAS_*` 宏通过 `MR_MACRO_IS_USED` 实现编译期存在性检查：

```c
#define MR_HT_HAS_PATH(...) \
    MR_MACRO_IS_USED(MR_CAT(MR_HT_PATH(__VA_ARGS__), _EXIST))
```

**工作原理**：

1. 拼接符号和 `_EXIST` 后缀
2. 如果符号存在，`MR_MACRO_IS_USED` 返回 1
3. 如果符号不存在，返回 0

**使用场景**：

```c
#if MR_HT_HAS_INST(uart)
    /* 有 UART 实例，编译相关代码 */
#endif
```

#### 默认值机制

`MR_HT_NODE_PROP_OR` 使用 `MR_MACRO_IF` 实现默认值：

```c
#define MR_HT_NODE_PROP_OR(_node, _prop, _default) \
    MR_MACRO_IF(MR_HT_NODE_HAS_PROP(_node, _prop), \
                (MR_HT_NODE_PROP(_node, _prop)), (_default))
```

**工作原理**：

1. 检查属性是否存在
2. 存在则返回属性值
3. 不存在则返回默认值

#### 迭代实现

`MR_HT_INST_FOREACH` 和 `MR_HT_NODE_PROP_ELEM_FOREACH` 基于 listify 机制：

```c
#define MR_HT_INST_FOREACH(_compat, _fn, ...) \
    MR_MACRO_IF(MR_HT_HAS_INST(_compat), \
                (MR_LISTIFY(MR_CAT(MR_HT_PATH(INST, _compat), _NUM), (;) \
                 , _fn, ##__VA_ARGS__)), \
                ())
```

**工作原理**：

1. 检查实例是否存在
2. 获取实例数量（`*_NUM`）
3. 使用 `MR_LISTIFY` 展开调用

### 3.4 设计权衡

#### 为什么选择编译时而非运行时？

**编译时的优势**：

- 零运行时开销
- 编译期类型检查
- 代码体积更小（无需存储设备树）
- 适合资源受限系统

**运行时的优势**：

- 灵活性更高
- 支持热插拔
- 可以从外部文件加载

**hardtree 的选择**：

对于嵌入式场景，硬件配置在编译时确定，编译时方案更合适。

#### 与 Zephyr Devicetree 的对比

| 特性 | Zephyr Devicetree | mrX hardtree |
|------|------------------|--------------|
| 实现方式 | 编译时 + 运行时 | 纯编译时 |
| 数据存储 | ELF section | 宏符号 |
| 访问方式 | 函数调用 | 宏展开 |
| 运行时开销 | 有查找开销 | 零开销 |
| 灵活性 | 较高 | 编译期确定 |

**设计理念差异**：

- Zephyr：兼顾灵活性和性能
- mrX：极致优化性能和代码体积

#### 命名复杂度的权衡

hardtree 的符号命名较为复杂（如 `__MR_HT_N_S_SOC_S_UART_S_0`）：

**为什么这样设计**：

1. 全局唯一：避免符号冲突
2. 可解析：命名包含完整路径信息
3. 自动化：由工具生成，用户不直接使用

**用户体验**：

- 用户通过简洁的宏接口访问（`MR_HT_PATH`）
- 复杂符号由宏处理，不暴露给用户

---

## 4. 参考资料

### 4.1 相关模块

- **driver**：驱动框架，hardtree 与其紧密集成
- **listify**：列表展开机制，用于实现迭代功能
- **service**：通用工具宏，提供宏编程基础

### 4.2 设计参考

- **Zephyr Devicetree**：Zephyr RTOS 的设备树系统，hardtree 的主要灵感来源
- **Linux Devicetree**：Linux 内核的设备树规范
- **DTS (Device Tree Source)**：设备树源语言，hardtree 的输入格式

### 4.3 延伸阅读

- **设备树规范**：理解设备树的标准格式
- **编译期元编程**：理解宏的高级用法
- **链接器脚本**：理解符号和段的布局
- **代码生成技术**：理解如何自动生成绑定代码
