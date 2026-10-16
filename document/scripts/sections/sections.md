# Sections 注册段脚本

sections 为使用 GNU ld 风格链接脚本的工程，把 mrX 的 initcall、测试、驱动和 RPC 注册段 收集到指定输出段中。命令入口是
scripts/plugins/sections/sections.py，会原地修改链接脚本。 Keil MDK 工程不需要使用此命令， armlink 默认即按名字连续放置这些注册段。

## 基本操作

~~~text
python ./scripts/cli.py sections -i path/to/app.ld
~~~

选项：

| 选项         | 默认值        | 说明                                     |
|--------------|---------------|------------------------------------------|
| -i, --input  | 必填          | 目标 GNU ld 风格链接脚本；命令会原地写回 |
| -c, --config | sections.toml | 段描述 TOML；默认相对项目根目录          |

自定义段描述：

~~~text
python ./scripts/cli.py sections -i build/board.ld -c config/sections.toml
~~~

命令要求输入中存在顶层 SECTIONS { ... } 块。找不到、括号不匹配、配置无效或文件不可读 时会失败；成功时输出
`Build: <linker script>`。对同一输入重复执行会先移除由 mrX 标记的 旧规则，再按当前 TOML 重写，因此可以在生成步骤中重复调用。

## sections.toml 格式

仓库默认文件示例：

~~~toml
[[section]]
name = "initcall"
input = "mr_initcall.*"
output = ".text"
keep = true
sort = true
comment = "Initcall"

[[section]]
name = "rpc"
input = "mr_rpc.*"
output = ".text"
keep = true
sort = true
comment = "Rpc"
~~~

每个 [[section]] 都必须提供 name、input、output、keep 和 sort；comment 可省略。字段含义：

| 字段    | 说明                                      |
|---------|-------------------------------------------|
| name    | 规则名称，用于标记和排查                  |
| input   | 输入段通配模式，例如 mr_initcall.*        |
| output  | GNU ld 输出段名，例如 .text               |
| keep    | true 包装为 KEEP(...)，避免链接器垃圾回收 |
| sort    | true 包装为 SORT(...)，按链接器规则排序   |
| comment | 生成到链接脚本中的可读说明；缺省使用 name |

规则会按 output 分组写入对应输出段。每条规则的生成形式：

~~~text
keep = true, sort = true   -> KEEP(*(SORT(mr_initcall.*)))
keep = true, sort = false  -> KEEP(*(mr_initcall.*))
keep = false, sort = true  -> *(SORT(mr_initcall.*))
keep = false, sort = false -> *(mr_initcall.*)
~~~

生成条目前会写入 `/* @mrX: <comment> */` 标记。脚本只重写这些已标记条目，并保留同一 输出段中的用户规则；修改 TOML
后再次运行即可刷新规则。

## 与链接流程的关系

注册宏把函数或描述放入 `mr_initcall.*`、`mr_driver.*`、`mr_rpc.*` 等输入段。链接脚本必须 同时做到：

1. 用 KEEP 保留这些段，避免 --gc-sections 丢弃注册项。
2. 把它们聚合到可执行映像中的输出段。
3. 按需要用 SORT 保证初始化级别或注册顺序稳定。

因此应在最终交给 GNU ld 的链接脚本上运行命令，且运行后不要让后续生成步骤覆盖这些修改。 非 GNU ld
工具链请按该工具链语法手工实现同等的保留、聚合和排序规则。

## 常见问题

| 现象                          | 处理方式                                                                                 |
|-------------------------------|------------------------------------------------------------------------------------------|
| Section config file not found | 从 mrX/运行，或通过 -c 指定正确 TOML                                                     |
| Missing table: [[section]]    | 确认使用数组表 [[section]]，不是 [section]                                               |
| Invalid field ... keep/sort   | TOML 中必须使用布尔值 true / false                                                       |
| 找不到 SECTIONS { ... }       | 把 -i 改为最终 GNU ld 链接脚本，确认存在顶层 SECTIONS                                    |
| 规则重复                      | 先确认旧条目带有 @mrX: 标记；用户手写的同名规则不会被删除                                |
| MDK 工程也执行了此命令        | MDK 不要把 GNU ld 规则套到 MDK，armlink 默认放置即可，详见 [MDK 工程维护](../mdk/mdk.md) |

## 脚本关系

sections.py 只解析 TOML 和链接脚本文本，不调用编译器或链接器。它处理的是注册段收集规则，
和 [Platform](../platform/platform.md)、[MDK](../mdk/mdk.md) 的文件工程维护互相独立。
