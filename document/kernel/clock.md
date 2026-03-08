# clock - 时钟服务

## 1. 模块概述

### 1.1 功能介绍

clock 模块提供了统一的时钟管理服务，是整个系统的时间基准。它维护着系统时钟滴答（tick），支持高精度计时、系统运行时间统计、以及低功耗的
tickless 机制。

在嵌入式系统中，时钟服务是核心基础设施：

- **系统心跳**：为定时器、调度器提供时间基准
- **时间计算**：提供延时、超时等时间相关功能
- **低功耗**：通过 tickless 机制减少不必要的系统活动
- **统一抽象**：屏蔽底层硬件时钟的差异

### 1.2 主要特性

- **统一的时钟滴答**：提供标准化的系统 tick，作为时间计算基准
- **双采样算法**：避免时钟溢出时的竞态条件
- **tickless 机制**：支持低功耗，通过钩子控制睡眠时间
- **钩子扩展**：允许用户模块注册时钟回调，自定义时钟行为
- **高分辨率时钟**（可选）：支持微秒级高精度计时
- **时间戳转换**（可选）：将时钟 tick 转换为日历时间
- **运行时间统计**：记录系统实际运行时间（不受睡眠影响）

### 1.3 适用场景

clock 模块适用于以下场景：

- **定时器驱动**：为软件定时器提供时间基准
- **任务调度**：实现时间片轮转或优先级调度
- **低功耗系统**：通过 tickless 减少唤醒次数
- **性能统计**：测量代码执行时间、系统负载
- **延时控制**：精确的微秒级延时
- **日志时间戳**：为日志消息添加时间信息

---

## 2. 使用指南

### 2.1 核心数据结构

#### mr_tick_t

```c
typedef mr_uint32_t mr_tick_t;
```

**系统时钟滴答类型**，32 位无符号整数。

**特性**：

- 默认每秒 1000 个 tick（可配置 `MR_CFG_TICK_PER_SECOND`）
- 会循环溢出（约 49.7 天溢出一次）
- 不表示绝对时间，只表示相对时间

**范围**：

- 最小值：0
- 最大值：`MR_TICK_MAX` (2³² - 1)

#### mr_uptime_t

```c
typedef mr_uint64_t mr_uptime_t;
```

**系统运行时间类型**，64 位无符号整数。

**特性**：

- 记录系统实际运行时间（不包括睡眠时间）
- 不会溢出（足以运行数千年）
- 单位：毫秒（取决于 `MR_CFG_TICK_PER_SECOND`）

#### mr_clock_hook_t

```c
typedef mr_tick_t(mr_clock_hook_t)(void);
```

**时钟钩子函数类型**。

**返回值**：

- 返回"还需要的 tick 数量"
- 返回 0 表示不需要等待

**用途**：

- tickless 机制中，系统可以根据这个返回值决定实际睡眠时间

### 2.2 配置选项

#### MR_CFG_TICK_PER_SECOND

```c
#define MR_CFG_TICK_PER_SECOND (1000U)
```

**每秒时钟滴答数**，默认 1000（即 1 tick = 1 ms）。

**可调整范围**：

- 较低的值（如 100）：精度更低，但开销更小
- 较高的值（如 10000）：精度更高，但开销更大

#### MR_CFG_CLOCK_HOOK_SIZE

```c
#define MR_CFG_CLOCK_HOOK_SIZE (2)
```

**时钟钩子数量**，默认 2 个。

**用途**：

- 定义系统支持的时钟钩子数量
- 可根据实际需求调整

### 2.3 主要接口说明

#### 时间单位转换宏

##### MR_TICK_MSEC()

```c
#define MR_TICK_MSEC(_ms) ((_ms) * 1000UL / MR_CFG_TICK_PER_SECOND)
```

**功能**：将毫秒转换为时钟 tick

**参数**：

- `_ms`：毫秒数

**示例**：

```c
mr_tick_t ticks = MR_TICK_MSEC(500);  /* 500 毫秒对应的 tick 数 */
```

##### MR_TICK_USEC()

```c
#define MR_TICK_USEC(_us) ((_us) * 1000000UL / MR_CFG_TICK_PER_SECOND)
```

**功能**：将微秒转换为时钟 tick

**参数**：

- `_us`：微秒数

**示例**：

