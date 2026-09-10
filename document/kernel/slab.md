# 内存块分配器（Slab）

> 内存块分配器在用户提供的静态缓冲上按固定块大小做无锁分配：位图跟踪每个块的
> 占用状态，分配/释放均为原子位操作，适合数量与大小固定的对象池，避免通用堆的
> 碎片与加锁开销。

| 项       | 说明                                                                                                                            |
|----------|---------------------------------------------------------------------------------------------------------------------------------|
| 功能宏   | 无（始终编译，无 `MR_USE_*` 开关与 Kconfig 项）                                                                                 |
| 头文件   | `<mrX/kernel/mr_slab.h>`                                                                                                        |
| 依赖模块 | 无强依赖（位操作基于 `mrX/include/mrX/libc/mr_atomic.h` 原子原语；与 [内存分配 malloc](../libc/malloc.md) 相互独立，见第 1 节） |
| 参考测试 | `mrX/test/kernel/slab.c`                                                                                                        |

---

## 用户指南

### 1. 模块概述

内存块分配器解决的问题是： **为一类大小固定的对象提供零碎片、零块头开销、 无锁的快速分配**。

特性：

- 固定块大小：一个 slab 内所有块等长（`size`），按位图逐块占用管理。
- 存储由用户提供：位图与块缓冲都由调用者分配（通常是静态数组），模块本身不 申请内存。
- 无锁并发安全：`mr_slab_alloc` 以原子 CAS 抢占位图位，`mr_slab_free` 以原子 位清除释放，多线程/中断上下文并发调用无需外层加锁。
- 与 libc 的 `malloc` 相互独立：`malloc`（`MR_USE_MALLOC`）在 `MR_CFG_HEAP_SIZE`
  指定的通用堆上按需分配任意大小（见 [内存分配 malloc](../libc/malloc.md)）， slab 不使用该堆、不使用 `mr_malloc`
  ，两者可并存并按场景选用。

选用建议：对象大小与数量编译期已知（消息槽、缓冲帧池、固定句柄表）优先 slab； 大小不定或数量运行期才确定的分配走 `malloc`。

### 2. 配置与依赖

本模块没有功能宏与 Kconfig 选项，始终编译。包含头文件即可使用：

```c
#include <mrX/kernel/mr_slab.h>
```

头文件自动引入 `mr_service.h` 与 `mr_atomic.h`，无需手动包含。

**关键换算宏**

| 宏                       | 说明                                                  |
|--------------------------|-------------------------------------------------------|
| `MR_SLAB_MAP_BITS`       | 每个 `mr_atomic_t` 位图字管理的块数（32 位平台为 32） |
| `MR_SLAB_MAP_SIZE(_num)` | 管理 `_num` 个块所需的位图字节数，用于声明位图数组    |

使用前需准备两块存储：位图数组（大小 `MR_SLAB_MAP_SIZE(num)`，且按
`sizeof(mr_atomic_t)` 对齐）与块缓冲数组（大小 `size * num`）。

### 3. 快速上手

一个 64 字节 × 16 块的静态对象池（改编自 `mrX/test/kernel/slab.c` 的全局定义）：

```c
#include <mrX/kernel/mr_slab.h>

#define OBJ_SIZE   64
#define OBJ_NUM    16
#define MAP_SIZE   MR_SLAB_MAP_SIZE(OBJ_NUM)

static mr_uint8_t pool_map[MAP_SIZE];
static mr_uint8_t pool_buf[OBJ_SIZE * OBJ_NUM];
static mr_slab_t  pool = MR_SLAB_INIT(pool_map, pool_buf, OBJ_SIZE, OBJ_NUM);

void demo(void) {
    void *blk = mr_slab_alloc(&pool);
    if (blk == MR_NULL) {
        return;                     /* Pool exhausted */
    }

    /* Use blk (OBJ_SIZE bytes) here */

    mr_slab_free(&pool, blk);
}
```

要点：

- `MR_SLAB_INIT` 直接以字段初始化填好 `mr_slab_t`，位图清零可用 `mr_memset`
  复位整个池（测试中的 `slab_reset` 即如此）。
- 位图数组必须按 `sizeof(mr_atomic_t)` 对齐：静态数组天然满足，
  `mr_slab_init` 会校验并对齐错误返回 0。

