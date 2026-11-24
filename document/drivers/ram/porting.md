# RAM 驱动移植指南

本文面向平台与外设驱动开发者，说明如何实现 `mr_ram_ops_t` 并接入 RAM 驱动框架。公共
API 与使用方式见 [RAM 驱动](ram.md)。

## 移植边界

| 职责                                                                                 | 归属       |
|--------------------------------------------------------------------------------------|------------|
| `setup` 时调用 `probe`、校验几何合法性并缓存 `mr_ram_info_t`                         | RAM 公共层 |
| `mr_ram_info` 读取缓存、`shutdown` 时清空缓存                                        | RAM 公共层 |
| `mr_ram_region` 索引越界检查（`index >= regions_num` 返回 `-MR_EINVAL`）             | RAM 公共层 |
| `mr_ram_read` / `write` 能力检查（`-MR_ENOSYS`）与总容量溢出安全检查（`-MR_EINVAL`） | RAM 公共层 |
| 硬件控制器初始化、ID 读取与稳定几何信息填充（`probe`）                               | Backend    |
| 直接映射地址窗口计算（`region`）与地址对齐/总线宽度限制检查                          | Backend    |
| 实际数据搬运（`memcpy` 或 SPI/QSPI 控制器事务）与 Cache 一致性维护                   | Backend    |

## 操作表（ops）

Backend 需实现 `mr_ram_ops_t`：

| 成员     | 原型要点                                                                         | 必需性                   | 公共层行为                                                                                 |
|----------|----------------------------------------------------------------------------------|--------------------------|--------------------------------------------------------------------------------------------|
| `probe`  | `(drv, mr_ram_info_t *info) -> mr_err_t`                                         | 必需                     | `setup` 时通过 `MR_ASSERT` 检查非空并调用；返回非 0 则 `setup` 失败                        |
| `region` | `(drv, mr_uint32_t index, mr_ram_region_t *region) -> mr_err_t`                  | `regions_num > 0` 时必需 | `regions_num > 0` 但为 `MR_NULL` 时 `setup` 返回 `-MR_EINVAL`；运行期缺失返回 `-MR_ENOSYS` |
| `read`   | `(drv, mr_uint64_t offset, mr_uint8_t *buf, mr_size_t size) -> mr_ssize_t`       | 可选                     | 为 `MR_NULL` 时 `mr_ram_read` 返回 `-MR_ENOSYS`                                            |
| `write`  | `(drv, mr_uint64_t offset, const mr_uint8_t *buf, mr_size_t size) -> mr_ssize_t` | 可选                     | 为 `MR_NULL` 时 `mr_ram_write` 返回 `-MR_ENOSYS`                                           |

## 关键契约

- **几何稳定性**：`probe` 填充的 `info->size` 必须非 0 且可重复；`region` 返回的
  `address` 与 `size` 必须是 CPU 可直接解引用的有效映射区，且不超出总容量。
- **校验顺序**：公共层在 `mr_ram_read` / `mr_ram_write`
  中先检查回调是否为空，再检查范围，因此未实现对应回调时越界请求也会返回
  `-MR_ENOSYS`。
- **底层限制与并发**：公共层不检查对齐、总线宽度、Cache
  一致性或并发安全。控制器对传输长度、地址对齐有要求时，由 backend
  自行校验并返回明确负错误码；需互斥访问的外部总线 RAM 由 backend 自行加锁。
- **生命周期**：公共层没有 `teardown` / `remove` 回调，`shutdown`
  仅清零缓存信息；硬件电源或控制器复位如需管理应由平台自行处理。

## HardTree 安装

使用 `<mrX/drivers/ram/mr_ram.h>` 提供的 `MR_HT_RAM_INSTALL(_id, _priv, _ops)`
宏安装静态实例。该宏仅创建零初始化的 `mr_ram_data_t`，不从 HardTree
读取标准容量属性；物理地址、总线控制器、片选与时序参数由平台在 HardTree 节点中自行定义并填充
`_priv`。

## 验收要点

- [ ] `mr_driver_setup` 后 `mr_ram_info` 返回正确容量与 `regions_num`，`shutdown`
  后再次 `setup` 可正常重新 `probe`。
- [ ] 直接映射 RAM 的 `mr_ram_region` 返回地址可直接读写且与 `size` 匹配；间接
  RAM 返回 `regions_num == 0`。
- [ ] `mr_ram_read` / `mr_ram_write` 在边界偏移（含
  `offset == info.size, size == 0` 与越界访问）下返回值符合预期。