```c
mr_tick_t ticks = MR_TICK_USEC(1500);  /* 1500 微秒对应的 tick 数 */
```

#### 时钟操作接口

##### mr_clock_increase()

```c
void mr_clock_increase(mr_tick_t tick);
```

**功能**：增加时钟滴答

**参数**：

- `tick`：要增加的 tick 数量

**说明**：

- 通常由底层的时钟中断调用
- 会触发所有已注册的钩子函数
- 如果滴答溢出，会自动增加运行时间计数器

**注意事项**：

- **不要在中断上下文外手动调用**，除非你是做测试或模拟

##### mr_clock_tick()

```c
mr_tick_t mr_clock_tick(void);
```

**功能**：获取当前时钟滴答

**返回值**：当前 tick 值

**示例**：

```c
mr_tick_t tick1 = mr_clock_tick();
/* ... 一些操作 ... */
mr_tick_t tick2 = mr_clock_tick();
mr_tick_t elapsed = tick2 - tick1;  /* 经过的 tick 数 */
```

##### mr_clock_tickless()

```c
mr_tick_t mr_clock_tickless(void);
```

**功能**：获取 tickless 滴答

**返回值**：考虑所有钩子后，"还需要等待的最小 tick 数"

**工作原理**：

1. 遍历所有已注册的钩子
2. 每个钩子返回"还需要等待的 tick 数"
3. 返回所有返回值中的最小值

**用途**：

- 低功耗场景：系统可以根据返回值决定实际睡眠时间
- 调度器：确定下一次任务唤醒时间

**示例**：

```c
mr_tick_t next_tick = mr_clock_tickless();
/* 系统可以安全地睡眠 next_tick 个 tick */
```

##### mr_clock_uptime()

```c
mr_uptime_t mr_clock_uptime(void);
```

**功能**：获取系统运行时间（毫秒）

**返回值**：系统实际运行时间

**特性**：

- 使用双采样算法，避免溢出跳变时的竞态条件
- 不包括系统睡眠时间

**示例**：

```c
mr_uptime_t uptime1 = mr_clock_uptime();
task_run();
mr_uptime_t uptime2 = mr_clock_uptime();
printf("Task took %llu ms\n", uptime2 - uptime1);
```

##### mr_clock_busy_wait()

```c
void mr_clock_busy_wait(mr_uint32_t us);
```

**功能**：忙等待指定的微秒数（不让出 CPU）

**参数**：

- `us`：等待的微秒数

**示例**：

```c
/* 等待 100 微秒 */
mr_clock_busy_wait(100);
```

**注意事项**：

- 这是忙等待，会持续占用 CPU
- 仅用于非常短的延时
- 长时间延时应该使用睡眠机制

#### 钩子管理接口

##### mr_clock_hook_add()

```c
mr_err_t mr_clock_hook_add(mr_clock_hook_t *entry);
```

**功能**：添加时钟钩子

**参数**：

- `entry`：钩子函数指针

**返回值**：

- `0`：成功
- 负数：失败（如钩子槽已满）

**示例**：

```c
static mr_tick_t my_hook(void) {
    /* 返回下一次需要唤醒的 tick 数 */
    return 100;  /* 100 ticks 后再次唤醒 */
}

mr_clock_hook_add(my_hook);
```

### 2.4 高分辨率时钟（可选功能）

当定义了 `MR_USE_CLOCK_HIRES` 时，以下接口可用：

#### mr_hires_tick_t

```c
typedef mr_uint64_t mr_hires_tick_t;
```

**高分辨率时钟滴答类型**，64 位无符号整数。

**特性**：

- 默认每秒 1,000,000 个 tick（可配置 `MR_CFG_HIRES_TICK_PER_SECOND`）
- 即 1 tick = 1 微秒
- 提供微秒级精度

#### mr_hires_timestamp_t

```c
typedef struct mr_hires_timestamp {
    mr_uint16_t year;
    mr_uint8_t  month;
    mr_uint8_t  day;
    mr_uint8_t  hour;
    mr_uint8_t  minute;
    mr_uint8_t  second;
    mr_uint16_t millisecond;
    mr_uint16_t microsecond;
} mr_hires_timestamp_t;
```

**时间戳结构**，包含年、月、日、时、分、秒、毫秒、微秒。

##### mr_clock_hires_tick_set()

