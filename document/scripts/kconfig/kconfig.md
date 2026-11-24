# Kconfig 配置脚本

## 用途

`kconfig`（`scripts/plugins/kconfig/kconfig.py`）基于 `kconfiglib` 读取 `Kconfig`
树与 `.config` 文件，可选地启动 `menuconfig` 交互界面，并将规范化后的配置导出为 C
头文件（默认 `include/mrX/mr_config.h`）。通常在安装好平台文件后、编译固件前执行；每当修改
`Kconfig` 或 `.config` 后都需要重新运行以更新头文件。

## 常用命令

启动 `menuconfig` 交互配置并生成头文件：

```bash
python ./scripts/cli.py kconfig
```

跳过交互界面，直接根据现有 `.config` 生成头文件（适用于 CI 或无终端环境）：

```bash
python ./scripts/cli.py kconfig --no-ui
```

自定义输入与输出路径：

```bash
python ./scripts/cli.py kconfig -k config/Kconfig -c config/board.config -o build/include/mr_config.h --no-ui
```

## 参数说明

| 选项              | 默认值                      | 说明                                            |
|-------------------|-----------------------------|-------------------------------------------------|
| `-k`, `--kconfig` | `Kconfig`                   | 入口 `Kconfig` 文件路径                         |
| `-c`, `--config`  | `.config`                   | 配置文件路径；`--no-ui` 模式下必须已存在        |
| `-o`, `--output`  | `./include/mrX/mr_config.h` | 输出头文件路径，父目录不存在时自动创建          |
| `--no-ui`         | 关闭                        | 跳过 `menuconfig` 界面，直接读取 `.config` 导出 |

未显式指定选项时，默认路径相对于仓库根目录 `mrX/` 解析；显式传入的相对路径相对于当前会话目录解析。直接修改
`.config` 时，脚本仍会按 `Kconfig` 的依赖规则重新规范化并写回 `.config`
，手工写入的冲突项不会保留。

## 输出与排错

命令执行成功后输出 `Build: <输出路径>`。导出头文件时脚本会执行三项处理：移除
`CONFIG_` 宏前缀（如 `CONFIG_MR_USE_TIMER` 导出为 `MR_USE_TIMER`）、根据输出文件名生成
include guard，以及补充生成时间戳和 `extern "C"` 保护。`mr_config.h`
为自动生成文件，不应手工修改。

| 常见现象                 | 排查方式                                                             |
|--------------------------|----------------------------------------------------------------------|
| `Kconfig file not found` | 确认在 `mrX/` 目录下执行，或通过 `-k` 指定正确的 `Kconfig` 路径      |
| `menuconfig` 无法启动    | 安装 `menuconfig`（见 `scripts/requirements.txt`），或改用 `--no-ui` |
| `Config file not found`  | 先不带 `--no-ui` 运行一次并保存配置，或用 `-c` 指定已有 `.config`    |
| 平台相关配置项缺失       | 先执行 `platform install` 安装平台 Kconfig，再重新运行 `kconfig`     |
| 生成结果未体现新修改     | 确认在 `menuconfig` 中已保存退出，且 `-k`/`-c`/`-o` 指向同一套文件   |
