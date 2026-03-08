# driver - 驱动框架

## 1. 模块概述

### 1.1 功能介绍

driver 模块是 mrX 框架的核心驱动框架，为所有设备驱动提供统一的抽象接口和管理机制。它基于 object 模块实现，将面向对象的设计理念应用到设备驱动领域，使得不同类型的设备（GPIO、UART、I2C、ADC 等）可以通过统一的接口进行管理。

driver 模块的设计灵感来源于 Linux 内核的设备驱动模型和 Zephyr 的设备模型，但针对资源受限的嵌入式系统进行了精简和优化。它提供了一套完整的驱动生命周期管理机制，从驱动的注册、初始化、启用到关闭和卸载，确保驱动在整个系统生命周期中的正确运行。

### 1.2 主要特性

- **统一的驱动抽象**：所有设备驱动都继承自 `mr_driver_t` 基类
- **完整的生命周期管理**：init、setup、shutdown、del 四个阶段
- **引用计数机制**：基于 object 模块，防止驱动在使用中被意外卸载
- **分级初始化**：支持 EARLY、PRIO、NORMAL、LAST、DEFER 五个初始化级别
- **原子操作保护**：驱动的状态和标志使用原子操作，确保线程安全
- **静态和动态驱动**：同时支持编译期静态驱动和运行时动态驱动
- **类型标识系统**：通过主类型和子类型标识驱动类型
- **HardTree 集成**：与硬件描述系统无缝集成，自动从设备树生成驱动代码
- **驱动查找机制**：通过名称快速查找已注册的驱动

### 1.3 适用场景

driver 模块适用于以下场景：

- **外设驱动**：GPIO、UART、I2C、SPI、ADC、DAC 等外设设备
- **存储设备**：RAM、Flash、SD 卡等存储设备
- **网络设备**：以太网、WiFi、蓝牙等网络接口
- **人机交互**：LED、按键、显示屏等交互设备
- **传感器**：温度、湿度、加速度等传感器

**不适用场景**：

- 纯软件功能模块（应使用 service 模块）
- 不需要硬件抽象的底层寄存器操作

---

## 2. 使用指南

### 2.1 核心数据结构

#### mr_driver_type_t

```c
typedef enum mr_driver_type {
    MR_DRIVER_TYPE_NONE = 0,
    MR_DRIVER_TYPE_RAM = 0x0100,
    MR_DRIVER_TYPE_GPIO = 0x0200,
    MR_DRIVER_TYPE_UART = 0x0300,
    MR_DRIVER_TYPE_I2C = 0x0400,
    MR_DRIVER_TYPE_ADC = 0x0500,
    MR_DRIVER_TYPE_DAC = 0x0600,
    MR_DRIVER_TYPE_DMA = 0x0700,
    MR_DRIVER_TYPE_MASK = 0xff00,
} mr_driver_type_t;
```

驱动类型枚举，用于标识驱动的类型：

- **高字节（主类型）**：表示驱动的类别（RAM、GPIO、UART 等）
- **低字节（子类型）**：由具体驱动定义，表示同一类别下的不同变体

#### mr_driver_clazz_t

```c
typedef struct mr_driver_clazz {
    const char *name;
    mr_driver_method_t *init;
    mr_driver_method_t *deinit;
} mr_driver_clazz_t;
```

驱动类信息，描述一个驱动类型：

- `name`：驱动类名称
- `init`：驱动的初始化函数
- `deinit`：驱动的清理函数

#### mr_driver_t

```c
typedef struct mr_driver {
    mr_object_t parent;        /* 父类对象 */
    const mr_driver_clazz_t *clazz;  /* 驱动类 */
    mr_atomic_t flags;         /* 驱动标志 */
    mr_uint16_t type;          /* 驱动类型 */
    mr_int16_t state;          /* 驱动状态 */
    mr_ptr_t data;             /* 驱动数据 */
    mr_ptr_t priv;             /* 私有数据 */
    mr_ptr_t ops;              /* 私有操作 */
} mr_driver_t;
```

所有驱动的基础结构：

- `parent`：继承自 mr_object_t，支持引用计数
- `clazz`：指向驱动类信息
- `flags`：原子标志，包括 SETUP、LOCK、DYING
- `type`：驱动类型标识
- `state`：驱动加载状态（0 表示成功，负值表示错误码）
- `data`：**驱动框架层的运行时数据**（由驱动框架定义，如 GPIO 的 exist_mask）
- `priv`：**平台特定的私有数据**（由平台驱动定义和使用，如寄存器基地址）
- `ops`：**驱动框架要求的操作接口**（由平台驱动实现，如 GPIO 的 pin_write）

**data、ops、priv 的职责划分**：

