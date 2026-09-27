# FLASH 驱动移植指南

> 本文说明如何实现片上或片外 FLASH backend。公共 API 与几何模型见 [FLASH](flash.md)。

## 1. 移植边界

FLASH 公共层在 setup 时调用 `probe` 并缓存 `mr_flash_info_t`，之后负责设备总范围检查。页对齐、program 规则、擦除
geometry、跨页拆分、busy 等待和器件协议都由 backend 负责。

## 2. 操作表

```c
typedef struct mr_flash_ops {
    mr_err_t (*probe)(const mr_driver_t *drv, mr_flash_info_t *info);
    mr_err_t (*region)(const mr_driver_t *drv, mr_uint32_t index,
                       mr_flash_region_t *region);
    mr_ssize_t (*read)(const mr_driver_t *drv, mr_uint64_t offset,
                       mr_uint8_t *buf, mr_size_t size);
    mr_ssize_t (*program)(const mr_driver_t *drv, mr_uint64_t offset,
                          const mr_uint8_t *buf, mr_size_t size);
    mr_err_t (*erase)(const mr_driver_t *drv, mr_uint64_t offset,
                      mr_uint64_t size);
} mr_flash_ops_t;
```

`probe` 是 setup 必需能力；`region/read/program/erase` 可按设备能力提供。

## 3. Probe 与几何

`probe` 至少要返回有效总容量，并根据器件能力填写 program/page/region 等信息。若设备包含多种 erase geometry，通过 `region` 按
index 返回 `mr_flash_region_t`。

建议从真实芯片参数、JEDEC/SFDP、控制器寄存器或板级描述中得到 geometry，而不是在公共层重复保存同一份信息。

## 4. Read / Program / Erase

backend 负责：

- program 对齐与最小编程粒度；
- page boundary；
- erase 对齐和 region erase_size；
- 写前擦除要求；
- busy/timeout/error 状态；
- 片外总线事务和片选；
- cache、memory-map 或 XIP 一致性。

公共层只保证请求没有越过 `info.size`，不会替 backend 自动拆分跨页操作。

## 5. 私有状态与并发

私有数据可保存控制器、总线设备、映射基址、geometry cache、锁和当前操作状态。若硬件不允许 read 与 program/erase 并发，backend
自己完成串行化或返回 busy。

如果程序本身从同一 FLASH 执行，要额外处理芯片对 program/erase 期间取指的限制。

## 6. HardTree 与安装

`MR_HT_FLASH_INSTALL` 只负责注册实例，不要求把容量和 region geometry 固化到 HardTree。适合写入 HardTree
的通常是控制器/总线连接、片选、映射基址等静态资源。

## 7. 当前参考

当前可参考：

```text
mrX/arch/st/stm32h5/platform/drivers/flash/flash.c
```

该 backend 展示了 `MR_HT_FLASH_INSTALL`、均匀扇区 probe/region、128-bit program 与 cache 一致性处理。公共契约的行为约束见
`mrX/test/drivers/flash/flash.c`。

## 8. 移植检查清单

- probe 返回的总容量非 0 且稳定；
- region 覆盖范围与 erase geometry 正确；
- program/page/erase 对齐错误由 backend 明确处理；
- 跨页 program 行为明确；
- timeout/busy/error 不会留下半活动事务；
- XIP/cache/memory-map 一致性明确；
- stop/abort 不存在时也不会让公共生命周期误认为硬件已停止。
