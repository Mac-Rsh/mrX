# 内存分配（Malloc）

提供嵌入式友好的通用动态堆分配器（`mr_malloc` / `mr_free`
等），在编译期静态预留的堆池上按需分配与释放任意大小的内存块，供内核对象动态创建与上层组件使用。

| 项目   | 说明                                                                                                            |
|--------|-----------------------------------------------------------------------------------------------------------------|
| 功能宏 | `MR_USE_MALLOC`（内置堆）；`MR_USE_MALLOC_STD`（转发标准库）；`MR_USE_MALLOC_3RD`（Kconfig 预留项，当前无实现） |
| 头文件 | `<mrX/libc/mr_malloc.h>`                                                                                        |
| 依赖   | [字符串](string.md)、[内核初始化](../kernel/initcall.md)                                                        |
| 测试   | `test/libc/malloc.c`                                                                                            |

## 配置

| 配置项              | 类型           | 默认值 | 说明                                                                          |
|---------------------|----------------|--------|-------------------------------------------------------------------------------|
| `MR_USE_MALLOC`     | bool（choice） | y      | 使用内置堆分配器                                                              |
| `MR_USE_MALLOC_STD` | bool（choice） | n      | 宏转发标准库 `malloc` / `free` / `calloc` / `realloc`（需宿主 OS 支持）       |
| `MR_USE_MALLOC_3RD` | bool（choice） | n      | 第三方分配器预留项，当前头文件与源码无对应实现分支                            |
| `MR_CFG_HEAP_SIZE`  | int            | 4096   | 内置堆池大小（字节），范围 `32 ~ 2147483647`，仅非 `MR_USE_MALLOC_STD` 时生效 |

启用 `MR_USE_MALLOC` 时，内核启动阶段会自动调用 `mr_libc_malloc_init`
初始化静态堆池，应用无需手动初始化。若切换到 `MR_USE_MALLOC_STD`
，仅保留四个标准分配宏，其余查询、区域扩展与统计接口均不可用。

## 快速上手

```c
#include <mrX/libc/mr_malloc.h>

void example(void) {
    void *buf = mr_malloc(64);
    if (buf == MR_NULL) {
        return;
    }

    /* 扩容到 256 字节；失败时旧块仍有效 */
    void *newp = mr_realloc(buf, 256);
    if (newp == MR_NULL) {
        mr_free(buf);
        return;
    }
    buf = newp;

    mr_free(buf);
}
```

## 核心机制与约束

- **分配与对齐**：分配块按机器字长（`sizeof(void *)`）对齐，实际可用大小可能大于请求值，可通过
  `mr_malloc_usable_size` 查询。`mr_malloc(0)` 或空间不足返回 `MR_NULL`，空间不足同时递增
  `fail_count`。
- **清零与重分配**：`mr_calloc(num, size)` 仅清零请求的 `num * size`
  字节（不承诺清零因未拆分而多出的尾部），乘积溢出或为 0 返回 `MR_NULL`。
  `mr_realloc` 总是新分配块、拷贝旧数据（长度为新旧可用大小的较小值）并释放旧块，不做原地扩展；
  `mr_realloc(MR_NULL, size)` 等价于 `mr_malloc(size)`，`mr_realloc(mem, 0)`
  释放旧块并返回 `MR_NULL`。
- **释放与指针前置条件**：`mr_free(MR_NULL)` 和 `mr_malloc_usable_size(MR_NULL)`
  安全。非空参数必须是当前内置分配器返回且尚未释放的块首地址，不可重复释放或传入野指针；魔数校验只在读取块头之后进行，无法防野指针越界读。
- **碎片与选型**：内置堆采用首次适应（first-fit）+ 按地址升序空闲链 + 相邻空闲块双向合并。
  `mr_malloc` 与 `mr_free`
  最坏时间复杂度与空闲碎片数成正比。固定大小对象的频繁分配/释放建议使用无碎片的 [Slab](../kernel/slab.md)。