| 成员 | 定义者 | 使用者 | 作用 | 示例 |
|------|--------|--------|------|------|
| `data` | 驱动框架 | 驱动框架代码 | 框架需要的运行时数据 | GPIO 的 exist_mask、invert_mask |
| `ops` | 驱动框架 | 平台驱动实现 | 框架要求的操作函数接口 | pin_write、pin_read 等函数指针 |
| `priv` | 平台驱动 | 平台驱动代码 | 平台特定的私有数据 | 寄存器基地址、中断号等 |

#### 驱动标志定义

```c
#define MR_DRIVER_FLAG_SETUP    (1U << 0)  /* 驱动已设置 */
#define MR_DRIVER_FLAG_LOCK     (1U << 1)  /* 驱动已锁定 */
#define MR_DRIVER_FLAG_DYING    (1U << 2)  /* 驱动正在销毁 */
```

### 2.2 主要接口说明

#### 驱动类定义接口

##### MR_DRIVER_CLAZZES_DEFINE()

```c
#define MR_DRIVER_CLAZZES_DEFINE(_name, _init, _statik_deinit, _dynamic_deinit)
```

**功能**：定义一个驱动的类信息对（静态和动态）

**参数**：

- `_name`：驱动类名称
- `_init`：初始化函数
- `_statik_deinit`：静态驱动的清理函数
- `_dynamic_deinit`：动态驱动的清理函数（通常需要释放内存）

**示例**：

```c
static mr_err_t my_driver_init(const struct mr_driver *drv) {
    printf("Driver initialized\n");
    return 0;
}

static void my_driver_statik_deinit(const struct mr_driver *drv) {
    printf("Static driver cleanup\n");
}

static void my_driver_dynamic_deinit(const struct mr_driver *drv) {
    printf("Dynamic driver cleanup\n");
    mr_driver_t *driver = (mr_driver_t *)drv;
    /* 动态驱动需要释放内存 */
    free(driver);
}

MR_DRIVER_CLAZZES_DEFINE(my_driver,
                        my_driver_init,
                        my_driver_statik_deinit,
                        my_driver_dynamic_deinit);
```

**关于 init 为 NULL**：

并非所有驱动都需要统一的初始化流程。对于简单的驱动框架（如 GPIO），可以将 `init` 设为 `MR_NULL`：

```c
/* GPIO 驱动不需要统一的初始化 */
MR_DRIVER_CLAZZES_DEFINE(gpio, MR_NULL, MR_NULL, gpio_dynamic_deinit);
```

**选择建议**：

- **有初始化需求**：如 UART（需要配置波特率）、I2C（需要配置时钟）等
- **无初始化需求**：如 GPIO（只提供操作接口，由平台驱动负责硬件初始化）

#### 驱动安装宏

##### MR_DRIVER_INSTALL()

```c
#define MR_DRIVER_INSTALL(_name, _dclazz, _type, _data, _priv, _ops, _level)
```

**功能**：安装一个静态驱动，自动注册到初始化系统

**参数**：

- `_name`：驱动名称
- `_dclazz`：驱动类
- `_type`：驱动类型
- `_data`：驱动数据
- `_priv`：私有数据
- `_ops`：操作函数表
- `_level`：初始化级别

**初始化级别**：

```c
#define MR_DRIVER_LEVEL_EARLY       "1"         /* 最早初始化 */
#define MR_DRIVER_LEVEL_PRIO(_prio) "2." _prio  /* 优先级初始化 */
#define MR_DRIVER_LEVEL_NORMAL      "3"         /* 正常初始化 */
#define MR_DRIVER_LEVEL_LAST        "4"         /* 最后初始化 */
#define MR_DRIVER_LEVEL_DEFER       "9"         /* 延迟初始化 */
```

**示例**：

```c
/* 定义驱动的私有数据 */
static const my_gpio_config_t gpio_config = {
    .port = 0,
    .pin = 5,
    .direction = GPIO_OUTPUT,
};

/* 定义驱动的操作函数 */
static const my_gpio_ops_t gpio_ops = {
    .write = my_gpio_write,
    .read = my_gpio_read,
};

/* 安装驱动 - 在正常初始化阶段自动初始化 */
MR_DRIVER_INSTALL(gpio0,
                  MR_DRIVER_CLAZZES_STATIK(MR_CLAZZES_FIND(my_driver)),
                  MR_DRIVER_TYPE_GPIO | 0,
                  &gpio_config,
                  NULL,
                  &gpio_ops,
                  MR_DRIVER_LEVEL_NORMAL);
```

#### 驱动生命周期接口

##### mr_driver_init()

```c
mr_err_t mr_driver_init(mr_driver_t *drv,
                        const mr_driver_clazz_t *dclazz,
                        mr_uint16_t type,
                        void *data,
                        void *priv,
                        void *ops);
```

