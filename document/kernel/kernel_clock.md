[**中文**](kernel_clock.md) | [**English**](kernel_clock_en.md)

---

# 时钟管理

时钟管理模块维护系统 tick、tickless 查询钩子、系统运行时间和可选的高分辨率时钟。普通 tick 使用 `mr_tick_t`，由
`mr_clock_increase()` 推进；高分辨率 tick 由平台层接口提供；忙等延时同样委托给平台层实现。

该模块不创建硬件定时器。硬件或平台定时中断应在合适位置调用 `mr_clock_increase()`，从而推进内核时钟并触发时钟钩子。

## 核心数据结构和宏

### tick 配置

```c
#if !defined(MR_CFG_TICK_PER_SECOND)
#define MR_CFG_TICK_PER_SECOND (1000U)
#endif

typedef mr_uint32_t mr_tick_t;
#define MR_TICK_MAX MR_UINT32_MAX
```

`MR_CFG_TICK_PER_SECOND` 定义普通时钟每秒 tick 数，默认 1000。`mr_tick_t` 为 32 位无符号类型，溢出由模块内部使用粗粒度计数记录。

### tick 转换宏

```c
#define MR_TICK_MSEC(_ms) \
    ((mr_tick_t)(((mr_uint64_t)(_ms) * MR_CFG_TICK_PER_SECOND) / 1000ULL))

#define MR_TICK_USEC(_us) \
    ((mr_tick_t)(((mr_uint64_t)(_us) * MR_CFG_TICK_PER_SECOND) / 1000000ULL))
```

两个宏使用 64 位中间结果将毫秒或微秒转换为普通 tick，最终截断为 `mr_tick_t`。

### 高分辨率时钟

```c
#if defined(MR_USE_CLOCK_HIRES)
typedef mr_uint64_t mr_hires_tick_t;
#define MR_HIRES_TICK_MAX MR_UINT64_MAX

typedef struct mr_hires_timestamp {
    mr_uint16_t year;
    mr_uint8_t month;
    mr_uint8_t day;
    mr_uint8_t hour;
    mr_uint8_t minute;
    mr_uint8_t second;
    mr_uint16_t millisecond;
    mr_uint16_t microsecond;
} mr_hires_timestamp_t;
#endif
```

高分辨率时钟由 `MR_USE_CLOCK_HIRES` 控制。`MR_CFG_HIRES_TICK_PER_SECOND` 默认 1000000，且编译期限制不超过 1000000。时间戳转换以
1970-01-01 00:00:00 为起点，字段拆分到微秒。

### 时钟钩子

```c
typedef mr_tick_t(mr_clock_hook_t)(void);
```

时钟钩子返回下一次需要唤醒或处理的剩余 tick。模块内部通过 `mr_hook_t` 保存钩子数组，大小由 `MR_CFG_CLOCK_HOOK_SIZE` 控制，默认
2，且必须大于等于 2。

### 平台时钟接口

```c
extern void mr_port_clock_hires_tick_set(mr_uint64_t tick);
extern mr_uint64_t mr_port_clock_hires_tick(void);
extern void mr_port_clock_busy_wait(mr_uint32_t us);
```

平台接口位于 `include/mrX/port/mr_port_clock.h`。高分辨率 tick 设置/读取和忙等延时均通过这些接口完成。

## 主要接口

### 推进和读取普通 tick

```c
void mr_clock_increase(mr_tick_t tick);
mr_tick_t mr_clock_tick(void);
```

`mr_clock_increase()` 将内部 `__clock_tick` 增加 `tick`。如果增加后 tick 值小于增加前的值，说明 32 位 tick 发生回绕，模块会把
`__clock_uptime` 加 1。随后函数会执行时钟钩子。

`mr_clock_tick()` 返回当前 32 位 tick 值。

### tickless 查询和钩子注册

```c
mr_tick_t mr_clock_tickless(void);
mr_err_t mr_clock_hook_add(mr_clock_hook_t *entry);
```

`mr_clock_tickless()` 执行所有时钟钩子，返回所有正返回值中的最小值；如果没有钩子返回正值，则返回 `MR_TICK_MAX`。

`mr_clock_hook_add()` 将钩子加入内部 hook 表。参数为空由断言检查，容量不足时返回值取决于 `mr_hook_add()`。

### 高分辨率 tick 和时间戳

```c
void mr_clock_hires_tick_set(mr_hires_tick_t tick);
mr_hires_tick_t mr_clock_hires_tick(void);
mr_hires_timestamp_t mr_hires_tick2timestamp(mr_hires_tick_t tick,
                                             int time_zone);
```

这些接口只在 `MR_USE_CLOCK_HIRES` 启用时可用。设置和读取高分辨率 tick 直接调用平台接口。

`mr_hires_tick2timestamp()` 先按 `MR_CFG_HIRES_TICK_PER_SECOND` 校正到微秒，再应用时区偏移。`time_zone` 被限制在
`[-12, 12]`，负偏移导致的负时间会被截断到 0。随后函数按闰年规则拆分为年月日时分秒、毫秒和微秒。

### 运行时间和忙等

```c
mr_uptime_t mr_clock_uptime(void);
void mr_clock_busy_wait(mr_uint32_t us);
```

`mr_clock_uptime()` 使用双采样读取粗粒度回绕计数和当前 tick，计算从启动以来的毫秒数。换算因子为
`MR_ROUND_DIV(1000ULL, MR_CFG_TICK_PER_SECOND)`。

`mr_clock_busy_wait()` 调用 `mr_port_clock_busy_wait(us)`，不会让出 CPU。

## 使用示例

```c
static mr_tick_t timer_tickless(void) {
    return MR_TICK_MSEC(10);
}

void systick_isr(void) {
    mr_clock_increase(1);
}

void example(void) {
    mr_clock_hook_add(timer_tickless);

    if (mr_clock_tickless() <= MR_TICK_MSEC(10)) {
        /* 可以根据最近的 tickless 结果配置下一次平台定时器。 */
    }

    mr_clock_busy_wait(5);
}
```

测试 `test/kernel/clock.c` 覆盖了 tick 增加、tickless 钩子、毫秒/微秒转换、uptime、busy wait，以及在启用高分辨率时钟时的 tick
设置/读取、时间戳转换、闰年、负时区和时区边界。

## 注意事项

- `mr_clock_increase()` 会执行所有时钟钩子，钩子应保持短小且不应执行会阻塞的操作。
- `mr_clock_tick()` 返回 32 位 tick，调用者需要自行处理回绕；需要长时间运行时间时使用 `mr_clock_uptime()`。
- `mr_clock_uptime()` 当前按毫秒返回，精度受 `MR_CFG_TICK_PER_SECOND` 和取整因子影响。
- `mr_clock_busy_wait()` 是忙等，不释放 CPU。
- 高分辨率 tick 的实际单调性和精度由平台层 `mr_port_clock_*` 实现决定。
