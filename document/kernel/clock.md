# 时钟（Clock）

> 时钟模块提供全局系统节拍（tick）计数、开机时长、每 tick 钩子分发与高分辨率
> 时间源，是内核一切时间相关服务（[软定时器](timer.md)、延时工作等）的时间基准。

| 项       | 说明                                                                        |
|----------|-----------------------------------------------------------------------------|
| 功能宏   | `MR_USE_CLOCK_HIRES`（仅门控高分辨率扩展，默认开启；核心时钟始终编译）      |
| 头文件   | `<mrX/kernel/mr_clock.h>`                                                   |
| 依赖模块 | [钩子 Hook](hook.md)（tick 钩子链）、移植层（高分辨率 tick 源与 busy-wait） |
| 参考测试 | `mrX/test/kernel/clock.c`                                                   |

---

## 用户指南

### 1. 模块概述

时钟模块解决的问题是： **为整个内核提供统一、单调递增的时间基准，并在每个 tick 分发回调**。特性：

- 32 位全局 tick 计数（`mr_clock_tick`），由移植层周期性调用
  `mr_clock_increase` 推进，读取无需加锁。
- 64 位开机时长（`mr_clock_uptime`，毫秒），tick 回绕不影响其正确性。
- 每 tick 钩子链：钩子返回"距下次关心的剩余 tick 数"，汇总为 tickless，供 低功耗休眠调度使用。
- `MR_TICK_MSEC` / `MR_TICK_USEC` 毫秒/微秒转 tick 换算宏。
- 高分辨率扩展（`MR_USE_CLOCK_HIRES`）：64 位高分辨率 tick 与 Unix 纪元 日历时间戳转换（含闰年与时区处理）。
- 微秒级忙等 `mr_clock_busy_wait`（不让出 CPU）。

不适用于：阻塞式线程延时（本模块只计数与分发，不做任务调度）、tick 粒度 以下的精确延时（请使用高分辨率扩展或硬件定时器）。

### 2. 配置与依赖

| 配置项                         | 类型 | 默认值  | 说明                                                   |
|--------------------------------|------|---------|--------------------------------------------------------|
| `MR_CFG_TICK_PER_SECOND`       | 整型 | 1000    | 每秒 tick 数（`mrX/include/mrX/kernel/Kconfig:35`）    |
| `MR_USE_CLOCK_HIRES`           | bool | y       | 启用高分辨率扩展（`Kconfig:47`）                       |
| `MR_CFG_HIRES_TICK_PER_SECOND` | 整型 | 1000000 | 高分辨率 tick 频率，必须不大于 1000000（`Kconfig:55`） |
| `MR_CFG_CLOCK_HOOK_SIZE`       | 整型 | 2       | 钩子表容量，范围 2~128（`Kconfig:26`）                 |

```c
#include <mrX/kernel/mr_clock.h>
```

该头文件在配置宏未定义时自带与 Kconfig 相同的默认值 （`mrX/include/mrX/kernel/mr_clock.h:24`、`mr_clock.h:34`），未走
Kconfig 流程也能直接编译。时间换算：`MR_TICK_MSEC(1000)` 等于
`MR_CFG_TICK_PER_SECOND`；默认 1000 tick/s 下 1 tick = 1 ms。

### 3. 快速上手

读取 tick 与开机时长，并注册一个每 tick 被调用的钩子：

```c
#include <mrX/kernel/mr_clock.h>

static mr_tick_t my_hook(void) {
    return MR_TICK_MAX;         /* MR_TICK_MAX means "no preference" */
}

void clock_demo(void) {
    mr_tick_t now;
    mr_uptime_t uptime_ms;

    mr_clock_hook_add(my_hook);
    mr_clock_increase(1);       /* Usually driven by the platform tick source */

    now = mr_clock_tick();
    uptime_ms = mr_clock_uptime();
    (void)now;
    (void)uptime_ms;
}
```

要点：

- `mr_clock_increase` 由移植层的 tick 源调用（目标板通常是滴答定时器中断， 宿主模拟环境由平台 tick 线程调用，见
  `mrX/platform/port/clock.c:38`）， 应用代码一般不直接调用。
- 钩子函数类型为 `mr_tick_t (*)(void)`，返回值含义见第 4 节。

### 4. 使用说明

