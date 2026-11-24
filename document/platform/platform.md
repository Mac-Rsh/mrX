# 平台移植层（Platform）

平台移植层为 mrX 内核提供与硬件或宿主环境相关的底层实现（时钟节拍、中断开关、字符输出、断言处理及可选的缓存维护），是内核对接任意
MCU、SoC 或宿主环境的统一边界。

| 项目     | 说明                                                                                 |
|----------|--------------------------------------------------------------------------------------|
| 功能宏   | `MR_USE_PLATFORM`（由已安装平台的 Kconfig 自动选中）                                 |
| 头文件   | `<mrX/platform/mr_platform.h>`（安装后生成）、`<mrX/port/mr_port_*.h>`（内核侧契约） |
| 依赖     | [时钟 Clock](../kernel/clock.md)、[初始化调用](../kernel/initcall.md)                |
| 参考实现 | `mrX/arch/` 下各平台目录（安装后复制至 `mrX/platform/`）                             |

## 配置

| 配置项                            | 类型 | 默认值   | 说明                                                                 |
|-----------------------------------|------|----------|----------------------------------------------------------------------|
| `MR_USE_PLATFORM`                 | bool | 平台定义 | 启用平台初始化，`mr_kernel_init()` 会调用 `mr_platform_init()`       |
| `MR_CFG_TICK_PER_SECOND`          | int  | 1000     | 系统 tick 频率（Hz），平台 tick 源按此频率驱动 `mr_clock_increase()` |
| `MR_USE_CLOCK_HIRES`              | bool | y        | 启用高分辨率时钟，要求实现 `mr_port_clock_hires_*` 接口              |
| `MR_CFG_HIRES_TICK_PER_SECOND`    | int  | 1000000  | 高分辨率 tick 频率（Hz，上限 `1000000`）                             |
| `MR_USE_DCACHE` / `MR_USE_ICACHE` | bool | 平台定义 | 启用后要求实现 `<mrX/port/mr_port_cache.h>` 中对应缓存接口           |

## 快速上手

仓库克隆后 `mrX/platform/` 与 `mrX/include/mrX/platform/` 默认为空，使用前需先通过
CLI 安装目标平台，再重新运行 `kconfig` 生成 `mr_config.h`：

```bash
python ./scripts/cli.py platform install -i arch/win/install.toml
python ./scripts/cli.py kconfig
```

将安装后的 `platform/` 源码纳入工程编译（不要直接收集 `arch/` 原始目录）。应用启动时只需调用
`mr_user_init()`，内核会自动完成 `mr_platform_init()` 并启动 tick 源：

```c
#include <mrX.h>

static void heartbeat(mr_timer_t *timer, void *param) {
    MR_IGNORE(timer);
    MR_IGNORE(param);
    mr_printf("uptime = %llu ms\r\n", mr_clock_uptime());
}

int main(void) {
    static mr_timer_t timer;

    mr_user_init();
    mr_timer_init(&timer, heartbeat, MR_NULL, MR_TICK_MSEC(1000) | MR_TIMER_PERIODIC);
    mr_timer_start(&timer);
    while (1) {
    }
}
```

### Windows 宿主平台说明

仓库内置 `arch/win/` 作为宿主验证平台：普通 tick 线程基于高精度性能计数器推进并在调度延迟后补发；启用
`MR_USE_CLOCK_HIRES` 时以启动 UTC 时间为基准按性能计数器推进。在 Kconfig 的
`Libc: Standard library` 中可将 `Atomic` 选为 `3RD`（`MR_USE_ATOMIC_3RD`，基于
`__atomic` 内建函数）或将 `Assertion` 选为 `3RD`（`MR_USE_ASSERT_3RD`）。

## 核心机制与约束

```text
+-----------------------------------+
| 内核（clock / irq / log / printf） |
+-----------------+-----------------+
                  | 仅依赖契约头文件
+-----------------v-----------------+
| include/mrX/port/mr_port_*.h      |  内核侧接口契约（无实现）
+-----------------+-----------------+
                  | 经 platform install 安装并在链接期绑定
+-----------------v-----------------+
| platform/ （来自 arch/<vendor>/） |  调用目标 SDK 或宿主 API
+-----------------------------------+
```

- **单平台与安装边界**：同一输出目录同时只能安装一个平台，切换平台前必须先执行
  `platform uninstall`。`platform/` 与 `.platform/` 状态目录不纳入 Git 管理。
