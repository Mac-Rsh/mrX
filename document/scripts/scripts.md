[**中文**](scripts_scripts.md) | [**English**](scripts_scripts_en.md)

---

# Scripts 工具

Scripts 模块提供 mrX 工程的 Python 命令行工具入口，用于配置生成、硬件树编译、平台文件安装、链接脚本段规则维护，以及 MDK 工程文件同步。

脚本入口位于 `scripts/cli.py`，插件发现和注册逻辑位于 `scripts/dock.py`。建议在 `mrX` 目录下运行脚本，因为多数默认输入和输出路径都按当前工作目录解析。

## 基本用法

### 安装依赖

```text
python -m pip install -r scripts/requirements.txt
```

`requirements.txt` 包含 `kconfiglib` 和 `windows-curses`。脚本还依赖 Python 标准库中的 `tomllib`，因此建议使用 Python 3.11 或更新版本。

### 命令入口

```text
python ./scripts/cli.py <command> [args...]
```

常用辅助命令如下：

```text
python ./scripts/cli.py list
python ./scripts/cli.py help
python ./scripts/cli.py help kconfig
```

`dock.py` 是插件框架，不是独立命令行入口。正常使用时应通过 `cli.py` 调用。

## 插件机制

插件通过 `@export_plugin` 标记导出，并提供 `PluginSpec` 和 `run(args)`：

```python
@export_plugin
class ExampleCommand:
    spec = PluginSpec(name="example", description="Example command.")

    def run(self, args: list[str]) -> int:
        return 0
```

`Dock.discover_plugins()` 会扫描 `scripts/plugins` 下的顶层模块，以及一级子包中的直接 `.py` 模块。插件可以通过 `spec.aliases` 注册别名；命令名或别名冲突时会打印提示并保留先注册的插件。

当前可用命令包括：

| 命令 | 功能 |
| --- | --- |
| `list` | 列出已发现的主命令 |
| `help` | 显示命令用法 |
| `echo` | 输出传入参数 |
| `kconfig` | 处理 Kconfig 并生成 C 配置头文件 |
| `htc` | 编译 `.hts`/`.htsi` 硬件树文件并生成 `mr_hardgen.h` |
| `sections` | 将托管 section 规则写入 GNU ld 链接脚本 |
| `platform` | 安装、卸载或检查平台文件 |
| `mdk` | 管理 Keil MDK/uVision 工程文件 |

## 配置生成

### Kconfig

```text
python ./scripts/cli.py kconfig [options]
```

`kconfig` 命令处理 `Kconfig` 和 `.config`，然后生成 C 头文件。默认情况下命令会先启动外部 `menuconfig`，再按参数解析输入和输出路径；如果只需要生成文件，应使用 `--no-ui`。默认路径如下：

| 参数 | 默认值 | 说明 |
| --- | --- | --- |
| `-k, --kconfig` | `Kconfig` | Kconfig 输入文件 |
| `-c, --config` | `.config` | Kconfig 配置文件 |
| `-o, --output` | `./include/mrX/mr_config.h` | 输出 C 头文件 |
| `--no-ui` | 关闭 | 不启动 `menuconfig` |

示例：

```text
python ./scripts/cli.py kconfig --no-ui
python ./scripts/cli.py kconfig -k Kconfig -c .config -o include/mrX/mr_config.h --no-ui
```

生成头文件时会移除 `CONFIG_` 前缀。例如 `CONFIG_MR_USE_LOG` 会输出为 `MR_USE_LOG`。该命令会重写 `.config` 和输出头文件；自动化构建中通常应使用 `--no-ui`。

### HTC 硬件树编译器

```text
python ./scripts/cli.py htc -i <input.hts|input.htsi> [-o <output.h>]
```

`htc` 命令将 `.hts` 或 `.htsi` 硬件树源码编译为 C 宏头文件，默认输出为 `./include/mrX/mr_hardgen.h`。命令会创建输出目录并覆盖目标头文件。生成的宏供 `mr_hardtree.h` 中的 `MR_HT_*` 宏访问节点、属性、label、alias 和 compatible 实例。

`.hts` 通常作为主硬件树输入文件；`.htsi` 通常作为可复用 include 片段，二者关系类似 Linux Devicetree 中 `.dts` 与 `.dtsi` 的关系。预处理器会按 include 路径读取 `.hts` 和 `.htsi` 文件。

编译流程如下：

```text
Preprocessor -> Lexer -> Parser -> SemanticBuilder -> TreeValidator -> MacroBuilder -> header
```

`.hts`/`.htsi` 文件必须使用根节点结构：

```dts
/ {
    aliases {
        console = &uart0;
    }

    uart0: uart@0x4000 {
        compatible = "uart";
        status = "okay";
        baudrate = 115200;
    }
}
```

支持的主要语法包括节点 label、节点地址、普通属性、flag 属性、label 引用、路径引用、展开数组 `<...>` 和 inline 数组 `[...]`。

预处理器支持：

```c
/include/"file.htsi"
#include "file.htsi"
#include <file.htsi>
#define NAME value
#undef NAME
#if expr
#ifdef NAME
#ifndef NAME
#elif expr
#else
#endif
```

节点 `status` 支持 `okay`、`disabled`、`early`、`last`、`defer` 和 `prio(n)`，其中 `prio(n)` 的 `n` 范围为 `0..9`。`disabled` 节点不会输出普通节点宏，引用 `disabled` 节点会产生 warning。

