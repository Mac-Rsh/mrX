[**中文**](libc_malloc.md) | [**English**](libc_malloc_en.md)

---

# libc Dynamic Memory

`mr_malloc.h` and `libc/malloc.c` provide dynamic memory allocation interfaces. When `MR_USE_MALLOC_STD` is enabled, the
allocation interfaces map to the standard library; otherwise, under `MR_USE_MALLOC`, mrX uses a built-in heap
implementation. The built-in heap manages memory via a free block linked list and disables interrupts during allocation
and deallocation to protect the heap structure.

## Core Data Structures and Macros

### Heap Block

```c
#define MR_HEAP_MAGIC (0x6d725800U)

typedef struct mr_heap_block {
    volatile mr_uint32_t magic;
    struct mr_heap_block *next;
    mr_size_t size;
} mr_heap_block_t;
```

`magic` is used to distinguish allocated blocks from free blocks. `next` links free blocks in a linked list. `size`
indicates the usable payload size of the block in bytes, excluding the block header.

### Heap Region

```c
typedef struct mr_heap_region {
    void *address;
    mr_size_t size;
} mr_heap_region_t;
```

`mr_heap_region_add()` uses an array terminated by `{ .address = MR_NULL }` to add additional memory regions.

### Heap Info

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

`total_bytes`, `used_bytes`, and `free_bytes` record heap capacity and usage. `fail_count` records the number of
allocation failures. `frag_num` records the number of free fragments. `max_free_bytes`, `disp_kb_ratio`, and
`frag_ratio` are updated during non-lazy queries and are used to observe the largest free block and fragmentation level.

## Main Interface

### Allocation and Deallocation

```c
void *mr_malloc(mr_size_t size);
void mr_free(void *mem);
mr_size_t mr_malloc_usable_size(void *mem);
```

`mr_malloc()` aligns the requested size to `MR_ALIGN_SIZE` and takes the first block in the free list that satisfies the
size; if the remaining space is larger than the split threshold, a new free block is split off. `mr_free()` checks the
block magic, marks it as free when valid, inserts it back into the list by address, and merges adjacent free blocks.
`mr_malloc_usable_size()` returns the usable size of an allocated block, returning 0 for invalid or null pointers.

### calloc and realloc

```c
void *mr_calloc(mr_size_t num, mr_size_t size);
void *mr_realloc(void *mem, mr_size_t size);
```

`mr_calloc()` checks for multiplication overflow, and after successful allocation, zeroes the memory with `mr_memset()`.
`mr_realloc()` first allocates a new block, then copies `min(old_size, size)` bytes, and finally frees the old block.

### Heap Region and Statistics

```c
void mr_heap_region_add(const mr_heap_region_t *regions);
mr_heap_info_t mr_heap_information(mr_bool_t lazy);
```

`mr_heap_region_add()` can add additional memory regions to the heap. Each region must have a valid address and positive
size, and the last element's `address` must be `MR_NULL`. `mr_heap_information(MR_TRUE)` only copies the current
statistics; `mr_heap_information(MR_FALSE)` traverses the free list to update time-consuming statistics fields.

## Initialization

In the built-in implementation, `malloc.c` defines an internal function:

```c
void mr_libc_malloc_init(void);
```

This function creates the default static heap `__heap` with size determined by `MR_CFG_HEAP_SIZE`, defaulting to 4 KiB,
and must be no less than 32 bytes. In the source code, `kernel/initcall.c` calls this initialization function via
`extern`.

## Usage Example

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

Example of adding an additional region:

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

## Notes

- When `MR_USE_MALLOC_STD` is enabled, `mr_malloc()`, `mr_free()`, `mr_calloc()`, and `mr_realloc()` map to the standard
  library; `mr_malloc_usable_size()`, `mr_heap_region_add()`, and `mr_heap_information()` are only declared in the
  non-standard implementation.
- Built-in `mr_malloc(0)` returns `MR_NULL`, `mr_free(MR_NULL)` returns directly.
- Built-in `mr_realloc(mem, 0)` calls `mr_malloc(0)` which returns `MR_NULL`, then still calls `mr_memcpy(new, mem, 0)`
  and `mr_free(mem)`; when string safety checks are not enabled, a null destination pointer in `mr_memcpy()` requires
  caution.
- Heap operations protect the internal linked list by disabling interrupts, and do not provide cross-core
  synchronization guarantees in the C11 sense.
- `mr_heap_region_add()` does not check for a null `regions` pointer; the caller must pass a valid array terminated with
  a null address.
- Tests cover zero-size allocation, freeing null pointers, usable size, calloc zeroing, realloc data preservation,
  fragment reuse, and additional region addition.