**功能**：初始化一个驱动对象

**参数**：

- `drv`：驱动对象指针
- `dclazz`：驱动类
- `type`：驱动类型
- `data`：驱动数据
- `priv`：私有数据
- `ops`：操作函数表

**返回值**：

- `0`：成功
- `-MR_EINVAL`：参数错误

##### mr_driver_create()

```c
mr_driver_t *mr_driver_create(const mr_driver_clazz_t *dclazz,
                              mr_uint16_t type,
                              void *data,
                              void *priv,
                              void *ops);
```

**功能**：创建一个动态驱动（自动分配内存）

**返回值**：

- 成功：返回驱动对象指针
- 失败：返回 `MR_NULL`

##### mr_driver_setup()

```c
mr_err_t mr_driver_setup(mr_driver_t *drv);
```

**功能**：设置驱动（调用 init 函数）

**返回值**：

- `0`：成功
- `-MR_EBUSY`：驱动已被锁定
- `-MR_ENOENT`：驱动正在销毁

**注意事项**：

- 使用原子锁保证线程安全
- 幂等操作：多次调用只执行一次初始化

##### mr_driver_shutdown()

```c
mr_err_t mr_driver_shutdown(mr_driver_t *drv);
```

**功能**：关闭驱动（调用 deinit 函数）

**返回值**：

- `0`：成功
- `-MR_EBUSY`：驱动已被锁定

##### mr_driver_del()

```c
mr_err_t mr_driver_del(mr_driver_t *drv);
```

**功能**：删除驱动（释放引用计数）

**说明**：

- 标记驱动为 DYING 状态
- 当引用计数归零时，调用 release 函数

#### 驱动查找接口

##### MR_DRIVER_FIND()

```c
#define MR_DRIVER_FIND(_name) (&MR_CAT(__mr_driver_, _name))
```

**功能**：通过名称查找已安装的驱动

**参数**：

- `_name`：驱动名称

**返回值**：驱动对象指针，如果未定义则编译错误

##### mr_driver_find()

```c
mr_driver_t *mr_driver_find(const char *name);
```

**功能**：运行时查找驱动（用于动态场景）

**参数**：

- `name`：驱动名称字符串

**返回值**：

- 成功：返回驱动对象指针
- 失败：返回 `MR_NULL`

#### 引用计数接口

##### mr_driver_get()

```c
mr_driver_t *mr_driver_get(mr_driver_t *drv);
```

**功能**：获取驱动引用（增加引用计数）

##### mr_driver_put()

```c
mr_bool_t mr_driver_put(mr_driver_t *drv);
```

**功能**：释放驱动引用（减少引用计数）

**返回值**：

- `MR_TRUE`：引用计数归零，对象已释放
- `MR_FALSE`：引用计数仍大于 0

#### 状态查询接口

##### mr_driver_is_ready()

```c
mr_bool_t mr_driver_is_ready(mr_driver_t *drv);
```

**功能**：检查驱动是否就绪

**返回值**：

- `MR_TRUE`：驱动已初始化且状态正常
- `MR_FALSE`：驱动未初始化或状态异常

##### mr_driver_type()

```c
mr_uint16_t mr_driver_type(mr_driver_t *drv);
```

**功能**：获取驱动类型

#### 辅助宏

##### MR_DRIVER_DATA()

```c
#define MR_DRIVER_DATA(_drv) ((void *)((mr_driver_t *)(_drv))->data)
```

**功能**：获取驱动的数据指针

##### MR_DRIVER_PRIV()

```c
#define MR_DRIVER_PRIV(_drv) ((void *)((mr_driver_t *)(_drv))->priv)
```

**功能**：获取驱动的私有数据指针

##### MR_DRIVER_OPS()

```c
#define MR_DRIVER_OPS(_drv) ((void *)((mr_driver_t *)(_drv))->ops)
```

**功能**：获取驱动的操作函数表指针

##### MR_DRIVER_ASSERT()

```c
#define MR_DRIVER_ASSERT(_drv, _type)
```

**功能**：断言驱动有效且类型匹配

**检查项**：

- 驱动指针非空
- 驱动已初始化
- 驱动类是 driver
- 驱动已设置（SETUP 标志）
- 驱动类型匹配

### 2.3 典型应用案例

#### 案例 1：真实 GPIO 驱动的 data/ops/priv 划分

这个案例展示了驱动框架层和平台驱动层如何协作。