```c
void mr_clock_hires_tick_set(mr_hires_tick_t tick);
```

**功能**：设置高分辨率时钟滴答

**参数**：

- `tick`：要设置的 tick 值

**说明**：

- 通常由底层硬件驱动调用
- 用户一般不需要手动调用

##### mr_clock_hires_tick()

```c
mr_hires_tick_t mr_clock_hires_tick(void);
```

**功能**：获取高分辨率时钟滴答

**返回值**：当前高分辨率 tick 值

**示例**：

```c
mr_hires_tick_t t1 = mr_clock_hires_tick();
/* 操作... */
mr_hires_tick_t t2 = mr_clock_hires_tick();
printf("Elapsed: %llu us\n", t2 - t1);
```

##### mr_hires_tick2timestamp()

```c
mr_hires_timestamp_t mr_hires_tick2timestamp(mr_hires_tick_t tick, int time_zone);
```

**功能**：将高分辨率滴答转换为日历时间戳

**参数**：

- `tick`：高分辨率滴答
- `time_zone`：时区（UTC+n，如 8 表示 UTC+8）

**返回值**：时间戳结构

**限制**：

- 不考虑闰秒
- 时区范围：-12 到 +12
- 年份从 1970 开始

**示例**：

```c
mr_hires_tick_t tick = 1725580800000000ULL;  /* 某个时间点 */
mr_hires_timestamp_t ts = mr_hires_tick2timestamp(tick, 8);  /* UTC+8 */
printf("Time: %04u-%02u-%02u %02u:%02u:%02u.%03u\n",
       ts.year, ts.month, ts.day,
       ts.hour, ts.minute, ts.second, ts.millisecond);
```

### 2.5 典型应用案例

#### 案例 1：基础用法 - 时钟初始化和获取

```c
#include <mrX/kernel/mr_clock.h>
#include <stdio.h>

int main(void) {
    mr_tick_t tick1, tick2;

    /* 获取当前时钟滴答 */
    tick1 = mr_clock_tick();
    printf("Current tick: %u\n", tick1);

    /* 模拟一些操作 */
    for (volatile int i = 0; i < 1000; i++);

    /* 再次获取时钟滴答 */
    tick2 = mr_clock_tick();
    printf("Elapsed ticks: %u\n", tick2 - tick1);

    /* 计算经过的时间（毫秒） */
    uint32_t elapsed_ms = (tick2 - tick1) * 1000 / MR_CFG_TICK_PER_SECOND;
    printf("Elapsed time: %u ms\n", elapsed_ms);

    return 0;
}
```

#### 案例 2：tickless 机制 - 低功耗优化

```c
#include <mrX/kernel/mr_clock.h>
#include <stdio.h>

/* 定义两个任务的钩子 */
static mr_tick_t task1_hook(void) {
    /* 任务1 需要 100 ticks 后再次运行 */
    return 100;
}

static mr_tick_t task2_hook(void) {
    /* 任务2 需要 200 ticks 后再次运行 */
    return 200;
}

void init_low_power_system(void) {
    /* 注册钩子 */
    mr_clock_hook_add(task1_hook);
    mr_clock_hook_add(task2_hook);
}

void low_power_main_loop(void) {
    while (1) {
        /* 获取 tickless tick */
        mr_tick_t next_tick = mr_clock_tickless();

        if (next_tick > 0) {
            /* 有任务需要等待 */
            printf("Sleeping for %u ticks...\n", next_tick);

            /* 进入低功耗模式，会自动在 next_tick ticks 后唤醒 */
            enter_low_power_mode(next_tick);
        } else {
            /* 没有任务需要等待 */
            printf("No tasks, deep sleep...\n");
            enter_deep_sleep();
        }

        /* 处理任务... */
    }
}
```

**说明**：

- `mr_clock_tickless()` 会考虑所有钩子的返回值
- 系统可以根据返回值决定实际睡眠时间
- 这样可以避免不必要的唤醒，节省功耗

#### 案例 3：精确延时

```c
#include <mrX/kernel/mr_clock.h>
#include <mrX/libc/mr_printf.h>

void precise_delay_ms(uint32_t ms) {
    mr_tick_t start = mr_clock_tick();
    mr_tick_t target_ticks = MR_TICK_MSEC(ms);

    while ((mr_clock_tick() - start) < target_ticks) {
        /* 忙等待 */
    }
}

/* 使用示例 */
void example_delay(void) {
    printf("Delaying for 100 ms...\n");
    precise_delay_ms(100);
    printf("Done!\n");
}
```

