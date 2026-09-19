# CLI（scripts/cli.py）

> mrX 命令行工具：无参数进入常驻交互 Shell，带命令时单次执行并退出。用于生成
> `mr_config.h`、`mr_hardgen.h` 等构建前文件，以及平台、链接脚本、MDK、RPC IDL 等维护命令。

本页说明 CLI 的共同行为和交互 Shell。各命令的完整输入格式、操作步骤和故障排查见：

| 命令       | 详细教程                               |
|------------|----------------------------------------|
| `kconfig`  | [Kconfig 配置与头文件生成](../kconfig/kconfig.md) |
| `htc`      | [HTC 硬件描述编译](../htc/htc.md)             |
| `rpc-idl`  | [RPC IDL 编译](../rpc-idl/rpc-idl.md)             |
| `platform` | [平台文件安装与卸载](../platform/platform.md)      |
| `sections` | [GNU ld 注册段处理](../sections/sections.md)       |
| `mdk`      | [Keil MDK 工程维护](../mdk/mdk.md)            |
| `echo`     | [echo 最小插件示例](../echo/echo.md)           |

| 项       | 说明                                            |
|----------|-------------------------------------------------|
| 功能宏   | 无（主机 Python 工具，不进入固件编译）          |
| 入口     | `mrX/scripts/cli.py`                            |
| 依赖     | Python 3.11+，见 `mrX/scripts/requirements.txt` |
| 工作目录 | 建议在 `mrX/` 下运行，以便复制粘贴下文命令      |

安装依赖：

```text
pip install -r ./scripts/requirements.txt
```

---

## 用户指南

### 1. 交互模式

在 TTY 中不带命令启动即可进入常驻交互 Shell：

```text
python ./scripts/cli.py
```

`python ./scripts/cli.py shell` 也可以进入交互模式，但 `shell` 不是注册命令：它由顶层分派 单独识别（
`scripts/framework/application.py:73`），不出现在 `list` 中，`help shell` 会以退出码 2 报未知命令。需要查看命令清单时用不带参数的启动方式或
`list`。

提示符为 `mrX>`。输入与单次执行相同的命令行，例如 `kconfig`。

若 stdin/stdout 不是 TTY（管道、重定向），无参数或 `shell` 启动不会等待输入，而是打印总帮助并以退出码 0 结束。

### 2. 单次执行

带命令时解析 `sys.argv`、执行一次并退出，退出码回传给操作系统：

```text
python ./scripts/cli.py kconfig
python ./scripts/cli.py htc -i board.hts
python ./scripts/cli.py rpc-idl -i file.idl
python ./scripts/cli.py list
python ./scripts/cli.py help kconfig
```

全局选项必须写在命令名之前：`--no-color`、`--verbose`、`--help`（`-h`）、`--version`。
`python ./scripts/cli.py -h` 与 `--help` 等价，打印总帮助。命令名之后的 `-h` / `--help` 由该命令 自己的参数解析器处理，显示的是子命令帮助，例如
`kconfig --help`。

### 3. 配置与生成命令

从 `mrX/` 目录运行：

```text
python ./scripts/cli.py kconfig
python ./scripts/cli.py htc -i <硬件模板>.hts
```

默认 `Kconfig`、`.config` 和生成头 `include/mrX/mr_config.h`、`include/mrX/mr_hardgen.h`
相对于仓库根目录。用户显式给出的相对路径相对于当前会话目录（交互里是 `cd` 后的目录， 单次执行是进程当前目录）。

`kconfig` 选项：

- `-k/--kconfig`：Kconfig 文件路径（默认 `Kconfig`）。
- `-c/--config`：`.config` 文件路径（默认 `.config`）。
- `-o/--output`：生成头文件路径（默认 `include/mrX/mr_config.h`）。
- `--no-ui`：跳过 menuconfig 界面；缺省时先启动 `menuconfig`（需要已安装）再生成。

`htc` 选项：

- `-i/--input`：必填，输入 `.hts` / `.htsi` 文件。
- `-o/--output`：输出头文件路径（默认 `include/mrX/mr_hardgen.h`）。
- `-I`：附加包含搜索目录，可重复。
- `--no-cpp`：使用内置预处理器代替外部 C 预处理器。
- `--strict`：对不属于 Linux DTS 的 HTC 扩展语法给出告警。

### 4. 维护命令

- `rpc-idl`：编译 RPC `.idl` 为 C 头文件与源文件。`-i/--input` 必填；`-o/--output` 为输出目录 或头文件路径（默认
  `include/mrX/modules/rpc/mr_rpc_idl.h`，只允许改目录，头文件名 固定为 `mr_rpc_idl.h`）；`-c/--source` 为输出 `.c` 路径（默认
  `modules/rpc/idl.c`）；
  `-I` 附加包含搜索目录，可重复；`--no-cpp` 使用内置预处理器代替外部 C 预处理器。 两个输出以临时文件原子替换写入，失败时回滚。
