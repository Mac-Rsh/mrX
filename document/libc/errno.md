# errno - 错误码

## 1. 模块概述

### 1.1 功能介绍

errno 模块定义了 mrX 框架使用的标准错误码系统。它提供了一套统一的错误码定义，使得框架内所有模块可以使用一致的错误报告机制。

该模块支持三种配置模式：STD 模式（使用标准 errno.h）、3RD 模式（使用第三方库）和自定义模式，确保在不同环境下的兼容性。

### 1.2 主要特性

- **标准错误码**：定义了常用的 POSIX 风格错误码
- **多模式支持**：STD、3RD、自定义三种模式
- **统一接口**：所有模块使用相同的错误码
- **类型安全**：使用 `mr_err_t` 类型表示错误码

### 1.3 适用场景

errno 模块适用于以下场景：

- **API 返回值**：函数返回错误码表示操作结果
- **错误传播**：在调用栈中传播错误信息
- **错误处理**：统一处理各种错误情况

---

## 2. 使用指南

### 2.1 错误码说明

#### 错误类型

```c
typedef int                             mr_err_t;
```

`mr_err_t` 是错误码的基础类型，本质上是有符号整数。

**约定**：

- 返回 `0` 表示成功
- 返回负值表示错误（如 `-MR_EINVAL`）
- 正值通常不使用，保留给特殊用途

#### 标准错误码

| 错误码 | 值 | 含义 | 使用场景 |
|--------|-----|------|---------|
| `MR_EPERM` | 1 | 操作不允许 | 权限不足 |
| `MR_ENOENT` | 2 | 条目不存在 | 文件/设备未找到 |
| `MR_EIO` | 5 | I/O 错误 | 硬件通信失败 |
| `MR_EAGAIN` | 11 | 再试一次 | 资源暂时不可用 |
| `MR_ENOMEM` | 12 | 内存不足 | 内存分配失败 |
| `MR_EBUSY` | 16 | 资源忙 | 设备已被占用 |
| `MR_EEXIST` | 17 | 已存在 | 重复创建 |
| `MR_EINVAL` | 22 | 无效参数 | 参数错误 |
| `MR_ENOSYS` | 40 | 功能未实现 | 功能未支持 |
| `MR_ETIMEDOUT` | 116 | 操作超时 | 等待超时 |

### 2.2 配置模式

#### STD 模式（标准库模式）

```c
#define MR_USE_ERRNO_STD
```

**特点**：

- 直接使用标准库的 `<errno.h>`
- 错误码映射到标准 errno 值
- 适合有标准库支持的平台

**定义**：

```c
#if defined(MR_USE_ERRNO_STD)
#define MR_EPERM                        EPERM
#define MR_ENOENT                       ENOENT
#define MR_EIO                          EIO
#define MR_EAGAIN                       EAGAIN
#define MR_ENOMEM                       ENOMEM
#define MR_EBUSY                        EBUSY
#define MR_EEXIST                       EEXIST
#define MR_EINVAL                       EINVAL
#define MR_ENOSYS                       ENOSYS
#define MR_ETIMEDOUT                    ETIMEDOUT
#endif
```

#### 3RD 模式（第三方库模式）

```c
#define MR_USE_ERRNO_3RD
```

**特点**：

- 使用第三方库提供的错误码定义
- 适配特定的 RTOS 或库
- 错误码定义在 `mrX/3party/libc/mr_errno.h`

**定义**：

```c
#elif defined(MR_USE_ERRNO_3RD)
/* In 'mrX/3party/libc/mr_errno.h' */
#endif
```

#### 自定义模式

**特点**：

- 使用框架自定义的错误码值
- 与 POSIX errno 兼容的值定义
- 不依赖任何外部库

**定义**：

```c
#else
#define MR_EPERM                        (1)
#define MR_ENOENT                       (2)
#define MR_EIO                          (5)
#define MR_EAGAIN                       (11)
#define MR_ENOMEM                       (12)
#define MR_EBUSY                        (16)
#define MR_EEXIST                       (17)
#define MR_EINVAL                       (22)
#define MR_ENOSYS                       (40)
#define MR_ETIMEDOUT                    (116)
#endif
```

