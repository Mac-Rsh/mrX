# FLASH 驱动（FLASH）

FLASH 驱动为片内 Flash 与片外 NOR
Flash（SPI/QSPI/OSPI）提供统一的运行时几何探测、擦除区域查询以及按字节偏移的读取、编程与擦除接口。

| 项目   | 说明                             |
|--------|----------------------------------|
| 功能宏 | `MR_USE_DRIVER_FLASH`            |
| 头文件 | `<mrX/drivers/flash/mr_flash.h>` |
| 依赖   | [驱动框架](../driver.md)         |
| 测试   | `test/drivers/flash/flash.c`     |

## 配置

| 配置项                | 类型 | 默认值 | 说明                                    |
|-----------------------|------|--------|-----------------------------------------|
| `MR_USE_DRIVER_FLASH` | bool | n      | 启用 FLASH 驱动（依赖 `MR_USE_DRIVER`） |

## 快速上手

```c
#include <mrX/drivers/flash/mr_flash.h>

mr_err_t flash_update_sector(const char *name, mr_uint64_t offset,
                             const void *data, mr_size_t size) {
    mr_driver_t *flash = mr_driver_find(name);
    mr_flash_info_t info;
    mr_flash_region_t region;
    mr_err_t ret;

    if ((flash == MR_NULL) || (mr_driver_setup(flash) != 0)) {
        return -MR_ENODEV;
    }

    mr_flash_info(flash, &info);
    ret = mr_flash_region(flash, 0U, &region);
    if (ret != 0) {
        return ret;
    }

    /* 编程前必须先按擦除粒度显式擦除 */
    ret = mr_flash_erase(flash, offset, region.erase_size);
    if (ret != 0) {
        return ret;
    }

    return (mr_flash_program(flash, offset, data, size) == (mr_ssize_t)size)
               ? 0
               : -MR_EIO;
}
```

## 核心机制与约束

- **运行时几何探测**：`mr_driver_setup` 时框架调用必需的 `ops->probe` 发现设备几何并缓存到
  `mr_flash_data_t.info`，`probe` 必须保证 `size != 0`、`program_size != 0`、
  `regions_num != 0`，且当 `page_size != 0` 时 `page_size >= program_size`（否则返回
  `-MR_EINVAL`）。`erased_value[4]` 按地址顺序描述擦除后的重复字节模式（如 NOR 填
  `{0xff, 0xff, 0xff, 0xff}`，偏移 `offset` 处的期望值为
  `erased_value[offset % 4U]`）。
- **擦除区域（region）**：`mr_flash_region` 按索引查询 `mr_flash_region_t`（
  `offset`、`size`、`erase_size`），支持非均匀扇区布局。注意 `flash_init` 允许
  `probe` 报告 `regions_num > 0` 但不实现 `ops->region`，此时调用
  `mr_flash_region` 会返回 `-MR_ENOSYS`。
- **不隐式擦除与对齐策略**：`mr_flash_program` 仅执行编程，不会隐式擦除目标区域。公共层只检查
  `[offset, offset + size)` 是否落在 `info.size` 范围内（采用减法防溢出，
  `buf == MR_NULL && size != 0` 由 `MR_ASSERT` 拦截），不检查 `program_size` /
  `page_size` / `erase_size` 对齐，也不自动拆分跨页写入——对齐与跨页策略由后端或上层存储管理负责。
- **生命周期与并发**：FLASH clazz 未定义 `deinit` 回调，`shutdown` 不会清空 `info`
  缓存，再次 `setup` 会重新 `probe` 并覆盖缓存。公共层不加锁，且片内 Flash
  编程/擦除可能涉及关中断、禁取指或 Cache 失效，并发与 ISR 调用约束以具体平台后端为准。

## API 速查

| API                                                  | 说明                                                                         |
|------------------------------------------------------|------------------------------------------------------------------------------|
| `MR_FLASH_DEFINE` / `MR_FLASH_INSTALL`               | 编译期静态定义 / 安装 FLASH 驱动                                             |
| `MR_HT_FLASH_INSTALL`                                | HardTree 实例安装（创建零初始化的 `mr_flash_data_t`）                        |
| `mr_flash_init` / `mr_flash_create` / `mr_flash_del` | 初始化静态实例 / 动态创建 / 删除 FLASH 驱动                                  |
| `mr_flash_get` / `mr_flash_put`                      | 驱动引用计数加一 / 减一                                                      |
| `mr_flash_info`                                      | 读取 `setup` 时缓存的 `mr_flash_info_t`                                      |
| `mr_flash_region`                                    | 查询索引 `index` 对应的擦除区域 `mr_flash_region_t`                          |
| `mr_flash_read`                                      | 按逻辑偏移读取数据，成功返回实际读取字节数（可能短读），失败返回负数错误码   |
| `mr_flash_program`                                   | 按逻辑偏移编程数据（不自动擦除），成功返回实际写入字节数，失败返回负数错误码 |
| `mr_flash_erase`                                     | 按逻辑偏移擦除 `size` 字节，成功返回 `0`                                     |

## 内部实现

`drivers/flash/flash.c` 中的 `flash_init` 在 `setup` 时调用 `ops->probe`
并校验几何字段后写入 `flash_data->info`，`flash_release` 在驱动删除时释放动态创建的
`mr_flash_data_t`。`mr_flash_read` / `program` / `erase` 均先检查可选回调是否存在（缺失返回
`-MR_ENOSYS`），再执行总容量边界检查（越界返回 `-MR_EINVAL`），最后透传给后端。
