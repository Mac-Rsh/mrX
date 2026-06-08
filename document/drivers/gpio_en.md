[**中文**](drivers_gpio.md) | [**English**](drivers_gpio_en.md)

---

# GPIO Driver

The GPIO driver provides a common digital I/O abstraction for configuring, reading, writing, and waiting on GPIO pins. It supports analog, input, output, and external interrupt modes, pull-up/down, push-pull/open-drain output, logical inversion, and port-wide access.

The module is only available when `MR_USE_DRIVER_GPIO` is enabled. The HC595 and HC165 extensions are controlled by `MR_USE_DRIVER_GPIO_HC595` and `MR_USE_DRIVER_GPIO_HC165`.

## Core Data Structures and Macros

### GPIO Configuration

```c
typedef struct mr_gpio_config {
    mr_uint8_t mode: 3;
    mr_uint8_t alt: 1;
    mr_uint8_t pull: 2;
    mr_uint8_t otype: 1;
    mr_uint8_t level: 1;
} mr_gpio_config_t;
```

`mode` uses `mr_gpio_mode_t` and supports `MR_GPIO_MODE_ANALOG`, `MR_GPIO_MODE_INPUT`, `MR_GPIO_MODE_OUTPUT`, and multiple IRQ trigger modes. `pull` uses `mr_gpio_pull_t`, `otype` uses `mr_gpio_otype_t`, and `level` uses `mr_gpio_level_t`.

### GPIO Data and Operations

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

`exist_mask` marks valid pins. A single GPIO driver can abstract up to 32 pins. `invert_mask` provides logical inversion; pin and port read/write APIs translate values through this mask.

### Definition and Installation Macros

```c
#define MR_GPIO_DATA_INIT(_exist_mask, _invert_mask)
#define MR_GPIO_DEFINE(_name, _data, _priv, _ops)
#define MR_GPIO_INSTALL(_name, _data, _priv, _ops, _level)
#define MR_HT_GPIO_INSTALL(_id, _priv, _ops)
```

`MR_GPIO_DEFINE()` defines a static GPIO driver. `MR_GPIO_INSTALL()` installs the driver entry at a specified init level. `MR_HT_GPIO_INSTALL()` generates a GPIO driver from a HardTree node.

## Module Interface

### Lifecycle

```c
mr_err_t mr_gpio_init(mr_driver_t *drv, mr_gpio_data_t *data,
                      void *priv, mr_gpio_ops_t *ops);
mr_driver_t *mr_gpio_create(mr_gpio_data_t *data, void *priv,
                            mr_gpio_ops_t *ops);
mr_err_t mr_gpio_del(mr_driver_t *drv);
mr_driver_t *mr_gpio_get(mr_driver_t *drv);
mr_bool_t mr_gpio_put(mr_driver_t *drv);
```

These APIs wrap the generic driver lifecycle for GPIO. The driver must be set up before GPIO configuration or I/O APIs are called.

### Configuration Interface

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

`mr_gpio_configure()` checks `pin < 32` and verifies the pin through `exist_mask`. When configuring output mode, it writes the default output level from `config->level`.

### I/O Interface

```c
mr_ssize_t mr_gpio_write(mr_driver_t *drv, mr_uint32_t pin, mr_uint8_t value);
mr_ssize_t mr_gpio_read(mr_driver_t *drv, mr_uint32_t pin, mr_uint8_t *value);
mr_ssize_t mr_gpio_port_write(mr_driver_t *drv, mr_uint32_t mask);
mr_ssize_t mr_gpio_port_read(mr_driver_t *drv, mr_uint32_t *mask);
mr_err_t mr_gpio_toggle(mr_driver_t *drv, mr_uint32_t pin);
mr_err_t mr_gpio_port_toggle(mr_driver_t *drv);
```

Single-pin read/write returns `sizeof(value)` on success. Port read/write returns `sizeof(mask)` on success. Port reads are masked by `exist_mask`.

### Event Interface

```c
#define MR_GPIO_WAIT_EXTI (MR_WAIT_IN | (1ULL << 0))

mr_err_t mr_gpio_event_attach(mr_driver_t *drv, mr_uint32_t pin,
                              mr_waiter_t *entry);
```

`mr_gpio_event_attach()` attaches a waiter to a GPIO external interrupt event. If the low-level `event_attach` operation is not implemented, it returns `-MR_ENOSYS`.

## HardTree GPIO Spec

```c
typedef struct mr_ht_gpio_spec {
    mr_driver_t *drv;
    mr_uint8_t pin;
} mr_ht_gpio_spec_t;
```

GPIO spec property cells use this format:

```text
[phandle, pin]
```

Common wrappers include `mr_ht_gpio_configure()`, `mr_ht_gpio_input()`, `mr_ht_gpio_output()`, `mr_ht_gpio_write()`, `mr_ht_gpio_read()`, `mr_ht_gpio_toggle()`, and `mr_ht_gpio_event_attach()`.

## HC595 and HC165 Extensions

The HC595 extension exposes cascaded 74HC595 output shift registers as a GPIO output port. It only supports `MR_GPIO_MODE_OUTPUT`. `cascade_num` supports 1 to 4 chips, up to 32 bits. `pin_read()` and `port_read()` return the shadow value instead of reading hardware state.

The HC165 extension exposes cascaded 74HC165 input shift registers as a GPIO input port. It only supports `MR_GPIO_MODE_INPUT`. `cascade_num` supports 1 to 4 chips. Write operations return `-MR_EPERM`, and events are not supported.

## Usage Example

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

This example configures pin 5 as push-pull output and writes it high, then configures pin 6 as pull-up input and reads its logical value.

## Notes

- A single GPIO driver supports up to 32 pins. Pin validity is controlled by `exist_mask`.
- `invert_mask` affects logical values for pin and port read/write operations.
- `event_attach` is optional; if it is not implemented, `mr_gpio_event_attach()` returns `-MR_ENOSYS`.
- HC595 supports output only, and HC165 supports input only.
- HC595/HC165 cascade count is limited to 1 to 4.
