# 框架使用指南（Usage）

本文介绍如何把 mrX 接入一个现有的应用工程，从环境准备一路走到 `mr_user_init()`
正常启动。

> 下文所有命令均默认在 **mrX 根目录**（包含 `Kconfig` 和 `scripts/cli.py`
> 的目录）下执行。

---

## 接入总览

```text
1. 安装平台 (platform)  ──▶  2. 配置功能 (kconfig)  ──▶  3. 硬件描述 (htc, 可选)
                                                                  │
6. 调用 mr_user_init() ◀──  5. 链接与注册段 (sections) ◀──  4. 纳入工程编译
```

> [!IMPORTANT]
> **必须先装平台再做 Kconfig 配置**。平台包会在 `Kconfig`
中注册自己的配置项，先安装才能在配置界面里看到它们。

---

## 前置准备

1. 一个能正常编译的应用工程（mrX 作为源码依赖接入）。
2. Python 3.11+，并在 mrX 根目录安装脚本依赖：

```bash
python -m pip install -r ./scripts/requirements.txt
```

---

## 1. 安装平台适配

mrX 的
tick、中断开关、控制台输出和硬件驱动后端由平台层提供。按 [Platform 文档](platform/platform.md)
选择并安装与目标芯片匹配的平台，安装后的文件会落在 `<mrX>/platform/` 下。

---

## 2. 配置功能（Kconfig）

mrX 通过两类宏裁剪功能和资源：

- `MR_USE_*`：模块功能开关（是否编译）。
- `MR_CFG_*`：静态资源或参数（缓冲区大小、队列深度、最大对象数等）。

在 mrX 根目录运行交互配置界面：

```bash
python ./scripts/cli.py kconfig
```

保存并退出后，工具会自动维护 `.config` 并生成 `include/mrX/mr_config.h`。

如果是 CI 或脚本等无终端交互环境，在已有 `.config` 的前提下运行：

```bash
python ./scripts/cli.py kconfig --no-ui
```

> `mr_config.h`
>
是自动生成产物，不要手动修改。更多参数见 [Kconfig 脚本说明](scripts/kconfig/kconfig.md)。

---

## 3. 编译硬件描述（HTC，可选）

如果不使用硬件树（HardTree）驱动或板级描述，可以跳过这一步。

准备好描述板级外设和引脚的 `.hts` 文件后，运行：

```bash
python ./scripts/cli.py htc -i <硬件描述>.hts
```

默认生成 `include/mrX/mr_hardgen.h`。常见选项：

- `-I <目录>`：追加 `#include` 搜索路径（可多次指定）。
- `--no-cpp`：环境没有外部 C 预处理器时跳过预处理。
- `--strict`：开启严格校验。

每次修改 `.hts` 或硬件属性后都要重新运行一次 HTC。语法与 `MR_HT_*`
宏详见 [HTC 脚本说明](scripts/htc/htc.md) 和 [驱动框架](drivers/driver.md)。

---

## 4. 纳入应用构建

mrX 不接管你的构建系统，需要在工程中手动配置头文件路径和源码列表。

### 头文件搜索路径

只需添加一条：

```text
<mrX>/include
```

添加后即可使用 `#include <mrX.h>` 或按需包含单个头文件（如
`#include <mrX/kernel/mr_timer.h>`）。

### 源码收集范围

递归收集以下 6 个目录下的全部 `.c` 文件：

```text
<mrX>/kernel/
<mrX>/libc/
<mrX>/drivers/
<mrX>/fs/
<mrX>/modules/
<mrX>/platform/
```

**四个常见坑**：

1. **必须递归收集**：`drivers/gpio/`、`fs/` 等子目录里才是主体代码，只加顶层目录会漏文件。
2. **不要收集 `arch/`**：`arch/` 是适配源码仓库，真正参与编译的是安装后的
   `platform/`，同时加入会导致符号重定义。
3. **不要收集 `test/`**：除非要运行单元测试（见 [Test 文档](test/test.md)）。
4. **不要先打成静态库（`.a`/`.lib`）再链接**：initcall、驱动和 RPC
   依赖链接段注册；先归档会导致“只被段引用、未被显式调用”的目标文件被链接器丢弃。请以目标文件（
   `.o` / CMake `OBJECT` 库）形式直接参与最终链接。

Keil MDK 工程可使用 `python ./scripts/cli.py mdk`
自动同步文件列表，详见 [MDK 脚本说明](scripts/mdk/mdk.md)。

---

## 5. 链接与注册段

mrX 使用自定义段收集 initcall、驱动实例、RPC
接口和测试用例。链接器必须保留并聚合这些段，否则框架启动时找不到任何注册项。

- **GNU ld 链接脚本（GCC / Clang）**：运行以下命令原地注入注册段规则（可重复执行，幂等）：

```bash
python ./scripts/cli.py sections -i <链接脚本>.lds
```

- **Keil MDK（armlink）**：无需运行 `sections`。注册段符号带有 `used` 属性，armlink
  会自动保留并按段名连续排列。
- **其他链接器**：参考根目录 `sections.toml`，按对应链接器语法实现保留与起止符号聚合。

---

## 6. 启动框架

在应用完成底层时钟和基础硬件初始化后，调用一次：

```c
mr_user_init();
```

`mr_user_init()` 会依次初始化平台、已启用的内核服务以及所有注册的 initcall，
**不需要**再手动调用 `mr_kernel_init()` 或 `mr_platform_init()`。

如果启用了 `MR_USE_ENTRY`（见 [Entry 文档](kernel/entry.md)），框架会在 `main()`
之前完成内核初始化，但应用层的用户 initcall 仍需在 `main()` 中调用
`mr_user_init()` 触发。

> [!WARNING]
> 注册到 initcall 中的函数必须初始化完就返回，不要在里面写死循环或无限等待，否则后续模块将永远不会被初始化。

---

## 常见问题排查

| 现象                              | 排查方向                                                                                 |
|-----------------------------------|------------------------------------------------------------------------------------------|
| 编译找不到 `mr_port.h` 或平台实现 | 是否已执行 `platform install`，且工程收集了 `platform/`                                  |
| 改了配置但代码行为没变            | 是否保存了 menuconfig 并重新生成了 `mr_config.h`                                         |
| HTC 报错或找不到驱动实例          | 检查 `-I` 路径、节点 `compatible` 与 `status`、是否重新生成了 `mr_hardgen.h`             |
| Initcall / 驱动未执行             | 是否调用了 `mr_user_init()`；是否误把 mrX 打成了静态库；GNU ld 脚本是否运行了 `sections` |
| 链接时报重复符号                  | 检查是否误将 `arch/` 和 `platform/` 同时加入了编译                                       |
| 没有 `printf` 或日志输出          | 检查功能宏是否开启、日志级别过滤以及平台输出接口是否接通                                 |

---

## CLI 常用命令速查

```bash
python ./scripts/cli.py list                        # 查看所有可用子命令
python ./scripts/cli.py help <command>              # 查看某条命令的详细参数
python ./scripts/cli.py                             # 不带参数进入交互式 Shell
python ./scripts/cli.py kconfig                     # 启动配置 UI
python ./scripts/cli.py kconfig --no-ui             # 根据 .config 直接生成 mr_config.h
python ./scripts/cli.py htc -i board.hts            # 编译硬件描述文件
python ./scripts/cli.py sections -i link.lds        # 注入 GNU ld 注册段规则
```

更多用法见 [CLI 总览](scripts/cli/cli.md)。
