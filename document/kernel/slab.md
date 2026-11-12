# 内存块分配器（Slab）

> 内存块分配器在调用者提供的缓冲上按固定块大小分配：位图跟踪每个块的占用状态，
> 分配/释放通过原子位操作协调，适合数量与大小固定的对象池，避免变长堆的外部碎片与块头开销。

| 项       | 说明                                                                                                                            |
|----------|---------------------------------------------------------------------------------------------------------------------------------|
| 功能宏   | 无（始终编译，无 `MR_USE_*` 开关与 Kconfig 项）                                                                                 |
| 头文件   | `<mrX/kernel/mr_slab.h>`                                                                                                        |
| 依赖模块 | 无强依赖（位操作基于 `mrX/include/mrX/libc/mr_atomic.h` 原子原语；与 [内存分配 malloc](../libc/malloc.md) 相互独立，见第 1 节） |
| 参考测试 | `mrX/test/kernel/slab.c`                                                                                                        |

---

## 用户指南

### 1. 模块概述

内存块分配器解决的问题是：
**为一类大小固定的对象提供无变长块外部碎片、零块头开销的快速分配**
。固定块仍可能因为对象小于块大小而产生块内剩余空间。

特性：

- 固定块大小：一个 slab 内所有块等长（`size`），按位图逐块占用管理。
- 存储由用户提供：位图与块缓冲都由调用者分配（通常是静态数组），模块本身不 申请内存。
- 分配器本身不使用互斥锁：`mr_slab_alloc` 以原子 CAS 抢占位图位，`mr_slab_free`
  以原子位清除释放；并发能力依赖底层
  `mr_atomic_*` 实现以及调用者正确的块所有权管理。
- 与 libc 的 `malloc` 相互独立：`malloc`（`MR_USE_MALLOC`）在 `MR_CFG_HEAP_SIZE`
  指定的通用堆上按需分配任意大小（见 [内存分配 malloc](../libc/malloc.md)）， slab
  不使用该堆、不使用 `mr_malloc`
  ，两者可并存并按场景选用。

选用建议：对象大小与数量编译期已知（消息槽、缓冲帧池、固定句柄表）优先 slab；
大小不定或数量运行期才确定的分配走 `malloc`。

### 2. 配置与依赖

本模块没有功能宏与 Kconfig 选项，始终编译。包含头文件即可使用：

```c
#include <mrX/kernel/mr_slab.h>
```

头文件自动引入 `mr_service.h` 与 `mr_atomic.h`，无需手动包含。

**关键换算宏**

| 宏                       | 说明                                                           |
|--------------------------|----------------------------------------------------------------|
| `MR_SLAB_MAP_BITS`       | 每个 `mr_atomic_t` 位图字管理的块数：`sizeof(mr_atomic_t) * 8` |
| `MR_SLAB_MAP_SIZE(_num)` | 管理 `_num` 个块所需的位图字节数，用于声明位图数组             |

使用前需准备两块存储：位图数组（大小 `MR_SLAB_MAP_SIZE(num)`，且按
`sizeof(mr_atomic_t)` 对齐）与块缓冲数组（大小 `size * num`）。

### 3. 快速上手

一个 64 字节 × 16 块的静态对象池（改编自 `mrX/test/kernel/slab.c` 的全局定义）：

```c
#include <mrX/kernel/mr_slab.h>

#define OBJ_SIZE   64
#define OBJ_NUM    16
#define MAP_SIZE   MR_SLAB_MAP_SIZE(OBJ_NUM)

static mr_atomic_t pool_map[MAP_SIZE / sizeof(mr_atomic_t)];
static mr_uint8_t pool_buf[OBJ_SIZE * OBJ_NUM];
static mr_slab_t  pool = MR_SLAB_INIT(pool_map, pool_buf, OBJ_SIZE, OBJ_NUM);

void demo(void) {
    void *blk = mr_slab_alloc(&pool);
    if (blk == MR_NULL) {
        return;                     /* 池已耗尽 */
    }

    /* 在此使用 blk（OBJ_SIZE 字节） */

    mr_slab_free(&pool, blk);
}
```

要点：

- `MR_SLAB_INIT` 直接以字段初始化填好 `mr_slab_t`，位图清零可用 `mr_memset`
  复位整个池（测试中的 `slab_reset` 即如此）。
- 位图存储必须满足 `mr_atomic_t` 的对齐要求。使用 `mr_atomic_t` 数组最直接；普通
  `mr_uint8_t` 数组即使是静态存储，C
  类型本身也不保证具备 `mr_atomic_t` 对齐。`mr_slab_init` 会检查位图地址对齐，
  `MR_SLAB_INIT` 宏则不会。

