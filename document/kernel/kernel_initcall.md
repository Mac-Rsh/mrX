# 初始化调用

初始化调用模块用于把初始化函数放入指定链接段，并在用户初始化阶段按链接段排序后的顺序依次执行。模块同时提供内核初始化入口，负责启动平台、内存分配、定时器和中断等基础组件。

`MR_INIT_EXPORT()` 导出的条目依赖 `sections.toml` 中的 `initcall` 段规则保留并排序。链接脚本或构建系统需要提供 `__mr_initcall_start` 和 `__mr_initcall_end` 符号，`mr_user_init()` 通过这两个符号遍历初始化表。

## 核心数据结构和宏

### 初始化级别

```c
#define MR_INIT_LEVEL_IRQ          "1.0"
#define MR_INIT_LEVEL_DRIVER       "1.1"
#define MR_INIT_LEVEL_USER(_level) "8."_level
```

初始化级别是链接段名的一部分。当前预定义级别包括中断级别、驱动级别和用户级别。用户级别通过字符串拼接形成 `8.<level>`。

`kernel/initcall.c` 内部还导出两个边界条目：`start` 使用级别 `"0.end"`，`end` 使用级别 `"9.end"`。它们用于确保用户初始化遍历范围内存在起止条目。

### 初始化函数和条目

```c
typedef void(mr_initcall_entry_t)(void);

typedef struct mr_initcall {
    const char *name;
    mr_initcall_entry_t *entry;
} mr_initcall_t;
```

`name` 保存初始化函数名字符串，`entry` 保存初始化函数指针。初始化函数不带参数，也不返回错误码。

### 导出宏

```c
#define MR_INIT_EXPORT(_entry, _level)                                         \
    MR_USED const mr_initcall_t MR_CAT(__mr_initcall_, _entry)                 \
        MR_SECTION_ALIGN("mr_initcall." _level)                                \
        = {.name = MR_STR(_entry), .entry = (_entry)}
```

`MR_INIT_EXPORT()` 定义一个 `const mr_initcall_t` 对象，变量名为 `__mr_initcall_<entry>`，段名为 `mr_initcall.<level>`，并使用 `MR_USED` 防止被编译器删除。

### 段配置

`sections.toml` 中与初始化调用相关的配置如下：

```toml
[[section]]
name = "initcall"
input = "mr_initcall.*"
output = ".text"
keep = true
sort = true
comment = "Initcall"
```

该配置要求保留所有 `mr_initcall.*` 输入段，并按段名排序后输出到 `.text`。因此初始化级别字符串直接影响执行顺序。

## 主要接口

### 导出初始化函数

```c
MR_INIT_EXPORT(entry, level);
```

导出的 `entry` 必须符合 `void entry(void)` 形式。`level` 是字符串字面量或能拼接成字符串的宏，例如 `MR_INIT_LEVEL_DRIVER` 或 `MR_INIT_LEVEL_USER("0")`。

### 内核初始化

```c
void mr_kernel_init(void);
```

`mr_kernel_init()` 使用内部静态状态 `__initcall_kstatus` 保证只执行一次。根据配置项，它依次调用：

1. `mr_platform_init()`，当 `MR_USE_PLATFORM` 启用时。
2. `mr_libc_malloc_init()`，当 `MR_USE_MALLOC` 启用时。
3. `mr_kernel_timer_init()`，当 `MR_USE_TIMER` 启用时。
4. `mr_kernel_irq_init()`，当 `MR_USE_IRQ` 启用时。

注释中说明这里没有使用原子变量，因为原子实现可能依赖尚未初始化的底层能力。

### 用户初始化

```c
void mr_user_init(void);
```

`mr_user_init()` 先断言 `__mr_initcall_start < __mr_initcall_end`，然后调用 `mr_kernel_init()`。用户初始化状态 `__initcall_ustatus` 确保导出初始化函数只执行一次。

随后函数从 `__mr_initcall_start` 遍历到 `__mr_initcall_end`，对每个 `mr_initcall_t` 调用 `call->entry()`。遍历条件为 `call <= end`，因此结束边界条目本身也会执行。

## 使用示例

```c
#include <mrX/kernel/mr_initcall.h>

static void driver_init(void) {
    /* 初始化驱动资源。 */
}
MR_INIT_EXPORT(driver_init, MR_INIT_LEVEL_DRIVER);

static void app_init(void) {
    /* 初始化用户模块。 */
}
MR_INIT_EXPORT(app_init, MR_INIT_LEVEL_USER("0"));

void boot(void) {
    mr_user_init();
}
```

在该示例中，`mr_user_init()` 会先确保内核基础组件初始化，再按链接段排序顺序执行导出的初始化条目。

## 注意事项

- 初始化函数没有返回值，失败处理需要由函数内部完成。
- 初始化顺序取决于段名字符串排序，不是 C 文件编译顺序。
- `MR_INIT_LEVEL_USER(_level)` 直接拼接字符串，传入参数应能形成合法字符串字面量，例如 `"0"`。
- `mr_kernel_init()` 和 `mr_user_init()` 都只执行一次；重复调用会直接返回或跳过用户条目。
- 构建系统必须根据 `sections.toml` 保留并排序 `mr_initcall.*` 段，同时提供 `__mr_initcall_start` 和 `__mr_initcall_end` 符号。
- 初始化状态使用 `volatile mr_bool_t`，不是通用并发同步原语；该入口应在受控启动流程中调用。
