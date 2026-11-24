# 内存块分配器（Slab）

基于原子位图的定长内存块分配器，在调用者提供的缓冲区上按固定块大小管理对象池，无外部碎片与块头开销，与通用堆
`mr_malloc` 相互独立。

| 项目   | 说明                                            |
|--------|-------------------------------------------------|
| 功能宏 | 无，始终编译                                    |
| 头文件 | `<mrX/kernel/mr_slab.h>`                        |
| 依赖   | 无（内部使用 `mr_atomic` 与 `mr_ffs64`）        |
| 测试   | `test/kernel/slab.c`、`test/kernel/slab_deep.c` |

## 快速上手

```c
#include <mrX/kernel/mr_slab.h>

#define BLK_SIZE                        64
#define BLK_NUM                         16
#define MAP_WORDS                       (MR_SLAB_MAP_SIZE(BLK_NUM) / sizeof(mr_atomic_t))

/* 位图需按 mr_atomic_t 对齐，推荐直接用 mr_atomic_t 数组 */
static mr_atomic_t pool_map[MAP_WORDS];
static mr_uint8_t pool_buf[BLK_SIZE * BLK_NUM];
static mr_slab_t pool = MR_SLAB_INIT(pool_map, pool_buf, BLK_SIZE, BLK_NUM);

void slab_demo(void) {
    void *blk = mr_slab_alloc(&pool);
    if (blk == MR_NULL) {
        return; /* 池已满 */
    }

    int idx = mr_slab_ptr2index(&pool, blk);
    (void)idx;

    mr_slab_free(&pool, blk);
}
```

## 核心机制与约束

- **存储与对齐**：调用者负责提供位图和缓冲区两块内存。位图大小至少为
  `MR_SLAB_MAP_SIZE(num)` 字节，且首地址必须按 `sizeof(mr_atomic_t)` 对齐。
  `mr_slab_init()` 会校验位图对齐、清零位图字，并将 `num` 钳位到位图实际可容纳的最大块数；
  `MR_SLAB_INIT()` 仅填充描述符，不清零位图也不校验对齐。
- **并发与无锁实现**：`mr_slab_alloc()` 使用 `mr_atomic_compare_exchange()`
  抢占最低空闲位，`mr_slab_free()` 使用 `mr_atomic_fetch_and()`
  清除对应位，无需互斥锁即可支持多线程并发分配/释放不同块（ISR 安全性取决于平台
  `mr_atomic` 实现）。
- **释放校验与幂等性**：`mr_slab_free()` 会检查 `mem` 是否落在
  `[buf, buf + num * size)` 范围内且为 `size`
  整数倍偏移；越界或非块首地址将被静默忽略。重复释放同一块不会破坏位图，但需避免在释放后继续访问已归还他人的块。
- **块内容不清零**：分配出的内存保留原有数据，需调用者自行初始化。

## API 速查

| API                                                             | 说明                                                             |
|-----------------------------------------------------------------|------------------------------------------------------------------|
| `MR_SLAB_MAP_BITS`                                              | 单个 `mr_atomic_t` 位图字管理的块数（`sizeof(mr_atomic_t) * 8`） |
| `MR_SLAB_MAP_SIZE(_num)`                                        | 计算管理 `_num` 个块所需的位图字节数                             |
| `MR_SLAB_INIT(_map, _buf, _size, _num)`                         | 静态初始化 `mr_slab_t` 描述符                                    |
| `mr_uint32_t mr_slab_init(slab, map, map_size, buf, size, num)` | 运行时初始化并清零位图，返回实际管理的块数（位图未对齐返回 0）   |
| `void *mr_slab_alloc(mr_slab_t *slab)`                          | 分配一个空闲块（最低空闲位优先），池满返回 `MR_NULL`             |
| `void mr_slab_free(mr_slab_t *slab, void *mem)`                 | 归还块，非法地址静默忽略                                         |
| `int mr_slab_ptr2index(mr_slab_t *slab, void *mem)`             | 块指针转块下标（从 0 开始），非法地址返回 `-1`                   |
| `void *mr_slab_index2ptr(mr_slab_t *slab, int index)`           | 块下标转块指针，下标越界返回 `MR_NULL`                           |
