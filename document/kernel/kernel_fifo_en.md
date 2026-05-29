[**中文**](kernel_fifo.md) | [**English**](kernel_fifo_en.md)

---

# FIFO Ring Buffer

The `mr_fifo` module provides a fixed-capacity ring FIFO buffer. The buffer reads and writes in units of elements,
internally using atomic read and write indexes `ridx` and `widx` to track read/write positions, and wraps around via a
capacity mask.

FIFO supports caller-provided external buffers as well as dynamic buffer allocation via `mr_fifo_alloc()`. Normal writes
only write the available portion when space is insufficient; overwrite writes discard the oldest data to retain the
newest data.

## Core Data Structures and Macros

### FIFO Control Block

```c
typedef struct mr_fifo {
    mr_atomic_t ridx, widx;
    mr_uint32_t elemsz;
    mr_uint32_t mask;
    mr_uint8_t *buf;
} mr_fifo_t;
```

Field descriptions:

| Field    | Description                                                                        |
|----------|------------------------------------------------------------------------------------|
| `ridx`   | Read index, incremented when popping, reading, or skipping data.                   |
| `widx`   | Write index, incremented when pushing, writing, or finishing a write.              |
| `elemsz` | Size of a single element, in bytes.                                                |
| `mask`   | Capacity mask; actual capacity is `mask + 1`, capacity is aligned to a power of 2. |
| `buf`    | Data buffer pointer. `MR_NULL` indicates uninitialized or freed.                   |

## Module Interface

### Initialize with External Buffer

```c
mr_uint32_t mr_fifo_init(mr_fifo_t *fifo, void *buf, mr_uint32_t size,
                         mr_uint32_t elemsz);
```

### Dynamic Allocation and Free

```c
mr_uint32_t mr_fifo_alloc(mr_fifo_t *fifo, mr_uint32_t num,
                          mr_uint32_t elemsz);
void mr_fifo_free(mr_fifo_t *fifo);
```

### Single Element Read/Write

```c
mr_uint32_t mr_fifo_push(mr_fifo_t *fifo, const void *data);
mr_uint32_t mr_fifo_pop(mr_fifo_t *fifo, void *data);
```

### Bulk Read/Write

```c
mr_uint32_t mr_fifo_write(mr_fifo_t *fifo, const void *buf, mr_uint32_t num);
mr_uint32_t mr_fifo_read(mr_fifo_t *fifo, void *buf, mr_uint32_t num);
mr_uint32_t mr_fifo_peek(mr_fifo_t *fifo, void *buf, mr_uint32_t num);
```

### Overwrite Write

```c
mr_uint32_t mr_fifo_overwrite(mr_fifo_t *fifo, const void *buf, mr_uint32_t num);
```

### Status Query

```c
MR_INLINE mr_uint32_t mr_fifo_len(mr_fifo_t *fifo);
MR_INLINE mr_uint32_t mr_fifo_avail(mr_fifo_t *fifo);
MR_INLINE mr_uint32_t mr_fifo_size(mr_fifo_t *fifo);
MR_INLINE mr_uint32_t mr_fifo_skip(mr_fifo_t *fifo, mr_uint32_t num);
MR_INLINE void mr_fifo_reset(mr_fifo_t *fifo);
MR_INLINE mr_bool_t mr_fifo_is_empty(mr_fifo_t *fifo);
MR_INLINE mr_bool_t mr_fifo_is_full(mr_fifo_t *fifo);
```

### Zero-Copy Claim and Finish

```c
mr_uint32_t mr_fifo_write_claim(mr_fifo_t *fifo, mr_uint32_t off, void **buf, mr_uint32_t want);
MR_INLINE mr_uint32_t mr_fifo_write_finish(mr_fifo_t *fifo, mr_uint32_t num);
mr_uint32_t mr_fifo_overwrite_claim(mr_fifo_t *fifo, mr_uint32_t off, void **buf, mr_uint32_t want);
MR_INLINE mr_uint32_t mr_fifo_overwrite_finish(mr_fifo_t *fifo, mr_uint32_t num);
mr_uint32_t mr_fifo_read_claim(mr_fifo_t *fifo, mr_uint32_t off, void **buf, mr_uint32_t want);
MR_INLINE void mr_fifo_read_finish(mr_fifo_t *fifo, mr_uint32_t num);
```

## Usage Example

### External Buffer Read/Write

```c
#include <mrX/kernel/mr_fifo.h>

typedef struct sample_elem {
    int id;
} sample_elem_t;

void sample_fifo_usage(void)
{
    mr_uint8_t buf[128];
    mr_fifo_t fifo;
    sample_elem_t in = {.id = 42};
    sample_elem_t out;

    if (mr_fifo_init(&fifo, buf, sizeof(buf), sizeof(sample_elem_t)) == 0) {
        return;
    }

    mr_fifo_push(&fifo, &in);
    mr_fifo_pop(&fifo, &out);
}
```

## Notes

- `mr_fifo_init()` does not clean the `fifo` content when parameters are invalid.
- The buffer allocated by `mr_fifo_alloc()` must be freed via `mr_fifo_free()`.
- Bulk read/write truncates based on available readable or writable space; the return value is the actual number of
  elements processed.
- `peek` does not advance the read index; `read` does.
- `claim` returns a contiguous segment; it does not guarantee covering the entire request across the wrap-around in one
  call; handle cases where the return value is less than the requested amount.
- The module uses atomic index operations but does not provide a full multi-producer, multi-consumer synchronization
  protocol; complex concurrent scenarios require the caller to additionally constrain the access model or add locking.