**tick 与 uptime**：`mr_clock_tick` 返回 32 位 tick，会回绕，只适合差值比较 （差值语义由 [软定时器 Timer](timer.md) 等上层处理）；
`mr_clock_uptime`
返回 64 位毫秒数，单调递增，适合直接测量时间间隔。

**钩子**：

- 钩子在每个 tick 被调用一次，运行在 `mr_clock_increase` 调用者的上下文 （目标板为 tick 中断）。钩子内禁止阻塞与耗时操作。
- 返回值约定：返回"距本模块下一次需要被调度的剩余 tick 数"（须大于 0 才参与 汇总），返回 `MR_TICK_MAX` 表示无意见。所有钩子的最小正返回值即
  `mr_clock_tickless` 的结果，供低功耗场景在下一个关注点之前休眠。

**高分辨率扩展**（`MR_USE_CLOCK_HIRES`）：

- `mr_hires_tick` 的绝对含义由移植层决定：宿主模拟环境将其初始化为 Unix 纪元（`mrX/platform/port/clock.c:60`），可直接经
  `mr_hires_tick2timestamp`
  得到日历时间；目标板需由移植层保证相同语义。
- `mr_hires_tick2timestamp` 的时区参数为 UTC+n，超出 ±12 会被钳位。
- `mr_clock_busy_wait(us)` 忙等指定微秒数，期间不让出 CPU，只适合极短延时。

### 5. API 参考

时间读取与推进：

| API                 | 说明                                       |
|---------------------|--------------------------------------------|
| `mr_clock_tick`     | 读取当前 32 位 tick                        |
| `mr_clock_uptime`   | 读取 64 位开机毫秒数                       |
| `mr_clock_tickless` | 获取 tickless（重跑钩子链取最小剩余 tick） |
| `mr_clock_increase` | 推进 tick（由移植层 tick 源调用）          |

钩子：

| API                 | 说明                 |
|---------------------|----------------------|
| `mr_clock_hook_add` | 注册一个每 tick 钩子 |

换算与延时（宏）：

| API                                     | 说明                         |
|-----------------------------------------|------------------------------|
| `MR_TICK_MSEC(ms)` / `MR_TICK_USEC(us)` | 毫秒/微秒转 tick             |
| `mr_clock_busy_wait`                    | 忙等指定微秒数（不让出 CPU） |

高分辨率（需 `MR_USE_CLOCK_HIRES`）：

| API                       | 说明                                    |
|---------------------------|-----------------------------------------|
| `mr_clock_hires_tick_set` | 设置高分辨率 tick（由移植层初始化调用） |
| `mr_clock_hires_tick`     | 读取高分辨率 tick                       |
| `mr_hires_tick2timestamp` | 高分辨率 tick 转 UTC 日历时间戳         |

**`mr_clock_increase`**

```c
void mr_clock_increase(mr_tick_t tick);
```

将 tick 计数增加 `tick`（通常为 1），随后依次调用所有已注册钩子。由移植层 tick 源调用，应用不要重复调用，否则所有基于 tick
的服务都会加速。

**`mr_clock_tick` / `mr_clock_uptime` / `mr_clock_tickless`**

```c
mr_tick_t mr_clock_tick(void);
mr_uptime_t mr_clock_uptime(void);
mr_tick_t mr_clock_tickless(void);
```

`tick` 返回 32 位 tick；`uptime` 返回 64 位毫秒；`tickless` 重新执行钩子链， 汇总各钩子返回的最小正剩余 tick，无钩子或无正返回值时返回
`MR_TICK_MAX`。

**`mr_clock_hook_add`**

```c
mr_err_t mr_clock_hook_add(mr_clock_hook_t *entry);
```

- `entry`：钩子函数，类型 `mr_tick_t (*)(void)`，不可为 `MR_NULL`。
- 返回：成功 `0`；钩子表已满返回 `-MR_ENOMEM`。表容量由
  `MR_CFG_CLOCK_HOOK_SIZE` 决定。

**`mr_clock_hires_tick_set` / `mr_clock_hires_tick`**

```c
void mr_clock_hires_tick_set(mr_hires_tick_t tick);
mr_hires_tick_t mr_clock_hires_tick(void);
```

直接读写移植层高分辨率计数（`mr_port_clock_hires_tick_set/hires_tick`）；
`set` 供移植层初始化为绝对时间基准，应用无需使用。

