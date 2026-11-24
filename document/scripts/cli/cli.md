# CLI（scripts/cli.py）

`scripts/cli.py` 是 mrX 的主机端统一命令行入口，支持交互式
Shell（无参数启动）与单次执行（带子命令启动），用于生成 `mr_config.h`、`mr_hardgen.h`
等构建前文件以及执行工程维护任务。

工具依赖 Python 3.11+，首次使用前在仓库根目录 `mrX/` 下安装依赖：

```bash
pip install -r ./scripts/requirements.txt
```

## 用途

CLI 将各构建与维护工具整合到同一入口下，所有子命令共用路径解析、参数补全、诊断输出和退出码规范：

| 命令       | 说明                                          | 详细文档                                     |
|------------|-----------------------------------------------|----------------------------------------------|
| `kconfig`  | 读取 Kconfig 与 `.config` 生成 `mr_config.h`  | [Kconfig 配置脚本](../kconfig/kconfig.md)    |
| `htc`      | 编译 `.hts`/`.htsi` 硬件描述为 `mr_hardgen.h` | [HTC 硬件描述编译](../htc/htc.md)            |
| `rpc-idl`  | 编译 `.idl` 接口文件为 RPC C 代码             | [RPC IDL 编译](../rpc-idl/rpc-idl.md)        |
| `platform` | 安装、卸载或检查平台适配文件                  | [平台文件管理](../platform/platform.md)      |
| `sections` | 向 GNU ld 链接脚本注入托管段规则              | [GNU ld 注册段处理](../sections/sections.md) |
| `mdk`      | 增删、同步或重命名 Keil MDK 工程文件          | [Keil MDK 工程维护](../mdk/mdk.md)           |
| `echo`     | 原样打印参数，最小插件示例                    | [echo 插件示例](../echo/echo.md)             |

## 常用命令

### 交互模式

在 TTY 终端中不带参数（或显式传入 `shell`）即可进入常驻交互模式，提示符为 `mrX>`：

```bash
python ./scripts/cli.py
```

若标准输入或输出不是 TTY（如管道、CI 重定向），无参数或 `shell`
启动不会等待输入，而是直接打印总帮助并以退出码 `0` 退出。注意 `shell` 由顶层分派器（
`scripts/framework/application.py`）单独识别，不在 `list` 列表中，`help shell`
会返回未知命令错误。

交互模式内置命令包括 `help`（别名 `h`）、`list`（别名 `ls`）、`history`、`clear`、`pwd`、
`cd <目录>`、`exit`、`quit`（别名 `q`）。其中 `cd` 仅修改当前会话工作目录，不影响外部进程。

### 单次执行

在命令后直接附带子命令和参数即可单次执行并退出，退出码回传给系统：

```bash
python ./scripts/cli.py list
python ./scripts/cli.py help kconfig
python ./scripts/cli.py kconfig --no-ui
python ./scripts/cli.py htc -i board.hts
python ./scripts/cli.py rpc-idl -i service.idl
```

## 参数说明

### 全局选项

全局选项 **必须放在子命令名之前**；放在子命令之后的 `-h`/`--help` 由对应子命令自己解析：

| 选项           | 说明               |
|----------------|--------------------|
| `-h`, `--help` | 打印总帮助并退出   |
| `--version`    | 打印项目版本并退出 |
| `--verbose`    | 开启详细输出模式   |
| `--no-color`   | 禁用终端彩色输出   |

### 路径解析规则

- 未显式指定的默认路径（如 `Kconfig`、`.config`、`include/mrX/mr_config.h`、
  `include/mrX/mr_hardgen.h`）始终相对于仓库根目录 `mrX/` 解析。
- 用户显式传入的相对路径相对于 **当前会话目录**解析（单次执行为进程当前目录，交互模式为
  `cd` 后的目录）。

### 交互快捷键与补全

交互模式内置命令名、别名、选项、枚举值和文件路径的 `Tab` 补全（不提供操作系统级别的
Bash/PowerShell 补全脚本）：

- `Tab`：补全命令、选项或路径（Windows 路径保留反斜杠，含空格路径可用引号）。
- `↑` / `↓`、`Ctrl-R`：浏览或搜索历史命令，按 `→` 或 `End`
  接受灰色历史建议。历史记录保存在用户状态目录（Windows 下通常为
  `%LOCALAPPDATA%\MacRsh\mrX\cli.history`），不写入 Git 仓库。
- `Ctrl-C`：在提示符下清空当前输入；在命令执行中中断当前命令（返回码 `130`），不退出
  Shell。
- `Ctrl-D`（Windows 下也可用 `Ctrl-Z`）或 `exit` / `quit`：退出交互模式，并返回最近一次命令的退出码。

## 输出与排错

| 退出码 | 含义                                   |
|--------|----------------------------------------|
| `0`    | 命令执行成功、打印帮助或版本           |
| `1`    | 命令运行失败或 I/O 错误                |
| `2`    | 未知命令或参数解析错误                 |
| `130`  | 用户通过 `Ctrl-C` 中断了正在执行的命令 |

若某个插件在启动时加载失败，CLI 会输出诊断提示，但不会阻断 `help`、`list`
或其他正常插件的运行。

### 新增插件约定

`scripts/cli.py` 本身只是薄入口，调度、参数解析、输出和补全由 `scripts/framework/`
提供。新增命令只需在 `scripts/plugins/` 下实现 `Command` 子类并在
`scripts/plugins/catalog.py` 的 `BUILTIN_PLUGINS` 中注册包名：

```python
from argparse import ArgumentParser, Namespace
from framework.command import Command, CommandResult, CommandSpec
from framework.completion import FILE_COMPLETION, attach_path_completion
from framework.context import CommandContext
from framework.plugin import ApplicationServices, Plugin


class ExampleCommand(Command):
    spec = CommandSpec(name="example", description="Example command")

    def configure(self, parser: ArgumentParser) -> None:
        action = parser.add_argument("-i", "--input", required=True, help="Input file")
        attach_path_completion(action, FILE_COMPLETION)

    def execute(self, context: CommandContext, arguments: Namespace) -> CommandResult:
        path = context.resolve_path(arguments.input)
        context.console.write(str(path))
        return CommandResult.success()


def create_plugin(services: ApplicationServices) -> Plugin:
    del services
    return Plugin(
        name="example",
        version="1.0.0",
        description="Example plugin",
        commands=(ExampleCommand(),),
    )
```

开发插件时需注意：命令参数只在 `configure()` 中定义一次（帮助、解析与补全共用）；输出统一走
`context.console`，不要直接使用 `print()`；路径解析统一使用
`context.resolve_path()` 或 `context.resolve_optional_path()`。
