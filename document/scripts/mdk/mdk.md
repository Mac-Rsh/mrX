# Keil MDK 工程维护脚本

## 用途

`mdk`（`scripts/plugins/mdk/mdk.py`）用于维护 Keil uVision 工程（`.uvprojx` /
`.uvproj`）的源文件条目与分组：支持按文件或目录添加（`add`）、删除（`del`）、双向同步（
`sync`）以及工程重命名（`rename`）。命令会直接修改工程 XML，操作前建议保持工程处于
Git 等版本控制下。

### 接入 mrX 工程的约定

- 只对编译单元目录（如 `kernel/`、`libc/`、安装后的 `platform/`、`drivers/`、
  `modules/`、`fs/`，以及按需加入的 `test/`）执行 `add` 或 `sync`， **不要把
  `include/` 加入工程文件组**，也不要加入 `arch/` 和 `scripts/`。
- 头文件搜索路径在 MDK 的 **Options for Target → C/C++ → Include Paths** 中手动配置为
  `mrX/include`（不是 `mrX/include/mrX`，因为源码使用 `#include <mrX/...>`）。
- 注册段（`initcall`、`driver`、`rpc`、`testcase`）在 MDK 下无需配置 scatter 文件或
  `--keep`：注册条目带 `used` 属性，armlink 默认按段名字典序连续放置同类只读段，
  `initcall.c` 自身通过首尾哨兵符号定界。 **不要对 MDK 工程执行 `sections`
  命令**。

## 常用命令

```bash
# 添加单个文件到指定分组
python ./scripts/cli.py mdk add -p app.uvprojx -s src/main.c -g Sources

# 递归添加目录并自动按首层子目录分组（省略 -g）
python ./scripts/cli.py mdk add -p app.uvprojx -s mrX/kernel

# 从工程中移除文件或目录条目（不删除磁盘文件）
python ./scripts/cli.py mdk del -p app.uvprojx -s src/old.c

# 同步目录：补充新增文件并清理磁盘上已不存在的条目
python ./scripts/cli.py mdk sync -p app.uvprojx -s src -g Sources

# 重命名工程及 Target（同步处理配套的 .uvoptx 和 .uvguix.*）
python ./scripts/cli.py mdk rename -p app.uvprojx -n demo
```

## 参数说明

| 选项                  | 适用动作             | 说明                                                   |
|-----------------------|----------------------|--------------------------------------------------------|
| `action`              | 全部                 | 位置参数：`add`、`del`、`sync`、`rename`               |
| `-p`, `--project`     | 全部                 | 必填，目标 `.uvprojx` 或 `.uvproj` 工程路径            |
| `-s`, `--source`      | `add`、`del`、`sync` | 源文件或源码目录路径                                   |
| `-n`, `--name`        | `rename`             | 必填，新工程及 Target 名称                             |
| `-t`, `--target`      | 全部                 | 指定 Target 名称；多 Target 工程必须显式指定           |
| `-g`, `--group`       | `add`、`del`、`sync` | 根分组名；目录模式省略 `-g` 时按首层可见子目录自动分组 |
| `--keep-empty-groups` | `del`、`sync`        | 移除文件后保留空分组（默认会清理空分组）               |

### 分组与类型映射规则

- **自动分组**：目录模式下省略 `-g` 时，脚本按源目录的 **首层可见子目录**建组（如
  `src/drivers` 进入 `drivers`）；源目录顶层的直接文件不会进入自动分组，需要收录顶层文件时请显式指定
  `-g`。隐藏文件及包含隐藏路径段的目录会被跳过。
- **文件类型**：根据扩展名自动映射 MDK `FileType`：`.c`（C）、`.cpp`/`.cc`/`.cxx`
  （C++）、`.s`/`.sx`/`.asm`（汇编）、`.o`/`.obj`（目标文件）、`.a`/`.lib`
  （库文件），其余扩展名使用通用类型。
- **去重与删除边界**：按工程文件所在目录的相对路径去重（不同目录下的同名文件可共存）。
  `del` 与 `sync` 仅修改 XML 条目，不会删除磁盘上的源文件。

## 输出与排错

当工程发生实际变更时，命令以 UTF-8 格式写回 XML 并输出 `Build: <project>`
；若请求未引起任何变化，工程文件不会被重写。

| 常见现象                       | 排查方式                                                                       |
|--------------------------------|--------------------------------------------------------------------------------|
| `Unsupported MDK project file` | 确认工程文件后缀为 `.uvprojx` 或 `.uvproj`，且 XML 中包含 `Targets/Target`     |
| `Multiple targets found`       | 多 Target 工程必须通过 `-t <TargetName>` 指定目标                              |
| `Missing source path`          | `add`、`del`、`sync` 动作均需提供 `-s`                                         |
| 自动分组漏掉顶层文件           | 自动分组只扫描首层可见子目录；含顶层文件时请传入 `-g`                          |
| 出现 `Skip` 提示               | 目标路径已在工程中（`add`）或未在工程中登记（`del`），属于正常去重/保护        |
| 删除后分组消失                 | 默认自动清理空组，如需保留请加上 `--keep-empty-groups`                         |
| `rename` 失败                  | 检查新 Target 名是否与其他 Target 冲突，或目标 `.uvprojx`/`.uvoptx` 是否已存在 |