**`mr_hires_tick2timestamp`**

```c
mr_hires_timestamp_t mr_hires_tick2timestamp(mr_hires_tick_t tick, int time_zone);
```

- `tick`：高分辨率 tick（含义由移植层定义，宿主为 Unix 纪元）。
- `time_zone`：时区 UTC+n，超出 ±12 被钳位。
- 返回：`mr_hires_timestamp_t`（年/月/日/时/分/秒/毫秒/微秒字段）。

**`mr_clock_busy_wait`**

```c
void mr_clock_busy_wait(mr_uint32_t us);
```

忙等 `us` 微秒，不放弃 CPU；实际精度取决于移植层 `mr_port_clock_busy_wait`。

### 6. 完整示例

示例 1：用 uptime 测量一段代码的耗时（毫秒）：

```c
mr_uptime_t t0 = mr_clock_uptime();
work_function();
mr_uptime_t cost = mr_clock_uptime() - t0;      /* elapsed milliseconds */
```

示例 2：注册钩子参与 tickless 汇总（改编自 `mrX/test/kernel/clock.c:33`）：

```c
static volatile mr_tick_t my_next;

static mr_tick_t poll_hook(void) {
    return my_next;
}

void tickless_demo(void) {
    mr_tick_t less;

    mr_clock_hook_add(poll_hook);
    my_next = 3;
    less = mr_clock_tickless();     /* less == 3 */
    my_next = 7;
    less = mr_clock_tickless();     /* less == 7 */
    (void)less;
}
```

示例 3：高分辨率 tick 转日历时间（改编自 `mrX/test/kernel/clock.c:102`）：

```c
mr_hires_timestamp_t ts = mr_hires_tick2timestamp(1725580800000000ULL, 8);
/* ts == 2024-09-06 08:00:00 (UTC+8) */
```

### 7. 常见问题（FAQ）

**Q：tick / 定时器完全不走？**

排查移植层 tick 源是否已初始化并在周期性调用 `mr_clock_increase`；宿主模拟 环境由平台 tick 线程驱动（
`mrX/platform/port/clock.c:38`）。

**Q：钩子注册失败返回 `-MR_ENOMEM`？**

钩子表容量为 `MR_CFG_CLOCK_HOOK_SIZE`（默认 2），已被内核服务（如软定时器的 到期检查）占用时需要调大该配置后重新生成
`mr_config.h`。

**Q：`MR_TICK_MSEC(1)` 结果是 0？**

换算是整数除法，`MR_CFG_TICK_PER_SECOND` 低于 1000 时 1 毫秒不足 1 tick， 结果截断为 0。

**Q：`mr_clock_tick` 为什么会回绕？能直接比较两个 tick 吗？**

tick 为 32 位无符号数，长期运行必然回绕，直接比较绝对值在回绕边界会出错。 应使用差值语义（由 [软定时器 Timer](timer.md)
等模块处理），或改用 64 位的
`mr_clock_uptime`。

**Q：tickless 有什么用？**

每个钩子声明"距下次关心还有多少 tick"，`mr_clock_tickless` 给出全系统最近的 关注点，低功耗场景可在该点之前暂停 tick 源、唤醒后用
`mr_clock_increase`
一次性补齐。

**Q：不用高分辨率扩展，`mr_hires_tick` 还能用吗？**

不能。高分辨率 API 全部在 `MR_USE_CLOCK_HIRES` 下才声明与编译 （`mrX/include/mrX/kernel/mr_clock.h:113`）；且
`MR_CFG_HIRES_TICK_PER_SECOND`
不得大于 1000000，否则编译报错。

---

## 开发者指南

### 8. 架构总览

时钟是内核基础设施层，位于移植层之上、各时间服务之下：

```
   +------------------------------+
   | 平台 tick 源                  |  目标板: 滴答定时器 ISR
   | (mrX/platform/port/clock.c)  |  宿主: tick 线程
   +---------------+--------------+
                   | mr_clock_increase(1)
   +---------------v--------------+
   | 时钟 Clock                    |----> __clock_tick (32 位原子量)
   |                              |----> __clock_uptime (64 位毫秒, 回绕扩展)
   |                              |----> __clock_hook 钩子链
   +------+-----------------------+
          | clock_hook() 每 tick 分发
   +------v-----------------------+
   | 消费者                        |  软定时器到期检查(timer 模块注册)
   |                              |  其他内核服务的周期回调
   +------------------------------+
        依赖：钩子 Hook(钩子链容器)、原子操作(mr_atomic)、
              移植层(mr_port_clock: hires tick 与 busy-wait)
```