- `platform`：安装、卸载或查看平台文件。操作为位置参数 `install`（默认）、`uninstall`、
  `status`；`-i/--input` 为平台 `.toml` 文件；`--dry-run` 只打印计划不写文件；`--force`
  允许卸载安装后已被修改的文件。安装状态保存在输出目录的 `.platform/` 中。
- `sections`：向 GNU ld 风格链接脚本的 `SECTIONS` 块插入托管段收集规则。
  `-i/--input` 必填，为目标 `.ld` 文件；`-c/--config` 为段描述 TOML 文件 （默认 `sections.toml`）。
- `mdk`：维护 Keil MDK 工程。操作为位置参数 `add`、`del`、`sync`、`rename`；
  `-p/--project` 必填；`-s/--source` 用于 add / del / sync；`-n/--name` 用于 rename；
  `-t/--target` 指定目标名；`-g/--group` 指定根分组（目录源省略时自动分组）；
  `--keep-empty-groups` 在删除文件后保留空分组。
- `echo`：原样打印参数，作为最小插件示例。

### 5. 补全与历史

仅交互模式提供补全，不安装 PowerShell / Bash / Zsh 等操作系统 Shell 补全，也没有
`--install-completion`。

- Tab：补全命令名、别名、子命令/`choices`、选项和路径。
- `htc -` 可补全 `-i`、`-o`、`-I`、`--no-cpp`、`--strict`。
- `platform ` 可补全 `install`、`uninstall`、`status`。
- 路径补全保留 Windows 反斜杠，带空格的目录可用引号。
- 输入时按历史显示灰色建议，按 End 或右方向键接受。
- 上/下方向键浏览历史；Ctrl-R 搜索历史。
- 历史文件在操作系统用户状态目录中（Windows 一般为 `%LOCALAPPDATA%\MacRsh\mrX\cli.history`），不写入 Git 仓库。重启 CLI
  后历史仍可用。

### 6. 中断与退出

- 提示符下 Ctrl-C：清空当前输入，不退出 Shell。
- 命令执行中 Ctrl-C：该次命令退出码 130，交互 Shell 继续运行。
- Ctrl-D 退出交互模式；Windows 下也可用 Ctrl-Z。
- `exit` / `quit`（或 `q`）退出，返回最近一次命令的退出码。
- 交互里命令失败只更新 `last_exit_code`，不终止 Shell。

其它交互命令：`help [command ...]`、`list`、`history`、`clear`、`pwd`、`cd <directory>`。 其中 `list` 可写作 `ls`，`help` 可写作
`h`（`scripts/framework/builtins.py:33,63`）。
`cd` 只改会话目录，不调用进程级 `os.chdir()`。

### 7. 退出码

| 码  | 含义                                                                   |
|-----|------------------------------------------------------------------------|
| 0   | 成功、帮助、版本                                                       |
| 1   | 命令运行错误（插件加载失败只留下诊断，不改变退出码，也不阻止其它命令） |
| 2   | 未知命令或参数错误                                                     |
| 130 | 用户中断正在执行的命令                                                 |

---

## 开发者指南

### 8. 架构

`cli.py` 只是薄入口。`framework/` 负责调度、注册、argparse、输出、交互和补全。 第一方插件在 `plugins/catalog.py` 显式列出，每个包只导出：

```python
def create_plugin(services: ApplicationServices) -> Plugin:
    ...
```

命令参数只定义一次：`Command.configure()` / `create_parser()`。帮助、解析和 Tab 补全 都从同一个 argparse parser 读取。HTC、RPC
IDL 的 frontend / semantic / backend 不因 CLI 而改写。

单个插件加载失败会留下诊断，但 `help` 和其它插件仍可用。

### 9. 插件入口示例

在 `plugins/catalog.py` 的 `BUILTIN_PLUGINS` 中加入包名，并在包的 `__init__.py` 中：

```python
from framework.plugin import ApplicationServices, Plugin
from framework.command import Command, CommandResult, CommandSpec
from framework.completion import FILE_COMPLETION, attach_path_completion
from argparse import ArgumentParser, Namespace
from framework.context import CommandContext


class ExampleCommand(Command):
    spec = CommandSpec(
        name="example",
        description="Example command",
    )

    def configure(self, parser: ArgumentParser) -> None:
        action = parser.add_argument("-i", "--input", required=True, help="Input file")
        attach_path_completion(action, FILE_COMPLETION)

    def execute(self, context: CommandContext,
                arguments: Namespace) -> CommandResult:
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

业务代码通过 `CommandContext.console` 输出，不要直接依赖 Rich，也不要使用 `print()`。 相对路径一律走
`context.resolve_path()`；项目默认文件用
`context.resolve_optional_path(user_value, default)`。
