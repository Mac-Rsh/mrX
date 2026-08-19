[**中文**](fs_nsp.md) | [**English**](fs_nsp_en.md)

---

# 命名空间

Namespace 保存「当前从哪开始解析路径」：ROOT 和 cwd 两个 dentry。全局还有一个静态的 ROOT dentry（名字 `"/"`），所有命名空间最初都指向它。

单用户模式下 FS 自己提供一份全局 `mr_fs_nsp_t`，并在 initcall 里 `mr_fs_nsp_init()`。多用户模式下这份接口改由平台适配层实现，不向应用暴露。

头文件是 `<mrX/fs/mr_nsp.h>`，当前不通过 `<mrX.h>` 聚合。

## 结构

```c
typedef struct mr_fs_nsp {
    struct mr_dentry *root;
    struct mr_dentry *cwd;
} mr_fs_nsp_t;
```

`root` 和 `cwd` 都持有 dentry 引用。`mr_fs_chroot()` / `mr_fs_chdir()` 会替换它们并 `put` 旧引用。

## 接口

```c
struct mr_dentry *mr_fs_nsp_root(void);
mr_err_t mr_fs_nsp_init(mr_fs_nsp_t *nsp);
mr_fs_nsp_t *mr_fs_nsp_current(void);
mr_bool_t mr_fs_nsp_is_root(const struct mr_dentry *dentry);
int mr_fs_nsp_lock(void);
void mr_fs_nsp_unlock(int mask);
```

`mr_fs_nsp_root()` 返回全局静态 ROOT，不增加引用。它在任何命名空间初始化之前就存在，也是 `mr_fs_mount_root()` 的挂载目标。

`mr_fs_nsp_init()` 把 `nsp->root` 和 `nsp->cwd` 都 `get` 成全局 ROOT。失败时两个字段都为空。

`mr_fs_nsp_current()`：

- 未开 `MR_USE_FS_MULTIUSER`：返回 FS 内部的那一份全局命名空间。
- 开启后：必须由平台提供，返回**当前任务**拥有的命名空间。返回 `MR_NULL` 时路径 API 得到 `-MR_EINVAL`。

`mr_fs_nsp_is_root()` 只判断是不是全局 ROOT 对象，不是「当前命名空间的 root」。chroot 之后当前 root 可能是别的 dentry。

## 锁

`mr_fs_nsp_lock()` 关中断并拿到全局命名空间自旋锁，返回值必须交给配对的 `mr_fs_nsp_unlock()`。持锁区间内禁止：

- 阻塞
- 调用文件系统 `lookup` / `create` / `unlink` 等实现回调
- 再调会操作同一把锁的路径 API

Dentry 树、挂载拓扑、文件系统注册表、super-block 链表都靠这把锁。路径层的模式是：锁内走缓存，锁外调文件系统，回来再锁内提交。

## 多用户适配

开启 `MR_USE_FS_MULTIUSER` 后，平台需要：

1. 为每个任务保存一份 `mr_fs_nsp_t`。
2. 实现 `mr_fs_nsp_current()`，返回当前任务的那份。
3. 在任务可用文件系统之前调用 `mr_fs_nsp_init()`。
4. 任务退出时 `put` 掉 `root` 和 `cwd`（若仍持有）。

`mr_fs_file_init()` 在多用户模式下不会初始化任何任务命名空间。全局 ROOT dentry 仍然由 FS 提供，`mr_fs_mount_root()` 仍然不读当前命名空间。

## 和 chroot / cwd 的关系

`mr_fs_chdir()` 把 `nsp->cwd` 换成查找得到的目录 dentry。`mr_fs_chroot()` 换成新的 root；若旧 cwd 不在新 root 子树内，cwd 也改成新 root，避免相对路径逃出。已经打开的目录句柄不跟着变。

`mr_fs_getcwd()` 从 `cwd` 走到 `nsp->root`（不是全局 ROOT），拼出当前命名空间内的绝对路径。

## 注意事项

- 应用代码使用 `mr_fs_chdir()` / `chroot()` / `getcwd()`，不要直接改 `nsp` 字段。
- 锁是全局一把，不是每个 `mr_fs_nsp_t` 一把。
- 全局 ROOT 的创建引用来自静态对象初始化，不要对 `mr_fs_nsp_root()` 的返回值 `put`，除非你自己 `get` 过。
