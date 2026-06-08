[**中文**](drivers_driver.md) | [**English**](drivers_driver_en.md)

---

# 驱动框架

Driver 模块提供 mrX 的通用驱动对象模型。驱动以内嵌 `mr_object_t` 管理生命周期，通过 `mr_driver_clazz_t` 描述初始化和反初始化方法，并通过 `data`、`priv`、`ops` 保存具体驱动的数据、私有上下文和底层操作集。

驱动可以静态定义、动态创建，也可以安装到链接段中随系统启动自动初始化。模块只有在 `MR_USE_DRIVER` 启用时可用。

## 核心数据结构和宏

### 驱动控制块

```c
typedef struct mr_driver {
    mr_object_t parent;
    const mr_driver_clazz_t *clazz;
    mr_atomic_t flags;
    mr_uint16_t type;
    mr_int16_t state;
    mr_ptr_t data;
    mr_ptr_t priv;
    mr_ptr_t ops;
} mr_driver_t;
```

字段含义如下：

| 字段 | 含义 |
|------|------|
| `parent` | 对象控制块，用于引用计数和静态/动态释放。 |
| `clazz` | 驱动初始化和反初始化方法表。 |
| `flags` | 驱动状态标志。 |
| `type` | 驱动主类型。 |
| `state` | 最近一次 setup 或 shutdown 的状态码。 |
| `data` | 驱动数据，通常指向模块专用 data 结构。 |
| `priv` | 平台或设备私有数据。 |
| `ops` | 底层操作表。 |

### 驱动类

```c
typedef mr_err_t(mr_driver_method_t)(const struct mr_driver *drv);

typedef struct mr_driver_clazz {
    const char *name;
    mr_driver_method_t *init;
    mr_driver_method_t *deinit;
} mr_driver_clazz_t;
```

`init` 在 `mr_driver_setup()` 中调用，`deinit` 在 `mr_driver_shutdown()` 或驱动释放时调用。若对应方法为 `MR_NULL`，框架认为该步骤成功。

### 标志、类型和初始化级别

```c
#define MR_DRIVER_FLAG_SETUP  (1U << 0)
#define MR_DRIVER_FLAG_LOCK   (1U << 1)
#define MR_DRIVER_FLAG_DYING  (1U << 2)

#define MR_DRIVER_LEVEL_EARLY   "1"
#define MR_DRIVER_LEVEL_PRIO(p) "2." p
#define MR_DRIVER_LEVEL_NORMAL  "3"
#define MR_DRIVER_LEVEL_LAST    "4"
#define MR_DRIVER_LEVEL_DEFER   "9"
```

`MR_DRIVER_FLAG_SETUP` 表示驱动已完成 setup；`MR_DRIVER_FLAG_LOCK` 用于避免 setup/shutdown 重入；`MR_DRIVER_FLAG_DYING` 表示驱动正在删除，后续 setup/shutdown 会失败。

驱动类型使用 `mr_driver_type_t` 表示，当前包含 RAM、GPIO、UART、I2C、ADC、DAC、DMA 等主类型，主类型掩码为 `MR_DRIVER_TYPE_MASK`。

## 模块接口

### 初始化、创建和删除

```c
mr_err_t mr_driver_init(mr_driver_t *drv, const mr_driver_clazz_t *dclazz,
                        mr_uint16_t type, void *data, void *priv, void *ops);
mr_driver_t *mr_driver_create(const mr_driver_clazz_t *dclazz,
                              mr_uint16_t type, void *data, void *priv,
                              void *ops);
mr_err_t mr_driver_del(mr_driver_t *drv);
```

`mr_driver_init()` 初始化调用者提供的静态驱动对象。`mr_driver_create()` 从堆中分配驱动对象并初始化为动态对象。动态对象在最后一个引用释放时会释放 `mr_driver_t` 本体。

`mr_driver_del()` 会先设置 `MR_DRIVER_FLAG_DYING`，再释放对象引用。若对象释放时驱动仍处于 setup 状态，框架会调用 `deinit`。

### setup、shutdown 和状态查询

```c
mr_err_t mr_driver_setup(mr_driver_t *drv);
mr_err_t mr_driver_shutdown(mr_driver_t *drv);
mr_bool_t mr_driver_is_ready(mr_driver_t *drv);
mr_int16_t mr_driver_state(mr_driver_t *drv);
```

