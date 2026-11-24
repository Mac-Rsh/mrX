# 入口（Entry）

将工具链或板级启动入口对接到 `mr_kernel_init()`，确保用户 `main()` 执行前内核服务已就绪。

| 项目   | 说明                                                          |
|--------|---------------------------------------------------------------|
| 功能宏 | `MR_USE_ENTRY`（默认关闭）                                    |
| 头文件 | 无独立头文件（初始化接口声明于 `<mrX/kernel/mr_initcall.h>`） |
| 依赖   | [初始化调用（Initcall）](initcall.md)                         |

## 配置

| 配置项         | 类型 | 默认值 | 说明                                               |
|----------------|------|--------|----------------------------------------------------|
| `MR_USE_ENTRY` | bool | n      | 启用自动入口接线；关闭时 `kernel/entry.c` 编译为空 |

## 快速上手

启用 `MR_USE_ENTRY` 后，ARMCC 工具链自动接管 `main()`；其他工具链需在板级启动代码里调用
`mr_entry()`，应用 `main()` 中按需调用 `mr_user_init()` 执行用户级初始化项：

```c
#include <mrX/kernel/mr_initcall.h>

static void app_init(void) {
    /* 在内核服务就绪后执行的应用级初始化 */
}
MR_INIT_EXPORT(app_init, MR_INIT_LEVEL_USER("0"));

int main(void) {
    /* 若启用 MR_USE_ENTRY，此处 mr_kernel_init() 已完成 */
    mr_user_init(); /* 执行所有 MR_INIT_EXPORT 项（内部会自动幂等补做 mr_kernel_init） */

    for (;;) {
        /* 主循环 */
    }
    return 0;
}
```

## 核心机制与约束

- **两条工具链分支**：
    - **ARMCC（`__ARMCC_VERSION`）**：通过链接器特性 `$Sub$$main` 自动拦截 `main()`
      ，先调用 `mr_kernel_init()`，再通过 `$Super$$main` 进入用户 `main()`
      ，对应用和启动汇编完全透明。
    - **其他工具链**：导出 `int mr_entry(void)`，由板级启动代码代替 `main` 调用；
      `mr_entry()` 执行 `mr_kernel_init()` 后直接调用 `main()`，启动代码不可再额外跳转
      `main()`。
- **不自动运行用户 initcall**：Entry 仅调用 `mr_kernel_init()`，不会遍历
  `MR_INIT_EXPORT` 导出的用户初始化项；若需要执行用户 initcall，需在 `main()`
  中显式调用 `mr_user_init()`。
- **返回值与幂等性**：`$Sub$$main` 与 `mr_entry()` 均忽略用户 `main()` 的返回值并固定返回
  `0`。初始化状态由 [初始化调用（Initcall）](initcall.md) 的幂等标记控制，基于启动期单线程假设，不可并发调用
  `mr_entry()`。
- **手动接入**：未启用 `MR_USE_ENTRY` 时，在 `main()` 开头直接调用
  `mr_user_init()`（或 `mr_kernel_init()`）即可，功能等价。

## API 速查

| API                         | 说明                                                                       |
|-----------------------------|----------------------------------------------------------------------------|
| `int mr_entry(void)`        | 非 ARMCC 工具链的启动入口函数（定义于 `kernel/entry.c`，无公共头文件声明） |
| `void mr_kernel_init(void)` | 内核服务初始化序列（详见 [Initcall](initcall.md)）                         |
| `void mr_user_init(void)`   | 确保内核初始化后执行全部用户 initcall（详见 [Initcall](initcall.md)）      |