- **并发与中断安全**：`mr_malloc`、`mr_free` 与 `mr_heap_information` 通过平台关中断（
  `mr_port_irq_disable/enable`）保护堆链表，最坏关中断时长随碎片数线性增长，不建议作为通用
  ISR-safe 接口。`mr_malloc_usable_size` 无锁；`mr_heap_region_add`
  无锁，仅限初始化阶段外部串行调用。

## API 速查

| API                                                        | 说明                                                          |
|------------------------------------------------------------|---------------------------------------------------------------|
| `void *mr_malloc(mr_size_t size)`                          | 分配指定字节数的对齐内存块，失败或 `size == 0` 返回 `MR_NULL` |
| `void mr_free(void *mem)`                                  | 释放内存块，`MR_NULL` 安全                                    |
| `void *mr_calloc(mr_size_t num, mr_size_t size)`           | 分配并清零 `num * size` 字节，溢出或为 0 返回 `MR_NULL`       |
| `void *mr_realloc(void *mem, mr_size_t size)`              | 重新分配大小并保留数据前缀，失败返回 `MR_NULL` 且旧块不变     |
| `mr_size_t mr_malloc_usable_size(void *mem)`               | 查询已分配块的实际可用字节数，`MR_NULL` 或魔数不匹配返回 `0`  |
| `void mr_heap_region_add(const mr_heap_region_t *regions)` | 向堆追加额外内存区域（如外部 SRAM），仅限初始化阶段调用       |
| `mr_heap_info_t mr_heap_information(mr_bool_t lazy)`       | 获取堆统计快照；`lazy` 为 `MR_TRUE` 时跳过全链扫描            |

**`mr_heap_region_add` 约束**：`regions` 数组须以 `address == MR_NULL` 终止（实现遇到
`size == 0` 也会停止）；每个区域只能添加一次、不得与已有堆区域重叠、生命周期须覆盖分配器全程，过小区域会被静默跳过。

**`mr_heap_information` 说明**：`lazy` 传 `MR_TRUE` 时跳过全链扫描，不更新
`max_free_bytes`（最大空闲块）、`disp_kb_ratio` 与 `frag_ratio`（千分比），适合高频或低延迟采样；传
`MR_FALSE` 时刷新全部字段。

```c
static mr_uint8_t ext_ram[1024];
const mr_heap_region_t regions[] = {
    { .address = ext_ram, .size = sizeof(ext_ram) },
    { .address = MR_NULL, .size = 0 },
};
mr_heap_region_add(regions);
```

## 内部实现

核心实现位于 `libc/malloc.c`，头文件类型定义在 `include/mrX/libc/mr_malloc.h`：

```text
__heap_blk_head --> [ free_blk (magic=~MR_HEAP_MAGIC) ] --> [ free_blk ] --> NULL
                         ^
                         | 按地址升序排列，释放时双向合并相邻空闲块
```

- **块头结构（`mr_heap_block_t`）**：包含 `magic`（已分配为 `MR_HEAP_MAGIC` 即
  `0x6d725800`，空闲为其按位取反）、`next`（空闲链表指针）与 `size`（可用负载字节数）。
- **分配与拆分**：`mr_malloc` 将请求长度按 `MR_ALIGN_SIZE` 向上对齐，从
  `__heap_blk_head` 顺序查找首个满足大小的空闲块；若剩余空间大于
  `HEAP_SPLIT_THRESHOLD`（4 个块头大小）则拆出新空闲块插回链表，否则整块交付。
- **释放与合并**：`mr_free` 由负载指针反推块头并校验 `magic`，置为空闲后经
  `heap_insert_block` 按地址插入空闲链，并尝试与前后相邻空闲块合并。
- **初始化**：内核启动时 `kernel/initcall.c` 调用 `mr_libc_malloc_init`，将静态数组
  `__heap[MR_CFG_HEAP_SIZE]` 包装为首个空闲块挂入链表。
