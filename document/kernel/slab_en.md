[**中文**](kernel_slab.md) | [**English**](kernel_slab_en.md)

---

# Slab Fixed-Block Allocator

The Slab module allocates fixed-size memory blocks from a contiguous buffer provided by the caller. The module does not
allocate or free the underlying buffer; it only maintains a bitmap state and marks block occupancy via atomic bit
operations. It is suitable for kernel-internal scenarios where object sizes are fixed, allocations and deallocations are
frequent, and general heap management is not required.

This document describes the Slab control block, bitmap size calculation, main interfaces, and usage constraints.

## Core Data Structures and Macros

### Slab Control Block

```c
typedef struct mr_slab {
    mr_atomic_t *map;
    mr_uint32_t size;
    mr_uint8_t *buf;
    mr_uint32_t num;
} mr_slab_t;
```

Field descriptions:

| Field  | Description                                                                                                           |
|--------|-----------------------------------------------------------------------------------------------------------------------|
| `map`  | Bitmap array, where each bit indicates whether a fixed block is allocated. This field is accessed as `mr_atomic_t *`. |
| `size` | Byte size of each fixed block.                                                                                        |
| `buf`  | Start address of the fixed block storage area.                                                                        |
| `num`  | Number of manageable fixed blocks.                                                                                    |

### Bitmap Size

```c
#define MR_SLAB_MAP_SIZE(_num) ((((_num) + 31U) >> 5) << 2)
```

`MR_SLAB_MAP_SIZE()` calculates the number of bytes required for the bitmap based on the number of blocks. The current
implementation manages the bitmap in groups of 32 bits, so every 32 blocks require 4 bytes of bitmap space.

### Static Initialization

```c
#define MR_SLAB_INIT(_map, _buf, _size, _num) \
    {                                         \
        .map = (mr_atomic_t *)(_map),         \
        .buf = (mr_uint8_t *)(_buf),          \
        .size = (_size),                      \
        .num = (_num),                        \
    }
```

`MR_SLAB_INIT()` is used to statically construct an `mr_slab_t`. The caller must still ensure that the `_map` content is
zeroed before the first allocation; otherwise, non-zero bits will be treated as occupied.

## Module Interface

### Initializing a Slab

```c
mr_uint32_t mr_slab_init(mr_slab_t *slab, void *map,
                         mr_uint32_t map_size, void *buf,
                         mr_uint32_t size, mr_uint32_t num);
```

`mr_slab_init()` sets up the Slab control block and returns the actual number of manageable blocks. The function limits
`map_size` to `MR_SLAB_MAP_SIZE(num)`, then limits `num` to `map_size * 8U`, so when bitmap space is insufficient, the
actual number of blocks will be less than the passed-in `num`.

This interface does not clear the bitmap nor initialize the underlying buffer content. In test cases, the bitmap is
explicitly zeroed before allocation.

### Allocating a Fixed Block

```c
void *mr_slab_alloc(mr_slab_t *slab);
```

`mr_slab_alloc()` searches the bitmap for the first free bit and uses an atomic compare-and-swap to mark that bit as
occupied. On success, it returns the address of the corresponding block; if no free block is available or the hit bit
exceeds `slab->num`, it returns `MR_NULL`.

The allocated memory content is not zeroed. The caller must initialize it according to the object's requirements.

### Freeing a Fixed Block

```c
void mr_slab_free(mr_slab_t *slab, void *mem);
```

`mr_slab_free()` clears the bit corresponding to the block containing `mem`. If `mem` lies outside the Slab buffer
range, the function returns immediately.

The current implementation only checks whether the address falls within the range `[buf, buf + num * size)`; it does not
check whether `mem` is aligned to the block size. Tests cover non-aligned address freeing: passing `allocated + 1` maps
to the same block index via integer division and frees that block.

### Pointer and Index Conversion

```c
int mr_slab_ptr2index(mr_slab_t *slab, void *mem);
void *mr_slab_index2ptr(mr_slab_t *slab, int index);
```

`mr_slab_ptr2index()` converts an address within the buffer to a block index. Returns `-1` if the address is outside the
Slab management range. This function also does not require address alignment; a non-aligned address is mapped to its
containing block.

`mr_slab_index2ptr()` converts a block index to the block's start address. Returns `MR_NULL` if the index is less than 0
or not less than `slab->num`.

## Usage Example

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

This example creates a Slab using a static bitmap and a static buffer. After zeroing the bitmap, `mr_slab_alloc()`
returns a fixed block, which is then released via `mr_slab_free()` after use.

## Notes

- The storage duration of `map` and `buf` must cover the usage period of the Slab.
- `mr_slab_init()` and `MR_SLAB_INIT()` do not zero the bitmap; the bitmap should be zeroed before first use.
- `mr_slab_free()` does not verify whether the pointer is the start address of a block; the caller should pass the
  original pointer returned by `mr_slab_alloc()`.
- Freeing the same block multiple times clears the same bit repeatedly; the module does not report an error.
- Bitmap operations use atomic interfaces, but the control block fields and underlying buffer content are not
  automatically protected by the Slab; when multiple execution contexts share the same block content, the caller must
  synchronize access.