## 链接脚本段规则

### Sections

```text
python ./scripts/cli.py sections -i <input.ld> [options]
```

`sections` 命令读取 `sections.toml`，把配置的输入段规则写入 GNU ld 链接脚本的 `SECTIONS { ... }` 块。

| 参数 | 默认值 | 说明 |
| --- | --- | --- |
| `-i, --input` | 无 | 目标 linker script，必需 |
| `-c, --config` | `sections.toml` | section 配置文件 |

配置示例：

```toml
[[section]]
name = "initcall"
input = "mr_initcall.*"
output = ".text"
keep = true
sort = true
comment = "Initcall"
```

生成规则会带 `@mrX:` 标记注释：

```c
/* @mrX: Initcall */
KEEP(*(SORT(mr_initcall.*)))
```

如果目标 output section 已存在，规则会插入到该 section 内；否则会新增 output section。命令会原地改写 `.ld` 文件。

## 平台文件安装

### Platform

```text
python ./scripts/cli.py platform [install|uninstall|status] [options]
```

`platform` 命令根据平台 `install.toml` 安装或卸载平台相关文件。常见用法如下：

```text
python ./scripts/cli.py platform install -i arch/win/install.toml --dry-run
python ./scripts/cli.py platform install -i arch/win/install.toml
python ./scripts/cli.py platform status -i arch/win/install.toml
python ./scripts/cli.py platform uninstall -i arch/win/install.toml
```

参数说明：

| 参数 | 说明 |
| --- | --- |
| `install` | 安装平台文件，默认 action |
| `uninstall` | 恢复安装前的文件状态 |
| `status` | 查看平台安装状态 |
| `-i, --input` | 平台 `install.toml` |
| `--dry-run` | 只打印计划，不写文件 |
| `--force` | 卸载时忽略安装后文件变化 |

平台配置格式示例：

```toml
[platform]
name = "win"

[dir]
src = "."
dst = "./../.."

[[platform.add]]
src = "include/platform"
dst = "include/mrX/platform"
```

安装时会写入 `.platform/manifest.json` 和 `.platform/previous`，用于记录文件状态和卸载恢复。卸载时如果发现安装后的目标文件已变化，默认会拒绝恢复；确认可以丢弃这些变化时才使用 `--force`。

## MDK 工程维护

### MDK

```text
python ./scripts/cli.py mdk <add|del|sync|rename> -p <project.uvprojx> [options]
```

`mdk` 命令用于管理 Keil MDK/uVision 工程文件，支持 `.uvprojx` 和 `.uvproj`。

| 参数 | 说明 |
| --- | --- |
| `add` | 将源文件或目录加入工程 |
| `del` | 从工程中删除匹配文件项 |
| `sync` | 在 `--source` 覆盖范围内添加新增文件并删除已不存在的工程条目 |
| `rename` | 重命名工程文件集和工程输出名 |
| `-p, --project` | 目标 `.uvprojx` 或 `.uvproj` 文件 |
| `-s, --source` | `add`、`del`、`sync` 使用的源文件或目录 |
| `-n, --name` | `rename` 使用的新工程名 |
| `-t, --target` | 多 Target 工程中的目标名 |
| `-g, --group` | 指定 MDK Group 名 |
| `--keep-empty-groups` | 删除后保留空 Group |

示例：

```text
python ./scripts/cli.py mdk add -p project.uvprojx -s src
python ./scripts/cli.py mdk sync -p project.uvprojx -s src -g Sources
python ./scripts/cli.py mdk rename -p old.uvprojx -n new
```

`add`、`del` 和 `sync` 会修改工程 XML。`rename` 还可能重命名 `.uvoptx`、`.uvopt`、`.uvguix.*` 或 `.uvgui.*` 文件，并更新工程中的 `TargetName` 和 `OutputName`。

当 `--source` 是目录且未指定 `--group` 时，`add`、`del` 和 `sync` 会按源目录下可见的一级子目录自动分组，并递归处理各子树。点号开头的隐藏目录和隐藏文件会被跳过。指定 `--group` 时，会把源目录树保留在给定 Group 名下，例如 `Sources/subdir`。

## 注意事项

- 建议从 `mrX` 目录运行脚本，避免默认相对路径指向错误位置。
- `kconfig` 默认会启动外部命令 `menuconfig`；未安装时应使用 `--no-ui`。
- `kconfig` 要求 `.config` 已存在，并会重写 `.config` 和输出头文件。
- `htc` 会创建输出目录并覆盖 `-o` 指定的头文件或默认 `mr_hardgen.h`。
- `htc` 的语法是 mrX 自定义硬件树语法，不等价于完整 Devicetree DTS。
- `htc` include 禁止绝对路径和 `..` 路径穿越，宏只支持简单 object-like 宏。
- `sections` 要求链接脚本中存在可识别的 `SECTIONS { ... }` 块，并会原地改写输入文件。
- `sections` 会删除和重写带 `/* @mrX: ... */` 标记的托管规则，不要把手写内容伪装成同类生成块。
- `platform install` 和 `uninstall` 会复制、删除或恢复文件，建议先使用 `--dry-run`。
- `platform uninstall --force` 会忽略安装后文件变化，应谨慎使用。
- `mdk rename` 涉及多个工程相关文件的重命名和 XML 更新，执行前建议先保存或提交工程文件。
- 插件导入失败时会打印错误并跳过该插件，可用 `list` 确认当前实际可用命令。
