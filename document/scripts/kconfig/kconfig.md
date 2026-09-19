# Kconfig 配置脚本

kconfig 读取 Kconfig 树和 .config，可选地打开 menuconfig，然后把规范化后的配置导出 为 C 头文件。它对应
scripts/plugins/kconfig/kconfig.py，不是固件中的运行时模块。

## 什么时候使用

平台安装完成后、编译 mrX 之前运行一次。平台目录可能带有自己的 Kconfig，先安装平台 再打开配置界面，才能在配置树中看到这些选项。修改
Kconfig 或 .config 后，都要重新 运行命令生成最新的 include/mrX/mr_config.h。

## 准备环境

在 mrX/ 目录安装脚本依赖：

~~~text
python -m pip install -r ./scripts/requirements.txt
~~~

默认交互流程还需要系统中可执行的 menuconfig。如果当前机器没有它，可以使用已有的 .config 配合 --no-ui；该模式不会打开界面。

## 基本操作

### 交互配置

~~~text
python ./scripts/cli.py kconfig
~~~

命令按项目根目录读取 Kconfig 和 .config，启动 menuconfig。在界面中修改选项并保存 退出后，脚本会再次读取配置并生成：

~~~text
include/mrX/mr_config.h
~~~

再次执行同一命令会加载上次保存的 .config，可以继续修改。脚本输出 Build: 行表示 头文件已写入。

### 非交互配置

自动化构建或没有终端界面时使用：

~~~text
python ./scripts/cli.py kconfig --no-ui
~~~

此时必须已有 .config；文件不存在会返回错误。直接编辑 .config 只适合自动化或批量 场景，Kconfig
的依赖和默认值仍由脚本重新解析，不能把手工写入的冲突状态当成最终结果。

### 自定义输入和输出

~~~text
python ./scripts/cli.py kconfig -k config/Kconfig -c config/board.config -o build/include/mr_config.h --no-ui
~~~

PowerShell 中也可以把命令写成多行。选项含义如下：

| 选项          | 默认值                  | 说明                              |
|---------------|-------------------------|-----------------------------------|
| -k, --kconfig | Kconfig                 | Kconfig 入口文件                  |
| -c, --config  | .config                 | 配置保存文件；--no-ui 下必须存在  |
| -o, --output  | include/mrX/mr_config.h | 生成头文件路径，父目录会自动创建  |
| --no-ui       | 关闭                    | 跳过 menuconfig，直接读取 .config |

未显式指定的默认路径相对 mrX 项目根目录解析；显式写出的相对路径相对当前会话 目录解析。交互 Shell 中可先执行 `pwd` 和
`cd <目录>` 检查或改变会话目录。

## 生成文件内容

脚本先调用 kconfiglib 规范化 .config，再生成 autoconf 风格内容，并做三项包装：

- 去掉宏名前缀 CONFIG_，因此配置 CONFIG_MR_USE_TIMER 会导出为 MR_USE_TIMER。
- 根据输出文件名生成 include guard。
- 写入生成时间和 C++ extern "C" 保护。

mr_config.h 是生成文件，不要手工修改；应修改 Kconfig 或 .config 后重新运行脚本。 输出文件所在目录不存在时会自动创建。

## 常见问题

| 现象                   | 处理方式                                                 |
|------------------------|----------------------------------------------------------|
| Kconfig file not found | 确认从 mrX/ 运行，或用 -k 指向实际入口文件               |
| menuconfig 无法启动    | 安装 menuconfig，或改用 --no-ui 读取已有 .config         |
| Config file not found  | 先运行一次交互配置生成 .config，或用 -c 指向已有配置     |
| 生成头文件没有预期选项 | 确认保存了 UI 修改，并检查 -k、-c、-o 是否指向同一套文件 |
| 平台选项缺失           | 先执行 platform install，再重新运行 Kconfig              |

## 脚本关系

kconfig.py 负责命令参数、UI 调用和文件输出；配置解析由 kconfiglib 完成，CLI 的路径 解析和输出由 scripts/framework/
提供。该命令成功退出码为 0，输入或生成失败为 1， 参数解析错误为 2。
