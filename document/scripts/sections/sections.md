# Sections 注册段脚本

## 用途

`sections`（`scripts/plugins/sections/sections.py`）用于为 GNU ld 风格链接脚本（
`.ld`）自动注入或刷新 mrX 的托管段收集规则（如 `mr_initcall.*`、`mr_rpc.*` 等），确保启用
`--gc-sections` 时注册条目不被裁剪且按段名排序聚合。Keil MDK 工程由 armlink
默认按段名连续放置同类只读段， **不要对 MDK 工程使用此命令**。

## 常用命令

```bash
# 使用仓库默认的 sections.toml 更新指定链接脚本
python ./scripts/cli.py sections -i path/to/app.ld

# 使用自定义段描述文件
python ./scripts/cli.py sections -i build/board.ld -c config/sections.toml
```

## 参数说明

| 选项             | 默认值          | 说明                                           |
|------------------|-----------------|------------------------------------------------|
| `-i`, `--input`  | 必填            | 目标 GNU ld 风格链接脚本，命令会原地修改该文件 |
| `-c`, `--config` | `sections.toml` | 段描述 TOML 文件，默认相对于仓库根目录解析     |

### `sections.toml` 配置格式

```toml
[[section]]
name = "initcall"
input = "mr_initcall.*"
output = ".text"
keep = true
sort = true
comment = "Initcall"
```

每个 `[[section]]` 必须包含 `name`、`input`、`output`、`keep`（bool）和 `sort`（bool），
`comment` 可省略（默认回退为 `name`）。脚本按 `output` 分组将规则插入对应输出段：

- `keep = true, sort = true` → `KEEP(*(SORT(mr_initcall.*)))`
- `keep = true, sort = false` → `KEEP(*(mr_initcall.*))`
- `keep = false, sort = true` → `*(SORT(mr_initcall.*))`
- `keep = false, sort = false` → `*(mr_initcall.*)`

## 输出与排错

脚本生成规则时会在前一行附带 `/* @mrX: <comment> */` 标记。重复执行命令时，脚本仅清理并重写带有
`@mrX:` 标记的托管条目，同一输出段内手写的其他规则会被完整保留。成功时输出
`Build: <linker script>`。

| 常见现象                          | 排查方式                                                              |
|-----------------------------------|-----------------------------------------------------------------------|
| `Section config file not found`   | 确认在 `mrX/` 下运行，或用 `-c` 指定正确的 TOML 路径                  |
| `Missing table: [[section]]`      | 确认 TOML 使用的是数组表 `[[section]]` 而非 `[section]`               |
| `Failed to find SECTIONS { ... }` | 确保 `-i` 指向最终交给 GNU ld 的 `.ld` 文件且包含顶层 `SECTIONS` 块   |
| 规则重复出现                      | 检查旧规则是否缺少 `/* @mrX: ... */` 标记；手写同名规则不会被自动删除 |
