# FLASH 驱动移植指南

本文面向平台与外设驱动开发者，说明如何实现 `mr_flash_ops_t` 并接入 FLASH 驱动框架。公共
API 与使用方式见 [FLASH 驱动](flash.md)。

## 移植边界

| 职责                                                                                                 | 归属         |
|------------------------------------------------------------------------------------------------------|--------------|
| `setup` 时调用 `probe`、校验基础几何约束并缓存 `mr_flash_info_t`                                     | FLASH 公共层 |
| `mr_flash_info` 读取缓存、`mr_flash_region` 索引越界检查（`index >= regions_num` 返回 `-MR_EINVAL`） | FLASH 公共层 |
| `mr_flash_read` / `program` / `erase` 能力检查（`-MR_ENOSYS`）与总容量溢出安全检查（`-MR_EINVAL`）   | FLASH 公共层 |
| JEDEC ID / SFDP 探测或片内容量读取，填充稳定的 `mr_flash_info_t` 与 `erased_value[4]`                | Backend      |
| 擦除区域表计算（`region`）、对齐检查与跨页编程拆分                                                   | Backend      |
| 总线事务、芯片擦写忙等待、写保护解锁、Cache / XIP 一致性与并发加锁                                   | Backend      |

## 操作表（ops）

Backend 需实现 `mr_flash_ops_t`：

| 成员      | 原型要点                                                                         | 必需性           | 公共层行为                                                             |
|-----------|----------------------------------------------------------------------------------|------------------|------------------------------------------------------------------------|
| `probe`   | `(drv, mr_flash_info_t *info) -> mr_err_t`                                       | 必需             | `setup` 时通过 `MR_ASSERT` 检查非空并调用；几何不合法返回 `-MR_EINVAL` |
| `region`  | `(drv, mr_uint32_t index, mr_flash_region_t *region) -> mr_err_t`                | 可选（建议实现） | 为 `MR_NULL` 时 `mr_flash_region` 返回 `-MR_ENOSYS`                    |
| `read`    | `(drv, mr_uint64_t offset, mr_uint8_t *buf, mr_size_t size) -> mr_ssize_t`       | 可选             | 为 `MR_NULL` 时 `mr_flash_read` 返回 `-MR_ENOSYS`                      |
| `program` | `(drv, mr_uint64_t offset, const mr_uint8_t *buf, mr_size_t size) -> mr_ssize_t` | 可选             | 为 `MR_NULL` 时 `mr_flash_program` 返回 `-MR_ENOSYS`                   |
| `erase`   | `(drv, mr_uint64_t offset, mr_uint64_t size) -> mr_err_t`                        | 可选             | 为 `MR_NULL` 时 `mr_flash_erase` 返回 `-MR_ENOSYS`                     |

## 关键契约

- **几何与擦除模式**：`probe` 必须保证 `size != 0`、`program_size != 0`、
  `regions_num != 0`，且 `page_size != 0` 时不小于 `program_size`。
  `erased_value[4]` 必须按地址顺序填写（单字节模式重复四次，双字节模式重复两次）。
- **校验顺序**：公共层在 `read` / `program` / `erase`
  中先检查回调是否为空，再检查范围，因此未实现对应回调时越界请求也会返回
  `-MR_ENOSYS`。
- **对齐与边界**：公共层不检查 `program_size`、`page_size` 或 `erase_size`
  对齐；若器件有对齐要求，backend 需自行校验并返回明确负错误码（或按器件能力拆分跨页写），且
  `program` 不可隐式执行 `erase`。
- **XIP 与 Cache 一致性**：对支持内存映射或 XIP 的 Flash，在编程或擦除成功后，backend
  必须按目标架构要求失效对应的 D-Cache / I-Cache，并在平台文档中说明是否允许在同阵列取指时操作。
- **生命周期**：FLASH 没有 `deinit` 回调，`shutdown` 不会等待或中止异步操作，也无需实现
  teardown；再次 `setup` 会重新调用 `probe` 并覆盖缓存。

## HardTree 安装

使用 `<mrX/drivers/flash/mr_flash.h>` 提供的
`MR_HT_FLASH_INSTALL(_id, _priv, _ops)` 宏安装静态实例。该宏仅创建零初始化的
`mr_flash_data_t`，不从 HardTree 读取容量或擦除粒度；总线句柄、片选、映射基址等静态硬件信息由平台在节点中定义并填充
`_priv`，运行时几何统一由 `probe` 获取。

## 验收要点

- [ ] `probe` 与 `region` 返回的容量、页大小、编程粒度及擦除区域与芯片手册一致。
- [ ] `read`、`program`（含跨页与非对齐场景）与 `erase`（含非均匀扇区）行为符合预期，失败返回负错误码。
- [ ] `program` 不破坏目标区间外的数据，且不会隐式擦除。
- [ ] 编程/擦除后的 Cache 一致性、XIP 取指限制与并发访问策略已在平台文档中说明。
