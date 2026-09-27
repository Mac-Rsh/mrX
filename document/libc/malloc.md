# 内存分配（Malloc）

> 内存分配模块提供嵌入式友好的通用堆分配器（`mr_malloc` / `mr_free` 等），在编译期
> 静态预留的堆池上按需分配与释放任意大小的内存块，供内核对象动态创建与上层组件使用。

| 项       | 说明                                                                                                                                                                     |
|----------|--------------------------------------------------------------------------------------------------------------------------------------------------------------------------|
| 功能宏   | `MR_USE_MALLOC`（默认内置实现）；`MR_USE_MALLOC_STD` 转发标准库；`MR_USE_MALLOC_3RD` 目前仅为 Kconfig 预留项，代码尚无后端实现                                           |
| 头文件   | `<mrX/libc/mr_malloc.h>`                                                                                                                                                 |
| 依赖模块 | [字符串 String](string.md)（内部实现使用 `mr_memcpy` / `mr_memset`）、平台中断接口（`mr_port_irq`，用于临界区保护）、[内核初始化](../kernel/initcall.md)（调用堆初始化） |
| 参考测试 | `mrX/test/libc/malloc.c`                                                                                                                                                 |

---

## 用户指南

### 1. 模块概述

内存分配模块解决的问题是： **在没有操作系统堆服务、也不想引入完整 C 库堆的目标板上，
提供一个确定大小、行为可预期的通用内存分配器**。

特性：

- 堆池编译期静态预留（`MR_CFG_HEAP_SIZE` 字节的静态数组），不依赖链接脚本或运行期探测。
- 首次适应（first-fit）分配 + 按地址排序的空闲链 + 相邻空闲块双向合并。`mr_malloc` 查找和 `mr_free`
  重新插入都需要线性扫描空闲块链，最坏开销与空闲碎片数成正比。
- `mr_malloc_usable_size` 可查询任意已分配块的实际可用大小。
- `mr_calloc`（清零分配）与 `mr_realloc`（扩缩容并保留数据）完备。
- `mr_heap_region_add` 支持在初始化阶段追加额外的内存区域（如外部 SRAM）。
- `mr_heap_information` 提供总量、占用、空闲、失败次数、碎片数、最大空闲块等统计信息。
- 分配类 API 通过 `MR_NULL` 表示失败，并维护部分分配失败统计；释放/查询接口仍要求调用者传入满足其所有权前置条件的指针，magic
  只能发现部分误用，不能把任意野指针变成安全输入。

与 [内核 Slab](../kernel/slab.md) 的分工：两者相互独立、互不依赖。slab 在调用者缓冲上按 **固定块大小**
用原子位图管理，没有每块头部和变长堆的外部碎片问题；本模块在通用堆上按 **任意大小**
分配，适合对象数量与大小运行期才确定的场合（如内核定时器、工作队列的动态创建）。内核组件的动态创建路径（timer、work、wait、irq、fifo
等）使用的 正是本模块的 `mr_malloc`。

### 2. 配置与依赖

| 配置项              | 类型           | 默认值 | 说明                                                                                                               |
|---------------------|----------------|--------|--------------------------------------------------------------------------------------------------------------------|
| `MR_USE_MALLOC`     | bool（choice） | y      | 使用内置堆实现（`mrX/include/mrX/libc/Kconfig:69`）                                                                |
| `MR_USE_MALLOC_STD` | bool（choice） | n      | 直接宏转发标准库 `malloc` / `free`，需要宿主 OS 支持                                                               |
| `MR_USE_MALLOC_3RD` | bool（choice） | n      | 第三方分配器预留选项；当前头文件/源码没有对应实现分支                                                              |
| `MR_CFG_HEAP_SIZE`  | 整型           | 4096   | 内置堆池大小（字节），范围 32 ~ 2147483647，仅在非 `MR_USE_MALLOC_STD` 时生效（`mrX/include/mrX/libc/Kconfig:76`） |

选择 `MR_USE_MALLOC` 并重新生成 `mr_config.h` 后，包含头文件即可使用：

```c
#include <mrX/libc/mr_malloc.h>
```

