# 初始化调用（Initcall）

提供基于链接器段收集的自动初始化机制：各组件通过 `MR_INIT_EXPORT`
将初始化函数按级别放入专用段，系统启动时按字典序自动遍历执行。

| 项目   | 说明                                                                                                                |
|--------|---------------------------------------------------------------------------------------------------------------------|
| 功能宏 | 无（始终编译；内核初始化序列按各模块 `MR_USE_*` 条件展开）                                                          |
| 头文件 | `<mrX/kernel/mr_initcall.h>`                                                                                        |
| 依赖   | [平台（Platform）](../platform/platform.md)、[软定时器（Timer）](timer.md)、[中断管理（Irq）](irq.md)（按配置接线） |
| 测试   | `test/kernel/initcall.c`                                                                                            |

## 快速上手

```c
#include <mrX/kernel/mr_initcall.h>

static void app_early_init(void) {
    /* 先于 app_late_init 执行 */
}
MR_INIT_EXPORT(app_early_init, MR_INIT_LEVEL_USER("0"));

static void app_late_init(void) {
    /* 在 app_early_init 之后执行 */
}
MR_INIT_EXPORT(app_late_init, MR_INIT_LEVEL_USER("1"));

int main(void) {
    mr_user_init(); /* 先幂等执行 mr_kernel_init()，再按级别顺序执行全部导出项 */
    return 0;
}
```

## 核心机制与约束

- **两段式启动**：
    1. `mr_kernel_init()` 按固定顺序初始化核心服务：`mr_platform_init`（
       `MR_USE_PLATFORM`）→ `mr_libc_malloc_init`（`MR_USE_MALLOC`）→
       `mr_kernel_timer_init`（`MR_USE_TIMER`）→ `mr_kernel_irq_init`（
       `MR_USE_IRQ`）→ `mr_fs_file_init`（`MR_USE_FS`）。
    2. `mr_user_init()` 先调用 `mr_kernel_init()`，再按段名字典序遍历执行全部
       `MR_INIT_EXPORT` 条目。若启用了 [入口（Entry）](entry.md)，Entry 仅自动执行
       `mr_kernel_init()`，应用需自行在 `main()` 中调用 `mr_user_init()`。
- **级别与排序边界**：链接脚本将 `mr_initcall.*` 段按字典序收集，运行期从哨兵
  `"0.end"` 遍历至 `"9.end"`。自定义级别字符串必须落在该区间内，推荐直接使用预定义宏。
- **函数签名与错误处理**：导出函数原型固定为 `void (*)(void)`
  ，无参数也无返回值，同一函数不可重复导出（会引起符号重定义）。初始化失败无返回值上报通道，需自行记录状态或断言。
- **单线程启动假设**：`mr_kernel_init()` 与 `mr_user_init()` 内部使用普通
  `volatile mr_bool_t` 状态标记实现幂等短路，不可在多线程环境下并发调用。

## API 速查

| API / 宏                                         | 说明                                                             |
|--------------------------------------------------|------------------------------------------------------------------|
| `MR_INIT_EXPORT(_entry, _level)`                 | 导出初始化函数 `_entry` 到 `mr_initcall.<_level>` 段             |
| `MR_INIT_LEVEL_IRQ` / `FS` / `DRIVER` / `MODULE` | 框架预定义级别，值依次为 `"1.0"`、`"1.1"`、`"1.2"`、`"1.3"`      |
| `MR_INIT_LEVEL_USER(_level)`                     | 用户级初始化级别，展开为 `"8."_level`                            |
| `void mr_kernel_init(void)`                      | 执行硬编码的内核服务初始化序列（幂等）                           |
| `void mr_user_init(void)`                        | 确保内核已初始化后，按顺序执行全部 `MR_INIT_EXPORT` 条目（幂等） |

## 内部实现

核心实现位于 `kernel/initcall.c`：

```text
mr_user_init()
  ├─ MR_ASSERT(start < end)                 /* 校验链接段哨兵边界 */
  ├─ mr_kernel_init()                       /* 先初始化核心内核服务 */
  └─ for addr in [__mr_initcall_start .. __mr_initcall_end]:
       call->entry()                        /* 依次执行 "0.end" ~ "9.end" 间的条目 */
```

- **哨兵机制**：`kernel/initcall.c` 自身导出了空函数 `start`（`"0.end"`）和 `end`（
  `"9.end"`），其生成的符号 `__mr_initcall_start` 与 `__mr_initcall_end`
  即为遍历的起止边界，链接脚本（参见 `sections.toml` 中的
  `KEEP(*(SORT(mr_initcall.*)))`）负责按段名排序。
- **非原子状态标记**：`__initcall_kstatus` 与 `__initcall_ustatus` 在进入实际初始化前即置为
  `MR_TRUE`，既防止重入死循环，又避免在原子操作尚未就绪的极早期启动阶段产生依赖。
