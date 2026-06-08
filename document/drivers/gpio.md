[**中文**](drivers_gpio.md) | [**English**](drivers_gpio_en.md)

---

# GPIO 驱动

GPIO 驱动提供通用数字 I/O 抽象，用于配置、读写和监听 GPIO pin。模块支持模拟、输入、输出和外部中断模式，支持上拉/下拉、推挽/开漏、逻辑反相和 port 批量读写。

模块只有在 `MR_USE_DRIVER_GPIO` 启用时可用。HC595 和 HC165 扩展分别由 `MR_USE_DRIVER_GPIO_HC595` 和 `MR_USE_DRIVER_GPIO_HC165` 控制。

## 核心数据结构和宏

### GPIO 配置

```c
typedef struct mr_gpio_config {
    mr_uint8_t mode: 3;
    mr_uint8_t alt: 1;
    mr_uint8_t pull: 2;
    mr_uint8_t otype: 1;
    mr_uint8_t level: 1;
} mr_gpio_config_t;
```

`mode` 使用 `mr_gpio_mode_t`，支持 `MR_GPIO_MODE_ANALOG`、`MR_GPIO_MODE_INPUT`、`MR_GPIO_MODE_OUTPUT` 和多种 IRQ 触发模式。`pull` 使用 `mr_gpio_pull_t`，`otype` 使用 `mr_gpio_otype_t`，`level` 使用 `mr_gpio_level_t`。

### GPIO 数据和操作集

```c
typedef struct mr_gpio_data {
    mr_uint32_t exist_mask;
    mr_uint32_t invert_mask;
} mr_gpio_data_t;

typedef struct mr_gpio_ops {
    mr_err_t (*pin_configure)(const mr_driver_t *drv, mr_uint32_t pin,
                              const mr_gpio_config_t *config);
    mr_err_t (*pin_configuration)(const mr_driver_t *drv, mr_uint32_t pin,
                                  mr_gpio_config_t *config);
    mr_err_t (*pin_write)(const mr_driver_t *drv, mr_uint32_t pin,
                          mr_uint8_t value);
    mr_err_t (*pin_read)(const mr_driver_t *drv, mr_uint32_t pin,
                         mr_uint8_t *value);
    mr_err_t (*port_write)(const mr_driver_t *drv, mr_uint32_t mask);
    mr_err_t (*port_read)(const mr_driver_t *drv, mr_uint32_t *mask);
    mr_err_t (*event_attach)(const mr_driver_t *drv, mr_uint32_t pin,
                             mr_waiter_t *entry);
} mr_gpio_ops_t;
```

`exist_mask` 表示有效 pin，单个 GPIO driver 最多抽象 32 个 pin。`invert_mask` 用于逻辑反相，pin/port 读写接口会按该掩码转换逻辑值。

### 定义和安装宏

```c
#define MR_GPIO_DATA_INIT(_exist_mask, _invert_mask)
#define MR_GPIO_DEFINE(_name, _data, _priv, _ops)
#define MR_GPIO_INSTALL(_name, _data, _priv, _ops, _level)
#define MR_HT_GPIO_INSTALL(_id, _priv, _ops)
```

`MR_GPIO_DEFINE()` 定义静态 GPIO driver；`MR_GPIO_INSTALL()` 会将 driver 入口安装到指定初始化级别；`MR_HT_GPIO_INSTALL()` 从 HardTree 节点生成 GPIO driver。

## 模块接口

### 生命周期

```c
mr_err_t mr_gpio_init(mr_driver_t *drv, mr_gpio_data_t *data,
                      void *priv, mr_gpio_ops_t *ops);
mr_driver_t *mr_gpio_create(mr_gpio_data_t *data, void *priv,
                            mr_gpio_ops_t *ops);
mr_err_t mr_gpio_del(mr_driver_t *drv);
mr_driver_t *mr_gpio_get(mr_driver_t *drv);
mr_bool_t mr_gpio_put(mr_driver_t *drv);
```

这些接口是通用 driver 生命周期接口的 GPIO 封装。调用 GPIO 读写和配置接口前，驱动必须已经 setup。

### 配置接口