### 4. 使用说明

**初始化方式**

- 静态初始化宏 `MR_SLAB_INIT(_map, _buf, _size, _num)`：只填描述符，不清零位图、不检查对齐，也不校验
  `buf/size/num`
  ；调用者必须自行保证这些前置条件。
- 运行时 `mr_slab_init`：检查位图地址是否按 `sizeof(mr_atomic_t)` 对齐，随后清零
  `map_size` 覆盖的全部位图字，并把 `num`
  钳位到位图容量 `map_size / sizeof(mr_atomic_t) * MR_SLAB_MAP_BITS`
  。返回值是实际管理块数，因此返回 0
  既可能是位图未对齐，也可能只是最终可管理块数为 0。

**分配与释放**

| 操作                          | 行为                                                                        |
|-------------------------------|-----------------------------------------------------------------------------|
| `mr_slab_alloc`               | 取最低空闲块，返回其地址；池满返回 `MR_NULL`                                |
| `mr_slab_free(slab, mem)`     | 归还块；`mem` 越界、非块起始地址时静默忽略，不返回错误                      |
| 对同一块重复 `mr_slab_free`   | 位图位幂等清除，无损坏；但该块可能被再次分配给他人，见 FAQ                  |
| `mr_slab_free` 传入非本池地址 | 实现会先做地址范围/块边界判断；公共约定仍应只传本 slab 的块地址或 `MR_NULL` |

**块地址约定**

- `mr_slab_alloc` 返回的地址总是 `buf + index * size`，即块边界对齐的地址；
  `mr_slab_free` 只接受这类地址（内部校验偏移是否为 `size` 整数倍）。
- `mr_slab_ptr2index` / `mr_slab_index2ptr` 提供地址与块号的双向换算：
  越界或未对齐时前者返回 -1、后者返回 `MR_NULL`
  ，可用于诊断与索引化存储。

**并发约定**：位图修改使用原子操作，因此正常的“不同所有者分配/释放各自块”场景不需要
slab 自身再加互斥锁；`mr_slab_init`
必须在池投入使用前完成。是否可在 ISR 使用还取决于目标平台 `mr_atomic_*`
的实现与调用环境，Slab 层不单独作统一保证。

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
- `map`：位图存储，必须满足 `mr_atomic_t` 对齐；函数只显式检查这一项。
- `map_size`：位图字节数，先按 `sizeof(mr_atomic_t)` 向下折算为完整位图字数量，
  `num` 超出容量时被钳位。
- `slab`、`map`、`buf` 必须是有效存储，`size` 应大于 0；这些前置条件当前实现不会完整校验。
- 返回：实际管理的块数；位图未对齐或最终管理块数本来就是 0 时都可能返回 0。

**`mr_slab_alloc`**

```c
void *mr_slab_alloc(mr_slab_t *slab);
```

- 返回：成功为块地址（`buf + index * size`）；池满返回 `MR_NULL`。
- 块内容不清零，敏感数据需调用者自行处理。并发分配通过位图 CAS
  协调，但块生命周期/所有权仍由调用者负责。

**`mr_slab_free`**

```c
void mr_slab_free(mr_slab_t *slab, void *mem);
```

- `mem`：必须是 `mr_slab_alloc` 返回的地址。
- 返回：无。实现对地址范围和块边界做检查，不匹配时直接返回；调用者仍应只传入本 slab
  分配出的块指针；“任意伪造地址也能被正确处理”不是本接口承诺的可移植契约。

**`mr_slab_ptr2index` / `mr_slab_index2ptr`**

```c
int mr_slab_ptr2index(mr_slab_t *slab, void *mem);
void *mr_slab_index2ptr(mr_slab_t *slab, int index);
```

`ptr2index`：在 slab 配置有效且 `size > 0` 的前提下，块号从 0 计；地址不在池内或不是块起始地址返回
-1。`index2ptr`：`index`
越界（含负数）返回 `MR_NULL`。两者都假定 `slab` 本身已正确初始化。

### 6. 完整示例

示例 1：固定槽位池 + 块号索引存储（改编自 `mrX/test/kernel/slab.c`）：