选择 `MR_USE_MALLOC_STD` 时，`mr_malloc` / `mr_free` / `mr_calloc` / `mr_realloc`
退化为标准库同名函数的宏；`mr_malloc_usable_size`、`mr_heap_region_add`、
`mr_heap_information` 及堆统计结构不可用。堆初始化由内核启动序列自动完成 （`MR_USE_MALLOC` 时内核初始化调用
`mr_libc_malloc_init`），应用无需手动初始化。

### 3. 快速上手

分配、使用、释放一个缓冲：

```c
#include <mrX/libc/mr_malloc.h>

void example(void) {
    void *buf;

    buf = mr_malloc(128);
    if (buf == MR_NULL) {
        /* Allocation failed: heap exhausted */
        return;
    }

    /* Use buf here */

    mr_free(buf);
}
```

要点：

- `mr_malloc` 失败返回 `MR_NULL`，使用前必须判空。
- `mr_malloc(0)` 返回 `MR_NULL`；`mr_free(MR_NULL)` 安全无操作。
- 释放后的指针不可再使用。

### 4. 使用说明

**分配语义**

- 分配得到的块按机器字长（`sizeof(void *)`）对齐，实际可用大小不小于请求大小， 可用 `mr_malloc_usable_size` 查询。
- 请求大小为 0 返回 `MR_NULL`；堆空间不足返回 `MR_NULL` 并使失败计数加一。
- `mr_calloc(num, size)` 分配 `num * size` 字节，并清零 **请求的这部分字节**；若底层分配块的 usable size 因未拆分而更大，额外尾部不在
  `mr_calloc` 的清零承诺内。乘积溢出或为 0 返回 `MR_NULL`。
- `mr_realloc(mem, size)` 总是分配新块、拷贝旧数据（拷贝长度为旧可用大小与 新大小的较小者）、释放旧块并返回新指针；
  `mr_realloc(MR_NULL, size)` 等价于
  `mr_malloc(size)`；`mr_realloc(mem, 0)` 释放 `mem` 并返回 `MR_NULL`； 扩容失败时旧块保持有效，返回 `MR_NULL`。

**释放语义**

- `mr_free(MR_NULL)` 安全。
- 除 `MR_NULL` 外，`mr_free` 的输入应是当前内置分配器返回且尚未释放的块首地址。实现会先通过 `mem - sizeof(block)` 读取块头，再检查
  magic，因此 **不能**用它安全探测任意野指针；重复 free 也不属于受保证用法。
- `mr_malloc_usable_size(MR_NULL)` 返回 0；非空参数同样应是当前有效分配块。magic 不匹配时返回 0，但这不是对任意地址的内存安全校验。

**调用上下文**

- `mr_malloc`、`mr_free`、`mr_heap_information` 用 `mr_port_irq_disable/enable`
  保护内置堆的共享链表/统计；first-fit、按地址插入和完整统计扫描都可能在线性碎片链上运行，因此最坏关中断时间并非常数。
- `mr_malloc_usable_size` 不加锁；`mr_calloc/realloc` 是由若干基础操作组合而成；`mr_heap_region_add`
  完全依赖调用者在初始化阶段外部串行化。模块不能据此统一承诺所有 API 在所有平台都 ISR-safe。

**堆统计**

`mr_heap_information(lazy)` 返回堆信息快照：`lazy` 传 `MR_TRUE` 时跳过最耗时的 全链扫描（不更新最大空闲块与碎片比例字段），适合在中断或频繁调用场合使用；
传 `MR_FALSE` 时全部字段完整刷新。

### 5. API 参考

分配与释放：

| API                     | 说明                       |
|-------------------------|----------------------------|
| `mr_malloc`             | 分配指定大小的内存块       |
| `mr_free`               | 释放内存块                 |
| `mr_calloc`             | 分配并清零                 |
| `mr_realloc`            | 重新分配大小并保留原有数据 |
| `mr_malloc_usable_size` | 查询内存块的实际可用大小   |

堆管理与诊断：

| API                   | 说明                                   |
|-----------------------|----------------------------------------|
| `mr_heap_region_add`  | 向堆追加额外的内存区域（仅初始化阶段） |
| `mr_heap_information` | 获取堆统计信息快照                     |