#### 案例 4：测量代码执行时间

```c
#include <mrX/kernel/mr_clock.h>
#include <stdio.h>

void measure_function_performance(void (*func)(void)) {
    mr_tick_t start, end;

    start = mr_clock_tick();
    func();
    end = mr_clock_tick();

    uint32_t elapsed_ticks = end - start;
    uint32_t elapsed_ms = elapsed_ticks * 1000 / MR_CFG_TICK_PER_SECOND;

    printf("Function took %u ticks (%u ms)\n", elapsed_ticks, elapsed_ms);
}

void example_function(void) {
    volatile int sum = 0;
    for (int i = 0; i < 10000; i++) {
        sum += i;
    }
}

int main(void) {
    measure_function_performance(example_function);
    return 0;
}
```

#### 案例 5：系统运行时间统计

```c
#include <mrX/kernel/mr_clock.h>
#include <stdio.h>

void example_uptime(void) {
    mr_uptime_t start, end;

    printf("Program started\n");
    start = mr_clock_uptime();

    /* 模拟运行 2 秒 */
    mr_clock_busy_wait(2000000);  /* 忙等待 2 秒 */

    end = mr_clock_uptime();
    printf("Uptime: %llu ms\n", end - start);

    printf("Program paused\n");
    /* 系统睡眠 10 秒（不计入运行时间） */
    system_sleep(10000);

    end = mr_clock_uptime();
    printf("Uptime after sleep: %llu ms\n", end - start);
}
```

#### 案例 6：高分辨率时间测量

```c
#if defined(MR_USE_CLOCK_HIRES)
#include <mrX/kernel/mr_clock.h>
#include <stdio.h>

void high_resolution_measurement(void) {
    mr_hires_tick_t start, end;

    /* 测量某个操作的耗时 */
    start = mr_clock_hires_tick();

    /* 执行操作 */
    volatile int result = perform_operation();

    end = mr_clock_hires_tick();

    printf("Operation took %llu us\n", end - start);
}
#endif
```

---

## 3. 设计思路

### 3.1 设计目标

clock 模块的设计目标：

1. **统一时间基准**：为整个系统提供一致的时间服务
2. **低功耗支持**：通过 tickless 机制减少不必要的系统活动
3. **可扩展性**：通过钩子机制允许用户自定义时钟行为
4. **高精度计时**：可选支持微秒级高精度计时
5. **安全可靠**：处理时钟溢出等边界情况

### 3.2 双计数器设计

clock 模块使用两个计数器：

1. **细粒度计数器（`__clock_tick`）**：
    - 每次 `mr_clock_increase` 调用都会增加
    - 记录实际的系统 tick
    - 32 位，约 49.7 天溢出一次

2. **粗粒度计数器（__clock_uptime）**：
    - 只在细粒度计数器溢出时增加
    - 记录"完整的时间单位"
    - 无符号 64 位，不会溢出

**设计理由**：

- 细粒度计数器提供高精度
- 粗粒度计数器提供长期的时间基准
- 通过组合两个计数器，既保证精度，又避免溢出

**计算公式**：

```
uptime = (粗粒度计数 × 2³² + 细粒度计数) × 时间转换系数
```

### 3.3 双采样算法

`mr_clock_uptime()` 使用双采样算法避免竞态条件：

**问题场景**：

```
时刻 T0：粗粒度计数 = 100，细粒度计数 = 2³²-2（快溢出）
时刻 T1：读取细粒度计数 = 2³²-2
时刻 T2：粗粒度计数溢出变为 101
时刻 T3：读取细粒度计数 = 0（已回绕）

如果直接计算：(101 × 2³² + 2³²-2) vs (101 × 2³² + 0)
结果不一致！
```

**双采样算法**：

```c
do {
    thin = mr_atomic_load(&__clock_tick);
    coarse2 = mr_atomic_load(&__clock_uptime);

    if (coarse2 != coarse) {
        /* 粗粒度计数已更新，重试 */
        coarse = coarse2;
        continue;
    }

    /* 粗粒度计数稳定，可以使用 */
    break;
} while (1);
```

**工作原理**：

