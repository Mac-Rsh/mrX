# FLASH 驱动移植指南

> 本文面向 backend 开发者，说明如何实现片内或片外 FLASH backend、需要遵守的几何与错误契约，以及
> 参考实现的能力边界。公共 API 与用户语义见 [FLASH 驱动](flash.md)。

## 1. 移植边界

公共层在 `mr_driver_setup()` 阶段调用一次 `probe`，把返回的 `mr_flash_info_t` 缓存进
`mr_flash_data_t.info`，此后只按这份缓存做
总容量范围检查。除 `probe` 之外的 callback 都是可选能力，公共层只在回调存在时转发。

| 职责                                    | 公共层（`mrX/drivers/flash/flash.c` + `mr_flash.h` 内联包装）                                                                    | backend                                                             |
|-----------------------------------------|----------------------------------------------------------------------------------------------------------------------------------|---------------------------------------------------------------------|
| 几何发现                                | setup 时调用一次 `probe`；校验 `size != 0`、`program_size != 0`、`regions_num != 0`，且 `page_size` 非 0 时不小于 `program_size` | 填充 `mr_flash_info_t` 的全部字段，值必须稳定且可重复               |
| 几何缓存                                | 缓存 `mr_flash_info_t`；`mr_flash_info()` 直接回读缓存                                                                           | 不依赖公共层保存 region 表；`region` 每次调用自行计算或返回静态表项 |
| region 查询                             | 检查 callback 是否存在、`index < info.regions_num`                                                                               | 按 `index` 返回 `offset`/`size`/`erase_size`，自身保证几何自洽      |
| 总容量边界                              | 用 `offset > size \|\| size > info.size - offset` 的形式拒绝越界请求，不进入 backend                                             | 不需要重复实现同一层的总容量检查                                    |
| 对齐、跨页、粒度                        | 完全不检查                                                                                                                       | 全部由 backend 决定并返回明确错误码                                 |
| 锁、busy 等待、器件协议、片选、总线事务 | 不加锁、不等待                                                                                                                   | 由 backend 完成并串行化                                             |
| cache / XIP 一致性                      | 不处理                                                                                                                           | 由 backend 在改动 memory-mapped 内容后自行处理                      |
| 生命周期                                | FLASH clazz 没有 deinit 回调，shutdown 不会清空 `flash_data->info`                                                               | 不需要实现 teardown；重新 setup 会重新执行 `probe` 并覆盖缓存       |

建议实现位置：

```text
mrX/arch/<vendor>/<target>/platform/drivers/flash/flash.c
```

## 2. 操作表

`mr_flash_ops_t` 的 5 个成员（签名取自
`mrX/include/mrX/drivers/flash/mr_flash.h`）：

| 成员      | 签名要点                                                                                            | 必需性                                                 | 公共层行为                                             |
|-----------|-----------------------------------------------------------------------------------------------------|--------------------------------------------------------|--------------------------------------------------------|
| `probe`   | `mr_err_t (*)(const mr_driver_t *drv, mr_flash_info_t *info)`                                       | setup 必需（`MR_ASSERT(flash_ops->probe != MR_NULL)`） | 直接调用；返回非 0 则 setup 失败并原样返回该错误码     |
| `region`  | `mr_err_t (*)(const mr_driver_t *drv, mr_uint32_t index, mr_flash_region_t *region)`                | 可选                                                   | 为 `MR_NULL` 时 `mr_flash_region()` 返回 `-MR_ENOSYS`  |
| `read`    | `mr_ssize_t (*)(const mr_driver_t *drv, mr_uint64_t offset, mr_uint8_t *buf, mr_size_t size)`       | 可选                                                   | 为 `MR_NULL` 时 `mr_flash_read()` 返回 `-MR_ENOSYS`    |
| `program` | `mr_ssize_t (*)(const mr_driver_t *drv, mr_uint64_t offset, const mr_uint8_t *buf, mr_size_t size)` | 可选                                                   | 为 `MR_NULL` 时 `mr_flash_program()` 返回 `-MR_ENOSYS` |
| `erase`   | `mr_err_t (*)(const mr_driver_t *drv, mr_uint64_t offset, mr_uint64_t size)`                        | 可选                                                   | 为 `MR_NULL` 时 `mr_flash_erase()` 返回 `-MR_ENOSYS`   |

公共层在 `mr_flash_read/program/erase` 中先做能力检查再做范围检查，因此越界请求在
callback 缺失时返回的是
`-MR_ENOSYS` 而不是 `-MR_EINVAL`。

注意 `flash_init` 要求 `regions_num != 0`，但并不要求 `region` callback 存在：
`probe` 报告了 region 数量却没有实现
`region` 的 backend 可以 setup 成功，直到调用 `mr_flash_region()` 才暴露
`-MR_ENOSYS`。这不是推荐做法，实现 region 几何的
backend 应当把回调一并提供。

## 3. probe 与几何契约

`probe` 返回 `mr_flash_info_t`，`region` 返回 `mr_flash_region_t`。backend 必须保证报告的容量、编程粒度、擦除粒度和 region 范围与实际器件一致。公共层只做其头文件规定的基本校验；不要把某个控制器的扇区大小、bank 数或编程宽度写成通用约束。

`probe` 成功时须填写 `erased_value` 的四个字节，按 `read` 的地址顺序从 Flash 偏移 0
起表示擦除后的重复模式。单字节模式重复四次，双字节模式重复两次。

## 4. read

`read` 成功时返回实际读取的字节数，可能短读。backend 自行负责映射、访问校验和缓存可见性；公共层不指定 memory-mapped 或总线读取方式。

## 5. program

`program` 只执行编程，不自动擦除。backend 应根据 `program_size`、页边界和底层写入能力拆分请求，处理必要的读改写，并防止影响请求区间外的数据。失败时返回负错误码；不可把已部分完成的写操作报告成全部成功。

## 6. erase

`erase` 成功时返回 0，失败返回负错误码。对齐、region 跨越与操作粒度由 backend 依据实际几何验证和执行。公共层不替 backend 安排多个擦除区域的划分。

## 7. 缓存与执行安全

对可执行或 memory-mapped 的存储器，编程/擦除后应按目标架构要求处理数据与指令缓存，并考虑从同一阵列取指及并发访问的风险。是否需要失效、失效范围和失败后的恢复策略都属于平台契约，不得默认所有 FLASH 使用同一种缓存模型。

## 8. HardTree 与安装

用 `MR_HT_FLASH_INSTALL` 或驱动框架约定注册实例。静态的总线、片选、控制器和可选资源由平台描述；运行时发现的容量与 region 几何来自 `probe`，不可与硬件树中重复的猜测值冲突。

## 9. 移植验收

- [ ] probe/region 返回的容量、粒度和区域关系与目标存储器一致。
- [ ] read 短读、program 非对齐或跨界、erase region 对齐都按契约处理。
- [ ] 任何硬件失败都映射成负错误码，成功返回值与实际完成操作相符。
- [ ] 写入后的缓存一致性、供电中断和并发访问约束有平台级说明。
