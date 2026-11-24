# 时钟（Clock）

提供全局系统节拍（tick）计数、64 位开机时长、每 tick
钩子分发与高分辨率时间扩展，是[软定时器](timer.md)、延时工作等时间服务的基础时钟源。

| 项目   | 说明                                                                   |
|--------|------------------------------------------------------------------------|
| 功能宏 | `MR_USE_CLOCK_HIRES`（仅门控高分辨率扩展，默认开启；核心时钟始终编译） |
| 头文件 | `<mrX/kernel/mr_clock.h>`                                              |
| 依赖   | [钩子（Hook）](hook.md)                                                |
| 测试   | `test/kernel/clock.c`、`test/kernel/clock_deep.c`                      |

## 配置

`<mrX/kernel/mr_clock.h>` 与 `kernel/clock.c` 为未定义的配置宏提供了与 Kconfig
相同的默认值，未启用 Kconfig 也能直接编译。

| 配置项                         | 类型 | 默认值  | 说明                                                 |
|--------------------------------|------|---------|------------------------------------------------------|
| `MR_CFG_CLOCK_HOOK_SIZE`       | int  | 2       | 时钟钩子表容量（范围 2~128，小于 2 编译报错）        |
| `MR_CFG_TICK_PER_SECOND`       | int  | 1000    | 系统节拍频率（Hz）                                   |
| `MR_USE_CLOCK_HIRES`           | bool | y       | 启用高分辨率时钟扩展                                 |
| `MR_CFG_HIRES_TICK_PER_SECOND` | int  | 1000000 | 高分辨率 tick 频率（Hz，上限 1000000，超出编译报错） |

## 快速上手

```c
#include <mrX/kernel/mr_clock.h>

static mr_tick_t my_hook(void) {
    return MR_TICK_MAX; /* MR_TICK_MAX 表示暂无近期关注点 */
}

void clock_demo(void) {
    mr_clock_hook_add(my_hook);

    mr_uptime_t t0 = mr_clock_uptime();
    mr_clock_busy_wait(100);            /* 忙等 100us，不让出 CPU */
    mr_uptime_t cost_ms = mr_clock_uptime() - t0;

    mr_tick_t now = mr_clock_tick();    /* 32 位 tick，仅用于差值比较 */
    (void)cost_ms;
    (void)now;
}
```

## 核心机制与约束

- **tick 与 uptime**：`mr_clock_tick()` 返回 32
  位无符号计数，长期运行会回绕，不可直接比较绝对值大小；需要单调时间差时应使用 64
  位毫秒数 `mr_clock_uptime()`。
- **钩子执行时机**：`mr_clock_increase(tick)` 在推进计数后执行一次全部已注册钩子（在调用者上下文同步执行，通常为
  tick ISR）。即使传入 `tick > 1` 进行休眠补偿，钩子也只执行一次，不会逐 tick 重放。
- **tickless 汇总**：每个钩子返回“距下次需要被调度的剩余 tick 数”（`0` 和
  `MR_TICK_MAX` 均不参与最小值竞争），`mr_clock_tickless()`
  会重新执行整条钩子链并返回最小正值；若无有效返回值则返回 `MR_TICK_MAX`。
- **并发与可重入**：`mr_clock_tick()` 与 `mr_clock_uptime()` 无锁且可在任意上下文调用。
  `mr_hook_add()` 仅保护注册过程，遍历钩子链时不持锁，建议在初始化阶段完成注册；若
  `mr_clock_tickless()` 与 `mr_clock_increase()` 并发调用，钩子函数需自行保证可重入。
- **高分辨率时间与时区**：`mr_clock_hires_tick()` 的零点基准由平台移植层决定。
  `mr_hires_tick2timestamp()` 固定将输入解释为自 `1970-01-01 00:00:00 UTC`
  起的经过时间，并将 `time_zone` 钳位至 `[-12, 12]`（结果小于 0 时截断为 0）。
- **整数换算截断**：`MR_TICK_MSEC`、`MR_TICK_USEC` 等宏使用整数除法，当
  `MR_CFG_TICK_PER_SECOND < 1000` 时 `MR_TICK_MSEC(1)` 会截断为 `0`。

## API 速查

### 时间读取与推进

| API                                       | 说明                                                          |
|-------------------------------------------|---------------------------------------------------------------|
| `void mr_clock_increase(mr_tick_t tick)`  | 推进 `tick` 个节拍并触发一次钩子链（由移植层 tick 源调用）    |
| `mr_tick_t mr_clock_tick(void)`           | 读取当前 32 位 tick                                           |
| `mr_uptime_t mr_clock_uptime(void)`       | 读取 64 位开机时长（毫秒）                                    |
| `mr_tick_t mr_clock_tickless(void)`       | 重新执行钩子链，返回最小正剩余 tick（无则返回 `MR_TICK_MAX`） |
| `void mr_clock_busy_wait(mr_uint32_t us)` | 忙等指定微秒数，不让出 CPU（直通 `mr_port_clock_busy_wait`）  |

### 钩子与换算宏

| API                                                  | 说明                                                           |
|------------------------------------------------------|----------------------------------------------------------------|
| `mr_err_t mr_clock_hook_add(mr_clock_hook_t *entry)` | 注册 tick 钩子（`mr_tick_t (*)(void)`）；表满返回 `-MR_ENOMEM` |
| `MR_TICK_MSEC(_ms)` / `MR_TICK_USEC(_us)`            | 毫秒 / 微秒转 tick                                             |
| `MR_MSEC_TICK(_tick)` / `MR_USEC_TICK(_tick)`        | tick 转毫秒 / 微秒                                             |

### 高分辨率扩展（`MR_USE_CLOCK_HIRES`）

| API                                                                                 | 说明                                                |
|-------------------------------------------------------------------------------------|-----------------------------------------------------|
| `void mr_clock_hires_tick_set(mr_hires_tick_t tick)`                                | 设置高分辨率 tick（供移植层初始化基准）             |
| `mr_hires_tick_t mr_clock_hires_tick(void)`                                         | 读取当前高分辨率 tick                               |
| `mr_hires_timestamp_t mr_hires_tick2timestamp(mr_hires_tick_t tick, int time_zone)` | 将高分辨率 tick 转换为 `UTC + time_zone` 日历时间戳 |

## 内部实现

核心实现位于 `kernel/clock.c`：

```text
mr_clock_increase(tick)
  ├─ __clock_gen++ (奇数: 写进行中)
  ├─ __clock_tick += tick (若回绕则 __clock_uptime++)
  ├─ __clock_gen++ (偶数: 写完成)
  └─ clock_hook() 遍历 __clock_hook 汇总最小正返回值
```

- **无锁一致性快照**：`mr_clock_uptime()` 采用 seqlock 机制，通过原子代数计数器
  `__clock_gen` 确保读取到的高位回绕次数 `__clock_uptime` 与低位 `__clock_tick`
  属于同一时刻，避免 32 位回绕边界读出撕裂值。
- **防溢出缩放**：`clock_scale_floor()` 在换算毫秒和微秒时先拆分商与余数再分别乘除，避免
  `tick * scale` 溢出 `uint64_t`，超出范围时饱和返回 `MR_UINT64_MAX`。