- 钩子链由 [钩子 Hook](hook.md) 模块提供容器与加锁添加接口。
- [软定时器 Timer](timer.md) 在 `mr_kernel_timer_init`（`mrX/kernel/timer.c:264`） 中把 `timer_timeout_check` 注册为时钟钩子。
- 高分辨率时间不走 tick 通路：`mr_clock_hires_tick` 直通移植层实现 （`mrX/kernel/clock.c:114`）。

### 9. 数据结构

公开类型（`mrX/include/mrX/kernel/mr_clock.h`）：

| 类型                   | 定义位置        | 说明                                           |
|------------------------|-----------------|------------------------------------------------|
| `mr_tick_t`            | `mr_clock.h:30` | `mr_uint32_t`，全局 tick；`MR_TICK_MAX` 为全 1 |
| `mr_hires_tick_t`      | `mr_clock.h:43` | `mr_uint64_t`，高分辨率 tick                   |
| `mr_hires_timestamp_t` | `mr_clock.h:47` | 年/月/日/时/分/秒/毫秒/微秒字段                |
| `mr_uptime_t`          | `mr_clock.h:60` | `mr_uint64_t`，开机毫秒数                      |
| `mr_clock_hook_t`      | `mr_clock.h:64` | 钩子类型 `mr_tick_t (*)(void)`                 |

内部静态数据（`mrX/kernel/clock.c:17-32`）：

| 变量                                    | 说明                                         |
|-----------------------------------------|----------------------------------------------|
| `__clock_entry[MR_CFG_CLOCK_HOOK_SIZE]` | 钩子函数指针表                               |
| `__clock_hook`                          | `mr_hook_t` 钩子容器（`MR_HOOK_INIT`）       |
| `__clock_uptime`                        | 原子量，tick 回绕次数（"粗粒度"高位）        |
| `__clock_tick`                          | 原子量，当前 32 位 tick（"细粒度"低位）      |
| `__clock_gen`                           | 原子量，代数计数器，供 uptime 一致性快照使用 |

`mr_uptime_t` 的编码方式：`tick = coarse * 2^32 + thin`，`coarse` 是回绕次数、
`thin` 是当前 32 位 tick，拼接成 64 位后按 `MR_CFG_TICK_PER_SECOND` 换算为 毫秒（`mrX/kernel/clock.c:208-210`）。

### 10. 核心流程

**tick 推进**（`mr_clock_increase`，`mrX/kernel/clock.c:55`）：代数计数器
`__clock_gen` 加 1（变为奇数，标记"更新中"）；`__clock_tick` 原子加 `tick`， 若 `(last + tick) < last` 判定回绕则
`__clock_uptime` 加 1；代数再加 1 （恢复偶数）；最后调用 `clock_hook()` 分发钩子。

**钩子分发与 tickless**（`clock_hook`，`clock.c:34`）：`MR_HOOK_EACH` 按注册 顺序调用每个钩子，取所有 **大于 0** 的返回值中的最小值作为
tickless，初值为
`MR_TICK_MAX`。`mr_clock_tickless`（`clock.c:80`）直接复用 `clock_hook()`， 因此查询 tickless 会重新执行一次钩子链。

**uptime 一致性快照**（`mr_clock_uptime`，`clock.c:185`）：seqlock 读法——读 代数 `gen` 为奇数（写者进行中）则重读；依次读
`coarse`、`thin` 后再读
`gen2`，`gen != gen2` 说明期间发生过推进，重试。全程无锁，保证 `coarse` 与
`thin` 属于同一时刻。拼接后经 `clock_scale_floor` 换算为毫秒。

**防溢出缩放**（`clock_scale_floor`，`clock.c:93`）：先对余数部分乘除、再对 商部分乘除（"split before multiplication"），避免
`tick * scale` 溢出 64 位； 商过大时结果饱和为 `MR_UINT64_MAX`。`mr_hires_tick2timestamp` 与
`mr_clock_uptime` 共用该函数。

