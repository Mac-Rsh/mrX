[**中文**](kernel_slab.md) | [**English**](kernel_slab_en.md)

---

# Slab 固定块分配器

Slab 模块用于从调用者提供的一段连续缓冲区中分配固定大小的内存块。模块不负责申请或释放底层缓冲区，只维护位图状态，并通过原子位操作标记块的占用状态。它适用于对象大小固定、分配和释放频繁、且不需要通用堆管理能力的内核内部场景。

本文说明 Slab 控制块、位图尺寸计算、主要接口和使用约束。

## 核心数据结构和宏

### Slab 控制块

```c
typedef struct mr_slab {
    mr_atomic_t *map;
    mr_uint32_t size;
    mr_uint8_t *buf;
    mr_uint32_t num;
} mr_slab_t;
```

字段含义如下：

| 字段     | 含义                                            |
|--------|-----------------------------------------------|
| `map`  | 位图数组，每一位表示一个固定块是否已分配。该字段以 `mr_atomic_t *` 访问。 |
| `size` | 每个固定块的字节数。                                    |
| `buf`  | 固定块存储区起始地址。                                   |
| `num`  | 可管理的固定块数量。                                    |

### 位图尺寸

```c
#define MR_SLAB_MAP_SIZE(_num) ((((_num) + 31U) >> 5) << 2)
```

`MR_SLAB_MAP_SIZE()` 根据块数量计算位图所需字节数。当前实现以 32 位为一组管理位图，因此每 32 个块需要 4 字节位图空间。

### 静态初始化

```c
#define MR_SLAB_INIT(_map, _buf, _size, _num) \
    {                                         \
        .map = (mr_atomic_t *)(_map),         \
        .buf = (mr_uint8_t *)(_buf),          \
        .size = (_size),                      \
        .num = (_num),                        \
    }
```

`MR_SLAB_INIT()` 用于静态构造 `mr_slab_t`。调用者仍需保证 `_map` 内容在首次分配前被清零，否则非零位会被视为已占用。

## 模块接口

### 初始化 Slab

```c
mr_uint32_t mr_slab_init(mr_slab_t *slab, void *map,
                         mr_uint32_t map_size, void *buf,
                         mr_uint32_t size, mr_uint32_t num);
```

`mr_slab_init()` 设置 Slab 控制块并返回实际可管理的块数量。函数会把 `map_size` 限制到 `MR_SLAB_MAP_SIZE(num)`，再把 `num`
限制到 `map_size * 8U`，因此位图空间不足时，实际块数量会小于传入的 `num`。

该接口不清空位图，也不初始化底层缓冲区内容。测试用例中会在分配前显式清零位图。

### 分配固定块

```c
void *mr_slab_alloc(mr_slab_t *slab);
```

`mr_slab_alloc()` 从位图中查找第一个空闲位，并使用原子比较交换将该位设置为已占用。分配成功时返回对应块地址；没有空闲块或命中的位超出
`slab->num` 时返回 `MR_NULL`。

分配出的内存内容不会被清零。调用者需要按对象需求自行初始化。

### 释放固定块

```c
void mr_slab_free(mr_slab_t *slab, void *mem);
```

`mr_slab_free()` 将 `mem` 所在块对应的位清零。若 `mem` 位于 Slab 缓冲区范围之外，函数直接返回。

当前实现只检查地址是否落在 `[buf, buf + num * size)` 范围内，不检查 `mem` 是否按块大小对齐。测试覆盖了非对齐地址释放：传入
`allocated + 1` 会按整数除法映射到该块索引，并释放该块。

### 指针和索引转换

```c
int mr_slab_ptr2index(mr_slab_t *slab, void *mem);
void *mr_slab_index2ptr(mr_slab_t *slab, int index);
```

`mr_slab_ptr2index()` 将缓冲区内地址转换为块索引。地址超出 Slab 管理范围时返回 `-1`。该函数同样不要求地址对齐，非对齐地址会被映射到所在块。

`mr_slab_index2ptr()` 将块索引转换为块起始地址。索引小于 0 或不小于 `slab->num` 时返回 `MR_NULL`。

## 使用示例

```c
#include <mrX/kernel/mr_slab.h>
#include <mrX/libc/mr_string.h>

#define OBJ_SIZE 64
#define OBJ_NUM  16

static mr_uint8_t slab_map[MR_SLAB_MAP_SIZE(OBJ_NUM)];
static mr_uint8_t slab_buf[OBJ_SIZE * OBJ_NUM];
static mr_slab_t slab = MR_SLAB_INIT(slab_map, slab_buf, OBJ_SIZE, OBJ_NUM);

void sample_slab_usage(void)
{
    void *mem;

    mr_memset(slab_map, 0, sizeof(slab_map));

    mem = mr_slab_alloc(&slab);
    if (mem == MR_NULL) {
        return;
    }

    mr_memset(mem, 0, OBJ_SIZE);
    mr_slab_free(&slab, mem);
}
```

该示例使用静态位图和静态缓冲区创建 Slab。位图清零后，`mr_slab_alloc()` 返回一个固定块，使用完成后通过 `mr_slab_free()` 释放。

## 注意事项

- `map` 和 `buf` 的存储期必须覆盖 Slab 的使用期。
- `mr_slab_init()` 和 `MR_SLAB_INIT()` 不清零位图；首次使用前应清零位图。
- `mr_slab_free()` 不校验指针是否为块起始地址，调用者应传入由 `mr_slab_alloc()` 返回的原始指针。
- 重复释放同一块会重复清除同一位，模块不会报告错误。
- 位图操作使用原子接口，但控制块字段和底层缓冲区内容不由 Slab 自动保护；多个执行上下文共享同一块内容时，调用者需要自行同步。