### 4. 使用说明

**初始化方式**

- 静态初始化宏 `MR_SLAB_INIT(_map, _buf, _size, _num)`：编译期填好描述符， 前提是位图存储已清零。
- 运行时 `mr_slab_init`：清零位图并填写描述符，同时把 `num` 钳位到位图容量 （
  `map_size / sizeof(mr_atomic_t) * MR_SLAB_MAP_BITS`），返回实际管理的块数； 位图未对齐返回 0 表示初始化失败。

**分配与释放**

| 操作                                   | 行为                                                       |
|----------------------------------------|------------------------------------------------------------|
| `mr_slab_alloc`                        | 取最低空闲块，返回其地址；池满返回 `MR_NULL`               |
| `mr_slab_free(slab, mem)`              | 归还块；`mem` 越界、非块起始地址时静默忽略，不返回错误     |
| 对同一块重复 `mr_slab_free`            | 位图位幂等清除，无损坏；但该块可能被再次分配给他人，见 FAQ |
| `mr_slab_free(MR_NULL)` 或任意外部指针 | 安全，被范围校验拦截                                       |

**块地址约定**

- `mr_slab_alloc` 返回的地址总是 `buf + index * size`，即块边界对齐的地址；
  `mr_slab_free` 只接受这类地址（内部校验偏移是否为 `size` 整数倍）。
- `mr_slab_ptr2index` / `mr_slab_index2ptr` 提供地址与块号的双向换算： 越界或未对齐时前者返回 -1、后者返回 `MR_NULL`
  ，可用于诊断与索引化存储。

**并发约定**：`alloc`/`free` 可在多线程与中断上下文并发调用，无需外层锁；
`mr_slab_init` 不是并发安全的，须在池投入使用前单独完成。

### 5. API 参考

初始化：

| API                      | 说明                             |
|--------------------------|----------------------------------|
| `MR_SLAB_INIT`           | 静态初始化宏（编译期填写描述符） |
| `mr_slab_init`           | 运行时初始化：清零位图、钳位块数 |
| `MR_SLAB_MAP_SIZE(_num)` | 宏：计算 `_num` 块所需位图字节数 |
| `MR_SLAB_MAP_BITS`       | 宏：单个位图字管理的块数         |

分配与释放：

| API             | 说明           |
|-----------------|----------------|
| `mr_slab_alloc` | 分配一个空闲块 |
| `mr_slab_free`  | 释放一个块     |

地址换算：

| API                 | 说明                                  |
|---------------------|---------------------------------------|
| `mr_slab_ptr2index` | 块地址 → 块号，非法地址返回 -1        |
| `mr_slab_index2ptr` | 块号 → 块地址，非法块号返回 `MR_NULL` |

**`mr_slab_init`**

```c
mr_uint32_t mr_slab_init(mr_slab_t *slab, void *map, mr_uint32_t map_size,
                         void *buf, mr_uint32_t size, mr_uint32_t num);
```

- `slab`：待初始化的描述符。
- `map`：位图存储，必须按 `sizeof(mr_atomic_t)` 对齐，否则返回 0。
- `map_size`：位图字节数，`num` 超出其容量时被钳位。
- `buf`：块缓冲起始地址；`size`：单块字节数；`num`：期望块数。
- 返回：实际管理的块数；位图未对齐返回 0。

**`mr_slab_alloc`**

```c
void *mr_slab_alloc(mr_slab_t *slab);
```

- 返回：成功为块地址（`buf + index * size`）；池满返回 `MR_NULL`。
- 注意：可并发调用；块内容不清零，敏感数据自行处理。

**`mr_slab_free`**

```c
void mr_slab_free(mr_slab_t *slab, void *mem);
```

- `mem`：必须是 `mr_slab_alloc` 返回的地址。
- 返回：无。`mem` 为 `MR_NULL`、越界或未落在块边界上时静默忽略。

**`mr_slab_ptr2index` / `mr_slab_index2ptr`**

```c
int mr_slab_ptr2index(mr_slab_t *slab, void *mem);
void *mr_slab_index2ptr(mr_slab_t *slab, int index);
```

`ptr2index`：块号从 0 计，`mem` 不在池内或不是块起始地址返回 -1。
`index2ptr`：`index` 越界（含负数）返回 `MR_NULL`。

