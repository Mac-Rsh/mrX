# RAM 驱动移植指南

> 本文面向 backend 开发者，说明如何实现 RAM backend 的几何发现、region
> 与读写契约，并明确公共层已经承担的部分。
> 公共 API 与用户语义见 [RAM 设备](ram.md)。

## 1. 移植边界

公共层在 `mr_driver_setup()` 阶段调用一次 `probe`，把返回的 `mr_ram_info_t` 缓存进
`mr_ram_data_t.info`，此后按这份缓存做
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

`region` 的“必需”由 `ram_init` 显式检查：`probe` 成功后若 `regions_num != 0` 且
`rm_ops->region == MR_NULL`，setup 返回
`-MR_EINVAL` 并不缓存几何。因此 `regions_num > 0` 的 backend 必须提供 `region`
；只有纯间接访问 RAM 可以用
`regions_num = 0` 配上仅有 `probe`/`read`/`write` 的 ops 表。运行期的
`-MR_ENOSYS` 只在 `regions_num == 0` 时通过
`mr_ram_region(0, ...)` 出现（`index` 越界检查在能力检查之后）。

公共层在 `mr_ram_read/write` 中先做能力检查再做范围检查，因此越界请求在 callback
缺失时返回的是 `-MR_ENOSYS` 而不是
`-MR_EINVAL`。

## 3. 存储区域与访问

backend 负责把公共 RAM 设备的读写操作映射到真实存储区域，按公共接口验证边界、容量与返回长度。内存映射、总线寻址、对齐及缓存需求由平台决定；公共 RAM 层不保证特定芯片上存在此设备。

## 4. 配置与生命周期

初始化时确认实际容量与读写能力。动态创建、设备 setup/shutdown 与私有资源释放遵循通用驱动生命周期；必要时拒绝未就绪或不支持的请求，不得返回伪成功。

## 5. HardTree 与安装

将静态地址、总线、片选及控制器属性留给平台，使用公共 RAM 驱动注册接口连接操作表。平台实现是否存在不构成公共 API 的能力保证。

## 6. 移植验收

- [ ] 读写长度、偏移、越界和错误码按公共接口实现。
- [ ] 访问期间的缓存、总线和设备生命周期安全。
