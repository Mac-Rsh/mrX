# RAM 驱动（RAM）

RAM 驱动统一抽象 CPU 可直接寻址的片内 SRAM/SDRAM/PSRAM 以及需通过控制器间接访问的外部
RAM（如 SPI/QSPI PSRAM），在 `setup` 阶段通过 `probe`
发现设备容量与直接映射区域，并提供无整数溢出的按偏移读写接口。

| 项目   | 说明                                                                   |
|--------|------------------------------------------------------------------------|
| 功能宏 | `MR_USE_DRIVER_RAM`                                                    |
| 头文件 | `<mrX/drivers/ram/mr_ram.h>`                                           |
| 依赖   | [驱动框架](../driver.md)、[动态内存](../../libc/malloc.md)（动态创建） |
| 测试   | `test/drivers/ram/ram.c`                                               |

## 配置

| 配置项              | 类型 | 默认值 | 说明                                  |
|---------------------|------|--------|---------------------------------------|
| `MR_USE_DRIVER_RAM` | bool | n      | 启用 RAM 驱动（依赖 `MR_USE_DRIVER`） |

## 快速上手

```c
#include <mrX/drivers/ram/mr_ram.h>

mr_err_t ram_rw_example(const char *name) {
    mr_driver_t *ram = mr_driver_find(name);
    mr_ram_info_t info;
    mr_uint8_t tx[4] = {0x11U, 0x22U, 0x33U, 0x44U};
    mr_uint8_t rx[4] = {0};

    if ((ram == MR_NULL) || (mr_driver_setup(ram) != 0)) {
        return -MR_ENODEV;
    }

    mr_ram_info(ram, &info);
    if (info.size < sizeof(tx)) {
        return -MR_ENOSPC;
    }

    if (mr_ram_write(ram, 0ULL, tx, sizeof(tx)) != (mr_ssize_t)sizeof(tx)) {
        return -MR_EIO;
    }
    if (mr_ram_read(ram, 0ULL, rx, sizeof(rx)) != (mr_ssize_t)sizeof(rx)) {
        return -MR_EIO;
    }
    return 0;
}
```

## 核心机制与约束

- **直接映射与间接访问**：`mr_ram_info_t` 包含设备总容量 `size`（不可为
  0）与可直接寻址区域数 `regions_num`。`regions_num > 0` 时可通过 `mr_ram_region`
  获取 CPU 可直接解引用的物理/虚拟地址窗口；`regions_num == 0` 表示纯间接访问
  RAM（如 SPI PSRAM），此时仅通过 `mr_ram_read` / `mr_ram_write` 访问。
- **生命周期**：`mr_driver_setup` 时框架调用必需的 `ops->probe` 获取几何信息并缓存到
  `mr_ram_data_t.info`（若 `info.size == 0` 或 `regions_num > 0` 但未提供
  `ops->region`，`setup` 返回 `-MR_EINVAL`）。`shutdown` 时清空缓存，再次 `setup`
  会重新执行 `probe`。
- **边界检查**：`mr_ram_read` 与 `mr_ram_write` 先检查对应 `ops` 是否存在（缺失返回
  `-MR_ENOSYS`），再使用 `offset <= info.size && size <= info.size - offset`
  做溢出安全检查，越界返回 `-MR_EINVAL`（`offset == info.size && size == 0` 合法）。
  `buf == MR_NULL && size != 0` 由 `MR_ASSERT` 拦截。
- **能力边界**：RAM 驱动不提供擦除、掉电保持、Cache/MPU 属性控制或 ECC
  状态上报。公共层无内部锁，多任务并发访问需由应用或后端保证。

## API 速查

| API                                            | 说明                                                                         |
|------------------------------------------------|------------------------------------------------------------------------------|
| `MR_RAM_DEFINE` / `MR_RAM_INSTALL`             | 编译期静态定义 / 安装 RAM 驱动                                               |
| `MR_HT_RAM_INSTALL`                            | HardTree 实例安装（创建零初始化的 `mr_ram_data_t`）                          |
| `mr_ram_init` / `mr_ram_create` / `mr_ram_del` | 初始化静态实例 / 动态创建 / 删除 RAM 驱动                                    |
| `mr_ram_get` / `mr_ram_put`                    | 驱动引用计数加一 / 减一                                                      |
| `mr_ram_info`                                  | 读取 `setup` 时缓存的 `size` 与 `regions_num`                                |
| `mr_ram_region`                                | 查询索引 `index` 对应的直接映射区域（`address`、`size`）                     |
| `mr_ram_read`                                  | 从逻辑偏移 `offset` 读取 `size` 字节到 `buf`，返回实际读取字节数或负数错误码 |
| `mr_ram_write`                                 | 向逻辑偏移 `offset` 写入 `size` 字节，返回实际写入字节数或负数错误码         |

## 内部实现

```text
mr_driver_setup() ──▶ ops->probe() ──▶ 校验 size != 0 且 (regions_num == 0 || ops->region) ──▶ 缓存 ram_data.info
                                                                                                   │
mr_ram_region() / mr_ram_read() / mr_ram_write() ◀── 能力检查 (-MR_ENOSYS) + 范围检查 (-MR_EINVAL) ◀┘
```

`drivers/ram/ram.c` 中 `ram_init` 校验 `probe` 结果并缓存几何信息；`ram_deinit`
清零 `ram_data->info`；`ram_release` 释放动态创建的 `mr_ram_data_t`。