1. 读取细粒度 tick
2. 读取粗粒度 uptime
3. 再次读取粗粒度 uptime
4. 如果两次读取的粗粒度值不同，说明发生了更新，重试
5. 如果相同，说明粗粒度计数稳定，可以使用

### 3.4 tickless 机制

tickless 机制用于优化功耗：

**传统方式**：

- 系统每隔固定时间（如 1ms）被时钟中断唤醒
- 即使没有任务需要执行，也会消耗 CPU 时间

**tickless 方式**：

- 通过钩子询问各个模块"还需要多少 tick？"
- 系统根据最小返回值决定睡眠时间
- 如果没有任务需要执行，可以睡眠更长时间

**钩子返回值的含义**：

- 返回 0：立即需要处理
- 返回 N：可以等待 N 个 tick 后再处理

**实现**：

```c
static mr_tick_t clock_hook(void) {
    mr_tick_t tickless = MR_TICK_MAX;

    /* 遍历所有钩子 */
    MR_HOOK_EACH(i, entry, mr_clock_hook_t, &__clock_hook) {
        less = entry();  /* 调用钩子获取返回值 */
        tickless = MIN(tickless, less);  /* 取最小值 */
    }

    return tickless;
}
```

### 3.5 钩子系统的设计

**为什么需要钩子系统？**

1. **解耦**：时钟模块不需要知道具体有哪些任务
2. **灵活**：用户可以自由注册/注销钩子
3. **高效**：直接函数调用，无查找开销

**典型应用场景**：

- **定时器管理**：定时器钩子返回下一次到期的 tick 数
- **任务调度**：调度器钩子返回下一个任务切换的 tick 数
- **看门狗**：喂狗钩子确保系统正常运行

### 3.6 时间转换设计

`mr_hires_tick2timestamp()` 的设计考虑：

1. **简化实现**：不考虑闰秒，避免复杂性
2. **时区处理**：支持 -12 到 +12 的标准时区
3. **基准时间**：从 1970-01-01 00:00:00 UTC 开始
4. **精度权衡**：毫秒和微秒级别的精度已经满足大多数需求

---

## 4. 使用注意事项

### 4.1 时钟溢出处理

**不要假设 tick 单调递增**：

```c
/* 错误的写法 */
mr_tick_t t1 = mr_clock_tick();
/* ... */
mr_tick_t t2 = mr_clock_tick();
if (t2 > t1) {
    /* 有溢出风险！*/
}
```

**正确的写法**：

```c
/* 计算差值，正确处理溢出 */
mr_tick_t elapsed = t2 - t1;  /* 即使溢出，计算结果也是正确的 */
```

### 4.2 时钟精度配置

`MR_CFG_TICK_PER_SECOND` 的选择建议：

| 应用场景  | 推荐值     | 理由                     |
|-------|---------|------------------------|
| 通用系统  | 1000    | 1 ms 精度，性能开销适中         |
| 实时系统  | 10000   | 100 µs 精度，但中断开销较高      |
| 低功耗系统 | 100     | 10 ms 精度，减少中断和唤醒次数     |
| 高性能计算 | 1000000 | 1 µs 精度，测量短耗时操作，中断开销很高 |

### 4.3 钩子函数编写规范

**钩子函数的要求**：

1. **快速执行**：钩子会被频繁调用，必须快速返回
2. **无阻塞**：不能睡眠或阻塞
3. **线程安全**：可能会在中断上下文调用

**示例**：

```c
/* ✓ 正确：快速、无阻塞 */
static mr_tick_t my_timer_hook(void) {
    return next_expiration_tick();
}

/* ✗ 错误：阻塞等待 */
static mr_tick_t bad_hook(void) {
    while (!event_ready()) {
        /* 错误！不要阻塞 */
    }
    return 0;
}
```

---

## 5. 参考资料

### 5.1 设计参考

- **Linux tickless kernel**：Linux 的动态时钟机制
- **FreeBSD tc_ticktock**：FreeBSD 的 tickless 时钟
- **Zephyr tickless kernel**：Zephyr RTOS 的低功耗时钟

### 5.2 延伸阅读

- **双采样算法**：理解多核环境下的竞态条件
- **低功耗设计**：tickless kernel 的功耗优化原理
- **高精度计时**：硬件定时器、TSC、HPET 等高精度计时源
