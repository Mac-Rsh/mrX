[**中文**](kernel_finder.md) | [**English**](kernel_finder_en.md)

---

# Finder Lookup Tree

The `mr_finder` module provides a name-based hierarchical lookup tree when `MR_USE_FINDER` is enabled. Each finder is a
kernel object with a name, parent node pointer, sibling list node, and child list head. The module supports lookup by
relative path, automatic creation of intermediate nodes by path, and attaching existing nodes to the end of a path.

Finder uses a global spin lock to protect tree structure modifications and path lookups, and uses `mr_object` to manage
reference counting and static/dynamic object release paths.

## Core Data Structures and Macros

### Name Descriptor

```c
typedef struct mr_finder_name {
    const char *str;
    mr_uint32_t hash;
    mr_uint32_t len;
} mr_finder_name_t;
```

`str` points to the name string, `hash` stores the hash value of the name, and `len` stores the length of the name
excluding the trailing `\0`. Lookup first compares the hash and length, then compares the string content to handle hash
collisions.

### Finder Control Block

```c
MR_CLAZZES_EXPORT(finder);
typedef struct mr_finder {
    mr_object_t parent;
#if !defined(MR_CFG_FINDER_INAME_LEN)
#define MR_CFG_FINDER_INAME_LEN        (4)
#endif
    char iname[MR_CFG_FINDER_INAME_LEN];
    mr_finder_name_t name;
    struct mr_finder *upper;
    mr_list_t sibling, lower;
} mr_finder_t;
```

Field descriptions:

| Field     | Description                                                                                                              |
|-----------|--------------------------------------------------------------------------------------------------------------------------|
| `parent`  | Embedded object control block for reference counting and release callback.                                               |
| `iname`   | Inline short name storage area, default length `4` bytes, configurable via `MR_CFG_FINDER_INAME_LEN`.                    |
| `name`    | Name string, hash value, and length. Points to `iname` for short names or dynamically allocated memory for longer names. |
| `upper`   | Parent finder pointer, `MR_NULL` when not added to a tree.                                                               |
| `sibling` | Sibling node linked into the parent's `lower` list.                                                                      |
| `lower`   | Child finder list head.                                                                                                  |

### Name and Status Macros

```c
#define MR_FINDER_NAME(_finder)         ((_finder)->name.str)
#define MR_FINDER_NAME_LEN(_finder)     ((_finder)->name.len)
#define MR_FINDER_IS_ADDED(_finder)     (!mr_list_is_empty(&(_finder)->sibling))
```

## Module Interface

### Initialize and Create

```c
mr_err_t mr_finder_init(mr_finder_t *finder, const char *fmt, ...);
mr_finder_t *mr_finder_create(const char *fmt, ...);
mr_err_t mr_finder_vinit(mr_finder_t *finder, const char *fmt, mr_va_list args);
mr_finder_t *mr_finder_vcreate(const char *fmt, mr_va_list args);
```

### Delete and Reference

```c
mr_err_t mr_finder_del(mr_finder_t *finder);
MR_INLINE mr_finder_t *mr_finder_get(mr_finder_t *finder);
MR_INLINE mr_bool_t mr_finder_put(mr_finder_t *finder);
```

### Add and Remove

```c
mr_err_t mr_finder_add(mr_finder_t *finder, mr_finder_t *upper);
mr_err_t mr_finder_remove(mr_finder_t *finder);
```

### Path Lookup

```c
mr_finder_t *mr_finder_lookup(mr_finder_t *start, mr_finder_t *limit,
                              const char *fmt, ...);
mr_finder_t *mr_finder_vlookup(mr_finder_t *start, mr_finder_t *limit,
                               const char *fmt, mr_va_list args);
```

### Create Path and Add Node

```c
mr_finder_t *mr_finder_mkdir(mr_finder_t *start, mr_finder_t *limit,
                             const char *fmt, ...);
mr_err_t mr_finder_mkdir_add(mr_finder_t *start, mr_finder_t *limit,
                             mr_finder_t *finder, const char *fmt, ...);
```

## Usage Example

```c
#include <mrX/kernel/mr_finder.h>

void sample_finder_usage(void)
{
    mr_finder_t root;
    mr_finder_t child;
    mr_finder_t *found;

    if (mr_finder_init(&root, "root") != 0) {
        return;
    }
    if (mr_finder_init(&child, "child") != 0) {
        mr_finder_del(&root);
        return;
    }

    if (mr_finder_add(&child, &root) == 0) {
        found = mr_finder_lookup(&root, MR_NULL, "child");
        (void)found;
        mr_finder_remove(&child);
    }

    mr_finder_del(&child);
    mr_finder_del(&root);
}
```

### Create Path and Add Leaf Node

```c
void sample_finder_mkdir_add(void)
{
    mr_finder_t root;
    mr_finder_t file;

    if (mr_finder_init(&root, "root") != 0) {
        return;
    }
    if (mr_finder_init(&file, "myfile") != 0) {
        mr_finder_del(&root);
        return;
    }

    if (mr_finder_mkdir_add(&root, MR_NULL, &file, "dir1/dir2") == 0) {
        (void)mr_finder_lookup(&root, MR_NULL, "dir1/dir2/myfile");
        mr_finder_remove(&file);
    }

    mr_finder_del(&file);
    mr_finder_del(&root);
}
```

## Notes

- This module provides data structures and interface implementations only when `MR_USE_FINDER` is defined.
- The pointer returned by `mr_finder_lookup()` does not automatically increase the reference count; call
  `mr_finder_get()` if it needs to be held across lock boundaries for an extended period.
- After a successful `mr_finder_add()`, a reference to the parent is held; `mr_finder_remove()` or the object release
  path releases this reference.
- The current recursion protection only checks for direct parent-child mutual addition, not a full ancestor chain cycle
  detection.
- Intermediate nodes created by `mr_finder_mkdir()` are dynamic finders and are reclaimed through the object reference
  release path.
- The finder tree structure is protected by an internal global spin lock within the module, but the external lifecycle
  of finder objects and the rules for holding returned pointers still require the caller to follow the reference
  counting convention.
