[**中文**](fs_dentry.md) | [**English**](fs_dentry_en.md)

---

# Directory Entry

The dentry module maintains the directory-entry tree of the path namespace. Each `mr_dentry_t` is one path component: a name, a parent, a child list, and an optional bound inode. Path lookup walks this cache first. On a miss it asks the file system for an inode and instantiates a new dentry.

Dentries use an embedded `mr_object_t` for references. Applications usually reach them only through the [file interface](file_en.md). File-system implementations and VFS internals use this module directly.

## Core Data Structures

### Dentry Control Block

```c
typedef struct mr_dentry {
    mr_object_t parent;
    char iname[MR_CFG_FS_DENTRY_INLINE_NAME_LEN];
    mr_dentry_name_t name;
    struct mr_dentry *mounted;
    struct mr_dentry *upper;
    mr_list_t sibling, lower;
    const mr_dentry_ops_t *ops;
    struct mr_inode *inode;
    mr_uint8_t state;
} mr_dentry_t;
```

| Field | Meaning |
|-------|---------|
| `parent` | Object control block. The initial reference count is 1. |
| `iname` | Inline buffer for short names. Names shorter than `MR_CFG_FS_DENTRY_INLINE_NAME_LEN` are stored here; longer names go on the heap. This is not the maximum file-name length. |
| `name` | Name view: `str`, `hash`, and `len` (excluding `\0`). `str` may point at `iname` or a heap buffer. |
| `mounted` | Root of a file system mounted over this dentry. Lookup enters it. |
| `upper` | Parent directory dentry. The global ROOT has `upper == MR_NULL`. |
| `sibling` | Sibling list node hung from the parent's `lower`. An empty list means the dentry is not in the tree. |
| `lower` | Head of the child dentry list. |
| `ops` | Hash/compare used for names under this directory. `MR_NULL` selects the default djb2-xor hash. |
| `inode` | Bound inode. The dentry holds its own inode reference. |
| `state` | Internal state: ACTIVE, MOVING, UNLINKING, DEAD. |

`MR_DENTRY_IS_ADDED(dentry)` uses whether `sibling` is empty to tell if the node is attached to a parent.

### Name

```c
typedef struct mr_dentry_name {
    const char *str;
    mr_uint32_t hash;
    mr_uint32_t len;
} mr_dentry_name_t;

#define MR_DENTRY_HASH_SEED  (5381u)
#define MR_DENTRY_NAME(d)    ((d)->name.str)
#define MR_DENTRY_NAME_LEN(d) ((d)->name.len)
```

The default hash is djb2-xor with seed `MR_DENTRY_HASH_SEED`. The default compare is length plus byte equality. A parent directory may supply a custom `mr_dentry_ops_t`. Lookup hashes with the **parent** ops so it matches existing children.

A name must be a single component and must not contain `/` (except the global ROOT name `"/"`). Empty names, `"."`, and `".."` cannot be created as dentry names.

### Dentry Operations

```c
typedef struct mr_dentry_ops {
    mr_uint32_t (*hash)(const char *name, mr_uint32_t len);
    mr_bool_t (*compare)(const char *a, const char *b, mr_uint32_t len);
} mr_dentry_ops_t;
```

Either function may be `MR_NULL` to use the default. A file system that needs case-insensitive names or a custom encoding provides `dops` on the super-block; attach copies it onto the mounted root dentry.

## Global ROOT

```c
#define MR_DENTRY_ROOT_INIT(_dentry)
```

The global ROOT is a static dentry named `"/"`, with `name.len == 0` and no `upper`. `mr_fs_nsp_root()` returns it. `MR_DENTRY_ROOT_INIT()` is only for this ROOT; do not use it for ordinary dentries.

## Lifecycle

1. After `mr_dentry_init()` / `mr_dentry_create()`, the reference count is 1 and the dentry is not in the tree.
2. `mr_dentry_add()` hangs it under the parent's `lower`. An existing name returns `-MR_EEXIST`.
3. `mr_dentry_instantiate()` binds an inode. The dentry takes its own inode reference; the caller's reference is unchanged.
4. `mr_dentry_get()` / `mr_dentry_put()` adjust the reference count.
5. `mr_dentry_unlink()` sets UNLINKING, calls the parent inode `unlink` if present, removes the node from the tree, sets DEAD, and `mr_object_del()`s the creation reference.
6. On last put, release unlinks from the parent list if needed, `mr_inode_put()`s the bound inode, and frees a heap name. A dynamic dentry also `mr_free()`s itself.

Internal states separate “notify the file system” from “commit to the tree”:

| State | Meaning |
|-------|---------|
| ACTIVE | Normal node. Lookup, rename, and unlink are allowed. |
| MOVING | Destination name is reserved, rename is not committed. |
| UNLINKING | File-system unlink has been or is about to be called, not committed. |
| DEAD | Removed from the tree, waiting for the last reference. |

`add` / `unlink` / `instantiate` on MOVING, UNLINKING, or DEAD nodes fail.

## Module Interfaces

### Create and Delete

```c
mr_err_t mr_dentry_init(mr_dentry_t *dentry, const mr_dentry_ops_t *ops,
                        const char *fmt, ...);
mr_dentry_t *mr_dentry_create(const mr_dentry_ops_t *ops, const char *fmt, ...);
mr_err_t mr_dentry_del(mr_dentry_t *dentry);
```

The name is formatted with `fmt`, using the same rules as `mr_snprintf()`. `mr_dentry_init()` initializes a caller-provided static object. `mr_dentry_create()` allocates a dynamic object from the heap.

`mr_dentry_del()` drops the creation reference. MOVING or UNLINKING returns `-MR_EBUSY`. A second delete on an already DEAD object still `put`s it.