- **临界区嵌套语义**：`mr_port_irq_disable()` 返回的 `mask` 必须与
  `mr_port_irq_enable(mask)` 配对使用，实现必须支持嵌套并恢复进入前的状态，不得简化为无条件开中断。
- **中断与并发安全**：`mr_port_clock_hires_tick()` 可能在多上下文并发读取，需避免计数撕裂；
  `mr_port_output()` 需明确在中断上下文的可重入性与阻塞边界；`mr_platform_init()`
  仅在启动阶段由 `mr_kernel_init()` 串行调用一次。
- **断言与缓存契约**：`mr_port_assert()` 应保存或输出 `file`/`line`/`expr`
  现场（若返回，上层 `MR_ASSERT`/`MR_PANIC` 宏会立即进入死循环）。启用
  `MR_USE_DCACHE`/`MR_USE_ICACHE` 时必须实现全部对应符号，并处理好 cache-line
  对齐、内存屏障与 DMA 区域。

## API 速查

| 接口                                                                | 头文件                         | 说明                                                             |
|---------------------------------------------------------------------|--------------------------------|------------------------------------------------------------------|
| `mr_platform_init()`                                                | `<mrX/platform/mr_platform.h>` | 平台总初始化入口，由 `mr_kernel_init()` 自动调用                 |
| `mr_port_irq_disable()` / `mr_port_irq_enable(mask)`                | `<mrX/port/mr_port_irq.h>`     | 关/恢复中断或临界区，支持嵌套                                    |
| `mr_port_clock_hires_tick()` / `mr_port_clock_hires_tick_set(tick)` | `<mrX/port/mr_port_clock.h>`   | 读写高分辨率 tick（仅 `MR_USE_CLOCK_HIRES`）                     |
| `mr_port_clock_busy_wait(us)`                                       | `<mrX/port/mr_port_clock.h>`   | 微秒级忙等待（不放弃 CPU）                                       |
| `mr_port_output(buf, size)`                                         | `<mrX/port/mr_port_output.h>`  | 输出指定字节缓冲区，返回实际输出字节数或负值                     |
| `mr_port_trace(timestamp, type, tag, name, args, argc)`             | `<mrX/port/mr_port_trace.h>`   | 转发内核跟踪事件至上层记录器（仅 `MR_USE_TRACE`）                |
| `mr_port_assert(file, line, expr)`                                  | `<mrX/port/mr_port_assert.h>`  | 处理断言失败（记录现场、停机或复位）                             |
| `mr_port_dcache_*()` / `mr_port_icache_*()`                         | `<mrX/port/mr_port_cache.h>`   | 缓存启停、line size 查询及区间/全局 clean/invalidate（条件开启） |

## 新平台移植与验收

若只需为已有平台补充单个外设驱动，请直接查看对应驱动目录的 `porting.md`
。移植全新芯片或宿主环境的步骤如下：

1. 创建 `mrX/arch/<vendor>/<target>/`，编写 `install.toml`、平台 `Kconfig` 与
   `include/platform/mr_platform.h`：

```toml
[platform]
name = "vendor,chip"

[dir]
src = "."
dst = "./../../.."

[[platform.add]]
src = "include/platform"
dst = "include/mrX/platform"

[[platform.add]]
src = "platform"
dst = "platform"
```

2. 在 `platform/port/` 下实现 `clock.c`、`irq.c`、`output.c`、`assert.c`（以及可选的
   `cache.c`）。
3. 在 `platform/platform.c` 中实现 `mr_platform_init()`，按依赖顺序初始化时钟源、中断和输出通道；如启用
   `MR_USE_IRQ_DEFER`，在 `platform/irq/` 完成延迟中断接线。
4. 在平台目录下补充 `README.md`，记录该芯片特有的运行期语义（如时间量化、中断顺序与硬件限制）。

### 验收要点

- 使用 `platform install ... --dry-run` 与 `platform status` 验证安装路径，确保工程仅编译
  `platform/` 而非 `arch/`。
- 验证 tick 速率与 `MR_CFG_TICK_PER_SECOND` 一致，且发生延迟时能正确补发。
- 验证 `mr_port_irq_disable`/`enable` 嵌套调用不会提前打开中断。
- 验证 `mr_port_output` 返回实际写入字节数，`mr_port_assert` 能输出诊断信息（非空实现）。
- 启用 `MR_USE_CLOCK_HIRES` 或 cache 端口时，分别验证单调性与 `set` 偏移语义、以及未对齐地址的
  clean/invalidate 行为。
