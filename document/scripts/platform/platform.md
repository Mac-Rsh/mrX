# Platform 平台文件脚本

## 用途

`platform`（`scripts/plugins/platform/platform.py`）根据平台描述文件
`install.toml` 中的复制规则，将芯片平台头文件和适配源码安装到 mrX
工作目录，并支持查看安装状态以及通过哈希校验安全卸载、恢复安装前的旧文件。关于
`mr_port_*` 移植契约本身，见 [平台移植层文档](../../platform/platform.md)。

## 常用命令

```bash
# 先预览安装计划，确认无误后再实际写入
python ./scripts/cli.py platform install -i arch/win/install.toml --dry-run
python ./scripts/cli.py platform install -i arch/win/install.toml

# 查看当前安装状态（不传 -i 时以当前会话目录为输出根）
python ./scripts/cli.py platform status
python ./scripts/cli.py platform status -i arch/win/install.toml

# 卸载当前平台并恢复安装前保存的文件
python ./scripts/cli.py platform uninstall
python ./scripts/cli.py platform uninstall --force
```

## 参数说明

| 选项            | 默认值    | 说明                                                               |
|-----------------|-----------|--------------------------------------------------------------------|
| `action`        | `install` | 位置参数：`install`、`uninstall`、`status`                         |
| `-i`, `--input` | 无        | 平台 `.toml` 描述文件；`install` 时必填，`status`/`uninstall` 可选 |
| `--dry-run`     | 关闭      | 仅解析并打印执行计划，不实际复制、删除或恢复文件                   |
| `--force`       | 关闭      | 卸载时忽略已安装文件被修改的警告，强制恢复或删除                   |

### `install.toml` 配置格式

```toml
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
```

- `[platform].name`：必填，用于清单记录与 `status` 展示。
- `[dir].src` / `[dir].dst`：相对 `install.toml` 所在目录解析；省略 `[dir]` 时均默认为
  `install.toml` 所在目录。
- `[[platform.add]]`：可重复定义；`src` 相对 `[dir].src`（支持单文件、目录或 `*`/
  `?`/`[` 通配符，目录与通配结果会递归保留相对结构），`dst` 相对 `[dir].dst`
  且必须严格位于输出目录内（禁止绝对路径或越界）。若多条规则命中同一目标路径，后解析的规则覆盖前者。

## 输出与排错

安装状态保存在输出根目录的 `.platform/` 目录中（`manifest.json` 记录当前平台与文件
SHA-256，`previous/` 保存安装前备份，`tmp/`
为事务临时目录），请勿手工删改该目录。同一输出目录下已有安装记录时，必须先执行
`platform uninstall` 再安装新平台。

| 常见现象                       | 排查方式                                                                       |
|--------------------------------|--------------------------------------------------------------------------------|
| `Missing platform file`        | `install` 动作必须通过 `-i` 指定 `install.toml`                                |
| `Platform already installed`   | 切换或重装平台前先执行 `platform uninstall`                                    |
| 找不到源文件或通配无匹配       | 以 `install.toml` 的 `[dir].src` 为基准检查 `[[platform.add]].src`             |
| `Skip: changed installed file` | 已安装文件在安装后被手工修改，确认可丢弃修改后加 `--force` 卸载                |
| 编译仍找不到平台头文件         | 用 `platform status` 核对输出根目录，并确认工程已收录安装后的 `platform/` 源码 |