```c
#define MSG_SIZE   64
#define MSG_NUM    16
#define MSG_MAP    MR_SLAB_MAP_SIZE(MSG_NUM)

static mr_atomic_t msg_map[MSG_MAP / sizeof(mr_atomic_t)];
static mr_uint8_t msg_buf[MSG_SIZE * MSG_NUM];
static mr_slab_t  msg_pool = MR_SLAB_INIT(msg_map, msg_buf, MSG_SIZE, MSG_NUM);

void slot_demo(void) {
    void *a = mr_slab_alloc(&msg_pool);
    void *b = mr_slab_alloc(&msg_pool);

    /* 槽位从块号 0 起依次分配 */
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
        /* 如预期池已耗尽 */
    }

    for (i = 0; i < MSG_NUM; i++) {
        mr_slab_free(&msg_pool, blk[i]);
    }

    /* 释放的块再次从最低块号开始复用 */
    void *again = mr_slab_alloc(&msg_pool);
    if (again != MR_NULL) {
        mr_slab_free(&msg_pool, again);
    }
}
```

更多行为参考（交错释放、非法释放、并发压测）见 `mrX/test/kernel/slab.c`。

### 7. 常见问题（FAQ）

**Q：`mr_slab_alloc` 返回 `MR_NULL` 但我确定没满？**

检查位图是否被意外置位：`MR_SLAB_INIT` 不会清零位图；具有静态存储期且未显式初始化的位图对象由
C
规则零初始化，栈上或复用存储则需自行清零。也确认位图容量和对齐满足要求，或直接改用
`mr_slab_init` 完成清零。

**Q：`mr_slab_init` 返回 0？**

首先检查位图地址是否按 `sizeof(mr_atomic_t)` 对齐；此外，`map_size` 太小或传入的
`num` 为 0 也会使最终管理块数为 0。建议直接使用
`mr_atomic_t` 数组作为位图存储。

**Q：对同一个块 free 了两次会怎样？**

位图位被第二次清除（幂等），不会破坏池结构；但若第一次释放后块已重新分配给
其他使用者，第二次释放会把在用块标记为空闲，导致两个使用者拿到同一块。务必
保证"谁分配谁释放"。

**Q：free 一个不是本池的指针会怎样？**

当前实现会先用地址范围与块边界条件过滤，现有测试也覆盖了若干外部地址输入。但接口设计上仍应只归还本池分配出的块；对任意无关/伪造指针做关系比较并不是应依赖的可移植
C 契约。

**Q：块大小需要对齐吗？**

模块按 `buf + index * size` 寻址，不做对齐处理。若块内要存放有对齐要求的类型，既要保证
`buf` 基址满足该对齐，也要让 `size`
步长保持该对齐（通常取对齐值的整数倍）。

**Q：和 `malloc` 怎么选？**

对象大小固定、数量已知、分配释放频繁 → slab（无块头、无碎片、无锁）；大小 不定或需要
`sizeof`
之外的生命周期管理 → [内存分配 malloc](../libc/malloc.md)。 两者独立，可混用。

---

## 开发者指南

### 8. 架构总览

slab 是内核中一个独立的固定块分配器，无功能宏开关、无初始化钩子、不参与内核
初始化序列，完全由使用者驱动：

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

与 libc `malloc` 的关系（源码确认）：`mrX/libc/malloc.c` 的 `mr_malloc` 在静态 堆
`__heap[MR_CFG_HEAP_SIZE]` 上做变长块分配；
`mrX/kernel/slab.c` 全文不包含
`mr_malloc.h`，也不引用该堆——两者是平行的分配手段，slab 的存储永远由调用者
提供。内核其余模块（如 irq 动作、timer 动态对象）使用的是
`mr_malloc` 而非 slab。

### 9. 数据结构

描述符（`mrX/include/mrX/kernel/mr_slab.h:34`）：

```c
typedef struct mr_slab {
    mr_atomic_t *map;       /* 位图：每块一位，1 = 已分配 */
    mr_uint32_t  size;      /* 单块字节数 */
    mr_uint8_t  *buf;       /* 块缓冲基址 */
    mr_uint32_t  num;       /* 块数（钳位到位图容量） */
} mr_slab_t;
```

| 字段   | 说明                                                                                                             |
|--------|------------------------------------------------------------------------------------------------------------------|
| `map`  | 位图数组，第 `i` 位为 1 表示第 `i` 块已分配；每 `MR_SLAB_MAP_BITS` 位一个原子字                                  |
| `size` | 单块字节数，同时是块边界对齐的判定模数                                                                           |
| `buf`  | 块缓冲基址，块 `i` 的地址恒为 `buf + i * size`                                                                   |
| `num`  | 实际管理的块数；`mr_slab_init` 会钳位到 `map_size / sizeof(mr_atomic_t) * MR_SLAB_MAP_BITS`（`mr_slab.h:80-81`） |

位图换算宏：`MR_SLAB_MAP_BITS = sizeof(mr_atomic_t) * 8`（`mr_slab.h:25`）；
`MR_SLAB_MAP_SIZE(_num)` 向上取整到原子字数再乘字长（`mr_slab.h:29`）。 源内另有
`SLAB_MAP_NUM(_num)`（
`mrX/kernel/slab.c:13`）计算位图字数，用于
`alloc` 遍历。

