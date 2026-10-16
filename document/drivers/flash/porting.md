# FLASH 驱动移植指南

> 本文面向 backend 开发者，说明如何实现片内或片外 FLASH backend、需要遵守的几何与错误契约，以及 STM32H5
> 参考实现的能力边界。公共 API 与用户语义见 [FLASH 驱动](flash.md)。

## 1. 移植边界

公共层在 `mr_driver_setup()` 阶段调用一次 `probe`，把返回的 `mr_flash_info_t` 缓存进 `mr_flash_data_t.info`，此后只按这份缓存做
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

`mr_flash_ops_t` 的 5 个成员（签名取自 `mrX/include/mrX/drivers/flash/mr_flash.h`）：

| 成员      | 签名要点                                                                                            | 必需性                                                 | 公共层行为                                             |
|-----------|-----------------------------------------------------------------------------------------------------|--------------------------------------------------------|--------------------------------------------------------|
| `probe`   | `mr_err_t (*)(const mr_driver_t *drv, mr_flash_info_t *info)`                                       | setup 必需（`MR_ASSERT(flash_ops->probe != MR_NULL)`） | 直接调用；返回非 0 则 setup 失败并原样返回该错误码     |
| `region`  | `mr_err_t (*)(const mr_driver_t *drv, mr_uint32_t index, mr_flash_region_t *region)`                | 可选                                                   | 为 `MR_NULL` 时 `mr_flash_region()` 返回 `-MR_ENOSYS`  |
| `read`    | `mr_ssize_t (*)(const mr_driver_t *drv, mr_uint64_t offset, mr_uint8_t *buf, mr_size_t size)`       | 可选                                                   | 为 `MR_NULL` 时 `mr_flash_read()` 返回 `-MR_ENOSYS`    |
| `program` | `mr_ssize_t (*)(const mr_driver_t *drv, mr_uint64_t offset, const mr_uint8_t *buf, mr_size_t size)` | 可选                                                   | 为 `MR_NULL` 时 `mr_flash_program()` 返回 `-MR_ENOSYS` |
| `erase`   | `mr_err_t (*)(const mr_driver_t *drv, mr_uint64_t offset, mr_uint64_t size)`                        | 可选                                                   | 为 `MR_NULL` 时 `mr_flash_erase()` 返回 `-MR_ENOSYS`   |

公共层在 `mr_flash_read/program/erase` 中先做能力检查再做范围检查，因此越界请求在 callback 缺失时返回的是
`-MR_ENOSYS` 而不是 `-MR_EINVAL`。

注意 `flash_init` 要求 `regions_num != 0`，但并不要求 `region` callback 存在：`probe` 报告了 region 数量却没有实现
`region` 的 backend 可以 setup 成功，直到调用 `mr_flash_region()` 才暴露 `-MR_ENOSYS`。这不是推荐做法，实现 region 几何的
backend 应当把回调一并提供。

## 3. probe 与几何契约

`probe` 必须填充 `mr_flash_info_t` 的全部 5 个字段，因为公共层不会先把 `info` 清零再传入：

| 字段           | 含义                           | 公共层约束                               |
|----------------|--------------------------------|------------------------------------------|
| `size`         | 器件总容量（字节）             | 必须非 0；作为后续所有范围检查的唯一依据 |
| `program_size` | 最小编程单位（字节）           | 必须非 0                                 |
| `page_size`    | 编程页大小；无页边界概念时为 0 | 为 0，或不小于 `program_size`            |
| `regions_num`  | 擦除 region 数量               | 必须非 0                                 |
| `erased_value` | 擦除后的字节值                 | 公共层不校验                             |

任一约束不满足时 `flash_init` 返回 `-MR_EINVAL` 且 **不更新缓存**（旧缓存保持原值）。公共层不校验 `erased_value`、region
覆盖关系、region 的 `erase_size`，也不要求 `program_size`/`page_size` 是 2 的幂——这些都由 backend 负责自洽。