```c
mr_err_t mr_gpio_configure(mr_driver_t *drv, mr_uint32_t pin,
                           mr_gpio_config_t *config);
mr_err_t mr_gpio_configuration(mr_driver_t *drv, mr_uint32_t pin,
                               mr_gpio_config_t *config);
mr_err_t mr_gpio_input(mr_driver_t *drv, mr_uint32_t pin,
                       mr_gpio_pull_t pull);
mr_err_t mr_gpio_output(mr_driver_t *drv, mr_uint32_t pin,
                        mr_gpio_otype_t otype, mr_gpio_level_t level);
```

`mr_gpio_configure()` 会检查 `pin < 32` 且 `exist_mask` 中对应 bit 有效。配置输出模式时，接口会按 `config->level` 写入默认输出值。

### 读写接口

```c
mr_ssize_t mr_gpio_write(mr_driver_t *drv, mr_uint32_t pin, mr_uint8_t value);
mr_ssize_t mr_gpio_read(mr_driver_t *drv, mr_uint32_t pin, mr_uint8_t *value);
mr_ssize_t mr_gpio_port_write(mr_driver_t *drv, mr_uint32_t mask);
mr_ssize_t mr_gpio_port_read(mr_driver_t *drv, mr_uint32_t *mask);
mr_err_t mr_gpio_toggle(mr_driver_t *drv, mr_uint32_t pin);
mr_err_t mr_gpio_port_toggle(mr_driver_t *drv);
```

单 pin 读写成功时返回 `sizeof(value)`，port 读写成功时返回 `sizeof(mask)`。port 读出的结果会与 `exist_mask` 相与。

### 事件接口

```c
#define MR_GPIO_WAIT_EXTI (MR_WAIT_IN | (1ULL << 0))

mr_err_t mr_gpio_event_attach(mr_driver_t *drv, mr_uint32_t pin,
                              mr_waiter_t *entry);
```

`mr_gpio_event_attach()` 用于将等待项绑定到 GPIO 外部中断事件。若底层 `event_attach` 未实现，返回 `-MR_ENOSYS`。

## HardTree GPIO spec

```c
typedef struct mr_ht_gpio_spec {
    mr_driver_t *drv;
    mr_uint8_t pin;
} mr_ht_gpio_spec_t;
```

GPIO spec 属性单元格式为：

```text
[phandle, pin]
```

常用接口包括 `mr_ht_gpio_configure()`、`mr_ht_gpio_input()`、`mr_ht_gpio_output()`、`mr_ht_gpio_write()`、`mr_ht_gpio_read()`、`mr_ht_gpio_toggle()` 和 `mr_ht_gpio_event_attach()`。

## HC595 和 HC165 扩展

HC595 扩展将 74HC595 级联输出寄存器抽象为 GPIO 输出端口。它只支持 `MR_GPIO_MODE_OUTPUT`，`cascade_num` 支持 1 到 4，最多 32 bit。`pin_read()` 和 `port_read()` 返回 shadow 值，不读取真实硬件状态。

HC165 扩展将 74HC165 级联输入寄存器抽象为 GPIO 输入端口。它只支持 `MR_GPIO_MODE_INPUT`，`cascade_num` 支持 1 到 4。写接口返回 `-MR_EPERM`，事件接口不支持。

## 使用示例

```c
void sample_gpio_usage(mr_driver_t *gpio_drv)
{
    mr_uint8_t value;

    mr_gpio_output(gpio_drv, 5, MR_GPIO_OTYPE_PP, MR_GPIO_LEVEL_LOW);
    mr_gpio_write(gpio_drv, 5, MR_GPIO_LEVEL_HIGH);

    mr_gpio_input(gpio_drv, 6, MR_GPIO_PULL_UP);
    mr_gpio_read(gpio_drv, 6, &value);

    mr_gpio_toggle(gpio_drv, 5);
}
```

该示例配置 pin 5 为推挽输出并写高电平，配置 pin 6 为上拉输入并读取逻辑值。

## 注意事项

- 单个 GPIO driver 最多支持 32 个 pin，pin 有效性由 `exist_mask` 决定。
- `invert_mask` 会影响 pin/port 读写的逻辑值。
- `event_attach` 是可选能力，未实现时返回 `-MR_ENOSYS`。
- HC595 只支持输出，HC165 只支持输入。
- HC595/HC165 的级联数量限制为 1 到 4。