### 6. 完整示例

示例 1：固定槽位池 + 块号索引存储（改编自 `mrX/test/kernel/slab.c`）：

```c
#define MSG_SIZE   64
#define MSG_NUM    16
#define MSG_MAP    MR_SLAB_MAP_SIZE(MSG_NUM)

static mr_uint8_t msg_map[MSG_MAP];
static mr_uint8_t msg_buf[MSG_SIZE * MSG_NUM];
static mr_slab_t  msg_pool = MR_SLAB_INIT(msg_map, msg_buf, MSG_SIZE, MSG_NUM);

void slot_demo(void) {
    void *a = mr_slab_alloc(&msg_pool);
    void *b = mr_slab_alloc(&msg_pool);

    /* Slots are handed out from index 0 upward */
    if ((a != MR_NULL) && (b != MR_NULL)) {
        int ia = mr_slab_ptr2index(&msg_pool, a);       /* 0 */
        int ib = mr_slab_ptr2index(&msg_pool, b);       /* 1 */
        (void)ia; (void)ib;
    }

    mr_slab_free(&msg_pool, a);
    mr_slab_free(&msg_pool, b);
}
```

示例 2：耗尽与回收（改编自测试 `multi_alloc` / `alloc_free_cycle` 用例）：

```c
void exhaust_demo(void) {
    void *blk[MSG_NUM];
    int i;

    for (i = 0; i < MSG_NUM; i++) {
        blk[i] = mr_slab_alloc(&msg_pool);
    }
    if (mr_slab_alloc(&msg_pool) == MR_NULL) {
        /* Pool exhausted as expected */
    }

    for (i = 0; i < MSG_NUM; i++) {
        mr_slab_free(&msg_pool, blk[i]);
    }

    /* Freed blocks are reused from the lowest index again */
    void *again = mr_slab_alloc(&msg_pool);
    if (again != MR_NULL) {
        mr_slab_free(&msg_pool, again);
    }
}
```

更多行为参考（交错释放、非法释放、并发压测）见 `mrX/test/kernel/slab.c`。

### 7. 常见问题（FAQ）

**Q：`mr_slab_alloc` 返回 `MR_NULL` 但我确定没满？**

检查位图是否被意外置位：`MR_SLAB_INIT` 静态初始化的前提是位图存储为零， 全局/静态数组天然为零，但栈上或复用内存需先
`mr_memset` 清零。也确认
`mr_slab_init` 的返回值等于预期块数（可能被位图容量钳位）。

**Q：`mr_slab_init` 返回 0？**

位图地址未按 `sizeof(mr_atomic_t)` 对齐。使用 `mr_atomic_t` 数组或对齐的静态 数组作位图。

**Q：对同一个块 free 了两次会怎样？**

位图位被第二次清除（幂等），不会破坏池结构；但若第一次释放后块已重新分配给 其他使用者，第二次释放会把在用块标记为空闲，导致两个使用者拿到同一块。务必
保证"谁分配谁释放"。

**Q：free 一个不是本池的指针会怎样？**

被范围与对齐校验拦截，静默忽略，无任何效果。这也是测试 `invalid_free` 覆盖的 行为。

**Q：块大小需要对齐吗？**

模块按 `buf + index * size` 寻址，不做对齐处理；若块内要存放需要对齐的类型， 把 `size` 设为对齐值的整数倍。

**Q：和 `malloc` 怎么选？**

对象大小固定、数量已知、分配释放频繁 → slab（无块头、无碎片、无锁）；大小 不定或需要 `sizeof`
之外的生命周期管理 → [内存分配 malloc](../libc/malloc.md)。 两者独立，可混用。

---

## 开发者指南

### 8. 架构总览

slab 是内核中一个独立的固定块分配器，无功能宏开关、无初始化钩子、不参与内核 初始化序列，完全由使用者驱动：

```
        +----------------------------------+
        |  使用者（对象池拥有者）            |
        +-----------------+----------------+
                           | mr_slab_alloc / mr_slab_free
        +-----------------v----------------+
        |  内存块分配器 Slab                 |
        |  (mrX/kernel/slab.c)             |----> 原子位图（每字一个 CAS 域）
        +-----------------+----------------+
                           | 用户提供的存储
        +-----------------v----------------+
        |  位图数组 map + 块缓冲 buf         |
        +----------------------------------+
             依赖：mr_atomic（CAS/位原子操作）、mr_string（mr_ffs64 位扫描）、
                   无 malloc：与 libc 堆（MR_CFG_HEAP_SIZE）完全独立
```

