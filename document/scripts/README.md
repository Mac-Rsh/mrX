# 主机脚本（Scripts）

`scripts/` 存放构建前与工程维护用的 Python 工具，所有功能通过统一入口
`scripts/cli.py` 调用。

> 建议始终在 **mrX 根目录** 下执行命令。

## 安装依赖

```bash
python -m pip install -r ./scripts/requirements.txt
```

## 命令列表

| 命令               | 对应脚本                               | 用途                                        | 文档                                |
|--------------------|----------------------------------------|---------------------------------------------|-------------------------------------|
| `cli` / 交互 Shell | `scripts/cli.py`                       | 统一命令行入口与交互式 Shell                | [cli.md](cli/cli.md)                |
| `kconfig`          | `scripts/plugins/kconfig/kconfig.py`   | 读取 Kconfig / `.config` 生成 `mr_config.h` | [kconfig.md](kconfig/kconfig.md)    |
| `htc`              | `scripts/plugins/htc/htc.py`           | 将 `.hts` / `.htsi` 编译为 `mr_hardgen.h`   | [htc.md](htc/htc.md)                |
| `rpc-idl`          | `scripts/plugins/rpc_idl/rpc_idl.py`   | 将 `.idl` 编译为 RPC 收发代码               | [rpc-idl.md](rpc-idl/rpc-idl.md)    |
| `platform`         | `scripts/plugins/platform/platform.py` | 安装、查看和卸载平台适配文件                | [platform.md](platform/platform.md) |
| `sections`         | `scripts/plugins/sections/sections.py` | 向 GNU ld 链接脚本注入注册段规则            | [sections.md](sections/sections.md) |
| `mdk`              | `scripts/plugins/mdk/mdk.py`           | 同步或重命名 Keil MDK 工程文件              | [mdk.md](mdk/mdk.md)                |
| `echo`             | `scripts/plugins/commands/echo.py`     | 参数回显（最小插件开发示例）                | [echo.md](echo/echo.md)             |

`scripts/framework/` 以及各插件内部的辅助子目录（如 `frontend/`、`backend/`
）均为内部库，不能直接作为命令运行。

## 常见使用顺序

1. **安装平台**：`python ./scripts/cli.py platform install ...`
2. **生成配置**：`python ./scripts/cli.py kconfig`
3. **编译硬件描述**（可选）：`python ./scripts/cli.py htc -i board.hts`
4. **处理链接脚本**（GNU ld）：`python ./scripts/cli.py sections -i link.lds`
5. **生成 RPC 代码**（使用 RPC 时）：
   `python ./scripts/cli.py rpc-idl -i service.idl`

生成的头文件、C 文件和已安装的平台副本都是工具产物，修改输入文件后重新运行命令即可，不要直接手改生成文件。

## 常用命令行技巧

```bash
python ./scripts/cli.py --version       # 查看版本（全局选项需放在子命令之前）
python ./scripts/cli.py list            # 列出所有可用命令
python ./scripts/cli.py help kconfig    # 查看指定命令的帮助
python ./scripts/cli.py                 # 进入交互模式（支持 cd，仅影响当前会话）
```
