# libc 动态内存

`mr_malloc.h` 和 `libc/malloc.c` 提供动态内存分配接口。启用 `MR_USE_MALLOC_STD` 时，分配接口映射到标准库；否则在 `MR_USE_MALLOC` 下使用 mrX 内置堆实现。内置堆通过空闲块链表管理内存，并在分配和释放期间关闭中断保护堆结构。

## 核心数据结构和宏

### 堆块

```c
#define MR_HEAP_MAGIC (0x6d725800U)

typedef struct mr_heap_block {
    volatile mr_uint32_t magic;
    struct mr_heap_block *next;
    mr_size_t size;
} mr_heap_block_t;
```

`magic` 用于区分已分配块和空闲块。`next` 用于链接空闲块链表。`size` 表示该块可用负载字节数，不包含块头。

### 堆区域

```c
typedef struct mr_heap_region {
    void *address;
    mr_size_t size;
} mr_heap_region_t;
```

`mr_heap_region_add()` 使用以 `{ .address = MR_NULL }` 结尾的数组添加额外内存区域。

### 堆信息

```c
typedef struct mr_heap_info {
    mr_size_t total_bytes;
    mr_size_t used_bytes;
    mr_size_t free_bytes;
    mr_size_t fail_count;
    mr_size_t frag_num;
    mr_size_t max_free_bytes;
    mr_size_t disp_kb_ratio;
    mr_size_t frag_ratio;
} mr_heap_info_t;
```

`total_bytes`、`used_bytes` 和 `free_bytes` 记录堆容量和使用情况。`fail_count` 记录分配失败次数。`frag_num` 记录空闲碎片数量。`max_free_bytes`、`disp_kb_ratio` 和 `frag_ratio` 在非 lazy 查询时更新，用于观察最大空闲块和碎片化程度。

## 主要接口

### 分配和释放

```c
void *mr_malloc(mr_size_t size);
void mr_free(void *mem);
mr_size_t mr_malloc_usable_size(void *mem);
```

`mr_malloc()` 对请求大小按 `MR_ALIGN_SIZE` 对齐，从空闲链表中取第一个满足大小的块；剩余空间大于拆分阈值时会拆分出新的空闲块。`mr_free()` 检查块 magic，合法时标记为空闲并按地址插回链表，相邻空闲块会合并。`mr_malloc_usable_size()` 返回已分配块的可用大小，非法指针或空指针返回 0。

### calloc 和 realloc

```c
void *mr_calloc(mr_size_t num, mr_size_t size);
void *mr_realloc(void *mem, mr_size_t size);
```

`mr_calloc()` 检查乘法溢出，分配成功后用 `mr_memset()` 清零。`mr_realloc()` 先分配新块，再复制 `min(old_size, size)` 字节，最后释放旧块。

### 堆区域和统计

```c
void mr_heap_region_add(const mr_heap_region_t *regions);
mr_heap_info_t mr_heap_information(mr_bool_t lazy);
```

`mr_heap_region_add()` 可把额外内存区域加入堆。每个区域必须有有效地址和正大小，数组最后一个元素的 `address` 必须为 `MR_NULL`。`mr_heap_information(MR_TRUE)` 只复制当前统计值；`mr_heap_information(MR_FALSE)` 会遍历空闲链表更新耗时统计字段。

## 初始化

内置实现中，`malloc.c` 定义内部函数：

```c
void mr_libc_malloc_init(void);
```

该函数创建默认静态堆 `__heap`，大小由 `MR_CFG_HEAP_SIZE` 决定，默认 4 KiB，且必须不小于 32 字节。源码中 `kernel/initcall.c` 以 `extern` 方式调用该初始化函数。

## 使用示例

```c
#include <mrX/libc/mr_malloc.h>
#include <mrX/libc/mr_string.h>

void sample_malloc_usage(void)
{
    char *buf = mr_calloc(16, sizeof(char));
    char *new_buf;

    if (buf == MR_NULL) {
        return;
    }

    mr_strncpy(buf, "mrX", 16);
    new_buf = mr_realloc(buf, 32);
    if (new_buf != MR_NULL) {
        buf = new_buf;
    }
    mr_free(buf);
}
```

添加额外区域示例：

```c
static mr_uint8_t extra_heap[256];

static const mr_heap_region_t regions[] = {
    { .address = extra_heap, .size = sizeof(extra_heap) },
    { .address = MR_NULL,    .size = 0                  },
};

void sample_add_region(void)
{
    mr_heap_region_add(regions);
}
```

## 注意事项

- 启用 `MR_USE_MALLOC_STD` 时，`mr_malloc()`、`mr_free()`、`mr_calloc()` 和 `mr_realloc()` 映射到标准库；`mr_malloc_usable_size()`、`mr_heap_region_add()` 和 `mr_heap_information()` 只在非标准实现中声明。
- 内置 `mr_malloc(0)` 返回 `MR_NULL`，`mr_free(MR_NULL)` 直接返回。
- 内置 `mr_realloc(mem, 0)` 会调用 `mr_malloc(0)` 得到 `MR_NULL`，随后仍调用 `mr_memcpy(new, mem, 0)` 和 `mr_free(mem)`；在未启用字符串安全检查时，空目标指针参与 `mr_memcpy()` 需要谨慎。
- 堆操作通过关中断保护内部链表，不提供 C11 意义上的跨核同步保证。
- `mr_heap_region_add()` 未对 `regions` 空指针做检查，调用方必须传入有效且以空地址结尾的数组。
- 测试覆盖零大小分配、释放空指针、可用大小、calloc 清零、realloc 数据保留、碎片复用和额外区域添加。