### 2.3 典型应用案例

#### 案例 1：函数返回错误码

```c
#include <mrX/libc/mr_errno.h>

/* 成功返回 0，失败返回负错误码 */
mr_err_t device_init(mr_uint32_t device_id) {
    /* 检查设备 ID 是否有效 */
    if (device_id >= MAX_DEVICE_COUNT) {
        return -MR_EINVAL;  /* 无效参数 */
    }

    /* 检查设备是否已初始化 */
    if (devices[device_id].initialized) {
        return -MR_EEXIST;  /* 已存在 */
    }

    /* 分配内存 */
    void *buffer = mr_malloc(1024);
    if (buffer == MR_NULL) {
        return -MR_ENOMEM;  /* 内存不足 */
    }

    /* 初始化设备 */
    devices[device_id].buffer = buffer;
    devices[device_id].initialized = MR_TRUE;

    return 0;  /* 成功 */
}

/* 调用方处理错误 */
mr_err_t ret = device_init(5);
if (ret != 0) {
    printf("Device init failed: %d\n", ret);
    /* 处理错误 */
}
```

#### 案例 2：错误传播

```c
#include <mrX/libc/mr_errno.h>

/* 底层驱动函数 */
static mr_err_t spi_transfer(const void *tx_buf, void *rx_buf, mr_size_t len) {
    if (spi_is_busy()) {
        return -MR_EBUSY;  /* SPI 总线忙 */
    }
    /* 执行传输... */
    return 0;
}

/* 中间层函数 */
mr_err_t sensor_read(mr_uint8_t reg, mr_uint8_t *value) {
    mr_err_t ret;

    /* 发送寄存器地址 */
    ret = spi_transfer(&reg, MR_NULL, 1);
    if (ret != 0) {
        return ret;  /* 直接传播错误 */
    }

    /* 读取数据 */
    ret = spi_transfer(MR_NULL, value, 1);
    if (ret != 0) {
        return ret;  /* 直接传播错误 */
    }

    return 0;
}

/* 高层函数 */
mr_err_t get_temperature(mr_int32_t *temperature) {
    mr_uint8_t value;
    mr_err_t ret;

    ret = sensor_read(TEMP_REG, &value);
    if (ret != 0) {
        return ret;  /* 传播错误 */
    }

    *temperature = (mr_int32_t)value;
    return 0;
}
```

#### 案例 3：超时处理

```c
#include <mrX/libc/mr_errno.h>
#include <mrX/kernel/mr_clock.h>

mr_err_t wait_for_data(mr_uint8_t *data, mr_uint32_t timeout_ms) {
    mr_uint32_t start = mr_clock_get_ms();

    while ((mr_clock_get_ms() - start) < timeout_ms) {
        if (data_available()) {
            *data = read_data();
            return 0;  /* 成功 */
        }
    }

    return -MR_ETIMEDOUT;  /* 超时 */
}
```

#### 案例 4：组合错误处理

```c
#include <mrX/libc/mr_errno.h>

mr_err_t open_device(const char *name, device_t **device_out) {
    device_t *dev;
    mr_err_t ret;

    /* 参数检查 */
    if (name == MR_NULL || device_out == MR_NULL) {
        return -MR_EINVAL;
    }

    /* 查找设备 */
    dev = find_device(name);
    if (dev == MR_NULL) {
        return -MR_ENOENT;
    }

    /* 检查是否已打开 */
    if (dev->open_count > 0) {
        return -MR_EBUSY;
    }

    /* 打开设备 */
    ret = dev->ops->open(dev);
    if (ret != 0) {
        return ret;  /* 返回具体的错误 */
    }

    dev->open_count++;
    *device_out = dev;
    return 0;
}
```

#### 案例 5：错误码转换