与 libc `malloc` 的关系（源码确认）：`mrX/libc/malloc.c` 的 `mr_malloc` 在静态 堆 `__heap[MR_CFG_HEAP_SIZE]` 上做变长块分配；
`mrX/kernel/slab.c` 全文不包含
`mr_malloc.h`，也不引用该堆——两者是平行的分配手段，slab 的存储永远由调用者 提供。内核其余模块（如 irq 动作、timer 动态对象）使用的是
`mr_malloc` 而非 slab。

### 9. 数据结构

描述符（`mrX/include/mrX/kernel/mr_slab.h:34`）：

```c
typedef struct mr_slab {
    mr_atomic_t *map;       /* Bitmap: one bit per block, 1 = allocated */
    mr_uint32_t  size;      /* Block size in bytes */
    mr_uint8_t  *buf;       /* Block buffer base */
    mr_uint32_t  num;       /* Number of blocks (clamped to map capacity) */
} mr_slab_t;
```

| 字段   | 说明                                                                                                             |
|--------|------------------------------------------------------------------------------------------------------------------|
| `map`  | 位图数组，第 `i` 位为 1 表示第 `i` 块已分配；每 `MR_SLAB_MAP_BITS` 位一个原子字                                  |
| `size` | 单块字节数，同时是块边界对齐的判定模数                                                                           |
| `buf`  | 块缓冲基址，块 `i` 的地址恒为 `buf + i * size`                                                                   |
| `num`  | 实际管理的块数；`mr_slab_init` 会钳位到 `map_size / sizeof(mr_atomic_t) * MR_SLAB_MAP_BITS`（`mr_slab.h:80-81`） |

位图换算宏：`MR_SLAB_MAP_BITS = sizeof(mr_atomic_t) * 8`（`mr_slab.h:25`）；
`MR_SLAB_MAP_SIZE(_num)` 向上取整到原子字数再乘字长（`mr_slab.h:29`）。 源内另有 `SLAB_MAP_NUM(_num)`（
`mrX/kernel/slab.c:13`）计算位图字数，用于
`alloc` 遍历。

### 10. 核心流程

**初始化**：`mr_slab_init`（`mr_slab.h:68`）先校验位图对齐（未对齐返回 0）， 再按位图容量钳位 `num`，逐字 `mr_atomic_init(0)`
清零，最后填写描述符。
`MR_SLAB_INIT` 宏不做任何清零，依赖存储本身为零。

**分配**：`mr_slab_alloc`（`mrX/kernel/slab.c:16`）按字遍历位图：

1. 对第 `i` 个位图字计算有效位掩码 `mask`（最后一个字可能不满位，
   `slab.c:23-25`）。
2. 原子读当前字 `last`，取反后与 `mask` 相与，`mr_ffs64` 找最低空闲位（`slab.c:32`；`mr_ffs64` 见
   `mrX/include/mrX/libc/mr_string.h:208`，返回值为 1 基）。
3. CAS 把该位置 1：失败（其他执行者抢先）则基于新值重试同一字。
4. 成功则块号 `off = i * MR_SLAB_MAP_BITS + bit - 1`，返回
   `buf + off * size`（`slab.c:48-51`）。全池无空位返回 `MR_NULL`。

"最低位优先 + CAS 重试"使分配结果确定（低号块先发），测试
`alloc_free_cycle` / `unaligned_free` 依赖该性质验证复用顺序。

**释放**：`mr_slab_free`（`slab.c:56`）：

1. 范围校验：`mem` 不在 `[buf, buf + num * size)` 内直接返回（`slab.c:60`）。
2. 边界校验：偏移不是 `size` 整数倍（非块起始）直接返回（`slab.c:67`）。
3. 换算位号后 `mr_atomic_fetch_and` 清位（`slab.c:78`）——清已为 0 的位是幂等的， 这也是重复 free 不损坏结构的原因。