`mr_driver_setup()` 调用驱动类的 `init` 方法。该接口是幂等的，已 setup 的驱动再次调用会直接返回 0。`mr_driver_shutdown()` 调用 `deinit` 方法，未 setup 的驱动直接返回 0。

`mr_driver_is_ready()` 只有在驱动对象有效、已经 setup 且 `state == 0` 时返回 `MR_TRUE`。

### 查找和引用管理

```c
mr_driver_t *mr_driver_find(const char *name);
mr_driver_t *mr_driver_get(mr_driver_t *drv);
mr_bool_t mr_driver_put(mr_driver_t *drv);
```

`mr_driver_find()` 只查找通过 `MR_DRIVER_INSTALL()` 导出到 driver 链接段中的驱动。`mr_driver_get()` 和 `mr_driver_put()` 是 object 引用计数接口的驱动封装。

### 访问宏

```c
MR_DRIVER_TYPE(drv)
MR_DRIVER_IS_TYPE(drv, type)
MR_DRIVER_DATA(drv)
MR_DRIVER_PRIV(drv)
MR_DRIVER_OPS(drv)
MR_DRIVER_IS_SETUP(drv)
MR_DRIVER_ASSERT(drv, major_type)
```

`MR_DRIVER_ASSERT()` 会检查驱动非空、object 已初始化、属于 driver 类、已经 setup，并且主类型匹配。因此具体驱动 API 通常要求调用前驱动已经 setup。

## 驱动安装和启动流程

```c
#define MR_DRIVER_INSTALL(_name, _dclazz, _type, _data, _priv, _ops, _level)
```

`MR_DRIVER_INSTALL()` 定义驱动对象和驱动入口，并将入口放入 `mr_driver.<level>` 链接段。系统启动时 `mr_drivers_init()` 会遍历 `start` 到 `defer` 之间的入口并调用 `mr_driver_setup()`。

`MR_DRIVER_LEVEL_DEFER` 级别的驱动位于延迟初始化区域，默认不会被启动阶段自动 setup，需要调用方按需 setup。

## HardTree 支持

`mr_hardtree.h` 提供基于 `mr_hardgen.h` 的编译期硬件树访问宏。驱动层常用宏如下：

```c
MR_HT_HAS_DRIVER()
MR_HT_DRIVER_NUM()
MR_HT_DRIVER(id)
MR_HT_DRIVER_FOREACH(fn, ...)
MR_HT_DRIVER_INSTALL(id, dclazz, type, data, priv, ops)
MR_HT_DRIVER_FIND(node)
```

`MR_HT_DRIVER_INSTALL()` 会读取硬件树节点的 `INIT_LEVEL` 属性；若属性不存在，默认使用 `MR_DRIVER_LEVEL_DEFER`。

## 使用示例

```c
static mr_err_t sample_driver_init(const mr_driver_t *drv)
{
    (void)drv;
    return 0;
}

static mr_err_t sample_driver_deinit(const mr_driver_t *drv)
{
    (void)drv;
    return 0;
}

static const mr_driver_clazz_t sample_driver_clazz =
    MR_DRIVER_CLAZZ_INIT("sample", sample_driver_init, sample_driver_deinit);

void sample_driver_usage(void)
{
    mr_driver_t *drv;

    drv = mr_driver_create(&sample_driver_clazz, MR_DRIVER_TYPE_RAM,
                           MR_NULL, MR_NULL, MR_NULL);
    mr_driver_setup(drv);

    if (mr_driver_is_ready(drv)) {
        /* Use driver data, private data, or operations here. */
    }

    mr_driver_shutdown(drv);
    mr_driver_del(drv);
}
```

该示例动态创建一个驱动对象，执行 setup，使用完成后 shutdown 并删除对象。

## 注意事项

- `mr_driver_init()` 用于静态对象，调用前对象必须未初始化。
- `mr_driver_create()` 返回动态对象，删除后不能继续访问原指针。
- `mr_driver_setup()` 和 `mr_driver_shutdown()` 会通过内部 lock 避免重入；重入时返回 `-MR_EBUSY`。
- 驱动进入 dying 状态后，setup/shutdown 返回 `-MR_ENOENT`。
- `mr_driver_find()` 依赖链接脚本保留并排序 `mr_driver.*` 段。
- HardTree 驱动若未显式配置初始化级别，默认不会在启动阶段自动 setup。