**`mr_malloc`**

```c
void *mr_malloc(mr_size_t size);
```

- `size`：请求字节数，为 0 时返回 `MR_NULL`。
- 返回：成功返回对齐的内存块指针，失败返回 `MR_NULL`。

**`mr_free`**

```c
void mr_free(void *mem);
```

- `mem`：`MR_NULL` 安全；其他值应为当前内置分配器返回且尚未释放的块首地址。magic 检查只在读取推定块头之后发生，不是通用野指针验证。
- 返回：无。

**`mr_malloc_usable_size`**

```c
mr_size_t mr_malloc_usable_size(void *mem);
```

- 返回：当前有效分配块的实际可用字节数（通常不小于请求值）；`MR_NULL` 返回 0。非空参数必须满足有效块前置条件，magic 不匹配时实现返回
  0。

**`mr_calloc`**

```c
void *mr_calloc(mr_size_t num, mr_size_t size);
```

- `num` / `size`：块数与块大小，乘积溢出（`num * size` 超过 `MR_SIZE_MAX`）或为 0 返回 `MR_NULL`。
- 返回：成功返回全零内存块指针，失败返回 `MR_NULL`。

**`mr_realloc`**

```c
void *mr_realloc(void *mem, mr_size_t size);
```

- `mem`：旧块指针，可为 `MR_NULL`（等价于 `mr_malloc`）。
- `size`：新大小，为 0 时释放旧块并返回 `MR_NULL`。
- 返回：成功返回新块指针（原数据前缀被保留），失败返回 `MR_NULL`（旧块不变）。

**`mr_heap_region_add`**

```c
void mr_heap_region_add(const mr_heap_region_t *regions);
```

- `regions`：区域数组。公开约定用 `address == MR_NULL` 终止；当前实现的循环条件还会在遇到 `size == 0` 时直接结束后续遍历。
- 约束：每个区域只能添加一次、不得与任何堆区域重叠、生命周期须覆盖分配器全程；这些约束由调用者保证，core
  不做重叠/重复检测。过小区域会被跳过；调用者须保证串行调用。

**`mr_heap_information`**

```c
mr_heap_info_t mr_heap_information(mr_bool_t lazy);
```

- `lazy`：`MR_TRUE` 跳过全链扫描（不更新 `max_free_bytes`、`disp_kb_ratio`、
  `frag_ratio`），`MR_FALSE` 完整刷新。
- 返回：堆信息结构体（按值返回）。

### 6. 完整示例

示例 1：动态缓冲的扩容与校验（改编自 `mrX/test/libc/malloc.c` 的 `realloc_resize`）：

```c
void realloc_example(void) {
    void *p;
    void *newp;

    p = mr_malloc(64);
    if (p == MR_NULL) {
        return;
    }

    /* Grow to 256 bytes, old data prefix is preserved */
    newp = mr_realloc(p, 256);
    if (newp == MR_NULL) {
        mr_free(p);                 /* realloc failure keeps the old block valid */
        return;
    }
    p = newp;

    mr_free(p);
}
```

示例 2：使用堆统计观察占用与碎片：

```c
void heap_stat_example(void) {
    mr_heap_info_t info;
    void *p;

    p = mr_malloc(256);
    info = mr_heap_information(MR_TRUE);

    /* info.total_bytes / used_bytes / free_bytes / fail_count ... */
    (void)info;

    mr_free(p);
    info = mr_heap_information(MR_FALSE);   /* Full scan for frag ratio */
}
```

示例 3：初始化阶段追加外部 RAM 区域：

```c
void region_example(void) {
    static mr_uint8_t ext_ram[1024];
    const mr_heap_region_t regions[] = {
        {.address = ext_ram,    .size = sizeof(ext_ram)},
        {.address = MR_NULL,    .size = 0}
    };

    mr_heap_region_add(regions);
}
```

更多行为参考（清零、碎片复用、失败路径、统计一致性）见 `mrX/test/libc/malloc.c`。

### 7. 常见问题（FAQ）

**Q：`mr_malloc` 返回 `MR_NULL`，但堆明明没满？**