```c
#include <mrX/drivers/gpio/mr_gpio.h>

/* ========== 驱动框架层 (mrX/drivers/gpio/gpio.c) ========== */

/* data：由驱动框架定义，框架代码使用的数据结构 */
typedef struct mr_gpio_data {
    mr_uint32_t exist_mask;   /* 存在的 GPIO 引脚掩码 */
    mr_uint32_t invert_mask;  /* 电平反转掩码 */
} mr_gpio_data_t;

/* ops：由驱动框架定义，要求平台驱动实现的操作接口 */
typedef struct mr_gpio_ops {
    mr_err_t (*pin_configure)(const mr_driver_t *drv, mr_uint32_t pin,
                              const mr_gpio_config_t *config);
    mr_err_t (*pin_write)(const mr_driver_t *drv, mr_uint32_t pin,
                          mr_uint8_t value);
    mr_err_t (*pin_read)(const mr_driver_t *drv, mr_uint32_t pin,
                         mr_uint8_t *value);
    /* ... 更多操作 ... */
} mr_gpio_ops_t;

/* 驱动框架函数：使用 data 和 ops */
mr_err_t mr_gpio_write(mr_driver_t *drv, mr_uint32_t pin, mr_uint8_t value) {
    const mr_gpio_ops_t *gp_ops;
    mr_gpio_data_t *gp_data;

    /* 获取 data（框架定义的数据）和 ops（平台实现的操作） */
    gp_data = (mr_gpio_data_t *)MR_DRIVER_DATA(drv);
    gp_ops = (mr_gpio_ops_t *)MR_DRIVER_OPS(drv);

    /* 使用 data 检查引脚是否存在 */
    if (!(gp_data->exist_mask & (1U << pin))) {
        return -MR_EINVAL;
    }

    /* 使用 data 处理电平反转 */
    value = (!!value) ^ (!!(gp_data->invert_mask & (1U << pin)));

    /* 调用 ops（平台实现的函数）执行实际操作 */
    return gp_ops->pin_write(drv, pin, value);
}

/* GPIO 驱动类：init 为 NULL，因为不需要统一初始化 */
MR_DRIVER_CLAZZES_DEFINE(gpio, MR_NULL, MR_NULL, gpio_dynamic_deinit);

/* ========== 平台驱动层 (arch/wch/32v/platform/drivers/gpio/gpio.c) ========== */

/* priv：由平台驱动定义，存储平台特定的私有数据 */
typedef struct {
    volatile uint32_t *base_addr;  /* GPIO 寄存器基地址 */
    int irq_num;                   /* 中断号 */
} wch_gpio_priv_t;

/* ops 实现：平台驱动实现框架要求的操作接口 */
static mr_err_t wch_gpio_pin_write(const mr_driver_t *drv,
                                   mr_uint32_t pin,
                                   mr_uint8_t value) {
    /* 获取 priv（平台私有数据） */
    wch_gpio_priv_t *priv = MR_DRIVER_PRIV(drv);

    /* 使用 priv 中的基地址操作硬件寄存器 */
    if (value) {
        priv->base_addr[BSRR] = (1U << pin);
    } else {
        priv->base_addr[BSRR] = (1U << (pin + 16));
    }
    return 0;
}

/* ... 其他 ops 实现 ... */

static const mr_gpio_ops_t wch_gpio_ops = {
    .pin_configure = wch_gpio_pin_configure,
    .pin_write = wch_gpio_pin_write,
    .pin_read = wch_gpio_pin_read,
    /* ... */
};

/* 安装驱动 */
static const wch_gpio_priv_t gpioa_priv = {
    .base_addr = (volatile uint32_t *)0x40010800,
    .irq_num = 10,
};

static const mr_gpio_data_t gpioa_data = {
    .exist_mask = 0xffff,  /* PA0-PA15 都存在 */
    .invert_mask = 0x0000, /* 不反转电平 */
};

MR_DRIVER_INSTALL(gpioa,
                  MR_DRIVER_CLAZZES_STATIK(MR_DRIVER_CLAZZES_FIND(gpio)),
                  MR_DRIVER_TYPE_GPIO | 0,
                  &gpioa_data,    /* data：框架定义的数据 */
                  &gpioa_priv,    /* priv：平台私有数据 */
                  &wch_gpio_ops,  /* ops：平台实现的操作 */
                  MR_DRIVER_LEVEL_NORMAL);
```

**职责划分总结**：

| 组件 | 定义位置 | 使用位置 | 作用 |
|------|----------|----------|------|
| `mr_gpio_data_t` (data) | 驱动框架 | 框架代码 | 框架需要的运行时数据（如引脚掩码） |
| `mr_gpio_ops_t` (ops) | 驱动框架 | 平台驱动 | 框架要求的操作接口定义 |
| `wch_gpio_priv_t` (priv) | 平台驱动 | 平台驱动 | 平台特定的私有数据（如寄存器地址） |
| `wch_gpio_ops` 实现 | 平台驱动 | 框架代码 | 具体的硬件操作实现 |