### 10. 核心流程

**初始化**：`mr_slab_init`（`mr_slab.h:68`）先校验位图对齐（未对齐返回 0）， 再按位图容量钳位
`num`，逐字 `mr_atomic_init(0)`
清零，最后填写描述符。
`MR_SLAB_INIT` 宏不做任何清零，依赖存储本身为零。

**分配**：`mr_slab_alloc`（`mrX/kernel/slab.c:16`）按字遍历位图：

1. 对第 `i` 个位图字计算有效位掩码 `mask`（最后一个字可能不满位，
   `slab.c:23-25`）。
2. 原子读当前字 `last`，取反后与 `mask` 相与，`mr_ffs64` 找最低空闲位（`slab.c:32`；
   `mr_ffs64` 见
   `mrX/include/mrX/libc/mr_string.h:208`，返回值为 1 基）。
3. CAS 把该位置 1：失败（其他执行者抢先）则基于新值重试同一字。
4. 成功则块号 `off = i * MR_SLAB_MAP_BITS + bit - 1`，返回
   `buf + off * size`（`slab.c:48-51`）。全池无空位返回 `MR_NULL`。

单线程/无竞争时，“最低位优先”会优先返回低号空闲块，测试 `alloc_free_cycle` /
`unaligned_free` 用这一点验证复用顺序；并发竞争时各调用的完成顺序由
CAS 竞争决定，不能据此推导跨线程的确定分配顺序。

**释放**：`mr_slab_free`（`slab.c:56`）：

1. 范围校验：`mem` 不在 `[buf, buf + num * size)` 内直接返回（`slab.c:60`）。
2. 边界校验：偏移不是 `size` 整数倍（非块起始）直接返回（`slab.c:67`）。
3. 换算位号后 `mr_atomic_fetch_and` 清位（`slab.c:78`）——清已为 0 的位是幂等的，
   这也是重复 free 不损坏结构的原因。

**地址换算**：`mr_slab_ptr2index` / `mr_slab_index2ptr`（`mr_slab.h:119` /
`mr_slab.h:146`）与释放使用同一套范围/边界判定，保证"非法地址"在所有入口 行为一致。

### 11. 并发与中断安全

- `mr_slab_alloc` 以每个位图字为 CAS 竞争域；`mr_slab_free` 用原子清位。现有
  `concurrent_alloc_free` 测试用 4 个 pthread
  并发执行正常“分配→使用→释放”循环。
- 这些原子位操作只保护分配元数据，不替调用者解决块所有权：同一块被两个执行流重复
  free、或 free 后继续使用，都可能造成重新分配后的别名问题。
- `mr_slab_init` 非并发安全，须在池投入使用前完成；运行期重建位图需调用者自行同步。
- ISR 可用性取决于目标平台 `mr_atomic_*` 是否适合该上下文；Slab 本身不调用 malloc
  或阻塞原语，但不据此额外承诺所有平台都
  ISR-safe。

### 12. 扩展与派生

slab 无对象 clazz，也无 `MR_CLAZZES_DEFINE`/`MR_*_DERIVE_INIT` 一类的派生入口：
`mr_slab_t` 既不内嵌 `mr_object_t`
，也不注册任何对象类型（`mrX/include/mrX/kernel/mr_slab.h:34`），扩展方式只有
**组合与封装**，没有继承通道：

- 桶级组合：需要多种规格时，定义多个 `mr_slab_t`（各自的位图与缓冲），由拥有者在外层按规格选桶；返回的块指针可经
  `MR_CONTAINER_OF`（`mrX/include/mrX/kernel/mr_service.h:32`
  ）直接还原为定长外层对象，块本身不需要块头。返回值的块号可由
  `mr_slab_ptr2index` 取得（`mr_slab.h:119`），可直接用作外层槽位索引存储的下标。
- 结构体封装：自定义池类型把 `mr_slab_t` 作为成员，池的语义（复位、统计、批量回收）全部写在外层，模块不提供回调、ops
  表或注册函数。模块无函数指针成员，也无 `MR_USE_*`/`MR_CFG_*` 开关（见第 14
  节），定制面仅剩 `MR_SLAB_MAP_BITS` /
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

**错误约定**：模块不返回错误码——分配失败返回 `MR_NULL`，初始化失败返回 0，
非法释放静默忽略，地址换算失败返回 -1 / `MR_NULL`
。这些约定源自函数签名而非
`mr_errno` 体系，调用方按值判断即可。