堆是首次适应 + 空闲块合并的分配器：即使空闲总量足够，也可能没有单个空闲块能 容纳请求。用 `mr_heap_information(MR_FALSE)` 查看
`max_free_bytes`（最大空闲块） 而非 `free_bytes`（空闲总量）；两者差距大说明碎片化严重，可考虑改用
[内核 Slab](../kernel/slab.md) 池化固定大小的对象。

**Q：为什么 `mr_realloc` 之后地址变了？**

`mr_realloc` 的实现是"分配新块 + 拷贝 + 释放旧块"，不做原地扩展，返回地址通常 与原来不同；持有旧指针的别名必须更新。

**Q：`mr_free` 传了野指针会怎样？**

不要这样调用。实现会先根据传入地址向前定位并读取块头，然后才检查 magic；真正的野指针可能连块头读取都不安全。magic
只能辅助发现某些错误（例如仍可访问的非活动块），不能替代指针所有权和生命周期管理。

**Q：`mr_malloc_usable_size` 为什么比请求值大？**

块大小按机器字长对齐，且块大小不足以拆分时会整块交给调用者，实际可用大小 因此可能大于请求值。写入时不要超过该值，读取该值可用于安全地重分配/拷贝。

**Q：中断里能调用 `mr_malloc` 吗？**

不建议把它当作统一 ISR-safe API。内置 `malloc/free` 确实通过平台 IRQ 屏蔽保护共享状态，但查找/插入可能线性扫描碎片链，且能否在
ISR 嵌套语义下正确使用取决于 `mr_port_irq_disable/enable` 的平台实现。中断路径更适合预分配或固定块池。

---

## 开发者指南

### 8. 架构总览

内存分配是 libc 服务层模块，位于内核与各组件之下：

```
        +------------------------------------------+
        | 内核组件动态创建（timer/work/wait/irq/fifo）|
        +---------------------+--------------------+
                              | mr_malloc()/mr_free()
        +---------------------v--------------------+
        | 内存分配 Malloc                           |
        |  静态堆池 __heap[MR_CFG_HEAP_SIZE]        |
        |  空闲块单向链表（按地址升序）+ 统计         |
        +---------------------+--------------------+
                              | 依赖
        +---------------------v--------------------+
        | mr_string（memset/memcpy）、mr_port_irq   |
        +------------------------------------------+
```

关键接线：

- 内核启动序列在 `mrX/kernel/initcall.c:53` 调用 `mr_libc_malloc_init()`
  （`mrX/libc/malloc.c:363`），把静态堆池初始化为第一个空闲块。
- [内核 Slab](../kernel/slab.md) 不依赖本模块：slab 的位图与块缓冲由调用者静态 提供，分配走原子位操作（
  `mrX/kernel/slab.c:27`），全程不触碰通用堆。
- 内核格式化输出只在启用 `MR_USE_PRINTF_SAFE` 时为每次调用使用 `mr_malloc` 取得独立缓冲；默认 Printf 使用静态缓冲。

### 9. 数据结构

块头（`mrX/include/mrX/libc/mr_malloc.h:40`）：

```c
typedef struct mr_heap_block {
    volatile mr_uint32_t magic;     /* Allocated: MR_HEAP_MAGIC, free: ~MR_HEAP_MAGIC */
    struct mr_heap_block *next;     /* Next free block (free blocks only) */
    mr_size_t size;                 /* Usable payload size of this block */
} mr_heap_block_t;
```

| 字段    | 说明                                                                                                                       |
|---------|----------------------------------------------------------------------------------------------------------------------------|
| `magic` | 魔数校验：已分配为 `MR_HEAP_MAGIC`（`0x6d725800`），空闲为其按位取反；`mr_free` / `mr_malloc_usable_size` 依据它识别有效块 |
| `next`  | 空闲块链表指针；块被分配后置 `MR_NULL` 并从链表摘除                                                                        |
| `size`  | 本块负载大小（不含块头），即 `mr_malloc_usable_size` 的返回值                                                              |

全局状态（`mrX/libc/malloc.c:21-25`）：