#### 案例 2：定义和使用静态驱动

```c
#include <mrX/drivers/mr_driver.h>
#include <mrX/kernel/mr_initcall.h>

/* ========== 驱动实现 ========== */

/* 驱动私有数据 */
typedef struct {
    volatile uint32_t *base_addr;
    int irq_num;
} my_uart_priv_t;

/* 驱动配置数据 */
typedef struct {
    int baudrate;
    int data_bits;
    int parity;
} my_uart_config_t;

/* 驱动初始化 */
static mr_err_t my_uart_init(const struct mr_driver *drv) {
    my_uart_priv_t *priv = MR_DRIVER_PRIV(drv);
    my_uart_config_t *config = MR_DRIVER_DATA(drv);

    printf("UART init: baudrate=%d, bits=%d\n",
           config->baudrate, config->data_bits);

    /* 硬件初始化... */

    return 0;
}

/* 驱动清理 */
static void my_uart_deinit(const struct mr_driver *drv) {
    printf("UART deinit\n");
    /* 硬件清理... */
}

/* 定义驱动类 */
MR_DRIVER_CLAZZES_DEFINE(my_uart,
                        my_uart_init,
                        my_uart_deinit,
                        my_uart_deinit);

/* ========== 驱动安装 ========== */

/* 私有数据 */
static my_uart_priv_t uart0_priv = {
    .base_addr = (volatile uint32_t *)0x40000000,
    .irq_num = 10,
};

/* 配置数据 */
static const my_uart_config_t uart0_config = {
    .baudrate = 115200,
    .data_bits = 8,
    .parity = 0,
};

/* 安装驱动 */
MR_DRIVER_INSTALL(uart0,
                  MR_DRIVER_CLAZZES_STATIK(MR_CLAZZES_FIND(my_uart)),
                  MR_DRIVER_TYPE_UART | 0,
                  &uart0_config,
                  &uart0_priv,
                  NULL,
                  MR_DRIVER_LEVEL_NORMAL);

/* ========== 使用驱动 ========== */

void example_use_uart(void) {
    mr_driver_t *uart0;

    /* 查找驱动 */
    uart0 = MR_DRIVER_FIND(uart0);

    /* 获取引用 */
    mr_driver_get(uart0);

    /* 检查是否就绪 */
    if (mr_driver_is_ready(uart0)) {
        printf("UART is ready\n");

        /* 获取配置数据 */
        my_uart_config_t *config = MR_DRIVER_DATA(uart0);
        printf("Baudrate: %d\n", config->baudrate);
    }

    /* 释放引用 */
    mr_driver_put(uart0);
}
```

#### 案例 2：动态创建驱动

```c
#include <mrX/drivers/mr_driver.h>
#include <stdlib.h>

typedef struct {
    int channel;
    int resolution;
} adc_priv_t;

static mr_err_t my_adc_init(const struct mr_driver *drv) {
    adc_priv_t *priv = MR_DRIVER_PRIV(drv);
    printf("ADC channel %d init\n", priv->channel);
    return 0;
}

static void my_adc_dynamic_deinit(const struct mr_driver *drv) {
    adc_priv_t *priv = MR_DRIVER_PRIV(drv);
    printf("ADC channel %d deinit\n", priv->channel);
    /* 动态驱动需要释放私有数据 */
    free(priv);
}

MR_DRIVER_CLAZZES_DEFINE(my_adc,
                        my_adc_init,
                        NULL,
                        my_adc_dynamic_deinit);

void example_create_adc(int channel) {
    const mr_driver_clazz_t *dclazz =
        MR_DRIVER_CLAZZES_DYNAMIC(MR_CLAZZES_FIND(my_adc));
    mr_driver_t *adc;

    /* 分配私有数据 */
    adc_priv_t *priv = malloc(sizeof(adc_priv_t));
    priv->channel = channel;
    priv->resolution = 12;

    /* 创建驱动 */
    adc = mr_driver_create(dclazz,
                          MR_DRIVER_TYPE_ADC | 0,
                          NULL,  /* 无配置数据 */
                          priv,  /* 私有数据 */
                          NULL); /* 无操作表 */

    if (adc) {
        /* 设置驱动 */
        mr_driver_setup(adc);

        /* 使用驱动... */

        /* 删除驱动（会自动释放内存） */
        mr_driver_del(adc);
    }
}
```

#### 案例 3：使用驱动类型检查