`region` 通过 `mr_flash_region_t` 描述擦除几何：`offset`、`size`（相对器件基址）与 `erase_size`（该区间的最小可擦除单位）。
公共层只做 `index < info.regions_num` 的范围转发，不缓存 region 数组，也不检查多个 region 是否互相重叠或完整覆盖
`[0, size)`。`regions_num == 1` 表示整片几何均匀，是片内 Flash 的常见形态。

### STM32H5 片上阵列的实际几何

`mrX/arch/st/stm32h5/platform/drivers/flash/flash.c` 的 `flash_probe` 返回固定几何，`flash_region` 只接受 `index == 0`：

| 项                     | 值                            | 说明                                      |
|------------------------|-------------------------------|-------------------------------------------|
| `info.size`            | `FLASH_SIZE`                  | 由芯片头文件给出，随具体 H5 型号变化      |
| `info.program_size`    | `16` 字节                     | 128-bit quadword，H5 flash 的最小编程单位 |
| `info.page_size`       | `0`                           | H5 编程无页边界概念                       |
| `info.regions_num`     | `1`                           | 单一天然均匀擦除 region                   |
| `info.erased_value`    | `0xFF`                        |                                           |
| `region(0).offset`     | `0`                           |                                           |
| `region(0).size`       | `FLASH_SIZE`                  |                                           |
| `region(0).erase_size` | `8 KB`（`FLASH_SECTOR_SIZE`） | H5 sector 大小                            |
| `region(index != 0)`   | —                             | 返回 `-MR_EINVAL`                         |

H503/H523/H533 为单 bank，其余 H5 系列为双 bank。bank 数只影响 backend 内部的地址换算，不体现在几何里：`size` 与
`region` 始终按整片连续地址空间报告。其他平台移植时不要假设“单 region + 均匀 8 KB”；这是 H5 的器件事实，不是公共层契约。

## 4. read 语义

`read` 成功时返回 **实际读取的字节数**，失败返回负错误码。公共层允许短读：它只要求返回值为负或落在 `[0, size]`，不会因为
返回小于 `size` 而报错，调用方必须自行检查短读。

STM32H5 的 `flash_read` 是 memory-mapped 普通拷贝：从 `FLASH_BASE + offset` 复制 `size` 字节并返回 `size`。它不做对齐检查、
不访问 HAL、不要求解锁，也不在读取路径上做 cache 维护——读取和其他内存访问一样会经过 D-cache，一致性由写入路径的失效动作
保证（见第 7 节）。`size == 0` 时拷贝长度为 0 并返回 0。

`offset` 到地址的换算在 backend 内部走 32 位地址空间。由于公共层把 `offset` 限制在 `info.size` 之内，且 H5 的
`FLASH_BASE + FLASH_SIZE` 不越过 32 位，换算是安全的；但其他平台若在 32 位地址空间之外映射器件，必须在 backend 内自行
校验并返回错误码，不能依赖公共层。

## 5. program 语义

`program` 表示纯粹的程序写入， **不做隐式擦除**。成功返回实际编程的字节数，失败返回负错误码。

### 公共契约

- 不要求 `offset` 按 `program_size` 对齐；
- 不要求 `size` 是 `program_size` 的整数倍；
- 不自动拆分跨页/跨粒度操作；
- 只检查 `[offset, offset + size)` 是否落在 `info.size` 内，不检查与 region 或 `erase_size` 的关系；
- `size == 0` 且偏移合法时仍会调用 backend。

### STM32H5 backend 的实际行为

H5 的 `flash_program` 用“word merging”把任意字节区间落到 128-bit quadword 上：

1. `size == 0` 直接返回 0，不接触硬件。
2. 先 `HAL_FLASH_Unlock()`；失败则直接返回映射后的错误码，不执行任何写入。
3. **头部**：若起始地址不落在 128-bit 边界，读回包含起始字节的整个 quadword，把请求覆盖的字节替换为 `buf` 的内容，再整字编程。
4. **主体**：完整的 128-bit word 逐个通过对齐的栈缓冲区分段编程。
5. **尾部**：剩余不足一个字的字节同样用读回-合并-整字编程的方式写入。
6. 任何一条失败路径都会先 `HAL_FLASH_Lock()` 再返回错误码；全部成功时在返回前 lock。

