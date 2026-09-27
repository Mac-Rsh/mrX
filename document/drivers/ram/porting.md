# RAM 驱动移植指南

> 本文说明如何实现 RAM 设备 backend。公共几何和读写 API 见 [RAM](ram.md)。

## 1. 移植边界

RAM 公共层在 setup 时调用 `probe`，缓存 `mr_ram_info_t` 并负责总范围检查。实际 memory-map、控制器初始化、region 属性以及读写方式由
backend 负责。

## 2. 操作表

```c
typedef struct mr_ram_ops {
    mr_err_t (*probe)(const mr_driver_t *drv, mr_ram_info_t *info);
    mr_err_t (*region)(const mr_driver_t *drv, mr_uint32_t index,
                       mr_ram_region_t *region);
    mr_ssize_t (*read)(const mr_driver_t *drv, mr_uint64_t offset,
                       mr_uint8_t *buf, mr_size_t size);
    mr_ssize_t (*write)(const mr_driver_t *drv, mr_uint64_t offset,
                        const mr_uint8_t *buf, mr_size_t size);
} mr_ram_ops_t;
```

`probe` 是 setup 必需能力；`region/read/write` 可按设备能力提供。

## 3. Probe 与 Region

`probe` 返回总容量和 region 数量。若 `regions_num > 0`，backend 必须提供 `region`。

RAM region 用于表达 CPU 映射区或不同属性的内存区域。backend 应保证所有 region 的 offset/size 与总容量一致，并明确 region
的访问属性。

## 4. 读写实现

memory-mapped RAM 可以直接基于映射基址实现 read/write；片外 RAM 控制器也可以通过控制器事务完成。

backend 负责：

- 实际地址 `base + offset` 的安全换算；
- 硬件总线宽度和未对齐访问；
- cache/coherency；
- ECC/error 状态；
- 访问受限 region。

公共层已经做总范围检查，但不会替 backend 处理控制器自己的粒度限制。

## 5. 私有状态与生命周期

私有数据可保存映射基址、控制器、region 表、cache 属性和锁。setup 只通过 probe
获取几何，不代表硬件一定由公共层初始化；需要控制器初始化时应在平台驱动自己的初始化路径中完成。

## 6. HardTree 与安装

`MR_HT_RAM_INSTALL` 用于注册 RAM 实例。静态资源可以来自 HardTree，但容量/region 若能由硬件可靠 probe，应优先由 probe
返回，避免两份信息漂移。

## 7. 当前参考

当前仓库没有 STM32H5 RAM backend。可参考：

- [FLASH 移植指南](../flash/porting.md) 的 probe/region 几何模型；
- `mrX/test/drivers/ram/ram.c` 的公共契约；
- [驱动框架](../driver.md) 的生命周期与注册方式。

## 8. 移植检查清单

- probe 返回 size 非 0；
- regions_num 与 region callback 一致；
- region 不越过总容量；
- 64 位 offset 到目标地址换算无溢出；
- 未对齐和总线宽度行为明确；
- cache/ECC/并发约束有清楚定义；
- read/write 返回实际完成字节数或明确错误。