```c
#include <mrX/drivers/mr_driver.h>

/* 通用驱动处理函数 */
void process_driver(mr_driver_t *drv) {
    /* 获取驱动类型 */
    mr_uint16_t type = mr_driver_type(drv);

    /* 根据类型处理 */
    switch (type & MR_DRIVER_TYPE_MASK) {
        case MR_DRIVER_TYPE_GPIO:
            printf("This is a GPIO driver\n");
            break;

        case MR_DRIVER_TYPE_UART:
            printf("This is a UART driver\n");
            break;

        case MR_DRIVER_TYPE_I2C:
            printf("This is an I2C driver\n");
            break;

        default:
            printf("Unknown driver type: 0x%x\n", type);
            break;
    }
}

/* 类型检查宏示例 */
void example_gpio_operation(mr_driver_t *drv) {
    /* 断言这是一个 GPIO 驱动 */
    MR_DRIVER_ASSERT(drv, MR_DRIVER_TYPE_GPIO);

    /* 安全地执行 GPIO 操作 */
    printf("Performing GPIO operation\n");
}
```

#### 案例 4：带优先级的驱动初始化

```c
#include <mrX/drivers/mr_driver.h>

/* 时钟驱动 - 最早初始化 */
MR_DRIVER_INSTALL(clk,
                  MR_DRIVER_CLAZZES_STATIK(MR_CLAZZES_FIND(clk_driver)),
                  MR_DRIVER_TYPE_NONE,
                  NULL, NULL, NULL,
                  MR_DRIVER_LEVEL_EARLY);

/* GPIO 驱动 - 优先级 10，依赖时钟 */
MR_DRIVER_INSTALL(gpio,
                  MR_DRIVER_CLAZZES_STATIK(MR_CLAZZES_FIND(gpio_driver)),
                  MR_DRIVER_TYPE_GPIO | 0,
                  NULL, NULL, NULL,
                  MR_DRIVER_LEVEL_PRIO("10"));

/* UART 驱动 - 优先级 20，依赖 GPIO */
MR_DRIVER_INSTALL(uart,
                  MR_DRIVER_CLAZZES_STATIK(MR_CLAZZES_FIND(uart_driver)),
                  MR_DRIVER_TYPE_UART | 0,
                  NULL, NULL, NULL,
                  MR_DRIVER_LEVEL_PRIO("20"));

/* 应用层驱动 - 最后初始化 */
MR_DRIVER_INSTALL(app,
                  MR_DRIVER_CLAZZES_STATIK(MR_CLAZZES_FIND(app_driver)),
                  MR_DRIVER_TYPE_NONE,
                  NULL, NULL, NULL,
                  MR_DRIVER_LEVEL_LAST);

/* 初始化顺序：
 *   1. clk (EARLY = "1")
 *   2. gpio (PRIO = "2.10")
 *   3. uart (PRIO = "2.20")
 *   4. app (LAST = "4")
 */
```

---

## 3. 设计思路

### 3.1 设计目标

driver 模块的设计目标包括：

1. **统一抽象**：为所有设备驱动提供统一的接口和管理机制
2. **生命周期管理**：完整的驱动生命周期，从创建到销毁
3. **类型安全**：通过类型标识防止类型混淆
4. **线程安全**：使用原子操作保护关键状态
5. **灵活初始化**：支持分级初始化，处理驱动间的依赖关系
6. **零开销抽象**：静态驱动在编译期完全确定，无额外运行时成本
7. **HardTree 集成**：与硬件描述系统无缝配合

### 3.2 架构设计

#### 继承自 object 模块

driver 模块完全基于 object 模块实现：

```c
typedef struct mr_driver {
    mr_object_t parent;    /* 继承自 object */
    /* ... 驱动特有成员 ... */
} mr_driver_t;
```

**优势**：

- 自动获得引用计数功能
- 支持动态类型检查
- 统一的生命周期管理

#### 双 clazz 设计

每个驱动类型需要定义两个 clazz：

```c
typedef struct mr_driver_clazzes {
    const mr_driver_clazz_t statik;    /* 静态驱动 */
    const mr_driver_clazz_t dynamic;   /* 动态驱动 */
} mr_driver_clazzes_t;
```

**设计理由**：

1. **不同的内存管理方式**：
   - `statik`：用于静态分配的驱动，释放时不释放内存
   - `dynamic`：用于动态分配的驱动，释放时会调用 free()

2. **类型安全**：防止错误地释放栈上驱动的内存

#### 驱动状态机

驱动的状态转换如下：

```
   [未初始化] -- mr_driver_init() --> [已初始化]
                                         |
                                         v
                                    mr_driver_setup()
                                         |
                                         v
   [已设置] <-- MR_DRIVER_FLAG_SETUP ---+
       |
       v
   mr_driver_shutdown()
       |
       v
   [已关闭]
       |
       v
   mr_driver_del() --> [DYING] --> 引用计数归零 --> [已释放]
```