关键约束：合并时如果 quadword 中 **请求范围之外**的任一字节不是 `0xFF`（即已经写过的位），backend 返回 `-MR_EIO`，不会
覆盖它。这意味着：

- 在已编程区域上做非整字覆盖写会失败并返回 `-MR_EIO`，符合 NOR “只能把 1 写成 0” 的物理约束；
- 对已经写过的数据重新 program 同一个 128-bit 字（即使内容相同）也会失败；
- 需要改写的内容必须由调用方先 `erase`；
- 由于合并是逐字的，一次跨越多个 128-bit 字的 `program` 在失败时可能已经写入了前面的字—— **program 不是事务**，失败返回的
  字节数信息会丢失（返回的是负错误码，不是已完成字节数）。

错误码映射（`mr_stm32h5_hal_status`）：`HAL_BUSY` → `-MR_EBUSY`，`HAL_TIMEOUT` → `-MR_ETIMEDOUT`，其余 HAL 错误 →
`-MR_EIO`。如果编程全部成功但最后的 lock 失败，返回的是该 lock 错误码（通常 `-MR_EIO`），已完成的字节数不再上报。
unlock 在第一次调用 HAL 之前，lock 在每条返回路径上（早退时的 lock 返回值被忽略），因此不会把 flash 留在解锁状态。

## 6. erase 语义

`erase` 成功返回 0，失败返回负错误码。

### 公共契约

公共层只检查 `[offset, offset + size)` 是否落在 `info.size` 内， **不检查** `offset`/`size` 是否按 region 的 `erase_size`
对齐，也不把请求拆分成多个 region。对齐是 backend 的责任。

### STM32H5 backend 的实际行为

- `offset` 与 `size` **都必须**是 `FLASH_SECTOR_SIZE`（8 KB）的整数倍，并且都能装进 32 位，否则返回 `-MR_EINVAL`；
- `size == 0` 在偏移合法时返回 0，不接触硬件；
- 参数检查通过后 `HAL_FLASH_Unlock()`，失败则直接返回映射后的错误码；
- 循环中每次 `HAL_FLASHEx_Erase()` 只擦除一个对齐 sector（`NbSectors = 1`），随后 `offset += 8 KB`、`size -= 8 KB`；
- 遇到第一个失败就跳出循环，返回该错误码， **剩余 sector 不会被擦除**；`sector_error` 只是 HAL 输出参数，不作为返回值；
- 循环结束后统一 `HAL_FLASH_Lock()`。若此前已有错误，保留原错误码；只有在无其他错误时 lock 失败才上报为 `-MR_EIO`；
- 由于一次调用可能擦除多个 sector， **erase 也不是原子操作**：中途失败会留下“前半已擦除、后半未擦除”的状态。

地址换算是把全局 `offset` 折算成 bank 本地 sector 坐标（`bank_size = FLASH_SIZE / bank count`），双 bank 器件上跨越
bank 边界的 erase 会在循环里自然落到不同 bank 的 HAL 调用上，语义上仍然连续。

## 7. cache 与取指一致性

片上 Flash 是 memory-mapped 的，代码也可能直接从同一阵列执行，因此写入后必须让 cache 中可能存在的旧别名失效。

STM32H5 backend 的条件编译开关是 `FLASH_USE_CACHE`，它由 `MR_USE_DCACHE` 或 `MR_USE_ICACHE` 任一打开触发：

- `program` 成功返回前，对 `[FLASH_BASE + offset, +已编程字节数)` 同时做 data 与 instruction alias 失效；
- `erase` 只在整体返回 0 时，对 `[FLASH_BASE + offset, +请求字节数)` 做同样的失效；
- 任一失败路径都不做失效，因为无法确定硬件已改动了多少；
- 具体失效动作由 port 层的 `mr_port_dcache_invalidate` / `mr_port_icache_invalidate` 提供（见
  `mrX/include/mrX/port/mr_port_cache.h`），backend 不自行操作 cache 寄存器。

