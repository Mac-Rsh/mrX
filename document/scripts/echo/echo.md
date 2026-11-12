# echo 最小插件示例

echo 是随 CLI 注册的最小插件命令，对应 scripts/plugins/commands/echo.py。它的作用是
验证插件加载、参数解析和共享输出服务，也可作为新增命令的最小参考。

## 用法

~~~text
python ./scripts/cli.py echo hello mrX
~~~

输出：

~~~text
hello mrX
~~~

命令接收零个或多个位置参数，并按原顺序用一个空格连接后输出：

~~~text
python ./scripts/cli.py echo "hello world" 123
~~~

会输出一行 hello world 123。不提供参数时输出空行并以成功码退出。引号由当前操作系统
Shell 负责处理，CLI 收到的是已经分割好的参数。

## 插件开发提示

该示例展示三个约定：

1. 继承 framework.command.Command 并在 CommandSpec 中声明名称和描述。
2. 在 configure () 中只定义一次参数；同一 parser 同时用于帮助、解析和补全。
3. 在 execute () 中通过 CommandContext.console 输出，并返回 CommandResult，不直接调用
   print ()。

新增插件后，在 scripts/plugins/catalog.py 的 BUILTIN_PLUGINS 中加入包名，并让包导出
create_plugin (services)。
完成后可用下面的命令确认注册成功：

~~~text
python ./scripts/cli.py list
python ./scripts/cli.py help <new-command>
~~~
