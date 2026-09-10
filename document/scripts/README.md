# mrX 主机脚本

mrX/scripts/ 是构建前和工程维护用的 Python 工具目录。用户通常只需要执行 cli.py，它会加载内置插件并把每个命令转发到对应脚本。所有相对路径都建议从
mrX/ 目录执行：

~~~text
cd mrX
python ./scripts/cli.py <command> [options]
~~~

## 安装与命令索引

Python 版本和依赖安装方式：

~~~text
python --version
python -m pip install -r ./scripts/requirements.txt
~~~

| 入口脚本                             | 命令        | 用途                                        | 教程                    |
|--------------------------------------|-------------|---------------------------------------------|-------------------------|
| scripts/cli.py                       | CLI / Shell | 统一入口、单次执行和交互 Shell              | [CLI 总览](cli.md)      |
| scripts/plugins/kconfig/kconfig.py   | kconfig     | 读取 Kconfig 和 .config，生成 mr_config.h   | [Kconfig](kconfig.md)   |
| scripts/plugins/htc/htc.py           | htc         | 把 .hts / .htsi 硬件描述编译为 mr_hardgen.h | [HTC](htc.md)           |
| scripts/plugins/rpc_idl/rpc_idl.py   | rpc-idl     | 把 .idl 编译为 RPC 头文件和源文件           | [RPC IDL](rpc-idl.md)   |
| scripts/plugins/platform/platform.py | platform    | 按 install.toml 安装、查看和卸载平台文件    | [Platform](platform.md) |
| scripts/plugins/sections/sections.py | sections    | 向 GNU ld 风格链接脚本写入注册段规则        | [Sections](sections.md) |
| scripts/plugins/mdk/mdk.py           | mdk         | 增删同步 MDK 工程文件，或重命名工程         | [MDK](mdk.md)           |
| scripts/plugins/commands/echo.py     | echo        | 原样打印参数，作为插件开发最小示例          | [Echo](echo.md)         |

scripts/framework/ 以及各插件的库代码子目录（HTC 的 frontend/、semantic/、validation/、backend/、common/， RPC IDL 的
frontend/、semantic/、backend/）都由命令脚本调用，没有独立的命令行参数，不需要也不应该
直接运行。想扩展命令时先阅读 [CLI 开发者指南](cli.md#8-架构)；插件必须在 scripts/plugins/catalog.py 中显式登记。

## 推荐使用顺序

接入 mrX 时按下面顺序执行：

1. 按 [平台文件安装与卸载](platform.md) 安装目标平台。
2. 按 [Kconfig](kconfig.md) 生成配置头文件。
3. 使用硬件树驱动时，按 [HTC](htc.md) 生成硬件描述头文件。
4. 使用 GNU ld 风格链接脚本时，按 [Sections](sections.md) 收集注册段；Keil MDK 工程按 [MDK](mdk.md) 维护工程文件。
5. 修改 .idl 后按 [RPC IDL](rpc-idl.md) 重新生成代码，并把生成的 idl.c 纳入编译。

生成的头文件、源文件和平台安装副本都应视为工具产物。修改输入文件后重新运行 相应命令，不要直接编辑生成文件来修复问题。

## 快速确认

在 mrX/ 下可以先查看命令是否已加载：

~~~text
python ./scripts/cli.py --version
python ./scripts/cli.py list
python ./scripts/cli.py help kconfig
~~~

--help（`-h`）、--version 等全局选项必须放在命令名之前。需要连续执行多个命令时，运行 python ./scripts/cli.py 不带命令进入交互模式（也可以写
`shell`，但它不是注册命令，不会出现在
`list` 中）；交互模式的 cd 只改变本次会话的路径，不会改变 Python 进程的工作目录。
