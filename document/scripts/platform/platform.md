# Platform 平台文件脚本

platform 按一个平台 install.toml 中的复制规则，把平台头文件和源码安装到 mrX 的工作目录； 它也负责查看安装状态和恢复安装前的文件。命令入口是
scripts/plugins/platform/platform.py。 平台移植契约和新芯片开发请继续阅读 [平台移植层文档](../../platform/platform.md)。

## 命令格式

~~~text
python ./scripts/cli.py platform <install|uninstall|status> [options]
~~~

动作省略时默认为 install。选项如下：

| 选项                           | 说明                                        |
|--------------------------------|---------------------------------------------|
| `-i`, `--input <install.toml>` | 平台描述文件；安装时必填，查看/卸载时可省略 |
| --dry-run                      | 只解析并打印计划，不复制、删除或恢复文件    |
| --force                        | 卸载时即使安装文件已被修改也强制恢复        |

### 安装已有平台

仓库自带 Windows 宿主示例：

~~~text
python ./scripts/cli.py platform install -i arch/win/install.toml
python ./scripts/cli.py platform status
~~~

安装前建议先查看计划：

~~~text
python ./scripts/cli.py platform install -i arch/win/install.toml --dry-run
~~~

计划会列出平台名称、源目录、输出目录、状态目录以及每个 Copy 和可能的 Save。确认后去掉 --dry-run
执行安装。同一输出目录已有安装状态时，脚本会拒绝再次安装；切换平台前先卸载 当前平台。

### 查看状态

~~~text
python ./scripts/cli.py platform status
python ./scripts/cli.py platform status -i arch/win/install.toml
~~~

不带 -i 时以当前会话目录为输出根；带 -i 时从该 TOML 的 [dir].dst 推导输出根。状态会 显示平台名称、状态目录、已安装文件数，以及文件被安装后发生过修改的数量。

### 卸载并恢复旧文件

~~~text
python ./scripts/cli.py platform uninstall
~~~

卸载会恢复安装前存在的文件，并删除安装时新建的文件和空目录。脚本会比较安装清单中的 SHA-256；如果安装副本后来被编辑，会先列出
Skip: changed installed file 并拒绝卸载。确认 要覆盖这些修改时才使用：

~~~text
python ./scripts/cli.py platform uninstall --force
~~~

也可以先用 --dry-run 查看将要恢复和删除的路径。

## 编写 install.toml

最小结构如下：

~~~toml
[platform]
name = "vendor,chip"

[dir]
src = "."
dst = "./../../.."

[[platform.add]]
src = "include/platform"
dst = "include/mrX/platform"

[[platform.add]]
src = "platform"
dst = "platform"
~~~

字段规则：

- [platform].name 必填，用于状态显示和清单记录。
- [dir].src、[dir].dst 相对 install.toml 所在目录解析；省略 [dir] 时源目录和输出目录都默认为 TOML 所在目录。
- [[platform.add]] 可重复；每项的 src 相对 [dir].src，dst 相对 [dir].dst。
- src 可以是单文件、目录或带 *、?、[ 的通配路径。目录和通配结果会递归收集文件并保留相对结构；单文件复制到目标文件，若目标已是目录则使用源文件名。
- 所有目标必须位于输出目录内，绝对路径和越出输出目录的目标会被拒绝。

源文件不存在、TOML 缺少必填字段、通配模式没有匹配项或目标路径不安全时，安装会在复制前
失败。多个规则指向同一目标时，后解析的规则覆盖前一条计划中的条目，因此应避免重叠规则。

## 状态和安全边界

安装状态位于输出根的 .platform/：

~~~text
.platform/manifest.json   # 当前平台和已安装文件哈希
.platform/previous/       # 安装前的文件和清单
.platform/tmp/            # 正在安装时的临时状态
~~~

安装和卸载都采用临时目录及清单交换方式；中断或复制失败时会尝试恢复旧状态。不要手工 删除或修改 .platform/，若遗留
tmp/，先按错误提示处理后再安装。

## 常见问题

| 现象                       | 处理方式                                                         |
|----------------------------|------------------------------------------------------------------|
| Missing platform file      | 安装动作必须提供 -i install.toml                                 |
| Platform already installed | 先执行 platform uninstall 再切换或重装                           |
| 找不到源文件               | 以 TOML 的 [dir].src 为基准检查 [[platform.add]].src             |
| 卸载拒绝继续               | 先备份或确认已安装副本的修改，再使用 --force                     |
| 安装后仍找不到平台头文件   | 检查 status 的输出目录，并确保用户工程收集的是安装后的 platform/ |

## 脚本关系

platform.py 负责 TOML 校验、复制计划、清单和恢复；具体的 mr_port_* 接口实现仍属于平台 源码。平台命令不会修改 Kconfig，也不会处理
GNU ld 注册段；这些步骤分别见 [Kconfig](../kconfig/kconfig.md)
和 [Sections](../sections/sections.md)。