移植到其他平台时必须重新确认：该平台是否 memory-map、是否有 D-cache/ICache、失效粒度是 cache line 还是整段、以及
program/erase 期间能否取指。这些都不属于公共层契约，不能从 H5 直接推断。

## 8. HardTree 接线

STM32H5 的 compatible 是 `st_stm32h5_flash`。片上阵列没有独立的时钟门控、没有 GPIO、没有 DMA、没有中断，因此 **不需要任何
HardTree 属性**：

| 属性               | 必需性 | 含义                                     | 默认值 |
|--------------------|--------|------------------------------------------|--------|
| （无芯片私有属性） | —      | 片上阵列只靠 compatible 与 `status` 描述 | —      |

实现用 `MR_HT_FLASH_INSTALL(_id, MR_NULL, _ops)` 安装，private data 为 `MR_NULL`，五个 callback 的静态 `ops` 表由
`MR_HT_DRIVER_FOREACH` 展开到每个实例。容量与 region 几何一律来自运行时 `probe`， **不写入 HardTree**——这样几何只有一份
来源，不会与芯片寄存器或器件实际值漂移。

片外 Flash（SPI/QSPI/OSPI）的 HardTree 契约不同：控制器实例、片选 GPIO、映射基址等静态资源适合放进 HardTree 属性，而
JEDEC ID、SFDP 等运行时才能确认的容量与擦除几何仍应由 `probe` 返回。

整个实现包在 `#if MR_HT_HAS_DRIVER()` 内；模板里没有该 compatible 实例时，该翻译单元不产生任何符号。

## 9. STM32H5 能力与限制（其他平台必须重新确认）

以下是 H5 backend 的实际情况， **不是**公共层保证；移植到其他器件时逐项重新确认，不要照搬：

| 项                  | STM32H5 现状                                                 | 其他平台需要确认什么                                     |
|---------------------|--------------------------------------------------------------|----------------------------------------------------------|
| sector 大小         | 固定 8 KB                                                    | 是否存在非均匀 sector 布局，需要多个 region              |
| 编程粒度            | 128-bit quadword（`program_size = 16`）                      | 最小写入单位、是否支持字节/半字写入                      |
| 页边界              | 不存在（`page_size = 0`）                                    | 是否存在跨页限制，需要 backend 自行拆分                  |
| 部分字写入          | 支持，靠读回合并；范围外已编程字节导致 `-MR_EIO`             | 是否允许 read-modify-write，是否必须整页/整字            |
| 单次 program 原子性 | 不原子：跨多字失败时前面的字可能已写入                       | 是否有可用的原子性或回滚手段                             |
| 单次 erase 原子性   | 不原子：多 sector 请求中途失败会留下部分擦除                 | 是否支持批量擦除、失败后状态如何                         |
| 对齐要求            | 仅 `erase` 要求 8 KB 对齐；`program` 无对齐要求              | 是否要求 `program` 按 `program_size` 对齐                |
| flash 解锁/加锁     | backend 内部完成，所有返回路径都重新 lock                    | 是否需要初始化控制器、是否有时序模式切换                 |
| busy / timeout      | HAL 映射为 `-MR_EBUSY` / `-MR_ETIMEDOUT`，其余为 `-MR_EIO`   | 器件自身的 busy 轮询、超时与错误状态寄存器               |
| cache               | 依赖 `MR_USE_DCACHE`/`MR_USE_ICACHE` 与 port 层失效 helper   | cache 数量、失效粒度、是否需要写缓冲（write buffer）排空 |
| XIP                 | 阵列本身可执行；backend 不禁止 program/erase 期间取指        | program/erase 期间能否取指、是否需要搬运到 RAM 执行      |
| 地址宽度            | `FLASH_BASE + offset` 不越过 32 位                           | 器件是否映射到 32 位以上的地址空间                       |
| 并发                | backend 不加锁；调用方或上层负责串行化                       | 是否与其它执行上下文、DMA 或双核访问共享该阵列           |
| 错误后的数据完整性  | 参数错误与已编程冲突在动硬件前返回；硬件错误可能留下部分写入 | 部分写入是否可检测、是否需要校验或重试                   |