```c
#include <mrX/libc/mr_errno.h>

/* 将框架错误码转换为字符串描述 */
const char *mr_strerror(mr_err_t err) {
    switch (err) {
        case 0:
            return "Success";
        case -MR_EPERM:
            return "Operation not permitted";
        case -MR_ENOENT:
            return "No such file or directory";
        case -MR_EIO:
            return "I/O error";
        case -MR_EAGAIN:
            return "Resource temporarily unavailable";
        case -MR_ENOMEM:
            return "Out of memory";
        case -MR_EBUSY:
            return "Device or resource busy";
        case -MR_EEXIST:
            return "File exists";
        case -MR_EINVAL:
            return "Invalid argument";
        case -MR_ENOSYS:
            return "Function not implemented";
        case -MR_ETIMEDOUT:
            return "Connection timed out";
        default:
            return "Unknown error";
    }
}

/* 使用示例 */
mr_err_t ret = device_init(0);
if (ret != 0) {
    printf("Error: %s\n", mr_strerror(ret));
}
```

---

## 3. 设计思路

### 3.1 设计目标

errno 模块的设计目标：

1. **标准化**：使用 POSIX 风格的错误码，易于理解
2. **兼容性**：支持多种配置模式
3. **一致性**：整个框架使用统一的错误码
4. **简洁性**：只定义常用的错误码

### 3.2 错误码设计

#### 为什么返回负值？

1. **区分成功和失败**：
   - `0` 表示成功
   - 负值表示错误
   - 正值可以用于返回有效数据

2. **保持原错误码**：
   - `-MR_EINVAL` 比 `MR_EINVAL` 更清晰
   - 与 Linux 内核风格一致

3. **便于检查**：
   ```c
   if (ret < 0) {
       /* 发生错误 */
   }
   ```

#### 错误码值的来源

错误码值采用 POSIX 标准的定义：

```c
/* 在 Linux/Unix 系统中，这些值定义在 errno.h 或 asm/errno.h */
#define EPERM        1   /* Operation not permitted */
#define ENOENT       2   /* No such file or directory */
#define EIO          5   /* I/O error */
/* ... */
```

这样做的好处：

1. **可移植性**：标准化的错误码
2. **可读性**：开发者熟悉这些错误码
3. **互操作性**：易于与其他代码集成

### 3.3 实现原理

#### 宏定义的实现

```c
typedef int                             mr_err_t;

#if defined(MR_USE_ERRNO_STD)
    /* 映射到标准 errno */
    #define MR_EINVAL    EINVAL
#elif defined(MR_USE_ERRNO_3RD)
    /* 使用第三方库定义 */
#else
    /* 自定义值 */
    #define MR_EINVAL    (22)
#endif
```

#### 使用约定

```c
/* 函数定义 */
mr_err_t my_function(void) {
    /* 成功返回 0 */
    return 0;

    /* 失败返回负错误码 */
    return -MR_EINVAL;
}

/* 调用方处理 */
mr_err_t ret = my_function();
if (ret != 0) {
    /* 处理错误 */
    printf("Error: -MR_ENOMEM = %d, ret = %d\n", -MR_ENOMEM, ret);
}
```

---

## 4. 参考资料

### 4.1 相关模块

errno 模块被几乎所有需要错误处理的模块使用：

- **malloc**：内存分配失败返回 `-MR_ENOMEM`
- **driver**：驱动操作失败返回相应错误码
- **irq**：中断请求失败返回 `-MR_EBUSY`
- **timer**：定时器操作可能返回 `-MR_ETIMEDOUT`

### 4.2 相关标准

- **POSIX**：定义了标准 errno 值及其含义
- **ISO C**：标准 C 库的 errno 机制
- **Linux Kernel**：内核的错误码返回约定

### 4.3 延伸阅读

- **错误处理最佳实践**：如何设计良好的错误处理机制
- **errno 的历史**：了解 errno 的起源和发展
- **错误码设计**：如何设计合理的错误码系统
