# 内核入口

内核入口模块提供可选的系统启动入口封装。启用 `MR_USE_ENTRY` 后，入口代码会在进入用户 `main()` 之前调用 `mr_kernel_init()`，用于执行 mrX 内核初始化流程。

本文对应 `kernel/entry.c`、`include/mrX.h` 和 Kconfig 中入口相关配置。

## 模块用途

入口模块的目标是在应用主函数执行前统一完成内核初始化。`kernel/entry.c` 中的私有 `entry()` 函数只执行一项操作：调用 `mr_kernel_init()`。随后代码再进入真实的用户 `main()`。

不同工具链使用不同的入口接入方式。ARMCC 使用 `$Sub$$main()` 包装 `$Super$$main()`；其他工具链提供 `mr_entry()`，由启动代码或平台工程选择调用。

## 入口配置

### MR_USE_ENTRY

```kconfig
config MR_USE_ENTRY
    bool "Use entry"
    default n
```

`MR_USE_ENTRY` 控制 `kernel/entry.c` 是否编译入口封装。默认关闭。启用后，入口模块包含 `mr_initcall.h` 并调用 `mr_kernel_init()`。

## 主要实现

### 内部入口函数

```c
static void entry(void) {
    mr_kernel_init();
}
```

`entry()` 是 `kernel/entry.c` 内部函数，不对外声明。它负责触发内核初始化。

### ARMCC main 包装

```c
extern int $Super$$main(void);

int $Sub$$main(void) {
    entry();
    $Super$$main();
    return 0;
}
```

当定义 `__ARMCC_VERSION` 时，源码使用 ARMCC 的 `$Sub$$`/`$Super$$` 机制包装 `main()`。这种模式下，用户代码仍然实现普通 `main()`，工具链链接机制会先进入 `$Sub$$main()`，再调用 `$Super$$main()`。

### 通用入口函数

```c
extern int main(void);

int mr_entry(void) {
    entry();
    main();
    return 0;
}
```

非 ARMCC 工具链下，模块定义 `mr_entry()`。该函数先执行内核初始化，再调用用户 `main()`。当前头文件中没有单独声明 `mr_entry()`，平台启动文件或工程入口需要自行匹配该符号。

## 公共头文件关系

`include/mrX.h` 是 mrX 的聚合头文件。它包含内核公共头文件，包括 `mr_initcall.h`、`mr_hook.h`、`mr_log.h`、`mr_printf.h`、`mr_version.h` 等。入口实现直接包含的是 `<mrX/mr_config.h>` 和 `<mrX/kernel/mr_initcall.h>`，并不依赖 `<mrX.h>` 聚合头。

应用代码通常可以包含 `<mrX.h>` 获取内核、libc 和已启用驱动的公共接口；入口模块自身只关心初始化调用链。

## 使用示例

启用入口配置后，用户仍然可以编写普通 `main()`：

```c
#include <mrX.h>

int main(void) {
    /* mr_kernel_init() has been called before main when entry is wired in. */
    return 0;
}
```

非 ARMCC 工程如果希望使用 mrX 入口，需要让启动代码进入 `mr_entry()`：

```c
extern int mr_entry(void);

void platform_start(void) {
    (void)mr_entry();
}
```

## 注意事项

- `MR_USE_ENTRY` 默认关闭；未启用时 `kernel/entry.c` 不提供入口封装。
- ARMCC 路径依赖 `$Sub$$main` 和 `$Super$$main` 链接机制，适用性由工具链决定。
- 非 ARMCC 路径只定义 `mr_entry()`，不会自动替换 `main()`；启动代码需要显式调用或将入口符号配置为 `mr_entry`。
- `mr_entry()` 和 `$Sub$$main()` 都忽略用户 `main()` 的返回值并返回 0。
- 当前公共头文件中没有 `mr_entry()` 声明；外部启动代码需要自行声明，或由平台层提供匹配声明。