## 10. 参考实现

```text
mrX/arch/st/stm32h5/platform/drivers/flash/flash.c
```

值得阅读的函数与理由（本文不复制其实现）：

| 函数                  | 读它了解什么                                                   |
|-----------------------|----------------------------------------------------------------|
| `flash_probe`         | 固定几何的填写方式，以及 `page_size = 0` 的合法用法            |
| `flash_region`        | 单 region 均匀几何的写法，以及非法 `index` 返回 `-MR_EINVAL`   |
| `flash_read`          | memory-mapped 直接拷贝的最简形态                               |
| `flash_program`       | unlock/lock 的成对放置、头/主体/尾三段拆分与错误码返回         |
| `flash_program_merge` | 部分 quadword 的读回合并与 “范围外非 `0xFF` 即 `-MR_EIO`” 判定 |
| `flash_program_word`  | 单次 HAL 编程调用与 HAL 状态到 mrX 错误码的映射                |
| `flash_erase`         | 对齐校验、逐 sector 循环、首错即停与 lock 失败的降级上报       |
| `flash_locate`        | 全局 `offset` 到 bank 本地 sector 坐标的换算                   |
| `flash_cache_drop`    | D-cache/ICache alias 失效的调用位置与条件                      |

相关材料：

- `MR_HT_COMPAT`/`MR_HT_HAS_DRIVER`/`MR_HT_DRIVER_FOREACH` 与实例展开规则：`mrX/include/mrX/drivers/mr_driver.h`；
- HAL 状态映射：`mrX/arch/st/stm32h5/include/platform/mr_platform.h` 的 `mr_stm32h5_hal_status`；
- 公共契约测试：`mrX/test/drivers/flash/flash.c`（probe 校验、region 越界、可选 callback 的 `-MR_ENOSYS`）；
- 芯片运行期约定：`mrX/arch/st/stm32h5/README.md` 的 FLASH 一节。

## 11. 移植验收清单

- [ ] `probe` 填充全部 5 个字段，`size`/`program_size`/`regions_num` 非 0，`page_size` 为 0 或不小于 `program_size`。
- [ ] 多次调用 `probe` 返回相同几何，不随调用顺序或缓存状态变化。
- [ ] `regions_num > 0` 时同时实现了 `region`，且所有 region 的 `offset + size` 不越过 `info.size`。
- [ ] 每个 region 的 `erase_size` 是 2 的幂或已文档化，统一几何设备报告单 region。
- [ ] `erase` 对非 `erase_size` 对齐的请求返回 `-MR_EINVAL`，而不是静默扩大到对齐边界。
- [ ] `erase` 覆盖 `[offset, offset + size)` 的完整范围，包括 `offset` 落在 region 中间的情况。
- [ ] `program` 明确其对齐规则：要么接受任意偏移并自行合并，要么对不支持的对齐返回明确错误码。
- [ ] 在已编程区域上的非整字写入不会静默破坏范围外的字节，而是返回明确错误码。
- [ ] `read`/`program` 成功时返回实际字节数，短读/短写语义已在平台文档中写明。
- [ ] 所有 HAL 错误（busy/timeout/error）都映射为负错误码，不透传 0 或正值。
- [ ] 解锁、加锁在所有返回路径上成对；失败后 flash 不会被留在解锁状态。
- [ ] program/erase 的失败路径不承诺原子性，且该限制已写入平台文档。
- [ ] 若阵列可执行或已被 cache，写入后失效了对应的 D-cache/ICache alias。
- [ ] HardTree 只声明 compatible（以及真正需要的静态资源），容量与 geometry 只由 `probe` 提供。
- [ ] 未声明的 compatible 被 `MR_HT_HAS_DRIVER()` 裁掉，翻译单元不残留符号。
- [ ] 不支持的可选 callback 留为 `MR_NULL`，不用假实现冒充硬件能力。
