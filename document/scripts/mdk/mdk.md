# Keil MDK 工程维护脚本

mdk 维护 Keil uVision 的 .uvprojx 或 .uvproj XML 工程：向指定 Target 添加或删除源文件、按磁盘内容同步工程，或重命名工程文件组。命令入口是
scripts/plugins/mdk/mdk.py，会直接 更新工程文件，请在执行前让工程文件处于可恢复的版本控制状态。

## 命令格式

~~~text
python ./scripts/cli.py mdk <add|del|sync|rename> -p <project.uvprojx> [options]
~~~

| 选项                | 用于           | 说明                                   |
|---------------------|----------------|----------------------------------------|
| -p, --project       | 全部动作       | 必填，.uvprojx 或 .uvproj              |
| -s, --source        | add、del、sync | 源文件或目录                           |
| -n, --name          | rename         | 新工程/Target 名称，必填               |
| -t, --target        | 全部相关动作   | 多 Target 工程中指定 Target 名         |
| -g, --group         | add、del、sync | 根分组名称；省略时目录操作启用自动分组 |
| --keep-empty-groups | del、sync      | 删除文件后保留空分组                   |

相对路径相对当前会话目录，工程中的 FilePath 会按工程文件所在目录保存。工程文件必须 已经存在且 XML 结构包含
Targets/Target；多 Target 工程不指定 -t 会被拒绝，以免误改错误目标。

## 接入 mrX 的注意事项

MDK 工程分组应登记参与编译的编译单元（例如 `*.c`、`*.cpp`、`*.s`），不需要把头文件逐个加入工程。接入 mrX 时，只对源码目录使用
`add` 或 `sync`，不要把 `include/` 作为 `-s` 目录；`include/` 中的头文件应通过 MDK 的编译器 Include Paths 使用。

在 MDK 的 **Options for Target → C/C++ → Include Paths** 中添加 `mrX/include`，不要添加 `mrX/include/mrX`。这是因为 mrX
的公开头文件按
`#include <mrX/...>` 引用。`mdk` 命令只维护工程 XML 中的 File 条目，不负责配置 Include Paths。

mrX 的常用源码目录为 `kernel`、`libc`、安装后的 `platform`、`drivers`、`modules` 和 `fs`；`test` 仅在启用测试框架时按需加入。
`arch/` 是平台适配仓库，
`scripts/` 是主机端工具，均不应加入固件工程。

### 注册段与链接器

mrX 的注册段（initcall、driver、RPC、testcase）在 MDK 下不需要任何额外链接器配置：注册条目带有
`used` 属性，armlink 不会裁剪它们；内核以 `initcall.c` 自身导出的段首尾哨兵定界遍历 （`MR_INIT_EXPORT(start, "0.end")` 与
`MR_INIT_EXPORT(end, "9.end")`，其变量名即
`__mr_initcall_start` / `__mr_initcall_end`），不依赖链接器生成边界符号；而 armlink 默认把同类只读段按名字连续、字典序放置，天然满足注册段的收集、排序需求。

因此 MDK 工程不需要 scatter 文件，也不需要 `--keep` 等链接器选项，按 Target 对话框的默认内存布局链接即可；同时不要对 MDK
工程执行 `sections` 命令或套用 GNU ld 规则。 若工程因其他原因自建 scatter，需保证其中包含等效的收集规则（如
`*(mr_initcall.*)`）。

## 添加文件：add

添加单个文件并指定分组：

~~~text
python ./scripts/cli.py mdk add -p app.uvprojx -s src/main.c -g Sources
~~~

添加源码目录并把其中所有可见文件放入同一根分组：

~~~text
python ./scripts/cli.py mdk add -p app.uvprojx -s src -g Sources
~~~

目录模式会递归扫描文件，子目录会转成 `Sources/<relative-directory>` 分组。省略 `-g` 时启用自动分组：脚本按源目录的首层可见子目录创建分组，例如
src/drivers 会进入 drivers（或指定根组 下的 根组/drivers）。自动分组要求源目录有可见子目录；源目录顶层的直接文件不会进入自动
分组，如需收录它们请显式指定 -g。

隐藏文件和包含隐藏路径段的文件不会被自动分组，也不能作为添加源路径。已在工程中存在的 路径会输出
Skip，不会重复添加；不同文件但同名是允许的，因为去重依据是完整路径。

## 删除文件：del

删除一个文件：

~~~text
python ./scripts/cli.py mdk del -p app.uvprojx -s src/old.c
~~~

删除一个目录下已登记的所有文件：

~~~text
python ./scripts/cli.py mdk del -p app.uvprojx -s src/drivers -g Sources
~~~

脚本只删除工程 XML 中的 File 条目，不删除磁盘源文件。匹配不到的路径会输出 Skip 警告； 删除后默认移除没有文件的
Group，需要保留分组时加 --keep-empty-groups。

## 同步文件：sync

sync 同时做两件事：把源目录中尚未登记的文件加入工程，并删除工程中指向磁盘上已不存在 文件的条目：

~~~text
python ./scripts/cli.py mdk sync -p app.uvprojx -s src -g Sources
~~~

它不会删除磁盘文件，也不会清理源目录中没有被工程引用的文件。-s 目录不存在时，同步不会 新增文件，但可以清理该范围内工程中已失效的条目；若要保留被清理后的空组，加
--keep-empty-groups。

## 重命名工程：rename

~~~text
python ./scripts/cli.py mdk rename -p app.uvprojx -n demo
~~~

重命名会在同一目录生成 demo.uvprojx，并更新选中 Target 的 TargetName 和输出名。如果旁边 存在对应的 .uvoptx（.uvproj 对应
.uvopt），也会一起重命名并更新其中的 Target 名；匹配的 .uvguix.* / .uvgui.* 用户界面文件也会随工程文件组移动。

多 Target 工程必须指定要重命名的 -t；新名字不能与另一个 Target 冲突。rename 不需要 -s， 也不会重命名源代码文件。

## 文件类型和输出

添加文件时脚本根据后缀填写 MDK FileType：.c 为 C、.cpp/.cc/.cxx 为 C++、.s/.sx/.asm 为 汇编、.o/.obj 为对象文件、.a/.lib
为库，其余类型使用通用值。每次有修改时会以 UTF-8 XML 写回工程并输出 `Build: <project>`；如果请求不会造成变化，工程不会被重复写入。

## 常见问题

| 现象                         | 处理方式                                                                |
|------------------------------|-------------------------------------------------------------------------|
| Unsupported MDK project file | 只支持 .uvprojx 和 .uvproj                                              |
| Multiple targets found       | 用 `-t` 写出准确的 `<TargetName>`                                       |
| Missing source path          | add / del / sync 必须提供 -s                                            |
| 自动分组找不到文件           | 自动分组只看首层可见子目录；需要处理根目录文件时加 -g                   |
| 文件已存在被跳过             | 这是路径去重保护；确认工程中的 FilePath 是否已经指向该源文件            |
| 删除后分组消失               | 默认会清理空组；使用 --keep-empty-groups 保留                           |
| 重命名失败                   | 检查新 Target 名称冲突、同名目标文件是否已存在，以及旁边的 MDK 配套文件 |

## 脚本关系

mdk.py 只维护工程 XML 和工程文件组，不参与平台安装、注册段注入或固件编译。它会使用临时 文件和回滚路径降低写入中断风险，但仍建议在批量修改前保留工程备份。
