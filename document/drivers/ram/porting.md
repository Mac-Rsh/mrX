# RAM 驱动移植指南

> 本文面向 backend 开发者，说明如何实现 RAM backend 的几何发现、region 与读写契约，并明确公共层已经承担的部分。
> 公共 API 与用户语义见 [RAM 设备](ram.md)。

## 1. 移植边界

公共层在 `mr_driver_setup()` 阶段调用一次 `probe`，把返回的 `mr_ram_info_t` 缓存进 `mr_ram_data_t.info`，此后按这份缓存做
总容量范围检查；读写只负责转发。RAM 没有 erase、掉电保持、块设备或 teardown 语义。

| 职责                           | 公共层（`mrX/drivers/ram/ram.c` + `mr_ram.h` 内联包装）                                    | backend                                                             |
|--------------------------------|--------------------------------------------------------------------------------------------|---------------------------------------------------------------------|
| 几何发现                       | setup 时调用一次 `probe`；要求 `size != 0`；`regions_num != 0` 时要求 `region` 存在        | 填充 `mr_ram_info_t` 的两个字段，值必须稳定且可重复                 |
| 几何缓存                       | 缓存 `mr_ram_info_t`；`mr_ram_info()` 回读缓存；shutdown 清零缓存，再次 setup 重新 `probe` | 不依赖公共层保存 region 表；`region` 每次调用自行计算或返回静态表项 |
| region 查询                    | 检查 callback 是否存在、`index < info.regions_num`                                         | 返回 CPU 可直接解引用的 `address`/`size`，保证不越过总容量          |
| 总容量边界                     | 用 `offset > size \|\| size > info.size - offset` 的形式拒绝越界请求，不进入 backend       | 不需要重复实现同一层的总容量检查                                    |
| 地址换算                       | 不参与；`offset` 以 `mr_uint64_t` 传给 backend                                             | `base + offset` 的安全换算、溢出与粒度限制由 backend 负责           |
| 总线宽度、未对齐访问           | 完全不检查                                                                                 | 由 backend 决定并返回明确错误码                                     |
| cache / coherency / MPU 属性   | 不处理                                                                                     | 由 backend 或平台内存属性配置负责                                   |
| ECC、错误状态、访问受限 region | 不处理                                                                                     | 由 backend 负责并可返回器件相关错误码                               |
| 并发                           | 不加锁；同一实例的并发由调用方或硬件保证                                                   | 需要时在私有数据中自行串行化                                        |
| 生命周期                       | `ram_deinit` 只清零缓存；没有 teardown/remove 回调                                         | 控制器初始化与关闭不通过公共生命周期完成                            |

建议实现位置：

```text
mrX/arch/<vendor>/<target>/platform/drivers/ram/ram.c
```

## 2. 操作表

`mr_ram_ops_t` 的 4 个成员（签名取自 `mrX/include/mrX/drivers/ram/mr_ram.h`）：

| 成员     | 签名要点                                                                                            | 必需性                                              | 公共层行为                                                  |
|----------|-----------------------------------------------------------------------------------------------------|-----------------------------------------------------|-------------------------------------------------------------|
| `probe`  | `mr_err_t (*)(const mr_driver_t *drv, mr_ram_info_t *info)`                                         | setup 必需（`MR_ASSERT(rm_ops->probe != MR_NULL)`） | 直接调用；返回非 0 则 setup 失败并原样返回该错误码          |
| `region` | `mr_err_t (*)(const mr_driver_t *drv, mr_uint32_t index, mr_ram_region_t *region)`                  | `regions_num != 0` 时必需                           | 缺失时 setup 返回 `-MR_EINVAL`；运行期调用返回 `-MR_ENOSYS` |
| `read`   | `mr_ssize_t (*)(const mr_driver_t *drv, mr_uint64_t offset, mr_uint8_t *buf, mr_size_t size)`       | 可选                                                | 为 `MR_NULL` 时 `mr_ram_read()` 返回 `-MR_ENOSYS`           |
| `write`  | `mr_ssize_t (*)(const mr_driver_t *drv, mr_uint64_t offset, const mr_uint8_t *buf, mr_size_t size)` | 可选                                                | 为 `MR_NULL` 时 `mr_ram_write()` 返回 `-MR_ENOSYS`          |

`region` 的“必需”由 `ram_init` 显式检查：`probe` 成功后若 `regions_num != 0` 且 `rm_ops->region == MR_NULL`，setup 返回
`-MR_EINVAL` 并不缓存几何。因此 `regions_num > 0` 的 backend 必须提供 `region`；只有纯间接访问 RAM 可以用
`regions_num = 0` 配上仅有 `probe`/`read`/`write` 的 ops 表。运行期的 `-MR_ENOSYS` 只在 `regions_num == 0` 时通过
`mr_ram_region(0, ...)` 出现（`index` 越界检查在能力检查之后）。

公共层在 `mr_ram_read/write` 中先做能力检查再做范围检查，因此越界请求在 callback 缺失时返回的是 `-MR_ENOSYS` 而不是
`-MR_EINVAL`。