### Attach and Detach

```c
mr_err_t mr_dentry_add(mr_dentry_t *dentry, mr_dentry_t *upper);
mr_err_t mr_dentry_remove(mr_dentry_t *dentry);
```

`mr_dentry_add()` only updates the namespace tree; it does not call the file system. Already added returns `-MR_EBUSY`. A non-ACTIVE parent or self returns `-MR_ENOENT`.

`mr_dentry_remove()` unhooks from the parent without calling file-system `unlink` and without marking DEAD. It is for namespace-only adjustments of nodes that are not yet bound. DEAD or no parent returns `-MR_ENOENT`.

### Move and Rename

```c
mr_err_t mr_dentry_move(mr_dentry_t *dentry, mr_dentry_t *upper,
                        const char *name);
mr_err_t mr_dentry_rename(mr_dentry_t *dentry, mr_dentry_t *upper,
                          const char *name);
```

Both require the dentry to be in the tree and `name` to be a single component without `/`. Moving a node into its own subtree returns `-MR_EINVAL`. A name already used under the new parent returns `-MR_EEXIST`.

The implementation first hangs a reservation dentry under the destination to occupy the new name, sets the source to MOVING, then commits or cancels:

- `mr_dentry_move()` only updates the tree.
- `mr_dentry_rename()` calls the old parent inode `rename` before commit. On callback failure the reservation is cancelled and the tree is restored.

### Unlink a File-System Node

```c
mr_err_t mr_dentry_unlink(mr_dentry_t *dentry);
```

The node must be ACTIVE, in the tree, and bound to an inode. A directory with a non-empty `lower` returns `-MR_ENOTEMPTY`. If the parent inode provides `unlink`, that callback runs first; failure restores ACTIVE. On success `inode->nlink` is decremented, the node is removed from the tree, the state becomes DEAD, and the creation reference is dropped.

The path-layer `mr_fs_unlink()` / `mr_fs_rmdir()` also reject mount-points and ROOT before this.

### Children and Emptiness

```c
mr_dentry_t *mr_dentry_child_get(mr_dentry_t *parent, mr_size_t index);
mr_bool_t mr_dentry_is_empty(mr_dentry_t *dentry);
```

`mr_dentry_child_get()` returns the 0-based child. The returned dentry holds one reference; call `mr_dentry_put()` when done. Out of range returns `MR_NULL`. The default `readdir` uses this.

`mr_dentry_is_empty()` inspects `lower`. It only reflects the cache, not uninstantiated on-disk entries.

### Lookup

```c
mr_err_t mr_dentry_lookup(mr_dentry_t *start, mr_dentry_t *limit,
                          const char *path, mr_dentry_t **result);
```

`path` is relative to `start`. `limit` is the dentry `".."` cannot escape, usually the namespace ROOT or an `MR_AT_IN_ROOT` directory handle. On success `*result` holds one reference.

Lookup rules:

1. Start at `start`. If `mounted` is set, enter that root first.
2. Skip extra `/`. `"."` stays. `".."` walks to `upper`, or stays if already at `limit` or there is no `upper`.
3. Other components match children by hash.
4. A miss calls the parent inode `lookup`, instantiates the inode as a child, then continues.
5. After each component, follow `mounted` again.

The `lookup` callback runs without the namespace lock. The file system must return a new inode reference; VFS binds it to the dentry.

### Bind an Inode

```c
mr_err_t mr_dentry_instantiate(mr_dentry_t *dentry, mr_inode_t *inode);
mr_err_t mr_dentry_uninstantiate(mr_dentry_t *dentry);
```

`instantiate` requires an ACTIVE dentry with no inode, otherwise `-MR_EBUSY` / `-MR_ENOENT`. The dentry calls `mr_inode_get()` once more. `uninstantiate` drops that reference and clears `inode`; other references are unchanged.

### References

```c
mr_dentry_t *mr_dentry_get(mr_dentry_t *dentry);
mr_bool_t mr_dentry_put(mr_dentry_t *dentry);
```

These wrap `mr_object_get()` / `mr_object_put()`. Every dentry from lookup, `child_get`, or open must be matched with `put`.

## Usage Example

This sample creates a child, attaches it, and binds an inode. Real file systems usually go through `mr_fs_mkdir()` / `open` instead.

```c
#include <mrX/fs/mr_dentry.h>
#include <mrX/fs/mr_inode.h>

void sample_dentry_bind(mr_dentry_t *parent, mr_inode_t *inode)
{
    mr_dentry_t *child;

    child = mr_dentry_create(parent->ops, "%s", "leaf");
    if (!child) {
        return;
    }

    if ((mr_dentry_add(child, parent) != 0)
        || (mr_dentry_instantiate(child, inode) != 0)) {
        mr_dentry_del(child);
        return;
    }

    mr_dentry_put(child);
}
```

The pointer from `mr_dentry_create()` already holds the creation reference. The final `put` only drops one reference while the node remains in the tree; the parent's `lower` keeps it alive. To remove the name, use `mr_dentry_unlink()` or the path-layer `unlink`/`rmdir`.

## Notes

- Do not call inode callbacks or block while holding the namespace lock.
- Custom hash and compare must be paired, and every child under a parent must use the same ops.
- `mr_dentry_move()` does not rename the backing node; cross-file-system rename is rejected at the path layer (`-MR_EXDEV`).
- Mount overlay uses `mounted`. The covered dentry stays in the parent's `lower`. After unmount the name points at the original directory again.
- `MR_CFG_FS_DENTRY_INLINE_NAME_LEN` only controls short-name inlining, not the maximum file-name length.