**地址换算**：`mr_slab_ptr2index` / `mr_slab_index2ptr`（`mr_slab.h:119` /
`mr_slab.h:146`）与释放使用同一套范围/边界判定，保证"非法地址"在所有入口 行为一致。

### 11. 并发与中断安全

- `mr_slab_alloc`：无锁。每个位图字是独立的 CAS 域，同一字的竞争者经 do-while 重试收敛，不同字互不干扰；`mr_slab_free`
  的位清除与分配的位设置同为原子操作， 两者并发亦安全。测试 `concurrent_alloc_free` 以 4 线程 × 2000 次压测覆盖 （
  `mrX/test/kernel/slab.c:323`）。
- `mr_slab_init`：非并发安全，须在池投入使用前完成；运行期重建位图需使用者 自行保证互斥。
- 中断上下文：`alloc`/`free` 只含原子位操作，可在 ISR 中调用；但"分配后持有、 中断中再分配"等策略属使用者约定，模块不做重入计数。
- 无锁的代价：释放不校验"该块是否真的被分配过"，误用（重复 free、伪造地址） 不报错，见 FAQ。

### 12. 扩展与派生

slab 无对象 clazz，也无 `MR_CLAZZES_DEFINE`/`MR_*_DERIVE_INIT` 一类的派生入口：`mr_slab_t` 既不内嵌 `mr_object_t`
，也不注册任何对象类型（`mrX/include/mrX/kernel/mr_slab.h:34`），扩展方式只有 **组合与封装**，没有继承通道：

- 桶级组合：需要多种规格时，定义多个 `mr_slab_t`（各自的位图与缓冲），由拥有者在外层按规格选桶；返回的块指针可经
  `MR_CONTAINER_OF`（`mrX/include/mrX/kernel/mr_service.h:32`）直接还原为定长外层对象，块本身不需要块头。返回值的块号可由
  `mr_slab_ptr2index` 取得（`mr_slab.h:119`），是外层做槽位索引存储的现成抓手。
- 结构体封装：自定义池类型把 `mr_slab_t` 作为成员，池的语义（复位、统计、批量回收）全部写在外层，模块不提供回调、ops
  表或注册函数。模块无函数指针成员，也无 `MR_USE_*`/`MR_CFG_*` 开关（见第 14 节），定制面仅剩 `MR_SLAB_MAP_BITS` /
  `MR_SLAB_MAP_SIZE` 换算宏与自己的位图容量。

### 13. 源码导读

| 位置                                                     | 职责                                     |
|----------------------------------------------------------|------------------------------------------|
| `mrX/include/mrX/kernel/mr_slab.h:25`                    | 位图位宽与所需字节数换算宏               |
| `mrX/include/mrX/kernel/mr_slab.h:34`                    | `mr_slab_t` 描述符                       |
| `mrX/include/mrX/kernel/mr_slab.h:49`                    | `MR_SLAB_INIT` 静态初始化宏              |
| `mrX/include/mrX/kernel/mr_slab.h:68`                    | 运行时初始化（对齐校验、容量钳位）       |
| `mrX/include/mrX/kernel/mr_slab.h:119` / `mr_slab.h:146` | 地址/块号双向换算                        |
| `mrX/kernel/slab.c:13`                                   | 位图字数换算                             |
| `mrX/kernel/slab.c:16`                                   | CAS 无锁分配主循环                       |
| `mrX/kernel/slab.c:56`                                   | 释放（范围/边界校验 + 原子清位）         |
| `mrX/test/kernel/slab.c`                                 | 基础、交错、非法、边界与多线程行为级测试 |

### 14. 附录

**配置宏**

| 宏                       | 说明                                          |
|--------------------------|-----------------------------------------------|
| （无）                   | 本模块无 `MR_USE_*`/`MR_CFG_*` 配置，始终编译 |
| `MR_SLAB_MAP_BITS`       | 单个位图字管理的块数                          |
| `MR_SLAB_MAP_SIZE(_num)` | `_num` 块所需位图字节数                       |

**错误约定**：模块不返回错误码——分配失败返回 `MR_NULL`，初始化失败返回 0， 非法释放静默忽略，地址换算失败返回 -1 / `MR_NULL`
。这些约定源自函数签名而非
`mr_errno` 体系，调用方按值判断即可。
