# Windows 平台适配

面向 Windows 主机仿真开发与单元测试的平台包，提供高精度时钟、中断临界区、延迟中断线程、诊断输出、XTrace 文件输出以及常用外设的宿主仿真驱动（UART / FLASH / GPIO / ADC / I2C / SPI）。

所有外设仿真驱动既支持通过 HardTree（`.hts`）按标准 `compatible` 自动实例化，也提供 `mr_sim_*` 辅助接口（`<mrX/platform/mr_win_sim.h>`，由 `<mrX/platform/mr_platform.h>` 自动引入），方便在 C 单元测试框架中直接创建、注入激励和校验状态。

| Kconfig | 说明 |
| --- | --- |
| `MR_USE_WIN` | 固定启用 |
| `MR_USE_WIN_IRQ_DEFER_HOOK` | 启用宿主线程驱动的延迟中断执行（依赖 `MR_USE_IRQ_DEFER`） |
| `MR_USE_WIN_UART_TCP` | 启用 UART 的 Winsock2 TCP 监听后端（默认 `y`，MinGW 需链接 `-lws2_32`，MSVC 自动链接 `ws2_32.lib`） |
| `MR_CFG_WIN_UART_RX_BUF_SIZE` | 每个 Windows UART 实例的内部接收环形缓冲大小（字节），默认 4096 |

## 安装与构建

```powershell
python ./scripts/cli.py platform install -i ./arch/win/install.toml
python ./scripts/cli.py kconfig
```

使用 MinGW / GCC 编译且启用 `MR_USE_WIN_UART_TCP` 时，在链接参数中加入 `-lws2_32`。

## UART（`win,uart`）

支持三种工作模式：

- **Stdio 模式（默认，`MR_SIM_UART_MODE_STDIO`）**：`write` 输出到 `stdout` 并立即刷新，后台线程非阻塞读取 `stdin` 到接收环形缓冲并上报 `MR_UART_WAIT_RX_READY`。
- **TCP 监听模式（`MR_SIM_UART_MODE_TCP`）**：自动监听 `host:port`（默认 `127.0.0.1`），接受主机端工具（如 RPC 客户端、调试终端等）直连，断线后自动接受下一次重连；`port = 0` 时由系统分配临时端口，可通过 `mr_sim_uart_tcp_port()` 查询。
- **内存缓冲模式（`MR_SIM_UART_MODE_MEM`）**：纯内存收发环形缓冲，配合 `mr_sim_uart_rx_push()` / `mr_sim_uart_tx_pop()` 在单元测试中无阻塞验证协议栈。

TX stream 完整写入后才上报完成。内存模式下，请求超过发送环形缓冲容量时返回
`-MR_ERANGE`，当前可用空间不足时返回 `-MR_EAGAIN`，这两种情况下均不入队。
Stdio 或 TCP 返回短写时，TX stream 返回 `-MR_EIO`，可通过
`mr_uart_tx_stream_offset()` 查询后端报告的已写入字节数。

HardTree 属性（均可选）：

| 属性 | 默认值 | 说明 |
| --- | --- | --- |
| `win,port` | `0` | 非 0 时自动启用 TCP 监听模式并绑定该端口 |
| `win,host` | `"127.0.0.1"` | TCP 监听地址 |
| `win,mode` | 自动推导 | 显式指定模式（`0`=stdio，`1`=tcp，`2`=mem） |

## FLASH（`win,flash`）

使用宿主文件持久化模拟 NOR Flash：

- **真实擦写规则**：新建或扩容文件自动以 `erased_byte`（默认 `0xFF`）填充；`erase` 强制 `erase_size` 对齐；`program` 强制 `program_size` 对齐且不隐式擦除——`0xFF` 擦除态下只允许 `1 -> 0` 位翻转，`strict = MR_TRUE` 时非法 `0 -> 1` 编程返回 `-MR_EIO`。
- **掉电注入（Boot 测试）**：调用 `mr_sim_flash_power_loss_arm(drv, op_countdown, partial_bytes, cb, user_data)` 可在第 N 次写/擦除时仅落盘前 `partial_bytes` 字节后触发掉电，支持注册回调（如在回调中 `longjmp` 模拟瞬间复位）或返回 `-MR_EIO`。

HardTree 属性（均可选）：`win,path`（默认 `"win_flash.bin"`）、`win,size`（默认 256 KiB）、`win,erase_size`（默认 4096）、`win,program_size`（默认 4）、`win,page_size`（默认 256）、`win,erased_byte`（默认 `0xFF`）、`win,strict`（默认 `MR_TRUE`）。

## GPIO（`win,gpio`）与 ADC（`win,adc`）

均基于线程安全的内存状态表实现，并开放测试辅助函数：

- **GPIO**：`mr_sim_gpio_init()`、`mr_sim_gpio_set()`、`mr_sim_gpio_get()`、`mr_sim_gpio_port_set()`、`mr_sim_gpio_port_get()`。调用 `mr_sim_gpio_set()` 改变输入电平时，会自动按引脚配置的 `MR_GPIO_MODE_IRQ_*` 条件触发 `MR_GPIO_WAIT_EXTI` 事件。
- **ADC**：`mr_sim_adc_init()`、`mr_sim_adc_set()`、`mr_sim_adc_get()`、`mr_sim_adc_set_millivolt()`，启用 `MR_USE_DRIVER_ADC_STREAM` 时还提供 `mr_sim_adc_stream_step()` 用于步进流采样并触发 `HALF`/`FULL` 事件。

## I2C（`win,i2c`）与 SPI（`win,spi`）

提供轻量级虚拟 Master 总线，按需用 C 编写简单假器件回调并挂载到指定地址或片选，应用侧仍按标准 `mr_i2c_*` / `mr_spi_*` API 访问：

- **I2C**：`mr_sim_i2c_attach()` / `mr_sim_i2c_detach()`，内置即用型假 EEPROM `mr_sim_i2c_eeprom_attach()`（支持 1/2 字节地址、写与 repeated-START 随机读，未挂载地址返回 `-MR_EIO` 模拟 NACK）。
- **SPI**：`mr_sim_spi_attach()` / `mr_sim_spi_detach()`，内置即用型假 EEPROM `mr_sim_spi_eeprom_attach()`（支持 WREN/WRDI/RDSR/READ/WRITE 指令，未挂载片选 MISO 返回 `0xFF`）。