## 3. probe 与几何契约

`probe` 收到的是公共层清零过的 `info`（`mr_memset` 后传入），仍需填充两个字段：

| 字段          | 含义                                   | 公共层约束                                         |
|---------------|----------------------------------------|----------------------------------------------------|
| `size`        | 设备总容量（字节），逻辑偏移空间的上界 | 必须非 0，否则 setup 返回 `-MR_EINVAL` 且不缓存    |
| `regions_num` | CPU 可直接解引用区域的数量             | 为 0 合法（纯间接 RAM）；非 0 时 `region` 必须存在 |

`region` 通过 `mr_ram_region_t` 只描述 **CPU 可直接解引用** 的映射区：`address` 是要交给调用方直接访问的基址，`size` 是该
区域的字节数。它 **不是**读写接口的分片表：

- memory-mapped SRAM/SDRAM/PSRAM 通常报告 1 个或多个 bank 作为 region，同时也可以用 `read`/`write` 按偏移访问同一片内存；
- SPI/QSPI 等间接访问 RAM 报告 `regions_num = 0`，数据只能经 `read`/`write` 搬运；
- 报告了 region 的 backend 必须保证每个 region 都落在 `[0, info.size)` 之内，并且 `address + size` 不产生溢出，也不能把
  多个 region 的语义留给上层猜测。

公共层不校验 `regions_num` 与 region 覆盖范围的一致性，也不缓存 region 数组：`mr_ram_region()` 每次转发到 backend，由
backend 保证同一个 `index` 返回稳定、不冲突的结果。

`probe` 是唯一在 setup 期被调用的 backend 入口，因此它既可以只填固定几何，也可以在此时完成片外控制器的初始化与容量确认；
后者所需的时间与失败语义应记录在平台文档中，公共层不对 `probe` 的耗时或副作用做任何约束。

## 4. read / write 语义

`read`/`write` 成功时返回 **实际搬运的字节数**，失败返回负错误码。公共层允许短读/短写：返回负值以外的结果都会原样上抛，调用方
必须自行检查返回长度。

公共层在转发前完成的范围检查（用减法形式避免无符号溢出）：

```text
offset <= info.size
size   <= info.size - offset
```

因此 `offset == info.size && size == 0` 合法并会进入 backend；`offset > info.size`、或 `size > info.size - offset` 返回
`-MR_EINVAL`，不会调用 backend。`size == 0` 且偏移合法时也会调用 backend，backend 应能安全处理零长度请求。

backend 需要自行承担：

- **地址换算**：由 `mr_uint64_t offset` 得到目标地址。即使公共层已限制 `offset <= info.size`，`base + offset` 仍可能越过
  32 位地址空间或越过器件真实映射窗口，backend 必须自行判定并返回错误码，不能假设 `(mr_size_t)offset` 截断是安全的。
- **粒度与未对齐访问**：总线宽度、必须对齐的访问、控制器事务的最小单位都由 backend 决定；不支持时返回明确错误码，而不是
  静默读写相邻字节。
- **cache / coherency**：既然 region 的 `address` 会被调用方直接解引用，若该区域同时经 `read`/`write` 或 DMA 访问，backend
  必须说明并维护一致性策略。
- **错误状态**：ECC 错误、控制器错误、访问受限区域都应映射为负错误码（如 `-MR_EIO`），不要用“读回 0”冒充失败。
- **并发**：公共层不加锁；同一实例能否被并发访问必须由平台文档写清。

memory-mapped RAM 的最简实现就是基于映射基址的 `mr_memcpy`；间接 RAM 则在 `read`/`write` 内完成控制器命令与数据传输。

## 5. HardTree 与安装

通用安装宏：

```c
MR_HT_RAM_INSTALL(_id, _priv, _ops)
```

它只创建零初始化的 `mr_ram_data_t` 并注册实例；容量和 region 数量一律来自运行时 `probe`，不要求写入 HardTree，从而避免两份
几何漂移。HardTree 中用 compatible 与 `status` 描述实例存在性，需要控制器、片选、映射基址、时钟等静态资源时，再由平台自行新增
芯片私有属性并在 backend 内用 `MR_HT_DRIVER_HAS_PROP` / `MR_HT_DRIVER_PROP_OR` 读取。

backend 必须把实现包在 `#if MR_HT_HAS_DRIVER()` 内；模板里没有该 compatible 实例时，翻译单元不应残留符号。

## 6. 参考实现现状

**当前仓库不存在 STM32H5 RAM backend。** `mrX/arch/st/stm32h5/platform/drivers/` 下只有 `gpio/`、`dma/`、`uart/`、`tmr/`、
`pwm/`、`adc/`、`dac/`、`wdt/`、`flash/` 九个后端目录，没有 `ram/`；STM32H5 的 `README.md` 也没有 RAM 一节。因此本文
不指向任何厂商实现，移植时的可用材料是：