**状态标志**：

- `MR_DRIVER_FLAG_SETUP`：驱动已完成 setup
- `MR_DRIVER_FLAG_LOCK`：驱动正在被操作（线程安全）
- `MR_DRIVER_FLAG_DYING`：驱动正在销毁，拒绝新操作

#### 原子操作保护

驱动使用原子操作保护关键状态：

```c
static mr_err_t driver_lock(mr_driver_t *drv) {
    mr_atomic_t flags, lock;

    do {
        flags = mr_atomic_load(&drv->flags);

        /* 检查是否正在销毁 */
        if (flags & MR_DRIVER_FLAG_DYING) {
            return -MR_ENOENT;
        }

        /* 检查是否已锁定 */
        if (flags & MR_DRIVER_FLAG_LOCK) {
            return -MR_EBUSY;
        }

        /* 尝试获取锁 */
        lock = flags | MR_DRIVER_FLAG_LOCK;
    } while (!mr_atomic_compare_exchange(&drv->flags, &flags, lock));

    return 0;
}
```

**设计要点**：

- 使用 CAS（Compare-And-Swap）实现无锁算法
- 检查 DYING 状态，防止操作正在销毁的驱动
- 检查 LOCK 状态，实现简单的互斥

#### 分级初始化机制

driver 模块通过链接段的排序实现分级初始化：

```c
#define MR_DRIVER_INSTALL(_name, ...) \
    MR_DRIVER_DEFINE(_name, ...); \
    MR_USED const mr_driver_entry_t MR_CAT(__mr_driver_entry_, _name) \
        MR_SECTION_ALIGN("mr_driver." _level) \
        = {.name = MR_STR(_name), .driver = MR_DRIVER_FIND(_name)}
```

**工作原理**：

1. 每个驱动定义一个 `mr_driver_entry_t` 结构体
2. 结构体被放入特定的链接段：`mr_driver.{level}`
3. 链接器按段名字典序排序
4. 初始化时按顺序遍历所有驱动

**初始化顺序**：

```
"mr_driver.0.end"    (start 标记 - 定义自动初始化范围的起点)
"mr_driver.1"        (EARLY - 最早初始化的驱动)
"mr_driver.2.0"      (PRIO 0 - 优先级 0)
"mr_driver.2.1"      (PRIO 1 - 优先级 1)
...
"mr_driver.3"        (NORMAL - 正常初始化)
"mr_driver.4"        (LAST - 最后初始化)
"mr_driver.8.end"    (defer 标记 - 定义自动初始化范围的终点)
"mr_driver.9"        (DEFER - 延迟初始化，需手动触发)
"mr_driver.9.end"    (end 标记 - 定义驱动列表的终点)
```

**边界标记的作用**：

系统内置了三个特殊的标记驱动，用于划分初始化范围和方便调试：

```c
/* driver.c 中的定义 */
MR_DRIVER_INSTALL(start, MR_NULL, 0, MR_NULL, MR_NULL, MR_NULL, "0.end");
MR_DRIVER_INSTALL(defer, MR_NULL, 0, MR_NULL, MR_NULL, MR_NULL, "8.end");
MR_DRIVER_INSTALL(end, MR_NULL, 0, MR_NULL, MR_NULL, MR_NULL, "9.end");
```

- **start** (`"0.end"`)：自动初始化的起点标记
- **defer** (`"8.end"`)：自动初始化的终点标记（DEFER 级别的驱动不自动初始化）
- **end** (`"9.end"`)：驱动列表的终点标记

系统初始化时，`mr_drivers_init()` 会遍历从 `start` 到 `defer` 之间的所有驱动并自动调用 `mr_driver_setup()`。

### 3.3 实现要点

#### 驱动的幂等初始化

`mr_driver_setup()` 是幂等的：

```c
mr_err_t mr_driver_setup(mr_driver_t *drv) {
    /* ... 获取锁 ... */

    /* 检查是否已设置 */
    if (mr_atomic_load(&drv->flags) & MR_DRIVER_FLAG_SETUP) {
        ret = 0;
        goto _exit;
    }

    /* 执行初始化 */
    ret = drv->clazz->init(drv);
    if (ret == 0) {
        /* 标记为已设置 */
        mr_atomic_fetch_or(&drv->flags, MR_DRIVER_FLAG_SETUP);
    }

    /* ... 释放锁 ... */
}
```

**好处**：

- 多次调用不会重复初始化
- 简化调用方的错误处理

#### 驱动查找机制

驱动通过名称查找：