**日历时间转换**（`mr_hires_tick2timestamp`，`clock.c:119`）：时区钳位 ±12； 高分辨率 tick 换算为微秒；加时区偏移（负值截断为
0）；依次整除分解出 微秒/毫秒/秒/分/时，再按平闰年逐日扣减求年、月，余数为日。闰年判定为
"能被 4 整除且不被 100 整除，或能被 400 整除"。

**busy-wait**（`clock.c:213`）：直通移植层 `mr_port_clock_busy_wait`，内核侧 不做任何计数。

### 11. 并发与中断安全

- `__clock_tick`、`__clock_uptime`、`__clock_gen` 均为原子量，
  `mr_clock_tick` 与 `mr_clock_uptime` 的读取无锁，可在任意上下文调用。
- `mr_clock_increase` 不加锁（原子量 + 代数保证一致性），但末尾执行全部钩子； 目标板上它运行在 tick 中断中，钩子共享中断上下文的约束。
- 钩子注册经 `mr_hook_add`，由钩子模块自身的自旋锁保护（`mrX/kernel/hook.c:14`）， 与 tick 分发并发安全；建议在初始化阶段完成注册。
- 钩子之间、钩子与 `mr_clock_tickless` 的重跑之间没有互斥：钩子实现必须 可重入且幂等。

### 12. 扩展与派生

时钟的扩展点是钩子链：任何需要"每 tick 驱动"或"参与 tickless 声明"的服务， 实现一个 `mr_tick_t (*)(void)` 函数并
`mr_clock_hook_add` 即可，无需修改时钟 代码。参考用例：[软定时器 Timer](timer.md) 的到期检查 `timer_timeout_check`
注册为钩子并返回距下次到期的剩余 tick（`mrX/kernel/timer.c:264`）。注册时机 建议放在内核初始化序列中；钩子表容量由
`MR_CFG_CLOCK_HOOK_SIZE` 决定，超出 返回 `-MR_ENOMEM`。

### 13. 源码导读

| 位置                                   | 职责                                         |
|----------------------------------------|----------------------------------------------|
| `mrX/include/mrX/kernel/mr_clock.h:24` | tick 频率默认值与 tick/uptime/hires 类型     |
| `mrX/include/mrX/kernel/mr_clock.h:72` | `MR_TICK_MSEC` / `MR_TICK_USEC` 换算宏       |
| `mrX/kernel/clock.c:17`                | 钩子表容量兜底定义与钩子容器                 |
| `mrX/kernel/clock.c:28`                | tick/uptime/gen 三个原子量                   |
| `mrX/kernel/clock.c:34`                | 钩子分发与 tickless 汇总                     |
| `mrX/kernel/clock.c:55`                | tick 推进与回绕检测                          |
| `mrX/kernel/clock.c:93`                | 防溢出缩放 `clock_scale_floor`               |
| `mrX/kernel/clock.c:119`               | 高分辨率 tick 转日历时间戳                   |
| `mrX/kernel/clock.c:185`               | uptime 无锁一致性快照                        |
| `mrX/kernel/timer.c:264`               | 软定时器向时钟注册钩子（消费者示例）         |
| `mrX/platform/port/clock.c`            | 宿主移植层：tick 线程、hires 基准、busy-wait |
| `mrX/test/kernel/clock.c`              | 全部用户可见行为的行为级测试                 |

### 14. 附录：配置宏与错误码

**配置宏**

| 宏                             | 说明                                           |
|--------------------------------|------------------------------------------------|
| `MR_CFG_TICK_PER_SECOND`       | 每秒 tick 数，默认 1000，头文件内置同值兜底    |
| `MR_USE_CLOCK_HIRES`           | 高分辨率扩展开关，默认 y                       |
| `MR_CFG_HIRES_TICK_PER_SECOND` | 高分辨率 tick 频率，默认 1000000，上限 1000000 |
| `MR_CFG_CLOCK_HOOK_SIZE`       | 钩子表容量，默认 2，最小 2                     |
| `MR_TICK_MAX`                  | 32 位 tick 上限（全 1），亦作"无意见"返回值    |

**错误码**：本模块仅 `mr_clock_hook_add` 有失败路径，钩子表满返回
`-MR_ENOMEM`（`mrX/kernel/hook.c:34`）；`entry` 为 `MR_NULL` 在断言开启时由
`MR_ASSERT` 拦截。其余 API 无失败路径（返回 `void` 或无失败返回值）。