- `__heap[MR_CFG_HEAP_SIZE]`：静态堆池，按 `MR_ALIGN_SIZE`（机器字长）对齐。
- `__heap_blk_head`：表头哨兵，`next` 指向地址最小的空闲块；链表按地址升序排列， 这是相邻合并能一次遍历完成的前提。
- `__heap_info`：统计结构（`mr_heap_info_t`，`mr_malloc.h:54`），含
  `total_bytes` / `used_bytes` / `free_bytes` / `fail_count` / `frag_num` /
  `max_free_bytes` / `disp_kb_ratio` / `frag_ratio`。

区域描述（`mr_malloc.h:48`）：`mr_heap_region_t { void *address; mr_size_t size; }`， 用于 `mr_heap_region_add`。

### 10. 核心流程

**初始化**：`mr_libc_malloc_init`（`mrX/libc/malloc.c:363`）用 `heap_block_make`
把整个 `__heap` 包装成一个空闲块（块头之后预留一个对齐隔离带，保证负载对齐）， 挂到 `__heap_blk_head.next`，并初始化统计。

**分配（first-fit）**：`mr_malloc`（`mrX/libc/malloc.c:85`）：

1. 关中断（`mr_port_irq_disable`）。
2. 请求大小按 `MR_ALIGN_SIZE` 向上对齐；对齐后溢出返回失败。
3. 从链表头顺序找第一个 `size >= 请求` 的空闲块；找不到返回 `MR_NULL` 并累加
   `fail_count`。
4. 摘下该块、写入 `MR_HEAP_MAGIC`；若剩余部分大于拆分阈值
   `HEAP_SPLIT_THRESHOLD`（4 个块头大小，`malloc.c:29`），则拆出新空闲块插回 链表，否则整块交付（这也是可用大小可能大于请求值的原因）。
5. 更新 `used_bytes` / `free_bytes`，开中断返回。

**释放与合并**：`mr_free`（`malloc.c:162`）由传入 payload 指针反推块头并检查 magic；有效块标记为空闲后，`heap_insert_block`（
`malloc.c:31`）线性扫描到按地址排序的位置，再检查前后两个相邻块是否可合并。定位是 O (空闲碎片数)，定位后的双向邻接合并本身是常数次判断。

**重分配**：`mr_realloc`（`malloc.c:234`）=`mr_malloc` + `mr_memcpy`（拷贝长度为 新旧可用大小的较小者）+ `mr_free`，无原地扩展路径；
`mr_calloc`（`malloc.c:208`） = `mr_malloc` + `mr_memset`，入口先做乘积溢出检查。

**追加区域**：`mr_heap_region_add`（`malloc.c:274`）对每个区域用 `heap_block_make`
构造块头与隔离带（跳过放不下"两个拆分阈值 + 对齐"的区域），再把剩余空间作为一个 空闲块插入链表并累加统计。

**统计刷新**：`mr_heap_information`（`malloc.c:342`）关中断后按 `lazy` 决定是否 调用 `heap_information_full_update`（
`malloc.c:306`）遍历空闲链求最大空闲块、 按空闲块数与空闲字节数计算离散度 `disp_kb_ratio` 与碎片比 `frag_ratio`
（均以千分比 ‰ 表示、上限 1000），最后按值拷贝快照返回。

### 11. 并发与中断安全

- `mr_malloc` / `mr_free` / `mr_heap_information` 通过 `mr_port_irq_disable/enable` 串行化内置堆共享状态；这是单核 IRQ
  临界区设计，不等价于多核互斥。
- `mr_malloc` 的 first-fit、`mr_free` 的按地址插入以及 `mr_heap_information(MR_FALSE)` 的完整扫描都可能遍历空闲链，因此最坏关中断时长与碎片数量成正比。
- `mr_malloc_usable_size` 没有锁，只应在调用者独占该活动块生命周期时查询；`mr_calloc/realloc` 的 memset/memcpy
  在基础分配临界区之外完成。
- `mr_heap_region_add` 无内部锁，仅限堆已初始化且并发堆操作开始前调用，并由调用者外部串行化。
- `magic` 是 `volatile` 状态标志而非原子同步或地址验证机制；多核和同一块的并发生命周期操作需要更高层同步。

