# echo 插件示例

## 用途

`echo`（`scripts/plugins/commands/echo.py`）是随 CLI
注册的最小内置插件命令，用于验证插件加载、参数解析和共享输出服务，也可作为新增自定义
CLI 命令的最小参考模板。

## 常用命令

```bash
python ./scripts/cli.py echo hello mrX
python ./scripts/cli.py echo "hello world" 123
```

不提供任何参数时输出一个空行并以退出码 `0` 结束。

## 参数说明

| 参数   | 类型                     | 说明                                           |
|--------|--------------------------|------------------------------------------------|
| `text` | 位置参数（可选，可多个） | 待打印的文本片段，按输入顺序以单个空格拼接输出 |

引号由外层操作系统 Shell 负责解析，CLI 收到的是已完成分割的参数列表。

## 输出与排错

命令通过 `CommandContext.console.write()` 将拼接后的字符串输出到标准输出，固定返回成功退出码
`0`。

新增类似插件时，在 `scripts/plugins/` 下实现 `Command` 子类并导出
`create_plugin(services)`，随后将包名加入 `scripts/plugins/catalog.py` 的
`BUILTIN_PLUGINS` 列表。注册完成后可通过以下命令验证：

```bash
python ./scripts/cli.py list
python ./scripts/cli.py help <new-command>
```