| 材料                                                 | 用途                                                                                |
|------------------------------------------------------|-------------------------------------------------------------------------------------|
| `mrX/include/mrX/drivers/ram/mr_ram.h`               | ops 签名、`mr_ram_info_t`/`mr_ram_region_t`、`MR_HT_RAM_INSTALL` 的定义             |
| `mrX/drivers/ram/ram.c`                              | setup 期 `probe` 调用、几何校验与缓存、`ram_deinit` 的行为                          |
| `mrX/test/drivers/ram/ram.c`                         | 公共契约测试：probe 生命周期、`regions_num` 与 `region` 的一致性、范围检查、可选 IO |
| `mrX/arch/st/stm32h5/platform/drivers/flash/flash.c` | 同代几何发现模型（`probe` + `region` + setup 缓存）的实现范例                       |
| [FLASH 驱动移植指南](../flash/porting.md)            | 同一套 `probe`/`region`/范围检查模型，以及 backend 侧对齐与 cache 的处理方式        |
| `mrX/document/drivers/driver.md`                     | 驱动生命周期与注册方式                                                              |
| `mrX/document/drivers/ram/ram.md`                    | 公共 API、region 语义与错误语义                                                     |

`mrX/test/drivers/ram/ram.c` 覆盖的行为值得作为实现基线：`probe` 每次 setup 只执行一次，ready 状态下的重复 setup 不会
重新 `probe`，shutdown 后再 setup 会重新 `probe`；`info.size == 0`、`regions_num > 0` 但缺 `region`、`probe` 返回错误分别
导致 setup 返回 `-MR_EINVAL`/`-MR_EINVAL`/原错误码；`regions_num == 0` 时 `region`/`read`/`write` 全部返回 `-MR_ENOSYS`。

## 7. 平台必须重新确认的行为

实现新 RAM backend 时，以下各点都不能从既有实现推断，必须在目标平台上验证：

| 项                 | 需要确认什么                                                                     |
|--------------------|----------------------------------------------------------------------------------|
| 映射方式           | CPU 可直接解引用的地址窗口，还是必须经控制器按偏移访问                           |
| 容量               | 固定常量、寄存器回读、器件 ID/SFDP，还是板级描述；`size` 必须能稳定复现          |
| region 划分        | bank/窗口是否真的可以连续解引用，`address + size` 是否溢出，region 之间是否重叠  |
| 地址换算溢出       | `base + offset` 在 `mr_uint64_t` 与目标指针宽度下是否安全                        |
| 总线宽度与对齐     | 允许的访问宽度、未对齐访问是硬件支持还是必须软件拆分                             |
| cache 与 coherency | D-cache/ICache 是否覆盖该区域、是否需要 MPU 属性、DMA 与 CPU 访问如何保持一致    |
| ECC 与错误上报     | 可纠正/不可纠正错误如何暴露，是否需要中断或状态寄存器读取                        |
| 访问受限区域       | 是否存在只读、预留或安全属性区域，访问时的返回码是什么                           |
| 初始化与掉电       | 控制器上电/时钟/自刷新是否已由平台初始化路径完成，公共生命周期不负责             |
| 并发               | 是否允许多上下文并发访问、是否与 DMA 或其它核共享                                |
| 能力诚实           | 不支持 `region` 时用 `regions_num = 0`，不支持读写时留 `MR_NULL`，不用假实现凑数 |

## 8. 移植验收清单

- [ ] `probe` 填充 `size` 与 `regions_num`（公共层已把 `info` 清零后再传入，不要依赖未初始化字段）。
- [ ] `probe` 返回 0 时 `size != 0`；多次 setup 得到相同几何。
- [ ] `regions_num != 0` 时实现了 `region`，否则 setup 必然返回 `-MR_EINVAL`。
- [ ] 纯间接访问 RAM 明确报告 `regions_num = 0`，不伪造 region。
- [ ] 每个 region 的 `address`/`size` 合法、不越过 `info.size`、`address + size` 不溢出。
- [ ] `mr_ram_region(index)` 对越界 `index` 直接返回 `-MR_EINVAL`（公共层已拦截，但 backend 不应依赖它）。
- [ ] `read`/`write` 成功时返回实际字节数，失败返回负错误码。
- [ ] `size == 0` 与 `offset == info.size` 的边界请求被安全处理。
- [ ] `base + offset` 的换算在平台地址宽度下无溢出，并已文档化。
- [ ] 未对齐访问与总线宽度行为明确：支持、软件拆分，或返回明确错误码。
- [ ] cache/coherency 策略与 MPU 属性（如需要）已写明。
- [ ] ECC 与控制器错误映射为负错误码，不用读回 0 冒充成功。
- [ ] 控制器初始化与关闭不放在公共生命周期中，且已说明由谁完成。
- [ ] 并发约束（同一实例是否需要外部串行化、是否与 DMA 共享）已写入平台文档。
- [ ] HardTree 只描述实例与静态资源，容量/geometry 只来自 `probe`；实现包在 `MR_HT_HAS_DRIVER()` 内。