### 12. 扩展与派生

本模块没有对象 clazz，也没有注册式回调，扩展面是 **编译期后端选择 + 运行期堆区域追加**：

- **后端选择**：`MR_USE_MALLOC_STD` 时头文件把整个 API 宏转发标准库（`mrX/include/mrX/libc/mr_malloc.h:29-33`），
  `MR_USE_MALLOC` 时切换到内置堆（`mr_malloc.h:35-129`）；两种形态下内部实现源文件整体被门控（`mrX/libc/malloc.c:10`
  ），互斥参与编译。
- **第三方后端**：`MR_USE_MALLOC_3RD` 已在 choice 中定义（`mrX/include/mrX/libc/Kconfig:64`），但头文件目前只有上述两个分支，代码中没有任何
  `MR_USE_MALLOC_3RD` 分支——该选项是预留位，当前不存在可用的第三方分配器接入路径。
- **追加内存区域**：`mr_heap_region_add`（`mrX/libc/malloc.c:274`）是唯一的运行期扩展入口，可在初始化阶段把外部 SRAM
  等区域并入同一空闲链；区域列表以 `address == MR_NULL` 结尾，每个区域只能加入一次、不得与已有区域重叠且须终身有效，调用方还须自行串行化。
- **容量与诊断**：`MR_CFG_HEAP_SIZE`（`malloc.c:15-20` 回退与最小值检查）决定内建堆池大小；
  `mr_heap_information` / `mr_malloc_usable_size` 只属于内置后端（声明在 `mr_malloc.h:34` 之后的 `#else` 分支），切到 STD
  后端这些接口连同统计信息一起消失。

追加区域与统计的可观测性有测试固定：`mrX/test/libc/malloc.c:244`（新增区域后 `total_bytes` / `free_bytes` 增长）、
`malloc.c:263`（小于阈值的区域被静默跳过）。

### 13. 源码导读

| 位置                                  | 职责                                            |
|---------------------------------------|-------------------------------------------------|
| `mrX/include/mrX/libc/mr_malloc.h:29` | 两种后端的编译期切换与块头/区域/统计类型        |
| `mrX/libc/malloc.c:14`                | 堆池定义、`MR_CFG_HEAP_SIZE` 回退值与最小值检查 |
| `mrX/libc/malloc.c:31`                | 按地址有序插入 + 双向相邻合并                   |
| `mrX/libc/malloc.c:85`                | first-fit 分配与拆分                            |
| `mrX/libc/malloc.c:162`               | 释放（魔数校验）                                |
| `mrX/libc/malloc.c:234`               | `mr_realloc`（分配 + 拷贝 + 释放）              |
| `mrX/libc/malloc.c:274`               | 追加内存区域                                    |
| `mrX/libc/malloc.c:306`               | 全量统计刷新（最大空闲块与碎片比例）            |
| `mrX/libc/malloc.c:363`               | 堆初始化（内核启动序列调用）                    |
| `mrX/kernel/initcall.c:53`            | 内核初始化中的调用点                            |
| `mrX/test/libc/malloc.c`              | 分配/释放/清零/扩缩容/区域/统计的行为级测试     |

### 14. 附录

**配置宏**

| 宏                  | 说明                                                                            |
|---------------------|---------------------------------------------------------------------------------|
| `MR_USE_MALLOC`     | 内置堆实现开关（choice，默认）                                                  |
| `MR_USE_MALLOC_STD` | 转发标准库 malloc/free（choice）                                                |
| `MR_USE_MALLOC_3RD` | 第三方分配器预留 choice；当前无头文件/源码实现分支                              |
| `MR_CFG_HEAP_SIZE`  | 堆池字节数，默认 4096，最小 32（`malloc.c:18` 处编译期检查，不满足则 `#error`） |
| `MR_HEAP_MAGIC`     | 已分配块魔数 `0x6d725800`（内部实现细节）                                       |

**返回值与错误**：本模块 API 通过返回值 `MR_NULL` / `0` 表达失败，不返回负数 错误码；分配失败可通过 `mr_heap_information` 的
`fail_count` 观测。