```c
mr_driver_t *mr_driver_find(const char *name) {
    const mr_driver_entry_t *start = &__mr_driver_entry_start;
    const mr_driver_entry_t *end = &__mr_driver_entry_end;
    const mr_driver_entry_t *entry;

    for (entry = start + 1; entry < end; entry++) {
        if (mr_strcmp(entry->name, name) == 0) {
            return entry->driver;
        }
    }
    return MR_NULL;
}
```

**两种查找方式**：

1. **编译期查找**（`MR_DRIVER_FIND`）：直接通过符号查找，无运行时开销
2. **运行时查找**（`mr_driver_find`）：通过字符串匹配，用于动态场景

#### 驱动的自动初始化

系统启动时，`mr_drivers_init()` 自动初始化所有驱动：

```c
static void mr_drivers_init(void) {
    const mr_driver_entry_t *start = &__mr_driver_entry_start;
    const mr_driver_entry_t *end = &__mr_driver_entry_defer;
    const mr_driver_entry_t *entry;

    for (entry = start + 1; entry < end; entry++) {
        mr_driver_setup(entry->driver);
    }
}
MR_INIT_EXPORT(mr_drivers_init, MR_INIT_LEVEL_DRIVER);
```

**特点**：

- 遍历 `start` 到 `defer` 之间的所有驱动
- 自动调用 `mr_driver_setup()`
- 通过 initcall 机制自动调用

#### 驱动加载状态 (state)

`state` 字段用于记录驱动的加载状态：

```c
mr_int16_t state;  /* 0 表示成功，负值表示错误码 */
```

**状态的含义**：

| 值 | 含义 | 说明 |
|----|------|------|
| `0` | 加载成功 | 驱动已正确初始化，可以使用 |
| 负值 | 加载失败 | 存储的是错误码（如 `-MR_EINVAL`） |

**状态的使用场景**：

由于驱动的加载是自动过程，用户通常不感知加载的详细状态。通过 `state` 字段和 `mr_driver_is_ready()` 接口，用户可以：

1. **检查驱动状态**：调用 `mr_driver_is_ready()` 判断驱动是否可用
2. **排查加载问题**：如果驱动未就绪，可以通过 `mr_driver_state()` 获取错误码进行调试

**示例**：

```c
mr_driver_t *drv = MR_DRIVER_FIND(uart0);

if (!mr_driver_is_ready(drv)) {
    /* 驱动未就绪，获取错误码排查问题 */
    mr_err_t err = mr_driver_state(drv);
    printf("UART0 load failed: %d\n", err);
}
```

### 3.4 设计权衡

#### 静态驱动 vs 动态驱动

**静态驱动**（`MR_DRIVER_INSTALL`）：

- 优点：编译期完全确定，无运行时开销
- 缺点：灵活性差，需要重新编译才能修改

**动态驱动**（`mr_driver_create`）：

- 优点：灵活，运行时可以创建
- 缺点：需要内存分配，有运行时开销

**选择建议**：

- 板载硬件：使用静态驱动
- 插拔设备：使用动态驱动
- 资源受限：优先使用静态驱动

#### 驱动类型的设计

驱动类型使用 16 位整数：

```
|------- 主类型 (8位) -------|-- 子类型 (8位) --|
0x0100                      0x00FF
```

**设计考虑**：

- 主类型用于快速判断驱动类别
- 子类型用于区分同一类别下的不同设备
- 通过 `MR_DRIVER_TYPE_MASK` 提取主类型

**为什么不使用枚举？**

- 更灵活：驱动开发者可以自由定义子类型
- 更高效：位运算比字符串比较快得多
- 更紧凑：16 位整数比指针或字符串节省内存

#### 线程安全的实现

driver 模块使用原子操作而非互斥锁：

**优势**：

- 无死锁风险
- 无优先级反转
- 中断上下文安全
- 开销更小

**劣势**：

- 不能保护复杂的临界区
- 需要使用 CAS 自旋等待

**设计决策**：

对于嵌入式场景，原子操作足够且更合适。

---

## 4. 参考资料

### 4.1 相关模块

- **object**：对象系统，driver 模块基于 object 实现
- **ref**：引用计数系统，提供生命周期管理基础
- **initcall**：分级初始化机制，用于驱动的自动初始化
- **hardtree**：硬件描述系统，与 driver 模块紧密集成

### 4.2 设计参考

- **Linux Device Driver Model**：Linux 内核的设备驱动模型
- **Zephyr Device Model**：Zephyr RTOS 的设备模型
- **RT-Thread Device Model**：RT-Thread 的设备框架

### 4.3 延伸阅读

- **设备树（Device Tree）**：描述硬件配置的数据结构
- **设备驱动模型**：理解操作系统如何管理设备
- **引用计数**：理解共享资源的生命周期管理
- **原子操作**：理解无锁编程的基础
